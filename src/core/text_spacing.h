//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file text_spacing.h
 * @brief 日本語認識本文の文字間空白を整える契約です
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 日本語の文字間だけの空白整形を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include <string>
#include <string_view>

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief 日本語文字同士に挟まれた水平空白だけを除きます
 * @param[in] text 日本語と判定された認識本文のUTF-8。借用し、変更しません
 * @return 整形した所有UTF-8。英数字との境界、改行、先頭・末尾空白は保持します
 * @throws OperationError 不正UTF-8または変換上限超過
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string normalizeJapaneseSpacing( std::string_view text) ;

} // namespace koefude
