//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file asr_windows.cpp
 * @brief 発話の静音を優先し、上限内の連続認識区間を作ります
 * @version 1.0.0
 * @date 2026/09/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/10 発話範囲を保つ有界分割を追加
 * 0.1.0 Shunji Ogawa 2026/09/10 小さい声を補助する検出用copyと増幅上限を追加
 * 0.1.0 Shunji Ogawa 2026/09/14 VAD末尾の窓切上げと10ms丸めから終了上限を算出
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
#include "core/asr_windows.h"

#include <span>
#include <algorithm>
#include <cmath>
#include <limits>

#include "core/audio_data.h"
#include "core/types.h"

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief 固定Sileroの窓・時刻丸めを検証し、元PCM内の発話範囲へ戻します
 * @param[in] startCs 検出開始時刻（centisecond）。有限・非負で元PCM終端より前
 * @param[in] endCs 検出終了時刻（centisecond）。開始より後で固定engineの丸め上限内
 * @param[in] sampleCount 元16kHz mono PCMのsample数。1 sample～10分
 * @return 所有権を持たないsample番号の半開区間。許容する終了超過だけ元終端へ制限します
 * @throws OperationError PCM長の範囲外、不正時刻、丸め後の空区間
 * @note 固定Sileroの512 sample窓とwhisper.cppの10ms四捨五入を前提とします
 * 区間同士の順序・重複はsplitAsrWindowsで別に検証します
 * @see koefude::checkedVadWindow
 */
