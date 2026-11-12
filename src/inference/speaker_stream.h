//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file speaker_stream.h
 * @brief 全録音の代表特徴と固定量の時刻投票を管理します
 * @version 1.0.0
 * @date 2026/09/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/10 時間に比例するRAM保持を逐次処理へ変更
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <random>
#include <span>
#include <vector>

#include "core/types.h"
#include "inference/spectral_clustering.h"

namespace koefude {

constexpr std::size_t K_SPEAKER_FRAMES = 589 ; // 固定segmentation modelの10秒出力
constexpr std::size_t K_SPEAKER_FRAME_SAMPLES = 270 ; // 一frameの移動sample数
constexpr std::size_t K_SPEAKER_RECEPTIVE_SAMPLES = 991 ; // modelの受容野
constexpr std::size_t K_SPEAKER_RESERVOIR_ROWS = 4096 ; // 全録音から保持する代表特徴の数

/** @brief 一時記録を全体話者へ対応付けた一つの観測windowです */
struct SpeakerWindow {
	/// 元音声基準の開始sample
	std::uint64_t							   startSample = 0 ;
	/// 局所3枠の発話bit
	std::array<std::uint8_t, K_SPEAKER_FRAMES> masks{} ;
	/// 共通の全体cluster。-1は根拠不足
	std::array<int, 3>						   speakers{-1, -1, -1} ;
} ;

/** @brief 固定seedのreservoir samplingで全録音を最大4096特徴へ代表させます */
class SpeakerReservoir final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 一特徴を観測し、保持容量を増やさず標本を更新します
	 * @param[in] feature 有限・非ゼロ・最大1024次元の特徴
	 * @return 録音内で再利用しない特徴ID
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t							 observe( std::span<const float> feature) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 現在の代表特徴を返します
	 * @return instanceの寿命中のborrow
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const std::vector<std::vector<float>> &features() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 代表特徴に対応する元IDを返します
	 * @return 同じ順序のborrow
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const std::vector<std::uint64_t>		&ids() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 観測した全特徴数を返します
	 * @return 代表数とは独立した総数
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t							 seen() const noexcept ;

	private:
	/// 保持から外れた特徴も含む総数
	std::uint64_t					seen_ = 0 ;
	/// 録音ごとの固定seed
	std::mt19937_64					random_{0x4b6f6546756465ULL} ;
	/// 最大4096×1024個のfloat
	std::vector<std::vector<float>> features_ ;
	/// 各標本の録音内ID
	std::vector<std::uint64_t>		ids_ ;
} ;

/** @brief 代表特徴の全体clusterを、全録音の特徴へ対応付けます */
class SpeakerClassifier final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 推定済みの全体clusterと正規化した平均方向を保持します
	 * @param[in] reservoir この録音から得た代表特徴
	 * @param[in] clusters 代表特徴の全体cluster
	 */
	//------------------------------------------------------------------------------
	SpeakerClassifier( const SpeakerReservoir &reservoir, const SpectralClusters &clusters) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ録音内で共通のclusterを返します
	 * @param[in] id 元特徴のID
	 * @param[in] feature 読み戻した音響特徴
	 * @return 全体cluster番号。空の推定では-1
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] int classify( std::uint64_t id, std::span<const float> feature) const ;

	private:
	/// 標本には推定したlabelをそのまま使用
	std::map<std::uint64_t, int>	 known_ ;
	/// 最大20話者の正規化平均方向
	std::vector<std::vector<double>> centers_ ;
} ;

/** @brief 将来のwindowが影響しなくなったframeから確定する投票器です */
class SpeakerTimeline final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 元音声の終端と全体話者数を保持します
	 * @param[in] samples 元音声のsample数
	 * @param[in] speakers 1～20の全体cluster数
	 */
	//------------------------------------------------------------------------------
	SpeakerTimeline( std::uint64_t samples, int speakers) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 次の観測を加えます
	 * @param[in] window 開始sampleの昇順に渡す観測
	 */
	//------------------------------------------------------------------------------
	void								   add( const SpeakerWindow &window) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 終端まで確定します
	 * @return 時系列の所有区間。二回呼出しは禁止
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::vector<SpeakerTurn> finish() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 作業量を返します
	 * @return 現在保持するframe数。試験と診断用
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::size_t			   bufferedFrames() const noexcept ;

	private:
	/** @brief 一frameの人数・話者投票だけを保持します */
	struct Frame {
		/// 全体話者ごとの加重票
		std::array<float, 20> votes{} ;
		/// 発話人数の加重和
		float				  count = 0 ;
		/// 観測のweight総和
		float				  weight = 0 ;
	} ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 将来変更されないframeを確定します
	 * @param[in] target 未確定範囲の先頭frame
	 */
	//------------------------------------------------------------------------------
	void						  flush( std::uint64_t target) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 発話状態の変化を連続区間へ変換します
	 * @param[in] active 全体話者の発話bit
	 * @param[in] frame 元音声のframe番号
	 */
	//------------------------------------------------------------------------------
	void						  apply( std::uint32_t active, std::uint64_t frame) ;
	/// 元音声の終端sample
	std::uint64_t				  samples_ = 0 ;
	/// 元音声を覆うframe数
	std::uint64_t				  frames_ = 0 ;
	/// 全体人数
	int							  speakers_ = 0 ;
	/// deque先頭の全体frame番号
	std::uint64_t				  first_ = 0 ;
	/// 昇順観測の検証位置
	std::uint64_t				  lastStart_ = 0 ;
	/// 最初の観測と重複を区別する状態
	bool						  hasWindow_ = false ;
	/// 終了済みの二重利用を拒否
	bool						  finished_ = false ;
	/// 最大589frameの未確定投票
	std::deque<Frame>			  pending_ ;
	/// 話者ごとの発話開始。非発話はframes_
	std::array<std::uint64_t, 20> starts_{} ;
	/// 有界な確定結果
	std::vector<SpeakerTurn>	  turns_ ;
} ;

} // namespace koefude
