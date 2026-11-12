//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file input_messages.h
 * @brief 音声入力の選択形式と失敗理由の表示先を定義します
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 WAV接続に伴う入力理由の個別表示を追加
 * 0.1.0 Shunji Ogawa 2026/10/02 検証済みMP3を音声選択filterへ追加
 * 0.1.0 Shunji Ogawa 2026/10/02 検証済みnative FLACを音声選択filterへ追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include "app/resource.h"
#include "core/types.h"

namespace koefude {

inline constexpr wchar_t		 K_AUDIO_INPUT_PATTERN[] = L"*.m4a;*.wav;*.mp3;*.flac" ; // 製品音声の選択拡張子

//------------------------------------------------------------------------------
/**
 * @brief 入力の失敗分類を理由別の案内へ対応付けます
 * @param[in] kind 元の処理失敗分類。番号の順序には依存しません
 * @return 入力失敗ならresource ID、それ以外は0。本文やpathは含みません
 */
//------------------------------------------------------------------------------
[[nodiscard]] constexpr unsigned inputErrorResource( const ErrorKind kind) noexcept
{
	// 入力とモデル/推論の失敗を区別し、対応可能な復旧操作を示す
	switch( kind) {
		case ErrorKind::INVALID_INPUT:
			return IDS_INVALID ; // local通常fileではない入力
		case ErrorKind::INPUT_TOO_LARGE:
			return IDS_INPUT_LIMIT ; // メモリ・形式・整数の上限
		case ErrorKind::UNSUPPORTED_CODEC:
			return IDS_INPUT_CODEC ; // 未対応container/codec/音声属性
		case ErrorKind::DECODE_FAILED:
			return IDS_INPUT_DECODE ; // 破損や形式不整合による読込失敗
		case ErrorKind::INVALID_TIMELINE:
			return IDS_INPUT_TIMELINE ; // 音声時刻の不連続や出力長の不一致
		default:
			return 0 ; // 入力以外は既存の呼出し側の案内を使う
	}
}

} // namespace koefude
