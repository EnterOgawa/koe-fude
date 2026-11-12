//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_generation_contract.h
 * @brief モデル別の不変な生成方式と公式text rendererを定義します
 * @version 1.0.0
 * @date 2026/10/21
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/21 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の独立Plain契約を追加
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1343の文章専用補正を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
namespace koefude {
/** @brief 生成laneとtokenizerを混同しない固定方式です */
enum class WholeTextGenerationKind {
	/// 旧Analysis/Final契約
	GEMMA_THOUGHT_FINAL,
	/// 本文と自然EOSだけの契約
	MINISTRAL_PLAIN
} ;
inline constexpr std::string_view K_MINISTRAL16_GB_PROFILE_ID =
	"ministral3-14b-q4-k-m-cpu-avx2-norepack-16gb-bounded-v1" ; // 保存方式ID
inline constexpr std::string_view K_TEXT_CONTEXT_PROFILE_ID =
	"ministral14b-cpu-text-context-v1" ; // 音声を再処理しない方式ID
//------------------------------------------------------------------------------
/**
 * @brief 文章専用systemの固定byteを返します
 * @return LF・末尾LF付き1970byte
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string_view		  textContextSystem() noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 既存方式を変更せず文章専用設定を束縛します
 * @return 全設定preimage
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string			  textContextProfileEncoding() ;
//------------------------------------------------------------------------------
/**
 * @brief 新方式の全設定を固定順の全byteとして返します
 * @return 静的寿命のpreimage
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string_view		  ministral16gbProfileEncoding() noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 保存要求から方式を復元します
 * @param[in] id 保存ID
 * @param[in] digest 全設定SHA
 * @return 新IDと全SHAの一致だけPlain、旧保存要求は従来方式
 * @throws OperationError 新IDのSHA不一致。モデルやDLLを読みません
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextGenerationKind wholeTextGenerationKind( std::string_view					  id,
															  const std::array<std::uint8_t, 32> &digest) ;
//------------------------------------------------------------------------------
/**
 * @brief 公式二text messageを一byteもtrimせず描画します
 * @param[in] system system全UTF-8 byte
 * @param[in] user user全UTF-8 byte
 * @return BOSと四境界を一回含む完全prefix。入力controlは拒否します
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string			  renderMinistral3TwoTextPlain( std::string_view system, std::string_view user) ;
//------------------------------------------------------------------------------
/**
 * @brief 不変方式に合うrendererを選びます
 * @param[in] kind 生成方式
 * @param[in] system 全system
 * @param[in] user 全user
 * @return 同じ方式の完全prefix。不明方式は拒否します
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string			  renderWholeTextGeneration( WholeTextGenerationKind kind, std::string_view system,
																std::string_view user) ;
} // namespace koefude
