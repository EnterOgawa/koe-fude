//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file model_download.h
 * @brief 明示取得の進捗とnative/fakeの通信境界を定義します
 * @version 1.0.0
 * @date 2026/09/13
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/13 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/13 選択モデルの有界取得と確定の契約を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1283の用途13限定転送検査を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1397の対象用途/検証完了metadataを通知
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

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>

#include "inference/model_preparation.h"
#include "inference/model_store.h"

namespace koefude {

/** @brief 通常の認識jobと独立したモデル準備の段階です */
enum class ModelPreparationStage {
	/// localの有無・hashを確認中
	INSPECTING,
	/// 明示取得の操作待ち
	MISSING,
	/// 既存同梱fileからcacheへ準備中
	COPYING,
	/// 許可されたHTTPSから実byteを受信中
	DOWNLOADING,
	/// 固定size/hashの確認中
	VERIFYING,
	/// 検証済みartifactから必要fileを準備中
	EXTRACTING,
	/// 同じrootの全依存を検証済み
	READY,
	/// 所有処理の協調取消し待ち
	CANCELLING,
	/// 取消しと一時領域の回収を完了
	CANCELLED,
	/// 診断codeを保持した失敗
	FAILURE
} ;

/** @brief 時間から進捗を推測せず実byteを保持する値です */
struct ModelPreparationProgress {
	/// 現在の段階
	ModelPreparationStage  stage = ModelPreparationStage::INSPECTING ;
	/// この操作で実受信した合計byte数
	std::uint64_t		   transferredBytes = 0 ;
	/// 同じ操作の固定転送総量
	std::uint64_t		   transferBytes = 0 ;
	/// 現在対象の固定用途。pathを通知しない
	std::optional<ModelId> asset ;
	/// 当該対象の必要byte
	std::uint64_t		   requiredBytes = 0 ;
	/// 当該対象を検証してcommitした場合だけtrue
	bool				   verified = false ;
} ;

using ModelPreparationCallback = std::function<void( ModelPreparationProgress)> ; // workerで同期通知
using ModelDownloadSink = std::function<void( std::span<const std::byte>)> ; // 呼出し中だけ借用するbyte

/** @brief 通信だけをfakeへ差し替える逐次worker専用interfaceです */
class IModelTransport {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 通信資源を解放します
	 */
	//------------------------------------------------------------------------------
	virtual ~IModelTransport() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定HTTPS artifactを有界blockで受信先へ渡します
	 * @param[in] definition registryの信頼済み取得metadata。呼出し中有効
	 * @param[in] sink 同じworkerで呼ぶ受信先。NULL相当の空関数は禁止
	 * @param[in] stop 通信待ちを含む取消し
	 * @throws OperationError 通信・size・取得境界・受信先の失敗、取消し
	 * @note 返却前に全callbackを終了し、byteやcontextのborrowを残しません
	 */
	//------------------------------------------------------------------------------
	virtual void download( const ModelDownloadDefinition &definition, const ModelDownloadSink &sink,
						  std::stop_token stop) = 0 ;
} ;

/** @brief WinHTTPの非同期APIをworkerから使用する実通信です */
class WinHttpModelTransport final : public IModelTransport {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定HTTPS artifactを有界blockで受信先へ渡します
	 * @param[in] definition registryの信頼済み取得metadata。呼出し中有効
	 * @param[in] sink 同じworkerで呼ぶ受信先。NULL相当の空関数は禁止
	 * @param[in] stop 通信待ちを含む取消し
	 * @throws OperationError 通信・size・取得境界・受信先の失敗、取消し
	 * @note 返却前に全callbackを終了し、byteやcontextのborrowを残しません
	 */
	//------------------------------------------------------------------------------
	void download( const ModelDownloadDefinition &definition, const ModelDownloadSink &sink,
				  std::stop_token stop) override ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 通信と事前表示が共有する許可hostを返します
 * @return process寿命中有効な列
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::span<const std::wstring_view> modelDownloadHosts() noexcept ;

//------------------------------------------------------------------------------
/**
 * @brief 通信資源を作らず固定転送の容量と取得先を検査します
 * @param[in] definition 通常8GiB以内、超過は用途13の全field一致だけを許可します
 * @throws OperationError 不正容量・方式・URL。通信とsink呼出しはありません
 */
//------------------------------------------------------------------------------
void											 validateModelTransfer( const ModelDownloadDefinition &definition) ;
//------------------------------------------------------------------------------
/**
 * @brief redirect先を通信前に既存HTTPS境界で検査します
 * @param[in] url 一時queryを含められる取得先。保存しません
 * @throws OperationError scheme/host/port/認証/文字列の拒否
 */
//------------------------------------------------------------------------------
void											 validateModelDownloadLocation( std::wstring_view url) ;
//------------------------------------------------------------------------------
/**
 * @brief 次のredirectを既存の最大5回境界で検査します
 * @param[in] followed 既に追跡したredirect数
 * @throws OperationError 5回追跡済みなら元redirect失敗code
 */
//------------------------------------------------------------------------------
void											 validateModelDownloadRedirect( std::uint32_t followed) ;
//------------------------------------------------------------------------------
/**
 * @brief 全量応答のstatus/長さ/encodingを副作用なく検査します
 * @param[in] status 元HTTP status
 * @param[in] length 空可の十進Content-Length
 * @param[in] encoding 空またはidentity
 * @param[in] expected 固定転送byte
 * @throws OperationError 元HTTP statusまたは長さ・encodingの診断
 */
//------------------------------------------------------------------------------
void validateModelDownloadResponse( std::uint32_t status, std::wstring_view length, std::wstring_view encoding,
								   std::uint64_t expected) ;
//------------------------------------------------------------------------------
/**
 * @brief 有界blockの累計を加算前に検査します
 * @param[in] received 直前までの実byte
 * @param[in] bytes 次のblock、最大64KiB
 * @param[in] expected 固定転送byte
 * @return overflowしない新累計
 * @throws OperationError 過大応答または不正な累計
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::uint64_t		   advanceModelDownloadBytes( std::uint64_t received, std::uint64_t bytes,
															 std::uint64_t expected) ;

//------------------------------------------------------------------------------
/**
 * @brief cache準備に必要な追加空き容量の保守的な値を算出します
 * @param[in] plan 固定registryから生成した準備計画
 * @return 最終出力・最大一時archive・256MiBの余裕の合計byte数。Readyなら0
 * @throws OperationError 未知用途、容量の表現上限超過
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::uint64_t		   modelPreparationDiskBytes( const ModelPreparationPlan &plan) ;

//------------------------------------------------------------------------------
/**
 * @brief 許可済みの計画を再検査し、必要分だけをcacheへ確定します
 * @param[in] options 操作開始時に固定した選択構成
 * @param[in] bundleRoot 読取り専用の同梱root
 * @param[in] cacheRoot 検証したlocal cache root。未作成を許可
 * @param[in,out] transport 明示取得を担う同期境界。呼出し中有効
 * @param[in] stop コピー・通信・展開・検証の協調取消し
 * @param[in] progress 空を許すworker上の進捗通知
 * @return 全必要fileを一つのrootで検証した計画
 * @throws OperationError 取得境界、通信、file、hash、展開、競合、容量、取消し
 * @pre 利用者が選択構成の取得と通信に同意済みであること
 */
//------------------------------------------------------------------------------
[[nodiscard]] ModelPreparationPlan downloadModels( const EngineOptions &options, const std::filesystem::path &bundleRoot,
												  const std::filesystem::path &cacheRoot, IModelTransport &transport,
												  std::stop_token stop, const ModelPreparationCallback &progress) ;

} // namespace koefude
