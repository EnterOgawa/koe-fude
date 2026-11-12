//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file speaker_attribution.cpp
 * @brief 同時に有効な候補IDの集合を有界な時間範囲として保存します
 * @version 1.0.0
 * @date 2026/09/11
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/11 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/11 同じIDの重複と逐次交代を区別する推定根拠を追加
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
#include "core/speaker_attribution.h"

#include <span>
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace koefude {
namespace {

/** @brief 推定区間の開始・終了を同時刻ごとに集約する走査点です */
struct TurnEvent {
	/// 元録音上の境界時刻（ms）
	std::int64_t timeMs = 0 ;
	/// 正規化済みの1始まり話者候補
	int			 speaker = 0 ;
	/// 開始は+1、終了は-1。同じIDの重複も数えて和集合にする
	int			 delta = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 外部値の不整合を本文を含まない分類で拒否します
 * @param[in] valid 成立すべき条件
 */
//------------------------------------------------------------------------------
void requireAttribution( const bool valid)
{
	// 不正な範囲からもっともらしい話者理由を作らない
	if( !valid) {
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 検索前に親行の時刻と候補IDの昇順を確認します
 * @param[in] line 本文を変更しない親行のborrow
 */
//------------------------------------------------------------------------------
void validateLineIdentity( const TranscriptLine &line)
{
	requireAttribution( line.startMs >= 0 && line.endMs >= line.startMs && line.endMs <= K_MAXIMUM_TIMELINE_MS &&
					   line.speakers.size() <= K_MAXIMUM_SEGMENTS) ;
	int previousId = 0 ; // 行の候補は重複なしの正規化済み昇順
	// 不正な順序をbinary_searchの前提へ渡さない
	for( const int speaker : line.speakers) { // 行全体の候補ID
		requireAttribution( speaker > previousId && speaker <= static_cast<int>( K_MAXIMUM_SEGMENTS)) ;
		previousId = speaker ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 走査回数の上限と取消しを確認します
 * @param[in,out] comparisons 今回のjobで処理済みの比較回数
 * @param[in] stop workerが監視する取消し要求
 */
//------------------------------------------------------------------------------
void checkComparison( std::size_t &comparisons, const std::stop_token stop)
{
	checkCancelled( stop) ;
	// 同じ区間を重複ASRへ照合する場合も有限の作業量へ制限する
	if( ++comparisons > 1'000'000) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 次の推定範囲とIDの分だけ、所有情報の残量を確認します
 * @param[in] count 追加する範囲数またはID数
 * @param[in,out] total 同じjobで保持する項目の合計
 */
//------------------------------------------------------------------------------
void addItems( const std::size_t count, std::size_t &total)
{
	// 加算前に照合し、範囲や短い候補を黙って切り捨てない
	if( total > K_MAXIMUM_ATTRIBUTION_ITEMS || count > K_MAXIMUM_ATTRIBUTION_ITEMS - total) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
	total += count ;
}

//------------------------------------------------------------------------------
/**
 * @brief 候補集合と推定重なりの有無を、表示の根拠種別へ変換します
 * @param[in] speakerCount 採用した行全体の候補数
 * @param[in] overlaps 正の時間で複数IDが同時に有効ならtrue
 * @return 未記録を除く新規結果の種別
 */
//------------------------------------------------------------------------------
[[nodiscard]] AttributionKind classify( const std::size_t speakerCount, const bool overlaps)
{
	// 候補がない本文は、空の推定結果として明示する
	if( speakerCount == 0) {
		return AttributionKind::UNASSIGNED ;
	}
	// 一つの候補の検出間の隙間を、架空の話者交代へ変換しない
	if( speakerCount == 1) {
		return AttributionKind::SINGLE_SPEAKER ;
	}
	return overlaps ? AttributionKind::ESTIMATED_OVERLAP : AttributionKind::UNRESOLVED_CHANGE ;
}

//------------------------------------------------------------------------------
/**
 * @brief 同じ集合が有効な正の区間だけを追加し、隣接した同じ集合をまとめます
 * @param[in,out] evidence 現在の行が所有する作成途中の推定根拠
 * @param[in] active 同じIDの重複数を含む、有効IDの昇順map
 * @param[in] startMs 元録音の開始時刻（ms）
 * @param[in] endMs 元録音の終了時刻（ms）
 * @param[in,out] totalItems 同じjobの保存項目数
 * @param[in,out] comparisons 同じjobの比較回数
 * @param[in] stop 走査中の取消し要求
 */
//------------------------------------------------------------------------------
void appendSpan( SpeakerAttribution &evidence, const std::map<int, int> &active, const std::int64_t startMs,
				const std::int64_t endMs, std::size_t &totalItems, std::size_t &comparisons, const std::stop_token stop)
{
	// 端で接するだけの集合や、根拠のない空白を推定発話として追加しない
	if( startMs == endMs || active.empty()) {
		return ;
	}
	std::vector<int> speakers ; // 現在の区間で同時に有効な候補ID
	// mapのIDを一回ずつ使い、同じIDの重複turnを二重に数えない
	for( const auto &[speaker, count] : active) { // 正規化済みIDと有効turn数
		checkComparison( comparisons, stop) ;
		requireAttribution( count > 0) ;
		speakers.push_back( speaker) ;
	}
	// 同じ集合の内部でturnが増減しても、区間数を増やさない
	if( !evidence.spans.empty() && evidence.spans.back().endMs == startMs &&
	   evidence.spans.back().speakers == speakers) {
		evidence.spans.back().endMs = endMs ;
		return ;
	}
	addItems( 1 + speakers.size(), totalItems) ;
	evidence.spans.push_back( {startMs, endMs, std::move( speakers)}) ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 種別と推定範囲、ID集合の整合性を確認します
 * @param[in] line 読取り専用の元行。旧履歴のUnavailableと空範囲も許可
 * @param[in,out] totalItems 同じjob内で検証済みの範囲数と範囲内ID数の合計
 * @throws OperationError 不正時刻・ID・種別、保存容量上限超過
 * @note 失敗時の合計は途中値です。本文を変換・分配せず、外部I/Oを行いません
 * @see koefude::validateSpeakerAttribution
 */
//------------------------------------------------------------------------------
void validateSpeakerAttribution( const TranscriptLine &line, std::size_t &totalItems)
{
	validateLineIdentity( line) ;
	const auto &evidence = line.attribution ; // 元行が所有する根拠のborrow
	// 旧結果の未記録を新しい推定理由へ読み替えない
	if( evidence.kind == AttributionKind::UNAVAILABLE) {
		requireAttribution( evidence.spans.empty()) ;
		return ;
	}
	addItems( evidence.spans.size(), totalItems) ;
	auto		  previousEnd = line.startMs ; // 重なりと逆順を検出する直前の終端
	std::set<int> observed ; // 範囲に実在した候補IDの和集合
	bool		  overlaps = false ; // 正の同時範囲が少なくとも一つある場合true
	// 各範囲が親行内にあり、候補集合と整合することを確かめる
	for( const auto &span : evidence.spans) { // 読取り専用の一つの半開区間
		addItems( span.speakers.size(), totalItems) ;
		requireAttribution( span.startMs >= previousEnd && span.endMs > span.startMs && span.endMs <= line.endMs &&
						   !span.speakers.empty()) ;
		previousEnd = span.endMs ;
		int previousId = 0 ; // この範囲の候補は重複なしの昇順
		// 未採用・未知・重複IDを、表示上の根拠へ混入させない
		for( const int speaker : span.speakers) { // この範囲で有効な候補
			requireAttribution( speaker > previousId &&
							   std::binary_search( line.speakers.begin(), line.speakers.end(), speaker)) ;
			previousId = speaker ;
			observed.insert( speaker) ;
		}
		overlaps = overlaps || span.speakers.size() > 1 ;
	}
	requireAttribution( std::equal( observed.begin(), observed.end(), line.speakers.begin(), line.speakers.end()) &&
					   evidence.kind == classify( line.speakers.size(), overlaps)) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 本文・時刻・候補IDを変えず、各行へ候補の推定範囲と理由を付けます
 * @param[in,out] lines workerが所有する未公開行。成功時は全行のattributionを更新
 * @param[in] turns 元録音全体のengine cluster ID付き区間。本呼出し中だけ借用
 * @param[in] stop 各走査中に確認する協調的取消し
 * @throws OperationError 不正時刻・未知ID・比較/保存容量上限・取消し
 * @note 失敗時は一部のattributionが更新され得るため、呼出側は未公開結果全体を破棄します
 * 候補へ採用されていない微小turnは追加せず、元turnsも変更しません
 * @see koefude::populateSpeakerAttribution
 */
//------------------------------------------------------------------------------
void populateSpeakerAttribution( const std::span<TranscriptLine> lines, const std::span<const SpeakerTurn> turns,
								const std::stop_token stop)
{
	checkCancelled( stop) ;
	// 入力containerも全録音の上限以内へ制限する
	if( lines.size() > K_MAXIMUM_SEGMENTS || turns.size() > K_MAXIMUM_SEGMENTS) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
	std::vector<SpeakerTurn> ordered( turns.begin(), turns.end()) ; // 元turnを変更しない所有copy
	std::sort( ordered.begin(), ordered.end(),
			  //------------------------------------------------------------------------------
			  /**
			   * @brief pipelineと同じ初出順を決めます
			   * @param[in] left 比較する元turn
			   * @param[in] right 比較する元turn
			   * @return 開始時刻、元cluster IDの順でleftが先ならtrue
			   */
			  //------------------------------------------------------------------------------
			  []( const SpeakerTurn &left, const SpeakerTurn &right)
			  {
				  return left.startMs < right.startMs ||
						 (left.startMs == right.startMs && left.speaker < right.speaker) ;
			  }) ;
	std::map<int, int> normalized ; // 全録音の初出順番号。一部分だけで振り直さない
	// raw IDを全録音の初出順へ変換し、時刻の契約も確認する
	for( auto &turn : ordered) { // この関数だけが所有する変換先
		checkCancelled( stop) ;
		requireAttribution( turn.startMs >= 0 && turn.endMs > turn.startMs && turn.endMs <= K_MAXIMUM_TIMELINE_MS &&
						   turn.speaker >= 0) ;
		const auto [entry, inserted] =
			normalized.try_emplace( turn.speaker, static_cast<int>( normalized.size()) + 1) ; // 初出時だけ採番する結果
		static_cast<void>( inserted) ;
		turn.speaker = entry->second ;
	}
	std::size_t	 firstTurn = 0 ; // 現在の行以前に終わった先頭turnを除く走査位置
	std::int64_t previousStart = -1 ; // 重なるASRの出力順が時刻を戻す場合の検出
	std::size_t	 totalItems = 0 ; // この結果が所有する全範囲数と範囲内ID数
	std::size_t	 validatedItems = 0 ; // 完成した情報の独立した整合性検証用合計
	std::size_t	 comparisons = 0 ; // 走査と有効集合の複製を含む全比較回数
	// 既存の行順・本文・話者候補を保持し、各行へ根拠だけを追加する
	for( auto &line : lines) { // workerの未公開の所有行
		checkCancelled( stop) ;
		validateLineIdentity( line) ;
		// token付きASR同士が重なる入力でも、前の時刻のturnを取りこぼさない
		if( line.startMs < previousStart) {
			firstTurn = 0 ;
		}
		previousStart = line.startMs ;
		SpeakerAttribution evidence ; // この行の完成まで公開しない推定根拠
		evidence.kind = classify( line.speakers.size(), false) ;
		std::vector<TurnEvent> events ; // 採用した候補に限った開始・終了点
		// 現在の行へ重ならない先頭区間を有限範囲で読み飛ばす
		while( firstTurn < ordered.size() && ordered[firstTurn].endMs <= line.startMs) {
			checkComparison( comparisons, stop) ;
			++firstTurn ;
		}
		// 未採用の微小turnで候補集合を増やさず、採用済みの根拠だけを切り出す
		for( std::size_t index = firstTurn; index < ordered.size() && ordered[index].startMs < line.endMs;
			++index) { // 元turn番号
			checkComparison( comparisons, stop) ;
			const auto &turn = ordered[index] ; // 正規化済みの元録音範囲
			const auto	start = std::max( line.startMs, turn.startMs) ; // 行内へ切り詰めた開始
			const auto	end = std::min( line.endMs, turn.endMs) ; // 行内へ切り詰めた終了
			// 端での接触と採用対象外の候補を有効な推定範囲にしない
			if( start < end && std::binary_search( line.speakers.begin(), line.speakers.end(), turn.speaker)) {
				events.push_back( {start, turn.speaker, 1}) ;
				events.push_back( {end, turn.speaker, -1}) ;
			}
		}
		std::sort( events.begin(), events.end(),
				  //------------------------------------------------------------------------------
				  /**
				   * @brief 同時刻の変更を一度に適用するため時刻で整列します
				   * @param[in] left 比較する境界
				   * @param[in] right 比較する境界
				   * @return leftが先ならtrue。同時刻内の順序は集約するため不問
				   */
				  //------------------------------------------------------------------------------
				  []( const TurnEvent &left, const TurnEvent &right)
				  {
					  return left.timeMs < right.timeMs ;
				  }) ;
		std::map<int, int> active ; // IDごとの有効turn数。0になれば要素を除く
		auto			   previousTime = line.startMs ; // 次の集合が有効になる直前の時刻
		std::size_t		   index = 0 ; // 次に消費する時刻境界の番号
		// 同時刻の開始と終了をまとめ、長さ0の偽の同時発話を作らない
		while( index < events.size()) {
			const auto time = events[index].timeMs ; // 今回まとめて消費する元時刻
			appendSpan( evidence, active, previousTime, time, totalItems, comparisons, stop) ;
			// この時刻にある全変更を反映してから次の正の時間区間へ進む
			while( index < events.size() && events[index].timeMs == time) {
				checkComparison( comparisons, stop) ;
				const auto &event = events[index++] ; // 現在の開始または終了点
				active[event.speaker] += event.delta ;
				// 同じ時刻で再登場しても、残数0の項目は有効集合に含めない
				if( active[event.speaker] == 0) {
					active.erase( event.speaker) ;
				}
			}
			previousTime = time ;
		}
		requireAttribution( active.empty()) ;
		const bool overlaps = std::any_of( evidence.spans.begin(), evidence.spans.end(),
			//------------------------------------------------------------------------------
			/**
			 * @brief 正の時間で複数候補が同時に有効か調べます
			 * @param[in] span 一つの推定範囲
			 * @return 同時に有効なIDが二つ以上ならtrue
			 */
			//------------------------------------------------------------------------------
			[]( const AttributionSpan &span)
			{
				return span.speakers.size() > 1 ;
			}) ;
		evidence.kind = classify( line.speakers.size(), overlaps) ;
		line.attribution = std::move( evidence) ;
		validateSpeakerAttribution( line, validatedItems) ;
	}
}

} // namespace koefude
