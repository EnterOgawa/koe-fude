//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_export.h
 * @brief 明示操作の補正全文TXTを不変snapshotへ固定します
 * @version 1.0.0
 * @date 2026/10/19
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/19 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/19 本文無編集の専用exportを追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/whole_text_correction.h"
#include "core/whole_text_persistence.h"
#include <filesystem>
#include <memory>
#include <string>

namespace koefude {
/** @brief 表示切替や再生成から独立した専用TXT要求です */
struct WholeTextExportJobRequest {
	/// 明示選択したTXT出力先
	std::filesystem::path						output ;
	/// 不変の元結果
	std::shared_ptr<const Transcript>			source ;
	/// 証拠付き不変全文
	std::shared_ptr<const WholeTextCorrection>	corrected ;
	/// 保存済みCompleteだけ
	std::shared_ptr<const WholeTextBoundedText> bounded ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 本文を複製せず借用します
 * @param[in] request 所有snapshot
 * @return request寿命内だけ有効な無編集本文
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string_view viewWholeTextExport( const WholeTextExportJobRequest &request) ;
//------------------------------------------------------------------------------
/**
 * @brief workerで証拠を照合して本文全byteを返します
 * @param[in] request 所有snapshot
 * @return 前後に付加しないUTF-8全文
 * @throws OperationError 証拠/束縛不正、容量超過
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string	   renderWholeTextExport( const WholeTextExportJobRequest &request) ;
//------------------------------------------------------------------------------
/**
 * @brief 既存atomic writerで明示TXTを保存します
 * @param[in] request 所有snapshot、拡張子はTXTだけ
 * @throws OperationError 検証/保存失敗。旧出力を保持します
 */
//------------------------------------------------------------------------------
void						   saveWholeTextExport( const WholeTextExportJobRequest &request) ;
} // namespace koefude
