//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_preparation.h
 * @brief 補正準備専用の所有DTOと明示検査・取得・導入境界を定義します
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1279の補正準備入口を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1280の明示導入・補助準備とcommit成功を保持
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1283の明示Gemma取得を同一枠へ追加
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1343の文章専用補正を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/engines.h"
#include "core/types.h"
#include "inference/model_download.h"
#include "inference/model_preparation.h"
#include "inference/model_store.h"
#include "inference/whole_text_model_download.h"
#include "inference/whole_text_model_import.h"
#include "inference/whole_text_runtime.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <vector>

namespace koefude {
/** @brief 同じ一枠で明示実行する操作です */
enum class WholeTextPreparationAction {
	/// 通信なし検査
	INSPECT,
	/// 正規取得済みlocal fileの導入
	IMPORT_GEMMA,
	/// 固定補助認識モデルの明示準備
	PREPARE_WHISPER,
	/// 固定補正モデルの明示Direct取得
	DOWNLOAD_GEMMA
} ;
/** @brief 準備対象を明示し、文章専用から補助音声を除外します */
enum class WholeTextPreparationUse {
	/// 従来の補助Whisper付き用途
	AUDIO_ASSISTED,
	/// 文章専用Ministralだけ
	TEXT_ONLY_CONTEXT
} ;
/** @brief 音声・本文・engineを持たない一回の所有要求です */
struct WholeTextPreparationRequest {
	/// 明示CPU方式
	WholeTextRuntimeVariant	   variant = WholeTextRuntimeVariant::PORTABLE ;
	/// 固定basenameの絶対DLL先
	std::filesystem::path	   runtimePath ;
	/// 同梱モデル先
	std::filesystem::path	   bundleRoot ;
	/// 空ならruntime合格後workerで標準先を解決
	std::filesystem::path	   cacheRoot ;
	/// 明示操作
	WholeTextPreparationAction action = WholeTextPreparationAction::INSPECT ;
	/// ImportGemma中だけ所有し終端へ残さない元file
	std::filesystem::path	   sourceFile ;
	/// 旧aggregate互換
	WholeTextPreparationUse	   use = WholeTextPreparationUse::AUDIO_ASSISTED ;
} ;
/** @brief 各要素の検査時点の状態です */
enum class WholeTextPreparationState {
	/// 前提拒否または未開始で未検査
	UNCHECKED,
	/// 固定size/SHA等の検査合格
	READY,
	/// 未配置
	MISSING,
	/// 内容・構成の不一致
	CORRUPT,
	/// CPU/OS契約不足
	UNSUPPORTED,
	/// I/O等の検査失敗
	FAILURE
} ;
/** @brief registry値を所有し、将来のregistry参照寿命に依存しない説明です */
struct WholeTextPreparationModel {
	/// 単一registryの用途
	ModelId				  role ;
	/// 固定モデル識別子
	std::string			  id ;
	/// 固定相対file
	std::filesystem::path file ;
	/// weight保存容量。RAM/VRAMではない
	std::uint64_t		  bytes = 0 ;
	/// 固定revision
	std::string			  revision ;
	/// 配布条件
	std::string			  license ;
	/// 一次資料
	std::string			  source ;
	/// 既存取得可否
	ModelTransferKind	  transfer = ModelTransferKind::UNAVAILABLE ;
} ;
/** @brief 一要素の所有状態と元診断を保持します */
struct WholeTextPreparationElement {
	/// 検査状態
	WholeTextPreparationState			   state = WholeTextPreparationState::UNCHECKED ;
	/// 失敗の元分類
	ErrorKind							   error = ErrorKind::INTERNAL ;
	/// APIの元code
	std::uint32_t						   code = 0 ;
	/// 検査対象の所有先
	std::filesystem::path				   effectivePath ;
	/// 単一registry由来metadata
	std::vector<WholeTextPreparationModel> models ;
} ;
/** @brief runtime/Whisper/Gemmaの検査snapshotです。leaseとengineは保持しません */
struct WholeTextPreparationPlan {
	/// 同じ結果に対応する固定要求
	WholeTextPreparationRequest request ;
	/// CPU/receipt/DLLの検査
	WholeTextPreparationElement runtime ;
	/// 固定Quality CPU4補助用
	WholeTextPreparationElement whisper ;
	/// 用途13、専用取得とlocal導入
	WholeTextPreparationElement gemma ;
	/// 三要素が全てReadyのときだけtrue
	bool						ready = false ;
	/// 表示と取得が共有する固定計画
	ModelPreparationPlan		whisperPlan ;
	/// 実rename成功後だけtrue
	bool						importCommitted = false ;
	/// Gemma導入操作の再利用結果
	bool						reusedCache = false ;
	/// Direct取得の実rename成功後だけtrue
	bool						downloadCommitted = false ;
	/// 欠落cacheの場合だけの同梱再利用
	bool						reusedBundle = false ;
} ;
/** @brief 総量不明の実段階だけを通知します。架空の率は作りません */
enum class WholeTextPreparationStage {
	/// 明示検査待ち
	IDLE,
	/// CPU/receipt/DLLを検査中
	RUNTIME,
	/// Whisperを検査中
	WHISPER,
	/// Gemmaを検査中
	GEMMA,
	/// 協調取消し待ち
	CANCELLING,
	/// 検査終了。Readyとは別
	COMPLETED,
	/// 取消し終端
	CANCELLED,
	/// 例外終端
	FAILURE,
	/// 元fileの属性/所有lease確認
	SOURCE_VALIDATION,
	/// 有界copyの実byte
	IMPORT_COPYING,
	/// CNG検証の実byte
	IMPORT_VERIFYING,
	/// 原子的確定中、取消しは完了待ち
	IMPORT_COMMITTING,
	/// 明示補助モデル準備の実byte
	WHISPER_DOWNLOADING,
	/// 専用Direct転送の実byte
	GEMMA_DOWNLOADING,
	/// 取得した所有一時fileの実検証byte
	DOWNLOAD_VERIFYING,
	/// 原子的確定中、取消しは完了待ち
	DOWNLOAD_COMMITTING
} ;
/** @brief 実段階とその段階内の実byteを保持します */
struct WholeTextPreparationProgress {
	/// 実段階
	WholeTextPreparationStage stage = WholeTextPreparationStage::IDLE ;
	/// 実処理byte数
	std::uint64_t			  completedBytes = 0 ;
	/// 0なら総量不明
	std::uint64_t			  totalBytes = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 段階だけの従来通知を受け入れます
	 * @param[in] value 実段階
	 * @param[in] completed 実処理byte
	 * @param[in] total 全byte
	 */
	//------------------------------------------------------------------------------
	WholeTextPreparationProgress( WholeTextPreparationStage value = WholeTextPreparationStage::IDLE,
								 std::uint64_t completed = 0, std::uint64_t total = 0) noexcept
		: stage( value), completedBytes( completed), totalBytes( total)
	{
	}
} ;
using WholeTextPreparationCallback = std::function<void( WholeTextPreparationProgress)> ; // 同期通知
/** @brief workerで同期注入する検査・取得・導入境界です。使用する関数は空を禁止します */
struct WholeTextPreparationServices {
	/// 検証だけ
	std::function<void( const WholeTextPreparationRequest &, std::stop_token)> runtime ;
	/// 通常準備計画だけ
	std::function<ModelPreparationPlan( const EngineOptions &, const std::filesystem::path &,
									   const std::filesystem::path &, std::stop_token)>
																						  whisper ;
	/// modelはGemmaだけを検査し、取得・生成を行いません
	std::function<ModelPresence( const std::filesystem::path &, ModelId, std::stop_token)> model ;
	/// 明示取得
	std::function<ModelPreparationPlan( const EngineOptions &, const std::filesystem::path &,
									   const std::filesystem::path &, std::stop_token,
									   const ModelPreparationCallback &)>
		prepareWhisper ;
	/// 明示導入
	std::function<WholeTextModelImportResult( const std::filesystem::path &, const std::filesystem::path &,
											 std::stop_token, const WholeTextImportCallback &)>
		importGemma ;
	/// 明示Direct取得だけ
	std::function<WholeTextModelDownloadResult( const std::filesystem::path &, const std::filesystem::path &,
											   std::stop_token, const WholeTextDownloadCallback &)>
		downloadGemma ;
	/// 明示選択した固定用途
	std::function<WholeTextModelImportResult( WholeTextRuntimeVariant, const std::filesystem::path &,
											 const std::filesystem::path &, std::stop_token,
											 const WholeTextImportCallback &)>
		importSelected ;
	/// 同じ用途のDirectだけ
	std::function<WholeTextModelDownloadResult( WholeTextRuntimeVariant, const std::filesystem::path &,
											   const std::filesystem::path &, std::stop_token,
											   const WholeTextDownloadCallback &)>
		downloadSelected ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 固定補助Whisper設定を返します
 * @return Quality/CPU4/日本語、VAD/話者/fallback無効
 */
//------------------------------------------------------------------------------
[[nodiscard]] EngineOptions				   wholeTextPreparationOptions() ;
//------------------------------------------------------------------------------
/**
 * @brief 製品の検査・明示準備境界を返します
 * @return worker専用の同期関数群
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextPreparationServices wholeTextPreparationServices() ;
//------------------------------------------------------------------------------
/**
 * @brief I/Oなしでregistryの用途・条件を表示用に所有します
 * @param[in] request 固定要求
 * @return 全要素未検査のmetadata。sourceは保持しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextPreparationPlan	   wholeTextPreparationPreview( const WholeTextPreparationRequest &request) ;
//------------------------------------------------------------------------------
/**
 * @brief 要求の明示操作をruntime検査から順に実行します
 * @param[in] request 所有pathと明示CPU方式
 * @param[in] stop 協調停止
 * @param[in] progress 空を許す同期実段階通知
 * @param[in] services 注入する同期境界
 * @return 不足・拒否も状態として保持した所有snapshot
 * @throws OperationError 取消し。その他の要素失敗は元診断付きで保持します
 * @note worker専用。runtime前にモデル保存先の属性も調べません
 * @pre PrepareWhisperは表示した用途・容量・条件・通信先に対する明示操作に限ります
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextPreparationPlan	   inspectWholeTextPreparation( const WholeTextPreparationRequest  &request,
																	   std::stop_token					   stop,
																	   const WholeTextPreparationCallback &progress,
																	   const WholeTextPreparationServices &services) ;
//------------------------------------------------------------------------------
/**
 * @brief 通常workerの同期準備です
 * @param[in] request 固定要求
 * @param[in] stop 停止
 * @param[in] progress 実段階
 * @return 検査snapshot
 * @throws OperationError 取消し
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextPreparationPlan	   runWholeTextPreparation( const WholeTextPreparationRequest  &request,
																   std::stop_token					   stop,
																   const WholeTextPreparationCallback &progress) ;
} // namespace koefude
