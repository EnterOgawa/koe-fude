//------------------------------------------------------------------------------

/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file job_controller.h
 * @brief 有限ジョブキューと所有結果の非同期受渡しを定義します
 * @version 1.0.0
 *
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 UIを止めない直列workerを追加
 * 0.1.0 Shunji Ogawa 2026/09/14 待機中の既定eventを通知せず実進捗まで受付状態を保持
 * 0.1.0 Shunji Ogawa 2026/10/04 UIへ渡す検証済み整形成功の所有型を追加
 * 0.1.0 Shunji Ogawa 2026/10/04 型付き整形/export要求と所有応答を同じqueueへ追加
 * 0.1.0 Shunji Ogawa 2026/10/19 自由全文の型付き要求と検証済み所有応答を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 元pathと専用全文TXTを型付き有限queueへ追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1287の明示診断と隔離接続を追加
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1344の明示API補正と独立保存を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1345の動的モデル・凍結単価と旧履歴互換を接続
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1370の文脈分割と明示途中再開を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1397の任意通常ログと確定終端を接続
 *
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1409の役割・引数・戻り値と所有寿命を具体化
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実使用の標準headerを直接include
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415のoperator括弧と文書lambdaの書式を補修
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */

//------------------------------------------------------------------------------

#pragma once

#include <optional>
#include <atomic>

#include <condition_variable>

#include <cstddef>

#include <cstdint>

#include <deque>

#include <filesystem>

#include <functional>

#include <map>

#include <memory>

#include <mutex>

#include <stop_token>

#include <thread>

#include <utility>

#include <variant>

#include <vector>

#include "core/api_audio_transcription.h"

#include "core/api_text_correction.h"

#include "core/correction_diagnostics.h"

#include "core/engines.h"

#include "core/pipeline.h"

#include "core/reading_export.h"

#include "core/reading_session.h"

#include "core/speaker_text_record.h"

#include "core/transcript_export.h"

#include "core/types.h"

#include "core/whole_text_correction.h"

#include "core/whole_text_export.h"

#include "core/whole_text_persistence.h"

namespace koefude {

class LocalDiagnosticLog ; // 旧試験と独立した、全workerより長寿命の任意sink

constexpr std::size_t K_MAXIMUM_OUTSTANDING_JOBS = 8 ; // 終端結果の未受領分も含む受付上限

/** @brief 音声認識だけの受付snapshotです */

struct AsrJobRequest {

	/// 元音声path
	std::filesystem::path			   input ;

	/// 推論設定の所有copy
	EngineOptions					   options ;

	/// 明示API受付行の永続ID。localは0も許可
	std::uint64_t					   historyId = 0 ;

	/// submitが設定する単調番号
	std::uint64_t					   operationId = 0 ;

	/// 受付時の共有キーrevision、秘密なし
	std::uint64_t					   apiKeyRevision = 0 ;

	/// 明示行menuだけの小token
	std::optional<ApiAudioResumeToken> audioResume ;

	/// 明示API構成Apply後の新規投入だけtrue
	bool							   audioApiSelected = false ;
} ;

/** @brief 同じ履歴に対する一回の整形要求です */

struct ReadingJobRequest {

	/// workerまで共有所有する不変原文
	std::shared_ptr<const Transcript> source ;

	/// operation_idはsubmit前0、controllerが受付IDを設定
	ReadingRequest					  request ;
} ;

/** @brief 原文全体に対する一回の自由全文要求です */

struct WholeTextCorrectionJobRequest {

	/// 同期workerまで共有所有する不変原文
	std::shared_ptr<const Transcript>			 source ;

	/// submit前operation_idは0、受付時に固定
	WholeTextCorrectionRequest					 request ;

	/// 同じ原文を認識した元path
	std::filesystem::path						 input ;

	/// 空ならworkerが標準二rootを解決
	std::filesystem::path						 modelRoot ;

	/// 明示診断時だけの受付前確保
	std::shared_ptr<CorrectionDiagnosticCapture> diagnostics ;
} ;

/** @brief payloadの型で処理を区別する有限queueの所有要求です */

struct JobRequest {

	/// 単一の所有要求
	std::variant<AsrJobRequest, ExportJobRequest, ReadingJobRequest, WholeTextCorrectionJobRequest,

				 WholeTextExportJobRequest, ApiTextCorrectionJobRequest, ApiCatalogJobRequest,

				 SpeakerTextJobRequest>

								 payload ;

