//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_text_store.cpp
 * @brief 有界API所有値とSHAをV7非変更で保存・復元します
 * @version 1.0.0
 * @date 2026/11/08
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/08 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/08 API sidecar schema1を追加
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1345の動的モデル・凍結単価と旧履歴互換を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1348の行メニュー直接実行と保存後表示を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1350のTerraと実在日付の共通応答照合を再読へ適用
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1351のAPI専用HTTP失敗・保存状態を接続
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1360のv3区切り再検証と旧v1/v2読取を追加
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
#include "platform/api_text_store.h"
#include "core/api_text_provenance.h"
#include "core/api_text_speaker_turns.h"
#include "platform/win_helpers.h"
#include <array>
#include <optional>
#include <span>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <json.hpp>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace koefude {
namespace {
using Json = nlohmann::json ; // 固定JSON版
//------------------------------------------------------------------------------
/**
 * @brief 不正なsidecarを原文から分離して拒否します
 */
//------------------------------------------------------------------------------
[[noreturn]] void corrupt()
{
	throw HistoryError( HistoryErrorKind::CORRUPT) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 整数のoptionalを保存します
 * @param[in] value presence値
 * @return nullまたは整数
 */
//------------------------------------------------------------------------------
Json optionalCount( const std::optional<std::uint64_t> &value)
{
	return value ? Json( *value) : Json( nullptr) ;
}
//------------------------------------------------------------------------------
/**
 * @brief usage値を読みます
 * @param[in] value 整数またはnull
 * @return presence整数
 */
//------------------------------------------------------------------------------
std::optional<std::uint64_t> readCount( const Json &value)
{
	// nullだけをunknownとして復元します
	if( value.is_null()) {
		return {} ;
	}
	// 負数やfloatを整数へ変換しません
	if( !value.is_number_unsigned() || value.get<std::uint64_t>() > 1000000000) {
		corrupt() ;
	}
	return value.get<std::uint64_t>() ;
}
//------------------------------------------------------------------------------
/**
 * @brief 試行をJSONへ変換します
 * @param[in] attempt 所有値、null可
 * @return 完全保存値
 */
//------------------------------------------------------------------------------
Json writeAttempt( const std::shared_ptr<const ApiTextAttempt> &attempt)
{
	// 未使用ならfile内にもnullだけを置きます
	if( !attempt) {
		return nullptr ;
	}
	const auto &a = *attempt ; // 同期borrow
	Json		chunks = Json::array() ; // 原文plan
	// 最大32個のplanを保持します
	for( const auto &c : a.draft.chunks) {
		chunks.push_back( {c.begin, c.end, c.before, c.after, c.estimate}) ;
	}
	Json results = Json::array() ; // 部分応答も元順
	// 最大32個の終端を保存します
	for( const auto &r : a.results) {
		results.push_back( {{"complete", r.complete},
						   {"text", r.text},
						   {"id", r.responseId},
						   {"model", r.model},
						   {"service_tier", r.serviceTier ? Json( *r.serviceTier) : Json( nullptr)},
						   {"price_applicable", r.priceApplicable},
						   {"billing", r.billing},
						   {"http", r.httpStatus},
						   {"http_error", r.httpError},
						   {"code", r.code},
						   {"usage",
							{optionalCount( r.usage.input), optionalCount( r.usage.output), optionalCount( r.usage.cached),
							 optionalCount( r.usage.reasoning), optionalCount( r.usage.cacheWrite)}}}) ;
	}
	return {{"profile_snapshot", a.draft.catalogSnapshot.empty() ? apiTextProfileSnapshot() : a.draft.catalogSnapshot},
			{"profile_version", a.draft.catalogSnapshot.empty()					 ? 1
								: apiTextSpeakerProfile( a.draft.catalogSnapshot) ? 3
																				 : 2},
			{"op", a.operationId},
			{"source_id", a.draft.source.historyId},
			{"source_sha", a.draft.source.sha256},
			{"projection", a.draft.projection},
			{"budget", a.draft.budget},
			{"estimate", a.draft.estimate},
			{"draft_sha", a.draft.digest},
			{"plan", chunks},
			{"consent", a.consent.id},
			{"confirmed", a.consent.confirmed},
			{"intent", a.consent.intent},
			{"consent_sha", a.consent.draft},
			{"state", a.state},
			{"results", results},
			{"candidate", a.candidate},
			{"candidate_sha", a.candidateSha},
			{"code", a.code}} ;
}
//------------------------------------------------------------------------------
/**
 * @brief 試行を厳密に復元します
 * @param[in] j 保存JSON
 * @param[in] source V7原文
 * @param[in] key 原文key
 * @return 同sourceの所有値
 */
//------------------------------------------------------------------------------
std::shared_ptr<const ApiTextAttempt> readAttempt( const Json &j, const Transcript &source, const ReadingSourceKey &key)
{
	// API未使用の履歴を空値で保持します
	if( j.is_null()) {
		return {} ;
	}
	auto	   a = std::make_shared<ApiTextAttempt>() ; // 全検証後にだけ公開
	// 指示・モデル・料金の保存snapshotも固定profileと一致を要求します
	const auto version = j.value( "profile_version", 1) ; // schema1はlegacyのみ
	const auto profile = j.at( "profile_snapshot").get<std::string>() ; // 元文字列を保持
	if( (version != 1 && version != 2 && version != 3) || (version == 1 && profile != apiTextProfileSnapshot())) {
		corrupt() ;
	}
	a->operationId = j.at( "op").get<std::uint64_t>() ;
	a->draft =
		makeApiTextDraft( source, key, j.at( "budget").get<std::uint64_t>(), version >= 2 ? profile : std::string{}) ;
	// 保存versionとprofileの組を厳密に保ち、v2をv3へ自動移行しません
	if( version >= 2 && apiTextSpeakerProfile( profile) != (version == 3)) {
		corrupt() ;
	}
	// 保存されたsourceと投影/設定の改変を拒否します
	if( a->operationId == 0 || j.at( "source_id").get<std::uint64_t>() != key.historyId ||
	   j.at( "source_sha").get<ApiTextDigest>() != key.sha256 ||
	   j.at( "projection").get<std::string>() != a->draft.projection ||
	   j.at( "draft_sha").get<ApiTextDigest>() != a->draft.digest ||
	   j.at( "estimate").get<std::uint64_t>() != a->draft.estimate || j.at( "plan").size() != a->draft.chunks.size()) {
		corrupt() ;
	}
	// 全plan位置を照合し、source長やUTF-8境界を別解釈しません
	for( std::size_t i = 0; i < a->draft.chunks.size(); ++i) { // plan位置
		const auto &c = a->draft.chunks[i] ; // 再計算した正本
		// 保存位置と完全一致を要求します
		if( j.at( "plan").at( i) != Json( {c.begin, c.end, c.before, c.after, c.estimate})) {
			corrupt() ;
		}
	}
	a->consent.id = j.at( "consent").get<std::array<std::uint8_t, 16>>() ;
	a->consent.draft = j.at( "consent_sha").get<ApiTextDigest>() ;
	a->consent.confirmed = j.at( "confirmed").get<bool>() ;
	a->consent.intent = j.value( "intent", ApiTextIntent::PREVIEW_BUTTON) ;
	a->state = j.at( "state").get<ApiTextState>() ;
	a->candidate = j.at( "candidate").get<std::string>() ;
	a->candidateSha = j.at( "candidate_sha").get<ApiTextDigest>() ;
	a->code = j.at( "code").get<std::uint32_t>() ;
	// enum・同意・候補容量・SHAを検証します
	if( a->state < ApiTextState::PENDING || a->state > ApiTextState::INTERRUPTED ||
	   a->consent.intent < ApiTextIntent::PREVIEW_BUTTON || a->consent.intent > ApiTextIntent::ROW_MENU ||
	   !a->consent.confirmed || a->consent.draft != a->draft.digest ||
	   a->candidate.size() > K_API_TEXT_CANDIDATE_LIMIT ||
	   (!a->candidate.empty() && a->candidateSha != apiTextDigest( a->candidate)) ||
	   j.at( "results").size() > a->draft.chunks.size()) {
		corrupt() ;
	}
	static_cast<void>( toWide( a->candidate)) ;
	std::size_t responseBytes = 0 ; // 新v3の区切りも含む保存応答の有限合計
	// 既知usage/部分候補を省略せず復元します
	for( const auto &r : j.at( "results")) {
		ApiTextChunkResult out ; // 一chunkの保存事実
		out.complete = r.at( "complete").get<bool>() ;
		out.text = r.at( "text").get<std::string>() ;
		out.responseId = r.at( "id").get<std::string>() ;
		out.model = r.at( "model").get<std::string>() ;
		// legacyの省略tierをdefault指定済みへ変えません
		if( r.contains( "service_tier") && !r.at( "service_tier").is_null()) {
			out.serviceTier = r.at( "service_tier").get<std::string>() ;
		}
		out.priceApplicable = r.value( "price_applicable", true) ;
		// 有界な実tier以外を履歴表示へ持ち込みません
		if( out.serviceTier &&
		   (out.serviceTier->empty() || out.serviceTier->size() > 64 ||
			out.serviceTier->find_first_not_of( "abcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos)) {
			corrupt() ;
		}
		out.billing = r.at( "billing").get<ApiTextBilling>() ;
		out.httpStatus = r.at( "http").get<unsigned>() ;
		out.code = r.at( "code").get<std::uint32_t>() ;
		// 旧schema1/2の省略はUnknownとし、数値以外と範囲外は拒否します
		if( r.contains( "http_error")) {
			const auto &value = r.at( "http_error") ; // 外部値を変換前に検査
			// 符号付き負数・小数・文字列・過大整数を通しません
			if( !value.is_number_unsigned() ||
			   value.get<std::uint64_t>() > static_cast<unsigned>( ApiTextHttpError::SERVICE_OVERLOADED)) {
				corrupt() ;
			}
			out.httpError = static_cast<ApiTextHttpError>( value.get<std::uint32_t>()) ;
		}
		const auto &u = r.at( "usage") ; // 固定5field
		// 外部保存値の容量と分類を検証します
		if( u.size() != 5 || out.text.size() > K_API_TEXT_CANDIDATE_LIMIT - responseBytes ||
		   out.responseId.size() > 256 || out.model.size() > 128 || out.billing < ApiTextBilling::NOT_SENT ||
		   out.billing > ApiTextBilling::USAGE_MISSING) {
			corrupt() ;
		}
		responseBytes += out.text.size() ;
		static_cast<void>( toWide( out.text)) ;
		out.usage = {readCount( u[0]), readCount( u[1]), readCount( u[2]), readCount( u[3]), readCount( u[4])} ;
		// v2のscopeとReadyは保存boolを信用せず凍結snapshotから再計算します
		if( version >= 2) {
			const auto applicable = apiProvenanceMatches( apiCatalogSnapshotModel( a->draft.catalogSnapshot),
														 out.model) &&
									(!out.serviceTier || *out.serviceTier == "default") ; // presenceを維持
			// 不一致をReadyへ昇格した外部保存値は拒否します
			if( out.priceApplicable != applicable ||
			   (out.complete && (!applicable || !apiTextUsageCost( out.usage, a->draft.catalogSnapshot)))) {
				corrupt() ;
			}
		}
		a->results.push_back( std::move( out)) ;
	}
	// 完全候補の整合性と元順の連結本文を再検証します
	const auto combined = apiTextCombinedCandidate( *a) ; // v3は発言IDと本文を再検証
	// candidateとchunk本文の不一致・Readyの未完を拒否します
	if( combined != a->candidate || (a->state == ApiTextState::CANDIDATE_READY &&
									(a->candidate.empty() || a->results.size() != a->draft.chunks.size() ||
									 std::any_of( a->results.begin(), a->results.end(),
		//------------------------------------------------------------------------------
		/**
		 * @brief 未完chunkを検出します
		 * @param[in] r 応答
		 * @return 未完
		 */
		//------------------------------------------------------------------------------
		[]( const auto &r)
		{
			return !r.complete ;
		})))) {
		corrupt() ;
	}
	// 再起動ではPending/Sendingを再開せず課金不明の中断にします
	if( a->state == ApiTextState::PENDING || a->state == ApiTextState::SENDING) {
		a->state = ApiTextState::INTERRUPTED ;
	}
	return a ;
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief runtime API値だけを符号化します
 * @param[in] snapshot 正本
 * @return digest付きschema2
 * @see koefude::encodeApiTextStore
 */
//------------------------------------------------------------------------------
std::vector<std::uint8_t> encodeApiTextStore( const HistorySnapshot &snapshot)
{
	Json entries = Json::array() ; // API使用履歴だけを収録
	// 件数はV7と同じ最大32件です
	for( const auto &e : snapshot.entries) {
		// API未使用値を新規保存しません
		if( !e.apiText.latest && !e.apiText.adopted) {
			continue ;
		}
		entries.push_back( {{"id", e.id},
						   {"source", fingerprintReadingSource( *e.result)},
						   {"latest", writeAttempt( e.apiText.latest)},
						   {"adopted", writeAttempt( e.apiText.adopted)},
						   {"update", e.apiText.update},
						   {"original", e.apiText.showOriginal}}) ;
	}
	const auto body = Json{
		{"domain", "koefude/api-text-corrections"},
		{"schema", 2},
		{"floor", snapshot.nextOperationId},
		{"entries",
		 entries}}.dump() ; // key/path/PCMなし
	// reserve時は最大候補追加分を事前に確保可能な範囲へ制限します
	if( body.size() > K_API_TEXT_STORE_LIMIT - 32) {
		throw HistoryError( HistoryErrorKind::TOO_LARGE) ;
	}
	std::vector<std::uint8_t> bytes( body.begin(), body.end()) ; // 有界なbody
	const auto				  sha = apiTextDigest( body) ; // 全体digest
	bytes.insert( bytes.end(), sha.begin(), sha.end()) ;
	return bytes ;
}
//------------------------------------------------------------------------------
/**
 * @brief 同じ全source keyにだけ結合します
 * @param[in] bytes 検証前byte
 * @param[in,out] snapshot V7読取済み値
 * @throws HistoryError 不正形式、部分適用なし
 * @see koefude::decodeApiTextStore
 */
//------------------------------------------------------------------------------
void decodeApiTextStore( std::span<const std::uint8_t> bytes, HistorySnapshot &snapshot)
{
	try {
		// 符号化前と同じ全体容量とdigestを検証します
		if( bytes.size() < 34 || bytes.size() > K_API_TEXT_STORE_LIMIT) {
			corrupt() ;
		}
		const std::string_view body( reinterpret_cast<const char *>( bytes.data()),
									bytes.size() - 32) ; // 呼出し中borrow
		const auto			   sha = apiTextDigest( body) ; // 外部内容の再計算値
		// 不正sidecarの一部も原文へ結合しません
		if( !std::equal( sha.begin(), sha.end(), bytes.end() - 32)) {
			corrupt() ;
		}
		std::vector<std::set<std::string>> keys ; // object keyの重複検証
		std::size_t						   events = 0 ; // 有界parseイベント
		const auto						   root = Json::parse( body,
			//------------------------------------------------------------------------------
			/**
			 * @brief JSONのparse event数/深さとobjectごとのkey重複を検査し、上限内の全eventを採用します
			 * @param[in] depth parserが通知する構造の深さ。32を超えるeventは拒否します
			 * @param[in] event object開始/終了、key、値等のparse event。objectごとに独立したkey集合を積み戻します
			 * @param[in] value 同期callback中だけ借用するJSON値。key eventで文字列として読み、内容は変更しません
			 * @return 正常時は常にtrueでeventを採用します。schema妥当性や保存成功を表しません
			 * @throws HistoryError CORRUPTとして、深さ超過、全event数1000000超過、またはkey集合なし/重複keyのとき
			 * @note 捕捉したkeys/eventsは同期parse中だけ更新します。動的確保やJSON型照会の例外も呼出側へ伝わります
			 */
			//------------------------------------------------------------------------------
			[&keys, &events]( int depth, Json::parse_event_t event, Json &value)
			{
				// 全履歴でも有限のevent数です
				if( depth > 32 || ++events > 1000000) {
					corrupt() ;
				}
				// 独立objectのkey集合を積みます
				if( event == Json::parse_event_t::object_start) {
					keys.emplace_back() ;
				}
				// 重複keyの上書きを禁止します
				if( event == Json::parse_event_t::key &&
				   (keys.empty() || !keys.back().insert( value.get<std::string>()).second)) {
					corrupt() ;
				}
				// 終端で集合を戻します
				if( event == Json::parse_event_t::object_end) {
					keys.pop_back() ;
				}
				return true ;
			}) ;
		// schemaとfloorをV7とは独立に検証します
		if( root.at( "domain") != "koefude/api-text-corrections" || (root.at( "schema") != 1 && root.at( "schema") != 2) ||
		   !root.at( "floor").is_number_unsigned() || root.at( "floor").get<std::uint64_t>() == 0 ||
		   !root.at( "entries").is_array() || root.at( "entries").size() > K_MAXIMUM_HISTORY) {
			corrupt() ;
		}
		auto					candidate = snapshot ; // 全件成功後にだけ原子的に適用
		std::set<std::uint64_t> ids ; // ID重複を禁止
		// 全保存entryをV7の原文keyへ照合します
		for( const auto &item : root.at( "entries")) {
			const auto id = item.at( "id").get<std::uint64_t>() ; // 同じ非再利用履歴ID
			// 重複IDを複数適用しません
			if( id == 0 || !ids.insert( id).second) {
				corrupt() ;
			}
			const auto entry = std::find_if( candidate.entries.begin(), candidate.entries.end(),
				//------------------------------------------------------------------------------
				/**
				 * @brief 原文IDを探します
				 * @param[in] e 記録
				 * @return 同ID
				 */
				//------------------------------------------------------------------------------
				[id]( const auto &e)
				{
					return e.id == id ;
				}) ; // 最大32件
			// 旧版の削除・原文変更後はstale sidecarを結合しません
			if( entry == candidate.entries.end() || !entry->result ||
			   item.at( "source").get<ApiTextDigest>() != fingerprintReadingSource( *entry->result)) {
				continue ;
			}
			const ReadingSourceKey key{id, fingerprintReadingSource( *entry->result)} ; // 全source key
			entry->apiText.latest = readAttempt( item.at( "latest"), *entry->result, key) ;
			entry->apiText.adopted = readAttempt( item.at( "adopted"), *entry->result, key) ;
			entry->apiText.update = item.at( "update").get<std::uint64_t>() ;
			entry->apiText.showOriginal = item.at( "original").get<bool>() ;
			// 採用済みは完全候補だけ、floorは全opより後です
			if( (entry->apiText.adopted && entry->apiText.adopted->state != ApiTextState::CANDIDATE_READY) ||
			   (entry->apiText.latest && entry->apiText.latest->operationId >= root.at( "floor").get<std::uint64_t>()) ||
			   (entry->apiText.adopted &&
				entry->apiText.adopted->operationId >= root.at( "floor").get<std::uint64_t>())) {
				corrupt() ;
			}
		}
		candidate.nextOperationId = std::max( candidate.nextOperationId, root.at( "floor").get<std::uint64_t>()) ;
		snapshot = std::move( candidate) ;
	} catch( const HistoryError &) {
		throw ;
	} catch( ...) {
		corrupt() ;
	}
}
} // namespace koefude
