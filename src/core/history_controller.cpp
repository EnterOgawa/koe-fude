//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file history_controller.cpp
 * @brief 履歴の非同期読み書きと最後の保存を待つ終了を実装します
 * @version 1.0.0
 * @date 2026/09/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/10 I/OをUIから分離し、revisionで古い保存通知を識別
 * 0.1.0 Shunji Ogawa 2026/10/19 callbackの契約・宣言の役割を明記
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1287の明示診断と隔離接続を追加
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1343の文章専用補正を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1344の明示API補正と独立保存を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1345の動的モデル・凍結単価と旧履歴互換を接続
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1370の文脈分割と明示途中再開を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372の独立話者投影と元原文保持を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1407の具体的な契約と処理意図を記載
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1409の役割・引数・戻り値と所有寿命を具体化
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
#include "core/history_controller.h"
#include "core/job_controller.h"
#include "platform/whole_text_journal_store.h"
#include <optional>
#include <exception>
#include <limits>
#include <utility>
#include <vector>
#include <windows.h>

#include <algorithm>
#include <chrono>

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief 保存失敗の通知に残すため、指定した履歴行の全文補正recordを検索します
 * @param[in] snapshot 失敗した保存要求の履歴snapshot。呼出し中だけ読み取ります
 * @param[in] id 保存失敗の通知対象となる履歴行ID。配列の添字ではありません
 * @return 最初にIDが一致した行のbounded recordのコピー。共有本文の所有権を共有します
 * 一致する行がなければ既定の空recordを返します。保存成功を示す値ではありません
 * @note 元snapshotとrecordを変更せず、I/Oを行いません
 */
