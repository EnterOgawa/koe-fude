//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_text_correction.h
 * @brief 明示選択した原文を送るAPI補正の所有契約を定義します
 * @version 1.0.0
 * @date 2026/11/08
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/08 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1344のAPI専用契約を追加
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1345の動的モデル・凍結単価と旧履歴互換を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1348の行メニュー直接実行と保存後表示を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1349の一回適用と全単価表示へ更新
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1351のAPI専用HTTP失敗・保存状態を接続
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1360の新規v3と旧v2保存snapshotの契約を明記
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1407のキー所有・保存OFF削除・例外時状態を明記
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1409の役割・引数・戻り値と所有寿命を具体化
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
#include "core/api_model_catalog.h"
#include "core/reading_view.h"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace koefude {
using ApiTextDigest = std::array<std::uint8_t, 32> ; // SHA-256の所有値
constexpr std::size_t K_API_TEXT_SELECTION_LIMIT = 2 * 1024 * 1024 ; // 選択UTF-8上限
constexpr std::size_t K_API_TEXT_CANDIDATE_LIMIT = 8 * 1024 * 1024 ; // 部分候補を含む上限
/** @brief APIの処理状態と保存状態を分けます */
enum class ApiTextState {
	/// 原文・Pending保存済み。自動再送しません
	PENDING,
	/// 送信intent保存済み。課金結果不明の可能性があります
	SENDING,
	/// 完全候補。保存ack後にだけ提示・採用可能
	CANDIDATE_READY,
	/// 不完全生成。空本文を含み採用不可
	INCOMPLETE,
	/// 原文と既知usageを保持する失敗
	FAILURE,
	/// 協調取消し。課金停止を保証しません
	CANCELLED,
	/// 再起動時の未完。再開・再送しません
	INTERRUPTED
} ;
/** @brief 送信後の課金の不確定性です */
enum class ApiTextBilling {
	/// SendRequest未到達
	NOT_SENT,
	/// 送信試行後の結果不明
	OUTCOME_UNKNOWN,
	/// usageの既知fieldあり。実課金額ではありません
	USAGE_OBSERVED,
	/// 応答受領済み、利用量未通知
	USAGE_MISSING
} ;
/** @brief 生成tokenと内訳をpresence付きで保持します */
struct ApiTextUsage {
	/// 全入力token
	std::optional<std::uint64_t> input ;
	/// reasoning込み出力token
	std::optional<std::uint64_t> output ;
	/// inputの部分集合
	std::optional<std::uint64_t> cached ;
	/// outputの部分集合
	std::optional<std::uint64_t> reasoning ;
	/// 未確認fieldはunknown
	std::optional<std::uint64_t> cacheWrite ;
} ;
/** @brief 原文byte位置の一回だけの被覆です */
struct ApiTextChunk {
	/// target先頭byte
	std::size_t	  begin = 0 ;
	/// target終端byte
	std::size_t	  end = 0 ;
	/// 前文脈先頭byte
	std::size_t	  before = 0 ;
	/// 後文脈終端byte
	std::size_t	  after = 0 ;
	/// USD百万分の一の予約概算
	std::uint64_t estimate = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 文章API計画の対象・文脈範囲と予約概算が変更されていないかを比較します
	 * @param[in] other 比較する別の計画区間。呼出し中だけ借用します
	 * @return UTF-8 byte位置begin/end/before/afterとestimate（USD百万分の一）がすべて一致すればtrue。それ以外はfalseです
	 * @note 指定範囲の本文は参照せず、UTF-8境界や計画全体の妥当性も検証しません
	 */
	//------------------------------------------------------------------------------
	bool		  operator==( const ApiTextChunk &other) const = default ;
} ;
/** @brief 対象全文と設定を束縛する不変draftです */
struct ApiTextDraft {
	/// 空は凍結v1、非空は保存v2または新規話者保持v3
	std::string				  catalogSnapshot ;
	/// 同じカードの全原文key
	ReadingSourceKey		  source ;
	/// 本文と標準話者番号だけ
	std::string				  projection ;
	/// 原順に一度だけ被覆
	std::vector<ApiTextChunk> chunks ;
	/// 今回のUSD予算百万分の一
	std::uint64_t			  budget = 5000000 ;
	/// 全chunkの事前概算
	std::uint64_t			  estimate = 0 ;
	/// 原文・設定・plan・予算の束縛
	ApiTextDigest			  digest{} ;
} ;
/** @brief 全文確認送信と行メニュー送信の実際の意図を区別します */
enum class ApiTextIntent {
	/// 旧全文preview後の明示送信
	PREVIEW_BUTTON,
	/// 対象行全文と事前設定による明示送信。preview実施を意味しません
	ROW_MENU
} ;
/** @brief 一回の明示送信へ束縛する同意です */
struct ApiTextConsent {
	/// OS乱数、再利用なし
	std::array<std::uint8_t, 16> id{} ;
	/// 対象全文/設定のdigest
	ApiTextDigest				 draft{} ;
	/// 対象を指定した明示操作時だけtrue
	bool						 confirmed = false ;
	/// 実際の操作種別
	ApiTextIntent				 intent = ApiTextIntent::PREVIEW_BUTTON ;
} ;
/** @brief 秘密を履歴DTOへ渡さないRAII所有型です */
class ApiKeySecret final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief キーの形式を検証し、破棄時にゼロ化する秘密ownerを構築します
	 * @param[in] value 所有移管する1～2048byteの秘密。各byteは33～126の表示ASCIIで、空白を許可しません
	 *                  不正時も呼出し元のmove済み値へ復元しません
	 * @throws OperationError 不正な長さ/byteなら、所有した秘密をゼロ化してInvalidInputで構築を失敗させます
	 * @note APIへの疎通やキーの有効性照会は行いません
	 */
	//------------------------------------------------------------------------------
	explicit ApiKeySecret( std::string value) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有キーをゼロ化します
	 */
	//------------------------------------------------------------------------------
	~ApiKeySecret() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 秘密の複製経路を増やさないためcopy構築を禁止します
	 * @note 名前のない第1引数（const ApiKeySecret &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	ApiKeySecret( const ApiKeySecret &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 秘密の複製経路を増やさないためcopy代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const ApiKeySecret &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	ApiKeySecret				  &operator=( const ApiKeySecret &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief workerのtransportへ渡すため、所有キーの読取り専用viewを借用します
	 * @return 検証済みの非空ASCII秘密を指す非所有view。ApiKeySecretが解放責任を持ちます
	 *         このownerを使用終了まで保持し、破棄後はviewを使用しないでください
	 * @note 秘密を複製・表示・log・履歴へ渡す許可を意味しません。借用自体は通信や確保を行いません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::string_view borrow() const noexcept
	{
		return value_ ;
	}

	private:
	/// serialize/log/display禁止
	std::string value_ ;
} ;
/** @brief 固定targetだけを扱うworker専用キー保管境界です */
class IApiKeyStore {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief workerのキー保管ownerを解放します
	 */
	//------------------------------------------------------------------------------
	virtual ~IApiKeyStore() = default ; // workerで破棄
	//------------------------------------------------------------------------------
	/**
	 * @brief workerのsessionキーを設定/解除し、明示した保存方式を固定資格情報targetへ反映します
	 *
	 * @param[in] key 検証済み秘密を共有所有するlease。非nullなら成功後もstoreが共有所有し、
	 * acquireで同じ秘密を使います。nullptrはsessionと固定targetの削除要求です
	 * 借用文字列を保存せず、他ownerが保持する秘密まで消去する契約ではありません
	 * @param[in] persist 非nullキーでtrueなら、現在ユーザーの固定targetへ資格情報を書込みます
	 * falseはsessionだけに保持し、既存の同target資格情報も削除します
	 * keyがnullptrなら本値によらず削除します。他targetは操作しません
	 * @throws OperationError 固定targetへの書込み/削除に失敗した場合はSaveFailedと
	 * 元Win32 codeを返します。削除時のERROR_NOT_FOUNDは成功扱いです
	 * @pre 同じjob workerに呼出しを直列化し、acquire/persistedを含む同一storeへの並行操作をしません
	 * @post 永続操作の成功後だけsession/読取済み状態/保存有無を更新します
	 * 例外時は従前のsessionとstore内状態を保持します。正常復帰時は指定した保存/削除を
	 * Windowsが受理済みです。OS資格情報とsessionを跨ぐtransactionや、他processによる
	 * 後続変更・OS障害に対する永続先の独自rollback/完全耐久性までは保証しません
	 * @note 同じOpenAIキーを使う文章改善/音声APIの保管境界です。キー登録だけでAPIを送信せず、
	 * 秘密を履歴・平文config・logへ渡しません
	 */
	//------------------------------------------------------------------------------
	virtual void			   set( std::shared_ptr<const ApiKeySecret> key, bool persist) = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief storeが最後に確認したキーの永続保存状態を照会します
	 * @return Windows実装は成功したset/acquireで固定した保存状態を返します。初回acquire前はfalse
	 *         このinterfaceの既定実装もfalseで、永続保存を報告しません
	 * @pre 同じworkerでset/acquireを含む操作を直列化し、破棄と並行しないこと
	 * @note 呼出すたびに資格情報を再照会しません。他processによる後続変更の実在確認ではありません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual bool persisted() const noexcept
	{
		return false ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定targetから初回だけ読み、以後はsessionにcacheした秘密の共有ownerを取得します
	 * @return 設定済みなら不変秘密の共有owner、未設定ならnullptr。後のsetでstoreが切り替わっても、
	 *         取得済みownerはその最後の共有参照を解放するまで生存します
	 * @throws OperationError 読取失敗はSaveFailedと元Win32 code、不正な保管size/秘密はInvalidInput
	 * @throws std::bad_alloc 秘密stringまたは共有ownerの確保に失敗した場合
	 * @pre 同じworkerでset/persistedを含む操作を直列化し、破棄と並行しないこと
	 * @post Windows実装では読取成功または未設定だけをcacheし、読取例外を未設定として固定しません
	 * @note 取得はAPI通信を行わず、未設定と読取異常を区別します
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual std::shared_ptr<const ApiKeySecret> acquire() = 0 ;
} ;
/** @brief 一回のHTTP終端で、生bodyはprovider以外へ渡しません */
struct ApiHttpResponse {
	/// 実HTTP status
	unsigned	  status = 0 ;
	/// WinHTTP元code
	std::uint32_t error = 0 ;
	/// SendRequestを一回呼んだ事実
	bool		  attempted = false ;
	/// 最大4MiB、エラー時は保存/表示禁止
	std::string	  body ;
} ;
/** @brief 同期workerへasync WinHTTPを閉じ込める境界です */
class IApiHttpTransport {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 全callback終了後にtransportを解放します
	 */
	//------------------------------------------------------------------------------
	virtual ~IApiHttpTransport() = default ; // callback終了後に破棄
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定HTTPS先へ一回だけPOSTします
	 * @param[in] body 最大1.25MiBのJSON
	 * @param[in] key 借用秘密
	 * @param[in] stop 協調取消し
	 * @param[in] deadline operation期限
	 * @return 有界な終端事実
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual ApiHttpResponse post( std::string_view body, const ApiKeySecret &key, std::stop_token stop,
											   std::chrono::steady_clock::time_point deadline) = 0 ;
} ;
/** @brief 生errorの自由文を保持しない固定診断です。省略値はUnknownです */
enum class ApiTextHttpError : std::uint32_t {
	/// 未通知・未知・不正、既存履歴の既定
	UNKNOWN,
	/// rate_limit_error / slow_down
	RATE_LIMIT,
	/// credit_balance_exhausted
	CREDIT_BALANCE,
	/// organization_spend_limit_exceeded
	ORGANIZATION_SPEND,
	/// project_spend_limit_exceeded
	PROJECT_SPEND,
	/// organization_usage_limit_exceeded
	ORGANIZATION_USAGE,
	/// 課金/利用枠の広い分類、内訳は不明
	INSUFFICIENT_QUOTA,
	/// service_unavailable_error / server_is_overloaded
	SERVICE_OVERLOADED
} ;
/** @brief 一chunkの保存可能な終端です */
struct ApiTextChunkResult {
	/// 構造検証済み全文だけtrue
	bool					   complete = false ;
	/// 取得済み本文、未完は採用不可
	std::string				   text ;
	/// 最大256byteの提供元ID
	std::string				   responseId ;
	/// 実応答model
	std::string				   model ;
	/// 既知部分を保持
	ApiTextUsage			   usage ;
	/// 実tier未通知とdefaultを区別
	std::optional<std::string> serviceTier ;
	/// v2実model/tier不一致は料金不明
	bool					   priceApplicable = true ;
	/// 課金実額ではありません
	ApiTextBilling			   billing = ApiTextBilling::NOT_SENT ;
	/// 生bodyなしの診断
	unsigned				   httpStatus = 0 ;
	/// 既知code/typeだけの固定値
	ApiTextHttpError		   httpError = ApiTextHttpError::UNKNOWN ;
	/// 元通信/構造code
	std::uint32_t			   code = 0 ;
} ;
/** @brief 同じworkerが利用するAPI文章補正器の境界です */
class IApiTextCorrector {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief transport借用を終了します
	 */
	//------------------------------------------------------------------------------
	virtual ~IApiTextCorrector() = default ; // transportのborrowより先に破棄
	//------------------------------------------------------------------------------
	/**
	 * @brief 確認済みの一chunkを補正します
	 * @param[in] draft 不変投影
	 * @param[in] index 0始まりのplan位置
	 * @param[in] key 秘密の同期borrow
	 * @param[in] stop 協調取消し
	 * @param[in] deadline 全operationの期限
	 * @return usage/部分本文を保持する終端
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual ApiTextChunkResult correctChunk( const ApiTextDraft &draft, std::size_t index,
														  const ApiKeySecret &key, std::stop_token stop,
														  std::chrono::steady_clock::time_point deadline) = 0 ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 固定OpenAI providerを作ります
 * @param[in,out] transport 全寿命内のborrow
 * @return 同期worker専用補正器
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::unique_ptr<IApiTextCorrector> makeApiTextCorrector( IApiHttpTransport &transport) ;
/** @brief 一operationの履歴所有値です */
struct ApiTextAttempt {
	/// controller発行ID
	std::uint64_t					operationId = 0 ;
	/// 送信前に固定した対象snapshot
	ApiTextDraft					draft ;
	/// 一回の明示同意
	ApiTextConsent					consent ;
	/// 生成終端
	ApiTextState					state = ApiTextState::PENDING ;
	/// 順序通りの有限終端
	std::vector<ApiTextChunkResult> results ;
	/// 完全/部分本文
	std::string						candidate ;
	/// 同結果採用の束縛
	ApiTextDigest					candidateSha{} ;
	/// 秘密のない診断
	std::uint32_t					code = 0 ;
} ;
/** @brief V7には書かず、sidecarだけへ保持するruntime値です */
struct ApiTextCorrectionRecord {
	/// 最後の試行
	std::shared_ptr<const ApiTextAttempt> latest ;
	/// 耐久採用した全文。行メニューは完全保存後だけ
	std::shared_ptr<const ApiTextAttempt> adopted ;
	/// 耐久更新番号
	std::uint64_t						  update = 0 ;
	/// API固有の表示意図
	bool								  showOriginal = false ;
	/// memory ownerを保持する保存失敗
	bool								  saveFailed = false ;
} ;
/** @brief 保存ackは同原文・同試行へ束縛します */
struct ApiTextPersistenceCompletion {
	/// 原文共有owner
	std::shared_ptr<const Transcript> source ;
	/// 全原文key
	ReadingSourceKey				  key ;
	/// 元試行ID
	std::uint64_t					  operationId = 0 ;
	/// 既知usage/候補を含む値
	ApiTextCorrectionRecord			  record ;
	/// atomic置換直後の事実
	bool							  committed = false ;
	/// 保存元code
	std::uint32_t					  code = 0 ;
} ;
/** @brief 通常履歴workerだけが保存を行います */
class IApiTextPersistencePort {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief controller非所有の共有portを解放します
	 */
	//------------------------------------------------------------------------------
	virtual ~IApiTextPersistencePort() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同folder排他で表示cacheを読みます
	 * @return 未設定なら空
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual ApiModelCatalog loadCatalog()
	{
		return {} ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同folderへ検証済みmetadataだけを保存します
	 * @param[in] catalog 所有値
	 */
	//------------------------------------------------------------------------------
	virtual void saveCatalog( const ApiModelCatalog &catalog)
	{
		static_cast<void>( catalog) ;
	} // controller非所有の共有port
	//------------------------------------------------------------------------------
	/**
	 * @brief 保存または送信前barrierを同期要求します
	 * @param[in] source 同じ原文owner
	 * @param[in] key 原文key
	 * @param[in] op 元試行ID
	 * @param[in] record 候補値
	 * @param[in] barrier trueはV7を先に保存
	 * @return 失敗でも同候補を保持
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual ApiTextPersistenceCompletion commit( std::shared_ptr<const Transcript> source,
															  ReadingSourceKey key, std::uint64_t op,
															  ApiTextCorrectionRecord record, bool barrier) = 0 ;
} ;
/** @brief worker要求は秘密をserializeしません */
enum class ApiTextAction {
	/// 原文と全planの確認準備。HTTP0
	PREPARE,
	/// 一回の明示送信
	SEND,
	/// 同じ保存済み候補を採用。HTTP0
	ADOPT,
	/// 原文表示選好。HTTP0
	ORIGINAL,
	/// 採用した補正文の表示。HTTP0
	CORRECTED,
	/// 同じ結果の保存再試行。HTTP0
	RETRY_SAVE,
	/// mask入力キーのworker設定。HTTP0
	SET_KEY,
	/// 固定targetとsessionキーの削除。HTTP0
	ERASE_KEY,
	/// 明示TXT出力。HTTP0
	EXPORT,
	/// 最新候補だけの破棄。HTTP0
	DISCARD,
	/// 価格未取得でも原文と既存候補だけを閲覧。HTTP0
	PREVIEW,
	/// 行メニュー一回で準備・送信・完全保存後の採用表示
	IMPROVE
} ;
/** @brief 同じJobControllerへ渡す専用要求です */
struct ApiTextCorrectionJobRequest {
	/// 準備/送信で選択した全設定
	std::string							catalogSnapshot ;
	/// 直接実行受付時の正本世代
	std::uint64_t						catalogGeneration = 0 ;
	/// 直接実行受付時のキー世代
	std::uint64_t						keyRevision = 0 ;
	/// 明示操作
	ApiTextAction						action = ApiTextAction::PREPARE ;
	/// 原文owner
	std::shared_ptr<const Transcript>	source ;
	/// source identity
	ReadingSourceKey					key ;
	/// submitで発行
	std::uint64_t						operationId = 0 ;
	/// 確認全文
	std::shared_ptr<const ApiTextDraft> draft ;
	/// Sendだけで必要
	ApiTextConsent						consent ;
	/// 採用/再保存で再生成しない
	ApiTextCorrectionRecord				record ;
	/// SetKeyだけの非永続lease
	std::shared_ptr<const ApiKeySecret> secret ;
	/// opt-in既定OFF
	bool								persistKey = false ;
	/// SetKeyでworkerの現秘密を保持して保存方式だけ変更
	bool								retainKey = false ;
	/// USD百万分の一
	std::uint64_t						budget = 5000000 ;
	/// 明示TXT出力、送信bodyへ混ぜない
	std::filesystem::path				exportPath ;
} ;
/** @brief API workerの型付き終端です */
struct ApiTextJobCompletion {
	/// secretを除去済み受付情報
	ApiTextCorrectionJobRequest			request ;
	/// Prepare結果
	std::shared_ptr<const ApiTextDraft> draft ;
	/// workerで全量UTF-16へ変換済み
	std::wstring						preview ;
	/// workerで変換済みの候補
	std::wstring						candidate ;
	/// 保存/失敗の所有ack
	ApiTextPersistenceCompletion		persistence ;
	/// 値をUIへ返さない
	bool								keyReady = false ;
	/// 秘密を含まない保存状態
	bool								keyPersisted = false ;
	/// workerの現在キーrevision
	std::uint64_t						keyRevision = 0 ;
	/// 直接実行後の公開metadataだけ、秘密なし
	std::optional<ApiModelCatalog>		catalog ;
	/// 秘密のない診断
	std::uint32_t						code = 0 ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 新規確認snapshotへ固定する現指示を返します
 * @return UTF-8所有値
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string		 apiTextCurrentPrompt() ;
//------------------------------------------------------------------------------
/**
 * @brief legacy profileの指示・設定・歴史料金を返します
 * @return UTF-8 JSON
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string		 apiTextProfileSnapshot() ;
//------------------------------------------------------------------------------
/**
 * @brief SHA-256を計算します
 * @param[in] text UTF-8/固定byte
 * @return digest
 */
//------------------------------------------------------------------------------
[[nodiscard]] ApiTextDigest		 apiTextDigest( std::string_view text) ;
//------------------------------------------------------------------------------
/**
 * @brief 全文投影と有限planを作ります
 * @param[in] source 原文
 * @param[in] key 原文key
 * @param[in] budget USD百万分の一
 * @param[in] catalogSnapshot 保存v2/新規v3の全設定、空は旧読取用
 * @return 改変検出digest付きdraft
 */
//------------------------------------------------------------------------------
[[nodiscard]] ApiTextDraft		 makeApiTextDraft( const Transcript &source, ReadingSourceKey key,
												  std::uint64_t budget = 5000000, std::string catalogSnapshot = {}) ;
//------------------------------------------------------------------------------
/**
 * @brief 明示ボタンで同意を発行します
 * @param[in] draft 確認済み全文
 * @param[in] intent 実際の明示操作
 * @return OS乱数同意
 */
//------------------------------------------------------------------------------
[[nodiscard]] ApiTextConsent	 confirmApiText( const ApiTextDraft &draft,
												ApiTextIntent		intent = ApiTextIntent::PREVIEW_BUTTON) ;
//------------------------------------------------------------------------------
/**
 * @brief Responsesの固定JSONを作ります
 * @param[in] draft 確認全文
 * @param[in] index 0始まり
 * @return 有界JSON
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string		 apiTextRequestBody( const ApiTextDraft &draft, std::size_t index) ;
//------------------------------------------------------------------------------
/**
 * @brief 生HTTPを保存可能な型へ変換します
 * @param[in] response 生応答
 * @return 部分情報を保持する終端
 */
//------------------------------------------------------------------------------
[[nodiscard]] ApiTextChunkResult parseApiTextResponse( const ApiHttpResponse &response) ;
//------------------------------------------------------------------------------
/**
 * @brief 内訳を二重加算しない概算範囲です
 * @param[in] usage 取得済み内訳
 * @return USD百万分の一の下限/上限、矛盾はnullopt
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::optional<std::pair<std::uint64_t, std::uint64_t>> apiTextUsageCost( const ApiTextUsage &usage) ;
//------------------------------------------------------------------------------
/**
 * @brief 確認時の動的料金で内訳を計算します
 * @param[in] usage 内訳
 * @param[in] snapshot 凍結v2/v3値、空はlegacy
 * @return USD百万分の一下限/上限、不明はnull
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::optional<std::pair<std::uint64_t, std::uint64_t>> apiTextUsageCost( const ApiTextUsage &usage,
																					  std::string_view	  snapshot) ;
//------------------------------------------------------------------------------
/**
 * @brief 専用worker処理を実行します
 * @param[in] request 所有要求
 * @param[in,out] keys キー境界
 * @param[in,out] http HTTP境界
 * @param[in,out] port 保存worker
 * @param[in] stop 取消し
 * @param[in] progress 実段階・確定chunk率の通知、空可
 * @param[in,out] catalog 本番workerの現在値、legacy試験だけnull可
 * @param[in,out] metadata 直接実行の認証確認用、他操作ではnull可
 * @return 秘密なし終端
 */
//------------------------------------------------------------------------------
[[nodiscard]] ApiTextJobCompletion runApiTextJob( const ApiTextCorrectionJobRequest &request, IApiKeyStore &keys,
												 IApiHttpTransport &http, IApiTextPersistencePort &port,
												 std::stop_token stop, const ProgressCallback &progress = {},
												 ApiModelCatalog	  *catalog = nullptr,
												 IApiCatalogTransport *metadata = nullptr) ;
//------------------------------------------------------------------------------
/**
 * @brief 固定Windows保管を作ります
 * @return worker専用owner
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::unique_ptr<IApiKeyStore>		 makeApiKeyStore() ;
//------------------------------------------------------------------------------
/**
 * @brief 固定WinHTTP transportを作ります
 * @return worker専用owner
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::unique_ptr<IApiHttpTransport> makeApiTextHttp() ;
} // namespace koefude
