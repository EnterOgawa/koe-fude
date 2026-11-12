//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_application_resources.cpp
 * @brief 同期補正の無進捗監視と保存前join・最終照会を実装します
 * @version 1.0.0
 * @date 2026/10/22
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/22 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/22 ADR1333の一thread監視と固定開始失敗を追加
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
#include "inference/whole_text_application_resources.h"
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <stop_token>
#include <system_error>
#include <thread>
#include <utility>
#include <windows.h>

namespace koefude::detail {
namespace {
/** @brief tickを所有threadの同期returnまで借用する実monitorです */
class ResourceMonitor final : public IWholeTextApplicationResourceMonitor {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 一jthreadだけを開始します
	 * @param[in] tick noexceptの同期処理
	 */
	//------------------------------------------------------------------------------
	explicit ResourceMonitor( WholeTextApplicationResourceTick tick)
		: thread_(
			  //------------------------------------------------------------------------------
			  /**
			   * @brief 待機基盤の失敗も所有controlへ通知して終了します
			   * @param[in] stop 所有workerからのmonitor停止
			   */
			  //------------------------------------------------------------------------------
			  [this, tick = std::move( tick)]( const std::stop_token stop) noexcept
			  {
				  try {
					  std::unique_lock lock( mutex_) ; // tick中は解放
					  // 所有stopまで100ms目標で待ち、本文や進捗には触れません
					  while( !stop.stop_requested()) {
						  static_cast<void>( wake_.wait_for( lock, stop, std::chrono::milliseconds( 100),
							  //------------------------------------------------------------------------------
							  /**
							   * @brief 停止tokenだけで解除します
							   * @return 常にfalse
							   */
							  //------------------------------------------------------------------------------
							  []
							  {
								  return false ;
							  })) ;
						  // 停止要求後の余分なtickを開始しません
						  if( stop.stop_requested()) {
							  break ;
						  }
						  lock.unlock() ;
						  tick() ; // controlがnoexceptを保証
						  lock.lock() ;
					  }
				  } catch( const std::bad_alloc &) {
					  tick.fail( ERROR_NOT_ENOUGH_MEMORY) ;
				  } catch( const std::system_error &error) {
					  tick.fail( static_cast<std::uint32_t>( error.code().value())) ;
				  } catch( ...) {
					  tick.fail( ERROR_GEN_FAILURE) ;
				  }
			  })
	{
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 借用tickより先に所有threadを回収します
	 */
	//------------------------------------------------------------------------------
	~ResourceMonitor() override
	{
		stopAndJoin() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 停止を要求し、進行中tickの同期returnまで待ちます
	 * @note tickのthreadから呼びません。return後のtick呼出しは禁止です
	 */
	//------------------------------------------------------------------------------
	void stopAndJoin() noexcept override
	{
		// 二度目の停止は再joinせず、終端後の照会を増やしません
		if( thread_.joinable()) {
			thread_.request_stop() ;
			thread_.join() ; // workerだけが呼び、guard/診断/待機mutexを保持しません
		}
	}

	private:
	/// 待機専用。thread開始前にworkerで構築します
	std::mutex					mutex_ ;
	/// 構築失敗はstart分類へ戻し、threadより後に破棄
	std::condition_variable_any wake_ ;
	/// 最後に構築、最初にjoin/破棄して待機資源の寿命を守ります
	std::jthread				thread_ ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 0codeを開始失敗の既定へ正規化します
 * @param[in] code 元32bit値
 * @return 非0の診断code
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::uint32_t startCode( const std::uint32_t code) noexcept
{
	return code == 0 ? ERROR_GEN_FAILURE : code ;
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 同期照会を実行します。空は回収済みの人工値です
 * @see koefude::detail::WholeTextApplicationResourceTick::operator()() const
 */
//------------------------------------------------------------------------------
void WholeTextApplicationResourceTick::operator()() const noexcept
{
	// 人工monitorが回収した空値からは照会しません
	if( sample) {
		sample() ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 監視不能を最初の元codeで通知します
 * @param[in] code 元非0code、0は31へ正規化します
 * @see koefude::detail::WholeTextApplicationResourceTick::fail
 */
//------------------------------------------------------------------------------
void WholeTextApplicationResourceTick::fail( const std::uint32_t code) const noexcept
{
	// 回収後の人工値には通知先もありません
	if( unavailable) {
		unavailable( code) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 借用がまだ生存するかを返します
 * @return 照会先があればtrue
 * @anchor koefude_detail_whole_text_resource_tick_bool_contract
 * @see \ref koefude_detail_whole_text_resource_tick_bool_contract "koefude::detail::WholeTextApplicationResourceTick::operator bool() const"
 */
//------------------------------------------------------------------------------
WholeTextApplicationResourceTick::operator bool() const noexcept
{
	return static_cast<bool>( sample) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 100ms目標の停止可能待機と一jthreadを所有します
 * @param[in] tick noexceptの同期borrow、stopAndJoinまで有効
 * @return 監視owner
 * @throws std::system_error thread開始失敗
 * @see koefude::detail::makeWholeTextApplicationResourceMonitor
 */
//------------------------------------------------------------------------------
std::unique_ptr<IWholeTextApplicationResourceMonitor>
makeWholeTextApplicationResourceMonitor( WholeTextApplicationResourceTick tick)
{
	return std::make_unique<ResourceMonitor>( std::move( tick)) ;
}
//------------------------------------------------------------------------------
/**
 * @brief baseline取得済みの同じguardだけを所有します
 * @param[in] guard 非nullの共有予算
 * @param[in] user 元利用者token
 * @pre guardは非nullでbaseline取得済みです。controlのfinishは同じworkerだけが呼びます
 * @see koefude::detail::WholeTextApplicationResourceControl::WholeTextApplicationResourceControl(std::shared_ptr< koefude::WholeTextResourceGuard >, std::stop_token)
 */
//------------------------------------------------------------------------------
WholeTextApplicationResourceControl::WholeTextApplicationResourceControl( std::shared_ptr<WholeTextResourceGuard> guard,
																		 const std::stop_token					 user)
	: guard_( std::move( guard)), userCallback_( user, ForwardUser{&effective_})
{
}
//------------------------------------------------------------------------------
/**
 * @brief 例外経路でも借用tickがownerを越えて生存しないよう回収します
 * @see koefude::detail::WholeTextApplicationResourceControl::~WholeTextApplicationResourceControl
 */
//------------------------------------------------------------------------------
WholeTextApplicationResourceControl::~WholeTextApplicationResourceControl()
{
	static_cast<void>( finish()) ;
}
//------------------------------------------------------------------------------
/**
 * @brief guard排他を保持せず処理側を協調停止します
 * @see koefude::detail::WholeTextApplicationResourceControl::ForwardUser::operator()() const
 */
//------------------------------------------------------------------------------
void WholeTextApplicationResourceControl::ForwardUser::operator()() const noexcept
{
	effective->request_stop() ;
}
//------------------------------------------------------------------------------
/**
 * @brief 後段へ入る前に必須monitorを開始します
 * @param[in] factory 人工依存も明示必須
 * @throws OperationError 固定分類/code
 * @pre 同じworkerで一回だけ呼び、成功後は同期処理終了後にfinishします
 * @see koefude::detail::WholeTextApplicationResourceControl::start
 */
//------------------------------------------------------------------------------
void WholeTextApplicationResourceControl::start( const WholeTextApplicationResourceMonitorFactory &factory)
{
	// 空依存は実threadへ暗黙fallbackせず、後段より先に拒否します
	if( !factory) {
		throw OperationError( ErrorKind::INTERNAL, ERROR_INVALID_FUNCTION) ;
	}
	try {
		monitor_ = factory( {
							//------------------------------------------------------------------------------
							/**
							 * @brief 同guard照会だけを同期実行します
							 */
							//------------------------------------------------------------------------------
							[this]() noexcept
							{
								tick() ;
							},
							//------------------------------------------------------------------------------
							/**
							 * @brief 監視基盤失敗を本文なしで通知します
							 * @param[in] code 元code
							 */
							//------------------------------------------------------------------------------
							[this]( std::uint32_t code) noexcept
							{
								monitorFailed( code) ;
							}}) ;
	} catch( const OperationError &error) {
		throw OperationError( ErrorKind::INTERNAL, startCode( error.code())) ;
	} catch( const std::bad_alloc &) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE, ERROR_NOT_ENOUGH_MEMORY) ;
	} catch( const std::system_error &error) {
		throw OperationError( ErrorKind::INTERNAL, startCode( static_cast<std::uint32_t>( error.code().value()))) ;
	} catch( ...) {
		throw OperationError( ErrorKind::INTERNAL, ERROR_GEN_FAILURE) ;
	}
	// null返却も開始成立とは扱いません
	if( !monitor_) {
		throw OperationError( ErrorKind::INTERNAL, ERROR_INVALID_FUNCTION) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief decoder/Whisperだけへ渡す処理tokenです
 * @return user又は資源で停止
 * @see koefude::detail::WholeTextApplicationResourceControl::processingToken
 */
//------------------------------------------------------------------------------
std::stop_token WholeTextApplicationResourceControl::processingToken() const noexcept
{
	return effective_.get_token() ;
}
//------------------------------------------------------------------------------
/**
 * @brief 同じguardだけをpollし、失敗なら処理停止を要求します
 * @see koefude::detail::WholeTextApplicationResourceControl::tick
 */
//------------------------------------------------------------------------------
void WholeTextApplicationResourceControl::tick() noexcept
{
	// join/freeze後はfakeの遅延通知からも資源照会を始めません
	if( !finished_.load()) {
		const bool failed = guard_->poll( true) ; // pollの内部排他はreturn前に解放
		// user callbackをguardの内部排他保持中に実行しません
		if( failed) {
			effective_.request_stop() ;
		}
	}
}
//------------------------------------------------------------------------------
/**
 * @brief native/PCM/context同期終了後にjoinし、同guardを最後に一回pollします
 * @return 最初のsticky失敗。再呼出しはjoin/probeを増やしません
 * @see koefude::detail::WholeTextApplicationResourceControl::finish
 */
//------------------------------------------------------------------------------
WholeTextResourceFailure WholeTextApplicationResourceControl::finish() noexcept
{
	// 同期workerだけがfinishを呼び、二度目は確定値だけを返します
	if( finished_.load()) {
		return frozen_ ;
	}
	// native callbackの同期終了後、進行中の最後のprobeをmutex外で回収します
	if( monitor_) {
		monitor_->stopAndJoin() ;
		monitor_.reset() ;
	}
	finished_.store( true) ;
	static_cast<void>( guard_->poll( true)) ; // polling_競合のない同guardで最後に一回
	frozen_ = guard_->failure() ;
	// 実guardのsticky理由を優先し、なければ監視基盤の測定不能を別codeで保持します
	if( frozen_.reason == 0 && monitorCode_.load() != 0) {
		frozen_ = {2, monitorCode_.load()} ;
	}
	return frozen_ ;
}
//------------------------------------------------------------------------------
/**
 * @brief 最初の監視不能だけをatomic保存して処理を止めます
 * @param[in] code 元code。guard/本文/portへ触れません
 * @see koefude::detail::WholeTextApplicationResourceControl::monitorFailed
 */
//------------------------------------------------------------------------------
void WholeTextApplicationResourceControl::monitorFailed( const std::uint32_t code) noexcept
{
	// join後に確定した値を監視不能通知から上書きしません
	if( !finished_.load()) {
		std::uint32_t expected = 0 ; // 最初の非0codeだけを採用
		monitorCode_.compare_exchange_strong( expected, startCode( code)) ;
		effective_.request_stop() ; // guard/診断のmutexを保持しません
	}
}
} // namespace koefude::detail
