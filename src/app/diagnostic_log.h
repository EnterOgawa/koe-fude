//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file diagnostic_log.h
 * @brief 試験起動だけで使う本文なしの診断イベントを定義します
 * @version 1.0.0
 * @date 2026/09/28
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/28 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/28 試験時の固定イベント記録を追加
 * 0.1.0 Shunji Ogawa 2026/10/03 結果の実行経路と不明値を本文なしで記録
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1287の明示診断と隔離接続を追加
 * 0.1.0 Shunji Ogawa 2026/10/22 ADR1323のcompiled三slot説明を訂正
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1407の具体的な契約と処理意図を記載
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実契約・所有寿命・戻り値の意味を具体化
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415のoperator括弧と文書lambdaの書式を補修
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 * @note 通常起動ではfileを作らず、音声・本文・pathをイベントへ渡しません
 */
//------------------------------------------------------------------------------
#pragma once

#include "core/correction_diagnostics.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

#include <windows.h>

#include "core/engines.h"

namespace koefude {

/** @brief 利用者のデータを含まない固定診断イベントです */
enum class DiagnosticEvent {
	/// 主Windowの作成完了
	APP_STARTED,
	/// message loopの正常終了
	APP_EXITED,
	/// 保存済み履歴の復元完了
	HISTORY_LOADED,
	/// 履歴読込の失敗
	HISTORY_LOAD_FAILED,
	/// 最新履歴の保存完了
	HISTORY_SAVED,
	/// 履歴保存の失敗
	HISTORY_SAVE_FAILED,
	/// モデル検査または明示取得の終端
	MODEL_CHECKED,
	/// 利用者の明示取得開始
	MODEL_DOWNLOAD_STARTED,
	/// 音声ジョブの受付
	JOB_ACCEPTED,
	/// 音声ジョブの終端
	JOB_FINISHED,
	/// 明示保存の受付
	SAVE_ACCEPTED,
	/// 明示保存の終端
	SAVE_FINISHED,
	/// clipboard copyの完了
	COPY_FINISHED,
	/// 選択ジョブの取消し要求
	CANCEL_REQUESTED,
	/// 受付時に固定したモデル・品質・要求backend
	JOB_CONFIGURED
} ;

/**
 * @brief 本文を渡さずに終端の実行事実だけを表します
 * @note 結果なしの場合、他のfieldを未観測のnullとして記録します
 */
struct DiagnosticExecution {
	/// 終端に結果DTOが存在する場合true
	bool	   resultAvailable = false ;
	/// 結果DTOのモデル。結果なしなら未観測
	AsrModel   model = AsrModel::PARAKEET ;
	/// 結果DTOの品質。結果なしなら未観測
	AsrProfile profile = AsrProfile::QUALITY ;
	/// 結果DTOの実GPU利用値。要求backendから推定しない
	bool	   usedGpu = false ;
	/// 結果DTOの同じモデルによるCPU再試行値
	bool	   cpuRetried = false ;
} ;

/** @brief 保存ackからUIが投影したscalarだけです */
struct DiagnosticCorrectionSave {
	/// 0不要、1実commit、2失敗、3未観測
	std::uint32_t				 state = 3 ;
	/// 実保存recordの0..6
	std::optional<std::uint32_t> attempt ;
	/// 実storage revision
	std::optional<std::uint64_t> revision ;
	/// 元owner照合結果
	std::optional<bool>			 originalRetained ;
	/// 前全文なしはnull
	std::optional<bool>			 previousRetained ;
	/// 実有効全文
	std::optional<std::uint32_t> activeKind ;
	/// 元HistoryErrorKind
	std::optional<std::uint32_t> error ;
	/// 元保存code
	std::optional<std::uint32_t> code ;
} ;
/** @brief 明示controlの固定scalar終端です */
struct DiagnosticCorrectionControl {
	/// 元履歴ID
	std::uint64_t				 historyId = 0 ;
	/// 元生成ID
	std::uint64_t				 operationId = 0 ;
	/// 別系列ID
	std::uint64_t				 actionId = 0 ;
	/// 再保存0、削除1
	std::uint32_t				 action = 0 ;
	/// 実metadata receipt
	bool						 committed = false ;
	/// 同じscalar投影
	DiagnosticCorrectionSave	 save ;
	/// 実戻り値だけ
	std::optional<bool>			 cleanupComplete ;
	/// 実残量byte
	std::optional<std::uint64_t> residualBytes ;
} ;
/**
 * @brief 明示した試験用fileのhandleと有限追跡状態を所有し、本文を含まない診断を記録します
 * @note 呼出し側で同一instanceへの操作を直列化します。内部に排他機構はありません
 * 空pathではfileを作らず、通常起動用のLocalDiagnosticLogとは独立しています
 * 作成・書込み・追跡上限の失敗はhealthy()へ残し、アプリの処理結果へ例外を伝えません
 * 最大4MiB/10,000行、生成/control/ASRの追跡は合計256操作です
 */
class DiagnosticLog final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 指定された試験用fileを新規作成します
	 * @param[in] path ローカルの既存directory内の新規file。空なら無効
	 * @note pathの内容をログへ記録せず、既存fileを上書きしません
	 */
	//------------------------------------------------------------------------------
	explicit DiagnosticLog( const std::filesystem::path &path) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有handleを閉じ、未有効の場合は何もしません
	 */
	//------------------------------------------------------------------------------
	~DiagnosticLog() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 試験用診断fileのhandleと有限追跡状態を一instanceだけに保つため、copy構築を禁止します
	 * @note 名前のない第1引数（const DiagnosticLog &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	DiagnosticLog( const DiagnosticLog &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 試験用診断fileのhandleと有限追跡状態を一instanceだけに保つため、copy代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const DiagnosticLog &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	DiagnosticLog &operator=( const DiagnosticLog &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定イベントと整数値だけを一行に追記します
	 * @param[in] event 固定のイベント種別
	 * @param[in] id 操作ID。該当しなければ0
	 * @param[in] value 状態または件数
	 * @param[in] auxiliary 追加の数値状態
	 * @param[in] code APIまたは製品の診断code
	 */
	//------------------------------------------------------------------------------
	void append( DiagnosticEvent event, std::uint64_t id = 0, std::uint64_t value = 0, std::uint64_t auxiliary = 0,
				std::uint32_t code = 0) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 結果の有無と実行経路をschema 2の追加イベントへ記録します
	 * @param[in] id 受付と同じ操作ID
	 * @param[in] execution enum/booleanだけの同期borrow。本文・pathを含みません
	 * @note 既存schema 1イベントを変更せず、ログ失敗は本処理へ伝播しません
	 */
	//------------------------------------------------------------------------------
	void appendExecution( std::uint64_t id, const DiagnosticExecution &execution) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 診断fileが有効で、記録や追跡に失敗を検出していないかを返します
	 * @return このinstanceの記録可否。処理対象の成功可否ではありません
	 * @retval true 有効なhandleを所有し、これまでの失敗を検出していません
	 * @retval false 空pathによる無効化、file作成/書込み失敗、上限超過等を検出しました
	 * @note 記録した処理自体の成功や、OS障害に対するログの耐久性を保証する値ではありません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool healthy() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 明示診断modeを開始し、session情報とcompiled三slotの実pinを記録します
	 * @pre 呼出し側が明示診断用の保存領域を隔離済みで、一sessionに一回だけ呼ぶこと
	 * @post 記録に失敗してもcurrentMode()はtrueを保持し、通常領域への切替えを防ぎます
	 * 二重呼出しは診断失敗として残します。製品の処理を開始する関数ではありません
	 */
	//------------------------------------------------------------------------------
	void			   beginCurrent() noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief beginCurrentによる明示診断sessionの開始有無を読みます
	 * @return 明示診断modeの開始有無
	 * @retval true 診断modeが開始されています。ログの記録失敗後もtrueです
	 * @retval false beginCurrentをまだ呼んでいません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool currentMode() const noexcept
	{
		return current_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 全文補正の受付成立を追跡し、同じ操作の後続終端を照合できるようにします
	 * @param[in] historyId 対象の履歴行ID。0不可
	 * @param[in] operationId 今回受け付けた生成操作ID。0不可。同じ組の重複登録は不可
	 * @param[in] variant compiled runtimeの固定slot番号。0～2
	 * @param[in] previous 受付前の有効全文種別。0はなし、1は旧V1、2はbounded全文です
	 * @note 通常modeでは何もせず、不正値・重複・追跡上限は診断だけを失敗にします
	 */
	//------------------------------------------------------------------------------
	void correctionAccepted( std::uint64_t historyId, std::uint64_t operationId, std::uint32_t variant,
							std::uint32_t previous) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief UIが受領した同じ生成操作の終端を、生成結果と実保存結果に分けて記録します
	 * @param[in] historyId correctionAcceptedに登録した履歴行ID
	 * @param[in] operationId 同じ受付に対応する生成操作ID。controlのaction IDではありません
	 * @param[in] snapshot workerの観測事実を保持する固定診断値。呼出し中だけ借用します
	 * @param[in] save 同じ保存ACKからUIが投影したscalar。未観測を成功へ補完しません
	 * @param[in] terminal 1は完全保存成功、2は生成失敗、3は取消し、4は保存失敗、5は未dispatch
	 * @param[in] error 元の製品エラー分類。未通知はnulloptで、成功を推定しません
	 * @param[in] code 元の診断code。未通知はnulloptで、既知の0と区別します
	 * @note 通常modeでは何もせず、未知/二重終端や不正値は診断だけを失敗にします
	 * 本文、path、任意例外文は引数へ含めず、生成や保存を再実行しません
	 */
	//------------------------------------------------------------------------------
	void correctionTerminal( std::uint64_t historyId, std::uint64_t operationId,
							const CorrectionDiagnosticSnapshot &snapshot, const DiagnosticCorrectionSave &save,
							std::uint32_t terminal, std::optional<ErrorKind> error,
							std::optional<std::uint32_t> code) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief control受付だけを記録します
	 * @param[in] value 固定ID/actionだけ
	 */
	//------------------------------------------------------------------------------
	void controlAccepted( const DiagnosticCorrectionControl &value) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief control専用slotの実結果を書きます
	 * @param[in] value 固定scalarだけ
	 */
	//------------------------------------------------------------------------------
	void controlTerminal( const DiagnosticCorrectionControl &value) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 保存履歴の復元事実を、新しい生成受付や生成成功と区別して記録します
	 * @param[in] historyId 復元した履歴行ID。0不可
	 * @param[in] operationId 保存されていた生成操作ID。0不可。新規IDを発行しません
	 * @param[in] attempt 保存済みWholeTextBoundedAttemptStateの固定数値
	 * @param[in] active 保存済みWholeTextActiveFullKindの固定数値
	 * @param[in] prefix 実際に復元したprefixのrecord件数。未観測はnulloptで、0件と区別します
	 * @param[in] complete 保存済みの完全本文参照が存在する場合trueです
	 * @note 呼出し側で固定数値を検証します。最大32件を記録し、通常modeでは何もしません
	 */
	//------------------------------------------------------------------------------
	void correctionRestored( std::uint64_t historyId, std::uint64_t operationId, std::uint32_t attempt,
							std::uint32_t active, std::optional<std::uint32_t> prefix, bool complete) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 全workerの排出後に未完操作と未保存件数を記録し、診断sessionを閉じます
	 * @param[in] unsaved UIが数えた未保存補正の件数。履歴上限内の0～32件
	 * @param[in] disposition 利用者が実際に選んだ終了方法の固定数値。未観測はnulloptです
	 * @pre workerの終端と保存ACKの回収を済ませ、以後新しい操作を登録しないこと
	 * @note 通常modeと終了済みsessionでは何もしません。未完操作を成功へ変えません
	 */
	//------------------------------------------------------------------------------
	void finishCurrent( std::uint32_t unsaved, std::optional<std::uint32_t> disposition) noexcept ;

	private:
	friend struct CurrentCorrectionDiagnosticsTestAccess ; // 専用fake試験だけの内部境界
	struct Tracked ; // 固定slotの前方宣言
	//------------------------------------------------------------------------------
	/**
	 * @brief 私有の固定formatter出力だけを書きます
	 * @param[in] line 内部ASCII、同期borrow
	 */
	//------------------------------------------------------------------------------
	void	 writeLine( std::string_view line) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ履歴行と生成操作の追跡slotを検索し、別系列のcontrolを除外します
	 * @param[in] historyId 受付時に登録した履歴行ID
	 * @param[in] operationId 同じ受付に対応する生成操作ID
	 * @return 一致するtracked_要素への非所有pointer。一致しなければnullptrです
	 * 解放せず、このinstanceの寿命内で同一操作を直列化して使用します
	 */
	//------------------------------------------------------------------------------
	Tracked *findCorrection( std::uint64_t historyId, std::uint64_t operationId) noexcept ;
	/** @brief 最大256操作のscalar追跡です */
	struct Tracked {
		/// 元履歴
		std::uint64_t historyId = 0 ;
		/// 元生成
		std::uint64_t operationId = 0 ;
		/// controlのみ非0
		std::uint64_t actionId = 0 ;
		/// control種別
		std::uint32_t action = 0 ;
		/// 二重終端拒否
		bool		  terminal = false ;
	} ;
	/// ASR/control/補正の合計上限
	std::array<Tracked, 256> tracked_{} ;
	/// 実登録数
	std::size_t				 trackedCount_ = 0 ;
	/// 従来ASR受付数
	std::size_t				 asrCount_ = 0 ;
	/// 復元上限32
	std::size_t				 restoredCount_ = 0 ;
	/// 実書込み済みbyte
	std::uint64_t			 bytes_ = 0 ;
	/// 実書込み済み行数
	std::uint32_t			 lines_ = 0 ;
	/// 明示modeだけtrue
	bool					 current_ = false ;
	/// 終了後の新操作禁止
	bool					 finished_ = false ;
	/// 試験用の排他的な所有file
	HANDLE					 file_ = INVALID_HANDLE_VALUE ;
	/// OS単調時刻の起点
	std::uint64_t			 startedMs_ = 0 ;
	/// 途中書込みまたは作成の失敗
	bool					 failed_ = false ;
} ;

} // namespace koefude
