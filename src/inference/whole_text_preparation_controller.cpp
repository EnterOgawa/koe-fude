//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_preparation_controller.cpp
 * @brief 一枠・単調ID・取消しを保つ補正準備workerを実装します
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1279の補正準備入口を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1280の明示導入・補助準備とcommit成功を保持
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1283の取得確定だけを後置取消しより優先
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
#include "inference/whole_text_preparation_controller.h"
#include "core/types.h"

#include <optional>
#include <type_traits>
#include <utility>

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief 一つの待機workerを開始します
 * @param[in] runner 所有する同期処理。空は禁止
 * @param[in] owner runner前の通知領域確保。空/NULL返却は禁止、失敗はI/O前に終端
 * @see koefude::WholeTextPreparationController::WholeTextPreparationController(koefude::WholeTextPreparationRunner, std::function< std::shared_ptr< koefude::WholeTextPreparationPlan >()>)
 */
//------------------------------------------------------------------------------
WholeTextPreparationController::WholeTextPreparationController(
	WholeTextPreparationRunner runner, std::function<std::shared_ptr<WholeTextPreparationPlan>()> owner)
	: owner_( std::move( owner)), runner_( std::move( runner)), worker_(
		//------------------------------------------------------------------------------
		/**
		 * @brief 所有instanceのworker境界を開始します
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
 * @brief 停止要求後にjoinします。UIはstoppedを確認してから破棄します
 * @see koefude::WholeTextPreparationController::~WholeTextPreparationController
 */
//------------------------------------------------------------------------------
WholeTextPreparationController::~WholeTextPreparationController()
{
	requestStop() ;
	worker_.join() ;
}

//------------------------------------------------------------------------------
/**
 * @brief 空きslotへ固定要求を受け付けます
 * @param[in] request 所有権を移す要求
 * @return 新ID。実行中・終端未受領・停止中・ID枯渇なら0
 * @see koefude::WholeTextPreparationController::submit
 */
//------------------------------------------------------------------------------
std::uint64_t WholeTextPreparationController::submit( WholeTextPreparationRequest request)
{
	const std::lock_guard lock( mutex_) ; // 所有要求の移動だけを排他する
	// 終端を受領するまで次操作で結果を上書きしない
	if( occupied_ || stopping_ || nextId_ == 0) {
		return 0 ;
	}
	event_ = {} ;
	event_.id = nextId_++ ;
	cancellation_ = std::stop_source{} ;
	pending_ = std::move( request) ;
	occupied_ = true ;
	dirty_ = true ;
	changed_.notify_one() ;
	return event_.id ;
}

//------------------------------------------------------------------------------
/**
 * @brief 現在のIDだけへ協調取消しを要求します
 * @param[in] id 受付番号。未知・受領済みの番号は無視
 * @see koefude::WholeTextPreparationController::cancel
 */
//------------------------------------------------------------------------------
void WholeTextPreparationController::cancel( const std::uint64_t id)
{
	std::stop_source cancellation( std::nostopstate) ; // 排他外で取消しcallbackを呼ぶ所有copy
	{
		const std::lock_guard lock( mutex_) ; // 古いIDが新操作を取り消さない照合
		// 未完了の同じ操作だけへ取消しを通知する
		if( occupied_ && event_.id == id && !event_.terminal) {
			cancellation = cancellation_ ;
			event_.progress = WholeTextPreparationStage::CANCELLING ;
			dirty_ = true ;
		}
	}
	cancellation.request_stop() ;
}

//------------------------------------------------------------------------------
/**
 * @brief 新規受付を停止し、現在操作を取り消します。joinしません
 * @see koefude::WholeTextPreparationController::requestStop
 */
//------------------------------------------------------------------------------
void WholeTextPreparationController::requestStop()
{
	std::stop_source cancellation( std::nostopstate) ; // 同期callbackを排他外へ分離
	{
		const std::lock_guard lock( mutex_) ; // 受付停止と現在の取消し元を確定
		stopping_ = true ;
		cancellation = cancellation_ ;
		changed_.notify_one() ;
	}
	cancellation.request_stop() ;
}

//------------------------------------------------------------------------------
/**
 * @brief worker終了を照合します
 * @return 終了時true
 * @see koefude::WholeTextPreparationController::stopped
 */
//------------------------------------------------------------------------------
bool WholeTextPreparationController::stopped() const noexcept
{
	return stopped_.load( std::memory_order_acquire) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 最新通知を受領します
 * @return 未更新ならnullopt。終端受領で枠を解放
 * @see koefude::WholeTextPreparationController::takeEvent
 */
//------------------------------------------------------------------------------
std::optional<WholeTextPreparationEvent> WholeTextPreparationController::takeEvent()
{
	const std::lock_guard lock( mutex_) ; // 所有通知のcopyと受付枠の解放だけを保護
	// 未更新の値を繰り返し適用しない
	if( !dirty_) {
		return std::nullopt ;
	}
	dirty_ = false ;
	// 終端受領後だけ新しい要求を受け付ける
	if( event_.terminal) {
		occupied_ = false ;
	}
	return event_ ;
}

//------------------------------------------------------------------------------
/**
 * @brief 同じ未完了操作の通知だけを差し替えます
 * @param[in] event 所有通知。古いID・終端後の通知は無視
 * @see koefude::WholeTextPreparationController::publish
 */
//------------------------------------------------------------------------------
void WholeTextPreparationController::publish( WholeTextPreparationEvent event) noexcept
{
	const std::lock_guard lock( mutex_) ; // I/Oとcallbackを含まない短い差替え
	// 受領済み・未知IDや終端後の通知は現在操作へ戻さない
	if( !occupied_ || event.id != event_.id || event_.terminal) {
		return ;
	}
	// 取消し要求後の中断不能区間は通常進捗へ表示を戻さない
	if( !event.terminal && event_.progress == WholeTextPreparationStage::CANCELLING) {
		event.progress = WholeTextPreparationStage::CANCELLING ;
	}
	event_ = std::move( event) ;
	dirty_ = true ;
}

//------------------------------------------------------------------------------
/**
 * @brief 要求をmutexの外で実行し、thread境界の例外を通知へ変換します
 * @see koefude::WholeTextPreparationController::run
 */
//------------------------------------------------------------------------------
void WholeTextPreparationController::run() noexcept
{
	// 新規要求を一つずつ処理し、停止時は所有要求の終端を残して終了する
	for(;;) {
		WholeTextPreparationRequest request ; // 排他外で処理する所有要求
		WholeTextPreparationEvent	result ; // 同じIDへ返す終端
		std::stop_token				stop ; // 操作単位の取消しをborrowせず保持
		{
			std::unique_lock lock( mutex_) ; // 待機中はcondition_variableが解放する
			changed_.wait( lock,
						  //------------------------------------------------------------------------------
						  /**
						   * @brief 新規要求か停止を確認します
						   * @return 待機終了時true
						   */
						  //------------------------------------------------------------------------------
						  [this]
						  {
							  return pending_.has_value() || stopping_ ;
						  }) ;
			// 受付済み要求があれば取消しを含む終端を作ってから終了する
			if( !pending_) {
				break ;
			}
			request = std::move( *pending_) ;
			pending_.reset() ;
			result.id = event_.id ;
			stop = cancellation_.get_token() ;
		}
		result.terminal = true ;
		try {
			// 開始前の取消しでもrunnerを呼ばず終端を確定する
			if( stop.stop_requested()) {
				throw OperationError( ErrorKind::CANCELLED) ;
			}
			auto owner = owner_() ; // renameより前、runnerのI/O前に一度だけ確保
			// NULLの人工ownerもrunnerを呼ばずに失敗へ変換します
			if( !owner) {
				throw OperationError( ErrorKind::INTERNAL, ERROR_NOT_ENOUGH_MEMORY) ;
			}
			auto plan = runner_( request, stop,
								//------------------------------------------------------------------------------
								/**
								 * @brief 現在操作へ最新進捗を集約します
								 * @param[in] progress 所有進捗値
								 */
								//------------------------------------------------------------------------------
								[this, id = result.id]( const auto progress)
								{
									WholeTextPreparationEvent update ; // 最新進捗だけを所有します
									update.id = id ;
									update.progress = progress.stage ;
									update.completedBytes = progress.completedBytes ;
									update.totalBytes = progress.totalBytes ;
									publish( std::move( update)) ;
								}) ; // callbackはこの同期呼出し中だけ有効
			// 同じ明示操作で実renameに勝った導入・取得だけは後置cancelで覆しません
			if( !((request.action == WholeTextPreparationAction::IMPORT_GEMMA && plan.importCommitted) ||
				 (request.action == WholeTextPreparationAction::DOWNLOAD_GEMMA && plan.downloadCommitted))) {
				checkCancelled( stop) ;
			}
			static_assert( std::is_nothrow_move_assignable_v<WholeTextPreparationPlan>) ;
			static_assert( std::is_nothrow_move_constructible_v<WholeTextPreparationPlan>) ;
			*owner = std::move( plan) ; // commit後にallocation/callbackを追加しません
			result.progress = WholeTextPreparationStage::COMPLETED ;
			result.plan = std::move( owner) ;
		} catch( const OperationError &error) {
			result.error = error.kind() ;
			result.code = error.code() ;
			result.progress = error.kind() == ErrorKind::CANCELLED ? WholeTextPreparationStage::CANCELLED
																   : WholeTextPreparationStage::FAILURE ;
		} catch( ...) {
			// 例外でthreadを失わず、診断可能な終端を保持して再試行を受け付ける
			result.progress = WholeTextPreparationStage::FAILURE ;
			result.error = ErrorKind::INTERNAL ;
		}
		publish( std::move( result)) ;
	}
	stopped_.store( true, std::memory_order_release) ;
}

} // namespace koefude
