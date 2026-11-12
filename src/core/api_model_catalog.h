//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_model_catalog.h
 * @brief APIモデル発見、検証済み単価、確認snapshotの境界を定義します
 * @version 1.0.0
 * @date 2026/11/08
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/08 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1345のmetadata限定取得を追加
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1347の音声テーブル・独立API設定と文章操作を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1348の行メニュー直接実行と保存後表示を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1349の一回適用と全単価表示へ更新
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1402の取得前候補と送信可否の独立性を明記
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
#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace koefude {
constexpr std::uint64_t K_API_NO_BUDGET_LIMIT =
	std::numeric_limits<std::uint64_t>::max() ; // 新規v2の利用者予算なし。技術上限は別途維持
class ApiKeySecret ; // 値をUIやcacheへ返さない秘密owner
class IApiKeyStore ; // workerだけが利用する固定target
class IApiTextPersistencePort ; // 既存folder排他の共有port
/** @brief USD/1M tokensを百万倍した整数、missingを0へ変換しません */
struct ApiModelPrice {
	/// Short5列、Long5列の順
	std::array<std::optional<std::uint64_t>, 10> rates{} ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 凍結した文章API単価表の値と未掲載状態を比較します
	 * @param[in] other 比較する別の単価表。呼出し中だけ借用します
	 * @return Standard Short/Long全10列のoptional単価が同じならtrue。未掲載nulloptと存在する0は異なります
	 * @note 単価の単位はUSD/1M tokensの百万倍です。取得UTC・資格・価格出典はこの型に含まず比較しません
	 */
	//------------------------------------------------------------------------------
	bool										 operator==( const ApiModelPrice &other) const = default ;
} ;
/** @brief 音声duration専用のUSD micro/min単価です */
struct ApiAudioPrice {
	/// 未掲載は空、text token単価と混ぜません
	std::optional<std::uint64_t> microPerMinute ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 凍結した音声duration単価の値と未掲載状態を比較します
	 * @param[in] other 比較する別の音声単価。呼出し中だけ借用します
	 * @return microPerMinute（USD百万分の一/min）のoptionalが同じならtrue。nulloptと存在する0は異なります
	 * @note 取得UTC・キー資格・実請求やusageは比較しません
	 */
	//------------------------------------------------------------------------------
	bool						 operator==( const ApiAudioPrice &other) const = default ;
} ;
/** @brief 公開表示だけに使うモデルmetadataです */
struct ApiModelInfo {
	/// 検証済みYYYY-MM-DD、空は未通知
	std::string shutdown ;
} ;
/** @brief componentごとの取得と最終試行を分離します */
struct ApiCatalogMetadata {
	/// 成功取得UTC秒、0は未取得
	std::int64_t						  fetched = 0 ;
	/// 最終試行UTC秒
	std::int64_t						  attempted = 0 ;
	/// 最終試行HTTP
	unsigned							  http = 0 ;
	/// 秘密なしの元診断
	std::uint32_t						  error = 0 ;
	/// 成功本文のSHA、本文は保存しません
	std::array<std::uint8_t, 32>		  sha{} ;
	/// process内だけの成功時刻
	std::chrono::steady_clock::time_point monotonic{} ;
} ;
/** @brief 秘密・本文を含まない一般API設定です */
struct ApiGeneralSettings {
	/// 検証済みモデルID、未取得でも選択を保持
	std::string	  model = "gpt-6.1-sol" ;
	/// 新規は利用者予算なし。旧数値は保持
	std::uint64_t budget = K_API_NO_BUDGET_LIMIT ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 文章APIの保存選好が再読前後で同じかを比較します
	 * @param[in] other 比較する別の保存選好。呼出し中だけ借用します
	 * @return UTF-8モデルID文字列とbudget整数（USD百万分の一）がともに一致すればtrue。それ以外はfalseです
	 * @note 旧数値予算と上限なしsentinelをそのまま比較し、設定の適格性やキー・metadataは検証しません
	 */
	//------------------------------------------------------------------------------
	bool		  operator==( const ApiGeneralSettings &other) const = default ;
} ;
/** @brief 単一job workerだけが変更するcatalogです */
struct ApiModelCatalog {
	/// 利用資格を推測しないID
	std::map<std::string, ApiModelInfo>	 models ;
	/// Flagship Standard表だけ
	std::map<std::string, ApiModelPrice> prices ;
	/// Models専用鮮度
	ApiCatalogMetadata					 modelMeta ;
	/// 公開価格専用鮮度
	ApiCatalogMetadata					 priceMeta ;
	/// gpt-transcribeのduration料金だけ
	ApiAudioPrice						 audioPrice ;
	/// 同じGETでもtext parserの成否と独立
	ApiCatalogMetadata					 audioPriceMeta ;
	/// 更新とキー変更ごとに増加
	std::uint64_t						 generation = 1 ;
	/// process内だけ、cacheへ保存しません
	std::uint64_t						 keyRevision = 1 ;
	/// 現sessionの成功キーrevision
	std::uint64_t						 modelsKeyRevision = 0 ;
	/// 別のprivate metadataで保存する一般設定
	ApiGeneralSettings					 settings ;
	/// cache初回読取済み
	bool								 loaded = false ;
	/// 未知版/破損の原cacheは上書きしません
	bool								 cacheCorrupt = false ;
	/// memory snapshotは保持
	bool								 saveFailed = false ;
	/// 更新中は送信不可
	bool								 refreshing = false ;
} ;
/** @brief 発見と送信互換性を分けた表示状態です */
enum class ApiModelAvailability {
	/// 全送信条件成立
	AVAILABLE,
	/// 音声・画像等の対象外ID
	INCOMPATIBLE,
	/// 厳密IDの互換性未確認
	CAPABILITY_UNVERIFIED,
	/// 必要料金なし
	PRICE_UNAVAILABLE,
	/// 未取得・鮮度・資格・消失
	UNAVAILABLE,
	/// 終了日到達
	SHUTDOWN
} ;
/** @brief 一つのcatalogから作るUIと送信の選択値です */
struct ApiCatalogSelection {
	/// 元IDを保持
	std::string			 id ;
	/// 送信可否
	ApiModelAvailability state = ApiModelAvailability::UNAVAILABLE ;
	/// 失敗後の期間内前回値で送信
	bool				 previous = false ;
	/// Availableだけの所有JSON、価格・能力・出典を凍結
	std::string			 snapshot ;
} ;
/** @brief metadata GETだけの有界応答です */
struct ApiCatalogHttpResponse {
	/// HTTP元status
	unsigned	  status = 0 ;
	/// 固定診断
	std::uint32_t error = 0 ;
	/// 有界ASCII
	std::string	  contentType ;
	/// 空またはidentityだけ許可
	std::string	  encoding ;
	/// Models1MiB、価格512KiB
	std::string	  body ;
} ;
/** @brief 公開価格へ秘密を渡せない固定typed transportです */
class IApiCatalogTransport {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief callback終了後に破棄します
	 */
	//------------------------------------------------------------------------------
	virtual ~IApiCatalogTransport() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定Modelsへ一回GETします
	 * @param[in] key 秘密borrow
	 * @param[in] stop 取消し
	 * @param[in] deadline operation期限
	 * @return 有界応答
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual ApiCatalogHttpResponse listModels( const ApiKeySecret &key, std::stop_token stop,
															std::chrono::steady_clock::time_point deadline) = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief Authorizationなしの固定価格GETです
	 * @param[in] stop 取消し
	 * @param[in] deadline operation期限
	 * @return 有界応答
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual ApiCatalogHttpResponse getPrices( std::stop_token						 stop,
														   std::chrono::steady_clock::time_point deadline) = 0 ;
} ;
/** @brief Transcript、path、本文を持たない専用更新要求です */
struct ApiCatalogJobRequest {
	/// submitが発行
	std::uint64_t					  operationId = 0 ;
	/// close/reopenのstale防止
	std::uint64_t					  windowGeneration = 0 ;
	/// 0は初回open、以後は一致必須
	std::uint64_t					  keyRevision = 0 ;
	/// キー設定後はfalse
	bool							  prices = true ;
	/// 起動時のlocal準備だけ。HTTP0、キー有無だけ返します
	bool							  localOnly = false ;
	/// 一般設定の保存だけ、HTTP0
	bool							  settingsOnly = false ;
	/// 明示適用、本文と秘密を持ちません
	std::optional<ApiGeneralSettings> settings ;
} ;
/** @brief UIへ秘密も生HTTPも渡さない所有終端です */
struct ApiCatalogJobCompletion {
	/// 元op/画面generation
	ApiCatalogJobRequest request ;
	/// 表示・選択の唯一の正本
	ApiModelCatalog		 catalog ;
	/// キー値なし
	bool				 keyReady = false ;
	/// 固定targetへの保存有無だけ
	bool				 keyPersisted = false ;
	/// cache警告もここへ保持
	std::uint32_t		 code = 0 ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 現在UTC秒を返します
 * @return Unix UTC秒
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::int64_t						   apiCatalogNow() ;
//------------------------------------------------------------------------------
/**
 * @brief Modelsのstrict JSONを読みます
 * @param[in] body UTF-8 JSON1MiB以内
 * @return 検証済みID
 * @throws OperationError 不正時、部分適用なし
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::map<std::string, ApiModelInfo>  parseApiModels( std::string_view body) ;
//------------------------------------------------------------------------------
/**
 * @brief 公式Flagship Standard11列だけを読みます
 * @param[in] body UTF-8 Markdown512KiB以内
 * @return presence付き料金
 * @throws OperationError 形式変更・曖昧な表
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::map<std::string, ApiModelPrice> parseApiPrices( std::string_view body) ;
//------------------------------------------------------------------------------
/**
 * @brief Audio tableだけからduration単価を読みます
 * @param[in] body 公式Markdown
 * @return presence付き単価
 * @throws OperationError 見出し/列/ID/単位の不整合
 */
//------------------------------------------------------------------------------
[[nodiscard]] ApiAudioPrice						   parseApiAudioPrice( std::string_view body) ;
//------------------------------------------------------------------------------
/**
 * @brief 厳密対応IDと取得済みIDから候補を作ります
 * @param[in] catalog 唯一の正本
 * @param[in] selected 保持するID
 * @param[in] now UTC秒
 * @return 未取得候補も含む和集合。送信条件成立時だけsnapshotを持ちます
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<ApiCatalogSelection>
apiCatalogChoices( const ApiModelCatalog &catalog, std::string_view selected, std::int64_t now = apiCatalogNow()) ;
//------------------------------------------------------------------------------
/**
 * @brief 行メニューのlocal前提だけを照合します。認証資格は送信前に別途確認します
 * @param[in] catalog local cacheと適用済み設定
 * @param[in] keyReady キー有無だけ
 * @param[in] now UTC秒
 * @return 厳密IDと料金が有効なら送信前準備用snapshot、他は空
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string			  apiMenuSnapshot( const ApiModelCatalog &catalog, bool keyReady,
													  std::int64_t now = apiCatalogNow()) ;
//------------------------------------------------------------------------------
/**
 * @brief snapshot単体を保存再読用に検証します
 * @param[in] snapshot 有界JSON
 * @return 検証済み価格
 * @throws OperationError 不正snapshot
 */
//------------------------------------------------------------------------------
[[nodiscard]] ApiModelPrice			  apiCatalogSnapshotPrice( std::string_view snapshot) ;
//------------------------------------------------------------------------------
/**
 * @brief snapshotの厳密モデルIDを返します
 * @param[in] snapshot 読取り専用の有界UTF-8 JSON snapshot。呼出し中だけ借用し、strict schemaを検証します
 * @return snapshot内で検証した厳密なモデルIDの独立所有stringです
 * @throws OperationError snapshotが不正、未対応schema、またはモデルIDの厳密条件を満たさない場合
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string			  apiCatalogSnapshotModel( std::string_view snapshot) ;
//------------------------------------------------------------------------------
/**
 * @brief workerの現在条件へdraftを照合します
 * @param[in] catalog runtime正本
 * @param[in] snapshot 確認時所有値
 * @return 現在も送信可能か
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool					  apiCatalogMatches( const ApiModelCatalog &catalog, std::string_view snapshot) ;
//------------------------------------------------------------------------------
/**
 * @brief キー変更で資格と同意を失効します
 * @param[in,out] catalog worker正本
 */
//------------------------------------------------------------------------------
void								  invalidateApiCatalogKey( ApiModelCatalog &catalog) ;
//------------------------------------------------------------------------------
/**
 * @brief 固定GETを直列実行します
 * @param[in] request 専用要求
 * @param[in,out] catalog 正本
 * @param[in,out] keys 固定キー
 * @param[in,out] http typed GET
 * @param[in,out] port 同folder保存
 * @param[in] stop 協調取消し
 * @return 部分成功を保持する所有終端
 */
//------------------------------------------------------------------------------
[[nodiscard]] ApiCatalogJobCompletion runApiCatalogJob( const ApiCatalogJobRequest &request, ApiModelCatalog &catalog,
													   IApiKeyStore &keys, IApiCatalogTransport &http,
													   IApiTextPersistencePort &port, std::stop_token stop) ;
//------------------------------------------------------------------------------
/**
 * @brief 固定WinHTTP GET境界を作ります
 * @return worker専用owner
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::unique_ptr<IApiCatalogTransport> makeApiCatalogHttp() ;
} // namespace koefude
