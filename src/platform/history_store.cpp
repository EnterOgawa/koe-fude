//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file history_store.cpp
 * @brief 履歴をユーザー専用local folderで読み、検証済み一時fileから置換します
 * @version 1.0.0
 * @date 2026/09/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/10 排他・private ACL・SHA-256・一時保存の回収を追加
 * 0.1.0 Shunji Ogawa 2026/10/04 形式5への初回保存前に旧履歴を退避し、正常復元/削除時に回収
 * 0.1.0 Shunji Ogawa 2026/10/19 分岐意図・宣言の役割を明記
 * 0.1.0 Shunji Ogawa 2026/10/19 V6移行前の全byte退避と有限回収を接続
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1344の明示API補正と独立保存を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1345の動的モデル・凍結単価と旧履歴互換を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1346の通常API補正・保存モデル表示・限定ログを接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1347の音声テーブル・独立API設定と文章操作を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1351のAPI専用HTTP失敗・保存状態を接続
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372の独立話者投影と元原文保持を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1375の適用済み選好の保存と復元を追加
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1378の明示更新と設定ACKを接続
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
#include "platform/history_store.h"
#include "core/api_text_provenance.h"
#include "platform/api_catalog_store.h"
#include "platform/api_text_store.h"
#include "platform/speaker_text_store.h"
#include "platform/whole_text_persistence_store.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <windows.h>

// Win32基本型を定義してからOSの暗号・権限・known folder APIを参照する
#include <bcrypt.h>
#include <objbase.h>
#include <sddl.h>
#include <shlobj.h>

#include "platform/win_helpers.h"

namespace koefude {
namespace {
constexpr DWORD K_REJECTED_ATTRIBUTES = FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE |
										FILE_ATTRIBUTE_RECALL_ON_OPEN |
										FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS ; // 同期・転送を伴う属性
constexpr std::wstring_view K_PARTIAL_PREFIX = L"history.partial-" ; // 自分が作る一時名の接頭辞
constexpr std::wstring_view K_HEX = L"0123456789abcdef" ; // 乱数をfile名へ変換する表

//------------------------------------------------------------------------------
/**
 * @brief CNGの失敗を診断code付き履歴異常へ変換します
 * @param[in] status OSのNTSTATUS
 */
//------------------------------------------------------------------------------
void						checkCrypto( const NTSTATUS status)
{
	// 暗号API失敗時に未検証の保存を進めない
	if( status < 0) {
		throw HistoryError( HistoryErrorKind::INTERNAL, static_cast<std::uint32_t>( status)) ;
	}
}

/** @brief BCryptのalgorithm handleを解放します */
struct AlgorithmDeleter {
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有providerを閉じます
	 * @param[in] handle 有効な所有handle
	 */
	//------------------------------------------------------------------------------
	void operator()( BCRYPT_ALG_HANDLE handle) const noexcept
	{
		BCryptCloseAlgorithmProvider( handle, 0) ;
	}
} ;

//------------------------------------------------------------------------------
/**
 * @brief 有界な保存内容のSHA-256を計算します
 * @param[in] bytes 読書きの間に変更しない保存byte列
 * @return 32byteのSHA-256。認証や暗号化ではなく破損検出に使います
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::array<std::uint8_t, 32> digest( const std::span<const std::uint8_t> bytes)
{
	BCRYPT_ALG_HANDLE algorithmHandle = nullptr ; // CNGが作成する所有provider
	checkCrypto( BCryptOpenAlgorithmProvider( &algorithmHandle, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) ;
	const std::unique_ptr<void, AlgorithmDeleter> algorithm( algorithmHandle) ;
	BCRYPT_HASH_HANDLE							  hashHandle = nullptr ; // OS内部bufferを使う所有hash context
	checkCrypto( BCryptCreateHash( algorithm.get(), &hashHandle, nullptr, 0, nullptr, 0, 0)) ;
	const std::unique_ptr<void, decltype(&BCryptDestroyHash)> hash( hashHandle, BCryptDestroyHash) ;
	// BCryptHashDataは入力を書き換えないAPI境界。512MiB以下なのでULONGへ収まる
	checkCrypto(
		BCryptHashData( hash.get(), const_cast<std::uint8_t *>( bytes.data()), static_cast<ULONG>( bytes.size()), 0)) ;
	std::array<std::uint8_t, 32> result{} ; // 固定長のSHA-256出力
	checkCrypto( BCryptFinishHash( hash.get(), result.data(), static_cast<ULONG>( result.size()), 0)) ;
	return result ;
}

/** @brief 現ユーザーとSYSTEMだけへ継承付きのアクセス権を設定する所有descriptorです */
class PrivateSecurity final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief processのユーザーSIDからprivate ACLを作ります
	 */
	//------------------------------------------------------------------------------
	PrivateSecurity()
	{
		HANDLE tokenHandle = nullptr ; // このscopeで解放するprocess token
		// 管理者権限を要求せず、自分のtokenの読取りだけを行う
		if( !OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &tokenHandle)) {
			throw HistoryError( HistoryErrorKind::LOCATION, GetLastError()) ;
		}
		const UniqueHandle token( tokenHandle) ;
		DWORD			   needed = 0 ; // TOKEN_USERに必要なbyte数
		// TOKEN_USERの必要容量照会結果。失敗codeと必要byte数を併せて検証する
		const BOOL		   queried = GetTokenInformation( token.get(), TokenUser, nullptr, 0, &needed) ;
		// 想定外の戻り値や過大tokenに基づくallocationを避ける
		if( queried || GetLastError() != ERROR_INSUFFICIENT_BUFFER || needed == 0 || needed > 65536) {
			throw HistoryError( HistoryErrorKind::LOCATION, GetLastError()) ;
		}
		std::vector<std::uint8_t> information( needed) ; // GetTokenInformation用の所有buffer
		// 取得中の失敗もprivate ACLなしの保存へfallbackしない
		if( !GetTokenInformation( token.get(), TokenUser, information.data(), needed, &needed)) {
			throw HistoryError( HistoryErrorKind::LOCATION, GetLastError()) ;
		}
		// OSが書いたTOKEN_USERを同期解釈する境界。SIDはinformationの寿命中だけ借用する
		const auto *user = reinterpret_cast<const TOKEN_USER *>( information.data()) ;
		LPWSTR		sid = nullptr ; // LocalFreeするSID文字列
		// SID変換失敗時も共用領域へ権限を広げない
		if( !ConvertSidToStringSidW( user->User.Sid, &sid)) {
			throw HistoryError( HistoryErrorKind::LOCATION, GetLastError()) ;
		}
		const std::unique_ptr<void, decltype(&LocalFree)> ownedSid( sid, LocalFree) ;
		// SYSTEMと現在利用者だけに継承可能な完全権限を与える所有SDDL
		const std::wstring	 sddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;" + std::wstring( sid) + L")" ;
		PSECURITY_DESCRIPTOR descriptor = nullptr ; // LocalFreeする自己相対descriptor
		// Windowsが検証したdescriptorだけをCreateFile/CreateDirectoryへ渡す
		if( !ConvertStringSecurityDescriptorToSecurityDescriptorW( sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
			throw HistoryError( HistoryErrorKind::LOCATION, GetLastError()) ;
		}
		descriptor_.reset( descriptor) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 新規作成APIへ渡す属性を返します
	 * @return 本instanceの寿命中だけ有効な借用属性
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] SECURITY_ATTRIBUTES attributes() const noexcept
	{
		return {sizeof(SECURITY_ATTRIBUTES), descriptor_.get(), FALSE} ;
	}