	//------------------------------------------------------------------------------
	/**
	 * @brief payload内のASR要求を編集するため、その要素を借用します
	 * @return payloadが所有するAsrJobRequestへの非所有参照。呼出し元は解放しません
	 *         このJobRequestの破棄、移動、payloadの差替えまでだけ利用してください
	 * @pre payloadはAsrJobRequestで、借用中に並行変更しないこと
	 * @throws std::bad_variant_access payloadが他の要求型の場合
	 */
	//------------------------------------------------------------------------------

	[[nodiscard]] AsrJobRequest &asr()

	{

		return std::get<AsrJobRequest>( payload) ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief payload内のASR要求を読取り専用で借用します
	 * @return payloadが所有するAsrJobRequestへの非所有const参照。空やNULLは返しません
	 *         このJobRequestの破棄、移動、payloadの差替えまでだけ利用してください
	 * @pre payloadはAsrJobRequestで、借用中に並行変更しないこと
	 * @throws std::bad_variant_access payloadが他の要求型の場合
	 */
	//------------------------------------------------------------------------------

	[[nodiscard]] const AsrJobRequest &asr() const

	{

		return std::get<AsrJobRequest>( payload) ;
	}
} ;

/** @brief workerが返す原文と任意の整形応答を共有所有します */

struct JobOutput {

	/// 独立投影の耐久採用ACK
	std::shared_ptr<const SpeakerTextJobCompletion>	   speakerText ;

	/// 本文を持たないmetadata終端
	std::shared_ptr<const ApiCatalogJobCompletion>	   apiCatalog ;

	/// 音声APIの耐久prefix/取消し終端
	std::shared_ptr<const ApiAudioJobCompletion>	   apiAudio ;

	/// 専用保存ackと失敗時のknown usage
	std::shared_ptr<const ApiTextJobCompletion>		   apiText ;

	/// 原文、保存時は入力snapshot
	std::shared_ptr<const Transcript>				   result ;

	/// 整形時だけの全行応答
	std::shared_ptr<const ReadingResponse>			   reading ;

	/// 自由全文時だけの所有応答
	std::shared_ptr<const WholeTextCorrectionResponse> wholeText ;

	/// V1と別型の保存ack
	std::shared_ptr<const WholeTextBoundedPersistenceCompletion>

		boundedPersistence ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ原文と型付き保存ackを保持します
	 * @param[in] source 原文
	 * @param[in] completion 保存ack
	 */
	//------------------------------------------------------------------------------

	JobOutput( std::shared_ptr<const Transcript>							   source,

			  std::shared_ptr<const WholeTextBoundedPersistenceCompletion> completion)

		: result( std::move( source)), boundedPersistence( std::move( completion))

	{
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 空の応答を構築します
	 */
	//------------------------------------------------------------------------------

	JobOutput() = default ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 既存ASR runnerの戻り値を所有応答へ移します
	 * @param[in] source 原文の共有所有値。null可
	 */
	//------------------------------------------------------------------------------

	JobOutput( std::shared_ptr<const Transcript> source) : result( std::move( source))
	{
	}
} ;

//------------------------------------------------------------------------------
/**
 * @brief worker上の同期生成器を型付き要求へ接続します
 * @param[in] request controllerが受付IDを設定済みの整形要求
 * @param[in,out] formatter この呼出しをworkerだけで使用する生成器
 * @param[in] stop 協調取消し
 * @param[in] progress 実処理の通知先
 * @return 原文と全行応答を共有所有する結果。全体検証はcontrollerが行います
 */
//------------------------------------------------------------------------------

[[nodiscard]] JobOutput runReadingJob( const ReadingJobRequest &request,

									  ITranscriptFormatter &formatter, std::stop_token stop,

									  const ProgressCallback &progress) ;

//------------------------------------------------------------------------------
/**
 * @brief workerの同期補正器を自由全文要求へ接続します
 * @param[in] request 受付ID設定済みの不変要求
 * @param[in,out] corrector workerだけで同期利用する補正器
 * @param[in] stop 協調取消し
 * @param[in] progress 実処理通知
 * @return 同じ原文ownerと未検証全文応答。構造検証はcontrollerで行います
 * @throws OperationError 入口不正または取消し。生成器の失敗はそのまま伝えます
 */
//------------------------------------------------------------------------------

[[nodiscard]] JobOutput runWholeTextCorrectionJob( const WholeTextCorrectionJobRequest &request,

												  IWholeTextCorrector				  &corrector,

												  std::stop_token					   stop,

												  const ProgressCallback			  &progress) ;

/** @brief UIへ渡す最新状態です。終端結果は受領まで失いません */

struct JobEvent {

