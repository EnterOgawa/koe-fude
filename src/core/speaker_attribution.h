//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file speaker_attribution.h
 * @brief 話者候補の根拠を元音声の推定範囲へ対応付けます
 * @version 1.0.0
 * @date 2026/09/11
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/11 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/11 逐次交代と推定重なりの理由・範囲を保持
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <cstddef>
#include <span>
#include <stop_token>

#include "core/types.h"

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief 本文・時刻・候補IDを変えず、各行へ候補の推定範囲と理由を付けます
 * @param[in,out] lines workerが所有する未公開行。成功時は全行のattributionを更新
 * @param[in] turns 元録音全体のengine cluster ID付き区間。本呼出し中だけ借用
 * @param[in] stop 各走査中に確認する協調的取消し
 * @throws OperationError 不正時刻・未知ID・比較/保存容量上限・取消し
 * @note 失敗時は一部のattributionが更新され得るため、呼出側は未公開結果全体を破棄します
 * 候補へ採用されていない微小turnは追加せず、元turnsも変更しません
 */
//------------------------------------------------------------------------------
void populateSpeakerAttribution( std::span<TranscriptLine> lines, std::span<const SpeakerTurn> turns,
								std::stop_token stop = {}) ;

//------------------------------------------------------------------------------
/**
 * @brief 種別と推定範囲、ID集合の整合性を確認します
 * @param[in] line 読取り専用の元行。旧履歴のUnavailableと空範囲も許可
 * @param[in,out] totalItems 同じjob内で検証済みの範囲数と範囲内ID数の合計
 * @throws OperationError 不正時刻・ID・種別、保存容量上限超過
 * @note 失敗時の合計は途中値です。本文を変換・分配せず、外部I/Oを行いません
 */
//------------------------------------------------------------------------------
void validateSpeakerAttribution( const TranscriptLine &line, std::size_t &totalItems) ;

} // namespace koefude
