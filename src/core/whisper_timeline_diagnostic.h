//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whisper_timeline_diagnostic.h
 * @brief Whisper時刻拒否の元scalarだけを固定容量で保持します
 * @version 1.0.0
 * @date 2026/10/21
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/21 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1304の本文なし拒否観測を追加
 * 0.1.0 Shunji Ogawa 2026/10/21 実窓格子とADR1306のmetadata入場上限を区別
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace koefude {

// 非有限値も元doubleの全bitとして保存できる環境だけを対象とします
static_assert( sizeof(double) == sizeof(std::uint64_t) && std::numeric_limits<double>::is_iec559) ;

/** @brief 既存guardの最初の拒否siteです。未観測はoptional不在で表します */
enum class WhisperTimelineFailureSite : std::uint32_t {
	/// 元行時刻の有限性・非負・cs整数schema
	ROW_SCHEMA,
	/// 当該source契約の全文metadata入場上限超過
	ROW_ENVELOPE,
	/// 特別tokenを含む元tokenのschema又は範囲
	TOKEN_SCHEMA_OR_RANGE
} ;

/** @brief 拒否tokenの元scalarです。本文・任意の単位文字列を持ちません */
struct WhisperTimelineTokenFailure {
	/// 行内0始まりtoken番号
	std::uint64_t index = 0 ;
	/// 判定doubleの全64bit
	std::uint64_t startBits = 0 ;
	/// 判定doubleの全64bit
	std::uint64_t endBits = 0 ;
	/// 元単位が固定cs文字列と一致する場合true
	bool		  unitIsCentiseconds = false ;
	/// 元special flag
	bool		  special = false ;
} ;

/** @brief 純粋guardが拒否時だけ同期copyする固定値です */
struct WhisperTimelineGuardFailure {
	/// 拒否site
	WhisperTimelineFailureSite				   site = WhisperTimelineFailureSite::ROW_SCHEMA ;
	/// 判定行開始doubleの全64bit
	std::uint64_t							   rowStartMsBits = 0 ;
	/// 判定行終端doubleの全64bit
	std::uint64_t							   rowEndMsBits = 0 ;
	/// token拒否時だけ存在
	std::optional<WhisperTimelineTokenFailure> token ;
} ;

/** @brief 保持済み窓・native時刻と同じ拒否を結合した本文なしsnapshotです */
struct WhisperTimelineFailure {
	/// 純粋guardの元判定値
	WhisperTimelineGuardFailure guard ;
	/// 全窓内0始まりindex
	std::uint64_t				windowIndex = 0 ;
	/// 元PCMの半開区間開始sample
	std::uint64_t				windowBeginSample = 0 ;
	/// 元PCMの半開区間終端sample
	std::uint64_t				windowEndSample = 0 ;
	/// 当該実窓sample数
	std::uint64_t				actualWindowSamples = 0 ;
	/// 当該窓内0始まり行index
	std::uint64_t				rowIndex = 0 ;
	/// 実窓のG=2*ceil(sample数/320)、単位cs。入場上限Bとは別です
	std::uint64_t				maximumCs = 0 ;
	/// 追加getterなしの元native開始cs
	std::int64_t				nativeRowT0Cs = 0 ;
	/// 追加getterなしの元native終端cs
	std::int64_t				nativeRowT1Cs = 0 ;
} ;

// 固定値copyだけで、診断中に動的確保・所有pointer・可変容量を追加しません
static_assert( sizeof(WhisperTimelineGuardFailure) <= 512 && sizeof(WhisperTimelineFailure) <= 512 &&
			  std::is_trivially_copyable_v<WhisperTimelineFailure>) ;

} // namespace koefude
