//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_audio_transcription.cpp
 * @brief 音声APIの非重複plan、strict応答、V8 recordを実装します
 * @version 1.0.0
 * @date 2026/11/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の有界音声送信契約を実装
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1370の文脈分割と明示途中再開を接続
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
#include "core/api_audio_transcription.h"
#include "core/asr_windows.h"
#include "core/audio_data.h"
#include "core/speaker_attribution.h"
#include "platform/win_helpers.h"
#include <optional>
#include <span>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <json.hpp>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <windows.h>

namespace koefude {
namespace {
using Json = nlohmann::json ; // 固定第三者JSON実装
//------------------------------------------------------------------------------
/**
 * @brief 不正外部値を本文なしで拒否します
 */
//------------------------------------------------------------------------------
[[noreturn]] void invalid()
{
	throw OperationError( ErrorKind::INVALID_INPUT, ERROR_INVALID_DATA) ;
}
//------------------------------------------------------------------------------
/**
 * @brief bounded JSONと重複keyを検証します
 * @param[in] body UTF-8
 * @param[in] limit byte上限
 * @param[in] eventsLimit 有限イベント上限
 * @return 完全JSON
 */
//------------------------------------------------------------------------------
Json strictJson( std::string_view body, std::size_t limit, unsigned eventsLimit = 50000)
{
	// byte/NUL/UTF8をparser allocation前に検証します
	if( body.empty() || body.size() > limit || body.find( '\0') != std::string_view::npos) {
		invalid() ;
	}
	static_cast<void>( toWide( body)) ;
	std::vector<std::set<std::string>> keys ; // objectごとの重複集合
	unsigned						   events = 0 ; // 未知fieldの資源制限
	return Json::parse( body,
					   //------------------------------------------------------------------------------
					   /**
					    * @brief 全fieldを有界検証します
					    * @param[in] depth 深さ
					    * @param[in] event 種類
					    * @param[in,out] value parser値
					    * @return true、違反は例外
					    */
					   //------------------------------------------------------------------------------
					   [&keys, &events, eventsLimit]( int depth, Json::parse_event_t event, Json &value)
					   {
						   // nested unknown値も無制限に解析しません
						   if( depth > 16 || ++events > eventsLimit) {
							   invalid() ;
						   }
						   // 新objectへ独立した集合を作ります
						   if( event == Json::parse_event_t::object_start) {
							   keys.emplace_back() ;
						   }
						   // 上書きされる同名keyを許可しません
						   if( event == Json::parse_event_t::key &&
							  (keys.empty() || !keys.back().insert( value.get<std::string>()).second)) {
							   invalid() ;
						   }
						   // 完了したobjectの集合を解放します
						   if( event == Json::parse_event_t::object_end) {
							   keys.pop_back() ;
						   }
						   return true ;
					   }) ;
}
//------------------------------------------------------------------------------
/**
 * @brief unsigned整数の型と範囲を検証します
 * @param[in] j JSON
 * @param[in] maximum 許可最大
 * @return 正確な整数
 */
//------------------------------------------------------------------------------
std::uint64_t uintValue( const Json &j, std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max())
{
	// 負数/小数/文字列からの暗黙変換を拒否します
	if( !j.is_number_unsigned() || j.get<std::uint64_t>() > maximum) {
		invalid() ;
	}
	return j.get<std::uint64_t>() ;
}
//------------------------------------------------------------------------------
/**
 * @brief 音声API計画のdigestへ含める価格metadataの五fieldをJSONへ複写します
 * @param[in] m 取得/試行時刻、HTTP、固定error、SHAを持つ凍結metadataのborrowです
 * @return fetched/attempted/http/error/shaを保持する独立したJSON objectです。取得や価格更新は行いません
 */
//------------------------------------------------------------------------------
Json metadataJson( const ApiCatalogMetadata &m)
{
	return Json{
		{"fetched", m.fetched}, {"attempted", m.attempted}, {"http", m.http}, {"error", m.error}, {"sha", m.sha}} ;
}
//------------------------------------------------------------------------------
/**
 * @brief 凍結planのdigest対象を作ります
 * @param[in] p 全窓
 * @return 固定field JSON
 */
//------------------------------------------------------------------------------
Json planJson( const ApiAudioPlan &p)
{
	Json windows = Json::array() ; // 最大50,000窓
	// 全窓を元sample順に写し、識別子以外の情報を加えません
	for( const auto &w : p.windows) {
		windows.push_back( {w.begin, w.end, w.speakers}) ;
	}
	auto value = Json{{"version", p.version},
					  {"model", K_API_AUDIO_MODEL},
					  {"basis", "LocalAudioWindow"},
					  {"pcm",
					   {{"version", p.pcm.version},
						{"samples", p.pcm.sampleCount},
						{"rate", p.pcm.sampleRate},
						{"channels", p.pcm.channels},
						{"origin", p.pcm.originSample},
						{"sha", p.pcm.pcmSha256}}},
					  {"auto", p.autoLanguage},
					  {"diarization", p.diarization},
					  {"vad", p.speechFilter},
					  {"windows", windows},
					  {"send_samples", p.sendSamples},
					  {"estimate", p.estimateMicroUsd},
					  {"price",
					   {{"rate", p.price.price.microPerMinute ? Json( *p.price.price.microPerMinute) : Json( nullptr)},
						{"meta", metadataJson( p.price.metadata)},
						{"source", p.price.source},
						{"parser", p.price.parser}}}} ;
	// v1のcanonical byteは変更せず、定数はv2だけのdigestへ入れます
	if( p.version == 2) {
		value["method"] = "Context90" ;
		value["target_samples"] = 90ULL * K_SAMPLE_RATE ;
		value["minimum_samples"] = 60ULL * K_SAMPLE_RATE ;
		value["maximum_samples"] = K_API_AUDIO_CONTEXT_SAMPLES ;
	}
	return value ;
}
//------------------------------------------------------------------------------
/**
 * @brief 窓/料金/PCMの全fieldとdigestを検証します
 * @param[in] p 所有plan
 */
//------------------------------------------------------------------------------
void validatePlan( const ApiAudioPlan &p)
{
	// 保存値から別model/単位/整数境界を推測採用しません
	if( (p.version != 1 && p.version != 2) || p.pcm.version != 1 || p.pcm.sampleCount == 0 ||
	   p.pcm.sampleCount > K_MAXIMUM_TIMELINE_SAMPLES || p.pcm.sampleRate != K_SAMPLE_RATE || p.pcm.channels != 1 ||
	   p.pcm.originSample != 0 || p.windows.size() > K_MAXIMUM_SEGMENTS || !p.price.price.microPerMinute ||
	   *p.price.price.microPerMinute > 1'000'000'000 ||
	   p.price.source != "https://developers.openai.com/api/docs/pricing.md" || p.price.parser != 1 ||
	   p.price.metadata.fetched <= 0 || p.price.metadata.attempted < 0 || p.price.metadata.http > 599) {
		invalid() ;
	}
	std::uint64_t previous = 0 ; // 先行終端、非重複照合
	std::uint64_t samples = 0 ; // overflowしないplan総量
	std::size_t	  items = 0 ; // 全候補の件数予算
	// 各窓を一度検証し、混在窓だけ26秒へ制限します
	for( const auto &w : p.windows) {
		// 候補の重複/逆順と範囲外を拒否します
		if( w.begin < previous || w.end <= w.begin || w.end > p.pcm.sampleCount ||
		   w.end - w.begin > (p.version == 2
								  ? K_API_AUDIO_CONTEXT_SAMPLES
								  : (w.speakers.size() > 1 ? 26ULL * K_SAMPLE_RATE : K_API_AUDIO_WINDOW_SAMPLES)) ||
		   w.speakers.size() > 20 || !std::is_sorted( w.speakers.begin(), w.speakers.end()) ||
		   std::adjacent_find( w.speakers.begin(), w.speakers.end()) != w.speakers.end() ||
		   (!w.speakers.empty() && (w.speakers.front() < 0 || w.speakers.back() > 10000)) ||
		   items > K_MAXIMUM_ATTRIBUTION_ITEMS - w.speakers.size()) {
			invalid() ;
		}
		// 新方式は最初から最後まで連続し、中間窓を60秒未満へ縮めません
		if( p.version == 2 && ((previous && w.begin != previous) ||
							  (&w != &p.windows.back() && w.end - w.begin < 60ULL * K_SAMPLE_RATE))) {
			invalid() ;
		}
		previous = w.end ;
		items += w.speakers.size() ;
		samples += w.end - w.begin ; // 非重複全長以下なので加算は表現境界内
	}
	// 概算とdigestを再計算し過去の別料金へ結合しません
	if( samples != p.sendSamples || p.estimateMicroUsd != estimateApiAudioCost( samples, *p.price.price.microPerMinute) ||
	   apiTextDigest( planJson( p).dump()) != p.digest) {
		invalid() ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief usageの保存fieldだけを写します
 * @param[in] u 観測値
 * @return JSON
 */
//------------------------------------------------------------------------------
Json usageJson( const ApiAudioUsage &u)
{
	return Json{{"kind", static_cast<unsigned>( u.kind)},
				{"seconds", u.seconds ? Json( *u.seconds) : Json( nullptr)},
				{"input", u.inputTokens ? Json( *u.inputTokens) : Json( nullptr)},
				{"output", u.outputTokens ? Json( *u.outputTokens) : Json( nullptr)},
				{"total", u.totalTokens ? Json( *u.totalTokens) : Json( nullptr)},
				{"applicable", u.priceApplicable}} ;
}

//------------------------------------------------------------------------------
/**
 * @brief chunkのtyped全fieldを符号化します
 * @param[in] c 観測値
 * @return JSON
 */
//------------------------------------------------------------------------------
Json chunkJson( const ApiAudioChunkResult &c)
{
	return Json{{"index", c.index},
				{"complete", c.complete},
				{"text", c.text},
				{"languages", c.languages ? Json( *c.languages) : Json( nullptr)},
				{"usage", usageJson( c.usage)},
				{"model", static_cast<unsigned>( c.modelReport)},
				{"billing", static_cast<unsigned>( c.billing)},
				{"http", c.http},
				{"code", c.code},
				{"http_error", static_cast<unsigned>( c.httpError)}} ;
}
//------------------------------------------------------------------------------
/**
 * @brief bounded chunkを復元します
 * @param[in] c 検証中JSON
 * @return 所有値
 */
//------------------------------------------------------------------------------
ApiAudioChunkResult readChunk( const Json &c)
{
	ApiAudioChunkResult v ; // 一窓分
	v.index = static_cast<std::uint32_t>( uintValue( c.at( "index"), 49999)) ;
	v.complete = c.at( "complete").get<bool>() ;
	v.text = c.at( "text").get<std::string>() ;
	// 省略と空配列を維持します
	if( !c.at( "languages").is_null()) {
		v.languages = c.at( "languages").get<std::vector<std::string>>() ;
		if( v.languages->size() > 32) {
			invalid() ;
		}
	}
	v.modelReport = static_cast<ApiAudioModelReport>( uintValue( c.at( "model"), 2)) ;
	v.billing = static_cast<ApiTextBilling>( uintValue( c.at( "billing"), 3)) ;
	v.http = static_cast<unsigned>( uintValue( c.at( "http"), 599)) ;
	v.code = static_cast<std::uint32_t>( uintValue( c.at( "code"), UINT32_MAX)) ;
	v.httpError = static_cast<ApiTextHttpError>( uintValue( c.at( "http_error"), 7)) ;
	const auto &u = c.at( "usage") ; // presence付き観測値
	v.usage.kind = static_cast<ApiAudioUsageKind>( uintValue( u.at( "kind"), 2)) ;
	v.usage.priceApplicable = u.at( "applicable").get<bool>() ;
	// durationの未通知を0秒に変換しません
	if( !u.at( "seconds").is_null()) {
		v.usage.seconds = u.at( "seconds").get<double>() ;
	}
	// tokenの未通知も独立して保持します
	if( !u.at( "input").is_null()) {
		v.usage.inputTokens = uintValue( u.at( "input"), 1'000'000'000) ;
	}
	if( !u.at( "output").is_null()) {
		v.usage.outputTokens = uintValue( u.at( "output"), 1'000'000'000) ;
	}
	if( !u.at( "total").is_null()) {
		v.usage.totalTokens = uintValue( u.at( "total"), 1'000'000'000) ;
	}
	return v ;
}
//------------------------------------------------------------------------------
/**
 * @brief 単価snapshotを符号化します
 * @param[in] p 単価
 * @return 全field JSON
 */
//------------------------------------------------------------------------------
Json priceJson( const ApiAudioPriceSnapshot &p)
{
	return Json{{"rate", p.price.microPerMinute ? Json( *p.price.microPerMinute) : Json( nullptr)},
				{"meta", metadataJson( p.metadata)},
				{"source", p.source},
				{"parser", p.parser}} ;
}
//------------------------------------------------------------------------------
/**
 * @brief attemptを符号化します
 * @param[in] a 操作
 * @return 全field JSON
 */
//------------------------------------------------------------------------------
Json attemptJson( const ApiAudioAttempt &a)
{
	return Json{{"operation", a.operationId},
				{"key_revision", a.keyRevision},
				{"price", priceJson( a.price)},
				{"start", a.startIndex},
				{"end", a.completeEnd},
				{"terminal", static_cast<unsigned>( a.terminal)},
				{"error", static_cast<unsigned>( a.error)},
				{"code", a.code},
				{"attempted", a.attemptedSamples},
				{"intent", a.intent ? Json( *a.intent) : Json( nullptr)},
				{"failure", a.failure ? chunkJson( *a.failure) : Json( nullptr)},
				{"parent_sha", a.parentSourceSha},
				{"end_sha", a.endSourceSha}} ;
}
//------------------------------------------------------------------------------
/**
 * @brief 凍結単価を復元します
 * @param[in] p JSON
 * @return 所有単価
 */
//------------------------------------------------------------------------------
ApiAudioPriceSnapshot readPrice( const Json &p)
{
	ApiAudioPriceSnapshot value ; // duration料金だけ
	value.price.microPerMinute = uintValue( p.at( "rate"), 1'000'000'000) ;
	value.source = p.at( "source").get<std::string>() ;
	value.parser = static_cast<std::uint32_t>( uintValue( p.at( "parser"), 1)) ;
	const auto &m = p.at( "meta") ; // 過去UTCは再取得しません
	value.metadata.fetched = m.at( "fetched").get<std::int64_t>() ;
	value.metadata.attempted = m.at( "attempted").get<std::int64_t>() ;
	value.metadata.http = static_cast<unsigned>( uintValue( m.at( "http"), 599)) ;
	value.metadata.error = static_cast<std::uint32_t>( uintValue( m.at( "error"), UINT32_MAX)) ;
	value.metadata.sha = m.at( "sha").get<ApiTextDigest>() ;
	return value ;
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 耐久正本を取得します
 * @param[in] token 小CAS値
 * @return 同じsource ownerとrecord
 * @throws OperationError stale/削除/上限
 * @see koefude::IApiAudioPersistencePort::loadAudioResume
 */
//------------------------------------------------------------------------------
ApiAudioResumeSnapshot IApiAudioPersistencePort::loadAudioResume( ApiAudioResumeToken token)
{
	static_cast<void>( token) ;
	throw OperationError( ErrorKind::INVALID_INPUT, ERROR_NOT_SUPPORTED) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 再開を新opへ耐久保存します
 * @param[in] token 元CAS値
 * @param[in] previousOwner 元snapshot owner
 * @param[in] record 新Pending
 * @param[in] source 同じ原文owner
 * @return 新leaseのACK、失敗時送信禁止
 * @see koefude::IApiAudioPersistencePort::beginAudioResume
 */
//------------------------------------------------------------------------------
ApiAudioPersistenceCompletion
IApiAudioPersistencePort::beginAudioResume( ApiAudioResumeToken token, std::shared_ptr<const Transcript> previousOwner,
										   ApiAudioRecord record, std::shared_ptr<const Transcript> source)
{
	static_cast<void>( token) ;
	static_cast<void>( previousOwner) ;
	record.saveFailed = true ;
	return {std::move( record), std::move( source), false, ERROR_NOT_SUPPORTED} ;
}
//------------------------------------------------------------------------------
/**
 * @brief 一窓だけの最小WAVを作ります
 * @param[in] samples 有限正規化PCM
 * @return metadataなしPCM16 WAV
 * @throws OperationError 不正/上限
 * @see koefude::makeApiAudioWav
 */
//------------------------------------------------------------------------------
std::string makeApiAudioWav( std::span<const float> samples)
{
	// 25MBの十進上限と内部600秒の両方をallocation前に守ります
	if( samples.empty() || samples.size() > K_API_AUDIO_WINDOW_SAMPLES || samples.size() > (25'000'000 - 45) / 2) {
		invalid() ;
	}
	std::string wav ; // metadataを持たない44byte headerとPCM16だけ
	wav.reserve( 44 + samples.size() * 2) ;
	const auto number =
		//------------------------------------------------------------------------------
		/**
		 * @brief little endian整数を追加します
		 * @param[in] value 固定fieldへ符号化する符号なし整数。下位byteからlittle endianで追加します
		 * @param[in] size 出力するbyte数。呼出側は整数型の幅以内の固定field幅を渡します
		 */
		//------------------------------------------------------------------------------
		[&wav]( std::uint32_t value, unsigned size)
		{
			// 固定2/4byteの上位bitを順に出力します
			for( unsigned i = 0; i < size; ++i) {
				wav.push_back( static_cast<char>( (value >> (i * 8)) & 255)) ;
			}
		} ;
	wav += "RIFF" ;
	number( static_cast<std::uint32_t>( 36 + samples.size() * 2), 4) ;
	wav += "WAVEfmt " ;
	number( 16, 4) ;
	number( 1, 2) ;
	number( 1, 2) ;
	number( K_SAMPLE_RATE, 4) ;
	number( K_SAMPLE_RATE * 2, 4) ;
	number( 2, 2) ;
	number( 16, 2) ;
	wav += "data" ;
	number( static_cast<std::uint32_t>( samples.size() * 2), 4) ;
	// floatの非有限値と範囲外を拒否し、符号端点をPCM16へ正確に写します
	for( const float sample : samples) {
		// clippingで不正値を隠しません
		if( !std::isfinite( sample) || std::abs( sample) > 1.0F) {
			invalid() ;
		}
		const auto scaled = static_cast<std::int32_t>( std::lround( static_cast<double>( sample) * 32768)) ; // 丸め値
		number( static_cast<std::uint16_t>( std::clamp( scaled, -32768, 32767)), 2) ;
	}
	return wav ;
}
//------------------------------------------------------------------------------
/**
 * @brief 厳密multipartを作ります
 * @param[in] wav 最小WAV
 * @param[in] autoLanguage 自動か
 * @param[in] boundary 有界ASCII乱数境界
 * @return 有界body
 * @throws OperationError 不正/衝突/上限
 * @see koefude::makeApiAudioMultipart
 */
//------------------------------------------------------------------------------
std::string makeApiAudioMultipart( std::string_view wav, bool autoLanguage, std::string_view boundary)
{
	// ASCII境界の注入、衝突、不正WAVサイズを拒否します
	if( boundary.size() < 16 || boundary.size() > 80 ||
	   boundary.find_first_not_of( "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-") !=
		   std::string_view::npos ||
	   wav.size() < 46 || wav.size() > 19'200'044 || wav.size() >= 25'000'000 || wav.substr( 0, 4) != "RIFF" ||
	   wav.substr( 8, 8) != "WAVEfmt " || wav.find( boundary) != std::string_view::npos) {
		invalid() ;
	}
	std::string body ; // 一窓だけの有界request
	body.reserve( wav.size() + 1024) ;
	const auto part =
		//------------------------------------------------------------------------------
		/**
		 * @brief 固定text partを追加します
		 * @param[in] name 固定名
		 * @param[in] value 固定値
		 */
		//------------------------------------------------------------------------------
		[&body, boundary]( std::string_view name, std::string_view value)
		{
			body += "--" ;
			body += boundary ;
			body += "\r\nContent-Disposition: form-data; name=\"" ;
			body += name ;
			body += "\"\r\n\r\n" ;
			body += value ;
			body += "\r\n" ;
		} ;
	part( "model", K_API_AUDIO_MODEL) ;
	part( "response_format", "json") ;
	// 日本語だけ複数形の公式fieldを使い、自動時はfield自体を省略します
	if( !autoLanguage) {
		part( "languages[]", "ja") ;
	}
	body += "--" ;
	body += boundary ;
	body += "\r\nContent-Disposition: form-data; name=\"file\"; "
			"filename=\"audio.wav\"\r\nContent-Type: audio/wav\r\n\r\n" ;
	body += wav ;
	body += "\r\n--" ;
	body += boundary ;
	body += "--\r\n" ;
	// part overheadを含む内部予算をDWORD化前に確認します
	if( body.size() > K_API_AUDIO_BODY_LIMIT) {
		invalid() ;
	}
	return body ;
}
//------------------------------------------------------------------------------
/**
 * @brief strict通常JSONを検証します
 * @param[in] response 生HTTP所有値
 * @param[in] sentSamples 実送信sample数
 * @return 完全応答/固定診断。生bodyは残しません
 * @see koefude::parseApiAudioResponse
 */
//------------------------------------------------------------------------------
ApiAudioChunkResult parseApiAudioResponse( const ApiAudioHttpResponse &response, std::uint64_t sentSamples)
{
	ApiAudioChunkResult result ; // raw body/自由文を保存値へ移しません
	result.http = response.status ;
	result.code = response.error ;
	result.billing = response.attempted ? ApiTextBilling::OUTCOME_UNKNOWN : ApiTextBilling::NOT_SENT ;
	// 通信失敗と非2xxは完全本文として解釈しません
	if( response.error || response.status < 200 || response.status >= 300) {
		// 既知code/typeだけを範囲検証済みenumへ変換します
		try {
			const auto j = strictJson( response.body, K_API_AUDIO_RESPONSE_LIMIT) ; // 非2xxも有界
			const auto e = j.find( "error") ; // 公式error envelope
			// 文字列値だけの既知分類を読みます
			if( e != j.end() && e->is_object()) {
				// message/param/response IDには触れません
				for( const char *field : {"code", "type"}) {
					const auto v = e->find( field) ; // 最大2既知field
					// 未知型は不明に残します
					if( v == e->end() || !v->is_string()) {
						continue ;
					}
					const auto s = v->get<std::string>() ; // 最大body内の一時値
					// 根拠のある定義済み文字列だけを変換します
					if( s == "rate_limit_error" || s == "slow_down") {
						result.httpError = ApiTextHttpError::RATE_LIMIT ;
					} else if( s == "credit_balance_exhausted") {
						result.httpError = ApiTextHttpError::CREDIT_BALANCE ;
					} else if( s == "organization_spend_limit_exceeded") {
						result.httpError = ApiTextHttpError::ORGANIZATION_SPEND ;
					} else if( s == "project_spend_limit_exceeded") {
						result.httpError = ApiTextHttpError::PROJECT_SPEND ;
					} else if( s == "organization_usage_limit_exceeded") {
						result.httpError = ApiTextHttpError::ORGANIZATION_USAGE ;
					} else if( s == "insufficient_quota") {
						result.httpError = ApiTextHttpError::INSUFFICIENT_QUOTA ;
					} else if( s == "service_unavailable_error" || s == "server_is_overloaded") {
						result.httpError = ApiTextHttpError::SERVICE_OVERLOADED ;
					}
				}
			}
		} catch( ...) {
			result.httpError = ApiTextHttpError::UNKNOWN ;
		} // 生errorを破棄
		return result ;
	}
	try {
		const auto j = strictJson( response.body, K_API_AUDIO_RESPONSE_LIMIT) ; // 完全EOFまで検証
		// 必須textの型と個別上限を検証します。空stringは完全応答です
		if( !j.is_object() || !j.contains( "text") || !j["text"].is_string()) {
			invalid() ;
		}
		auto text = j["text"].get<std::string>() ; // 検証前の有界一時copy
		// escapeされたNUL/不正Unicodeも拒否します
		if( text.size() > K_API_AUDIO_TEXT_LIMIT || text.find( '\0') != std::string::npos) {
			invalid() ;
		}
		static_cast<void>( toWide( text)) ;
		// 省略、空配列、実検出言語を独立に保持します
		if( j.contains( "languages")) {
			const auto &values = j["languages"] ; // 同期borrow
			// 無制限言語数と型変換を拒否します
			if( !values.is_array() || values.size() > 32) {
				invalid() ;
			}
			result.languages.emplace() ;
			// 公式ASCII言語codeだけを保存します
			for( const auto &value : values) {
				// 公式通常JSONは{code:string}でありrequestの文字列配列と区別します
				if( !value.is_object() || !value.contains( "code") || !value["code"].is_string()) {
					invalid() ;
				}
				auto code = value["code"].get<std::string>() ; // 有界一時code
				// ISO言語/localeのASCIIだけ許可します
				if( code.empty() || code.size() > 32 ||
				   code.find_first_not_of( "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") !=
					   std::string::npos) {
					invalid() ;
				}
				result.languages->push_back( std::move( code)) ;
			}
		}
		// 通常JSONで省略されるmodelを要求IDで補完しません
		if( j.contains( "model")) {
			result.modelReport = j["model"].is_string() && j["model"] == std::string( K_API_AUDIO_MODEL)
									 ? ApiAudioModelReport::VERIFIED
									 : ApiAudioModelReport::UNVERIFIED ;
		}
		// 未知usageは本文成功と独立してUnknownへ残します
		if( j.contains( "usage") && j["usage"].is_object()) {
			const auto &u = j["usage"] ; // 同期borrow
			// durationは非負有限secondsのときだけ保存します
			if( u.value( "type", std::string{}) == "duration") {
				// 負数/小数以外の型を暗黙変換しません
				if( !u.contains( "seconds") || !u["seconds"].is_number()) {
					invalid() ;
				}
				const double seconds = u["seconds"].get<double>() ; // 実観測秒
				// 異常usageは完全認識成功へ混ぜません
				if( !std::isfinite( seconds) || seconds < 0 || seconds > 1'000'000'000) {
					invalid() ;
				}
				result.usage.kind = ApiAudioUsageKind::DURATION ;
				result.usage.seconds = seconds ;
				const double actual = static_cast<double>( sentSamples) / K_SAMPLE_RATE ; // 実送信長
				result.usage.priceApplicable = seconds <= actual * 1.1 + 1.0 &&
											   seconds >= std::max( 0.0, actual * 0.9 - 1.0) &&
											   result.modelReport != ApiAudioModelReport::UNVERIFIED ;
			} else if( u.value( "type", std::string{}) == "tokens") {
				result.usage.kind = ApiAudioUsageKind::TOKENS ;
				// 公式token fieldだけを有界整数として保持します
				for( const auto &field : {std::pair{"input_tokens", &result.usage.inputTokens},
										 std::pair{"output_tokens", &result.usage.outputTokens},
										 std::pair{"total_tokens", &result.usage.totalTokens}}) {
					// 省略は空を維持します
					if( u.contains( field.first)) {
						*field.second = uintValue( u[field.first], 1'000'000'000) ;
					}
				}
			}
		}
		result.text = std::move( text) ;
		result.complete = true ;
		result.billing = result.usage.kind == ApiAudioUsageKind::UNKNOWN ? ApiTextBilling::USAGE_MISSING
																		 : ApiTextBilling::USAGE_OBSERVED ;
	} catch( ...) {
		result.code = ERROR_INVALID_DATA ;
	} // 不完全本文を採用しません
	return result ;
}
//------------------------------------------------------------------------------
/**
 * @brief 総送信sample数と分単価から、端数を切り上げた料金概算を計算します
 * @param[in] samples 16kHz mono基準の総送信sample数。再送分も計上する場合は呼出側で含めます
 * @param[in] rate 呼出側で取得・確認した分単価（USDの百万分の一/min）
 * @return ceil(samples * rate / (60 * 16000))に相当する概算（USDの百万分の一）
 * samplesまたはrateが0なら0です。実請求額やAPI通知usageではありません
 * @throws OperationError 分単位の商/余りとrateの積、または最終和がuint64_tで表現できない場合
 * 分類はErrorKind::INVALID_INPUT、codeはERROR_INVALID_DATAです
 * @note 未取得単価のoptionalは扱いません。未知単価を0へ読み替えて渡してはいけません
 * @see koefude::estimateApiAudioCost
 */
//------------------------------------------------------------------------------
std::uint64_t estimateApiAudioCost( std::uint64_t samples, std::uint64_t rate)
{
	constexpr std::uint64_t K_MINUTE_SAMPLES = 60ULL * K_SAMPLE_RATE ; // 同じ16kHz基準
	const auto				q = samples / K_MINUTE_SAMPLES ; // 積の前に商へ分割
	const auto				r = samples % K_MINUTE_SAMPLES ; // 分未満の余り
	// 商/余りの両積をそれぞれ検証します
	if( (q && rate > std::numeric_limits<std::uint64_t>::max() / q) ||
	   (r && rate > std::numeric_limits<std::uint64_t>::max() / r)) {
		invalid() ;
	}
	const auto base = q * rate ; // 全分の料金
	const auto tail = r * rate / K_MINUTE_SAMPLES + (r * rate % K_MINUTE_SAMPLES != 0 ? 1 : 0) ; // 切上げ
	// 最後の加算も表現範囲内だけ許可します
	if( base > std::numeric_limits<std::uint64_t>::max() - tail) {
		invalid() ;
	}
	return base + tail ;
}
//------------------------------------------------------------------------------
/**
 * @brief キー資格と取得metadataの鮮度を照合し、使用価格を所有copyへ固定します
 * @param[in] c Models・価格・取得時刻・キーrevisionを持つ、呼出し中だけの不変借用です
 * @param[in] keyReady 呼出側が確認した利用可能キーの有無。秘密値自体は受け取りません
 * @param[in] now 鮮度照合の現在UTC秒。各取得UTCが未来または24時間超なら拒否します
 * @return 条件を満たす価格と価格metadataの所有snapshot。キーなし、未load、破損cache、更新中、
 * キーrevision不一致、固定音声モデル未掲載、Modelsの最終HTTP401/403、単価未取得、または
 * Models/価格metadataの鮮度不成立ならnulloptです。記録済みmonotonicも非未来・24時間以内を要求します
 * @note キー実体・残高・実際の送信可否は確認せず、通信・cache更新・設定変更を行いません
 * 存在する0単価は未取得と区別して返します。所有copyの動的確保例外は伝播します
 * @see koefude::apiAudioPriceSnapshot
 */
//------------------------------------------------------------------------------
std::optional<ApiAudioPriceSnapshot> apiAudioPriceSnapshot( const ApiModelCatalog &c, bool keyReady, std::int64_t now)
{
	const auto fresh =
		//------------------------------------------------------------------------------
		/**
		 * @brief UTC/monotonicの両方で鮮度を照合します
		 * @param[in] m metadata
		 * @return 有効
		 */
		//------------------------------------------------------------------------------
		[now]( const ApiCatalogMetadata &m)
		{
			return m.fetched > 0 && now >= m.fetched && now - m.fetched <= 86400 &&
				   (m.monotonic == std::chrono::steady_clock::time_point{} ||
					(std::chrono::steady_clock::now() >= m.monotonic &&
					 std::chrono::steady_clock::now() - m.monotonic <= std::chrono::hours( 24))) ;
		} ;
	// キー変更後の旧Models資格や未掲載単価で送信しません
	if( !keyReady || !c.loaded || c.cacheCorrupt || c.refreshing || c.modelsKeyRevision != c.keyRevision ||
	   !c.models.contains( std::string( K_API_AUDIO_MODEL)) || c.modelMeta.http == 401 || c.modelMeta.http == 403 ||
	   !fresh( c.modelMeta) || !fresh( c.audioPriceMeta) || !c.audioPrice.microPerMinute) {
		return std::nullopt ;
	}
	return ApiAudioPriceSnapshot{c.audioPrice, c.audioPriceMeta} ;
}
//------------------------------------------------------------------------------
/**
 * @brief 有界PCMのVAD範囲と全file turnから非重複planを作ります
 * @param[in] audio 全PCMの部分読取元
 * @param[in] speech 元sample範囲の和集合
 * @param[in] turns 全fileの音声cluster
 * @param[in] options 固定設定
 * @param[in] price 有効料金
 * @param[in] pcm 実測同一性
 * @param[in] stop 取消し
 * @return 全窓検証済みplan
 * @throws OperationError 不正/上限
 * @see koefude::makeApiAudioPlan
 */
//------------------------------------------------------------------------------
ApiAudioPlan makeApiAudioPlan( const AudioData &audio, std::span<const AsrSampleWindow> speech,
							  std::span<const SpeakerTurn> turns, const EngineOptions &options,
							  const ApiAudioPriceSnapshot &price, const DecodedPcmIdentity &pcm, std::stop_token stop)
{
	ApiAudioPlan plan ; // 全窓完成後だけ返す所有値
	plan.pcm = pcm ;
	plan.autoLanguage = options.autoLanguage ;
	plan.diarization = options.diarization ;
	plan.speechFilter = options.speechFilter ;
	plan.price = price ;
	// 全fileをRAMへ展開せず、端点だけを有界に保持します
	if( audio.sampleCount() != pcm.sampleCount || speech.size() > K_MAXIMUM_SEGMENTS ||
	   turns.size() > K_MAXIMUM_SEGMENTS) {
		invalid() ;
	}
	plan.version = options.asrProfile == AsrProfile::STANDARD ? 1 : 2 ;
	// 文脈方式は短VADと話者境界を跨ぎ、元PCMの連続範囲を保ちます
	if( plan.version == 2) {
		std::vector<AsrSampleWindow> ranges( speech.begin(), speech.end()) ; // 有界な端点だけ
		// VAD OFFは検出依存の空入力を全音声へ置き換えます
		if( !options.speechFilter) {
			ranges = {{0, pcm.sampleCount}} ;
		}
		std::sort( ranges.begin(), ranges.end(),
				  //------------------------------------------------------------------------------
				  /**
				   * @brief 和集合を原順にします
				   * @param[in] a 左
				   * @param[in] b 右
				   * @return 原順
				   */
				  //------------------------------------------------------------------------------
				  []( const auto &a, const auto &b)
				  {
					  return a.begin < b.begin ;
				  }) ;
		std::vector<AsrSampleWindow> merged ; // gap候補だけを保持
		// 重複や隣接を統合し、最初から最後までを送ります
		for( const auto &range : ranges) {
			// 不正範囲をclipして受理しません
			if( range.begin >= range.end || range.end > pcm.sampleCount) {
				invalid() ;
			}
			// 重複と隣接はgapではありません
			if( !merged.empty() && range.begin <= merged.back().end) {
				merged.back().end = std::max( merged.back().end, range.end) ;
			} else {
				merged.push_back( range) ;
			}
		}
		std::size_t			gap = 1 ; // 前から走査するgap
		std::uint64_t		offset = merged.empty() ? 0 : merged.front().begin ; // 元sample原点
		const std::uint64_t last = merged.empty() ? 0 : merged.back().end ; // VAD最終sample
		// 中間窓は60〜120秒、最後の短い窓は捨てず終端へ進めます
		while( offset < last) {
			checkCancelled( stop) ;
			std::uint64_t end = last ; // 120秒以内の末尾はそのまま保持
			// 長い残量だけで90秒近傍を探索します
			if( last - offset > K_API_AUDIO_CONTEXT_SAMPLES) {
				const auto	  low = offset + 60ULL * K_SAMPLE_RATE ; // 中間窓の最小
				const auto	  high = offset + K_API_AUDIO_CONTEXT_SAMPLES ; // 探索最大
				const auto	  target = offset + 90ULL * K_SAMPLE_RATE ; // 目標sample
				std::uint64_t distance = UINT64_MAX ; // gapなしならenergyへ進む
				// 終端が候補最小より前のgapを飛ばします
				while( gap < merged.size() && merged[gap].begin < low) {
					++gap ;
				}
				// 中央が候補範囲に入るgapから目標に最も近い点を採ります
				for( std::size_t j = gap > 1 ? gap - 1 : 1; j < merged.size(); ++j) {
					const auto middle = merged[j - 1].end + (merged[j].begin - merged[j - 1].end) / 2 ; // overflowなし
					// 以後はより遅いgapなので探索を終えます
					if( middle > high) {
						break ;
					}
					// 範囲外の中央は文脈長を縮めません
					if( middle < low) {
						continue ;
					}
					const auto delta = middle > target ? middle - target : target - middle ;
					// 同距離では早い点を保ちます
					if( delta < distance) {
						distance = delta ;
						end = middle ;
					}
				}
				// gap候補なしなら86〜90秒の20ms energyを比較します
				if( distance == UINT64_MAX) {
					constexpr std::size_t K_CELL = K_SAMPLE_RATE / 50 ; // 20ms
					std::vector<float>	  quiet( 4 * K_SAMPLE_RATE) ; // 静音探索だけ
					audio.readSamples( target - quiet.size(), quiet) ;
					double energyMin = std::numeric_limits<double>::max() ; // 最小二乗和
					end = target ;
					// 同値なら遅いcellを選びます
					for( std::size_t at = 0; at + K_CELL <= quiet.size(); at += K_CELL) {
						double energy = 0 ; // 当該20msの二乗和
						// 外部PCMの不正sampleをenergyで隠しません
						for( std::size_t k = at; k < at + K_CELL; ++k) {
							// 非有限または範囲外を拒否します
							if( !std::isfinite( quiet[k]) || std::abs( quiet[k]) > 1.0F) {
								invalid() ;
							}
							energy += static_cast<double>( quiet[k]) * quiet[k] ;
						}
						// 同値なら遅い点へ更新します
						if( energy <= energyMin) {
							energyMin = energy ;
							end = target - quiet.size() + at + K_CELL / 2 ;
						}
					}
				}
			}
			// 録音時間の固定上限を作らず、件数予算と前進を保証します
			if( plan.windows.size() >= K_MAXIMUM_SEGMENTS || end <= offset ||
			   end - offset > K_API_AUDIO_CONTEXT_SAMPLES) {
				invalid() ;
			}
			plan.windows.push_back( {offset, end, {}}) ;
			plan.sendSamples += end - offset ;
			offset = end ;
		}
		struct TurnPoint {
			std::uint64_t at ; // 音響端点の元sample
			int			  speaker ; // global cluster
			int			  delta ; // 開始1/終端-1
		} ;
		std::vector<TurnPoint> points ; // windowとの全探索を避ける有界端点
		// 分割を確定後にだけ話者端点を処理します
		for( const auto &t : turns) {
			// 不正turnの時間とclusterを拒否します
			if( t.startMs < 0 || t.endMs <= t.startMs || t.speaker < 0 || t.speaker > 10000 ||
			   t.endMs > static_cast<std::int64_t>( (pcm.sampleCount + 15) / 16)) {
				invalid() ;
			}
			points.push_back( {std::min( pcm.sampleCount, static_cast<std::uint64_t>( t.startMs) * 16), t.speaker, 1}) ;
			points.push_back( {std::min( pcm.sampleCount, static_cast<std::uint64_t>( t.endMs) * 16), t.speaker, -1}) ;
		}
		std::sort( points.begin(), points.end(),
				  //------------------------------------------------------------------------------
				  /**
				   * @brief 元sample順にします
				   * @param[in] a 左
				   * @param[in] b 右
				   * @return 原順
				   */
				  //------------------------------------------------------------------------------
				  []( const auto &a, const auto &b)
				  {
					  return a.at < b.at ;
				  }) ;
		std::map<int, int> active ; // 同sampleのturn数
		std::size_t		   point = 0 ; // 前から走査
		// 本文は推測分配せず、音響候補集合だけを保存します
		for( auto &window : plan.windows) {
			std::set<int> ids ; // 最大20候補
			// 窓先頭までの端点を適用します
			while( point < points.size() && points[point].at <= window.begin) {
				const auto &p = points[point++] ; // points内borrow
				active[p.speaker] += p.delta ;
				// 終端済みclusterを次窓へ持ち越しません
				if( active[p.speaker] == 0) {
					active.erase( p.speaker) ;
				}
			}
			// 先頭で有効な候補を追加します
			for( const auto &[id, count] : active) {
				// 有効な音響turnだけを候補へ加えます
				if( count > 0) {
					ids.insert( id) ;
				}
			}
			// 内側の交代は境界にせず候補へ集約します
			while( point < points.size() && points[point].at < window.end) {
				const auto &p = points[point++] ; // 原順の端点
				active[p.speaker] += p.delta ;
				// 開始turnは本文の候補へ加えます
				if( active[p.speaker] > 0) {
					ids.insert( p.speaker) ;
				}
				// 終端turnのactive所有だけを解放します
				if( active[p.speaker] == 0) {
					active.erase( p.speaker) ;
				}
			}
			window.speakers.assign( ids.begin(), ids.end()) ;
		}
		// gapを含む実送信量から概算を作ります
		if( !price.price.microPerMinute) {
			invalid() ;
		}
		plan.estimateMicroUsd = estimateApiAudioCost( plan.sendSamples, *price.price.microPerMinute) ;
		plan.digest = apiTextDigest( planJson( plan).dump()) ;
		validatePlan( plan) ;
		return plan ;
	}
	struct Point {
		std::uint64_t at ;
		int			  speaker ;
		int			  delta ;
	} ; // speaker=-1はVAD和集合の端点
	std::vector<Point> points ; // 最大200,000端点
	points.reserve( 2 * (speech.size() + turns.size())) ;
	// 重なりVAD範囲もdepthで和集合とし同sampleを二重送信しません
	for( const auto &w : speech) {
		// 範囲外と空区間を拒否します
		if( w.begin >= w.end || w.end > pcm.sampleCount) {
			invalid() ;
		}
		points.push_back( {w.begin, -1, 1}) ;
		points.push_back( {w.end, -1, -1}) ;
	}
	// 全fileの話者端点を同じsample原点へ戻します
	for( const auto &t : turns) {
		// 不明turnは音声を削除せず候補集合へ加えません
		if( t.startMs < 0 || t.endMs <= t.startMs || t.endMs > static_cast<std::int64_t>( (pcm.sampleCount + 15) / 16) ||
		   t.speaker < 0 || t.speaker > 10000) {
			invalid() ;
		}
		points.push_back( {std::min( pcm.sampleCount, static_cast<std::uint64_t>( t.startMs) * 16), t.speaker, 1}) ;
		points.push_back( {std::min( pcm.sampleCount, static_cast<std::uint64_t>( t.endMs) * 16), t.speaker, -1}) ;
	}
	std::sort( points.begin(), points.end(),
			  //------------------------------------------------------------------------------
			  /**
			   * @brief 同sampleの端点をまとめます
			   * @param[in] a 左
			   * @param[in] b 右
			   * @return 原順
			   */
			  //------------------------------------------------------------------------------
			  []( const Point &a, const Point &b)
			  {
				  return a.at < b.at ;
			  }) ;
	std::map<int, int>			active ; // clusterごとの重複turn数
	int							vad = 0 ; // VAD範囲の和集合depth
	std::uint64_t				previous = 0 ; // 次atom先頭
	std::vector<ApiAudioWindow> atoms ; // 同候補の隣接範囲は先に連結
	// 各端点を一回だけ処理しturn×window全探索を行いません
	for( std::size_t i = 0; i < points.size();) {
		checkCancelled( stop) ;
		const auto at = points[i].at ; // 同sampleのイベント群
		// VADが有効な正長範囲だけ送信候補とします
		if( vad > 0 && previous < at) {
			std::vector<int> speakers ; // 最大20のglobal候補
			// 現在有効なclusterだけを写します
			for( const auto &[id, count] : active) {
				if( count > 0) {
					speakers.push_back( id) ;
				}
			}
			// 過剰候補は有界に拒否します
			if( speakers.size() > 20 || atoms.size() >= K_MAXIMUM_ATTRIBUTION_ITEMS) {
				invalid() ;
			}
			// 同候補が連続する音声だけを長文脈へ結合します
			if( !atoms.empty() && atoms.back().end == previous && atoms.back().speakers == speakers) {
				atoms.back().end = at ;
			} else {
				atoms.push_back( {previous, at, std::move( speakers)}) ;
			}
		}
		// 同じsampleの開始/終端を全て適用してから次の範囲を作ります
		while( i < points.size() && points[i].at == at) {
			const auto &p = points[i++] ; // 現端点
			// VADと話者の集合を分けて更新します
			if( p.speaker == -1) {
				vad += p.delta ;
			} else {
				active[p.speaker] += p.delta ;
				if( active[p.speaker] == 0) {
					active.erase( p.speaker) ;
				}
			}
		}
		previous = at ;
	}
	// 1秒未満の相槌を捨てず、隣接結合後の候補を保守的に保持します
	std::vector<ApiAudioWindow> compact ; // erase反復をせず線形に短区間を保ちます
	compact.reserve( atoms.size()) ;
	// 各atomを高々一回結合し、先頭短区間は次の隣接atomへ結合します
	for( auto &atom : atoms) {
		// 正長の独立atomまたは長い隣接同士はそのまま残します
		if( compact.empty() ||
		   (atom.end - atom.begin >= K_SAMPLE_RATE && compact.back().end - compact.back().begin >= K_SAMPLE_RATE)) {
			compact.push_back( std::move( atom)) ;
			continue ;
		}
		auto speakers = compact.back().speakers ; // 結合後の全候補
		speakers.insert( speakers.end(), atom.speakers.begin(), atom.speakers.end()) ;
		std::sort( speakers.begin(), speakers.end()) ;
		speakers.erase( std::unique( speakers.begin(), speakers.end()), speakers.end()) ;
		const auto begin = compact.back().begin ; // 全範囲先頭
		const auto end = atom.end ; // 全範囲終端
		// gapを勝手に取り込まず、混在26秒/単独600秒を維持します
		if( compact.back().end != atom.begin ||
		   end - begin > (speakers.size() > 1 ? 26ULL * K_SAMPLE_RATE : K_API_AUDIO_WINDOW_SAMPLES)) {
			compact.push_back( std::move( atom)) ;
			continue ;
		}
		compact.back() = {begin, end, std::move( speakers)} ;
	}
	// 各atomを最大600秒だけ読み、超過時は静かな位置を既存splitterで選びます
	for( const auto &atom : compact) {
		std::uint64_t offset = atom.begin ; // このatom内の次sample
		const auto	  cap =
			   atom.speakers.size() > 1 ? 26ULL * K_SAMPLE_RATE : K_API_AUDIO_WINDOW_SAMPLES ; // 帰属別の上限
		// 短縮済み窓を切り出して元sampleを一度だけ被覆します
		while( offset < atom.end) {
			checkCancelled( stop) ;
			const auto count =
				static_cast<std::size_t>( std::min( K_API_AUDIO_WINDOW_SAMPLES, atom.end - offset)) ; // 読込予算
			std::vector<float> samples( count) ; // 最大36.63MiB、全file展開なし
			audio.readSamples( offset, samples) ;
			const AsrWindow whole{0, count} ; // 読込範囲
			const auto		windows = splitAsrWindows( samples, std::span( &whole, 1),
													  static_cast<std::size_t>( cap)) ; // 静音分割
			// 分割結果の全元sampleを一度だけ追加します
			for( const auto &w : windows) {
				// plan件数を追加前に検証します
				if( plan.windows.size() >= K_MAXIMUM_SEGMENTS) {
					invalid() ;
				}
				plan.windows.push_back( {offset + w.begin, offset + w.end, atom.speakers}) ;
				plan.sendSamples += w.end - w.begin ;
			}
			offset += count ;
		}
	}
	// 未掲載単価からゼロ料金を作りません
	if( !price.price.microPerMinute) {
		invalid() ;
	}
	plan.estimateMicroUsd = estimateApiAudioCost( plan.sendSamples, *price.price.microPerMinute) ;
	plan.digest = apiTextDigest( planJson( plan).dump()) ;
	validatePlan( plan) ;
	return plan ;
}
//------------------------------------------------------------------------------
/**
 * @brief 音声APIの保存状態が送信前または送信中に分類されるかを判定します
 * @param[in] t 判定する状態。状態を変更せず、通信や取消しも行いません
 * @return ApiAudioTerminal::PENDINGまたはSENDINGならtrue、それ以外の終端と未知enum値はfalseです
 * trueはAPI成功や実通信の継続を保証せず、未終端として扱うための分類です
 * @see koefude::apiAudioActive
 */
//------------------------------------------------------------------------------
bool apiAudioActive( ApiAudioTerminal t) noexcept
{
	return t == ApiAudioTerminal::PENDING || t == ApiAudioTerminal::SENDING ;
}
//------------------------------------------------------------------------------
/**
 * @brief 保存済み応答列の先頭から連続する完全応答の窓数を返します
 * @param[in] r 完全応答prefixと任意の末尾一失敗からなる検証済み記録の呼出し中だけの読取り借用です
 * @return 完全応答の窓数で、次の未完窓の0始まり位置にもなります。空textの完全応答も数え、
 * 応答なしなら0です。sample数・文字数・送信試行数ではありません
 * @pre validateApiAudioRecord等で件数と応答順序を検証済みであること
 * @note 末尾一件のcompleteだけから算出し、列全体を再検証しません。recordを変更しません
 * @see koefude::apiAudioCompletePrefix
 */
//------------------------------------------------------------------------------
std::uint32_t apiAudioCompletePrefix( const ApiAudioRecord &r) noexcept
{
	return static_cast<std::uint32_t>( r.chunks.size() - (!r.chunks.empty() && !r.chunks.back().complete ? 1 : 0)) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 保存recordを完全検証します
 * @param[in] r 保存値
 * @param[in] source 元原文、null可
 * @throws OperationError 不整合
 * @see koefude::validateApiAudioRecord
 */
//------------------------------------------------------------------------------
void validateApiAudioRecord( const ApiAudioRecord &r, const Transcript *source)
{
	validatePlan( r.plan) ;
	// ID/op/enumと件数はsourceへのアクセス前に検証します
	if( !r.historyId || !r.operationId || !r.keyRevision || static_cast<unsigned>( r.terminal) > 7 ||
	   static_cast<unsigned>( r.error) > static_cast<unsigned>( ErrorKind::TEMPORARY_STORAGE) ||
	   (r.version != 1 && r.version != 2) || r.chunks.size() > r.plan.windows.size() ||
	   (r.version == 1 && (!r.attempts.empty() || r.attemptedSamples > r.plan.sendSamples))) {
		invalid() ;
	}
	std::size_t	  bytes = 0 ; // 保存本文の合計予算
	std::uint64_t attempted = 0 ; // 観測送信量の照合
	// 完全prefixと最大一失敗だけを許可します
	for( std::size_t i = 0; i < r.chunks.size(); ++i) {
		const auto &c = r.chunks[i] ; // 固定位置
		// 失敗以後の後続応答や不正enumを受理しません
		if( c.index != i || (!c.complete && i + 1 != r.chunks.size()) || c.http > 599 ||
		   static_cast<unsigned>( c.modelReport) > 2 || static_cast<unsigned>( c.billing) > 3 ||
		   static_cast<unsigned>( c.httpError) > 7 || static_cast<unsigned>( c.usage.kind) > 2 ||
		   c.text.size() > K_API_AUDIO_TEXT_LIMIT || bytes > K_MAXIMUM_TEXT_BYTES - c.text.size() ||
		   (!c.complete && !c.text.empty()) || c.text.find( '\0') != std::string::npos ||
		   (c.usage.seconds &&
			(!std::isfinite( *c.usage.seconds) || *c.usage.seconds < 0 || *c.usage.seconds > 1'000'000'000))) {
			invalid() ;
		}
		static_cast<void>( toWide( c.text)) ;
		// codecへ直接渡された値にもHTTP parserと同じ言語上限を適用します
		if( c.languages) {
			if( c.languages->size() > 32) {
				invalid() ;
			}
			for( const auto &language : *c.languages) {
				if( language.empty() || language.size() > 32 ||
				   language.find_first_not_of( "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") !=
					   std::string::npos) {
					invalid() ;
				}
			}
		}
		// encode時もdecode時と同じtoken利用量の範囲を守ります
		for( const auto &tokens : {c.usage.inputTokens, c.usage.outputTokens, c.usage.totalTokens}) {
			if( tokens && *tokens > 1'000'000'000) {
				invalid() ;
			}
		}
		bytes += c.text.size() ;
		// 送信後の不明も送信試行量へ必ず加算します
		if( c.billing != ApiTextBilling::NOT_SENT) {
			attempted += r.plan.windows[i].end - r.plan.windows[i].begin ;
		}
	}
	// intentは次位置だけを表し、送信中断の課金不明を残します
	if( r.intent) {
		// 完全prefixを飛び越えるintentを拒否します
		if( *r.intent >= r.plan.windows.size() || *r.intent != r.chunks.size()) {
			invalid() ;
		}
		attempted += r.plan.windows[*r.intent].end - r.plan.windows[*r.intent].begin ;
	}
	// intentは未送信の可能性もあるためattempted上限として扱います
	if( (r.version == 1 && r.attemptedSamples > attempted) ||
	   (r.terminal == ApiAudioTerminal::COMPLETED &&
		(r.intent || r.chunks.size() != r.plan.windows.size() || bytes == 0)) ||
	   (r.terminal == ApiAudioTerminal::NO_SPEECH && (!r.plan.windows.empty() || !r.chunks.empty())) ||
	   (r.terminal == ApiAudioTerminal::EMPTY_TRANSCRIPT &&
		(bytes || r.intent || r.chunks.size() != r.plan.windows.size()))) {
		invalid() ;
	}
	// 完全終端へ失敗応答や未取得窓を含めません
	if( (r.terminal == ApiAudioTerminal::COMPLETED || r.terminal == ApiAudioTerminal::EMPTY_TRANSCRIPT) &&
	   std::any_of( r.chunks.begin(), r.chunks.end(),
				   //------------------------------------------------------------------------------
				   /**
				    * @brief 不完全応答を検出します
				    * @param[in] c 応答
				    * @return 不完全
				    */
				   //------------------------------------------------------------------------------
				   []( const auto &c)
				   {
					   return !c.complete ;
				   })) {
		invalid() ;
	}
	// v2の累積量は再送分を含むためplan総量では制限しません
	if( r.version == 2) {
		// 無制限の操作履歴を作りません
		if( r.attempts.empty() || r.attempts.size() > K_API_AUDIO_MAXIMUM_ATTEMPTS) {
			invalid() ;
		}
		std::uint32_t end = 0 ; // 隙間のない成功範囲
		std::uint64_t total = 0 ; // overflow付き累積試行量
		std::uint64_t op = 0 ; // 単調操作ID
		ApiTextDigest previousSha{} ; // 操作間のprefix連鎖
		// 各操作を一回検証し、本文の複製を持たせません
		for( std::size_t i = 0; i < r.attempts.size(); ++i) {
			const auto &a = r.attempts[i] ; // 操作metadata
			// 旧移行の初回parentSHAは未観測の空値で保持します
			if( !a.operationId || a.operationId <= op || !a.keyRevision || a.startIndex != end ||
			   a.completeEnd < a.startIndex || a.completeEnd > apiAudioCompletePrefix( r) ||
			   static_cast<unsigned>( a.terminal) > 7 || static_cast<unsigned>( a.error) > 13 ||
			   !a.price.price.microPerMinute || *a.price.price.microPerMinute > 1'000'000'000 ||
			   a.price.source != r.plan.price.source || a.price.parser != 1 || a.price.metadata.fetched <= 0 ||
			   a.price.metadata.attempted < 0 || a.price.metadata.http > 599 ||
			   (i && a.parentSourceSha != previousSha) || (i + 1 < r.attempts.size() && apiAudioActive( a.terminal))) {
				invalid() ;
			}
			std::uint64_t maximum = 0 ; // 当操作の送信可能量だけ
			// 同じ成功chunkは生成した一操作にだけ属します
			for( std::size_t k = a.startIndex; k < a.completeEnd; ++k) {
				if( r.chunks[k].billing != ApiTextBilling::NOT_SENT) {
					maximum += r.plan.windows[k].end - r.plan.windows[k].begin ;
				}
			}
			// failureの本文なしtyped診断も検証します
			if( a.failure) {
				const auto &f = *a.failure ; // 過去失敗chunk
				if( f.index != a.completeEnd || f.index >= r.plan.windows.size() || f.complete || !f.text.empty() ||
				   f.http > 599 || static_cast<unsigned>( f.billing) > 3 || static_cast<unsigned>( f.modelReport) > 2 ||
				   static_cast<unsigned>( f.httpError) > 7 || static_cast<unsigned>( f.usage.kind) > 2 ||
				   (f.languages && f.languages->size() > 32) ||
				   (f.usage.inputTokens && *f.usage.inputTokens > 1'000'000'000) ||
				   (f.usage.outputTokens && *f.usage.outputTokens > 1'000'000'000) ||
				   (f.usage.totalTokens && *f.usage.totalTokens > 1'000'000'000) ||
				   (f.usage.seconds &&
					(!std::isfinite( *f.usage.seconds) || *f.usage.seconds < 0 || *f.usage.seconds > 1'000'000'000))) {
					invalid() ;
				}
				// 明確未送信を試行量へ含めません
				if( f.billing != ApiTextBilling::NOT_SENT) {
					maximum += r.plan.windows[f.index].end - r.plan.windows[f.index].begin ;
				}
			}
			// intentは送信済みと断定せず上限へ含めます
			if( a.intent) {
				if( *a.intent != a.completeEnd || *a.intent >= r.plan.windows.size() || a.failure) {
					invalid() ;
				}
				maximum += r.plan.windows[*a.intent].end - r.plan.windows[*a.intent].begin ;
			}
			// 再送で累積量が大きくても整数overflowを拒否します
			if( a.attemptedSamples > maximum || total > UINT64_MAX - a.attemptedSamples) {
				invalid() ;
			}
			total += a.attemptedSamples ;
			end = a.completeEnd ;
			op = a.operationId ;
			previousSha = a.endSourceSha ;
		}
		const auto &a = r.attempts.back() ; // 現操作の投影
		// 現opとprefix/診断が別履歴へ混ざっていないことを照合します
		if( a.operationId != r.operationId || a.keyRevision != r.keyRevision ||
		   a.completeEnd != apiAudioCompletePrefix( r) || a.terminal != r.terminal || a.error != r.error ||
		   a.code != r.code || a.intent != r.intent || a.endSourceSha != r.sourceSha || total != r.attemptedSamples ||
		   a.failure.has_value() != (!r.chunks.empty() && !r.chunks.back().complete) ||
		   (a.failure && *a.failure != r.chunks.back())) {
			invalid() ;
		}
	}
	// 元本文があるときだけ全field SHAとPCM同一性を照合します
	if( source &&
	   (source->asrModel != AsrModel::OPEN_AI_AUDIO || !source->pcmIdentity ||
		(source->asrProfile != AsrProfile::QUALITY && source->asrProfile != AsrProfile::STANDARD) || source->usedGpu ||
		source->cpuRetried || *source->pcmIdentity != r.plan.pcm || fingerprintReadingSource( *source) != r.sourceSha)) {
		invalid() ;
	}
	// 保存済み本文は同じ完全応答prefixの区間組立てと一致する必要があります
	if( source) {
		std::vector<TextSegment> segments ; // API語時刻なしの送信範囲だけ
		// 完全textを一度だけ元窓へ戻し、別本文の移植を拒否します
		for( std::size_t i = 0; i < r.chunks.size(); ++i) {
			const auto &c = r.chunks[i] ; // 同じindexの完全/失敗値
			// 空応答と失敗は表示行を生成しません
			if( c.complete && !c.text.empty()) {
				const auto &w = r.plan.windows[i] ; // 固定元窓
				segments.push_back(
					{static_cast<std::int64_t>( w.begin / 16), static_cast<std::int64_t>( (w.end + 15) / 16), c.text}) ;
			}
		}
		auto expected = *source ; // 表示名は既存fingerprintの対象外
		expected.lines = assembleApiAudioTranscript( r, source->speakerTurns) ;
		if( fingerprintReadingSource( expected) != r.sourceSha) {
			invalid() ;
		}
	}
}
//------------------------------------------------------------------------------
/**
 * @brief V8の有界audio recordを符号化します
 * @param[in] r 検証済み値
 * @return 所有JSON
 * @throws OperationError 不正/上限
 * @see koefude::encodeApiAudioRecord
 */
//------------------------------------------------------------------------------
std::string encodeApiAudioRecord( const ApiAudioRecord &r)
{
	validateApiAudioRecord( r, nullptr) ;
	Json chunks = Json::array() ; // 最大50,000応答
	// 本文とtyped metadataだけを保存し生bodyを含めません
	for( const auto &c : r.chunks) {
		chunks.push_back( chunkJson( c)) ;
	}
	auto value = Json{{"domain", "koefude.api_audio.record"},
					  {"version", r.version},
					  {"history", r.historyId},
					  {"operation", r.operationId},
					  {"key_revision", r.keyRevision},
					  {"plan", planJson( r.plan)},
					  {"plan_sha", r.plan.digest},
					  {"chunks", chunks},
					  {"intent", r.intent ? Json( *r.intent) : Json( nullptr)},
					  {"terminal", static_cast<unsigned>( r.terminal)},
					  {"attempted", r.attemptedSamples},
					  {"source_sha", r.sourceSha},
					  {"update", r.update},
					  {"save_failed", r.saveFailed},
					  {"error", static_cast<unsigned>( r.error)},
					  {"code", r.code}} ;
	// v1のencode内容は維持し、新recordだけがattemptを持ちます
	if( r.version == 2) {
		value["attempts"] = Json::array() ;
		for( const auto &a : r.attempts) {
			value["attempts"].push_back( attemptJson( a)) ;
		}
	}
	auto body = value.dump() ;
	// encodeとdecodeで同じbyte/構造予算を適用し、書けたが再読不能なrecordを作りません
	static_cast<void>( strictJson( body, 40 * 1024 * 1024, 4'000'000)) ;
	return body ;
}
//------------------------------------------------------------------------------
/**
 * @brief V8 audio recordを厳密復元します
 * @param[in] body 有界JSON
 * @return 完全検証前の所有値。source整合は呼出側も検証します
 * @see koefude::decodeApiAudioRecord
 */
//------------------------------------------------------------------------------
ApiAudioRecord decodeApiAudioRecord( std::string_view body)
{
	try {
		const auto j = strictJson( body, 40 * 1024 * 1024, 4'000'000) ; // plan/prefixだけの保存上限
		// V8 domain内でも未知record版は拒否します
		if( j.at( "domain") != "koefude.api_audio.record" || (j.at( "version") != 1 && j.at( "version") != 2)) {
			invalid() ;
		}
		ApiAudioRecord r ; // 完全検証前に公開しません
		r.version = static_cast<std::uint32_t>( uintValue( j.at( "version"), 2)) ;
		r.historyId = uintValue( j.at( "history")) ;
		r.operationId = uintValue( j.at( "operation")) ;
		r.keyRevision = uintValue( j.at( "key_revision")) ;
		r.plan.digest = j.at( "plan_sha").get<ApiTextDigest>() ;
		r.sourceSha = j.at( "source_sha").get<ApiTextDigest>() ;
		r.update = uintValue( j.at( "update")) ;
		r.saveFailed = j.at( "save_failed").get<bool>() ;
		r.error = static_cast<ErrorKind>( uintValue( j.at( "error"), 13)) ;
		r.code = static_cast<std::uint32_t>( uintValue( j.at( "code"), UINT32_MAX)) ;
		r.terminal = static_cast<ApiAudioTerminal>( uintValue( j.at( "terminal"), 7)) ;
		r.attemptedSamples = uintValue( j.at( "attempted")) ;
		// nullは未送信、ゼロindexと区別します
		if( !j.at( "intent").is_null()) {
			r.intent = static_cast<std::uint32_t>( uintValue( j.at( "intent"), 49999)) ;
		}
		const auto &p = j.at( "plan") ; // 固定plan構造
		// API語時刻や任意modelの保存値へ読み替えません
		if( (p.at( "version") != 1 && p.at( "version") != 2) || p.at( "model") != std::string( K_API_AUDIO_MODEL) ||
		   p.at( "basis") != "LocalAudioWindow") {
			invalid() ;
		}
		r.plan.version = static_cast<std::uint32_t>( uintValue( p.at( "version"), 2)) ;
		// 新方式の固定定数を未知設定へ読み替えません
		if( r.plan.version == 2 &&
		   (p.at( "method") != "Context90" || uintValue( p.at( "target_samples")) != 90ULL * K_SAMPLE_RATE ||
			uintValue( p.at( "minimum_samples")) != 60ULL * K_SAMPLE_RATE ||
			uintValue( p.at( "maximum_samples")) != K_API_AUDIO_CONTEXT_SAMPLES)) {
			invalid() ;
		}
		const auto &pcm = p.at( "pcm") ; // 同じ全PCM証拠
		r.plan.pcm.version = static_cast<std::uint32_t>( uintValue( pcm.at( "version"), 1)) ;
		r.plan.pcm.sampleCount = uintValue( pcm.at( "samples")) ;
		r.plan.pcm.sampleRate = static_cast<std::uint32_t>( uintValue( pcm.at( "rate"), K_SAMPLE_RATE)) ;
		r.plan.pcm.channels = static_cast<std::uint32_t>( uintValue( pcm.at( "channels"), 1)) ;
		r.plan.pcm.originSample = uintValue( pcm.at( "origin")) ;
		r.plan.pcm.pcmSha256 = pcm.at( "sha").get<ApiTextDigest>() ;
		r.plan.autoLanguage = p.at( "auto").get<bool>() ;
		r.plan.diarization = p.at( "diarization").get<bool>() ;
		r.plan.speechFilter = p.at( "vad").get<bool>() ;
		r.plan.sendSamples = uintValue( p.at( "send_samples")) ;
		r.plan.estimateMicroUsd = uintValue( p.at( "estimate")) ;
		const auto &price = p.at( "price") ; // duration単価専用
		// missingは保存planの有効料金として採用しません
		if( !price.at( "rate").is_null()) {
			r.plan.price.price.microPerMinute = uintValue( price.at( "rate"), 1'000'000'000) ;
		}
		r.plan.price.source = price.at( "source").get<std::string>() ;
		r.plan.price.parser = static_cast<std::uint32_t>( uintValue( price.at( "parser"), 1)) ;
		const auto &m = price.at( "meta") ; // 時計は新規作成せず過去のUTCだけ復元
		r.plan.price.metadata.fetched = m.at( "fetched").get<std::int64_t>() ;
		r.plan.price.metadata.attempted = m.at( "attempted").get<std::int64_t>() ;
		r.plan.price.metadata.http = static_cast<unsigned>( uintValue( m.at( "http"), 599)) ;
		r.plan.price.metadata.error = static_cast<std::uint32_t>( uintValue( m.at( "error"), UINT32_MAX)) ;
		r.plan.price.metadata.sha = m.at( "sha").get<ApiTextDigest>() ;
		// containerの型と件数を先に検証します
		if( !p.at( "windows").is_array() || p.at( "windows").size() > K_MAXIMUM_SEGMENTS || !j.at( "chunks").is_array() ||
		   j.at( "chunks").size() > K_MAXIMUM_SEGMENTS) {
			invalid() ;
		}
		// 非重複全窓を原順で復元します
		for( const auto &w : p.at( "windows")) {
			// 固定3fieldだけの窓を受理します
			if( !w.is_array() || w.size() != 3 || !w[2].is_array() || w[2].size() > 20) {
				invalid() ;
			}
			r.plan.windows.push_back( {uintValue( w[0]), uintValue( w[1]), w[2].get<std::vector<int>>()}) ;
		}
		// 完全応答prefixのtyped値だけ復元します
		for( const auto &c : j.at( "chunks")) {
			r.chunks.push_back( readChunk( c)) ;
		}
		// 旧recordは再開時以外はattemptへ変換しません
		if( r.version == 2) {
			const auto &values = j.at( "attempts") ; // 有限操作履歴
			if( !values.is_array() || values.empty() || values.size() > K_API_AUDIO_MAXIMUM_ATTEMPTS) {
				invalid() ;
			}
			for( const auto &a : values) {
				ApiAudioAttempt v ; // 本文を複製しない操作metadata
				v.operationId = uintValue( a.at( "operation")) ;
				v.keyRevision = uintValue( a.at( "key_revision")) ;
				v.price = readPrice( a.at( "price")) ;
				v.startIndex = static_cast<std::uint32_t>( uintValue( a.at( "start"), 50000)) ;
				v.completeEnd = static_cast<std::uint32_t>( uintValue( a.at( "end"), 50000)) ;
				v.terminal = static_cast<ApiAudioTerminal>( uintValue( a.at( "terminal"), 7)) ;
				v.error = static_cast<ErrorKind>( uintValue( a.at( "error"), 13)) ;
				v.code = static_cast<std::uint32_t>( uintValue( a.at( "code"), UINT32_MAX)) ;
				v.attemptedSamples = uintValue( a.at( "attempted")) ;
				if( !a.at( "intent").is_null()) {
					v.intent = static_cast<std::uint32_t>( uintValue( a.at( "intent"), 49999)) ;
				}
				if( !a.at( "failure").is_null()) {
					v.failure = readChunk( a.at( "failure")) ;
				}
				v.parentSourceSha = a.at( "parent_sha").get<ApiTextDigest>() ;
				v.endSourceSha = a.at( "end_sha").get<ApiTextDigest>() ;
				r.attempts.push_back( std::move( v)) ;
			}
		}
		validateApiAudioRecord( r, nullptr) ;
		return r ;
	} catch( const OperationError &) {
		throw ;
	} catch( ...) {
		invalid() ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 失敗・取消し・中断した音声API記録を、明示再開用の小さな照合tokenへ写します
 * @param[in] r sourceと照合済みで、完全応答prefixと試行数が妥当な記録の、呼出し中だけの借用です
 * @return FAILURE/CANCELLED/INTERRUPTED、saveFailedなし、plan窓あり、attemptsが64未満なら、
 * history/update/plan/source/prefixと課金不明flagを持つ所有token。それ以外はnulloptです
 * 全窓応答済みでも終端を保存するためのtokenを返せます
 * @note 課金不明flagは未確定intent、またはNOT_SENT以外の末尾不完全応答を示します
 * 記録全体の検証、PCM再照合、キー資格確認、保存CAS、通信、有料再開は行いません
 * @see koefude::apiAudioResumeToken
 */
//------------------------------------------------------------------------------
std::optional<ApiAudioResumeToken> apiAudioResumeToken( const ApiAudioRecord &r)
{
	// 全取得済みでも終端未保存ならPOST0の再開を許可します
	if( r.saveFailed ||
	   (r.terminal != ApiAudioTerminal::FAILURE && r.terminal != ApiAudioTerminal::CANCELLED &&
		r.terminal != ApiAudioTerminal::INTERRUPTED) ||
	   r.plan.windows.empty() || r.attempts.size() >= K_API_AUDIO_MAXIMUM_ATTEMPTS) {
		return std::nullopt ;
	}
	return ApiAudioResumeToken{r.historyId,
							   r.update,
							   r.plan.digest,
							   r.sourceSha,
							   apiAudioCompletePrefix( r),
							   r.intent.has_value() || (!r.chunks.empty() && !r.chunks.back().complete &&
														r.chunks.back().billing != ApiTextBilling::NOT_SENT)} ;
}
//------------------------------------------------------------------------------
/**
 * @brief 現操作の終端metadataを最後の試行へ写し、全試行の観測送信量を集計します
 * @param[in,out] r workerが単独で変更する記録。v2かつattempts非空の場合だけ、
 * 最後の試行のcompleteEnd・終端・診断・intent・任意末尾失敗・endSourceShaを更新します
 * 成功時は全試行のattemptedSamples合計もrecordへ反映し、過去試行のfieldは保持します
 * @throws OperationError 送信量合計がuint64_tを超える場合。分類はErrorKind::INVALID_INPUT、
 * codeはERROR_INVALID_DATAです
 * @note v1またはattempts空なら変更しません。末尾失敗のcopyによる動的確保例外も伝播します
 * 例外時も直前までの最新試行更新は残り得るためrollbackを保証しません。保存・通信は行いません
 * @see koefude::syncApiAudioAttempt
 */
//------------------------------------------------------------------------------
void syncApiAudioAttempt( ApiAudioRecord &r)
{
	// 旧recordは明示再開まで変換しません
	if( r.version != 2 || r.attempts.empty()) {
		return ;
	}
	auto &a = r.attempts.back() ; // 現opだけを更新し過去metadataを凍結
	a.completeEnd = apiAudioCompletePrefix( r) ;
	a.terminal = r.terminal ;
	a.error = r.error ;
	a.code = r.code ;
	a.intent = r.intent ;
	a.failure = !r.chunks.empty() && !r.chunks.back().complete ? std::optional( r.chunks.back()) : std::nullopt ;
	a.endSourceSha = r.sourceSha ;
	std::uint64_t total = 0 ; // 再送分も数える累積量
	// 各操作の観測量をoverflow検証して合計します
	for( const auto &attempt : r.attempts) {
		if( total > UINT64_MAX - attempt.attemptedSamples) {
			invalid() ;
		}
		total += attempt.attemptedSamples ;
	}
	r.attemptedSamples = total ;
}
//------------------------------------------------------------------------------
/**
 * @brief 完全応答を生成した操作を、保存済みattemptの成功範囲から検索します
 *
 * @param[in] r 検証済みApiAudioRecordの読取り専用借用です。所有権は取得しません
 * @param[in] index r.plan.windows/r.chunksに対応する0始まりの音声窓位置です
 * 単位は窓数で、sample位置・秒数・attempt配列の添字ではありません
 * 本文が空でも完全応答済みの窓は成功範囲に含まれます
 * @return startIndex <= index < completeEndを満たす最初のrecord.attempts要素への
 * 非所有const pointer。該当範囲がなければnullptrで、呼出し元は解放してはいけません
 * 検証済みv1はattemptsが空なのでnullptrです。未完窓/範囲外もnullptrになります
 * @pre validateApiAudioRecord等でrecordの版・非重複成功範囲を検証してあること
 * 本関数自身は版/範囲の整合性を検証せず、不正recordの拒否を保証しません
 * @post recordを変更せず、動的確保・I/O・例外送出を行いません
 * @note 返却先はrecord.attemptsが所有します。recordの破棄/代入、要素消去、vectorの再配置等で
 * 無効になります。recordと当該要素の寿命内だけ使用し、変更操作を越えて保持しません
 * 同じrecordを不変に保つ間は複数threadで読めます。変更と並行せず、必要な排他と
 * owner寿命の保持は呼出し元が担います
 * @see koefude::apiAudioGeneratingAttempt
 */
//------------------------------------------------------------------------------
const ApiAudioAttempt *apiAudioGeneratingAttempt( const ApiAudioRecord &r, std::uint32_t index) noexcept
{
	// 空区間attemptは生成本文を所有しません
	for( const auto &a : r.attempts) {
		if( index >= a.startIndex && index < a.completeEnd) {
			return &a ;
		}
	}
	return nullptr ;
}
//------------------------------------------------------------------------------
/**
 * @brief 旧記録を保持する所有copyから、次の明示再開をPENDINGとして準備します
 * @param[in] previous sourceとの完全整合を検証する旧記録。関数内では変更しません
 * @param[in] source previousの本文・sourceShaを照合する、呼出し中だけの原文の不変借用です
 * @param[in] operation previous.operationIdより大きい新操作IDです
 * @param[in] revision 呼出側が確認した現在キーの非0 revision。秘密値は受け取りません
 * @param[in] price 新試行へ固定する価格。元planと過去試行の価格は保持します
 * @return 完全応答prefixを保持し、末尾失敗とintentを除き、新試行を追加したv2/PENDINGの所有記録
 * 旧v1は過去一試行へ移し、未観測の初回parentSourceShaは空のまま保持します
 * @throws OperationError 旧record/source不整合、再開tokenなし、操作ID非増加、revisionが0、
 * または新記録の検証不成立の場合。previous/sourceは失敗時も変更しません
 * @note 所有copyの動的確保例外は伝播します。実キー資格・価格の現在資格・元PCM再decodeは
 * 呼出側の責務であり、本関数は保存・通信・再開を実行しません
 * @see koefude::prepareApiAudioResume
 */
//------------------------------------------------------------------------------
ApiAudioRecord prepareApiAudioResume( const ApiAudioRecord &previous, const Transcript &source, std::uint64_t operation,
									 std::uint64_t revision, const ApiAudioPriceSnapshot &price)
{
	validateApiAudioRecord( previous, &source) ;
	// 最大64操作で止め、古いopの再利用を拒否します
	if( !apiAudioResumeToken( previous) || operation <= previous.operationId || !revision) {
		invalid() ;
	}
	auto r = previous ; // worker所有、UIはcopyしません
	// v1の確定証拠だけを一つの過去attemptへ移します
	if( r.version == 1) {
		ApiAudioAttempt old ; // 初回parentSHAは未観測の空値を維持
		old.operationId = r.operationId ;
		old.keyRevision = r.keyRevision ;
		old.price = r.plan.price ;
		old.attemptedSamples = r.attemptedSamples ;
		r.attempts.push_back( std::move( old)) ;
		r.version = 2 ;
		syncApiAudioAttempt( r) ;
	}
	const auto prefix = apiAudioCompletePrefix( r) ; // 空textもskip
	r.chunks.resize( prefix) ;
	r.intent.reset() ;
	r.operationId = operation ;
	r.keyRevision = revision ;
	r.terminal = ApiAudioTerminal::PENDING ;
	r.saveFailed = false ;
	r.error = ErrorKind::INTERNAL ;
	r.code = 0 ;
	ApiAudioAttempt next ; // 今回の価格だけを新しい操作へ固定
	next.operationId = operation ;
	next.keyRevision = revision ;
	next.price = price ;
	next.startIndex = next.completeEnd = prefix ;
	next.parentSourceSha = next.endSourceSha = r.sourceSha ;
	r.attempts.push_back( std::move( next)) ;
	syncApiAudioAttempt( r) ;
	validateApiAudioRecord( r, &source) ;
	return r ;
}
//------------------------------------------------------------------------------
/**
 * @brief 既存の完全応答と過去試行を保つ音声API記録の更新候補かを比較します
 * @param[in] old 現在の耐久記録の、呼出し中だけの読取り借用です
 * @param[in] next 同じhistory/plan/updateから作った更新候補の、呼出し中だけの借用です
 * @param[in] resume trueは専用beginとして新操作・v2/PENDING・prefix/source・過去試行の保全を、
 * falseは同version/op/keyRevision内の単調更新を照合します
 * @return 同一target、完全prefix非減少、既存完全応答不変、および指定更新方式の操作/試行保存条件を
 * すべて満たす場合true。一つでも不成立ならfalseです。保存成功・API成功・記録単独の妥当性ではありません
 * @pre 両recordと対応sourceの妥当性は別の検証で確認すること
 * @note 入力を変更せず、update加算・lease取得・保存・通信は行いません
 * 過去試行のJSON比較は動的確保を伴い、その例外は呼出側へ伝播します
 * @see koefude::validApiAudioTransition
 */
//------------------------------------------------------------------------------
bool validApiAudioTransition( const ApiAudioRecord &old, const ApiAudioRecord &next, bool resume)
{
	// 同じplanと耐久updateだけが専用writerへ進めます
	if( old.historyId != next.historyId || old.plan.digest != next.plan.digest || old.update != next.update ||
	   apiAudioCompletePrefix( old) > apiAudioCompletePrefix( next)) {
		return false ;
	}
	// usage/language/modelも含めて同じ完全prefixを保持します
	for( std::size_t i = 0; i < apiAudioCompletePrefix( old); ++i) {
		if( old.chunks[i] != next.chunks[i]) {
			return false ;
		}
	}
	// 過去失敗の保全を確認した専用beginだけopを切り替えます
	if( resume) {
		if( !apiAudioResumeToken( old) || next.operationId <= old.operationId || next.version != 2 ||
		   next.terminal != ApiAudioTerminal::PENDING || next.intent ||
		   next.chunks.size() != apiAudioCompletePrefix( old) || next.sourceSha != old.sourceSha ||
		   next.attempts.size() != (old.version == 1 ? 2 : old.attempts.size() + 1) ||
		   next.attemptedSamples != old.attemptedSamples) {
			return false ;
		}
		// v2の過去操作は全field不変です
		if( old.version == 2) {
			for( std::size_t i = 0; i < old.attempts.size(); ++i) {
				if( attemptJson( old.attempts[i]) != attemptJson( next.attempts[i])) {
					return false ;
				}
			}
		} else {
			const auto &a = next.attempts.front() ; // v1移行証拠
			if( a.operationId != old.operationId || a.keyRevision != old.keyRevision ||
			   priceJson( a.price) != priceJson( old.plan.price) || a.startIndex ||
			   a.completeEnd != apiAudioCompletePrefix( old) || a.terminal != old.terminal || a.error != old.error ||
			   a.code != old.code || a.attemptedSamples != old.attemptedSamples || a.intent != old.intent ||
			   a.parentSourceSha != ApiTextDigest{} || a.endSourceSha != old.sourceSha ||
			   a.failure.has_value() != (!old.chunks.empty() && !old.chunks.back().complete) ||
			   (a.failure && *a.failure != old.chunks.back())) {
				return false ;
			}
		}
		return true ;
	}
	// 通常更新から別op/版へ書き換えません
	if( old.version != next.version || old.operationId != next.operationId || old.keyRevision != next.keyRevision ||
	   old.chunks.size() > next.chunks.size() || old.attempts.size() != next.attempts.size()) {
		return false ;
	}
	// 一度観測した失敗は同じop内で書き換えません
	if( !old.chunks.empty() && !old.chunks.back().complete && old.chunks.back() != next.chunks[old.chunks.size() - 1]) {
		return false ;
	}
	// 現操作の開始条件と過去操作の全fieldを照合します
	for( std::size_t i = 0; i < old.attempts.size(); ++i) {
		const auto &a = old.attempts[i] ; // 耐久操作
		const auto &b = next.attempts[i] ; // 候補操作
		if( i + 1 < old.attempts.size()) {
			if( attemptJson( a) != attemptJson( b)) {
				return false ;
			}
		} else if( a.operationId != b.operationId || a.keyRevision != b.keyRevision ||
				  priceJson( a.price) != priceJson( b.price) || a.startIndex != b.startIndex ||
				  a.parentSourceSha != b.parentSourceSha || a.attemptedSamples > b.attemptedSamples) {
			return false ;
		}
	}
	return true ;
}
//------------------------------------------------------------------------------
/**
 * @brief 完全応答を各窓へ一度だけ組み立てます
 * @param[in] r 同じplan/prefix
 * @param[in] turns 全fileの音響turn
 * @return 本文未分配の原文行
 * @see koefude::assembleApiAudioTranscript
 */
//------------------------------------------------------------------------------
std::vector<TranscriptLine> assembleApiAudioTranscript( const ApiAudioRecord &r, std::span<const SpeakerTurn> turns)
{
	std::vector<TextSegment> segments ; // 語時刻を持たない窓単位本文
	std::vector<std::size_t> indices ; // 非空本文とplan窓の一意対応
	// 空応答も保存prefixへ残し、表示行だけ省略します
	for( std::size_t i = 0; i < r.chunks.size(); ++i) {
		const auto &c = r.chunks[i] ; // 同じindex
		// 不完全本文を組立てません
		if( c.complete && !c.text.empty()) {
			const auto &w = r.plan.windows[i] ; // API語時刻ではない送信範囲
			segments.push_back(
				{static_cast<std::int64_t>( w.begin / 16), static_cast<std::int64_t>( (w.end + 15) / 16), c.text}) ;
			indices.push_back( i) ;
		}
	}
	auto lines =
		assembleTranscript( segments, turns,
						   static_cast<std::int64_t>( (r.plan.pcm.sampleCount + 15) / 16)) ; // v1の従来帰属を維持
	// 長窓の複数候補は短いturnも捨てず、全文を単一話者へ塗りません
	if( r.plan.version == 2) {
		std::vector<SpeakerTurn> sorted( turns.begin(), turns.end()) ; // 初出順番号の正本
		std::sort( sorted.begin(), sorted.end(),
				  //------------------------------------------------------------------------------
				  /**
				   * @brief 初出順を揃えます
				   * @param[in] a 左
				   * @param[in] b 右
				   * @return 原順
				   */
				  //------------------------------------------------------------------------------
				  []( const auto &a, const auto &b)
				  {
					  return a.startMs < b.startMs || (a.startMs == b.startMs && a.speaker < b.speaker) ;
				  }) ;
		std::map<int, int> ids ; // raw global clusterから初出番号
		// 認識本文の有無によらず全turnを使います
		for( const auto &t : sorted) {
			ids.try_emplace( t.speaker, static_cast<int>( ids.size()) + 1) ;
		}
		// 文字数や意味で推測分配せず一窓一行を保持します
		for( std::size_t i = 0; i < lines.size(); ++i) {
			const auto &candidates = r.plan.windows[indices[i]].speakers ; // 音響候補だけ
			// 複数clusterは全文対応未確定として中立表示します
			if( candidates.size() > 1) {
				lines[i].speakers.clear() ;
				for( const int id : candidates) {
					// 保存planの候補が別sourceへ移植されていれば固定診断で拒否します
					if( !ids.contains( id)) {
						invalid() ;
					}
					lines[i].speakers.push_back( ids.at( id)) ;
				}
				std::sort( lines[i].speakers.begin(), lines[i].speakers.end()) ;
			}
		}
		populateSpeakerAttribution( lines, turns) ; // 音響範囲だけ、語割当てなし
	}
	return lines ;
}
} // namespace koefude
