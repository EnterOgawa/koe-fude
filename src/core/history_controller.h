//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file history_controller.h
 * @brief 履歴の読込みと最新snapshotの保存を専用workerへ渡します
 * @version 1.0.0
 * @date 2026/09/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/10 最新1件への集約と終了時の保存待ちを追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1287の明示診断と隔離接続を追加
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1343の文章専用補正を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1344の明示API補正と独立保存を接続
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372の独立話者投影と元原文保持を接続
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

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "core/history.h"

namespace koefude {

/** @brief 履歴workerが最後に確定した状態です */
enum class HistoryEventKind {
	/// 起動時の読込みに成功
	LOADED,
	/// 起動時に読めず、元fileを保護して受付停止
	LOAD_FAILED,
	/// 指定revisionまで保存済み
	SAVED,
	/// 型付き保存再試行・削除の確定
	BOUNDED_SAVED,
	/// 指定revisionの保存に失敗。画面の結果は保持
	SAVE_FAILED
} ;

/** @brief 通常保存の原文owner/keyを保持し、UIへengineを渡しません */
struct SavedOriginalIdentity {
	/// 保存workerが計算した全field identity
	ReadingSourceKey				  key ;
	/// snapshotと同じ不変owner
	std::shared_ptr<const Transcript> source ;
} ;
/** @brief HWND・借用pointerを含めず履歴の状態を返す所有通知です */
struct HistoryEvent {
	/// 完了した操作の分類
	HistoryEventKind											 kind = HistoryEventKind::LOADED ;
	/// 保存要求ごとの単調増加番号。loadは0
	std::uint64_t												 revision = 0 ;
	/// load成功時だけの所有結果
	std::shared_ptr<const HistorySnapshot>						 loaded ;
	/// 失敗時の分類
	HistoryErrorKind											 error = HistoryErrorKind::INTERNAL ;
	/// 元の診断code
	std::uint32_t												 code = 0 ;
	/// 再保存結果
	std::shared_ptr<const WholeTextBoundedPersistenceCompletion> bounded ;
	/// controlの元履歴ID、失敗も保持
	std::uint64_t												 historyId = 0 ;
	/// controlの元生成ID
	std::uint64_t												 operationId = 0 ;
	/// control専用の単調ID、普通通知は0
	std::uint64_t												 actionId = 0 ;
	/// 0再保存、1明示削除
	std::uint32_t												 action = 0 ;
	/// 実commitだけを保持
	HistorySaveReceipt											 controlReceipt ;
	/// 削除実戻り値
	std::optional<WholeTextBoundedCleanupResult>				 cleanup ;
	/// 通常Savedの最大32件のbarrier
	std::vector<SavedOriginalIdentity>							 savedOriginals ;
} ;

/** @brief 一つのI/Oと最新1件の待機snapshotだけを所有する履歴workerです */
class HistoryController final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 専用threadでstorageの読込みを開始します
	 * @param[in] storage このcontrollerが所有する同期storage。非NULL
	 * @param[in] notifier UI起床通知。空を許可し、mutexの外で呼びます
	 */
	//------------------------------------------------------------------------------
	HistoryController( std::unique_ptr<IHistoryStorage> storage, std::function<void()> notifier) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 残る保存を終了してjoinします。UIではstopped確認後に破棄します
	 */
	//------------------------------------------------------------------------------
	~HistoryController() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 単一storage writerと共有portの対応を複製しないためcopy構築を禁止します
	 * @note 名前のない第1引数（const HistoryController &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	HistoryController( const HistoryController &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 単一storage writerと共有portの対応を複製しないためcopy代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const HistoryController &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	HistoryController										  &operator=( const HistoryController &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 最新の履歴snapshotを保存待ち一件へ集約し、履歴workerへ書込みを依頼します
	 * @param[in] snapshot 保存する不変履歴の共有owner。本文を深く複製せず、workerまで所有を保持します
	 *                     nullptrは受付を拒否します。呼出し元のmove済み値を復元する契約ではありません
	 * @return 受け付けた保存要求の非0の単調revision。実I/Oの完了や耐久保存成功ではありません
	 * @retval 0 Loaded通知の未受領、停止中、nullptr入力、またはrevision枯渇で拒否しました
	 * @throws std::system_error 受付mutexのlockに失敗した場合
	 * @post 未開始の旧snapshotは今回の値で置き換わります。実行中の保存自体は中断しません
	 * @note 保存結果はSaved/SaveFailedのrevisionで照合します。本文のhash計算やI/Oはworkerが行います
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t								   save( std::shared_ptr<const HistorySnapshot> snapshot) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 有界全文補正の予約・終端保存を同じ履歴workerへ渡す共有portを取得します
	 * @return 同一PortStateを共有所有する非NULLのIWholeTextPersistencePort。raw controller参照を保持しません
	 *         返却ownerはcontroller破棄後も生存できますが、履歴workerを再開しません
	 * @pre このcontrollerの破棄と並行して取得しないこと
	 * @note writer終了後の新操作は終了状態により拒否されます。待機・取消し・ACKの条件は
	 *       IWholeTextPersistencePortの各操作契約に従います。取得自体はI/Oや保存を開始しません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::shared_ptr<IWholeTextPersistencePort>   wholeTextPort() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief API文章改善の原文照合・採用保存を同じ履歴workerへ渡す共有portを取得します
	 * @return 同一PortStateを共有所有する非NULLのIApiTextPersistencePort。raw controller参照を保持しません
	 *         返却ownerはcontroller破棄後も生存できますが、履歴workerを再開しません
	 * @pre このcontrollerの破棄と並行して取得しないこと
	 * @note writer終了後の新操作は終了状態により拒否されます。待機・取消し・ACKの条件は
	 *       IApiTextPersistencePortの各操作契約に従います。取得自体はI/Oや保存を開始しません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::shared_ptr<IApiTextPersistencePort>	   apiTextPort() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 音声APIのintent・prefix・最終ACK保存を同じ履歴workerへ渡す共有portを取得します
	 * @return 同一PortStateを共有所有する非NULLのIApiAudioPersistencePort。raw controller参照を保持しません
	 *         返却ownerはcontroller破棄後も生存できますが、履歴workerを再開しません
	 * @pre このcontrollerの破棄と並行して取得しないこと
	 * @note writer終了後の新操作は終了状態により拒否されます。待機・取消し・ACKの条件は
	 *       IApiAudioPersistencePortの各操作契約に従います。取得自体はI/Oや保存を開始しません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::shared_ptr<IApiAudioPersistencePort>	   apiAudioPort() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 原APIとは独立した話者投影の保存を同じ履歴workerへ渡す共有portを取得します
	 * @return 同一PortStateを共有所有する非NULLのISpeakerTextPersistencePort。raw controller参照を保持しません
	 *         返却ownerはcontroller破棄後も生存できますが、履歴workerを再開しません
	 * @pre このcontrollerの破棄と並行して取得しないこと
	 * @note writer終了後の新操作は終了状態により拒否されます。待機・取消し・ACKの条件は
	 *       ISpeakerTextPersistencePortの各操作契約に従います。取得自体はI/Oや保存を開始しません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::shared_ptr<ISpeakerTextPersistencePort> speakerTextPort() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ生成試行の未保存terminalを、生成し直さず専用control枠で保存再試行に予約します
	 * @param[in] historyId 保存失敗したterminalの非0の履歴ID
	 * @param[in] operationId 同じterminalの非0の生成操作ID。新しい生成IDではありません
	 * @param[out] actionId 任意の受付ID出力。nullptr可。有効pointerならlock成功後に0へ初期化し、
	 *                      受付成立後だけcontrol専用系列の非0 IDを書きます。復帰後は保持しません
	 * @return 予約成立ならtrue。未読込み・停止・ID不正/枯渇・未受領control/実行commandあり、
	 *         未保存terminalなし、または対象ID不一致ならfalseで、actionIdの出力は0です
	 * @throws std::system_error 受付mutexのlockに失敗した場合。actionIdも変更しません
	 * @note trueは耐久保存成功ではありません。actionIdに対応する後続HistoryEventで確認します
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool						  retryBoundedSave( std::uint64_t historyId, std::uint64_t operationId,
															   std::uint64_t *actionId = nullptr) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 指定した有界補正試行の明示削除を専用control枠へ予約します
	 * @param[in] historyId 削除対象の非0の履歴ID
	 * @param[in] operationId 削除対象の非0の生成操作ID。受付後workerで対象を照合します
	 * @param[out] actionId 任意の受付ID出力。nullptr可。有効pointerならlock成功後に0へ初期化し、
	 *                      受付成立後だけcontrol専用系列の非0 IDを書きます。復帰後は保持しません
	 * @return 予約成立ならtrue。未読込み・停止・ID不正/枯渇・未受領control/実行commandあり、
	 *         または生成予約のlease使用中ならfalseで、actionIdの出力は0です
	 * @throws std::system_error 受付mutexのlockに失敗した場合。actionIdも変更しません
	 * @note trueは削除済みを意味しません。後続HistoryEventのmetadata確定とcleanup結果を確認します
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool						  discardBoundedAttempt( std::uint64_t historyId, std::uint64_t operationId,
																	std::uint64_t *actionId = nullptr) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 新規受付を止め、最後の待機snapshotと受付済み操作を処理して終了するよう要求します
	 * @throws std::system_error 共有portのmutexをlockできなかった場合
	 * @note 待機snapshotを捨てずworkerを起こします。joinや終了待ちは行わず、完了はstoppedで確認します
	 */
	//------------------------------------------------------------------------------
	void									  requestStop() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 最後の通知、portの終了設定、storage解放まで履歴workerが終了したか確認します
	 * @return worker末尾のreleaseに対応するacquire読取りがtrueなら終了済み、falseなら終了未確認
	 * @pre このcontrollerの寿命を呼出し完了まで保持し、破棄と並行しないこと
	 * @note 停止要求済み、保存成功、通知受領済みを表す値ではありません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool						  stopped() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 未受領control通知を優先し、なければ最新の通常通知を一度だけ取り出します
	 * @return 所有通知。新しい通知がなければnullopt。本文等のDTOは共有所有し、control結果を含む
	 *         通知の取得は処理成功を意味しません。LoadFailedを空履歴のLoadedとして扱わないでください
	 * @throws std::system_error 共有portのmutexをlockできなかった場合。通知は消費しません
	 * @post Loadedが通常slotにあれば保存受付のreadyを有効化します。controlを返す時はその専用枠を解放し、
	 *       通常slotは残します。通常通知を返す時だけそのslotを空にします
	 * @note 通知内のrevision/action_idと元要求を照合します。I/Oや新しい保存要求は開始しません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::optional<HistoryEvent> takeEvent() ;

	private:
	/** @brief 一回の保存に対応する不変値です */
	struct WriteRequest {
		/// 保存に対応する単調増加番号
		std::uint64_t						   revision = 0 ;
		/// 呼出し後も有効な共有所有値
		std::shared_ptr<const HistorySnapshot> snapshot ;
	} ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 読込み、集約保存、例外の通知、終了を専用threadで処理します
	 */
	//------------------------------------------------------------------------------
	void run() noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 通知を所有格納し、排他を解いてからUIを起こします
	 * @param[in] event 最新の確定状態
	 */
	//------------------------------------------------------------------------------
	void publish( HistoryEvent event) noexcept ;
	class PortState ; // 共有生存期間の非集約control/ack
	/// workerより先に初期化
	std::shared_ptr<PortState>		 port_ ;
	/// 呼出しと解放を専用workerに限定するstorage
	std::unique_ptr<IHistoryStorage> storage_ ;
	/// worker終了まで通知先を維持する所有callback
	std::function<void()>			 notifier_ ;
	/// 最大1件の未開始保存。最新版で置き換える
	std::optional<WriteRequest>		 pending_ ;
	/// 普通通知で上書きしない未受領1slot
	std::optional<HistoryEvent>		 controlEvent_ ;
	/// wrap後は0、受付前に拒否
	std::uint64_t					 nextActionId_ = 1 ;
	/// 最大1件の未受領通知
	std::optional<HistoryEvent>		 event_ ;
	/// 0へwrapした後は受付禁止
	std::uint64_t					 nextRevision_ = 1 ;
	/// storageを読み込めた場合だけ保存を許可
	bool							 ready_ = false ;
	/// 新しい要求を受け付けない状態
	bool							 stopping_ = false ;
	/// 最後の通知とstorage解放が完了した状態
	std::atomic<bool>				 stopped_ = false ;
	/// 通知失敗はUI timerで回収する
	std::atomic<bool>				 notificationFailed_ = false ;
	/// 全member初期化後に開始する専用thread
	std::jthread					 worker_ ;
} ;

} // namespace koefude
