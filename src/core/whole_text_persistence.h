//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_persistence.h
 * @brief 有界全文の保存証拠と型付き履歴portを定義します
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1343の文章専用補正を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1409の役割・引数・戻り値と所有寿命を具体化
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/whole_text_bounded_result.h"
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace koefude {
struct WholeTextCorrectionJobRequest ; // 同期呼出しだけ借用する要求
struct WholeTextBoundedTerminal ; // 一つのJをmoveする終端
class WholeTextBoundedStorageBudget ; // 生成前の単独予約
class WholeTextPersistenceStore ; // 保存検証とprivate factoryの所有者
class HistoryController ; // 型付きackの所有者
/** @brief 保存対象の最新試行です */
enum class WholeTextBoundedAttemptState : std::uint32_t {
	/// 未試行
	IDLE,
	/// 耐久予約済み
	PENDING,
	/// 実行中
	RUNNING,
	/// 全量と保存が確定
	COMPLETE,
	/// 確定prefixだけ保持
	FAILURE,
	/// 取消し
	CANCELLED,
	/// 前回の未確定操作
	INTERRUPTED
} ;
/** @brief 生成終端から独立した保存状態です */
enum class WholeTextBoundedSaveState : std::uint32_t {
	/// 保存要求なし
	NONE,
	/// 受領待ち
	WAITING,
	/// worker保存中
	PERSISTING,
	/// metadata確定
	COMMITTED,
	/// 同じJの明示再保存待ち
	SAVE_FAILED
} ;
/** @brief Automaticが選択する保存済み全文の種類です */
enum class WholeTextActiveFullKind : std::uint32_t {
	/// 全文なし
	NONE,
	/// 既存全文
	V1,
	/// 有界全文
	BOUNDED
} ;
using WholeTextJournalKey = std::array<std::uint8_t, 16> ; // OS乱数のfile識別子
/** @brief 本文・handle・pathを含まない最大2048byteの保存証拠です */
struct WholeTextBoundedDescriptor {
	/// 一fileのキー
	WholeTextJournalKey			 journalKey{} ;
	/// volume/file index
	std::array<std::uint64_t, 2> fileIdentity{} ;
	/// 不変extent
	std::uint64_t				 reservedFileBytes = 25231360 ;
	/// 不変header
	WholeTextDigest				 headerSha256{} ;
	/// 保存分類
	WholeTextBoundedAttemptState state = WholeTextBoundedAttemptState::FAILURE ;
	/// 原要求・PCM・cursor・closeの元値
	WholeTextBoundedResult		 result ;
} ;
/** @brief worker検証済み本文だけを共有し、JをUIへ渡しません */
class WholeTextBoundedText final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 保存workerが検証したUTF-8本文を、内容を複製せず借用します
	 * @return body_が所有する不変本文への非所有view。本文0なら空viewです
	 *         このWholeTextBoundedTextの共有ownerを使用終了まで保持してください。破棄後は無効です
	 * @note 完全成功か部分prefixかはcompleteで別に確認します。view取得で生成やI/Oは行いません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::string_view text() const noexcept
	{
		return *body_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 本文に結び付いた保存証拠を読取り専用で借用します
	 * @return このinstance内のdescriptorへの非所有const参照。空/NULLではありません
	 *         このWholeTextBoundedTextの共有ownerを使用終了まで保持し、破棄後は参照しないでください
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const WholeTextBoundedDescriptor &descriptor() const noexcept
	{
		return descriptor_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 本文が耐久保存された完全な有界補正として利用できる状態か照会します
	 * @return durable_がtrueかつdescriptorのstateがCompleteならtrue、それ以外はfalse
	 *         falseでも検証済み部分prefixを閲覧できる場合があります
	 * @note 保存済み状態の照会だけで、ここから検証・生成・保存を開始しません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool complete() const noexcept
	{
		return durable_ && descriptor_.state == WholeTextBoundedAttemptState::COMPLETE ;
	}

	private:
	friend class WholeTextPersistenceStore ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 保存workerが検証済みの本文を一回moveします
	 * @param[in] descriptor 検証済み証拠
	 * @param[in] body 一つの本文
	 * @param[in] durable metadata確定予定。公開は確定後だけです
	 */
	//------------------------------------------------------------------------------
	WholeTextBoundedText( WholeTextBoundedDescriptor descriptor, std::shared_ptr<const std::string> body, bool durable)
		: descriptor_( std::move( descriptor)), body_( std::move( body)), durable_( durable)
	{
	}
	/// 全証拠
	WholeTextBoundedDescriptor		   descriptor_ ;
	/// Jごとに唯一のbody allocation
	std::shared_ptr<const std::string> body_ ;
	/// 未保存prefixを全文へ昇格しません
	bool							   durable_ = false ;
} ;
/** @brief 前の終端と独立した耐久予約です */
struct WholeTextBoundedInFlight {
	/// Pending/Running
	WholeTextBoundedAttemptState state = WholeTextBoundedAttemptState::PENDING ;
	/// 受理した全要求
	WholeTextCorrectionRequest	 request ;
	/// owner/checkpointと共通
	WholeTextJournalKey			 reservationKey{} ;
} ;
/** @brief 有効全文、最新終端、実行中予約を独立して保持します */
struct WholeTextBoundedRecord {
	/// 有効全文の選択
	WholeTextActiveFullKind						active = WholeTextActiveFullKind::NONE ;
	/// 保存済みComplete
	std::optional<WholeTextBoundedDescriptor>	effective ;
	/// 唯一bodyの共有owner
	std::shared_ptr<const WholeTextBoundedText> effectiveText ;
	/// 最新終端
	WholeTextBoundedAttemptState				latestState = WholeTextBoundedAttemptState::IDLE ;
	/// Jなし失敗も保持
	std::optional<WholeTextCorrectionRequest>	latestRequest ;
	/// 未観測値はpresenceで区別
	std::optional<WholeTextBoundedResult>		latestTerminal ;
	/// PCM値の保存presence。文章専用では元ASRの由来metadataだけ
	bool										pcmObserved = false ;
	/// 封印済み
	bool										bindingObserved = false ;
	/// 読めるprefixの証拠
	std::optional<WholeTextBoundedDescriptor>	latest ;
	/// Completeではeffectiveとalias
	std::shared_ptr<const WholeTextBoundedText> latestText ;
	/// 自動再開禁止
	std::optional<WholeTextBoundedInFlight>		inFlight ;
	/// 保存失敗を生成失敗と区別
	bool										saveErrorPresent = false ;
	/// HistoryErrorKind::Internal
	std::uint32_t								saveErrorKind = 7 ;
	/// 元OS code
	std::uint32_t								saveErrorCode = 0 ;
	/// cursor正かつ本文検証済み
	bool										prefixSelectionEligible = false ;
	/// live保存状態
	WholeTextBoundedSaveState					saveState = WholeTextBoundedSaveState::NONE ;
} ;
/** @brief atomic置換直後にallocationなしで固定するreceiptです */
struct HistorySaveReceipt {
	/// このbitを置換直後に確定
	bool			committed = false ;
	/// UI revisionとは独立
	std::uint64_t	storageRevision = 0 ;
	/// 全metadata bodyのSHA
	WholeTextDigest historySha256{} ;
} ;
/** @brief 明示削除後のmetadataとcleanupを独立して返します */
struct WholeTextBoundedCleanupResult {
	/// 参照除去済み
	bool		  metadataCommitted = false ;
	/// 全既知fileの削除確認
	bool		  cleanupComplete = false ;
	/// HistoryErrorKind
	std::uint32_t errorKind = 7 ;
	/// 元失敗
	std::uint32_t code = 0 ;
	/// 未解放charge
	std::uint64_t residualBytes = 0 ;
} ;
/** @brief typed履歴workerだけが作成できる終端ackです */
class WholeTextBoundedPersistenceCompletion final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 保存対象と同じ原文の共有ownerを読取り専用で借用します
	 * @return このcompletion内のsource_への非所有const参照。参照自体は空/NULLではありません
	 *         completionの共有ownerを使用終了まで保持し、破棄後は参照しないでください
	 * @note 参照そのものは非所有ですが、shared_ptrをcopyすれば原文の寿命を独立に保持できます
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const std::shared_ptr<const Transcript> &source() const noexcept
	{
		return source_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief ACKに対応する履歴ID・生成操作IDを含む要求を読取り専用で借用します
	 * @return このcompletion内のrequest_への非所有const参照。参照自体は空/NULLではありません
	 *         completionの共有ownerを使用終了まで保持し、破棄後は参照しないでください
	 * @note この参照から新しい処理を開始せず、対象操作との照合に使います
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const WholeTextCorrectionRequest &request() const noexcept
	{
		return request_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 有効全文・最新終端・保存失敗を分けた保存状態recordを読取り専用で借用します
	 * @return このcompletion内のrecord_への非所有const参照。参照自体は空/NULLではありません
	 *         completionの共有ownerを使用終了まで保持し、破棄後は参照しないでください
	 * @note 部分失敗や前採用を含み得るため、record取得だけを今回の完全成功と扱いません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const WholeTextBoundedRecord &record() const noexcept
	{
		return record_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief metadata置換直後に固定した耐久commit receiptを読取り専用で借用します
	 * @return このcompletion内のreceipt_への非所有const参照。参照自体は空/NULLではありません
	 *         completionの共有ownerを使用終了まで保持し、破棄後は参照しないでください
	 * @note committedはmetadataの置換成否であり、生成の完全成功はcompleteで別に確認します
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const HistorySaveReceipt &receipt() const noexcept
	{
		return receipt_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief このACKを今回の完全成功として適用できるか照会します
	 * @return receipt.committedがtrueかつrecord.latest_stateがCompleteの場合だけtrue
	 *         保存失敗・部分失敗・取消し等はfalseで、前採用の有効全文が存在しても今回成功にはしません
	 * @note 保存済みACKの状態照会であり、保存・再試行・生成を開始しません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool complete() const noexcept
	{
		return receipt_.committed && record_.latestState == WholeTextBoundedAttemptState::COMPLETE ;
	}

	private:
	friend class WholeTextPersistenceStore ;
	friend class HistoryController ;
	//------------------------------------------------------------------------------
	/**
	 * @brief portが予約前に空ackを確保します
	 */
	//------------------------------------------------------------------------------
	WholeTextBoundedPersistenceCompletion() = default ;
	/// pointerとcontrol blockも一致
	std::shared_ptr<const Transcript> source_ ;
	/// 受付ID
	WholeTextCorrectionRequest		  request_ ;
	/// body共有だけ
	WholeTextBoundedRecord			  record_ ;
	/// 置換後noexcept反映
	HistorySaveReceipt				  receipt_ ;
} ;
/** @brief 生存期間を共有する単一予約portです。UI threadで待機しません */
class IWholeTextPersistencePort {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 共有port資源を解放します
	 */
	//------------------------------------------------------------------------------
	virtual ~IWholeTextPersistencePort() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 全I/O前に耐久予約を取得します
	 * @param[in] request 受付済み要求
	 * @param[in] stop 協調停止
	 * @return move-onlyな一回予約
	 * @throws OperationError 拒否時は生成禁止
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual WholeTextBoundedStorageBudget reserve( const WholeTextCorrectionJobRequest &request,
																std::stop_token						 stop) = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 予約済みslotへ一terminalを移し確定ackを待ちます
	 * @param[in] terminal 同じ要求/Jの所有終端
	 * @param[in] stop precommit停止
	 * @return 保存またはSaveFailedの所有ack。受領後stopでもownerを失いません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual std::shared_ptr<const WholeTextBoundedPersistenceCompletion>
	receive( WholeTextBoundedTerminal terminal, std::stop_token stop) = 0 ;
} ;
/** @brief UIの選択と操作可能性だけの小さい値です */
struct WholeTextBoundedActions {
	/// 保存済み有効全文
	bool effective = false ;
	/// 明示prefix選択
	bool prefix = false ;
	/// 同じJ保存再試行
	bool retrySave = false ;
	/// 未完試行削除
	bool discard = false ;
	/// 新IDで生成
	bool recreate = false ;
	/// 補正文TXT/copy
	bool exportComplete = false ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief scalarとbody所有状態から操作を解決します
 * @param[in] record 不変record
 * @return 実装済み操作の可否
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextBoundedActions boundedActions( const WholeTextBoundedRecord &record) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 元ownerを保ち表示意図だけ解決します
 * @param[in] preference 保存した意図
 * @param[in] record 有界record
 * @param[in] v1 既存有効全文の有無
 * @param[in] reading 有効Readingの有無
 * @return 利用可能な表示。意図自体は書き換えません
 */
//------------------------------------------------------------------------------
[[nodiscard]] ResultPreference resolveBoundedDisplay( ResultPreference preference, const WholeTextBoundedRecord &record,
													 bool v1, bool reading) noexcept ;
namespace detail {
//------------------------------------------------------------------------------
/**
 * @brief descriptorを固定LE形式へ変換します
 * @param[in] value 証拠
 * @return 最大2048byte
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<std::uint8_t>	 encodeBoundedDescriptor( const WholeTextBoundedDescriptor &value) ;
//------------------------------------------------------------------------------
/**
 * @brief descriptorを厳密に復元します
 * @param[in] bytes 全encoding
 * @return 未開封Jの証拠
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextBoundedDescriptor decodeBoundedDescriptor( std::span<const std::uint8_t> bytes) ;
//------------------------------------------------------------------------------
/**
 * @brief V7 entry拡張を変換します
 * @param[in] id 元ID
 * @param[in] value record
 * @return 最大8192byte
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<std::uint8_t>	 encodeBoundedRecord( std::uint64_t id, const WholeTextBoundedRecord &value) ;
//------------------------------------------------------------------------------
/**
 * @brief V7 entry拡張を全消費します
 * @param[in] bytes 未読byte
 * @param[in] id 期待ID
 * @param[out] consumed 消費数
 * @return 検証済みrecord。bodyはplatformで復元します
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextBoundedRecord	 decodeBoundedRecord( std::span<const std::uint8_t> bytes, std::uint64_t id,
															 std::size_t &consumed) ;
//------------------------------------------------------------------------------
/**
 * @brief 元pointerと制御blockを照合します
 * @param[in] a owner
 * @param[in] b owner
 * @return 同一ならtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool						 sameTranscriptOwner( const std::shared_ptr<const Transcript> &a,
															 const std::shared_ptr<const Transcript> &b) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief bindingを既存domain/幅で計算します
 * @param[in] request 全要求
 * @param[in] auxiliary aux SHA
 * @return open binding
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextDigest			 boundedOpenBinding( const WholeTextCorrectionRequest &request,
															const WholeTextDigest			 &auxiliary) ;
//------------------------------------------------------------------------------
/**
 * @brief planとopenを既存domainで束縛します
 * @param[in] open open値
 * @param[in] plan plan SHA
 * @return input SHA
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextDigest			 boundedInputBinding( const WholeTextDigest &open, const WholeTextDigest &plan) ;
//------------------------------------------------------------------------------
/**
 * @brief descriptorの範囲/状態/closeを検証します
 * @param[in] value 全値
 */
//------------------------------------------------------------------------------
void									 validateBoundedDescriptor( const WholeTextBoundedDescriptor &value) ;
} // namespace detail
} // namespace koefude
