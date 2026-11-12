//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_application_resources.h
 * @brief 有界補正の同じ予算を無進捗区間と保存前終端まで所有します
 * @version 1.0.0
 * @date 2026/10/22
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/22 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/22 ADR1333の所有monitorと終端freezeを追加
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
#include "inference/whole_text_resource_guard.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <stop_token>

namespace koefude::detail {
/** @brief 同期borrowの照会と監視不能通知です。両呼出先はnoexceptを守ります */
struct WholeTextApplicationResourceTick {
	/// 同guardの資源照会だけ
	std::function<void()>			   sample ;
	/// 監視基盤の失敗、OS測定失敗とは別
	std::function<void( std::uint32_t)> unavailable ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同期照会を実行します。空は回収済みの人工値です
	 */
	//------------------------------------------------------------------------------
	void							   operator()() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 監視不能を最初の元codeで通知します
	 * @param[in] code 元非0code、0は31へ正規化します
	 */
	//------------------------------------------------------------------------------
	void							   fail( std::uint32_t code) const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 借用がまだ生存するかを返します
	 * @return 照会先があればtrue
	 * @anchor koefude_detail_whole_text_resource_tick_bool_contract
	 */
	//------------------------------------------------------------------------------
	explicit						   operator bool() const noexcept ;
} ;
/** @brief 一要求の監視だけを所有します。本文や保存portへ触れません */
class IWholeTextApplicationResourceMonitor {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有threadと借用tickを回収します
	 */
	//------------------------------------------------------------------------------
	virtual ~IWholeTextApplicationResourceMonitor() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 停止を要求し、進行中tickの同期returnまで待ちます
	 * @note tickのthreadから呼びません。return後のtick呼出しは禁止です
	 */
	//------------------------------------------------------------------------------
	virtual void stopAndJoin() noexcept = 0 ;
} ;
/** @brief 開始失敗時も生成途中の所有threadを回収するfactoryです */
using WholeTextApplicationResourceMonitorFactory =
	std::function<std::unique_ptr<IWholeTextApplicationResourceMonitor>( WholeTextApplicationResourceTick)> ;
//------------------------------------------------------------------------------
/**
 * @brief 100ms目標の停止可能待機と一jthreadを所有します
 * @param[in] tick noexceptの同期borrow、stopAndJoinまで有効
 * @return 監視owner
 * @throws std::system_error thread開始失敗
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::unique_ptr<IWholeTextApplicationResourceMonitor>
makeWholeTextApplicationResourceMonitor( WholeTextApplicationResourceTick tick) ;
/** @brief 元userと処理停止を分離し、一guardの監視終了と最終値を固定します */
class WholeTextApplicationResourceControl final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief baseline取得済みの同じguardだけを所有します
	 * @param[in] guard 非nullの共有予算
	 * @param[in] user 元利用者token
	 * @pre guardは非nullでbaseline取得済みです。controlのfinishは同じworkerだけが呼びます
	 */
	//------------------------------------------------------------------------------
	WholeTextApplicationResourceControl( std::shared_ptr<WholeTextResourceGuard> guard, std::stop_token user) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 例外経路でも借用tickがownerを越えて生存しないよう回収します
	 */
	//------------------------------------------------------------------------------
	~WholeTextApplicationResourceControl() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 一guardのmonitorとthisを借用するtick・停止callbackの寿命を複製しないため、copy構築を禁止します
	 * @note 名前のない第1引数（const WholeTextApplicationResourceControl &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	WholeTextApplicationResourceControl( const WholeTextApplicationResourceControl &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 一guardのmonitorとthisを借用するtick・停止callbackの寿命を複製しないため、copy代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const WholeTextApplicationResourceControl &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	WholeTextApplicationResourceControl	  &operator=( const WholeTextApplicationResourceControl &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 後段へ入る前に必須monitorを開始します
	 * @param[in] factory 人工依存も明示必須
	 * @throws OperationError 固定分類/code
	 * @pre 同じworkerで一回だけ呼び、成功後は同期処理終了後にfinishします
	 */
	//------------------------------------------------------------------------------
	void								   start( const WholeTextApplicationResourceMonitorFactory &factory) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief decoder/Whisperだけへ渡す処理tokenです
	 * @return user又は資源で停止
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::stop_token		   processingToken() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief native/PCM/context同期終了後にjoinし、同guardを最後に一回pollします
	 * @return 最初のsticky失敗。再呼出しはjoin/probeを増やしません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] WholeTextResourceFailure finish() noexcept ;

	private:
	/** @brief user停止を独立sourceへ転送します */
	struct ForwardUser {
		/// control内の長寿命owner
		std::stop_source *effective ;
		//------------------------------------------------------------------------------
		/**
		 * @brief guard排他を保持せず処理側を協調停止します
		 */
		//------------------------------------------------------------------------------
		void			  operator()() const noexcept ;
	} ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じguardだけをpollし、失敗なら処理停止を要求します
	 */
	//------------------------------------------------------------------------------
	void												  tick() noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 最初の監視不能だけをatomic保存して処理を止めます
	 * @param[in] code 元code。guard/本文/portへ触れません
	 */
	//------------------------------------------------------------------------------
	void												  monitorFailed( std::uint32_t code) noexcept ;
	/// baselineを作り直さない共有owner
	std::shared_ptr<WholeTextResourceGuard>				  guard_ ;
	/// 元利用者sourceを変更しない独立停止
	std::stop_source									  effective_ ;
	/// effective_より先に破棄
	std::stop_callback<ForwardUser>						  userCallback_ ;
	/// finishで回収
	std::unique_ptr<IWholeTextApplicationResourceMonitor> monitor_ ;
	/// join後のtickを拒否
	std::atomic<bool>									  finished_ = false ;
	/// 最初の監視基盤失敗、0は正常
	std::atomic<std::uint32_t>							  monitorCode_ = 0 ;
	/// workerだけがfinish後に読む小さい値
	WholeTextResourceFailure							  frozen_ ;
} ;
} // namespace koefude::detail
