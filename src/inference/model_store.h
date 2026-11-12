//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file model_store.h
 * @brief 固定モデルの定義と検証中のfile所有権を宣言します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 モデルhash検証の契約を追加
 * 0.1.0 Shunji Ogawa 2026/09/09 large-v3 FP16とSilero VADの固定用途を追加
 * 0.1.0 Shunji Ogawa 2026/09/09 モデルと品質の対応・表示・予算を単一registryへ集約
 * 0.1.0 Shunji Ogawa 2026/09/10 発話の取りこぼしを減らすFP32話者モデルへ用途を更新
 * 0.1.0 Shunji Ogawa 2026/09/10 文字起こしprofileごとの発話検出設定をregistryへ集約
 * 0.1.0 Shunji Ogawa 2026/09/10 ASR実装と補助token file・時刻幅を単一registryへ追加
 * 0.1.0 Shunji Ogawa 2026/09/11 FP32 graphと外部weightの検証・所有を単一registryへ追加
 * 0.1.0 Shunji Ogawa 2026/09/12 製品から独立した日本語LM比較用の固定用途を追加
 * 0.1.0 Shunji Ogawa 2026/09/13 準備計画用の取得metadata・共有用途と検証APIを追加
 * 0.1.0 Shunji Ogawa 2026/10/19 local全文検証用途を末尾へ追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1283の専用Direct取得の用途を明記
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372の独立話者投影と元原文保持を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "inference/whole_text_runtime.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>

#include "core/engines.h"
#include "platform/win_helpers.h"

