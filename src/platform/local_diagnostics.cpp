//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file local_diagnostics.cpp
 * @brief 通常診断の非同期追記、private ACL、有界世代管理を実装します
 * @version 1.0.0
 * @date 2026/11/11
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 1.0.0 Shunji Ogawa 2026/11/11 新規作成
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1397の10MiB/固定queue/別session sinkを追加
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
#include "core/local_diagnostics.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <windows.h>

#include <json.hpp>
#include <sddl.h>
#include <shlobj.h>

#include "inference/model_preparation.h"
#include "platform/win_helpers.h"
#include "version.h"

namespace koefude {
namespace {

using Json = nlohmann::json ; // 固定依存版のJSON encoder
constexpr DWORD		  K_REJECTED_ATTRIBUTES = 0x441400 ; // reparseとcloud同期属性
constexpr std::size_t K_MAXIMUM_LINE_BYTES = 2048 ; // 数値と固定識別子だけの上限

//------------------------------------------------------------------------------
/**
 * @brief 未知enumを自由文に変換しません
 * @param[in] value enumの整数値
 * @param[in] names 宣言順の固定名
 * @return 既知名またはunknown
 */
//------------------------------------------------------------------------------
template <typename Enum, std::size_t Size>
std::string_view fixedName( const Enum value, const std::array<std::string_view, Size> &names)
{
	const auto index = static_cast<std::size_t>( value) ; // 範囲照合だけに使う値
	return index < names.size() ? names[index] : "unknown" ;
}

//------------------------------------------------------------------------------
/**
 * @brief optionalの未観測値を0と区別します
 * @param[in] value 整数/boolean/固定enum
 * @return 値またはnull
 */
//------------------------------------------------------------------------------
template <typename Value> Json optionalValue( const std::optional<Value> &value)
{
	return value ? Json( *value) : Json( nullptr) ;
}

/** @brief 新規file/folderを現在ユーザーとSYSTEMへ限定します */
class PrivateSecurity final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief process tokenのSIDからprivate ACLを構築します
	 */
	//------------------------------------------------------------------------------
	PrivateSecurity()
	{
		HANDLE raw = nullptr ; // CloseHandleする所有token
		// 自process tokenの読取り失敗時はACLなしで保存しない
		if( !OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &raw)) {
			throw OperationError( ErrorKind::SAVE_FAILED, GetLastError()) ;
		}
		const UniqueHandle token( raw) ; // 全例外経路でも解放
		DWORD			   needed = 0 ; // TOKEN_USERの必要byte
		const BOOL		   queried = GetTokenInformation( token.get(), TokenUser, nullptr, 0, &needed) ;
		// 固定上限を超えるtoken情報を確保しない
		if( queried || GetLastError() != ERROR_INSUFFICIENT_BUFFER || needed == 0 || needed > 65536) {
			throw OperationError( ErrorKind::SAVE_FAILED, GetLastError()) ;
		}
		std::vector<std::uint8_t> information( needed) ; // OSからSIDを取得する短寿命buffer
		// SID未取得時は共用権限へfallbackしない
		if( !GetTokenInformation( token.get(), TokenUser, information.data(), needed, &needed)) {
			throw OperationError( ErrorKind::SAVE_FAILED, GetLastError()) ;
		}
		// OSが返したTOKEN_USERを同bufferの寿命内で解釈するAPI境界
		const auto *user = reinterpret_cast<const TOKEN_USER *>( information.data()) ;
		LPWSTR		sid = nullptr ; // LocalFreeする所有SID文字列。出力には使用しない
		// 変換失敗時はprivate ACLなしの保存を拒否する
		if( !ConvertSidToStringSidW( user->User.Sid, &sid)) {
			throw OperationError( ErrorKind::SAVE_FAILED, GetLastError()) ;
		}
		const std::unique_ptr<void, decltype(&LocalFree)> ownedSid( sid, LocalFree) ;
		const std::wstring	 sddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;" + std::wstring( sid) + L")" ;
		PSECURITY_DESCRIPTOR rawDescriptor = nullptr ; // LocalFreeする自己相対descriptor
		// Windowsが検証したdescriptorだけをfile APIへ渡す
		if( !ConvertStringSecurityDescriptorToSecurityDescriptorW( sddl.c_str(), SDDL_REVISION_1, &rawDescriptor,
																 nullptr)) {
			throw OperationError( ErrorKind::SAVE_FAILED, GetLastError()) ;
		}
		descriptor_.reset( rawDescriptor) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同期作成APIへ渡す属性です
	 * @return descriptorの短寿命borrow
	 */
	//------------------------------------------------------------------------------
	SECURITY_ATTRIBUTES attributes() const noexcept
	{
		return {sizeof(SECURITY_ATTRIBUTES), descriptor_.get(), FALSE} ;
	}

