//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file transcription_settings.cpp
 * @brief 明示選好の検証と有限設定workerを実装します
 * @version 1.0.0
 * @date 2026/11/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1375の保存ACKと起動復元を追加
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
#include "core/transcription_settings.h"
#include "inference/model_store.h"
#include <optional>
#include <limits>
#include <utility>
namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief EngineOptionsの公開選好だけを抽出します
 * @param[in] options 制約前の値
 * @return runtime情報を含まない値
 * @see koefude::transcriptionPreferences
 */
//------------------------------------------------------------------------------
TranscriptionPreferences transcriptionPreferences( const EngineOptions &options)
{
	return {options.asrModel,	  options.asrProfile,  options.backend,			 options.autoLanguage,
			options.speechFilter, options.diarization, options.expectedSpeakers, options.speakerMethod} ;
}
//------------------------------------------------------------------------------
/**
 * @brief 選好を既定の実行DTOへ戻します
 * @param[in] value 明示選好
 * @return pathなしの値
 * @see koefude::transcriptionOptions
 */
//------------------------------------------------------------------------------
EngineOptions transcriptionOptions( const TranscriptionPreferences &value)
{
	EngineOptions options ; // その他runtime値は従前既定
	options.asrModel = value.model ;
	options.asrProfile = value.profile ;
	options.backend = value.backend ;
	options.autoLanguage = value.autoLanguage ;
	options.speechFilter = value.speechFilter ;
	options.diarization = value.diarization ;
	options.expectedSpeakers = value.expectedSpeakers ;
	options.speakerMethod = value.method ;
	return options ;
}
//------------------------------------------------------------------------------
/**
 * @brief registry/enum/人数範囲を確認します
 * @param[in] value 選好
 * @return 全値が適格
 * @see koefude::validTranscriptionPreferences
 */
