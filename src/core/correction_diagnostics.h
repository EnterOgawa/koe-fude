//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file correction_diagnostics.h
 * @brief 本文を持たない固定容量の受動補正診断を定義します
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1287の明示診断を追加
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1301の補助CPU接続と限定検証を追加
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1304の最初の時刻拒否を固定容量へ追加
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/types.h"
#include "core/whisper_timeline_diagnostic.h"
#include "core/whole_text_bounded_result.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>
namespace koefude {
/** @brief 補助Whisperの実選択です。未実行はUnknownを保持します */
enum class WhisperCpuVariant : std::uint32_t {
	/// 未選択
	UNKNOWN,
	/// 既存静的CPU表
	PORTABLE,
	/// CPU/OS確認後の単一DLL
	AVX2
} ;
/** @brief 本文やpathを持たない選択理由です */
enum class WhisperCpuReason : std::uint32_t {
	/// 未選択
	UNKNOWN,
	/// CPU/OSが非適格又は不明
	CPU_UNSUPPORTED,
	/// portable専用build
	SLOT_ABSENT,
	/// CPU/資産/ABIが成立
	ELIGIBLE
} ;
/** @brief 固定容量の実選択と解放結果です */
struct WhisperCpuDiagnostic {
	/// 実選択だけを記録
	WhisperCpuVariant			 variant = WhisperCpuVariant::UNKNOWN ;
	/// portable理由を区別
	WhisperCpuReason			 reason = WhisperCpuReason::UNKNOWN ;
	/// fast DLLの完成digest、portableは空
	std::array<char, 65>		 buildSha{} ;
	/// 一回cleanupの実観測
	bool						 closeAttempted = false ;
	/// contextがある場合だけ固定ABI status
	std::optional<std::uint32_t> freeStatus ;
	/// loadした場合だけFreeLibrary成否
	std::optional<bool>			 moduleClosed ;
} ;
/** @brief 生成と保存を混同しない固定終端です */
enum class CorrectionDiagnosticOutcome : std::uint32_t {
	/// dispatch未開始
	NOT_STARTED,
	/// 保存前の全量候補
	READY_FOR_PERSISTENCE,
	/// 生成失敗
	FAILURE,
	/// 生成取消し
	CANCELLED
} ;
/** @brief 一つのphaseの最新実測だけを保持します */
struct CorrectionDiagnosticPhase {
	/// 実通知の有無
	bool						 observed = false ;
	/// 実通知段階
	Stage						 stage = Stage::VALIDATING ;
	/// 未知はnull、既知0は0
	std::optional<std::uint32_t> percent ;
} ;
/** @brief 既存probeの標本だけを集約する値です */
struct CorrectionDiagnosticResources {
	/// 未観測0、観測1、部分失敗2
	std::uint32_t				 status = 0 ;
	/// 実成功標本数
	std::uint64_t				 samples = 0 ;
	/// 標本最大byte
	std::optional<std::uint64_t> peakWorking ;
	/// 標本最大byte
	std::optional<std::uint64_t> peakPrivate ;
	/// 標本最小byte
	std::optional<std::uint64_t> minPhysical ;
	/// 標本最小byte
	std::optional<std::uint64_t> minCommit ;
	/// 標本間最大ms、1標本以下は未知
	std::optional<std::uint64_t> maxGapMs ;
	/// 既存guardの開始byte
	std::optional<std::uint64_t> baselineWorking ;
	/// 既存guardの開始byte
	std::optional<std::uint64_t> baselinePrivate ;
	/// 既存guardの上限byte
	std::optional<std::uint64_t> workingLimit ;
	/// 既存guardの上限byte
	std::optional<std::uint64_t> privateLimit ;
	/// 同じ開始snapshotのOS usable total
	std::optional<std::uint64_t> baselineUsableTotal ;
	/// 同じ開始snapshotのphysical available
	std::optional<std::uint64_t> baselineAvailablePhysical ;
	/// 同じ開始snapshotのcommit available
	std::optional<std::uint64_t> baselineAvailableCommit ;
} ;
/** @brief ownerや本文を一切含まない固定snapshotです */
struct CorrectionDiagnosticSnapshot {
	/// 診断失敗はsticky、製品には伝播しません
	bool									 valid = true ;
	/// 実runner入口
	bool									 workerStarted = false ;
	/// 予約後の実dispatch入口
	bool									 dispatchStarted = false ;
	/// receive前の終端固定
	bool									 sealed = false ;
	/// 候補状態
	CorrectionDiagnosticOutcome				 outcome = CorrectionDiagnosticOutcome::NOT_STARTED ;
	/// 各phase最新一件
	std::array<CorrectionDiagnosticPhase, 6> phases{} ;
	/// 完成計画の有無
	bool									 planObserved = false ;
	/// 実件数
	std::optional<std::uint32_t>			 plannedRecords ;
	/// 実生成予定数
	std::optional<std::uint32_t>			 plannedGenerations ;
	/// 確定prefix件数
	std::optional<std::uint32_t>			 completedRecords ;
	/// 全core終端byte
	std::optional<std::uint64_t>			 originalBytes ;
	/// 確定core終端byte
	std::optional<std::uint64_t>			 committedCoreEnd ;
	/// 確定本文byte
	std::optional<std::uint64_t>			 committedBodyBytes ;
	/// 未完current
	std::optional<std::uint32_t>			 failedIndex ;
	/// 実call数、未開始も既知0
	std::uint32_t							 nativeCalls = 0 ;
	/// 成功receipt数
	std::uint32_t							 nativeCompletions = 0 ;
	/// 成功receiptがある場合だけCPU0
	std::optional<std::uint32_t>			 backend ;
	/// 成功には既定Internalを付けません
	std::optional<ErrorKind>				 errorKind ;
	/// primary元code
	std::optional<std::uint32_t>			 code ;
	/// native元status
	std::optional<std::uint32_t>			 nativeStatus ;
	/// native元診断
	std::optional<std::uint32_t>			 nativeDiagnostic ;
	/// 元符号付き値
	std::optional<std::int32_t>				 nativeEngineCode ;
	/// 元guard理由
	std::optional<std::uint32_t>			 resourceReason ;
	/// 元guard code
	std::optional<std::uint32_t>			 resourceCode ;
	/// 明示close
	bool									 closeAttempted = false ;
	/// 元close status
	std::optional<std::uint32_t>			 closeStatus ;
	/// 元close診断
	std::optional<std::uint32_t>			 closeDiagnostic ;
	/// 元close符号付き値
	std::optional<std::int32_t>				 closeEngineCode ;
	/// 実close観測のみ
	std::optional<bool>						 releaseConfirmed ;
	/// 実close観測のみ
	std::optional<bool>						 restartRequired ;
	/// dispatchからreceive前まで
	std::optional<std::uint64_t>			 generationWallMs ;
	/// 受動標本
	CorrectionDiagnosticResources			 resources ;
	/// 補助専用、既存provider/profile/historyに含めません
	WhisperCpuDiagnostic					 whisperCpu ;
	/// 未観測はnull、最初の一件
	std::optional<WhisperTimelineFailure>	 whisperTimelineFailure ;
} ;
/** @brief 明示診断要求だけが受付前に確保する固定captureです */
class CorrectionDiagnosticCapture final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 最初の拒否scalarだけを保持し、診断失敗をsticky不完全へ変換します
	 * @param[in] failure 同期borrowの固定値。本文や所有pointerを持ちません
	 * @note mutex内はscalar copyだけです。後続cleanup/sealは記録を消しません
	 */
	//------------------------------------------------------------------------------
	void whisperTimelineFailed( const WhisperTimelineFailure &failure) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 実選択を固定容量へcopyします
	 * @param[in] variant 実表
	 * @param[in] reason 選択理由
	 * @param[in] digest fast完成SHA又は空
	 */
	//------------------------------------------------------------------------------
	void whisperSelected( WhisperCpuVariant variant, WhisperCpuReason reason, std::string_view digest) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同期scopeの一回cleanup結果を記録します
	 * @param[in] freeStatus context解放status、未作成はnull
	 * @param[in] moduleClosed load済moduleの成否、未loadはnull
	 */
	//------------------------------------------------------------------------------
	void whisperClosed( std::optional<std::uint32_t> freeStatus, std::optional<bool> moduleClosed) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 実worker開始を固定します
	 */
	//------------------------------------------------------------------------------
	void workerStarted() noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 予約後のdispatch入口で時刻を固定します
	 */
	//------------------------------------------------------------------------------
	void dispatchStarted() noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 集約前の実進捗を採ります
	 * @param[in] value 実通知
	 */
	//------------------------------------------------------------------------------
	void progress( Progress value) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief native call直前を記録します
	 */
	//------------------------------------------------------------------------------
	void nativeCall() noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 成功した生成receipt直後を記録します
	 */
	//------------------------------------------------------------------------------
	void nativeCompleted() noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief receiveへ移す前に一度だけ固定します
	 * @param[in] result 元終端、同期borrow
	 */
	//------------------------------------------------------------------------------
	void seal( const WholeTextBoundedResult &result) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 最新scalarをcopyします
	 * @return 本文なしの固定値
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] CorrectionDiagnosticSnapshot snapshot() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じprobe標本を集約します
	 * @param[in] working 当process byte
	 * @param[in] privateBytes private byte
	 * @param[in] physical available byte
	 * @param[in] commit available byte
	 */
	//------------------------------------------------------------------------------
	void sample( std::uint64_t working, std::uint64_t privateBytes, std::uint64_t physical,
				std::uint64_t commit) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 元probe失敗だけを保持します
	 */
	//------------------------------------------------------------------------------
	void probeFailed() noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 既存guardの同じ開始snapshotを追加照会なしでcopyします
	 * @param[in] working 開始working set byte
	 * @param[in] privateBytes 開始private byte
	 * @param[in] workingLimit 既存working set上限byte
	 * @param[in] privateLimit 既存private上限byte
	 * @param[in] usableTotal 同じ開始snapshotのOS usable total、未提供はnull
	 * @param[in] physical 同じ開始snapshotのavailable physical、未提供はnull
	 * @param[in] commit 同じ開始snapshotのavailable commit、未提供はnull
	 */
	//------------------------------------------------------------------------------
	void guardLimits( std::uint64_t working, std::uint64_t privateBytes, std::uint64_t workingLimit,
					 std::uint64_t privateLimit, std::optional<std::uint64_t> usableTotal = std::nullopt,
					 std::optional<std::uint64_t> physical = std::nullopt,
					 std::optional<std::uint64_t> commit = std::nullopt) noexcept ;

	private:
	friend struct CurrentCorrectionDiagnosticCaptureTestAccess ; // 固定counterの内部境界試験
	/// mutex取得失敗もsticky不完全へ伝えます
	mutable std::atomic<bool>							 failed_ = false ;
	/// 記録不能後も最初の試行を固定
	std::atomic<bool>									 timelineFailureAttempted_ = false ;
	/// copy/setだけ、I/Oを禁止
	mutable std::mutex									 mutex_ ;
	/// 固定容量
	CorrectionDiagnosticSnapshot						 value_ ;
	/// 予約後の原点
	std::chrono::steady_clock::time_point				 dispatchTime_{} ;
	/// 標本gap原点
	std::optional<std::chrono::steady_clock::time_point> lastSample_ ;
} ;
} // namespace koefude
