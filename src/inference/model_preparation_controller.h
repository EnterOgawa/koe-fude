//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file model_preparation_controller.h
 * @brief モデル準備を一操作に制限して非同期に管理します
 * @version 1.0.0
 * @date 2026/09/13
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/13 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/13 所有DTO・集約進捗・取消しのworkerを追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1397の任意通常診断と発火元を追加
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

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

#include "core/local_diagnostics.h"
#include "inference/model_download.h"

namespace koefude {

/** @brief 受付時の構成と明示取得の許可を所有する要求です */
struct ModelPreparationRequest {
	/// 受付後は変更しない構成
	EngineOptions		  options ;
	/// 読取り専用の同梱先
	std::filesystem::path bundleRoot ;
	/// 空ならworkerで標準cacheを解決
	std::filesystem::path cacheRoot ;
	/// 利用者の明示取得操作だけがtrueを指定
	bool				  download = false ;
	/// 通信許可と独立した発火元
	LocalDiagnosticReason reason = LocalDiagnosticReason::REFRESH ;
} ;

/** @brief 未受領の終端も一件だけ保持する所有通知です */
struct ModelPreparationEvent {
	/// 0を使用しない操作番号
	std::uint64_t								id = 0 ;
	/// 最新の実測進捗
	ModelPreparationProgress					progress ;
	/// これ以降に同じIDの通知を追加しない
	bool										terminal = false ;
	/// 成功時だけの不変計画
	std::shared_ptr<const ModelPreparationPlan> plan ;
	/// 失敗の分類
	ErrorKind									error = ErrorKind::INTERNAL ;
	/// 元APIの診断code
	std::uint32_t								code = 0 ;
} ;

using ModelPreparationRunner =
	std::function<ModelPreparationPlan( const ModelPreparationRequest &, std::stop_token,
									   const ModelPreparationCallback &)> ; // fakeへ差し替える同期境界

//------------------------------------------------------------------------------
/**
 * @brief worker上で読取り検査または明示取得を実行します
 * @param[in] request 固定要求。downloadだけが通信を許可
 * @param[in] stop 協調取消し
 * @param[in] progress 空を許す同期進捗通知
 * @return 固定構成の検査済み計画
 * @throws OperationError 検査・取得失敗、取消し
 */
//------------------------------------------------------------------------------
[[nodiscard]] ModelPreparationPlan runModelPreparation( const ModelPreparationRequest &request, std::stop_token stop,
													   const ModelPreparationCallback &progress) ;

/** @brief UIからpollingで受領し、HWNDをworkerへ渡さない逐次controllerです */
class ModelPreparationController   final {
	  public:
	  //------------------------------------------------------------------------------
	  /**
	   * @brief 一つの待機workerを開始します
	   * @param[in] runner 所有する同期処理。空は禁止
	   * @param[in] diagnostics 全workerより長寿命の通常sink、nullは旧試験のI/O0
	   */
	  //------------------------------------------------------------------------------
	  explicit ModelPreparationController( ModelPreparationRunner runner = runModelPreparation,
										  LocalDiagnosticLog	*diagnostics = nullptr) ;
	  //------------------------------------------------------------------------------
	  /**
	   * @brief 停止要求後にjoinします。UIはstoppedを確認してから破棄します
	   */
	  //------------------------------------------------------------------------------
	  ~ModelPreparationController() ;
	  //------------------------------------------------------------------------------
	  /**
	   * @brief 一準備workerと受付slot・操作ID・取消し・最新通知の対応を複製しないため、copy構築を禁止します
	   * @note 名前のない第1引数（const ModelPreparationController &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	   */
	  //------------------------------------------------------------------------------
	  ModelPreparationController( const ModelPreparationController &) = delete ;
	  //------------------------------------------------------------------------------
	  /**
	   * @brief 一準備workerと受付slot・操作ID・取消し・最新通知の対応を複製しないため、copy代入を禁止します
	   * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	   * @note 名前のない第1引数（const ModelPreparationController &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	   */
	  //------------------------------------------------------------------------------
	  ModelPreparationController						&operator=( const ModelPreparationController &) = delete ;
	  //------------------------------------------------------------------------------
	  /**
	   * @brief 空きslotへ固定要求を受け付けます
	   * @param[in] request 所有権を移す要求
	   * @return 新ID。実行中・終端未受領・停止中・ID枯渇なら0
	   */
	  //------------------------------------------------------------------------------
	  [[nodiscard]] std::uint64_t						 submit( ModelPreparationRequest request) ;
	  //------------------------------------------------------------------------------
	  /**
	   * @brief 現在のIDだけへ協調取消しを要求します
	   * @param[in] id 受付番号。未知・受領済みの番号は無視
	   */
	  //------------------------------------------------------------------------------
	  void												 cancel( std::uint64_t id) ;
	  //------------------------------------------------------------------------------
	  /**
	   * @brief 新規受付を停止し、現在操作を取り消します。joinしません
	   */
	  //------------------------------------------------------------------------------
	  void												 requestStop() ;
	  //------------------------------------------------------------------------------
	  /**
	   * @brief worker終了を照合します
	   * @return 終了時true
	   */
	  //------------------------------------------------------------------------------
	  [[nodiscard]] bool								 stopped() const noexcept ;
	  //------------------------------------------------------------------------------
	  /**
	   * @brief 最新通知を受領します
	   * @return 未更新ならnullopt。終端受領で枠を解放
	   */
	  //------------------------------------------------------------------------------
	  [[nodiscard]] std::optional<ModelPreparationEvent> takeEvent() ;

	  private:
	  //------------------------------------------------------------------------------
	  /**
	   * @brief 要求をmutexの外で実行し、thread境界の例外を通知へ変換します
	   */
	  //------------------------------------------------------------------------------
	  void									 run() noexcept ;
	  //------------------------------------------------------------------------------
	  /**
	   * @brief 同じ未完了操作の通知だけを差し替えます
	   * @param[in] event 所有通知。古いID・終端後の通知は無視
	   */
	  //------------------------------------------------------------------------------
	  void									 publish( ModelPreparationEvent event) noexcept ;
	  /// worker専用の同期処理
	  ModelPreparationRunner				 runner_ ;
	  /// 呼出元が全worker終了まで所有するborrow
	  LocalDiagnosticLog					*diagnostics_ ;
	  /// 要求・通知・受付状態だけを保護
	  std::mutex							 mutex_ ;
	  /// 要求または停止の待機
	  std::condition_variable				 changed_ ;
	  /// 最大1件の所有要求
	  std::optional<ModelPreparationRequest> pending_ ;
	  /// 現在操作の最新通知
	  ModelPreparationEvent					 event_ ;
	  /// 現在操作だけの取消し
	  std::stop_source						 cancellation_ ;
	  /// wrap後は0として受付を停止
	  std::uint64_t							 nextId_ = 1 ;
	  /// 終端未受領も含めたslot使用中
	  bool									 occupied_ = false ;
	  /// 未受領の更新あり
	  bool									 dirty_ = false ;
	  /// 新規受付禁止
	  bool									 stopping_ = false ;
	  /// 全処理が終了した状態
	  std::atomic<bool>						 stopped_ = false ;
	  /// 他memberの初期化後に開始
	  std::jthread							 worker_ ;
} ;

} // namespace koefude