//------------------------------------------------------------------------------
bool validTranscriptionPreferences( const TranscriptionPreferences &value) noexcept
{
	return findAsrDefinition( value.model, value.profile) != nullptr &&
		   (value.backend == Backend::AUTO || value.backend == Backend::CPU) && value.expectedSpeakers >= 0 &&
		   value.expectedSpeakers <= 20 &&
		   (value.method == SpeakerMethod::SPECTRAL || value.method == SpeakerMethod::LEGACY_THRESHOLD) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 所有storageのloadを開始します
 * @param[in] storage 非NULLの同期storage
 * @param[in] notifier mutex外の起床通知。空可
 * @see koefude::TranscriptionSettingsController::TranscriptionSettingsController(std::unique_ptr< koefude::ITranscriptionSettingsStorage >, std::function< void()>)
 */
//------------------------------------------------------------------------------
TranscriptionSettingsController::TranscriptionSettingsController( std::unique_ptr<ITranscriptionSettingsStorage> storage,
																 std::function<void()> notifier)
	: storage_( std::move( storage)), notifier_( std::move( notifier))
{
	// NULLはworker起動前に拒否し、不完全controllerを公開しません
	if( !storage_) {
		throw HistoryError( HistoryErrorKind::INTERNAL) ;
	}
	worker_ = std::jthread(
		//------------------------------------------------------------------------------
		/**
		 * @brief 例外を外へ漏らさない所有thread入口です
		 */
		//------------------------------------------------------------------------------
		[this]
		{
			run() ;
		}) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 受理済み保存をdrainしてjoinします。UIはstopped後に呼びます
 * @see koefude::TranscriptionSettingsController::~TranscriptionSettingsController
 */
//------------------------------------------------------------------------------
TranscriptionSettingsController::~TranscriptionSettingsController()
{
	requestStop() ;
	// 初期化失敗時もcallback先破棄前に所有threadだけを終了します
	if( worker_.joinable()) {
		worker_.join() ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 未読込/処理中/ACK待ちは拒否します
 * @param[in] value 明示選好
 * @return 新ID、拒否は0。自動retryなし
 * @see koefude::TranscriptionSettingsController::save
 */
//------------------------------------------------------------------------------
std::uint64_t TranscriptionSettingsController::save( TranscriptionPreferences value)
{
	// 基盤例外後は同じmutexへ再入せず新規保存を拒否します
	if( fatal_ != 0) {
		return 0 ;
	}
	std::scoped_lock lock( mutex_) ; // I/Oを含まない単一slot受付
	// 未復元・失敗・重複・不正値を保存へ進めません
	if( !ready_ || busy_ || event_ || stopping_ || stopped_ || fatal_ != 0 ||
	   next_ == std::numeric_limits<std::uint64_t>::max() || !validTranscriptionPreferences( value)) {
		return 0 ;
	}
	const auto id = next_++ ; // 今回だけの操作番号
	pending_ = TranscriptionSettingsEvent{id, false, value} ;
	busy_ = true ;
	condition_.notify_one() ;
	return id ;
}
//------------------------------------------------------------------------------
/**
 * @brief ACKを一回だけ取得します
 * @return 未更新はnullopt
 * @see koefude::TranscriptionSettingsController::takeEvent
 */
//------------------------------------------------------------------------------
std::optional<TranscriptionSettingsEvent> TranscriptionSettingsController::takeEvent()
{
	unsigned unread = 1 ; // 同期基盤例外の固定診断を一度だけ受領
	// 失敗したmutexへ再入せず、呼出し側を起動失敗と同じ受付停止へ戻します
	if( fatal_ != 0) {
		// 一度だけ失敗を返し、以前の成功slotを再公開しません
		if( fatal_.compare_exchange_strong( unread, 2)) {
			TranscriptionSettingsEvent failed ; // 本文なしの固定診断
			failed.code = ERROR_UNHANDLED_EXCEPTION ;
			return failed ;
		}
		return std::nullopt ;
	}
	std::scoped_lock lock( mutex_) ; // 小ACKだけをmove
	return std::exchange( event_, std::nullopt) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 新規受付を止め、受理済み保存を完了して終了します
 * @see koefude::TranscriptionSettingsController::requestStop
 */
//------------------------------------------------------------------------------
void TranscriptionSettingsController::requestStop()
{
	// 終端/同期基盤失敗後に壊れた待機経路へ再入しません
	if( stopped_ || fatal_ != 0) {
		return ;
	}
	std::scoped_lock lock( mutex_) ; // 書込みの完了をUIで待ちません
	stopping_ = true ;
	condition_.notify_one() ;
}
//------------------------------------------------------------------------------
/**
 * @brief storage解放後の終端を確認します
 * @return 終了時true
 * @see koefude::TranscriptionSettingsController::stopped
 */
//------------------------------------------------------------------------------
bool TranscriptionSettingsController::stopped() const noexcept
{
	return stopped_.load() ;
}
//------------------------------------------------------------------------------
/**
 * @brief 失敗してもI/O確定を巻き戻さない起床通知を行います
 * @see koefude::TranscriptionSettingsController::notify
 */
//------------------------------------------------------------------------------
void TranscriptionSettingsController::notify() noexcept
{
	try {
		// notifierなしの純粋試験もpollで同じACKを受け取れます
		if( notifier_) {
			notifier_() ;
		}
	} catch( ...) {
		// ACKは既にslotへ確定済み。通知例外で保存事実を失わずpollへ委ねます
	}
}
//------------------------------------------------------------------------------
/**
 * @brief UIへ借用resourceを渡さずI/Oを直列実行します
 * @see koefude::TranscriptionSettingsController::run
 */
//------------------------------------------------------------------------------
void TranscriptionSettingsController::run() noexcept
{
	try {
		runIo() ;
	} catch( ...) {
		// 条件待機/同期基盤の例外もthread境界で固定診断へ変換します
		fatal_ = 1 ; // 同じ基盤へ再入せず、次pollへ固定診断を残します
	}
	storage_.reset() ; // lock/ACLもこのworkerで解放
	notify() ; // 最後のnotifier終了後にだけstoppedを公開
	stopped_ = true ;
}
//------------------------------------------------------------------------------
/**
 * @brief load/saveと条件待機を単一threadで実行します
 * @throws std::exception 同期基盤失敗
 * @see koefude::TranscriptionSettingsController::runIo
 */
//------------------------------------------------------------------------------
void TranscriptionSettingsController::runIo()
{
	TranscriptionSettingsEvent current ; // 最初はload ID0
	// load一回後は受理済み保存を一件ずつ処理し、stopと未要求で終了します
	for(;;) {
		try {
			// 起動復元は保存を伴わず、missingだけ従前既定になります
			if( current.operation == 0) {
				current.value = storage_->load() ;
			} else {
				storage_->save( current.value) ;
			}
			// fakeを含む全storage出力を同じregistryで検証します
			if( !validTranscriptionPreferences( current.value)) {
				throw HistoryError( HistoryErrorKind::CORRUPT) ;
			}
			current.succeeded = true ;
		} catch( const HistoryError &error) {
			current.error = error.kind() ;
			current.code = error.code() ;
		} catch( ...) {
			current.error = HistoryErrorKind::INTERNAL ; // thread境界で固定診断へ変換
		}
		{
			std::scoped_lock lock( mutex_) ; // I/O後の小ACKを一件公開
			// 起動失敗は保存を許可せず原設定を保護します
			if( current.operation == 0) {
				ready_ = current.succeeded ;
			}
			busy_ = false ;
			event_ = current ;
		}
		notify() ;
		{
			std::unique_lock lock( mutex_) ; // UIではなく所有workerが待機
			condition_.wait( lock,
							//------------------------------------------------------------------------------
							/**
							 * @brief 待機終了条件だけを参照します
							 * @return 停止か要求あり
							 */
							//------------------------------------------------------------------------------
							[this]
							{
								return stopping_ || pending_.has_value() ;
							}) ;
			// 既に受けた一件だけはstop時も保存してから終わります
			if( pending_) {
				current = *pending_ ;
				pending_.reset() ;
			} else {
				break ;
			}
		}
	}
}
} // namespace koefude
