//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_ministral_contract.h
 * @brief モデル別の不変な生成方式と公式text rendererを定義します
 * @version 1.0.0
 * @date 2026/10/21
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/21 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の独立Plain契約を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "inference/whole_text_native_abi.h"
namespace koefude {
/** @brief 共有ABIのPODを変えず、固有DLLの情報値を固定します */
inline constexpr KoefudeWtInfoV1 K_MINISTRAL16_GB_NATIVE_INFO{
	sizeof(KoefudeWtInfoV1),
	1,
	64,
	7,
	"b1ff4ca23630ac7c5a275405353ddb5c486332c9",
	"6cc0f8c0cfbafcfd8b146d60e947a1b2511ace4db907b0a73f6a474d04bdca9b",
	5632,
	4096,
	1536,
	0,
	1536} ;
} // namespace koefude
