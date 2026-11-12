//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file asr_retry.h
 * @brief GPU失敗後に同じモデルを一度だけCPUへ再試行する契約を定義します
 * @version 1.0.0
 * @date 2026/09/11
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/11 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/11 所有context解放後の限定したCPU再試行を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <functional>
#include <stop_token>

#include "core/engines.h"

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief GPU失敗時に所有resourceが解放された後でCPUを最大一回試します
 * @param[in] useGpu 事前照合を通ったGPUで最初に処理する場合true
 * @param[in] cpuFallback 利用者が同じweightのCPU再試行を許可した場合true
 * @param[in] stop 試行間でも確認する取消し
 * @param[in] attempt trueはGPU、falseはCPUで同じ検証済みweightを処理する同期関数
 * NULL不可。contextは各呼出し内で所有し、例外時も解放してください
 * @return 最後の成功結果だけを返し、途中結果を混ぜません
 * @throws OperationError 取消し、非推論エラー、再試行無効、CPU失敗
 */
//------------------------------------------------------------------------------
[[nodiscard]] Recognition runAsrWithCpuRetry( bool useGpu, bool cpuFallback, std::stop_token stop,
											 const std::function<Recognition( bool)> &attempt) ;

} // namespace koefude
