//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file job_controller.cpp
 * @brief 有限キュー、進捗集約、取消し、非同期停止を実装します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 bounded workerと終端結果保持を追加
 * 0.1.0 Shunji Ogawa 2026/10/04 整形の全体検証を所有型へ閉じUIの再走査を不要に変更
 * 0.1.0 Shunji Ogawa 2026/10/04 整形要求を同一queueへ束縛し、全行応答と取消し競合を検証
 * 0.1.0 Shunji Ogawa 2026/10/19 自由全文の入口・型分離・取消し競合を接続
 * 0.1.0 Shunji Ogawa 2026/10/19 分岐意図・callbackの契約・宣言の役割を明記
 * 0.1.0 Shunji Ogawa 2026/10/19 全文の全体検証phaseと型分離を維持
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1287の明示診断と隔離接続を追加
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1344の明示API補正と独立保存を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1345の動的モデル・凍結単価と旧履歴互換を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1347の未表示行source keyをworker入口で確定
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1348の行メニュー直接実行と保存後表示を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1351のAPI専用HTTP失敗・保存状態を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1352の実区間進捗・採用保存を接続
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1370の文脈分割と明示途中再開を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372の独立話者投影と元原文保持を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1397の実段階と確定終端を排他外で記録
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
#include "core/job_controller.h"

#include "inference/whole_text_runtime.h"
#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <utility>
#include <variant>

#include "core/local_diagnostic_hooks.h"

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief worker上の同期生成器を型付き要求へ接続します
 * @param[in] request controllerが受付IDを設定済みの整形要求
 * @param[in,out] formatter この呼出しをworkerだけで使用する生成器
 * @param[in] stop 協調取消し
 * @param[in] progress 実処理の通知先
 * @return 原文と全行応答を共有所有する結果。全体検証はcontrollerが行います
 * @see koefude::runReadingJob
 */
//------------------------------------------------------------------------------
JobOutput runReadingJob( const ReadingJobRequest &request, ITranscriptFormatter &formatter, const std::stop_token stop,
						const ProgressCallback &progress)
{
	checkCancelled( stop) ;
	// 未受付の要求や原文なしを生成器へ渡さない
	if( !request.source || request.request.operationId == 0) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// この同期境界で所有する値、または確定状態の検証値です
	auto response = formatter.format( request.source, request.request, stop, progress) ; // 未検証の全行応答
	checkCancelled( stop) ;
	JobOutput output( request.source) ; // ASR原文の同じ共有所有値
	output.reading = std::make_shared<const ReadingResponse>( std::move( response)) ;
	return output ;
}

//------------------------------------------------------------------------------
/**
 * @brief workerの同期補正器を自由全文要求へ接続します
 * @param[in] request 受付ID設定済みの不変要求
 * @param[in,out] corrector workerだけで同期利用する補正器
 * @param[in] stop 協調取消し
 * @param[in] progress 実処理通知
 * @return 同じ原文ownerと未検証全文応答。構造検証はcontrollerで行います
 * @throws OperationError 入口不正または取消し。生成器の失敗はそのまま伝えます
 * @see koefude::runWholeTextCorrectionJob
 */
//------------------------------------------------------------------------------
JobOutput runWholeTextCorrectionJob( const WholeTextCorrectionJobRequest &request, IWholeTextCorrector &corrector,
									const std::stop_token stop, const ProgressCallback &progress)
{
	checkCancelled( stop) ;
	validateWholeTextCorrectionRequest( request.source, request.request) ;
	// この同期境界で所有する値、または確定状態の検証値です
	auto response = corrector.correct( request.source, request.request, stop, progress) ; // 未検証全文
	checkCancelled( stop) ;
	JobOutput output( request.source) ; // 同じ不変原文ownerを返す
	output.wholeText = std::make_shared<const WholeTextCorrectionResponse>( std::move( response)) ;
	return output ;
}

//------------------------------------------------------------------------------
/**
 * @brief workerを開始します
 * @param[in] runner 所有する同期処理。mutexの外で呼び出すことを保証
 * @param[in] notifier 所有する起床通知。worker終了まで通知先が有効であること
 * @param[in] diagnostics 通常sinkのborrow、nullは従来どおりI/O0
 * @see koefude::JobController::JobController(koefude::JobRunner, koefude::JobNotifier, koefude::LocalDiagnosticLog *)
 */
//------------------------------------------------------------------------------
JobController::JobController( JobRunner runner, JobNotifier notifier, LocalDiagnosticLog *diagnostics)
	: runner_( std::move( runner)), notifier_( std::move( notifier)), diagnostics_( diagnostics), worker_(
																								 [this]
																								 {
																									 run() ;
																								 })
{
}

//------------------------------------------------------------------------------
/**
 * @brief 停止要求後にjoinします。UIではstopped確認後に破棄してください
 * @see koefude::JobController::~JobController
 */
//------------------------------------------------------------------------------
JobController::~JobController()
{
	requestStop() ;
	worker_.join() ;
}

