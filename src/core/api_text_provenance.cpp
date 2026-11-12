//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_text_provenance.cpp
 * @brief 保存snapshotから要求モデルと実応答モデルを区別します
 * @version 1.0.0
 * @date 2026/11/08
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/08 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1346の本文なし出所・ログ接続を追加
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1348の行メニュー直接実行と保存後表示を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1350のTerra要求・実応答・日付出所を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1351のAPI専用HTTP失敗・保存状態を接続
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
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
#include "core/api_text_provenance.h"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace koefude {
namespace {
//------------------------------------------------------------------------------
/**
 * @brief YYYYMMDDの実在日付を検証します
 * @param[in] date 整数日付
 * @return 実在日付
 */
//------------------------------------------------------------------------------
bool validDate( std::uint32_t date) noexcept
{
	const auto year = static_cast<int>( date / 10000) ; // 整数年
	const auto month = date / 100 % 100 ; // 整数月
	const auto day = date % 100 ; // 整数日
	return year >= 2000 && year <= 2099 &&
		   std::chrono::year_month_day{std::chrono::year( year), std::chrono::month( month), std::chrono::day( day)}.ok() ;
}
//------------------------------------------------------------------------------
/**
 * @brief 既知モデルに固定文字列を対応付けます
 * @param[in] model 型付きID
 * @return ASCII固定値
 */
//------------------------------------------------------------------------------
std::string_view baseName( ApiProvenanceModel model) noexcept
{
	// 既知列挙以外は未検証へ閉じます
	switch( model) {
		case ApiProvenanceModel::GPT61_SOL:
			return "gpt-6.1-sol" ; // 既存互換profile
		case ApiProvenanceModel::GPT6_ASTRA:
			return "gpt-6-astra" ; // 既存互換profile
		case ApiProvenanceModel::GPT6_LUNA:
			return "gpt-6-luna" ; // 既存互換profile
		case ApiProvenanceModel::GPT61_TERRA:
			return "gpt-6.1-terra" ; // ADR1350の確認済み互換profile
		case ApiProvenanceModel::GPT_TRANSCRIBE:
			return "gpt-transcribe" ; // 音声用途の固定ID
		case ApiProvenanceModel::MISSING:
			return "not-reported" ; // 未通知
		case ApiProvenanceModel::MULTIPLE:
			return "multiple" ; // 複数
		default:
			return "unverified" ; // 任意文字列を返しません
	}
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief IDを既知modelと日付へ制限します
 * @param[in] id 借用ID
 * @return 未通知/未検証を補完しない整数値
 * @see koefude::apiProvenanceId
 */
//------------------------------------------------------------------------------
ApiProvenanceId apiProvenanceId( std::string_view id) noexcept
{
	// 未通知を現在選択で埋めません
	if( id.empty()) {
		return {} ;
	}
	constexpr std::array MODELS{ApiProvenanceModel::GPT61_SOL, ApiProvenanceModel::GPT6_ASTRA,
								ApiProvenanceModel::GPT6_LUNA,
								ApiProvenanceModel::GPT61_TERRA} ; // 厳密な既知profileだけ
	// 既知IDまたは実在する日付suffixだけを整数へ変換します
	for( const auto model : MODELS) {
		const auto name = baseName( model) ; // 固定prefix
		// 厳密IDは日付0です
		if( id == name) {
			return {model, 0} ;
		}
		// 日付suffix以外の任意文字・path・秘密を保存しません
		if( id.size() != name.size() + 11 || !id.starts_with( name) || id[name.size()] != '-' ||
		   id[name.size() + 5] != '-' || id[name.size() + 8] != '-') {
			continue ;
		}
		std::uint32_t date = 0 ; // 固定8桁だけを収集
		bool		  digits = true ; // 数字以外を拒否
		// YYYY-MM-DDを有界に走査します
		for( std::size_t i = name.size() + 1; i < id.size(); ++i) {
			// 固定の区切りを読み飛ばします
			if( i == name.size() + 5 || i == name.size() + 8) {
				continue ;
			}
			// 非数字はID未検証として扱います
			if( id[i] < '0' || id[i] > '9') {
				digits = false ;
				break ;
			}
			date = date * 10 + static_cast<unsigned>( id[i] - '0') ;
		}
		// 暦としても正しいsuffixだけを採用します
		if( digits && validDate( date)) {
			return {model, date} ;
		}
	}
	return {ApiProvenanceModel::UNVERIFIED, 0} ;
}
//------------------------------------------------------------------------------
/**
 * @brief 厳密要求へ同じ既知モデルの実応答だけを照合します
 * @param[in] requested 日付なしの厳密要求ID
 * @param[in] actual 厳密IDまたは実在日付suffix
 * @return 未知・別モデル・不正suffixはfalse。保存再読と実応答で同じ判定を使います
 * @see koefude::apiProvenanceMatches
 */
//------------------------------------------------------------------------------
bool apiProvenanceMatches( std::string_view requested, std::string_view actual) noexcept
{
	const auto expected = apiProvenanceId( requested) ; // 厳密既知IDだけを要求に使います
	const auto response = apiProvenanceId( actual) ; // 日付文法と暦日も検証済み
	return expected.model >= ApiProvenanceModel::GPT61_SOL && expected.model <= ApiProvenanceModel::GPT61_TERRA &&
		   expected.date == 0 && response.model == expected.model ;
}
//------------------------------------------------------------------------------
/**
 * @brief 固定文字だけのログ表示を返します
 * @param[in] id 型付きID
 * @return ASCII
 * @see koefude::apiProvenanceModelName
 */
//------------------------------------------------------------------------------
std::string apiProvenanceModelName( ApiProvenanceId id)
{
	std::string text( baseName( id.model)) ; // 固定ASCIIだけ
	// 検証済みモデルにだけ実在日付を付けます
	if( id.model >= ApiProvenanceModel::GPT61_SOL && id.model <= ApiProvenanceModel::GPT61_TERRA && id.date &&
	   validDate( id.date)) {
		const auto date = std::to_string( id.date) ; // 8桁保証
		text += "-" + date.substr( 0, 4) + "-" + date.substr( 4, 2) + "-" + date.substr( 6, 2) ;
	}
	return text ;
}
//------------------------------------------------------------------------------
/**
 * @brief live catalogなしで保存snapshotを読みます
 * @param[in] attempt 保存値
 * @return 要求/実応答を分けた出所
 * @see koefude::apiTextProvenance
 */
//------------------------------------------------------------------------------
ApiTextProvenance apiTextProvenance( const ApiTextAttempt &attempt) noexcept
{
	ApiTextProvenance result ; // 本文を所有しません
	result.state = attempt.state ;
	result.operation = attempt.operationId ;
	try {
		result.requested = apiProvenanceId( attempt.draft.catalogSnapshot.empty()
											   ? "gpt-6.1-sol"
											   : apiCatalogSnapshotModel( attempt.draft.catalogSnapshot)) ;
	} catch( ...) {
		result.requested = {ApiProvenanceModel::UNVERIFIED, 0} ; // 不正snapshotを推測で補完しません
	}
	bool first = true ; // 実応答0件は未通知
	// 保存済み最大32chunkのモデルだけを比較します
	for( const auto &chunk : attempt.results) {
		const auto id = apiProvenanceId( chunk.model) ; // 自由文はここで捨てます
		// 一つ目を正本にし、相違は複数と示します
		if( first) {
			result.actual = id ;
			first = false ;
		} else if( result.actual != id) { // 別実モデルを単一IDへ潰しません
			result.actual = {ApiProvenanceModel::MULTIPLE, 0} ;
		}
	}
	return result ;
}
//------------------------------------------------------------------------------
/**
 * @brief 再読でownerが分かれた同じ保存試行を照合します
 * @param[in] left 保存値、null可
 * @param[in] right 保存値、null可
 * @return 原文key/生成ID/本文SHAが全一致
 * @see koefude::sameApiTextAttempt
 */
//------------------------------------------------------------------------------
bool sameApiTextAttempt( const std::shared_ptr<const ApiTextAttempt> &left,
						const std::shared_ptr<const ApiTextAttempt> &right) noexcept
{
	return left && right && left->operationId == right->operationId && left->draft.source == right->draft.source &&
		   left->candidateSha == right->candidateSha ;
}
//------------------------------------------------------------------------------
/**
 * @brief API原文選好を旧local補正より優先すべきか返します
 * @param[in] record 保存値
 * @return API使用済みで原文が明示選択されている場合true
 * @see koefude::apiTextOriginalSelected
 */
//------------------------------------------------------------------------------
bool apiTextOriginalSelected( const ApiTextCorrectionRecord &record) noexcept
{
	return record.showOriginal && (record.latest || record.adopted) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 同じ通常worker処理へ限定ログを付けます
 * @param[in] request 明示要求
 * @param[in,out] keys キー境界
 * @param[in,out] http HTTP境界
 * @param[in,out] port 保存境界
 * @param[in] stop 取消し
 * @param[in] progress 実進捗
 * @param[in,out] catalog 正本、null可
 * @param[in,out] log 限定sink
 * @param[in,out] metadata 直接実行の固定GET境界、他はnull可
 * @return 通常処理と同じ所有終端
 * @throws OperationError 通常処理の拒否/失敗。ログ失敗は投げません
 * @see koefude::runApiTextJobLogged
 */
//------------------------------------------------------------------------------
ApiTextJobCompletion runApiTextJobLogged( const ApiTextCorrectionJobRequest &request, IApiKeyStore &keys,
										 IApiHttpTransport &http, IApiTextPersistencePort &port, std::stop_token stop,
										 const ProgressCallback &progress, ApiModelCatalog *catalog,
										 IApiCorrectionEventLog &log, IApiCatalogTransport *metadata)
{
	ApiCorrectionEventData event ; // 自由文なし型境界
	event.history = request.key.historyId ;
	event.operation = request.operationId ;
	bool enabled = true ; // 操作の選択だけは使用済みとして記録しません
	// 記録対象を送信/保存/表示の明示操作へ限定します
	switch( request.action) {
		case ApiTextAction::IMPROVE:
		case ApiTextAction::SEND:
			event.event = ApiCorrectionEvent::STARTED ;
			break ; // 明示要求
		case ApiTextAction::ADOPT:
			event.event = ApiCorrectionEvent::ADOPT ;
			break ; // 採用
		case ApiTextAction::ORIGINAL:
			event.event = ApiCorrectionEvent::ORIGINAL ;
			break ; // 原文
		case ApiTextAction::CORRECTED:
			event.event = ApiCorrectionEvent::CORRECTED ;
			break ; // 採用表示
		case ApiTextAction::RETRY_SAVE:
			event.event = ApiCorrectionEvent::RETRY_SAVE ;
			break ; // 保存
		default:
			enabled = false ;
			break ; // キー/選択/metadata/閲覧/明示exportは記録しません
	}
	// 送信開始は確認済みdraftの要求モデルだけを持ちます
	if( (request.action == ApiTextAction::SEND && request.draft) || request.action == ApiTextAction::IMPROVE) {
		ApiTextAttempt started ; // 原文・candidateを複製しません
		started.draft.catalogSnapshot = request.draft ? request.draft->catalogSnapshot : request.catalogSnapshot ;
		started.operationId = request.operationId ;
		event.provenance = apiTextProvenance( started) ;
		static_cast<void>( log.append( event)) ;
		event.event = ApiCorrectionEvent::TERMINAL ;
	}
	try {
		auto result = runApiTextJob( request, keys, http, port, stop, progress, catalog,
									metadata) ; // 通常処理
		// ログ成功を保存成功の条件へ追加しません
		if( enabled) {
			const auto &record =
				result.persistence.operationId ? result.persistence.record : request.record ; // 同要求の所有値
			auto attempt = request.action == ApiTextAction::CORRECTED ? record.adopted : record.latest ; // 同owner
			const bool sending =
				request.action == ApiTextAction::SEND || request.action == ApiTextAction::IMPROVE ; // 今回送信だけ
			// 送信前拒否に以前の採用Aを要求Bの成功出所として割り当てません
			if( sending && (!attempt || attempt->operationId != request.operationId)) {
				attempt.reset() ;
				event.provenance.state = stop.stop_requested() ? ApiTextState::CANCELLED : ApiTextState::FAILURE ;
			}
			// 対象候補がある場合だけ保存された出所を取得します
			if( attempt) {
				event.provenance = apiTextProvenance( *attempt) ;
				event.adopted = result.persistence.committed && sameApiTextAttempt( record.adopted, attempt) ;
			}
			event.code = result.code ? result.code : result.persistence.code ;
			// 最新の実応答診断をsummaryへ保持し、前試行から補完しません
			if( attempt && !attempt->results.empty()) {
				const auto &last = attempt->results.back() ; // 最大32件の末尾
				event.httpStatus = last.httpStatus ;
				event.code = last.code ;
				event.httpError = last.httpError ;
			}
			event.saved = result.persistence.committed ;
			static_cast<void>( log.append( event)) ;
			// 複数実モデルや不一致でも各実応答を失いません
			if( attempt && (request.action == ApiTextAction::SEND || request.action == ApiTextAction::IMPROVE)) {
				// 最大32chunkの検証済みIDだけを出します
				for( const auto &chunk : attempt->results) {
					++event.chunk ;
					event.httpStatus = chunk.httpStatus ;
					event.code = chunk.code ;
					event.httpError = chunk.httpError ;
					event.provenance.actual = apiProvenanceId( chunk.model) ;
					static_cast<void>( log.append( event)) ;
				}
			}
		}
		return result ;
	} catch( ...) {
		// 事前拒否や取消しも成功表示にしません
		if( enabled) {
			event.provenance.state = stop.stop_requested() ? ApiTextState::CANCELLED : ApiTextState::FAILURE ;
			static_cast<void>( log.append( event)) ;
		}
		throw ; // 通常処理の例外契約を維持します
	}
}
} // namespace koefude
