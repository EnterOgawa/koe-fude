//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file reading_export.cpp
 * @brief 元行と時刻・話者を保持した整形TXTを構築します
 * @version 1.0.0
 * @date 2026/10/04
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/04 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/04 所有snapshotから選択版を保存する境界を追加
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372の独立話者投影と元原文保持を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "core/reading_export.h"
#include "core/speaker_text_record.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace koefude {
namespace {
//------------------------------------------------------------------------------
/**
 * @brief 全文上限を加算前に守ります
 * @param[in,out] output 所有全文
 * @param[in] text 追加するUTF-8 borrow
 */
//------------------------------------------------------------------------------
void appendText( std::string &output, const std::string_view text)
{
	// 時刻・名前・注記の増加も含めた既存の出力上限を守る
	if( text.size() > 4 * K_MAXIMUM_TEXT_BYTES - output.size()) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
	output.append( text) ;
}
} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 固定した版の全文をUTF-8へ構築します
 * @param[in] request 所有source/overlayを持つ保存要求。変更しません
 * @return 原文の既存byte列、または元時刻/話者を保つ整形TXT
 * @throws OperationError 不整合なsource、未対応形式、容量超過
 * @note worker限定です。元音声・モデルを開きません
 * @see koefude::renderReadingExport
 */
//------------------------------------------------------------------------------
std::string renderReadingExport( const ExportJobRequest &request)
{
	// 明示専用操作だけにlabelを出力し、通常raw/copy/字幕の契約を変えない
	if( request.speakerText) {
		// 明示話者出力は同sourceのTXTだけに限定する
		if( !request.source || !request.speakerAudio || request.format != ExportFormat::PLAIN_TEXT) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
		return renderSpeakerTextExport( *request.source, *request.speakerAudio, *request.speakerText) ;
	}
	// 不明な版と原文なしは出力先へ書き込む前に拒否する
	if( !request.source || static_cast<std::uint32_t>( request.display) > 1) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// 原文は既存rendererの全byteと字幕cueをそのまま使用する
	if( request.display == ReadingDisplay::ORIGINAL) {
		return renderTranscript( *request.source, request.format) ;
	}
	// 補正語句の発話時刻を推定した字幕を作らない
	if( request.format != ExportFormat::PLAIN_TEXT) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	const auto	view = makeReadingView( request.source, request.sourceKey, request.formatted,
									   ReadingDisplay::FORMATTED) ; // 借用の寿命を持つ所有投影
	std::size_t changed = 0 ; // 変更を含む元行数
	std::size_t retained = 0 ; // 理由付きで原文を保持した行数
	// 全行の採用状態を本文と別の件数として示す
	for( const auto &line : view.lines()) {
		changed += line.kind == ReadingLineKind::REPLACED ? 1 : 0 ;
		retained += line.kind == ReadingLineKind::ORIGINAL_RETAINED ? 1 : 0 ;
	}
	std::string output = "整形文（時刻・話者は原文に基づく）\n変更行: " + std::to_string( changed) +
						 " / 原文保持行: " + std::to_string( retained) + "\n\n" ; // hashや内部profileは本文へ出さない
	// 既存投影のgroupを使い、別話者の行を跨いで本文を連結しない
	for( const auto &group : view.groups()) {
		const auto &first = view.lines()[group.lineIndices.front()] ; // 元話者/注記の参照行
		appendText( output, "[" + formatTimestamp( group.startMs) + " - " + formatTimestamp( group.endMs) + "] " +
							   formatSpeakerLabel( *first.original, view.source()) + "\n") ;
		// 元行順と各行の改行を保持して置換/原文を投影する
		for( const auto index : group.lineIndices) {
			appendText( output, view.lines()[index].text) ;
			appendText( output, "\n") ;
		}
		const auto annotation = formatAttributionRanges( *first.original, view.source()) ; // 元注記
		// 不明/混在の既存時間範囲は補正文と別の注記として残す
		if( !annotation.empty()) {
			appendText( output, annotation) ;
			appendText( output, "\n") ;
		}
		appendText( output, "\n") ;
	}
	return output ;
}

//------------------------------------------------------------------------------
/**
 * @brief 受付snapshotを既存atomic writerで保存します
 * @param[in] request 版・対象を固定した所有要求
 * @throws OperationError 出力不正/生成失敗/書込み失敗。既存出力を保持
 * @see koefude::saveReadingExport
 */
//------------------------------------------------------------------------------
void saveReadingExport( const ExportJobRequest &request)
{
	const auto contents = renderReadingExport( request) ; // 固定した受付版だけの全文
	saveRenderedTranscript( contents, request.output, request.format) ;
}
} // namespace koefude
