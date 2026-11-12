//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_text_speaker_turns.h
 * @brief API文章改善の発言IDと原話者を機械的に束縛します
 * @version 1.0.0
 * @date 2026/11/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1360の話者保持v3と厳密区切り検証を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/api_text_correction.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace koefude {
constexpr std::uint32_t K_API_TEXT_SPEAKER_STRUCTURE_ERROR = 0x20001360 ; // 本文なしの固定構造診断
/** @brief 応答の本文と原文行の機械的対応です。意味からの話者推定を含みません */
struct ApiTextSpeakerPart {
	/// 原文linesの0始まり位置
	std::size_t sourceLine = 0 ;
	/// 同じ行内の1始まり部分番号
	std::size_t part = 0 ;
	/// 元投影から得た標準話者label。表示名を含みません
	std::string label ;
	/// 区切りを除いた改善本文。引用label/段落は無編集で保持
	std::string text ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 検証済みsnapshotの話者保持方式を識別します
 * @param[in] snapshot 空はlegacy、非空はcatalog検証済みJSON
 * @return v3だけtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool							  apiTextSpeakerProfile( std::string_view snapshot) ;
//------------------------------------------------------------------------------
/**
 * @brief 新規v3へ固定する編集指示を返します
 * @return UTF-8の所有指示
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string					  apiTextSpeakerPrompt() ;
//------------------------------------------------------------------------------
/**
 * @brief 原文行/長発言partを専用区切りで投影します
 * @param[in] source 原文の所有borrow
 * @return 最大2MiBのUTF-8
 * @throws OperationError 容量超過、専用marker衝突、不正UTF-8
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string					  apiTextSpeakerProjection( const Transcript &source) ;
//------------------------------------------------------------------------------
/**
 * @brief v3投影の開始/終端だけを列挙します
 * @param[in] projection 検証済みv3投影
 * @return 0と各part終端の昇順byte位置
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<std::size_t>		  apiTextSpeakerBoundaries( std::string_view projection) ;
//------------------------------------------------------------------------------
/**
 * @brief 応答の全区切りを同じtargetの発言IDへ照合します
 * @param[in] draft v3不変投影
 * @param[in] index 0始まりchunk
 * @param[in] response 最大8MiBのUTF-8応答
 * @return 同順序の検証済み本文
 * @throws OperationError 欠落/重複/捏造/空本文/区切り外本文
 * @note CRLFと区切り行周囲のASCII空白は許可し、機械ID/標準話者は完全一致を要求します
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<ApiTextSpeakerPart> parseApiTextSpeakerParts( const ApiTextDraft &draft, std::size_t index,
																	   std::string_view response) ;
//------------------------------------------------------------------------------
/**
 * @brief 一応答のcopy/TXT候補を再構築します
 * @param[in] draft 固定plan
 * @param[in] index 0始まりchunk
 * @param[in] result 取得済み終端
 * @return legacyは元本文、v3は標準label付き本文。未完構造は取得本文を保持します
 * @throws OperationError 完全と主張するv3の構造不整合
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string					  apiTextChunkCandidate( const ApiTextDraft &draft, std::size_t index,
																	const ApiTextChunkResult &result) ;
//------------------------------------------------------------------------------
/**
 * @brief 保存応答から候補全文を同じ規則で再構築します
 * @param[in] attempt 最大32応答の所有borrow
 * @return 最大8MiBの候補
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string					  apiTextCombinedCandidate( const ApiTextAttempt &attempt) ;
} // namespace koefude
