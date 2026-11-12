//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_model_download.h
 * @brief 用途13だけの明示Direct取得と確定結果を定義します
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1283の固定Direct取得を追加
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
#include "inference/model_download.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <stop_token>

namespace koefude {
class ModelStagingFile ;
/** @brief 取得段階内の実byteだけを通知します */
enum class WholeTextDownloadStage {
	/// 実受信中
	DOWNLOADING,
	/// 所有一時handleのCNG検証
	VERIFYING,
	/// 最終取消し境界と原子的確定
	COMMITTING
} ;
/** @brief 64bitの実進捗です。総量0は不明です */
struct WholeTextDownloadProgress {
	/// 実段階
	WholeTextDownloadStage stage = WholeTextDownloadStage::DOWNLOADING ;
	/// 同段階の実byte
	std::uint64_t		   completedBytes = 0 ;
	/// 同段階の総byte
	std::uint64_t		   totalBytes = 0 ;
} ;
using WholeTextDownloadCallback = std::function<void( WholeTextDownloadProgress)> ; // 同期borrow
/** @brief 再利用と実renameを区別する所有DTOです */
struct WholeTextModelDownloadResult {
	/// 単一用途13
	ModelId				  role = wholeTextNativeModelId() ;
	/// 検証済みcacheまたはbundle
	std::filesystem::path path ;
	/// 正確な保存byte
	std::uint64_t		  bytes = 0 ;
	/// 既存cacheの内容一致
	bool				  reusedCache = false ;
	/// cache欠落時だけの同梱再利用
	bool				  reusedBundle = false ;
	/// 実rename成功後だけtrue
	bool				  downloadCommitted = false ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 固定Gemmaだけを検証・再利用または明示取得します
 * @param[in] bundleRoot 読取り専用同梱先。欠落cacheの場合だけ検査します
 * @param[in] cacheRoot 信頼済みlocal先。workerで標準先を解決済みです
 * @param[in,out] transport 既存同期HTTPS境界
 * @param[in] stop commit前までの協調取消し
 * @param[in] progress 空可の同期通知。commit後は呼びません
 * @return 確定前に所有値を確保した結果
 * @throws OperationError 通信・容量・保存・hash・排他・取消し。旧fileは保持します
 * @pre 表示した用途・容量・条件・保存先・通信先を確認した明示操作に限ります
 * @note worker専用。再試行・モデルload・生成・commit後の再検査を行いません
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextModelDownloadResult downloadWholeTextModel( const std::filesystem::path &bundleRoot,
																  const std::filesystem::path &cacheRoot,
																  IModelTransport &transport, std::stop_token stop,
																  const WholeTextDownloadCallback &progress = {}) ;
//------------------------------------------------------------------------------
/**
 * @brief 明示方式の単一registryモデルだけを準備します
 * @param[in] variant 固定方式
 * @param[in] bundleRoot 同梱先
 * @param[in] cacheRoot 標準cache
 * @param[in,out] transport 同期transport
 * @param[in] stop 停止
 * @param[in] progress 実byte通知
 * @return 固定用途の所有結果
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextModelDownloadResult downloadWholeTextModel( WholeTextRuntimeVariant	   variant,
																  const std::filesystem::path &bundleRoot,
																  const std::filesystem::path &cacheRoot,
																  IModelTransport &transport, std::stop_token stop,
																  const WholeTextDownloadCallback &progress = {}) ;
/** @brief 製品APIから小期待値を指定できない実装境界です */
class WholeTextModelDownloader			   final {
	friend WholeTextModelDownloadResult downloadWholeTextModel( const std::filesystem::path &,
																		   const std::filesystem::path &, IModelTransport &,
																		   std::stop_token, const WholeTextDownloadCallback &) ;
	friend WholeTextModelDownloadResult downloadWholeTextModel( WholeTextRuntimeVariant, const std::filesystem::path &,
																		   const std::filesystem::path &, IModelTransport &,
																		   std::stop_token, const WholeTextDownloadCallback &) ;
	friend struct WholeTextModelDownloadTestAccess ; // 信頼済み自作fixtureだけの境界
	/** @brief 確定前の人工I/O失敗を注入します */
	struct Hooks {
		/// 空は実空き容量検査
		std::function<void( std::uint64_t)>									   space ;
		/// 空は実rename
		std::function<void( ModelStagingFile &, const std::filesystem::path &)> commit ;
		/// 空は実全書込み
		std::function<void( ModelStagingFile &, std::span<const std::byte>)>	   append ;
	} ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 製品固定値または信頼済み小fixtureを同じ保存境界へ渡します
	 * @param[in] bundle 同梱先
	 * @param[in] cache cache先
	 * @param[in,out] transport 同期転送
	 * @param[in] file 最終期待値
	 * @param[in] download 転送期待値
	 * @param[in] production 用途13factory選択
	 * @param[in] stop 停止
	 * @param[in] progress 同期通知
	 * @param[in] hooks 人工境界
	 * @return 所有DTO
	 * @throws OperationError 検証・通信・保存・取消し
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] static WholeTextModelDownloadResult
	run( const std::filesystem::path &bundle, const std::filesystem::path &cache, IModelTransport &transport,
					const ModelDefinition &file, const ModelDownloadDefinition &download, bool production, std::stop_token stop,
					const WholeTextDownloadCallback &progress, const Hooks &hooks) ;
} ;
} // namespace koefude
