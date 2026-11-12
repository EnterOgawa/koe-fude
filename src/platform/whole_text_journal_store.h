//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_journal_store.h
 * @brief 未確定prefixの有界journalと単独所有terminalを定義します
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1285の有界逐次補正を接続
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実契約・所有寿命・戻り値の意味を具体化
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415のoperator括弧と文書lambdaの書式を補修
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/whole_text_bounded_result.h"
#include "inference/whole_text_resource_guard.h"
#include "platform/temporary_store.h"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <utility>
namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief planと固定192B成功slotが同じ8MiBに収まるか検査します
 * @param[in] encodedBytes 旧preimage byte
 * @param[in] records 全件数
 * @param[in] capacity 小さい試験だけ縮めるsection上限
 * @return checked適合
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool
							   wholeTextJournalPlanFits( std::uint64_t encodedBytes, std::uint64_t records,
														std::uint64_t capacity = 8388608) noexcept ; // この同期処理で使う所有値または検証値
inline constexpr std::uint64_t K_WHOLE_TEXT_JOURNAL_BYTES = 25231360 ; // 固定予約
/** @brief U4から実値または確定上界で渡す全使用量です */
struct WholeTextBoundedStorageUsage {
	/// 最大一つの旧/未完J
	std::uint64_t				 previousJournalBytes = 0 ;
	/// 0/1だけ
	std::uint32_t				 previousJournals = 0 ;
	/// 全旧metadata/temp/residual
	std::uint64_t				 operationOtherBytes = 0 ;
	/// 全新旧metadata/temp上界
	std::optional<std::uint64_t> metadataUpperBytes ;
	/// 原文/legacy/前の有効本文
	std::uint64_t				 retainedTextBytes = 0 ;
	/// 同じsourceの補正は0、新規entryだけ1
	std::uint32_t				 historyEntryDelta = 0 ;
} ;
struct WholeTextJournalBinding ; // seal時に全値を渡します
/** @brief 予約済みowner/checkpointへJ確定を直列通知します */
class IWholeTextJournalCheckpoint {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 耐久metadata ownerを解放します
	 */
	//------------------------------------------------------------------------------
	virtual ~IWholeTextJournalCheckpoint() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief header前に作成identityを耐久化します
	 * @param[in,out] file 同一J
	 */
	//------------------------------------------------------------------------------
	virtual void created( TransferableJournalFile &file) = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief J flush後に封印cursor0を確定します
	 * @param[in] binding 封印値
	 * @param[in] header header SHA
	 * @param[in] cursor 候補cursor
	 */
	//------------------------------------------------------------------------------
	virtual void sealed( const WholeTextJournalBinding &binding, const WholeTextDigest &header,
						// この同期境界で所有する値、または確定状態の検証値です
						const WholeTextBoundedCursor &cursor) = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief J flush後に候補cursorを原子的に確定します
	 * @param[in] cursor 候補
	 */
	//------------------------------------------------------------------------------
	virtual void commit( const WholeTextBoundedCursor &cursor) = 0 ;
} ;
/** @brief 一operationだけが消費できる事前予約です */
class WholeTextBoundedStorageBudget final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 64MiB/32件/512MiB/本文16MiBをchecked算術で予約します
	 * @param[in] root 同volume history root。空は既定
	 * @param[in] ledger U4が既存全量で初期化して保持する台帳
	 * @param[in] usage 全使用量/上界
	 */
	//------------------------------------------------------------------------------
	WholeTextBoundedStorageBudget( std::filesystem::path root, std::shared_ptr<TransferableJournalQuota> ledger,
								  WholeTextBoundedStorageUsage usage) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief fileへ渡していない予約だけを返します
	 */
	//------------------------------------------------------------------------------
	~WholeTextBoundedStorageBudget() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 一operationの予約chargeとcheckpointを一回だけfileへ移す責任を複製しないため、copy構築を禁止します
	 * @note 名前のない第1引数（const WholeTextBoundedStorageBudget &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	WholeTextBoundedStorageBudget( const WholeTextBoundedStorageBudget &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 一operationの予約chargeとcheckpointを一回だけfileへ移す責任を複製しないため、copy代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const WholeTextBoundedStorageBudget &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	WholeTextBoundedStorageBudget &operator=( const WholeTextBoundedStorageBudget &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 単独予約を移します
	 * @param[in,out] other 元owner
	 */
	//------------------------------------------------------------------------------
	WholeTextBoundedStorageBudget( WholeTextBoundedStorageBudget &&other) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 現予約を返して移します
	 * @param[in,out] other 元owner
	 * @return 自身
	 */
	//------------------------------------------------------------------------------
	WholeTextBoundedStorageBudget &operator=( WholeTextBoundedStorageBudget &&other) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 原文を含む既存本文byteを読みます
	 * @return U4が渡した全使用量
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t	   retainedTextBytes() const noexcept
	{
		return retainedTextBytes_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 残り本文容量を読みます
	 * @return 最大8MiB
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t bodyCapacity() const noexcept
	{
		return bodyCapacity_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 検証済みroot値を読みます
	 * @return 指定root
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const std::filesystem::path &root() const noexcept
	{
		return root_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 一回だけfileへ予約責任を移します
	 * @return 単独fileと共有する事前確保charge
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::shared_ptr<TransferableJournalCharge> takeCharge() noexcept ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 予約ownerを生成前に付けます
	 * @param[in] checkpoint 耐久slot
	 */
	//------------------------------------------------------------------------------
	void setCheckpoint( std::shared_ptr<IWholeTextJournalCheckpoint> checkpoint) noexcept
	{
		checkpoint_ = std::move( checkpoint) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じcheckpointをJへ移します
	 * @return 単独worker用owner
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::shared_ptr<IWholeTextJournalCheckpoint> takeCheckpoint() noexcept
	{
		return std::move( checkpoint_) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 予約chargeを生成前に設定します
	 * @return 同じ事前確保owner
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const std::shared_ptr<TransferableJournalCharge> &charge() const noexcept
	{
		return charge_ ;
	}

	private:
	/// 4KiB耐久slot
	std::shared_ptr<IWholeTextJournalCheckpoint> checkpoint_ ;
	/// U4指定保存先
	std::filesystem::path						 root_ ;
	/// 事前確保の台帳枠
	std::shared_ptr<TransferableJournalCharge>	 charge_ ;
	/// 既存全本文の計上値
	std::uint64_t								 retainedTextBytes_ = 0 ;
	/// 原文を捨てずに残る本文byte
	std::uint64_t								 bodyCapacity_ = 0 ;
} ;
/** @brief 一回の人工I/O失敗を共有所有する小さい値です */
struct WholeTextJournalFault {
	/// 1body後、2receipt後、3flush前、4read
	std::uint32_t stage = 0 ;
	/// 対象record
	std::uint32_t index = 0 ;
	/// 元OS相当code
	std::uint32_t code = 0 ;
} ;
/** @brief 完成計画の値だけを封印headerへ渡します */
struct WholeTextJournalBinding {
	/// source/profile/op
	WholeTextCorrectionRequest request ;
	/// 元PCM証拠
	DecodedPcmIdentity		   pcm ;
	/// 全件数とSHA
	WholeTextBoundedPromotion  promotion ;
	/// 全core byte
	WholeTextDigest			   originalSha256{} ;
} ;
/** @brief 全結果bufferを作らず同じ一fileへappendするownerです */
class WholeTextBoundedJournal final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 生成前に一fileとstream hash/read領域を物理予約します
	 * @param[in] budget 消費するquota
	 * @param[in] fault 小さい共有人工失敗、通常null
	 */
	//------------------------------------------------------------------------------
	explicit WholeTextBoundedJournal(
		WholeTextBoundedStorageBudget		   budget,
		// この同期境界で所有する値、または確定状態の検証値です
		std::shared_ptr<WholeTextJournalFault> fault = {}) ; // この同期処理で使う所有値または検証値
	//------------------------------------------------------------------------------
	/**
	 * @brief 未引渡しfileだけを通常cleanupします
	 */
	//------------------------------------------------------------------------------
	~WholeTextBoundedJournal() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 一payload fileとstream hash・cursor・checkpointの一貫した所有を複製しないため、copy構築を禁止します
	 * @note 名前のない第1引数（const WholeTextBoundedJournal &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	WholeTextBoundedJournal( const WholeTextBoundedJournal &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 一payload fileとstream hash・cursor・checkpointの一貫した所有を複製しないため、copy代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const WholeTextBoundedJournal &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	WholeTextBoundedJournal &operator=( const WholeTextBoundedJournal &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ一fileを移します
	 * @param[in,out] other 元owner
	 */
	//------------------------------------------------------------------------------
	WholeTextBoundedJournal( WholeTextBoundedJournal &&other) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 旧ownerをcleanupして移します
	 * @param[in,out] other 元owner
	 * @return 自身
	 */
	//------------------------------------------------------------------------------
	WholeTextBoundedJournal &operator=( WholeTextBoundedJournal &&other) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 全aux/同一plan/headerを一回write/flushし以後不変にします
	 * @param[in] binding 全値
	 * @param[in] auxiliary 全lossless byte
	 * @param[in] plan sealed plan
	 */
	//------------------------------------------------------------------------------
	void					 seal( const WholeTextJournalBinding &binding, std::span<const std::uint8_t> auxiliary,
								  const WholeTextChunkPlan &plan) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief body→receipt→flushの後だけcursorを進めます
	 * @param[in] record 次の封印record
	 * @param[in] body 一coreだけ
	 * @param[in] generated 生成成功receipt。空白copyではnull
	 * @param[in] stop 追記確定前の取消し
	 */
	//------------------------------------------------------------------------------
	void					 append( const WholeTextChunkRecord &record, std::string_view body,
									const WholeTextBoundedChunkReceipt *generated, std::stop_token stop) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 確定prefix内だけを部分readします
	 * @param[in] offset 本文byte位置
	 * @param[out] bytes 最大65536byte
	 */
	//------------------------------------------------------------------------------
	void					 readPrefix( std::uint64_t offset, std::span<std::uint8_t> bytes) const ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 全確定receiptとextentをstream照合します
	 * @param[in] plan 同じ封印計画
	 */
	//------------------------------------------------------------------------------
	void					 validatePrefix( const WholeTextChunkPlan &plan) const ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 確定cursor値を読みます
	 * @return 最後にflush済みの値
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] WholeTextBoundedCursor		 cursor() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 次chunkが使える本文残量を読みます
	 * @return byte残量
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t					 remaining() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じhandle/pathのownerへアクセスします
	 * @return 単独file
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] TransferableJournalFile		&file() noexcept ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 封印状態を読みます
	 * @return header確定時true
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool							 sealed() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 不変bindingを読みます
	 * @return owner寿命中の小値
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const WholeTextJournalBinding &binding() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief header SHAを読みます
	 * @return 封印値
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] WholeTextDigest				 headerSha256() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief stored planをstream復元します
	 * @param[in] stop 停止
	 * @return 保存plan
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] WholeTextChunkPlan			 storedPlan( std::stop_token stop = {}) const ;

	private:
	class Impl ; // privateな同一owner実装
	friend class WholeTextPersistenceStore ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 照合済みknown fileをcursorから復元します
	 * @param[in] file 同一file owner
	 * @param[in] descriptor checkpoint証拠
	 * @return 未commit tailを読まないjournal
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] static std::unique_ptr<WholeTextBoundedJournal> reopen( TransferableJournalFile		   file,
																		 const WholeTextBoundedDescriptor &descriptor) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 検証済みimplをmoveします
	 * @param[in] impl owner
	 */
	//------------------------------------------------------------------------------
	explicit WholeTextBoundedJournal( std::unique_ptr<Impl> impl) ;
	class Impl ; // hash資源/一blockだけの読み領域
	/// terminalへそのままmove
	std::unique_ptr<Impl> impl_ ;
} ;
/** @brief native所有物のないmove-onlyな保存前terminalです */
struct WholeTextBoundedTerminal {
	/// 同じ原文owner
	std::shared_ptr<const Transcript>		 source ;
	/// 小さい終端値
	WholeTextBoundedResult					 result ;
	/// 同じ一payload file
	std::unique_ptr<WholeTextBoundedJournal> journal ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 空の失敗terminalを事前確保します
	 */
	//------------------------------------------------------------------------------
	WholeTextBoundedTerminal() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 原文共有ownerと一payload journalを同じ保存前終端として移譲するため、copy構築を禁止します
	 * @note 名前のない第1引数（const WholeTextBoundedTerminal &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	WholeTextBoundedTerminal( const WholeTextBoundedTerminal &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 原文共有ownerと一payload journalを同じ保存前終端として移譲するため、copy代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const WholeTextBoundedTerminal &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	WholeTextBoundedTerminal &operator=( const WholeTextBoundedTerminal &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 全所有物をallocationなしで移します
	 * @param[in,out] other 元owner
	 */
	//------------------------------------------------------------------------------
	WholeTextBoundedTerminal( WholeTextBoundedTerminal &&other) noexcept = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 全所有物を移します
	 * @param[in,out] other 元owner
	 * @return 自身
	 */
	//------------------------------------------------------------------------------
	WholeTextBoundedTerminal &operator=( WholeTextBoundedTerminal &&other) noexcept = default ;
} ;
} // namespace koefude