	private:
	/// ACLのowner
	std::unique_ptr<void, decltype(&LocalFree)> descriptor_{nullptr, LocalFree} ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief local通常祖先だけにprivate directoryを作成します
 * @param[in] input 値として受け取った専用directory
 * @param[in] security 新規object用のprivate ACL
 * @return 検証済み絶対path
 */
//------------------------------------------------------------------------------
std::filesystem::path prepareDirectory( const std::filesystem::path &input, const PrivateSecurity &security)
{
	const auto path = validateModelDirectory( input) ; // 書込み前の全祖先属性確認
	auto	   current = path.root_path() ; // 検証するdriveから順に作成する
	// 未作成部分をprivate ACLで一段ずつ作り、作成競合後も属性を再検査する
	for( const auto &component : path.relative_path()) {
		current /= component ;
		auto access = security.attributes() ; // 同期CreateDirectoryのborrow
		// 存在する通常directoryにはACL変更や削除を行わない
		if( GetFileAttributesW( current.c_str()) == INVALID_FILE_ATTRIBUTES &&
		   !CreateDirectoryW( current.c_str(), &access) && GetLastError() != ERROR_ALREADY_EXISTS) {
			throw OperationError( ErrorKind::SAVE_FAILED, GetLastError()) ;
		}
		static_cast<void>( validateModelDirectory( current)) ;
	}
	return path ;
}

//------------------------------------------------------------------------------
/**
 * @brief 固定名が存在する場合だけ通常file属性を要求します
 * @param[in] path 検証済みdirectory内の固定名
 * @return 存在時true
 */
//------------------------------------------------------------------------------
bool regularExists( const std::filesystem::path &path)
{
	const DWORD attributes = GetFileAttributesW( path.c_str()) ; // 内容を読まない照会
	// 欠落以外を空ログとして扱わない
	if( attributes == INVALID_FILE_ATTRIBUTES) {
		const auto code = GetLastError() ; // 後続APIより先に保存する診断
		// 固定名の未作成だけを許可する
		if( code == ERROR_FILE_NOT_FOUND) {
			return false ;
		}
		throw OperationError( ErrorKind::SAVE_FAILED, code) ;
	}
	// reparse/cloud/directoryを開いたりrotationしない
	if( (attributes & (K_REJECTED_ATTRIBUTES | FILE_ATTRIBUTE_DIRECTORY)) != 0) {
		throw OperationError( ErrorKind::SAVE_FAILED, ERROR_INVALID_DATA) ;
	}
	return true ;
}

//------------------------------------------------------------------------------
/**
 * @brief 開いた後も通常disk fileと単一linkを要求します
 * @param[in] file 所有中の有効handle
 */
//------------------------------------------------------------------------------
void validateHandle( const HANDLE file)
{
	BY_HANDLE_FILE_INFORMATION information{} ; // TOCTOU後の実object属性
	// 属性差替えやhardlinkを専用ログの保存先として使わない
	if( !GetFileInformationByHandle( file, &information) || GetFileType( file) != FILE_TYPE_DISK ||
	   information.nNumberOfLinks != 1 ||
	   (information.dwFileAttributes & (K_REJECTED_ATTRIBUTES | FILE_ATTRIBUTE_DIRECTORY)) != 0) {
		throw OperationError( ErrorKind::SAVE_FAILED, ERROR_INVALID_DATA) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 有限世代の固定basenameを返します
 * @param[in] generation 現行0または旧1..4
 * @return 任意入力を含まない名前
 */
//------------------------------------------------------------------------------
const wchar_t *generationName( const std::size_t generation) noexcept
{
	constexpr std::array NAMES{L"diagnostics.jsonl", L"diagnostics.1.jsonl", L"diagnostics.2.jsonl",
							   L"diagnostics.3.jsonl", L"diagnostics.4.jsonl"} ; // 他のfileは列挙/削除しない
	return NAMES[generation] ;
}

//------------------------------------------------------------------------------
/**
 * @brief 強制終了等で残った末尾の未完行だけを回収します
 * @param[in] file lease内で所有するread/write handle
 * @return 一つの未完行を切り捨てた場合true。既存本文を解釈/出力しません
 */
//------------------------------------------------------------------------------
bool recoverIncompleteTail( const HANDLE file)
{
	LARGE_INTEGER length{} ; // 回収前の実容量
	// 容量取得失敗を空fileと扱わない
	if( !GetFileSizeEx( file, &length) || length.QuadPart < 0) {
		throw OperationError( ErrorKind::SAVE_FAILED, GetLastError()) ;
	}
	// 空fileは回収対象がない
	if( length.QuadPart == 0) {
		return false ;
	}
	std::array<char, K_MAXIMUM_LINE_BYTES> tail{} ; // 自分の有界一行分だけを読む
	const auto							   count =
		static_cast<DWORD>( std::min<std::uint64_t>( static_cast<std::uint64_t>( length.QuadPart), tail.size())) ;
	LARGE_INTEGER begin{} ; // 最後の最大2048byteの先頭
	begin.QuadPart = length.QuadPart - count ;
	DWORD received = 0 ; // 末尾blockの実読込みbyte
	// 読込み異常や無進行を成功した修復と呼ばない
	if( !SetFilePointerEx( file, begin, nullptr, FILE_BEGIN) || !ReadFile( file, tail.data(), count, &received, nullptr) ||
	   received != count) {
		throw OperationError( ErrorKind::SAVE_FAILED, GetLastError()) ;
	}
	const std::string_view view( tail.data(), count) ; // newline位置の検査だけに使うborrow
	// 完全な最終行はbyteを一切変更しない
	if( view.back() == '\n') {
		return false ;
	}
	const auto newline = view.rfind( '\n') ; // 有限block内の最後の完全行境界
	// 自分の最大行長を超えた未知の破損を、無制限走査や全削除で修復しない
	if( newline == std::string_view::npos && begin.QuadPart != 0) {
		throw OperationError( ErrorKind::SAVE_FAILED, ERROR_INVALID_DATA) ;
	}
	LARGE_INTEGER end{} ; // 最後の完全行の直後、先頭未完だけなら0
	end.QuadPart = begin.QuadPart + static_cast<LONGLONG>( newline == std::string_view::npos ? 0 : newline + 1) ;
	// 新しいsessionの一行を未完JSONへ連結しない
	if( !SetFilePointerEx( file, end, nullptr, FILE_BEGIN) || !SetEndOfFile( file)) {
		throw OperationError( ErrorKind::SAVE_FAILED, GetLastError()) ;
	}
	return true ;
}

/** @brief 受領時刻付きの固定容量queue項目です */
struct Envelope {
	/// 文字列/pointerなし
	LocalDiagnosticRecord record ;
	/// append時点のUTC
	SYSTEMTIME			  utc{} ;
	/// append時点の単調経過
	std::uint64_t		  elapsedMs = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 閉じた型だけをJSONへ投影します
 * @param[in] envelope 有限値
 * @param[in] session 数値session識別
 * @param[in] dropped 直前までの省略累計
 * @return 最大2048byteのASCII/UTF-8一行
 */
//------------------------------------------------------------------------------
std::string formatRecord( const Envelope &envelope, const std::uint64_t session, const std::uint64_t dropped)
{
	const auto								  &r = envelope.record ; // 自由文字列を一切持たない同期borrow
	constexpr std::array<std::string_view, 18> EVENTS{"session_started",
													  "session_ended",
													  "settings_restored",
													  "settings_applied",
													  "settings_opened",
													  "preparation_opened",
													  "model_check_requested",
													  "model_download_requested",
													  "model_started",
													  "model_stage",
													  "model_plan",
													  "model_asset_plan",
													  "model_finished",
													  "job_started",
													  "job_stage",
													  "job_finished",
													  "api_event",
													  "unknown"} ; // enumと対応する固定名
	constexpr std::array<std::string_view, 12> REASONS{"none",
													   "startup",
													   "settings_apply",
													   "prep_recheck_command",
													   "prep_download_command",
													   "settings_menu",
													   "settings_prepare_command",
													   "missing_for_job",
													   "missing_after_check",
													   "projection",
													   "refresh",
													   "unknown"} ; // 物理clickを推定しない入口名
	constexpr std::array<std::string_view, 10> OUTCOMES{"none",		 "ready",	  "missing", "invalid",
														"error",	 "completed", "warning", "no_speech",
														"cancelled", "unknown"} ; // 成功/失敗/取消しを区別
	constexpr std::array<std::string_view, 3>  STORAGES{"none", "user_cache", "app_models"} ;
	constexpr std::array<std::string_view, 4>  ASSET_ACTIONS{"reuse_cache", "reuse_app_models", "copy_app_model",
															 "download"} ; // 固定準備action
	constexpr std::array<std::string_view, 6>  PURPOSES{"none",		"local_asr", "audio_api",
														"text_api", "metadata",	 "other"} ;
	std::array<char, 32>					   utc{} ; // 固定書式のUTCのみ
	const auto								  &t = envelope.utc ; // OS時刻の整数field
	const int size = std::snprintf( utc.data(), utc.size(), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", t.wYear, t.wMonth,
								   t.wDay, t.wHour, t.wMinute, t.wSecond,
								   t.wMilliseconds) ; // UTCは壁時計、経過は別の単調時刻
	// buffer切詰めを有効な時刻として保存しない
	if( size <= 0 || static_cast<std::size_t>( size) >= utc.size()) {
		throw OperationError( ErrorKind::INTERNAL) ;
	}
	Json line{{"schema", 1},
			  {"diagnostics_revision", 1397},
			  {"version", KOEFUDE_VERSION_STRING},
			  {"event", fixedName( r.event, EVENTS)},
			  {"utc", utc.data()},
			  {"elapsed_ms", envelope.elapsedMs},
			  {"session", session},
			  {"pid", GetCurrentProcessId()},
			  {"operation", r.operation},
			  {"history", r.history},
			  {"reason", fixedName( r.reason, REASONS)},
			  {"outcome", fixedName( r.outcome, OUTCOMES)},
			  {"storage", fixedName( r.storage, STORAGES)},
			  {"purpose", fixedName( r.purpose, PURPOSES)},
			  {"model", optionalValue( r.model)},
			  {"profile", optionalValue( r.profile)},
			  {"requested_backend", optionalValue( r.requestedBackend)},
			  {"actual_backend", optionalValue( r.actualBackend)},
			  {"api_model", apiProvenanceModelName( r.apiModel)},
			  {"asset", r.asset ? Json( modelDefinition( *r.asset).id) : Json( nullptr)},
			  {"required_asset_bytes", r.asset ? Json( modelDefinition( *r.asset).bytes) : Json( nullptr)},
			  {"asset_action", r.assetAction ? Json( fixedName( *r.assetAction, ASSET_ACTIONS)) : Json( nullptr)},
			  {"api_action", optionalValue( r.apiAction)},
			  {"stage", optionalValue( r.stage)},
			  {"model_stage", optionalValue( r.modelStage)},
			  {"phase", optionalValue( r.phase)},
			  {"error", optionalValue( r.error)},
			  {"code", r.code},
			  {"http_status", r.httpStatus},
			  {"count", r.count},
			  {"total", r.total},
			  {"bytes", r.bytes},
			  {"reused", r.reused},
			  {"downloads", r.downloads},
			  {"copies", r.copies},
			  {"duration_ms", optionalValue( r.durationMs)},
			  {"processing_ms", optionalValue( r.processingMs)},
			  {"cpu_retried", optionalValue( r.cpuRetried)},
			  {"download", r.download},
			  {"verified", r.verified},
			  {"dropped_total", dropped}} ; // user文字列の入口なし
#if defined(NDEBUG)
    line["build_configuration"] = "release" ;
#else
    line["build_configuration"] = "debug" ;
#endif
    auto output = line.dump() + '\n' ; // 最大長を確認後だけI/Oへ渡す
	// 固定schemaが想定外に増えた場合は有界性を守りloggerだけを停止する
	if( output.size() > K_MAXIMUM_LINE_BYTES) {
		throw OperationError( ErrorKind::INTERNAL) ;
	}
	return output ;
}

//------------------------------------------------------------------------------
/**
 * @brief 共有leaseの間だけrotationと一行追記を行います
 * @param[in] directory privateな専用directory
 * @param[in] security 新file用ACL
 * @param[in] line 内部formatterだけが生成した有限一行
 * @return lease競合0、書込み成功1、未完行回収後の成功2。その他障害は例外
 */
//------------------------------------------------------------------------------
unsigned writeRecord( const std::filesystem::path &directory, const PrivateSecurity &security, const std::string &line)
{
	static_cast<void>( validateModelDirectory( directory)) ; // 各追記前に祖先を再確認
	const auto lockPath = directory / L"diagnostics.lock" ; // 0byteのprocess間lease
	static_cast<void>( regularExists( lockPath)) ;
	auto			   access = security.attributes() ; // 同期APIへのprivate ACL borrow
	const UniqueHandle lease( CreateFileW( lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, &access, OPEN_ALWAYS,
										 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
										 nullptr)) ; // 非待機。再試行しない
	// 別instanceが書込み中なら本処理を待たせずこのeventだけを省略する
	if( lease.get() == INVALID_HANDLE_VALUE) {
		const auto code = GetLastError() ; // 競合と保存障害を区別する元値
		// 既存lease競合だけをbest effortの省略とする
		if( code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION) {
			return 0 ;
		}
		throw OperationError( ErrorKind::SAVE_FAILED, code) ;
	}
	validateHandle( lease.get()) ;
	// 固定の五世代だけを確認し、任意fileや非通常fileには触れない
	for( std::size_t index = 0; index < K_LOCAL_DIAGNOSTIC_GENERATIONS; ++index) {
		const auto path = directory / generationName( index) ; // 管理する固定名だけ
		// 外部編集等で過大化した管理fileも保持上限へ戻し、他のfileには触れない
		if( regularExists( path)) {
			UniqueHandle  existing( CreateFileW( path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
											   FILE_FLAG_OPEN_REPARSE_POINT,
											   nullptr)) ; // 容量照会後に閉じるowner
			LARGE_INTEGER length{} ; // 全量読込をせずOSが返す容量だけを検査する
			// 非通常object/取得不能容量では削除をせずloggerだけを停止する
			if( existing.get() == INVALID_HANDLE_VALUE || !GetFileSizeEx( existing.get(), &length) ||
			   length.QuadPart < 0) {
				throw OperationError( ErrorKind::SAVE_FAILED, GetLastError()) ;
			}
			validateHandle( existing.get()) ;
			existing.reset() ; // 自分の読取handleを閉じてから固定名をevictする
			// 一世代上限を超えた固定名だけを破棄し、巨大な旧世代を維持しない
			if( static_cast<std::uint64_t>( length.QuadPart) > K_LOCAL_DIAGNOSTIC_FILE_BYTES &&
			   !DeleteFileW( path.c_str())) {
				throw OperationError( ErrorKind::SAVE_FAILED, GetLastError()) ;
			}
		}
	}
	const auto	  current = directory / generationName( 0) ; // 固定の現行名
	std::uint64_t bytes = 0 ; // 未作成は0byte
	// 現行が存在する場合は実handleの容量を確認する
	if( regularExists( current)) {
		const UniqueHandle file( CreateFileW( current.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
											FILE_FLAG_OPEN_REPARSE_POINT,
											nullptr)) ; // leaseの間は別writerが開かない
		LARGE_INTEGER	   length{} ; // 元の64bit file容量
		// 不正object/取得不能容量を空ログと扱わない
		if( file.get() == INVALID_HANDLE_VALUE || !GetFileSizeEx( file.get(), &length) || length.QuadPart < 0) {
			throw OperationError( ErrorKind::SAVE_FAILED, GetLastError()) ;
		}
		validateHandle( file.get()) ;
		bytes = static_cast<std::uint64_t>( length.QuadPart) ;
	}
	// 上限を超える前に一世代ずつrenameし、現行を含め常に五世代以内へ保つ
	if( bytes > K_LOCAL_DIAGNOSTIC_FILE_BYTES - line.size()) {
		const auto oldest = directory / generationName( K_LOCAL_DIAGNOSTIC_GENERATIONS - 1) ;
		// 固定最古世代以外を削除しない
		if( regularExists( oldest) && !DeleteFileW( oldest.c_str())) {
			throw OperationError( ErrorKind::SAVE_FAILED, GetLastError()) ;
		}
		// 新しい順を壊さないよう旧世代の末尾側から移動する
		for( std::size_t index = K_LOCAL_DIAGNOSTIC_GENERATIONS - 1; index > 0; --index) {
			const auto previous = directory / generationName( index - 1) ; // 移動元固定名
			// 欠落世代は飛ばし、共有違反等はloggerだけの失敗へ変換する
			if( regularExists( previous) &&
			   !MoveFileExW( previous.c_str(), (directory / generationName( index)).c_str(), MOVEFILE_REPLACE_EXISTING)) {
				throw OperationError( ErrorKind::SAVE_FAILED, GetLastError()) ;
			}
		}
	}
	const UniqueHandle file( CreateFileW( current.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, &access,
										OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
										nullptr)) ; // 一行ごとに閉じ、他instanceのrotationを許す
	// 保存不能時は通常処理へ例外を返さずwriterで処理する
	if( file.get() == INVALID_HANDLE_VALUE) {
		throw OperationError( ErrorKind::SAVE_FAILED, GetLastError()) ;
	}
	validateHandle( file.get()) ;
	const bool			recovered = recoverIncompleteTail( file.get()) ; // 次sessionのJSONL境界を回復
	const LARGE_INTEGER end{} ; // lease内でだけ末尾位置を決める
	// GENERIC_WRITEを使いFlushFileBuffersの契約を満たす
	if( !SetFilePointerEx( file.get(), end, nullptr, FILE_END)) {
		throw OperationError( ErrorKind::SAVE_FAILED, GetLastError()) ;
	}
	DWORD written = 0 ; // 短い一行の実書込みbyte
	// 部分書込み/flush失敗を完全な記録にしない
	if( !WriteFile( file.get(), line.data(), static_cast<DWORD>( line.size()), &written, nullptr) ||
	   written != line.size() || !FlushFileBuffers( file.get())) {
		throw OperationError( ErrorKind::SAVE_FAILED, GetLastError()) ;
	}
	return recovered ? 2 : 1 ;
}

} // namespace

/** @brief file I/O中にqueue mutexを持たない一sessionのwriter状態です */
struct LocalDiagnosticLog::State {
	/// UIには値だけを公開する不変保存先
	std::filesystem::path									directory ;
	/// allocationしない固定queue
	std::array<Envelope, K_LOCAL_DIAGNOSTIC_QUEUE_CAPACITY> queue{} ;
	/// 次に排出するslot
	std::size_t												head = 0 ;
	/// 有効slot数
	std::size_t												count = 0 ;
	/// queue/停止/最終slotだけの排他
	std::mutex												mutex ;
	/// 新通知と終了を待機
	std::condition_variable									changed ;
	/// 障害後の再帰log/再試行を禁止する状態
	std::atomic<bool>										failed = false ;
	/// queue/lease競合の省略累計
	std::atomic<std::uint64_t>								dropped = 0 ;
	/// 新通知を拒否して既存queueを排出する状態
	bool													stopping = false ;
	/// 満杯でも終端を予約できる専用一枠
	std::optional<Envelope>									final ;
	/// 壁時計と独立した起点
	std::uint64_t											startedMs = GetTickCount64() ;
	/// UTC FILETIMEの整数。PIDと組み合わせて区別
	std::uint64_t											session = 0 ;
	/// Stateを破棄する前にjoinする単独writer
	std::thread												worker ;

	//------------------------------------------------------------------------------
	/**
	 * @brief UTCと単調経過をappend時点に固定します
	 * @param[in] record 固定値
	 * @return 時刻付き所有値
	 */
	//------------------------------------------------------------------------------
	Envelope												envelope( LocalDiagnosticRecord record) const noexcept
	{
		Envelope value ; // file I/O/動的文字列のない小DTO
		value.record = record ;
		GetSystemTime( &value.utc) ;
		value.elapsedMs = GetTickCount64() - startedMs ;
		return value ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 最大256件を順に排出し、終了後に最終slotを一度だけ書きます
	 */
	//------------------------------------------------------------------------------
	void run() noexcept
	{
		try {
			const PrivateSecurity security ; // このwriterだけが所有するACL
			const auto			  prepared = prepareDirectory( directory, security) ; // UIでI/Oしない
			// producer停止とqueue排出が揃うまで有限queueを順に処理する
			for(;;) {
				Envelope value ; // mutexの外でformat/writeする所有copy
				bool	 last = false ; // 専用終端slotの一回排出
				{
					std::unique_lock lock( mutex) ; // I/Oの前に必ず解放する
					changed.wait( lock,
						//------------------------------------------------------------------------------
						/**
						 * @brief 終了または実通知で起床します
						 * @return 待機終了条件
						 */
						//------------------------------------------------------------------------------
						[this]
						{
							return stopping || count != 0 ;
						}) ;
					// 通常queue排出後は予約済み終端だけを取り出す
					if( count == 0) {
						// 例外/crashの正常終了を捏造しない
						if( !final) {
							break ;
						}
						value = *final ;
						final.reset() ;
						last = true ;
					} else {
						value = queue[head] ;
						head = (head + 1) % queue.size() ;
						--count ;
					}
				}
				// 障害時は一度だけcatchし、競合だけは省略数を残して次へ進む
				const auto written = writeRecord( prepared, security, formatRecord( value, session, dropped.load())) ;
				// lease省略と前sessionの未完行回収は後続行の累計で分かるようにする
				if( written != 1) {
					++dropped ;
				}
				// 最終slotの後から新eventを受け付けない
				if( last) {
					break ;
				}
			}
		} catch( ...) {
			// 生の例外文字列を記録せず、診断だけを非致命停止する
			failed.store( true) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief producer停止後にqueueを排出してwriterを閉じます
	 * @param[in] code 正常loop終端の値、例外終了/destructorはnull
	 */
	//------------------------------------------------------------------------------
	void close( const std::optional<std::uint32_t> code) noexcept
	{
		try {
			{
				const std::lock_guard lock( mutex) ; // 停止と最終slotの予約だけ
				// 二重finish/destructorで終端やthread joinを繰り返さない
				if( stopping) {
					return ;
				}
				stopping = true ;
				// 正常message loopの終端だけを明示記録する
				if( code) {
					LocalDiagnosticRecord record ; // 内容を含まない終了値だけ
					record.event = LocalDiagnosticEvent::SESSION_ENDED ;
					record.code = *code ;
					record.outcome =
						*code == 0 ? LocalDiagnosticOutcome::COMPLETED : LocalDiagnosticOutcome::DIAGNOSTIC_ERROR ;
					final = envelope( record) ;
				}
			}
			changed.notify_one() ;
			// 初期化に失敗した未開始threadへjoinしない
			if( worker.joinable()) {
				worker.join() ;
			}
		} catch( ...) {
			// thread同期障害もASR結果や終了codeへ伝播しない
			failed.store( true) ;
		}
	}
} ;

//------------------------------------------------------------------------------
/**
 * @brief 専用writerを開始します
 * @param[in] directory 所有保存先。空は無効/I/O0
 * @see koefude::LocalDiagnosticLog::LocalDiagnosticLog(std::filesystem::path)
 */
//------------------------------------------------------------------------------
LocalDiagnosticLog::LocalDiagnosticLog( std::filesystem::path directory) noexcept
{
	// 明示未指定の旧試験呼出しは通常ユーザー領域へfallbackしない
	if( directory.empty()) {
		return ;
	}
	try {
		state_ = std::make_unique<State>() ;
		state_->directory = std::move( directory) ;
		FILETIME utc{} ; // username/pathを使わないsession識別の起点
		GetSystemTimePreciseAsFileTime( &utc) ;
		state_->session = (static_cast<std::uint64_t>( utc.dwHighDateTime) << 32U) | utc.dwLowDateTime ;
		state_->worker = std::thread(
			//------------------------------------------------------------------------------
			/**
			 * @brief 所有Stateだけをwriterへ渡します
			 */
			//------------------------------------------------------------------------------
			[state = state_.get()]
			{
				state->run() ;
			}) ;
		LocalDiagnosticRecord record ; // 通常起動で事前操作なしに残す開始event
		record.event = LocalDiagnosticEvent::SESSION_STARTED ;
		append( record) ;
	} catch( ...) {
		// thread開始失敗を製品の起動失敗にしない
		state_.reset() ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 終了eventを捏造せず、残queueを排出してwriterをjoinします
 * @see koefude::LocalDiagnosticLog::~LocalDiagnosticLog
 */
//------------------------------------------------------------------------------
LocalDiagnosticLog::~LocalDiagnosticLog()
{
	// 無効sinkからthreadや保存先を作らない
	if( state_) {
		state_->close( {}) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 本文を含まない固定metadataを有限queueへコピーし、別writerへ記録を依頼します
 * @param[in] record 記録する小さい所有DTO。復帰後に呼出し元の値を参照しません
 * @pre instanceを破棄せず、finishは全producerの終了後に単一ownerから呼ぶこと
 * @note 複数threadから呼べ、file I/Oは行いませんがqueue mutexの待機はあり得ます
 *       無効・終了・障害後は無操作、満杯ならdroppedを加算します。復帰は記録完了を意味しません
 *       同期障害を外へ送出せずhealthyの障害状態へ保持します
 * @see koefude::LocalDiagnosticLog::append
 */
//------------------------------------------------------------------------------
void LocalDiagnosticLog::append( const LocalDiagnosticRecord record) noexcept
{
	// 障害後の再試行やfile I/Oをproducerへ持ち込まない
	if( !state_ || state_->failed.load()) {
		return ;
	}
	try {
		const std::lock_guard lock( state_->mutex) ; // 数値DTOのcopyだけを排他
		// 終了後の通知を次sessionや終端の後へ追加しない
		if( state_->stopping) {
			return ;
		}
		// 一定数を超えた通知でqueueを拡張しない
		if( state_->count == state_->queue.size()) {
			++state_->dropped ;
			return ;
		}
		state_->queue[(state_->head + state_->count) % state_->queue.size()] = state_->envelope( record) ;
		++state_->count ;
		state_->changed.notify_one() ;
	} catch( ...) {
		// mutex基盤障害も再帰logせずsinkだけを停止する
		state_->failed.store( true) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 正常message loopの終了recordを最終slotへ予約し、残queueを排出してwriterをjoinします
 * @param[in] exitCode message loopの整数終了code。正常loop終了の実値を渡します
 * @pre 全producer終了後に単一ownerから呼び、他のfinishや破棄と並行しないこと
 * @note この呼出しはwriter終了を待ちます。停止状態を設定済みの重複呼出し、または無効instanceは無操作です
 *       I/O/同期障害は本処理へ送出せずhealthyへ保持し、全recordの保存成功は保証しません
 * @see koefude::LocalDiagnosticLog::finish
 */
//------------------------------------------------------------------------------
void LocalDiagnosticLog::finish( const std::uint32_t exitCode) noexcept
{
	// 無効sinkの終了はI/O0のままにする
	if( state_) {
		state_->close( exitCode) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 通常診断の初期化やwriterで、記録障害を検出していないかを読みます
 * @return このinstanceの検出済み障害の有無。ASRやAPI処理の成否ではありません
 * @retval true Stateが存在し、failedが立っていません。writerの初回I/O前にもtrueになり得ます
 * @retval false 無効化・初期化失敗でStateがないか、writer等が障害を検出しました
 * @pre instanceの寿命を呼出し完了まで保持し、破棄と並行しないこと
 * @note queue全件の耐久保存、記録の無欠落、writerが現在稼働中であることは保証しません
 *       この読取り自体はfile I/Oを行いません
 * @see koefude::LocalDiagnosticLog::healthy
 */
//------------------------------------------------------------------------------
bool LocalDiagnosticLog::healthy() const noexcept
{
	return state_ && !state_->failed.load() ;
}

//------------------------------------------------------------------------------
/**
 * @brief queue満杯やwriterのlease競合等で省略した診断件数のsession累計を読みます
 * @return Stateが保持する省略件数。Stateがなければ0です。単位は診断record件数です
 * @pre instanceの寿命を呼出し完了まで保持し、破棄と並行しないこと
 * @note 無効化や障害後にappendが何もせず戻る場合もあるため、0は全件の記録成功を意味しません
 *       この読取り自体はfile I/Oを行いません
 * @see koefude::LocalDiagnosticLog::dropped
 */
//------------------------------------------------------------------------------
std::uint64_t LocalDiagnosticLog::dropped() const noexcept
{
	return state_ ? state_->dropped.load() : 0 ;
}

//------------------------------------------------------------------------------
/**
 * @brief 技術試験で通常領域と隔離領域を照合できるよう、診断先pathを借用で返します
 * @return State内の不変pathへの非所有const参照。Stateがなければstaticな空pathへの参照です
 *         呼出し元は解放せず、このinstanceの寿命を越えて保持しないでください
 * @pre instanceの寿命を借用の使用終了まで保持し、破棄と並行しないこと
 * @note I/O障害やfinish後も、Stateがあれば保持しているpathを返します。記録成功の判定値ではなく、
 *       このpath自体をログ本文へ書き出しません
 * @see koefude::LocalDiagnosticLog::directory
 */
//------------------------------------------------------------------------------
const std::filesystem::path &LocalDiagnosticLog::directory() const noexcept
{
	static const std::filesystem::path empty ; // 無効時の不変空値
	return state_ ? state_->directory : empty ;
}

//------------------------------------------------------------------------------
/**
 * @brief 通常または明示隔離historyに対応する診断保存先を解決します
 * @param[in] historyDirectory 同期借用するhistory directory。空なら現在ユーザーのLocalAppData、
 *                             非空ならabsolute/lexically_normal後の親directoryを基準にします
 * @return 空入力ならLocalAppData/koefude/logs、非空なら正規化した親/logsの所有path
 *         OS解決やpath確保の失敗は空pathを返し、別領域へfallbackしません
 * @note directory作成や設定変更は行いません。返却pathをlog本文へ渡しません
 * @see koefude::localDiagnosticDirectory
 */
//------------------------------------------------------------------------------
std::filesystem::path localDiagnosticDirectory( const std::filesystem::path &historyDirectory) noexcept
{
	try {
		// 明示隔離historyがあれば本人の通常stateへアクセスしない
		if( !historyDirectory.empty()) {
			return std::filesystem::absolute( historyDirectory).lexically_normal().parent_path() / L"logs" ;
		}
		PWSTR		  raw = nullptr ; // CoTaskMemFreeする既知folderの所有値
		const HRESULT status = SHGetKnownFolderPath( FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw) ;
		const std::unique_ptr<void, decltype(&CoTaskMemFree)> owned( raw, CoTaskMemFree) ;
		// OS解決失敗を別の共用pathへfallbackしない
		if( FAILED(status) || !raw) {
			return {} ;
		}
		return std::filesystem::path( raw) / L"koefude" / L"logs" ;
	} catch( ...) {
		// 保存先の解決障害はlogger無効だけで終了する
		return {} ;
	}
}

namespace {
/** @brief 既存API logの返値/保存契約を変更しないtyped teeです */
class LocalApiLog final : public IApiCorrectionEventLog {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 旧sinkと借用loggerを保持します
	 * @param[in] existing 所有旧sink
	 * @param[in] log workerより長寿命、null可
	 */
	//------------------------------------------------------------------------------
	LocalApiLog( std::unique_ptr<IApiCorrectionEventLog> existing, LocalDiagnosticLog *log)
		: existing_( std::move( existing)), log_( log)
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
		// 新sinkの成否を従来のAPI処理へ返さない
		if( log_) {
			LocalDiagnosticRecord record ; // 元の秘密なし型からscalarだけ投影
			record.event = LocalDiagnosticEvent::API_EVENT ;
			record.operation = event.operation ;
			record.history = event.history ;
			record.purpose = event.audio ? LocalDiagnosticPurpose::AUDIO_API : LocalDiagnosticPurpose::TEXT_API ;
			record.apiModel = event.provenance.requested ;
			record.code = event.code ;
			record.httpStatus = event.httpStatus ;
			record.count = event.chunk ;
			record.total = event.audioUnits ;
			record.durationMs = event.audioSamples / (K_SAMPLE_RATE / 1000) ;
			record.verified = event.saved ;
			record.phase = event.event == ApiCorrectionEvent::STARTED ? ApiTextPhase::PREPARING : ApiTextPhase::NONE ;
			log_->append( record) ;
		}
		return existing_ ? existing_->append( event) : false ;
	}

	private:
	/// 既存の有限API保存を維持
	std::unique_ptr<IApiCorrectionEventLog> existing_ ;
	/// 全worker終了までwWinMainが所有するborrow
	LocalDiagnosticLog					   *log_ ;
} ;
} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 既存API sinkへの記録を保ち、同じ固定metadataを通常診断にも複写するwrapperを作ります
 * @param[in] existing wrapperへ所有移管する既存sink。nullptr可。拒否時に呼出し元のmove済み値を復元しません
 * @param[in] log 複写先の非所有borrow。nullptr可。非NULLならwrapperの全append終了まで保持してください
 * @return wrapperの単独owner。wrapper確保失敗時はexistingのownerをそのまま返し、元も空ならnullptrです
 * @note wrapperのappendは既存sinkのboolを返し、既存sinkなしならfalseです
 *       通常診断への複写成否をAPI記録成功へ読み替えず、構築時には記録を開始しません
 * @see koefude::makeLocalDiagnosticApiLog
 */
//------------------------------------------------------------------------------
std::unique_ptr<IApiCorrectionEventLog> makeLocalDiagnosticApiLog( std::unique_ptr<IApiCorrectionEventLog> existing,
																  LocalDiagnosticLog					 *log) noexcept
{
	try {
		return std::make_unique<LocalApiLog>( std::move( existing), log) ;
	} catch( ...) {
		// 追加sinkの確保失敗でも旧sink/通常APIの起動資格を失わない
		return existing ;
	}
}

} // namespace koefude
