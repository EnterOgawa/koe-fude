//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file compact_table_status.h
 * @brief 一覧状態を型付き値から記号と短い実応答モデルへ変換します
 * @version 1.0.0
 * @date 2026/11/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1358の純粋表示分類を追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1360の話者区切り不整合を失敗記号へ対応
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1365の耐久採用有無を直近operationから独立した二値へ分類
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "app/api_text_provenance.h"
#include "app/api_text_status.h"
#include "app/resource.h"
#include "core/types.h"
#include <cstdint>
#include <string>
#include <windows.h>

namespace koefude {
/** @brief 色や翻訳文字列へ依存しない一覧の表示分類です */
enum class CompactTableState {
	/// 未実行または待機
	IDLE,
	/// 実処理または協調取消し待ち
	ACTIVE,
	/// 認識完了または耐久採用ACK済み
	COMPLETE,
	/// 警告・未完・候補・未知
	WARNING,
	/// 診断付き失敗
	FAILURE,
	/// 取消し・中断
	STOPPED,
	/// 認識本文なし
	NO_TEXT,
	/// HTTP0の表示・保存操作成功
	DISPLAY_SAVED
} ;
//------------------------------------------------------------------------------
/**
 * @brief 固定形状を返します
 * @param[in] state 表示分類
 * @return ＭＳ ゴシックで使える記号、未知分類は警告
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline std::wstring compactTableSymbol( CompactTableState state)
{
	// 色だけで判別させず、形状と詳細説明を併用します
	switch( state) {
		case CompactTableState::IDLE: // 未実行/待機
			return L"○" ;
		case CompactTableState::ACTIVE: // 段階は詳細へ保持
			return L"…" ;
		case CompactTableState::COMPLETE: // 確定した成功だけ
			return L"●" ;
		case CompactTableState::FAILURE: // 警告と区別
			return L"×" ;
		case CompactTableState::STOPPED: // 成功へ昇格しない
			return L"■" ;
		case CompactTableState::NO_TEXT: // 無音/本文なし
			return L"−" ;
		case CompactTableState::DISPLAY_SAVED: // 補正生成完了とは別
			return L"□" ;
		default: // 警告と未確認
			return L"△" ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 既存の認識状態IDを分類します
 * @param[in] id 固定状態resource、0は失敗
 * @param[in] error id=0時の型付き診断
 * @return 翻訳に依存しない分類
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline CompactTableState compactAsrState( UINT id, ErrorKind error)
{
	// 実段階は完了通知まで成功にしません
	if( id >= IDS_STAGES && id <= IDS_STAGES + static_cast<UINT>( Stage::COMPLETED)) {
		return CompactTableState::ACTIVE ;
	}
	// 型付きresourceだけで状態を判定します
	switch( id) {
		case IDS_READY: // 未実行
		case IDS_QUEUED: // 受付待機
			return CompactTableState::IDLE ;
		case IDS_CANCELLING: // 同期処理の終了待ち
			return CompactTableState::ACTIVE ;
		case IDS_COMPLETED: // 認識の確定結果
			return CompactTableState::COMPLETE ;
		case IDS_WARNING: // 本文を保持する警告
			return CompactTableState::WARNING ;
		case IDS_NO_TEXT: // 認識本文なし
			return CompactTableState::NO_TEXT ;
		case IDS_CANCELLED: // 保存済み取消し
		case IDS_HISTORY_INTERRUPTED: // 再読した中断
			return CompactTableState::STOPPED ;
		case 0: // live取消しもErrorKindのまま判定
			return error == ErrorKind::CANCELLED ? CompactTableState::STOPPED : CompactTableState::FAILURE ;
		default: // 未知を成功と呼びません
			return CompactTableState::WARNING ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief APIの実phaseと専用終端を分類します
 * @param[in] view 同opのtyped表示値
 * @return activeは初期Failed IDより優先、HTTP0成功は生成完了と区別
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline CompactTableState compactApiState( const ApiTextStatusView &view)
{
	// workerの最初の通知前も、同opの実進捗を失敗と誤表示しません
	if( view.progress.phase > ApiTextPhase::NONE && view.progress.phase < ApiTextPhase::COMPLETED) {
		return CompactTableState::ACTIVE ;
	}
	// 凍結した取消しは既定IDより優先します
	if( view.progress.phase == ApiTextPhase::CANCELLED) {
		return CompactTableState::STOPPED ;
	}
	// 成功通知だけで未採用候補や保存失敗を完了へ昇格しません
	switch( view.id) {
		case IDS_API_IDLE: // 試行なし
		case IDS_API_READY: // 準備のみ
		case IDS_API_STATE_PENDING: // 未送信準備
			return CompactTableState::IDLE ;
		case IDS_API_PREPARING: // 受付直後
		case IDS_API_AWAITING: // 実応答待ち
		case IDS_API_PERSISTING: // 実保存待ち
		case IDS_API_WORKING: // 実処理
			return CompactTableState::ACTIVE ;
		case IDS_API_ADOPTED_SAVED: // 耐久採用ACK専用
			return CompactTableState::COMPLETE ;
		case IDS_API_ORIGINAL_SAVED: // HTTP0の原文選好保存
		case IDS_API_CORRECTED_SHOWN: // 保存済み本文表示
		case IDS_API_SAVED: // 同じ結果の保存
		case IDS_API_DISCARDED: // 候補破棄
			return CompactTableState::DISPLAY_SAVED ;
		case IDS_API_STATE_CANCELLED: // 保存済み取消し
		case IDS_API_CANCELLED_STATUS: // live取消し
		case IDS_API_STATE_INTERRUPTED: // 再読した中断
			return CompactTableState::STOPPED ;
		case IDS_API_STATE_SENDING: // 旧送信intentは実稼働を意味しません
		case IDS_API_STATE_READY: // 未採用候補
		case IDS_API_STATE_INCOMPLETE: // 旧不完全候補
		case IDS_API_EMPTY_GENERATION: // 本文0
		case IDS_API_PARTIAL_GENERATION: // 部分本文
			return CompactTableState::WARNING ;
		case IDS_API_FAILED: // 専用失敗
		case IDS_API_SPEAKER_STRUCTURE_FAILED: // 新応答の発言区切り不整合
		case IDS_API_STATE_FAILED: // 保存済み失敗
		case IDS_API_SAVE_FAILED: // 保存失敗
		case IDS_API_PREFLIGHT_FAILED: // 事前拒否
		case IDS_API_TRANSPORT_FAILED: // 通信失敗
		case IDS_API_HTTP_FAILED: // 他HTTP拒否
		case IDS_API_HTTP_400: // 要求拒否
		case IDS_API_HTTP_401: // 認証拒否
		case IDS_API_HTTP_403: // 権限拒否
		case IDS_API_HTTP_429: // 利用制限
		case IDS_API_HTTP_503: // サービス失敗
			return CompactTableState::FAILURE ;
		default: // 未知を成功へ補完しません
			return CompactTableState::WARNING ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 現在原文の耐久採用だけを改善有無へ分類します
 * @param[in] record 再読のkey検証または同owner/keyの保存ACKを受けたrecord
 * @param[in] historyId 現在行の安定ID。0は未登録
 * @return 完全な採用本文ありはComplete、なしはIdleだけを返します
 * @pre 原文変更時は既存のrecord無効化を行い、別原文の値を渡しません
 * @note 最新試行/進捗/保存失敗/表示意図は前の耐久採用を取り消しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline CompactTableState compactImprovementState( const ApiTextCorrectionRecord &record,
															   std::uint64_t				  historyId) noexcept
{
	return historyId != 0 && record.adopted && record.adopted->draft.source.historyId == historyId &&
				   record.adopted->state == ApiTextState::CANDIDATE_READY && !record.adopted->candidate.empty()
			   ? CompactTableState::COMPLETE
			   : CompactTableState::IDLE ; // 未採用の完全候補も成功へ数えません
}
//------------------------------------------------------------------------------
/**
 * @brief 採用本文の生成元を優先して実応答モデルだけを表示します
 * @param[in] record 保存済み試行
 * @return 実ID、—、未確認、複数。要求名で補完しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline std::wstring compactApiModel( const ApiTextCorrectionRecord &record)
{
	const auto saved = record.adopted ? record.adopted : record.latest ; // 表示本文の出所優先
	// 実績なしは未通知です
	if( !saved) {
		return L"—" ;
	}
	const auto actual = apiTextProvenance( *saved).actual ; // 保存値だけ、catalog不要
	// 未通知・未知・複数を短く明示します
	switch( actual.model) {
		case ApiProvenanceModel::MISSING: // 要求モデルを借りません
			return L"—" ;
		case ApiProvenanceModel::UNVERIFIED: // 既知ID以外
			return resource( IDS_TABLE_MODEL_UNVERIFIED) ;
		case ApiProvenanceModel::MULTIPLE: // 区間間で異なります
			return resource( IDS_TABLE_MODEL_MULTIPLE) ;
		default: // 検証済みIDと実日付
			return apiProvenanceDisplayId( actual) ;
	}
}
} // namespace koefude
