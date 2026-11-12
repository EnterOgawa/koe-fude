//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file cuda_device.h
 * @brief CUDAとDXGIを同じadapterへ照合するGPU予算の契約を定義します
 * @version 1.0.0
 * @date 2026/09/11
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/11 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/11 CUDAの空きとWindowsのprocess予算を照合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <cstdint>

#include "core/engines.h"
#include "inference/gpu_budget.h"

namespace koefude {

/** @brief sherpa-onnxが使用するCUDA device 0の検証結果です */
struct CudaDeviceInfo {
	/// 物理容量と、CUDA/DXGIの小さい方の空き（byte）
	GpuBudget budget ;
	/// Autoを評価したRTX 3080 Tiとの一致
	bool	  validatedAdapter = false ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 同一deviceの観測値からoverflowしないGPU予算を作ります
 * @param[in] physical DXGIの専用物理容量（byte）
 * @param[in] cudaTotal CUDAが報告する総量（byte）
 * @param[in] cudaFree CUDAが報告する他process込みの空き（byte）
 * @param[in] adapterBudget DXGIがこのprocessへ提示する予算（byte）
 * @param[in] currentUsage DXGIが報告するこのprocessの現在使用量（byte）
 * @return 不明・矛盾値はknown=false。予算超過時はavailable=0
 */
//------------------------------------------------------------------------------
[[nodiscard]] GpuBudget		 combineCudaBudget( std::uint64_t physical, std::uint64_t cudaTotal, std::uint64_t cudaFree,
											   std::uint64_t adapterBudget, std::uint64_t currentUsage) noexcept ;

//------------------------------------------------------------------------------
/**
 * @brief CUDA device 0のLUIDをDXGIへ照合して実行前の空きを取得します
 * @return CUDA provider/driver/API/照合の不足時はbudget.known=false
 * @note worker専用の同期照会です。保持したprimary contextは戻る前に解放します
 * 以後の割当てを制限するhard limitではありません
 */
//------------------------------------------------------------------------------
[[nodiscard]] CudaDeviceInfo queryCudaDevice() ;

//------------------------------------------------------------------------------
/**
 * @brief 装置方針と検証済みprofileを、1GiBの余裕を残した予算へ照合します
 * @param[in] device 同一adapterの取得結果。provider不足時は予算不明
 * @param[in] backend ジョブ受付時に固定した実行方針
 * @param[in] required profileで確保する保守的な必要量（byte）
 * @param[in] validatedProfile Auto選択に合格したprofileならtrue
 * @return 明示CUDAまたは検証済みAutoで予算が足りる場合true
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool			 canUseCuda( const CudaDeviceInfo &device, Backend backend, std::uint64_t required,
										bool validatedProfile) noexcept ;

} // namespace koefude