//------------------------------------------------------------------------------
/**
 * @brief 復元済み履歴の番号を再利用しないよう、次回受付IDの下限を設定します
 * @param[in] floor 次のsubmitに使う非0の操作ID。現在のnext_id以上であること
 * @return 待機queueと未受領slotが空で、停止中でなく設定できた場合はtrue
 *         floorが0・後退、既に番号枯渇、未受領/待機要求あり、停止中ならfalseで変更しません
 * @throws std::system_error 受付mutexのlockに失敗した場合
 * @note ジョブの開始や通知は行わず、同じ受付mutexでsubmitと直列化します
 * @see koefude::JobController::seedNextId
 */
//------------------------------------------------------------------------------
bool JobController::seedNextId( std::uint64_t floor)
{
	const std::lock_guard lock( mutex_) ; /* IDだけ保護 */
	if( floor == 0 || nextId_ == 0 || floor < nextId_ || !slots_.empty() || !queue_.empty() || stopping_) {
		return false ;
	}
	nextId_ = floor ;
	return true ;
}
//------------------------------------------------------------------------------
/**
 * @brief 所有要求を有限queueへ受け付け、一つのworkerが順に処理する操作IDを発行します
 * @param[in] request 種別ごとの不変source・設定・送信意図を所有する要求。操作IDは受付前0です
 *                    値として受け取るため、拒否や例外時もmove前の呼出し元の値へ復元しません
 * @return 受付成立時の非0の単調操作ID。実行や保存の成功を示す値ではありません
 * @retval 0 停止中、未受領終端を含む8枠の満杯、番号枯渇、要求内容の不正、
 *           同一履歴の競合、または全API用途共通の一枠が使用中で受け付けませんでした
 * @throws std::bad_alloc 受付slotやqueueの確保に失敗した場合。消費したIDは再利用しません
 * @throws std::system_error 受付mutexのlockに失敗した場合
 * @post 受付時だけpayloadの操作IDを固定します。実処理の結果は後続のJobEventで受領します
 * @note API用途は明示選択・キーrevision・履歴ID等を種別ごとに照合します
 *       受付だけでは進捗通知を発行せず、実際のworker通知まで待機表示を保ちます
 * @see koefude::JobController::submit
 */
//------------------------------------------------------------------------------
std::uint64_t JobController::submit( JobRequest request)
{
	auto *asr = std::get_if<AsrJobRequest>( &request.payload) ; // 音声受付snapshot
	auto *projection = std::get_if<SpeakerTextJobRequest>( &request.payload) ; // PC内投影専用要求
	// UI由来の投影要求は本文なし小targetと未発行opだけを認める
	if( projection && (!projection->target.historyId || !projection->target.completePrefix || projection->operationId)) {
		return 0 ;
	}
	const bool audio =
		asr && (asr->audioResume || asr->options.asrModel == AsrModel::OPEN_AI_AUDIO) ; // 単一registry系統
	// APIへは明示設定と安定ID/キーrevisionを持つ新規受付だけを許可します
	if( audio && (!asr->audioApiSelected || !asr->historyId || !asr->apiKeyRevision || asr->operationId ||
				 (asr->audioResume &&
				  (asr->audioResume->historyId != asr->historyId || !asr->audioResume->update ||
				   asr->audioResume->planSha == ApiTextDigest{} || asr->audioResume->sourceSha == ApiTextDigest{})))) {
		return 0 ;
	}
	auto *catalog = std::get_if<ApiCatalogJobRequest>( &request.payload) ; // metadata独立要求
	// 旧operationの再利用を拒否します
	if( catalog && catalog->operationId != 0) {
		return 0 ;
	}
	auto *api = std::get_if<ApiTextCorrectionJobRequest>( &request.payload) ; // API専用受付
	// 未発行IDとsource identityだけを受理し、native証拠を捏造しません
	if( api &&
	   (api->operationId != 0 || (api->action != ApiTextAction::SET_KEY && api->action != ApiTextAction::ERASE_KEY &&
								  (!api->source || api->key.historyId == 0)))) {
		return 0 ;
	}
	auto *reading = std::get_if<ReadingJobRequest>( &request.payload) ; // 整形時だけの受付情報
	// 呼出し側の旧operationや本文なしの整形をqueueへ持ち込ませない
	if( reading && (!reading->source || reading->source->lines.empty() || reading->request.source.historyId == 0 ||
				   reading->request.operationId != 0)) {
		return 0 ;
	}
	auto *whole = std::get_if<WholeTextCorrectionJobRequest>( &request.payload) ; // 自由全文の受付
	// 旧operationと本文なしは自由全文でも受付枠へ入れない
	if( whole && (!whole->source || whole->source->lines.empty() || whole->request.source.historyId == 0 ||
				 whole->request.operationId != 0)) {
		return 0 ;
	}
	// この同期境界で所有する値、または確定状態の検証値です
	const auto			  historyId = projection ? projection->target.historyId
									  : reading	 ? reading->request.source.historyId
									  : whole	 ? whole->request.source.historyId
									  : audio	 ? asr->historyId
												 : 0 ; // 共通の重複key
	const std::lock_guard lock( mutex_) ; // 受付状態と二つのcontainerを同時に保護
	// 未受領結果を含めて上限を守り、IDを再利用しない
	if( stopping_ || slots_.size() >= K_MAXIMUM_OUTSTANDING_JOBS || nextId_ == 0) {
		return 0 ;
	}
	// 同一履歴の整形は終端受領まで一つに限定する
	if( historyId != 0) {
		// 最大8件の受付枠だけを比較し、音声や本文のhash計算はlock内で行わない
		for( const auto &[active_id, slot] : slots_) {
			static_cast<void>( active_id) ;
			// 別source版であっても同じ履歴の並行整形を受け付けない
			if( slot.readingHistoryId == historyId) {
				return 0 ;
			}
		}
	}
	// 最大一つのAPI要求を終端受領まで保持します
	if( (api || catalog || audio) &&
	   (nextId_ == std::numeric_limits<std::uint64_t>::max() || std::any_of( slots_.begin(), slots_.end(),
		//------------------------------------------------------------------------------
		/**
		 * @brief API受付枠を確認します
		 * @param[in] e slot
		 * @return API使用中
		 */
		//------------------------------------------------------------------------------
		[]( const auto &e)
		{
			return e.second.apiText ;
		}))) {
		return 0 ;
	}
	const auto id = nextId_++ ; // 失敗時も消費し、再試行で同じIDを使わない
	// 他jobと同じglobal IDを投影専用payloadへ発行する
	if( projection) {
		projection->operationId = id ;
	}
	slots_.try_emplace( id) ;
	// 受付IDを生成要求へ固定し、再試行で以前のIDを再利用しない
	if( reading) {
		reading->request.operationId = id ;
	}
	// 自由全文も単調IDと共通の履歴重複guardへ束縛する
	if( whole) {
		whole->request.operationId = id ;
	}
	// 音声APIも同じoperationとAPI単一受付枠を共有します
	if( asr) {
		asr->operationId = id ;
		if( audio) {
			slots_.at( id).apiText = true ;
		}
	}
	// APIも同じ単調operation IDを使います
	if( api) {
		api->operationId = id ;
		slots_.at( id).apiText = true ;
	}
	// metadataも同じ有界API枠を共有します
	if( catalog) {
		catalog->operationId = id ;
		slots_.at( id).apiText = true ;
	}
	slots_.at( id).readingHistoryId = historyId ;
	try {
		queue_.emplace_back( id, std::move( request)) ;
	} catch( ...) {
		// queue確保失敗では空の受付枠を残さず元の例外を呼出側へ返す
		slots_.erase( id) ;
		throw ;
	}
	slots_.at( id).event.id = id ;
	changed_.notify_one() ;
	return id ;
}