	private:
	/// private ACLの所有権
	std::unique_ptr<void, decltype(&LocalFree)> descriptor_{nullptr, LocalFree} ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief Windowsのユーザー専用LocalAppDataを取得します
 * @return 履歴専用directoryの絶対path
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::filesystem::path defaultDirectory()
{
	const HRESULT com = CoInitializeEx( nullptr, COINIT_MULTITHREADED) ; // このworkerのCOM初期化
	// 既存の別apartmentを壊してknown folder処理を続けない
	if( FAILED(com)) {
		throw HistoryError( HistoryErrorKind::LOCATION, static_cast<std::uint32_t>( com)) ;
	}
	PWSTR		  raw = nullptr ; // 成否にかかわらずCoTaskMemFreeするpath
	// 現在利用者のLocalAppDataのpathを取得したHRESULT
	const HRESULT status = SHGetKnownFolderPath( FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw) ;
	const std::unique_ptr<void, decltype(&CoTaskMemFree)> owned( raw, CoTaskMemFree) ;
	CoUninitialize() ; // 値だけを保持し、COM参照を別のthreadへ渡さない
	// system側で取得できないpathを環境変数の推測へ置き換えない
	if( FAILED(status) || !raw) {
		throw HistoryError( HistoryErrorKind::LOCATION, static_cast<std::uint32_t>( status)) ;
	}
	return std::filesystem::path( raw) / L"koefude" / L"history" ;
}

//------------------------------------------------------------------------------
/**
 * @brief local属性を上位から確認して専用folderを準備します
 * @param[in] input 値として指定された保存先
 * @param[in] security 新規folderだけへ適用するprivate属性
 * @param[in] create trueなら存在しないdirectoryを作成します
 * @return 正規化した絶対path
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::filesystem::path prepareDirectory( const std::filesystem::path &input,
													 const PrivateSecurity &security, const bool create)
{
	const auto native = input.native() ; // 未解釈のUTF-16入力値
	// 外部参照、device namespace、NUL、過大pathをOSのfile APIへ渡さない
	if( native.empty() || native.size() > 32700 || native.starts_with( L"\\\\") ||
	   native.find( L"://") != std::wstring::npos || native.find( L'\0') != std::wstring::npos) {
		throw HistoryError( HistoryErrorKind::LOCATION) ;
	}
	const auto	absolute = std::filesystem::absolute( input).lexically_normal() ; // 検証する絶対path
	const auto	root = absolute.root_path() ; // drive種別を検証するroot
	const DWORD drive = GetDriveTypeW( root.c_str()) ; // network driveを拒否するOS情報
	// 書込み可能なlocal drive上のfolderに限定し、drive自体とADSも拒否する
	if( absolute.native().size() < 4 || absolute.native()[1] != L':' ||
	   absolute.native().find( L':', 2) != std::wstring::npos || absolute == root ||
	   (drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE && drive != DRIVE_RAMDISK)) {
		throw HistoryError( HistoryErrorKind::LOCATION) ;
	}
	auto current = root ; // 上位から確認しているfolder
	// 未検証の祖先を跨いでcreate_directoriesせず、各構成要素を順に確認する
	for( const auto &component : absolute.relative_path()) {
		current /= component ;
		DWORD attributes = GetFileAttributesW( current.c_str()) ; // 書込み前の属性
		// 既存folderを再利用し、ない場合だけprivate ACLで作成する
		if( attributes == INVALID_FILE_ATTRIBUTES) {
			const DWORD error = GetLastError() ; // 存在なしと権限失敗を区別する診断
			// 読込専用確認時や未知の異常では新しいfolderを作らない
			if( !create || (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)) {
				throw HistoryError( HistoryErrorKind::LOCATION, error) ;
			}
			auto access = security.attributes() ; // CreateDirectoryが同期借用する属性
			// 他processとの作成競合以外は保存先準備の失敗として返す
			if( !CreateDirectoryW( current.c_str(), &access) && GetLastError() != ERROR_ALREADY_EXISTS) {
				throw HistoryError( HistoryErrorKind::LOCATION, GetLastError()) ;
			}
			attributes = GetFileAttributesW( current.c_str()) ;
		}
		// reparse・同期属性・非directoryを保存の祖先として使わない
		if( attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
		   (attributes & K_REJECTED_ATTRIBUTES) != 0) {
			throw HistoryError( HistoryErrorKind::LOCATION) ;
		}
	}
	return absolute ;
}

//------------------------------------------------------------------------------
/**
 * @brief 保存用fileが存在する場合だけ通常file属性を確認します
 * @param[in] path 検証済み専用folder内の固定file名
 * @return 存在する通常fileならtrue、未作成ならfalse
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool regularFileExists( const std::filesystem::path &path)
{
	const DWORD attributes = GetFileAttributesW( path.c_str()) ; // 内容を読まない属性照会
	// 未作成だけを空履歴として扱い、アクセス拒否を空のfileと誤認しない
	if( attributes == INVALID_FILE_ATTRIBUTES) {
		const DWORD error = GetLastError() ; // 直前APIの診断値
		// 有効な親folderの下でfileがない場合だけ新規作成を許可する
		if( error == ERROR_FILE_NOT_FOUND) {
			return false ;
		}
		throw HistoryError( HistoryErrorKind::READ, error) ;
	}
	// directory・cloud・reparseを履歴として読み書きしない
	if( (attributes & (K_REJECTED_ATTRIBUTES | FILE_ATTRIBUTE_DIRECTORY)) != 0) {
		throw HistoryError( HistoryErrorKind::LOCATION) ;
	}
	return true ;
}

//------------------------------------------------------------------------------
/**
 * @brief 開いたhandleが通常のdisk fileを指すことを確認します
 * @param[in] file 同期呼出し中だけ借用する有効handle
 */
//------------------------------------------------------------------------------
void validateFileHandle( const HANDLE file)
{
	BY_HANDLE_FILE_INFORMATION information{} ; // 開いた後のfile属性
	// 属性照会との間にreparse等へ差し替わった場合も内容を利用しない
	if( !GetFileInformationByHandle( file, &information) || GetFileType( file) != FILE_TYPE_DISK ||
	   (information.dwFileAttributes & (K_REJECTED_ATTRIBUTES | FILE_ATTRIBUTE_DIRECTORY)) != 0) {
		throw HistoryError( HistoryErrorKind::LOCATION, GetLastError()) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 有界なbyte列を全て書き、無進行と部分失敗を拒否します
 * @param[in] file このworkerが所有する書込みhandle
 * @param[in] bytes 同期書込みの間だけ借用するbyte列
 */
//------------------------------------------------------------------------------
void writeBytes( const HANDLE file, const std::span<const std::uint8_t> bytes)
{
	std::size_t offset = 0 ; // 実際に書き終えたbyte数
	// 完了まで最大64KiBずつ進め、OSの部分書込みも扱う
	while( offset < bytes.size()) {
		// 今回の履歴書込みで要求する最大64KiBのbyte数
		const DWORD count = static_cast<DWORD>( std::min<std::size_t>( 65536, bytes.size() - offset)) ;
		DWORD		written = 0 ; // 今回の実書込みbyte数
		// I/O異常を成功した保存として公開しない
		if( !WriteFile( file, bytes.data() + offset, count, &written, nullptr) || written == 0) {
			throw HistoryError( HistoryErrorKind::WRITE, GetLastError()) ;
		}
		offset += written ;
	}
}

/** @brief 通常補正だけの有限metadataを同じprivate folderへ記録します */
class ApiCorrectionEventLog final : public IApiCorrectionEventLog {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 遅延解決する履歴先を保持します
	 * @param[in] directory 空は通常保存先
	 */
	//------------------------------------------------------------------------------
	explicit ApiCorrectionEventLog( std::filesystem::path directory) : directory_( std::move( directory))
	{
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 有限metadataだけを書きます
	 * @param[in] event 自由文なし値
	 * @return 記録成功、失敗でも補正本処理を変更しません
	 */
	//------------------------------------------------------------------------------
	bool append( const ApiCorrectionEventData &event) noexcept override
	{
		try {
			static std::atomic_flag writing = ATOMIC_FLAG_INIT ; // 同processの非待機writer lease
			// 競合するログはbest effortで省略し、I/O待ちのmutexを持ちません
			if( writing.test_and_set( std::memory_order_acquire)) {
				return false ;
			}
			/** @brief I/O終端の全経路で非待機leaseを解放します */
			struct WriterLease {
				std::atomic_flag &writing ; // appendのstatic flagを借用
				//------------------------------------------------------------------------------
				/**
				 * @brief 成否にかかわらずwriter権を返します
				 */
				//------------------------------------------------------------------------------
				~WriterLease()
				{
					writing.clear( std::memory_order_release) ;
				}
			} ;
			const WriterLease	  lease{writing} ; // mutex保持中のblocking I/Oを作りません
			const PrivateSecurity security ; // 新fileはSYSTEMと現在ユーザーのみ
			const auto directory = prepareDirectory( directory_.empty() ? defaultDirectory() : directory_, security,
													false) ; // 既存private履歴先だけ
			const auto path = directory / L"api-correction-events.log" ; // 固定名
			const auto previous = directory / L"api-correction-events.1.log" ; // 前世代一つだけ
			static_cast<void>( regularFileExists( path)) ;
			static_cast<void>( regularFileExists( previous)) ;
			auto		 access = security.attributes() ; // 同期OS借用
			UniqueHandle file( CreateFileW( path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, &access,
										  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)) ;
			// 開けないログが補正や原文保存を失敗させることはありません
			if( file.get() == INVALID_HANDLE_VALUE) {
				file.release() ;
				return false ;
			}
			validateFileHandle( file.get()) ;
			constexpr std::array NAMES{"started",	"terminal",	  "adopt",	 "original",
									   "corrected", "retry-save", "restored"} ; // 固定イベント表
			constexpr std::array STATES{"pending", "sending",	"candidate-ready", "incomplete",
										"failed",  "cancelled", "interrupted"} ; // 固定状態表
			const auto			 kind = static_cast<unsigned>( event.event) ; // 配列境界検査用
			const auto			 state = static_cast<unsigned>( event.provenance.state) ; // 固定状態番号
			// 偽造された列挙値・無界chunkをログに通しません
			if( kind >= NAMES.size() || state > static_cast<unsigned>( ApiTextState::INTERRUPTED) ||
			   event.chunk > (event.audio ? 50000U : 32U) || event.httpStatus > 599 ||
			   (event.audio &&
				(event.audioUnits > 50000 || event.audioSamples > K_MAXIMUM_TIMELINE_SAMPLES ||
				 static_cast<unsigned>( event.audioTerminal) > 7 || static_cast<unsigned>( event.audioBilling) > 3 ||
				 event.provenance.requested.model != ApiProvenanceModel::GPT_TRANSCRIBE ||
				 event.provenance.requested.date)) ||
			   static_cast<unsigned>( event.httpError) > static_cast<unsigned>( ApiTextHttpError::SERVICE_OVERLOADED)) {
				return false ;
			}
			const auto			 now = std::time( nullptr) ; // UTCを文字列化するepoch秒
			std::tm				 utc{} ; // UTC暦
			std::array<char, 32> timestamp{} ; // 固定ISO8601容量
			// 時刻異常を任意文字列で補完しません
			if( gmtime_s( &utc, &now) || !std::strftime( timestamp.data(), timestamp.size(), "%Y-%m-%dT%H:%M:%SZ", &utc)) {
				return false ;
			}
			const std::string line =
				std::string( timestamp.data()) + " event=" + NAMES[kind] +
				" provider=openai purpose=" + std::string( event.audio ? "audio-transcription" : "text-improvement") +
				" history=" + std::to_string( event.history) + " operation=" + std::to_string( event.operation) +
				" generated-operation=" + std::to_string( event.provenance.operation) +
				" requested=" + apiProvenanceModelName( event.provenance.requested) +
				" actual=" + apiProvenanceModelName( event.provenance.actual) + " state=" + STATES[state] +
				" saved=" + std::to_string( event.saved) + " adopted=" + std::to_string( event.adopted) +
				" chunk=" + std::to_string( event.chunk) + " http=" + std::to_string( event.httpStatus) +
				" code=" + std::to_string( event.code) +
				" http-error=" + std::to_string( static_cast<unsigned>( event.httpError)) +
				(event.audio ? " audio-terminal=" + std::to_string( static_cast<unsigned>( event.audioTerminal)) +
								   " audio-billing=" + std::to_string( static_cast<unsigned>( event.audioBilling)) +
								   " audio-samples=" + std::to_string( event.audioSamples) +
								   " audio-units=" + std::to_string( event.audioUnits)
							 : std::string{}) +
				"\r\n" ;
			LARGE_INTEGER size{} ; // 現世代のbyte数
			// 整数変換前に最大file容量を確認します
			if( !GetFileSizeEx( file.get(), &size) || size.QuadPart < 0 || line.size() > 1024) {
				return false ;
			}
			constexpr LONGLONG K_LIMIT = 256 * 1024 ; // 各世代256KiB
			// 過大な既存fileを別世代へ残して容量上限を偽りません
			if( size.QuadPart > K_LIMIT) {
				return false ;
			}
			// 追加前に固定名の一世代だけへ回転します
			if( size.QuadPart + static_cast<LONGLONG>( line.size()) > K_LIMIT) {
				file.reset() ;
				// 既存前世代も通常fileと確認した上で一件だけ置き換えます
				if( !MoveFileExW( path.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
					return false ;
				}
				file.reset( CreateFileW( path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &access, CREATE_NEW,
									   FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)) ;
				// 作成失敗でも前世代と本文は保持されます
				if( file.get() == INVALID_HANDLE_VALUE) {
					file.release() ;
					return false ;
				}
				validateFileHandle( file.get()) ;
			}
			LARGE_INTEGER end{} ; // file終端へのoffset0
			// 無効なseekを部分成功と扱いません
			if( !SetFilePointerEx( file.get(), end, nullptr, FILE_END)) {
				return false ;
			}
			writeBytes( file.get(), std::span<const std::uint8_t>( reinterpret_cast<const std::uint8_t *>( line.data()),
																 line.size())) ; // 同期byte境界、line存命中
			return FlushFileBuffers( file.get()) != FALSE ;
		} catch( ...) {
			return false ; // 限定ログだけのbest effort失敗。原文/補正の状態は変更しません
		}
	}

	private:
	/// sink設定だけの保存先、イベントへ含めません
	std::filesystem::path directory_ ;
} ;

/** @brief 一つの専用folderの排他と保存fileを所有します */
class FileHistoryStorage final : public IHistoryStorage {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 保存先を所有し、実際の準備はworkerのloadまで遅延します
	 * @param[in] directory 空または明示したlocal保存先
	 */
	//------------------------------------------------------------------------------
	explicit FileHistoryStorage( std::filesystem::path directory) : directory_( std::move( directory))
	{
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 保存先の排他を取得して履歴を読み込みます
	 * @return 保存fileがない場合は空履歴。それ以外は検証済みの所有結果
	 * @throws HistoryError 既存履歴は失敗時も変更しません
	 * @note 一つの専用workerが最初に一度だけ呼び出します
	 */
	//------------------------------------------------------------------------------
	HistorySnapshot load() override
	{
		security_ = std::make_unique<PrivateSecurity>() ;
		directory_ = prepareDirectory( directory_.empty() ? defaultDirectory() : directory_, *security_, true) ;
		const auto lockPath = directory_ / L"history.lock" ; // 固定名の排他専用file
		static_cast<void>( regularFileExists( lockPath)) ;
		auto access = security_->attributes() ; // OSが同期借用するprivate ACL
		lock_.reset( CreateFileW( lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, &access, OPEN_ALWAYS,
								FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)) ;
		// 同じ履歴の二重起動を防ぎ、別processの記録を空履歴で置き換えない
		if( lock_.get() == INVALID_HANDLE_VALUE) {
			const DWORD error = GetLastError() ; // 共有違反と権限・disk異常の区別
			throw HistoryError( error == ERROR_SHARING_VIOLATION ? HistoryErrorKind::BUSY : HistoryErrorKind::LOCATION,
							   error) ;
		}
		validateFileHandle( lock_.get()) ;
		dataPath_ = directory_ / L"history.bin" ;
		backupPath_ = directory_ / L"history.pre-v7.bin" ;
		oldBackupPath_ = directory_ / L"history.pre-v5.bin" ;
		HistorySnapshot snapshot ; // 未作成時には空の初期履歴
		bool			loadedV7 = false ; // 完全検証した形式6でだけ旧退避を回収する
		// 既存fileがある場合は全内容とSHA-256を検証してから復元する
		if( regularFileExists( dataPath_)) {
			const UniqueHandle file( CreateFileW( dataPath_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
												OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT,
												nullptr)) ; // 書込み・削除共有を許可しない読込み
			// 存在確認後のopen失敗を空履歴として返さない
			if( file.get() == INVALID_HANDLE_VALUE) {
				throw HistoryError( HistoryErrorKind::READ, GetLastError()) ;
			}
			validateFileHandle( file.get()) ;
			LARGE_INTEGER size{} ; // 読取り前に検証するfile長（byte）
			// 形式の最小headerとSHA-256を含まないfileを拒否する
			if( !GetFileSizeEx( file.get(), &size) || size.QuadPart < 56) {
				throw HistoryError( HistoryErrorKind::CORRUPT, GetLastError()) ;
			}
			// 内容をallocationする前にfile全体の上限を確認する
			if( static_cast<std::uint64_t>( size.QuadPart) > K_MAXIMUM_HISTORY_BYTES + 32) {
				throw HistoryError( HistoryErrorKind::TOO_LARGE) ;
			}
			std::vector<std::uint8_t> bytes( static_cast<std::size_t>( size.QuadPart)) ; // 有界な読込先
			std::size_t				  offset = 0 ; // 全byte数まで進む読取り位置
			// 途中EOFとI/O異常を区別しながら、固定blockで最後まで読む
			while( offset < bytes.size()) {
				// 今回の履歴読込みで要求する最大64KiBのbyte数
				const DWORD count = static_cast<DWORD>( std::min<std::size_t>( 65536, bytes.size() - offset)) ;
				DWORD		read = 0 ; // 今回の実読取りbyte数
				// 途中内容だけで履歴を復元しない
				if( !ReadFile( file.get(), bytes.data() + offset, count, &read, nullptr) || read == 0) {
					throw HistoryError( HistoryErrorKind::READ, GetLastError()) ;
				}
				offset += read ;
			}
			const auto body = std::span( bytes).first( bytes.size() - 32) ; // digestを除いた保存内容
			const auto expected = digest( body) ; // 保存内容から再計算したSHA-256
			// 壊れた内容を結果として表示せず、既存fileをそのまま残す
			if( !std::equal( expected.begin(), expected.end(), bytes.end() - 32)) {
				throw HistoryError( HistoryErrorKind::CORRUPT) ;
			}
			snapshot = decodeHistory( body) ;
			loadedV7 = (bytes[8] == 7 || bytes[8] == 8) && bytes[9] == 0 && bytes[10] == 0 && bytes[11] == 0 ;
			// 旧形式の実byteを最大512MiB＋digestだけ保持し、初回保存前の退避に使う
			if( !loadedV7) {
				legacyBytes_ = std::move( bytes) ;
			}
		}
		// 未移行の旧退避も読込み前のmetadataだけで有限容量を確認します
		if( !loadedV7) {
			validateOldBackupBound() ;
		}
		// 正本を完全検証した起動でだけ旧退避を回収し、破損時に自動復元しない
		if( loadedV7 && regularFileExists( backupPath_) && !DeleteFileW( backupPath_.c_str())) {
			cleanupError_ = GetLastError() ; // 回収失敗でも検証済み正本を利用可能にする
		}
		// 旧pre-v5も完全検証済みV6のload後だけ回収します
		if( loadedV7 && regularFileExists( oldBackupPath_) && !DeleteFileW( oldBackupPath_.c_str())) {
			cleanupError_ = GetLastError() ;
		}
		// pre-v6もverified V7のload後だけ回収します
		if( loadedV7 && regularFileExists( directory_ / L"history.pre-v6.bin") &&
		   !DeleteFileW( (directory_ / L"history.pre-v6.bin").c_str())) {
			cleanupError_ = GetLastError() ;
		}
		cleanupPartials() ; // 本fileを検証してから、自分の未完了一時fileを回収する
		loaded_ = true ;
		bounded_ = std::make_unique<WholeTextPersistenceStore>(
			directory_,
			//------------------------------------------------------------------------------
			/**
			 * @brief 同じworkerのatomic writerを使います
			 * @param[in] value 候補
			 * @param[out] receipt 確定値
			 */
			//------------------------------------------------------------------------------
			[this]( const HistorySnapshot &value, HistorySaveReceipt &receipt)
			{
				writeSnapshot( value, &receipt) ;
			}) ;
		bounded_->load( snapshot) ;
		// sidecar破損時もV7原文とlocal補正は利用可能なまま保持します
		try {
			loadApiText( snapshot) ;
		} catch( const HistoryError &) {
			snapshot.apiSidecarCorrupt = true ;
		}
		ApiCorrectionEventLog log( directory_) ; // 検証済み保存出所だけを復元記録
		// 投影破損はrawの読込みと分離し、通常保存で上書きしない
		try {
			// 固定総量内の所有符号化byte
			const auto bytes = loadApiMetadata( L"history.speaker-text-v1.bin", K_SPEAKER_TEXT_STORE_BYTES) ;
			// 専用fileがある場合だけ投影codecを読み、原文から隔離する
			if( !bytes.empty()) {
				decodeSpeakerTextStore( bytes, snapshot) ;
			}
		} catch( const HistoryError &) {
			snapshot.speakerTextCorrupt = true ;
		}
		// 最大32履歴の最新候補/前採用を別々に記録し、再送しません
		for( const auto &entry : snapshot.entries) {
			// API未使用の履歴にはログfileを新規作成しません
			const std::array attempts{entry.apiText.latest, entry.apiText.adopted} ; // 保存identityを比較
			// 採用と最新が別試行のときだけ二件を残します
			for( std::size_t i = 0; i < attempts.size(); ++i) {
				const auto &attempt = attempts[i] ; // 今回の保存済み試行
				// 再読でownerが分かれても同試行を二重に記録しません
				if( !attempt || (i == 1 && sameApiTextAttempt( attempt, attempts[0]))) {
					continue ;
				}
				ApiCorrectionEventData event ; // 本文を渡せない型
				event.event = ApiCorrectionEvent::RESTORED ;
				event.history = entry.id ;
				event.operation = attempt->operationId ;
				event.provenance = apiTextProvenance( *attempt) ;
				// 再読でも保存済みの末尾HTTPと固定診断だけを記録します
				if( !attempt->results.empty()) {
					const auto &last = attempt->results.back() ; // 保存済み有限応答
					event.httpStatus = last.httpStatus ;
					event.code = last.code ;
					event.httpError = last.httpError ;
				}
				event.saved = true ;
				event.adopted = sameApiTextAttempt( attempt, entry.apiText.adopted) ;
				static_cast<void>( log.append( event)) ;
			}
		}
		return snapshot ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 前の有効な保存を保持しながら履歴を置き換えます
	 * @param[in] snapshot 呼出し中だけ有効な所有結果のborrow
	 * @throws HistoryError 失敗時は前の有効fileを保持します
	 * @pre 同じworkerでloadに成功していること
	 */
	//------------------------------------------------------------------------------
	void save( const HistorySnapshot &snapshot) override
	{
		// 普通saveも同じquotaと確定後cleanupへ通します
		if( bounded_) {
			bounded_->checkOrdinary( snapshot) ;
		}
		writeSnapshot( snapshot, nullptr) ;
		// API使用済みfolderの通常削除も同じworkerでsidecarへ反映します
		try {
			// V7は既に確定済みであり、後半失敗を旧原文へrollbackしません
			if( !snapshot.apiSidecarCorrupt && regularFileExists( directory_ / L"api-text-corrections.bin")) {
				saveApiText( snapshot) ;
			}
			// 削除/source更新の派生cleanupも同じworkerで確定し、失敗を通知する
			if( !snapshot.speakerTextCorrupt && regularFileExists( directory_ / L"history.speaker-text-v1.bin")) {
				// 専用sidecarの原子置換事実
				HistorySaveReceipt projectionReceipt ;
				saveSpeakerText( snapshot, {}, projectionReceipt) ;
			}
		} catch( const HistoryError &error) {
			throw HistoryError( error.kind(), error.code(), true) ; // main commit済みの失敗
		} catch( ...) {
			throw HistoryError( HistoryErrorKind::WRITE, ERROR_INTERNAL_ERROR, true) ;
		}
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( bounded_) {
			bounded_->ordinaryCommitted( snapshot) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 音声API終端だけを取消しcheckpoint付きで保存します
	 * @param[in] snapshot 同じwriterの候補
	 * @param[in] stop 完了置換前の取消し
	 * @param[out] receipt 原子置換直後の事実
	 * @note 既存local/文章保存はsaveのままです
	 */
	//------------------------------------------------------------------------------
	void saveAudio( const HistorySnapshot &snapshot, std::stop_token stop, HistorySaveReceipt &receipt) override
	{
		// 新音声だけが取消しcheckpointを持ち、旧sidecarの本文/digestを再生成しません
		if( bounded_) {
			bounded_->checkOrdinary( snapshot) ;
		}
		writeSnapshot( snapshot, &receipt, stop) ;
		if( bounded_) {
			bounded_->ordinaryCommitted( snapshot) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じwriter/lockで独立投影だけを原子保存します
	 * @param[in] snapshot durable候補
	 * @param[in] stop 置換直前取消し
	 * @param[out] receipt 原子置換直後の事実
	 */
	//------------------------------------------------------------------------------
	void saveSpeakerText( const HistorySnapshot &snapshot, const std::stop_token stop,
						 HistorySaveReceipt &receipt) override
	{
		// 未読込/破損派生fileを通常保存で無言上書きしない
		if( !loaded_ || snapshot.speakerTextCorrupt) {
			throw HistoryError( HistoryErrorKind::WRITE, ERROR_CRC) ;
		}
		saveApiMetadata( encodeSpeakerTextStore( snapshot), L"history.speaker-text-v1.bin", stop, &receipt) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じworker/lockでAPI sidecarだけを保存します
	 * @param[in] snapshot 同じ原文正本
	 */
	//------------------------------------------------------------------------------
	void saveApiText( const HistorySnapshot &snapshot) override
	{
		// 原文を読めない保存先や破損sidecarを上書きしません
		if( !loaded_ || snapshot.apiSidecarCorrupt) {
			throw HistoryError( HistoryErrorKind::WRITE) ;
		}
		const auto bytes = encodeApiTextStore( snapshot) ; // key/pathを含まない専用形式
		// 新規Pendingでは最悪の候補/部分本文分もHTTP前に容量予約します
		if( std::any_of( snapshot.entries.begin(), snapshot.entries.end(),
					   //------------------------------------------------------------------------------
					   /**
					    * @brief 未送信予約を検出します
					    * @param[in] e 記録
					    * @return Pending
					    */
					   //------------------------------------------------------------------------------
					   []( const auto &e)
					   {
						   return e.apiText.latest && e.apiText.latest->results.empty() &&
								  e.apiText.latest->state == ApiTextState::PENDING ;
					   }) &&
		   bytes.size() > K_API_TEXT_STORE_LIMIT - 32 * 1024 * 1024) {
			throw HistoryError( HistoryErrorKind::TOO_LARGE) ;
		}
		const auto target = directory_ / L"api-text-corrections.bin" ; // 固定名だけを使用
		static_cast<void>( prepareDirectory( directory_, *security_, false)) ;
		static_cast<void>( regularFileExists( target)) ;
		std::array<std::uint8_t, 12> random{} ; // 96bitの衝突しにくい一時名
		checkCrypto( BCryptGenRandom( nullptr, random.data(), static_cast<ULONG>( random.size()),
									BCRYPT_USE_SYSTEM_PREFERRED_RNG)) ;
		std::wstring name( K_PARTIAL_PREFIX) ; // 既存cleanup対象の自分のpartialだけ
		// 乱数をpath展開しないASCII hexへ変換します
		for( const auto value : random) {
			name += K_HEX[value >> 4] ;
			name += K_HEX[value & 15] ;
		}
		const auto	 temporary = directory_ / name ; // 同一private folder
		auto		 access = security_->attributes() ; // 現在ユーザーのprivate ACL
		UniqueHandle file( CreateFileW( temporary.c_str(), GENERIC_WRITE, 0, &access, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
									  nullptr)) ; // 既存fileを切り詰めません
		// 一時fileを安全に取得できなければ旧sidecarを維持します
		if( file.get() == INVALID_HANDLE_VALUE) {
			throw HistoryError( HistoryErrorKind::WRITE, GetLastError()) ;
		}
		try {
			writeBytes( file.get(), bytes) ;
			// flushに成功した後だけ正本へ原子置換します
			if( !FlushFileBuffers( file.get())) {
				throw HistoryError( HistoryErrorKind::WRITE, GetLastError()) ;
			}
			file.reset() ;
			static_cast<void>( prepareDirectory( directory_, *security_, false)) ;
			static_cast<void>( regularFileExists( target)) ;
			// redirect/reparse先へ書きません
			if( !MoveFileExW( temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
				throw HistoryError( HistoryErrorKind::WRITE, GetLastError()) ;
			}
		} catch( ...) {
			file.reset() ;
			static_cast<void>( DeleteFileW( temporary.c_str())) ;
			throw ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じlock/workerでsidecarを読みます
	 * @param[in,out] snapshot 検証済みV7
	 */
	//------------------------------------------------------------------------------
	void loadApiText( HistorySnapshot &snapshot)
	{
		const auto path = directory_ / L"api-text-corrections.bin" ; // 固定target
		// API未使用時はsidecarを作りません
		if( !regularFileExists( path)) {
			return ;
		}
		UniqueHandle file( CreateFileW( path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
									  FILE_FLAG_OPEN_REPARSE_POINT,
									  nullptr)) ; // 通常fileだけ
		// open失敗を空sidecarと扱いません
		if( file.get() == INVALID_HANDLE_VALUE) {
			throw HistoryError( HistoryErrorKind::READ, GetLastError()) ;
		}
		validateFileHandle( file.get()) ;
		LARGE_INTEGER size{} ; // 読取前の有界容量
		// allocation前に最大128MiBと最小digest長を検証します
		if( !GetFileSizeEx( file.get(), &size) || size.QuadPart < 34 ||
		   static_cast<std::uint64_t>( size.QuadPart) > K_API_TEXT_STORE_LIMIT) {
			throw HistoryError( HistoryErrorKind::CORRUPT) ;
		}
		std::vector<std::uint8_t> bytes( static_cast<std::size_t>( size.QuadPart)) ; // 有界読取先
		std::size_t				  offset = 0 ; // 実読取byte
		// 部分読取を扱い、EOFまたは全量で終了します
		while( offset < bytes.size()) {
			DWORD count = 0 ; // 今回の実読取数
			// 途中EOFを部分sidecarとして採用しません
			if( !ReadFile( file.get(), bytes.data() + offset,
						 static_cast<DWORD>( std::min<std::size_t>( 65536, bytes.size() - offset)), &count, nullptr) ||
			   count == 0) {
				throw HistoryError( HistoryErrorKind::READ, GetLastError()) ;
			}
			offset += count ;
		}
		decodeApiTextStore( bytes, snapshot) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief metadata専用cacheを書きます
	 * @param[in] catalog 検証済み値
	 */
	//------------------------------------------------------------------------------
	void saveCatalog( const ApiModelCatalog &catalog) override
	{
		// 既存storageのloadと排他取得成功が必須です
		if( !loaded_) {
			throw HistoryError( HistoryErrorKind::WRITE) ;
		}
		saveApiMetadata( encodeApiCatalog( catalog), L"api-catalog-v1.bin") ;
		saveApiMetadata( encodeApiSettings( catalog.settings), L"api-settings-v1.bin") ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定metadataを同じprivate履歴folder内の一時fileへ書き、flush後に原子的に置換します
	 * @param[in] bytes 呼出し中だけ借用する検証済み全metadata byte列
	 * @param[in] fixedName 内部固定のNUL終端UTF-16 file名。非nullで、directory_内の保存先名だけを指定します
	 * @param[in] stop flush後・原子置換直前に照合する協調取消しtoken
	 * @param[in,out] receipt 任意の保存事実出力。null可で、置換成功直後にだけcommitted=trueへ変更します。置換前失敗では変更しません
	 * @throws HistoryError 作成/書込/flush/置換失敗。取消しはcheckCancelledのOperationErrorで伝えます
	 * @note 失敗時は今回の一時fileをcleanupし、旧有効fileを保持します。同じ履歴workerから呼び、任意名や本文を生成しません
	 */
	//------------------------------------------------------------------------------
	void saveApiMetadata( const std::vector<std::uint8_t> &bytes, const wchar_t *fixedName,
						 const std::stop_token stop = {}, HistorySaveReceipt *receipt = nullptr)
	{
		const auto target = directory_ / fixedName ; // callerは固定名だけ
		static_cast<void>( prepareDirectory( directory_, *security_, false)) ;
		static_cast<void>( regularFileExists( target)) ;
		std::array<std::uint8_t, 12> random{} ; // 96bitの衝突しにくい一時名
		checkCrypto( BCryptGenRandom( nullptr, random.data(), static_cast<ULONG>( random.size()),
									BCRYPT_USE_SYSTEM_PREFERRED_RNG)) ;
		std::wstring name( K_PARTIAL_PREFIX) ; // 既存cleanup対象の自分のpartialだけ
		// 乱数をpath展開しないASCII hexへ変換します
		for( const auto value : random) {
			name += K_HEX[value >> 4] ;
			name += K_HEX[value & 15] ;
		}
		const auto	 temporary = directory_ / name ; // 同一private folder
		auto		 access = security_->attributes() ; // 現在ユーザーのprivate ACL
		UniqueHandle file( CreateFileW( temporary.c_str(), GENERIC_WRITE, 0, &access, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
									  nullptr)) ; // 既存fileを切り詰めません
		// 一時fileを安全に取得できなければ旧sidecarを維持します
		if( file.get() == INVALID_HANDLE_VALUE) {
			throw HistoryError( HistoryErrorKind::WRITE, GetLastError()) ;
		}
		try {
			writeBytes( file.get(), bytes) ;
			// flushに成功した後だけ正本へ原子置換します
			if( !FlushFileBuffers( file.get())) {
				throw HistoryError( HistoryErrorKind::WRITE, GetLastError()) ;
			}
			file.reset() ;
			static_cast<void>( prepareDirectory( directory_, *security_, false)) ;
			static_cast<void>( regularFileExists( target)) ;
			// redirect/reparse先へ書きません
			checkCancelled( stop) ; // 投影採用の線形化点はこの後の原子置換
			if( !MoveFileExW( temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
				throw HistoryError( HistoryErrorKind::WRITE, GetLastError()) ;
			}
			// 原子置換直後の事実だけを呼出し元へ返す
			if( receipt) {
				receipt->committed = true ;
			}
		} catch( ...) {
			file.reset() ;
			static_cast<void>( DeleteFileW( temporary.c_str())) ;
			throw ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ排他でmetadata cacheを読みます
	 * @return 未取得は空
	 */
	//------------------------------------------------------------------------------
	ApiModelCatalog loadCatalog() override
	{
		const auto bytes = loadApiMetadata( L"api-catalog-v1.bin", K_API_CATALOG_STORE_LIMIT) ;
		auto	   result = bytes.empty() ? ApiModelCatalog{} : decodeApiCatalog( bytes) ; // 既存cache
		const auto settings = loadApiMetadata( L"api-settings-v1.bin", 256) ; // 秘密と独立
		// 未使用時だけ既定値を維持します
		if( !settings.empty()) {
			result.settings = decodeApiSettings( settings) ;
		}
		return result ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 自分の固定metadataを有界に読みます
	 * @param[in] name 内部固定名
	 * @param[in] limit byte上限
	 * @return 未作成なら空、正常時全byte
	 */
	//------------------------------------------------------------------------------
	std::vector<std::uint8_t> loadApiMetadata( const wchar_t *name, std::size_t limit)
	{
		const auto path = directory_ / name ; // 固定target
		// API未使用時はsidecarを作りません
		if( !regularFileExists( path)) {
			return {} ;
		}
		UniqueHandle file( CreateFileW( path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
									  FILE_FLAG_OPEN_REPARSE_POINT,
									  nullptr)) ; // 通常fileだけ
		// open失敗を空sidecarと扱いません
		if( file.get() == INVALID_HANDLE_VALUE) {
			throw HistoryError( HistoryErrorKind::READ, GetLastError()) ;
		}
		validateFileHandle( file.get()) ;
		LARGE_INTEGER size{} ; // 読取前の有界容量
		// allocation前に最大128MiBと最小digest長を検証します
		if( !GetFileSizeEx( file.get(), &size) || size.QuadPart < 34 ||
		   static_cast<std::uint64_t>( size.QuadPart) > limit) {
			throw HistoryError( HistoryErrorKind::CORRUPT) ;
		}
		std::vector<std::uint8_t> bytes( static_cast<std::size_t>( size.QuadPart)) ; // 有界読取先
		std::size_t				  offset = 0 ; // 実読取byte
		// 部分読取を扱い、EOFまたは全量で終了します
		while( offset < bytes.size()) {
			DWORD count = 0 ; // 今回の実読取数
			// 途中EOFを部分sidecarとして採用しません
			if( !ReadFile( file.get(), bytes.data() + offset,
						 static_cast<DWORD>( std::min<std::size_t>( 65536, bytes.size() - offset)), &count, nullptr) ||
			   count == 0) {
				throw HistoryError( HistoryErrorKind::READ, GetLastError()) ;
			}
			offset += count ;
		}
		return bytes ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 生成前のPendingとquotaを耐久予約します
	 * @param[in,out] durable workerの正本
	 * @param[in] request 固定要求
	 * @param[in] stop 協調停止
	 * @return 一回予約。未対応fakeは拒否します
	 */
	//------------------------------------------------------------------------------
	WholeTextBoundedStorageBudget reserveBounded( HistorySnapshot &durable, const WholeTextCorrectionJobRequest &request,
												 std::stop_token stop) override
	{
		return bounded_->reserve( durable, request, stop) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同一terminalの保存を原子的に確定します
	 * @param[in,out] durable 正本
	 * @param[in,out] terminal 失敗時も非消費
	 * @param[out] receipt 置換直後のscalar
	 * @param[in] stop precommit停止
	 * @return 所有ack。保存前例外では旧結果を保持します
	 */
	//------------------------------------------------------------------------------
	std::shared_ptr<const WholeTextBoundedPersistenceCompletion> persistBounded( HistorySnapshot			 &durable,
																				WholeTextBoundedTerminal &terminal,
																				HistorySaveReceipt		 &receipt,
																				std::stop_token			  stop) override
	{
		return bounded_->persist( durable, terminal, receipt, stop) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 事前確保した未保存prefix ackを返します
	 * @param[in] kind 保存失敗分類
	 * @param[in] code 元code
	 * @return allocationなしのack/null
	 */
	//------------------------------------------------------------------------------
	std::shared_ptr<const WholeTextBoundedPersistenceCompletion> boundedFailure( HistoryErrorKind kind,
																				std::uint32_t	 code) noexcept override
	{
		return bounded_->failure( kind, code) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 指定した未完試行の参照除去を保存してから、同じ履歴IDの未参照な既知ownerをcleanupします
	 * @param[in,out] durable 履歴workerが借用させる正本snapshotです。候補のmetadata置換確認後に更新し、前の有効全文は保持します
	 * @param[in] source 対象entryのhistoryIdと原文SHAを持つ借用keyです。両方を正本の原文へ照合します
	 * @param[in] op 破棄対象の生成operationIdです。同じsourceの未保存予約、または進行中でない未完latestのIDへ一致させます
	 * @param[out] receipt 今回用に既定初期化する保存証拠です。metadata置換直後にcommitted、storageRevision、historySha256を確定します
	 * @return metadataCommittedは参照除去の保存、cleanupCompleteは対象となった既知ownerの削除確認です
	 * cleanup失敗時は元codeと未解放chargeのbyte数を返し、metadata確定や有効全文を巻き戻しません
	 * @throws HistoryError source/試行状態不一致、metadata保存、または資源計数の失敗。確保失敗等も伝播します
	 * @pre load済みのstorageを所有する履歴workerから呼び、receiptを今回用の既定値に初期化します
	 * @note 保存前の拒否/失敗ではdurableとreceiptを変更しません。例外時もreceipt.committedを確認し、確定後のdurableを未保存扱いへ戻しません
	 * @see koefude::IHistoryStorage::discardBounded
	 */
	//------------------------------------------------------------------------------
	WholeTextBoundedCleanupResult discardBounded( HistorySnapshot &durable, const ReadingSourceKey &source,
												 std::uint64_t op, HistorySaveReceipt &receipt) override
	{
		return bounded_->discard( durable, source, op, receipt) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief atomic成功時だけreceiptを確定します
	 * @param[in] snapshot 候補
	 * @param[out] receipt nullは普通保存。成功直後にnoexcept設定
	 * @param[in] audioStop 新音声の完了置換直前だけで確認する取消し。既存呼出しは空
	 */
	//------------------------------------------------------------------------------
	void writeSnapshot( const HistorySnapshot &snapshot, HistorySaveReceipt *receipt, std::stop_token audioStop = {})
	{
		// 読めなかった履歴や排他のない保存先を上書きしない
		if( !loaded_ || !lock_ || lock_.get() == INVALID_HANDLE_VALUE) {
			throw HistoryError( HistoryErrorKind::WRITE) ;
		}
		static_cast<void>( prepareDirectory( directory_, *security_, false)) ;
		static_cast<void>( regularFileExists( dataPath_)) ;
		const auto bytes = encodeHistory( snapshot) ; // I/O前に全体を検証する所有byte列
		const auto checksum = digest( bytes) ; // 内容の破損検出用SHA-256
		// 明示削除の空履歴は退避も先に回収し、削除済み内容を残さない
		if( snapshot.entries.empty()) {
			// 明示clearでは旧退避からの復活も禁止します
			if( regularFileExists( oldBackupPath_) && !DeleteFileW( oldBackupPath_.c_str())) {
				throw HistoryError( HistoryErrorKind::WRITE, GetLastError()) ;
			}
			// 全削除後の本文を退避履歴へ残さず、退避の削除失敗も保存失敗として返す
			if( regularFileExists( backupPath_) && !DeleteFileW( backupPath_.c_str())) {
				throw HistoryError( HistoryErrorKind::WRITE, GetLastError()) ;
			}
		}
		// 旧形式の初回置換は全byte照合したprivate退避の成功後にだけ行う
		else {
			validateOldBackupBound() ;
			// 旧正本がある初回保存だけで全byte退避を作成・照合します
			if( !legacyBytes_.empty()) {
				preserveLegacy() ;
			}
		}
		std::array<std::uint8_t, 12> random{} ; // 衝突を避ける96bitのOS乱数
		checkCrypto( BCryptGenRandom( nullptr, random.data(), static_cast<ULONG>( random.size()),
									BCRYPT_USE_SYSTEM_PREFERRED_RNG)) ;
		std::wstring name( K_PARTIAL_PREFIX) ; // 元音声名を使わないアプリ専用の一時名
		// 全乱数byteを安全なfile名文字へ変換する
		for( const std::uint8_t value : random) {
			name += K_HEX[value >> 4U] ;
			name += K_HEX[value & 15U] ;
		}
		const auto	 temporary = directory_ / name ; // 同一folder内の新規一時file
		auto		 access = security_->attributes() ; // 新規fileへ必ず付けるprivate ACL
		UniqueHandle file(
			CreateFileW( temporary.c_str(), GENERIC_WRITE, 0, &access, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)) ;
		// 一時名が衝突した場合も既存fileを切り詰めない
		if( file.get() == INVALID_HANDLE_VALUE) {
			throw HistoryError( HistoryErrorKind::WRITE, GetLastError()) ;
		}
		try {
			writeBytes( file.get(), bytes) ;
			writeBytes( file.get(), checksum) ;
			// 全内容のflush成功前に本fileの置換を開始しない
			if( !FlushFileBuffers( file.get())) {
				throw HistoryError( HistoryErrorKind::WRITE, GetLastError()) ;
			}
			file.reset() ;
			// 置換直前にもreparse・同期属性の差替えを拒否する
			static_cast<void>( prepareDirectory( directory_, *security_, false)) ;
			static_cast<void>( regularFileExists( dataPath_)) ;
			// 音声API完了だけの線形化点。取消しが先なら旧正本を保持し、置換後は完了優先
			checkCancelled( audioStop) ;
			// 同じdirectory内で一度に置き換え、前の有効履歴を部分書込みしない
			if( !MoveFileExW( temporary.c_str(), dataPath_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
				throw HistoryError( HistoryErrorKind::WRITE, GetLastError()) ;
			}
			// 原子置換の直後はcallbackやallocationより先にreceiptを固定します
			if( receipt) {
				receipt->committed = true ;
				receipt->storageRevision = ++storageRevision_ ;
				receipt->historySha256 = checksum ;
			}
			std::vector<std::uint8_t>().swap( legacyBytes_) ; // 退避用の旧byte所有領域を解放する
		} catch( ...) {
			file.reset() ;
			// 自分が作った一時名だけを削除し、消せなければ次回起動でも回収を試す
			if( !DeleteFileW( temporary.c_str())) {
				cleanupError_ = GetLastError() ;
			}
			throw ; // cleanupで元の保存失敗の診断を覆い隠さない
		}
	}

	private:
	//------------------------------------------------------------------------------
	/**
	 * @brief 保持中の旧pre-v5退避をallocationせず上限確認します
	 */
	//------------------------------------------------------------------------------
	void validateOldBackupBound() const
	{
		// 未作成の旧退避を新しく作成したり探索したりしません
		if( !regularFileExists( oldBackupPath_)) {
			return ;
		}
		const UniqueHandle backup( CreateFileW( oldBackupPath_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
											  OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT,
											  nullptr)) ; // 同期照会の間だけ所有する読取りhandle
		// 上限を確認できない旧退避を残したまま移行を開始しません
		if( backup.get() == INVALID_HANDLE_VALUE) {
			throw HistoryError( HistoryErrorKind::READ, GetLastError()) ;
		}
		validateFileHandle( backup.get()) ;
		LARGE_INTEGER size{} ; // digestを含む旧退避の論理byte数
		// OS照会の失敗を容量0として受け入れません
		if( !GetFileSizeEx( backup.get(), &size) || size.QuadPart < 0) {
			throw HistoryError( HistoryErrorKind::READ, GetLastError()) ;
		}
		// 正本と同じ512MiB body＋32byte digestの上限を保持中も維持します
		if( static_cast<std::uint64_t>( size.QuadPart) > K_MAXIMUM_HISTORY_BYTES + 32) {
			throw HistoryError( HistoryErrorKind::TOO_LARGE) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 旧履歴の全byteと同じprivate退避を最大1件だけ作成します
	 */
	//------------------------------------------------------------------------------
	void preserveLegacy()
	{
		// 既存退避は黙って上書きせず、同じ旧履歴である場合だけ再利用する
		if( !regularFileExists( backupPath_)) {
			auto		 access = security_->attributes() ; // 正本と同じ現ユーザー専用ACL
			UniqueHandle backup( CreateFileW( backupPath_.c_str(), GENERIC_WRITE, 0, &access, CREATE_NEW,
											FILE_ATTRIBUTE_NORMAL, nullptr)) ;
			// 未使用の固定名を取得できない場合は元の正本を変更しない
			if( backup.get() == INVALID_HANDLE_VALUE) {
				throw HistoryError( HistoryErrorKind::WRITE, GetLastError()) ;
			}
			try {
				writeBytes( backup.get(), legacyBytes_) ;
				// 完全な退避のflush成功後にだけ正本の移行を許可する
				if( !FlushFileBuffers( backup.get())) {
					throw HistoryError( HistoryErrorKind::WRITE, GetLastError()) ;
				}
				backup.reset() ;
			} catch( ...) {
				backup.reset() ;
				// CREATE_NEWで自分が作った失敗退避だけを回収し、元の失敗を再送する
				if( !DeleteFileW( backupPath_.c_str())) {
					cleanupError_ = GetLastError() ;
				}
				throw ;
			}
		}
		const UniqueHandle backup( CreateFileW( backupPath_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
											  OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT,
											  nullptr)) ; // 検証用borrow
		// 既存退避のopen失敗は、旧履歴の移行失敗として返す
		if( backup.get() == INVALID_HANDLE_VALUE) {
			throw HistoryError( HistoryErrorKind::WRITE, GetLastError()) ;
		}
		validateFileHandle( backup.get()) ;
		LARGE_INTEGER size{} ; // 退避全体の検証対象byte数
		// 保存時点の検証済み旧履歴と長さが違う退避を再利用しない
		if( !GetFileSizeEx( backup.get(), &size) || size.QuadPart < 0 ||
		   static_cast<std::uint64_t>( size.QuadPart) != legacyBytes_.size()) {
			throw HistoryError( HistoryErrorKind::WRITE, GetLastError()) ;
		}
		std::array<std::uint8_t, 65536> block{} ; // 固定64KiBの再読取りbuffer
		std::size_t						offset = 0 ; // 照合し終えた旧byte数
		// 一時allocationを増やさず、digestを含む全byteを照合する
		while( offset < legacyBytes_.size()) {
			// 旧履歴の残量と照合buffer容量の小さい方の読取りbyte数
			const auto count = static_cast<DWORD>( std::min( block.size(), legacyBytes_.size() - offset)) ;
			DWORD	   read = 0 ; // 今回の読取りbyte数
			// 部分EOF・I/O失敗・内容の相違は正本の置換前に拒否する
			if( !ReadFile( backup.get(), block.data(), count, &read, nullptr) || read == 0 ||
			   !std::equal( block.begin(), block.begin() + read, legacyBytes_.begin() + offset)) {
				throw HistoryError( HistoryErrorKind::WRITE, GetLastError()) ;
			}
			offset += read ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 前回の中断で残った専用一時fileを最大64件だけ回収します
	 */
	//------------------------------------------------------------------------------
	void cleanupPartials()
	{
		WIN32_FIND_DATAW entry{} ; // 検証するdirectory entry
		const auto		 pattern = directory_ / L"history.partial-*" ; // 自分の一時名だけの検索
		// この同期境界で所有する値、または確定状態の検証値です
		const HANDLE	 search = FindFirstFileW( pattern.c_str(), &entry) ; // 専用APIで閉じる検索handle
		// 一時fileがなければ履歴本体の読込み成功をそのまま返す
		if( search == INVALID_HANDLE_VALUE) {
			return ;
		}
		const std::unique_ptr<void, decltype(&FindClose)> owned( search, FindClose) ;
		// 過大なdirectoryでも最大64件に制限し、未知名・directory・linkは削除しない
		for( std::size_t index = 0; index < 64; ++index) {
			const std::wstring_view name( entry.cFileName) ; // 検索結果buffer内の借用名
			const auto				isHex =
				//------------------------------------------------------------------------------
				/**
				 * @brief 一時名が使う小文字hexだけを許可します
				 * @param[in] character 検証中のfile名文字
				 * @return OS乱数のhex表記に適合する場合true
				 */
				//------------------------------------------------------------------------------
				[]( const wchar_t character)
				{
					return K_HEX.find( character) != std::wstring_view::npos ;
				} ;
			// 予約した名前の通常fileだけを、列挙したfolderの中で回収する
			if( name.size() == K_PARTIAL_PREFIX.size() + 24 && name.starts_with( K_PARTIAL_PREFIX) &&
			   std::all_of( name.begin() + static_cast<std::ptrdiff_t>( K_PARTIAL_PREFIX.size()), name.end(), isHex) &&
			   (entry.dwFileAttributes & (K_REJECTED_ATTRIBUTES | FILE_ATTRIBUTE_DIRECTORY)) == 0) {
				// 一時fileの回収失敗では、読み込めた本履歴の利用を妨げない
				if( !DeleteFileW( (directory_ / name).c_str())) {
					cleanupError_ = GetLastError() ;
				}
			}
			// 検索終端またはOSの列挙停止で有界loopを抜ける
			if( !FindNextFileW( search, &entry)) {
				break ;
			}
		}
	}
	/// UI revisionとは別の確定番号
	std::uint64_t							   storageRevision_ = 0 ;
	/// 値として受け取り、load時に確定する専用保存先
	std::filesystem::path					   directory_ ;
	/// 専用folder内の固定本file名
	std::filesystem::path					   dataPath_ ;
	/// 形式6移行だけの固定退避名
	std::filesystem::path					   backupPath_ ;
	/// 移行中に上書きしない旧退避名
	std::filesystem::path					   oldBackupPath_ ;
	/// 初回保存成功まで保持する検証済み旧file。上限512MiB＋32byte
	std::vector<std::uint8_t>				   legacyBytes_ ;
	/// 新規fileへ付ける現ユーザー専用権限
	std::unique_ptr<PrivateSecurity>		   security_ ;
	/// storage寿命を通して保持する排他
	UniqueHandle							   lock_ ;
	/// lock解放前に同workerでclose
	std::unique_ptr<WholeTextPersistenceStore> bounded_ ;
	/// 既存履歴の検証成功時だけ保存を許可する
	bool									   loaded_ = false ;
	/// 一時file回収失敗の診断。次回起動で再試行する
	DWORD									   cleanupError_ = ERROR_SUCCESS ;
} ;
/** @brief 履歴/APIの正本へ触れず専用lockで選好だけを保存します */
class FileTranscriptionSettingsStorage final : public ITranscriptionSettingsStorage {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 保存先を値として保持します
	 * @param[in] directory 空はユーザー専用既定
	 */
	//------------------------------------------------------------------------------
	explicit FileTranscriptionSettingsStorage( std::filesystem::path directory) : directory_( std::move( directory))
	{
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 一度だけ読込み/lockを行います
	 * @return missingは従前既定
	 * @throws HistoryError 破損/読込み/lock失敗。原fileを変更しません
	 */
	//------------------------------------------------------------------------------
	TranscriptionPreferences load() override
	{
		security_ = std::make_unique<PrivateSecurity>() ; // 新規folder/fileにprivate ACL
		directory_ = prepareDirectory( directory_.empty() ? defaultDirectory().parent_path() / L"settings" : directory_,
									  *security_, true) ;
		auto	   access = security_->attributes() ; // OS同期borrow
		const auto lockPath = directory_ / L"transcription-settings.lock" ; // 他用途とは別lock
		static_cast<void>( regularFileExists( lockPath)) ;
		lock_.reset( CreateFileW( lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, &access, OPEN_ALWAYS,
								FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)) ;
		// 多重writerを待機/上書きへ変えず起動失敗として返します
		if( lock_.get() == INVALID_HANDLE_VALUE) {
			const auto code = GetLastError() ; // lock競合とアクセス失敗を区別
			throw HistoryError( code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION ? HistoryErrorKind::BUSY
																							   : HistoryErrorKind::READ,
							   code) ;
		}
		validateFileHandle( lock_.get()) ;
		target_ = directory_ / L"transcription-settings-v1.bin" ;
		TranscriptionPreferences value ; // 初回だけ従前既定
		// 既存正本は全値が検証できた場合だけ採用します
		if( regularFileExists( target_)) {
			UniqueHandle file( CreateFileW( target_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
										  FILE_FLAG_OPEN_REPARSE_POINT, nullptr)) ;
			// 読込拒否をmissingへ縮小しません
			if( file.get() == INVALID_HANDLE_VALUE) {
				throw HistoryError( HistoryErrorKind::READ, GetLastError()) ;
			}
			validateFileHandle( file.get()) ;
			LARGE_INTEGER size{} ; // 固定byte長だけを受理
			// 不正長をallocation前に拒否します
			if( !GetFileSizeEx( file.get(), &size) || (size.QuadPart != 48 && size.QuadPart != 49)) {
				throw HistoryError( HistoryErrorKind::CORRUPT) ;
			}
			std::vector<std::uint8_t> bytes( static_cast<std::size_t>( size.QuadPart)) ; // v1/v2固定長
			DWORD					  count = 0 ; // 実読取長
			// 小さい固定fileも部分読取を採用しません
			if( !ReadFile( file.get(), bytes.data(), static_cast<DWORD>( bytes.size()), &count, nullptr) ||
			   count != bytes.size()) {
				throw HistoryError( HistoryErrorKind::READ, GetLastError()) ;
			}
			value = decodeTranscriptionSettings( bytes) ;
		}
		loaded_ = true ;
		return value ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 原子保存とflushを完了します
	 * @param[in] value 検証済み選好
	 * @throws HistoryError 保存失敗。置換前なら旧fileを維持します
	 */
	//------------------------------------------------------------------------------
	void save( const TranscriptionPreferences &value) override
	{
		// load失敗後の自動上書きを拒否し、原fileを保護します
		if( !loaded_) {
			throw HistoryError( HistoryErrorKind::READ) ;
		}
		const auto bytes = encodeTranscriptionSettings( value) ; // 検証後の固定49byte
		static_cast<void>( prepareDirectory( directory_, *security_, false)) ;
		static_cast<void>( regularFileExists( target_)) ;
		std::array<std::uint8_t, 12> random{} ; // 自分の一時fileだけを解放
		checkCrypto( BCryptGenRandom( nullptr, random.data(), static_cast<ULONG>( random.size()),
									BCRYPT_USE_SYSTEM_PREFERRED_RNG)) ;
		std::wstring name = L"transcription-settings.partial-" ; // 他用途cleanupと分離
		// 有限乱数を固定hexへ写してpath要素を構成します
		for( const auto byte : random) {
			name += K_HEX[byte >> 4] ;
			name += K_HEX[byte & 15] ;
		}
		const auto	 temporary = directory_ / name ; // 同volumeの原子置換先
		auto		 access = security_->attributes() ; // private作成属性
		UniqueHandle file(
			CreateFileW( temporary.c_str(), GENERIC_WRITE, 0, &access, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)) ;
		// 一時file作成失敗なら旧値へ触れません
		if( file.get() == INVALID_HANDLE_VALUE) {
			throw HistoryError( HistoryErrorKind::WRITE, GetLastError()) ;
		}
		try {
			writeBytes( file.get(), bytes) ;
			// OSへのflush完了前には保存成功を公開しません
			if( !FlushFileBuffers( file.get())) {
				throw HistoryError( HistoryErrorKind::WRITE, GetLastError()) ;
			}
			file.reset() ;
			static_cast<void>( prepareDirectory( directory_, *security_, false)) ;
			static_cast<void>( regularFileExists( target_)) ;
			// 置換成功が耐久値の線形化点。受理後のcloseでも取り消しません
			if( !MoveFileExW( temporary.c_str(), target_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
				throw HistoryError( HistoryErrorKind::WRITE, GetLastError()) ;
			}
		} catch( ...) {
			file.reset() ;
			static_cast<void>( DeleteFileW( temporary.c_str())) ; // 自分の一時fileだけを回収
			throw ;
		}
	}

	private:
	/// 検証済み専用folder
	std::filesystem::path			 directory_ ;
	/// 固定正本
	std::filesystem::path			 target_ ;
	/// worker所有ACL
	std::unique_ptr<PrivateSecurity> security_ ;
	/// worker終了で解放
	UniqueHandle					 lock_ ;
	/// 初回検証成功後だけ保存可
	bool							 loaded_ = false ;
} ;
} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 固定schemaへ選好を符号化します
 * @param[in] value 選好
 * @return v2固定49byte、SHA付き
 * @throws HistoryError 不正選好
 * @see koefude::encodeTranscriptionSettings
 */
//------------------------------------------------------------------------------
std::vector<std::uint8_t> encodeTranscriptionSettings( const TranscriptionPreferences &value)
{
	// 未知のengine/profile/backend/人数を正規fileへしません
	if( !validTranscriptionPreferences( value)) {
		throw HistoryError( HistoryErrorKind::CORRUPT) ;
	}
	std::vector<std::uint8_t> bytes{'K',
									'F',
									'T',
									'S',
									'E',
									'T',
									'0',
									'2',
									static_cast<std::uint8_t>( value.model),
									static_cast<std::uint8_t>( value.profile),
									static_cast<std::uint8_t>( value.backend),
									static_cast<std::uint8_t>( value.autoLanguage),
									static_cast<std::uint8_t>( value.speechFilter),
									static_cast<std::uint8_t>( value.diarization),
									static_cast<std::uint8_t>( value.expectedSpeakers),
									static_cast<std::uint8_t>( value.method),
									static_cast<std::uint8_t>( value.checkUpdates)} ; // schema2固定17byte
	const auto				  sha = digest( bytes) ; // accidental corruption検出
	bytes.insert( bytes.end(), sha.begin(), sha.end()) ;
	return bytes ;
}
//------------------------------------------------------------------------------
/**
 * @brief 固定v1/v2 schema/範囲/registry/SHAを検証します
 * @param[in] bytes 外部file byte
 * @return 正常時だけ全選好
 * @throws HistoryError 未知版/破損/不正値
 * @see koefude::decodeTranscriptionSettings
 */
//------------------------------------------------------------------------------
TranscriptionPreferences decodeTranscriptionSettings( std::span<const std::uint8_t> bytes)
{
	constexpr std::array<std::uint8_t, 7> MAGIC{'K', 'F', 'T', 'S', 'E', 'T', '0'} ; // 共通prefix
	// 固定長/未知版/boolean以外を一部採用しません
	if( (bytes.size() != 48 && bytes.size() != 49) || bytes[11] > 1 || bytes[12] > 1 || bytes[13] > 1) {
		throw HistoryError( HistoryErrorKind::CORRUPT) ;
	}
	// 形式が未知の場合は破損と別に原fileを保護します
	if( !std::equal( MAGIC.begin(), MAGIC.end(), bytes.begin()) || (bytes[7] != '1' && bytes[7] != '2')) {
		throw HistoryError( HistoryErrorKind::VERSION) ;
	}
	// 既存v1は全選好を維持し、読込だけではv2へ置換しません
	if( (bytes[7] == '1' && bytes.size() != 48) || (bytes[7] == '2' && (bytes.size() != 49 || bytes[16] > 1))) {
		throw HistoryError( HistoryErrorKind::CORRUPT) ;
	}
	const auto body = bytes.size() - 32 ; // v1=16、v2=17byte
	const auto sha = digest( bytes.first( body)) ; // 固定bodyだけ
	// 破損byteを既定値へ黙って戻しません
	if( !std::equal( sha.begin(), sha.end(), bytes.begin() + static_cast<std::ptrdiff_t>( body))) {
		throw HistoryError( HistoryErrorKind::CORRUPT) ;
	}
	TranscriptionPreferences value{static_cast<AsrModel>( bytes[8]),
								   static_cast<AsrProfile>( bytes[9]),
								   static_cast<Backend>( bytes[10]),
								   bytes[11] != 0,
								   bytes[12] != 0,
								   bytes[13] != 0,
								   bytes[14],
								   static_cast<SpeakerMethod>( bytes[15]),
								   bytes[7] == '1' || bytes[16] != 0} ; // v1の追加選好はON既定
	// enumのrangeだけでなく実在profileとの組合せも照合します
	if( !validTranscriptionPreferences( value)) {
		throw HistoryError( HistoryErrorKind::CORRUPT) ;
	}
	return value ;
}
//------------------------------------------------------------------------------
/**
 * @brief 文字起こし選好だけのprivate storageを生成します。生成時I/Oなし
 * @param[in] directory 空はLocalAppData/koefude/settings、明示値は隔離用
 * @return 専用workerへ移す所有storage
 * @see koefude::makeFileTranscriptionSettingsStorage
 */
//------------------------------------------------------------------------------
std::unique_ptr<ITranscriptionSettingsStorage> makeFileTranscriptionSettingsStorage( std::filesystem::path directory)
{
	return std::make_unique<FileTranscriptionSettingsStorage>( std::move( directory)) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 専用workerへ所有権を渡す履歴storageを作成します
 * @param[in] directory 明示したlocal専用folder。空はLocalAppData/koefude/history
 * @return 所有storage。生成時にはI/Oせず、load時に検証・排他・読込みを行います
 * @note folder内のhistory.bin、history.lock、history.partial-*はアプリ専用です
 * @see koefude::makeFileHistoryStorage
 */
//------------------------------------------------------------------------------
std::unique_ptr<IHistoryStorage> makeFileHistoryStorage( std::filesystem::path directory)
{
	return std::make_unique<FileHistoryStorage>( std::move( directory)) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 現ユーザー専用履歴folder内の固定名sinkを作ります
 * @param[in] directory 空は通常履歴先
 * @return 遅延I/Oのowner
 * @see koefude::makeApiCorrectionEventLog
 */
//------------------------------------------------------------------------------
std::unique_ptr<IApiCorrectionEventLog> makeApiCorrectionEventLog( std::filesystem::path directory)
{
	return std::make_unique<ApiCorrectionEventLog>( std::move( directory)) ;
}

} // namespace koefude
