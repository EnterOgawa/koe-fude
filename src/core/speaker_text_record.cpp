//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file speaker_text_record.cpp
 * @brief 独立投影の固定来歴、再読検証、原byte exportを実装します
 * @version 1.0.0
 * @date 2026/11/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372のstrict snapshot codecと音響候補再検証を追加
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
#include "core/speaker_text_record.h"
#include "core/reading_view.h"
#include "core/speaker_text_json.h"
#include "inference/model_store.h"
#include "platform/win_helpers.h"
#include <optional>
#include <span>
#include <algorithm>
#include <array>
#include <exception>
#include <json.hpp>
#include <limits>
#include <new>
#include <set>
#include <utility>

namespace koefude {
namespace {
using Json = nlohmann::json ; // 固定版のbounded JSON
//------------------------------------------------------------------------------
/**
 * @brief 不成立を固定分類で拒否します
 * @param[in] valid 成立条件
 */
//------------------------------------------------------------------------------
void require( const bool valid)
{
	// 未検証の一部recordを採用しない
	if( !valid) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 小targetを固定順に変換します
 * @param[in] t target
 * @return JSON配列
 */
//------------------------------------------------------------------------------
Json targetJson( const SpeakerTextTarget &t)
{
	return Json::array( {t.historyId, t.generationOperation, t.update, t.sourceSha, t.planSha, t.completePrefix}) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 固定target配列を復元します
 * @param[in] j 六要素
 * @return 小CAS
 */
//------------------------------------------------------------------------------
SpeakerTextTarget readTarget( const Json &j)
{
	require( j.is_array() && j.size() == 6) ;
	return {speaker_text_json::count( j.at( 0)),
			speaker_text_json::count( j.at( 1)),
			speaker_text_json::count( j.at( 2)),
			speaker_text_json::digest( j.at( 3)),
			speaker_text_json::digest( j.at( 4)),
			static_cast<std::uint32_t>( speaker_text_json::count( j.at( 5), K_MAXIMUM_SEGMENTS))} ;
}
//------------------------------------------------------------------------------
/**
 * @brief 全参照を原順で変換します
 * @param[in] refs 全窓
 * @return JSON配列
 */
//------------------------------------------------------------------------------
Json referencesJson( const std::span<const TextAlignmentReferenceWindow> refs)
{
	Json out = Json::array() ; // 全中立を含める
	// 全窓の参照を一回ずつ保持する
	for( const auto &window : refs) {
		Json units = Json::array() ; // 当窓の全単位
		// 短い中立も省略しない
		for( const auto &unit : window.units) {
			Json tokens = Json::array() ; // 元PCMの発火と許可境界
			// 全byteを覆うtokenを同じ順で保存する
			for( const auto &token : unit.tokens) {
				tokens.push_back( {token.startMs, token.endMs, token.textEnd, token.canSplitAfter}) ;
			}
			units.push_back( {unit.text, unit.speaker, unit.reason, std::move( tokens)}) ;
		}
		out.push_back( {window.samples.begin, window.samples.end, window.origin, window.status, std::move( units)}) ;
	}
	return out ;
}
//------------------------------------------------------------------------------
/**
 * @brief 固定parts/採用証拠を変換します
 * @param[in] p 一窓
 * @return JSON値
 */
//------------------------------------------------------------------------------
Json projectionJson( const SpeakerTextWindowProjection &p)
{
	Json parts = Json::array() ; // 全byte partition
	// 同一話者連続blockも参照indicesを保持する
	for( const auto &part : p.parts) {
		parts.push_back(
			{part.chunkIndex, part.byteBegin, part.byteEnd, part.speaker, part.referenceUnits, part.reason}) ;
	}
	Json matches = Json::array() ; // 全最小経路の採用証拠
	// 対応条件の各固定値を保存する
	for( const auto &m : p.matches) {
		matches.push_back( {m.referenceUnit, m.byteBegin, m.byteEnd, m.contentScalars, m.exactScalars, m.anchorScalars,
						   m.editDistance, m.maximumMismatch, m.trimmedScalars}) ;
	}
	return {parts,		 matches, p.status, p.contentScalars, p.projectedScalars, p.matrixCells, p.featureComparisons,
			p.arenaBytes} ;
}
//------------------------------------------------------------------------------
/**
 * @brief 固定来歴を変換します
 * @param[in] e 実行trace
 * @return 許可fieldだけのJSON
 */
//------------------------------------------------------------------------------
Json executionJson( const AsrExecutionTrace &e)
{
	return {e.family,
			e.profile,
			e.backend,
			e.provider,
			e.modelId,
			e.verifiedModelIds,
			e.threads,
			e.speechFilter,
			e.language,
			e.decodingMethod,
			e.featureDim,
			e.frameMs,
			e.tokenTimestamps,
			e.externalWindows,
			e.observedWholeText,
			e.nativeContextsCreated,
			e.nativeContextsReleased,
			e.vadContextsCreated,
			e.vadContextsReleased} ;
}
//------------------------------------------------------------------------------
/**
 * @brief record全体の型付き値を作ります
 * @param[in] p 一採用
 * @return JSON
 */
//------------------------------------------------------------------------------
Json recordJson( const SpeakerTextProjectionRecord &p)
{
	Json windows = Json::array() ; // 全完全prefixのpartition
	// 各窓の証拠を原順に追加する
	for( const auto &window : p.windows) {
		windows.push_back( projectionJson( window)) ;
	}
	Json assets = Json::array() ; // graph/data/tokens/VADの固定配布pin
	// モデル容量をVRAMと混同せずasset来歴として保持する
	for( const auto &asset : p.assets) {
		assets.push_back( {asset.id, asset.revision, asset.bytes, asset.sha256}) ;
	}
	return {{"domain", "koefude.speaker_text_projection"},
			{"version", p.version},
			{"algorithm", 3},
			{"op", p.operationId},
			{"reference_op", p.referenceGenerationOperation},
			{"target", targetJson( p.target)},
			{"pcm",
			 {p.pcm.version, p.pcm.sampleCount, p.pcm.sampleRate, p.pcm.channels, p.pcm.originSample, p.pcm.pcmSha256}},
			{"turns", p.turnsSha},
			{"parameters", p.parametersSha},
			{"reference_sha", p.referenceSha},
			{"execution", executionJson( p.execution)},
			{"assets", assets},
			{"references", referencesJson( p.references)},
			{"windows", windows}} ;
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 有効なplan2日本語完全prefixだけを識別します
 * @param[in] audio 保存record
 * @return 適格なら小token、未適格ならnull
 * @see koefude::speakerTextTarget
 */
//------------------------------------------------------------------------------
std::optional<SpeakerTextTarget> speakerTextTarget( const ApiAudioRecord &audio)
{
	// 日本語文脈planの保存済み終端prefixだけを対象にする
	if( audio.plan.version != 2 || audio.plan.autoLanguage || !audio.plan.diarization || !audio.historyId ||
	   !audio.operationId || !audio.update || audio.saveFailed || audio.plan.digest == ApiTextDigest{} ||
	   audio.sourceSha == ApiTextDigest{} || apiAudioActive( audio.terminal)) {
		return {} ;
	}
	std::size_t count = 0 ; // 空textも完全応答として数える
	// 成功prefixの先頭からだけを使う
	for( const auto &chunk : audio.chunks) {
		// 不完全な応答より先を成功prefixへ含めない
		if( !chunk.complete) {
			break ;
		}
		++count ;
	}
	// 空/不正prefixを投影対象として返さない
	if( !count || count > audio.plan.windows.size() || count > K_MAXIMUM_SEGMENTS) {
		return {} ;
	}
	return SpeakerTextTarget{audio.historyId, audio.operationId, audio.update,
							 audio.sourceSha, audio.plan.digest, static_cast<std::uint32_t>( count)} ;
}
//------------------------------------------------------------------------------
/**
 * @brief 固定方式3のparameter SHAを返します
 * @return 本文なしdigest
 * @see koefude::speakerTextParametersSha
 */
//------------------------------------------------------------------------------
ApiTextDigest speakerTextParametersSha()
{
	return apiTextDigest( "koefude.speaker_text.v1/method3/scalars4096/cells4194304/arena33554432/"
						 "units512/tokens4096/min24/exact24/anchor12/grams8/postings8/edit20/"
						 "gap12/trim12/all_optimal/normal_ctc_quality_cpu/turn_atoms/v1") ;
}
//------------------------------------------------------------------------------
/**
 * @brief 全target turnを原順でhashします
 * @param[in] turns 全file保存turn
 * @return 全fieldのdigest
 * @see koefude::speakerTextTurnsSha
 */
//------------------------------------------------------------------------------
ApiTextDigest speakerTextTurnsSha( const std::span<const SpeakerTurn> turns)
{
	Json out = Json::array() ; // 正本の全field原順
	// 対象全file turnの全fieldを原順に束縛する
	for( const auto &turn : turns) {
		out.push_back( {turn.startMs, turn.endMs, turn.speaker}) ;
	}
	return apiTextDigest( out.dump()) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 全参照snapshotをhashします
 * @param[in] reference 中立を含む全窓
 * @return 全fieldのdigest
 * @see koefude::speakerTextReferenceSha
 */
//------------------------------------------------------------------------------
ApiTextDigest speakerTextReferenceSha( const std::span<const TextAlignmentReferenceWindow> reference)
{
	return apiTextDigest( referencesJson( reference).dump()) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 空完全応答を数えたchunkから非空source行へのmapを返します
 * @param[in] audio 元record
 * @return 非空chunk indexの原順列
 * @see koefude::speakerTextSourceChunks
 */
//------------------------------------------------------------------------------
std::vector<std::size_t> speakerTextSourceChunks( const ApiAudioRecord &audio)
{
	std::vector<std::size_t> chunks ; // source_line順からchunk_indexへ戻すmap
	// 全chunkを先頭から完全prefixの末尾まで対応付ける
	for( std::size_t at = 0; at < audio.chunks.size(); ++at) {
		// 不完全応答後の行を復元mapへ含めない
		if( !audio.chunks[at].complete) {
			break ;
		}
		// 空応答もchunk番号を消費し、非空だけがsource行を作る
		if( !audio.chunks[at].text.empty()) {
			chunks.push_back( at) ;
		}
	}
	return chunks ;
}
//------------------------------------------------------------------------------
/**
 * @brief 原文/plan/PCM/turn/参照/固定方式と全partitionをworkerで検証します
 * @param[in] value 採用候補
 * @param[in] source 耐久原文
 * @param[in] audio 耐久音声record
 * @param[in] replay trueは純粋対応/音響候補も再検証
 * @param[in] stop 協調停止
 * @throws OperationError 不正/上限/取消し。I/O/推論は行いません
 * @see koefude::validateSpeakerTextRecord
 */
//------------------------------------------------------------------------------
void validateSpeakerTextRecord( const SpeakerTextProjectionRecord &value, const Transcript &source,
							   const ApiAudioRecord &audio, const bool replay, const std::stop_token stop)
{
	checkCancelled( stop) ;
	const auto target = speakerTextTarget( audio) ; // 耐久正本へ束縛
	require( target && *target == value.target && value.version == 1 && value.operationId &&
			value.operationId != UINT64_MAX && value.operationId != audio.operationId &&
			value.referenceGenerationOperation && value.referenceGenerationOperation <= value.operationId &&
			source.pcmIdentity && value.pcm == audio.plan.pcm && value.pcm == *source.pcmIdentity &&
			!source.speakerTurns.empty() && fingerprintReadingSource( source) == value.target.sourceSha &&
			value.turnsSha == speakerTextTurnsSha( source.speakerTurns) &&
			value.parametersSha == speakerTextParametersSha() && value.references.size() == target->completePrefix &&
			value.windows.size() == value.references.size() && value.references.size() <= K_MAXIMUM_SEGMENTS) ;
	const auto &e = value.execution ; // 記録していない来歴をnativeへ補わない
	require( e.family == AsrModel::PARAKEET && e.profile == AsrProfile::QUALITY && e.backend == Backend::CPU &&
			e.provider == "cpu" && e.modelId == "parakeet-ja-ctc-fp32" && e.language == "Japanese" &&
			e.decodingMethod == "greedy_search" && e.featureDim == 80 && e.frameMs == 80 && e.tokenTimestamps &&
			e.externalWindows && e.speechFilter && !e.observedWholeText && e.threads >= 1 && e.threads <= 16 &&
			e.nativeContextsCreated == 1 && e.nativeContextsReleased == 1 && e.vadContextsCreated == 1 &&
			e.vadContextsReleased == 1) ;
	const std::set<std::string> expected{"parakeet-ja-ctc-fp32", "parakeet-ja-tokens", "parakeet-ja-ctc-fp32-data",
										 "silero-vad-6.2.0"} ; // 通常検証asset
	require( std::set<std::string>( e.verifiedModelIds.begin(), e.verifiedModelIds.end()) == expected &&
			e.verifiedModelIds.size() == 4) ;
	require( value.assets.size() == 4) ;
	std::set<std::string> assetIds ; // 重複や現値の黙示補完を防ぐ
	// 採取時の4assetを検証し、未記録来歴を現値で補わない
	for( const auto &asset : value.assets) {
		require( expected.contains( asset.id) && assetIds.insert( asset.id).second && !asset.revision.empty() &&
				asset.revision.size() <= 128 && asset.bytes > 0 && asset.sha256.size() == 64 &&
				std::all_of( asset.sha256.begin(), asset.sha256.end(),
							//------------------------------------------------------------------------------
							/**
							 * @brief SHAの小文字16進表現だけを認めます
							 * @param[in] c 一byte
							 * @return 既知16進文字ならtrue
							 */
							//------------------------------------------------------------------------------
							[]( const char c)
							{
								return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ;
							})) ;
	}
	std::size_t bytes = 0 ; // 全参照本文の固定予算
	std::size_t parts = 0 ; // 全partitionの固定予算
	const auto	sourceChunks = speakerTextSourceChunks( audio) ; // 空応答を飛ばす正しい行対応
	require( sourceChunks.size() == source.lines.size()) ;
	// 非空source行が原chunk本文とbyte一致することを検証する
	for( std::size_t line = 0; line < sourceChunks.size(); ++line) {
		require( source.lines[line].text == audio.chunks[sourceChunks[line]].text) ;
	}
	// 全窓の原byte partitionと参照所有を先頭から検証する
	for( std::size_t at = 0; at < value.references.size(); ++at) {
		checkCancelled( stop) ;
		const auto &ref = value.references[at] ; // 中立を含む当窓参照
		const auto &window = value.windows[at] ; // 原byte partition
		require( ref.samples.begin == audio.plan.windows[at].begin && ref.samples.end == audio.plan.windows[at].end &&
				ref.origin == TextReferenceOrigin::NATIVE_PARAKEET_TOKENS && ref.units.size() <= K_SPEAKER_TEXT_UNITS &&
				static_cast<unsigned>( ref.status) <= static_cast<unsigned>( SpeakerTextReason::UNMATCHED)) ;
		std::vector<TextSegment> segments ; // 音響候補を再検証する有界snapshot
		std::size_t				 tokens = 0 ; // 当窓の全発火上限
		// 全中立単位もtoken/UTF8容量の固定予算へ数える
		for( const auto &unit : ref.units) {
			require( unit.text.size() <= K_SPEAKER_TEXT_REFERENCE_BYTES - bytes && !unit.tokens.empty() &&
					unit.tokens.size() <= K_SPEAKER_TEXT_TOKENS - tokens) ;
			bytes += unit.text.size() ;
			tokens += unit.tokens.size() ;
			require( static_cast<unsigned>( unit.reason) <= static_cast<unsigned>( SpeakerTextReason::UNMATCHED)) ;
			segments.push_back( {unit.tokens.front().startMs, unit.tokens.back().endMs, unit.text, unit.tokens, true}) ;
		}
		require( window.parts.size() <= 50000 - parts) ;
		parts += window.parts.size() ;
		require( validateSpeakerTextPartition( audio.chunks[at].text, window, ref)) ;
		// partsが当chunkを指し、既知の中立理由だけを持つか調べる
		for( const auto &part : window.parts) {
			require( part.chunkIndex == at && static_cast<unsigned>( part.reason) <= 7) ;
		}
		// 再利用/読込では同じ固定算法と音響候補を再検証する
		if( replay) {
			// 保存した発火を対象全turnへ再照合し、sidecarのspeaker番号を信用しない
			if( !segments.empty()) {
				// 保存発火を対象全turnへ再照合した参照
				const auto checked = makeNativeTextReferenceWindow( ref.samples, segments, source.speakerTurns, stop) ;
				require( referencesJson( std::span( &checked, 1)) == referencesJson( std::span( &ref, 1))) ;
			}
			const auto expectedWindow = projectSpeakerText( at, audio.chunks[at].text, ref, stop) ; // 同じpure方式
			require( projectionJson( expectedWindow) == projectionJson( window)) ;
		}
	}
	require( value.referenceSha == speakerTextReferenceSha( value.references)) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 一採用recordを有界JSONへ変換します
 * @param[in] value 所有候補
 * @return API本文/path/名前を含まないJSON
 * @throws OperationError 上限
 * @see koefude::encodeSpeakerTextRecord
 */
//------------------------------------------------------------------------------
std::string encodeSpeakerTextRecord( const SpeakerTextProjectionRecord &value)
{
	const auto text = recordJson( value).dump() ; // 一回だけ有界参照を符号化
	require( text.size() <= K_SPEAKER_TEXT_RECORD_BYTES) ;
	return text ;
}
//------------------------------------------------------------------------------
/**
 * @brief 一採用recordを外部保存から復元し全bindingを検証します
 * @param[in] bytes 有界UTF-8JSON
 * @param[in] source 元原文
 * @param[in] audio 元record
 * @return 検証済み共有採用
 * @throws OperationError 不正/未知版/上限
 * @see koefude::decodeSpeakerTextRecord
 */
//------------------------------------------------------------------------------
std::shared_ptr<const SpeakerTextProjectionRecord>
decodeSpeakerTextRecord( const std::string_view bytes, const Transcript &source, const ApiAudioRecord &audio)
{
	require( bytes.size() <= K_SPEAKER_TEXT_RECORD_BYTES) ;
	try {
		const auto j = speaker_text_json::parse( bytes) ; // byte/深さ/重複/整数型を先行検証
		require( j.is_object() && j.size() == 14 && j.at( "domain") == "koefude.speaker_text_projection" &&
				j.at( "version") == 1 && j.at( "algorithm") == 3 && j.at( "references").is_array() &&
				j.at( "references").size() <= K_MAXIMUM_SEGMENTS && j.at( "windows").is_array() &&
				j.at( "windows").size() == j.at( "references").size()) ;
		auto p = std::make_shared<SpeakerTextProjectionRecord>() ; // 全検証後だけ公開
		p->operationId = speaker_text_json::count( j.at( "op"), UINT64_MAX - 1) ;
		p->target = readTarget( j.at( "target")) ;
		p->referenceGenerationOperation = speaker_text_json::count( j.at( "reference_op"), UINT64_MAX - 1) ;
		// 外部JSONの全PCM identity配列
		const auto &pcm = j.at( "pcm") ;
		require( pcm.is_array() && pcm.size() == 6) ;
		p->pcm = {static_cast<std::uint32_t>( speaker_text_json::count( pcm.at( 0), 1)),
				  speaker_text_json::count( pcm.at( 1), K_MAXIMUM_TIMELINE_SAMPLES),
				  static_cast<std::uint32_t>( speaker_text_json::count( pcm.at( 2), K_SAMPLE_RATE)),
				  static_cast<std::uint32_t>( speaker_text_json::count( pcm.at( 3), 1)),
				  speaker_text_json::count( pcm.at( 4), K_MAXIMUM_TIMELINE_SAMPLES),
				  speaker_text_json::digest( pcm.at( 5))} ;
		p->turnsSha = speaker_text_json::digest( j.at( "turns")) ;
		p->parametersSha = speaker_text_json::digest( j.at( "parameters")) ;
		p->referenceSha = speaker_text_json::digest( j.at( "reference_sha")) ;
		// 固定順の実行来歴配列
		const auto &e = j.at( "execution") ;
		require( e.is_array() && e.size() == 19) ;
		p->execution.family = static_cast<AsrModel>( speaker_text_json::count( e.at( 0), 2)) ;
		p->execution.profile = static_cast<AsrProfile>( speaker_text_json::count( e.at( 1), 1)) ;
		p->execution.backend = static_cast<Backend>( speaker_text_json::count( e.at( 2), 3)) ;
		require( e.at( 5).is_array() && e.at( 5).size() == 4) ;
		p->execution.provider = e.at( 3).get<std::string>() ;
		p->execution.modelId = e.at( 4).get<std::string>() ;
		p->execution.verifiedModelIds = e.at( 5).get<std::vector<std::string>>() ;
		p->execution.threads = static_cast<int>( speaker_text_json::count( e.at( 6), 16)) ;
		p->execution.speechFilter = e.at( 7).get<bool>() ;
		p->execution.language = e.at( 8).get<std::string>() ;
		p->execution.decodingMethod = e.at( 9).get<std::string>() ;
		p->execution.featureDim = static_cast<int>( speaker_text_json::count( e.at( 10), 80)) ;
		p->execution.frameMs = static_cast<int>( speaker_text_json::count( e.at( 11), 80)) ;
		p->execution.tokenTimestamps = e.at( 12).get<bool>() ;
		p->execution.externalWindows = e.at( 13).get<bool>() ;
		p->execution.observedWholeText = e.at( 14).get<bool>() ;
		p->execution.nativeContextsCreated = static_cast<std::uint32_t>( speaker_text_json::count( e.at( 15), 1)) ;
		p->execution.nativeContextsReleased = static_cast<std::uint32_t>( speaker_text_json::count( e.at( 16), 1)) ;
		p->execution.vadContextsCreated = static_cast<std::uint32_t>( speaker_text_json::count( e.at( 17), 1)) ;
		p->execution.vadContextsReleased = static_cast<std::uint32_t>( speaker_text_json::count( e.at( 18), 1)) ;
		require( j.at( "assets").is_array() && j.at( "assets").size() == 4) ;
		// 小asset snapshotを上限4件で復元する
		for( const auto &asset : j.at( "assets")) {
			require( asset.is_array() && asset.size() == 4) ;
			p->assets.push_back( {asset.at( 0).get<std::string>(), asset.at( 1).get<std::string>(),
								 speaker_text_json::count( asset.at( 2), UINT64_MAX - 1),
								 asset.at( 3).get<std::string>()}) ;
		}
		std::size_t referenceBytes = 0 ; // 確保前に全本文容量を照合
		// 保存全窓を上限内で原順に復元する
		for( const auto &rw : j.at( "references")) {
			require( rw.is_array() && rw.size() == 5 && rw.at( 4).is_array() && rw.at( 4).size() <= 512) ;
			// 当窓だけの所有参照snapshot
			TextAlignmentReferenceWindow ref ;
			ref.samples = {speaker_text_json::count( rw.at( 0), K_MAXIMUM_TIMELINE_SAMPLES),
						   speaker_text_json::count( rw.at( 1), K_MAXIMUM_TIMELINE_SAMPLES)} ;
			ref.origin = static_cast<TextReferenceOrigin>( speaker_text_json::count( rw.at( 2), 1)) ;
			ref.status = static_cast<SpeakerTextReason>( speaker_text_json::count( rw.at( 3), 7)) ;
			std::size_t tokenCount = 0 ; // 窓token上限
			// 各参照単位をUTF8総量とtoken上限の範囲で読む
			for( const auto &u : rw.at( 4)) {
				require( u.is_array() && u.size() == 4 && u.at( 0).is_string() &&
						u.at( 0).get_ref<const std::string &>().size() <=
							K_SPEAKER_TEXT_REFERENCE_BYTES - referenceBytes &&
						u.at( 3).is_array() && u.at( 3).size() <= 4096 - tokenCount) ;
				// 中立も欠かさない一参照単位
				TextAlignmentReferenceUnit unit ;
				unit.text = u.at( 0).get<std::string>() ;
				referenceBytes += unit.text.size() ;
				unit.speaker = u.at( 1).get<int>() ;
				unit.reason = u.at( 2).get<SpeakerTextReason>() ;
				tokenCount += u.at( 3).size() ;
				// 許可語境界と発火時刻を型/上限検証して復元する
				for( const auto &t : u.at( 3)) {
					require( t.is_array() && t.size() == 4) ;
					unit.tokens.push_back(
						{static_cast<std::int64_t>( speaker_text_json::count( t.at( 0), K_MAXIMUM_TIMELINE_MS)),
						 static_cast<std::int64_t>( speaker_text_json::count( t.at( 1), K_MAXIMUM_TIMELINE_MS)),
						 static_cast<std::size_t>( speaker_text_json::count( t.at( 2), K_SPEAKER_TEXT_WINDOW_BYTES)),
						 t.at( 3).get<bool>()}) ;
				}
				ref.units.push_back( std::move( unit)) ;
			}
			p->references.push_back( std::move( ref)) ;
		}
		// 保存partsを任意に信頼せず、同じpure方式から完全再構成して比較する
		for( std::size_t at = 0; at < p->references.size(); ++at) {
			require( at < audio.chunks.size()) ;
			// 当chunkに対応する元sample窓または固定方式の結果
			auto window = projectSpeakerText( at, audio.chunks[at].text, p->references[at]) ;
			require( projectionJson( window) == j.at( "windows").at( at)) ;
			p->windows.push_back( std::move( window)) ;
		}
		validateSpeakerTextRecord( *p, source, audio, true) ;
		// 任意追加field、float→整数、enum/asset改変をcanonical全値照合で拒否する
		require( recordJson( *p) == j) ;
		return p ;
	} catch( const OperationError &) {
		throw ;
	} catch( const std::bad_alloc &) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	} catch( const std::exception &) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 中立partに内容scalarがあるか確認します
 * @param[in] body 原UTF-8範囲
 * @return 明示separator以外があればtrue
 * @see koefude::speakerTextHasContent
 */
//------------------------------------------------------------------------------
bool speakerTextHasContent( const std::string_view body)
{
	const auto text = toWide( body) ; // 表示workerだけの有界変換
	return std::any_of( text.begin(), text.end(),
					   //------------------------------------------------------------------------------
					   /**
					    * @brief 明示separator以外を検出します
					    * @param[in] c code unit
					    * @return 内容がある場合true
					    */
					   //------------------------------------------------------------------------------
					   []( const wchar_t c)
					   {
						   return !(c <= L' ' || c == L'　' || c == L'。' || c == L'、' || c == L'，' || c == L'．' ||
									c == L',' || c == L'.' || c == L'!' || c == L'?' || c == L'！' || c == L'？' ||
									c == L'「' || c == L'」' || c == L'『' || c == L'』' || c == L'"' || c == L'\'') ;
					   }) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 明示話者付きTXTを原byteと現表示名から作ります
 * @param[in] source 現原文/表示名
 * @param[in] audio 原音声record
 * @param[in] projection 採用
 * @return 全中立も含むUTF-8。通常copy/TXTとは別の明示出力です
 * @see koefude::renderSpeakerTextExport
 */
//------------------------------------------------------------------------------
std::string renderSpeakerTextExport( const Transcript &source, const ApiAudioRecord &audio,
									const SpeakerTextProjectionRecord &projection)
{
	validateSpeakerTextRecord( projection, source, audio, false) ;
	std::string out = "話者はPC内の音響推定と本文対応です。APIの語時刻ではありません。\r\n" ; // 明示exportの説明
	const auto	chunks = speakerTextSourceChunks( audio) ; // 空完全応答の正しい対応
	// 非空元行をchunk mapから原順に明示TXTへ出す
	for( std::size_t line = 0; line < chunks.size(); ++line) {
		// 空完全応答を含む元chunk番号
		const auto	chunk = chunks[line] ;
		const auto &original = source.lines[line] ; // 元行と元chunk
		const auto	append =
			//------------------------------------------------------------------------------
			/**
			 * @brief 対象の現表示名と原byteを一blockとして出します
			 * @param[in] speaker 対象番号または中立-1
			 * @param[in] text 同期借用の原byte
			 */
			//------------------------------------------------------------------------------
			[&]( const int speaker, const std::string_view text)
			{
				std::string name = "話者未確定" ; // 中立も残す
				// 対象の正番号だけを現表示名から引き、中立名を推測しない
				if( speaker > 0) {
					// 対象IDまたは表示名を持つ既存owner
					const auto found = source.speakerNames.find( speaker) ;
					name = found == source.speakerNames.end() ? "話者" + std::to_string( speaker) : found->second ;
				}
				out += "[" + name + "]\r\n" ;
				out += text ;
				out += "\r\n\r\n" ;
			} ; // 同期原byte借用
		// 既存単一話者窓は従来の全文帰属を維持する
		if( original.speakers.size() == 1) {
			append( original.speakers.front(), original.text) ;
			continue ;
		}
		// 混在窓は中立も含む全partsの原byteを順に出す
		for( const auto &part : projection.windows[chunk].parts) {
			append( part.speaker,
				   std::string_view( audio.chunks[chunk].text).substr( part.byteBegin, part.byteEnd - part.byteBegin)) ;
		}
		require( out.size() <= 16 * K_MAXIMUM_TEXT_BYTES) ;
	}
	return out ;
}
} // namespace koefude
