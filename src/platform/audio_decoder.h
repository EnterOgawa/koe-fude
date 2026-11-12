//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file audio_decoder.h
 * @brief M4A/PCM16 WAV/Layer III MP3/native FLACのlocal decoder契約です
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 M4A優先のnative読込みを追加
 * 0.1.0 Shunji Ogawa 2026/10/02 一般入口を追加し、M4A専用契約を保持
 * 0.1.0 Shunji Ogawa 2026/10/02 MPEG-1/2 Layer III MP3の製品decodeを追加
 * 0.1.0 Shunji Ogawa 2026/10/02 native FLACと既知総数の実出力照合を追加
 * 0.1.0 Shunji Ogawa 2026/10/02 M4A内ALACの検証付きdecodeを追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <filesystem>
#include <stop_token>

#include "core/types.h"

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief local M4A/AAC・ALAC、PCM16 WAV、Layer III MP3、native FLACを16kHz monoへ変換します
 * @param[in] path 通常local file。WAVはmono/stereo・8～192kHz。呼出し中だけ参照します
 * @param[in] stop 協調的な取消しtoken
 * @param[in] progress worker上で受け取る段階通知。空を許可します
 * @return 元入力属性と部分読取り可能な所有PCM。原fileを変更しません
 * @throws OperationError 不正入力、上限/資源不足、未対応codec、decode/timeline異常、取消し
 * @note COM MTAを使用できるworker専用のblocking処理です。固定の録音時間制限は設けません
 * 途中失敗/取消しではPCMを返さず一時領域を解放します。UI threadで呼ばないこと
 * MP3はMPEG-1/2・mono/stereo・規定6rateとID3外枠/frame範囲を検証します
 * FLACは16/24bit・mono/stereo・8～192kHz・既知の正の総数を検証します
 * FLAC出力は16kHzで総数一致、他rateで理想数から上下2sampleまでです
 * ALACは単一track/version0・16/24bit・mono/stereo・8～48kHzと全packet範囲を検証します
 * ALAC出力は16kHzで総数一致、他rateで理想ceilから上下2sampleまでです
 * 圧縮payload/CRCの完全性や許容差内のPCM欠落を保証しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] AudioData decodeInputAudio( const std::filesystem::path &path, std::stop_token stop,
										 const ProgressCallback &progress = {}) ;

//------------------------------------------------------------------------------
/**
 * @brief local M4Aを16kHz mono float PCMへ変換します
 * @param[in] path 読取り対象。local通常fileのみで、呼出し中は参照可能なこと
 * @param[in] stop 協調的な取消しtoken
 * @param[in] progress worker上で受け取る段階通知。空を許可
 * @return 所有PCMと入力属性。原fileは変更しません
 * @throws OperationError 不正入力、上限超過、未対応codec、decode失敗、取消し
 * @note blocking処理です。COM MTAを使用できるworkerから呼び出します
 * AACの従来条件と、上記ALACの検証/出力数条件を使います
 */
//------------------------------------------------------------------------------
[[nodiscard]] AudioData decodeM4a( const std::filesystem::path &path, std::stop_token stop,
								  const ProgressCallback &progress = {}) ;

} // namespace koefude
