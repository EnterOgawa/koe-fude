//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_text_status.h
 * @brief API文章改善の終端を音声処理と独立した固定表示へ変換します
 * @version 1.0.0
 * @date 2026/11/08
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/08 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1351の共通終端・旧HTTP診断を追加
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1352の実区間進捗・採用保存を接続
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1353の実完了工程と採用ACKの塗りを追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1360の話者区切り不整合を専用表示
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "app/localization.h"
#include "app/resource.h"
#include "core/api_text_provenance.h"
#include "core/api_text_speaker_turns.h"
#include "core/job_controller.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
namespace koefude {
/** @brief 区間応答と最終採用保存だけを数える有界な表示値です */
struct ApiTextStepProgress {
	/// 完全応答数と、成功した最終採用保存一件
	unsigned completed = 0 ;
	/// 計画区間数+1。0は総量不明または失敗終端
	unsigned total = 0 ;
	/// 完了工程割合。時間割合や待機内生成率ではありません
	int		 percent = 0 ;
} ;
/** @brief 自由文を格納せず言語切替後も再描画できるAPI状態です */
struct ApiTextStatusView {
	/// 固定説明resource
	UINT							  id = IDS_API_FAILED ;
	/// 診断でだけ併記するHTTPstatus
	unsigned						  http = 0 ;
	/// 元通信/構造code
	std::uint32_t					  code = 0 ;
	/// 固定分類
	ApiTextHttpError				  detail = ApiTextHttpError::UNKNOWN ;
	/// 以前の採用結果を保持
	bool							  priorAdopted = false ;
	/// 今回の実段階/区間数。旧保存へ追加しません
	ApiTextProgress					  progress ;
	/// 同opのmonotonic経過、終端で凍結
	std::uint64_t					  elapsedSeconds = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 実応答と採用保存ACKから工程割合を返します
	 * @return 不明/失敗は0、計画後は完了応答/(区間数+1)、採用保存成功だけ100
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] ApiTextStepProgress steps() const noexcept
	{
		const bool saved = id == IDS_API_ADOPTED_SAVED ; // 型付き耐久ACK後の専用成功
		// HTTP0の表示/保存成功を、過去の生成時計や区間数へ結び付けません
		if( progress.phase == ApiTextPhase::NONE) {
			return {0, 0,
					saved || id == IDS_API_ORIGINAL_SAVED || id == IDS_API_CORRECTED_SHOWN || id == IDS_API_SAVED ||
							id == IDS_API_DISCARDED
						? 100
						: 0} ;
		}
		// 不正な区間数や失敗・取消しは成功色の完了へ昇格しません
		if( !progress.totalUnits || progress.totalUnits > 32 || progress.completedUnits > progress.totalUnits ||
		   progress.currentUnit > progress.totalUnits || progress.phase > ApiTextPhase::COMPLETED ||
		   id == IDS_API_SAVE_FAILED) {
			return {} ;
		}
		const bool complete = progress.phase == ApiTextPhase::COMPLETED && saved &&
							  progress.completedUnits == progress.totalUnits ; // 最終ACK
		const unsigned total = progress.totalUnits + 1 ; // 最大33工程で加算/積は有界
		const unsigned completed = progress.completedUnits + (complete ? 1U : 0U) ; // 事実だけ
		return {completed, total, static_cast<int>( completed * 100U / total)} ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定resourceと数値だけを表示します
	 * @return 本文/秘密を含まない説明
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::wstring text() const
	{
		auto value = resource( id) ; // 現UI言語の所有値
		// API内部の生成率を作らず、応答と採用保存の工程割合を明示します
		if( progress.phase != ApiTextPhase::NONE) {
			const bool terminal = progress.phase == ApiTextPhase::COMPLETED ||
								  progress.phase == ApiTextPhase::FAILURE ||
								  progress.phase == ApiTextPhase::CANCELLED ; // 凍結対象
			// 終端診断はADR1351を保ち、進行中だけ固定段階へ置き換えます
			if( !terminal) {
				value = resource( IDS_API_PROGRESS_TITLE) ;
			}
			const auto work = steps() ; // 最後の採用保存を含む有界な工程数
			// 計画前と失敗終端には架空の工程総数や成功率を出しません
			if( work.total) {
				value += L" " + std::to_wstring( work.completed) + L"/" + std::to_wstring( work.total) +
						 resource( IDS_API_STEPS_COMPLETE) + L" ·" ;
			}
			// plan前の0/0は架空の総区間数として表示しません
			if( progress.totalUnits) {
				value += L" " + std::to_wstring( progress.completedUnits) + L"/" + std::to_wstring( progress.totalUnits) +
						 resource( progress.phase == ApiTextPhase::SAVING_RESPONSE ? IDS_API_UNITS_RECEIVED
																				  : IDS_API_UNITS_COMPLETE) ;
			}
			// 終端は上の専用診断を使い、採用保存待ちと完了を混同しません
			if( !terminal) {
				value += L" · " ;
				// 固定enumだけを日本語resourceへ対応させます
				switch( progress.phase) {
					case ApiTextPhase::PREPARING: // plan確認
						value += resource( IDS_API_PHASE_PREPARING) ;
						break ;
					case ApiTextPhase::AUTHORIZING: // 明示認証確認
						value += resource( IDS_API_PHASE_AUTHORIZING) ;
						break ;
					case ApiTextPhase::RESERVING: // 送信intentの保存
						value += resource( IDS_API_PHASE_RESERVING) ;
						break ;
					case ApiTextPhase::AWAITING: // 内部percentが分からない待機
						value += std::to_wstring( progress.currentUnit) + resource( IDS_API_PHASE_AWAITING) ;
						break ;
					case ApiTextPhase::SAVING_RESPONSE: // 応答実受信後の候補保存
						value += resource( IDS_API_PHASE_RESPONSE) ;
						break ;
					case ApiTextPhase::ADOPTING: // 最終耐久ACK前
						value += resource( IDS_API_PHASE_ADOPTING) ;
						break ;
					case ApiTextPhase::CANCELLING: // 協調取消し待ち
						value += resource( IDS_API_PHASE_CANCELLING) ;
						break ;
					default: // 終端/Noneは上で別扱い
						break ;
				}
			}
			const auto seconds = std::to_wstring( elapsedSeconds % 60) ; // 2桁の秒表示
			const auto minutes = std::to_wstring( elapsedSeconds / 60) ; // 長時間も切り捨てない
			value += L" · " + resource( IDS_API_ELAPSED) + (minutes.size() < 2 ? L"0" : L"") + minutes + L":" +
					 (seconds.size() < 2 ? L"0" : L"") + seconds ;
		}
		// 汎用HTTP失敗だけに実statusを付けます。429等はresource内に固定表記があります
		if( id == IDS_API_HTTP_FAILED && http) {
			value += L" (HTTP" + std::to_wstring( http) + L")" ;
		}
		// 元codeをASR用の復旧案内へ変換しません
		if( code) {
			value += L" [code=" + std::to_wstring( code) + L"]" ;
		}
		// 既知の分類だけを別説明として加え、Unknownを残高不足にしません
		if( detail > ApiTextHttpError::UNKNOWN && detail <= ApiTextHttpError::SERVICE_OVERLOADED) {
			value += L" " + resource( IDS_API_HTTP_DETAIL_RATE + static_cast<unsigned>( detail) - 1) ;
		}
		// 失敗・取消しでも以前の完全保存済み採用は保持します
		if( priorAdopted) {
			value += L" " + resource( IDS_API_PRIOR_RETAINED) ;
		}
		return value ;
	}
} ;
/** @brief 一件の行/op/sourceに閉じた非永続の進捗時計です。本文を検査しません */
struct ApiTextProgressClock {
	/// 対象行だけ
	std::uint64_t					  history = 0 ;
	/// 生成operationだけ
	std::uint64_t					  operation = 0 ;
	/// 同じ原文のowner
	std::shared_ptr<const Transcript> source ;
	/// 既受信数を待機でも保持する固定DTO
	ApiTextProgress					  value ;
	/// 実経過秒
	std::uint64_t					  elapsedSeconds = 0 ;
	/// 終端後はtickを停止
	bool							  active = false ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 受付済み操作を開始します
	 * @param[in] row 行ID
	 * @param[in] op 単調ID
	 * @param[in] owner 原文owner
	 * @param[in] now 実monotonic開始ms
	 */
	//------------------------------------------------------------------------------
	void begin( std::uint64_t row, std::uint64_t op, std::shared_ptr<const Transcript> owner, std::uint64_t now)
	{
		history = row ;
		operation = op ;
		source = std::move( owner) ;
		value = {ApiTextPhase::PREPARING, 0, 0, 0, now, now} ;
		elapsedSeconds = 0 ;
		active = true ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 実経過だけを更新します
	 * @param[in] now monotonic ms
	 * @return 表示秒が変化した時true
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool tick( std::uint64_t now)
	{
		// 終端や時計の不正逆行を過大な経過へ変換しません
		if( !active || now < value.startedMs) {
			return false ;
		}
		const auto seconds = (now - value.startedMs) / 1000 ; // 待機から率を作らない
		const bool changed = seconds > elapsedSeconds ; // 約1秒ごとの表示更新だけ
		elapsedSeconds = std::max( elapsedSeconds, seconds) ;
		return changed ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じopの実通知だけを受領します
	 * @param[in] event worker値
	 * @return 適用した時true。別op/不正固定値はfalse
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool receive( const JobEvent &event)
	{
		// workerの最初の通知より前に取消し/拒否された終端も同じopなら凍結します
		if( active && event.id == operation && event.terminal && !event.apiCatalog &&
		   event.progress.apiText.phase == ApiTextPhase::NONE) {
			value.observedMs = progressMonotonicMs() ;
			static_cast<void>( tick( value.observedMs)) ;
			value.phase = event.succeeded						? ApiTextPhase::COMPLETED
						  : event.error == ErrorKind::CANCELLED ? ApiTextPhase::CANCELLED
																: ApiTextPhase::FAILURE ;
			active = false ;
			return true ;
		}
		const auto &p = event.progress.apiText ; // 本文なしDTO
		// metadata、古いop、終端後の遅延、範囲外の値は混入させません
		if( !active || event.id != operation || event.apiCatalog || p.phase == ApiTextPhase::NONE ||
		   p.phase > ApiTextPhase::CANCELLED || p.totalUnits > 32 || p.completedUnits > p.totalUnits ||
		   p.currentUnit > p.totalUnits || !p.startedMs || p.observedMs < p.startedMs) {
			return false ;
		}
		const auto acceptedMs = value.startedMs ; // UI受付の実開始をworker待機でも戻さない
		value = p ;
		value.startedMs = acceptedMs ;
		static_cast<void>( tick( p.observedMs)) ;
		// 実終端時刻で凍結し、無応答timerだけでは完了にしません
		if( event.terminal) {
			active = false ;
		}
		return true ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ行とownerだけへ表示を許可します
	 * @param[in] row 行ID
	 * @param[in] owner 現原文
	 * @return 同じ所有値の時true
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool matches( std::uint64_t row, const std::shared_ptr<const Transcript> &owner) const
	{
		return operation && history == row && owner == source && !owner.owner_before( source) &&
			   !source.owner_before( owner) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じopの固定表示へ実進捗を付けます
	 * @param[in,out] status API専用表示
	 */
	//------------------------------------------------------------------------------
	void decorate( ApiTextStatusView &status) const
	{
		status.progress = value ;
		status.elapsedSeconds = elapsedSeconds ;
	}
} ;
//------------------------------------------------------------------------------
/**
 * @brief 保存状態または今回の所有終端を分類します
 * @param[in] record 保存値
 * @param[in] operation 0は再読、非0は同opだけを使います
 * @param[in] cancelled 今回の取消し
 * @param[in] code 今回の事前/通信code
 * @param[in] saveFailed 今回の保存失敗
 * @return API専用の固定表示値
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline ApiTextStatusView apiTextRecordStatus( const ApiTextCorrectionRecord &record,
														   std::uint64_t operation = 0, bool cancelled = false,
														   std::uint32_t code = 0, bool saveFailed = false)
{
	ApiTextStatusView view ; // 失敗既定、成功へ暗黙に昇格しません
	const auto		  saved = record.latest ? record.latest : record.adopted ; // 破棄後も前採用を保持
	const auto		  latest = saved && (!operation || saved->operationId == operation)
								   ? saved
								   : nullptr ; // 前試行のHTTPを今回へ流用しません
	view.priorAdopted = record.adopted && !sameApiTextAttempt( latest, record.adopted) ;
	view.code = code ;
	// 保存失敗は生成成功やHTTP失敗と分け、再送を案内しません
	if( saveFailed || (latest && record.saveFailed)) {
		view.id = IDS_API_SAVE_FAILED ;
		return view ;
	}
	// 明示取消しは取得済みHTTP/本文の有無と独立した終端です
	if( cancelled || (latest && latest->state == ApiTextState::CANCELLED)) {
		view.id = IDS_API_CANCELLED_STATUS ;
		return view ;
	}
	// typed結果のない事前拒否は古いlatestの原因を借りません
	if( !latest) {
		view.id = operation ? IDS_API_PREFLIGHT_FAILED : IDS_API_IDLE ;
		return view ;
	}
	view.code = code ? code : latest->code ;
	// 終端した最新応答のHTTPを旧Incompleteより優先します
	if( !latest->results.empty()) {
		const auto &last = latest->results.back() ; // 有限chunkの末尾
		view.http = last.httpStatus ;
		view.detail = last.httpError ;
		view.code = view.code ? view.code : last.code ;
		// 実HTTP拒否を本文の一部取得や音声失敗へ変換しません
		if( last.httpStatus && (last.httpStatus < 200 || last.httpStatus >= 300)) {
			view.id = last.httpStatus == 429   ? IDS_API_HTTP_429
					  : last.httpStatus == 401 ? IDS_API_HTTP_401
					  : last.httpStatus == 403 ? IDS_API_HTTP_403
					  : last.httpStatus == 400 ? IDS_API_HTTP_400
					  : last.httpStatus == 503 ? IDS_API_HTTP_503
											   : IDS_API_HTTP_FAILED ;
			return view ;
		}
		// 話者区切りを失った新候補を採用成功や通信失敗として表示しません
		if( last.code == K_API_TEXT_SPEAKER_STRUCTURE_ERROR) {
			view.id = IDS_API_SPEAKER_STRUCTURE_FAILED ;
			return view ;
		}
		// HTTP未到達・通信エラーは不完全生成と区別します
		if( !last.httpStatus || (last.code && last.code != ERROR_INVALID_DATA && last.code != ERROR_FILE_TOO_LARGE)) {
			view.id = IDS_API_TRANSPORT_FAILED ;
			return view ;
		}
	}
	// 完全保存済み採用だけを通常の完了として示します
	if( latest->state == ApiTextState::CANDIDATE_READY) {
		view.id = sameApiTextAttempt( latest, record.adopted) ? IDS_API_ADOPTED_SAVED : IDS_API_STATE_READY ;
		return view ;
	}
	// 2xx不完全生成でも本文0を部分取得とは呼びません
	if( latest->state == ApiTextState::INCOMPLETE) {
		view.id = latest->candidate.empty() ? IDS_API_EMPTY_GENERATION : IDS_API_PARTIAL_GENERATION ;
		return view ;
	}
	view.id = IDS_API_STATE_PENDING + static_cast<unsigned>( latest->state) ;
	return view ;
}
//------------------------------------------------------------------------------
/**
 * @brief 今回のJobController終端をAPI専用表示へ変換します
 * @param[in] event 同opへ照合済みの終端
 * @return 以前の試行を原因にしない表示
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline ApiTextStatusView apiTextEventStatus( const JobEvent &event)
{
	// HTTP0操作の受付IDは保存済み生成IDと別なので、送信系と分けます
	if( event.apiText && event.succeeded) {
		const auto action = event.apiText->request.action ; // 同じ明示操作
		// 表示・保存だけの成功を生成ID不一致で事前拒否にしません
		if( action != ApiTextAction::IMPROVE && action != ApiTextAction::SEND) {
			ApiTextStatusView view ; // 自由文を持たない成功状態
			view.id = action == ApiTextAction::ORIGINAL		? IDS_API_ORIGINAL_SAVED
					  : action == ApiTextAction::CORRECTED	? IDS_API_CORRECTED_SHOWN
					  : action == ApiTextAction::ADOPT		? IDS_API_ADOPTED_SAVED
					  : action == ApiTextAction::RETRY_SAVE ? IDS_API_SAVED
					  : action == ApiTextAction::DISCARD	? IDS_API_DISCARDED
					  : action == ApiTextAction::EXPORT		? IDS_API_SAVED
															: IDS_API_READY ;
			return view ;
		}
	}
	const auto record = event.apiText ? (event.apiText->persistence.operationId ? event.apiText->persistence.record
																				: event.apiText->request.record)
									  : ApiTextCorrectionRecord{} ; // typedなしも固定説明
	return apiTextRecordStatus( record, event.id, event.error == ErrorKind::CANCELLED, event.code,
							   event.error == ErrorKind::SAVE_FAILED) ;
}
} // namespace koefude
