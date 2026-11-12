//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_plan.h
 * @brief 全量材料を借用する有限な局所計画と実計数の契約です
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1284の有限計画と生成しない計数を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1285の有界逐次補正を接続
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1343の文章専用補正を接続
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
#include "core/types.h"
#include "core/whole_text_generation_contract.h"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>
namespace koefude {
using WholeTextDigest = std::array<std::uint8_t, 32> ; // 全byteのSHA
/** @brief 本文を含まない有限計画の診断です */
enum class WholeTextPlanDiagnostic : std::uint32_t {
	/// 計画byte不足
	PLAN_BYTES = 0x57580001,
	/// record数不足
	PLAN_CHUNKS,
	/// 計数作業量不足
	PLAN_WORK,
	/// 一scalarに必要な資料が不適合
	PROJECTION_DENSITY,
	/// 全量被覆の不整合
	COVERAGE,
	/// 非空白原文なし
	NO_ORIGINAL_CONTENT,
	/// 計数ABI不足
	RUNTIME_CAPABILITY,
	/// 実計数証拠不整合
	COUNTER_RECEIPT
} ;
/** @brief 固定上限を小さい合成試験だけで縮められる値です */
struct WholeTextPlanLimits {
	/// 原文/raw/token本文
	std::uint64_t inputBytes = 8388608 ;
	/// canonical
	std::uint64_t auxiliaryBytes = 8388608 ;
	/// 作業index確保
	std::uint64_t indexBytes = 16777216 ;
	/// UTF-8 checkpoint
	std::uint64_t checkpointBytes = 1048576 ;
	/// sealed encoding
	std::uint64_t planBytes = 8388608 ;
	/// 全record
	std::uint64_t chunks = 16384 ;
	/// catalog
	std::uint64_t packets = 200004 ;
	/// canonical subspan
	std::uint64_t physicalSpans = 524288 ;
	/// O/A一次slice
	std::uint64_t primarySlices = 131072 ;
	/// system+user+rendered
	std::uint64_t promptBytes = 65536 ;
	/// 局所slice
	std::uint64_t promptSlices = 1024 ;
	/// 生成recordの候補数
	std::uint64_t candidatesPerChunk = 64 ;
	/// 全候補数
	std::uint64_t measureCalls = 262144 ;
	/// prefix/component全入力
	std::uint64_t measuredBytes = 268435456 ;
} ;
/** @brief 元文字列の半開byte範囲です */
struct WholeTextByteRange {
	/// UTF-8開始
	std::uint64_t begin = 0 ;
	/// UTF-8終端
	std::uint64_t end = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 封印計画内の同じUTF-8 byte範囲かを比較します
	 * @param[in] other 比較する別の半開範囲。呼出し中だけ借用します
	 * @return begin/endのbyte位置が両方一致すればtrue。それ以外はfalseです
	 * @note 本文内容や元ownerは参照せず、UTF-8境界も検証しません
	 */
	//------------------------------------------------------------------------------
	bool		  operator==( const WholeTextByteRange &other) const = default ;
} ;
/** @brief 元行の無編集borrowです */
struct WholeTextOriginalOrigin {
	/// owner寿命内だけ有効
	std::string_view text ;
	/// 元時刻
	std::int64_t	 startMs = 0 ;
	/// 元時刻
	std::int64_t	 endMs = 0 ;
} ;
/** @brief 時刻を発明しないraw行のborrowです */
struct WholeTextRawOrigin {
	/// raw本文だけ。token本文は別canonical
	std::string_view			text ;
	/// 予定窓index
	std::uint32_t				window = 0 ;
	/// 窓内row
	std::uint32_t				row = 0 ;
	/// 元sample
	std::uint64_t				windowBegin = 0 ;
	/// 元sample
	std::uint64_t				windowEnd = 0 ;
	/// 厳密成立時だけ元ms
	std::optional<std::int64_t> startMs ;
	/// 厳密成立時だけ元ms
	std::optional<std::int64_t> endMs ;
	/// 元列の全ordinal数
	std::uint64_t				tokenCount = 0 ;
} ;
/** @brief lossless canonicalの物理packetです */
struct WholeTextCanonicalSpan {
	/// 0共通、1窓、2row header、3token、4trace
	std::uint32_t	   kind = 0 ;
	/// kind内の元ordinal
	std::uint32_t	   origin = 0 ;
	/// canonicalの全byteを分割
	WholeTextByteRange bytes ;
	/// token packetの全件数
	std::uint64_t	   tokenCount = 0 ;
	/// 窓packetの元sample開始。時刻の発明ではありません
	std::uint64_t	   anchorSample = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 封印計画が元canonical packetの境界と来歴を保つかを比較します
	 * @param[in] other 比較する別のcanonical記述。呼出し中だけ借用します
	 * @return kind/origin、bytesの半開byte範囲、tokenCount、anchorSampleがすべて一致すればtrue。それ以外はfalseです
	 * @note packet本文を深く比較せず、元全byteのdigestと妥当性は別に照合します
	 */
	//------------------------------------------------------------------------------
	bool			   operator==( const WholeTextCanonicalSpan &other) const = default ;
} ;
/** @brief 文字列を所有せず材料ownerの寿命に束縛する入力です */
struct WholeTextPlanInputView {
	/// 全原文
	std::span<const WholeTextOriginalOrigin> original ;
	/// 全raw
	std::span<const WholeTextRawOrigin>		 raw ;
	/// 全canonical境界
	std::span<const WholeTextCanonicalSpan>	 canonicalSpans ;
	/// 全lossless byte
	std::span<const std::uint8_t>			 auxiliary ;
	/// 行/時刻も束縛するfingerprint
	WholeTextDigest							 sourceSha256{} ;
	/// 全canonical
	WholeTextDigest							 auxiliarySha256{} ;
	/// request/profile/opも束縛
	WholeTextDigest							 inputBinding{} ;
	/// 新1865byte
	std::string_view						 system ;
	/// 不変方式
	WholeTextGenerationKind					 kind = WholeTextGenerationKind::GEMMA_THOUGHT_FINAL ;
	/// 新文章専用材料だけraw/canonical/auxiliaryを空にします
	bool									 textOnly = false ;
} ;
/** @brief 一次または読取り専用の元sliceです */
struct WholeTextPlanSlice {
	/// 元O/A index
	std::uint32_t	   origin = 0 ;
	/// 元string内の範囲
	WholeTextByteRange bytes ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 封印計画の元行sliceが同じかを比較します
	 * @param[in] other 比較する別の元slice。呼出し中だけ借用します
	 * @return originの元行indexとbytesの半開byte範囲がともに一致すればtrue。それ以外はfalseです
	 * @note 元文字列の内容やownerを比較せず、範囲の実在性は別の検証を要します
	 */
	//------------------------------------------------------------------------------
	bool			   operator==( const WholeTextPlanSlice &other) const = default ;
} ;
/** @brief uncertainな技術割当てを保持します */
struct WholeTextRawAssignment {
	/// anchor O index
	std::uint32_t original = 0 ;
	/// KnownStartAnchor、それ以外WindowStartFallback
	bool		  knownStart = false ;
	/// 原文行の時刻包絡との重なり
	bool		  envelopeOverlap = false ;
	//------------------------------------------------------------------------------
	/**
	 * @brief raw行の技術的な割当て方針が封印計画と同じかを比較します
	 * @param[in] other 比較する別の割当て記録。呼出し中だけ借用します
	 * @return originalの原文行index、knownStart、envelopeOverlapがすべて一致すればtrue。それ以外はfalseです
	 * @note 音声・意味の正解や話者同一性を判定せず、割当ての保存値だけを比較します
	 */
	//------------------------------------------------------------------------------
	bool		  operator==( const WholeTextRawAssignment &other) const = default ;
} ;
/** @brief 一候補だけ所有する実promptです */
struct WholeTextChunkPrompt {
	/// 無編集固定system
	std::string				system ;
	/// strict ordered JSON
	std::string				user ;
	/// plain text計数用
	std::string				core ;
	/// plain text計数用
	std::string				before ;
	/// plain text計数用
	std::string				after ;
	/// 全byte/rangeの束縛
	WholeTextDigest			candidateBinding{} ;
	/// renderer選択
	WholeTextGenerationKind kind = WholeTextGenerationKind::GEMMA_THOUGHT_FINAL ;
} ;
/** @brief 同じvocabで独立に計数したreceiptです */
struct WholeTextTokenReceipt {
	/// live native session
	std::uint64_t				   session = 0 ;
	/// 1始まり
	std::uint64_t				   sequence = 0 ;
	/// 全rendered独立計数
	std::uint32_t				   prefixTokens = 0 ;
	/// plain text
	std::uint32_t				   coreTokens = 0 ;
	/// 空だけ0可
	std::uint32_t				   beforeTokens = 0 ;
	/// 空だけ0可
	std::uint32_t				   afterTokens = 0 ;
	/// 全制約成立
	bool						   fits = false ;
	/// bufferに全IDあり
	bool						   completeTokenDigest = false ;
	/// system,user,rendered,core,before,after
	std::array<std::uint64_t, 6>   bytes{} ;
	/// 各全byte
	std::array<WholeTextDigest, 6> sha256{} ;
	/// 呼出し候補
	WholeTextDigest				   candidateBinding{} ;
	/// 全prefix ID列LE u32
	WholeTextDigest				   tokenSha256{} ;
	/// 固定encodingのreceipt
	WholeTextDigest				   receiptSha256{} ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 計数receiptが同じsession・候補・全観測証拠を保持するかを比較します
	 * @param[in] other 比較する別のreceipt。呼出し中だけ借用します
	 * @return session/sequence、四token数、fits/completeTokenDigest、六byte長と全digest配列がすべて一致すればtrue。それ以外はfalseです
	 * @note 配列は内容で比較します。prompt本文やtoken ID列は保持せず、receipt自体の検証やnative再計数は行いません
	 */
	//------------------------------------------------------------------------------
	bool						   operator==( const WholeTextTokenReceipt &other) const = default ;
} ;
using WholeTextTokenCounter = std::function<WholeTextTokenReceipt( const WholeTextChunkPrompt &)> ; // 生成なし
/** @brief 生成または空白無編集copyのrecordです */
struct WholeTextChunkRecord {
	/// 0始まり連番
	std::uint32_t						 index = 0 ;
	/// 直接連結tapeの正の範囲
	WholeTextByteRange					 core ;
	/// 読取り専用
	WholeTextByteRange					 before ;
	/// 読取り専用
	WholeTextByteRange					 after ;
	/// nativeを呼ばない
	bool								 verbatimWhitespace = false ;
	/// 全coreの一次slice
	std::vector<WholeTextPlanSlice>		 original ;
	/// 全担当rawの一次slice
	std::vector<WholeTextPlanSlice>		 raw ;
	/// Verbatimはnull
	std::optional<WholeTextTokenReceipt> receipt ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 保存した生成区間の封印recordと計画recordを比較します
	 * @param[in] other 比較する別のrecord。呼出し中だけ借用します
	 * @return index、core/before/afterのbyte範囲、verbatimWhitespace、original/rawのslice列の内容と順序、
	 * optional receiptがすべて一致すればtrue。それ以外はfalseです
	 * @note vectorは要素の値を比較し、保持pointerは比較しません。生成本文の同値や成功はこの比較だけでは確認できません
	 */
	//------------------------------------------------------------------------------
	bool								 operator==( const WholeTextChunkRecord &other) const = default ;
} ;
/** @brief 全canonical spanの一意な一次ownerです */
struct WholeTextCanonicalOwner {
	/// 元packetとの完全照合
	WholeTextCanonicalSpan span ;
	/// 非空生成core
	std::uint32_t		   chunk = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief canonical packetの記述と一次担当chunkが同じかを比較します
	 * @param[in] other 比較する別の一次割当て。呼出し中だけ借用します
	 * @return spanの記述全体とchunkの0始まりindexがともに一致すればtrue。それ以外はfalseです
	 * @note ownerは計画上の担当を表し、shared_ptrの所有control block同一性やpacket本文の深い同値ではありません
	 */
	//------------------------------------------------------------------------------
	bool				   operator==( const WholeTextCanonicalOwner &other) const = default ;
} ;
/** @brief pointerと全promptを持たない封印計画です */
struct WholeTextChunkPlan {
	/// core元順
	std::vector<WholeTextChunkRecord>	 chunks ;
	/// raw元順
	std::vector<WholeTextRawAssignment>	 assignments ;
	/// 全packet元順
	std::vector<WholeTextCanonicalOwner> canonical ;
	/// 要求全体
	WholeTextDigest						 inputBinding{} ;
	/// 再連結
	WholeTextDigest						 originalSha256{} ;
	/// lossless全byte
	WholeTextDigest						 auxiliarySha256{} ;
	/// 固定順encoding
	WholeTextDigest						 planSha256{} ;
	/// 実候補回数
	std::uint64_t						 measures = 0 ;
	/// 実計数入力総byte
	std::uint64_t						 measuredBytes = 0 ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 本文を含まない診断を投げます
 * @param[in] valid 成立
 * @param[in] diagnostic 分類
 */
//------------------------------------------------------------------------------
void							 requireWholeTextPlan( bool valid, WholeTextPlanDiagnostic diagnostic) ;
//------------------------------------------------------------------------------
/**
 * @brief 全byteのSHAを計算します
 * @param[in] bytes 全入力
 * @return raw32byte
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextDigest	 wholeTextSha256( std::span<const std::uint8_t> bytes) ;
//------------------------------------------------------------------------------
/**
 * @brief UTF-8 byteのSHAです
 * @param[in] bytes 全入力
 * @return raw32byte
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextDigest	 wholeTextSha256( std::string_view bytes) ;
//------------------------------------------------------------------------------
/**
 * @brief SHAを小文字hexにします
 * @param[in] digest 全SHA
 * @return 64byte
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string		 wholeTextHex( const WholeTextDigest &digest) ;
//------------------------------------------------------------------------------
/**
 * @brief receiptの固定encodingをhashします
 * @param[in] receipt 全field
 * @return 全SHA
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextDigest	 wholeTextReceiptDigest( const WholeTextTokenReceipt &receipt) ;
//------------------------------------------------------------------------------
/**
 * @brief 全原文を分割して全rawを一回割当てます
 * @param[in] input 同じownerのborrow
 * @param[in] counter 生成なし実計数
 * @param[in] stop 協調停止
 * @param[in] limits 固定値か内部試験で縮小した値
 * @return 全coverage検証済み計画
 * @throws OperationError 部分計画を返しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextChunkPlan makeWholeTextChunkPlan( const WholeTextPlanInputView &input,
														const WholeTextTokenCounter &counter, std::stop_token stop,
														// この同期境界で所有する値、または確定状態の検証値です
														const WholeTextPlanLimits &limits = {}) ;
//------------------------------------------------------------------------------
/**
 * @brief 元byte/ID/receipt/全coverageと封印を照合します
 * @param[in] input 同じowner
 * @param[in] plan 全計画
 * @param[in] stop 停止
 * @throws OperationError 不一致。原文を変更しません
 */
//------------------------------------------------------------------------------
void validateWholeTextChunkPlan( const WholeTextPlanInputView &input, const WholeTextChunkPlan &plan,
								std::stop_token stop) ;
//------------------------------------------------------------------------------
/**
 * @brief 同じ材料だけから一候補を描画します
 * @param[in] input 全量borrow
 * @param[in] record 元range
 * @param[in] stop 停止
 * @return 一候補所有byte
 * @throws OperationError 不正range/密度超過
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextChunkPrompt materializeWholeTextChunk( const WholeTextPlanInputView &input,
															 const WholeTextChunkRecord &record, std::stop_token stop) ;
namespace detail {
using WholeTextReadAt = std::function<void( std::uint64_t, std::span<std::uint8_t>)> ; // 最大64KiB同期read
//------------------------------------------------------------------------------
/**
 * @brief 全encodingを複製せずstream SHAを計算します
 * @param[in] readAt 有界同期reader
 * @param[in] bytes 全長
 * @param[in] stop 停止
 * @return 全SHA
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextDigest	 wholeTextStreamSha256( const WholeTextReadAt &readAt, std::uint64_t bytes,
													   // この同期境界で所有する値、または確定状態の検証値です
													   std::stop_token stop = {}) ;
//------------------------------------------------------------------------------
/**
 * @brief 保存planを64KiB以下のreadで復元します
 * @param[in] readAt plan相対reader
 * @param[in] encodedBytes 全長
 * @param[in] expectedSha 期待SHA
 * @param[in] stop 停止
 * @return 16MiB以下の構造検証済みplan。tokenize/生成は行いません
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextChunkPlan decodeStoredWholeTextPlan( const WholeTextReadAt &readAt, std::uint64_t encodedBytes,
														   const WholeTextDigest &expectedSha,
														   // この同期境界で所有する値、または確定状態の検証値です
														   std::stop_token		  stop = {}) ;

//------------------------------------------------------------------------------
/**
 * @brief modelを開く前に全材料とindex確保上限を検査します
 * @param[in] input 全量borrow
 * @param[in] stop 停止。生成/計数は行いません
 */
//------------------------------------------------------------------------------
void preflightWholeTextPlanMaterials( const WholeTextPlanInputView &input, std::stop_token stop) ;
//------------------------------------------------------------------------------
/**
 * @brief 内部試験で不正fieldを再hashして構造拒否を独立確認します
 * @param[in] plan 全DTO
 * @return 固定順encodingのSHA、計画の受理ではありません
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextDigest wholeTextPlanDigest( const WholeTextChunkPlan &plan) ;
//------------------------------------------------------------------------------
/**
 * @brief 旧封印と同一byteのpreimageを一つだけ返します
 * @param[in] plan 全計画
 * @return 最大8MiBの同一encoding。受理は行いません
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string	  wholeTextPlanEncoding( const WholeTextChunkPlan &plan) ;
} // namespace detail
} // namespace koefude
