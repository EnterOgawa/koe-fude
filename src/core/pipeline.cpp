//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file pipeline.cpp
 * @brief 音声処理と話者付き結果の組立てを、UIから独立して実行します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 直列pipelineと部分失敗時の本文保持を追加
 * 0.1.0 Shunji Ogawa 2026/09/09 ASR profileと発話区間設定を結果へ引き継ぐ
 * 0.1.0 Shunji Ogawa 2026/09/09 実行時のモデル系統も結果へ保持
 * 0.1.0 Shunji Ogawa 2026/09/09 pipeline全体を単調時計で計測し、再実行ごとに確定
 * 0.1.0 Shunji Ogawa 2026/09/10 録音時間の固定上限を除き、64bitの全体時刻を保持
 * 0.1.0 Shunji Ogawa 2026/09/10 対応tokenの時刻で話者交代を分け、同じ候補の本文を再連結
 * 0.1.0 Shunji Ogawa 2026/09/10 交代のない発話は区間全体で結合し、frame間の過分割を防止
 * 0.1.0 Shunji Ogawa 2026/09/10 CTCは音声交代と単語境界で分け、発話内の重なりを保持
 * 0.1.0 Shunji Ogawa 2026/09/11 本文・候補を保って理由と推定範囲を付加し、取消しを確認
 * 0.1.0 Shunji Ogawa 2026/10/19 分岐意図・callbackの契約・宣言の役割を明記
 * 0.1.0 Shunji Ogawa 2026/10/19 元ASR前の全PCM照合と独立進捗を接続
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1316のASR所有確定・独立話者候補・解放後評価を追加
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
#include "core/pipeline.h"
#include "core/speaker_attribution.h"

