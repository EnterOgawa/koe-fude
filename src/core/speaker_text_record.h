//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file speaker_text_record.h
 * @brief 原APIとは独立した話者表示の所有値、CAS、保存portを定義します
 * @version 1.0.0
 * @date 2026/11/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372の独立投影保存と操作所有権を追加
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
#include "core/engines.h"
#include "core/pipeline.h"
#include "core/speaker_text_projection.h"
#include "core/types.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace koefude {
constexpr std::size_t K_SPEAKER_TEXT_RECORD_BYTES = 16 * 1024 * 1024 ; // 一採用record上限
constexpr std::size_t K_SPEAKER_TEXT_REFERENCE_BYTES = 8 * 1024 * 1024 ; // 一recordの参照本文上限
constexpr std::size_t K_SPEAKER_TEXT_STORE_BYTES = 64 * 1024 * 1024 ; // 全sidecar上限
/** @brief 本文をUIでcopy/hashしない、耐久音声正本の小CASです */
struct SpeakerTextTarget {
	/// 削除後も再使用しない行ID
	std::uint64_t historyId = 0 ;
	/// 音声API生成op。投影opとは別
	std::uint64_t generationOperation = 0 ;
	/// 音声recordの耐久revision
	std::uint64_t update = 0 ;
	/// 表示名を除く原文identity
	ApiTextDigest sourceSha{} ;
	/// 保存planの全field
	ApiTextDigest planSha{} ;
	/// 空textも含む完全応答数
	std::uint32_t completePrefix = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 保存・表示のCASに使う対象tokenが同じ音声API正本を指すか比較します
	 * @param[in] other 比較する別の対象token。呼出し中だけ借用します
	 * @return historyId/generationOperation/update/sourceSha/planSha/completePrefix（窓数）がすべて一致すればtrue。それ以外はfalseです
	 * @note tokenを比較するだけで、保存・更新や原子的CASを行いません
	 * 本文や原文ownerは直接比較せず、同じshared_ptr ownerであることは別の照合を要します
	 */
	//------------------------------------------------------------------------------
	bool		  operator==( const SpeakerTextTarget &other) const = default ;
} ;
/** @brief 音声APIの終端を変更しない投影専用状態です */
enum class SpeakerTextState : std::uint32_t {
	/// begin耐久予約済み
	PENDING,
	/// identity/通常native参照中
	PRODUCING_REFERENCE,
	/// 取得した当窓のpure対応中
	ALIGNING,
	/// 全partitionを耐久保存中
	SAVING,
	/// 新採用の保存ACK済み
	READY,
	/// 新しい支持なし。前採用を保持
	UNCHANGED,
	/// 原文/前採用を保持した失敗
	FAILURE,
	/// 置換前に要求された取消し
	CANCELLED,
	/// 起動後に残った未完試行
	INTERRUPTED
} ;
/** @brief 採用と独立して残す最新試行の固定metadataです */
struct SpeakerTextAttempt {
	/// 同じglobal floorから発行する投影op
	std::uint64_t	  operationId = 0 ;
	/// 開始した耐久原文
	SpeakerTextTarget target ;
	/// 最新の実終端
	SpeakerTextState  state = SpeakerTextState::PENDING ;
	/// 自由文を持たない診断
	ErrorKind		  error = ErrorKind::INTERNAL ;
	/// 元診断値
	std::uint32_t	  code = 0 ;
} ;
/** @brief 採取時に実verify済みだったassetの小snapshotです。未記録値を現値で補いません */
struct SpeakerTextAsset {
	/// 実検証済みregistry用途
	std::string	  id ;
	/// 採取時のimmutable配布revision。未記録は空
	std::string	  revision ;
	/// 採取時のfile容量
	std::uint64_t bytes = 0 ;
	/// 採取時の64桁SHA。未記録は空
	std::string	  sha256 ;
} ;
/** @brief 本文を複製せず、全参照snapshotと原byte範囲を独立所有します */
struct SpeakerTextProjectionRecord {
	/// 専用sidecar版
	std::uint32_t							  version = 1 ;
	/// 投影op。API生成opを変更しない
	std::uint64_t							  operationId = 0 ;
	/// 参照を実生成したop。再利用で変更しない
	std::uint64_t							  referenceGenerationOperation = 0 ;
	/// 同じsource/plan/prefixへの束縛
	SpeakerTextTarget						  target ;
	/// 全PCM実観測identity
	DecodedPcmIdentity						  pcm ;
	/// 対象全file turnの全field
	ApiTextDigest							  turnsSha{} ;
	/// 方式3/固定threshold
	ApiTextDigest							  parametersSha{} ;
	/// 全中立を含む参照snapshot
	ApiTextDigest							  referenceSha{} ;
	/// 通常native経路の型付き来歴
	AsrExecutionTrace						  execution ;
	/// 実採取時のrevision/size/SHA
	std::vector<SpeakerTextAsset>			  assets ;
	/// 原順の全窓
	std::vector<TextAlignmentReferenceWindow> references ;
	/// 原byteを指すpartsと固定証拠
	std::vector<SpeakerTextWindowProjection>  windows ;
} ;
/** @brief 前採用と最新試行を分離した、一行のsidecar状態です */
struct SpeakerTextRecord {
	/// 保存ACK済みの前採用
	std::shared_ptr<const SpeakerTextProjectionRecord> adopted ;
	/// Pending/失敗でも前採用を消さない
	std::optional<SpeakerTextAttempt>				   latest ;
} ;
/** @brief 同じ履歴workerから取得する不変所有snapshotです */
struct SpeakerTextSnapshot {
	/// durableの小token
	SpeakerTextTarget					  target ;
	/// 表示名の変更を別ownerで保持
	std::shared_ptr<const Transcript>	  source ;
	/// 原API recordのimmutable共有
	std::shared_ptr<const ApiAudioRecord> audio ;
	/// native処理専用local path。保存/logへ複製しない
	std::filesystem::path				  input ;
	/// 前採用と最新試行
	SpeakerTextRecord					  record ;
} ;
/** @brief 同source/opの保存事実だけをUIへ返します */
struct SpeakerTextCompletion {
	/// 保存した元原文
	SpeakerTextTarget target ;
	/// ACKまたは前採用/最新失敗
	SpeakerTextRecord record ;
	/// 原子置換成功だけtrue
	bool			  committed = false ;
	/// 保存元診断
	std::uint32_t	  code = 0 ;
} ;
/** @brief UIから渡す小さい明示投影要求です */
struct SpeakerTextJobRequest {
	/// menu前後で照合する耐久小token
	SpeakerTextTarget	  target ;
	/// 準備済み通常Quality資産のlocal root
	std::filesystem::path modelRoot ;
	/// JobControllerが同じglobal floorから発行
	std::uint64_t		  operationId = 0 ;
} ;
/** @brief 原APIの終端から分離した、投影専用job応答です */
struct SpeakerTextJobCompletion {
	/// 同writer/同opの保存ACK
	SpeakerTextCompletion			  persistence ;
	/// 表示名はUIの現在ownerを優先
	std::shared_ptr<const Transcript> source ;
	/// 今回は実native生成を行っていない場合true
	bool							  referenceReused = false ;
	/// cache時は0。過去traceとは別
	std::uint32_t					  nativeContextsThisOperation = 0 ;
} ;
/** @brief 同じ一履歴writerへ同期要求するworker用portです */
class ISpeakerTextPersistencePort {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 非所有portの共有寿命を終了します
	 */
	//------------------------------------------------------------------------------
	virtual ~ISpeakerTextPersistencePort() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 耐久正本を小tokenで取得します
	 * @param[in] target UI捕捉token
	 * @return 同じ原文/record
	 * @throws OperationError stale等
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual SpeakerTextSnapshot	loadSpeakerText( SpeakerTextTarget target) = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief metadata floor予約後にbegin/最終sidecarを保存します
	 * @param[in] target 同じ耐久原文
	 * @param[in] record 最新試行/前採用
	 * @param[in] begin trueは予約
	 * @param[in] stop 最終原子置換直前の取消し
	 * @return 同じsource/opの保存事実
	 * @throws OperationError stale/保存失敗
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual SpeakerTextCompletion commitSpeakerText( SpeakerTextTarget target, SpeakerTextRecord record,
																  bool begin, std::stop_token stop = {}) = 0 ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 有効なplan2日本語完全prefixだけを識別します
 * @param[in] audio 保存record
 * @return 適格なら小token、未適格ならnull
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::optional<SpeakerTextTarget> speakerTextTarget( const ApiAudioRecord &audio) ;
//------------------------------------------------------------------------------
/**
 * @brief 固定方式3のparameter SHAを返します
 * @return 本文なしdigest
 */
//------------------------------------------------------------------------------
[[nodiscard]] ApiTextDigest					   speakerTextParametersSha() ;
//------------------------------------------------------------------------------
/**
 * @brief 全target turnを原順でhashします
 * @param[in] turns 全file保存turn
 * @return 全fieldのdigest
 */
//------------------------------------------------------------------------------
[[nodiscard]] ApiTextDigest					   speakerTextTurnsSha( std::span<const SpeakerTurn> turns) ;
//------------------------------------------------------------------------------
/**
 * @brief 全参照snapshotをhashします
 * @param[in] reference 中立を含む全窓
 * @return 全fieldのdigest
 */
//------------------------------------------------------------------------------
[[nodiscard]] ApiTextDigest speakerTextReferenceSha( std::span<const TextAlignmentReferenceWindow> reference) ;
//------------------------------------------------------------------------------
/**
 * @brief 原文/plan/PCM/turn/参照/固定方式と全partitionをworkerで検証します
 * @param[in] value 採用候補
 * @param[in] source 耐久原文
 * @param[in] audio 耐久音声record
 * @param[in] replay trueは純粋対応/音響候補も再検証
 * @param[in] stop 協調停止
 * @throws OperationError 不正/上限/取消し。I/O/推論は行いません
 */
//------------------------------------------------------------------------------
void					  validateSpeakerTextRecord( const SpeakerTextProjectionRecord &value, const Transcript &source,
													const ApiAudioRecord &audio, bool replay = true, std::stop_token stop = {}) ;
//------------------------------------------------------------------------------
/**
 * @brief 一採用recordを有界JSONへ変換します
 * @param[in] value 所有候補
 * @return API本文/path/名前を含まないJSON
 * @throws OperationError 上限
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string encodeSpeakerTextRecord( const SpeakerTextProjectionRecord &value) ;
//------------------------------------------------------------------------------
/**
 * @brief 一採用recordを外部保存から復元し全bindingを検証します
 * @param[in] bytes 有界UTF-8JSON
 * @param[in] source 元原文
 * @param[in] audio 元record
 * @return 検証済み共有採用
 * @throws OperationError 不正/未知版/上限
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::shared_ptr<const SpeakerTextProjectionRecord>
decodeSpeakerTextRecord( std::string_view bytes, const Transcript &source, const ApiAudioRecord &audio) ;
//------------------------------------------------------------------------------
/**
 * @brief 空完全応答を数えたchunkから非空source行へのmapを返します
 * @param[in] audio 元record
 * @return 非空chunk indexの原順列
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<std::size_t> speakerTextSourceChunks( const ApiAudioRecord &audio) ;
//------------------------------------------------------------------------------
/**
 * @brief 中立partに内容scalarがあるか確認します
 * @param[in] body 原UTF-8範囲
 * @return 明示separator以外があればtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool					   speakerTextHasContent( std::string_view body) ;
//------------------------------------------------------------------------------
/**
 * @brief 明示話者付きTXTを原byteと現表示名から作ります
 * @param[in] source 現原文/表示名
 * @param[in] audio 原音声record
 * @param[in] projection 採用
 * @return 全中立も含むUTF-8。通常copy/TXTとは別の明示出力です
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string			   renderSpeakerTextExport( const Transcript &source, const ApiAudioRecord &audio,
															   const SpeakerTextProjectionRecord &projection) ;
} // namespace koefude