	/// 0を使用しない単調増加ID
	std::uint64_t id = 0 ;

	/// 最新の確定段階
	Progress	  progress ;

	/// trueならこのIDへの通知は最後
	bool		  terminal = false ;

	/// 耐久確定は取消し後も保持
	std::shared_ptr<const WholeTextBoundedPersistenceCompletion>

													   boundedPersistence ;

	/// 終端で固定、通常はnull
	std::optional<CorrectionDiagnosticSnapshot>		   correctionDiagnostics ;

	/// trueなら正常完了
	bool											   succeeded = false ;

	/// 異常終端の分類
	ErrorKind										   error = ErrorKind::INTERNAL ;

	/// 元APIの診断値
	std::uint32_t									   code = 0 ;

	/// 完了結果の共有不変所有権
	std::shared_ptr<const Transcript>				   result ;

	/// 整形終端だけの共有所有応答
	std::shared_ptr<const ReadingResponse>			   reading ;

	/// worker検証済み成功
	std::shared_ptr<const ValidatedReadingCompletion>  readingCompletion ;

	/// 自由全文の共有所有応答
	std::shared_ptr<const WholeTextCorrectionResponse> wholeText ;

	/// 自由全文のworker検証済み成功
	std::shared_ptr<const ValidatedWholeTextCorrectionCompletion>

													wholeTextCompletion ;

	/// 本文を持たないmetadata終端
	std::shared_ptr<const ApiCatalogJobCompletion>	apiCatalog ;

	/// 音声APIの耐久prefix/取消し終端
	std::shared_ptr<const ApiAudioJobCompletion>	apiAudio ;

	/// 取消し後も保存済み終端を保持
	std::shared_ptr<const ApiTextJobCompletion>		apiText ;

	/// 音声API成功とは独立した終端
	std::shared_ptr<const SpeakerTextJobCompletion> speakerText ;
} ;

using JobRunner = std::function<JobOutput( const JobRequest &, std::stop_token,

										  const ProgressCallback &)> ; // fakeへ差替え可能な同期処理

using JobNotifier = std::function<void()> ; // ポインタを運ばないUI起床通知

/** @brief 一つのworkerが有界な要求を順番に実行します */

class JobController final {

	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief workerを開始します
	 * @param[in] runner 所有する同期処理。mutexの外で呼び出すことを保証
	 * @param[in] notifier 所有する起床通知。worker終了まで通知先が有効であること
	 * @param[in] diagnostics 通常sinkのborrow、nullは従来どおりI/O0
	 */
	//------------------------------------------------------------------------------

	JobController( JobRunner runner, JobNotifier notifier, LocalDiagnosticLog *diagnostics = nullptr) ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 停止要求後にjoinします。UIではstopped確認後に破棄してください
	 */
	//------------------------------------------------------------------------------

	~JobController() ;

	//------------------------------------------------------------------------------
	/**
	 * @brief worker・受付slot・取消し状態の対応を複製しないためcopy構築を禁止します
	 * @note 名前のない第1引数（const JobController &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	JobController( const JobController &) = delete ;

	//------------------------------------------------------------------------------
	/**
	 * @brief worker・受付slot・取消し状態の対応を複製しないためcopy代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const JobController &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	JobController					   &operator=( const JobController &) = delete ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 復元済み履歴の番号を再利用しないよう、次回受付IDの下限を設定します
	 * @param[in] floor 次のsubmitに使う非0の操作ID。現在のnext_id以上であること
	 * @return 待機queueと未受領slotが空で、停止中でなく設定できた場合はtrue
	 *         floorが0・後退、既に番号枯渇、未受領/待機要求あり、停止中ならfalseで変更しません
	 * @throws std::system_error 受付mutexのlockに失敗した場合
	 * @note ジョブの開始や通知は行わず、同じ受付mutexでsubmitと直列化します
	 */
	//------------------------------------------------------------------------------

	[[nodiscard]] bool					seedNextId( std::uint64_t floor) ;

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
	 */
	//------------------------------------------------------------------------------

	[[nodiscard]] std::uint64_t			submit( JobRequest request) ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 指定した未完了要求のstop状態へ協調取消しを通知します
	 * @param[in] id submitで受け付けた操作ID。未知・受領済み・既に終端のIDなら無操作です
	 * @throws std::system_error 受付mutexのlockに失敗した場合
	 * @note mutex外でstd::stop_source::request_stopを呼び、同期stop callbackの実行を許します
	 *       復帰は推論中断や終端保存の完了を意味しません。終端JobEventで結果を確認します
	 */
	//------------------------------------------------------------------------------

