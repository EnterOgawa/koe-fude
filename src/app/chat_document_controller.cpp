//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file chat_document_controller.cpp
 * @brief 最新表示の置換・協調取消し・古い応答の拒否を実装します
 * @version 1.0.0
 * @date 2026/10/04
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/04 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/04 最新1件の表示準備と所有終端を実装
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
#include "app/chat_document_controller.h"
#include <optional>
#include <utility>

namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief workerを開始します
 * @param[in] runner 同期準備器。HWND/engineを捕捉しません
 * @param[in] notifier 起床通知のみ。process破棄まで有効
 * @see koefude::ChatDocumentController::ChatDocumentController
 */
//------------------------------------------------------------------------------
ChatDocumentController::ChatDocumentController( ChatDocumentRunner runner, std::function<void()> notifier)
	: runner_( std::move( runner)), notifier_( std::move( notifier)), worker_(
		//------------------------------------------------------------------------------
		/**
		 * @brief 所有workerの例外境界を開始します
		 */
		//------------------------------------------------------------------------------
		[this]
		{
			run() ;
		})
{
}
//------------------------------------------------------------------------------
/**
 * @brief 取消し後にworkerを回収します
 * @note UIはstopped確認後に破棄します
 * @see koefude::ChatDocumentController::~ChatDocumentController
 */
//------------------------------------------------------------------------------
ChatDocumentController::~ChatDocumentController()
{
	requestStop() ;
	worker_.join() ;
}
//------------------------------------------------------------------------------
/**
 * @brief 最新要求で前の待機を置き換えます
 * @param[in] request 所有DTO
 * @return 受付ID、停止/枯渇時0
 * @see koefude::ChatDocumentController::submit
 */
//------------------------------------------------------------------------------
std::uint64_t ChatDocumentController::submit( ChatDocumentRequest request)
{
	std::stop_source nextStop ; // allocation失敗をUIの例外境界で扱う次の取消し状態
	std::stop_source previous( std::nostopstate) ; // callbackをlock外で起床する取消し状態
	std::uint64_t	 id = 0 ; // 成立した要求だけに割り当てるID
	{
		const std::lock_guard lock( mutex_) ; // 最大一つの所有DTOを置換する短い排他
		// 停止とID枯渇では前の結果を新要求として返さない
		if( stopping_ || nextId_ == 0) {
			return 0 ;
		}
		id = nextId_++ ;
		pending_.emplace( id, std::move( request)) ;
		pendingStop_ = std::move( nextStop) ;
		latestId_ = id ;
		event_.reset() ;
		previous = cancellation_ ;
	}
	previous.request_stop() ;
	changed_.notify_one() ;
	return id ;
}
//------------------------------------------------------------------------------
/**
 * @brief 最新の終端を一回だけ回収します
 * @return 所有応答、未到着ならnullopt
 * @see koefude::ChatDocumentController::takeEvent
 */
//------------------------------------------------------------------------------
std::optional<ChatDocumentEvent> ChatDocumentController::takeEvent()
{
	const std::lock_guard lock( mutex_) ; // 同じ所有応答を二度返さない排他
	auto				  event = std::move( event_) ; // 呼出側へ移す最新応答
	event_.reset() ;
	return event ;
}
//------------------------------------------------------------------------------
/**
 * @brief 新規受付を停止し、実行中にも協調取消しを通知します
 * @see koefude::ChatDocumentController::requestStop
 */
//------------------------------------------------------------------------------
void ChatDocumentController::requestStop()
{
	std::stop_source previous( std::nostopstate) ; // notifierを排他外で呼ぶ共有取消し
	{
		const std::lock_guard lock( mutex_) ; // 先に新規受付と待機を止める
		stopping_ = true ;
		pending_.reset() ;
		event_.reset() ;
		previous = cancellation_ ;
	}
	previous.request_stop() ;
	changed_.notify_one() ;
}
//------------------------------------------------------------------------------
/**
 * @brief notifierを含むworker終了を確認します
 * @return 終了時true
 * @see koefude::ChatDocumentController::stopped
 */
//------------------------------------------------------------------------------
bool ChatDocumentController::stopped() const noexcept
{
	return stopped_.load( std::memory_order_acquire) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 通知callbackの障害を確認します
 * @return 障害を記録した場合true
 * @see koefude::ChatDocumentController::notificationFailed
 */
//------------------------------------------------------------------------------
bool ChatDocumentController::notificationFailed() const noexcept
{
	return notificationFailed_.load() ;
}
//------------------------------------------------------------------------------
/**
 * @brief 最新要求だけを処理し、例外を所有診断へ変換します
 * @see koefude::ChatDocumentController::run
 */
//------------------------------------------------------------------------------
void ChatDocumentController::run() noexcept
{
	// 常に最新待機だけを取り出し、停止時は所有応答を新しくpublishしない
	for(;;) {
		std::optional<std::pair<std::uint64_t, ChatDocumentRequest>> job ; // 今回だけの所有要求
		std::stop_token												 stop ; // この準備だけの協調取消し
		{
			std::unique_lock lock( mutex_) ; // 条件待ちの間は解放する
			changed_.wait( lock,
						  //------------------------------------------------------------------------------
						  /**
						   * @brief 処理すべき状態を確認します
						   * @return 起床時true
						   */
						  //------------------------------------------------------------------------------
						  [this]
						  {
							  return stopping_ || pending_.has_value() ;
						  }) ;
			// 待機を破棄済みの停止では、通知を追加せず終了する
			if( stopping_) {
				break ;
			}
			job = std::move( pending_) ;
			pending_.reset() ;
			cancellation_ = pendingStop_ ; // worker内でstop stateのheap確保を行わない
			stop = cancellation_.get_token() ;
		}
		ChatDocumentEvent event ; // 成功/失敗を共通の所有終端にする
		event.id = job->first ;
		try {
			checkCancelled( stop) ;
			auto document = runner_( job->second, stop) ; // 推論なしの所有表示文書
			checkCancelled( stop) ;
			// runnerが別カード/版/名前の文書を返すことも拒否する
			if( document.request != job->second) {
				throw OperationError( ErrorKind::INVALID_INPUT) ;
			}
			event.document = std::make_shared<const PreparedChatDocument>( std::move( document)) ;
		} catch( const OperationError &error) {
			event.error = error.kind() ;
			event.code = error.code() ;
		} catch( ...) {
			event.error = ErrorKind::INTERNAL ;
		}
		{
			const std::lock_guard lock( mutex_) ; // 最終IDと取消しをpublish時にも照合する
			// 古い要求の成功/失敗を新しいカードの表示へ渡さない
			if( stopping_ || latestId_ != event.id || stop.stop_requested()) {
				continue ;
			}
			event_ = std::move( event) ;
		}
		try {
			// 通知なしのpollingでも所有応答を回収できる
			if( notifier_) {
				notifier_() ;
			}
		} catch( ...) {
			// 応答を保持済みなので既存timerの回収に委ね、thread外へ例外を出さない
			notificationFailed_.store( true) ;
		}
	}
	stopped_.store( true, std::memory_order_release) ;
}
} // namespace koefude
