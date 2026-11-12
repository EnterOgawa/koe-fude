//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file asr_retry.cpp
 * @brief 推論失敗だけを対象に、同じweightで最大一回のCPU再試行を実装します
 * @version 1.0.0
 * @date 2026/09/11
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/11 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/11 OOM・推論失敗と取消し・不正結果の再試行を区別
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
#include "inference/asr_retry.h"

#include <exception>
#include <new>

#include <Windows.h>

namespace koefude {
namespace {

//------------------------------------------------------------------------------
/**
 * @brief 一回のnative処理の例外を本文を含まない分類へ変換します
 * @param[in] attempt contextをこの呼出し内で所有・解放する非NULLの関数
 * @param[in] gpu 今回の実行先がGPUならtrue
 * @return 一回の成功結果。失敗時は所有context解放後に例外
 * @throws OperationError 元の分類、memory不足、第三者推論の失敗
 */
//------------------------------------------------------------------------------
[[nodiscard]] Recognition invokeAttempt( const std::function<Recognition( bool)> &attempt, const bool gpu)
{
	try {
		auto result = attempt( gpu) ; // この試行だけの所有本文
		result.usedGpu = gpu ;
		result.cpuRetried = false ;
		return result ;
	} catch( const OperationError &) {
		throw ; // 取消し・入力・結果の分類を変えない
	} catch( const std::bad_alloc &) {
		throw OperationError( ErrorKind::INFERENCE_FAILED, ERROR_NOT_ENOUGH_MEMORY) ;
	} catch( const std::exception &) {
		// 第三者の例外本文はpath等を含み得るため、分類だけを保持する
		throw OperationError( ErrorKind::INFERENCE_FAILED) ;
	}
}

} // namespace

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
 * @see koefude::runAsrWithCpuRetry
 */
//------------------------------------------------------------------------------
Recognition runAsrWithCpuRetry( const bool useGpu, const bool cpuFallback, const std::stop_token stop,
							   const std::function<Recognition( bool)> &attempt)
{
	checkCancelled( stop) ;
	// 空のcallbackを推論エラーとして再試行しない
	if( !attempt) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	try {
		return invokeAttempt( attempt, useGpu) ;
	} catch( const OperationError &error) {
		// 推論失敗以外、CPU自体の失敗、利用者が禁止した再試行は元の失敗を返す
		if( !useGpu || !cpuFallback || error.kind() != ErrorKind::INFERENCE_FAILED) {
			throw ;
		}
	}
	checkCancelled( stop) ; // GPU contextの解放後にも取消しを優先する
	auto result = invokeAttempt( attempt, false) ; // 同じweightによる最後の一回
	result.cpuRetried = true ;
	return result ;
}

} // namespace koefude
