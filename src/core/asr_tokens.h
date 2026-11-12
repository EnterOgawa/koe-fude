//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file asr_tokens.h
 * @brief ASR本文と発火frame時刻の一致を確認する契約を定義します
 * @version 1.0.0
 * @date 2026/09/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/10 CTC tokenを本文のUTF-8境界と音声時刻へ対応付け
 * 0.1.0 Shunji Ogawa 2026/09/10 発火位置の明示と単語境界照合前の分割禁止を契約化
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "core/types.h"

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief CTCのtoken列が本文に完全一致した場合だけ、元音声の時刻を付けます
 * @param[in,out] segment 所有本文と元音声区間。既存tokensを消し、成功時だけ全対応を設定
 * @param[in] pieces 呼出し中だけ借用するUTF-8 token。ASCIIのbyte fallback表記を許可
 * @param[in] startsSeconds piecesと同じ件数の区間相対開始秒。非負・有限・単調順
 * @param[in] frameMs 固定モデルの発火frame幅（ms）。1～1000
 * @return 本文と全tokenが対応したらtrue。不一致なら本文を保ちtokensを空にしてfalse
 * @throws OperationError 不正な時刻・件数・上限。失敗時も本文と区間時刻は変更しません
 * @note 時刻はframeの発火位置であり、音素の厳密な継続時間を保証しません
 * 成功時はtoken_times_are_anchorsをtrueにし、全tokenのcan_split_afterをfalseにします
 * 話者交代で分割するには、別途単語等の境界と照合して分割許可を設定します
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool bindCtcTokens( TextSegment &segment, std::span<const std::string_view> pieces,
								 std::span<const float> startsSeconds, std::int64_t frameMs) ;

} // namespace koefude
