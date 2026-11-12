//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_bounded_corrector.h
 * @brief 同じlive sessionの逐次生成から保存前terminalへ接続します
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1285の有界逐次補正を接続
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1287の明示診断と隔離接続を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/correction_diagnostics.h"
#include "inference/whole_text_bounded_preparation.h"
#include "platform/whole_text_journal_store.h"
namespace koefude {
/** @brief 計画と一fileを消費する同期worker専用の補正器です */
class NativeBoundedWholeTextCorrector final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 全計画→promotion→逐次core→明示closeを一回実行します
	 * @param[in] prepared 材料/計画/live sessionをmoveするowner
	 * @param[in] terminal 生成前に全通知値/journalを確保した保存前terminal
	 * @param[in] guard 同じbaseline
	 * @param[in] gate weak通知gate、null可
	 * @param[in] stop 停止
	 * @param[in] diagnostics 任意の固定scalar capture、null可
	 * @return native所有物のない未保存terminal
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] WholeTextBoundedTerminal execute(
		PreparedBoundedWholeTextPlan prepared, WholeTextBoundedTerminal terminal,
		std::shared_ptr<WholeTextResourceGuard> guard, std::shared_ptr<WholeTextBoundedProgressGate> gate,
		std::stop_token								 stop,
		std::shared_ptr<CorrectionDiagnosticCapture> diagnostics = nullptr) ; // この境界のdiagnosticsを保持する局所値
} ;
} // namespace koefude
