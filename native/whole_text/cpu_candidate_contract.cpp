//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file cpu_candidate_contract.cpp
 * @brief CPUID leafとOSXSAVEを確認して安全にXCR0を照会します
 * @version 1.0.0
 * @date 2026/10/19
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/19 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/19 baseline照会と純粋判定を分離
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実使用の標準headerを直接include
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "cpu_candidate_contract.h"
#include <array>
#include <cstddef>
#include <intrin.h>
#include <windows.h>

namespace koefude::whole_text::cpu_candidate {
namespace {
//------------------------------------------------------------------------------
/**
 * @brief 命令失敗をsnapshotへ返します
 * @param[in] data 未使用
 * @param[in] leaf 照会leaf、subleafは0
 * @param[out] result 成功時4register
 * @return 命令成功ならtrue
 */
//------------------------------------------------------------------------------
bool hostCpuid( void *data, std::uint32_t leaf, std::array<std::uint32_t, 4> &result) noexcept
{
	static_cast<void>( data) ;
	int registers[4]{} ; // SEH範囲は所有C++ objectを含めません
	__try {
		__cpuidex( registers, static_cast<int>( leaf), 0) ;
	} __except( EXCEPTION_EXECUTE_HANDLER) {
		return false ;
	}
	// 四registerの生bitを符号なしで保持します
	for( std::size_t index = 0; index < result.size(); ++index) {
		result[index] = static_cast<std::uint32_t>( registers[index]) ;
	}
	return true ;
}
//------------------------------------------------------------------------------
/**
 * @brief 前提確認済みのXCR0だけを照会します
 * @param[in] data 未使用
 * @param[out] result 成功時XCR0
 * @return 命令成功ならtrue
 */
//------------------------------------------------------------------------------
bool hostXcr( void *data, std::uint64_t &result) noexcept
{
	static_cast<void>( data) ;
	__try {
		result = _xgetbv( 0) ;
		return true ;
	} __except( EXCEPTION_EXECUTE_HANDLER) {
		return false ;
	}
}
} // namespace

//------------------------------------------------------------------------------
/**
 * @brief leaf存在を確認して必要bitとOS状態を採取します
 * @param[in] cpuid 非nullの同期照会
 * @param[in] xcr 非nullの条件付き照会
 * @param[in] data callback終了まで生存する借用値、null可
 * @return 失敗・未照会を明示したsnapshot
 * @see koefude::whole_text::cpu_candidate::collect
 */
//------------------------------------------------------------------------------
Snapshot collect( CpuidQuery cpuid, XcrQuery xcr, void *data) noexcept
{
	Snapshot					 snapshot{} ; // 失敗・未照会を既定にします
	std::array<std::uint32_t, 4> basic{} ; // leaf上限
	std::array<std::uint32_t, 4> extended{} ; // 拡張leaf上限
	// 照会手段がない場合は命令を試しません
	if( cpuid == nullptr || xcr == nullptr || !cpuid( data, 0, basic) || !cpuid( data, 0x80000000U, extended)) {
		return snapshot ;
	}
	snapshot.maximumBasic = basic[0] ;
	snapshot.maximumExtended = extended[0] ;
	// 存在しないleafを読み、別leafの値を誤認しないため拒否します
	if( basic[0] < 7 || extended[0] < 0x80000001U) {
		return snapshot ;
	}
	std::array<std::uint32_t, 4> one{} ; // leaf1のECX
	std::array<std::uint32_t, 4> seven{} ; // leaf7のEBX
	std::array<std::uint32_t, 4> ext{} ; // LZCNTのECX
	// 必須leafの照会失敗は不明として拒否します
	if( !cpuid( data, 1, one) || !cpuid( data, 7, seven) || !cpuid( data, 0x80000001U, ext)) {
		return snapshot ;
	}
	// 契約順に対応するCPUID leaf1 ECXのbit位置です
	constexpr std::array<unsigned, 11> K_LEAF_ONE_BITS{0, 9, 19, 20, 23, 22, 26, 27, 28, 12, 29} ;
	// SSE3からF16Cまでのbitを独立した契約順へ写します
	for( std::size_t index = 0; index < K_LEAF_ONE_BITS.size(); ++index) {
		snapshot.features |= ((one[2] >> K_LEAF_ONE_BITS[index]) & 1U) << index ;
	}
	snapshot.features |= ((seven[1] >> 3U) & 1U) << 11U ;
	snapshot.features |= ((seven[1] >> 8U) & 1U) << 12U ;
	snapshot.features |= ((seven[1] >> 5U) & 1U) << 13U ;
	snapshot.features |= ((ext[2] >> 5U) & 1U) << 14U ;
	snapshot.queryOk = true ;
	// XSAVEとOSXSAVEを確認するまでXGETBVを一度も実行しません
	if( (snapshot.features & K_XSAVE_FEATURES) == K_XSAVE_FEATURES) {
		snapshot.xgetbvQueried = true ;
		snapshot.xgetbvOk = xcr( data, snapshot.xcr0) ;
	}
	return snapshot ;
}
//------------------------------------------------------------------------------
/**
 * @brief 実機のCPUID/XCR0をbaseline host内で採取します
 * @return 有界snapshot
 * @see koefude::whole_text::cpu_candidate::queryHost
 */
//------------------------------------------------------------------------------
Snapshot queryHost() noexcept
{
	return collect( hostCpuid, hostXcr, nullptr) ;
}
//------------------------------------------------------------------------------
/**
 * @brief DLLロードより前に純粋なCPU契約を判定します
 * @param[in] variant 明示variant
 * @param[in] snapshot 改変しない照会値
 * @return 全前提を満たす場合だけtrue。portableは追加ISAを要求しません
 * @see koefude::whole_text::cpu_candidate::supports
 */
//------------------------------------------------------------------------------
bool supports( Variant variant, const Snapshot &snapshot) noexcept
{
	return variant == Variant::PORTABLE ||
		   (snapshot.queryOk && snapshot.maximumBasic >= 7 && snapshot.maximumExtended >= 0x80000001U &&
			(snapshot.features & K_REQUIRED_FEATURES) == K_REQUIRED_FEATURES && snapshot.xgetbvQueried &&
			snapshot.xgetbvOk && (snapshot.xcr0 & 6U) == 6U) ;
}
} // namespace koefude::whole_text::cpu_candidate
