//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file shared_asr_windows.cpp
 * @brief primaryから受けた元窓を変形せず読みます
 * @version 1.0.0
 * @date 2026/10/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/09 有限な元sample窓の共通検査を実装
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
#include "core/shared_asr_windows.h"

#include <span>
#include <cmath>
#include <cstddef>
#include <limits>

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief 元sample窓の件数/byte・順序・全PCM範囲を検査します
 * @param[in] windows 最大K_MAXIMUM_SEGMENTSの借用窓。空可
 * @param[in] sampleCount 正の全PCM sample数。録音時間の製品固定上限ではありません
 * @throws OperationError 件数/byte超過、不正・逆順・重複・範囲外窓
 * @see koefude::checkSharedAsrWindows
 */
//------------------------------------------------------------------------------
void checkSharedAsrWindows( const std::span<const AsrSampleWindow> windows, const std::uint64_t sampleCount)
{
	// 件数とbyteをallocation前に検査し、製品の表現可能な全範囲だけを受けます
	if( sampleCount == 0 || sampleCount > K_MAXIMUM_TIMELINE_SAMPLES || windows.size() > K_MAXIMUM_SEGMENTS ||
	   windows.size() > std::numeric_limits<std::size_t>::max() / sizeof(AsrSampleWindow)) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
	std::uint64_t previousEnd = 0 ; // 直前の実窓終端。重複と逆順を検出します
	// 全窓を一度確認し、不正な最初の窓で停止します
	for( const auto &window : windows) { // 現在の元sample窓
		// 無音の間隔は許し、不正・範囲外・重複窓を修復せず拒否します
		if( window.begin >= window.end || window.end > sampleCount || window.begin < previousEnd ||
		   window.end - window.begin > K_SHARED_ASR_WINDOW_SAMPLES) {
			throw OperationError( ErrorKind::INVALID_TIMELINE) ;
		}
		previousEnd = window.end ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 検査済み窓の原PCMだけを有界bufferへ読み込みます
 * @param[in] audio 呼出し中有効な全PCM。内容や原点を変更しません
 * @param[in] window 元sampleの半開窓。最大26秒です
 * @return 元PCMと同じfloat値の所有copy。gain・正規化を加えません
 * @throws OperationError 不正範囲、不正PCM、I/O失敗
 * @see koefude::readSharedAsrWindow
 */
//------------------------------------------------------------------------------
std::vector<float> readSharedAsrWindow( const AudioData &audio, const AsrSampleWindow window)
{
	checkSharedAsrWindows( std::span( &window, 1), audio.sampleCount()) ;
	const auto		   count = static_cast<std::size_t>( window.end - window.begin) ; // 検査済み有界要素数
	std::vector<float> samples( count) ; // 同期認識中だけ所有する原PCM copy
	audio.readSamples( window.begin, samples) ;
	// decoder以外のfake/内部sourceからの非有限・非正規化PCMもnative境界で拒否します
	for( const auto sample : samples) { // 当該窓の原sample
		// 不正な入力へgainやclampを適用せず、元sourceを変更しないまま失敗します
		if( !std::isfinite( sample) || sample < -1.0F || sample > 1.0F) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
	}
	return samples ;
}

//------------------------------------------------------------------------------
/**
 * @brief 明示観測時だけ実窓を有限追加し、直前窓との境界を検査します
 * @param[in,out] recognition workerの所有部分結果。失敗前のOriginal/sidecarは保持します
 * @param[in] window nativeへ実際に渡した元sample窓
 * @param[in] sampleCount 同じ全原PCMのsample数
 * @param[in] observer 同期借用通知。空なら通常認識として窓追加/新cap検査を行いません
 * @throws OperationError 観測窓の件数/byte/境界超過。callback例外は所有RAII scopeへ戻ります
 * @see koefude::captureAsrWindow
 */
//------------------------------------------------------------------------------
void captureAsrWindow( Recognition &recognition, const AsrSampleWindow window, const std::uint64_t sampleCount,
					  const RecognitionObserver &observer)
{
	// 観測を要求しない通常productでは新export/capで旧成功条件を変えません
	if( !observer) {
		return ;
	}
	// 追加前に件数/byteを検査し、既に正当な部分結果を変更しません
	if( recognition.inputWindows.size() >= K_MAXIMUM_SEGMENTS ||
	   recognition.inputWindows.size() >= std::numeric_limits<std::size_t>::max() / sizeof(AsrSampleWindow)) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
	checkSharedAsrWindows( std::span( &window, 1), sampleCount) ;
	// 直前終端との比較だけで順序を確認し、毎窓の全列再walkを避けます
	if( !recognition.inputWindows.empty() && window.begin < recognition.inputWindows.back().end) {
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
	recognition.inputWindows.push_back( window) ;
	observer( recognition) ;
}

} // namespace koefude
