//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file parakeet_api_followup.h
 * @brief 新しいParakeet原文の案内と一sessionの明示続行を有限に保持します
 * @version 1.0.0
 * @date 2026/11/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1376のUIなしの案内/続行stateを追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415のoperator括弧と文書lambdaの書式を補修
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/pipeline.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <utility>

namespace koefude {
/** @brief 本文をcopy/hashしない、一つの明示操作の不変対象です */
struct ParakeetApiTarget {
	/// 行選択と独立した履歴ID
	std::uint64_t					  history = 0 ;
	/// 同IDの原文置換を拒否するowner
	std::shared_ptr<const Transcript> source ;
	/// この原文ownerの履歴保存ACKだけでtrue
	bool							  saved = false ;
} ;
/** @brief 開いている設定内で一度だけ消費する、非永続の送信意図です */
struct ParakeetApiContinuation {
	/// 「改善する」で確定した同じ原文
	ParakeetApiTarget target ;
	/// close/reopenで異なる設定session
	std::uint64_t	  session = 0 ;
	/// 単調時計の開始値
	std::uint64_t	  startedMs = 0 ;
} ;
/** @brief UI threadだけで最大32件の案内と一件の明示続行を保持します。通信/I/Oは行いません */
class ParakeetApiFollowup final {
	public:
	/// 製品の履歴上限と同じ有限量
	static constexpr std::size_t   K_MAXIMUM_OFFERS = 32 ;
	/// 設定内の明示意図だけの期限
	static constexpr std::uint64_t K_LIFETIME_MS = 10 * 60 * 1000 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 新しい成功原文だけを登録します
	 * @param[in] target 同じ受付の所有値
	 */
	//------------------------------------------------------------------------------
	void						   offer( ParakeetApiTarget target)
	{
		// 本文なし・不正ID・同じ行の重複と上限超過を予約へ変えません
		if( !target.history || !target.source || target.source->lines.empty() || offers_.size() >= K_MAXIMUM_OFFERS ||
		   std::any_of( offers_.begin(), offers_.end(),
					   //------------------------------------------------------------------------------
					   /**
					    * @brief 同じ行の登録を照合します
					    * @param[in] value 候補
					    * @return 一致
					    */
					   //------------------------------------------------------------------------------
					   [&]( const auto &value)
					   {
						   return value.history == target.history ;
					   })) {
			return ;
		}
		offers_.push_back( std::move( target)) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ保存ownerだけを案内可能にします
	 * @param[in] history 保存ID
	 * @param[in] source writerがACKした原文owner
	 */
	//------------------------------------------------------------------------------
	void acknowledge( std::uint64_t history, const std::shared_ptr<const Transcript> &source)
	{
		// 最大32件を一巡し、他原文のACKを流用しません
		for( auto &value : offers_) {
			// 同じ行でも別原文は未保存のまま保持します
			if( value.history == history && value.source == source) {
				value.saved = true ;
			}
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 一件をmodal前にqueueから外します
	 * @return 保存済み先頭、未保存/空はなし
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::optional<ParakeetApiTarget> take()
	{
		// 先頭の原文保存ACK前に次の案内へ追い越しません
		if( offers_.empty() || !offers_.front().saved || prompt_) {
			return std::nullopt ;
		}
		prompt_ = std::move( offers_.front()) ;
		offers_.pop_front() ;
		return prompt_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief modal中の削除/手動Improveによる消費も照合します
	 * @param[in] improve 明示選択
	 * @return 未失効の同意対象、辞退/失効はなし
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::optional<ParakeetApiTarget> answer( bool improve)
	{
		auto value = std::exchange( prompt_, std::nullopt) ; // 一度消費してから外部処理へ渡す
		return improve ? value : std::nullopt ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ行の未実行意図をすべて失効します
	 * @param[in] history 削除/新要求の元ID
	 */
	//------------------------------------------------------------------------------
	void cancel( std::uint64_t history)
	{
		std::erase_if( offers_,
					  //------------------------------------------------------------------------------
					  /**
					   * @brief 対象行だけを除きます
					   * @param[in] value 候補
					   * @return 同ID
					   */
					  //------------------------------------------------------------------------------
					  [&]( const auto &value)
					  {
						  return value.history == history ;
					  }) ;
		// 表示中のmodalも、戻り値から送信を再開しません
		if( prompt_ && prompt_->history == history) {
			prompt_.reset() ;
		}
		// すでに別行へ移った意図は変更しません
		if( continuation_ && continuation_->target.history == history) {
			continuation_.reset() ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief main close/全削除ではsession内の意図をすべて破棄します
	 */
	//------------------------------------------------------------------------------
	void clear() noexcept
	{
		offers_.clear() ;
		prompt_.reset() ;
		continuation_.reset() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 有限待ち件数を返します
	 * @return 最大32
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::size_t size() const noexcept
	{
		return offers_.size() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 古いownerを有限queueから除きます
	 * @tparam Predicate UIなしの照合
	 * @param[in] valid 同期中だけtargetを借用し、有効ならtrue
	 */
	//------------------------------------------------------------------------------
	template <class Predicate> void discardStale( Predicate valid)
	{
		std::erase_if( offers_,
					  //------------------------------------------------------------------------------
					  /**
					   * @brief 失効した原文だけを破棄します
					   * @param[in] value 候補
					   * @return 古いowner
					   */
					  //------------------------------------------------------------------------------
					  [&]( const auto &value)
					  {
						  return !valid( value) ;
					  }) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 一件の設定内意図を所有します
	 * @param[in] value 同意済みの同session対象
	 */
	//------------------------------------------------------------------------------
	void wait( ParakeetApiContinuation value)
	{
		continuation_ = std::move( value) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief UI内でだけ現在の意図を参照します
	 * @return 非所有、次のstate変更まで有効
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const std::optional<ParakeetApiContinuation> &pending() const noexcept
	{
		return continuation_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 設定内意図を実行/失効の前に一度だけ消費します
	 */
	//------------------------------------------------------------------------------
	void finish() noexcept
	{
		continuation_.reset() ;
	}

	private:
	/// 同時受付上限内だけ
	std::deque<ParakeetApiTarget>		   offers_ ;
	/// modal中の取消し照合用の所有値
	std::optional<ParakeetApiTarget>	   prompt_ ;
	/// 同sessionの資格を照合する一件
	std::optional<ParakeetApiContinuation> continuation_ ;
} ;
/** @brief 自processのmodal待ち中に案内を再入させないscopeです。UI thread専用 */
class FollowupModalScope final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 同期scopeを開始します
	 * @param[in,out] depth UI所有の生存するcounter
	 */
	//------------------------------------------------------------------------------
	explicit FollowupModalScope( unsigned &depth) noexcept : depth_( depth)
	{
		++depth_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief modalから戻ると深さを元へ戻します
	 */
	//------------------------------------------------------------------------------
	~FollowupModalScope() noexcept
	{
		--depth_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief scopeの重複解放を禁止します
	 * @note 名前のない第1引数（const FollowupModalScope &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	FollowupModalScope( const FollowupModalScope &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief scopeの重複解放を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const FollowupModalScope &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	FollowupModalScope &operator=( const FollowupModalScope &) = delete ;

	private:
	/// 同期関数の呼出元が所有するcounter
	unsigned &depth_ ;
} ;
} // namespace koefude
