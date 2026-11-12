//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file app_update_native.cpp
 * @brief 固定GitHub feed、private streaming取得とpinned Authenticode更新を実装します
 * @version 1.0.0
 * @date 2026/11/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 1.0.0 Shunji Ogawa 2026/11/10 新規作成
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1378のnative取得/署名/正常終了待機を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "platform/app_update_native.h"
#include "platform/win_helpers.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bcrypt.h>
#include <chrono>
#include <cstring>
#include <functional>
#include <objbase.h>
#include <sddl.h>
#include <shlobj.h>
#include <softpub.h>
#include <span>
#include <utility>
#include <vector>
#include <wincrypt.h>
#include <windows.h>
#include <winhttp.h>
#include <wintrust.h>
namespace koefude {
namespace {
constexpr std::wstring_view K_INSTALLER_DESCRIPTION =
	L"KoeFude windows-x64-cuda-vulkan Setup" ; // 署名対象のarchitecture/backend marker
constexpr std::string_view			   K_OWNER_MARKER = "KoeFude.Update.Staging/1\n" ; // 自作owner識別
constexpr std::array<std::uint8_t, 32> K_PUBLISHER_CERTIFICATE{
	0x6f, 0xb2, 0x81, 0xbf, 0xef, 0x63, 0xe3, 0x84, 0x4b, 0x6d, 0xd2, 0x1e, 0x99, 0xc8, 0x5e, 0x7e, 0xba,
	0x04, 0x9e, 0xc0, 0x4a, 0x1b, 0x9f, 0xc1, 0xde, 0xb6, 0xca, 0x61, 0x3d, 0x38, 0xdc, 0xae} ; // 既存EV leaf DER
//------------------------------------------------------------------------------
/**
 * @brief 停止を例外へ正規化します
 * @param[in] stop 当opのtoken
 */
//------------------------------------------------------------------------------
void cancelled( std::stop_token stop)
{
	// 取消し後に追加I/O/launchを進めません
	if( stop.stop_requested()) {
		throw UpdateFailure{UpdateError::CANCELLED} ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 元Win32失敗を固定分類へ変換します
 * @param[in] ok 成功条件
 * @param[in] error 固定分類
 */
//------------------------------------------------------------------------------
void require( BOOL ok, UpdateError error = UpdateError::IO)
{
	// 失敗直後のcodeを保持し、自由文を生成しません
	if( !ok) {
		throw UpdateFailure{error, GetLastError()} ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief CNG成功だけを採用します
 * @param[in] status CNG戻り値
 */
//------------------------------------------------------------------------------
void crypto( NTSTATUS status)
{
	// signed値が負のstatusは検証失敗です
	if( status < 0) {
		throw UpdateFailure{UpdateError::HASH, static_cast<std::uint32_t>( status)} ;
	}
}
/** @brief providerの解放責任です */
struct AlgorithmDelete {
	//------------------------------------------------------------------------------
	/**
	 * @brief 非NULL providerを閉じます
	 * @param[in] value CNG handle
	 */
	//------------------------------------------------------------------------------
	void operator()( void *value) const noexcept
	{
		BCryptCloseAlgorithmProvider( value, 0) ;
	}
} ;
/** @brief hash stateの解放責任です */
struct HashDelete {
	//------------------------------------------------------------------------------
	/**
	 * @brief 非NULL hashを閉じます
	 * @param[in] value CNG handle
	 */
	//------------------------------------------------------------------------------
	void operator()( void *value) const noexcept
	{
		BCryptDestroyHash( value) ;
	}
} ;
//------------------------------------------------------------------------------
/**
 * @brief 同じlocked fileの正確なSHAを有界RAMで計算します
 * @param[in] file 書込共有なしのborrow
 * @param[in] asset 正確なsize/SHA
 * @param[in] stop 当op取消し
 */
//------------------------------------------------------------------------------
void verifyHash( HANDLE file, const UpdateAsset &asset, std::stop_token stop)
{
	LARGE_INTEGER length{} ; // 開いたfileの長さ
	require( GetFileSizeEx( file, &length)) ;
	// 期待sizeは最大4GiB、元feedを迂回した呼出しも拒否します
	if( !asset.size || asset.size > K_UPDATE_INSTALLER_LIMIT || length.QuadPart < 0 ||
	   static_cast<std::uint64_t>( length.QuadPart) != asset.size) {
		throw UpdateFailure{UpdateError::SIZE} ;
	}
	require( SetFilePointerEx( file, {}, nullptr, FILE_BEGIN)) ;
	BCRYPT_ALG_HANDLE rawAlgorithm = nullptr ; // provider受取先
	crypto( BCryptOpenAlgorithmProvider( &rawAlgorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) ;
	const std::unique_ptr<void, AlgorithmDelete> algorithm( rawAlgorithm) ; // provider所有
	BCRYPT_HASH_HANDLE							 rawHash = nullptr ; // hash受取先
	crypto( BCryptCreateHash( algorithm.get(), &rawHash, nullptr, 0, nullptr, 0, 0)) ;
	const std::unique_ptr<void, HashDelete> hash( rawHash) ; // hash所有
	std::array<std::uint8_t, 65536>			buffer{} ; // 全installerをRAMへ置きません
	std::uint64_t							total = 0 ; // 実読取byte
	// EOF/取消しまで64KiBずつhashし、期待size超過で終了します
	for(;;) {
		cancelled( stop) ;
		DWORD count = 0 ; // 今回実byte数
		require( ReadFile( file, buffer.data(), static_cast<DWORD>( buffer.size()), &count, nullptr)) ;
		// EOFだけでhashを確定します
		if( !count) {
			break ;
		}
		total += count ;
		// 拡張されたfileを無制限に読みません
		if( total > asset.size) {
			throw UpdateFailure{UpdateError::SIZE} ;
		}
		crypto( BCryptHashData( hash.get(), buffer.data(), count, 0)) ;
	}
	std::array<std::uint8_t, 32> digest{} ; // 実SHA256
	crypto( BCryptFinishHash( hash.get(), digest.data(), static_cast<ULONG>( digest.size()), 0)) ;
	// 正確なbyte数とdigestが一致した時だけ署名へ進めます
	if( total != asset.size || digest != asset.sha256) {
		throw UpdateFailure{total != asset.size ? UpdateError::SIZE : UpdateError::HASH} ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 署名resourceの製品/数値版/architecture markerを照合します
 * @param[in] file locked installer
 * @param[in] path 同file
 * @param[in] asset 期待版
 */
//------------------------------------------------------------------------------
void verifyProduct( HANDLE file, const std::filesystem::path &path, const UpdateAsset &asset)
{
	IMAGE_DOS_HEADER dos{} ; // bounded PE先頭
	DWORD			 count = 0 ; // 実読取byte
	require( SetFilePointerEx( file, {}, nullptr, FILE_BEGIN)) ;
	require( ReadFile( file, &dos, sizeof(dos), &count, nullptr)) ;
	// 正規Inno x86 bootstrapだけを期待し、壊れたoffsetをseekしません
	if( count != sizeof(dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 64 || dos.e_lfanew > 1024 * 1024) {
		throw UpdateFailure{UpdateError::PRODUCT} ;
	}
	LARGE_INTEGER offset{} ; // NT header位置
	offset.QuadPart = dos.e_lfanew ;
	require( SetFilePointerEx( file, offset, nullptr, FILE_BEGIN)) ;
	struct PeHeader {
		DWORD			  signature ; // PE signature
		IMAGE_FILE_HEADER file ; // bootstrap machine
	} pe{} ; // 固定24byte
	require( ReadFile( file, &pe, sizeof(pe), &count, nullptr)) ;
	// x64 payloadの正規x86 bootstrapを製品/署名markerと共に確認します
	if( count != sizeof(pe) || pe.signature != IMAGE_NT_SIGNATURE || pe.file.Machine != IMAGE_FILE_MACHINE_I386) {
		throw UpdateFailure{UpdateError::PRODUCT} ;
	}
	DWORD		unused = 0 ; // Windows APIの未使用出力
	const DWORD bytes = GetFileVersionInfoSizeExW( FILE_VER_GET_NEUTRAL, path.c_str(), &unused) ;
	// version resourceがない/巨大な候補は実行しません
	if( !bytes || bytes > 1024 * 1024) {
		throw UpdateFailure{UpdateError::PRODUCT} ;
	}
	std::vector<std::uint8_t> resource( bytes) ; // 最大1MiB
	require( GetFileVersionInfoExW( FILE_VER_GET_NEUTRAL, path.c_str(), 0, bytes, resource.data()), UpdateError::PRODUCT) ;
	VS_FIXEDFILEINFO *fixed = nullptr ; // resource寿命内のborrow
	UINT			  length = 0 ; // QueryValueのbyte数
	require( VerQueryValueW( resource.data(), L"\\", reinterpret_cast<void **>( &fixed), &length), UpdateError::PRODUCT) ;
	const auto &v = asset.version.parts ; // 数値版
	// 期待版だけを認め、ProductVersionを要求版で補完しません
	if( !fixed || length < sizeof(*fixed) || fixed->dwSignature != VS_FFI_SIGNATURE ||
	   fixed->dwProductVersionMS != static_cast<DWORD>( MAKELONG(v[1], v[0])) ||
	   fixed->dwProductVersionLS != static_cast<DWORD>( MAKELONG(0, v[2])) ||
	   fixed->dwFileVersionMS != fixed->dwProductVersionMS || fixed->dwFileVersionLS != fixed->dwProductVersionLS) {
		throw UpdateFailure{UpdateError::PRODUCT} ;
	}
	struct Translation {
		WORD language ; // resource言語
		WORD codepage ; // resource codepage
	} ; // OS固定layout
	Translation *translations = nullptr ; // resource寿命内borrow
	require( VerQueryValueW( resource.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void **>( &translations),
						   &length),
			UpdateError::PRODUCT) ;
	// 不足/過大/不完全なTranslationを解釈しません
	if( !translations || !length || length % sizeof(Translation) || length > 16 * sizeof(Translation)) {
		throw UpdateFailure{UpdateError::PRODUCT} ;
	}
	const auto canonical = toWide( updateVersionText( asset.version)) ; // strict UTF-16
	bool	   matched = false ; // 一つのtranslationに全fieldを要求
	// 最大16言語の検証済みTranslationだけを照合します
	for( std::size_t at = 0; at < length / sizeof(Translation); ++at) {
		std::array<wchar_t, 64> root{} ; // 固定resource prefix
		swprintf_s( root.data(), root.size(), L"\\StringFileInfo\\%04x%04x\\", translations[at].language,
				   translations[at].codepage) ;
		const auto equals =
			//------------------------------------------------------------------------------
			/**
			 * @brief 当translationの短い文字列を比較します
			 * @param[in] key 固定key
			 * @param[in] expected 固定期待値
			 * @return 正確な一致
			 */
			//------------------------------------------------------------------------------
			[&]( std::wstring_view key, std::wstring_view expected)
			{
				const auto query = std::wstring( root.data()) + std::wstring( key) ; // 固定短query
				wchar_t	  *text = nullptr ; // version resource内のborrow
				UINT	   chars = 0 ; // null込み文字数
				return VerQueryValueW( resource.data(), query.c_str(), reinterpret_cast<void **>( &text), &chars) && text &&
					   chars > 0 && chars <= 256 && text[chars - 1] == L'\0' &&
					   matchesUpdateResource( std::wstring_view( text, chars - 1), expected) ;
			} ;
		matched |= equals( L"ProductName", L"KoeFude") && equals( L"ProductVersion", canonical) &&
				   equals( L"FileDescription", K_INSTALLER_DESCRIPTION) ;
	}
	// 自由文やasset名だけで違う製品を採用しません
	if( !matched) {
		throw UpdateFailure{UpdateError::PRODUCT} ;
	}
}
/** @brief WinHTTP handleの単独所有です */
struct HttpDelete {
	//------------------------------------------------------------------------------
	/**
	 * @brief 非NULL handleを閉じます
	 * @param[in] handle WinHTTP資源
	 */
	//------------------------------------------------------------------------------
	void operator()( void *handle) const noexcept
	{
		WinHttpCloseHandle( handle) ;
	}
} ;
using HttpHandle = std::unique_ptr<void, HttpDelete> ; // request/connection/session所有
/** @brief HANDLE_CLOSINGまで生存するcallback状態です */
struct RequestState {
	/// 段階完了
	UniqueHandle			changed{CreateEventW( nullptr, FALSE, FALSE, nullptr)} ;
	/// callback解放許可
	UniqueHandle			closed{CreateEventW( nullptr, TRUE, FALSE, nullptr)} ;
	/// 最新完了status
	std::atomic<DWORD>		status{0} ;
	/// 元通信code
	std::atomic<DWORD>		error{0} ;
	/// 実read byte
	std::atomic<DWORD>		count{0} ;
	/// async read先
	std::array<char, 65536> buffer{} ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief callbackから値だけを通知します
 * @param[in] handle 非所有request
 * @param[in] context closingまで有効なborrow
 * @param[in] status 完了種類
 * @param[in] information callback中borrow
 * @param[in] length 情報長byte
 */
//------------------------------------------------------------------------------
void CALLBACK updateStatus( HINTERNET handle, DWORD_PTR context, DWORD status, LPVOID information, DWORD length) noexcept
{
	static_cast<void>( handle) ;
	auto *state = reinterpret_cast<RequestState *>( context) ; // WinHTTP借用境界
	// context登録前の通知は利用しません
	if( !state) {
		return ;
	}
	// 最終callbackだけでcontext解放を許可します
	if( status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
		SetEvent( state->closed.get()) ;
		return ;
	}
	// 補助通知を完了と誤認しません
	if( status != WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE && status != WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE &&
	   status != WINHTTP_CALLBACK_STATUS_READ_COMPLETE && status != WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
		return ;
	}
	// 元codeだけを保持しbody/messageを渡しません
	if( status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR && information && length >= sizeof(WINHTTP_ASYNC_RESULT)) {
		state->error = static_cast<WINHTTP_ASYNC_RESULT *>( information)->dwError ;
	}
	state->count = length ;
	state->status.store( status, std::memory_order_release) ;
	SetEvent( state->changed.get()) ;
}
/** @brief read sinkを使うbounded GETのheader結果です */
struct HttpResult {
	/// 実HTTP status
	DWORD		status = 0 ;
	/// 最大8192byte、worker内部だけ
	std::string location ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 固定host/手動allowlist URLへ一回だけGETします
 * @param[in] url 検証済みHTTPS
 * @param[in] limit 応答byte上限
 * @param[in] deadline 全体期限
 * @param[in] stop 取消し
 * @param[in] sink 同期chunk、最大64KiB
 * @param[in] metadata 固定feed選択
 * @return HTTP status/転送先
 * @throws UpdateFailure 通信/byte上限/期限
 */
//------------------------------------------------------------------------------
HttpResult get( std::string_view url, std::uint64_t limit, std::chrono::steady_clock::time_point deadline,
			   std::stop_token stop, const std::function<void( std::span<const char>)> &sink, bool metadata)
{
	cancelled( stop) ;
	const auto	   wide = toWide( url) ; // 正規URLだけ
	URL_COMPONENTS parts{} ; // WinHTTPのborrowed slice
	parts.dwStructSize = sizeof(parts) ;
	parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>( -1) ;
	require( WinHttpCrackUrl( wide.c_str(), static_cast<DWORD>( wide.size()), 0, &parts), UpdateError::METADATA) ;
	const std::wstring host( parts.lpszHostName, parts.dwHostNameLength) ; // 正規host
	const auto		   path = std::wstring( parts.lpszUrlPath, parts.dwUrlPathLength) +
					  std::wstring( parts.lpszExtraInfo, parts.dwExtraInfoLength) ; // query含む
	// external callerでも非HTTPS/port変更/host逸脱を拒否します
	if( parts.nScheme != INTERNET_SCHEME_HTTPS || parts.nPort != 443 ||
	   (host != L"api.github.com" && host != L"github.com" && host != L"release-assets.githubusercontent.com")) {
		throw UpdateFailure{UpdateError::METADATA} ;
	}
	const HttpHandle session( WinHttpOpen( L"KoeFude-Update/1.0.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
										 WINHTTP_NO_PROXY_BYPASS,
										 WINHTTP_FLAG_ASYNC)) ; // credential/proxy探索なし
	require( session != nullptr, UpdateError::NETWORK) ;
	require( WinHttpSetTimeouts( session.get(), 15000, 15000, 30000, 30000), UpdateError::NETWORK) ;
	const HttpHandle connection( WinHttpConnect( session.get(), host.c_str(), 443, 0)) ;
	require( connection != nullptr, UpdateError::NETWORK) ;
	RequestState state ; // requestより長寿命
	require( state.changed && state.closed, UpdateError::NETWORK) ;
	HttpHandle request( WinHttpOpenRequest( connection.get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
										  WINHTTP_DEFAULT_ACCEPT_TYPES,
										  WINHTTP_FLAG_SECURE)) ; // TLS弱化なし
	require( request != nullptr, UpdateError::NETWORK) ;
	DWORD disabled =
		WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION ; // 自動転送/認証なし
	DWORD	  maximumHeader = 65536 ; // headerも有限
	DWORD	  autologon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH ; // OS資格情報なし
	DWORD_PTR context = reinterpret_cast<DWORD_PTR>( &state) ; // closingまで有効
	require( WinHttpSetOption( request.get(), WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) &&
				WinHttpSetOption( request.get(), WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE, &maximumHeader,
								 sizeof(maximumHeader)) &&
				WinHttpSetOption( request.get(), WINHTTP_OPTION_AUTOLOGON_POLICY, &autologon, sizeof(autologon)) &&
				WinHttpSetOption( request.get(), WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)),
			UpdateError::NETWORK) ;
	// callback登録の失敗後はSendRequestしません
	if( WinHttpSetStatusCallback( request.get(), updateStatus,
								WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES,
								0) == WINHTTP_INVALID_STATUS_CALLBACK) {
		throw UpdateFailure{UpdateError::NETWORK, GetLastError()} ;
	}
	/** @brief 全例外経路でclosing通知までcontextを維持します */
	struct Closing {
		HttpHandle	 &request ; // 所有request
		RequestState &state ; // 先に作成したcontext
		//------------------------------------------------------------------------------
		/**
		 * @brief callback解放を確認します
		 */
		//------------------------------------------------------------------------------
		~Closing()
		{
			request.reset() ;
			static_cast<void>( WaitForSingleObject( state.closed.get(), INFINITE)) ;
		}
	} closing{request, state} ; // read/sink例外にも適用
	const auto wait =
		//------------------------------------------------------------------------------
		/**
		 * @brief async通知を取消し/全体期限付きで待ちます
		 * @param[in] expected 完了種類
		 */
		//------------------------------------------------------------------------------
		[&]( DWORD expected)
		{
			// workerだけが実通知を待ち、stop/deadlineでrequest closeへ進みます
			for(;;) {
				cancelled( stop) ;
				// 段階が進まなくても全体期限を延長しません
				if( std::chrono::steady_clock::now() >= deadline) {
					throw UpdateFailure{UpdateError::NETWORK, ERROR_TIMEOUT} ;
				}
				const DWORD result = WaitForSingleObject( state.changed.get(), 50) ; // UI待機なし
				// 通知がない間は有限周期で取消しを確認します
				if( result == WAIT_TIMEOUT) {
					continue ;
				}
				require( result == WAIT_OBJECT_0, UpdateError::NETWORK) ;
				const auto status = state.status.load( std::memory_order_acquire) ; // callback確定値
				// WinHTTPの内部再送要求も自動retryしません
				if( status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
					throw UpdateFailure{UpdateError::NETWORK, state.error.load()} ;
				}
				// 不一致を成功EOFとして扱いません
				if( status != expected) {
					throw UpdateFailure{UpdateError::NETWORK, ERROR_INVALID_DATA} ;
				}
				return ;
			}
		} ;
	const auto headers = metadata ? L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: "
									L"2022-11-28\r\nAccept-Encoding: identity\r\n"
								  : L"Accept: application/octet-stream\r\nAccept-Encoding: "
									L"identity\r\n" ; // 認証/本文なし
	require( WinHttpAddRequestHeaders( request.get(), headers, static_cast<DWORD>( -1), WINHTTP_ADDREQ_FLAG_ADD),
			UpdateError::NETWORK) ;
	const auto sent = WinHttpSendRequest( request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
										 context) ; // 一回だけ
	require( sent || GetLastError() == ERROR_IO_PENDING, UpdateError::NETWORK) ;
	wait( WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE) ;
	const auto receiving = WinHttpReceiveResponse( request.get(), nullptr) ; // async
	require( receiving || GetLastError() == ERROR_IO_PENDING, UpdateError::NETWORK) ;
	wait( WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE) ;
	HttpResult result ; // bodyを持たない固定header結果
	DWORD	   bytes = sizeof(result.status) ; // 数値status長
	require( WinHttpQueryHeaders( request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
								WINHTTP_HEADER_NAME_BY_INDEX, &result.status, &bytes, WINHTTP_NO_HEADER_INDEX),
			UpdateError::NETWORK) ;
	// redirectを自動実行せず、呼出し側の一転送判定へ渡します
	if( result.status == 301 || result.status == 302 || result.status == 303 || result.status == 307 ||
	   result.status == 308) {
		std::array<wchar_t, 8193> location{} ; // 固定上限
		bytes = sizeof(location) ;
		require( WinHttpQueryHeaders( request.get(), WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
									location.data(), &bytes, WINHTTP_NO_HEADER_INDEX),
				UpdateError::METADATA) ;
		result.location = toUtf8( location.data()) ;
		return result ;
	}
	// エラーbodyを読込/表示せず、実HTTPだけを返します
	if( result.status != 200) {
		return result ;
	}
	std::array<wchar_t, 128> header{} ; // 固定長header照会
	bytes = sizeof(header) ;
	// 不明な圧縮を解凍して上限を回避しません
	if( WinHttpQueryHeaders( request.get(), WINHTTP_QUERY_CONTENT_ENCODING, WINHTTP_HEADER_NAME_BY_INDEX, header.data(),
						   &bytes, WINHTTP_NO_HEADER_INDEX)) {
		// identity以外はfail closed
		if( std::wstring_view( header.data()) != L"identity") {
			throw UpdateFailure{UpdateError::METADATA} ;
		}
	} else if( GetLastError() != ERROR_WINHTTP_HEADER_NOT_FOUND) {
		throw UpdateFailure{UpdateError::NETWORK, GetLastError()} ;
	}
	std::uint64_t total = 0 ; // 実受信byte
	// 上限/EOF/取消しまで固定bufferへ受信し、sinkへ逐次渡します
	for(;;) {
		const auto reading =
			WinHttpReadData( request.get(), state.buffer.data(), static_cast<DWORD>( state.buffer.size()), nullptr) ;
		require( reading || GetLastError() == ERROR_IO_PENDING, UpdateError::NETWORK) ;
		wait( WINHTTP_CALLBACK_STATUS_READ_COMPLETE) ;
		const auto count = state.count.load() ; // 実byte
		// EOFだけが正常終端です
		if( !count) {
			break ;
		}
		// metadata/assetそれぞれのsize上限をsink書込み前に照合します
		if( count > state.buffer.size() || count > limit - total) {
			throw UpdateFailure{UpdateError::SIZE} ;
		}
		sink( {state.buffer.data(), count}) ;
		total += count ;
	}
	return result ;
}
/** @brief private新規file/directoryのACLを所有します */
class UpdateSecurity final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 現在利用者のSIDだけからprivate ACLを作ります
	 */
	//------------------------------------------------------------------------------
	UpdateSecurity()
	{
		HANDLE raw = nullptr ; // 自token受取先
		require( OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &raw)) ;
		const UniqueHandle token( raw) ; // token所有
		DWORD			   count = 0 ; // TOKEN_USER必要byte
		static_cast<void>( GetTokenInformation( token.get(), TokenUser, nullptr, 0, &count)) ;
		// 外部可変sizeを無制限allocationしません
		if( GetLastError() != ERROR_INSUFFICIENT_BUFFER || !count || count > 65536) {
			throw UpdateFailure{UpdateError::IO} ;
		}
		std::vector<std::uint8_t> data( count) ; // OS出力領域
		require( GetTokenInformation( token.get(), TokenUser, data.data(), count, &count)) ;
		const auto *user = reinterpret_cast<TOKEN_USER *>( data.data()) ; // OS構造体borrow
		LPWSTR		sid = nullptr ; // LocalFree所有へ移すSID
		require( ConvertSidToStringSidW( user->User.Sid, &sid)) ;
		const std::unique_ptr<void, decltype(&LocalFree)> owner( sid, LocalFree) ; // SID所有
		const auto			 sddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;" + std::wstring( sid) + L")" ;
		PSECURITY_DESCRIPTOR descriptor = nullptr ; // OS検証済みSD
		require(
			ConvertStringSecurityDescriptorToSecurityDescriptorW( sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) ;
		descriptor_.reset( descriptor) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief このscopeで借用する新規作成属性です
	 * @return private非継承属性
	 */
	//------------------------------------------------------------------------------
	SECURITY_ATTRIBUTES attributes() const
	{
		return {sizeof(SECURITY_ATTRIBUTES), descriptor_.get(), FALSE} ;
	}

	private:
	/// SD所有
	std::unique_ptr<void, decltype(&LocalFree)> descriptor_{nullptr, LocalFree} ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief LocalAppDataを値だけで取得します
 * @return 正規local path
 */
//------------------------------------------------------------------------------
std::filesystem::path localAppData()
{
	PWSTR	   raw = nullptr ; // CoTaskMemFree所有
	const auto status = SHGetKnownFolderPath( FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw) ;
	const std::unique_ptr<void, decltype(&CoTaskMemFree)> owner( raw, CoTaskMemFree) ; // path所有
	// 環境変数の推測先へfallbackしません
	if( FAILED(status) || !raw) {
		throw UpdateFailure{UpdateError::IO, static_cast<std::uint32_t>( status)} ;
	}
	return std::filesystem::path( raw) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 通常local directoryだけをprivate作成します
 * @param[in] path 絶対local path
 * @param[in] attributes private属性
 */
//------------------------------------------------------------------------------
void privateDirectory( const std::filesystem::path &path, SECURITY_ATTRIBUTES &attributes)
{
	// UNC/deviceを作成対象にしません
	if( !path.is_absolute() || path.native().starts_with( L"\\\\")) {
		throw UpdateFailure{UpdateError::IO} ;
	}
	std::filesystem::path current ; // 上位からの検証位置
	// 各祖先を順に確認し、不正属性の下へ作成しません
	for( const auto &part : path) {
		current /= part ;
		// volume rootは後続directoryから確認します
		if( current == path.root_name()) {
			continue ;
		}
		DWORD flags = GetFileAttributesW( current.c_str()) ; // 同期属性だけ
		// 新規祖先も同じprivate ACLへ限定します
		if( flags == INVALID_FILE_ATTRIBUTES) {
			const auto error = GetLastError() ; // 存在しない場合だけ作成
			// access denied等をmissingに読み替えません
			if( error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
				throw UpdateFailure{UpdateError::IO, error} ;
			}
			require( CreateDirectoryW( current.c_str(), &attributes)) ;
			flags = GetFileAttributesW( current.c_str()) ;
		}
		// reparse/cloud同期先/通常fileは拒否します
		if( flags == INVALID_FILE_ATTRIBUTES || !(flags & FILE_ATTRIBUTE_DIRECTORY) ||
		   flags & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_OPEN |
					FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS)) {
			throw UpdateFailure{UpdateError::IO} ;
		}
	}
}
/** @brief 一操作のfile/dirを所有するnative実装です */
class NativeUpdate final : public IUpdatePlatform {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 自分のstagingだけを解放します
	 */
	//------------------------------------------------------------------------------
	~NativeUpdate() override
	{
		reset() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定feedを一回GETします
	 * @param[in] stop 取消し
	 * @return 有界応答
	 */
	//------------------------------------------------------------------------------
	UpdateFeed check( std::stop_token stop) override
	{
		UpdateFeed feed ; // parserへ渡すbounded body
		const auto result = get(
			"https://api.github.com/repos/EnterOgawa/koe-fude/releases/latest", K_UPDATE_METADATA_LIMIT,
			std::chrono::steady_clock::now() + std::chrono::seconds( 30), stop,
			//------------------------------------------------------------------------------
			/**
			 * @brief 有界metadataだけを追記します
			 * @param[in] chunk 実byte
			 */
			//------------------------------------------------------------------------------
			[&feed]( std::span<const char> chunk)
			{
				feed.body.append( chunk.data(), chunk.size()) ;
			},
			true) ;
		// metadataはredirectを追跡しません
		if( !result.location.empty()) {
			throw UpdateFailure{UpdateError::METADATA} ;
		}
		feed.status = result.status ;
		return feed ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief streaming取得、hash/署名/版確認を完了しread lockを保ちます
	 * @param[in] asset 検証済み候補
	 * @param[in] stop 取消し
	 * @throws UpdateFailure 失敗し今回stagingをcleanup
	 */
	//------------------------------------------------------------------------------
	void prepare( const UpdateAsset &asset, std::stop_token stop) override
	{
		reset() ;
		cancelled( stop) ;
		UpdateSecurity security ; // 新規staging ACL
		auto		   access = security.attributes() ; // scope内borrow
		const auto	   root = localAppData() / L"koefude" / L"updates" ; // history/cacheと別
		privateDirectory( root, access) ;
		static_cast<void>( cleanupUpdateStaging( root)) ; // 過去の未使用ownerだけを有限回収
		ULARGE_INTEGER available{} ; // 現在利用者が使用できる空き容量
		require( GetDiskFreeSpaceExW( root.c_str(), &available, nullptr, nullptr)) ;
		// 実不足なら通信を開始せず、他仕事の余裕を保ちます
		if( available.QuadPart < asset.size + 64 * 1024 * 1024) {
			throw UpdateFailure{UpdateError::IO, ERROR_DISK_FULL} ;
		}
		std::array<std::uint8_t, 16> random{} ; // 新規ownerの乱数
		crypto( BCryptGenRandom( nullptr, random.data(), static_cast<ULONG>( random.size()),
							   BCRYPT_USE_SYSTEM_PREFERRED_RNG)) ;
		std::wstring				name = L"update-" ; // 自分の一操作だけのprefix
		constexpr std::wstring_view HEX = L"0123456789abcdef" ; // 固定表
		// 16byteだけをpath構文のない名前へ写します
		for( const auto byte : random) {
			name += HEX[byte >> 4] ;
			name += HEX[byte & 15] ;
		}
		directory_ = root / name ;
		require( CreateDirectoryW( directory_.c_str(), &access)) ; // CREATE_NEW相当、再利用しません
		directoryLock_.reset( CreateFileW( directory_.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr,
										 OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
										 nullptr)) ; // 自directoryの差替えも止める
		require( directoryLock_.get() != INVALID_HANDLE_VALUE) ;
		const auto		   markerPath = directory_ / L"owner-v1" ; // cleanupの固定証拠
		const UniqueHandle marker( CreateFileW( markerPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &access, CREATE_NEW,
											  FILE_ATTRIBUTE_NORMAL, nullptr)) ;
		require( marker.get() != INVALID_HANDLE_VALUE) ;
		DWORD markerBytes = 0 ; // 固定markerの実書込長
		require( WriteFile( marker.get(), K_OWNER_MARKER.data(), static_cast<DWORD>( K_OWNER_MARKER.size()), &markerBytes,
						  nullptr)) ;
		// 不完全markerを将来のownerとして認めません
		if( markerBytes != K_OWNER_MARKER.size()) {
			throw UpdateFailure{UpdateError::IO} ;
		}
		require( FlushFileBuffers( marker.get())) ;
		path_ = directory_ / L"setup.exe" ;
		file_.reset( CreateFileW( path_.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, &access, CREATE_NEW,
								FILE_ATTRIBUTE_TEMPORARY, nullptr)) ;
		require( file_.get() != INVALID_HANDLE_VALUE) ;
		const auto	  deadline = std::chrono::steady_clock::now() + std::chrono::minutes( 30) ; // 全取得期限
		std::uint64_t received = 0 ; // 実書込みbyte数
		const auto	  sink =
			//------------------------------------------------------------------------------
			/**
			 * @brief 64KiBまでの同期chunkをfileへ追記します
			 * @param[in] chunk 借用byte
			 */
			//------------------------------------------------------------------------------
			[&]( std::span<const char> chunk)
			{
				cancelled( stop) ;
				DWORD written = 0 ; // 実write byte
				require( WriteFile( file_.get(), chunk.data(), static_cast<DWORD>( chunk.size()), &written, nullptr)) ;
				// 部分writeを受信成功へ変換しません
				if( written != chunk.size()) {
					throw UpdateFailure{UpdateError::IO} ;
				}
				received += written ;
			} ;
		auto result = get( asset.url, asset.size, deadline, stop, sink, false) ; // 明示取得一回
		// 正規GitHubの一転送だけを自分で検証して追跡します
		if( !result.location.empty()) {
			// 非正規host/path/query/portは送信前に拒否します
			if( !allowedUpdateRedirect( result.location) || received) {
				throw UpdateFailure{UpdateError::METADATA} ;
			}
			result = get( result.location, asset.size, deadline, stop, sink, false) ;
		}
		// 二度目のredirect/非200/size不一致は実行候補にしません
		if( result.status != 200 || !result.location.empty()) {
			throw UpdateFailure{UpdateError::HTTP, result.status} ;
		}
		// 完全受信だけを検証へ渡します
		if( received != asset.size) {
			throw UpdateFailure{UpdateError::SIZE} ;
		}
		require( FlushFileBuffers( file_.get())) ;
		file_.reset() ; // 書込みhandleを閉じ、以後read-onlyに固定
		file_.reset( CreateFileW( path_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
								FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)) ;
		require( file_.get() != INVALID_HANDLE_VALUE) ;
		verifyUpdateInstaller( file_.get(), path_, asset, stop) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じlocked fileを再照合し通常wizardを起動します
	 * @param[in] asset 同候補
	 * @param[in] stop 取消し、最終CreateProcess前まで有効
	 * @throws UpdateFailure 起動失敗、旧appのdataは変更しません
	 */
	//------------------------------------------------------------------------------
	void launch( const UpdateAsset &asset, std::stop_token stop) override
	{
		// 未準備/cleanup後のpathを起動しません
		if( !file_ || path_.empty()) {
			throw UpdateFailure{UpdateError::LAUNCH} ;
		}
		verifyUpdateInstaller( file_.get(), path_, asset, stop) ; // 同じlockを最終再照合
		require( SetHandleInformation( file_.get(), HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT), UpdateError::LAUNCH) ;
		SIZE_T bytes = 0 ; // attribute list必要長
		static_cast<void>( InitializeProcThreadAttributeList( nullptr, 1, 0, &bytes)) ;
		std::vector<std::uint8_t> storage( bytes) ; // 一handleだけのlist
		auto					 *list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>( storage.data()) ; // OS境界
		require( InitializeProcThreadAttributeList( list, 1, 0, &bytes), UpdateError::LAUNCH) ;
		/** @brief 初期化済みattribute listを必ず解放します */
		struct AttributeOwner {
			LPPROC_THREAD_ATTRIBUTE_LIST value ; // storage寿命内borrow
			//------------------------------------------------------------------------------
			/**
			 * @brief OSの内部資源を解放します
			 */
			//------------------------------------------------------------------------------
			~AttributeOwner()
			{
				DeleteProcThreadAttributeList( value) ;
			}
		} attributes{list} ;
		HANDLE inherited = file_.get() ; // 書込/削除なしlockだけを継承
		require( UpdateProcThreadAttribute( list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &inherited, sizeof(inherited),
										  nullptr, nullptr),
				UpdateError::LAUNCH) ;
		STARTUPINFOEXW startup{} ; // 継承handleを明示限定
		startup.StartupInfo.cb = sizeof(startup) ;
		startup.lpAttributeList = list ;
		PROCESS_INFORMATION process{} ; // この起動だけのhandle
		auto				command = L"\"" + path_.native() + L"\" /NORESTART /KoeFudeWaitPid=" +
					   std::to_wstring( GetCurrentProcessId()) ; // silent/外部入力なし
		cancelled( stop) ; // 起動前の最終checkpoint以降だけOS processを作ります
		const auto launched =
			CreateProcessW( path_.c_str(), command.data(), nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT, nullptr,
						   directory_.c_str(), &startup.StartupInfo, &process) ;
		const auto code = launched ? ERROR_SUCCESS : GetLastError() ; // 直後診断
		static_cast<void>( SetHandleInformation( file_.get(), HANDLE_FLAG_INHERIT, 0)) ;
		// 起動失敗を終了成功として表示しません
		if( !launched) {
			throw UpdateFailure{UpdateError::LAUNCH, code} ;
		}
		const UniqueHandle thread( process.hThread) ; // 子を操作せず解放
		const UniqueHandle child( process.hProcess) ; // wait/killなし
		launched_ = true ; // 実行中fileをcleanupせず次の明示更新へ残します
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 今回stagingだけを破棄します
	 */
	//------------------------------------------------------------------------------
	void reset() noexcept override
	{
		file_.reset() ;
		directoryLock_.reset() ;
		// 今回作成した未起動fileだけを回収し、既存ownerを削除しません
		if( !launched_ && !path_.empty()) {
			static_cast<void>( DeleteFileW( path_.c_str())) ;
		}
		// 自分が作った固定markerだけを回収します
		if( !launched_ && !directory_.empty()) {
			static_cast<void>( DeleteFileW( (directory_ / L"owner-v1").c_str())) ;
		}
		// 自分の空directory以外を再帰削除しません
		if( !launched_ && !directory_.empty()) {
			static_cast<void>( RemoveDirectoryW( directory_.c_str())) ;
		}
		path_.clear() ;
		directory_.clear() ;
		launched_ = false ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定AppIdの導入先と実moduleを照合します
	 * @return installedならtrue
	 */
	//------------------------------------------------------------------------------
	bool installed() override
	{
		std::array<wchar_t, 32768> module{} ; // Windows path上限
		const auto chars = GetModuleFileNameW( nullptr, module.data(), static_cast<DWORD>( module.size())) ;
		// 不明導入元はportableとして扱い、outへ置換しません
		if( !chars || chars >= module.size()) {
			return false ;
		}
		std::array<wchar_t, 32768> location{} ; // 固定AppIdの導入先だけ
		DWORD					   bytes = sizeof(location) ; // REG_SZのbyte上限
		const auto				   status =
			RegGetValueW( HKEY_CURRENT_USER,
						 L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
						 L"\\{6B3A1531-0794-4E2A-A6B3-DCC78F5D79F4}_is1",
						 L"InstallLocation", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, location.data(), &bytes) ;
		// 他app/別user/不明値へ導入判定を広げません
		if( status != ERROR_SUCCESS || bytes < sizeof(wchar_t) || bytes > sizeof(location)) {
			return false ;
		}
		auto current = std::filesystem::path( module.data()).parent_path().lexically_normal() ; // 値のみ
		auto target = std::filesystem::path( location.data()).lexically_normal() ; // trailing separator除去
		// Innoの末尾separatorを比較だけから除きます
		if( target.filename().empty()) {
			target = target.parent_path() ;
		}
		return CompareStringOrdinal( current.c_str(), -1, target.c_str(), -1, TRUE) == CSTR_EQUAL ;
	}

	private:
	/// 今回乱数ownerだけ
	std::filesystem::path directory_ ;
	/// 固定setup.exe
	std::filesystem::path path_ ;
	/// private parentの差替え防止
	UniqueHandle		  directoryLock_ ;
	/// read-only/share-readだけの検証済みfile
	UniqueHandle		  file_ ;
	/// 起動済みfileを削除しません
	bool				  launched_ = false ;
} ;
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief Innoの固定長resourceの末尾ASCII space paddingだけを除いて完全照合します
 * @param[in] actual 終端NUL除去済み、最大255 UTF-16文字
 * @param[in] expected 固定期待文字列
 * @return 正確な一致。先頭/途中/Unicode空白/制御文字は正規化しません
 * @see koefude::matchesUpdateResource
 */
//------------------------------------------------------------------------------
bool matchesUpdateResource( std::wstring_view actual, std::wstring_view expected) noexcept
{
	// 固定resource上限と空の期待値を拒否し、任意長の正規化へ広げません
	if( actual.size() > 255 || expected.empty() || expected.size() > 255) {
		return false ;
	}
	// Innoが予約した末尾ASCII spaceだけを、文字数上限まで一度ずつ除きます
	while( !actual.empty() && actual.back() == L' ') {
		actual.remove_suffix( 1) ;
	}
	return actual == expected ;
}
//------------------------------------------------------------------------------
/**
 * @brief 明示取得時に、未使用でmarkerのある自作ownerだけを有限回収します
 * @param[in] root 検証済みprivate更新root。未知file/使用中/非正規ownerは保持します
 * @return 回収した空owner数
 * @throws UpdateFailure 64件超過/列挙失敗。再帰削除なし
 * @see koefude::cleanupUpdateStaging
 */
//------------------------------------------------------------------------------
std::size_t cleanupUpdateStaging( const std::filesystem::path &root)
{
	std::size_t removed = 0 ; // 回収済みowner数
	std::size_t inspected = 0 ; // 最外の有限列挙
	// 最大64ownerを確認し、未知名/reparse/使用中には削除操作を行いません
	for( const auto &entry : std::filesystem::directory_iterator( root)) {
		// 異常な大量残存は有限失敗とし、無限走査しません
		if( ++inspected > 64) {
			throw UpdateFailure{UpdateError::IO, ERROR_TOO_MANY_NAMES} ;
		}
		const auto name = entry.path().filename().native() ; // basenameだけ
		// 自作の32hex owner以外へ処理を広げません
		if( name.size() != 39 || !name.starts_with( L"update-") ||
		   !std::all_of( name.begin() + 7, name.end(),
						//------------------------------------------------------------------------------
						/**
						 * @brief 正規owner名だけを認めます
						 * @param[in] c 文字
						 * @return hex
						 */
						//------------------------------------------------------------------------------
						[]( wchar_t c)
						{
							return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') ;
						})) {
			continue ;
		}
		const DWORD flags = GetFileAttributesW( entry.path().c_str()) ; // 非読込み属性照会
		// junction/cloud/通常fileを所有directoryと扱いません
		if( flags == INVALID_FILE_ATTRIBUTES || !(flags & FILE_ATTRIBUTE_DIRECTORY) ||
		   flags & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_OPEN |
					FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS)) {
			continue ;
		}
		const UniqueHandle directory( CreateFileW( entry.path().c_str(), FILE_LIST_DIRECTORY | DELETE, FILE_SHARE_READ,
												 nullptr, OPEN_EXISTING,
												 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)) ;
		// 自worker/installerが使用中なら強制削除しません
		if( directory.get() == INVALID_HANDLE_VALUE) {
			continue ;
		}
		bool	 known = true ; // 既知の二通常fileだけが対象
		unsigned children = 0 ; // 一owner内の上限
		// marker/setup以外のfileが一つでもあればowner全体を保持します
		for( const auto &child : std::filesystem::directory_iterator( entry.path())) {
			const auto fileName = child.path().filename().native() ; // 固定basename照合
			const auto attributes = GetFileAttributesW( child.path().c_str()) ; // 属性だけ
			// 不明/過大/dir/reparse/同期属性のあるownerは保持します
			if( ++children > 2 || (fileName != L"owner-v1" && fileName != L"setup.exe") ||
			   attributes == INVALID_FILE_ATTRIBUTES ||
			   attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE |
							 FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS)) {
				known = false ;
				break ;
			}
		}
		// 未知fileを含むownerを部分的に壊しません
		if( !known) {
			continue ;
		}
		UniqueHandle marker( CreateFileW( (entry.path() / L"owner-v1").c_str(), GENERIC_READ | DELETE, 0, nullptr,
										OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)) ;
		// 未使用markerを専有できないownerは保持します
		if( marker.get() == INVALID_HANDLE_VALUE) {
			continue ;
		}
		LARGE_INTEGER		 length{} ; // 正確なmarker長
		std::array<char, 32> data{} ; // 固定markerより少し大きい上限
		DWORD				 read = 0 ; // 実読取
		// markerは正確な長さ/内容が一致した時だけ自作ownerと認めます
		if( !GetFileSizeEx( marker.get(), &length) || length.QuadPart != static_cast<LONGLONG>( K_OWNER_MARKER.size()) ||
		   !ReadFile( marker.get(), data.data(), static_cast<DWORD>( K_OWNER_MARKER.size()), &read, nullptr) ||
		   read != K_OWNER_MARKER.size() || std::string_view( data.data(), read) != K_OWNER_MARKER) {
			continue ;
		}
		UniqueHandle setup( CreateFileW( (entry.path() / L"setup.exe").c_str(), GENERIC_READ | DELETE, 0, nullptr,
									   OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)) ;
		// 不完全ownerでsetupがない場合だけはmarker/空dirを回収できます
		if( setup.get() == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_NOT_FOUND) {
			continue ;
		}
		FILE_DISPOSITION_INFO disposition{TRUE} ; // 開いた同handleの削除だけ
		// 使用中installerは専有openに失敗するためここへ到達しません
		if( setup.get() != INVALID_HANDLE_VALUE &&
		   !SetFileInformationByHandle( setup.get(), FileDispositionInfo, &disposition, sizeof(disposition))) {
			continue ;
		}
		setup.reset() ; // delete-on-closeを完了
		// markerの削除失敗は未知fileへ削除範囲を広げず残します
		if( !SetFileInformationByHandle( marker.get(), FileDispositionInfo, &disposition, sizeof(disposition))) {
			continue ;
		}
		// marker close後に同directory handleで空ownerだけを削除します
		marker.reset() ;
		// 空directoryだけが消えます。未知childが競合挿入されれば失敗して保持します
		if( SetFileInformationByHandle( directory.get(), FileDispositionInfo, &disposition, sizeof(disposition))) {
			++removed ;
		}
	}
	return removed ;
}
//------------------------------------------------------------------------------
/**
 * @brief 開いたread lockとpathの同じinstallerを検証します
 * @param[in] file 書込/削除共有なしの非所有handle
 * @param[in] path 同じlocal通常file
 * @param[in] asset 期待版/size/SHA
 * @param[in] stop 取消し
 * @param[in] cacheOnly 内部正例試験だけtrue、製品は標準CA照会を許可
 * @throws UpdateFailure trust/hash/製品不一致。installer起動は行いません
 * @see koefude::verifyUpdateInstaller
 */
//------------------------------------------------------------------------------
void verifyUpdateInstaller( HANDLE file, const std::filesystem::path &path, const UpdateAsset &asset,
						   std::stop_token stop, bool cacheOnly)
{
	cancelled( stop) ;
	verifyHash( file, asset, stop) ;
	WINTRUST_FILE_INFO info{} ; // 同じhandleを署名providerへ渡す
	info.cbStruct = sizeof(info) ;
	info.pcwszFilePath = path.c_str() ;
	info.hFile = file ;
	WINTRUST_DATA trust{} ; // UIを禁止し、明示更新時だけ標準CA照会を許可
	trust.cbStruct = sizeof(trust) ;
	trust.dwUIChoice = WTD_UI_NONE ;
	trust.fdwRevocationChecks = WTD_REVOKE_NONE ;
	trust.dwUnionChoice = WTD_CHOICE_FILE ;
	trust.pFile = &info ;
	trust.dwStateAction = WTD_STATEACTION_VERIFY ;
	trust.dwProvFlags = WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT | (cacheOnly ? WTD_CACHE_ONLY_URL_RETRIEVAL : 0) ;
	GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2 ; // 標準Authenticode policy
	/** @brief state handleを成功/失敗とも解放します */
	struct TrustOwner {
		WINTRUST_DATA &trust ; // 最後まで生存する値
		GUID		  &action ; // 同policy
		//------------------------------------------------------------------------------
		/**
		 * @brief WinTrustのstateを必ずCLOSEします
		 */
		//------------------------------------------------------------------------------
		~TrustOwner()
		{
			trust.dwStateAction = WTD_STATEACTION_CLOSE ;
			static_cast<void>( WinVerifyTrust( reinterpret_cast<HWND>( INVALID_HANDLE_VALUE), &action,
											 &trust)) ; // UIなしの公式sentinel境界
		}
	} owner{trust, action} ;
	const LONG status = WinVerifyTrust( reinterpret_cast<HWND>( INVALID_HANDLE_VALUE), &action,
									   &trust) ; // UIなしの公式sentinel境界
	// HRESULTのSUCCEEDEDではなくLONG==0だけを成功とします
	if( status != ERROR_SUCCESS) {
		throw UpdateFailure{UpdateError::SIGNATURE, static_cast<std::uint32_t>( status)} ;
	}
	auto *provider = WTHelperProvDataFromStateData( trust.hWVTStateData) ; // trust state内borrow
	auto *signer = provider ? WTHelperGetProvSignerFromChain( provider, 0, FALSE, 0) : nullptr ;
	// timestamp signerや未知chainをpublisherとして扱いません
	if( !signer || !signer->csCertChain || !signer->pasCertChain[0].pCert) {
		throw UpdateFailure{UpdateError::PUBLISHER} ;
	}
	const auto					*cert = signer->pasCertChain[0].pCert ; // 実署名leaf
	std::array<std::uint8_t, 32> digest{} ; // leaf DER SHA256
	DWORD						 bytes = static_cast<DWORD>( digest.size()) ; // 出力上限
	require( CryptHashCertificate2( BCRYPT_SHA256_ALGORITHM, 0, nullptr, cert->pbCertEncoded, cert->cbCertEncoded,
								  digest.data(), &bytes),
			UpdateError::PUBLISHER) ;
	std::array<wchar_t, 256> publisher{} ; // 固定publisher CN
	const auto				 chars =
		CertGetNameStringW( cert, CERT_NAME_ATTR_TYPE, 0, const_cast<char *>( szOID_COMMON_NAME), publisher.data(),
						   static_cast<DWORD>( publisher.size())) ; // API固定引数
	// subject部分一致等でpinを迂回しません
	if( bytes != digest.size() || digest != K_PUBLISHER_CERTIFICATE || !chars || chars > publisher.size() ||
	   std::wstring_view( publisher.data()) != L"ENTER SYSTEM, K.K.") {
		throw UpdateFailure{UpdateError::PUBLISHER} ;
	}
	cancelled( stop) ;
	verifyProduct( file, path, asset) ;
}
//------------------------------------------------------------------------------
/**
 * @brief native固定通信/検証を生成します
 * @return 未実行platform
 * @see koefude::makeNativeUpdatePlatform
 */
//------------------------------------------------------------------------------
std::unique_ptr<IUpdatePlatform> makeNativeUpdatePlatform()
{
	return std::make_unique<NativeUpdate>() ;
}
} // namespace koefude
