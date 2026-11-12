//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file audio_identity.h
 * @brief 全正規化PCMの整数属性とbit同一性を定義します
 * @version 1.0.0
 * @date 2026/10/19
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/19 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/19 全PCMの有界な同一性照合を追加
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
#include "core/types.h"
#include <array>
#include <cstdint>
#include <stop_token>

namespace koefude {
/** @brief 元ASRへ渡した全16kHz mono PCMの実測値です */
struct DecodedPcmIdentity {
	/// binary32 little endian全bitの契約版
	std::uint32_t				 version = 1 ;
	/// 切り詰めない全sample数
	std::uint64_t				 sampleCount = 0 ;
	/// 正規化後のHz
	std::uint32_t				 sampleRate = K_SAMPLE_RATE ;
	/// 正規化後のmono
	std::uint32_t				 channels = 1 ;
	/// 元fileのsample原点
	std::uint64_t				 originSample = 0 ;
	/// 全sampleの4byte列のSHA-256
	std::array<std::uint8_t, 32> pcmSha256{} ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 全fieldを比較します
	 * @param[in] other 比較値
	 * @return 完全一致ならtrue
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool			 operator==( const DecodedPcmIdentity &other) const noexcept = default ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 全PCMを有界に読み同一性を採取します
 * @param[in] audio worker所有の全view。sourceとsamplesの同時使用は禁止
 * @param[in] stop 協調取消し
 * @param[in] progress 実sample進捗、空可
 * @return 全読取り成功後だけ返す実測値
 * @throws OperationError 短縮view、不正PCM、I/O異常、取消し
 */
//------------------------------------------------------------------------------
[[nodiscard]] DecodedPcmIdentity fingerprintDecodedPcm( const AudioData &audio, std::stop_token stop,
													   const ProgressCallback &progress = {}) ;
} // namespace koefude
