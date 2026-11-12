//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_persistence_store.h
 * @brief 同一Jの耐久所有と保存トランザクションを定義します
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実契約・所有寿命・戻り値の意味を具体化し、標準headerを直接include
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415のoperator括弧と文書lambdaの書式を補修
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/history.h"
#include "core/job_controller.h"
#include "platform/whole_text_journal_store.h"
#include <array>
#include <span>
#include <functional>
#include <memory>
namespace koefude {
namespace detail {
/** @brief 通常構成から選択しない同期tiny試験の失敗注入です */
struct WholeTextPersistenceTestHooks {
	/// logical Jだけ
	std::shared_ptr<WholeTextJournalTestIo> journal ;
	/// checkpoint write11/flush12/replace13、rename20、metadata21
	std::uint32_t							failStage = 0 ;
	/// 元相当code
	std::uint32_t							code = 112 ;
	/// 縮小fixtureでも正しいallocation上界
	std::uint64_t							allocationUnit = 4096 ;
	/// 縮小だけ許可
	std::uint64_t							operationLimit = 67108864 ;
	/// 縮小だけ許可
	std::uint64_t							historyLimit = 536870912 ;
	/// 縮小だけ許可
	std::uint64_t							textLimit = 16777216 ;
	/// allocation不要の小trace
	std::array<std::uint32_t, 32>			hits{} ;
} ;
/** @brief 人工logical長さも扱うentry別の保持textです */
struct WholeTextRetainedEntryAmount {
	/// 元entry ID
	std::uint64_t id = 0 ;
	/// 同entryのunique本文byte
	std::uint64_t bytes = 0 ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 対象entryだけの保持textをchecked計数します
 * @param[in] entries 最大32件の既知量
 * @param[in] id 対象ID
 * @return 対象の本文合計
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::uint64_t persistenceRetainedEntryBytes( std::span<const WholeTextRetainedEntryAmount> entries,
														  std::uint64_t									id) ;
/** @brief 有界quota算術の入力です */
struct WholeTextPersistenceAmounts {
	/// 旧J
	std::uint64_t previousJournal = 0 ;
	/// 新J
	std::uint64_t newJournal = 25231360 ;
	/// 全旧snapshot/owner/residual
	std::uint64_t oldMetadata = 0 ;
	/// 全new/temp/backup上界
	std::uint64_t newMetadata = 0 ;
	/// 全保持byte
	std::uint64_t historyExisting = 0 ;
	/// 全保持本文
	std::uint64_t textExisting = 0 ;
	/// 既存件数
	std::uint32_t entries = 0 ;
	/// 同source再作成は0
	std::uint32_t entryDelta = 0 ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 固定inventory境界とknown属性を照合します
 * @param[in] probe 現probe0始まり
 * @param[in] slots 既存slot数
 * @param[in] known 名前/ID/header/台帳の一致
 * @param[in] unsafe 同期/link等
 * @param[in] sizeKnown 実使用量の照会成功
 * @param[in] live 他owner使用中
 * @return 読取り/cleanupの候補として安全な時true
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool persistenceInventoryCandidate( std::uint32_t probe, std::uint32_t slots, bool known, bool unsafe,
												 bool sizeKnown, bool live) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 全加算と上限を生成前に検査します
 * @param[in] amount 全使用量
 * @param[in] operationLimit 縮小だけ
 * @param[in] historyLimit 縮小だけ
 * @param[in] textLimit 縮小だけ
 * @return 全条件適合
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool persistenceAmountsFit( const WholeTextPersistenceAmounts &amount,
										 // この同期境界で所有する値、または確定状態の検証値です
										 std::uint64_t						operationLimit = 67108864,
										 // この同期境界で所有する値、または確定状態の検証値です
										 std::uint64_t						historyLimit = 536870912,
										 // この同期境界で所有する値、または確定状態の検証値です
										 std::uint64_t						textLimit = 16777216) noexcept ;
} // namespace detail
/** @brief history.lockを持つ一workerだけが使う耐久補正storageです */
class WholeTextPersistenceStore final {
	public:
	using Commit = std::function<void( const HistorySnapshot &, HistorySaveReceipt &)> ; // 同期atomic保存
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定rootとatomic writerを借用せず保持します
	 * @param[in] root 検証済みprivate history root
	 * @param[in] commit 同期writer
	 * @param[in] hooks 内部試験だけ。通常null
	 * @anchor koefude_whole_text_persistence_store_construct_contract
	 */
	//------------------------------------------------------------------------------
	WholeTextPersistenceStore( std::filesystem::path root, Commit commit,
							  // この同期境界で所有する値、または確定状態の検証値です
							  std::shared_ptr<detail::WholeTextPersistenceTestHooks> hooks = {}) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 耐久ownerをclose-onlyで解放します
	 */
	//------------------------------------------------------------------------------
	~WholeTextPersistenceStore() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief history.lock下の一workerに属する耐久inventory・quota・checkpointとwriterを複製しないため、copy構築を禁止します
	 * @note 名前のない第1引数（const WholeTextPersistenceStore &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	WholeTextPersistenceStore( const WholeTextPersistenceStore &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief history.lock下の一workerに属する耐久inventory・quota・checkpointとwriterを複製しないため、copy代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const WholeTextPersistenceStore &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	WholeTextPersistenceStore &operator=( const WholeTextPersistenceStore &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief lock後に有界inventoryと復元を行います
	 * @param[in,out] durable 検証済み正本。原文は失敗時も保持
	 */
	//------------------------------------------------------------------------------
	void					   load( HistorySnapshot &durable) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief Pendingと全quotaを確定します
	 * @param[in,out] durable 正本
	 * @param[in] request 全要求
	 * @param[in] stop 停止
	 * @return 生成前の一回予約
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] WholeTextBoundedStorageBudget
	reserve( HistorySnapshot &durable, const WholeTextCorrectionJobRequest &request, std::stop_token stop) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じterminalを検証してmetadataを確定します
	 * @param[in,out] durable 正本
	 * @param[in,out] terminal 一J
	 * @param[out] receipt 原子置換結果
	 * @param[in] stop precommit停止
	 * @return 所有immutable completion。失敗時terminalは非消費
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::shared_ptr<const WholeTextBoundedPersistenceCompletion>
	persist( HistorySnapshot &durable, WholeTextBoundedTerminal &terminal, HistorySaveReceipt &receipt,
			std::stop_token stop) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 事前確保した未保存prefixを返します
	 * @param[in] kind 保存分類
	 * @param[in] code 元code
	 * @return 旧有効全文を保つ未保存ack
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::shared_ptr<const WholeTextBoundedPersistenceCompletion> failure( HistoryErrorKind kind,
																					   std::uint32_t	code) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 未完参照を原子的に除去してからcleanupします
	 * @param[in,out] durable 正本
	 * @param[in] source 元key
	 * @param[in] op 元op
	 * @param[out] receipt 保存結果
	 * @return 独立cleanup結果
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] WholeTextBoundedCleanupResult discard( HistorySnapshot &durable, const ReadingSourceKey &source,
														std::uint64_t op, HistorySaveReceipt &receipt) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 普通save前にも全snapshot増加を予約上界へ照合します
	 * @param[in] candidate 候補
	 */
	//------------------------------------------------------------------------------
	void										checkOrdinary( const HistorySnapshot &candidate) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 成功した普通save後に既知の参照外Jだけを回収します
	 * @param[in] durable 新正本
	 */
	//------------------------------------------------------------------------------
	void										ordinaryCommitted( const HistorySnapshot &durable) noexcept ;

	private:
	class Impl ; // fixed inventory/checkpoint/charge
	/// worker限定
	std::unique_ptr<Impl> impl_ ;
} ;
} // namespace koefude