#include <span>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace koefude {
namespace {

using Clock = std::chrono::steady_clock ; // wall-clock変更の影響を受けない処理時間計測

//------------------------------------------------------------------------------
/**
 * @brief 開始点からの経過秒を返します
 * @param[in] start 同じsteady_clockの開始点
 * @return 非負の経過秒
 */
//------------------------------------------------------------------------------
[[nodiscard]] double elapsedSeconds( const Clock::time_point start)
{
	return std::chrono::duration<double>( Clock::now() - start).count() ;
}

/** @brief 音声の交代と本文の許可終端が対応した一つの分割位置です */
struct SpeechCut {
	/// 元音声上の表示境界。前後のCTC発火の間へ制限
	std::int64_t timeMs = 0 ;
	/// 前の発話に属する元UTF-8本文の終端
	std::size_t	 textEnd = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief CTC発火そのものを発話区間とせず、音声の交代に対応する本文境界を選びます
 * @param[in] segment 全本文と時刻を検証済みのCTC区間。同期呼出し中だけ借用
 * @param[in] turns 開始順で検証済みの話者区間。現在の親区間以前の末尾を含んでよい値
 * @param[in,out] comparisons 全jobの比較回数。100万回を超えたら例外で終了
 * @param[in] stop 境界探索の途中でも確認する取消し要求
 * @return 単調な時刻とUTF-8終端。根拠のない交代は追加せず、呼出し元で混在を保持
 * @throws OperationError 比較回数の上限超過
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<SpeechCut> anchorSpeechCuts( const TextSegment					&segment,
													  const std::span<const SpeakerTurn> turns,
													  std::size_t &comparisons, const std::stop_token stop)
{
	constexpr std::int64_t	 K_BOUNDARY_RADIUS_MS = 800 ; // 発火と検出のずれを照合する最大距離
	std::vector<SpeakerTurn> relevant ; // 親区間に正の時間で重なる元話者区間
	// 現在のASR終了より前だけを読み、外側の時刻をclipする
	for( const auto &turn : turns) { // 検証済みの開始順区間
		checkCancelled( stop) ;
		// 開始順なので以後の区間は全て対象外となる
		if( turn.startMs >= segment.endMs) {
			break ;
		}
		// 入力の重なりが多い場合も無制限にworkerを占有しない
		if( ++comparisons > 1'000'000) {
			throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
		}
		// 親区間の開始前に終了した区間を交代候補にしない
		if( turn.endMs > segment.startMs) {
			relevant.push_back(
				{std::max( turn.startMs, segment.startMs), std::min( turn.endMs, segment.endMs), turn.speaker}) ;
		}
	}
	std::sort( relevant.begin(), relevant.end(),
			  //------------------------------------------------------------------------------
			  /**
			   * @brief 同時開始の包含発話を外側から走査できる順序を返します
			   * @param[in] left 比較する元音声区間
			   * @param[in] right 比較する元音声区間
			   * @return leftが開始順・終了逆順・ID順で先ならtrue
			   */
			  //------------------------------------------------------------------------------
			  []( const SpeakerTurn &left, const SpeakerTurn &right)
			  {
				  return left.startMs < right.startMs ||
						 (left.startMs == right.startMs &&
						  (left.endMs > right.endMs || (left.endMs == right.endMs && left.speaker < right.speaker))) ;
			  }) ;
	std::vector<SpeakerTurn> continuing ; // 他の発話に包含されず、終了が先へ延びる発話列
	// 同一発話に含まれる重なりで文章境界を増やさず、実際の交代を抽出する
	for( const auto &turn : relevant) { // 親ASR内の比較対象
		// 最初の発話は包含・交代の比較基準として保持する
		if( continuing.empty()) {
			continuing.push_back( turn) ;
		}
		// 同じ話者の検出間の隙間は交代にせず、次の終了まで一つにする
		else if( turn.endMs > continuing.back().endMs) {
			if( turn.speaker == continuing.back().speaker) {
				continuing.back().endMs = turn.endMs ;
			} else {
				// 長さで短い独立応答を除かず、別話者へ延びる全区間を保持する
				continuing.push_back( turn) ;
			}
		}
	}
	std::vector<std::int64_t> changes ; // 前話者の終了と次話者の開始の中点
	// 継続列の隣接区間から、元音声が示す交代候補だけを作る
	for( std::size_t index = 1; index < continuing.size(); ++index) { // 次話者の番号
		changes.push_back( (continuing[index - 1].endMs + continuing[index].startMs) / 2) ;
	}
	std::vector<SpeechCut> cuts ; // 文章の順序を保つ採用境界
	// 隣の交代との中点で探索範囲を分け、短い応答を前後の発話へ吸収しない
	for( std::size_t changeIndex = 0; changeIndex < changes.size(); ++changeIndex) { // 交代番号
		const auto	 change = changes[changeIndex] ; // 今回の音声上の交代時刻
		const auto	 lower = changeIndex == 0 ? segment.startMs // 前交代との探索境界
											  : (changes[changeIndex - 1] + change) / 2 ;
		const auto	 upper = changeIndex + 1 == changes.size() // 後交代との探索境界
								 ? segment.endMs
								 : (change + changes[changeIndex + 1]) / 2 ;
		SpeechCut	 best ; // この交代に対応する最良の本文終端。0は未発見
		auto		 bestScore = std::numeric_limits<std::int64_t>::min() ; // 候補の間隔と距離の評価値
		std::int64_t bestDistance = K_BOUNDARY_RADIUS_MS + 1 ; // 同点時の近さを比較する距離
		// 末尾以外の全token終端を確認し、単語の途中・逆順・離れた位置を除く
		for( std::size_t index = 1; index < segment.tokens.size(); ++index) { // 次発火の番号
			checkCancelled( stop) ;
			// tokenと交代の組合せも全jobの有界な比較予算へ含める
			if( ++comparisons > 1'000'000) {
				throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
			}
			const auto &left = segment.tokens[index - 1] ; // 分割前の最後の発火frame
			const auto &right = segment.tokens[index] ; // 分割後の最初の発火frame
			// 単語内や採用済み本文の位置へ戻る境界を候補にしない
			if( !left.canSplitAfter || (!cuts.empty() && left.textEnd <= cuts.back().textEnd)) {
				continue ;
			}
			const auto gapStart = std::min( left.endMs, right.startMs) ; // 重複frameにも対応
			const auto time = std::clamp( change, gapStart, right.startMs) ; // 元音声の分割時刻
			const auto distance = time > change ? time - change : change - time ; // 音声とのずれ
			// 別の交代に近い位置や、許容距離を超す文字へ無理に話者を割り当てない
			if( time <= lower || time >= upper || distance > K_BOUNDARY_RADIUS_MS ||
			   (!cuts.empty() && time <= cuts.back().timeMs)) {
				continue ;
			}
			const auto gap = std::min( 2 * K_BOUNDARY_RADIUS_MS, right.startMs - gapStart) ; // 有界間隔
			const auto score = gap - 2 * distance ; // 間隔を優先しつつ離れた候補を減点する
			// 同点なら音声交代に近い境界を使い、探索順による遠い選択を避ける
			if( score > bestScore || (score == bestScore && distance < bestDistance)) {
				best = {time, left.textEnd} ;
				bestScore = score ;
				bestDistance = distance ;
			}
		}
		// 根拠ある境界だけを採用し、未解決の交代は発話全体の混在として残す
		if( best.textEnd != 0) {
			cuts.push_back( best) ;
		}
	}
	return cuts ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 時刻の重なりから話者を注釈し、曖昧な区間は複数・不明で残します
 * @param[in] text 元音声基準のASR区間。対応tokenがあれば交代で分割。呼出し中のみ借用
 * @param[in] turns 元音声基準の話者区間。呼出し中のみ借用
 * @param[in] durationMs 元音声の有効時間（ms）、0より大きいこと
 * @param[in] stop 組立てと推定範囲の走査で確認する協調的取消し
 * @return 初出順の話者番号を持つ所有行。入力は変更しません
 * @throws OperationError 不正時刻・token対応、過大な行数・text・token数・区間比較回数、取消し
 * @note CTC発火は継続区間として扱わず、音声交代に近い単語等の許可境界で分割します
 * 発話内の候補と推定範囲を保持し、単語途中へ話者を推測配分しません
 * @see koefude::assembleTranscript
 */
//------------------------------------------------------------------------------
std::vector<TranscriptLine> assembleTranscript( const std::span<const TextSegment> text,
											   const std::span<const SpeakerTurn> turns, const std::int64_t durationMs,
											   const std::stop_token stop)
{
	checkCancelled( stop) ;
	// 有界な外部resultだけを処理し、不正な音声時間で計算しない
	if( durationMs <= 0 || durationMs > K_MAXIMUM_TIMELINE_MS || text.size() > K_MAXIMUM_SEGMENTS ||
	   turns.size() > K_MAXIMUM_SEGMENTS) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
	std::vector<SpeakerTurn> sortedTurns( turns.begin(), turns.end()) ; // 初出順を確定する所有copy
	std::sort( sortedTurns.begin(), sortedTurns.end(),
			  //------------------------------------------------------------------------------
			  /**
			   * @brief 話者の初出を開始時刻と元cluster IDの順で固定します
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
	std::map<int, int> normalized ; // engineのopaque cluster IDから初出順番号への対応
	// 各話者の初出で番号を固定し、再登場時に同じ番号を使う
	for( const auto &turn : sortedTurns) {
		// 入力区間の不正を比較処理へ持ち込まない
		if( turn.startMs < 0 || turn.endMs <= turn.startMs || turn.endMs > durationMs || turn.speaker < 0) {
			throw OperationError( ErrorKind::INVALID_TIMELINE) ;
		}
		normalized.try_emplace( turn.speaker, static_cast<int>( normalized.size()) + 1) ;
	}
	std::vector<TranscriptLine> lines ; // 呼出し元へ返す所有表示行
	std::size_t					firstTurn = 0 ; // 現在のtextより前に終了したturnを除く走査開始位置
	std::size_t					comparisons = 0 ; // 異常なoverlapによる比較爆発を防ぐ総回数
	std::size_t					textBytes = 0 ; // 1jobの結果容量
	std::int64_t				previousStart = -1 ; // ASR区間の時系列を検証する直前開始時刻
	std::size_t					tokenCount = 0 ; // 全録音の任意token情報を有界に保つ合計
	// ASRの順序を保持しながら、その時間範囲と交差するturnだけを確認する
	for( const auto &segment : text) {
		checkCancelled( stop) ;
		// 不正時刻・逆順・過大textを出力へ混入させない
		if( segment.startMs < 0 || segment.startMs < previousStart || segment.endMs < segment.startMs ||
		   segment.endMs > durationMs || segment.text.size() > K_MAXIMUM_TEXT_BYTES - textBytes ||
		   segment.tokens.size() > K_MAXIMUM_ASR_TOKENS - tokenCount) {
			throw OperationError( ErrorKind::INVALID_TIMELINE) ;
		}
		previousStart = segment.startMs ;
		textBytes += segment.text.size() ;
		tokenCount += segment.tokens.size() ;
		// 内容のないASR区間は表示行を作らない
		if( segment.text.empty()) {
			// 本文のない区間に対応tokenだけが残る不正DTOを拒否する
			if( !segment.tokens.empty()) {
				throw OperationError( ErrorKind::INVALID_TIMELINE) ;
			}
			continue ;
		}
		// 以後のASRにも重ならない先頭turnを有限範囲で読み飛ばす
		while( firstTurn < sortedTurns.size() && sortedTurns[firstTurn].endMs <= segment.startMs) {
			++firstTurn ;
		}
		const auto firstLine = lines.size() ; // 同じASR区間内だけでtoken本文を再連結する境界
		auto	   partFirstTurn = firstTurn ; // 次の重複ASR区間に走査位置を持ち越さない
		const auto appendPart =
			//------------------------------------------------------------------------------
			/**
			 * @brief 音声に基づく一部分へ話者を付け、同じ候補が続けば本文を連結します
			 * @param[in] startMs 検証済みの元音声開始ms
			 * @param[in] endMs 検証済みの元音声終了ms
			 * @param[in] contents 親区間から借用する有効UTF-8本文
			 * @note 借用はこの同期呼出し中だけ有効で、所有行へcopyします
			 */
			//------------------------------------------------------------------------------
			[&]( const std::int64_t startMs, const std::int64_t endMs, const std::string_view contents)
			{
				// 以後のtokenに重ならないturnを同じASR区間の範囲内で読み飛ばす
				while( partFirstTurn < sortedTurns.size() && sortedTurns[partFirstTurn].endMs <= startMs) {
					++partFirstTurn ;
				}
				std::map<int, std::int64_t> overlaps ; // 話者ごとの重なり時間（ms）
				std::map<int, std::int64_t> coveredEnd ; // 同じ話者の重複を二重集計しない終端
				// 部分本文の終了前に始まるturnだけを比較する
				for( std::size_t index = partFirstTurn; index < sortedTurns.size() && sortedTurns[index].startMs < endMs;
					++index) {
					checkCancelled( stop) ;
					// 大量の重複区間でもworkerを無制限に占有しない
					if( ++comparisons > 1'000'000) {
						throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
					}
					const auto &turn = sortedTurns[index] ; // 有界配列内の比較対象
					const int	speaker = normalized.at( turn.speaker) ; // 音声の初出順番号
					const auto	start = std::max( {startMs, turn.startMs, coveredEnd[speaker]}) ; // 未集計開始ms
					const auto	end = std::min( endMs, turn.endMs) ; // 未集計交差範囲の終端ms
					const auto	overlap = end - start ; // 同じ話者の重複を除いた時間（ms）
					// 正の時間を共有しない話者を候補に含めない
					if( overlap > 0) {
						overlaps[speaker] += overlap ;
						coveredEnd[speaker] = end ;
					}
				}
				TranscriptLine line{startMs, endMs, {}, std::string( contents)} ; // 所有する部分行
				const auto	   spanMs = std::max<std::int64_t>( 1, endMs - startMs) ; // ゼロ除算防止
				// 微小な接触を除き、十分重なる全話者を候補として保持する
				for( const auto &[speaker, overlap] : overlaps) { // 初出順番号と共有時間
					// 区間の15%か150msの小さい方を共有する話者を採用する
					if( overlap >= std::max<std::int64_t>( 1, std::min<std::int64_t>( 150, spanMs * 15 / 100))) {
						line.speakers.push_back( speaker) ;
					}
				}
				// 同じASR本文内の連続tokenは改行せず、元の文字を一度だけ連結する
				if( lines.size() > firstLine && lines.back().speakers == line.speakers) {
					lines.back().text += line.text ;
					lines.back().endMs = std::max( lines.back().endMs, line.endMs) ;
				} else {
					// tokenによる分割後も表示区間数の上限を維持する
					if( lines.size() >= K_MAXIMUM_SEGMENTS) {
						throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
					}
					lines.push_back( std::move( line)) ;
				}
			} ;
		// 対応tokenがない既存結果は区間全体の重なりから判定する
		if( segment.tokens.empty()) {
			appendPart( segment.startMs, segment.endMs, segment.text) ;
			continue ;
		}
		int	 firstSpeaker = -1 ; // 親区間へ正の時間で重なる最初の音声cluster
		bool hasSpeakerChange = false ; // 複数clusterがあればtoken時刻で交代を分ける
		// 短い別話者も残すため、候補の時間しきい値を適用する前に交代の有無を調べる
		for( std::size_t index = firstTurn; index < sortedTurns.size() && sortedTurns[index].startMs < segment.endMs;
			++index) { // 親ASR区間と交差し得る音声turn
			// 親区間の走査も有界な比較予算へ含める
			if( ++comparisons > 1'000'000) {
				throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
			}
			const auto &turn = sortedTurns[index] ; // 検証済みの元音声区間
			// 開始順の配列内に残る、既に終了した重複turnを候補へ含めない
			if( turn.endMs <= segment.startMs) {
				continue ;
			}
			// 最初のclusterを基準にして、同じ人の発話内の隙間では分割しない
			if( firstSpeaker < 0) {
				firstSpeaker = turn.speaker ;
				// 少しでも別clusterが重なる場合は、全tokenの対応へ進む
			} else if( firstSpeaker != turn.speaker) {
				hasSpeakerChange = true ;
				break ;
			}
		}
		std::size_t textBegin = 0 ; // 次のtokenへ一度だけ割り当てる本文の先頭byte
		auto		tokenStart = segment.startMs ; // token開始時刻の単調性を確認する前値
		// 全tokenの対応byteと時刻を確認し、同じ原文の連続範囲だけを処理する
		for( const auto &token : segment.tokens) { // engine所有物からcopy済みの対応情報
			// UTF-8途中や範囲外・逆順・本文の欠落を推測で補正しない
			if( token.startMs < tokenStart || token.endMs <= token.startMs || token.endMs > segment.endMs ||
			   token.textEnd <= textBegin || token.textEnd > segment.text.size() ||
			   (token.textEnd < segment.text.size() &&
				(static_cast<unsigned char>( segment.text[token.textEnd]) & 0xc0U) == 0x80U)) {
				throw OperationError( ErrorKind::INVALID_TIMELINE) ;
			}
			tokenStart = token.startMs ;
			// 話者交代のある区間だけを分割し、細かい検出の隙間で一人の発話を壊さない
			if( hasSpeakerChange && !segment.tokenTimesAreAnchors) {
				appendPart( token.startMs, token.endMs,
						   std::string_view( segment.text).substr( textBegin, token.textEnd - textBegin)) ;
			}
			textBegin = token.textEnd ;
		}
		// 最後の本文まで対応していない部分的なalignmentを正常としない
		if( textBegin != segment.text.size()) {
			throw OperationError( ErrorKind::INVALID_TIMELINE) ;
		}
		// CTCの全発火を検証した後、音声交代に対応する単語境界だけで親区間を分ける
		if( hasSpeakerChange && segment.tokenTimesAreAnchors) {
			// 元音声と本文の順序が対応した所有分割位置
			const auto	cuts = anchorSpeechCuts( segment, std::span( sortedTurns).subspan( firstTurn), comparisons, stop) ;
			std::size_t begin = 0 ; // 元本文を一度だけ出力する次のbyte
			auto		start = segment.startMs ; // この発話全体の元音声開始ms
			// 各採用境界までの全文と元話者区間を照合し、短い重なりも候補として保持
			for( const auto &cut : cuts) { // 単調な本文終端と時刻
				appendPart( start, cut.timeMs, std::string_view( segment.text).substr( begin, cut.textEnd - begin)) ;
				start = cut.timeMs ;
				begin = cut.textEnd ;
			}
			appendPart( start, segment.endMs, std::string_view( segment.text).substr( begin)) ;
		}
		// tokenの検証は省かず、一人以下なら既存の区間全体の重なり規則を維持する
		else if( !hasSpeakerChange) {
			appendPart( segment.startMs, segment.endMs, segment.text) ;
		}
	}
	populateSpeakerAttribution( lines, sortedTurns, stop) ;
	return lines ;
}

//------------------------------------------------------------------------------
/**
 * @brief 事前確保した所有結果を同じpipeline本体で直接構築します
 * @param[out] result 呼出し前に確保した空の所有holder。失敗時は未公開の部分値です
 * @param[in] input local入力path
 * @param[in] options 受付時の固定設定
 * @param[in] decoder 必須の同期decoder
 * @param[in,out] transcriber 同期ASR
 * @param[in,out] diarizer 同期話者処理
 * @param[in] stop 元user取消し
 * @param[in] progress 任意の同期通知
 * @param[in,out] resources 任意の同期資源制御
 * @note MSVCのmap move構築は確保を伴うため、製品factoryは値返却を経由しません
 * finish後に結果のmove構築・copy・共有holder確保を行わない同一本体です
 * @see koefude::detail::runPipelineInto
 */
//------------------------------------------------------------------------------
void detail::runPipelineInto( Transcript &result, const std::filesystem::path &input, const EngineOptions &options,
							 const AudioDecoder &decoder, ITranscriptionEngine &transcriber, ISpeakerDiarizer &diarizer,
							 const std::stop_token stop, const ProgressCallback &progress,
							 IPipelineResourceControl *resources)
{
	const auto pipelineStart = Clock::now() ; // queue待機を除く合計時間の開始点
	const auto processingStop = resources ? resources->processingStop() : stop ; // 実効取消し
	const auto checkpoint =
		//------------------------------------------------------------------------------
		/**
		 * @brief 同じ制御の強制評価を行い、旧入口では元userだけを確認します
		 * @param[in] boundary 所有権と段階の境界
		 */
		//------------------------------------------------------------------------------
		[resources, stop]( const PipelineMemoryBoundary boundary)
		{
			// 旧呼出しへOS依存や監視threadを追加しません
			if( resources) {
				resources->checkpoint( boundary) ;
			} else {
				checkCancelled( stop) ;
			}
		} ;
	// 事前確保したholderのASR-only本文を候補と独立して保持します
	result.speakerMethod = options.speakerMethod ;
	result.asrProfile = options.asrProfile ;
	result.asrModel = options.asrModel ;
	result.speechFilter = options.speechFilter ;
	result.diarizationRequested = options.diarization ;
	std::vector<SpeakerTurn>	speakerTurns ; // 最終評価まで確定しない話者候補
	std::vector<TranscriptLine> speakerLines ; // ASR-onlyを置換しない所有本文候補
	int							speakerCount = 0 ; // 候補に存在する全cluster数
	bool						asrCommitted = false ; // Unknown行と全metadataの正常な所有確定
	bool						failed = false ; // 解放後に分類する処理失敗の有無
	bool						speakerFailure = false ; // 話者try内だけの失敗を外側callback失敗から区別します
	ErrorKind					errorKind = ErrorKind::INTERNAL ; // 非資源の元分類も維持する値
	std::uint32_t				errorCode = 0 ; // 元native/HRESULTまたは確保失敗8
	try {
		checkpoint( PipelineMemoryBoundary::BEFORE_DECODE) ;
		// 依存が欠けた状態で同期decoderを実行しません
		if( !decoder) {
			throw OperationError( ErrorKind::INTERNAL) ;
		}
		const auto decodeStart = Clock::now() ; // decodeと全PCM照合の開始点
		const auto audio = decoder( input, processingStop, progress) ; // このscopeだけのPCM
		checkpoint( PipelineMemoryBoundary::AFTER_DECODE_BEFORE_IDENTITY) ;
		result.pcmIdentity = fingerprintDecodedPcm( audio, processingStop, progress) ;
		result.decodeSeconds = elapsedSeconds( decodeStart) ;
		result.durationSeconds = static_cast<double>( audio.sampleCount()) / K_SAMPLE_RATE ;
		checkpoint( PipelineMemoryBoundary::AFTER_IDENTITY_BEFORE_ASR) ;
		const auto transcriptionStart = Clock::now() ; // model検証を含むASR開始点
		const auto recognition = transcriber.transcribe( audio, options, processingStop, progress) ;
		// native returnだけでは保持可能な本文の成立としません
		checkpoint( PipelineMemoryBoundary::AFTER_ASR_BEFORE_UNKNOWN_SNAPSHOT) ;
		result.transcriptionSeconds = elapsedSeconds( transcriptionStart) ;
		result.usedGpu = recognition.usedGpu ;
		result.cpuRetried = recognition.cpuRetried ;
		const auto durationMs = static_cast<std::int64_t>( audio.sampleCount() / 16) ; // 元音声ms
		result.lines = assembleTranscript( recognition.segments, {}, durationMs, processingStop) ;
		asrCommitted = true ;
		checkpoint( PipelineMemoryBoundary::ASR_ONLY_SNAPSHOT_COMMITTED) ;

		// 話者を依頼した場合だけ、保持済みUnknown本文とは別の候補を作ります
		if( options.diarization) {
			const auto diarizationStart = Clock::now() ; // 話者検証・推定・結合の開始点
			try {
				checkpoint( PipelineMemoryBoundary::BEFORE_DIARIZER) ;
				speakerTurns = diarizer.diarize( audio, options, processingStop, progress) ;
				checkpoint( PipelineMemoryBoundary::AFTER_DIARIZER) ;
				reportProgress( progress, Stage::COMBINING, 0) ;
				speakerLines = assembleTranscript( recognition.segments, speakerTurns, durationMs, processingStop) ;
				std::set<int> speakers ; // 表示text外の区間も含むcluster集合
				// 候補全体のclusterを数え、初出番号や結合規則は変更しません
				for( const auto &turn : speakerTurns) { // 今回だけの推定候補
					speakers.insert( turn.speaker) ;
				}
				speakerCount = static_cast<int>( speakers.size()) ;
				checkpoint( PipelineMemoryBoundary::AFTER_SPEAKER_COMBINE) ;
			} catch( ...) {
				speakerFailure = true ;
				result.diarizationSeconds = elapsedSeconds( diarizationStart) ;
				throw ; // 外側でPCM/Recognition解放後に元分類を確定します
			}
			result.diarizationSeconds = elapsedSeconds( diarizationStart) ;
		}
		reportProgress( progress, Stage::COMBINING, 100) ;
		checkCancelled( stop) ;
	} catch( const OperationError &error) {
		failed = true ;
		errorKind = error.kind() ;
		errorCode = error.code() ;
	} catch( const std::bad_alloc &) {
		failed = true ;
		errorKind = ErrorKind::INPUT_TOO_LARGE ;
		errorCode = 8 ; // ERROR_NOT_ENOUGH_MEMORYのOS非依存な診断値
	} catch( ...) {
		failed = true ;
		errorKind = ErrorKind::INTERNAL ;
	}
	// 失敗候補の要素を追加確保なしで破棄し、ASR-onlyは再構築しません
	// containerの残容量はscope終了時に解放し、Debug proxyのための救済確保も避けます
	if( failed) {
		speakerTurns.clear() ;
		speakerLines.clear() ;
	}
	// 上のscopeを抜け、全native・PCM・Recognitionの解放後に一度だけ確定します
	const auto resourceFailure = resources ? resources->finish() : PipelineResourceFailure{} ;
	// 同時停止とfinalize中のuser取消しも資源警告より常に優先します
	if( stop.stop_requested()) {
		throw OperationError( ErrorKind::CANCELLED, errorCode) ;
	}
	// effective Cancelledを含む資源由来の停止だけを利用可能RAMの分類へ戻します
	if( resourceFailure.reason != PipelineResourceStopReason::NONE) {
		failed = true ;
		errorKind = ErrorKind::INPUT_TOO_LARGE ;
		errorCode = errorCode != 0 ? errorCode : resourceFailure.code ;
	}
	// 元userでない取消しでも、資源観測なしの既存取消し意味は維持します
	if( failed && (!asrCommitted || !options.diarization || errorKind == ErrorKind::CANCELLED ||
				  (!speakerFailure && resourceFailure.reason == PipelineResourceStopReason::NONE))) {
		throw OperationError( errorKind, errorCode) ;
	}
	// 所有成立後の話者失敗はUnknown本文をそのまま警告付きで返します
	if( failed) {
		result.diarizationWarning = true ;
		result.diarizationError = errorKind ;
		result.diarizationCode = errorCode ;
	}
	// 正常に完了した話者依頼だけで、最終評価済み候補を確定します
	else if( options.diarization) {
		// 最後の資源評価とuser確認を通過した候補だけをnoexcept moveで確定します
		result.lines = std::move( speakerLines) ;
		result.speakerTurns = std::move( speakerTurns) ;
		result.speakerCount = speakerCount ;
	}
	result.totalSeconds = elapsedSeconds( pipelineStart) ;
	// 通常controllerが終端Completedを通知するため、guard終了後の外部callbackを避けます
	if( !resources) {
		reportProgress( progress, Stage::COMPLETED, 100) ;
	}
	// 資源警告をsticky/effective停止で再throwせず、元userだけを最後に確認します
	checkCancelled( stop) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 一つの音声ジョブを読み込みから話者付きtextまで処理します
 * @param[in] input 利用者が指定したlocal path
 * @param[in] options ジョブ受付時に固定した設定
 * @param[in] decoder 呼出し中に有効な同期decoder
 * @param[in,out] transcriber worker専用ASR instance
 * @param[in,out] diarizer worker専用話者分離instance
 * @param[in] stop 元user取消し。資源停止とは区別します
 * @param[in] progress worker上の通知先。空を許可
 * @param[in,out] resources 任意の同期borrow。nullは従来どおりOS照会・監視threadなし
 * native・PCM・Recognition解放後にfinishし、呼出し終了後は保持しません
 * @return 完了結果。話者分離だけの失敗時は本文と警告を保持
 * @throws OperationError decode/ASR/結合失敗、取消し
 * @see koefude::runPipeline
 */
//------------------------------------------------------------------------------
Transcript runPipeline( const std::filesystem::path &input, const EngineOptions &options, const AudioDecoder &decoder,
					   ITranscriptionEngine &transcriber, ISpeakerDiarizer &diarizer, const std::stop_token stop,
					   const ProgressCallback &progress, IPipelineResourceControl *resources)
{
	Transcript result ; // 値返却を維持する旧source互換入口の所有結果
	detail::runPipelineInto( result, input, options, decoder, transcriber, diarizer, stop, progress, resources) ;
	return result ;
}

} // namespace koefude
