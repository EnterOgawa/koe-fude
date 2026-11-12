//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file compact_status.h
 * @brief 常設状態を短い語句と実進捗だけへ変換します
 * @version 1.0.0
 * @date 2026/11/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 1.0.0 Shunji Ogawa 2026/11/10 新規作成
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1380の短い状態と幅に応じた補助情報を追加
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
#include "app/localization.h"
#include "app/resource.h"
#include "core/job_controller.h"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief 詳しい処理状態のresource IDを、常設status用の短い分類へ対応付けます
 * @param[in] id 元の状態resource ID。音声段階・全文補正段階・個別終端を受け付けます
 * @return 対応するIDS_SHORT_*のresource ID。未定義IDはIDS_SHORT_ATTENTIONです
 * @note 本文や翻訳済み文字列を解析せず、処理状態・完了判定を変更しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline UINT compactStatusResource( UINT id) noexcept
{
	// Stage順を維持し、本文を翻訳文字列から解析しません
	constexpr std::array STAGES{IDS_SHORT_VALIDATING, IDS_SHORT_DECODING,  IDS_SHORT_LOADING, IDS_SHORT_TRANSCRIBING,
								IDS_SHORT_DIARIZING,  IDS_SHORT_COMBINING, IDS_SHORT_SAVING,  IDS_SHORT_DONE} ;
	// 連続した音声段階IDだけを、同じ順序の短い段階名へ対応付ける
	if( id >= IDS_STAGES && id < IDS_STAGES + STAGES.size()) {
		return STAGES[id - IDS_STAGES] ;
	}
	// 全文補正の定義済み段階だけを対応付け、音声段階と同じ添字として扱わない
	if( id >= IDS_WHOLE_PHASES && id <= IDS_WHOLE_PHASE_VALIDATE) {
		constexpr std::array WHOLE{IDS_SHORT_VALIDATING, IDS_SHORT_TRANSCRIBING, IDS_SHORT_PREPARING,
								   IDS_SHORT_GENERATING, IDS_SHORT_VALIDATING} ;
		return WHOLE[id - IDS_WHOLE_PHASES] ;
	}
	// 同じ利用者向け状態を表す個別IDだけを集約し、詳細診断を常設statusへ混ぜない
	switch( id) {
		// 受付待ちの通常状態
		case IDS_READY:
			return IDS_SHORT_READY ;
		// 音声と整形の待機要求を同じキュー待ち表示へまとめる
		case IDS_QUEUED:
		case IDS_READING_QUEUED:
			return IDS_SHORT_QUEUED ;
		// 各処理で既に確定した正常終端を完了表示へまとめる
		case IDS_COMPLETED:
		case IDS_READING_DONE:
		case IDS_SPEAKER_TEXT_DONE:
			return IDS_SHORT_DONE ;
		// 利用可能な結果を保持する警告付き終端
		case IDS_WARNING:
			return IDS_SHORT_WARNING ;
		// 本文のない終端を通常の完了と区別する
		case IDS_NO_TEXT:
			return IDS_SHORT_NO_TEXT ;
		// 取消し要求後もworkerの終端を待っている状態
		case IDS_CANCELLING:
		case IDS_READING_CANCELLING:
			return IDS_SHORT_CANCELLING ;
		// 取消しの確定後を待機中表示と区別する
		case IDS_CANCELLED:
		case IDS_READING_CANCELLED:
		case IDS_SPEAKER_TEXT_CANCELLED:
			return IDS_SHORT_CANCELLED ;
		// 前回起動の未完操作を今回の失敗や完了へ読み替えない
		case IDS_HISTORY_INTERRUPTED:
		case IDS_READING_INTERRUPTED:
			return IDS_SHORT_INTERRUPTED ;
		// アプリ全体の終了に伴うworker排出待ち
		case IDS_STOPPING:
			return IDS_SHORT_STOPPING ;
		// 整形器の読込み中を生成中と区別する
		case IDS_READING_LOADING:
			return IDS_SHORT_LOADING ;
		// 整形本文の生成が進行している段階
		case IDS_READING_FORMATTING:
			return IDS_SHORT_GENERATING ;
		// 生成結果の検証が残っており、まだ完了として表示しない
		case IDS_READING_VALIDATING:
			return IDS_SHORT_VALIDATING ;
		// 整形・話者投影の実行前準備を同じ短い準備表示へまとめる
		case IDS_READING_PREPARING:
		case IDS_SPEAKER_TEXT_PREPARING:
			return IDS_SHORT_PREPARING ;
		// 音声API後の話者投影は話者処理として示す
		case IDS_SPEAKER_TEXT_ACTIVE:
			return IDS_SHORT_DIARIZING ;
		// 明示保存の確定を生成完了と区別する
		case IDS_SAVED:
		case IDS_TRANSCRIPTION_SAVED:
			return IDS_SHORT_SAVED ;
		// 履歴削除の確定を示す
		case IDS_HISTORY_REMOVED:
			return IDS_SHORT_REMOVED ;
		// 詳しい拒否理由は別通知へ残し、常設欄では処理失敗へまとめる
		case IDS_READING_FAILED:
		case IDS_READING_GENERATION_FAILED:
		case IDS_WHOLE_NO_PCM:
		case IDS_WHOLE_PCM_MISMATCH:
		case IDS_WHOLE_RESOURCES:
		case IDS_WHOLE_MODELS:
		case IDS_WHOLE_CPU_UNSUPPORTED:
		case IDS_WHOLE_RUNTIME_REJECTED:
		case IDS_WHOLE_RUNTIME_MISSING:
		case IDS_SPEAKER_TEXT_FAILED:
			return IDS_SHORT_FAILED ;
		// 対応を持たない状態を、根拠なく準備完了や成功へ変換しない
		default:
			return IDS_SHORT_ATTENTION ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief API文章改善の実段階を、常設status用の短いresource IDへ対応付けます
 * @param[in] phase 同じAPI操作が通知した処理段階
 * @return 対応するIDS_SHORT_*。未分類の段階はIDS_SHORT_PREPARINGです
 * @note 応答待ち・保存待ち・終端を区別し、区間数から完了を推測しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline UINT compactApiPhase( ApiTextPhase phase) noexcept
{
	// 実際のAPI段階だけを分類し、同じ保存待ちの段階は短い一語へ集約する
	switch( phase) {
		// 提供元の応答待ち。内部の生成率は分からない
		case ApiTextPhase::AWAITING:
			return IDS_SHORT_AWAITING ;
		// intent・応答・採用のいずれも、保存ACKを待つ段階
		case ApiTextPhase::RESERVING:
		case ApiTextPhase::SAVING_RESPONSE:
		case ApiTextPhase::ADOPTING:
			return IDS_SHORT_SAVING ;
		// 協調取消しを要求済みで、終端はまだ確定していない
		case ApiTextPhase::CANCELLING:
			return IDS_SHORT_CANCELLING ;
		// 採用保存まで確定した正常終端
		case ApiTextPhase::COMPLETED:
			return IDS_SHORT_DONE ;
		// 失敗終端。旧採用の有無とは独立した状態
		case ApiTextPhase::FAILURE:
			return IDS_SHORT_FAILED ;
		// 取消し終端を失敗や成功へまとめない
		case ApiTextPhase::CANCELLED:
			return IDS_SHORT_CANCELLED ;
		// 未通知・その他の段階を成功へ補完せず準備中として示す
		default:
			return IDS_SHORT_PREPARING ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief API文章改善の短い段階名へ、実区間数と同じ操作の経過時間を付けます
 * @param[in] text 基本の段階名。値として受け取り、呼出し元の文字列は変更しません
 * @param[in] progress 同じ操作から実際に受信した進捗DTO。呼出し中だけ読み取ります
 * @param[in] seconds 同じ操作の経過秒数。終端では呼出し側が凍結した値を渡します
 * @return 所有UTF-16文字列。総区間数がある時に完了数/総数を、進行段階では現在窓も付け、
 * None以外では経過を分:秒で付けます。百分率や残り時間は推測しません
 * @note DTOを変更せず、時計の読取り・通信・保存を行いません
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline std::wstring compactApiProgress( std::wstring text, const ApiTextProgress &progress,
													 std::uint64_t seconds)
{
	// 総数が分かる実操作だけに区間数を示し、未通知を0/0と表示しない
	if( progress.totalUnits) {
		text += L" " + std::to_wstring( progress.completedUnits) + L"/" + std::to_wstring( progress.totalUnits) ;
		// 進行中で実在する現在窓だけを補足し、終端に次の窓があるように見せない
		if( progress.phase > ApiTextPhase::NONE && progress.phase < ApiTextPhase::COMPLETED && progress.currentUnit) {
			text += L" (#" + std::to_wstring( progress.currentUnit) + L")" ;
		}
	}
	// 操作開始の通知がある時だけ、呼出し側が同じopで管理した経過を添える
	if( progress.phase != ApiTextPhase::NONE) {
		// 秒部分を二桁に整えるための0～59の十進表記
		const auto tail = std::to_wstring( seconds % 60) ;
		text += L" · " + std::to_wstring( seconds / 60) + L":" + (tail.size() < 2 ? L"0" : L"") + tail ;
	}
	return text ;
}
//------------------------------------------------------------------------------
/**
 * @brief 現段階を保持し、現在fontの実幅へ収まる補助状態だけを順に追加します
 * @tparam Measure 文字列を受け取り、同じfontでの横幅を返せる同期callable型
 * @param[in] primary 優先して残す現段階の所有UTF-16文字列。幅超過でも切り詰めません
 * @param[in] history 履歴の短い状態。空なら追加しません。呼出し中だけ借用します
 * @param[in] improvement 改善文の採用有無。空なら追加しません。呼出し中だけ借用します
 * @param[in] width 表示に使用できる横幅（pixel）。measureの戻り値と同じ単位です
 * @param[in] measure 候補全体の横幅を測る関数。呼出し中だけ参照し、保持しません
 * @return primaryに、全体幅がwidth以下となる補助項目だけを追加した所有文字列
 * history、improvementの順で独立に試し、収まらない項目は省きます
 * @note primary自体が超過する場合まで幅内を保証しません。measureや文字列確保の例外は伝播します
 */
//------------------------------------------------------------------------------
template <typename Measure>
[[nodiscard]] std::wstring fitCompactStatus( std::wstring primary, std::wstring_view history,
											std::wstring_view improvement, int width, const Measure &measure)
{
	// 狭い時は現段階を優先し、補助状態の一部だけを切って残しません
	for( const auto part : {history, improvement}) {
		// 空項目のために区切りだけを追加しない
		if( !part.empty()) {
			// 一項目を追加した時の全体候補。採用前に実font幅を測る
			auto candidate = primary + L" | " + std::wstring( part) ;
			// 項目の途中で切らず、区切りを含む全体が収まる場合だけ採用する
			if( measure( candidate) <= width) {
				primary = std::move( candidate) ;
			}
		}
	}
	return primary ;
}
} // namespace koefude
