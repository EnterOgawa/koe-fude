//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file audio_data.cpp
 * @brief PCMの部分読込みと静音を優先する有界なASR入力を実装します
 * @version 1.0.0
 * @date 2026/09/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/10 固定時間上限を全量allocationから分離
 * 0.1.0 Shunji Ogawa 2026/10/19 callbackの契約を明記
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
#include "core/audio_data.h"

#include <span>
#include <algorithm>
#include <cmath>

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief このviewで利用するsample数を返します
 * @return 明示試験範囲内の総数
 * @see koefude::AudioData::sampleCount
 */
//------------------------------------------------------------------------------
std::uint64_t AudioData::sampleCount() const noexcept
{
	return std::min( sampleLimit, source ? source->size() : samples.size()) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 連続した部分PCMを所有bufferへ読み込みます
 * @param[in] offset view内のsample番号
 * @param[out] destination 同期呼出し中だけ借用する出力。失敗時は内容未確定
 * @throws OperationError 範囲外またはI/O異常
 * @see koefude::AudioData::readSamples
 */
//------------------------------------------------------------------------------
void AudioData::readSamples( const std::uint64_t offset, const std::span<float> destination) const
{
	const auto count = sampleCount() ; // 64bitで照合するviewの終端
	// 加算前に残りを照合し、範囲外と二重のPCM所有元を拒否する
	if( offset > count || destination.size() > count - offset || (source && !samples.empty())) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// 空範囲ではNULLになり得るvectorのdataに対してpointer演算しない
	if( destination.empty()) {
		return ;
	}
	// 実音声は部分I/O、fakeは同期copyとして同じ境界を使用する
	if( source) {
		source->read( offset, destination) ;
	} else {
		std::copy_n( samples.data() + static_cast<std::size_t>( offset), destination.size(), destination.data()) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 静音を優先して区切った連続PCMを読み込みます
 * @param[in] audio workerが所有する音声。全量をRAMへ読み込みません
 * @param[in] offset 先頭sample番号。次回は返された要素数だけ進めます
 * @param[in] maximumSamples 処理単位の最大要素数。1～K_ASR_CHUNK_SAMPLES
 * @return 空でない所有PCM。失敗時は部分結果を返しません
 * @throws OperationError 範囲外、不正PCM、I/O異常
 * @see koefude::readAsrChunk
 */
//------------------------------------------------------------------------------
std::vector<float> readAsrChunk( const AudioData &audio, const std::uint64_t offset, const std::size_t maximumSamples)
{
	const auto total = audio.sampleCount() ; // 音声全体の終端
	// 進まない処理単位と無効な開始位置を拒否する
	if( offset >= total || maximumSamples == 0 || maximumSamples > K_ASR_CHUNK_SAMPLES) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	const auto count =
		static_cast<std::size_t>( std::min<std::uint64_t>( maximumSamples, total - offset)) ; // 今回の読込み要素数
	std::vector<float> result( count) ; // 最大一処理単位だけの所有PCM
	audio.readSamples( offset, result) ;
	// 外部PCM sourceの不正値をengineへ渡さない
	if( !std::all_of( result.begin(), result.end(),
					//------------------------------------------------------------------------------
					/**
					 * @brief engineへ渡せる正規化済みPCM値かを検査します
					 * @param[in] value 一sampleの振幅
					 * @return 有限値かつ閉区間[-1, 1]内ならtrue
					 */
					//------------------------------------------------------------------------------
					[]( const float value)
					{
						return std::isfinite( value) && value >= -1.0F && value <= 1.0F ;
					})) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	constexpr std::size_t K_CELL = K_SAMPLE_RATE / 50 ; // 静音探索の20ms単位
	constexpr std::size_t K_LOOKBACK = 10 * K_SAMPLE_RATE ; // 境界直前の静音探索幅
	// 最終部分や短い試験入力は末尾まで含め、空の後続処理を作らない
	if( offset + count == total || count <= K_LOOKBACK) {
		return result ;
	}
	std::size_t quietBegin = 0 ; // 継続する静音の先頭。0は静音外
	std::size_t cut = count ; // 十分な静音がなければ連続した固定単位で分割
	// 末尾10秒の20msごとの平均energyから、200ms以上の静音中の境界を探す
	for( std::size_t cell = count - K_LOOKBACK; cell + K_CELL <= count; cell += K_CELL) {
		double energy = 0 ; // 当該cellの二乗和
		// 有限な正規化sampleだけを集計する
		for( std::size_t index = cell; index < cell + K_CELL; ++index) {
			energy += static_cast<double>( result[index]) * result[index] ;
		}
		// 静音が続く場合は開始を保持し、十分な長さがあれば中央を候補にする
		if( energy / K_CELL <= 0.0001) {
			quietBegin = quietBegin == 0 ? cell : quietBegin ;
			// 200ms以上の低energy区間だけを文脈の区切り候補とする
			if( cell + K_CELL - quietBegin >= K_SAMPLE_RATE / 5) {
				cut = ((quietBegin + cell + K_CELL) / 2 / K_CELL) * K_CELL ;
			}
		} else {
			quietBegin = 0 ; // 次の静音を独立して探す
		}
	}
	result.resize( cut) ; // 次回のoffsetは実際に渡したsample数だけ進める
	return result ;
}

} // namespace koefude
