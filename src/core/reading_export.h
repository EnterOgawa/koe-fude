//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file reading_export.h
 * @brief 原文または整形文を固定した所有export要求を定義します
 * @version 1.0.0
 * @date 2026/10/04
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/04 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/04 選択版と原文/置換の所有権を保存要求へ固定
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372の独立話者投影と元原文保持を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/reading_view.h"
#include "core/transcript_export.h"
#include <filesystem>
#include <memory>
#include <string>

namespace koefude {
struct SpeakerTextProjectionRecord ; // 独立した検証済み採用
struct ApiAudioRecord ; // 元API recordのimmutable owner
/** @brief 受付後の表示切替に影響されない所有保存要求です */
struct ExportJobRequest {
	/// 利用者が明示した出力先
	std::filesystem::path							   output ;
	/// 不変の元結果
	std::shared_ptr<const Transcript>				   source ;
	/// 整形版だけで照合する原文key
	ReadingSourceKey								   sourceKey ;
	/// 任意の所有置換結果
	std::shared_ptr<const FormattedTranscript>		   formatted ;
	/// 受付時に解決した表示版
	ReadingDisplay									   display = ReadingDisplay::ORIGINAL ;
	/// 整形版はTextだけを許可
	ExportFormat									   format = ExportFormat::PLAIN_TEXT ;
	/// 明示話者付きTXTだけ
	std::shared_ptr<const SpeakerTextProjectionRecord> speakerText ;
	/// 同じ元API
	std::shared_ptr<const ApiAudioRecord>			   speakerAudio ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 固定した版の全文をUTF-8へ構築します
 * @param[in] request 所有source/overlayを持つ保存要求。変更しません
 * @return 原文の既存byte列、または元時刻/話者を保つ整形TXT
 * @throws OperationError 不整合なsource、未対応形式、容量超過
 * @note worker限定です。元音声・モデルを開きません
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string renderReadingExport( const ExportJobRequest &request) ;

//------------------------------------------------------------------------------
/**
 * @brief 受付snapshotを既存atomic writerで保存します
 * @param[in] request 版・対象を固定した所有要求
 * @throws OperationError 出力不正/生成失敗/書込み失敗。既存出力を保持
 */
//------------------------------------------------------------------------------
void					  saveReadingExport( const ExportJobRequest &request) ;
} // namespace koefude
