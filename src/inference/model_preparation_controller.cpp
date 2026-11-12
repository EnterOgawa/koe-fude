//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file model_preparation_controller.cpp
 * @brief モデル準備の非同期実行と操作番号による通知を実装します
 * @version 1.0.0
 * @date 2026/09/13
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/13 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/13 有界worker・取消し・終端保持を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 宣言の役割を明記
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1397の固定metadataを排他外で記録
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
#include "inference/model_preparation_controller.h"

#include <optional>
#include <utility>

#include "core/local_diagnostic_hooks.h"

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief worker上で読取り検査または明示取得を実行します
 * @param[in] request 固定要求。downloadだけが通信を許可
 * @param[in] stop 協調取消し
 * @param[in] progress 空を許す同期進捗通知
 * @return 固定構成の検査済み計画
 * @throws OperationError 検査・取得失敗、取消し
 * @see koefude::runModelPreparation
 */
//------------------------------------------------------------------------------
ModelPreparationPlan runModelPreparation( const ModelPreparationRequest &request, const std::stop_token stop,
										 const ModelPreparationCallback &progress)
{
	// cacheはworker上で現在ユーザーに対応させた所有path
	const auto cache = request.cacheRoot.empty() ? defaultModelCache() : request.cacheRoot ;
	// 通信と書込みは明示取得の要求だけで実行する
	if( request.download) {
		WinHttpModelTransport transport ; // 同じworkerで所有・解放する実通信
		return downloadModels( request.options, request.bundleRoot, cache, transport, stop, progress) ;
	}
	LocalModelInspector inspector ; // 読取り検査は通信もdirectory作成もしない
	return planModelPreparation( request.options, request.bundleRoot, cache, inspector, stop) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 一つの待機workerを開始します
 * @param[in] runner 所有する同期処理。空は禁止
 * @param[in] diagnostics 全workerより長寿命の通常sink、nullは旧試験のI/O0
 * @see koefude::ModelPreparationController::ModelPreparationController(koefude::ModelPreparationRunner, koefude::LocalDiagnosticLog *)
 */
//------------------------------------------------------------------------------
ModelPreparationController::ModelPreparationController( ModelPreparationRunner runner, LocalDiagnosticLog *diagnostics)
	: runner_( std::move( runner)), diagnostics_( diagnostics), worker_(
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
 * @see koefude::ModelPreparationController::~ModelPreparationController
 */
//------------------------------------------------------------------------------
ModelPreparationController::~ModelPreparationController()
{
	requestStop() ;
	worker_.join() ;
}

//------------------------------------------------------------------------------
/**
 * @brief 空きslotへ固定要求を受け付けます
 * @param[in] request 所有権を移す要求
 * @return 新ID。実行中・終端未受領・停止中・ID枯渇なら0
 * @see koefude::ModelPreparationController::submit
 */
//------------------------------------------------------------------------------
std::uint64_t ModelPreparationController::submit( ModelPreparationRequest request)
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
 * @see koefude::ModelPreparationController::cancel
 */
//------------------------------------------------------------------------------
void ModelPreparationController::cancel( const std::uint64_t id)
{
	std::stop_source cancellation( std::nostopstate) ; // 排他外で取消しcallbackを呼ぶ所有copy
	{
		const std::lock_guard lock( mutex_) ; // 古いIDが新操作を取り消さない照合
		// 未完了の同じ操作だけへ取消しを通知する
		if( occupied_ && event_.id == id && !event_.terminal) {
			cancellation = cancellation_ ;
			event_.progress.stage = ModelPreparationStage::CANCELLING ;
			dirty_ = true ;
		}
	}
	cancellation.request_stop() ;
}

//------------------------------------------------------------------------------
/**
 * @brief 新規受付を停止し、現在操作を取り消します。joinしません
 * @see koefude::ModelPreparationController::requestStop
 */
//------------------------------------------------------------------------------
void ModelPreparationController::requestStop()
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
 * @see koefude::ModelPreparationController::stopped
 */
//------------------------------------------------------------------------------
bool ModelPreparationController::stopped() const noexcept
{
	return stopped_.load( std::memory_order_acquire) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 最新通知を受領します
 * @return 未更新ならnullopt。終端受領で枠を解放
 * @see koefude::ModelPreparationController::takeEvent
 */
//------------------------------------------------------------------------------
std::optional<ModelPreparationEvent> ModelPreparationController::takeEvent()
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
 * @see koefude::ModelPreparationController::publish
 */
//------------------------------------------------------------------------------
void ModelPreparationController::publish( ModelPreparationEvent event) noexcept
{
	const std::lock_guard lock( mutex_) ; // I/Oとcallbackを含まない短い差替え
	// 受領済み・未知IDや終端後の通知は現在操作へ戻さない
	if( !occupied_ || event.id != event_.id || event_.terminal) {
		return ;
	}
	// 取消し要求後の中断不能区間は通常進捗へ表示を戻さない
	if( !event.terminal && event_.progress.stage == ModelPreparationStage::CANCELLING) {
		event.progress.stage = ModelPreparationStage::CANCELLING ;
	}
	event_ = std::move( event) ;
	dirty_ = true ;
}

//------------------------------------------------------------------------------
/**
 * @brief 要求をmutexの外で実行し、thread境界の例外を通知へ変換します
 * @see koefude::ModelPreparationController::run
 */
//------------------------------------------------------------------------------
void ModelPreparationController::run() noexcept
{
	// 新規要求を一つずつ処理し、停止時は所有要求の終端を残して終了する
	for(;;) {
		ModelPreparationRequest request ; // 排他外で処理する所有要求
		ModelPreparationEvent	result ; // 同じIDへ返す終端
		std::stop_token			stop ; // 操作単位の取消しをborrowせず保持
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
		// 実worker開始でだけ記録し、画面表示を取得開始へ読み替えない
		if( diagnostics_) {
			auto record = localModelStarted( result.id, request) ; // pathなしの小copy
			record.event = request.download ? LocalDiagnosticEvent::MODEL_DOWNLOAD_REQUESTED
											: LocalDiagnosticEvent::MODEL_CHECK_REQUESTED ;
			diagnostics_->append( record) ;
			record.event = LocalDiagnosticEvent::MODEL_STARTED ;
			diagnostics_->append( record) ;
		}
		std::optional<ModelPreparationProgress> lastLogged ; // byte/tickごとの反復を省く
		try {
			// 開始前の取消しでもrunnerを呼ばず終端を確定する
			if( stop.stop_requested()) {
				throw OperationError( ErrorKind::CANCELLED) ;
			}
			auto plan = runner_( request, stop,
								//------------------------------------------------------------------------------
								/**
								 * @brief 現在操作へ最新進捗を集約します
								 * @param[in] progress 所有進捗値
								 */
								//------------------------------------------------------------------------------
								[this, id = result.id, &request, &lastLogged]( const auto progress)
								{
									// 実段階/対象/検証完了の変化だけを記録し、転送blockでは増やさない
									if( diagnostics_ && (!lastLogged || lastLogged->stage != progress.stage ||
														lastLogged->asset != progress.asset ||
														lastLogged->verified != progress.verified)) {
										auto record = localModelStarted( id, request) ; // 固定metadata
										record.event = LocalDiagnosticEvent::MODEL_STAGE ;
										record.modelStage = progress.stage ;
										record.asset = progress.asset ;
										record.bytes = progress.requiredBytes ;
										record.verified = progress.verified ;
										diagnostics_->append( record) ;
										lastLogged = progress ;
									}
									publish( {id, progress}) ;
								}) ; // callbackはこの同期呼出し中だけ有効
			result.progress.stage = plan.ready ? ModelPreparationStage::READY : ModelPreparationStage::MISSING ;
			result.plan = std::make_shared<const ModelPreparationPlan>( std::move( plan)) ;
		} catch( const OperationError &error) {
			result.error = error.kind() ;
			result.code = error.code() ;
			result.progress.stage = error.kind() == ErrorKind::CANCELLED ? ModelPreparationStage::CANCELLED
																		 : ModelPreparationStage::FAILURE ;
		} catch( ...) {
			// 例外でthreadを失わず、診断可能な終端を保持して再試行を受け付ける
			result.progress.stage = ModelPreparationStage::FAILURE ;
			result.error = ErrorKind::INTERNAL ;
		}
		logLocalModelTerminal( diagnostics_, request, result) ;
		publish( std::move( result)) ;
	}
	stopped_.store( true, std::memory_order_release) ;
}

} // namespace koefude
