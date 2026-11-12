//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file ctc_terms.h
 * @brief 固定語彙の本文へ明示用語を照合する有界な契約を定義します
 * @version 1.0.0
 * @date 2026/09/12
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/12 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/12 本文を変えない用語照合と上限付き補助点を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415のoperator括弧と文書lambdaの書式を補修
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace koefude {

constexpr std::size_t K_MAXIMUM_CTC_TERMS = 64 ; // 同じ入力窓で追跡する明示用語数
constexpr std::size_t K_MAXIMUM_CTC_TERM_BYTES = 192 ; // UTF-8での一用語のbyte上限
constexpr std::size_t K_MAXIMUM_CTC_TERM_TOTAL_BYTES = 4096 ; // 全用語と状態数を制限するbyte上限
constexpr std::size_t K_MAXIMUM_CTC_TERM_WORK = 32'000'000 ; // 一窓で照合する本文byte数の上限

/** @brief 一つの本文prefixが所有する、用語の進行状態です */
struct CtcTermState {
	/// 有界な接尾prefix状態の番号。0はroot
	std::uint32_t node = 0 ;
	/// 一度以上完成した用語IDのbit集合
	std::uint64_t completed = 0 ;
} ;

/** @brief 音響確率から分離して記録する用語の補助点です */
struct CtcTermScore {
	/// 完全一致した異なる語への加点。全体で8以下
	double completed = 0 ;
	/// 探索中だけ使う未完成語の先読み。最終採点には使わない
	double partial = 0 ;
} ;

/** @brief 固定語彙で用語を表現できることを示す一つの所有診断列です */
struct CtcTermSpelling {
	/// 正規化しない明示UTF-8用語
	std::string				  text ;
	/// 最少個数、同率はID辞書順の最大64非blank token
	std::vector<std::int32_t> tokens ;
} ;

/**
 * @brief 明示用語と固定語彙をcopyし、本文だけを照合するimmutable contextです
 * @note 推論workerが所有します。用語一致は音声中の存在や正解を保証しません
 */
class CtcTermContext final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 語彙での表現可能性と容量を検証し、用語状態を構築します
	 * @param[in] pieces ID順のUTF-8本文piece。語頭記号は既存decoderと同じ空白へ変換済み
	 * 2～8192語、各256byte以下。構築中だけ借用し内部へcopyします
	 * @param[in] blank pieces内のblank ID。blankと予約tokenは用語の表現に使いません
	 * @param[in] terms 最大64語、1語192byte、合計4096byte以下。空集合は無効化です
	 * @param[in] boost 一語一回の加点。有限な0～4。0は音響順位を変更しません
	 * @param[in] stop 構築中の取消し。失敗時はcontextを返しません
	 * @throws OperationError 不正語彙/用語、表現不能、上限超過、取消し
	 */
	//------------------------------------------------------------------------------
	CtcTermContext( std::span<const std::string> pieces, std::int32_t blank, std::span<const std::string> terms,
				   double boost, std::stop_token stop = {}) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有語彙と用語状態を解放します
	 */
	//------------------------------------------------------------------------------
	~CtcTermContext() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 大きな状態の暗黙copyを禁止します
	 * @param[in] other 複製元
	 */
	//------------------------------------------------------------------------------
	CtcTermContext( const CtcTermContext &other) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有状態の暗黙代入を禁止します
	 * @param[in] other 代入元
	 * @return 利用不可
	 */
	//------------------------------------------------------------------------------
	CtcTermContext								  &operator=( const CtcTermContext &other) = delete ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 新しく発火した一tokenの本文pieceだけ用語状態を進めます
	 * @param[in] state 同じcontextで得たprefix状態
	 * @param[in] token 語彙内の非blank ID。同token継続やblank frameには呼びません
	 * @return 新しい状態。元の本文・tokenは変更しません
	 * @throws OperationError 状態/ID不正。予約tokenは進行をrootへ戻します
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] CtcTermState					   advance( CtcTermState state, std::int32_t token) const ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 完成済みの加点と、一時的な先読み点を分けて返します
	 * @param[in] state 同じcontextで得たprefix状態
	 * @return 合計8以下の補助点。最終順位にはcompletedだけを使用します
	 * @throws OperationError 状態番号または用語IDの不正
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] CtcTermScore					   score( CtcTermState state) const ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 語彙同一性の件数境界を返します
	 * @return blankを含む固定語彙数
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::size_t					   vocabularySize() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 語彙同一性のblank境界を返します
	 * @return 固定blank ID
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::int32_t					   blank() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 無効化と測定条件を照合します
	 * @return 一語の明示加点。0～4
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] double						   boost() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 照合量の計算に使うpiece長を返します
	 * @param[in] token 非blank ID
	 * @return 所有本文pieceのbyte数
	 * @throws OperationError ID不正
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::size_t					   pieceBytes( std::int32_t token) const ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 診断用の表現例を借用します
	 * @return 本contextの寿命内で有効なimmutable列
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::span<const CtcTermSpelling> spellings() const noexcept ;

	private:
	class Impl ; // byte状態と語彙の実装を公開契約から分離する型
	/// 同じworkerで所有するimmutable照合状態
	std::unique_ptr<Impl> implementation_ ;
} ;

} // namespace koefude
