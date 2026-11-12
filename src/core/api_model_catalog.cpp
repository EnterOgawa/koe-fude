//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_model_catalog.cpp
 * @brief 有界metadata解析と凍結料金snapshotを実装します
 * @version 1.0.0
 * @date 2026/11/08
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/08 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1345の動的取得・選択条件を追加
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1347の音声テーブル・独立API設定と文章操作を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1348の行メニュー直接実行と保存後表示を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1349の一回適用と全単価表示へ更新
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1350の厳密Terra profileを接続
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1360の新規話者保持v3と凍結v2読取を分岐
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1402の取得前候補と公式価格のsection境界へ対応
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
#include "core/api_model_catalog.h"
#include "core/api_text_correction.h"
#include "core/api_text_speaker_turns.h"
#include "platform/api_catalog_store.h"
#include "platform/win_helpers.h"
#include <optional>
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <iterator>
#include <json.hpp>
#include <limits>
#include <set>
#include <utility>
#include <windows.h>
namespace koefude {
namespace {
using Json = nlohmann::json ; // 固定JSON版
constexpr std::string_view K_PRICE_URL = "https://developers.openai.com/api/docs/pricing.md" ; // 固定公開先
constexpr std::string_view K_MODELS_URL = "https://api.openai.com/v1/models" ; // 固定認証先
constexpr std::array<std::string_view, 4> K_SUPPORTED_MODELS{"gpt-6.1-sol", "gpt-6-astra", "gpt-6-luna",
															 "gpt-6.1-terra"} ; // 既存の厳密送信profile
constexpr std::string_view				  K_HEADER =
	"| Model | Short context input | Short context output | Short context cached input | Short "
	"context 30m write | Short context 12h write | Long context input | Long context output | Long "
	"context cached input | Long context 30m write | Long context 12h write |" ; // 順序固定
constexpr std::string_view K_DELIMITER =
	"| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |" ; // 列数固定
//------------------------------------------------------------------------------
/**
 * @brief 推測補完せず不正入力を拒否します
 */
//------------------------------------------------------------------------------
[[noreturn]] void invalid()
{
	throw OperationError( ErrorKind::INVALID_INPUT, ERROR_INVALID_DATA) ;
}
//------------------------------------------------------------------------------
/**
 * @brief ID文法を検証します
 * @param[in] id 元ASCII値
 * @return 許容ID
 */
//------------------------------------------------------------------------------
bool validId( std::string_view id)
{
	return !id.empty() && id.size() <= 128 &&
		   std::all_of( id.begin(), id.end(),
					   //------------------------------------------------------------------------------
					   /**
					    * @brief ASCII ID文字だけ許可します
					    * @param[in] c byte
					    * @return 許可
					    */
					   //------------------------------------------------------------------------------
					   []( unsigned char c)
					   {
						   return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
								  c == '.' || c == '_' || c == ':' || c == '-' ;
					   }) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 日付の実在を検証します
 * @param[in] date YYYY-MM-DD
 * @return 正当な日付
 */
//------------------------------------------------------------------------------
bool validDate( std::string_view date)
{
	// 無通知は別状態、ここでは暦日だけを受理します
	if( date.size() != 10 || date[4] != '-' || date[7] != '-') {
		return false ;
	}
	int					   values[3]{} ; // 年月日を有界解析
	const std::string_view parts[]{date.substr( 0, 4), date.substr( 5, 2), date.substr( 8, 2)} ; // 固定3区間
	// 全区間を最後まで10進整数として読みます
	for( unsigned i = 0; i < 3; ++i) { // 年月日位置
		const auto result = std::from_chars( parts[i].data(), parts[i].data() + parts[i].size(),
											values[i]) ; // 実解析
		// 符号や部分解析を暦日へ変換しません
		if( result.ec != std::errc{} || result.ptr != parts[i].data() + parts[i].size() ||
		   parts[i].find_first_not_of( "0123456789") != std::string_view::npos) {
			return false ;
		}
	}
	return values[0] >= 1970 && std::chrono::year_month_day( std::chrono::year( values[0]),
															std::chrono::month( static_cast<unsigned>( values[1])),
															std::chrono::day( static_cast<unsigned>( values[2])))
									.ok() ;
}
//------------------------------------------------------------------------------
/**
 * @brief 上限・UTF-8・制御文字を先に検証します
 * @param[in] body 外部本文
 * @param[in] limit byte上限
 */
//------------------------------------------------------------------------------
void validateBody( std::string_view body, std::size_t limit)
{
	// 不正UTF-8もOS変換境界で拒否します
	if( body.empty() || body.size() > limit) {
		invalid() ;
	}
	static_cast<void>( toWide( body)) ;
	// 改行とtab以外の制御文字を拒否します
	for( const unsigned char c : body) {
		// JSON/MarkdownへNULとDELを持ち込みません
		if( (c < 32 && c != '\t' && c != '\r' && c != '\n') || c == 127) {
			invalid() ;
		}
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 重複と深さを制限してJSONを解析します
 * @param[in] body UTF-8 JSON
 * @return 完全な値
 */
//------------------------------------------------------------------------------
Json strictJson( std::string_view body)
{
	std::vector<std::set<std::string>> keys ; // objectごとの重複検出
	unsigned						   events = 0 ; // 有限parserイベント
	return Json::parse( body,
					   //------------------------------------------------------------------------------
					   /**
					    * @brief JSONのparse event数/深さとobjectごとのkey重複を検査し、上限内の全eventを採用します
					    * @param[in] depth parserが通知する構造の深さ。16を超えるeventは拒否します
					    * @param[in] event object開始/終了、key、値等のparse event。objectごとに独立したkey集合を積み戻します
					    * @param[in] value 同期callback中だけ借用するJSON値。key eventで文字列として読み、内容は変更しません
					    * @return 正常時は常にtrueでeventを採用します。schema妥当性や保存成功を表しません
					    * @throws OperationError INVALID_INPUTとして、深さ超過、全event数50000超過、またはkey集合なし/重複keyのとき
					    * @note 捕捉したkeys/eventsは同期parse中だけ更新します。動的確保やJSON型照会の例外も呼出側へ伝わります
					    */
					   //------------------------------------------------------------------------------
					   [&keys, &events]( int depth, Json::parse_event_t event, Json &value)
					   {
						   // 未知fieldを無制限に解析しません
						   if( depth > 16 || ++events > 50000) {
							   invalid() ;
						   }
						   // 独立objectの集合を開始します
						   if( event == Json::parse_event_t::object_start) {
							   keys.emplace_back() ;
						   }
						   // 上書きされる重複keyを禁止します
						   if( event == Json::parse_event_t::key &&
							  (keys.empty() || !keys.back().insert( value.get<std::string>()).second)) {
							   invalid() ;
						   }
						   // object終端で集合を解放します
						   if( event == Json::parse_event_t::object_end) {
							   keys.pop_back() ;
						   }
						   return true ;
					   }) ;
}
//------------------------------------------------------------------------------
/**
 * @brief ASCII空白だけを表の周囲から除きます
 * @param[in] value セル
 * @return 呼出し元寿命のview
 */
//------------------------------------------------------------------------------
std::string_view trim( std::string_view value)
{
	// 左端の空白を有限に読み飛ばします
	while( !value.empty() && (value.front() == ' ' || value.front() == '\t')) {
		value.remove_prefix( 1) ;
	}
	// 右端の空白を有限に読み飛ばします
	while( !value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
		value.remove_suffix( 1) ;
	}
	return value ;
}
//------------------------------------------------------------------------------
/**
 * @brief 行を有界に分離します
 * @param[in] body 検証済み本文
 * @return 借用行
 */
//------------------------------------------------------------------------------
std::vector<std::string_view> lines( std::string_view body)
{
	std::vector<std::string_view> result ; // 最大8192行
	// EOFか上限で終了し、切断を成功にしません
	while( !body.empty()) {
		const auto end = body.find( '\n') ; // 行終端またはEOF
		const auto rawLine = body.substr( 0, end) ; // trim前の長さも検証
		const auto line = trim( rawLine) ; // CRLFも統一
		// 過長行と行数超過を拒否します
		if( rawLine.size() > 4096 || result.size() >= 8192) {
			invalid() ;
		}
		result.push_back( line) ;
		// 最終行は一度だけ追加します
		if( end == std::string_view::npos) {
			break ;
		}
		body.remove_prefix( end + 1) ;
	}
	return result ;
}
//------------------------------------------------------------------------------
/**
 * @brief exact見出しが範囲内で一つだけあることを検証します
 * @param[in] all 全行
 * @param[in] marker exact行
 * @param[in] begin 範囲先頭
 * @param[in] end 範囲終端
 * @return 一意位置
 */
//------------------------------------------------------------------------------
std::size_t uniqueLine( const std::vector<std::string_view> &all, std::string_view marker, std::size_t begin,
					   std::size_t end)
{
	std::size_t found = end ; // 未発見sentinel
	// 指定範囲だけを走査し専門表の同名見出しを混ぜません
	for( std::size_t i = begin; i < end; ++i) { // 行位置
		// 完全一致だけを見出しとします
		if( all[i] == marker) {
			// 同範囲の二つ目は曖昧なので拒否します
			if( found != end) {
				invalid() ;
			}
			found = i ;
		}
	}
	// 必須境界がない時は先頭/末尾を推測しません
	if( found == end) {
		invalid() ;
	}
	return found ;
}
//------------------------------------------------------------------------------
/**
 * @brief decimalを丸めずscaled integerへ変換します
 * @param[in] cell $通常decimalまたは-
 * @return presence料金
 */
//------------------------------------------------------------------------------
std::optional<std::uint64_t> decimal( std::string_view cell)
{
	// 欠損を明示ゼロと区別します
	if( cell == "-") {
		return {} ;
	}
	// 通貨単位の省略や異なる通貨を推測しません
	if( cell.size() < 2 || cell.front() != '$') {
		invalid() ;
	}
	cell.remove_prefix( 1) ;
	const auto	  dot = cell.find( '.') ; // 小数点位置
	const auto	  whole = cell.substr( 0, dot) ; // 整数部
	const auto	  fraction = dot == std::string_view::npos ? std::string_view{} : cell.substr( dot + 1) ; // 小数部
	std::uint64_t integer = 0 ; // 最大100万USD/1M
	const auto	  parsed = std::from_chars( whole.data(), whole.data() + whole.size(), integer) ; // locale非依存
	// 負数/指数/桁超過/区切りを拒否します
	if( whole.empty() || whole.find_first_not_of( "0123456789") != std::string_view::npos || parsed.ec != std::errc{} ||
	   parsed.ptr != whole.data() + whole.size() || integer > 1000000 || fraction.size() > 6 ||
	   fraction.find_first_not_of( "0123456789") != std::string_view::npos ||
	   (dot != std::string_view::npos && fraction.empty())) {
		invalid() ;
	}
	std::uint64_t sub = 0 ; // 小数6桁へ末尾ゼロだけを補います
	// 最大6桁だけを読みます
	for( std::size_t i = 0; i < 6; ++i) { // 十進小数桁
		sub = sub * 10 + (i < fraction.size() ? static_cast<unsigned>( fraction[i] - '0') : 0) ;
	}
	const auto result = integer * 1000000 + sub ; // 制限済みなのでoverflowなし
	// 上限の端数超過も拒否します
	if( result > 1000000000000ULL) {
		invalid() ;
	}
	return result ;
}
//------------------------------------------------------------------------------
/**
 * @brief 現在UTCと単調時計の両方で鮮度を判定します
 * @param[in] meta 成功値
 * @param[in] now 現UTC秒
 * @return 24時間以内
 */
//------------------------------------------------------------------------------
bool fresh( const ApiCatalogMetadata &meta, std::int64_t now)
{
	// 未来時刻・逆行・未取得を拒否します
	if( meta.fetched <= 0 || now < meta.fetched || now - meta.fetched > 86400 ||
	   (meta.attempted && now < meta.attempted)) {
		return false ;
	}
	// cache再読の公開価格はUTCのみ、同process取得は単調経過も検証します
	if( meta.monotonic != std::chrono::steady_clock::time_point{} &&
	   (std::chrono::steady_clock::now() < meta.monotonic ||
		std::chrono::steady_clock::now() - meta.monotonic > std::chrono::seconds( 86400))) {
		return false ;
	}
	return true ;
}
//------------------------------------------------------------------------------
/**
 * @brief 確認済み厳密IDだけを送信profileへ接続します
 * @param[in] id 元ID
 * @return 検証済み
 */
//------------------------------------------------------------------------------
bool capability( std::string_view id)
{
	return std::find( K_SUPPORTED_MODELS.begin(), K_SUPPORTED_MODELS.end(), id) != K_SUPPORTED_MODELS.end() ;
}
//------------------------------------------------------------------------------
/**
 * @brief 必要料金が全て存在するか確認します
 * @param[in] price presence付き価格
 * @return 概算可能
 */
//------------------------------------------------------------------------------
bool completePrice( const ApiModelPrice &price)
{
	return std::all_of( price.rates.begin(), price.rates.begin() + 5,
					   //------------------------------------------------------------------------------
					   /**
					    * @brief missingを検出します
					    * @param[in] value presence料金
					    * @return 既知
					    */
					   //------------------------------------------------------------------------------
					   []( const auto &value)
					   {
						   return value.has_value() ;
					   }) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 料金をcanonical配列へ変換します
 * @param[in] price 保存料金
 * @return null付きJSON
 */
//------------------------------------------------------------------------------
Json priceJson( const ApiModelPrice &price)
{
	Json result = Json::array() ; // short/long順序固定
	// 全10セルのpresenceを維持します
	for( const auto &rate : price.rates) {
		result.push_back( rate ? Json( *rate) : Json( nullptr)) ;
	}
	return result ;
}
//------------------------------------------------------------------------------
/**
 * @brief 確認snapshotを作ります
 * @param[in] catalog runtime正本
 * @param[in] id exact ID
 * @param[in] previous 前回値確認
 * @return 全設定を束縛したJSON
 */
//------------------------------------------------------------------------------
std::string snapshot( const ApiModelCatalog &catalog, const std::string &id, bool previous)
{
	return Json{{"profile", "api-text-speaker-v3"},
				{"prompt", apiTextSpeakerPrompt()},
				{"prompt_version", 3},
				{"prompt_sha", apiTextDigest( apiTextSpeakerPrompt())},
				{"model", id},
				{"allowed_models", Json::array( {id})},
				{"capability_revision", 1},
				{"context_tokens", 1050000},
				{"supported_output", 128000},
				{"effort", "medium"},
				{"max_output_tokens", 65536},
				{"store", false},
				{"background", false},
				{"stream", false},
				{"service_tier", "default"},
				{"currency", "USD"},
				{"mode", "Standard"},
				{"context", "Short"},
				{"short_input_max", 272000},
				{"prices", priceJson( catalog.prices.at( id))},
				{"price_url", K_PRICE_URL},
				{"models_url", K_MODELS_URL},
				{"price_fetched", catalog.priceMeta.fetched},
				{"models_fetched", catalog.modelMeta.fetched},
				{"price_sha", catalog.priceMeta.sha},
				{"models_sha", catalog.modelMeta.sha},
				{"parser_revision", 1},
				{"cost_revision", 2},
				{"generation", catalog.generation},
				{"key_revision", catalog.keyRevision},
				{"previous", previous}}
		.dump() ;
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 現在UTC秒を返します
 * @return Unix UTC秒
 * @see koefude::apiCatalogNow
 */
//------------------------------------------------------------------------------
std::int64_t apiCatalogNow()
{
	return std::chrono::duration_cast<std::chrono::seconds>( std::chrono::system_clock::now().time_since_epoch())
		.count() ;
}
//------------------------------------------------------------------------------
/**
 * @brief Modelsのstrict JSONを読みます
 * @param[in] body UTF-8 JSON1MiB以内
 * @return 検証済みID
 * @throws OperationError 不正時、部分適用なし
 * @see koefude::parseApiModels
 */
//------------------------------------------------------------------------------
std::map<std::string, ApiModelInfo> parseApiModels( std::string_view body)
{
	validateBody( body, 1048576) ;
	const auto	root = strictJson( body) ; // 重複・深さ検証済み
	const auto &data = root.at( "data") ; // 公式list形
	// 数や配列・objectの型を厳密に検証します
	if( root.at( "object") != "list" || !data.is_array() || data.size() > 4096) {
		invalid() ;
	}
	std::map<std::string, ApiModelInfo> result ; // 一意ID
	// bounded dataだけを読み、unknown fieldは保持しません
	for( const auto &item : data) {
		const auto	id = item.at( "id").get<std::string>() ; // ASCII ID
		const auto &created = item.at( "created") ; // 符号・型検証
		// owned_byは利用資格のallowlistへ使いません
		if( item.at( "object") != "model" || !validId( id) || !created.is_number_integer() ||
		   (!created.is_number_unsigned() && created.get<std::int64_t>() < 0)) {
			invalid() ;
		}
		ApiModelInfo info ; // shutdown未通知を維持
		const auto	 shutdown = item.find( "shutdown_date") ; // optional公式日付
		// nullは未通知、文字列なら実在暦日を要求します
		if( shutdown != item.end() && !shutdown->is_null()) {
			info.shutdown = shutdown->get<std::string>() ;
			// 不正日付を黙って無期限へ変えません
			if( !validDate( info.shutdown)) {
				invalid() ;
			}
		}
		// 同IDの上書きは拒否します
		if( !result.emplace( id, std::move( info)).second) {
			invalid() ;
		}
	}
	return result ;
}
//------------------------------------------------------------------------------
/**
 * @brief Audio tableだけからduration単価を読みます
 * @param[in] body 公式Markdown
 * @return presence付き単価
 * @throws OperationError 見出し/列/ID/単位の不整合
 * @see koefude::parseApiAudioPrice
 */
//------------------------------------------------------------------------------
ApiAudioPrice parseApiAudioPrice( std::string_view body)
{
	validateBody( body, 524288) ;
	const auto	all = lines( body) ; // 公式Markdownの有界行
	const auto	heading = uniqueLine( all, "Transcription models", 0, all.size()) ; // 音声専用見出し
	std::size_t end = all.size() ; // 次の同階層sectionまで
	// table後の別料金を誤採用しないよう固定headerを最初に照合します
	const auto	header = uniqueLine( all, "| Model | Use case | Input | Output | Estimated cost |", heading, end) ;
	// 固定列順とdelimiterだけを受理します
	if( header + 1 >= all.size() || all[header + 1] != "| --- | --- | --- | --- | --- |") {
		invalid() ;
	}
	ApiAudioPrice price ; // 未掲載を0へ変換しません
	bool		  found = false ; // 同IDの重複を拒否
	// 対象tableの連続行だけを解析します
	for( std::size_t index = header + 2; index < end && all[index].starts_with( "|"); ++index) {
		const auto row = all[index] ; // 有界table行
		// 厳密ID行だけを照合しtoken単価を読みません
		if( !row.starts_with( "| gpt-transcribe |")) {
			continue ;
		}
		// 表のID/use case/input/output/unitの変更は推測しません
		if( found || !row.starts_with( "| gpt-transcribe | Transcription | - | - | $") || !row.ends_with( " / minute |")) {
			invalid() ;
		}
		found = true ;
		const auto	  begin = row.find( '$') + 1 ; // USD十進値先頭
		const auto	  decimal = row.substr( begin, row.size() - begin - 11) ; // 固定minute suffixを除去
		std::uint64_t whole = 0 ; // 最大9桁のUSD整数部
		std::uint64_t fraction = 0 ; // 最大6桁のmicro部
		unsigned	  digits = 0 ; // 小数桁数
		bool		  dot = false ; // 小数点一つだけ
		// 科学記数法/負数/通貨単位混在を拒否します
		for( const char c : decimal) {
			// 一つの小数点だけ区切りとして扱います
			if( c == '.' && !dot) {
				dot = true ;
				continue ;
			}
			// ASCII十進数字だけを整数へ変換します
			if( c < '0' || c > '9' || (!dot && whole > 1000) || (dot && digits >= 6)) {
				invalid() ;
			}
			if( dot) {
				fraction = fraction * 10 + static_cast<unsigned>( c - '0') ;
				++digits ;
			} else {
				whole = whole * 10 + static_cast<unsigned>( c - '0') ;
			}
		}
		// 空/小数点だけは単価と見なしません
		if( decimal.empty() || (dot && !digits)) {
			invalid() ;
		}
		// microUSDへ正確にscaleします
		while( digits < 6) {
			fraction *= 10 ;
			++digits ;
		}
		price.microPerMinute = whole * 1'000'000 + fraction ;
	}
	return price ;
}
//------------------------------------------------------------------------------
/**
 * @brief 公式Flagship Standard11列だけを読みます
 * @param[in] body UTF-8 Markdown512KiB以内
 * @return presence付き料金
 * @throws OperationError 形式変更・曖昧な表
 * @see koefude::parseApiPrices
 */
//------------------------------------------------------------------------------
std::map<std::string, ApiModelPrice> parseApiPrices( std::string_view body)
{
	validateBody( body, 524288) ;
	const auto all = lines( body) ; // 表範囲の借用行
	const auto title = uniqueLine( all, "# Pricing", 0, all.size()) ; // 必須title
	const auto anchor = uniqueLine( all, "<a id=\"latest-models\"></a>", title, all.size()) ; // 公式anchor
	const auto flagship = uniqueLine( all, "Flagship models", anchor, all.size()) ; // 単独label
	const auto boundary = std::find_if( all.begin() + flagship + 1, all.end(),
		//------------------------------------------------------------------------------
		/**
		 * @brief 最初の既知sectionでFlagshipを閉じます
		 * @param[in] line 解析済み行
		 * @return 現行または従来の価格種別境界
		 */
		//------------------------------------------------------------------------------
		[]( const auto line)
		{
			return line == "Multimodal models" || line == "Cyber models" ;
		}) ;
	// 境界がない文書を末尾までFlagshipと推測しません
	if( boundary == all.end()) {
		invalid() ;
	}
	const auto sectionEnd = static_cast<std::size_t>( std::distance( all.begin(), boundary)) ; // 最初の価格種別境界
	const auto standard = uniqueLine( all, "### Standard pricing data", flagship, sectionEnd) ; // Flagshipだけ
	const auto batch = uniqueLine( all, "### Batch pricing data", standard, sectionEnd) ; // 次mode境界
	static_cast<void>( uniqueLine( all, "Prices per 1M tokens.", flagship, sectionEnd)) ;
	static_cast<void>(
		uniqueLine( all, "Short context: ≤272K input tokens. Long context: >272K input tokens.", flagship, sectionEnd)) ;
	const auto header = uniqueLine( all, K_HEADER, standard, batch) ; // 一つだけの11列表
	// 列区切りの変更を推測補正しません
	if( header + 1 >= batch || all[header + 1] != K_DELIMITER) {
		invalid() ;
	}
	// HTML応答をMarkdownと取り違えません。公式anchorと既知モデル注釈は別契約です
	for( const auto line : all) {
		// 公式anchor以外のHTML行を拒否します
		if( line.starts_with( '<') && !(line.starts_with( "<a id=\"") && line.ends_with( "\"></a>"))) {
			invalid() ;
		}
	}
	std::map<std::string, ApiModelPrice> result ; // Short/Longを別位置で保持
	std::size_t							 row = header + 2 ; // 最初のdata行
	// 空行で対象表を終了し、後続旧4列表へ流しません
	for(; row < batch && !all[row].empty(); ++row) {
		auto line = all[row] ; // 現在行
		// 最大行数と明示pipe両端を要求します
		if( result.size() >= 1024 || !line.starts_with( '|') || !line.ends_with( '|')) {
			invalid() ;
		}
		line.remove_prefix( 1) ;
		line.remove_suffix( 1) ;
		std::vector<std::string_view> cells ; // 11列だけ
		// pipeを数え、余分な列も不足列も後で拒否します
		for(;;) {
			const auto pipe = line.find( '|') ; // 次セル終端
			cells.push_back( trim( line.substr( 0, pipe))) ;
			// 最終セルで終了します
			if( pipe == std::string_view::npos) {
				break ;
			}
			line.remove_prefix( pipe + 1) ;
		}
		// 列数が固定と異なる応答は採用しません
		if( cells.size() != 11) {
			invalid() ;
		}
		auto					   id = cells[0] ; // 条件注釈を分離する前のモデルセル
		constexpr std::string_view SUFFIX = " (<272K context length)" ; // 公式の既知条件だけ
		// 既知suffix以外の注釈を推測除去しません
		if( id.ends_with( SUFFIX)) {
			id.remove_suffix( SUFFIX.size()) ;
		}
		// 不明なモデル記法を別IDへ変形しません
		if( !validId( id)) {
			invalid() ;
		}
		ApiModelPrice price ; // 欠損をpresence付きで保持
		// 全10セルの文法を検証しLongも不正を見逃しません
		for( std::size_t i = 0; i < 10; ++i) {
			price.rates[i] = decimal( cells[i + 1]) ;
		}
		// input/output必須、他セルはmissingとして保持します
		if( !price.rates[0] || !price.rates[1] || !result.emplace( std::string( id), price).second) {
			invalid() ;
		}
	}
	// 空表と予期しない別表を拒否します。既知の旧4列だけ認識します
	if( result.empty()) {
		invalid() ;
	}
	// 同Standard内の表を一つずつ確認し未知headerを混入させません
	for(; row < batch; ++row) {
		// pipe行は旧4列headerから始まる別表だけを許可します
		if( all[row].starts_with( "| Model") && all[row] != "| Model | Input | Output | Cached input |") {
			invalid() ;
		}
	}
	return result ;
}
//------------------------------------------------------------------------------
/**
 * @brief 厳密対応IDと取得済みIDから候補を作ります
 * @param[in] catalog 唯一の正本
 * @param[in] selected 保持するID
 * @param[in] now UTC秒
 * @return 未取得候補も含む和集合。送信条件成立時だけsnapshotを持ちます
 * @see koefude::apiCatalogChoices
 */
//------------------------------------------------------------------------------
std::vector<ApiCatalogSelection> apiCatalogChoices( const ApiModelCatalog &catalog, std::string_view selected,
												   std::int64_t now)
{
	std::set<std::string> ids ; // 対応済みIDと取得済みIDの和集合
	// 取得前も既存対応モデルを選べますが、認証と単価の送信条件は後で独立判定します
	for( const auto id : K_SUPPORTED_MODELS) {
		ids.emplace( id) ;
	}
	// ModelsのIDを全て一覧へ追加します
	for( const auto &[id, info] : catalog.models) {
		static_cast<void>( info) ;
		ids.insert( id) ;
	}
	// 公開価格だけのIDも利用可と断定せず追加します
	for( const auto &[id, price] : catalog.prices) {
		static_cast<void>( price) ;
		ids.insert( id) ;
	}
	// 消えた選択も保持し無言切替を防ぎます
	if( validId( selected)) {
		ids.emplace( selected) ;
	}
	std::vector<ApiCatalogSelection> result ; // UI/送信共通値
	// 各IDを独立した可否へ分類します
	for( const auto &id : ids) {
		ApiCatalogSelection choice ; // 初期は利用不可
		choice.id = id ;
		const auto model = catalog.models.find( id) ; // 現在のモデル存在
		const auto price = catalog.prices.find( id) ; // 現在の価格存在
		// 対象外IDを互換性未確認と区別します
		if( id.starts_with( "ft:") || id.find( "audio") != std::string::npos || id.find( "image") != std::string::npos ||
		   id.find( "embedding") != std::string::npos || id.find( "tts") != std::string::npos ||
		   id.find( "whisper") != std::string::npos) {
			choice.state = ApiModelAvailability::INCOMPATIBLE ;
		} else if( !capability( id)) { // 根拠のないprefixで有効化しません
			choice.state = ApiModelAvailability::CAPABILITY_UNVERIFIED ;
		} else if( price == catalog.prices.end() || !completePrice( price->second)) { // missingは無料ではありません
			choice.state = ApiModelAvailability::PRICE_UNAVAILABLE ;
		} else if( model != catalog.models.end() && !model->second.shutdown.empty()) { // 暦日の当日から終了です
			const auto &date = model->second.shutdown ; // 検証済み日付
			const auto	year = std::stoi( date.substr( 0, 4)) ; // 4桁有界
			const auto	month = static_cast<unsigned>( std::stoi( date.substr( 5, 2))) ; // 2桁有界
			const auto	day = static_cast<unsigned>( std::stoi( date.substr( 8, 2))) ; // 2桁有界
			const auto	expiry =
				std::chrono::sys_days( std::chrono::year( year) / month / day).time_since_epoch() ; // UTC日
			// 当日の開始以降を利用不可にします
			if( now >= std::chrono::duration_cast<std::chrono::seconds>( expiry).count()) {
				choice.state = ApiModelAvailability::SHUTDOWN ;
			}
		}
		// 全前提成立時だけAvailableへ進みます
		if( choice.state == ApiModelAvailability::UNAVAILABLE && capability( id) && price != catalog.prices.end() &&
		   completePrice( price->second) && model != catalog.models.end() && !catalog.refreshing &&
		   catalog.modelsKeyRevision == catalog.keyRevision && catalog.modelMeta.http != 401 &&
		   catalog.modelMeta.http != 403 && fresh( catalog.modelMeta, now) && fresh( catalog.priceMeta, now)) {
			choice.state = ApiModelAvailability::AVAILABLE ;
			choice.previous = catalog.modelMeta.error || catalog.priceMeta.error || catalog.modelMeta.http != 200 ||
							  catalog.priceMeta.http != 200 ;
			choice.snapshot = snapshot( catalog, id, choice.previous) ;
		}
		result.push_back( std::move( choice)) ;
	}
	return result ;
}
//------------------------------------------------------------------------------
/**
 * @brief 行メニューのlocal前提だけを照合します。認証資格は送信前に別途確認します
 * @param[in] catalog local cacheと適用済み設定
 * @param[in] keyReady キー有無だけ
 * @param[in] now UTC秒
 * @return 厳密IDと料金が有効なら送信前準備用snapshot、他は空
 * @see koefude::apiMenuSnapshot
 */
//------------------------------------------------------------------------------
std::string apiMenuSnapshot( const ApiModelCatalog &catalog, bool keyReady, std::int64_t now)
{
	const auto &id = catalog.settings.model ; // 適用済みexact IDだけ
	const auto	model = catalog.models.find( id) ; // 保存済み存在情報
	const auto	price = catalog.prices.find( id) ; // 保存済みStandard単価
	// local準備でsessionの認証成功を捏造せず、認証拒否も送信可能へ戻しません
	if( !keyReady || !catalog.loaded || catalog.cacheCorrupt || catalog.refreshing || !capability( id) ||
	   model == catalog.models.end() || price == catalog.prices.end() || !completePrice( price->second) ||
	   !fresh( catalog.modelMeta, now) || !fresh( catalog.priceMeta, now) || catalog.modelMeta.http == 401 ||
	   catalog.modelMeta.http == 403) {
		return {} ;
	}
	// 終了日は当日から無効とします
	if( !model->second.shutdown.empty()) {
		const auto &date = model->second.shutdown ; // parser検証済み暦日
		const auto	expiry = std::chrono::sys_days( std::chrono::year( std::stoi( date.substr( 0, 4))) /
												   static_cast<unsigned>( std::stoi( date.substr( 5, 2))) /
												   static_cast<unsigned>( std::stoi( date.substr( 8, 2)))) ; // UTC日
		// 終了したモデルの確認GETやPOSTを開始しません
		if( now >= std::chrono::duration_cast<std::chrono::seconds>( expiry.time_since_epoch()).count()) {
			return {} ;
		}
	}
	return snapshot( catalog, id,
					catalog.modelMeta.error || catalog.priceMeta.error || catalog.modelMeta.http != 200 ||
						catalog.priceMeta.http != 200) ;
}
//------------------------------------------------------------------------------
/**
 * @brief snapshot単体を保存再読用に検証します
 * @param[in] value 有界JSON
 * @return 検証済み価格
 * @throws OperationError 不正snapshot
 * @see koefude::apiCatalogSnapshotPrice
 */
//------------------------------------------------------------------------------
ApiModelPrice apiCatalogSnapshotPrice( std::string_view value)
{
	validateBody( value, 16384) ;
	const auto j = strictJson( value) ; // 保存sourceとは独立の凍結契約
	const auto id = j.at( "model").get<std::string>() ; // 厳密モデル
	// revision・料金scope・要求設定を厳密検証します
	if( !capability( id) || (j.at( "profile") != "api-text-context-v2" && j.at( "profile") != "api-text-speaker-v3") ||
	   j.at( "allowed_models") != Json::array( {id}) || j.at( "capability_revision") != 1 ||
	   j.at( "context_tokens") != 1050000 || j.at( "supported_output") != 128000 || j.at( "effort") != "medium" ||
	   j.at( "max_output_tokens") != 65536 || j.at( "store") != false || j.at( "background") != false ||
	   j.at( "stream") != false || j.at( "service_tier") != "default" || j.at( "currency") != "USD" ||
	   j.at( "mode") != "Standard" || j.at( "context") != "Short" || j.at( "short_input_max") != 272000 ||
	   j.at( "price_url").get<std::string>() != K_PRICE_URL || j.at( "models_url").get<std::string>() != K_MODELS_URL ||
	   j.at( "parser_revision") != 1 || j.at( "cost_revision") != 2 || j.at( "prices").size() != 10 ||
	   j.at( "price_fetched").get<std::int64_t>() <= 0 || j.at( "models_fetched").get<std::int64_t>() <= 0 ||
	   j.at( "generation").get<std::uint64_t>() == 0 || j.at( "key_revision").get<std::uint64_t>() == 0) {
		invalid() ;
	}
	const auto prompt = j.at( "prompt").get<std::string>() ; // 送信時の所有指示
	validateBody( prompt, 8192) ;
	// 指示も版とSHAで束縛し、現在指示へ置換しません
	if( (j.at( "profile") == "api-text-context-v2" ? j.at( "prompt_version") != 1
												 : j.at( "prompt_version") != 3 || prompt != apiTextSpeakerPrompt()) ||
	   j.at( "prompt_sha").get<ApiTextDigest>() != apiTextDigest( prompt)) {
		invalid() ;
	}
	static_cast<void>( j.at( "previous").get<bool>()) ;
	static_cast<void>( j.at( "price_sha").get<std::array<std::uint8_t, 32>>()) ;
	static_cast<void>( j.at( "models_sha").get<std::array<std::uint8_t, 32>>()) ;
	ApiModelPrice result ; // 全セルのpresenceを復元
	// scaled integer以外を数値変換しません
	for( std::size_t i = 0; i < 10; ++i) { // 各価格位置
		const auto &rate = j.at( "prices").at( i) ; // 保存セル
		// nullだけをmissingとして保持します
		if( rate.is_null()) {
			continue ;
		}
		// 範囲と整数型を検証します
		if( !rate.is_number_unsigned() || rate.get<std::uint64_t>() > 1000000000000ULL) {
			invalid() ;
		}
		result.rates[i] = rate.get<std::uint64_t>() ;
	}
	// 新profileに必要な全料金を要求します
	if( !completePrice( result)) {
		invalid() ;
	}
	return result ;
}
//------------------------------------------------------------------------------
/**
 * @brief snapshotの厳密モデルIDを返します
 * @param[in] value 読取り専用の有界UTF-8 JSON snapshot。呼出し中だけ借用し、strict schemaを検証します
 * @return snapshot内で検証した厳密なモデルIDの独立所有stringです
 * @see koefude::apiCatalogSnapshotModel
 * @throws OperationError snapshotが不正、未対応schema、またはモデルIDの厳密条件を満たさない場合
 */
//------------------------------------------------------------------------------
std::string apiCatalogSnapshotModel( std::string_view value)
{
	static_cast<void>( apiCatalogSnapshotPrice( value)) ;
	return strictJson( value).at( "model").get<std::string>() ;
}
//------------------------------------------------------------------------------
/**
 * @brief workerの現在条件へdraftを照合します
 * @param[in] catalog runtime正本
 * @param[in] value 確認時所有値
 * @return 現在も送信可能か
 * @see koefude::apiCatalogMatches
 */
//------------------------------------------------------------------------------
bool apiCatalogMatches( const ApiModelCatalog &catalog, std::string_view value)
{
	const auto id = apiCatalogSnapshotModel( value) ; // 保存値を先に検証
	const auto choices = apiCatalogChoices( catalog, id) ; // workerの現在鮮度
	return std::any_of( choices.begin(), choices.end(),
					   //------------------------------------------------------------------------------
					   /**
					    * @brief 完全同一snapshotだけ許可します
					    * @param[in] choice 現選択値
					    * @return 一致
					    */
					   //------------------------------------------------------------------------------
					   [&value, &id]( const auto &choice)
					   {
						   return choice.id == id && !choice.snapshot.empty() && choice.snapshot == value ;
					   }) ;
}
//------------------------------------------------------------------------------
/**
 * @brief キー変更で資格と同意を失効します
 * @param[in,out] catalog worker正本
 * @see koefude::invalidateApiCatalogKey
 */
//------------------------------------------------------------------------------
void invalidateApiCatalogKey( ApiModelCatalog &catalog)
{
	// wrapを再利用せず停止可能な異常とします
	if( catalog.keyRevision == std::numeric_limits<std::uint64_t>::max() ||
	   catalog.generation == std::numeric_limits<std::uint64_t>::max()) {
		invalid() ;
	}
	++catalog.keyRevision ;
	++catalog.generation ;
	catalog.modelsKeyRevision = 0 ;
}
//------------------------------------------------------------------------------
/**
 * @brief 固定GETを直列実行します
 * @param[in] request 専用要求
 * @param[in,out] catalog 正本
 * @param[in,out] keys 固定キー
 * @param[in,out] http typed GET
 * @param[in,out] port 同folder保存
 * @param[in] stop 協調取消し
 * @return 部分成功を保持する所有終端
 * @see koefude::runApiCatalogJob
 */
//------------------------------------------------------------------------------
ApiCatalogJobCompletion runApiCatalogJob( const ApiCatalogJobRequest &request, ApiModelCatalog &catalog,
										 IApiKeyStore &keys, IApiCatalogTransport &http, IApiTextPersistencePort &port,
										 std::stop_token stop)
{
	ApiCatalogJobCompletion completion ; // metadataだけの終端
	completion.request = request ;
	// sourceなし要求でもキーrevision不一致は通信前に拒否します
	if( request.operationId == 0 || (request.keyRevision && request.keyRevision != catalog.keyRevision) ||
	   catalog.refreshing || catalog.generation == std::numeric_limits<std::uint64_t>::max()) {
		invalid() ;
	}
	// 初回だけcacheを表示用に読み、資格の成功revisionは復元しません
	if( !catalog.loaded) {
		try {
			const auto runtimeKeyRevision = catalog.keyRevision ; // cacheへ保存しない現在値
			const auto runtimeGeneration = catalog.generation ; // キー操作で進んだ値
			catalog = port.loadCatalog() ; // 表示metadataだけ復元
			catalog.keyRevision = runtimeKeyRevision ;
			catalog.generation = runtimeGeneration ;
			catalog.modelsKeyRevision = 0 ;
		} catch( ...) {
			completion.code = ERROR_INVALID_DATA ;
			catalog.cacheCorrupt = true ;
		}
		catalog.loaded = true ;
	}
	// 起動時はlocal cacheと固定targetの有無だけをworkerで準備します
	if( request.localOnly) {
		completion.keyReady = keys.acquire() != nullptr ;
		completion.keyPersisted = keys.persisted() ;
		completion.catalog = catalog ;
		return completion ;
	}
	const auto previousSettings = catalog.settings ; // 保存失敗時は有効設定を維持
	// 明示した一般設定だけを反映し送信同意を失効します
	if( request.settings) {
		static_cast<void>( encodeApiSettings( *request.settings)) ;
		catalog.settings = *request.settings ;
	}
	++catalog.generation ;
	// 設定適用はHTTPも認証GETも開始しません
	if( request.settingsOnly) {
		try {
			checkCancelled( stop) ;
			// 未知/破損cacheの原fileを一般設定適用で上書きしません
			if( catalog.cacheCorrupt) {
				throw OperationError( ErrorKind::SAVE_FAILED, ERROR_INVALID_DATA) ;
			}
			port.saveCatalog( catalog) ;
		} catch( ...) {
			catalog.settings = previousSettings ;
			completion.code = ERROR_WRITE_FAULT ;
		}
		completion.catalog = catalog ;
		return completion ;
	}
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 60) ; // operation期限
	const auto key = keys.acquire() ; // このworkerだけがキーへ触れます
	completion.keyReady = key != nullptr ;
	completion.keyPersisted = keys.persisted() ;
	// キー未設定時は前回の利用資格を送信前提へ残しません
	if( !key) {
		catalog.modelsKeyRevision = 0 ;
	}
	catalog.refreshing = true ;
	const auto fetch =
		//------------------------------------------------------------------------------
		/**
		 * @brief 一componentを一回だけ検証採用します
		 * @param[in] prices 公開価格か
		 */
		//------------------------------------------------------------------------------
		[&]( bool prices)
		{
			auto &meta = prices ? catalog.priceMeta : catalog.modelMeta ; // 鮮度は独立
			meta.attempted = apiCatalogNow() ;
			meta.http = 0 ;
			meta.error = 0 ;
			try {
				checkCancelled( stop) ;
				// 直列後半も全体期限を越えて開始しません
				if( std::chrono::steady_clock::now() >= deadline) {
					throw OperationError( ErrorKind::INTERNAL, ERROR_TIMEOUT) ;
				}
				const auto response =
					prices ? http.getPrices( stop, deadline) : http.listModels( *key, stop, deadline) ; // typed分離
				meta.http = response.status ;
				meta.error = response.error ;
				// transport失敗もduration側の最終試行だけへ独立反映します
				if( prices) {
					catalog.audioPriceMeta.attempted = meta.attempted ;
					catalog.audioPriceMeta.http = response.status ;
					catalog.audioPriceMeta.error = response.error ? response.error : ERROR_INVALID_DATA ;
				}
				const auto expected = prices ? "text/markdown" : "application/json" ; // 検証するMIME
				auto	   type = response.contentType.substr( 0, response.contentType.find( ';')) ; // charsetだけを分離
				// 有効な成功応答だけをparserへ渡します
				if( response.error || response.status != 200 || type != expected ||
				   (!response.encoding.empty() && response.encoding != "identity")) {
					invalid() ;
				}
				const auto bodySha = apiTextDigest( response.body) ; // 採用前に成功metadataも準備
				// 部分成功は各componentごとに独立採用します
				if( prices) {
					// 同じ公式GETを独立duration parserへ渡し、text失敗に巻き込まない
					auto &audioMeta = catalog.audioPriceMeta ; // 成功と最終試行は別field
					audioMeta.attempted = meta.attempted ;
					audioMeta.http = response.status ;
					audioMeta.error = 0 ;
					try {
						catalog.audioPrice = parseApiAudioPrice( response.body) ;
						audioMeta.fetched = meta.attempted ;
						audioMeta.sha = bodySha ;
						audioMeta.monotonic = std::chrono::steady_clock::now() ;
					} catch( ...) {
						audioMeta.error = ERROR_INVALID_DATA ;
					}
					catalog.prices = parseApiPrices( response.body) ;
				} else {
					catalog.models = parseApiModels( response.body) ;
					catalog.modelsKeyRevision = catalog.keyRevision ;
				}
				meta.fetched = meta.attempted ;
				meta.sha = bodySha ;
				meta.monotonic = std::chrono::steady_clock::now() ;
			} catch( const OperationError &error) {
				// 旧成功日時を維持し、試行の失敗だけ記録します
				meta.error = meta.error ? meta.error : error.code() ? error.code() : ERROR_INVALID_DATA ;
			} catch( ...) {
				meta.error = ERROR_INVALID_DATA ; // 生HTTP/exceptionを表示しません
			}
		} ;
	// open/manual時だけ価格を一回取得します
	if( request.prices) {
		fetch( true) ;
	}
	// キー未設定では認証GETを開始しません
	if( key) {
		fetch( false) ;
	}
	catalog.refreshing = false ;
	try {
		// 不正cacheの原fileは保持し新snapshotはprocess内だけに残します
		if( catalog.cacheCorrupt) {
			throw OperationError( ErrorKind::SAVE_FAILED, ERROR_INVALID_DATA) ;
		}
		port.saveCatalog( catalog) ;
		catalog.saveFailed = false ;
	} catch( ...) {
		catalog.saveFailed = true ;
		completion.code = ERROR_WRITE_FAULT ;
	}
	completion.catalog = catalog ;
	return completion ;
}
} // namespace koefude
