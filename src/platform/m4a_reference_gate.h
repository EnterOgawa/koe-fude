//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file m4a_reference_gate.h
 * @brief MF作成前のM4A自己包含参照とALAC entryの識別契約です
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 外部参照をnative型判定前に拒否
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
#include <stop_token>

#include "platform/alac_format.h"

namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief M4Aの外部参照を拒否し、ALAC entryの有無を返します
 * @param[in] fileBytes 同じ元streamの実byte数
 * @param[in] readAt 同じworkerの同期read。最大8byte/spanを満たし、保持しません
 * @param[in] stop 各read前後で確認する取消しtoken
 * @return stsdにALAC entryがあればtrue。明らかな非box入力はfalse
 * @throws OperationError 不正範囲/読込み、走査資源、外部参照、取消し。元例外は伝播
 * @note AACの時間表/track数/codec範囲を検査するAPIではありません
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool inspectM4aLocalReferences( std::uint64_t fileBytes, const AlacRead &readAt,
											 std::stop_token stop = {}) ;
} // namespace koefude
