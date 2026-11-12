//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_audio_status.h
 * @brief 音声APIだけの実進捗、部分状態、料金出所を表示します
 * @version 1.0.0
 * @date 2026/11/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の音声API固定表示を追加
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1370の文脈分割と明示途中再開を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1407の具体的な契約と処理意図を記載
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "app/api_catalog_time.h"
#include "app/localization.h"
#include "app/resource.h"
#include "core/api_audio_transcription.h"
#include <algorithm>
#include <string>
namespace koefude {
/** @brief 本文/名前/pathなしの音声API表示値です */
struct ApiAudioStatusView {
	/// 文章改善とは別の実段階
	ApiTextProgress			   progress ;
	/// 固定終端
	ApiAudioTerminal		   terminal = ApiAudioTerminal::PENDING ;
	/// liveと保存済みを区別
	bool					   ended = false ;
	/// 完全prefix本文あり
	bool					   prefix = false ;
	/// 今回はPOST0、旧HTTPと区別
	bool					   preparationFailed = false ;
	/// 未耐久prefixを成功へしません
	bool					   saveFailed = false ;
	/// HTTP0と実statusを区別
	unsigned				   http = 0 ;
	/// 元通信code
	std::uint32_t			   code = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief live段階または保存終端から、音声API専用の状態説明を作ります
	 * @return 固定resourceと実区間数・観測経過・診断値による所有UTF-16文字列
	 * 部分原文を完全成功と区別し、処理本文やファイル名は含めません
	 * @note この表示値を変更せず、通信・保存・進捗の推測を行いません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::wstring text() const
	{
		unsigned id = IDS_AUDIO_PREPARING ; // 準備中の既定表示
		// 保存済みの終端とHTTP原因を音声再生失敗から分離します
		if( ended) {
			id = preparationFailed								  ? IDS_AUDIO_RESUME_PREP_FAILED
				 : saveFailed									  ? IDS_AUDIO_SAVE_FAILED
				 : terminal == ApiAudioTerminal::COMPLETED		  ? IDS_AUDIO_DONE
				 : terminal == ApiAudioTerminal::NO_SPEECH		  ? IDS_AUDIO_NO_SPEECH
				 : terminal == ApiAudioTerminal::EMPTY_TRANSCRIPT ? IDS_AUDIO_EMPTY
				 : terminal == ApiAudioTerminal::CANCELLED		  ? IDS_AUDIO_CANCELLED
				 : terminal == ApiAudioTerminal::INTERRUPTED	  ? IDS_AUDIO_INTERRUPTED
				 : http == 401									  ? IDS_AUDIO_AUTH
				 : http == 403									  ? IDS_AUDIO_FORBIDDEN
				 : http == 404									  ? IDS_AUDIO_MODEL_MISSING
				 : http == 413									  ? IDS_AUDIO_SIZE
				 : http == 429									  ? IDS_AUDIO_LIMIT
				 : http >= 500									  ? IDS_AUDIO_SERVICE
																  : IDS_AUDIO_FAILED ;
		} else {
			// HTTP待機の架空%/残時間を作らず実stageだけを表示します
			switch( progress.phase) {
				case ApiTextPhase::RESERVING:
					id = IDS_AUDIO_INTENT ;
					break ; // POST前保存
				case ApiTextPhase::AWAITING:
					id = IDS_AUDIO_AWAITING ;
					break ; // 一回応答待ち
				case ApiTextPhase::SAVING_RESPONSE:
					id = IDS_AUDIO_PREFIX_SAVE ;
					break ; // 完全応答の保存
				case ApiTextPhase::ADOPTING:
					id = IDS_AUDIO_FINAL_SAVE ;
					break ; // 最終耐久ACK待ち
				case ApiTextPhase::CANCELLING:
					id = IDS_AUDIO_CANCELLING ;
					break ; // 協調停止待ち
				default:
					break ; // 未通知段階は準備中
			}
		}
		auto value = resource( id) ; // 現言語resource
		// 実planの区間数とmonotonic経過だけを追加します
		if( progress.totalUnits) {
			value += L"  " + std::to_wstring( progress.completedUnits) + L"/" + std::to_wstring( progress.totalUnits) ;
			// 完了応答保存中も現在の実区間番号を保持します
			if( !ended && progress.currentUnit) {
				value += L" (#" + std::to_wstring( progress.currentUnit) + L")" ;
			}
		}
		// 終端はobserved値で凍結し、壁時計から経過を作りません
		if( progress.startedMs && progress.observedMs >= progress.startedMs) {
			value += L"  " + std::to_wstring( (progress.observedMs - progress.startedMs) / 1000) +
					 resource( IDS_AUDIO_SECONDS) ;
		}
		// 部分原文を完全成功と区別します
		if( ended && prefix && terminal != ApiAudioTerminal::COMPLETED) {
			value += L" / " + resource( IDS_AUDIO_PARTIAL) ;
		}
		// 固定診断値だけを表示し自由文/bodyを持ち込みません
		if( ended && (http || code)) {
			value += L" [HTTP " + std::to_wstring( http) + L", " + std::to_wstring( code) + L"]" ;
		}
		return value ;
	}
} ;
//------------------------------------------------------------------------------
/**
 * @brief 保存済み音声APIの窓数・本文の有無・最終診断を、終端状態の表示値へ写します
 * @param[in] r 同じ履歴行の検証済みrecord。呼出し中だけ読み取り、本文を複製しません
 * @return endedがtrueの所有表示値。completeな窓を数え、空でない本文の有無を別に保持します
 * HTTPは最後のchunkの値で、chunkがなければ0です。新しい処理の成功判定は行いません
 * @pre plan窓数が表示型のuint32_tに収まり、recordが保存時の検証を通っていること
 * @note recordを変更せず、通信や保存を開始しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline ApiAudioStatusView apiAudioStatus( const ApiAudioRecord &r)
{
	ApiAudioStatusView view ; // 原文本文のcopyなし
	view.ended = true ;
	view.terminal = r.terminal ;
	view.saveFailed = r.saveFailed ;
	view.code = r.code ;
	view.progress.totalUnits = static_cast<std::uint32_t>( r.plan.windows.size()) ;
	// 完全応答数と本文有無を別々に数えます
	for( const auto &chunk : r.chunks) {
		// 不完全応答を成功数へ含めません
		if( chunk.complete) {
			++view.progress.completedUnits ;
		}
		view.prefix = view.prefix || !chunk.text.empty() ;
		view.http = chunk.http ;
	}
	return view ;
}
//------------------------------------------------------------------------------
/**
 * @brief 音声APIの送信区間・価格の出所・応答の観測値をtooltip用の説明へまとめます
 * @param[in] r 同じ履歴行の検証済み保存record。呼出し中だけ読み取り、所有権は取得しません
 * @return 本文やファイル名を含まない所有UTF-16文字列。概算と利用量未通知を区別し、
 * 過去attemptの単価と診断、当試行単価による未完窓の概算を含みます
 * @pre 各attemptの分単価が存在するなど、recordが保存時の検証を通っていること
 * @note 要求モデル名で実応答モデルを補完せず、通信・課金照会・保存を行いません
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline std::wstring apiAudioDetails( const ApiAudioRecord &r)
{
	// tooltip全体を所有する表示文。原文本文や入力pathを含めず、固定説明と数値だけを追加する
	auto text = resource( IDS_AUDIO_ORIGIN) + L" gpt-transcribe\r\n" + resource( IDS_AUDIO_TIME_BASIS) ;
	text += L"\r\n" +
			resource( r.plan.version == 2 ? IDS_AUDIO_CONTEXT_NOTICE : IDS_AUDIO_STANDARD_NOTICE) ; // 要求IDと実IDは別
	text += L"\r\n" + resource( IDS_AUDIO_SEND_DURATION) + L" " + std::to_wstring( r.plan.sendSamples / K_SAMPLE_RATE) +
			resource( IDS_AUDIO_SECONDS) + L" / " + std::to_wstring( r.attemptedSamples / K_SAMPLE_RATE) +
			resource( IDS_AUDIO_SECONDS) ;
	text +=
		L"\r\n" + resource( IDS_AUDIO_ESTIMATE) + L" $" + std::to_wstring( r.plan.estimateMicroUsd / 1'000'000) + L"." ;
	auto fraction = std::to_wstring( r.plan.estimateMicroUsd % 1'000'000) ; // microUSDを小数6桁で表示
	text += std::wstring( 6 - fraction.size(), L'0') + fraction ;
	text +=
		L" / UTC " + formatApiCatalogTime( r.plan.price.metadata.fetched) + L"\r\n" + resource( IDS_AUDIO_USAGE_UNKNOWN) ;
	bool   verified = !r.chunks.empty() ; // 要求IDで実応答を補完しません
	bool   unverified = false ; // 任意prefix通知は料金保留
	double observed = 0 ; // 通知されたdurationだけを合計
	bool   duration = false ; // 未通知を0秒と混同しません
	// 小さいmetadataだけを全実応答から集計します
	for( const auto &c : r.chunks) {
		verified = verified && c.modelReport == ApiAudioModelReport::VERIFIED ;
		unverified = unverified || c.modelReport == ApiAudioModelReport::UNVERIFIED ;
		// 未通知を0秒とみなさず、応答に実在するdurationだけを観測量へ加える
		if( c.usage.seconds) {
			observed += *c.usage.seconds ;
			duration = true ;
		}
	}
	// 全応答が実モデルを検証済みの場合だけ、検証済みと明示する
	if( verified) {
		text += L"\r\n" + resource( IDS_AUDIO_ACTUAL_VERIFIED) ;
	} else if( unverified) {
		text += L"\r\n" + resource( IDS_AUDIO_ACTUAL_UNVERIFIED) ;
	}
	// 一つでも通知を観測した場合だけ、実観測durationを説明へ含める
	if( duration) {
		text += L"\r\n" + resource( IDS_AUDIO_USAGE_OBSERVED) + std::to_wstring( observed) + resource( IDS_AUDIO_SECONDS) ;
	}
	// 過去価格は今回の単価で再計算せず操作ごとに表示します
	for( const auto &a : r.attempts) {
		text += L"\r\n" + resource( IDS_AUDIO_ATTEMPT) + L" " + std::to_wstring( a.operationId) + L" / " +
				std::to_wstring( a.keyRevision) + L" / [" + std::to_wstring( a.startIndex) + L"," +
				std::to_wstring( a.completeEnd) + L") / " + std::to_wstring( a.attemptedSamples / K_SAMPLE_RATE) +
				L" / " + std::to_wstring( *a.price.price.microPerMinute) + L" / " +
				formatApiCatalogTime( a.price.metadata.fetched) ;
		// 未完失敗/intentの課金不明を新しい成功で消しません
		if( a.intent || (a.failure && a.failure->billing != ApiTextBilling::NOT_SENT)) {
			text += L" / " + resource( IDS_AUDIO_USAGE_UNKNOWN) ;
		}
		// 過去操作の固定診断も現在のHTTPへ読み替えず保持します
		if( a.code || a.failure) {
			text += L" [HTTP " + std::to_wstring( a.failure ? a.failure->http : 0) + L", " +
					std::to_wstring( a.failure ? a.failure->code : a.code) + L"]" ;
		}
	}
	// 今回の未完了量だけを当操作の単価で概算し、過去の額を再計算しません
	if( !r.attempts.empty()) {
		std::uint64_t remaining = 0 ; // 非重複plan以下なのでoverflowしません
		// 成功prefixの次からplan終端までの未完窓を合算し、今後送信する音声量の概算に用いる
		for( std::size_t i = apiAudioCompletePrefix( r); i < r.plan.windows.size(); ++i) {
			remaining += r.plan.windows[i].end - r.plan.windows[i].begin ;
		}
		// 未完窓の音声量を当試行の分単価で計算したmicroUSD。既に発生した実請求額ではない
		const auto amount = estimateApiAudioCost( remaining, *r.attempts.back().price.price.microPerMinute) ;
		text +=
			L"\r\n" + resource( IDS_AUDIO_REMAINING) + L" $" + std::to_wstring( static_cast<double>( amount) / 1'000'000) ;
	}
	text += L"\r\nhttps://developers.openai.com/api/docs/pricing.md" ;
	return text ;
}
} // namespace koefude
