//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file chat_document_controller.h
 * @brief 最新の表示準備だけを有界に所有するworkerを定義します
 * @version 1.0.0
 * @date 2026/10/04
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/04 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/04 推論と独立した表示準備workerを追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "app/chat_document.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>

namespace koefude {
/** @brief 内容を含まない診断と所有表示文書の終端です */
struct ChatDocumentEvent {
	/// 再利用しない受付ID
	std::uint64_t								id = 0 ;
	/// 成功時だけの所有文書
	std::shared_ptr<const PreparedChatDocument> document ;
	/// 失敗分類
	ErrorKind									error = ErrorKind::INTERNAL ;
	/// 元の診断値
	std::uint32_t								code = 0 ;
} ;
using ChatDocumentRunner =
	std::function<PreparedChatDocument( const ChatDocumentRequest &,
									   std::stop_token)> ; // 本文照合/構築のみ。推論を行わない同期境界

/** @brief 実行1件/最新待機1件/未受領1件を保持する直列表示workerです */
class ChatDocumentController final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief workerを開始します
	 * @param[in] runner 同期準備器。HWND/engineを捕捉しません
	 * @param[in] notifier 起床通知のみ。process破棄まで有効
	 */
	//------------------------------------------------------------------------------
	ChatDocumentController( ChatDocumentRunner runner, std::function<void()> notifier) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 取消し後にworkerを回収します
	 * @note UIはstopped確認後に破棄します
	 */
	//------------------------------------------------------------------------------
	~ChatDocumentController() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 最新要求で前の待機を置き換えます
	 * @param[in] request 所有DTO
	 * @return 受付ID、停止/枯渇時0
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t					   submit( ChatDocumentRequest request) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 最新の終端を一回だけ回収します
	 * @return 所有応答、未到着ならnullopt
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::optional<ChatDocumentEvent> takeEvent() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 新規受付を停止し、実行中にも協調取消しを通知します
	 */
	//------------------------------------------------------------------------------
	void										   requestStop() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief notifierを含むworker終了を確認します
	 * @return 終了時true
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool							   stopped() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 通知callbackの障害を確認します
	 * @return 障害を記録した場合true
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool							   notificationFailed() const noexcept ;

	private:
	//------------------------------------------------------------------------------
	/**
	 * @brief 最新要求だけを処理し、例外を所有診断へ変換します
	 */
	//------------------------------------------------------------------------------
	void														 run() noexcept ;
	/// 生成器なしの同期処理
	ChatDocumentRunner											 runner_ ;
	/// 所有文書を運ばない通知
	std::function<void()>										 notifier_ ;
	/// 受付/回収の短い排他
	std::mutex													 mutex_ ;
	/// 最新待機または終了の通知
	std::condition_variable										 changed_ ;
	/// 最新待機1件
	std::optional<std::pair<std::uint64_t, ChatDocumentRequest>> pending_ ;
	/// 最新未受領1件
	std::optional<ChatDocumentEvent>							 event_ ;
	/// 現準備だけの取消し
	std::stop_source											 cancellation_{std::nostopstate} ;
	/// UI側で確保済みの最新待機取消し
	std::stop_source											 pendingStop_{std::nostopstate} ;
	/// wrap後は0として受付を停止
	std::uint64_t												 nextId_ = 1 ;
	/// 古い準備のpublishを拒否するID
	std::uint64_t												 latestId_ = 0 ;
	/// 新規受付停止
	bool														 stopping_ = false ;
	/// notifier完了後の終了状態
	std::atomic<bool>											 stopped_ = false ;
	/// 応答保持後の通知障害
	std::atomic<bool>											 notificationFailed_ = false ;
	/// 全member初期化後に開始する所有worker
	std::jthread												 worker_ ;
} ;
} // namespace koefude