//------------------------------------------------------------------------------
AsrWindow checkedVadWindow( const double startCs, const double endCs, const std::size_t sampleCount)
{
	// 空入力と有界PCMを超える長さを、丸めの加算より先に拒否する
	if( sampleCount == 0 || sampleCount > K_ASR_CHUNK_SAMPLES) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	constexpr std::size_t K_VAD_WINDOW_SAMPLES = 512 ; // 固定Sileroが最終窓へ補うsample数の単位
	constexpr std::size_t K_CENTISECOND_SAMPLES = K_SAMPLE_RATE / 100 ; // 10msに対応する160 sample
	const auto			  paddedSamples = (sampleCount + K_VAD_WINDOW_SAMPLES - 1) / K_VAD_WINDOW_SAMPLES *
							   K_VAD_WINDOW_SAMPLES ; // 10分上限により加算・乗算はsize_t内
	const auto maximumEndCs =
		(paddedSamples + K_CENTISECOND_SAMPLES / 2) / K_CENTISECOND_SAMPLES ; // 外部APIの四捨五入後の最大終了cs
	// engineが生成し得ない範囲や非有限値を、整数への変換や終端clipで隠さない
	if( !std::isfinite( startCs) || !std::isfinite( endCs) || startCs < 0 || endCs <= startCs ||
	   startCs * K_CENTISECOND_SAMPLES >= sampleCount || endCs > static_cast<double>( maximumEndCs)) {
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
	const auto begin =
		static_cast<std::size_t>( std::llround( startCs * K_CENTISECOND_SAMPLES)) ; // 検証済みの開始sample
	const auto end = std::min( sampleCount, static_cast<std::size_t>( std::llround(
											   endCs * K_CENTISECOND_SAMPLES))) ; // 窓とcs丸めによる末尾超過だけを除く
	// 微小な正時間でもsampleへ丸めて空になった区間を認識へ渡さない
	if( begin >= end) {
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
	return {begin, end} ;
}

//------------------------------------------------------------------------------
/**
 * @brief 長い発話を静かな位置で分け、検出範囲の欠落・重複を防ぎます
 * @param[in] samples 検証済み16kHz mono PCM。呼出し中だけ借用します
 * @param[in] speech 開始順の非重複発話範囲。空なら空結果です
 * @param[in] maximumSamples 1 sample～10分の分割上限sample数
 * @return 元PCMに対する所有区間列。失敗時に部分結果を返しません
 * @throws OperationError 範囲外、重複、逆順、不正PCM、上限外
 * @see koefude::splitAsrWindows
 */
//------------------------------------------------------------------------------
std::vector<AsrWindow> splitAsrWindows( const std::span<const float> samples, const std::span<const AsrWindow> speech,
									   const std::size_t maximumSamples)
{
	// 進まない上限や有界PCMより大きい入力を処理しない
	if( maximumSamples == 0 || maximumSamples > K_ASR_CHUNK_SAMPLES || samples.size() > K_ASR_CHUNK_SAMPLES) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// 外部VADが参照するPCMの非有限値・範囲外を拒否する
	for( const float value : samples) { // 検証する正規化sample
		// RMS比較にNaNや無限大を混ぜない
		if( !std::isfinite( value) || std::abs( value) > 1.0F) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
	}
	std::vector<AsrWindow> result ; // 検出済みsampleを一度ずつ含む所有区間
	std::size_t			   previousEnd = 0 ; // 元範囲の順序と重複の確認位置
	constexpr std::size_t  K_CELL_SAMPLES = K_SAMPLE_RATE / 50 ; // energy比較の20ms幅
	constexpr std::size_t  K_LOOKBACK_SAMPLES = 4 * K_SAMPLE_RATE ; // 上限直前の静音探索幅
	// 検出発話を順番に分割し、VADが保持した先頭から末尾までを全て含める
	for( const auto &interval : speech) { // 今回の非所有VAD区間
		// VADの範囲・順序を信用せず、減算や添字利用より先に検査する
		if( interval.begin < previousEnd || interval.begin >= interval.end || interval.end > samples.size()) {
			throw OperationError( ErrorKind::INVALID_TIMELINE) ;
		}
		std::size_t begin = interval.begin ; // 次に出力するsample位置
		// 長い発話だけを上限内へ分割し、最後の短い部分も保持する
		while( begin < interval.end) {
			std::size_t end = begin + std::min( maximumSamples, interval.end - begin) ; // 仮の終端
			// 最終部分は末尾まで保持し、十分長い中間部分だけで静音を探す
			if( end < interval.end && maximumSamples > 2 * K_LOOKBACK_SAMPLES) {
				double		minimumEnergy = std::numeric_limits<double>::max() ; // 最小二乗和
				std::size_t cut = end ; // 候補がなくても必ず進む既定終端
				// 上限直前4秒の最も静かな20ms中央を切断点にする
				for( std::size_t cell = end - K_LOOKBACK_SAMPLES; cell + K_CELL_SAMPLES <= end;
					cell += K_CELL_SAMPLES) { // energyを比較するcellの先頭
					double energy = 0 ; // 当該cellの二乗和
					// 有限なsampleのenergyを同じ20ms幅で比較する
					for( std::size_t index = cell; index < cell + K_CELL_SAMPLES; ++index) { // sample
						energy += static_cast<double>( samples[index]) * samples[index] ;
					}
					// 同値なら後の位置を選び、不要に短い区間を作らない
					if( energy <= minimumEnergy) {
						minimumEnergy = energy ;
						cut = cell + K_CELL_SAMPLES / 2 ;
					}
				}
				end = cut ;
			}
			// 不正に細かい区間設定でも結果を無制限に確保しない
			if( result.size() >= K_MAXIMUM_SEGMENTS) {
				throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
			}
			result.push_back( {begin, end}) ;
			begin = end ; // 次は同じ境界から始め、二重認識やsample欠落を作らない
		}
		previousEnd = interval.end ;
	}
	return result ;
}

//------------------------------------------------------------------------------
/**
 * @brief 小さい声の検出を補助するため、VADへ渡すcopyだけを有界なgainで増幅します
 * @param[in] samples 16kHz mono PCM。空を許可し、内容は変更しません
 * @return 所有する検出用PCM。ASRの元音声として置き換えないでください
 * @throws OperationError 10分超、非有限値、正規化範囲外
 * @see koefude::prepareVadAudio
 */
//------------------------------------------------------------------------------
std::vector<float> prepareVadAudio( const std::span<const float> samples)
{
	// 検出用copyも有界PCMと同じ上限内に保つ
	if( samples.size() > K_ASR_CHUNK_SAMPLES) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// 空PCMでgainの先頭や末尾へアクセスしない
	if( samples.empty()) {
		return {} ;
	}
	constexpr std::size_t K_FRAME_SAMPLES = K_SAMPLE_RATE ; // gainを測る1秒窓
	constexpr double	  K_TARGET_RMS = 0.03 ; // 検出側だけの振幅目標
	constexpr double	  K_MAXIMUM_GAIN = 32.0 ; // 雑音や量子化誤差の増幅上限
	std::vector<double>	  gains ; // 各窓の中心での増幅率
	// 全窓の二乗平均とpeakを測り、増幅のみの有界gainを決める
	for( std::size_t begin = 0; begin < samples.size(); begin += K_FRAME_SAMPLES) { // 窓の先頭
		const auto count = std::min( K_FRAME_SAMPLES, samples.size() - begin) ; // 最終窓の実要素数
		double	   energy = 0 ; // RMSを求める二乗和
		double	   peak = 0 ; // 窓内peakによる増幅制限
		// 不正sampleを加算前に拒否して統計へNaNを混ぜない
		for( std::size_t index = begin; index < begin + count; ++index) { // 窓内のsample位置
			const double value = samples[index] ; // 精度を保って集計する現在の振幅
			// 元音声自体の正規化範囲を検査する
			if( !std::isfinite( value) || std::abs( value) > 1.0) {
				throw OperationError( ErrorKind::INVALID_INPUT) ;
			}
			energy += value * value ;
			peak = std::max( peak, std::abs( value)) ;
		}
		const double rms = std::sqrt( energy / static_cast<double>( count)) ; // 当該窓のRMS
		double		 gain = 1.0 ; // 無音は増幅せず、信号を捏造しない
		// 非ゼロの窓だけで除算し、元の強い音を減衰させない
		if( rms > 0 && peak > 0) {
			gain = std::max( 1.0, std::min( {K_MAXIMUM_GAIN, K_TARGET_RMS / rms, 0.95 / peak})) ;
		}
		gains.push_back( gain) ;
	}
	std::vector<float> result( samples.size()) ; // 元音声と寿命を分けるVAD専用copy
	// gainの窓中心間を連続に補間し、1秒境界での急な段差を避ける
	for( std::size_t index = 0; index < samples.size(); ++index) { // copyする元sample位置
		const double position = static_cast<double>( index) / K_FRAME_SAMPLES - 0.5 ; // 窓中心基準
		double		 gain = gains.front() ; // 最初の窓中心より前のgain
		// 先頭以外は隣接する窓中心から補間する
		if( position > 0) {
			const auto	 left = std::min( static_cast<std::size_t>( position), gains.size() - 1) ; // 左側の窓
			const auto	 right = std::min( left + 1, gains.size() - 1) ; // 終端では最後の値を維持
			const double ratio = std::clamp( position - static_cast<double>( left), 0.0, 1.0) ; // 中心間の比率
			gain = gains[left] + (gains[right] - gains[left]) * ratio ;
		}
		result[index] = static_cast<float>( std::clamp( static_cast<double>( samples[index]) * gain, -1.0, 1.0)) ;
	}
	return result ;
}

} // namespace koefude
