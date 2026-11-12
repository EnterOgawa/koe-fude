//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_catalog_store.h
 * @brief 本文と秘密を含まないcatalog cacheの有界codecを定義します
 * @version 1.0.0
 * @date 2026/11/08
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/08 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/08 SHA付きmetadata専用cacheを追加
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1347の音声テーブル・独立API設定と文章操作を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/api_model_catalog.h"
#include <span>
namespace koefude {
constexpr std::size_t					K_API_CATALOG_STORE_LIMIT = 2097152 ; // digest込みcache上限
//------------------------------------------------------------------------------
/**
 * @brief 表示cacheだけを符号化します
 * @param[in] catalog 検証済み正本
 * @return schema1/SHA付きbyte
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<std::uint8_t> encodeApiCatalog( const ApiModelCatalog &catalog) ;
//------------------------------------------------------------------------------
/**
 * @brief cacheを資格未確認として復元します
 * @param[in] bytes 有界byte
 * @return 検証済み表示値
 * @throws HistoryError 不正・未知形式
 */
//------------------------------------------------------------------------------
[[nodiscard]] ApiModelCatalog			decodeApiCatalog( std::span<const std::uint8_t> bytes) ;
//------------------------------------------------------------------------------
/**
 * @brief 一般設定を有界に符号化します
 * @param[in] settings IDと整数予算だけ
 * @return 256byte以内のSHA付き設定
 * @throws HistoryError 不正値
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<std::uint8_t> encodeApiSettings( const ApiGeneralSettings &settings) ;
//------------------------------------------------------------------------------
/**
 * @brief 一般設定を復元します
 * @param[in] bytes 256byte以内
 * @return 検証済み設定
 * @throws HistoryError 不正/未知形式、部分採用なし
 */
//------------------------------------------------------------------------------
[[nodiscard]] ApiGeneralSettings		decodeApiSettings( std::span<const std::uint8_t> bytes) ;
} // namespace koefude
