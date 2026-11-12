//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_text_provenance.h
 * @brief 補正本文の保存出所と自由文を受け取らないイベント境界を定義します
 * @version 1.0.0
 * @date 2026/11/08
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/08 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1346の表示・通常補正ログを追加
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1348の行メニュー直接実行と保存後表示を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1350のTerra出所を既存列挙の末尾へ追加
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1351のAPI専用HTTP失敗・保存状態を接続
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実契約・所有寿命・戻り値の意味を具体化
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415のoperator括弧と文書lambdaの書式を補修
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/api_audio_transcription.h"
#include "core/api_text_correction.h"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>

namespace koefude {
/** @brief 自由文を格納できない既知モデル識別子です */
enum class ApiProvenanceModel {
	/// 実応答未通知
	MISSING,
	/// 既知ID以外
	UNVERIFIED,
	/// chunk間で相違
	MULTIPLE,
	/// gpt-6.1-sol
	GPT61_SOL,
	/// gpt-6-astra
	GPT6_ASTRA,
	/// gpt-6-luna
	GPT6_LUNA,
	/// gpt-6.1-terra、既存の整数値を維持して末尾追加
	GPT61_TERRA,
	/// 音声APIの要求/厳密応答だけ。text互換性には追加しません
	GPT_TRANSCRIBE
} ;
/** @brief 実応答の日付付きIDも整数だけで表します */
struct ApiProvenanceId {
	/// 固定列挙値
	ApiProvenanceModel model = ApiProvenanceModel::MISSING ;
	/// 0または検証済みYYYYMMDD
	std::uint32_t	   date = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 複数応答区間の検証済みモデル出所が一致するかを比較します
	 * @param[in] other 比較する別の型付き出所。呼出し中だけ借用します
	 * @return model列挙値とdate（0またはYYYYMMDD整数）が両方一致すればtrue。それ以外はfalseです
	 * @note 要求IDによる補完や日付の妥当性確認は行いません
	 */
	//------------------------------------------------------------------------------
	bool			   operator==( const ApiProvenanceId &other) const = default ;
} ;
/** @brief 本文を含まない一試行の出所です */
struct ApiTextProvenance {
	/// 保存draft由来
	ApiProvenanceId requested ;
	/// 保存応答由来、選択値で補完しません
	ApiProvenanceId actual ;
	/// 成功・取消し・未完を維持
	ApiTextState	state = ApiTextState::PENDING ;
	/// 生成試行ID
	std::uint64_t	operation = 0 ;
} ;
/** @brief 通常ログに許可する固定イベントです */
enum class ApiCorrectionEvent {
	/// 明示送信要求の開始、送信成功を意味しません
	STARTED,
	/// 要求終端
	TERMINAL,
	/// 採用操作
	ADOPT,
	/// 原文選択
	ORIGINAL,
	/// 採用本文選択
	CORRECTED,
	/// 同結果の保存再試行
	RETRY_SAVE,
	/// 保存済み出所の再読
	RESTORED
} ;
/** @brief path・本文・秘密・自由文を渡せないログ値です */
struct ApiCorrectionEventData {
	/// 固定種類
	ApiCorrectionEvent event = ApiCorrectionEvent::TERMINAL ;
	/// 履歴ID
	std::uint64_t	   history = 0 ;
	/// 操作ID
	std::uint64_t	   operation = 0 ;
	/// 元生成試行とモデル
	ApiTextProvenance  provenance ;
	/// 保存成功ack
	bool			   saved = false ;
	/// 対象本文の採用保存成功
	bool			   adopted = false ;
	/// 0はsummary、1から最大32は実応答
	std::uint32_t	   chunk = 0 ;
	/// HTTP未到達は0
	unsigned		   httpStatus = 0 ;
	/// 元通信/固定構造code
	std::uint32_t	   code = 0 ;
	/// 固定診断のみ
	ApiTextHttpError   httpError = ApiTextHttpError::UNKNOWN ;
	/// 固定用途: falseは従来文章改善、trueは明示音声送信
	bool			   audio = false ;
	/// 音声専用終端、textと混ぜない
	ApiAudioTerminal   audioTerminal = ApiAudioTerminal::PENDING ;
	/// 課金不明を0へ変換しない
	ApiTextBilling	   audioBilling = ApiTextBilling::NOT_SENT ;
	/// 実送信試行量、固定16kHz
	std::uint64_t	   audioSamples = 0 ;
	/// 固定plan区間数、最大50,000
	std::uint32_t	   audioUnits = 0 ;
} ;
/** @brief I/O失敗を補正処理へ伝播しない限定sinkです */
class IApiCorrectionEventLog {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief sink所有資源を解放します
	 */
	//------------------------------------------------------------------------------
	virtual ~IApiCorrectionEventLog() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 有限metadataだけを書きます
	 * @param[in] event 自由文なし値
	 * @return 記録成功、失敗でも補正本処理を変更しません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual bool append( const ApiCorrectionEventData &event) noexcept = 0 ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief IDを既知modelと日付へ制限します
 * @param[in] id 借用ID
 * @return 未通知/未検証を補完しない整数値
 */
//------------------------------------------------------------------------------
[[nodiscard]] ApiProvenanceId	   apiProvenanceId( std::string_view id) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 厳密要求へ同じ既知モデルの実応答だけを照合します
 * @param[in] requested 日付なしの厳密要求ID
 * @param[in] actual 厳密IDまたは実在日付suffix
 * @return 未知・別モデル・不正suffixはfalse。保存再読と実応答で同じ判定を使います
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool				   apiProvenanceMatches( std::string_view requested, std::string_view actual) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 固定文字だけのログ表示を返します
 * @param[in] id 型付きID
 * @return ASCII
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string		   apiProvenanceModelName( ApiProvenanceId id) ;
//------------------------------------------------------------------------------
/**
 * @brief live catalogなしで保存snapshotを読みます
 * @param[in] attempt 保存値
 * @return 要求/実応答を分けた出所
 */
//------------------------------------------------------------------------------
[[nodiscard]] ApiTextProvenance	   apiTextProvenance( const ApiTextAttempt &attempt) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 再読でownerが分かれた同じ保存試行を照合します
 * @param[in] left 保存値、null可
 * @param[in] right 保存値、null可
 * @return 原文key/生成ID/本文SHAが全一致
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool				   sameApiTextAttempt( const std::shared_ptr<const ApiTextAttempt> &left,
													  const std::shared_ptr<const ApiTextAttempt> &right) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief API原文選好を旧local補正より優先すべきか返します
 * @param[in] record 保存値
 * @return API使用済みで原文が明示選択されている場合true
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool				   apiTextOriginalSelected( const ApiTextCorrectionRecord &record) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 同じ通常worker処理へ限定ログを付けます
 * @param[in] request 明示要求
 * @param[in,out] keys キー境界
 * @param[in,out] http HTTP境界
 * @param[in,out] port 保存境界
 * @param[in] stop 取消し
 * @param[in] progress 実進捗
 * @param[in,out] catalog 正本、null可
 * @param[in,out] log 限定sink
 * @param[in,out] metadata 直接実行の固定GET境界、他はnull可
 * @return 通常処理と同じ所有終端
 * @throws OperationError 通常処理の拒否/失敗。ログ失敗は投げません
 */
//------------------------------------------------------------------------------
[[nodiscard]] ApiTextJobCompletion runApiTextJobLogged( const ApiTextCorrectionJobRequest &request, IApiKeyStore &keys,
													   IApiHttpTransport &http, IApiTextPersistencePort &port,
													   std::stop_token stop, const ProgressCallback &progress,
													   ApiModelCatalog *catalog, IApiCorrectionEventLog &log,
													   IApiCatalogTransport *metadata = nullptr) ;
//------------------------------------------------------------------------------
/**
 * @brief 現ユーザー専用履歴folder内の固定名sinkを作ります
 * @param[in] directory 空は通常履歴先
 * @return 遅延I/Oのowner
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::unique_ptr<IApiCorrectionEventLog> makeApiCorrectionEventLog( std::filesystem::path directory = {}) ;
} // namespace koefude
