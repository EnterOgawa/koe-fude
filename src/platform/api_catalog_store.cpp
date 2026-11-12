//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_catalog_store.cpp
 * @brief API公開metadataだけを保存し再起動資格を失効して復元します
 * @version 1.0.0
 * @date 2026/11/08
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/08 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/08 schema1 cacheを追加
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1347の音声テーブル・独立API設定と文章操作を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1349の一回適用と全単価表示へ更新
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
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
#include "platform/api_catalog_store.h"
#include "core/api_text_correction.h"
#include "core/history.h"
#include <span>
#include <utility>
#include <algorithm>
#include <json.hpp>
#include <set>
namespace koefude {
namespace {
using Json = nlohmann::json ; // 固定JSON版
//------------------------------------------------------------------------------
/**
 * @brief 固定metadataだけを保存します
 * @param[in] m 成功・試行値
 * @return 保存JSON
 */
//------------------------------------------------------------------------------
Json metaJson( const ApiCatalogMetadata &m)
{
	return Json{
		{"fetched", m.fetched}, {"attempted", m.attempted}, {"http", m.http}, {"error", m.error}, {"sha", m.sha}} ;
}
//------------------------------------------------------------------------------
/**
 * @brief 保存metadataの型と範囲を確認します
 * @param[in] j 外部値
 * @return monotonicなしの値
 */
//------------------------------------------------------------------------------
ApiCatalogMetadata readMeta( const Json &j)
{
	ApiCatalogMetadata m ; // process clockは復元しません
	m.fetched = j.at( "fetched").get<std::int64_t>() ;
	m.attempted = j.at( "attempted").get<std::int64_t>() ;
	m.http = j.at( "http").get<unsigned>() ;
	m.error = j.at( "error").get<std::uint32_t>() ;
	m.sha = j.at( "sha").get<ApiTextDigest>() ;
	// 未来時刻は表示可能、鮮度で拒否します。負数や異常statusは破損です
	if( m.fetched < 0 || m.attempted < 0 || m.http > 599) {
		throw HistoryError( HistoryErrorKind::CORRUPT) ;
	}
	return m ;
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 表示cacheだけを符号化します
 * @param[in] c 検証済み正本
 * @return schema1/SHA付きbyte
 * @see koefude::encodeApiCatalog
 */
//------------------------------------------------------------------------------
std::vector<std::uint8_t> encodeApiCatalog( const ApiModelCatalog &c)
{
	Json models = Json::array() ; // 所有者名は保持しません
	// 全表示IDと終了日だけ保存します
	for( const auto &[id, info] : c.models) {
		models.push_back( {{"id", id},
						  {"object", "model"},
						  {"created", 0},
						  {"shutdown_date", info.shutdown.empty() ? Json( nullptr) : Json( info.shutdown)}}) ;
	}
	Json prices = Json::object() ; // 全decimalのpresence
	// 各モデルのshort/longを区別します
	for( const auto &[id, price] : c.prices) {
		Json values = Json::array() ; // 固定10セル
		// nullと明示ゼロを維持します
		for( const auto &rate : price.rates) {
			values.push_back( rate ? Json( *rate) : Json( nullptr)) ;
		}
		prices[id] = std::move( values) ;
	}
	const auto body =
		Json{{"domain", "koefude/api-catalog"},
			 {"schema", 2},
			 {"parser", 1},
			 {"audio_rate", c.audioPrice.microPerMinute ? Json( *c.audioPrice.microPerMinute) : Json( nullptr)},
			 {"audio_meta", metaJson( c.audioPriceMeta)},
			 {"model_url", "https://api.openai.com/v1/models"},
			 {"price_url", "https://developers.openai.com/api/docs/pricing.md"},
			 {"models", {{"object", "list"}, {"data", models}}},
			 {"prices", prices},
			 {"model_meta", metaJson( c.modelMeta)},
			 {"price_meta", metaJson( c.priceMeta)}}
			.dump() ; // key/body/pathなし
	// 件数とbyte上限を破るcacheは書きません
	if( c.models.size() > 4096 || c.prices.size() > 1024 || body.size() > K_API_CATALOG_STORE_LIMIT - 32) {
		throw HistoryError( HistoryErrorKind::TOO_LARGE) ;
	}
	std::vector<std::uint8_t> result( body.begin(), body.end()) ; // 全体body
	const auto				  sha = apiTextDigest( body) ; // 破損検出用SHA
	result.insert( result.end(), sha.begin(), sha.end()) ;
	return result ;
}
//------------------------------------------------------------------------------
/**
 * @brief cacheを資格未確認として復元します
 * @param[in] bytes 有界byte
 * @return 検証済み表示値
 * @throws HistoryError 不正・未知形式
 * @see koefude::decodeApiCatalog
 */
//------------------------------------------------------------------------------
ApiModelCatalog decodeApiCatalog( std::span<const std::uint8_t> bytes)
{
	try {
		// parse前に固定容量とSHAを検証します
		if( bytes.size() < 34 || bytes.size() > K_API_CATALOG_STORE_LIMIT) {
			throw HistoryError( HistoryErrorKind::CORRUPT) ;
		}
		const std::string_view body( reinterpret_cast<const char *>( bytes.data()),
									bytes.size() - 32) ; // 同期borrow
		const auto			   sha = apiTextDigest( body) ; // 固定SHA
		// 部分破損を一部採用しません
		if( !std::equal( sha.begin(), sha.end(), bytes.end() - 32)) {
			throw HistoryError( HistoryErrorKind::CORRUPT) ;
		}
		std::vector<std::set<std::string>> keys ; // 重複key禁止
		unsigned						   events = 0 ; // parse上限
		const auto						   j = Json::parse( body,
			//------------------------------------------------------------------------------
			/**
			 * @brief JSONのparse event数/深さとobjectごとのkey重複を検査し、上限内の全eventを採用します
			 * @param[in] depth parserが通知する構造の深さ。16を超えるeventは拒否します
			 * @param[in] event object開始/終了、key、値等のparse event。objectごとに独立したkey集合を積み戻します
			 * @param[in] value 同期callback中だけ借用するJSON値。key eventで文字列として読み、内容は変更しません
			 * @return 正常時は常にtrueでeventを採用します。schema妥当性や保存成功を表しません
			 * @throws HistoryError CORRUPTとして、深さ超過、全event数150000超過、またはkey集合なし/重複keyのとき
			 * @note 捕捉したkeys/eventsは同期parse中だけ更新します。動的確保やJSON型照会の例外も呼出側へ伝わります
			 */
			//------------------------------------------------------------------------------
			[&keys, &events]( int depth, Json::parse_event_t event, Json &value)
			{
				// 件数とは別に未知fieldの資源も制限します
				if( depth > 16 || ++events > 150000) {
					throw HistoryError( HistoryErrorKind::CORRUPT) ;
				}
				// objectの独立集合を開始します
				if( event == Json::parse_event_t::object_start) {
					keys.emplace_back() ;
				}
				// 同keyの上書きを拒否します
				if( event == Json::parse_event_t::key &&
				   (keys.empty() || !keys.back().insert( value.get<std::string>()).second)) {
					throw HistoryError( HistoryErrorKind::CORRUPT) ;
				}
				// objectの集合を終了します
				if( event == Json::parse_event_t::object_end) {
					keys.pop_back() ;
				}
				return true ;
			}) ;
		// 未知版や別domainを表示cacheへ結合しません
		if( j.at( "domain") != "koefude/api-catalog" || (j.at( "schema") != 1 && j.at( "schema") != 2) ||
		   j.at( "parser") != 1 || j.at( "model_url") != "https://api.openai.com/v1/models" ||
		   j.at( "price_url") != "https://developers.openai.com/api/docs/pricing.md" || !j.at( "prices").is_object() ||
		   j.at( "prices").size() > 1024) {
			throw HistoryError( HistoryErrorKind::CORRUPT) ;
		}
		ApiModelCatalog result ; // 現キー成功revisionは0のまま
		result.models = parseApiModels( j.at( "models").dump()) ;
		result.modelMeta = readMeta( j.at( "model_meta")) ;
		result.priceMeta = readMeta( j.at( "price_meta")) ;
		// schema1は音声未取得のまま復元し、旧text単価へ触れません
		if( j.at( "schema") == 2) {
			result.audioPriceMeta = readMeta( j.at( "audio_meta")) ;
			// nullと明示ゼロを維持し、負数/小数/過大値を拒否します
			if( !j.at( "audio_rate").is_null()) {
				if( !j.at( "audio_rate").is_number_unsigned() ||
				   j.at( "audio_rate").get<std::uint64_t>() > 1'000'000'000) {
					throw HistoryError( HistoryErrorKind::CORRUPT) ;
				}
				result.audioPrice.microPerMinute = j.at( "audio_rate").get<std::uint64_t>() ;
			}
		}
		// IDはModels parserの文法を再利用し、任意文字列をUIへ混ぜません
		for( const auto &[id, rates] : j.at( "prices").items()) {
			static_cast<void>( parseApiModels(
				Json{{"object", "list"}, {"data", Json::array( {{{"object", "model"}, {"id", id}, {"created", 0}}})}}
					.dump())) ;
			// 表の固定列数を検証します
			if( !rates.is_array() || rates.size() != 10) {
				throw HistoryError( HistoryErrorKind::CORRUPT) ;
			}
			ApiModelPrice price ; // presenceを維持
			// 各料金の型と上限を確認します
			for( std::size_t i = 0; i < 10; ++i) {
				// missingはnullのままです
				if( rates[i].is_null()) {
					continue ;
				}
				// scaled integerだけを受理します
				if( !rates[i].is_number_unsigned() || rates[i].get<std::uint64_t>() > 1000000000000ULL) {
					throw HistoryError( HistoryErrorKind::CORRUPT) ;
				}
				price.rates[i] = rates[i].get<std::uint64_t>() ;
			}
			// 必須二列の欠落は破損です
			if( !price.rates[0] || !price.rates[1]) {
				throw HistoryError( HistoryErrorKind::CORRUPT) ;
			}
			result.prices.emplace( id, price) ;
		}
		return result ;
	} catch( const HistoryError &) {
		throw ;
	} catch( ...) {
		throw HistoryError( HistoryErrorKind::CORRUPT) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 一般設定を有界に符号化します
 * @param[in] settings IDと整数予算だけ
 * @return 256byte以内のSHA付き設定
 * @throws HistoryError 不正値
 * @see koefude::encodeApiSettings
 */
//------------------------------------------------------------------------------
std::vector<std::uint8_t> encodeApiSettings( const ApiGeneralSettings &settings)
{
	// キー・任意自由文を許可せず、Modelsと同じID文法を使います
	if( settings.model.empty() || settings.model.size() > 128 || settings.budget < 10000 ||
	   (settings.budget > 200000000 && settings.budget != K_API_NO_BUDGET_LIMIT) ||
	   settings.model.find_first_not_of( "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._") !=
		   std::string::npos) {
		throw HistoryError( HistoryErrorKind::CORRUPT) ;
	}
	const auto body =
		Json{{"schema", 1}, {"model", settings.model}, {"budget", settings.budget}}.dump() ; // 固定3fieldだけ
	std::vector<std::uint8_t> bytes( body.begin(), body.end()) ; // 有界metadata
	const auto				  digest = apiTextDigest( body) ; // 破損検出
	bytes.insert( bytes.end(), digest.begin(), digest.end()) ;
	return bytes ;
}
//------------------------------------------------------------------------------
/**
 * @brief 一般設定を復元します
 * @param[in] bytes 256byte以内
 * @return 検証済み設定
 * @throws HistoryError 不正/未知形式、部分採用なし
 * @see koefude::decodeApiSettings
 */
//------------------------------------------------------------------------------
ApiGeneralSettings decodeApiSettings( std::span<const std::uint8_t> bytes)
{
	try {
		// allocation/parse前に設定専用の小さい上限を確認します
		if( bytes.size() < 34 || bytes.size() > 256) {
			throw HistoryError( HistoryErrorKind::CORRUPT) ;
		}
		const std::string_view body( reinterpret_cast<const char *>( bytes.data()),
									bytes.size() - 32) ; // 同期borrow
		const auto			   digest = apiTextDigest( body) ; // 内容照合
		// digest不一致を既定値へ黙って縮小しません
		if( !std::equal( digest.begin(), digest.end(), bytes.end() - 32)) {
			throw HistoryError( HistoryErrorKind::CORRUPT) ;
		}
		const auto		   value = Json::parse( body) ; // canonical形式だけを再符号化照合
		ApiGeneralSettings settings ; // 正常時だけ返す値
		settings.model = value.at( "model").get<std::string>() ;
		settings.budget = value.at( "budget").get<std::uint64_t>() ;
		const auto canonical = encodeApiSettings( settings) ; // 未知field/重複keyも拒否
		// schemaを含む正規形が一致する時だけ採用します
		if( value.at( "schema") != 1 || canonical.size() != bytes.size() ||
		   !std::equal( canonical.begin(), canonical.end(), bytes.begin())) {
			throw HistoryError( HistoryErrorKind::CORRUPT) ;
		}
		return settings ;
	} catch( ...) {
		throw HistoryError( HistoryErrorKind::CORRUPT) ;
	}
}
} // namespace koefude
