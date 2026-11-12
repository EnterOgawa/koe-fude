//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file model_http.cpp
 * @brief 許可したHTTPSだけを非同期WinHTTPで有界取得します
 * @version 1.0.0
 * @date 2026/09/13
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/13 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/13 手動redirect・非同期取消し・callback寿命を実装
 * 0.1.0 Shunji Ogawa 2026/10/19 宣言の役割を明記
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1283の用途13完全一致例外と応答検査を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1407の具体的な契約と処理意図を記載
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
#include "inference/model_download.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

#include <winhttp.h>

#include "core/types.h"
#include "platform/win_helpers.h"

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief 通信と事前表示が共有する許可hostを返します
 * @return process寿命中有効な列
 * @see koefude::modelDownloadHosts
 */
//------------------------------------------------------------------------------
std::span<const std::wstring_view> modelDownloadHosts() noexcept
{
	static constexpr std::array<std::wstring_view, 8> K_HOSTS{L"github.com",
															  L"release-assets.githubusercontent.com",
															  L"objects.githubusercontent.com",
															  L"huggingface.co",
															  L"cdn-lfs.huggingface.co",
															  L"cdn-lfs-us-1.huggingface.co",
															  L"cas-bridge.xethub.hf.co",
															  L"us.aws.cdn.hf.co"} ; // 通信とUIが共有する固定host
	return K_HOSTS ;
}

namespace {

/** @brief callbackを持たないsession/connectionを解放します */
struct HttpDeleter {
	//------------------------------------------------------------------------------
	/**
	 * @brief WinHTTP資源を閉じます
	 * @param[in] value 所有する有効handle
	 */
	//------------------------------------------------------------------------------
	void operator()( HINTERNET value) const noexcept
	{
		WinHttpCloseHandle( value) ;
	}
} ;
using HttpHandle = std::unique_ptr<void, HttpDeleter> ; // session/connectionの所有型

/** @brief 検査済みURLをWinHTTPの値へ分けます */
struct HttpLocation {
	/// 固定allowlistと照合済みのhost
	std::wstring host ;
	/// pathとquery。診断やlogへ出力しない
	std::wstring target ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 取得先をHTTPSと固定hostへ制限します
 * @param[in] url 最大16384 UTF-16単位の取得先
 * @return WinHTTPへ渡す所有host/path
 * @throws OperationError scheme/host/port/auth/文字列の拒否
 */
//------------------------------------------------------------------------------
[[nodiscard]] HttpLocation checkedLocation( const std::wstring &url)
{
	// 制御文字、fragment、曖昧な区切りや過大URLをHTTP APIへ渡さない
	if( url.empty() || url.size() > 16384 ||
	   std::any_of( url.begin(), url.end(),
				   //------------------------------------------------------------------------------
				   /**
				    * @brief URLで許さない文字を検出します
				    * @param[in] unit UTF-16単位
				    * @return 拒否文字ならtrue
				    */
				   //------------------------------------------------------------------------------
				   []( const wchar_t unit)
				   {
					   return unit <= 32 || unit >= 127 || unit == L'\\' || unit == L'#' ;
				   })) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_WINHTTP_INVALID_URL) ;
	}
	URL_COMPONENTS parts{} ; // 呼出し中はurl内のborrowだけを受け取る構造体
	parts.dwStructSize = sizeof(parts) ;
	parts.dwHostNameLength = static_cast<DWORD>( -1) ;
	parts.dwUrlPathLength = static_cast<DWORD>( -1) ;
	parts.dwExtraInfoLength = static_cast<DWORD>( -1) ;
	parts.dwUserNameLength = static_cast<DWORD>( -1) ;
	parts.dwPasswordLength = static_cast<DWORD>( -1) ;
	// URL内資格情報やHTTPS以外のportを使用しない
	if( !WinHttpCrackUrl( url.c_str(), static_cast<DWORD>( url.size()), 0, &parts) ||
	   parts.nScheme != INTERNET_SCHEME_HTTPS || parts.nPort != INTERNET_DEFAULT_HTTPS_PORT ||
	   parts.dwUserNameLength != 0 || parts.dwPasswordLength != 0) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_WINHTTP_INVALID_URL) ;
	}
	std::wstring host( parts.lpszHostName, parts.dwHostNameLength) ; // 大文字小文字を統一するhost
	std::transform( host.begin(), host.end(), host.begin(),
				   //------------------------------------------------------------------------------
				   /**
				    * @brief ASCII hostを小文字へ揃えます
				    * @param[in] unit ASCII文字
				    * @return 小文字化した値
				    */
				   //------------------------------------------------------------------------------
				   []( const wchar_t unit)
				   {
					   return unit >= L'A' && unit <= L'Z' ? static_cast<wchar_t>( unit + 32) : unit ;
				   }) ;
	const auto hosts = modelDownloadHosts() ; // 通信と事前表示が共有する固定host
	// suffix一致や任意subdomainを許可しない
	if( std::find( hosts.begin(), hosts.end(), host) == hosts.end()) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_WINHTTP_REDIRECT_FAILED) ;
	}
	std::wstring target( parts.lpszUrlPath, parts.dwUrlPathLength) ; // queryを含める要求先
	// queryがなければNULL pointerの0長appendを行わない
	if( parts.dwExtraInfoLength != 0) {
		target.append( parts.lpszExtraInfo, parts.dwExtraInfoLength) ;
	}
	// hostだけのURLはrootへ要求する
	if( target.empty()) {
		target = L"/" ;
	}
	return {std::move( host), std::move( target)} ;
}

