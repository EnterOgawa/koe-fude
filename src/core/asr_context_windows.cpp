//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file asr_context_windows.cpp
 * @brief 音声だけの文脈窓と、本文不変の中心token選択を実装します
 * @version 1.0.0
 * @date 2026/09/11
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/11 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/11 VAD範囲の保存、短い末尾、UTF-8と取消しを実装
 * 0.1.0 Shunji Ogawa 2026/09/12 片側文脈の上限と完了前の取消しを明示検証
 * 0.1.0 Shunji Ogawa 2026/10/19 宣言の役割を明記
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "core/asr_context_windows.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>
#include <string_view>
#include <vector>

#include "core/audio_data.h"
#include "core/types.h"
#include "platform/win_helpers.h"

namespace koefude {
namespace {

constexpr std::size_t K_MINIMUM_SAMPLES = 1280 ; // 固定CTCが要求する80ms
constexpr std::size_t K_CORE_SAMPLES = 20 * K_SAMPLE_RATE ; // 比較条件として固定する中心20秒
constexpr std::size_t K_CONTEXT_SAMPLES = 3 * K_SAMPLE_RATE ; // 各側に渡す文脈の上限3秒

//------------------------------------------------------------------------------
/**
 * @brief 停止要求を部分成功へ変えません
 * @param[in] stop 呼出し元の取消し
 */
//------------------------------------------------------------------------------
void				  checkStop( const std::stop_token stop)
{
	// 次の検証や所有copyを開始せず、呼出し元へ取消しを返す
	if( stop.stop_requested()) {
		throw OperationError( ErrorKind::CANCELLED) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 入力窓と中心の容量・包含関係を確認します
 * @param[in] window 検証対象のsample範囲
 */
//------------------------------------------------------------------------------
void validateWindow( const AsrContextWindow window)
{
	// 引き算とmsへの変換に先立ち、範囲・最大26秒・中心20秒以下を確認する
	if( window.input.begin >= window.input.end || window.input.end > K_ASR_CHUNK_SAMPLES ||
	   window.input.end - window.input.begin < K_MINIMUM_SAMPLES ||
	   window.input.end - window.input.begin > K_CORE_SAMPLES + 2 * K_CONTEXT_SAMPLES ||
	   window.core.begin < window.input.begin || window.core.end > window.input.end ||
	   window.core.begin >= window.core.end || window.core.end - window.core.begin > K_CORE_SAMPLES ||
	   window.core.begin - window.input.begin > K_CONTEXT_SAMPLES ||
	   window.input.end - window.core.end > K_CONTEXT_SAMPLES) {
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief VADの和集合を保って中心20秒・左右3秒の比較窓を配置します
 * @param[in] speech 開始順の非重複範囲。隣接のみ結合し、隙間は埋めません。空を許可
 * @param[in] sampleCount 元16kHz mono PCMの要素数。80ms～10分
 * @param[in] shifted trueなら各連続範囲の最初の中心だけ10秒、以降は20秒
 * @param[in] stop 検証と各窓の作成時に確認する取消し
 * @return 中心がVAD範囲を過不足なく覆う所有窓列。失敗時に部分結果を返しません
 * @throws OperationError 不正範囲、上限超過、取消し
 * @see koefude::makeAsrContextWindows
 */
//------------------------------------------------------------------------------
std::vector<AsrContextWindow> makeAsrContextWindows( const std::span<const AsrWindow> speech,
													const std::size_t sampleCount, const bool shifted,
													const std::stop_token stop)
{
	checkStop( stop) ;
	// 有界PCMの契約を検証してから結果用containerを確保する
	if( sampleCount < K_MINIMUM_SAMPLES || sampleCount > K_ASR_CHUNK_SAMPLES ||
	   speech.size() > K_MAXIMUM_CONTEXT_WINDOWS) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
	std::vector<AsrWindow> runs ; // 隣接VAD窓だけを結合した、同じ音声の和集合
	// 逆順や重複を拒否し、元のVADの隙間を最後まで保つ
	for( const auto window : speech) { // 次の元VAD範囲
		checkStop( stop) ;
		// 不正範囲と過小入力を、別窓による音声補間で隠さない
		if( window.begin >= window.end || window.end > sampleCount || window.end - window.begin < K_MINIMUM_SAMPLES ||
		   (!runs.empty() && window.begin < runs.back().end)) {
			throw OperationError( ErrorKind::INVALID_TIMELINE) ;
		}
		// 元の分割点が接している場合だけ、連続発話として窓を置き直す
		if( !runs.empty() && window.begin == runs.back().end) {
			runs.back().end = window.end ;
		} else {
			runs.push_back( window) ;
		}
	}
	std::vector<AsrContextWindow> result ; // 元sampleに対する非重複中心と文脈入力
	// 各VAD連続範囲の末尾に達するまで、中心を一度ずつ覆う
	for( const auto run : runs) { // VADの隙間で分かれた一範囲
		auto begin = run.begin ; // 次の中心の開始sample
		// 初回10秒の配置でも最初の音声を捨てず、その後を20秒ごとに進める
		while( begin < run.end) {
			checkStop( stop) ;
			// 最初の中心か通常の中心かに対応するsample上限
			const auto			   length = shifted && begin == run.begin ? K_CORE_SAMPLES / 2 : K_CORE_SAMPLES ;
			const auto			   end = begin + std::min( length, run.end - begin) ; // overflowなしの終端
			const AsrContextWindow window{{begin - std::min( K_CONTEXT_SAMPLES, begin - run.begin),
										   end + std::min( K_CONTEXT_SAMPLES, run.end - end)},
										  {begin, end}} ; // VAD範囲内へclipした最大26秒の入力と中心
			validateWindow( window) ;
			// 件数を超えてから蓄積することを避ける
			if( result.size() == K_MAXIMUM_CONTEXT_WINDOWS) {
				throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
			}
			result.push_back( window) ;
			begin = end ;
		}
	}
	checkStop( stop) ;
	return result ;
}

//------------------------------------------------------------------------------
/**
 * @brief tokenの元byteと発火anchorで、一つの中心に属する候補を取り出します
 * @param[in] raw 入力窓全体の所有本文。非空なら全UTF-8を覆う512個以内のtokenが必須
 * @param[in] window 同じPCMに対する入力と中心。時刻は既存と同じsample/16のms
 * @param[in] stop 検証とtoken選択時に確認する取消し
 * @return 元本文の連続範囲とtoken時刻、元index、切断の確認理由。rawは変更しません
 * @throws OperationError 不正時刻、UTF-8不正、本文被覆なし、上限超過、取消し
 * @note 分割許可のない位置も比較候補として保持し、製品本文への自動採用は行いません
 * 空本文は空候補です。失敗時に部分結果を返しません
 * @see koefude::selectContextTokens
 */
//------------------------------------------------------------------------------
ContextSelection selectContextTokens( const TextSegment &raw, const AsrContextWindow window, const std::stop_token stop)
{
	checkStop( stop) ;
	validateWindow( window) ;
	// copy前に本文とtokenの独立した上限を確認する
	if( raw.text.size() > K_MAXIMUM_CONTEXT_TEXT_BYTES || raw.tokens.size() > K_MAXIMUM_CONTEXT_TOKENS) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
	const auto inputBegin = static_cast<std::int64_t>( window.input.begin / 16) ; // 元入力のms
	const auto inputEnd = static_cast<std::int64_t>( window.input.end / 16) ; // 元入力の終端ms
	const auto coreBegin = static_cast<std::int64_t>( window.core.begin / 16) ; // 所属の始端ms
	const auto coreEnd = static_cast<std::int64_t>( window.core.end / 16) ; // 所属の排他的終端ms
	// 別の音声窓の結果や、anchorでない継続時間を同じ条件として扱わない
	if( raw.startMs != inputBegin || raw.endMs != inputEnd || (!raw.text.empty() && !raw.tokenTimesAreAnchors)) {
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
	static_cast<void>( toWide( raw.text)) ; // 不正UTF-8を所属判定前に全体で拒否する
	std::size_t		 previousByte = 0 ; // 次のtokenの元本文先頭byte
	auto			 previousTime = inputBegin ; // 単調な発火anchorの前値
	ContextSelection result ; // 完全に成功したときだけ返す局所所有候補
	result.segment.startMs = inputBegin ;
	result.segment.endMs = inputEnd ;
	result.segment.tokenTimesAreAnchors = raw.tokenTimesAreAnchors ;
	// 中心外を含む全tokenの時刻と本文被覆を検証し、所属した元byteだけをcopyする
	for( std::size_t index = 0; index < raw.tokens.size(); ++index) { // 元tokenのindex
		checkStop( stop) ;
		const auto &token = raw.tokens[index] ; // rawの寿命内で借用する一token
		// UTF-8途中、逆順、時刻の逸脱、本文の欠落を補正して成功させない
		if( token.startMs < previousTime || token.endMs <= token.startMs || token.endMs > inputEnd ||
		   token.textEnd <= previousByte || token.textEnd > raw.text.size() ||
		   (token.textEnd < raw.text.size() &&
			(static_cast<unsigned char>( raw.text[token.textEnd]) & 0xc0U) == 0x80U)) {
			throw OperationError( ErrorKind::INVALID_TIMELINE) ;
		}
		// 終端ちょうどの発火は次の中心へ所属させ、文字列の同一性は判定に使わない
		if( token.startMs >= coreBegin && token.startMs < coreEnd) {
			// 最初の所属tokenより前の切断が単語途中かを記録する
			if( result.sourceTokens.empty()) {
				result.cutsWordBefore = index > 0 && !raw.tokens[index - 1].canSplitAfter ;
			}
			result.segment.text.append( raw.text, previousByte, token.textEnd - previousByte) ;
			result.segment.tokens.push_back(
				{token.startMs, token.endMs, result.segment.text.size(), token.canSplitAfter}) ;
			result.sourceTokens.push_back( index) ;
			result.cutsWordAfter = index + 1 < raw.tokens.size() && !token.canSplitAfter ;
			result.anchorCrossesEnd = result.anchorCrossesEnd || token.endMs > coreEnd ;
		}
		previousByte = token.textEnd ;
		previousTime = token.startMs ;
	}
	// 時刻なしの非空本文や最後のbyteまで対応しない結果を拒否する
	if( previousByte != raw.text.size()) {
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
	checkStop( stop) ;
	return result ;
}

} // namespace koefude
