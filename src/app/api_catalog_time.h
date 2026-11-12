//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_catalog_time.h
 * @brief 公開metadataの取得UTCを共通表示します
 * @version 1.0.0
 * @date 2026/11/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/09 既存UTC formatterを音声APIにも共用
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "app/localization.h"
#include "app/resource.h"
#include <array>
#include <cstdint>
#include <ctime>
#include <string>
namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief UTC秒を既存の人間可読表示へ変換します
 * @param[in] utc Unix秒
 * @return YYYY-MM-DD HH:mm:ss UTC、未取得/不正は固定resource
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline std::wstring formatApiCatalogTime( std::int64_t utc)
{
	// 未取得を1970年の取得として表示しません
	if( utc <= 0) {
		return resource( IDS_API_META_MISSING) ;
	}
	const std::time_t value = static_cast<std::time_t>( utc) ; // x64の64bit時刻
	std::tm			  broken{} ; // UTC各field
	// 範囲外cache時刻を推測補完しません
	if( gmtime_s( &broken, &value) != 0) {
		return resource( IDS_API_META_EXPIRED) ;
	}
	std::array<wchar_t, 64> text{} ; // 固定表示buffer
	// 固定形式が収まった場合だけ日付を表示します
	if( std::wcsftime( text.data(), text.size(), L"%Y-%m-%d %H:%M:%S UTC", &broken) == 0) {
		return resource( IDS_API_META_EXPIRED) ;
	}
	return text.data() ;
}
} // namespace koefude
