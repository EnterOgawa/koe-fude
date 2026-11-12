//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_runtime.h
 * @brief 固定二方式のCPU契約と読込み前の完成runtime検証を管理します
 * @version 1.0.0
 * @date 2026/10/19
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/19 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/19 ADR1273の通常製品runtime契約を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1284の生成しない計数と有限計画の境界を追加
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実契約・所有寿命・戻り値の意味を具体化
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415のoperator括弧と文書lambdaの書式を補修
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "../../native/whole_text/cpu_candidate_contract.h"
#include "platform/win_helpers.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>

namespace koefude {
/** @brief 新規実行を許可する二つだけのCPU契約です */
enum class WholeTextRuntimeVariant {
	/// 既存の追加ISAなし構成
	PORTABLE,
	/// 明示AVX2、repack OFF構成
	AVX2_NO_REPACK,
	/// 独立したMinistral16GB評価構成（ordinal2）
	MINISTRAL14_B_AVX2_NO_REPACK16_GB
} ;
/** @brief 固定basenameとprofile識別子です。開発pathは保持しません */
struct WholeTextRuntimeDescriptor {
	/// 固定CPU契約
	WholeTextRuntimeVariant variant ;
	/// 新規要求用の識別子
	std::string_view		profileId ;
	/// receiptとencoding共通の契約
	std::string_view		cpuContract ;
	/// EXE直下の固有名
	std::wstring_view		dllName ;
	/// 固有sidecar名
	std::wstring_view		receiptName ;
} ;
/** @brief 構築時に検証した完成fileの期待値です */
struct WholeTextRuntimePin {
	/// 未指定slotは利用不可
	bool		  available = false ;
	/// 完成DLLの全byte数
	std::uint64_t dllBytes = 0 ;
	/// 完成DLLのSHA-256
	std::string	  dllSha256 ;
	/// sidecar全byteのSHA-256
	std::string	  receiptSha256 ;
	/// 実Debug/Release
	std::string	  configuration ;
	/// 旧slotを暗黙昇格しない明示mode
	bool		  accountingV2 = false ;
	/// schema3の完成target pin
	std::uint32_t capabilitiesMask = 0 ;
	/// V2共通header全byte pin
	std::string	  abiHeaderSha256 ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 固定descriptorを返します
 * @param[in] variant 二方式のいずれか
 * @return 静的寿命の契約
 */
//------------------------------------------------------------------------------
[[nodiscard]] const WholeTextRuntimeDescriptor &wholeTextRuntimeDescriptor( WholeTextRuntimeVariant variant) ;
//------------------------------------------------------------------------------
/**
 * @brief 新規要求だけをid/digestで完全照合します
 * @param[in] id profile名
 * @param[in] digest 全SHA
 * @return 一致時のみ静的契約、他はnull
 */
//------------------------------------------------------------------------------
[[nodiscard]] const WholeTextRuntimeDescriptor *findWholeTextRuntime( std::string_view					 id,
																	 const std::array<std::uint8_t, 32> &digest) ;
//------------------------------------------------------------------------------
/**
 * @brief 新計数profileだけを照合します
 * @param[in] id 新方式
 * @param[in] digest 全設定SHA
 * @return 固定descriptorかnull
 */
//------------------------------------------------------------------------------
[[nodiscard]] const WholeTextRuntimeDescriptor *findWholeTextBoundedRuntime( std::string_view					id,
																			const std::array<std::uint8_t, 32> &digest) ;
/** @brief 完成DLLとreceiptのwrite/delete拒否leaseです。移動・複製しません */
class VerifiedWholeTextRuntime					final {
	 public:
	 //------------------------------------------------------------------------------
	 /**
	  * @brief 検証済みDLLとreceiptのwrite/delete拒否handleを同じ不変ownerへ固定するため、copy構築を禁止します
	  * @note 名前のない第1引数（const VerifiedWholeTextRuntime &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	  */
	 //------------------------------------------------------------------------------
	 VerifiedWholeTextRuntime( const VerifiedWholeTextRuntime &) = delete ;
	 //------------------------------------------------------------------------------
	 /**
	  * @brief 検証済みDLLとreceiptのwrite/delete拒否handleを同じ不変ownerへ固定するため、copy代入を禁止します
	  * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	  * @note 名前のない第1引数（const VerifiedWholeTextRuntime &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	  */
	 //------------------------------------------------------------------------------
	 VerifiedWholeTextRuntime &operator=( const VerifiedWholeTextRuntime &) = delete ;
	 //------------------------------------------------------------------------------
	 /**
	  * @brief 同期native呼出しとFreeLibraryの後まで両leaseを保持します
	  */
	 //------------------------------------------------------------------------------
	 ~VerifiedWholeTextRuntime() = default ;
	 //------------------------------------------------------------------------------
	 /**
	  * @brief 同じ方式とpathへの束縛を調べます
	  * @param[in] variant 要求方式
	  * @param[in] path 固定絶対path
	  * @return 同じownerならtrue
	  */
	 //------------------------------------------------------------------------------
	 [[nodiscard]] bool matches( WholeTextRuntimeVariant variant, const std::filesystem::path &path) const noexcept ;
	 //------------------------------------------------------------------------------
	 /**
	  * @brief 検証済み絶対pathを返します
	  * @return owner寿命まで有効なpath
	  */
	 //------------------------------------------------------------------------------
	 [[nodiscard]] const std::filesystem::path &path() const noexcept ;
	 //------------------------------------------------------------------------------
	 /**
	  * @brief 検証した固有方式を返します
	  * @return 不変の方式
	  */
	 //------------------------------------------------------------------------------
	 [[nodiscard]] WholeTextRuntimeVariant		variant() const noexcept
	 {
		 return variant_ ;
	 }
	 //------------------------------------------------------------------------------
	 /**
	  * @brief schema3で検証した全bitを読みます
	  * @return 旧契約0、新契約1または3
	  */
	 //------------------------------------------------------------------------------
	 [[nodiscard]] std::uint32_t capabilitiesMask() const noexcept
	 {
		 return capabilitiesMask_ ;
	 }
	 //------------------------------------------------------------------------------
	 /**
	  * @brief 検証したschema3か調べます
	  * @return V2を明示検証した時true
	  */
	 //------------------------------------------------------------------------------
	 [[nodiscard]] bool accountingV2() const noexcept
	 {
		 return accountingV2_ ;
	 }

