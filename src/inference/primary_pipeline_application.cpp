//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file primary_pipeline_application.cpp
 * @brief 一開始snapshotと最終解放までの主処理RAM監視を実装します
 * @version 1.0.0
 * @date 2026/10/21
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/21 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1316の通常factory・実効停止・最終評価を追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "inference/primary_pipeline_application.h"
#include "inference/local_engines.h"
#include "platform/audio_decoder.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>

namespace koefude {
namespace {

/** @brief 目標100msでtickし、callbackの借用終了をjoinで保証します */
class ResourceMonitor final : public detail::IPipelineResourceMonitor {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 一つの所有threadを開始します
	 * @param[in] tick controlが生存中だけ使うnoexcept通知
	 */
	//------------------------------------------------------------------------------
	explicit ResourceMonitor( detail::PipelineResourceTick tick)
		: thread_(
			  //------------------------------------------------------------------------------
			  /**
			   * @brief 停止要求または100ms待機の完了で監視を進めます
			   * @param[in] stop この監視threadだけの停止token
			   */
			  //------------------------------------------------------------------------------
			  [this, tick = std::move( tick)]( const std::stop_token stop)
			  {
				  std::unique_lock lock( waitMutex_) ; // tick実行時には保持しない待機専用排他
				  // 停止まで有限間隔で繰り返し、native無進捗中も同じguardを評価します
				  while( !stop.stop_requested()) {
					  static_cast<void>( wake_.wait_for( lock, stop, std::chrono::milliseconds( 100),
						  //------------------------------------------------------------------------------
						  /**
						   * @brief 停止通知かtimeoutだけで待機を終えます
						   * @return 通常通知で成立する条件はありません
						   */
						  //------------------------------------------------------------------------------
						  []
						  {
							  return false ;
						  })) ;
					  // 停止後に新しいtickを開始しません
					  if( !stop.stop_requested()) {
						  lock.unlock() ;
						  tick() ;
						  lock.lock() ;
					  }
				  }
			  })
	{
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 借用callbackが残らないよう停止・joinします
	 */
	//------------------------------------------------------------------------------
	~ResourceMonitor() override
	{
		stopAndJoin() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 停止を要求し、進行中の最後のtickも完了させます
	 * @note noexcept。返却後のcallback呼出しは禁止します
	 */
	//------------------------------------------------------------------------------
	void stopAndJoin() noexcept override
	{
		thread_.request_stop() ;
		wake_.notify_all() ;
		// 二回目の呼出しではjoin済み所有物へ触れません
		if( thread_.joinable()) {
			thread_.join() ;
		}
	}

	private:
	/// probe用とは独立した待機排他
	std::mutex					waitMutex_ ;
	/// 停止時に100ms待機を解除する通知
	std::condition_variable_any wake_ ;
	/// 上の待機所有物より先に解放する監視thread
	std::jthread				thread_ ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief ガード構築由来のResultTooLargeだけをprimary分類へ変換します
 * @param[in] probe 必須の同期probe
 * @param[in] stop 元user取消し
 * @return 一回のbaselineを持つ所有guard
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::unique_ptr<WholeTextResourceGuard> makePrimaryGuard( const WholeTextResourceProbe &probe,
																	   const std::stop_token		 stop)
{
	try {
		return std::make_unique<WholeTextResourceGuard>( probe, WholeTextResourcePolicy::LOW_MEMORY16_GB) ;
	} catch( const OperationError &error) {
		// 初回照会の失敗と同時でも元user取消しを正本にします
		if( stop.stop_requested()) {
			throw OperationError( ErrorKind::CANCELLED, error.code()) ;
		}
		throw OperationError( ErrorKind::INPUT_TOO_LARGE, error.code()) ;
	}
}

/** @brief joinまでguard・probe・停止sourceの寿命を一つにまとめます */
class PrimaryResourceControl final : public IPipelineResourceControl {
	public:
	/** @brief 元user取消しを別の実効sourceへ転送する借用関数です */
	struct UserStopForwarder {
		/// callbackより長寿命の実効source
		std::stop_source &effective ;
		//------------------------------------------------------------------------------
		/**
		 * @brief 元user取消しだけを実効停止へ転送します
		 */
		//------------------------------------------------------------------------------
		void			  operator()() const noexcept
		{
			effective.request_stop() ;
		}
	} ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 入力I/O前の唯一のbaselineと停止転送を確立します
	 * @param[in,out] services 終了まで生存するprobe・observer・monitor factory
	 * @param[in] user 元user取消し
	 */
	//------------------------------------------------------------------------------
	PrimaryResourceControl( detail::PrimaryPipelineServices &services, const std::stop_token user)
		: guard_( makePrimaryGuard( services.resourceProbe, user)), user_( user),
		  userCallback_( user, UserStopForwarder{effective_}), services_( services)
	{
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 例外経路でも同じ最終評価を完了します
	 */
	//------------------------------------------------------------------------------
	~PrimaryResourceControl() override
	{
		static_cast<void>( finish()) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief monitorを必ず成立させ、無監視の処理を拒否します
	 */
	//------------------------------------------------------------------------------
	void start()
	{
		monitor_ = services_.startMonitor(
			//------------------------------------------------------------------------------
			/**
			 * @brief native無進捗中も同じguardを安全に評価します
			 */
			//------------------------------------------------------------------------------
			[this]() noexcept
			{
				poll( true) ;
			}) ;
		// nullを返す依存ではdecoderへ進みません
		if( !monitor_) {
			throw OperationError( ErrorKind::INTERNAL, 1) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 元user取消しと資源停止を受ける実効tokenです
	 * @return 同期呼出しの全処理と既存retryへ渡すtoken
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::stop_token processingStop() const noexcept override
	{
		return effective_.get_token() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 進捗や監視callbackから待たずに資源を確認します
	 * @note nativeのC callbackへ例外を送出しません。通知先は排他外で呼びます
	 */
	//------------------------------------------------------------------------------
	void pollProgress() noexcept override
	{
		poll( false) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief monitorの実tickと進捗のthrottleを同じ排他へ合流します
	 * @param[in] force 100ms周期のmonitor tickは一回の評価を必ず試します
	 */
	//------------------------------------------------------------------------------
	void poll( const bool force) noexcept
	{
		PipelineResourceFailure current ; // 排他外で要求・公開する停止値
		{
			std::unique_lock lock( probeMutex_, std::try_to_lock) ; // callbackは待ちません
			// 競合中は強制境界側へ任せ、finalize後はprobeを行いません
			if( !lock.owns_lock() || frozen_) {
				return ;
			}
			// 外側排他で直列化し、既存guardの100ms内部throttleも保持します
			static_cast<void>( guard_->poll( force)) ;
			current = guardFailure() ;
		}
		publish( current) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 元user取消しを優先し、同じ予算を強制評価します
	 * @param[in] boundary 同期所有権の境界
	 * @param[in] nativeCode 非0なら元native診断を例外codeへ保持します
	 * @throws OperationError 取消し、またはガード由来だけのInputTooLarge
	 */
	//------------------------------------------------------------------------------
	void checkpoint( const PipelineMemoryBoundary boundary, const std::uint32_t nativeCode = 0) override
	{
		// 元user停止はprobeや資源失敗より常に優先します
		if( user_.stop_requested()) {
			throw OperationError( ErrorKind::CANCELLED, nativeCode) ;
		}
		PipelineResourceFailure current ; // 短いprobe排他の外へ運ぶ所有値
		{
			std::lock_guard lock( probeMutex_) ; // 強制境界は競合の完了を待って照会します
			// finish後に追加照会せず、保存済み停止値を再利用します
			if( !frozen_) {
				static_cast<void>( guard_->poll( true)) ;
			}
			current = guardFailure() ;
		}
		publish( current) ;
		// 照会中の元user停止も資源分類へ変換しません
		if( user_.stop_requested()) {
			throw OperationError( ErrorKind::CANCELLED, nativeCode) ;
		}
		// ガード由来だけをprimaryの利用可能RAM分類へ変換します
		if( current.reason != PipelineResourceStopReason::NONE) {
			throw OperationError( ErrorKind::INPUT_TOO_LARGE, nativeCode != 0 ? nativeCode : current.code) ;
		}
		observe( boundary) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 実効停止要求の完了後に公開された最初の資源失敗を読みます
	 * @return 未停止ならNone。所有値なので返却後も有効です
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] PipelineResourceFailure failure() const noexcept override
	{
		const auto value = published_.load() ; // effective要求後のreason/codeを不可分に取得
		return {static_cast<PipelineResourceStopReason>( static_cast<std::uint32_t>( value)),
				static_cast<std::uint32_t>( value >> 32)} ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief monitorを停止・joinし、最後の強制評価を一度だけ確定します
	 * @return 保存済みの最初の失敗。再呼出しは再join・再probeしません
	 * @note native・PCM・Recognition解放後に呼びます。例外経路も同じ寿命を守ります
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] PipelineResourceFailure finish() noexcept override
	{
		// 同じworkerの再呼出しはjoin・probe・観測を繰り返しません
		if( finished_) {
			return failure() ;
		}
		// joinをprobe排他の外で待ち、進行中の最後のtickを受け取ります
		if( monitor_) {
			monitor_->stopAndJoin() ;
		}
		PipelineResourceFailure current ; // join後の最後の評価結果
		{
			std::lock_guard lock( probeMutex_) ; // callback終了後の一回の強制評価
			static_cast<void>( guard_->poll( true)) ;
			current = guardFailure() ;
			frozen_ = true ;
		}
		publish( current) ;
		finished_ = true ;
		observe( PipelineMemoryBoundary::AFTER_CLEANUP_FINALIZED) ;
		return failure() ;
	}

	private:
	//------------------------------------------------------------------------------
	/**
	 * @brief 既存ガードの小さい停止値をOS非依存型へ写します
	 * @return ガードの最初のreason/code
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] PipelineResourceFailure guardFailure() const noexcept
	{
		const auto value = guard_->failure() ; // 既存guardが保持する最初の理由
		return {static_cast<PipelineResourceStopReason>( value.reason), value.code} ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 実効停止を完了してから失敗値を公開します
	 * @param[in] current ガードから読んだ所有値
	 */
	//------------------------------------------------------------------------------
	void publish( const PipelineResourceFailure current) noexcept
	{
		// 正常標本では停止sourceを変更しません
		if( current.reason != PipelineResourceStopReason::NONE) {
			effective_.request_stop() ;
			published_.store( (static_cast<std::uint64_t>( current.code) << 32) |
							 static_cast<std::uint32_t>( current.reason)) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 取得済み値だけを排他外で受動通知します
	 * @param[in] boundary 完了した同期境界
	 * @note observerは例外を送出しない契約です。tick/最終評価の追加照会を行いません
	 */
	//------------------------------------------------------------------------------
	void observe( const PipelineMemoryBoundary boundary) const noexcept
	{
		// 空observerでは観測用の割当てや照会を行いません
		if( services_.observe) {
			services_.observe(
				{boundary, guard_->baseline(), guard_->workingLimit(), guard_->privateLimit(), failure()}) ;
		}
	}

	/// 全所有物とmonitorより長寿命の一guard
	std::unique_ptr<WholeTextResourceGuard>			  guard_ ;
	/// 元user取消しの正本
	std::stop_token									  user_ ;
	/// RAM停止でuser側を書き換えない独立source
	std::stop_source								  effective_ ;
	/// effective解放前に解除する転送
	std::stop_callback<UserStopForwarder>			  userCallback_ ;
	/// 同期中生存する必須依存
	detail::PrimaryPipelineServices					 &services_ ;
	/// guardへの照会を直列化する短い排他
	std::mutex										  probeMutex_ ;
	/// probe排他内だけで使う照会終了状態
	bool											  frozen_ = false ;
	/// workerだけが呼ぶexactly-once finalization
	bool											  finished_ = false ;
	/// 停止要求後のreason/code
	std::atomic<std::uint64_t>						  published_ = 0 ;
	/// 最初に停止・解放する所有物
	std::unique_ptr<detail::IPipelineResourceMonitor> monitor_ ;
} ;

static_assert( std::is_nothrow_move_assignable_v<Transcript>) ;
static_assert( std::is_nothrow_constructible_v<std::shared_ptr<const Transcript>, const std::shared_ptr<Transcript> &>) ;

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 同じ16GB guardの所有scopeで音声APIを実行します
 * @param[in] task 同期中だけcontrolを借用する処理
 * @param[in] stop 元user取消し
 * @param[in] probe 空なら通常OS probe
 * @throws OperationError guard成立前の資源失敗
 * @see koefude::detail::runPrimaryResourceTask
 */
//------------------------------------------------------------------------------
void detail::runPrimaryResourceTask( const std::function<void( IPipelineResourceControl &)> &task, std::stop_token stop,
									const WholeTextResourceProbe &probe)
{
	LocalAsrEngine			unused ; // guard依存契約のadapter。API処理からは呼びません
	SherpaDiarizer			diarizer ; // guardの依存holderだけ
	PrimaryPipelineServices services{decodeInputAudio,
									 unused,
									 diarizer,
									 probe ? probe : WholeTextResourceProbe{probeWholeTextResources},
									 //------------------------------------------------------------------------------
									 /**
									  * @brief 同じ100ms monitorを所有します
									  * @param[in] tick guard通知
									  * @return 所有monitor
									  */
									 //------------------------------------------------------------------------------
									 []( PipelineResourceTick tick)
									 {
										 return std::make_unique<ResourceMonitor>( std::move( tick)) ;
									 },
									 {}} ;
	PrimaryResourceControl	control( services, stop) ; // 既存と同じbaseline/working/private予算
	control.start() ;
	task( control) ; // PCM/native解放とfinish、typed終端保存は同じ同期taskが順序を守ります
}

//------------------------------------------------------------------------------
/**
 * @brief 通常入口と同じguard・core・結果返却を注入依存で実行します
 * @param[in] input 入力path
 * @param[in] options 固定設定
 * @param[in,out] services 同期終了まで有効な必須依存と任意の受動観測
 * @param[in] stop 元user取消し
 * @param[in] progress 任意の同期通知先
 * @return 事前確保したholderで所有するconst共有結果
 * @throws OperationError 通常入口と同じ失敗。bad_allocはInputTooLarge/8です
 * @see koefude::detail::runPrimaryPipelineWithServices
 */
//------------------------------------------------------------------------------
std::shared_ptr<const Transcript> detail::runPrimaryPipelineWithServices( const std::filesystem::path &input,
																		 const EngineOptions		 &options,
																		 PrimaryPipelineServices	 &services,
																		 const std::stop_token		  stop,
																		 const ProgressCallback		 &progress)
{
	checkCancelled( stop) ;
	try {
		// 必須依存が欠けた場合はbaselineや入力I/Oへ進みません
		if( !services.decoder || !services.resourceProbe || !services.startMonitor) {
			throw OperationError( ErrorKind::INTERNAL, 1) ;
		}
		PrimaryResourceControl control( services, stop) ; // decoder前の唯一のbaseline
		const ProgressCallback guardedProgress =
			//------------------------------------------------------------------------------
			/**
			 * @brief 通知先が空でも資源をpollし、user通知は排他外で呼びます
			 * @param[in] value 既存処理が確定した進捗
			 */
			//------------------------------------------------------------------------------
			[&control, &progress]( const Progress value)
			{
				control.pollProgress() ;
				// 呼出し元に通知先がある場合だけ既存進捗を転送します
				if( progress) {
					progress( value) ;
				}
			} ;
		control.start() ;
		auto holder = std::make_shared<Transcript>() ; // decode前に確保する唯一の共有holder
		detail::runPipelineInto( *holder, input, options, services.decoder, services.transcriber, services.diarizer,
								stop, guardedProgress, &control) ;
		// finalization後は確保済みholderの共有const化だけで返します
		checkCancelled( stop) ;
		return holder ;
	} catch( const std::bad_alloc &) {
		// holderを含む成立前の真確保失敗を、成功や救済本文へ変換しません
		if( stop.stop_requested()) {
			throw OperationError( ErrorKind::CANCELLED, 8) ;
		}
		throw OperationError( ErrorKind::INPUT_TOO_LARGE, 8) ;
	} catch( const OperationError &error) {
		// monitor開始や解放中に競合した元user取消しも最後まで優先します
		if( stop.stop_requested()) {
			throw OperationError( ErrorKind::CANCELLED, error.code()) ;
		}
		throw ;
	} catch( ...) {
		// thread開始等の未知例外も入力開始前に失敗として返します
		if( stop.stop_requested()) {
			throw OperationError( ErrorKind::CANCELLED) ;
		}
		throw OperationError( ErrorKind::INTERNAL) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief UIとCLIで同じ16GB向け工学予算を適用します
 * @param[in] input 通常decoderへ渡すlocal入力path
 * @param[in] options 受付時に固定した設定。モデルやGPU方針を変更しません
 * @param[in] stop 元user取消し。資源停止はこのsourceを変更しません
 * @param[in] progress 同期worker通知先。空可
 * @param[in] decoder 任意の同期decoder。空なら通常decoderを使います
 * @return PCMとnative所有物を解放したconst共有結果
 * @throws OperationError 取消し、入力・推論失敗、またはRAM予算不足
 * @note 割当てhard limitや搭載16GB実機受入を保証するAPIではありません
 * @see koefude::runPrimaryPipelineJob
 */
//------------------------------------------------------------------------------
std::shared_ptr<const Transcript> runPrimaryPipelineJob( const std::filesystem::path &input,
														const EngineOptions &options, const std::stop_token stop,
														const ProgressCallback &progress, const AudioDecoder &decoder)
{
	checkCancelled( stop) ;
	LocalAsrEngine					transcriber ; // native contextを載せない軽い同期adapter
	SherpaDiarizer					diarizer ; // ASR後にだけ初期化する話者adapter
	detail::PrimaryPipelineServices services{decoder ? decoder : AudioDecoder{decodeInputAudio},
											 transcriber,
											 diarizer,
											 probeWholeTextResources,
											 //------------------------------------------------------------------------------
											 /**
											  * @brief 通常製品の一監視threadを所有して開始します
											  * @param[in] tick 同期controlを借用するnoexcept通知
											  * @return stopAndJoinで借用を終える所有monitor
											  */
											 //------------------------------------------------------------------------------
											 []( detail::PipelineResourceTick tick)
											 {
												 return std::make_unique<ResourceMonitor>( std::move( tick)) ;
											 },
											 {}} ; // factory以外へOS依存を広げません
	return detail::runPrimaryPipelineWithServices( input, options, services, stop, progress) ;
}

} // namespace koefude