/** @brief close通知が完了するまでcallback contextと受信bufferを所有します */
class HttpRequest final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief callbackの起床eventを作成します
	 */
	//------------------------------------------------------------------------------
	HttpRequest()
		: changed_( CreateEventW( nullptr, FALSE, FALSE, nullptr)), closed_( CreateEventW( nullptr, TRUE, FALSE, nullptr))
	{
		// callback待機に必要な資源を得られなければ通信を始めない
		if( !changed_ || !closed_) {
			throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 最後のclose通知までbufferを保持してから解放します
	 */
	//------------------------------------------------------------------------------
	~HttpRequest()
	{
		// 登録済みcallbackにはclose後もcontextが必要なので同期を取る
		if( request_) {
			WinHttpCloseHandle( request_) ;
			// callback登録後だけ、最後のHANDLE_CLOSINGを待つ
			if( callbackSet_) {
				WaitForSingleObject( closed_.get(), INFINITE) ;
			}
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief contextの移動と複製を禁止します
	 * @note 名前のない第1引数（const HttpRequest &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	HttpRequest( const HttpRequest &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief contextの代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const HttpRequest &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	HttpRequest &operator=( const HttpRequest &) = delete ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 検査済みURLを取得開始します
	 * @param[in] session 呼出し中とobject寿命中有効なsession
	 * @param[in] location 検査済みhost/path
	 * @param[in] stop 接続中の取消し
	 * @throws OperationError 通信/設定失敗、取消し
	 */
	//------------------------------------------------------------------------------
	void		 open( HINTERNET session, const HttpLocation &location, const std::stop_token stop)
	{
		connection_.reset( WinHttpConnect( session, location.host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0)) ;
		// 接続資源を得られなければ要求を作らない
		if( !connection_) {
			throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
		}
		request_ = WinHttpOpenRequest( connection_.get(), L"GET", location.target.c_str(), nullptr, WINHTTP_NO_REFERER,
									  WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) ;
		// 不正requestをcallbackへ渡さない
		if( !request_) {
			throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
		}
		DWORD_PTR context = reinterpret_cast<DWORD_PTR>( this) ; // close完了まで移動しないAPI境界
		// 自動redirect・cookie・認証情報の送信を無効にするWinHTTP flag
		DWORD	  disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION ;
		DWORD	  headerLimit = 65536 ; // 応答headerの最大byte数
		// 自動送信や自動追跡を明示的に無効にする
		if( !WinHttpSetOption( request_, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)) ||
		   !WinHttpSetOption( request_, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) ||
		   !WinHttpSetOption( request_, WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE, &headerLimit, sizeof(headerLimit))) {
			throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
		}
		// 全完了とhandle寿命だけを通知し、受信dataをcallbackからUIへ渡さない
		if( WinHttpSetStatusCallback( request_, callback,
									WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES,
									0) == WINHTTP_INVALID_STATUS_CALLBACK) {
			throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
		}
		callbackSet_ = true ;
		start( WinHttpSendRequest( request_, L"Accept-Encoding: identity\r\n", static_cast<DWORD>( -1),
								 WINHTTP_NO_REQUEST_DATA, 0, 0, context)) ;
		wait( WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, stop) ;
		start( WinHttpReceiveResponse( request_, nullptr)) ;
		wait( WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, stop) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief HTTP statusを取得します
	 * @return HTTP status code
	 * @throws OperationError 取得失敗
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] DWORD status() const
	{
		DWORD value = 0 ; // 数値のHTTP応答
		DWORD size = sizeof(value) ; // 出力領域のbyte数
		// 診断のない成功を作らない
		if( !WinHttpQueryHeaders( request_, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
								WINHTTP_HEADER_NAME_BY_INDEX, &value, &size, WINHTTP_NO_HEADER_INDEX)) {
			throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
		}
		return value ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 上限付きheaderを取得します
	 * @param[in] field WinHTTPのheader識別子
	 * @return 未指定は空、指定時は最大16384文字。queryはlogへ渡しません
	 * @throws OperationError 過大header、HTTP API失敗
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::wstring header( const DWORD field) const
	{
		DWORD		bytes = 0 ; // 必要なUTF-16領域byte数
		// header必要容量の照会結果。errorと必要byte数で欠落または再取得を判定する
		const BOOL	empty = WinHttpQueryHeaders( request_, field, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &bytes,
												WINHTTP_NO_HEADER_INDEX) ;
		const DWORD code = GetLastError() ; // size照会の診断値
		// 指定されていないheaderは呼出し側の方針で扱う
		if( !empty && code == ERROR_WINHTTP_HEADER_NOT_FOUND) {
			return {} ;
		}
		// headerの要求で無制限にallocationしない
		if( empty || code != ERROR_INSUFFICIENT_BUFFER || bytes > 32770 || bytes < 2) {
			throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_WINHTTP_INVALID_SERVER_RESPONSE) ;
		}
		std::wstring value( bytes / sizeof(wchar_t), L'\0') ; // 同期照会の所有領域
		// 全領域へ読み込めた値だけを返す
		if( !WinHttpQueryHeaders( request_, field, WINHTTP_HEADER_NAME_BY_INDEX, value.data(), &bytes,
								WINHTTP_NO_HEADER_INDEX)) {
			throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
		}
		value.resize( bytes / sizeof(wchar_t)) ;
		// OSが含める終端だけを除き、本文の空白等は変換しない
		while( !value.empty() && value.back() == L'\0') {
			value.pop_back() ;
		}
		return value ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 一つの有界blockを読みます
	 * @param[in] stop 受信待ち中の取消し
	 * @return 次のreadまたはobject破棄まで有効なborrow。空はEOF
	 * @throws OperationError 受信失敗、取消し
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::span<const std::byte> read( const std::stop_token stop)
	{
		checkCancelled( stop) ;
		start( WinHttpReadData( request_, buffer_.data(), static_cast<DWORD>( buffer_.size()), nullptr)) ;
		wait( WINHTTP_CALLBACK_STATUS_READ_COMPLETE, stop) ;
		const auto bytes = readBytes_.load( std::memory_order_acquire) ; // callbackが確定したbyte数
		// APIが示す長さを所有bufferの外へ公開しない
		if( bytes > buffer_.size()) {
			throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_INVALID_DATA) ;
		}
		return std::span<const std::byte>( buffer_).first( bytes) ;
	}

	private:
	//------------------------------------------------------------------------------
	/**
	 * @brief 非同期APIが要求を受理したことを確認します
	 * @param[in] result 開始APIの戻り値
	 */
	//------------------------------------------------------------------------------
	void start( const BOOL result) const
	{
		// 完了待ち以外の開始失敗を待機へ移さない
		if( !result && GetLastError() != ERROR_IO_PENDING) {
			throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 一段階の完了を取消し可能に待ちます
	 * @param[in] expected 期待する完了bit
	 * @param[in] stop 協調取消し
	 * @throws OperationError 通信失敗、45秒の無応答、取消し
	 */
	//------------------------------------------------------------------------------
	void wait( const DWORD expected, const std::stop_token stop)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 45) ; // 無応答上限
		// 完了・失敗・取消し・timeoutのいずれかまで有界間隔で待機する
		for(;;) {
			checkCancelled( stop) ;
			const DWORD flags = flags_.load( std::memory_order_acquire) ; // 最新の完了状態
			// request errorは取消し要求の有無を確認してから元codeで通知する
			if( (flags & WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) != 0) {
				throw OperationError( ErrorKind::MODEL_REJECTED, error_.load( std::memory_order_acquire)) ;
			}
			// 今回の完了bitだけを消費して次のAPIへ進む
			if( (flags & expected) != 0) {
				flags_.fetch_and( ~expected, std::memory_order_acq_rel) ;
				return ;
			}
			// 応答しないendpointにworkerを永久占有させない
			if( std::chrono::steady_clock::now() >= deadline) {
				throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_WINHTTP_TIMEOUT) ;
			}
			const DWORD result = WaitForSingleObject( changed_.get(), 50) ; // UIには影響しないworker待機
			// event待機の異常をbusy loopで隠さない
			if( result == WAIT_FAILED) {
				throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
			}
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief WinHTTP callbackから完了値だけを保持します
	 * @param[in] internet WinHTTPが所有する通知元handle
	 * @param[in] context close通知まで有効なHttpRequestのborrow
	 * @param[in] status 完了種別
	 * @param[in] information callback中だけ有効な情報。NULL可
	 * @param[in] bytes 情報のbyte数または読取り完了byte数
	 * @note 例外、allocation、UI呼出し、blocking処理を行いません
	 */
	//------------------------------------------------------------------------------
	static void CALLBACK callback( HINTERNET internet, DWORD_PTR context, DWORD status, void *information,
								  DWORD bytes) noexcept
	{
		static_cast<void>( internet) ;
		// context設定前の通知には所有objectがない
		if( context == 0) {
			return ;
		}
		auto *self = reinterpret_cast<HttpRequest *>( context) ; // 所有workerがclose通知まで保持する境界
		// 最後の通知でcontextと受信bufferの破棄を許可する
		if( status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
			SetEvent( self->closed_.get()) ;
			return ;
		}
		// 受信量は同じ所有bufferに対応する確定値だけを渡す
		if( status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
			self->readBytes_.store( bytes, std::memory_order_release) ;
		}
		// 非同期エラーの元codeを情報の寿命中にcopyする
		if( status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
			DWORD error = ERROR_WINHTTP_INTERNAL_ERROR ; // 不正な通知を成功にしない既定値
			// 正しい大きさの構造体だけを解釈する
			if( information && bytes == sizeof(WINHTTP_ASYNC_RESULT)) {
				error = static_cast<WINHTTP_ASYNC_RESULT *>( information)->dwError ;
			}
			self->error_.store( error, std::memory_order_release) ;
		}
		self->flags_.fetch_or( status, std::memory_order_release) ;
		SetEvent( self->changed_.get()) ;
	}

	/// 完了bitの確認を起こすauto-reset event
	UniqueHandle				 changed_ ;
	/// 最後のcallbackの完了通知
	UniqueHandle				 closed_ ;
	/// requestより長く保持する接続
	HttpHandle					 connection_ ;
	/// close callback完了までこのobjectが所有
	HINTERNET					 request_ = nullptr ;
	/// close通知が必要な状態
	bool						 callbackSet_ = false ;
	/// callbackとworker間の完了bit
	std::atomic<DWORD>			 flags_{0} ;
	/// 元の非同期error code
	std::atomic<DWORD>			 error_{0} ;
	/// 今回確定した読取り量
	std::atomic<DWORD>			 readBytes_{0} ;
	/// close callback前には解放しない受信領域
	std::array<std::byte, 65536> buffer_{} ;
} ;

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief redirect先を通信前に既存HTTPS境界で検査します
 * @param[in] url 一時queryを含められる取得先。保存しません
 * @throws OperationError scheme/host/port/認証/文字列の拒否
 * @see koefude::validateModelDownloadLocation
 */
//------------------------------------------------------------------------------
void validateModelDownloadLocation( std::wstring_view url)
{
	static_cast<void>( checkedLocation( std::wstring( url))) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 次のredirectを既存の最大5回境界で検査します
 * @param[in] followed 既に追跡したredirect数
 * @throws OperationError 5回追跡済みなら元redirect失敗code
 * @see koefude::validateModelDownloadRedirect
 */
//------------------------------------------------------------------------------
void validateModelDownloadRedirect( std::uint32_t followed)
{
	// 5回追跡後の追加redirectを要求送信前に拒否します
	if( followed >= 5) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_WINHTTP_REDIRECT_FAILED) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 通信資源を作らず固定転送の容量と取得先を検査します
 * @param[in] definition 通常8GiB以内、超過は用途13の全field一致だけを許可します
 * @throws OperationError 不正容量・方式・URL。通信とsink呼出しはありません
 * @see koefude::validateModelTransfer
 */
//------------------------------------------------------------------------------
void validateModelTransfer( const ModelDownloadDefinition &definition)
{
	constexpr std::uint64_t K_ORDINARY_LIMIT = 8ULL * 1024 * 1024 * 1024 ; // 通常転送上限
	const auto			   &fixed = modelDownloadDefinition( wholeTextNativeModelId()) ; // 唯一の例外
	const auto			   &file = modelDefinition( wholeTextNativeModelId()) ; // 最終size/hash
	const bool				exact =
		definition.kind == ModelTransferKind::DIRECT && definition.kind == fixed.kind && definition.url == fixed.url &&
		definition.revision == fixed.revision && definition.license == fixed.license &&
		definition.source == fixed.source && definition.transferBytes == fixed.transferBytes &&
		definition.transferSha256 == fixed.transferSha256 && definition.archiveMember == fixed.archiveMember &&
		definition.suffixHex == fixed.suffixHex && fixed.transferBytes == file.bytes &&
		fixed.transferSha256 == file.sha256 ; // 全fieldと最終期待値の一致
	// 8GiB超の汎用許可や比較専用資産の転送を開始しません
	if( definition.transferBytes == 0 || definition.kind == ModelTransferKind::UNAVAILABLE ||
	   (definition.transferBytes > K_ORDINARY_LIMIT && !exact)) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_INVALID_DATA) ;
	}
	validateModelDownloadLocation( toWide( definition.url)) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 全量応答のstatus/長さ/encodingを副作用なく検査します
 * @param[in] status 元HTTP status
 * @param[in] length 空可の十進Content-Length
 * @param[in] encoding 空またはidentity
 * @param[in] expected 固定転送byte
 * @throws OperationError 元HTTP statusまたは長さ・encodingの診断
 * @see koefude::validateModelDownloadResponse
 */
//------------------------------------------------------------------------------
void validateModelDownloadResponse( std::uint32_t status, std::wstring_view length, std::wstring_view encoding,
								   std::uint64_t expected)
{
	// 認証要求・partial等の元HTTP statusを保持します
	if( status != 200) {
		throw OperationError( ErrorKind::MODEL_REJECTED, status) ;
	}
	// 宣言がある場合だけ十進固定長と厳密一致を要求します
	if( !length.empty() && length != std::to_wstring( expected)) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_BAD_LENGTH) ;
	}
	// 圧縮応答を暗黙に変換しません
	if( !encoding.empty() && encoding != L"identity") {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_WINHTTP_INVALID_SERVER_RESPONSE) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 有界blockの累計を加算前に検査します
 * @param[in] received 直前までの実byte
 * @param[in] bytes 次のblock、最大64KiB
 * @param[in] expected 固定転送byte
 * @return overflowしない新累計
 * @throws OperationError 過大応答または不正な累計
 * @see koefude::advanceModelDownloadBytes
 */
//------------------------------------------------------------------------------
std::uint64_t advanceModelDownloadBytes( std::uint64_t received, std::uint64_t bytes, std::uint64_t expected)
{
	// 引算前の累計と有界blockを検査してuint64 overflowを拒否します
	if( received > expected || bytes > 65536 || bytes > expected - received) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_FILE_TOO_LARGE) ;
	}
	return received + bytes ;
}
//------------------------------------------------------------------------------
/**
 * @brief 固定HTTPS artifactを有界blockで受信先へ渡します
 * @param[in] definition registryの信頼済み取得metadata。呼出し中有効
 * @param[in] sink 同じworkerで呼ぶ受信先。NULL相当の空関数は禁止
 * @param[in] stop 通信待ちを含む取消し
 * @throws OperationError 通信・size・取得境界・受信先の失敗、取消し
 * @note 返却前に全callbackを終了し、byteやcontextのborrowを残しません
 * @see koefude::WinHttpModelTransport::download
 */
//------------------------------------------------------------------------------
void WinHttpModelTransport::download( const ModelDownloadDefinition &definition, const ModelDownloadSink &sink,
									 const std::stop_token stop)
{
	checkCancelled( stop) ;
	// metadataと受信先が不正ならsessionを作る前に拒否する
	if( !sink) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_INVALID_DATA) ;
	}
	validateModelTransfer( definition) ;
	auto url = toWide( definition.url) ; // URLは診断へ出力しない所有値
	static_cast<void>( checkedLocation( url)) ;
	const HttpHandle session( WinHttpOpen( L"KoeFude/0.1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
										 WINHTTP_NO_PROXY_BYPASS,
										 WINHTTP_FLAG_SECURE_DEFAULTS)) ; // TLS1.2以降の非同期session
	// TLSを弱めるsessionや別libraryへfallbackしない
	if( !session || !WinHttpSetTimeouts( session.get(), 10000, 10000, 10000, 15000)) {
		throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
	}
	// 各redirectを同じHTTPS境界で確認し、上限後の追跡を禁止する
	for( int redirects = 0; redirects <= 5; ++redirects) { // 今回までのredirect回数
		checkCancelled( stop) ;
		const auto	location = checkedLocation( url) ; // 今回の検査済み要求先
		HttpRequest request ; // callback完了まで所有する通信
		request.open( session.get(), location, stop) ;
		const DWORD status = request.status() ; // 応答の数値分類
		// 標準redirectだけを手動追跡し、次の要求前にURLを再検査する
		if( status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
			validateModelDownloadRedirect( static_cast<std::uint32_t>( redirects)) ;
			auto next = request.header( WINHTTP_QUERY_LOCATION) ; // 一時的な署名queryを含み得る値
			// 同じoriginのroot相対だけを明示的に補完する
			if( next.starts_with( L"/") && !next.starts_with( L"//")) {
				next = L"https://" + location.host + next ;
			}
			static_cast<void>( checkedLocation( next)) ;
			url = std::move( next) ;
			continue ;
		}
		validateModelDownloadResponse( status, {}, {}, definition.transferBytes) ;
		const auto length = request.header( WINHTTP_QUERY_CONTENT_LENGTH) ; // 省略可能な宣言size
		const auto encoding = request.header( WINHTTP_QUERY_CONTENT_ENCODING) ; // byte一致を壊す圧縮表現
		validateModelDownloadResponse( status, length, encoding, definition.transferBytes) ;
		std::uint64_t received = 0 ; // 宣言値と別に数える実受信byte数
		// EOF/上限/取消しまで64KiB以下のblockを逐次渡す
		for(;;) {
			const auto bytes = request.read( stop) ; // 次のreadまで有効なborrow
			// EOFで正確な総量を確定する
			if( bytes.empty()) {
				break ;
			}
			// Content-Lengthがなくても応答を無制限に受け入れない
			const auto next = advanceModelDownloadBytes( received, bytes.size(),
														definition.transferBytes) ; // 加算前検査
			sink( bytes) ;
			received = next ;
		}
		// 短い応答を成功として後続へ渡さない
		if( received != definition.transferBytes) {
			throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_BAD_LENGTH) ;
		}
		return ;
	}
	throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_WINHTTP_REDIRECT_FAILED) ;
}

} // namespace koefude