	 private:
	 //------------------------------------------------------------------------------
	 /**
	  * @brief 検証factoryだけがleaseを封印します
	  * @param[in] variant CPU契約
	  * @param[in] path 正規path
	  * @param[in] dll DLLの所有lease
	  * @param[in] receipt sidecar所有lease
	  * @param[in] accountingV2 schema3を明示検証した事実
	  * @param[in] capabilitiesMask 検証済みcapability全bit、旧契約は0
	  */
	 //------------------------------------------------------------------------------
	 VerifiedWholeTextRuntime( WholeTextRuntimeVariant variant, std::filesystem::path path, UniqueHandle dll,
											  UniqueHandle receipt, bool accountingV2, std::uint32_t capabilitiesMask) ;
	 friend std::shared_ptr<const VerifiedWholeTextRuntime>
	 verifyWholeTextRuntimeWithPin( WholeTextRuntimeVariant, const std::filesystem::path &, const WholeTextRuntimePin &,
												   const whole_text::cpu_candidate::Snapshot &, std::stop_token) ;
	 /// 一要求の固定方式
	 const WholeTextRuntimeVariant variant_ ;
	 /// 探索しない絶対path
	 const std::filesystem::path   path_ ;
	 /// 書込み・削除を拒否するlease
	 const UniqueHandle			   dll_ ;
	 /// 同じsidecarのlease
	 const UniqueHandle			   receipt_ ;
	 /// schema3の固定capability全値
	 const std::uint32_t		   capabilitiesMask_ ;
	 /// schema1/2をV2用途へ読替えない封印
	 const bool					   accountingV2_ ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief compiled完成pinを返します
 * @param[in] variant 固定方式
 * @return pathを含まない構築時期待値
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextRuntimePin wholeTextRuntimePin( WholeTextRuntimeVariant variant) ;
//------------------------------------------------------------------------------
/**
 * @brief baseline CPU gateの後にfile全byteを検証しownerを返します
 * @param[in] variant 固定方式
 * @param[in] path 固有名の絶対local DLL
 * @param[in] pin 構築時期待値
 * @param[in] cpu baseline収集値
 * @param[in] stop 協調停止
 * @return 全native callより長く保持するowner
 * @throws OperationError 拒否時はmodel/decode/ASR/DLL loadへ進みません
 * @note 小さい人工fileの内部試験も同じ境界を使用します
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::shared_ptr<const VerifiedWholeTextRuntime>
verifyWholeTextRuntimeWithPin( WholeTextRuntimeVariant variant, const std::filesystem::path &path,
							  const WholeTextRuntimePin &pin, const whole_text::cpu_candidate::Snapshot &cpu,
							  std::stop_token stop) ;
//------------------------------------------------------------------------------
/**
 * @brief 通常factoryでcompiled期待値と実CPUを検証します
 * @param[in] variant 要求方式
 * @param[in] path 固定絶対path
 * @param[in] stop 停止
 * @return 両fileの検証owner
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::shared_ptr<const VerifiedWholeTextRuntime>
verifyWholeTextRuntime( WholeTextRuntimeVariant variant, const std::filesystem::path &path, std::stop_token stop) ;
} // namespace koefude
