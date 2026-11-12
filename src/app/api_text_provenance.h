//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_text_provenance.h
 * @brief 保存出所を日本語resourceからUI専用に表示します
 * @version 1.0.0
 * @date 2026/11/08
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/08 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1346の要求/実応答表示を追加
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
#include "platform/win_helpers.h"
#include <string>
namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief 自由文を表示しないモデル名です
 * @param[in] id 検証済みID
 * @return UI文字列
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline std::wstring apiProvenanceDisplayId( ApiProvenanceId id)
{
	// 未通知・未検証・複数を現在選択と混同しません
	switch( id.model) {
		case ApiProvenanceModel::MISSING:
			return resource( IDS_API_MODEL_NOT_REPORTED) ; // 未通知
		case ApiProvenanceModel::UNVERIFIED:
			return resource( IDS_API_MODEL_NOT_VERIFIED) ; // 未検証
		case ApiProvenanceModel::MULTIPLE:
			return resource( IDS_API_MODEL_MULTIPLE) ; // chunk差
		default:
			return toWide( apiProvenanceModelName( id)) ; // 固定IDのみ
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 本文へ混ぜない出所説明を作ります
 * @param[in] attempt 保存snapshot
 * @param[in] adopted 採用済み本文か
 * @return 要求/実応答と成功状態
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline std::wstring apiTextProvenanceDisplay( const ApiTextAttempt &attempt, bool adopted)
{
	const auto source = apiTextProvenance( attempt) ; // live catalog不要
	return resource( IDS_API_PROVENANCE_MODEL) + L" OpenAI / " + apiProvenanceDisplayId( source.requested) + L" (" +
		   resource( IDS_API_REQUESTED_MODEL) + L") / " + resource( IDS_API_ACTUAL_MODEL) + L": " +
		   apiProvenanceDisplayId( source.actual) + L" / " +
		   resource( adopted ? IDS_API_PROVENANCE_ADOPTED : IDS_API_PROVENANCE_CANDIDATE) + L" / " +
		   resource( IDS_API_STATE_PENDING + static_cast<unsigned>( source.state)) ;
}
} // namespace koefude
