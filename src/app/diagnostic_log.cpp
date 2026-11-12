//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file diagnostic_log.cpp
 * @brief 試験時だけの有界な診断イベントfileを実装します
 * @version 1.0.0
 * @date 2026/09/28
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/28 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/28 ローカルfileへの固定イベント記録を追加
 * 0.1.0 Shunji Ogawa 2026/10/03 実行結果なしをnullとして保存する追加イベントを実装
 * 0.1.0 Shunji Ogawa 2026/10/19 分岐意図・宣言の役割を明記
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1287の明示診断と隔離接続を追加
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1301の補助CPU接続と限定検証を追加
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
 * 0.1.0 Shunji Ogawa 2026/10/22 ADR1323の三slot受付境界と説明を整合
 * 0.1.0 Shunji Ogawa 2026/10/22 ADR1327の明示診断に読取りだけを共有
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実使用の標準headerを直接include
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "app/diagnostic_log.h"

#include "inference/whole_text_runtime.h"
#include <optional>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace koefude {
namespace {

constexpr DWORD	   K_REJECTED_ATTRIBUTES = 0x441400 ; // reparseとcloud同期属性

//------------------------------------------------------------------------------
/**
 * @brief 試験用pathがローカル通常directory内にあるか確認します
 * @param[in] path 読み書き前の候補path
 * @return 祖先とfile名が安全ならtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool validLogPath( const std::filesystem::path &path)
{
	const auto absolute = std::filesystem::absolute( path).lexically_normal() ; // OSへ渡す正規化path
	const auto native = absolute.native() ; // 文字列境界の検査にだけ使う値
	const auto root = absolute.root_path() ; // drive種別を問い合わせるroot
	// network/device/ADSや長すぎるpathを試験ログの保存先にしない
	if( native.size() < 4 || native.size() > 32700 || native[1] != L':' || native.find( L':', 2) != std::wstring::npos ||
	   path.filename().empty() ||
	   (GetDriveTypeW( root.c_str()) != DRIVE_FIXED && GetDriveTypeW( root.c_str()) != DRIVE_REMOVABLE &&
		GetDriveTypeW( root.c_str()) != DRIVE_RAMDISK)) {
		return false ;
	}
	auto current = root ; // 存在確認する先祖directory
	// 全先祖が通常directoryであることを確認してから新規fileを開く
	for( const auto &component : absolute.parent_path().relative_path()) {
		current /= component ;
		const DWORD attributes = GetFileAttributesW( current.c_str()) ; // 同期属性を含む値
		// reparse/cloud placeholderを経由して外部へ送らない
		if( attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
		   (attributes & K_REJECTED_ATTRIBUTES) != 0) {
			return false ;
		}
	}
	return true ;
}

//------------------------------------------------------------------------------
/**
 * @brief enumから固定ASCII識別子を得ます
 * @param[in] event 記録する固定イベント
 * @return 本文やpathを含まない静的文字列
 */
//------------------------------------------------------------------------------
[[nodiscard]] const char *eventName( const DiagnosticEvent event) noexcept
{
	// 入力値を文字列へ変換せず、宣言した識別子だけを許す
	switch( event) {
		case DiagnosticEvent::APP_STARTED:
			return "app_started" ;
		case DiagnosticEvent::APP_EXITED:
			return "app_exited" ;
		case DiagnosticEvent::HISTORY_LOADED:
			return "history_loaded" ;
		case DiagnosticEvent::HISTORY_LOAD_FAILED:
			return "history_load_failed" ;
		case DiagnosticEvent::HISTORY_SAVED:
			return "history_saved" ;
		case DiagnosticEvent::HISTORY_SAVE_FAILED:
			return "history_save_failed" ;
		case DiagnosticEvent::MODEL_CHECKED:
			return "model_checked" ;
		case DiagnosticEvent::MODEL_DOWNLOAD_STARTED:
			return "model_download_started" ;
		case DiagnosticEvent::JOB_ACCEPTED:
			return "job_accepted" ;
		case DiagnosticEvent::JOB_FINISHED:
			return "job_finished" ;
		case DiagnosticEvent::SAVE_ACCEPTED:
			return "save_accepted" ;
		case DiagnosticEvent::SAVE_FINISHED:
			return "save_finished" ;
		case DiagnosticEvent::COPY_FINISHED:
			return "copy_finished" ;
		case DiagnosticEvent::CANCEL_REQUESTED:
			return "cancel_requested" ;
		case DiagnosticEvent::JOB_CONFIGURED:
			return "job_configured" ;
	}
	return "unknown" ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 指定された試験用fileを新規作成します
 * @param[in] path ローカルの既存directory内の新規file。空なら無効
 * @note pathの内容をログへ記録せず、既存fileを上書きしません
 * @see koefude::DiagnosticLog::DiagnosticLog(const std::filesystem::path &)
 */
//------------------------------------------------------------------------------
DiagnosticLog::DiagnosticLog( const std::filesystem::path &path) noexcept
{
	// 未指定なら通常起動として永続fileを一切作らない
	if( path.empty()) {
		return ;
	}
	try {
		// pathを記録せず、既存fileへの追記・上書きを拒否する
		if( !validLogPath( path)) {
			failed_ = true ;
			return ;
		}
		const auto absolute = std::filesystem::absolute( path).lexically_normal() ; // 開く値
		// 明示診断の生存中readだけを許し、別writerと削除による観測の破壊は拒否する
		file_ = CreateFileW( absolute.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
							FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr) ;
		// 書込み不能でもアプリ本体の結果を失わせない
		if( file_ == INVALID_HANDLE_VALUE) {
			failed_ = true ;
			return ;
		}
		startedMs_ = GetTickCount64() ;
	} catch( ...) {
		failed_ = true ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 所有handleを閉じ、未有効の場合は何もしません
 * @see koefude::DiagnosticLog::~DiagnosticLog
 */
//------------------------------------------------------------------------------
DiagnosticLog::~DiagnosticLog()
{
	// 未有効や作成失敗時にOSの無効handleを閉じない
	if( file_ != INVALID_HANDLE_VALUE) {
		CloseHandle( file_) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 固定イベントと整数値だけを一行に追記します
 * @param[in] event 固定のイベント種別
 * @param[in] id 操作ID。該当しなければ0
 * @param[in] value 状態または件数
 * @param[in] auxiliary 追加の数値状態
 * @param[in] code APIまたは製品の診断code
 * @see koefude::DiagnosticLog::append
 */
//------------------------------------------------------------------------------
void DiagnosticLog::append( const DiagnosticEvent event, const std::uint64_t id, const std::uint64_t value,
						   const std::uint64_t auxiliary, const std::uint32_t code) noexcept
{
	// 失敗後の部分ログを完全な証拠として増やさない
	if( file_ == INVALID_HANDLE_VALUE || failed_) {
		return ;
	}
	// 明示modeではASR受付も共通上限に数えます
	if( current_ && event == DiagnosticEvent::JOB_ACCEPTED) {
		// 上限後は製品を止めずログだけを不完全にします
		if( trackedCount_ + asrCount_ >= tracked_.size()) {
			failed_ = true ;
			return ;
		}
		++asrCount_ ;
	}
	std::array<char, 256> line{} ; // 固定識別子と整数だけの有界な一行
	// 終端NULを除く整形結果のbyte数。負値とbuffer超過は書込み前に拒否する
	const int			  size = std::snprintf( line.data(), line.size(),
											   "{\"schema_version\":1,\"event\":\"%s\",\"elapsed_ms\":%llu,"
														   "\"id\":%llu,\"value\":%llu,\"aux\":%llu,\"code\":%u}\n",
											   eventName( event), static_cast<unsigned long long>( GetTickCount64() - startedMs_),
											   static_cast<unsigned long long>( id), static_cast<unsigned long long>( value),
											   static_cast<unsigned long long>( auxiliary), static_cast<unsigned>( code)) ;
	// buffer切詰めや部分書込みを成功したイベントにしない
	if( size <= 0 || static_cast<std::size_t>( size) >= line.size()) {
		failed_ = true ;
		return ;
	}
	writeLine( std::string_view( line.data(), static_cast<std::size_t>( size))) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 診断fileが有効で、記録や追跡に失敗を検出していないかを返します
 * @return このinstanceの記録可否。処理対象の成功可否ではありません
 * @retval true 有効なhandleを所有し、これまでの失敗を検出していません
 * @retval false 空pathによる無効化、file作成/書込み失敗、上限超過等を検出しました
 * @note 記録した処理自体の成功や、OS障害に対するログの耐久性を保証する値ではありません
 * @see koefude::DiagnosticLog::healthy
 */
//------------------------------------------------------------------------------
bool DiagnosticLog::healthy() const noexcept
{
	return file_ != INVALID_HANDLE_VALUE && !failed_ ;
}

//------------------------------------------------------------------------------
/**
 * @brief 結果の有無と実行経路をschema 2の追加イベントへ記録します
 * @param[in] id 受付と同じ操作ID
 * @param[in] execution enum/booleanだけの同期borrow。本文・pathを含みません
 * @note 既存schema 1イベントを変更せず、ログ失敗は本処理へ伝播しません
 * @see koefude::DiagnosticLog::appendExecution
 */
//------------------------------------------------------------------------------
void DiagnosticLog::appendExecution( const std::uint64_t id, const DiagnosticExecution &execution) noexcept
{
	// 無効/書込み失敗後のログを、完全な実行証拠として追記しない
	if( file_ == INVALID_HANDLE_VALUE || failed_) {
		return ;
	}
	std::array<char, 384> line{} ; // 固定名とenum/boolean/nullだけの有界な一行
	int					  size = 0 ; // snprintfの実byte数。範囲検証してからOSへ渡す
	// 結果DTOがある場合だけ、実際の処理で確定した実行経路を記録する
	if( execution.resultAvailable) {
		size = std::snprintf( line.data(), line.size(),
							 "{\"schema_version\":2,\"event\":\"job_execution\",\"elapsed_ms\":%llu,"
							 "\"id\":%llu,\"result_available\":true,\"asr_model\":%u,\"asr_profile\":%u,"
							 "\"used_gpu\":%s,\"cpu_retried\":%s}\n",
							 static_cast<unsigned long long>( GetTickCount64() - startedMs_),
							 static_cast<unsigned long long>( id), static_cast<unsigned>( execution.model),
							 static_cast<unsigned>( execution.profile), execution.usedGpu ? "true" : "false",
							 execution.cpuRetried ? "true" : "false") ;
	} else {
		// 失敗/取消し等の結果なしを、既定flagのfalseからCPU成功へ読み替えない
		size = std::snprintf( line.data(), line.size(),
							 "{\"schema_version\":2,\"event\":\"job_execution\",\"elapsed_ms\":%llu,"
							 "\"id\":%llu,\"result_available\":false,\"asr_model\":null,\"asr_profile\":null,"
							 "\"used_gpu\":null,\"cpu_retried\":null}\n",
							 static_cast<unsigned long long>( GetTickCount64() - startedMs_),
							 static_cast<unsigned long long>( id)) ;
	}
	// 切詰めや部分書込みを正常なイベントとして保持しない
	if( size <= 0 || static_cast<std::size_t>( size) >= line.size()) {
		failed_ = true ;
		return ;
	}
	writeLine( std::string_view( line.data(), static_cast<std::size_t>( size))) ;
}

namespace {
/** @brief 私有formatterだけが固定fieldをASCIIへ変換します */
class ScalarLine final {
	public:
	/// 一行、上限検査はwriterでも行います
	std::string data ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定eventと時刻から開始します
	 * @param[in] event 内部固定名
	 * @param[in] time 実受領時刻ms
	 */
	//------------------------------------------------------------------------------
	ScalarLine( const char *event, std::uint64_t time) : data( "{\"schema_version\":3,\"event\":\"")
	{
		data += event ;
		data += "\",\"elapsed_ms\":" ;
		data += std::to_string( time) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定fieldを加えます
	 * @param[in] key 内部固定名
	 * @param[in] value bool/整数/enum
	 */
	//------------------------------------------------------------------------------
	template <class T> void add( const char *key, T value)
	{
		data += ",\"" ;
		data += key ;
		data += "\":" ;
		// booleanと整数を別のJSON型として保持します
		if constexpr( std::is_same_v<T, bool>) {
			data += value ? "true" : "false" ;
		} else if constexpr( std::is_enum_v<T>) {
			data += std::to_string( static_cast<std::underlying_type_t<T>>( value)) ;
		} else {
			data += std::to_string( value) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 未観測をnullで加えます
	 * @param[in] key 固定名
	 * @param[in] value 任意scalar
	 */
	//------------------------------------------------------------------------------
	template <class T> void add( const char *key, std::optional<T> value)
	{
		// 未観測を既知0に変換しません
		if( value) {
			add( key, *value) ;
		} else {
			data += ",\"" ;
			data += key ;
			data += "\":null" ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief compiled code hashだけを加えます
	 * @param[in] key 固定名
	 * @param[in] value compiled SHA
	 */
	//------------------------------------------------------------------------------
	void pin( const char *key, const std::string &value)
	{
		data += ",\"" ;
		data += key ;
		data += "\":" ;
		// compiled APIの空pinは未観測です
		if( value.empty()) {
			data += "null" ;
			return ;
		}
		// 任意文字列を外へ通さず正確な64hexだけを許します
		if( value.size() != 64 || value.find_first_not_of( "0123456789abcdef") != std::string::npos) {
			throw std::invalid_argument( "compiled_pin") ;
		}
		data += "\"" ;
		data += value ;
		data += "\"" ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 一行を閉じます
	 * @return 内部formatterのASCII
	 */
	//------------------------------------------------------------------------------
	std::string_view finish()
	{
		data += "}\n" ;
		return data ;
	}
} ;
//------------------------------------------------------------------------------
/**
 * @brief 同じopの固定IDを付けます
 * @param[in,out] line formatter
 * @param[in] history 元ID
 * @param[in] operation 元op
 */
//------------------------------------------------------------------------------
void operationFields( ScalarLine &line, std::uint64_t history, std::uint64_t operation)
{
	line.add( "history_id", history) ;
	line.add( "operation_id", operation) ;
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 私有の固定formatter出力だけを書きます
 * @param[in] line 内部ASCII、同期borrow
 * @see koefude::DiagnosticLog::writeLine
 */
//------------------------------------------------------------------------------
void DiagnosticLog::writeLine( std::string_view line) noexcept
{
	// 短書込みや全上限をstickyにし、切詰めません
	if( file_ == INVALID_HANDLE_VALUE || failed_) {
		return ;
	}
	// 加算前に全上限を検査します
	if( line.empty() || line.size() > 4096 || lines_ >= 10000 || line.size() > 4194304 - bytes_) {
		failed_ = true ;
		return ;
	}
	DWORD written = 0 ; // 実書込み数
	// 本処理へはI/O失敗を伝播しません
	if( !WriteFile( file_, line.data(), static_cast<DWORD>( line.size()), &written, nullptr) || written != line.size()) {
		failed_ = true ;
		return ;
	}
	bytes_ += written ;
	++lines_ ;
}
//------------------------------------------------------------------------------
/**
 * @brief 同じ履歴行と生成操作の追跡slotを検索し、別系列のcontrolを除外します
 * @param[in] historyId 受付時に登録した履歴行ID
 * @param[in] operationId 同じ受付に対応する生成操作ID
 * @return 一致するtracked_要素への非所有pointer。一致しなければnullptrです
 * 解放せず、このinstanceの寿命内で同一操作を直列化して使用します
 * @see koefude::DiagnosticLog::findCorrection
 */
//------------------------------------------------------------------------------
DiagnosticLog::Tracked *DiagnosticLog::findCorrection( std::uint64_t historyId, std::uint64_t operationId) noexcept
{
	// 固定256slot以内だけを元IDで検索します
	for( std::size_t index = 0; index < trackedCount_; ++index) {
		auto &item = tracked_[index] ; /* 固定slot */
		// control系列は生成系列とは別です
		if( item.historyId == historyId && item.operationId == operationId && item.actionId == 0) {
			return &item ;
		}
	}
	return nullptr ;
}
//------------------------------------------------------------------------------
/**
 * @brief 明示診断modeを開始し、session情報とcompiled三slotの実pinを記録します
 * @pre 呼出し側が明示診断用の保存領域を隔離済みで、一sessionに一回だけ呼ぶこと
 * @post 記録に失敗してもcurrentMode()はtrueを保持し、通常領域への切替えを防ぎます
 * 二重呼出しは診断失敗として残します。製品の処理を開始する関数ではありません
 * @see koefude::DiagnosticLog::beginCurrent
 */
//------------------------------------------------------------------------------
void DiagnosticLog::beginCurrent() noexcept
{
	// 通常起動ではこの入口を呼びません
	if( current_) {
		failed_ = true ;
		return ;
	}
	current_ = true ; // I/O失敗でも明示modeのroot隔離を解除しません
	// 記録不能でも製品は隔離したまま処理を継続します
	if( !healthy()) {
		failed_ = true ;
		return ;
	}
	try {
		ScalarLine session( "acceptance_session", GetTickCount64() - startedMs_) ; /* 固定宣言 */
		session.add( "generation", 1287) ;
		session.add( "diagnostic_mode", true) ;
		session.add( "history_isolated", true) ;
		session.add( "model_writes_isolated", true) ;
		writeLine( session.finish()) ;
		// 既存APIの実compiled pinを三slot採りcapabilityを捏造しません
		for( std::uint32_t slot = 0; slot < 3; ++slot) {
			const auto pin = wholeTextRuntimePin( static_cast<WholeTextRuntimeVariant>( slot)) ; // 実compiled値
			ScalarLine line( "runtime_pin", GetTickCount64() - startedMs_) ; // 固定metadata
			line.add( "slot", slot) ;
			line.add( "available", pin.available) ;
			line.add( "configuration", pin.configuration == "Debug" ? 0 : 1) ;
			line.add( "dll_bytes", pin.available ? std::optional<std::uint64_t>( pin.dllBytes) : std::nullopt) ;
			line.pin( "dll_sha256", pin.available ? pin.dllSha256 : std::string{}) ;
			line.pin( "receipt_sha256", pin.available ? pin.receiptSha256 : std::string{}) ;
			writeLine( line.finish()) ;
		}
	} catch( ...) {
		failed_ = true ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 全文補正の受付成立を追跡し、同じ操作の後続終端を照合できるようにします
 * @param[in] historyId 対象の履歴行ID。0不可
 * @param[in] operationId 今回受け付けた生成操作ID。0不可。同じ組の重複登録は不可
 * @param[in] variant compiled runtimeの固定slot番号。0～2
 * @param[in] previous 受付前の有効全文種別。0はなし、1は旧V1、2はbounded全文です
 * @note 通常modeでは何もせず、不正値・重複・追跡上限は診断だけを失敗にします
 * @see koefude::DiagnosticLog::correctionAccepted
 */
//------------------------------------------------------------------------------
void DiagnosticLog::correctionAccepted( std::uint64_t historyId, std::uint64_t operationId, std::uint32_t variant,
									   std::uint32_t previous) noexcept
{
	// 通常時はcaptureも記録も行いません
	if( !current_) {
		return ;
	}
	// stale/重複/追跡上限は成功へ変えません
	if( finished_ || !historyId || !operationId || variant > 2 || previous > 2 ||
	   findCorrection( historyId, operationId) || trackedCount_ + asrCount_ >= tracked_.size()) {
		failed_ = true ;
		return ;
	}
	tracked_[trackedCount_++] = {historyId, operationId, 0, 0, false} ;
	try {
		ScalarLine line( "correction_accepted", GetTickCount64() - startedMs_) ; /* 受付事実 */
		operationFields( line, historyId, operationId) ;
		line.add( "runtime_variant", variant) ;
		line.add( "previous_full_kind", previous) ;
		line.add( "original_present", true) ;
		writeLine( line.finish()) ;
	} catch( ...) {
		failed_ = true ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief UIが受領した同じ生成操作の終端を、生成結果と実保存結果に分けて記録します
 * @param[in] historyId correctionAcceptedに登録した履歴行ID
 * @param[in] operationId 同じ受付に対応する生成操作ID。controlのaction IDではありません
 * @param[in] snapshot workerの観測事実を保持する固定診断値。呼出し中だけ借用します
 * @param[in] save 同じ保存ACKからUIが投影したscalar。未観測を成功へ補完しません
 * @param[in] terminal 1は完全保存成功、2は生成失敗、3は取消し、4は保存失敗、5は未dispatch
 * @param[in] error 元の製品エラー分類。未通知はnulloptで、成功を推定しません
 * @param[in] code 元の診断code。未通知はnulloptで、既知の0と区別します
 * @note 通常modeでは何もせず、未知/二重終端や不正値は診断だけを失敗にします
 * 本文、path、任意例外文は引数へ含めず、生成や保存を再実行しません
 * @see koefude::DiagnosticLog::correctionTerminal
 */
//------------------------------------------------------------------------------
void DiagnosticLog::correctionTerminal( std::uint64_t historyId, std::uint64_t operationId,
									   const CorrectionDiagnosticSnapshot &snapshot,
									   const DiagnosticCorrectionSave &save, std::uint32_t terminal,
									   std::optional<ErrorKind> error, std::optional<std::uint32_t> code) noexcept
{
	// 通常起動と追跡外通知を区別します
	if( !current_) {
		return ;
	}
	auto *item = findCorrection( historyId, operationId) ; // 受付済みslot
	// 二重・未知終端から成功を作りません
	if( !item || item->terminal || finished_) {
		failed_ = true ;
		return ;
	}
	// 外部自由値を整形せず、固定enum/上限をwriter前に照合します
	if( terminal < 1 || terminal > 5 || save.state > 3 || (save.attempt && *save.attempt > 6) ||
	   (save.activeKind && *save.activeKind > 2) || (save.error && *save.error > 7) ||
	   (error && static_cast<unsigned>( *error) > 13) || static_cast<unsigned>( snapshot.outcome) > 3 ||
	   snapshot.nativeCalls > 16384 || snapshot.nativeCompletions > snapshot.nativeCalls ||
	   static_cast<unsigned>( snapshot.whisperCpu.variant) > 2 ||
	   static_cast<unsigned>( snapshot.whisperCpu.reason) > 3) {
		failed_ = true ;
		return ;
	}
	// 6phaseも未定義enum/率を行へ出しません
	for( const auto &phase : snapshot.phases) {
		// 不正観測は診断だけを失敗させます
		if( phase.observed && (static_cast<unsigned>( phase.stage) > 7 || (phase.percent && *phase.percent > 100))) {
			failed_ = true ;
			return ;
		}
	}
	item->terminal = true ;
	try {
		// 実workerを観測した場合だけ開始を書きます
		if( snapshot.workerStarted) {
			ScalarLine line( "correction_started", GetTickCount64() - startedMs_) ; /* sticky開始 */
			operationFields( line, historyId, operationId) ;
			line.add( "worker_started", true) ;
			writeLine( line.finish()) ;
		}
		// 何chunkでも最新6phase以内だけを書きます
		for( std::uint32_t phase = 0; phase < snapshot.phases.size(); ++phase) {
			const auto &value = snapshot.phases[phase] ; /* 実phase */
			// 未観測phaseに架空の進捗を作りません
			if( value.observed) {
				ScalarLine line( "correction_progress", GetTickCount64() - startedMs_) ; /* 最新だけ */
				operationFields( line, historyId, operationId) ;
				line.add( "phase", phase) ;
				line.add( "stage", value.stage) ;
				line.add( "percent", value.percent) ;
				writeLine( line.finish()) ;
			}
		}
		const auto &v = snapshot ; // 生成と保存は別行です
		ScalarLine	generation( "correction_generation_terminal",
							   GetTickCount64() - startedMs_) ; // receive前に固定した生成終端の一行
		operationFields( generation, historyId, operationId) ;
		generation.add( "dispatch_started", v.dispatchStarted) ;
		generation.add( "outcome", v.outcome) ;
		generation.add( "plan_observed", v.planObserved) ;
		generation.add( "planned_records", v.plannedRecords) ;
		generation.add( "planned_generations", v.plannedGenerations) ;
		generation.add( "completed_records", v.completedRecords) ;
		generation.add( "original_bytes", v.originalBytes) ;
		generation.add( "committed_core_end", v.committedCoreEnd) ;
		generation.add( "committed_body_bytes", v.committedBodyBytes) ;
		generation.add( "failed_index", v.failedIndex) ;
		generation.add( "native_generation_calls", v.nativeCalls) ;
		generation.add( "native_generation_completions", v.nativeCompletions) ;
		generation.add( "backend_observed", v.backend) ;
		generation.add( "error_kind", v.errorKind) ;
		generation.add( "code", v.code) ;
		generation.add( "native_status", v.nativeStatus) ;
		generation.add( "native_diagnostic", v.nativeDiagnostic) ;
		generation.add( "native_engine_code", v.nativeEngineCode) ;
		generation.add( "resource_reason", v.resourceReason) ;
		generation.add( "resource_code", v.resourceCode) ;
		generation.add( "close_attempted", v.closeAttempted) ;
		generation.add( "close_status", v.closeStatus) ;
		generation.add( "close_diagnostic", v.closeDiagnostic) ;
		generation.add( "close_engine_code", v.closeEngineCode) ;
		generation.add( "release_confirmed", v.releaseConfirmed) ;
		generation.add( "restart_required", v.restartRequired) ;
		generation.add( "generation_wall_ms", v.generationWallMs) ;
		generation.add( "whisper_cpu_variant", v.whisperCpu.variant) ;
		generation.add( "whisper_cpu_reason", v.whisperCpu.reason) ;
		const auto digestEnd =
			std::find( v.whisperCpu.buildSha.begin(), v.whisperCpu.buildSha.end(), '\0') ; // 固定容量のみ
		// 不正な非終端値を自由文字列としてwriterへ通しません
		if( digestEnd == v.whisperCpu.buildSha.end()) {
			throw std::invalid_argument( "whisper_cpu_pin") ;
		}
		generation.pin( "whisper_cpu_build_sha256", std::string( v.whisperCpu.buildSha.begin(), digestEnd)) ;
		generation.add( "whisper_cpu_close_attempted", v.whisperCpu.closeAttempted) ;
		generation.add( "whisper_cpu_free_status", v.whisperCpu.freeStatus) ;
		generation.add( "whisper_cpu_module_closed", v.whisperCpu.moduleClosed) ;

		writeLine( generation.finish()) ;
		const auto &resource = v.resources ; // 追加probeなし
		ScalarLine	resources( "correction_resources",
							  GetTickCount64() - startedMs_) ; // 既存probeの受動標本だけを記録する一行
		operationFields( resources, historyId, operationId) ;
		resources.add( "probe_status", resource.status) ;
		resources.add( "samples", resource.samples) ;
		resources.add( "sampled_peak_working_bytes", resource.peakWorking) ;
		resources.add( "sampled_peak_private_bytes", resource.peakPrivate) ;
		resources.add( "min_available_physical_bytes", resource.minPhysical) ;
		resources.add( "min_available_commit_bytes", resource.minCommit) ;
		resources.add( "max_sample_gap_ms", resource.maxGapMs) ;
		resources.add( "baseline_working_bytes", resource.baselineWorking) ;
		resources.add( "baseline_private_bytes", resource.baselinePrivate) ;
		resources.add( "working_limit_bytes", resource.workingLimit) ;
		resources.add( "private_limit_bytes", resource.privateLimit) ;

		writeLine( resources.finish()) ;
		ScalarLine saved( "correction_save_terminal",
						 GetTickCount64() - startedMs_) ; // 生成と分離した保存receiptの一行
		operationFields( saved, historyId, operationId) ;
		saved.add( "action_id", 0) ;
		saved.add( "save_state", save.state) ;
		saved.add( "attempt_state", save.attempt) ;
		saved.add( "storage_revision", save.revision) ;
		saved.add( "original_retained", save.originalRetained) ;
		saved.add( "previous_full_retained", save.previousRetained) ;
		saved.add( "active_full_kind", save.activeKind) ;
		saved.add( "error_kind", save.error) ;
		saved.add( "code", save.code) ;

		writeLine( saved.finish()) ;
		ScalarLine done( "correction_finished", GetTickCount64() - startedMs_) ; // 終端
		operationFields( done, historyId, operationId) ;
		done.add( "job_terminal", terminal) ;
		done.add( "error_kind", error) ;
		done.add( "code", code) ;
		writeLine( done.finish()) ;
		// invalid snapshotを健康なログへ見せません
		if( !snapshot.valid) {
			failed_ = true ;
		}
	} catch( ...) {
		failed_ = true ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief control受付だけを記録します
 * @param[in] value 固定ID/actionだけ
 * @see koefude::DiagnosticLog::controlAccepted
 */
//------------------------------------------------------------------------------
void DiagnosticLog::controlAccepted( const DiagnosticCorrectionControl &value) noexcept
{
	// 普通起動に記録を追加しません
	if( !current_) {
		return ;
	}
	// 一つのcontrol系列を別IDで追跡します
	if( finished_ || !value.historyId || !value.operationId || !value.actionId || value.action > 1 ||
	   trackedCount_ + asrCount_ >= tracked_.size()) {
		failed_ = true ;
		return ;
	}
	tracked_[trackedCount_++] = {value.historyId, value.operationId, value.actionId, value.action, false} ;
	try {
		ScalarLine line( "correction_control_accepted", GetTickCount64() - startedMs_) ; /* 受付 */
		operationFields( line, value.historyId, value.operationId) ;
		line.add( "action_id", value.actionId) ;
		line.add( "action", value.action) ;
		writeLine( line.finish()) ;
	} catch( ...) {
		failed_ = true ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief control専用slotの実結果を書きます
 * @param[in] value 固定scalarだけ
 * @see koefude::DiagnosticLog::controlTerminal
 */
//------------------------------------------------------------------------------
void DiagnosticLog::controlTerminal( const DiagnosticCorrectionControl &value) noexcept
{
	// 普通起動では永続化しません
	if( !current_) {
		return ;
	}
	Tracked *found = nullptr ; // 固定slot検索
	// 最大256件の同IDだけを照合します
	for( std::size_t index = 0; index < trackedCount_; ++index) {
		auto &item = tracked_[index] ; /* 固定slot */
		// actionまで一致する受付だけを終端化します
		if( item.historyId == value.historyId && item.operationId == value.operationId &&
		   item.actionId == value.actionId && item.action == value.action && !item.terminal) {
			found = &item ;
			break ;
		}
	}
	// stale/二重通知を拒否します
	if( !found || finished_ || value.save.state < 1 || value.save.state > 3 ||
	   (value.save.attempt && *value.save.attempt > 6) || (value.save.error && *value.save.error > 7) ||
	   (value.committed && (!value.save.revision || value.save.state != 1))) {
		failed_ = true ;
		return ;
	}
	found->terminal = true ;
	try {
		ScalarLine line( "correction_control_terminal", GetTickCount64() - startedMs_) ; /* 実receipt */
		operationFields( line, value.historyId, value.operationId) ;
		line.add( "action_id", value.actionId) ;
		line.add( "action", value.action) ;
		line.add( "metadata_committed", value.committed) ;
		line.add( "save_state", value.save.state) ;
		line.add( "storage_revision", value.save.revision) ;
		line.add( "attempt_state", value.save.attempt) ;
		line.add( "original_retained", value.save.originalRetained) ;
		line.add( "previous_full_retained", value.save.previousRetained) ;
		line.add( "cleanup_complete", value.cleanupComplete) ;
		line.add( "residual_bytes", value.residualBytes) ;
		line.add( "error_kind", value.save.error) ;
		line.add( "code", value.save.code) ;
		writeLine( line.finish()) ;
	} catch( ...) {
		failed_ = true ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 保存履歴の復元事実を、新しい生成受付や生成成功と区別して記録します
 * @param[in] historyId 復元した履歴行ID。0不可
 * @param[in] operationId 保存されていた生成操作ID。0不可。新規IDを発行しません
 * @param[in] attempt 保存済みWholeTextBoundedAttemptStateの固定数値
 * @param[in] active 保存済みWholeTextActiveFullKindの固定数値
 * @param[in] prefix 実際に復元したprefixのrecord件数。未観測はnulloptで、0件と区別します
 * @param[in] complete 保存済みの完全本文参照が存在する場合trueです
 * @note 呼出し側で固定数値を検証します。最大32件を記録し、通常modeでは何もしません
 * @see koefude::DiagnosticLog::correctionRestored
 */
//------------------------------------------------------------------------------
void DiagnosticLog::correctionRestored( std::uint64_t historyId, std::uint64_t operationId, std::uint32_t attempt,
									   std::uint32_t active, std::optional<std::uint32_t> prefix,
									   bool complete) noexcept
{
	// 復元は生成受付へ数えません
	if( !current_) {
		return ;
	}
	// 上限後も製品復元は継続します
	if( restoredCount_ >= 32 || !historyId || !operationId) {
		failed_ = true ;
		return ;
	}
	++restoredCount_ ;
	try {
		ScalarLine line( "correction_restored", GetTickCount64() - startedMs_) ; /* 保存事実だけ */
		operationFields( line, historyId, operationId) ;
		line.add( "attempt_state", attempt) ;
		line.add( "active_full_kind", active) ;
		line.add( "prefix_records", prefix) ;
		line.add( "complete_reference", complete) ;
		writeLine( line.finish()) ;
	} catch( ...) {
		failed_ = true ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 全workerの排出後に未完操作と未保存件数を記録し、診断sessionを閉じます
 * @param[in] unsaved UIが数えた未保存補正の件数。履歴上限内の0～32件
 * @param[in] disposition 利用者が実際に選んだ終了方法の固定数値。未観測はnulloptです
 * @pre workerの終端と保存ACKの回収を済ませ、以後新しい操作を登録しないこと
 * @note 通常modeと終了済みsessionでは何もしません。未完操作を成功へ変えません
 * @see koefude::DiagnosticLog::finishCurrent
 */
//------------------------------------------------------------------------------
void DiagnosticLog::finishCurrent( std::uint32_t unsaved, std::optional<std::uint32_t> disposition) noexcept
{
	// 普通起動と二重終了は記録しません
	if( !current_ || finished_) {
		return ;
	}
	std::uint32_t corrections = 0 ; // 実未完生成
	std::uint32_t controls = 0 ; // 実未完control
	// 未受領を成功へ補わず件数として残します
	for( std::size_t index = 0; index < trackedCount_; ++index) {
		const auto &item = tracked_[index] ; /* 受付slot */
		// 未完だけを集計します
		if( !item.terminal) {
			item.actionId ? ++controls : ++corrections ;
		}
	}
	try {
		ScalarLine line( "acceptance_finish", GetTickCount64() - startedMs_) ; /* 実終了事実 */
		line.add( "unfinished_corrections", corrections) ;
		line.add( "pending_controls", controls) ;
		line.add( "unsaved_corrections", unsaved) ;
		line.add( "close_disposition", disposition) ;
		writeLine( line.finish()) ;
		finished_ = true ;
	} catch( ...) {
		failed_ = true ;
	}
}
} // namespace koefude
