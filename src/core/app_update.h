//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file app_update.h
 * @brief 明示更新の有界DTO、信頼境界と単独workerを定義します
 * @version 1.0.0
 * @date 2026/11/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 1.0.0 Shunji Ogawa 2026/11/10 新規作成
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1378の起動確認と利用者選択更新を追加
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
#include <array>
#include <atomic>
#include <compare>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>

namespace koefude {
constexpr std::size_t	K_UPDATE_METADATA_LIMIT = 1024 * 1024 ; // feedの最大byte
constexpr std::uint64_t K_UPDATE_INSTALLER_LIMIT = 4ULL * 1024 * 1024 * 1024 ; // installer上限
/** @brief 署名resourceと同じ範囲のstable三成分です */
struct UpdateVersion {
	/// major/minor/patch
	std::array<std::uint16_t, 3> parts{} ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 数値三成分を比較します
	 * @param[in] other 他版
	 * @return 辞書順
	 */
	//------------------------------------------------------------------------------
	auto						 operator<=>( const UpdateVersion &other) const = default ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief canonical stable版だけを受理します
 * @param[in] text vは任意
 * @return 不正/範囲超過はnullopt
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::optional<UpdateVersion> parseUpdateVersion( std::string_view text) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief canonical三成分を作ります
 * @param[in] version 検証済み版
 * @return ASCII版
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string				   updateVersionText( UpdateVersion version) ;
/** @brief 実行候補は固定repoの検証済みasset一件だけです */
struct UpdateAsset {
	/// stable三成分
	UpdateVersion				 version ;
	/// 正の最大4GiB
	std::uint64_t				 size = 0 ;
	/// GitHub digest
	std::array<std::uint8_t, 32> sha256{} ;
	/// 固定repo/tag/assetから再構築して照合したURL
	std::string					 url ;
} ;
/** @brief UI/logへ自由文やURLを渡さない固定診断です */
enum class UpdateError {
	/// 正常
	NONE,
	/// offline/timeout等
	NETWORK,
	/// 404/403等、statusを別保持
	HTTP,
	/// 不正feed/曖昧asset
	METADATA,
	/// byte数不一致
	SIZE,
	/// SHA256不一致
	HASH,
	/// trust未成立/unsigned
	SIGNATURE,
	/// pinned証明書以外
	PUBLISHER,
	/// 製品/版/architecture marker不一致
	PRODUCT,
	/// private staging I/O失敗
	IO,
	/// CreateProcess失敗
	LAUNCH,
	/// 利用者取消し/終了
	CANCELLED
} ;
/** @brief worker内部の固定例外。応答本文を保持しません */
struct UpdateFailure {
	/// 固定分類
	UpdateError	  error = UpdateError::METADATA ;
	/// 元OS値またはHTTP status
	std::uint32_t code = 0 ;
} ;
/** @brief HTTP bodyはparserだけへ所有転送します */
struct UpdateFeed {
	/// 実HTTP status
	std::uint32_t status = 0 ;
	/// 最大1MiB、UIへ渡しません
	std::string	  body ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief bounded JSONとassetを照合します
 * @param[in] bytes 有界UTF-8
 * @return 正規asset
 * @throws UpdateFailure 不正feed
 */
//------------------------------------------------------------------------------
[[nodiscard]] UpdateAsset parseUpdateFeed( std::string_view bytes) ;
//------------------------------------------------------------------------------
/**
 * @brief 手動追跡可能なasset転送先だけを受理します
 * @param[in] url Location値
 * @return release-assets固定host/pathの正規HTTPSならtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool		  allowedUpdateRedirect( std::string_view url) noexcept ;
/** @brief network/hash/trust/起動をworkerへ隔離する同期境界です */
class IUpdatePlatform {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有file/handleだけを解放します
	 */
	//------------------------------------------------------------------------------
	virtual ~IUpdatePlatform() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定feedを一回GETします
	 * @param[in] stop 取消し
	 * @return 有界応答
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual UpdateFeed check( std::stop_token stop) = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief streaming取得、hash/署名/版確認を完了しread lockを保ちます
	 * @param[in] asset 検証済み候補
	 * @param[in] stop 取消し
	 * @throws UpdateFailure 失敗し今回stagingをcleanup
	 */
	//------------------------------------------------------------------------------
	virtual void					 prepare( const UpdateAsset &asset, std::stop_token stop) = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じlocked fileを再照合し通常wizardを起動します
	 * @param[in] asset 同候補
	 * @param[in] stop 取消し、最終CreateProcess前まで有効
	 * @throws UpdateFailure 起動失敗、旧appのdataは変更しません
	 */
	//------------------------------------------------------------------------------
	virtual void					 launch( const UpdateAsset &asset, std::stop_token stop) = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 今回stagingだけを破棄します
	 */
	//------------------------------------------------------------------------------
	virtual void					 reset() noexcept = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定AppIdの導入先と実moduleを照合します
	 * @return installedならtrue
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual bool		 installed() = 0 ;
} ;
/** @brief UIが扱う有限の段階です */
enum class UpdateState {
	/// 未確認
	UNCHECKED,
	/// metadata取得中
	CHECKING,
	/// より新しい正規候補あり
	AVAILABLE,
	/// 正規応答の版は現在以下
	CURRENT,
	/// 未公開/通信失敗/不正応答
	UNAVAILABLE,
	/// 明示同意後の取得/検証中
	DOWNLOADING,
	/// 署名検証済み、終了更新の明示操作待ち
	READY,
	/// 協調closeの保存完了後に起動中
	LAUNCHING,
	/// 正規installer起動成功
	LAUNCHED,
	/// 取消し
	CANCELLED,
	/// 取得/検証/起動の失敗
	FAILURE
} ;
/** @brief URL/path/本文を含まない小ACKです */
struct UpdateEvent {
	/// 単調op、0は未実行
	std::uint64_t operation = 0 ;
	/// 実段階
	UpdateState	  state = UpdateState::UNCHECKED ;
	/// 候補または確認版
	UpdateVersion version ;
	/// 固定診断
	UpdateError	  error = UpdateError::NONE ;
	/// 元OS/HTTP値
	std::uint32_t code = 0 ;
	/// portable説明を区別
	bool		  installed = false ;
} ;
/** @brief 一要求/一終端ACKを所有し、UIでjoinしない更新controllerです */
class AppUpdateController final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有workerを開始します
	 * @param[in] current 現appの版
	 * @param[in] platform 非NULL同期境界
	 * @param[in] notify HWNDへの値なし通知、空可
	 */
	//------------------------------------------------------------------------------
	AppUpdateController( UpdateVersion current, std::unique_ptr<IUpdatePlatform> platform,
						std::function<void()> notify = {}) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief stopped後にjoinします。初期化失敗時は取消しを要求します
	 */
	//------------------------------------------------------------------------------
	~AppUpdateController() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 単一更新workerとplatform、取消し、未受領ACKの対応を複製しないため、copy構築を禁止します
	 * @note 名前のない第1引数（const AppUpdateController &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	AppUpdateController( const AppUpdateController &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 単一更新workerとplatform、取消し、未受領ACKの対応を複製しないため、copy代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const AppUpdateController &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	AppUpdateController						&operator=( const AppUpdateController &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 起動一回または手動確認を受理します
	 * @return op、重複/停止は0
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t				 check() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 明示取得を受理します
	 * @param[in] checkedOp 確認済み候補のop
	 * @return 新op、stale/重複は0
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t				 download( std::uint64_t checkedOp) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 保存済みidleの明示終了更新です
	 * @param[in] readyOp 準備済みop
	 * @return 新op、stale/重複は0
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t				 launch( std::uint64_t readyOp) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 当該処理だけを取消します
	 */
	//------------------------------------------------------------------------------
	void									 cancel() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 終端を一度回収します
	 * @return 新通知なしはnullopt
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::optional<UpdateEvent> takeEvent() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 受付停止と協調取消しを要求します
	 */
	//------------------------------------------------------------------------------
	void									 requestStop() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 通知とplatform解放完了を返します
	 * @return 完了時true
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool						 stopped() const noexcept ;

	private:
	//------------------------------------------------------------------------------
	/**
	 * @brief 受付を一slotへ固定します
	 * @param[in] kind 0確認/1取得/2起動
	 * @param[in] owner 前段op
	 * @return 新opまたは0
	 */
	//------------------------------------------------------------------------------
	std::uint64_t					 submit( unsigned kind, std::uint64_t owner) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief platform同期処理を単一workerで逐次実行します
	 */
	//------------------------------------------------------------------------------
	void							 run() noexcept ;
	/// 現版
	UpdateVersion					 current_ ;
	/// worker限定
	std::unique_ptr<IUpdatePlatform> platform_ ;
	/// pointerを渡さない通知
	std::function<void()>			 notify_ ;
	/// 小slotだけを保護
	std::mutex						 mutex_ ;
	/// worker待機だけ
	std::condition_variable			 condition_ ;
	/// 最大一要求
	std::optional<unsigned>			 pending_ ;
	/// 最大一終端
	std::optional<UpdateEvent>		 event_ ;
	/// 前段照合用
	UpdateEvent						 state_ ;
	/// worker更新、受付lock内は状態だけ
	std::optional<UpdateAsset>		 asset_ ;
	/// 当該opの取消し
	std::stop_source				 cancellation_ ;
	/// 単調op
	std::uint64_t					 next_ = 1 ;
	/// ACK待ちも新規拒否
	bool							 busy_ = false ;
	/// 受付終了
	bool							 stopping_ = false ;
	/// 全資源解放後
	std::atomic_bool				 stopped_ = false ;
	/// 同期基盤失敗をmutex外から一度回収
	std::atomic_uint				 fatal_ = 0 ;
	/// 単独所有
	std::jthread					 worker_ ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief native固定通信/検証を生成します
 * @return 未実行platform
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::unique_ptr<IUpdatePlatform> makeNativeUpdatePlatform() ;
} // namespace koefude
