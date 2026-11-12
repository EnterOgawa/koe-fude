//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_application.h
 * @brief 同じPCMから補助認識とnative全文へ進む通常workflowを定義します
 * @version 1.0.0
 * @date 2026/10/19
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/19 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/19 通常workerと同期試験seamを追加
 * 0.1.0 Shunji Ogawa 2026/10/19 一回実測の受動観測と証拠保持を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 固定AVX2方式と通常workerの検証接続を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1285の有界逐次補正を接続
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1287の明示診断と隔離接続を追加
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1313の明示16GB入口と段階共通guardを追加
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1319の生成前単一core受入契約を追加
 * 0.1.0 Shunji Ogawa 2026/10/22 ADR1333の所有monitorと独立処理停止を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1343の文章専用補正を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/job_controller.h"
#include "inference/model_preparation.h"
#include "inference/whole_text_application_resources.h"
#include "inference/whole_text_bounded_corrector.h"
#include "inference/whole_text_native_preparation.h"
#include "inference/whole_text_resource_guard.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace koefude {
struct WholeTextApplicationObservation ; // 任意の受動出力、実行依存を選びません
/** @brief 確認済みEXEから固定する有限rootです */
struct WholeTextApplicationConfig {
	/// EXE parentの固定basename
	std::filesystem::path					   nativeModule ;
	/// EXE/models
	std::filesystem::path					   bundleRoot ;
	/// 標準ユーザーcache
	std::filesystem::path					   cacheRoot ;
	/// AVX2固有basename、旧aggregateの末尾
	std::filesystem::path					   avx2NativeModule ;
	/// 共有寿命の通常保存port
	std::shared_ptr<IWholeTextPersistencePort> persistence ;
	/// 明示試験だけcache Missing参照規則
	bool									   diagnosticReferenceOnly = false ;
	/// 固有第三slot、旧aggregate末尾
	std::filesystem::path					   ministral16gbNativeModule ;
	/// 明示probeだけの生成前上限、通常0
	std::uint32_t							   textContextGenerationLimit = 0 ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 全PCMを26秒以下で隙間なく被覆します
 * @param[in] sampleCount 実全sample数
 * @return 最大50000の連続窓
 * @throws OperationError 0/表現境界/窓数上限違反、確保前に拒否
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<AsrSampleWindow> makeWholeTextApplicationWindows( std::uint64_t sampleCount) ;
//------------------------------------------------------------------------------
/**
 * @brief 実decoder/ASR/検証/nativeを一回の通常workerへ接続します
 * @param[in] request 非0opと原文/元path
 * @param[in] config 不変root
 * @param[in] stop 協調停止
 * @param[in] progress phase付き通知
 * @param[out] observation 任意出力、null可。同期終了まで有効、終了後だけ読取可能
 * @return 全量を処理した未検証応答、controllerで最終検証
 */
//------------------------------------------------------------------------------
[[nodiscard]] JobOutput					   runWholeTextApplicationJob( const WholeTextCorrectionJobRequest &request,
																	  const WholeTextApplicationConfig &config, std::stop_token stop,
																	  const ProgressCallback		  &progress,
																	  // この同期境界で所有する値、または確定状態の検証値です
																	  WholeTextApplicationObservation *observation = nullptr) ;
//------------------------------------------------------------------------------
/**
 * @brief 通常実依存で有界補正を接続し、保存前terminalだけを返します
 * @param[in] request 原文/元PCM
 * @param[in] config 固定root
 * @param[in] storageBudget 一回消費する全使用量予約
 * @param[in] stop 停止
 * @param[in] progress 同期通知
 * @return 未保存の単独所有terminal。通常dispatchは保存portを経由します
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextBoundedTerminal runBoundedWholeTextApplicationJob( const WholeTextCorrectionJobRequest &request,
																		 const WholeTextApplicationConfig	 &config,
																		 WholeTextBoundedStorageBudget storageBudget,
																		 std::stop_token			   stop,
																		 const ProgressCallback		  &progress) ;
//------------------------------------------------------------------------------
/**
 * @brief 16GB用policyを固定し、再decodeからcloseまで同じ予算で実行します
 * @param[in] request 原文と元PCM identity
 * @param[in] config 固定root
 * @param[in] storageBudget 一回消費する保存予約
 * @param[in] stop 利用者停止
 * @param[in] progress 同期通知
 * @return 原文を保持する未保存terminal
 * @note application APIだけの入口です。実16GB品質受入や割当てhard limitではありません
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextBoundedTerminal runBoundedWholeTextApplication16gbJob(
	const WholeTextCorrectionJobRequest &request, const WholeTextApplicationConfig &config,
	WholeTextBoundedStorageBudget storageBudget, std::stop_token stop, const ProgressCallback &progress) ;
//------------------------------------------------------------------------------
/**
 * @brief 音声を開かず原文だけで有界補正します
 * @param[in] request 新profileとPCM由来metadata付き原文
 * @param[in] config 固定root
 * @param[in] storageBudget 保存予約
 * @param[in] stop 停止
 * @param[in] progress 実進捗
 * @return 保存前terminal。decoder/Whisperを構築せず、既存monitor/closeを完了します
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextBoundedTerminal runTextContextCorrectionApplicationJob(
	const WholeTextCorrectionJobRequest &request, const WholeTextApplicationConfig &config,
	WholeTextBoundedStorageBudget storageBudget, std::stop_token stop, const ProgressCallback &progress) ;
//------------------------------------------------------------------------------
/**
 * @brief 明示試験だけMissing用途を標準cacheへ読取り参照します
 * @param[in] options 固定用途
 * @param[in] reference 読取り専用root
 * @param[in] cache 専用書込みroot
 * @param[in,out] inspector worker同期検査
 * @param[in] stop 停止
 * @return 単一rootで安全に使える計画。混在はcopyなしの明示準備です
 */
//------------------------------------------------------------------------------
[[nodiscard]] ModelPreparationPlan planCorrectionDiagnosticModels( const EngineOptions		  &options,
																  const std::filesystem::path &reference,
																  const std::filesystem::path &cache,
																  IModelInspector &inspector, std::stop_token stop) ;
namespace detail {
/** @brief 有限比較で固定した全量の期待値です。本文や材料の借用を保持しません */
struct WholeTextSingleCoreAdmission {
	/// 元履歴IDとreading SHA
	ReadingSourceKey source ;
	/// clean履歴の全field byte
	std::uint64_t	 originalRecordBytes = 0 ;
	/// clean履歴の全field SHA
	WholeTextDigest	 originalRecordSha256{} ;
	/// 原行の無区切り連結byte
	std::uint64_t	 originalTapeBytes = 0 ;
	/// 全原文tape SHA
	WholeTextDigest	 originalTapeSha256{} ;
	/// 全補助canonical byte
	std::uint64_t	 auxiliaryBytes = 0 ;
	/// 全補助canonical SHA
	WholeTextDigest	 auxiliarySha256{} ;
	/// 全raw行数
	std::uint32_t	 rawRows = 0 ;
	/// 全物理packet数
	std::uint32_t	 canonicalSpans = 0 ;
} ;
/** @brief 未到達と検査中・受入・拒否を区別します */
enum class WholeTextPlanAdmissionState : std::uint32_t {
	/// prepare未成立
	NOT_REACHED,
	/// 全検査成立
	ACCEPTED,
	/// 観測した不一致
	REJECTED,
	/// 検査例外で成立不明
	UNKNOWN
} ;
/** @brief 本文なしで最初の不成立境界を示します */
enum class WholeTextPlanAdmissionReason : std::uint32_t {
	/// 不一致なし又は未到達
	NONE,
	/// 必須期待値不正
	INVALID_REQUIREMENTS,
	/// reading identity差
	SOURCE_IDENTITY,
	/// 全field差
	ORIGINAL_RECORD,
	/// 全原文差
	ORIGINAL_TAPE,
	/// 全補助byte差
	AUXILIARY,
	/// 全一次被覆差
	COUNTS_OR_COVERAGE,
	/// 単一全core不成立
	PLAN_SHAPE,
	/// 同じlive sessionの実fit不成立
	COUNTER_RECEIPT,
	/// 利用者又は資源停止
	STOPPED,
	/// 未確定の検査例外
	EXCEPTION
} ;
/**
 * @brief 終端でallocationなしに転記する固定容量の同期証拠です
 * @note 各validがfalseの0/SHAを実観測値と扱いません。worker終了後だけ読みます
 */
struct WholeTextPlanAdmissionReceipt {
	/// 検査状態
	WholeTextPlanAdmissionState		state = WholeTextPlanAdmissionState::NOT_REACHED ;
	/// 最初の理由
	WholeTextPlanAdmissionReason	reason = WholeTextPlanAdmissionReason::NONE ;
	/// prepare成功後の到達
	bool							gateEntered = false ;
	/// 通常correctorへmoveする直前
	bool							correctorHandoffStarted = false ;
	/// 元利用者stopの実値
	bool							userStopRequested = false ;
	/// 同期要求ID
	std::uint64_t					operationId = 0 ;
	/// source再計算済み
	bool							sourceValid = false ;
	/// 元履歴ID
	std::uint64_t					sourceHistoryId = 0 ;
	/// 再計算したreading SHA
	WholeTextDigest					readingSourceSha256{} ;
	/// 全field encode/hash済み
	bool							originalRecordValid = false ;
	/// 全fieldの実長
	std::uint64_t					originalRecordBytes = 0 ;
	/// 全field実SHA
	WholeTextDigest					originalRecordSha256{} ;
	/// 全tape hash済み
	bool							originalTapeValid = false ;
	/// 全tape実長
	std::uint64_t					originalTapeBytes = 0 ;
	/// 全tape実SHA
	WholeTextDigest					originalTapeSha256{} ;
	/// 全補助hash済み
	bool							auxiliaryValid = false ;
	/// 全補助実長
	std::uint64_t					auxiliaryBytes = 0 ;
	/// 全補助実SHA
	WholeTextDigest					auxiliarySha256{} ;
	/// 既存の全一次被覆validator成立
	bool							coverageValid = false ;
	/// 全raw実数
	std::uint32_t					rawRows = 0 ;
	/// 全packet実数
	std::uint32_t					canonicalSpans = 0 ;
	/// 全core実数
	std::uint32_t					records = 0 ;
	/// plan/input封印観測済み
	bool							planValid = false ;
	/// 封印plan SHA
	WholeTextDigest					planSha256{} ;
	/// 封印input SHA
	WholeTextDigest					inputSha256{} ;
	/// 観測した唯一のcore
	WholeTextByteRange				core ;
	/// 観測したbefore
	WholeTextByteRange				before ;
	/// 観測したafter
	WholeTextByteRange				after ;
	/// 唯一の実receiptを取得済み
	bool							countValid = false ;
	/// 固定配列だけの実計数
	WholeTextTokenReceipt			count ;
	/// encodingがHistoryErrorを返したか
	bool							historyErrorValid = false ;
	/// 元HistoryErrorKind
	std::uint32_t					historyErrorKind = 0 ;
	/// 元履歴code
	std::uint32_t					historyErrorCode = 0 ;
	/// closeと独立した元native値
	WholeTextNativeFailureSnapshot	native ;
	/// 元close全field
	WholeTextNativeCloseReceipt		close ;
	/// 最初のsticky資源停止
	WholeTextBoundedResourceFailure resourceFailure ;
	/// 返却terminalの分類
	ErrorKind						errorKind = ErrorKind::INTERNAL ;
	/// 返却terminalのcode
	std::uint32_t					errorCode = 0 ;
} ;
using WholeTextBoundedDispatch =
	std::function<WholeTextBoundedTerminal( WholeTextBoundedStorageBudget)> ; // 一回のtyped入口
//------------------------------------------------------------------------------
/**
 * @brief reserve前のfallbackとtyped全体catchで必ず同leaseをsettleします
 * @param[in] request 固定要求
 * @param[in] port 共有保存port
 * @param[in] dispatch 一回typed処理
 * @param[in] stop 停止
 * @return 別型の保存ack。V1応答へ変換しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] JobOutput runBoundedWithPersistence( const WholeTextCorrectionJobRequest			   &request,
												  const std::shared_ptr<IWholeTextPersistencePort> &port,
												  const WholeTextBoundedDispatch &dispatch, std::stop_token stop) ;

/** @brief 通常と同じworkflowを人工入力で試す最小の同期seamです */
struct WholeTextApplicationServices {
	/// 通常はdecode_input_audio
	AudioDecoder decoder ;
	/// 通常はLocalAsrEngine
	std::function<Recognition( const AudioData &, const EngineOptions &, std::span<const AsrSampleWindow>,
							  std::stop_token, const ProgressCallback &, const RecognitionObserver &, AsrTimelineUse)>
						   whisper ;
	/// 通常はLocalModelInspector
	IModelInspector		  &inspector ;
	/// 通常はverify_model、一回だけ
	WholeTextModelVerifier verifier ;
	/// 通常はnative
	std::function<std::unique_ptr<IWholeTextCorrector>( const std::filesystem::path &, PreparedWholeTextNativeInput,
													   std::shared_ptr<WholeTextResourceGuard>,
													   std::shared_ptr<const VerifiedWholeTextRuntime>)>
						   corrector ;
	/// 通常はOS実値
	WholeTextResourceProbe resourceProbe ;
	/// 通常は実CPU/compiled pin
	std::function<std::shared_ptr<const VerifiedWholeTextRuntime>( WholeTextRuntimeVariant,
																  const std::filesystem::path &, std::stop_token)>
											   runtimePreflight ;
	/// LowMemory有界だけ必須
	WholeTextApplicationResourceMonitorFactory resourceMonitor ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 同じ通常workflowの同期依存だけを注入します
 * @param[in] request 非0op
 * @param[in] config 隔離root
 * @param[in,out] services 同期中有効
 * @param[in] stop 停止
 * @param[in] progress 通知
 * @param[out] observation 任意の受動出力、null可。同期終了後だけ読みます
 * @param[in] policy 明示予算。既定の研究policyはPCM解放後から監視します
 * @return 通常と同じ所有応答
 * @throws OperationError 元分類/codeを維持
 */
//------------------------------------------------------------------------------
[[nodiscard]] JobOutput
									  runWholeTextApplicationWithServices( const WholeTextCorrectionJobRequest &request,
																		  const WholeTextApplicationConfig &config, WholeTextApplicationServices &services,
																		  std::stop_token stop, const ProgressCallback &progress,
																		  // この同期境界で所有する値、または確定状態の検証値です
																		  WholeTextApplicationObservation *observation = nullptr,
																		  WholeTextResourcePolicy policy = WholeTextResourcePolicy::RESEARCH_HALF_RESERVE) ;
//------------------------------------------------------------------------------
/**
 * @brief PCM/context解放後の一captureだけを返す共通境界です
 * @param[in] request 全要求
 * @param[in,out] services 同期依存
 * @param[in] options 固定Whisper設定
 * @param[in] windows 全予定窓
 * @param[in] stop 停止
 * @param[in] progress 通知
 * @param[out] observation 任意観測
 * @return 全raw/token/trace所有capture
 * @param[in] guard 共有予算、nullなら従来のcapture。return前にPCMを解放します
 * @param[in] processingStop decoder/Whisper専用停止。省略時は元stopを維持します
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextWhisperCapture collectWholeTextApplicationCapture(
	const WholeTextCorrectionJobRequest &request, WholeTextApplicationServices &services, const EngineOptions &options,
	std::span<const AsrSampleWindow> windows, std::stop_token stop,
	// この同期境界で所有する値、または確定状態の検証値です
	const ProgressCallback &progress, WholeTextApplicationObservation *observation = nullptr,
	std::shared_ptr<WholeTextResourceGuard> guard = {}, std::optional<std::stop_token> processingStop = std::nullopt) ;
//------------------------------------------------------------------------------
/**
 * @brief 同じtyped入口の人工依存seamです
 * @param[in] request 全要求
 * @param[in] config 隔離root
 * @param[in] storageBudget 一回予約
 * @param[in,out] services 同じ通常サービス
 * @param[in] loader V2固定export
 * @param[in] stop 停止
 * @param[in] progress 同期通知
 * @param[in] policy 明示予算。既定は従来研究用
 * @return native所有物のない未保存terminal
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextBoundedTerminal runBoundedWholeTextApplicationWithServices(
	const WholeTextCorrectionJobRequest &request, const WholeTextApplicationConfig &config,
	WholeTextBoundedStorageBudget storageBudget, WholeTextApplicationServices &services,
	const WholeTextCounterLoader &loader, std::stop_token stop, const ProgressCallback &progress,
	WholeTextResourcePolicy policy = WholeTextResourcePolicy::RESEARCH_HALF_RESERVE) ;
//------------------------------------------------------------------------------
/**
 * @brief 実16GB入口と同じ固定policyを人工依存で検証します
 * @param[in] request 原文と元PCM identity
 * @param[in] config 隔離root
 * @param[in] storageBudget 一回予約
 * @param[in,out] services 同期中有効な依存
 * @param[in] loader V2固定export
 * @param[in] stop 利用者停止
 * @param[in] progress 通知
 * @return 同じbounded処理の未保存terminal
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextBoundedTerminal runBoundedWholeTextApplication16gbWithServices(
	const WholeTextCorrectionJobRequest &request, const WholeTextApplicationConfig &config,
	WholeTextBoundedStorageBudget storageBudget, WholeTextApplicationServices &services,
	const WholeTextCounterLoader &loader, std::stop_token stop, const ProgressCallback &progress) ;
//------------------------------------------------------------------------------
/**
 * @brief 同じlive計画の全量一致と単一実fitを生成前に必須検査します
 * @param[in] request 原文と元path
 * @param[in] config 固定root
 * @param[in] storageBudget 一回予約
 * @param[in,out] services 同期七依存
 * @param[in] loader 固定V2 export
 * @param[in] admission 不変期待値、終了まで有効
 * @param[out] receipt 固定同期出力、終了まで有効。nativeへ借用しません
 * @param[in] stop 利用者停止
 * @param[in] progress 既存同期通知
 * @return 原文を保持する通常terminal。16GB品質/搭載量の合格ではありません
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextBoundedTerminal runBoundedWholeTextApplication16gbWithAdmission(
	const WholeTextCorrectionJobRequest &request, const WholeTextApplicationConfig &config,
	WholeTextBoundedStorageBudget storageBudget, WholeTextApplicationServices &services,
	const WholeTextCounterLoader &loader, const WholeTextSingleCoreAdmission &admission,
	WholeTextPlanAdmissionReceipt &receipt, std::stop_token stop, const ProgressCallback &progress) ;
} // namespace detail
} // namespace koefude
