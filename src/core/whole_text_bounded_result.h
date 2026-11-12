//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_bounded_result.h
 * @brief native所有物を持たない有界補正の値と成功receiptを定義します
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1285の有界逐次補正を接続
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1343の文章専用補正を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/audio_identity.h"
#include "core/whole_text_correction.h"
#include "core/whole_text_plan.h"
#include <cstdint>
#include <optional>
#include <string_view>
namespace koefude {
/** @brief 逐次補正の固定診断です */
enum class WholeTextBoundedDiagnostic : std::uint32_t {
	/// U4受領未接続
	PERSISTENCE_UNAVAILABLE = 0x57590001,
	/// fileまたは予約容量不足
	JOURNAL_CAPACITY,
	/// 封印encoding不一致
	JOURNAL_ENCODING,
	/// 全byte/計数/生成証拠不一致
	CHUNK_RECEIPT,
	/// 本文合計容量不足
	AGGREGATE_BYTES,
	/// 解放未確認
	RESTART_REQUIRED,
	/// 原ASRのPCM由来metadataなし、生成しません
	SOURCE_METADATA_MISSING,
	/// 固定表面保護検査の不一致
	PROTECTED_SPAN_CHANGED,
	/// 明示有限probeの生成前件数拒否
	FINITE_GENERATION_LIMIT
} ;
/** @brief native所有権の観測結果です */
enum class WholeTextNativeOwnership : std::uint32_t {
	/// 未取得
	NONE,
	/// 有効整合closed1
	CONFIRMED_RELEASED,
	/// process寿命で保持
	RETAINED_UNKNOWN
} ;
/** @brief OS/native ownerを含まない明示closeの元値です */
struct WholeTextNativeCloseReceipt {
	/// 最終所有状態
	WholeTextNativeOwnership ownership = WholeTextNativeOwnership::NONE ;
	/// 明示cleanup試行
	bool					 attempted = false ;
	/// size/ABI/reserved
	bool					 responseHeaderValid = false ;
	/// statusとclosedの整合
	bool					 dispositionConsistent = false ;
	/// 解放を確認
	bool					 releaseConfirmed = false ;
	/// 再open禁止
	bool					 restartRequired = false ;
	/// 元status
	std::uint32_t			 rawStatus = 0 ;
	/// 元size
	std::uint32_t			 rawStructSize = 0 ;
	/// 元ABI
	std::uint32_t			 rawAbiVersion = 0 ;
	/// 元closed
	std::uint32_t			 rawClosed = 0 ;
	/// 元診断
	std::uint32_t			 rawDiagnostic = 0 ;
	/// 元符号付きcode
	std::int32_t			 rawEngineCode = 0 ;
	/// 元reserved
	std::uint32_t			 rawReserved = 0 ;
} ;
/** @brief 最後のnative callの元statusをcloseとは別に保持します */
struct WholeTextNativeFailureSnapshot {
	/// 元status
	std::uint32_t status = 0 ;
	/// 元診断
	std::uint32_t diagnostic = 0 ;
	/// 元符号
	std::int32_t  engineCode = 0 ;
} ;
/** @brief 最初の資源失敗をnative所有なしに保存する値です */
struct WholeTextBoundedResourceFailure {
	/// 0正常、1予算、2probe
	std::uint32_t reason = 0 ;
	/// 元診断
	std::uint32_t code = 0 ;
} ;
/** @brief promotion時に固定する計画の値です */
struct WholeTextBoundedPromotion {
	/// open束縛
	WholeTextDigest openBinding{} ;
	/// sealed計画
	WholeTextDigest planSha256{} ;
	/// 入力全体
	WholeTextDigest inputSha256{} ;
	/// 全core終端
	std::uint64_t	originalBytes = 0 ;
	/// 実計数回数
	std::uint64_t	measures = 0 ;
	/// 全実計数入力byte
	std::uint64_t	measuredBytes = 0 ;
	/// 空白を含む全件
	std::uint32_t	recordCount = 0 ;
	/// native生成件数
	std::uint32_t	generationCount = 0 ;
} ;
/** @brief 成功した一coreだけの値です。pointerと本文は持ちません */
struct WholeTextBoundedChunkReceipt {
	/// 履歴用の不活性なID
	std::uint64_t	   session = 0 ;
	/// 元plan順
	std::uint32_t	   index = 0 ;
	/// 元tape範囲
	WholeTextByteRange core ;
	/// 計画束縛
	WholeTextDigest	   planSha256{} ;
	/// 入力束縛
	WholeTextDigest	   inputSha256{} ;
	/// 受理済み計数receipt
	WholeTextDigest	   countSha256{} ;
	/// 全prefix ID列
	WholeTextDigest	   tokenSha256{} ;
	/// 全rendered byte
	WholeTextDigest	   renderedSha256{} ;
	/// 一core本文
	WholeTextDigest	   bodySha256{} ;
	/// 成功receipt
	WholeTextDigest	   receiptSha256{} ;
	/// 全描画byte
	std::uint64_t	   renderedBytes = 0 ;
	/// 本文byte
	std::uint32_t	   bodyBytes = 0 ;
	/// 全prefix実数
	std::uint32_t	   prefixTokens = 0 ;
	/// sampled思考
	std::uint32_t	   analysisTokens = 0 ;
	/// 制御closing
	std::uint32_t	   closingTokens = 0 ;
	/// 自然EOG込み
	std::uint32_t	   finalTokens = 0 ;
	/// 自然終端
	bool			   naturalEog = false ;
} ;
/** @brief journalへ未確定の進捗を公開しない固定cursorです */
struct WholeTextBoundedCursor {
	/// flush成功record数
	std::uint32_t	count = 0 ;
	/// 元coreの連続終端
	std::uint64_t	coreEnd = 0 ;
	/// 確定本文byte
	std::uint64_t	bodyBytes = 0 ;
	/// 確定extentの全SHA
	WholeTextDigest bodySha256{} ;
} ;
/** @brief 保存確定とは区別する終端状態です */
enum class WholeTextBoundedOutcome {
	/// 全生成と明示close済み、保存未確定
	READY_FOR_PERSISTENCE,
	/// 失敗、確定prefixだけを保持
	FAILURE,
	/// 利用者取消し、cleanup証拠は独立保持
	CANCELLED
} ;
/** @brief 失敗段階を原文やnative所有なしで保持する値です */
struct WholeTextBoundedResult {
	/// まだ保存未確定
	WholeTextBoundedOutcome			outcome = WholeTextBoundedOutcome::FAILURE ;
	/// 元要求全値
	WholeTextCorrectionRequest		request ;
	/// 元音声の同一性。文章専用では再観測しないASR由来metadata
	DecodedPcmIdentity				pcm ;
	/// 封印計画値
	WholeTextBoundedPromotion		binding ;
	/// 確定prefixだけ
	WholeTextBoundedCursor			committed ;
	/// 未完current
	std::optional<std::uint32_t>	failedIndex ;
	/// current元範囲
	WholeTextByteRange				failedCore ;
	/// primary分類
	ErrorKind						errorKind = ErrorKind::INTERNAL ;
	/// primary元code
	std::uint32_t					errorCode = 0 ;
	/// 最後のnative元status
	std::uint32_t					nativeStatus = 0 ;
	/// 元分類
	std::uint32_t					nativeDiagnostic = 0 ;
	/// 元符号
	std::int32_t					nativeEngineCode = 0 ;
	/// 最初の資源失敗
	WholeTextBoundedResourceFailure resourceFailure ;
	/// 元close全値
	WholeTextNativeCloseReceipt		close ;
	/// retained native owner
	bool							restartRequired = false ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief bounded条件を本文なし診断で拒否します
 * @param[in] valid 条件
 * @param[in] diagnostic 固定分類
 * @param[in] kind 失敗種別
 */
//------------------------------------------------------------------------------
void						  requireWholeTextBounded( bool valid, WholeTextBoundedDiagnostic diagnostic,
													  ErrorKind kind = ErrorKind::INVALID_INPUT) ;
//------------------------------------------------------------------------------
/**
 * @brief 成功生成receiptの固定LE encodingをhashします
 * @param[in] receipt 全値
 * @return 固定SHA
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextDigest wholeTextGeneratedReceiptDigest( const WholeTextBoundedChunkReceipt &receipt) ;
//------------------------------------------------------------------------------
/**
 * @brief 無編集copyの固定LE encodingをhashします
 * @param[in] input 入力SHA
 * @param[in] plan 計画SHA
 * @param[in] index 元順
 * @param[in] core 元範囲
 * @param[in] body 元byte
 * @return copy証拠SHA
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextDigest wholeTextVerbatimReceiptDigest( const WholeTextDigest &input, const WholeTextDigest &plan,
															 std::uint32_t index, WholeTextByteRange core,
															 std::string_view body) ;
//------------------------------------------------------------------------------
/**
 * @brief 生成本文のUTF8/NUL/markerと実counterを照合します
 * @param[in] receipt 全成功値
 * @param[in] body 同期借用本文
 * @param[in] kind 計数receiptを照合する生成方式です。Plainはanalysis/closingを0、Gemmaは個別の上限へ照合し、未知方式は拒否します
 */
//------------------------------------------------------------------------------
void validateWholeTextGeneratedReceipt( const WholeTextBoundedChunkReceipt &receipt, std::string_view body,
									   WholeTextGenerationKind kind = WholeTextGenerationKind::GEMMA_THOUGHT_FINAL) ;
} // namespace koefude
