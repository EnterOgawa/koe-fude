//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file cpu_candidate_contract.h
 * @brief baseline命令だけで候補のCPU/OS契約を判定します
 * @version 1.0.0
 * @date 2026/10/19
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/19 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/19 ADR1270の照会seamと読込み前gateを追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include <array>
#include <cstdint>

namespace koefude::whole_text::cpu_candidate {
// 15必須bitの順序はCPUIDから収集した明示契約です
inline constexpr std::uint32_t K_REQUIRED_FEATURES = (1U << 15U) - 1U ;
// bit6/7はXSAVE/OSXSAVEで、XGETBV実行の前提です
inline constexpr std::uint32_t K_XSAVE_FEATURES = (1U << 6U) | (1U << 7U) ;
/** @brief portableはAVX/XCR0を要求しません */
enum class Variant {
	/// 追加AVX命令を要求しないcontrolです
	PORTABLE,
	/// CPU/OS gate後だけ使用する未接続候補です
	AVX2
} ;
/** @brief 生CPUIDの有界な照会結果です。未照会と失敗を区別します */
struct Snapshot {
	/// 全ての必要leaf照会成功
	bool		  queryOk = false ;
	/// CPUID(0).EAX
	std::uint32_t maximumBasic = 0 ;
	/// CPUID(80000000h).EAX
	std::uint32_t maximumExtended = 0 ;
	/// 必須集合のbit順
	std::uint32_t features = 0 ;
	/// 前提成立後にだけ真
	bool		  xgetbvQueried = false ;
	/// XCR0照会の成功
	bool		  xgetbvOk = false ;
	/// XMM/YMM有効bitを含む生値
	std::uint64_t xcr0 = 0 ;
} ;
// callbackは同期借用、失敗はfalse。純粋テストでは命令を実行しません
using CpuidQuery = bool (*)( void *, std::uint32_t, std::array<std::uint32_t, 4> &) noexcept ;
// XSAVE/OSXSAVE成立後だけ呼び、XCR0を返します
using XcrQuery = bool (*)( void *, std::uint64_t &) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief leaf存在を確認して必要bitとOS状態を採取します
 * @param[in] cpuid 非nullの同期照会
 * @param[in] xcr 非nullの条件付き照会
 * @param[in] data callback終了まで生存する借用値、null可
 * @return 失敗・未照会を明示したsnapshot
 */
//------------------------------------------------------------------------------
[[nodiscard]] Snapshot collect( CpuidQuery cpuid, XcrQuery xcr, void *data) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 実機のCPUID/XCR0をbaseline host内で採取します
 * @return 有界snapshot
 */
//------------------------------------------------------------------------------
[[nodiscard]] Snapshot queryHost() noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief DLLロードより前に純粋なCPU契約を判定します
 * @param[in] variant 明示variant
 * @param[in] snapshot 改変しない照会値
 * @return 全前提を満たす場合だけtrue。portableは追加ISAを要求しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool	   supports( Variant variant, const Snapshot &snapshot) noexcept ;
} // namespace koefude::whole_text::cpu_candidate