//------------------------------------------------------------------------------
/**
 * @brief 指定した未完了要求のstop状態へ協調取消しを通知します
 * @param[in] id submitで受け付けた操作ID。未知・受領済み・既に終端のIDなら無操作です
 * @throws std::system_error 受付mutexのlockに失敗した場合
 * @note mutex外でstd::stop_source::request_stopを呼び、同期stop callbackの実行を許します
 *       復帰は推論中断や終端保存の完了を意味しません。終端JobEventで結果を確認します
 * @see koefude::JobController::cancel
 */
//------------------------------------------------------------------------------
void JobController::cancel( const std::uint64_t id)
{
	std::stop_source cancellation( std::nostopstate) ; // lock外で通知する共有stop state
	{
		const std::lock_guard lock( mutex_) ; // IDの検索だけを排他
		const auto			  found = slots_.find( id) ; // 未受領の要求との照合
		// 未知または終了済みIDへの取消しは別ジョブへ影響させない
		if( found == slots_.end() || found->second.event.terminal) {
			return ;
		}
		cancellation = found->second.cancellation ;
	}
	cancellation.request_stop() ;
}

//------------------------------------------------------------------------------
/**
 * @brief 新規受付を止め、未受領枠にある全要求へ協調取消しを通知します
 * @throws std::system_error 受付mutexのlockに失敗した場合
 * @note 取消し通知はmutex外で行い、同期stop callbackが走る場合があります。joinは行いません
 *       終端結果はtakeEventsで回収でき、worker終了はstoppedで別に確認します
 * @see koefude::JobController::requestStop
 */
//------------------------------------------------------------------------------
void JobController::requestStop()
{
	// mutexの外で取消しを要求するために複製する有限個のstop状態
	std::array<std::optional<std::stop_source>, K_MAXIMUM_OUTSTANDING_JOBS> cancellations ;
	std::size_t count = 0 ; // heap確保せずに複製する有効な取消し状態の数
	{
		const std::lock_guard lock( mutex_) ; // 新規受付を先に止める
		stopping_ = true ;
		// 未受領の全要求へ取消しを伝え、終端結果自体は回収可能なまま残す
		for( const auto &[id, slot] : slots_) {
			static_cast<void>( id) ;
			cancellations[count++].emplace( slot.cancellation) ;
		}
	}
	// 最大8件の取消しをmutexを持たずに要求する
	for( auto &cancellation : cancellations) {
		// 未使用の受付枠にはstop stateが存在しない
		if( cancellation) {
			cancellation->request_stop() ;
		}
	}
	changed_.notify_one() ;
}

