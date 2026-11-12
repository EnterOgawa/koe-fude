//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file transcript_export.h
 * @brief 文字起こしの明示保存と表示文字列の生成を宣言します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 TXT/SRT/VTTの出力契約を追加
 * 0.1.0 Shunji Ogawa 2026/09/11 話者候補の理由と推定範囲を本文と区別して保存
 * 0.1.0 Shunji Ogawa 2026/10/04 構築済み全文を同じatomic writerで保存する境界を追加
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
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/pipeline.h"

namespace koefude {

/** @brief 利用者が選ぶ保存形式です */
enum class ExportFormat {
	/// 時刻と話者を含むUTF-8本文
	PLAIN_TEXT,
	/// SubRip字幕
	SRT,
	/// WebVTT字幕
	VTT
} ;

//------------------------------------------------------------------------------
/**
 * @brief 元音声の時刻を固定形式へ変換します
 * @param[in] milliseconds 0以上の時刻（ms）
 * @param[in] comma trueはSRTの小数点表記
 * @return HH:MM:SS.mmmまたはHH:MM:SS,mmm
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string				  formatTimestamp( std::int64_t milliseconds, bool comma = false) ;

//------------------------------------------------------------------------------
/**
 * @brief 連続する同じ一人の発言を改行でまとめます
 * @param[in] lines 元の時刻付き区間。変更しません
 * @return 表示とTXT用の所有区間。不明・混在・別話者を跨いで結合しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<TranscriptLine> groupConsecutiveSpeech( std::span<const TranscriptLine> lines) ;

//------------------------------------------------------------------------------
/**
 * @brief 話者番号と明示編集名から表示labelを作ります
 * @param[in] line 表示する区間
 * @param[in] transcript このfileの編集名mapを持つ結果
 * @return 帰属未確定、交代、推定重なり、理由未記録を区別するUTF-8 label
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string				  formatSpeakerLabel( const TranscriptLine &line, const Transcript &transcript) ;

//------------------------------------------------------------------------------
/**
 * @brief 複数候補が有効な推定時間範囲を、本文と区別した注記へ変換します
 * @param[in] line 検証済みの推定情報を持つ区間
 * @param[in] transcript このfileで利用者が指定した表示名
 * @return 所有UTF-8注記。複数候補の根拠を記録していない行は空文字列
 * @throws OperationError 注記の出力容量上限超過
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string formatAttributionRanges( const TranscriptLine &line, const Transcript &transcript) ;

//------------------------------------------------------------------------------
/**
 * @brief 表示とcopyに使うUTF-8文字列を生成します
 * @param[in] transcript 所有結果の借用参照
 * @param[in] format 出力形式
 * @return 所有UTF-8文字列
 * @throws OperationError 未定義形式・出力上限超過
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string renderTranscript( const Transcript &transcript, ExportFormat format) ;

//------------------------------------------------------------------------------
/**
 * @brief 明示された出力先へ一時fileから置き換えて保存します
 * @param[in] transcript 変更しない所有結果
 * @param[in] output 利用者が選んだ出力path。音声拡張子は禁止
 * @param[in] format 保存形式
 * @throws OperationError path不正、書込み失敗、上限超過
 * @note blocking処理です。workerで実行し、入力音声へ書き込みません
 */
//------------------------------------------------------------------------------
void saveTranscript( const Transcript &transcript, const std::filesystem::path &output, ExportFormat format) ;

//------------------------------------------------------------------------------
/**
 * @brief 構築済みUTF-8全文を原文と同じatomic writerで保存します
 * @param[in] contents 呼出し中だけ借用する全文。32MiB以内
 * @param[in] output 明示出力先。選択形式の拡張子を要求
 * @param[in] format 構築済み本文の形式
 * @throws OperationError 形式/Unicode/容量/書込み失敗。旧出力を保持
 * @note worker限定。原音声やengineを参照しません
 */
//------------------------------------------------------------------------------
void saveRenderedTranscript( std::string_view contents, const std::filesystem::path &output, ExportFormat format) ;

} // namespace koefude