//------------------------------------------------------------------------------
WholeTextBoundedRecord entryForFailure( const HistorySnapshot &snapshot, std::uint64_t id)
{
	// 保存要求に含まれる行を先頭から調べ、対象IDのrecordを見つけるか全行を調べて終了する
	for( const auto &entry : snapshot.entries) {
		// 失敗した行の前採用・最新試行だけを通知へ残し、別行のrecordを流用しない
		if( entry.id == id) {
			return entry.bounded ;
		}
	}
	return {} ;
}
/** @brief mutexの外でI/Oする固定非集約slotと事前ackです */
class HistoryController::PortState final : public IWholeTextPersistencePort,
										   public IApiTextPersistencePort,
										   public IApiAudioPersistencePort,
										   public ISpeakerTextPersistencePort {
	public:
	/// slot移動だけを保護
	std::mutex													 mutex ;
	/// controller workerへの起床
	std::condition_variable										 changed ;
	/// job workerへの確定通知
	std::condition_variable										 ack ;
	/// reserve1/receive2/retry3/discard4
	unsigned													 command = 0 ;
	/// Loaded ack後だけ
	bool														 ready = false ;
	/// 新予約拒否
	bool														 stopping = false ;
	/// storage解放済み
	bool														 ended = false ;
	/// 同lease terminal待ち
	bool														 reserved = false ;
	/// 一つのSaveFailed owner
	bool														 unsaved = false ;
	/// 固定ack
	bool														 done = false ;
	/// 原文/要求envelope
	std::optional<WholeTextCorrectionJobRequest>				 request ;
	/// 一つのmove-only J
	std::optional<WholeTextBoundedTerminal>						 terminal ;
	/// 一回移管
	std::optional<WholeTextBoundedStorageBudget>				 budget ;
	/// 不変ack
	std::shared_ptr<const WholeTextBoundedPersistenceCompletion> completion ;
	/// 予約前確保
	std::shared_ptr<WholeTextBoundedPersistenceCompletion>		 fallback ;
	/// reserve失敗だけ
	std::exception_ptr											 error ;
	/// terminal保存前の取消し
	std::stop_token												 receiveStop ;
	/// UI enqueueの対象
	std::uint64_t												 historyId = 0 ;
	/// 再試行でも同op
	std::uint64_t												 operationId = 0 ;
	/// 専用control ID
	std::uint64_t												 actionId = 0 ;
	/// take_eventまで新controlを拒否
	bool														 controlPending = false ;
	/// 最大一つのAPI保存要求
	std::optional<ApiTextPersistenceCompletion>					 apiValue ;
	/// 保存完了後も候補ownerを維持
	std::optional<ApiTextPersistenceCompletion>					 apiAck ;
	/// V7先行保存を必要とする予約
	bool														 apiBarrier = false ;
	/// active要求の同owner/key/op
	std::optional<ApiTextPersistenceCompletion>					 apiLease ;
	/// HTTP0再保存の同結果owner
	std::optional<ApiTextPersistenceCompletion>					 apiUnsaved ;
	/// metadataだけの最大1slot
	ApiModelCatalog												 catalogValue ;
	/// catalog同期ack
	bool														 catalogDone = false ;
	/// 同じwriterへの一slot
	std::optional<ApiAudioPersistenceCompletion>				 audioValue ;
	/// 原文owner付きACK
	std::optional<ApiAudioPersistenceCompletion>				 audioAck ;
	/// history/op/planの一active lease
	std::optional<ApiAudioRecord>								 audioLease ;
	/// 専用resume beginだけ旧終端から新Pendingへ進む
	bool														 audioResume = false ;
	/// UIの小CAS値
	ApiAudioResumeToken											 audioToken ;
	/// renameも検知する元owner
	std::shared_ptr<const Transcript>							 audioPreviousOwner ;
	/// workerからworkerへの所有移管
	std::optional<ApiAudioResumeSnapshot>						 audioSnapshot ;
	/// 取得ACK、例外も含む
	bool														 audioSnapshotDone = false ;
	/// 操作元の小CAS
	SpeakerTextTarget											 projectionTarget ;
	/// 最新試行と前採用
	SpeakerTextRecord											 projectionValue ;
	/// 同writerの不変原文
	std::optional<SpeakerTextSnapshot>							 projectionSnapshot ;
	/// 投影専用のACK
	std::optional<SpeakerTextCompletion>						 projectionAck ;
	/// 同source/opの処理所有者
	std::optional<SpeakerTextAttempt>							 projectionLease ;
	/// metadata floor予約を伴う入口
	bool														 projectionBegin = false ;
	/// 同じcommandの完了
	bool														 projectionDone = false ;
	/// 原子採用直前だけの取消し
	std::stop_token												 projectionStop ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 耐久正本を小tokenで取得します
	 * @param[in] target UI捕捉token
	 * @return 同じ原文/record
	 * @throws OperationError stale等
	 */
	//------------------------------------------------------------------------------
	SpeakerTextSnapshot											 loadSpeakerText( SpeakerTextTarget target) override
	{
		std::unique_lock lock( mutex) ; // slotだけを保護する
		// 同writerが未準備/停止中/別投影所有中ならsnapshotを渡さない
		if( !ready || stopping || ended || command || projectionLease) {
			throw OperationError( ErrorKind::INVALID_INPUT, ERROR_BUSY) ;
		}
		projectionTarget = target ;
		projectionSnapshot.reset() ;
		projectionDone = false ;
		error = {} ;
		command = 10 ;
		changed.notify_one() ;
		ack.wait( lock,
				 //------------------------------------------------------------------------------
				 /**
				  * @brief 同writerの完了か終了を待ちます
				  * @return 同期要求が終端ならtrue
				  */
				 //------------------------------------------------------------------------------
				 [this]
				 {
					 return projectionDone || ended ;
				 }) ;
		// writerのI/O失敗を音声処理のInternalへ読み替えず保存失敗として返します
		if( error) {
			try {
				std::rethrow_exception( error) ;
			} catch( const HistoryError &failure) {
				throw OperationError( ErrorKind::SAVE_FAILED, failure.code()) ;
			}
		}
		// writerが同source snapshotを返せなければ開始しない
		if( !projectionSnapshot) {
			throw OperationError( ErrorKind::SAVE_FAILED, ERROR_NOT_READY) ;
		}
		return std::move( *projectionSnapshot) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief metadata floor予約後にbegin/最終sidecarを保存します
	 * @param[in] target 同じ耐久原文
	 * @param[in] record 最新試行/前採用
	 * @param[in] begin trueは予約
	 * @param[in] stop 最終原子置換直前の取消し
	 * @return 同じsource/opの保存事実
	 * @throws OperationError stale/保存失敗
	 */
	//------------------------------------------------------------------------------
	SpeakerTextCompletion commitSpeakerText( SpeakerTextTarget target, SpeakerTextRecord record, bool begin,
											std::stop_token stop) override
	{
		std::unique_lock lock( mutex) ; // blocking I/Oはwriterへ渡す
		// 停止後の新beginと競合commandを拒否する
		if( !ready || ended || command || (stopping && begin)) {
			throw OperationError( ErrorKind::SAVE_FAILED, ERROR_BUSY) ;
		}
		projectionTarget = target ;
		projectionValue = std::move( record) ;
		projectionBegin = begin ;
		projectionStop = stop ;
		projectionAck.reset() ;
		projectionDone = false ;
		error = {} ;
		command = 11 ;
		changed.notify_one() ;
		ack.wait( lock,
				 //------------------------------------------------------------------------------
				 /**
				  * @brief 同writerの完了か終了を待ちます
				  * @return 同期要求が終端ならtrue
				  */
				 //------------------------------------------------------------------------------
				 [this]
				 {
					 return projectionDone || ended ;
				 }) ;
		// 原子保存のI/O診断を投影専用の保存失敗へ保ちます
		if( error) {
			try {
				std::rethrow_exception( error) ;
			} catch( const HistoryError &failure) {
				throw OperationError( ErrorKind::SAVE_FAILED, failure.code()) ;
			}
		}
		// 原子保存ACKがなければ投影完了を作らない
		if( !projectionAck) {
			throw OperationError( ErrorKind::SAVE_FAILED, ERROR_NOT_READY) ;
		}
		return std::move( *projectionAck) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 耐久正本を取得します
	 * @param[in] token 小CAS値
	 * @return 同じsource ownerとrecord
	 * @throws OperationError stale/削除/上限
	 */
	//------------------------------------------------------------------------------
	ApiAudioResumeSnapshot loadAudioResume( ApiAudioResumeToken token) override
	{
		std::unique_lock lock( mutex) ; // slotだけを保護、I/O/hashはworker
		// 別API操作、close、未読込の正本を取得しません
		if( !ready || stopping || ended || command || audioLease) {
			throw OperationError( ErrorKind::INVALID_INPUT, ERROR_BUSY) ;
		}
		audioToken = token ;
		audioSnapshot.reset() ;
		audioSnapshotDone = false ;
		error = {} ;
		command = 9 ;
		changed.notify_one() ;
		ack.wait( lock,
				 //------------------------------------------------------------------------------
				 /**
				  * @brief 正本取得の終端を待ちます
				  * @return 完了
				  */
				 //------------------------------------------------------------------------------
				 [this]
				 {
					 return audioSnapshotDone || ended ;
				 }) ;
		// 削除/stale/不正は元本文を変更せず拒否します
		if( error) {
			std::rethrow_exception( error) ;
		}
		if( !audioSnapshot) {
			throw OperationError( ErrorKind::SAVE_FAILED, ERROR_NOT_READY) ;
		}
		return std::move( *audioSnapshot) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 再開を新opへ耐久保存します
	 * @param[in] token 元CAS値
	 * @param[in] previousOwner 元snapshot owner
	 * @param[in] record 新Pending
	 * @param[in] source 同じ原文owner
	 * @return 新leaseのACK、失敗時送信禁止
	 */
	//------------------------------------------------------------------------------
	ApiAudioPersistenceCompletion beginAudioResume( ApiAudioResumeToken				 token,
												   std::shared_ptr<const Transcript> previousOwner,
												   ApiAudioRecord					 record,
												   std::shared_ptr<const Transcript> source) override
	{
		return exchangeAudio( std::move( record), std::move( source), true, {}, true, token, std::move( previousOwner)) ;
	}
	/// Pending通常保存を先行排出するbarrier
	bool						  audioBegin = false ;
	/// 音声の完全終端だけの置換前取消し
	std::stop_token				  audioCommitStop ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ履歴にだけ保存します
	 * @param[in] record 検証済み単調prefix
	 * @param[in] source 所有原文
	 * @param[in] begin 初回Pending保存barrierか
	 * @param[in] commitStop 完了の原子置換直前だけで確認する取消し
	 * @return 未保存prefixも保持するACK。停止中も既存terminalを排出します
	 */
	//------------------------------------------------------------------------------
	ApiAudioPersistenceCompletion commitAudio( ApiAudioRecord record, std::shared_ptr<const Transcript> source,
											  bool begin, std::stop_token commitStop) override
	{
		return exchangeAudio( std::move( record), std::move( source), begin, commitStop, false, {}, {}) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ一slotで保存ACKを待ちます
	 * @param[in] record 候補
	 * @param[in] source 同prefix
	 * @param[in] begin 初回barrier
	 * @param[in] commitStop 完全終端取消し
	 * @param[in] resume 専用再開
	 * @param[in] token 旧小CAS
	 * @param[in] previousOwner 旧所有値
	 * @return 同record/sourceのACK
	 */
	//------------------------------------------------------------------------------
	ApiAudioPersistenceCompletion exchangeAudio( ApiAudioRecord record, std::shared_ptr<const Transcript> source,
												bool begin, std::stop_token commitStop, bool resume,
												ApiAudioResumeToken				  token,
												std::shared_ptr<const Transcript> previousOwner)
	{
		std::unique_lock lock( mutex) ; // slot操作だけ、I/Oはworker
		// 停止中も受付済み音声終端を排出します
		if( !ready || ended || command || (stopping && begin)) {
			record.saveFailed = true ;
			return {std::move( record), std::move( source), false, ERROR_BUSY} ;
		}
		audioValue = ApiAudioPersistenceCompletion{std::move( record), std::move( source)} ;
		audioBegin = begin ;
		audioResume = resume ;
		audioToken = token ;
		audioPreviousOwner = std::move( previousOwner) ;
		audioAck.reset() ;
		command = 8 ;
		changed.notify_one() ;
		audioCommitStop = commitStop ;
		ack.wait( lock,
				 //------------------------------------------------------------------------------
				 /**
				  * @brief 同じwriterのACKだけを待ちます
				  * @return 終端
				  */
				 //------------------------------------------------------------------------------
				 [this]
				 {
					 return audioAck.has_value() || ended ;
				 }) ;
		// 保存不能でもmemory prefixを破棄しません
		if( !audioAck) {
			throw OperationError( ErrorKind::SAVE_FAILED) ;
		}
		return std::move( *audioAck) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ履歴workerへmetadata I/Oを渡します
	 * @param[in] save 保存か
	 * @param[in] value 保存値
	 * @return 読取/確定値
	 */
	//------------------------------------------------------------------------------
	ApiModelCatalog catalogExchange( bool save, const ApiModelCatalog &value)
	{
		std::unique_lock lock( mutex) ; // slotだけ保護
		// 停止中や別commandを上書きしません
		if( !ready || stopping || ended || command) {
			throw OperationError( ErrorKind::SAVE_FAILED) ;
		}
		catalogValue = value ;
		catalogDone = false ;
		error = {} ;
		command = save ? 7 : 6 ;
		changed.notify_one() ;
		ack.wait( lock,
				 //------------------------------------------------------------------------------
				 /**
				  * @brief 同じI/Oのackだけを待ちます
				  * @return 終端
				  */
				 //------------------------------------------------------------------------------
				 [this]
				 {
					 return catalogDone || ended ;
				 }) ;
		// worker終了・保存例外を成功へ変換しません
		if( !catalogDone) {
			throw OperationError( ErrorKind::SAVE_FAILED) ;
		}
		if( error) {
			std::rethrow_exception( error) ;
		}
		return catalogValue ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同folder排他で表示cacheを読みます
	 * @return 未設定なら空
	 */
	//------------------------------------------------------------------------------
	ApiModelCatalog loadCatalog() override
	{
		return catalogExchange( false, {}) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief catalogを履歴workerへ複写し、同じstorageのmetadata保存終了を待ちます
	 * @param[in] value 呼出し完了まで借用するcatalogです。受付時に内部slotへcopyし、呼出し元objectの所有権は移しません
	 * @throws OperationError 未読込み・停止・別command実行中、またはworker終了で保存ACKを得られない場合
	 * @pre 履歴worker自身から呼ばず、portと入力を復帰まで生存させます
	 * @note 待機中はmutexを解放し、保存I/Oは履歴workerで実行します。workerの保存例外やcopyの確保失敗は伝播します
	 * 正常復帰はcatalog保存呼出しの終了を示します。commit receiptは返さず、例外だけで未書込みとは断定できません
	 * @see koefude::IApiTextPersistencePort::saveCatalog
	 */
	//------------------------------------------------------------------------------
	void saveCatalog( const ApiModelCatalog &value) override
	{
		static_cast<void>( catalogExchange( true, value)) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 保存または送信前barrierを同期要求します
	 * @param[in] source 同じ原文owner
	 * @param[in] key 原文key
	 * @param[in] op 元試行ID
	 * @param[in] record 候補値
	 * @param[in] barrier trueはV7を先に保存
	 * @return 失敗でも同候補を保持
	 */
	//------------------------------------------------------------------------------
	ApiTextPersistenceCompletion commit( std::shared_ptr<const Transcript> source, ReadingSourceKey key,
										std::uint64_t op, ApiTextCorrectionRecord record, bool barrier) override
	{
		std::unique_lock lock( mutex) ; // 受付/所有移動だけ、I/Oなし
		// 未読込と重複slotを拒否し、停止中も既存API終端の保存は許可します
		if( !ready || ended || command || (stopping && barrier)) {
			ApiTextPersistenceCompletion failure{source, key, op, record} ; // memory保持
			failure.record.saveFailed = true ;
			failure.code = ERROR_BUSY ;
			return failure ;
		}
		apiValue = ApiTextPersistenceCompletion{std::move( source), key, op, std::move( record)} ;
		apiBarrier = barrier ;
		apiAck.reset() ;
		command = 5 ;
		changed.notify_one() ;
		ack.wait( lock,
				 //------------------------------------------------------------------------------
				 /**
				  * @brief 同じ保存workerの確定を待ちます
				  * @return 完了
				  */
				 //------------------------------------------------------------------------------
				 [this]
				 {
					 return apiAck.has_value() || ended ;
				 }) ;
		// ownerを持つ失敗終端を必ず返します
		if( !apiAck) {
			throw OperationError( ErrorKind::SAVE_FAILED) ;
		}
		return std::move( *apiAck) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 後続の補正をdispatchする前に、履歴workerでPendingとoperation watermarkを保存して予約ACKを待ちます
	 * @param[in] value 同期中だけ借用する補正要求です。原文共有owner、原文keyと生成operationId等を複写してportに保持します
	 * 呼出し元の要求objectの所有権は移しません
	 * @param[in] stop enqueue前の取消し確認に使うtokenです。受付後の予約保存には空tokenを渡し、ACK待機は中断しません
	 * @return 保存workerの予約ACKを経た、同じ要求に対応するmove専用の容量予約です。本文生成や終端採用の完了ではありません
	 * @throws OperationError 取消し、受付状態の拒否、予約失敗、または予約ACKにbudgetがない場合
	 * storageのHistoryErrorは元codeを保って用途別ErrorKindへ変換し、その他の例外は伝播します
	 * @pre 履歴worker自身から呼ばず、portを復帰まで生存させます
	 * @note 予約自身のowner/Pending保存I/Oはこの呼出し中に行います。待機中はmutexを解放し、ACK後だけ補正処理へ予約をmoveします
	 * @see koefude::IWholeTextPersistencePort::reserve
	 */
	//------------------------------------------------------------------------------
	WholeTextBoundedStorageBudget reserve( const WholeTextCorrectionJobRequest &value, std::stop_token stop) override
	{
		auto copy = value ; // reserve前に全copyを確保
		// この同期境界で所有する値、または確定状態の検証値です
		auto failure = std::shared_ptr<WholeTextBoundedPersistenceCompletion>(
			new WholeTextBoundedPersistenceCompletion) ; // 事前ack
		failure->source_ = value.source ;
		failure->request_ = value.request ;
		checkCancelled( stop) ;
		std::unique_lock lock( mutex) ; // enqueueだけ
		// 未読込・終了・重複予約は全I/O前に拒否します
		if( !ready || stopping || ended || command || reserved || unsaved) {
			throw OperationError( ErrorKind::MODEL_REJECTED,
								 static_cast<std::uint32_t>( WholeTextBoundedDiagnostic::PERSISTENCE_UNAVAILABLE)) ;
		}
		request = std::move( copy) ;
		fallback = std::move( failure) ;
		error = {} ;
		done = false ;
		command = 1 ;
		changed.notify_one() ;
		ack.wait( lock,
				 //------------------------------------------------------------------------------
				 /**
				  * @brief 固定reserve ackを待ちます
				  * @return 完了
				  */
				 //------------------------------------------------------------------------------
				 [this]
				 {
					 return done || ended ;
				 }) ;
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( error) {
			try {
				std::rethrow_exception( error) ;
			} catch( const HistoryError &historyError) {
				const auto kind = historyError.kind() ; // 保存workerの元分類を保持
				const auto operationKind =
					kind == HistoryErrorKind::TOO_LARGE									? ErrorKind::RESULT_TOO_LARGE
					: kind == HistoryErrorKind::CORRUPT									? ErrorKind::INVALID_INPUT
					: kind == HistoryErrorKind::READ || kind == HistoryErrorKind::WRITE ? ErrorKind::SAVE_FAILED
					: kind == HistoryErrorKind::INTERNAL								? ErrorKind::INTERNAL
														 : ErrorKind::MODEL_REJECTED ; // port契約分類
				throw OperationError( operationKind, historyError.code()) ;
			}
		}
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( !budget) {
			throw OperationError( ErrorKind::INTERNAL) ;
		}
		auto result = std::move( *budget) ; // 単独quota
		budget.reset() ;
		return result ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 予約と同じ原文owner・生成operationIdの終端を受領し、履歴workerの保存ACKを待ちます
	 * @param[in] value 値で受け取る終端ownerです。予約要求との照合後にjournal等をportへmoveし、呼出し元のmove前状態は復元しません
	 * @param[in] stop 保存層へ渡す取消しtokenです。受領後のACK待機は打ち切りません
	 * 保存層の確認時に停止済みなら完了候補をCancelledとして保存し、前採用を保ちます。耐久化済み結果は巻き戻しません
	 * @return 保存層が公開した不変ACKの共有ownerです。NULLでないこととsource/requestが今回の要求へ一致することを確認して使います
	 * receipt().committedはmetadata置換確定、record().saveState/saveErrorPresent等は保存失敗を示します
	 * complete()はそのreceipt確定とlatestState=COMPLETEがそろう時だけtrueで、受領や前採用の存在だけでは今回成功になりません
	 * @throws OperationError 予約なし・別command/終端あり・worker終了・原文owner/生成operationId不一致ではINVALID_INPUTです
	 * @pre 履歴worker自身から呼ばず、portを復帰まで生存させます
	 * @note 通常の保存失敗ではportが終端/journalを未保存として保持し、明示再保存へ残します
	 * receipt確定時に保持を解き、worker終了時の所有物解放は別に行います
	 * @see koefude::IWholeTextPersistencePort::receive
	 */
	//------------------------------------------------------------------------------
	std::shared_ptr<const WholeTextBoundedPersistenceCompletion> receive( WholeTextBoundedTerminal value,
																		 std::stop_token		  stop) override
	{
		std::unique_lock lock( mutex) ; // 所有移管だけ
		// 拒否時は呼出しownerを失わず、正常受領後は停止でも排出します
		if( !reserved || command || terminal || ended || !request ||
		   !detail::sameTranscriptOwner( value.source, request->source) ||
		   value.result.request.operationId != request->request.operationId) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
		terminal = std::move( value) ;
		receiveStop = stop ;
		done = false ;
		command = 2 ;
		changed.notify_one() ;
		ack.wait( lock,
				 //------------------------------------------------------------------------------
				 /**
				  * @brief 所有terminalの保存ackを待ちます
				  * @return 確定済み
				  */
				 //------------------------------------------------------------------------------
				 [this]
				 {
					 return done || ended ;
				 }) ;
		return completion ;
	}
} ;
//------------------------------------------------------------------------------
/**
 * @brief 専用threadでstorageの読込みを開始します
 * @param[in] storage このcontrollerが所有する同期storage。非NULL
 * @param[in] notifier UI起床通知。空を許可し、mutexの外で呼びます
 * @see koefude::HistoryController::HistoryController(std::unique_ptr< koefude::IHistoryStorage >, std::function< void()>)
 */
//------------------------------------------------------------------------------
HistoryController::HistoryController( std::unique_ptr<IHistoryStorage> storage, std::function<void()> notifier)
	: port_( std::make_shared<PortState>()), storage_( std::move( storage)), notifier_( std::move( notifier)), worker_(
																											  [this]
																											  {
																												  run() ;
																											  })
{
}

//------------------------------------------------------------------------------
/**
 * @brief 残る保存を終了してjoinします。UIではstopped確認後に破棄します
 * @see koefude::HistoryController::~HistoryController
 */
//------------------------------------------------------------------------------
HistoryController::~HistoryController()
{
	requestStop() ;
	worker_.join() ;
}

//------------------------------------------------------------------------------
/**
 * @brief 最新の履歴snapshotを保存待ち一件へ集約し、履歴workerへ書込みを依頼します
 * @param[in] snapshot 保存する不変履歴の共有owner。本文を深く複製せず、workerまで所有を保持します
 *                     nullptrは受付を拒否します。呼出し元のmove済み値を復元する契約ではありません
 * @return 受け付けた保存要求の非0の単調revision。実I/Oの完了や耐久保存成功ではありません
 * @retval 0 Loaded通知の未受領、停止中、nullptr入力、またはrevision枯渇で拒否しました
 * @throws std::system_error 受付mutexのlockに失敗した場合
 * @post 未開始の旧snapshotは今回の値で置き換わります。実行中の保存自体は中断しません
 * @note 保存結果はSaved/SaveFailedのrevisionで照合します。本文のhash計算やI/Oはworkerが行います
 * @see koefude::HistoryController::save
 */
//------------------------------------------------------------------------------
std::uint64_t HistoryController::save( std::shared_ptr<const HistorySnapshot> snapshot)
{
	const std::lock_guard lock( port_->mutex) ; // 受付状態と待機slotの差替えを保護
	// 読込み失敗後の空履歴で既存fileを上書きせず、終了後も要求を受け付けない
	if( !ready_ || stopping_ || !snapshot || nextRevision_ == 0) {
		return 0 ;
	}
	const auto revision = nextRevision_++ ; // 失敗した要求でも再利用しない番号
	pending_ = WriteRequest{revision, std::move( snapshot)} ;
	port_->changed.notify_one() ;
	return revision ;
}

//------------------------------------------------------------------------------
/**
 * @brief 有界全文補正の予約・終端保存を同じ履歴workerへ渡す共有portを取得します
 * @return 同一PortStateを共有所有する非NULLのIWholeTextPersistencePort。raw controller参照を保持しません
 *         返却ownerはcontroller破棄後も生存できますが、履歴workerを再開しません
 * @pre このcontrollerの破棄と並行して取得しないこと
 * @note writer終了後の新操作は終了状態により拒否されます。待機・取消し・ACKの条件は
 *       IWholeTextPersistencePortの各操作契約に従います。取得自体はI/Oや保存を開始しません
 * @see koefude::HistoryController::wholeTextPort
 */
//------------------------------------------------------------------------------
std::shared_ptr<IWholeTextPersistencePort> HistoryController::wholeTextPort() const noexcept
{
	return port_ ;
}
//------------------------------------------------------------------------------
/**
 * @brief API文章改善の原文照合・採用保存を同じ履歴workerへ渡す共有portを取得します
 * @return 同一PortStateを共有所有する非NULLのIApiTextPersistencePort。raw controller参照を保持しません
 *         返却ownerはcontroller破棄後も生存できますが、履歴workerを再開しません
 * @pre このcontrollerの破棄と並行して取得しないこと
 * @note writer終了後の新操作は終了状態により拒否されます。待機・取消し・ACKの条件は
 *       IApiTextPersistencePortの各操作契約に従います。取得自体はI/Oや保存を開始しません
 * @see koefude::HistoryController::apiTextPort
 */
//------------------------------------------------------------------------------
std::shared_ptr<IApiTextPersistencePort> HistoryController::apiTextPort() const noexcept
{
	return port_ ;
}
//------------------------------------------------------------------------------
/**
 * @brief 音声APIのintent・prefix・最終ACK保存を同じ履歴workerへ渡す共有portを取得します
 * @return 同一PortStateを共有所有する非NULLのIApiAudioPersistencePort。raw controller参照を保持しません
 *         返却ownerはcontroller破棄後も生存できますが、履歴workerを再開しません
 * @pre このcontrollerの破棄と並行して取得しないこと
 * @note writer終了後の新操作は終了状態により拒否されます。待機・取消し・ACKの条件は
 *       IApiAudioPersistencePortの各操作契約に従います。取得自体はI/Oや保存を開始しません
 * @see koefude::HistoryController::apiAudioPort
 */
//------------------------------------------------------------------------------
std::shared_ptr<IApiAudioPersistencePort> HistoryController::apiAudioPort() const noexcept
{
	return port_ ;
}
//------------------------------------------------------------------------------
/**
 * @brief 原APIとは独立した話者投影の保存を同じ履歴workerへ渡す共有portを取得します
 * @return 同一PortStateを共有所有する非NULLのISpeakerTextPersistencePort。raw controller参照を保持しません
 *         返却ownerはcontroller破棄後も生存できますが、履歴workerを再開しません
 * @pre このcontrollerの破棄と並行して取得しないこと
 * @note writer終了後の新操作は終了状態により拒否されます。待機・取消し・ACKの条件は
 *       ISpeakerTextPersistencePortの各操作契約に従います。取得自体はI/Oや保存を開始しません
 * @see koefude::HistoryController::speakerTextPort
 */
//------------------------------------------------------------------------------
std::shared_ptr<ISpeakerTextPersistencePort> HistoryController::speakerTextPort() const noexcept
{
	return port_ ;
}
//------------------------------------------------------------------------------
/**
 * @brief 同じ生成試行の未保存terminalを、生成し直さず専用control枠で保存再試行に予約します
 * @param[in] historyId 保存失敗したterminalの非0の履歴ID
 * @param[in] operationId 同じterminalの非0の生成操作ID。新しい生成IDではありません
 * @param[out] actionId 任意の受付ID出力。nullptr可。有効pointerならlock成功後に0へ初期化し、
 *                      受付成立後だけcontrol専用系列の非0 IDを書きます。復帰後は保持しません
 * @return 予約成立ならtrue。未読込み・停止・ID不正/枯渇・未受領control/実行commandあり、
 *         未保存terminalなし、または対象ID不一致ならfalseで、actionIdの出力は0です
 * @throws std::system_error 受付mutexのlockに失敗した場合。actionIdも変更しません
 * @note trueは耐久保存成功ではありません。actionIdに対応する後続HistoryEventで確認します
 * @see koefude::HistoryController::retryBoundedSave
 */
//------------------------------------------------------------------------------
bool HistoryController::retryBoundedSave( std::uint64_t historyId, std::uint64_t operationId, std::uint64_t *actionId)
{
	const std::lock_guard lock( port_->mutex) ; /* enqueueだけ */
	// 受付失敗ではIDを返しません
	if( actionId) {
		*actionId = 0 ;
	}
	// 専用未受領slotは普通保存から独立して保持します
	if( port_->controlPending || nextActionId_ == 0 || historyId == 0 || operationId == 0) {
		return false ;
	}
	if( !ready_ || stopping_ || port_->command || !port_->unsaved || !port_->terminal ||
	   port_->terminal->result.request.operationId != operationId ||
	   port_->terminal->result.request.source.historyId != historyId) {
		return false ;
	}
	port_->command = 3 ;
	port_->historyId = historyId ;
	port_->operationId = operationId ;
	port_->actionId = nextActionId_++ ;
	port_->controlPending = true ;
	// IDは受付成立後だけ公開します
	if( actionId) {
		*actionId = port_->actionId ;
	}
	port_->changed.notify_one() ;
	return true ;
}
//------------------------------------------------------------------------------
/**
 * @brief 指定した有界補正試行の明示削除を専用control枠へ予約します
 * @param[in] historyId 削除対象の非0の履歴ID
 * @param[in] operationId 削除対象の非0の生成操作ID。受付後workerで対象を照合します
 * @param[out] actionId 任意の受付ID出力。nullptr可。有効pointerならlock成功後に0へ初期化し、
 *                      受付成立後だけcontrol専用系列の非0 IDを書きます。復帰後は保持しません
 * @return 予約成立ならtrue。未読込み・停止・ID不正/枯渇・未受領control/実行commandあり、
 *         または生成予約のlease使用中ならfalseで、actionIdの出力は0です
 * @throws std::system_error 受付mutexのlockに失敗した場合。actionIdも変更しません
 * @note trueは削除済みを意味しません。後続HistoryEventのmetadata確定とcleanup結果を確認します
 * @see koefude::HistoryController::discardBoundedAttempt
 */
//------------------------------------------------------------------------------
bool HistoryController::discardBoundedAttempt( std::uint64_t historyId, std::uint64_t operationId,
											  std::uint64_t *actionId)
{
	const std::lock_guard lock( port_->mutex) ; /* enqueueだけ */
	// 受付失敗ではIDを返しません
	if( actionId) {
		*actionId = 0 ;
	}
	// 専用未受領slotは普通保存から独立して保持します
	if( port_->controlPending || nextActionId_ == 0 || historyId == 0 || operationId == 0) {
		return false ;
	}
	if( !ready_ || stopping_ || port_->command || port_->reserved) {
		return false ;
	}
	port_->command = 4 ;
	port_->historyId = historyId ;
	port_->operationId = operationId ;
	port_->actionId = nextActionId_++ ;
	port_->controlPending = true ;
	// IDは受付成立後だけ公開します
	if( actionId) {
		*actionId = port_->actionId ;
	}
	port_->changed.notify_one() ;
	return true ;
}
//------------------------------------------------------------------------------
/**
 * @brief 新規受付を止め、最後の待機snapshotと受付済み操作を処理して終了するよう要求します
 * @throws std::system_error 共有portのmutexをlockできなかった場合
 * @note 待機snapshotを捨てずworkerを起こします。joinや終了待ちは行わず、完了はstoppedで確認します
 * @see koefude::HistoryController::requestStop
 */
//------------------------------------------------------------------------------
void HistoryController::requestStop()
{
	const std::lock_guard lock( port_->mutex) ; // 待機snapshotを残したまま受付だけを停止
	stopping_ = true ;
	port_->stopping = true ;
	port_->changed.notify_one() ;
}

//------------------------------------------------------------------------------
/**
 * @brief 最後の通知、portの終了設定、storage解放まで履歴workerが終了したか確認します
 * @return worker末尾のreleaseに対応するacquire読取りがtrueなら終了済み、falseなら終了未確認
 * @pre このcontrollerの寿命を呼出し完了まで保持し、破棄と並行しないこと
 * @note 停止要求済み、保存成功、通知受領済みを表す値ではありません
 * @see koefude::HistoryController::stopped
 */
//------------------------------------------------------------------------------
bool HistoryController::stopped() const noexcept
{
	return stopped_.load( std::memory_order_acquire) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 未受領control通知を優先し、なければ最新の通常通知を一度だけ取り出します
 * @return 所有通知。新しい通知がなければnullopt。本文等のDTOは共有所有し、control結果を含む
 *         通知の取得は処理成功を意味しません。LoadFailedを空履歴のLoadedとして扱わないでください
 * @throws std::system_error 共有portのmutexをlockできなかった場合。通知は消費しません
 * @post Loadedが通常slotにあれば保存受付のreadyを有効化します。controlを返す時はその専用枠を解放し、
 *       通常slotは残します。通常通知を返す時だけそのslotを空にします
 * @note 通知内のrevision/action_idと元要求を照合します。I/Oや新しい保存要求は開始しません
 * @see koefude::HistoryController::takeEvent
 */
//------------------------------------------------------------------------------
std::optional<HistoryEvent> HistoryController::takeEvent()
{
	const std::lock_guard lock( port_->mutex) ; // 未受領通知の所有権を移す
	// 読込結果を受領する前の保存で、Loaded通知を上書きさせない
	if( event_ && event_->kind == HistoryEventKind::LOADED) {
		ready_ = true ;
		port_->ready = true ;
	}
	// controlを受領するまで次controlの受付を再開しません
	if( controlEvent_) {
		port_->controlPending = false ;
		return std::exchange( controlEvent_, std::nullopt) ;
	}
	return std::exchange( event_, std::nullopt) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 通知を所有格納し、排他を解いてからUIを起こします
 * @param[in] event 最新の確定状態
 * @see koefude::HistoryController::publish
 */
//------------------------------------------------------------------------------
void HistoryController::publish( HistoryEvent event) noexcept
{
	{
		const std::lock_guard lock( port_->mutex) ; // I/OやUI通知を含めない短い差替え
		// 非集約controlを普通Saved/SaveFailedから保護します
		if( event.actionId != 0) {
			controlEvent_ = std::move( event) ;
		} else {
			event_ = std::move( event) ;
		}
	}
	try {
		// pollingだけでも使用できるように空callbackを許可する
		if( notifier_) {
			notifier_() ;
		}
	} catch( ...) {
		// 所有eventを残し、失敗を記録してUIの定期回収へ委ねる
		notificationFailed_.store( true) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 読込み、集約保存、例外の通知、終了を専用threadで処理します
 * @see koefude::HistoryController::run
 */
//------------------------------------------------------------------------------
void HistoryController::run() noexcept
{
	HistorySnapshot			   durable ; // storageが確定した唯一の正本
	std::vector<std::uint64_t> deletedIds ; // stale snapshotによる削除済みIDの復活を防止
	bool					   loaded = false ; // 失敗後に空履歴を書きません
	try {
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( !storage_) {
			throw HistoryError( HistoryErrorKind::INTERNAL) ;
		}
		deletedIds.reserve( 32) ; // thread entryのtry内で小さい記憶枠を確保
		durable = storage_->load() ;
		publish( {HistoryEventKind::LOADED, 0, std::make_shared<const HistorySnapshot>( durable)}) ;
		loaded = true ;
	} catch( const HistoryError &error) {
		publish( {HistoryEventKind::LOAD_FAILED, 0, {}, error.kind(), error.code()}) ;
	} catch( ...) {
		publish( {HistoryEventKind::LOAD_FAILED, 0, {}, HistoryErrorKind::INTERNAL}) ;
	}
	// 普通saveと一controlを同じworkerで直列処理します
	while( loaded) {
		WriteRequest ordinary ; /* 所有snapshot */
		unsigned	 command = 0 ; /* 非集約処理 */
		{
			std::unique_lock lock( port_->mutex) ; /* I/O時は解除 */
			port_->changed.wait( lock,
								//------------------------------------------------------------------------------
								/**
								 * @brief 待機中にI/Oをしません
								 * @return 処理または停止
								 */
								//------------------------------------------------------------------------------
								[this]
								{
									return stopping_ || pending_ || port_->command ;
								}) ;
			// 初回音声intentより先に同IDのPending普通保存を有限barrierで排出します
			if( port_->command == 8 && port_->audioBegin && !pending_ && port_->audioValue &&
			   std::none_of( durable.entries.begin(), durable.entries.end(),
							//------------------------------------------------------------------------------
							/**
							 * @brief Pending受付IDを探します
							 * @param[in] e 記録
							 * @return 一致
							 */
							//------------------------------------------------------------------------------
							[this]( const auto &e)
							{
								return e.id == port_->audioValue->record.historyId ;
							})) {
				static_cast<void>( port_->changed.wait_for( lock, std::chrono::seconds( 5),
					//------------------------------------------------------------------------------
					/**
					 * @brief UIの受付保存か停止でbarrierを解きます
					 * @return 到着
					 */
					//------------------------------------------------------------------------------
					[this]
					{
						return pending_.has_value() || stopping_ ;
					})) ;
			}
			// 既に受理したterminalは停止要求より先に処理します
			if( port_->command == 11 && !port_->projectionBegin) {
				command = 11 ;
				// 既存音声の終端保存をpending通常保存より先に排出する
			} else if( port_->command == 8 && (!port_->audioBegin || !pending_)) {
				command = 8 ;
			} else if( port_->command == 9 && !pending_) {
				command = 9 ;
			} else if( port_->command == 2 || port_->command == 3 || port_->command == 4 || port_->command == 5) {
				command = port_->command ;
			} else if( pending_) {
				ordinary = std::move( *pending_) ;
				pending_.reset() ;
			} else if( port_->command) {
				command = port_->command ;
				// 全操作のlease解放を待ってからwriterを終了する
			} else if( stopping_ && !port_->reserved && !port_->apiLease && !port_->audioLease &&
					  !port_->projectionLease) {
				break ;
			} else {
				continue ;
			}
		}
		// 投影も同じ一writerで予約/最終ACKし、raw/API recordを書き戻さない
		if( command == 10 || command == 11) {
			try {
				// 同writerの耐久原文を持つ対象行
				auto entry = std::find_if( durable.entries.begin(), durable.entries.end(),
					//------------------------------------------------------------------------------
					/**
					 * @brief 安定行IDを照合します
					 * @param[in] e 耐久履歴
					 * @return 同じ対象ならtrue
					 */
					//------------------------------------------------------------------------------
					[this]( const auto &e)
					{
						return e.id == port_->projectionTarget.historyId ;
					}) ;
				// 削除/source更新/破損file/他用途leaseへの古い要求を拒否する
				if( durable.speakerTextCorrupt || entry == durable.entries.end() || !entry->result || !entry->apiAudio ||
				   speakerTextTarget( *entry->apiAudio) != port_->projectionTarget ||
				   fingerprintReadingSource( *entry->result) != port_->projectionTarget.sourceSha ||
				   (port_->audioLease && port_->audioLease->historyId == entry->id) ||
				   (port_->apiLease && port_->apiLease->key.historyId == entry->id) || entry->bounded.inFlight) {
					throw OperationError( ErrorKind::INVALID_INPUT, ERROR_REVISION_MISMATCH) ;
				}
				// 読取commandは同writerの共有不変ownerだけを返す
				if( command == 10) {
					port_->projectionSnapshot = SpeakerTextSnapshot{port_->projectionTarget, entry->result,
																	entry->apiAudio, entry->input, entry->speakerText} ;
				} else {
					const auto &latest = port_->projectionValue.latest ; // 同source/opの一試行
					// 最新試行のopとtargetが要求に一致しなければ拒否する
					if( !latest || latest->target != port_->projectionTarget || !latest->operationId ||
					   latest->operationId == UINT64_MAX || latest->operationId == entry->apiAudio->operationId) {
						throw OperationError( ErrorKind::INVALID_INPUT, ERROR_INVALID_DATA) ;
					}
					// beginはnativeを開始する前にglobal floorを耐久予約する
					if( port_->projectionBegin) {
						// 重複/旧op/API同opやPending以外の開始を拒否する
						if( port_->projectionLease || latest->state != SpeakerTextState::PENDING ||
						   (entry->speakerText.latest &&
							entry->speakerText.latest->operationId >= latest->operationId)) {
							throw OperationError( ErrorKind::INVALID_INPUT, ERROR_BUSY) ;
						}
						HistorySnapshot reservation = durable ; // raw/schemaを変えずfloor metadataだけ予約
						reservation.nextOperationId = std::max( reservation.nextOperationId, latest->operationId + 1) ;
						HistorySaveReceipt floorReceipt ; // sidecar破損/削除後もopを再利用しない
						try {
							storage_->saveAudio( reservation, {}, floorReceipt) ;
						} catch( ...) {
							// metadata予約が置換済みなら失敗時も番号を巻き戻さない
							if( floorReceipt.committed) {
								durable = std::move( reservation) ;
							}
							throw ;
						}
						durable = std::move( reservation) ;
						// 終端は同source/opの現在leaseからだけ受け取る
					} else if( !port_->projectionLease || port_->projectionLease->operationId != latest->operationId ||
							  port_->projectionLease->target != port_->projectionTarget ||
							  latest->state <= SpeakerTextState::SAVING) {
						throw OperationError( ErrorKind::INVALID_INPUT, ERROR_REVISION_MISMATCH) ;
					}
					HistorySnapshot candidate = durable ; // 派生sidecarだけの保存候補
					// 候補snapshot内の同じ安定行ID
					auto			current = std::find_if( candidate.entries.begin(), candidate.entries.end(),
						//------------------------------------------------------------------------------
						/**
						 * @brief 候補内の対象IDを照合します
						 * @param[in] e 候補履歴
						 * @return 同じ安定IDならtrue
						 */
						//------------------------------------------------------------------------------
						[this]( const auto &e)
						{
							return e.id == port_->projectionTarget.historyId ;
						}) ;
					const auto		previous = current->speakerText.adopted ; // 最新失敗で前採用を消さない
					// 失敗/取消し/支持なしでは前採用を維持する
					if( port_->projectionBegin || latest->state != SpeakerTextState::READY) {
						port_->projectionValue.adopted = previous ;
					} else {
						// Ready採用にも同opの完整recordを必須とする
						if( !port_->projectionValue.adopted ||
						   port_->projectionValue.adopted->operationId != latest->operationId) {
							throw OperationError( ErrorKind::INVALID_INPUT) ;
						}
						validateSpeakerTextRecord( *port_->projectionValue.adopted, *current->result, *current->apiAudio,
												  true) ;
					}
					current->speakerText = port_->projectionValue ;
					HistorySaveReceipt receipt ; // 原子置換直後を成功の正本とする
					try {
						storage_->saveSpeakerText( candidate, port_->projectionStop, receipt) ;
					} catch( const OperationError &error) {
						// 原子置換後は取消しより耐久した採用を優先する
						if( receipt.committed) { /* 置換後の取消しより耐久成功を優先する */
							// 置換前の取消しだけを前採用を保つ終端へ変換する
						} else if( error.kind() == ErrorKind::CANCELLED && !port_->projectionBegin) {
							current->speakerText.adopted = previous ;
							current->speakerText.latest->state = SpeakerTextState::CANCELLED ;
							current->speakerText.latest->error = ErrorKind::CANCELLED ;
							storage_->saveSpeakerText( candidate, {}, receipt) ;
						} else {
							throw ;
						}
					}
					// 置換事実がなければ成功ACKを返さない
					if( !receipt.committed) {
						throw OperationError( ErrorKind::SAVE_FAILED) ;
					}
					durable = std::move( candidate) ;
					// 原子保存後に確定した同source記録
					const auto saved = std::find_if( durable.entries.begin(), durable.entries.end(),
						//------------------------------------------------------------------------------
						/**
						 * @brief 置換後の同じ行だけを返します
						 * @param[in] e 耐久履歴
						 * @return 同じ安定IDならtrue
						 */
						//------------------------------------------------------------------------------
						[this]( const auto &e)
						{
							return e.id == port_->projectionTarget.historyId ;
						}) ;
					port_->projectionAck = SpeakerTextCompletion{port_->projectionTarget, saved->speakerText, true, 0} ;
					// begin成功時だけ投影leaseを所有し、終端で解放する
					if( port_->projectionBegin) {
						port_->projectionLease = *latest ;
					} else {
						port_->projectionLease.reset() ;
					}
				}
			} catch( ...) {
				port_->error = std::current_exception() ;
				// 終端失敗でもleaseを残してcloseを塞がない
				if( command == 11 && !port_->projectionBegin) {
					port_->projectionLease.reset() ;
				}
			}
			{
				const std::lock_guard lock( port_->mutex) ;
				port_->command = 0 ;
				port_->projectionDone = true ;
				port_->ack.notify_all() ;
			}
			continue ;
		}
		// UIの本文copy/hashなしで同じ耐久recordをASR workerへ渡します
		if( command == 9) {
			try {
				const auto entry = std::find_if( durable.entries.begin(), durable.entries.end(),
					//------------------------------------------------------------------------------
					/**
					 * @brief 小IDを正本へ照合します
					 * @param[in] e 候補
					 * @return 同じ行
					 */
					//------------------------------------------------------------------------------
					[this]( const auto &e)
					{
						return e.id == port_->audioToken.historyId ;
					}) ;
				// 同じupdate/plan/source/prefix/課金不明だけを受理します
				if( entry == durable.entries.end() || !entry->apiAudio || !entry->result || port_->audioLease ||
				   apiAudioResumeToken( *entry->apiAudio) != port_->audioToken) {
					throw OperationError( ErrorKind::INVALID_INPUT, ERROR_REVISION_MISMATCH) ;
				}
				validateApiAudioRecord( *entry->apiAudio, entry->result.get()) ;
				port_->audioSnapshot = ApiAudioResumeSnapshot{*entry->apiAudio, entry->result, entry->input} ;
			} catch( ...) {
				port_->error = std::current_exception() ; // 本人本文を例外へ含めません
			}
			{
				std::lock_guard lock( port_->mutex) ; // ACKだけを公開
				port_->command = 0 ;
				port_->audioSnapshotDone = true ;
			}
			port_->ack.notify_one() ;
			continue ;
		}
		// 音声intent/prefix/terminalも同じatomic通常履歴置換へ直列化します
		if( command == 8) {
			auto value = *port_->audioValue ; // command中はcallerが変更しないowner
			try {
				validateApiAudioRecord( value.record, value.source.get()) ;
				auto candidate = durable ; // 失敗時は最後の耐久prefixを保持
				auto entry = std::find_if( candidate.entries.begin(), candidate.entries.end(),
					//------------------------------------------------------------------------------
					/**
					 * @brief 同じ永続IDだけを探します
					 * @param[in] e 記録
					 * @return 一致
					 */
					//------------------------------------------------------------------------------
					[&value]( const auto &e)
					{
						return e.id == value.record.historyId ;
					}) ;
				// 未受付/削除/番号枯渇と別opの保存を拒否します
				if( entry == candidate.entries.end() || !value.source || !value.record.operationId ||
				   value.record.operationId == UINT64_MAX ||
				   (entry->apiAudio && entry->apiAudio->update == UINT64_MAX)) {
					throw HistoryError( HistoryErrorKind::CORRUPT) ;
				}
				// 専用beginは旧終端のCASとsnapshot ownerを再確認します
				if( port_->audioResume) {
					if( port_->audioLease || !entry->apiAudio ||
					   apiAudioResumeToken( *entry->apiAudio) != port_->audioToken ||
					   !detail::sameTranscriptOwner( entry->result, port_->audioPreviousOwner) ||
					   !detail::sameTranscriptOwner( value.source, port_->audioPreviousOwner) ||
					   !validApiAudioTransition( *entry->apiAudio, value.record, true)) {
						throw HistoryError( HistoryErrorKind::BUSY, ERROR_REVISION_MISMATCH) ;
					}
				} else if( port_->audioBegin) {
					// 新規の入口を既存recordへ無条件に緩めません
					if( entry->state != HistoryState::PENDING || entry->apiAudio) {
						throw HistoryError( HistoryErrorKind::CORRUPT) ;
					}
				} else {
					// intent/prefix/終端は同じactive leaseの単調遷移だけです
					if( !port_->audioLease || !entry->apiAudio ||
					   port_->audioLease->historyId != value.record.historyId ||
					   port_->audioLease->operationId != value.record.operationId ||
					   port_->audioLease->plan.digest != value.record.plan.digest ||
					   !validApiAudioTransition( *entry->apiAudio, value.record, false)) {
						throw HistoryError( HistoryErrorKind::CORRUPT) ;
					}
				}
				value.record.update = entry->apiAudio ? entry->apiAudio->update + 1 : 1 ;
				value.record.saveFailed = false ;
				entry->apiAudio = std::make_shared<const ApiAudioRecord>( value.record) ;
				entry->result = value.source ;
				entry->filter = 0 ;
				const auto terminal = value.record.terminal ; // 固定状態から履歴分類へ写す
				entry->state =
					terminal == ApiAudioTerminal::COMPLETED
						? (value.source->diarizationWarning ? HistoryState::COMPLETED_WITH_WARNINGS
															: HistoryState::COMPLETED)
					: terminal == ApiAudioTerminal::NO_SPEECH || terminal == ApiAudioTerminal::EMPTY_TRANSCRIPT
						? HistoryState::NO_TEXT
					: terminal == ApiAudioTerminal::CANCELLED	? HistoryState::CANCELLED
					: terminal == ApiAudioTerminal::INTERRUPTED ? HistoryState::INTERRUPTED
					: terminal == ApiAudioTerminal::FAILURE		? HistoryState::FAILURE
																: HistoryState::PENDING ;
				entry->error = value.record.error ;
				entry->code = value.record.code ;
				candidate.nextOperationId = std::max( candidate.nextOperationId, value.record.operationId + 1) ;
				HistorySaveReceipt receipt ; // 音声APIだけの線形化checkpoint
				try {
					storage_->saveAudio( candidate, port_->audioCommitStop, receipt) ;
				} catch( const OperationError &error) {
					// 原子置換前の取消しだけをCancelled prefixへ確定し、完了へしません
					if( error.kind() != ErrorKind::CANCELLED || receipt.committed) {
						throw ;
					}
					value.record.terminal = ApiAudioTerminal::CANCELLED ;
					value.record.error = ErrorKind::CANCELLED ;
					value.record.code = error.code() ;
					syncApiAudioAttempt( value.record) ;
					entry->apiAudio = std::make_shared<const ApiAudioRecord>( value.record) ;
					entry->state = HistoryState::CANCELLED ;
					entry->error = ErrorKind::CANCELLED ;
					entry->code = error.code() ;
					storage_->saveAudio( candidate, {},
										receipt) ; // 取消し終端は排出し再送しません
				}
				durable = std::move( candidate) ;
				value.committed = true ;
			} catch( const HistoryError &error) {
				value.record.saveFailed = true ;
				value.code = error.code() ;
			} catch( const OperationError &error) {
				value.record.saveFailed = true ;
				value.code = error.code() ;
			} catch( ...) {
				value.record.saveFailed = true ;
				value.code = ERROR_INTERNAL_ERROR ;
			}
			{
				const std::lock_guard lock( port_->mutex) ; // ACK/leaseの所有移動だけ
				// 保存失敗と終端でactive leaseを解放し、次fileを許可します
				if( value.committed && apiAudioActive( value.record.terminal)) {
					port_->audioLease = value.record ;
				} else {
					port_->audioLease.reset() ;
				}
				port_->audioAck = std::move( value) ;
				port_->audioValue.reset() ;
				port_->audioPreviousOwner.reset() ;
				port_->command = 0 ;
			}
			port_->ack.notify_one() ;
			continue ;
		}
		// metadata I/Oも既存storageとfolder排他を共有し単一writerへ直列化します
		if( command == 6 || command == 7) {
			try {
				// 固定cacheだけを扱い通常原文を変更しません
				if( command == 7) {
					storage_->saveCatalog( port_->catalogValue) ;
				} else {
					port_->catalogValue = storage_->loadCatalog() ;
				}
			} catch( ...) {
				port_->error = std::current_exception() ;
			}
			{
				const std::lock_guard lock( port_->mutex) ; // ackだけを保護
				port_->command = 0 ;
				port_->catalogDone = true ;
			}
			port_->ack.notify_all() ;
			continue ;
		}
		// API保存は同じstorage ownerと既存lockの下で直列化します
		if( command == 5) {
			auto value = *port_->apiValue ; // command中は他threadが変更しない所有値
			try {
				const auto entry = std::find_if( durable.entries.begin(), durable.entries.end(),
					//------------------------------------------------------------------------------
					/**
					 * @brief 同じ原文keyを探します
					 * @param[in] e 記録
					 * @return 同ID
					 */
					//------------------------------------------------------------------------------
					[&value]( const auto &e)
					{
						return e.id == value.key.historyId ;
					}) ; // 最大32件
				// 最新Savedの原文owner/key以外はHTTP前に拒否します
				if( durable.apiSidecarCorrupt || durable.apiSidecarSyncFailed || entry == durable.entries.end() ||
				   !entry->result || !detail::sameTranscriptOwner( entry->result, value.source) ||
				   value.key.sha256 != fingerprintReadingSource( *entry->result) || value.operationId == 0 ||
				   value.operationId == std::numeric_limits<std::uint64_t>::max()) {
					throw HistoryError( HistoryErrorKind::CORRUPT) ;
				}
				// 別opや別sourceの候補を保存portへ移植しません
				if( (value.record.latest && (value.record.latest->operationId != value.operationId ||
											value.record.latest->draft.source != value.key)) ||
				   (value.record.adopted && value.record.adopted->draft.source != value.key) ||
				   (port_->apiLease &&
					(port_->apiLease->operationId != value.operationId || port_->apiLease->key != value.key ||
					 !detail::sameTranscriptOwner( port_->apiLease->source, value.source)))) {
					throw HistoryError( HistoryErrorKind::CORRUPT) ;
				}
				// reserve以外は耐久試行または同じ未保存結果にだけ束縛します
				if( !port_->apiBarrier && !port_->apiLease &&
				   !(entry->apiText.latest && entry->apiText.latest->operationId == value.operationId) &&
				   !(entry->apiText.adopted && entry->apiText.adopted->operationId == value.operationId) &&
				   !(port_->apiUnsaved && port_->apiUnsaved->operationId == value.operationId &&
					 port_->apiUnsaved->key == value.key)) {
					throw HistoryError( HistoryErrorKind::BUSY) ;
				}
				// Pending予約だけは先にV7原文とoperation floorを確定します
				if( port_->apiBarrier) {
					// 同じ同意/operationを再送へ使いません
					if( entry->apiText.latest &&
					   (entry->apiText.latest->operationId >= value.operationId ||
						entry->apiText.latest->consent.id == value.record.latest->consent.id)) {
						throw HistoryError( HistoryErrorKind::BUSY) ;
					}
					durable.nextOperationId = std::max( durable.nextOperationId, value.operationId + 1) ;
					storage_->save( durable) ;
				}
				// 初回barrier失敗の同結果再保存でもfloorをV7へ先に確定します
				if( value.operationId >= durable.nextOperationId) {
					durable.nextOperationId = value.operationId + 1 ;
					storage_->save( durable) ;
				}
				auto	   candidate = durable ; // sidecar失敗時に前採用を維持
				const auto target = std::find_if( candidate.entries.begin(), candidate.entries.end(),
					//------------------------------------------------------------------------------
					/**
					 * @brief 同じ履歴だけを更新します
					 * @param[in] e 記録
					 * @return 同ID
					 */
					//------------------------------------------------------------------------------
					[&value]( const auto &e)
					{
						return e.id == value.key.historyId ;
					}) ; // 有限検索
				// 古いUI snapshotから採用済み値を巻き戻しません
				if( value.record.update < target->apiText.update) {
					throw HistoryError( HistoryErrorKind::BUSY) ;
				}
				value.record.update = target->apiText.update + 1 ;
				value.record.saveFailed = false ;
				target->apiText = value.record ;
				storage_->saveApiText( candidate) ;
				value.committed = true ;
				durable = std::move( candidate) ;
				// 最初のbarrier成功で同opのleaseを保持します
				if( port_->apiBarrier) {
					port_->apiLease = value ;
				}
			} catch( const HistoryError &error) {
				value.record.saveFailed = true ;
				value.code = error.code() ;
			} catch( ...) {
				value.record.saveFailed = true ;
				value.code = ERROR_INTERNAL_ERROR ;
			}
			{
				const std::lock_guard lock( port_->mutex) ; // ackの所有移動だけ
				// 失敗ownerは同結果の再保存まで保持し、送信再開には使いません
				if( !value.committed) {
					port_->apiUnsaved = value ;
				} else {
					port_->apiUnsaved.reset() ;
				}
				// 終端または保存失敗ではactive leaseを解放します
				if( !value.committed || !value.record.latest ||
				   (value.record.latest->state != ApiTextState::PENDING &&
					value.record.latest->state != ApiTextState::SENDING)) {
					port_->apiLease.reset() ;
				}
				port_->apiAck = std::move( value) ;
				port_->apiValue.reset() ;
				port_->command = 0 ;
			}
			port_->ack.notify_one() ;
			continue ;
		}
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( command == 1) {
			try {
				// この同期境界で所有する値、または確定状態の検証値です
				auto budget = storage_->reserveBounded( durable, *port_->request, {}) ; /* 普通save完了後だけ予約 */
				const std::lock_guard lock( port_->mutex) ; /* ack所有移動 */
				port_->budget = std::move( budget) ;
				port_->reserved = true ;
				port_->command = 0 ;
				port_->done = true ;
			} catch( ...) {
				const std::lock_guard lock( port_->mutex) ; /* fixed error ack */
				port_->error = std::current_exception() ;
				port_->request.reset() ;
				port_->fallback.reset() ;
				port_->command = 0 ;
				port_->done = true ;
			}
			port_->ack.notify_one() ;
			continue ;
		}
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( command == 2 || command == 3) {
			HistorySaveReceipt											 receipt ; /* noalloc scalar */
			std::shared_ptr<const WholeTextBoundedPersistenceCompletion> completion ; /* 既存owner */
			try {
				completion = storage_->persistBounded( durable, *port_->terminal, receipt,
													  command == 2 ? port_->receiveStop : std::stop_token{}) ;
			} catch( const HistoryError &error) {
				completion = storage_->boundedFailure( error.kind(), error.code()) ;
				// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
				if( !completion) {
					port_->fallback->record_.saveState = WholeTextBoundedSaveState::SAVE_FAILED ;
					port_->fallback->record_.saveErrorPresent = true ;
					port_->fallback->record_.saveErrorCode = error.code() ;
					port_->fallback->record_.saveErrorKind = static_cast<std::uint32_t>( error.kind()) ;
					completion = port_->fallback ;
				}
			} catch( ...) {
				completion = storage_->boundedFailure( HistoryErrorKind::INTERNAL, 0) ;
				// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
				if( !completion) {
					port_->fallback->record_.saveState = WholeTextBoundedSaveState::SAVE_FAILED ;
					port_->fallback->record_.saveErrorPresent = true ;
					port_->fallback->record_.saveErrorKind = static_cast<std::uint32_t>( HistoryErrorKind::INTERNAL) ;
					port_->fallback->record_.saveErrorCode = 0 ;
					completion = port_->fallback ;
				}
			}

			std::optional<WholeTextBoundedTerminal> releasedTerminal ; // handle解放をmutexの外へ移すowner
			{
				const std::lock_guard lock( port_->mutex) ; /* 同じslotを解決 */
				port_->completion = completion ;
				port_->reserved = false ;
				port_->unsaved = !receipt.committed ;
				port_->command = 0 ;
				port_->done = true ;
				// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
				if( receipt.committed) {
					releasedTerminal = std::move( port_->terminal) ;
					port_->terminal.reset() ;
					port_->request.reset() ;
					port_->fallback.reset() ;
				}
			}
			releasedTerminal.reset() ; // CloseHandle/cleanupはmutexを保持せず行います
			port_->ack.notify_one() ;
			// 再保存結果はUIが明示的に受領できます。普通revisionのdirtyはackしません
			if( command == 3) {
				HistoryEvent event ; /* 不変結果通知 */
				event.kind = HistoryEventKind::BOUNDED_SAVED ;
				event.historyId = port_->historyId ;
				event.operationId = port_->operationId ;
				event.actionId = port_->actionId ;
				event.action = 0 ;
				event.controlReceipt = receipt ;
				// 元保存分類はcontrol失敗でも失いません
				if( completion && completion->record().saveErrorPresent) {
					event.error = static_cast<HistoryErrorKind>( completion->record().saveErrorKind) ;
					event.code = completion->record().saveErrorCode ;
				}
				event.bounded = std::move( completion) ;
				publish( std::move( event)) ;
			}
			continue ;
		}
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( command == 4) {
			HistoryEvent event ; /* 削除結果 */
			event.historyId = port_->historyId ;
			event.operationId = port_->operationId ;
			event.actionId = port_->actionId ;
			event.action = 1 ;
			try {
				// この同期境界で所有する値、または確定状態の検証値です
				const auto found = std::find_if( durable.entries.begin(), durable.entries.end(),
					//------------------------------------------------------------------------------
					/**
					 * @brief 同じ元IDを検索します
					 * @param[in] e entry
					 * @return 対象
					 */
					//------------------------------------------------------------------------------
					[this]( const auto &e)
					{
						return e.id == port_->historyId ;
					}) ; /* 最大32 */
				// 未知opと最新request欠落もdereference前に拒否します
				if( found == durable.entries.end() || !found->result ||
				   (port_->terminal && (port_->terminal->result.request.operationId != port_->operationId ||
										port_->terminal->result.request.source.historyId != port_->historyId)) ||
				   (!port_->terminal && (!found->bounded.latestRequest ||
										 found->bounded.latestRequest->operationId != port_->operationId))) {
					throw HistoryError( HistoryErrorKind::CORRUPT) ;
				}
				// この同期境界で所有する値、または確定状態の検証値です
				auto completion = std::shared_ptr<WholeTextBoundedPersistenceCompletion>(
					new WholeTextBoundedPersistenceCompletion) ; // commit前に全ackを確保
				completion->source_ = found->result ;
				completion->request_ =
					port_->terminal ? port_->terminal->result.request : *found->bounded.latestRequest ;
				completion->record_ = found->bounded ;
				auto &record = completion->record_ ; // Storeと同じ参照除去候補
				record.inFlight.reset() ;
				record.latest.reset() ;
				record.latestText.reset() ;
				record.latestTerminal.reset() ;
				record.latestRequest.reset() ;
				record.latestState = WholeTextBoundedAttemptState::IDLE ;
				record.prefixSelectionEligible = false ;
				record.pcmObserved = false ;
				record.bindingObserved = false ;
				record.saveErrorPresent = false ;
				record.saveErrorKind = 7 ;
				record.saveErrorCode = 0 ;
				record.saveState = WholeTextBoundedSaveState::COMMITTED ;
				HistorySaveReceipt receipt ; /* 参照除去commit */
				// 未保存ownerを削除前にclose-onlyし、known identityでcleanupします
				if( port_->unsaved && port_->terminal && port_->terminal->journal) {
					port_->terminal->journal->file().retainDurable() ;
					port_->terminal->journal.reset() ;
				}
				event.cleanup = storage_->discardBounded( durable, {found->id, fingerprintReadingSource( *found->result)},
														 port_->operationId, receipt) ;
				event.controlReceipt = receipt ;
				event.kind = HistoryEventKind::BOUNDED_SAVED ;
				completion->receipt_ = receipt ; // 置換後はnoexceptのscalarとowner移動だけ
				event.bounded = std::move( completion) ;
				port_->unsaved = false ;
				port_->terminal.reset() ;
				port_->request.reset() ;
				port_->fallback.reset() ;
			} catch( const HistoryError &error) {
				event.kind = HistoryEventKind::SAVE_FAILED ;
				event.error = error.kind() ;
				event.code = error.code() ;
			} catch( ...) {
				event.kind = HistoryEventKind::SAVE_FAILED ;
				event.error = HistoryErrorKind::INTERNAL ;
			}
			{
				const std::lock_guard lock( port_->mutex) ; /* control解放 */
				port_->command = 0 ;
			}
			publish( std::move( event)) ;
			continue ;
		}
		std::shared_ptr<const HistorySnapshot> previousSnapshot ; // I/O前またはmain commit済みの正本DTO
		HistorySnapshot						   candidate ; // catchでもV7確定済み内容を保持するowner
		std::vector<std::uint64_t>			   removed ; // main commit後も削除済みIDを復活させません
		try {
			previousSnapshot = std::make_shared<const HistorySnapshot>( durable) ; // catch内で確保しません
			candidate = *ordinary.snapshot ; /* UI変更と正本をmerge */
			candidate.apiSidecarCorrupt = durable.apiSidecarCorrupt ; // 不正sidecarを普通保存から保護
			candidate.speakerTextCorrupt = durable.speakerTextCorrupt ;
			candidate.nextOperationId = std::max( candidate.nextOperationId, durable.nextOperationId) ;
			// 過去に確定削除したIDは古いsnapshotからも復活させません
			std::erase_if( candidate.entries,
						  //------------------------------------------------------------------------------
						  /**
						   * @brief 耐久削除済みの行が古いsnapshotから復活しないよう、除去対象を識別します
						   * @param[in] entry 保存候補snapshotの一行。erase_ifの同期呼出し中だけ読みます
						   * @return entry.idがdeleted_idsに含まれるならtrueで候補から除去され、含まれなければfalse
						   * @note 述語自体は削除IDの記録やfile I/Oを行いません
						   */
						  //------------------------------------------------------------------------------
						  [&deletedIds]( const auto &entry)
						  {
							  return std::find( deletedIds.begin(), deletedIds.end(), entry.id) != deletedIds.end() ;
						  }) ;
			// API生成中の原文削除/owner差替えを普通保存から拒否します
			if( port_->apiLease && std::none_of( candidate.entries.begin(), candidate.entries.end(),
				//------------------------------------------------------------------------------
				/**
				 * @brief active API sourceを保持するか照合します
				 * @param[in] e 記録
				 * @return 同owner
				 */
				//------------------------------------------------------------------------------
				[this]( const auto &e)
				{
					return e.id == port_->apiLease->key.historyId &&
						   detail::sameTranscriptOwner( e.result,
													   port_->apiLease->source) ;
				})) {
				throw HistoryError( HistoryErrorKind::BUSY) ;
			}
			// 音声API active行の削除を普通snapshotから行いません
			if( port_->audioLease && std::none_of( candidate.entries.begin(), candidate.entries.end(),
				//------------------------------------------------------------------------------
				/**
				 * @brief 同じ受付行を保持するか検証します
				 * @param[in] e 記録
				 * @return 一致
				 */
				//------------------------------------------------------------------------------
				[this]( const auto &e)
				{
					return e.id == port_->audioLease->historyId ;
				})) {
				throw HistoryError( HistoryErrorKind::BUSY) ;
			}
			// bounded recordはworker正本を優先し古いUI snapshotで消しません
			for( auto &entry : candidate.entries) {
				// この同期境界で所有する値、または確定状態の検証値です
				const auto old = std::find_if( durable.entries.begin(), durable.entries.end(),
					//------------------------------------------------------------------------------
					/**
					 * @brief 同じIDの正本を検索します
					 * @param[in] saved entry
					 * @return 同じID
					 */
					//------------------------------------------------------------------------------
					[&entry]( const auto &saved)
					{
						return saved.id == entry.id ;
					}) ; /* 最大32 */
				if( old != durable.entries.end()) {
					// UIの古い派生値を信用せず、現sourceのwriter正本だけを残す
					if( entry.result && old->result && entry.apiAudio && old->apiAudio &&
					   fingerprintReadingSource( *entry.result) == fingerprintReadingSource( *old->result) &&
					   speakerTextTarget( *entry.apiAudio) == speakerTextTarget( *old->apiAudio)) {
						entry.speakerText = old->speakerText ;
					} else {
						entry.speakerText = {} ;
					}
					// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
					if( old->bounded.inFlight && !detail::sameTranscriptOwner( old->result, entry.result)) {
						throw HistoryError( HistoryErrorKind::BUSY) ;
					}
					// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
					if( (old->bounded.effective || old->bounded.latest) && entry.result && old->result) {
						// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
						if( fingerprintReadingSource( *entry.result) != fingerprintReadingSource( *old->result) ||
						   entry.result->pcmIdentity != old->result->pcmIdentity) {
							throw HistoryError( HistoryErrorKind::CORRUPT) ;
						}
					}
					// 音声recordは専用portだけが変更し、遅いUI snapshotで巻き戻しません
					if( old->apiAudio) {
						const bool same = entry.apiAudio && entry.apiAudio->update == old->apiAudio->update &&
										  entry.apiAudio->operationId == old->apiAudio->operationId && entry.result &&
										  fingerprintReadingSource( *entry.result) == old->apiAudio->sourceSha &&
										  entry.result->pcmIdentity == old->result->pcmIdentity ;
						// 同じprefixの表示名変更だけは現在ownerとして保持します
						if( !same || port_->audioLease) {
							entry.result = old->result ;
						}
						entry.apiAudio = old->apiAudio ;
						entry.state = old->state ;
						entry.error = old->error ;
						entry.code = old->code ;
						entry.filter = old->filter ;
					}
					entry.bounded = old->bounded ;
					// 同じ原文ならAPIの新しい耐久状態を遅いsnapshotから保護します
					if( entry.result && old->result &&
					   fingerprintReadingSource( *entry.result) == fingerprintReadingSource( *old->result)) {
						entry.apiText = old->apiText ;
					} else {
						// 原文の明示変更後に旧sourceのAPI本文を移植しません
						entry.apiText = {} ;
					}
				} else {
					// 新しい履歴のAPI値は保存portのbarrierからだけ作成します
					entry.apiText = {} ;
					entry.speakerText = {} ;
				}
			}
			// active entryの普通削除はmetadataへ流しません
			for( const auto &old : durable.entries) {
				// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
				if( old.bounded.inFlight && std::none_of( candidate.entries.begin(), candidate.entries.end(),
					//------------------------------------------------------------------------------
					/**
					 * @brief 削除を検出します
					 * @param[in] e entry
					 * @return 同ID
					 */
					//------------------------------------------------------------------------------
					[&old]( const auto &e)
					{
						return e.id == old.id ;
					})) {
					throw HistoryError( HistoryErrorKind::BUSY) ;
				}
			}
			removed = deletedIds ; // 置換前に削除IDの記憶領域を確保
			// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
			for( const auto &entry : durable.entries) {
				// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
				if( std::none_of( candidate.entries.begin(), candidate.entries.end(),
								//------------------------------------------------------------------------------
								/**
								 * @brief 耐久履歴の行が今回の保存候補にも同じIDで残っているか調べます
								 * @param[in] next 保存候補snapshotの一行の読取り専用借用。比較中だけ有効です
								 * @return next.idが外側の耐久行entry.idと等しければtrue、それ以外はfalse
								 * @note none_ofで一致なしとなった行だけを外側で削除済みIDへ記録します
								 */
								//------------------------------------------------------------------------------
								[&entry]( const auto &next)
								{
									return next.id == entry.id ;
								})) {
					removed.push_back( entry.id) ;
				}
			}
			HistoryEvent saved{HistoryEventKind::SAVED, ordinary.revision} ; // commit前に全確保
			saved.savedOriginals.reserve( candidate.entries.size()) ;
			// 元snapshotの同じownerと全field keyを保存成功ackへ束縛します
			for( const auto &entry : candidate.entries) {
				// 非空原文だけが新文章要求の対象です
				if( entry.result && !entry.result->lines.empty()) {
					saved.savedOriginals.push_back( {{entry.id, fingerprintReadingSource( *entry.result)}, entry.result}) ;
				}
			}
			candidate.apiSidecarSyncFailed = false ; // 後半再保存が成功すれば未完を解除
			storage_->save( candidate) ;
			deletedIds = std::move( removed) ; // commit後noexcept
			durable = std::move( candidate) ;
			publish( std::move( saved)) ;
		} catch( const HistoryError &error) {
			// mainの原文削除/変更が確定済みならmemory正本も必ず新しいsourceへ進めます
			if( error.mainCommitted()) {
				candidate.apiSidecarSyncFailed = true ;
				deletedIds = std::move( removed) ;
				durable = std::move( candidate) ;
				previousSnapshot = std::make_shared<const HistorySnapshot>( durable) ;
			}
			publish( {HistoryEventKind::SAVE_FAILED, ordinary.revision, previousSnapshot, error.kind(), error.code()}) ;
		} catch( ...) {
			publish( {HistoryEventKind::SAVE_FAILED, ordinary.revision, {}, HistoryErrorKind::INTERNAL}) ;
		}
	}
	std::optional<WholeTextBoundedTerminal> releasedTerminal ; // 終了時のI/O owner
	{
		const std::lock_guard lock( port_->mutex) ; /* portの新規操作だけ停止 */
		port_->ended = true ;
		port_->ready = false ;
		releasedTerminal = std::move( port_->terminal) ;
		port_->terminal.reset() ;
	}
	releasedTerminal.reset() ; // handle解放は排他の外です
	storage_.reset() ;
	port_->ack.notify_all() ;
	stopped_.store( true, std::memory_order_release) ;
}
} // namespace koefude
