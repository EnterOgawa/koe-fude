//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file speaker_stream.cpp
 * @brief 代表特徴の抽出と全体話者の有界な時刻投票を実装します
 * @version 1.0.0
 * @date 2026/09/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/10 録音全長によらない作業量と共通clusterを追加
 * 0.1.0 Shunji Ogawa 2026/10/19 callbackの契約・宣言の役割を明記
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実使用の標準headerを直接include
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "inference/speaker_stream.h"

#include <array>
#include <span>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace koefude {
namespace {
//------------------------------------------------------------------------------
/**
 * @brief 特徴の有限性と次元を確認します
 * @param[in] feature 同期借用する特徴
 * @return ノルム
 */
//------------------------------------------------------------------------------
[[nodiscard]] double featureNorm( const std::span<const float> feature)
{
	// 過大なschemaと空特徴を比較へ持ち込まない
	if( feature.empty() || feature.size() > 1024) {
		throw OperationError( ErrorKind::INFERENCE_FAILED) ;
	}
	double sum = 0 ; // ノルムの二乗
	// 全次元を確認し、NaN・Inf・ゼロ特徴を拒否する
	for( const auto value : feature) {
		// 非有限値を距離や平均へ伝播させない
		if( !std::isfinite( value)) {
			throw OperationError( ErrorKind::INFERENCE_FAILED) ;
		}
		sum += static_cast<double>( value) * value ;
	}
	// 正規化できない特徴を架空の話者へ割り当てない
	if( sum <= 1e-20 || !std::isfinite( sum)) {
		throw OperationError( ErrorKind::INFERENCE_FAILED) ;
	}
	return std::sqrt( sum) ;
}
} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 一特徴を観測し、保持容量を増やさず標本を更新します
 * @param[in] feature 有限・非ゼロ・最大1024次元の特徴
 * @return 録音内で再利用しない特徴ID
 * @see koefude::SpeakerReservoir::observe
 */
//------------------------------------------------------------------------------
std::uint64_t SpeakerReservoir::observe( const std::span<const float> feature)
{
	static_cast<void>( featureNorm( feature)) ;
	// schema変更やID overflowで以前の特徴を取り違えない
	if( (!features_.empty() && feature.size() != features_.front().size()) ||
	   seen_ == std::numeric_limits<std::uint64_t>::max()) {
		throw OperationError( ErrorKind::INFERENCE_FAILED) ;
	}
	const auto id = seen_++ ; // 元録音内での一意な特徴ID
	// 容量内の短い録音は全特徴を保持し、従来の結果を維持する
	if( features_.size() < K_SPEAKER_RESERVOIR_ROWS) {
		features_.emplace_back( feature.begin(), feature.end()) ;
		ids_.push_back( id) ;
	} else {
		std::uniform_int_distribution<std::uint64_t> select( 0, id) ; // 全観測へ同じ採用確率
		const auto									 slot = select( random_) ; // 今回置換する代表位置、範囲外なら非採用
		// 4096件超でも処理は継続し、代表だけを入れ替える
		if( slot < features_.size()) {
			features_[static_cast<std::size_t>( slot)].assign( feature.begin(), feature.end()) ;
			ids_[static_cast<std::size_t>( slot)] = id ;
		}
	}
	return id ;
}

//------------------------------------------------------------------------------
/**
 * @brief 現在の代表特徴を返します
 * @return instanceの寿命中のborrow
 * @see koefude::SpeakerReservoir::features
 */
//------------------------------------------------------------------------------
const std::vector<std::vector<float>> &SpeakerReservoir::features() const noexcept
{
	return features_ ;
}
//------------------------------------------------------------------------------
/**
 * @brief 代表特徴に対応する元IDを返します
 * @return 同じ順序のborrow
 * @see koefude::SpeakerReservoir::ids
 */
//------------------------------------------------------------------------------
const std::vector<std::uint64_t> &SpeakerReservoir::ids() const noexcept
{
	return ids_ ;
}
//------------------------------------------------------------------------------
/**
 * @brief 観測した全特徴数を返します
 * @return 代表数とは独立した総数
 * @see koefude::SpeakerReservoir::seen
 */
//------------------------------------------------------------------------------
std::uint64_t SpeakerReservoir::seen() const noexcept
{
	return seen_ ;
}

//------------------------------------------------------------------------------
/**
 * @brief 推定済みの全体clusterと正規化した平均方向を保持します
 * @param[in] reservoir この録音から得た代表特徴
 * @param[in] clusters 代表特徴の全体cluster
 * @see koefude::SpeakerClassifier::SpeakerClassifier
 */
