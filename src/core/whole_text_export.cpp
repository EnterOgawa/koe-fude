//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_export.cpp
 * @brief 証拠付き全文を本文無編集でatomic保存します
 * @version 1.0.0
 * @date 2026/10/19
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/19 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/19 時刻や話者を付加しない専用TXTを追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
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
#include "core/whole_text_export.h"
#include "core/transcript_export.h"

namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief 本文を複製せず借用します
 * @param[in] request 所有snapshot
 * @return request寿命内だけ有効な無編集本文
 * @see koefude::viewWholeTextExport
 */
//------------------------------------------------------------------------------
std::string_view viewWholeTextExport( const WholeTextExportJobRequest &request)
{
	// 二方式の混在と未保存prefixを出力へ渡しません
	if( !request.source || static_cast<bool>( request.corrected) == static_cast<bool>( request.bounded)) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
	if( request.bounded) {
		const auto &d = request.bounded->descriptor() ; /* 不変証拠 */
		if( !request.bounded->complete() ||
		   d.result.request.source.sha256 != fingerprintReadingSource( *request.source) ||
		   !request.source->pcmIdentity || *request.source->pcmIdentity != d.result.pcm) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
		return request.bounded->text() ;
	}
	validatePersistedWholeTextCorrection( *request.source, request.corrected->source, *request.corrected) ;
	return request.corrected->text ;
}
//------------------------------------------------------------------------------
/**
 * @brief workerで証拠を照合して本文全byteを返します
 * @param[in] request 所有snapshot
 * @return 前後に付加しないUTF-8全文
 * @throws OperationError 証拠/束縛不正、容量超過
 * @see koefude::renderWholeTextExport
 */
//------------------------------------------------------------------------------
std::string renderWholeTextExport( const WholeTextExportJobRequest &request)
{
	return std::string( viewWholeTextExport( request)) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 既存atomic writerで明示TXTを保存します
 * @param[in] request 所有snapshot、拡張子はTXTだけ
 * @throws OperationError 検証/保存失敗。旧出力を保持します
 * @see koefude::saveWholeTextExport
 */
//------------------------------------------------------------------------------
void saveWholeTextExport( const WholeTextExportJobRequest &request)
{
	saveRenderedTranscript( viewWholeTextExport( request), request.output, ExportFormat::PLAIN_TEXT) ;
}
} // namespace koefude
