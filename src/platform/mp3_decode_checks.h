//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file mp3_decode_checks.h
 * @brief MP3のnative属性と実PCM数の停止条件を宣言します
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 圧縮frameと実出力の照合契約を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <cstdint>

#include "platform/mp3_format.h"

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief 検証済みframeとMFのnative属性を照合します
 * @param[in] info parser成功時のmetadata。呼出し中だけ借用します
 * @param[in] rate MFが解釈したsample周波数（Hz）
 * @param[in] channels MFが解釈したchannel数
 * @param[in] align 圧縮streamのblock alignment（byte）
 * @throws OperationError 不一致はDecodeFailed。入力metadataを変更しません
 * @note Audio major typeとMP3 subtypeはCOM境界で別途検証します
 */
//------------------------------------------------------------------------------
void validateMp3NativeFormat( const Mp3InputInfo &info, std::uint32_t rate, std::uint32_t channels, std::uint32_t align) ;

//------------------------------------------------------------------------------
/**
 * @brief 追記予定の実PCM数がnominalより過大でないことを確認します
 * @param[in] info parser成功時のmetadata。呼出し中だけ借用します
 * @param[in] actual 追記後の16kHz mono sample数。途中の0を許可します
 * @throws OperationError nominalより1,600 sampleを超える場合InvalidTimeline
 * @note 大小で減算を選び、nominalへの加算overflowを避けます
 */
//------------------------------------------------------------------------------
void validateMp3OutputUpperBound( const Mp3InputInfo &info, std::uint64_t actual) ;

//------------------------------------------------------------------------------
/**
 * @brief 終端の実PCM数をdelay/paddingを含む保守的な停止条件へ照合します
 * @param[in] info parser成功時のmetadata。呼出し中だけ借用します
 * @param[in] actual 成功候補の16kHz mono sample数。空出力は許可しません
 * @throws OperationError 空PCM/不正属性はDecodeFailed、数差超過はInvalidTimeline
 * @note 不足許容はceil((8190 + frame_samples) * 16000 / rate) + 1600です
 * gapless補正や音声payload/許容内の欠落を保証する条件ではありません
 */
//------------------------------------------------------------------------------
void validateMp3OutputSamples( const Mp3InputInfo &info, std::uint64_t actual) ;

} // namespace koefude
