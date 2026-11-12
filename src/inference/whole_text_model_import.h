//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_model_import.h
 * @brief 用途13の取得済みlocalモデルを有界に検証して導入します
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1280の用途限定local導入を追加
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "inference/model_store.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <stop_token>

namespace koefude {
class ModelStagingFile ;
/** @brief 導入の実段階です */
enum class WholeTextImportStage {
	/// source leaseと長さの確認
	SOURCE_VALIDATION,
	/// 64KiB以下のstream copy
	COPYING,
	/// 所有一時fileのCNG検証
	VERIFYING,
	/// 取消し確認後の原子的確定
	COMMITTING
} ;
/** @brief 一段階内の実byteを64bitで保持します */
struct WholeTextImportProgress {
	/// 現段階
	WholeTextImportStage stage = WholeTextImportStage::SOURCE_VALIDATION ;
	/// 実処理済みbyte
	std::uint64_t		 completedBytes = 0 ;
	/// 0は総量不明
	std::uint64_t		 totalBytes = 0 ;
} ;
using WholeTextImportCallback = std::function<void( WholeTextImportProgress)> ; // 同期borrow通知
/** @brief 再利用と実rename成功を区別する所有結果です */
struct WholeTextModelImportResult {
	/// 単一registry用途13
	ModelId				  role = wholeTextNativeModelId() ;
	/// 検証済みcacheの所有path
	std::filesystem::path path ;
	/// 正確な保存容量
	std::uint64_t		  bytes = 0 ;
	/// source data読取りなしの再利用
	bool				  reusedCache = false ;
	/// 実rename成功後だけtrue
	bool				  importCommitted = false ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 正規取得済みfileを用途13の固定値で標準cacheへ導入します
 * @param[in] sourceFile 絶対local通常file。sourceを書換えず永続記録しません
 * @param[in] cacheRoot 信頼済みlocal cache。workerで標準先を解決済みです
 * @param[in] stop commit前までの協調取消し
 * @param[in] progress 空可の同期実byte通知。commit後は呼びません
 * @return 用途・確定path・byte・再利用/commitの所有結果
 * @throws OperationError path/容量/hash/I/O/排他/取消し。旧最終fileを保持します
 * @note worker専用。モデルload、通信、生成は行いません
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextModelImportResult importWholeTextModel( const std::filesystem::path	&sourceFile,
															  const std::filesystem::path	&cacheRoot,
															  std::stop_token				 stop,
															  const WholeTextImportCallback &progress = {}) ;

//------------------------------------------------------------------------------
/**
 * @brief 明示方式の単一registryモデルだけを準備します
 * @param[in] variant 固定方式
 * @param[in] sourceFile 元file
 * @param[in] cacheRoot 標準cache
 * @param[in] stop 停止
 * @param[in] progress 実byte通知
 * @return 固定用途の所有結果
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextModelImportResult importWholeTextModel( WholeTextRuntimeVariant		 variant,
															  const std::filesystem::path	&sourceFile,
															  const std::filesystem::path	&cacheRoot,
															  std::stop_token				 stop,
															  const WholeTextImportCallback &progress = {}) ;
/** @brief 製品から期待size/hashを指定できない同期実装境界です */
class WholeTextModelImporter			 final {
	friend WholeTextModelImportResult importWholeTextModel( const std::filesystem::path &, const std::filesystem::path &,
																	   std::stop_token, const WholeTextImportCallback &) ;
	friend WholeTextModelImportResult importWholeTextModel( WholeTextRuntimeVariant, const std::filesystem::path &,
																	   const std::filesystem::path &, std::stop_token,
																	   const WholeTextImportCallback &) ;
	friend struct WholeTextModelImportTestAccess ; // 自作小fixtureだけの信頼済み期待値
	/** @brief commit前の人工失敗/取消しを同期注入する試験専用境界です */
	struct Hooks {
		/// 空は実rename
		std::function<void( ModelStagingFile &, const std::filesystem::path &)> commit ;
		/// 空は実copyだけ
		std::function<void( std::uint64_t)>									   copying ;
		/// 空は実空き容量検査
		std::function<void( std::uint64_t)>									   space ;
		/// 空は実全書込み
		std::function<void( ModelStagingFile &, std::span<const std::byte>)>	   append ;
	} ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定値または自試験の信頼済み小fileだけを処理します
	 * @param[in] sourceFile 元file
	 * @param[in] cacheRoot cache
	 * @param[in] definition 信頼済み期待値
	 * @param[in] production trueは用途13factory
	 * @param[in] stop 停止
	 * @param[in] progress 実段階通知
	 * @param[in] hooks 人工境界
	 * @return 所有導入結果
	 * @throws OperationError 検証・I/O・取消し
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] static WholeTextModelImportResult run( const std::filesystem::path &sourceFile,
																	const std::filesystem::path &cacheRoot,
																	const ModelDefinition &definition, bool production,
																	std::stop_token stop, const WholeTextImportCallback &progress,
																	const Hooks &hooks) ;
} ;
} // namespace koefude
