//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file model_preparation.h
 * @brief 選択構成の不足モデルと検証済みcacheの再利用計画を宣言します
 * @version 1.0.0
 * @date 2026/09/13
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/13 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/13 取得前の依存解決と読取り専用検査を追加
 * 0.1.0 Shunji Ogawa 2026/09/13 native取得でも共用する保存先検査の契約を公開
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
#pragma once

#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <vector>

#include "core/engines.h"
#include "inference/model_store.h"

namespace koefude {

/** @brief file検査で確認した瞬間の準備状態です */
enum class ModelPresence {
	/// fileまたは親directoryが存在しない
	MISSING,
	/// 固定size/hashが一致した
	VALID,
	/// 通常fileだが固定size/hashが一致しない
	CORRUPT
} ;

/** @brief worker専用の検査境界です。同期処理中にUIやmutexを保持しません */
class IModelInspector {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 検査実装を解放します
	 */
	//------------------------------------------------------------------------------
	virtual ~IModelInspector() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 単一fileの有無と内容を検査します
	 * @param[in] root 呼出し中有効な検査済みlocal絶対directory
	 * @param[in] id 固定registryの用途
	 * @param[in] stop 協調取消し
	 * @return size/hashの検査結果。I/O失敗をMissingへ変換しません
	 * @throws OperationError 属性拒否、I/O失敗、取消し
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual ModelPresence inspect( const std::filesystem::path &root, ModelId id,
												std::stop_token stop) = 0 ;
} ;

/** @brief CNGによる固定size/hashの読取り検査を行います */
class LocalModelInspector final : public IModelInspector {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 単一fileの有無と内容を検査します
	 * @param[in] root 呼出し中有効な検査済みlocal絶対directory
	 * @param[in] id 固定registryの用途
	 * @param[in] stop 協調取消し
	 * @return size/hashの検査結果。I/O失敗をMissingへ変換しません
	 * @throws OperationError 属性拒否、I/O失敗、取消し
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] ModelPresence inspect( const std::filesystem::path &root, ModelId id, std::stop_token stop) override ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 同じ検証境界で合成fileや固定modelを検査します
 * @param[in] root local絶対directory
 * @param[in] definition 呼出し中有効な信頼済み期待値。外部入力は禁止
 * @param[in] stop 協調取消し
 * @return 検証時点の状態。返却時に検査lockは解放済みです
 * @throws OperationError 属性拒否、I/O失敗、取消し
 */
//------------------------------------------------------------------------------
[[nodiscard]] ModelPresence inspectModelFile( const std::filesystem::path &root, const ModelDefinition &definition,
											 std::stop_token stop) ;

/** @brief 準備完了までに必要なfile単位の操作です */
enum class ModelPreparationAction {
	/// 検証済みcacheをそのまま再利用する
	REUSE_CACHE,
	/// 全依存が揃う同梱rootをそのまま再利用する
	REUSE_BUNDLE,
	/// 検証済み同梱fileをcacheへcopyする予定
	COPY_BUNDLE,
	/// 未取得または破損fileを固定取得元から準備する予定
	DOWNLOAD
} ;

/** @brief 必要な一つのfileについて検査状態と操作を保持します */
struct ModelPreparationEntry {
	/// 単一registryの用途
	ModelId				   id ;
	/// cache側の状態
	ModelPresence		   cache ;
	/// 同梱側の状態
	ModelPresence		   bundle ;
	/// この計画で必要な操作
	ModelPreparationAction action ;
} ;

/** @brief 読取り専用検査で得る所有DTOです。engine使用時には再検証が必要です */
struct ModelPreparationPlan {
	/// 検査した同梱directory
	std::filesystem::path			   bundleRoot ;
	/// 書込み予定のユーザーdirectory。未作成を許可
	std::filesystem::path			   cacheRoot ;
	/// 全依存を揃えてからengineへ渡す一つのroot
	std::filesystem::path			   effectiveRoot ;
	/// 選択構成だけの重複しない必要file
	std::vector<ModelPreparationEntry> entries ;
	/// 同じURL/hashを一度だけ数える転送の代表用途
	std::vector<ModelId>			   transfers ;
	/// 全必要fileの最終byte数。VRAMとは異なる
	std::uint64_t					   requiredBytes = 0 ;
	/// 同梱からcopy予定の最終byte数
	std::uint64_t					   copyBytes = 0 ;
	/// 重複を除いたartifact転送byte数
	std::uint64_t					   downloadBytes = 0 ;
	/// 一つのrootで全file検証に成功した場合だけtrue
	bool							   ready = false ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 選択ASRと有効な処理から必要fileを一意に解決します
 * @param[in] options ジョブまたは準備操作で固定した設定
 * @return registryの用途だけを保持する所有列
 * @throws OperationError 未定義構成や準備対象外の依存
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<ModelId>	requiredModels( const EngineOptions &options) ;

//------------------------------------------------------------------------------
/**
 * @brief 標準ユーザーcacheのlocal絶対pathを取得し、既存の親属性を確認します
 * @return LocalAppData配下のkoefude/models。directoryは作成しません
 * @throws OperationError OS取得失敗、network/reparse/cloud等の非対応保存先
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::filesystem::path defaultModelCache() ;

//------------------------------------------------------------------------------
/**
 * @brief 保存先と既存祖先のlocal・通常directory属性を確認します
 * @param[in] root local絶対directory。未作成部分を許可
 * @return 正規化した所有path。file作成は行いません
 * @throws OperationError 非対応path、reparse/cloud、属性取得失敗
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::filesystem::path validateModelDirectory( const std::filesystem::path &root) ;

//------------------------------------------------------------------------------
/**
 * @brief 必要fileだけを検査し、既取得の再利用と不足分の準備計画を作成します
 * @param[in] options 選択構成。model_rootは使わず明示root引数を検査します
 * @param[in] bundleRoot 同梱のlocal絶対directory。欠落を許可
 * @param[in] cacheRoot cacheのlocal絶対directory。欠落を許可
 * @param[in,out] inspector 同じworkerが同期呼出しする検査実装。呼出し中有効
 * @param[in] stop 検査中の協調取消し
 * @return 全検査成功時だけ返す所有計画。copy/取得/書込みは実行しません
 * @throws OperationError 不正root、未知構成、I/O失敗、取消し
 * @note worker専用。engineは使用時に固定hashと所有lockを再取得します
 */
//------------------------------------------------------------------------------
[[nodiscard]] ModelPreparationPlan	planModelPreparation( const EngineOptions		 &options,
														 const std::filesystem::path &bundleRoot,
														 const std::filesystem::path &cacheRoot,
														 IModelInspector &inspector, std::stop_token stop) ;

} // namespace koefude