	void								cancel( std::uint64_t id) ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 新規受付を止め、未受領枠にある全要求へ協調取消しを通知します
	 * @throws std::system_error 受付mutexのlockに失敗した場合
	 * @note 取消し通知はmutex外で行い、同期stop callbackが走る場合があります。joinは行いません
	 *       終端結果はtakeEventsで回収でき、worker終了はstoppedで別に確認します
	 */
	//------------------------------------------------------------------------------

	void								requestStop() ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 通知callbackを含むworkerの終了が確定したか照会します
	 * @return worker末尾のreleaseに対応するacquire読取りがtrueなら終了済み、falseなら終了未確認
	 *         停止要求を出しただけではtrueになりません
	 * @pre このcontrollerの寿命を呼出し完了まで保持し、破棄と並行しないこと
	 * @note 終端eventの受領済みや全ジョブ成功を示す値ではありません
	 */
	//------------------------------------------------------------------------------

	[[nodiscard]] bool					stopped() const noexcept ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 未受領の更新だけをID昇順に回収し、返す終端要求の受付枠を解放します
	 * @return 最大8件の所有event列。各IDの進捗は最新値一件へ集約し、結果DTOは共有所有します
	 *         空は新しい通知がないことを表し、worker終了や全ジョブ成功を意味しません
	 * @throws std::bad_alloc 返却vectorの事前reserveに失敗した場合。未受領状態と受付枠は変更しません
	 * @throws std::system_error 受付mutexのlockに失敗した場合。未受領状態と受付枠は変更しません
	 * @post 返した更新のdirtyを解除し、終端を返したIDだけslotを削除します
	 * @note 現在のJobEventはscalar/固定配列/共有ownerから成り、reserve後のcopyで本文を確保しません
	 */
	//------------------------------------------------------------------------------

	[[nodiscard]] std::vector<JobEvent> takeEvents() ;

	private:
	/** @brief 一要求の取消し状態と未受領eventを所有します */

	struct Slot {

		/// activeとqueuedのどちらも取消し可能
		std::stop_source cancellation ;

		/// 終端まで同じIDに対応する格納領域
		JobEvent		 event ;

		/// 全カードを通じAPI要求は最大一つ
		bool			 apiText = false ;

		/// 実進捗または終端をpublishした後だけ未受領の更新が存在する
		bool			 dirty = false ;
		/// 実worker開始、未開始は0
		std::uint64_t	 startedMs = 0 ;

		// この同期境界で所有する値、または確定状態の検証値です

		/// 同一履歴の行整形/全文の交差重複拒否。ASR/exportは0
		std::uint64_t	 readingHistoryId =

			0 ;
	} ;

	//------------------------------------------------------------------------------
	/**
	 * @brief worker境界内で要求と例外を処理します
	 */
	//------------------------------------------------------------------------------

	void											 run() noexcept ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 最新eventをmutex内で差し替え、外側でUIを起こします
	 * @param[in] event 同じIDの最新値。所有権を移す
	 */
	//------------------------------------------------------------------------------

	void											 publish( JobEvent event) noexcept ;

	/// workerのみが呼び出す処理
	JobRunner										 runner_ ;

	/// worker終了まで有効な起床先
	JobNotifier										 notifier_ ;
	/// wWinMainが全worker停止まで保持するborrow
	LocalDiagnosticLog								*diagnostics_ ;

	/// 要求・event・停止受付状態だけの保護
	mutable std::mutex								 mutex_ ;

	/// 空キューと停止要求の待機
	std::condition_variable							 changed_ ;

	/// 最大8件の未開始要求
	std::deque<std::pair<std::uint64_t, JobRequest>> queue_ ;

	/// 終端未受領も含む最大8件の状態
	std::map<std::uint64_t, Slot>					 slots_ ;

	/// wrap後は0として受付停止
	std::uint64_t									 nextId_ = 1 ;

	/// trueなら新規受付禁止
	bool											 stopping_ = false ;

	/// notifier終了後にreleaseする完了状態
	std::atomic<bool>								 stopped_ = false ;

	/// 通知例外を記録しpollingで回収
	std::atomic<bool>								 notificationFailed_ = false ;

	/// 他member初期化後に開始、破棄前にjoin
	std::jthread									 worker_ ;
} ;

} // namespace koefude