//------------------------------------------------------------------------------
SpeakerClassifier::SpeakerClassifier( const SpeakerReservoir &reservoir, const SpectralClusters &clusters)
{
	const auto &features = reservoir.features() ; // 読取りだけに使う代表特徴
	// cluster結果のschemaと件数を信頼境界で確認する
	if( clusters.count < 0 || clusters.count > 20 || clusters.labels.size() != features.size() ||
	   (clusters.count == 0) != features.empty()) {
		throw OperationError( ErrorKind::INFERENCE_FAILED) ;
	}
	// 無音には平均方向を捏造しない
	if( features.empty()) {
		return ;
	}
	centers_.assign( static_cast<std::size_t>( clusters.count), std::vector<double>( features.front().size(), 0)) ;
	// 正規化した各特徴を推定clusterの平均方向へ集計する
	for( std::size_t row = 0; row < features.size(); ++row) {
		const int label = clusters.labels[row] ; // この代表特徴の共通cluster
		// 無効なlabelで平均配列を参照しない
		if( label < 0 || label >= clusters.count) {
			throw OperationError( ErrorKind::INFERENCE_FAILED) ;
		}
		known_.emplace( reservoir.ids()[row], label) ;
		const auto norm = featureNorm( features[row]) ; // 特徴ごとの長さの影響を除く
		auto	  &center = centers_[static_cast<std::size_t>( label)] ; // 集計先
		// 各次元の方向だけを等weightで加える
		for( std::size_t column = 0; column < center.size(); ++column) {
			center[column] += features[row][column] / norm ;
		}
	}
	// 全体clusterの方向を正規化してcosine比較の基準にする
	for( auto &center : centers_) {
		// cluster中心vectorの長さの二乗。正規化前に有限性と退化を検証する
		const auto squared = std::inner_product( center.begin(), center.end(), center.begin(), 0.0) ;
		// 空または相殺されたclusterを任意の方向として利用しない
		if( squared <= 1e-20 || !std::isfinite( squared)) {
			throw OperationError( ErrorKind::INFERENCE_FAILED) ;
		}
		const auto norm = std::sqrt( squared) ; // 集計方向の長さ
		// 各成分を単位方向へ揃える
		for( auto &value : center) {
			value /= norm ;
		}
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 同じ録音内で共通のclusterを返します
 * @param[in] id 元特徴のID
 * @param[in] feature 読み戻した音響特徴
 * @return 全体cluster番号。空の推定では-1
 * @see koefude::SpeakerClassifier::classify
 */
//------------------------------------------------------------------------------
int SpeakerClassifier::classify( const std::uint64_t id, const std::span<const float> feature) const
{
	const auto found = known_.find( id) ; // 代表特徴の推定labelは変更しない
	// 容量内の録音では全特徴がこの経路を使い、元の判定と一致する
	if( found != known_.end()) {
		return found->second ;
	}
	// 根拠がない録音に話者を追加しない
	if( centers_.empty()) {
		return -1 ;
	}
	const auto norm = featureNorm( feature) ; // 外部から読み戻した特徴の検証
	// 異なる次元の特徴を黙って切り詰めない
	if( feature.size() != centers_.front().size()) {
		throw OperationError( ErrorKind::INFERENCE_FAILED) ;
	}
	double best = -std::numeric_limits<double>::infinity() ; // 最大cosine類似度
	int	   label = -1 ; // 根拠が得られた全体cluster
	// 全録音で共通の最大20個の平均方向と比較する
	for( std::size_t row = 0; row < centers_.size(); ++row) {
		double score = 0 ; // 当該clusterとのcosine類似度
		// 固定schema内の次元だけを比較する
		for( std::size_t column = 0; column < feature.size(); ++column) {
			score += centers_[row][column] * feature[column] / norm ;
		}
		// 同点は先のclusterを維持して再現可能にする
		if( score > best) {
			best = score ;
			label = static_cast<int>( row) ;
		}
	}
	return label ;
}

//------------------------------------------------------------------------------
/**
 * @brief 元音声の終端と全体話者数を保持します
 * @param[in] samples 元音声のsample数
 * @param[in] speakers 1～20の全体cluster数
 * @see koefude::SpeakerTimeline::SpeakerTimeline
 */
//------------------------------------------------------------------------------
SpeakerTimeline::SpeakerTimeline( const std::uint64_t samples, const int speakers)
	: samples_( samples), frames_( (samples + K_SPEAKER_FRAME_SAMPLES - 1) / K_SPEAKER_FRAME_SAMPLES), speakers_( speakers)
{
	// 固定時間ではなく時刻の表現範囲とcluster数だけを制限する
	if( samples == 0 || samples > K_MAXIMUM_TIMELINE_SAMPLES || speakers < 1 || speakers > 20) {
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
	starts_.fill( frames_) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 発話状態の変化を連続区間へ変換します
 * @param[in] active 全体話者の発話bit
 * @param[in] frame 元音声のframe番号
 * @see koefude::SpeakerTimeline::apply
 */
//------------------------------------------------------------------------------
void SpeakerTimeline::apply( const std::uint32_t active, const std::uint64_t frame)
{
	// 各話者の連続した発話だけを同じ区間へまとめる
	for( int speaker = 0; speaker < speakers_; ++speaker) {
		auto	  &begin = starts_[static_cast<std::size_t>( speaker)] ; // 対象話者の開始位置
		const bool speaking = (active & (1U << speaker)) != 0 ; // 今frameの状態
		// 新しい発話は現在の全体frameから開始する
		if( speaking && begin == frames_) {
			begin = frame ;
		}
		// 連続発話が途切れた時だけ区間を確定する
		if( !speaking && begin != frames_) {
			// 受容野の中心を補正し、元音声末尾へclipした発話開始ms
			const auto startMs = static_cast<std::int64_t>(
				std::min( samples_, begin * K_SPEAKER_FRAME_SAMPLES + K_SPEAKER_RECEPTIVE_SAMPLES / 2) / 16) ;
			// 受容野の中心を補正し、元音声末尾へclipした発話終了ms
			const auto endMs = static_cast<std::int64_t>(
				std::min( samples_, frame * K_SPEAKER_FRAME_SAMPLES + K_SPEAKER_RECEPTIVE_SAMPLES / 2) / 16) ;
			// 瞬間的な反転は出力せず、結果容量の実上限を維持する
			if( endMs - startMs >= 100) {
				// 過大結果を部分成功として返さない
				if( turns_.size() >= K_MAXIMUM_SEGMENTS) {
					throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
				}
				turns_.push_back( {startMs, endMs, speaker}) ;
			}
			begin = frames_ ;
		}
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 将来変更されないframeを確定します
 * @param[in] target 未確定範囲の先頭frame
 * @see koefude::SpeakerTimeline::flush
 */
//------------------------------------------------------------------------------
void SpeakerTimeline::flush( std::uint64_t target)
{
	target = std::min( target, frames_) ;
	// 今後の観測で変更されないframeから投票を確定して解放する
	while( first_ < target && !pending_.empty()) {
		const auto	 &frame = pending_.front() ; // 現在の最古の未確定観測
		std::uint32_t active = 0 ; // 採用する話者集合
		// 観測があるframeだけから同時発話人数を選ぶ
		if( frame.weight > 0) {
			// 重み付き投票から推定し、登録話者数内に制限した同時発話人数
			const int count = std::clamp( static_cast<int>( std::lround( frame.count / frame.weight)), 0, speakers_) ;
			std::array<int, 20> order{} ; // 固定量の順位作業領域
			std::iota( order.begin(), order.end(), 0) ;
			std::sort( order.begin(), order.begin() + speakers_,
					  //------------------------------------------------------------------------------
					  /**
					   * @brief 同一frameの話者票を降順、同票なら話者ID順で並べます
					   * @param[in] left 左候補の検証済み話者ID
					   * @param[in] right 右候補の検証済み話者ID
					   * @return 左の票が多いか、同票でIDが小さければtrue
					   * @note frameは同期sortの完了まで参照し、callbackを保存しません
					   */
					  //------------------------------------------------------------------------------
					  [&]( const int left, const int right)
					  {
						  const float a = frame.votes[static_cast<std::size_t>( left)] ; // 左候補
						  const float b = frame.votes[static_cast<std::size_t>( right)] ; // 右候補
						  return a > b || (a == b && left < right) ;
					  }) ;
			// 0票を補わず、人数に対応する上位話者だけを採用する
			for( int index = 0; index < count; ++index) {
				const int speaker = order[static_cast<std::size_t>( index)] ; // 採用候補
				// 音声上の根拠がある場合だけ発話bitを立てる
				if( frame.votes[static_cast<std::size_t>( speaker)] > 0) {
					active |= 1U << speaker ;
				}
			}
		}
		apply( active, first_++) ;
		pending_.pop_front() ;
	}
	// 長い未観測の空白は配列や反復を作らず、開始位置だけ進める
	if( first_ < target) {
		apply( 0, first_) ;
		first_ = target ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 次の観測を加えます
 * @param[in] window 開始sampleの昇順に渡す観測
 * @see koefude::SpeakerTimeline::add
 */
//------------------------------------------------------------------------------
void SpeakerTimeline::add( const SpeakerWindow &window)
{
	// 不正・逆順・重複観測で既に確定したframeを変更しない
	if( finished_ || window.startSample >= samples_ || (hasWindow_ && window.startSample <= lastStart_)) {
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
	hasWindow_ = true ;
	lastStart_ = window.startSample ;
	// window開始sampleを丸めて求めた元音声の先頭frame番号
	const auto first = (window.startSample + K_SPEAKER_FRAME_SAMPLES / 2) / K_SPEAKER_FRAME_SAMPLES ;
	flush( first) ;
	// 未確定末尾へ投票できる、このwindow内の最大frame数
	const auto count = static_cast<std::size_t>( std::min<std::uint64_t>( K_SPEAKER_FRAMES, frames_ - first_)) ;
	pending_.resize( std::max( pending_.size(), count)) ;
	// 現windowの最大589frameだけへ加重投票する
	for( std::size_t index = 0; index < count; ++index) {
		const auto mask = window.masks[index] ; // 固定modelの局所3枠
		// 未定義bitを発話人数として使わない
		if( mask > 7) {
			throw OperationError( ErrorKind::INFERENCE_FAILED) ;
		}
		// window内のframe位置を閉区間[0, 1]へ正規化した値
		const float position = static_cast<float>( index) / static_cast<float>( K_SPEAKER_FRAMES - 1) ;
		// window中央を重くし、端でも0票にしない三角形の投票重み
		const float weight = 0.1F + 0.9F * (1.0F - std::abs( 2.0F * position - 1.0F)) ;
		auto	   &frame = pending_[index] ; // 元音声の同じ時刻に対応する投票
		frame.count += weight * static_cast<float>( std::popcount( static_cast<unsigned>( mask))) ;
		frame.weight += weight ;
		std::uint32_t active = 0 ; // 同じ全体話者への局所重複を除く集合
		// 局所の最大3枠を全録音の共通clusterへ対応させる
		for( std::size_t local = 0; local < 3; ++local) {
			const int speaker = window.speakers[local] ; // -1は根拠不足
			// 予期しないclusterでshiftや配列参照を行わない
			if( speaker < -1 || speaker >= speakers_) {
				throw OperationError( ErrorKind::INFERENCE_FAILED) ;
			}
			// 観測した発話で根拠がある局所話者だけを加える
			if( speaker >= 0 && (mask & (1U << local)) != 0) {
				active |= 1U << speaker ;
			}
		}
		// 各全体話者へ一回だけ投票する
		for( int speaker = 0; speaker < speakers_; ++speaker) {
			// 観測した話者にだけweightを付ける
			if( (active & (1U << speaker)) != 0) {
				frame.votes[static_cast<std::size_t>( speaker)] += weight ;
			}
		}
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 終端まで確定します
 * @return 時系列の所有区間。二回呼出しは禁止
 * @see koefude::SpeakerTimeline::finish
 */
//------------------------------------------------------------------------------
std::vector<SpeakerTurn> SpeakerTimeline::finish()
{
	// 二重確定で結果を失った空成功を返さない
	if( finished_) {
		throw OperationError( ErrorKind::INTERNAL) ;
	}
	flush( frames_) ;
	apply( 0, frames_) ;
	finished_ = true ;
	std::sort( turns_.begin(), turns_.end(),
			  //------------------------------------------------------------------------------
			  /**
			   * @brief 確定した話者区間を元音声の開始時刻順へ並べます
			   * @param[in] left 同期sort中だけ借用する左の話者区間
			   * @param[in] right 同期sort中だけ借用する右の話者区間
			   * @return 左が開始時刻順、同時刻なら話者ID順で先になる場合true
			   */
			  //------------------------------------------------------------------------------
			  []( const SpeakerTurn &left, const SpeakerTurn &right)
			  {
				  return left.startMs < right.startMs ||
						 (left.startMs == right.startMs && left.speaker < right.speaker) ;
			  }) ;
	return std::move( turns_) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 作業量を返します
 * @return 現在保持するframe数。試験と診断用
 * @see koefude::SpeakerTimeline::bufferedFrames
 */
//------------------------------------------------------------------------------
std::size_t SpeakerTimeline::bufferedFrames() const noexcept
{
	return pending_.size() ;
}

} // namespace koefude
