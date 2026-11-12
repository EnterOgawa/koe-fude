//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_audio_transcription.h
 * @brief 明示音声APIの有界plan、応答、料金、耐久終端を定義します
 * @version 1.0.0
 * @date 2026/11/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の初回API文字起こしを追加
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1370の文脈分割と明示途中再開を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1407の生成attempt検索の窓単位・借用寿命を明記
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
#include "core/api_text_correction.h"
#include "core/audio_identity.h"
#include "core/engines.h"
#include "core/pipeline.h"
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace koefude {
constexpr std::string_view K_API_AUDIO_MODEL = "gpt-transcribe" ; // 単一registryへ照合する要求ID
constexpr std::uint64_t	   K_API_AUDIO_WINDOW_SAMPLES = 600ULL * K_SAMPLE_RATE ; // 一窓、全録音上限ではない
constexpr std::uint64_t	   K_API_AUDIO_CONTEXT_SAMPLES = 120ULL * K_SAMPLE_RATE ; // 新規送信buffer上限
constexpr std::size_t	   K_API_AUDIO_MAXIMUM_ATTEMPTS = 64 ; // 明示操作単位の保存上限
constexpr std::size_t	   K_API_AUDIO_BODY_LIMIT = 20 * 1024 * 1024 ; // multipart全体のbyte予算
constexpr std::size_t	   K_API_AUDIO_RESPONSE_LIMIT = 1024 * 1024 ; // HTTP本文byte上限
constexpr std::size_t	   K_API_AUDIO_TEXT_LIMIT = 256 * 1024 ; // 一応答の本文byte上限
/** @brief API時刻ではなく元PCMの送信範囲です */
struct ApiAudioWindow {
	/// 元sample半開範囲の先頭
	std::uint64_t	 begin = 0 ;
	/// 元sample半開範囲の終端
	std::uint64_t	 end = 0 ;
	/// 元global cluster候補。不明は空、混在は複数
	std::vector<int> speakers ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 保存planの送信範囲と話者候補列が変更されていないかを比較します
	 * @param[in] other 比較する別窓。呼出し中だけ借用し、保持しません
	 * @return 16kHz元PCMの半開sample範囲begin/endと、speakersのID・順序がすべて同じならtrue。それ以外はfalseです
	 * @note 音声sampleや話者埋込み自体は比較せず、範囲の妥当性も検証しません
	 */
	//------------------------------------------------------------------------------
	bool			 operator==( const ApiAudioWindow &other) const = default ;
} ;
/** @brief duration料金をtext token単価から独立して凍結します */
struct ApiAudioPriceSnapshot {
	/// USD micro/min、未取得を0にしない
	ApiAudioPrice	   price ;
	/// 成功UTC/SHAと最終試行を保持
	ApiCatalogMetadata metadata ;
	/// 固定出典
	std::string		   source = "https://developers.openai.com/api/docs/pricing.md" ;
	/// duration table検証版
	std::uint32_t	   parser = 1 ;
} ;
/** @brief 全音声の同一性と非重複窓をPOST前に固定します */
struct ApiAudioPlan {
	/// 1従来話者境界、2文脈90秒。旧digestは維持
	std::uint32_t				version = 1 ;
	/// 全PCMを有界走査した証拠
	DecodedPcmIdentity			pcm ;
	/// falseだけlanguages[]=ja
	bool						autoLanguage = false ;
	/// 選択時のCPU話者解析
	bool						diarization = true ;
	/// VADの選択
	bool						speechFilter = true ;
	/// 元sample順、最大50,000
	std::vector<ApiAudioWindow> windows ;
	/// 無音gap結合を含む全送信時間
	std::uint64_t				sendSamples = 0 ;
	/// 当操作で有効だった出典
	ApiAudioPriceSnapshot		price ;
	/// 全plan概算、実請求ではない
	std::uint64_t				estimateMicroUsd = 0 ;
	/// PCM/設定/料金/全窓の束縛
	ApiTextDigest				digest{} ;
} ;
/** @brief APIが通知した利用量の形式です */
enum class ApiAudioUsageKind : std::uint32_t {
	/// 未通知または未知形式、ゼロ利用ではありません
	UNKNOWN,
	/// secondsを通知
	DURATION,
	/// token利用量。duration料金へ換算しません
	TOKENS
} ;
/** @brief 未通知とゼロを区別する観測利用量です */
struct ApiAudioUsage {
	/// 実通知形式
	ApiAudioUsageKind			 kind = ApiAudioUsageKind::UNKNOWN ;
	/// 有限・非負
	std::optional<double>		 seconds ;
	/// 未通知は空
	std::optional<std::uint64_t> inputTokens ;
	/// 未通知は空
	std::optional<std::uint64_t> outputTokens ;
	/// 未通知は空
	std::optional<std::uint64_t> totalTokens ;
	/// 不合理なduration/未知modelならfalse
	bool						 priceApplicable = false ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 観測値を比較します
	 * @param[in] other 比較値
	 * @return 全field一致
	 */
	//------------------------------------------------------------------------------
	bool						 operator==( const ApiAudioUsage &other) const = default ;
} ;
/** @brief 実model IDの通知有無を要求IDから分離します */
enum class ApiAudioModelReport : std::uint32_t {
	/// 通常JSONにはmodelがありません
	NOT_REPORTED,
	/// 要求と厳密一致、任意prefixは禁止
	VERIFIED,
	/// 未知IDを保存・表示せず料金だけ保留
	UNVERIFIED
} ;
/** @brief 一音声区間の保存可能な完全応答または固定診断です */
struct ApiAudioChunkResult {
	/// plan内0始まり位置
	std::uint32_t							index = 0 ;
	/// 完全JSON/text検証済み
	bool									complete = false ;
	/// 全文を一窓へ一度だけ束縛
	std::string								text ;
	/// 省略と空配列も区別
	std::optional<std::vector<std::string>> languages ;
	/// 料金実額ではない観測値
	ApiAudioUsage							usage ;
	/// 実出所
	ApiAudioModelReport						modelReport = ApiAudioModelReport::NOT_REPORTED ;
	/// 不明を無料へ変換しない
	ApiTextBilling							billing = ApiTextBilling::NOT_SENT ;
	/// 元HTTP statusのみ
	unsigned								http = 0 ;
	/// 元通信または固定検証code
	std::uint32_t							code = 0 ;
	/// 既知enumだけ
	ApiTextHttpError						httpError = ApiTextHttpError::UNKNOWN ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 保存prefixを比較します
	 * @param[in] other 比較値
	 * @return 全field一致
	 */
	//------------------------------------------------------------------------------
	bool									operator==( const ApiAudioChunkResult &other) const = default ;
} ;
/** @brief raw bodyはproviderだけが所有する一回のHTTP応答です */
struct ApiAudioHttpResponse {
	/// 元HTTP
	unsigned	  status = 0 ;
	/// 元通信code
	std::uint32_t error = 0 ;
	/// SendRequestを一回呼んだ事実
	bool		  attempted = false ;
	/// 最大1MiB。UI/logへ渡しません
	std::string	  body ;
} ;
/** @brief 任意URL/filename/話者名を受け取れない固定音声POST境界です */
class IApiAudioHttpTransport {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 全callbackと秘密borrowを終了します
	 */
	//------------------------------------------------------------------------------
	virtual ~IApiAudioHttpTransport() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 一窓だけを固定先へ送ります
	 * @param[in] samples 有限16kHz mono、最大600秒
	 * @param[in] autoLanguage 日本語hintを省略するか
	 * @param[in] key 秘密borrow
	 * @param[in] stop 協調取消し
	 * @param[in] deadline 当窓の期限
	 * @return 有界終端
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual ApiAudioHttpResponse post( std::span<const float> samples, bool autoLanguage,
													const ApiKeySecret &key, std::stop_token stop,
													std::chrono::steady_clock::time_point deadline) = 0 ;
} ;
/** @brief 完全JSON検証までを一同期呼出しへ閉じ込めます */
class IApiAudioTranscriber {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief transport borrowを終了します
	 */
	//------------------------------------------------------------------------------
	virtual ~IApiAudioTranscriber() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 一窓を一回送信し検証します
	 * @param[in] samples 有界PCM
	 * @param[in] autoLanguage 言語選択
	 * @param[in] key 秘密borrow
	 * @param[in] stop 取消し
	 * @return 完全本文または本文なしの固定診断。自動再送しません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual ApiAudioChunkResult transcribe( std::span<const float> samples, bool autoLanguage,
														 const ApiKeySecret &key, std::stop_token stop) = 0 ;
} ;
/** @brief 通常の成功と部分原文を伴う失敗を分離します */
enum class ApiAudioTerminal : std::uint32_t {
	/// plan耐久保存済み、送信なし
	PENDING,
	/// intent耐久保存済み、結果不明の可能性
	SENDING,
	/// 全窓と終端の耐久ACK後だけ成功
	COMPLETED,
	/// VADで0窓、POST0
	NO_SPEECH,
	/// 完全応答はあるが全本文が空
	EMPTY_TRANSCRIPT,
	/// prefixを保持、後続0
	FAILURE,
	/// prefixを保持、課金停止を保証しない
	CANCELLED,
	/// 保存再読で未完を復元。再開しない
	INTERRUPTED
} ;
/** @brief 本文を複製せず、一回の操作の価格と成功範囲を凍結します */
struct ApiAudioAttempt {
	/// 生成操作、再開ごとに新ID
	std::uint64_t					   operationId = 0 ;
	/// 当操作の共有キーrevision
	std::uint64_t					   keyRevision = 0 ;
	/// 当操作だけの単価
	ApiAudioPriceSnapshot			   price ;
	/// 完全prefixの開始位置
	std::uint32_t					   startIndex = 0 ;
	/// 排他終端、空応答も含む
	std::uint32_t					   completeEnd = 0 ;
	/// 当操作の終端
	ApiAudioTerminal				   terminal = ApiAudioTerminal::PENDING ;
	/// chunkがない準備/保存失敗も保持
	ErrorKind						   error = ErrorKind::INTERNAL ;
	/// 当操作終端の元診断
	std::uint32_t					   code = 0 ;
	/// この操作の観測試行量
	std::uint64_t					   attemptedSamples = 0 ;
	/// 最後の未確定POST
	std::optional<std::uint32_t>	   intent ;
	/// 本文なしの失敗応答
	std::optional<ApiAudioChunkResult> failure ;
	/// 開始時prefix。旧v1移行の空値は未観測
	ApiTextDigest					   parentSourceSha{} ;
	/// 終了時prefixのidentity
	ApiTextDigest					   endSourceSha{} ;
} ;
/** @brief 原文・利用量・intentを同じhistory/op/planへ束縛します */
struct ApiAudioRecord {
	/// 旧record1は読取だけでは変換しません
	std::uint32_t					 version = 1 ;
	/// 永続ID
	std::uint64_t					 historyId = 0 ;
	/// 単調operation
	std::uint64_t					 operationId = 0 ;
	/// 受付時の値。秘密なし
	std::uint64_t					 keyRevision = 0 ;
	/// POST前に全窓を固定
	ApiAudioPlan					 plan ;
	/// 順序通りの完全応答prefixと最大一失敗
	std::vector<ApiAudioChunkResult> chunks ;
	/// 次POSTの保存済み位置
	std::optional<std::uint32_t>	 intent ;
	/// 固定状態
	ApiAudioTerminal				 terminal = ApiAudioTerminal::PENDING ;
	/// 送信試行量。usageから推測しない
	std::uint64_t					 attemptedSamples = 0 ;
	/// 同じ原文の全field identity
	ApiTextDigest					 sourceSha{} ;
	/// 最大64、成功本文はchunksだけが所有
	std::vector<ApiAudioAttempt>	 attempts ;
	/// 同workerの耐久更新番号
	std::uint64_t					 update = 0 ;
	/// memory prefixと耐久prefixを区別
	bool							 saveFailed = false ;
	/// 音声/API固定失敗分類
	ErrorKind						 error = ErrorKind::INTERNAL ;
	/// 診断値
	std::uint32_t					 code = 0 ;
} ;
/** @brief UIからworkerへ渡す本文を含まないCAS tokenです */
struct ApiAudioResumeToken {
	/// 同じ行だけへ束縛
	std::uint64_t historyId = 0 ;
	/// 最後の耐久更新番号
	std::uint64_t update = 0 ;
	/// 保存planの全field
	ApiTextDigest planSha{} ;
	/// 保存完全prefixの全field
	ApiTextDigest sourceSha{} ;
	/// 空textも完全応答
	std::uint32_t completePrefix = 0 ;
	/// intentまたは未完応答の課金不明
	bool		  uncertain = false ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 小tokenを比較します
	 * @param[in] other 比較値
	 * @return 全field一致
	 */
	//------------------------------------------------------------------------------
	bool		  operator==( const ApiAudioResumeToken &other) const = default ;
} ;
/** @brief 履歴workerが耐久正本から返す準備用所有snapshotです */
struct ApiAudioResumeSnapshot {
	/// UIでcopy/hashしません
	ApiAudioRecord					  record ;
	/// begin時にownerもCAS照合
	std::shared_ptr<const Transcript> source ;
	/// durableの元入力、外部送信しません
	std::filesystem::path			  input ;
} ;
/** @brief UIへ秘密を渡さない同一保存ACKです */
struct ApiAudioPersistenceCompletion {
	/// 同history/op/plan/prefix
	ApiAudioRecord					  record ;
	/// 同じ所有原文
	std::shared_ptr<const Transcript> source ;
	/// atomic flush/replaceの事実
	bool							  committed = false ;
	/// 保存元code
	std::uint32_t					  code = 0 ;
} ;
/** @brief 既存の一履歴writerへ同期ACKを求めます。UIは呼びません */
class IApiAudioPersistencePort {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief controller非所有portを解放します
	 */
	//------------------------------------------------------------------------------
	virtual ~IApiAudioPersistencePort() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 耐久正本を取得します
	 * @param[in] token 小CAS値
	 * @return 同じsource ownerとrecord
	 * @throws OperationError stale/削除/上限
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual ApiAudioResumeSnapshot loadAudioResume( ApiAudioResumeToken token) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 再開を新opへ耐久保存します
	 * @param[in] token 元CAS値
	 * @param[in] previousOwner 元snapshot owner
	 * @param[in] record 新Pending
	 * @param[in] source 同じ原文owner
	 * @return 新leaseのACK、失敗時送信禁止
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual ApiAudioPersistenceCompletion
	beginAudioResume( ApiAudioResumeToken token, std::shared_ptr<const Transcript> previousOwner, ApiAudioRecord record,
					 std::shared_ptr<const Transcript> source) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ履歴にだけ保存します
	 * @param[in] record 検証済み単調prefix
	 * @param[in] source 所有原文
	 * @param[in] begin 初回Pending保存barrierか
	 * @param[in] commitStop 完了の原子置換直前だけで確認する取消し
	 * @return 未保存prefixも保持するACK。停止中も既存terminalを排出します
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual ApiAudioPersistenceCompletion commitAudio( ApiAudioRecord					  record,
																	std::shared_ptr<const Transcript> source,
																	bool begin, std::stop_token commitStop = {}) = 0 ;
} ;
/** @brief 音声jobだけの取消し後も保持する型付き終端です */
struct ApiAudioJobCompletion {
	/// 同record/sourceのACK
	ApiAudioPersistenceCompletion persistence ;
	/// 全必要保存済み時だけ
	bool						  succeeded = false ;
	/// 旧owner/recordを通常ASR失敗で消さない
	bool						  resumePreparationFailed = false ;
	/// 今回の事前拒否、旧HTTPを流用しない
	ErrorKind					  preparationError = ErrorKind::INTERNAL ;
	/// 今回だけの診断
	std::uint32_t				  preparationCode = 0 ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 一窓だけの最小WAVを作ります
 * @param[in] samples 有限正規化PCM
 * @return metadataなしPCM16 WAV
 * @throws OperationError 不正/上限
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string makeApiAudioWav( std::span<const float> samples) ;
//------------------------------------------------------------------------------
/**
 * @brief 厳密multipartを作ります
 * @param[in] wav 最小WAV
 * @param[in] autoLanguage 自動か
 * @param[in] boundary 有界ASCII乱数境界
 * @return 有界body
 * @throws OperationError 不正/衝突/上限
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string makeApiAudioMultipart( std::string_view wav, bool autoLanguage, std::string_view boundary) ;
//------------------------------------------------------------------------------
/**
 * @brief strict通常JSONを検証します
 * @param[in] response 生HTTP所有値
 * @param[in] sentSamples 実送信sample数
 * @return 完全応答/固定診断。生bodyは残しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] ApiAudioChunkResult parseApiAudioResponse( const ApiAudioHttpResponse &response,
														std::uint64_t				sentSamples) ;
//------------------------------------------------------------------------------
/**
 * @brief 有界PCMのVAD範囲と全file turnから非重複planを作ります
 * @param[in] audio 全PCMの部分読取元
 * @param[in] speech 元sample範囲の和集合
 * @param[in] turns 全fileの音声cluster
 * @param[in] options 固定設定
 * @param[in] price 有効料金
 * @param[in] pcm 実測同一性
 * @param[in] stop 取消し
 * @return 全窓検証済みplan
 * @throws OperationError 不正/上限
 */
//------------------------------------------------------------------------------
[[nodiscard]] ApiAudioPlan		  makeApiAudioPlan( const AudioData &audio, std::span<const AsrSampleWindow> speech,
												   std::span<const SpeakerTurn> turns, const EngineOptions &options,
												   const ApiAudioPriceSnapshot &price, const DecodedPcmIdentity &pcm,
												   std::stop_token stop = {}) ;
//------------------------------------------------------------------------------
/**
 * @brief 総送信sample数と分単価から、端数を切り上げた料金概算を計算します
 * @param[in] samples 16kHz mono基準の総送信sample数。再送分も計上する場合は呼出側で含めます
 * @param[in] rate 呼出側で取得・確認した分単価（USDの百万分の一/min）
 * @return ceil(samples * rate / (60 * 16000))に相当する概算（USDの百万分の一）
 * samplesまたはrateが0なら0です。実請求額やAPI通知usageではありません
 * @throws OperationError 分単位の商/余りとrateの積、または最終和がuint64_tで表現できない場合
 * 分類はErrorKind::INVALID_INPUT、codeはERROR_INVALID_DATAです
 * @note 未取得単価のoptionalは扱いません。未知単価を0へ読み替えて渡してはいけません
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::uint64_t		  estimateApiAudioCost( std::uint64_t samples, std::uint64_t rate) ;
//------------------------------------------------------------------------------
/**
 * @brief キー資格と取得metadataの鮮度を照合し、使用価格を所有copyへ固定します
 * @param[in] catalog Models・価格・取得時刻・キーrevisionを持つ、呼出し中だけの不変借用です
 * @param[in] keyReady 呼出側が確認した利用可能キーの有無。秘密値自体は受け取りません
 * @param[in] now 鮮度照合の現在UTC秒。各取得UTCが未来または24時間超なら拒否します
 * @return 条件を満たす価格と価格metadataの所有snapshot。キーなし、未load、破損cache、更新中、
 * キーrevision不一致、固定音声モデル未掲載、Modelsの最終HTTP401/403、単価未取得、または
 * Models/価格metadataの鮮度不成立ならnulloptです。記録済みmonotonicも非未来・24時間以内を要求します
 * @note キー実体・残高・実際の送信可否は確認せず、通信・cache更新・設定変更を行いません
 * 存在する0単価は未取得と区別して返します。所有copyの動的確保例外は伝播します
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::optional<ApiAudioPriceSnapshot> apiAudioPriceSnapshot( const ApiModelCatalog &catalog, bool keyReady,
																		 std::int64_t now = apiCatalogNow()) ;
//------------------------------------------------------------------------------
/**
 * @brief 保存recordを完全検証します
 * @param[in] record 保存値
 * @param[in] source 元原文、null可
 * @throws OperationError 不整合
 */
//------------------------------------------------------------------------------
void						 validateApiAudioRecord( const ApiAudioRecord &record, const Transcript *source) ;
//------------------------------------------------------------------------------
/**
 * @brief V8の有界audio recordを符号化します
 * @param[in] record 検証済み値
 * @return 所有JSON
 * @throws OperationError 不正/上限
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string	 encodeApiAudioRecord( const ApiAudioRecord &record) ;
//------------------------------------------------------------------------------
/**
 * @brief V8 audio recordを厳密復元します
 * @param[in] body 有界JSON
 * @return 完全検証前の所有値。source整合は呼出側も検証します
 */
//------------------------------------------------------------------------------
[[nodiscard]] ApiAudioRecord decodeApiAudioRecord( std::string_view body) ;
//------------------------------------------------------------------------------
/**
 * @brief 音声APIの保存状態が送信前または送信中に分類されるかを判定します
 * @param[in] terminal 判定する状態。状態を変更せず、通信や取消しも行いません
 * @return ApiAudioTerminal::PENDINGまたはSENDINGならtrue、それ以外の終端と未知enum値はfalseです
 * trueはAPI成功や実通信の継続を保証せず、未終端として扱うための分類です
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool			 apiAudioActive( ApiAudioTerminal terminal) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 保存済み応答列の先頭から連続する完全応答の窓数を返します
 * @param[in] record 完全応答prefixと任意の末尾一失敗からなる検証済み記録の呼出し中だけの読取り借用です
 * @return 完全応答の窓数で、次の未完窓の0始まり位置にもなります。空textの完全応答も数え、
 * 応答なしなら0です。sample数・文字数・送信試行数ではありません
 * @pre validateApiAudioRecord等で件数と応答順序を検証済みであること
 * @note 末尾一件のcompleteだけから算出し、列全体を再検証しません。recordを変更しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::uint32_t	 apiAudioCompletePrefix( const ApiAudioRecord &record) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 失敗・取消し・中断した音声API記録を、明示再開用の小さな照合tokenへ写します
 * @param[in] record sourceと照合済みで、完全応答prefixと試行数が妥当な記録の、呼出し中だけの借用です
 * @return FAILURE/CANCELLED/INTERRUPTED、saveFailedなし、plan窓あり、attemptsが64未満なら、
 * history/update/plan/source/prefixと課金不明flagを持つ所有token。それ以外はnulloptです
 * 全窓応答済みでも終端を保存するためのtokenを返せます
 * @note 課金不明flagは未確定intent、またはNOT_SENT以外の末尾不完全応答を示します
 * 記録全体の検証、PCM再照合、キー資格確認、保存CAS、通信、有料再開は行いません
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::optional<ApiAudioResumeToken> apiAudioResumeToken( const ApiAudioRecord &record) ;
//------------------------------------------------------------------------------
/**
 * @brief 現操作の終端metadataを最後の試行へ写し、全試行の観測送信量を集計します
 * @param[in,out] record workerが単独で変更する記録。v2かつattempts非空の場合だけ、
 * 最後の試行のcompleteEnd・終端・診断・intent・任意末尾失敗・endSourceShaを更新します
 * 成功時は全試行のattemptedSamples合計もrecordへ反映し、過去試行のfieldは保持します
 * @throws OperationError 送信量合計がuint64_tを超える場合。分類はErrorKind::INVALID_INPUT、
 * codeはERROR_INVALID_DATAです
 * @note v1またはattempts空なら変更しません。末尾失敗のcopyによる動的確保例外も伝播します
 * 例外時も直前までの最新試行更新は残り得るためrollbackを保証しません。保存・通信は行いません
 */
//------------------------------------------------------------------------------
void											 syncApiAudioAttempt( ApiAudioRecord &record) ;
//------------------------------------------------------------------------------
/**
 * @brief 完全応答を生成した操作を、保存済みattemptの成功範囲から検索します
 *
 * @param[in] record 検証済みApiAudioRecordの読取り専用借用です。所有権は取得しません
 * @param[in] index record.plan.windows/record.chunksに対応する0始まりの音声窓位置です
 * 単位は窓数で、sample位置・秒数・attempt配列の添字ではありません
 * 本文が空でも完全応答済みの窓は成功範囲に含まれます
 * @return startIndex <= index < completeEndを満たす最初のrecord.attempts要素への
 * 非所有const pointer。該当範囲がなければnullptrで、呼出し元は解放してはいけません
 * 検証済みv1はattemptsが空なのでnullptrです。未完窓/範囲外もnullptrになります
 * @pre validateApiAudioRecord等でrecordの版・非重複成功範囲を検証してあること
 * 本関数自身は版/範囲の整合性を検証せず、不正recordの拒否を保証しません
 * @post recordを変更せず、動的確保・I/O・例外送出を行いません
 * @note 返却先はrecord.attemptsが所有します。recordの破棄/代入、要素消去、vectorの再配置等で
 * 無効になります。recordと当該要素の寿命内だけ使用し、変更操作を越えて保持しません
 * 同じrecordを不変に保つ間は複数threadで読めます。変更と並行せず、必要な排他と
 * owner寿命の保持は呼出し元が担います
 */
//------------------------------------------------------------------------------
[[nodiscard]] const ApiAudioAttempt				*apiAudioGeneratingAttempt( const ApiAudioRecord &record,
																		   std::uint32_t		 index) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 旧記録を保持する所有copyから、次の明示再開をPENDINGとして準備します
 * @param[in] previous sourceとの完全整合を検証する旧記録。関数内では変更しません
 * @param[in] source previousの本文・sourceShaを照合する、呼出し中だけの原文の不変借用です
 * @param[in] operation previous.operationIdより大きい新操作IDです
 * @param[in] revision 呼出側が確認した現在キーの非0 revision。秘密値は受け取りません
 * @param[in] price 新試行へ固定する価格。元planと過去試行の価格は保持します
 * @return 完全応答prefixを保持し、末尾失敗とintentを除き、新試行を追加したv2/PENDINGの所有記録
 * 旧v1は過去一試行へ移し、未観測の初回parentSourceShaは空のまま保持します
 * @throws OperationError 旧record/source不整合、再開tokenなし、操作ID非増加、revisionが0、
 * または新記録の検証不成立の場合。previous/sourceは失敗時も変更しません
 * @note 所有copyの動的確保例外は伝播します。実キー資格・価格の現在資格・元PCM再decodeは
 * 呼出側の責務であり、本関数は保存・通信・再開を実行しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] ApiAudioRecord prepareApiAudioResume( const ApiAudioRecord &previous, const Transcript &source,
												   std::uint64_t operation, std::uint64_t revision,
												   const ApiAudioPriceSnapshot &price) ;
//------------------------------------------------------------------------------
/**
 * @brief 既存の完全応答と過去試行を保つ音声API記録の更新候補かを比較します
 * @param[in] oldValue 現在の耐久記録の、呼出し中だけの読取り借用です
 * @param[in] next 同じhistory/plan/updateから作った更新候補の、呼出し中だけの借用です
 * @param[in] resume trueは専用beginとして新操作・v2/PENDING・prefix/source・過去試行の保全を、
 * falseは同version/op/keyRevision内の単調更新を照合します
 * @return 同一target、完全prefix非減少、既存完全応答不変、および指定更新方式の操作/試行保存条件を
 * すべて満たす場合true。一つでも不成立ならfalseです。保存成功・API成功・記録単独の妥当性ではありません
 * @pre 両recordと対応sourceの妥当性は別の検証で確認すること
 * @note 入力を変更せず、update加算・lease取得・保存・通信は行いません
 * 過去試行のJSON比較は動的確保を伴い、その例外は呼出側へ伝播します
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool validApiAudioTransition( const ApiAudioRecord &oldValue, const ApiAudioRecord &next, bool resume) ;
//------------------------------------------------------------------------------
/**
 * @brief 完全応答を各窓へ一度だけ組み立てます
 * @param[in] record 同じplan/prefix
 * @param[in] turns 全fileの音響turn
 * @return 本文未分配の原文行
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<TranscriptLine>			  assembleApiAudioTranscript( const ApiAudioRecord		 &record,
																				 std::span<const SpeakerTurn> turns) ;
//------------------------------------------------------------------------------
/**
 * @brief 固定WinHTTP音声transportを作ります
 * @return worker専用owner
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::unique_ptr<IApiAudioHttpTransport> makeApiAudioHttp() ;
//------------------------------------------------------------------------------
/**
 * @brief 同期providerを作ります
 * @param[in,out] http 呼出しより長寿命のborrow
 * @return 所有provider
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::unique_ptr<IApiAudioTranscriber>	  makeApiAudioTranscriber( IApiAudioHttpTransport &http) ;
} // namespace koefude