namespace koefude {

/** @brief models.lock.json内の固定された用途を表します */
enum class ModelId {
	/// 日本語ASR用の多言語turbo Q8
	WHISPER = 0,
	/// 話者区間推定用のFP32モデル
	SEGMENTATION = 1,
	/// ファイル内話者clustering用のCAM++埋込みモデル
	EMBEDDING = 2,
	/// 品質優先の多言語large-v3 FP16
	WHISPER_LARGE = 3,
	/// CPUで発話の有無を検出するSilero VAD
	VOICE_ACTIVITY = 4,
	/// 日本語Kotoba-Whisper v2.0のFP16
	KOTOBA = 5,
	/// 日本語Kotoba-Whisper v2.0のQ5_0
	KOTOBA_QUANTIZED = 6,
	/// 日本語Parakeet CTCのINT8 ONNX
	PARAKEET = 7,
	/// 同じrevisionの文字token定義
	PARAKEET_TOKENS = 8,
	/// 日本語Parakeet CTCのFP32 graph
	PARAKEET_FLOAT = 9,
	/// FP32 graphから参照する固定外部weight
	PARAKEET_FLOAT_DATA = 10,
	/// 比較専用の日本語GPT-2 FP32と固定小文字化metadata
	JAPANESE_LM = 11,
	/// 同じ比較用revisionのSentencePieceモデル
	JAPANESE_LM_TOKENS = 12,
	/// 専用Direct取得とlocal導入に使う全文補正weight
	WHOLE_TEXT_GEMMA = 13,
	/// 16GB評価専用の末尾用途
	WHOLE_TEXT_MINISTRAL14_B = 14
} ;

/** @brief 単一registryからworker内で選択するnative ASR実装です */
enum class AsrEngine {
	/// 検証済みGPUまたはCPUで使うwhisper.cpp
	WHISPER_CPP,
	/// sherpa-onnxのCUDA/CPU CTC経路
	PARAKEET_CTC,
	/// 固定OpenAI音声API。local ASRでは実行しません
	OPEN_AI_TRANSCRIPTIONS
} ;

/** @brief build時にmodels.lock.jsonから生成する不変metadataです */
struct ModelDefinition {
	/// pathや音声を含まない固定モデル識別子
	std::string_view  id ;
	/// model rootからの相対file名
	std::wstring_view file ;
	/// 配布元artifactの固定SHA-256、小文字hex
	std::string_view  sha256 ;
	/// 検証する正確なfile size
	std::uint64_t	  bytes ;
	/// registryに固定した配布revision。旧synthetic定義の未記録は空
	std::string_view  revision{} ;
} ;

/** @brief 固定artifactから必要fileを準備する方式です */
enum class ModelTransferKind {
	/// 転送したfileをそのまま最終検証する
	DIRECT,
	/// 検証済みarchiveから固定memberを取り出す
	TAR_BZIP2,
	/// 検証済み小型graphへ固定metadataを追記する
	METADATA_APPEND,
	/// 比較専用資産。製品の取得対象にしない
	UNAVAILABLE
} ;

/** @brief registryから生成する取得・条件表示用の不変metadataです */
struct ModelDownloadDefinition {
	/// native準備時の変換方式
	ModelTransferKind kind ;
	/// 許可されたHTTPS取得先。比較専用は空
	std::string_view  url ;
	/// hashと併せて固定した配布revision
	std::string_view  revision ;
	/// 元weightを含む配布条件の識別子
	std::string_view  license ;
	/// 配布条件と変換元を確認する一次資料URL
	std::string_view  source ;
	/// 圧縮やmetadata追記前の正確な転送byte数
	std::uint64_t	  transferBytes ;
	/// 転送artifactの小文字SHA-256
	std::string_view  transferSha256 ;
	/// archive内の固定通常file。その他の方式は空
	std::string_view  archiveMember ;
	/// 固定metadataのhex。追記方式以外は空
	std::string_view  suffixHex ;
} ;

/** @brief 設定に応じて追加する共有モデルを単一registryで定義します */
struct SharedModelRoles {
	/// speech_filter有効時のVAD
	ModelId voiceActivity ;
	/// diarization有効時の区間推定
	ModelId segmentation ;
	/// diarization有効時の話者埋込み
	ModelId embedding ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief local全文モデルの生成用途を返します
 * @return 単一lock由来の固定用途
 */
//------------------------------------------------------------------------------
[[nodiscard]] ModelId						 wholeTextNativeModelId() noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 明示方式の単一registry用途を返します
 * @param[in] variant 固定方式
 * @return registryの対応用途。不明値は拒否します
 */
//------------------------------------------------------------------------------
[[nodiscard]] ModelId						 wholeTextNativeModelId( WholeTextRuntimeVariant variant) ;

//------------------------------------------------------------------------------
/**
 * @brief 共有用途を取得します
 * @return process寿命中有効な不変参照
 */
//------------------------------------------------------------------------------
[[nodiscard]] const SharedModelRoles		&sharedModelRoles() noexcept ;

//------------------------------------------------------------------------------
/**
 * @brief 固定用途の取得metadataを返します
 * @param[in] id 単一registryの用途
 * @return process寿命中有効な不変参照
 * @throws OperationError 未定義の用途の場合
 */
//------------------------------------------------------------------------------
[[nodiscard]] const ModelDownloadDefinition &modelDownloadDefinition( ModelId id) ;

/** @brief 単一registryから生成するASR構成と日本語UI resourceの対応です */
struct AsrDefinition {
	/// localだけの具体的weight。APIは未設定
	std::optional<ModelId> modelId ;
	/// UIで選択するモデル系統
	AsrModel			   family ;
	/// 系統内の品質設定
	AsrProfile			   profile ;
	/// モデル選択欄の日本語resource ID
	unsigned int		   familyResource ;
	/// 品質選択欄の構成説明resource ID
	unsigned int		   choiceResource ;
	/// 実行結果へ表示する構成名resource ID
	unsigned int		   resultResource ;
	/// trueなら自動判定を使用せず日本語に固定する
	bool				   japaneseOnly ;
	/// 事前照合する保守的なGPU必要予算（byte）
	std::uint64_t		   gpuBytes ;
	/// 実測対象adapterでのAuto選択を許可する検証状態
	bool				   gpuValidated ;
	/// 前区間から引き継ぐ最大token数。0は引継ぎを無効化
	int					   historyTokens ;
	/// 発話確率のしきい値、千分率で1～999
	int					   vadThresholdMilli ;
	/// 別発話へ区切る無音の最短時間、100～2000ms
	int					   vadSilenceMs ;
	/// 発話の前後に残す元音声、0～1000ms
	int					   vadPaddingMs ;
	/// UIから独立したworker内の実装選択
	AsrEngine			   engine ;
	/// 同時に検証・lockする文字token file。不要なら空
	std::optional<ModelId> tokensModel ;
	/// tokenが表す発火frame幅（ms）。対応時刻を使わない構成は0
	int					   frameMs ;
	/// 発話検出に渡すcopyだけへ小さい声の補助gainを適用する
	bool				   vadGain ;
	/// graphと同時に検証・lockする外部weight
	std::optional<ModelId> externalDataModel ;
	/// APIだけの厳密要求ID。localでは空
	std::string_view	   apiModelId{} ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 利用可能な全ASR構成を返します
 * @return process寿命中有効な不変view
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::span<const AsrDefinition> asrDefinitions() noexcept ;

//------------------------------------------------------------------------------
/**
 * @brief 選択可能な構成が単一registryに存在するか確認します
 * @param[in] family モデル系統
 * @param[in] profile 系統内の構成
 * @return 存在すればprocess寿命の借用pointer、未定義ならNULL
 */
//------------------------------------------------------------------------------
[[nodiscard]] const AsrDefinition			*findAsrDefinition( AsrModel family, AsrProfile profile) noexcept ;

//------------------------------------------------------------------------------
/**
 * @brief モデルと品質を一つの固定weightへ解決します
 * @param[in] family 利用者が選択したモデル系統
 * @param[in] profile 選択した系統内の品質
 * @return process寿命中有効なregistryの不変参照
 * @throws OperationError 未定義の組合せならInvalidInput
 */
//------------------------------------------------------------------------------
[[nodiscard]] const AsrDefinition			&asrDefinition( AsrModel family, AsrProfile profile) ;

/** @brief 検証済みモデルと、その変更を防ぐ読取りhandleを所有します */
struct VerifiedModel {
	/// worker内でengineへ渡すpath
	std::filesystem::path path ;
	/// engine解放までwrite/deleteを拒否する所有handle
	UniqueHandle		  lock ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 用途に対応する単一registryの定義を返します
 * @param[in] id 固定されたモデル用途
 * @return processの寿命中有効な不変定義への参照
 * @throws OperationError 未定義の用途の場合
 */
//------------------------------------------------------------------------------
[[nodiscard]] const ModelDefinition &modelDefinition( ModelId id) ;

//------------------------------------------------------------------------------
/**
 * @brief 固定sizeとSHA-256を確認し、engine終了までfile変更を防ぎます
 * @param[in] root 利用者が選んだlocal model directory
 * @param[in] id 検証する固定用途
 * @param[in] stop hash読取り中の取消し
 * @return 検証済みpathと所有lock。返り値をengine contextより長く保持すること
 * @throws OperationError 未準備、hash不一致、I/O失敗、取消し
 */
//------------------------------------------------------------------------------
[[nodiscard]] VerifiedModel			 verifyModel( const std::filesystem::path &root, ModelId id, std::stop_token stop) ;

//------------------------------------------------------------------------------
/**
 * @brief 信頼済み定義のfileを固定size/hashで照合し変更拒否lockを所有します
 * @param[in] root local model directory
 * @param[in] definition 呼出し中有効なregistryまたはtestの信頼済み期待値。外部入力は禁止
 * @param[in] stop hash読取り中の取消し
 * @return 内容一致なら検証済みpathと所有lock、size/hash不一致なら空。engineより長く保持
 * @throws OperationError 欠落、属性拒否、I/O失敗、取消し。I/O失敗は空結果に変換しません
 * @note worker専用のblocking APIです。mutex保持中に呼び出しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::optional<VerifiedModel> checkModelFile( const std::filesystem::path &root,
														  const ModelDefinition &definition, std::stop_token stop) ;

} // namespace koefude
