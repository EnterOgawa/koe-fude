//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file spectral_clustering.h
 * @brief 音響特徴から人数と近傍数を推定する有界NME-SCの契約です
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 NME-SCの独立した数値計算境界を追加
 * 0.1.0 Shunji Ogawa 2026/09/10 音響方向を覆うgraph標本化の契約を記録
 * 0.1.0 Shunji Ogawa 2026/09/23 自動人数を保持する診断用graph選択を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <span>
#include <stop_token>
#include <vector>

#include "core/engines.h"

namespace koefude {

/** @brief 推論resourceや声の特徴を含まない、file内のcluster対応です */
struct SpectralClusters {
	/// 入力行に対応する0始まりの話者cluster
	std::vector<int> labels ;
	/// 推定された全体人数。空入力なら0
	int				 count = 0 ;
	/// NMEで選択したgraphの近傍数。退化入力は0
	int				 neighbors = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief NME-SCと音響的な群内変動から話者clusterを自動推定します
 * @param[in] embeddings 4096行・1024次元以下の有限な非ゼロ特徴。保存しません
 * @param[in] expectedSpeakers 0は人数未知、1～20は任意の既知人数
 * @param[in] stop 固有値計算やk-meansの間で確認する取消し状態
 * @param[in] graphSelection Coupledは現行方式、Independentは診断専用のgraph比較
 * @return 入力行と同じ長さのlabelと推定人数
 * @throws OperationError 不正特徴、上限超過、数値異常、取消し
 * @note 音響方向を覆う最大384行のgraphと有限候補を使用します。ADR 0009/0010/0019参照
 */
//------------------------------------------------------------------------------
[[nodiscard]] SpectralClusters clusterSpeakers( std::span<const std::vector<float>> embeddings, int expectedSpeakers = 0,
											   std::stop_token		  stop = {},
											   SpectralGraphSelection graphSelection = SpectralGraphSelection::COUPLED) ;

} // namespace koefude
