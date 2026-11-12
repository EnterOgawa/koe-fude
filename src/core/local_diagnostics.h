//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file local_diagnostics.h
 * @brief 本文を受け取らない通常ローカル診断と有界writerを定義します
 * @version 1.0.0
 * @date 2026/11/11
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 1.0.0 Shunji Ogawa 2026/11/11 新規作成
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1397の通常診断を旧試験sinkから分離
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1409の役割・引数・戻り値と所有寿命を具体化
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415のoperator括弧と文書lambdaの書式を補修
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
#include <memory>
#include <optional>

#include "core/api_text_provenance.h"
#include "core/engines.h"
#include "inference/model_download.h"

namespace koefude {

constexpr std::uint64_t K_LOCAL_DIAGNOSTIC_FILE_BYTES = 2 * 1024 * 1024 ; // 一世代のbyte上限
constexpr std::size_t	K_LOCAL_DIAGNOSTIC_GENERATIONS = 5 ; // 現行と旧四世代
constexpr std::size_t	K_LOCAL_DIAGNOSTIC_QUEUE_CAPACITY = 256 ; // 固定容量の非I/O通知枠

/** @brief 自由文字列を持たない通常診断イベントです */
enum class LocalDiagnosticEvent {
	/// logger開始。Window作成前も観測する
	SESSION_STARTED,
	/// 全アプリworker終了後の正常message loop終端
	SESSION_ENDED,
	/// 次job選好の復元成否
	SETTINGS_RESTORED,
	/// 明示Applyの耐久ACK成否
	SETTINGS_APPLIED,
	/// 設定入口の操作
	SETTINGS_OPENED,
	/// モデル準備表示の入口
	PREPARATION_OPENED,
	/// 通信なし検査の受付
	MODEL_CHECK_REQUESTED,
	/// 明示取得の受付。実転送とは別
	MODEL_DOWNLOAD_REQUESTED,
	/// 準備workerの開始
	MODEL_STARTED,
	/// 実準備段階または対象の変化
	MODEL_STAGE,
	/// 検査結果と計画件数
	MODEL_PLAN,
	/// 固定一用途のcache/bundle検査と予定
	MODEL_ASSET_PLAN,
	/// 同じ準備opの終端
	MODEL_FINISHED,
	/// 同じjob IDのworker開始
	JOB_STARTED,
	/// 同一stage/tickを省く実段階
	JOB_STAGE,
	/// controllerで取消し/保存を確定した終端
	JOB_FINISHED,
	/// 従来の型付きAPIログからの追加投影
	API_EVENT
} ;

/** @brief モデル検査/取得と画面表示の発火元です */
enum class LocalDiagnosticReason {
	/// 該当しない
	NONE,
	/// 設定復元後の起動一回
	STARTUP,
	/// 明示Applyの保存ACK
	SETTINGS_APPLY,
	/// 再検査button
	EXPLICIT_RECHECK,
	/// 取得button
	EXPLICIT_DOWNLOAD,
	/// 設定menu
	SETTINGS_MENU,
	/// 設定内の準備画面button
	SETTINGS_PREPARATION,
	/// 新規音声の準備不足
	MISSING_FOR_JOB,
	/// 検査終端の不足/失敗
	MISSING_AFTER_CHECK,
	/// 明示PC内話者補助準備
	PROJECTION,
	/// 既存の再表示/依存再評価
	REFRESH
} ;

/** @brief 未観測と失敗を成功へまとめない終端分類です */
enum class LocalDiagnosticOutcome {
	/// 未終端または該当なし
	NONE,
	/// 全依存検証済み
	READY,
	/// 不足あり
	MISSING,
	/// size/hash不一致あり
	INVALID,
	/// 失敗
	DIAGNOSTIC_ERROR,
	/// 完了
	COMPLETED,
	/// 警告付き結果
	WARNING,
	/// 本文なし成功
	NO_SPEECH,
	/// 協調取消し
	CANCELLED
} ;

/** @brief pathを表現できない保存領域種別です */
enum class LocalDiagnosticStorage {
	/// 未確定/該当なし
	NONE,
	/// 現ユーザーのモデルcache
	USER_CACHE,
	/// アプリ配下のモデル
	APP_MODELS
} ;

/** @brief 共通job IDを用途と組み合わせて照合します */
enum class LocalDiagnosticPurpose {
	/// 該当なし
	NONE,
	/// PC内文字起こし
	LOCAL_ASR,
	/// 明示音声API
	AUDIO_API,
	/// 明示文章改善/保存操作
	TEXT_API,
	/// 明示API metadata/キー設定
	METADATA,
	/// 既存の保存/PC内補正など
	OTHER
} ;

/**
 * @brief ログへ渡せる固定metadataだけです。自由文/借用pointer/秘密は格納できません
 * @note 0/nullは未観測です。モデル用途は単一registryからだけ文字列化します
 */
struct LocalDiagnosticRecord {
	/// 固定event
	LocalDiagnosticEvent				  event = LocalDiagnosticEvent::JOB_STAGE ;
	/// 固定trigger
	LocalDiagnosticReason				  reason = LocalDiagnosticReason::NONE ;
	/// 実終端
	LocalDiagnosticOutcome				  outcome = LocalDiagnosticOutcome::NONE ;
	/// 保存領域の種別
	LocalDiagnosticStorage				  storage = LocalDiagnosticStorage::NONE ;
	/// 共通job用途
	LocalDiagnosticPurpose				  purpose = LocalDiagnosticPurpose::NONE ;
	/// sink系列ごとの単調ID
	std::uint64_t						  operation = 0 ;
	/// 元履歴の整数ID
	std::uint64_t						  history = 0 ;
	/// 既知ASR系統
	std::optional<AsrModel>				  model ;
	/// 選択品質
	std::optional<AsrProfile>			  profile ;
	/// 要求方針
	std::optional<Backend>				  requestedBackend ;
	/// 観測済み実装だけ
	std::optional<Backend>				  actualBackend ;
	/// 固定registry用途
	std::optional<ModelId>				  asset ;
	/// 件数と分離した当該用途の準備計画
	std::optional<ModelPreparationAction> assetAction ;
	/// 検証済みAPI ID。未知文字列を保持しない
	ApiProvenanceId						  apiModel ;
	/// API操作enum。件数へ流用しない
	std::optional<ApiTextAction>		  apiAction ;
	/// 音声段階
	std::optional<Stage>				  stage ;
	/// モデル準備段階
	std::optional<ModelPreparationStage>  modelStage ;
	/// APIの実段階
	std::optional<ApiTextPhase>			  phase ;
	/// 固定失敗分類
	std::optional<ErrorKind>			  error ;
	/// 元API/固定診断code
	std::uint32_t						  code = 0 ;
	/// HTTP未到達0
	std::uint32_t						  httpStatus = 0 ;
	/// 行/区間/用途数
	std::uint64_t						  count = 0 ;
	/// 実計画総数
	std::uint64_t						  total = 0 ;
	/// 必要/実転送byte
	std::uint64_t						  bytes = 0 ;
	/// 再利用予定file数
	std::uint64_t						  reused = 0 ;
	/// 実転送計画数
	std::uint64_t						  downloads = 0 ;
	/// 同梱copy予定数
	std::uint64_t						  copies = 0 ;
	/// 元音声の長さ
	std::optional<std::uint64_t>		  durationMs ;
	/// 実処理時間
	std::optional<std::uint64_t>		  processingMs ;
	/// 同じモデルでCPU再試行した実値
	std::optional<bool>					  cpuRetried ;
	/// 明示取得の許可。checkではfalse
	bool								  download = false ;
	/// 対象fileの検証/commit成功時だけtrue
	bool								  verified = false ;
} ;

/**
 * @brief 別writerが固定metadataを有限JSONLへ保存します
 * @note appendは複数threadから可、file I/Oなし。close/destructorは単一ownerから
 * 全producer終了後に呼び、queueを排出します。障害は本処理へ伝播しません
 */
class LocalDiagnosticLog final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 専用writerを開始します
	 * @param[in] directory 所有保存先。空は無効/I/O0
	 */
	//------------------------------------------------------------------------------
	explicit LocalDiagnosticLog( std::filesystem::path directory) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 終了eventを捏造せず、残queueを排出してwriterをjoinします
	 */
	//------------------------------------------------------------------------------
	~LocalDiagnosticLog() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 単一Stateと診断writerの専属所有を維持するためcopy構築を禁止します
	 * @note 名前のない第1引数（const LocalDiagnosticLog &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	LocalDiagnosticLog( const LocalDiagnosticLog &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 単一Stateと診断writerの専属所有を維持するためcopy代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const LocalDiagnosticLog &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	LocalDiagnosticLog						  &operator=( const LocalDiagnosticLog &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 本文を含まない固定metadataを有限queueへコピーし、別writerへ記録を依頼します
	 * @param[in] record 記録する小さい所有DTO。復帰後に呼出し元の値を参照しません
	 * @pre instanceを破棄せず、finishは全producerの終了後に単一ownerから呼ぶこと
	 * @note 複数threadから呼べ、file I/Oは行いませんがqueue mutexの待機はあり得ます
	 *       無効・終了・障害後は無操作、満杯ならdroppedを加算します。復帰は記録完了を意味しません
	 *       同期障害を外へ送出せずhealthyの障害状態へ保持します
	 */
	//------------------------------------------------------------------------------
	void									   append( LocalDiagnosticRecord record) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 正常message loopの終了recordを最終slotへ予約し、残queueを排出してwriterをjoinします
	 * @param[in] exitCode message loopの整数終了code。正常loop終了の実値を渡します
	 * @pre 全producer終了後に単一ownerから呼び、他のfinishや破棄と並行しないこと
	 * @note この呼出しはwriter終了を待ちます。停止状態を設定済みの重複呼出し、または無効instanceは無操作です
	 *       I/O/同期障害は本処理へ送出せずhealthyへ保持し、全recordの保存成功は保証しません
	 */
	//------------------------------------------------------------------------------
	void									   finish( std::uint32_t exitCode) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 通常診断の初期化やwriterで、記録障害を検出していないかを読みます
	 * @return このinstanceの検出済み障害の有無。ASRやAPI処理の成否ではありません
	 * @retval true Stateが存在し、failedが立っていません。writerの初回I/O前にもtrueになり得ます
	 * @retval false 無効化・初期化失敗でStateがないか、writer等が障害を検出しました
	 * @pre instanceの寿命を呼出し完了まで保持し、破棄と並行しないこと
	 * @note queue全件の耐久保存、記録の無欠落、writerが現在稼働中であることは保証しません
	 *       この読取り自体はfile I/Oを行いません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool						   healthy() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief queue満杯やwriterのlease競合等で省略した診断件数のsession累計を読みます
	 * @return Stateが保持する省略件数。Stateがなければ0です。単位は診断record件数です
	 * @pre instanceの寿命を呼出し完了まで保持し、破棄と並行しないこと
	 * @note 無効化や障害後にappendが何もせず戻る場合もあるため、0は全件の記録成功を意味しません
	 *       この読取り自体はfile I/Oを行いません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t				   dropped() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 技術試験で通常領域と隔離領域を照合できるよう、診断先pathを借用で返します
	 * @return State内の不変pathへの非所有const参照。Stateがなければstaticな空pathへの参照です
	 *         呼出し元は解放せず、このinstanceの寿命を越えて保持しないでください
	 * @pre instanceの寿命を借用の使用終了まで保持し、破棄と並行しないこと
	 * @note I/O障害やfinish後も、Stateがあれば保持しているpathを返します。記録成功の判定値ではなく、
	 *       このpath自体をログ本文へ書き出しません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const std::filesystem::path &directory() const noexcept ;

	private:
	struct State ; // 非同期I/O状態の単独所有実装
	/// 初期化失敗はnull
	std::unique_ptr<State> state_ ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 通常または明示隔離historyに対応する診断保存先を解決します
 * @param[in] historyDirectory 同期借用するhistory directory。空なら現在ユーザーのLocalAppData、
 *                             非空ならabsolute/lexically_normal後の親directoryを基準にします
 * @return 空入力ならLocalAppData/koefude/logs、非空なら正規化した親/logsの所有path
 *         OS解決やpath確保の失敗は空pathを返し、別領域へfallbackしません
 * @note directory作成や設定変更は行いません。返却pathをlog本文へ渡しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::filesystem::path localDiagnosticDirectory( const std::filesystem::path &historyDirectory) noexcept ;

//------------------------------------------------------------------------------
/**
 * @brief 既存API sinkへの記録を保ち、同じ固定metadataを通常診断にも複写するwrapperを作ります
 * @param[in] existing wrapperへ所有移管する既存sink。nullptr可。拒否時に呼出し元のmove済み値を復元しません
 * @param[in] log 複写先の非所有borrow。nullptr可。非NULLならwrapperの全append終了まで保持してください
 * @return wrapperの単独owner。wrapper確保失敗時はexistingのownerをそのまま返し、元も空ならnullptrです
 * @note wrapperのappendは既存sinkのboolを返し、既存sinkなしならfalseです
 *       通常診断への複写成否をAPI記録成功へ読み替えず、構築時には記録を開始しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::unique_ptr<IApiCorrectionEventLog>
makeLocalDiagnosticApiLog( std::unique_ptr<IApiCorrectionEventLog> existing, LocalDiagnosticLog *log) noexcept ;

} // namespace koefude