//------------------------------------------------------------------------------
/**
 * @brief 通知callbackを含むworkerの終了が確定したか照会します
 * @return worker末尾のreleaseに対応するacquire読取りがtrueなら終了済み、falseなら終了未確認
 *         停止要求を出しただけではtrueになりません
 * @pre このcontrollerの寿命を呼出し完了まで保持し、破棄と並行しないこと
 * @note 終端eventの受領済みや全ジョブ成功を示す値ではありません
 * @see koefude::JobController::stopped
 */
//------------------------------------------------------------------------------
bool JobController::stopped() const noexcept
{
	return stopped_.load( std::memory_order_acquire) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 未受領の更新だけをID昇順に回収し、返す終端要求の受付枠を解放します
 * @return 最大8件の所有event列。各IDの進捗は最新値一件へ集約し、結果DTOは共有所有します
 *         空は新しい通知がないことを表し、worker終了や全ジョブ成功を意味しません
 * @throws std::bad_alloc 返却vectorの事前reserveに失敗した場合。未受領状態と受付枠は変更しません
 * @throws std::system_error 受付mutexのlockに失敗した場合。未受領状態と受付枠は変更しません
 * @post 返した更新のdirtyを解除し、終端を返したIDだけslotを削除します
 * @note 現在のJobEventはscalar/固定配列/共有ownerから成り、reserve後のcopyで本文を確保しません
 * @see koefude::JobController::takeEvents
 */
//------------------------------------------------------------------------------
std::vector<JobEvent> JobController::takeEvents()
{
	std::vector<JobEvent> events ; // 最大8件の所有受領結果
	events.reserve( K_MAXIMUM_OUTSTANDING_JOBS) ;
	const std::lock_guard lock( mutex_) ; // 更新と終端枠の解放を同時に行う
	// 未受領の更新だけを取り出し、進捗値の古い履歴を増やさない
	for( auto entry = slots_.begin(); entry != slots_.end();) {
		// 更新なしの進行中要求を繰返しUIへ送らない
		if( !entry->second.dirty) {
			++entry ;
			continue ;
		}
		events.push_back( entry->second.event) ;
		entry->second.dirty = false ;
		// 終端eventの受領が終わった時点で初めて受付枠を解放する
		if( entry->second.event.terminal) {
			entry = slots_.erase( entry) ;
		} else {
			++entry ;
		}
	}
	return events ;
}

//------------------------------------------------------------------------------
/**
 * @brief 最新eventをmutex内で差し替え、外側でUIを起こします
 * @param[in] event 同じIDの最新値。所有権を移す
 * @see koefude::JobController::publish
 */
//------------------------------------------------------------------------------
void JobController::publish( JobEvent event) noexcept
{
	std::optional<LocalDiagnosticRecord> diagnostic ; // 排他外へ渡す固定scalarだけ
	{
		const std::lock_guard lock( mutex_) ; // 既存slotへ代入し、workerではnode確保しない
		const auto			  found = slots_.find( event.id) ; // 受付IDを再照合
		// 未知IDと終端後の遅延進捗を別の表示へ適用しない
		if( found == slots_.end() || found->second.event.terminal) {
			return ;
		}
		// 最終取消し確認とpublishの間に到着した取消しも成功へしない
		if( event.terminal && event.succeeded && found->second.cancellation.stop_requested() && !event.apiText &&
		   !event.apiCatalog && !event.apiAudio && !event.speakerText &&
		   !(event.boundedPersistence && event.boundedPersistence->receipt().committed)) {
			event.succeeded = false ;
			event.error = ErrorKind::CANCELLED ;
			event.result.reset() ;
			event.reading.reset() ;
			event.readingCompletion.reset() ;
			event.wholeText.reset() ;
			event.wholeTextCompletion.reset() ;
		}
		// API終端でも最新の実区間数/開始を落とさず、実終端時刻で経過を凍結します
		if( event.terminal && found->second.apiText &&
		   found->second.event.progress.apiText.phase != ApiTextPhase::NONE) {
			event.progress.apiText = found->second.event.progress.apiText ;
			event.progress.apiText.phase = event.succeeded						 ? ApiTextPhase::COMPLETED
										   : event.error == ErrorKind::CANCELLED ? ApiTextPhase::CANCELLED
																				 : ApiTextPhase::FAILURE ;
			event.progress.apiText.observedMs = progressMonotonicMs() ;
			event.progress.percent = event.succeeded ? 100 : -1 ;
		}
		// 音声APIの実経過/区間数も別DTOとして終端で凍結します
		if( event.terminal && found->second.event.progress.apiAudio.phase != ApiTextPhase::NONE) {
			event.progress.apiAudio = found->second.event.progress.apiAudio ;
			event.progress.apiAudio.phase = event.succeeded						  ? ApiTextPhase::COMPLETED
											: event.error == ErrorKind::CANCELLED ? ApiTextPhase::CANCELLED
																				  : ApiTextPhase::FAILURE ;
			event.progress.apiAudio.observedMs = progressMonotonicMs() ;
			event.progress.percent = event.succeeded ? 100 : -1 ;
		}
		// 投影も実進捗の開始と窓数を終端へ継承し、原音声APIとは別に凍結します
		if( event.terminal && found->second.event.progress.speakerText.phase != ApiTextPhase::NONE) {
			event.progress.speakerText = found->second.event.progress.speakerText ;
			event.progress.speakerText.phase = event.succeeded						 ? ApiTextPhase::COMPLETED
											   : event.error == ErrorKind::CANCELLED ? ApiTextPhase::CANCELLED
																					 : ApiTextPhase::FAILURE ;
			event.progress.speakerText.observedMs = progressMonotonicMs() ;
			event.progress.percent = event.succeeded ? 100 : -1 ;
		}
		// 取消し/耐久ACKの最終判定後だけ、成功を捏造しない終端を投影する
		if( event.terminal && diagnostics_) {
			diagnostic = localJobTerminal( event) ;
			diagnostic->processingMs = progressMonotonicMs() - found->second.startedMs ;
		}
		found->second.event = std::move( event) ;
		found->second.dirty = true ;
	}
	// queue mutexの外へ小値だけを渡し、logger障害をnotifierへ伝播しない
	if( diagnostic) {
		diagnostics_->append( *diagnostic) ;
	}
	try {
		// pollingだけの利用でも空callbackを呼ばない
		if( notifier_) {
			notifier_() ;
		}
	} catch( ...) {
		// eventは保持済みなのでUI timerで回収し、通知障害を記録する
		notificationFailed_.store( true) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief worker境界内で要求と例外を処理します
 * @see koefude::JobController::run
 */
//------------------------------------------------------------------------------
void JobController::run() noexcept
{
	// 停止要求後もqueued要求を取消し終端にし、最後の通知までworkerが責任を持つ
	for(;;) {
		std::pair<std::uint64_t, JobRequest> job ; // mutex外で実行する要求の所有権
		std::stop_token						 stop ; // この要求専用の取消し状態
		{
			std::unique_lock lock( mutex_) ; // queue取り出し時だけ排他
			changed_.wait( lock,
						  //------------------------------------------------------------------------------
						  /**
						   * @brief 新規jobまたは停止要求が届いた時だけworkerの待機を解除します
						   * @return 停止中または実行待ちjobがある場合true
						   * @note mutex_保持中に評価し、捕捉したthisはdestructorのjoin完了まで有効です
						   */
						  //------------------------------------------------------------------------------
						  [this]
						  {
							  return stopping_ || !queue_.empty() ;
						  }) ;
			// 新規受付停止後にqueueを排出した時点で安全にworkerを終了する
			if( queue_.empty() && stopping_) {
				break ;
			}
			job = std::move( queue_.front()) ;
			queue_.pop_front() ;
			stop = slots_.at( job.first).cancellation.get_token() ;
			slots_.at( job.first).startedMs = progressMonotonicMs() ;
		}
		JobEvent terminal ; // 必ず一度だけpublishする終端状態
		terminal.id = job.first ;
		terminal.terminal = true ;
		// 取消し済みqueued要求も同じIDで開始/終端を照合できるようにする
		if( diagnostics_) {
			diagnostics_->append( localJobStarted( job.first, job.second)) ;
		}
		std::optional<Progress> lastLogged ; // 同一stage/tickの重複を省く有限追跡
		const auto *diagnosticRequest = std::get_if<WholeTextCorrectionJobRequest>( &job.second.payload) ; // 任意診断
		const auto	capture = diagnosticRequest ? diagnosticRequest->diagnostics : nullptr ; // 同期owner
		try {
			checkCancelled( stop) ;
			auto *apiRequest = std::get_if<ApiTextCorrectionJobRequest>( &job.second.payload) ; // 受付owner
			// 未表示行の大きい本文hashもworkerで一度だけ確定し、終端は同じkeyへ照合します
			if( apiRequest && apiRequest->source && apiRequest->key.sha256 == ApiTextDigest{}) {
				apiRequest->key.sha256 = fingerprintReadingSource( *apiRequest->source) ;
			}
			// この同期境界で所有する値、または確定状態の検証値です
			const auto *reading = std::get_if<ReadingJobRequest>( &job.second.payload) ; // 現要求の型
			// 生成前に不変原文への束縛をworkerで照合する
			if( reading && reading->request.source.sha256 != fingerprintReadingSource( *reading->source)) {
				throw OperationError( ErrorKind::INVALID_INPUT) ;
			}
			// この同期境界で所有する値、または確定状態の検証値です
			const auto *whole =
				std::get_if<WholeTextCorrectionJobRequest>( &job.second.payload) ; // 自由全文なら生成前に全要求を照合
			// fakeや将来engineを呼ぶ前にsource/profile/IDの不一致を拒否する
			if( whole) {
				validateWholeTextCorrectionRequest( whole->source, whole->request) ;
			}
			// 実runner入口を取消し検証後に固定します
			if( capture) {
				capture->workerStarted() ;
			}
			auto output =

				runner_( job.second, stop,
						//------------------------------------------------------------------------------
						/**
						 * @brief 同じjob IDの進捗を診断段階・補正計測・UI通知へ同期配信します
						 * @param[in] value runnerが通知したProgressのcopyです。段階が変わった時だけ有限診断へ記録し、最新UI進捗は集約します
						 * @note thisとlastLoggedはrunner_の同期実行中だけ借用します。captureは共有所有し、終了後にcallbackを保持しません
						 */
						//------------------------------------------------------------------------------
						[this, id = job.first, capture, &lastLogged]( const Progress value)
						{
							// 最新値の集約で実段階を失う前に、段階変化だけを記録する
							if( diagnostics_ && (!lastLogged || !sameLocalDiagnosticStage( *lastLogged, value))) {
								LocalDiagnosticRecord record ; // 本文もbufferもcopyしない小DTO
								record.event = LocalDiagnosticEvent::JOB_STAGE ;
								record.operation = id ;
								record.stage = value.stage ;
								record.phase = value.apiAudio.phase != ApiTextPhase::NONE ? value.apiAudio.phase
																						  : value.apiText.phase ;
								record.count = value.apiAudio.phase != ApiTextPhase::NONE
												   ? value.apiAudio.completedUnits
												   : value.apiText.completedUnits ;
								record.total = value.apiAudio.phase != ApiTextPhase::NONE ? value.apiAudio.totalUnits
																						  : value.apiText.totalUnits ;
								diagnostics_->append( record) ;
								lastLogged = value ;
							}
							// 通知の集約より前にphaseごとの最新実測を採ります
							if( capture) {
								capture->progress( value) ;
							}
							JobEvent event ; // PCMやengine所有権を持たない進捗DTO
							event.id = id ;
							event.progress = value ;
							publish( std::move( event)) ;
						}) ;
			// PC内投影は原API終端/料金/本文を更新するASR応答として扱わない
			if( output.speakerText) {
				const auto *q = std::get_if<SpeakerTextJobRequest>( &job.second.payload) ; // 同要求を照合
				// 同opの投影専用保存応答
				const auto &completion = *output.speakerText ;
				// 終端の小target/opを受付時値へ照合して古い採用を拒否する
				if( !q || q->operationId != job.first || completion.persistence.target != q->target ||
				   !completion.persistence.record.latest ||
				   completion.persistence.record.latest->operationId != job.first) {
					throw OperationError( ErrorKind::INVALID_INPUT) ;
				}
				terminal.speakerText = std::move( output.speakerText) ;
				terminal.result = completion.source ;
				// 保存事実を持つ最新の独立試行
				const auto &latest = *completion.persistence.record.latest ;
				terminal.succeeded = completion.persistence.committed && (latest.state == SpeakerTextState::READY ||
																		  latest.state == SpeakerTextState::UNCHANGED) ;
				terminal.error = latest.state == SpeakerTextState::CANCELLED ? ErrorKind::CANCELLED : latest.error ;
				terminal.code = completion.persistence.code ? completion.persistence.code : latest.code ;
				terminal.terminal = true ;
				publish( std::move( terminal)) ;
				continue ;
			}
			// metadataは原文ownerと無関係なtyped終端として照合します
			if( output.apiCatalog) {
				const auto *catalog = std::get_if<ApiCatalogJobRequest>( &job.second.payload) ; // 元要求
				// 別画面・別op・キーrevisionの終端を適用しません
				if( !catalog || output.apiCatalog->request.operationId != job.first ||
				   output.apiCatalog->request.windowGeneration != catalog->windowGeneration ||
				   output.apiCatalog->request.keyRevision != catalog->keyRevision) {
					throw OperationError( ErrorKind::INVALID_INPUT) ;
				}
				terminal.apiCatalog = std::move( output.apiCatalog) ;
				terminal.succeeded = terminal.apiCatalog->code == 0 ;
				publish( std::move( terminal)) ;
				continue ;
			}
			// typed要求へ欠落した応答は成功にしません
			if( std::holds_alternative<ApiCatalogJobRequest>( job.second.payload)) {
				throw OperationError( ErrorKind::INVALID_INPUT) ;
			}
			// API typed terminalは後発取消しでもknown usageと保存ackを保持します
			if( output.apiText) {
				const auto *api = std::get_if<ApiTextCorrectionJobRequest>( &job.second.payload) ; // 元要求
				// 別source/別opの応答をUIへ適用しません
				if( !api || output.apiText->request.operationId != job.first ||
				   output.apiText->request.action != api->action ||
				   output.apiText->request.catalogGeneration != api->catalogGeneration ||
				   output.apiText->request.keyRevision != api->keyRevision ||
				   output.apiText->request.budget != api->budget ||
				   output.apiText->request.catalogSnapshot != api->catalogSnapshot ||
				   output.apiText->request.key != api->key || output.apiText->request.source != api->source ||
				   output.apiText->request.source.owner_before( api->source) ||
				   api->source.owner_before( output.apiText->request.source)) {
					throw OperationError( ErrorKind::INVALID_INPUT) ;
				}
				terminal.apiText = std::move( output.apiText) ;
				terminal.result = api->source ;
				const auto &apiResult = *terminal.apiText ; // known usageを消さず成功を分類
				const auto	action = api->action ; // API操作の明確な成功条件
				terminal.succeeded = apiResult.code == 0 ;
				// 有料送信は保存済み完全候補だけを成功とします
				if( action == ApiTextAction::IMPROVE) {
					const auto &ack = apiResult.persistence ; // 実採用保存だけが成功です
					terminal.succeeded =
						apiResult.code == 0 && ack.committed && detail::sameTranscriptOwner( ack.source, api->source) &&
						ack.key == api->key && ack.operationId == job.first && ack.record.latest &&
						ack.record.adopted && ack.record.latest->operationId == job.first &&
						ack.record.adopted->operationId == job.first && ack.record.latest->draft.source == api->key &&
						ack.record.adopted->draft.source == api->key &&
						ack.record.adopted->consent.intent == ApiTextIntent::ROW_MENU &&
						!ack.record.adopted->candidate.empty() &&
						ack.record.latest->state == ApiTextState::CANDIDATE_READY &&
						ack.record.adopted->candidateSha == ack.record.latest->candidateSha &&
						!ack.record.showOriginal && !ack.record.saveFailed ;
				} else if( action == ApiTextAction::SEND) {
					terminal.succeeded = apiResult.persistence.committed && apiResult.persistence.record.latest &&
										 apiResult.persistence.record.latest->state == ApiTextState::CANDIDATE_READY ;
				} else if( action == ApiTextAction::ADOPT || action == ApiTextAction::ORIGINAL ||
						  action == ApiTextAction::CORRECTED || action == ApiTextAction::RETRY_SAVE ||
						  action == ApiTextAction::DISCARD) {
					terminal.succeeded = apiResult.persistence.committed ;
				}
				// 取消し・保存失敗と不完全生成を通常成功から分けます
				if( !terminal.succeeded) {
					const auto latest = apiResult.persistence.record.latest ; // 同要求の終端
					terminal.error = apiResult.persistence.record.saveFailed ? ErrorKind::SAVE_FAILED
									 : stop.stop_requested() || (latest && latest->state == ApiTextState::CANCELLED)
										 ? ErrorKind::CANCELLED
										 : ErrorKind::INTERNAL ;
					terminal.code = apiResult.code ? apiResult.code : apiResult.persistence.code ;
					// 保存ackが0でも最新試行の元通信codeを捨てません
					if( !terminal.code && latest && latest->operationId == job.first) {
						terminal.code = latest->code ;
						// 古いattempt.codeが0でもchunkにある診断を使えます
						if( !terminal.code && !latest->results.empty()) {
							terminal.code = latest->results.back().code ;
						}
					}
				}
				publish( std::move( terminal)) ;
				continue ;
			}
			// 専用API要求からtyped終端が欠落したrunnerを成功にしません
			if( std::holds_alternative<ApiTextCorrectionJobRequest>( job.second.payload)) {
				throw OperationError( ErrorKind::INVALID_INPUT) ;
			}
			// 音声APIの部分原文は通常ASRの成功/取消し破棄へ流しません
			if( output.apiAudio) {
				const auto *asr = std::get_if<AsrJobRequest>( &job.second.payload) ; // 同じ明示受付
				const auto &value = output.apiAudio->persistence ; // typed ACK
				const bool	preparation = output.apiAudio->resumePreparationFailed ;
				// 新opの終端と旧保存値を明示区別し、事前拒否は旧HTTPで説明しません
				if( !asr || !asr->audioApiSelected || asr->historyId != value.record.historyId ||
				   asr->operationId != job.first || value.source != output.result ||
				   (preparation
						? (!asr->audioResume || asr->audioResume->planSha != value.record.plan.digest ||
						   asr->audioResume->sourceSha != value.record.sourceSha ||
						   asr->audioResume->update != value.record.update)
						: (value.record.operationId != job.first || asr->apiKeyRevision != value.record.keyRevision)) ||
				   (asr->audioResume && asr->audioResume->planSha != value.record.plan.digest)) {
					throw OperationError( ErrorKind::INVALID_INPUT) ;
				}
				validateApiAudioRecord( value.record, value.source.get()) ;
				terminal.apiAudio = std::move( output.apiAudio) ;
				terminal.result = std::move( output.result) ;
				terminal.succeeded = terminal.apiAudio->succeeded && value.committed &&
									 (value.record.terminal == ApiAudioTerminal::COMPLETED ||
									  value.record.terminal == ApiAudioTerminal::NO_SPEECH ||
									  value.record.terminal == ApiAudioTerminal::EMPTY_TRANSCRIPT) ;
				terminal.error = preparation			   ? terminal.apiAudio->preparationError
								 : value.record.saveFailed ? ErrorKind::SAVE_FAILED
														   : value.record.error ;
				terminal.code = preparation	 ? terminal.apiAudio->preparationCode
								: value.code ? value.code
											 : value.record.code ;
				terminal.progress = {terminal.succeeded ? Stage::COMPLETED : Stage::TRANSCRIBING,
									 terminal.succeeded ? 100 : -1} ;
				publish( std::move( terminal)) ;
				continue ;
			}
			// API構成をlocal runnerの成功値へ暗黙fallbackしません
			if( const auto *asr = std::get_if<AsrJobRequest>( &job.second.payload) ;
			   asr && asr->options.asrModel == AsrModel::OPEN_AI_AUDIO) {
				throw OperationError( ErrorKind::INVALID_INPUT) ;
			}
			// 耐久ackは後発cancelで成功を戻しません
			if( output.boundedPersistence) {
				// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
				if( !whole || output.wholeText || output.reading ||
				   !findWholeTextBoundedRuntime( whole->request.profileId, whole->request.profileSha256) ||
				   !detail::sameTranscriptOwner( output.boundedPersistence->source(), whole->source) ||
				   output.boundedPersistence->request().operationId != job.first ||
				   output.boundedPersistence->request().source != whole->request.source ||
				   output.boundedPersistence->request().profileId != whole->request.profileId ||
				   output.boundedPersistence->request().profileSha256 != whole->request.profileSha256) {
					throw OperationError( ErrorKind::INVALID_INPUT) ;
				}
				terminal.boundedPersistence = std::move( output.boundedPersistence) ;
				terminal.result = whole->source ;
				terminal.succeeded = terminal.boundedPersistence->complete() ;
				// この同期境界で所有する値、または確定状態の検証値です
				const auto &record = terminal.boundedPersistence->record() ; /* 保存状態は生成と独立 */
				terminal.error = record.saveState == WholeTextBoundedSaveState::SAVE_FAILED
									 ? ErrorKind::SAVE_FAILED
									 : (record.latestTerminal ? record.latestTerminal->errorKind : ErrorKind::INTERNAL) ;
				terminal.code = record.saveErrorPresent
									? record.saveErrorCode
									: (record.latestTerminal ? record.latestTerminal->errorCode : 0) ;
				terminal.progress = {terminal.succeeded ? Stage::COMPLETED : Stage::COMBINING,
									 terminal.succeeded ? 100 : -1} ;
				// 保存ackと独立した生成snapshotを終端へ同伴します
				if( capture) {
					terminal.correctionDiagnostics = capture->snapshot() ;
				}
				publish( std::move( terminal)) ;
				continue ;
			}
			checkCancelled( stop) ;
			// 整形応答は全行とID/profileを照合し、原文を別値に差し替えさせない
			if( reading) {
				// 整形応答の欠落や原文ownerの不一致を成功結果として公開しない
				if( !output.reading || output.wholeText || output.result != reading->source) {
					throw OperationError( ErrorKind::INVALID_INPUT) ;
				}
				JobEvent validating ; // 本文を持たない実検証段階
				validating.id = job.first ;
				validating.progress = {Stage::COMBINING, -1} ;
				publish( std::move( validating)) ;
				terminal.readingCompletion = std::make_shared<const ValidatedReadingCompletion>(
					reading->source, reading->request, output.reading) ;
			}
			// 自由全文へ行対応応答や別原文ownerを混ぜない
			else if( whole) {
				// 原文のpointerだけでなく共有制御blockも同じことを確認する
				if( !output.wholeText || output.reading || output.result != whole->source ||
				   output.result.owner_before( whole->source) || whole->source.owner_before( output.result)) {
					throw OperationError( ErrorKind::INVALID_INPUT) ;
				}
				JobEvent validating ; // 本文なしの全体検証段階
				validating.id = job.first ;
				validating.progress = {Stage::COMBINING, -1, WholeTextPhase::OUTPUT_VALIDATION} ;
				publish( std::move( validating)) ;
				terminal.wholeTextCompletion = std::make_shared<const ValidatedWholeTextCorrectionCompletion>(
					whole->source, whole->request, output.wholeText) ;
			}
			// ASR/exportから別型の応答を混入させない
			else if( output.reading || output.wholeText) {
				throw OperationError( ErrorKind::INVALID_INPUT) ;
			}
			terminal.result = std::move( output.result) ;
			terminal.reading = std::move( output.reading) ;
			terminal.wholeText = std::move( output.wholeText) ;
			terminal.succeeded = true ;
			terminal.progress = {Stage::COMPLETED, 100} ;
		} catch( const OperationError &error) {
			terminal.result.reset() ; // 取消しが完了直後に競合した結果も成功本文として渡さない
			terminal.reading.reset() ; // 失敗した部分整形を渡さない
			terminal.readingCompletion.reset() ; // 検証後の取消しでも成功を保持しない
			terminal.wholeText.reset() ; // 失敗した全文を公開しない
			terminal.wholeTextCompletion.reset() ; // 検証後の取消しも所有成功を破棄
			terminal.error = error.kind() ;
			terminal.code = error.code() ;
		} catch( ...) {
			terminal.result.reset() ; // 例外終端に成功snapshotを残さない
			terminal.reading.reset() ; // 旧有効結果は呼出側のReadingRecordで保持する
			terminal.readingCompletion.reset() ; // 部分的な成功ownerを破棄する
			terminal.wholeText.reset() ; // 例外終端へ全文を残さない
			terminal.wholeTextCompletion.reset() ; // 全文の部分成功を残さない
			// engine例外をthread外へ漏らさず、次ジョブを受け付けられる状態に戻す
			terminal.error = ErrorKind::INTERNAL ;
		}
		// 入口拒否やrunner例外もNotStarted/元生成終端を保持します
		if( capture) {
			terminal.correctionDiagnostics = capture->snapshot() ;
		}
		publish( std::move( terminal)) ;
	}
	stopped_.store( true, std::memory_order_release) ;
}

} // namespace koefude
