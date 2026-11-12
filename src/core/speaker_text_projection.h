//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file speaker_text_projection.h
 * @brief 原API本文を変更せず音響参照と対応するbyte範囲だけを定義します
 * @version 1.0.0
 * @date 2026/11/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372方式3の有界対応器とnative参照契約を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include "core/engines.h"
#include "core/types.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace koefude {
constexpr std::uint32_t K_SPEAKER_TEXT_ALGORITHM_VERSION = 3 ; // ADR1372で固定した方式
constexpr std::size_t	K_SPEAKER_TEXT_SCALARS = 4096 ; // 各窓の照合内容scalar上限
constexpr std::size_t	K_SPEAKER_TEXT_MATRIX_CELLS = 4'194'304 ; // 主DPと局所照合の合計cell予算
constexpr std::size_t	K_SPEAKER_TEXT_UNITS = 512 ; // 中立を含む一窓の参照単位上限
constexpr std::size_t	K_SPEAKER_TEXT_TOKENS = 4096 ; // 一窓の発火token上限
constexpr std::size_t	K_SPEAKER_TEXT_WINDOW_BYTES = 256 * 1024 ; // 一窓の原UTF-8上限
constexpr std::uint64_t K_SPEAKER_TEXT_WINDOW_SAMPLES = 120ULL * K_SAMPLE_RATE ; // PCM読取り上限

/** @brief 未記録の来歴をnative実行として補完しない参照種別です */
enum class TextReferenceOrigin : std::uint32_t {
	/// 保存済み区間行から作る限定参照
	SAVED_TRANSCRIPT,
	/// 通常Quality CPUが返したtoken参照
	NATIVE_PARAKEET_TOKENS
} ;
/** @brief 本文を含まない参照候補または対応失敗の固定理由です */
enum class SpeakerTextReason : std::uint32_t {
	/// 全発火範囲が同じ単一候補に被覆される
	SINGLE_CANDIDATE,
	/// 不明候補または発火範囲の被覆不足
	UNKNOWN,
	/// 発火範囲に複数候補が同時に有効
	OVERLAP,
	/// 分割禁止atom内で候補が変化
	CANDIDATE_CHANGE,
	/// 元窓の境界を跨ぐ参照
	CROSS_WINDOW,
	/// token/UTF-8/発火時刻の契約違反
	INVALID_TOKENS,
	/// 固定資源上限または確保失敗
	RESOURCE_LIMIT,
	/// 方式3の全条件を満たす対応なし
	UNMATCHED
} ;
/** @brief 発火tokenを失わない、一候補または中立の参照単位です */
struct TextAlignmentReferenceUnit {
	/// 無編集UTF-8。中立/短応答も保持
	std::string			   text ;
	/// 本単位本文の相対byte終端と元PCM発火ms
	std::vector<TextToken> tokens ;
	/// 対象turnを初出順に正規化した番号。中立は-1
	int					   speaker = -1 ;
	/// 音響候補の固定理由
	SpeakerTextReason	   reason = SpeakerTextReason::UNKNOWN ;
} ;
/** @brief 一つのAPI窓に対する全参照単位の有界snapshotです */
struct TextAlignmentReferenceWindow {
	/// 全PCM内の半開sample範囲
	AsrSampleWindow							samples ;
	/// 実際の取得元
	TextReferenceOrigin						origin = TextReferenceOrigin::NATIVE_PARAKEET_TOKENS ;
	/// 中立も原順に一度だけ保持
	std::vector<TextAlignmentReferenceUnit> units ;
	/// 不正/上限ならunitsは空
	SpeakerTextReason						status = SpeakerTextReason::SINGLE_CANDIDATE ;
} ;
/** @brief 採用した参照単位と文字対応量です。正答率やAPI語時刻ではありません */
struct SpeakerTextMatch {
	/// 対象窓の参照単位番号
	std::size_t referenceUnit = 0 ;
	/// 原API本文の半開byte範囲
	std::size_t byteBegin = 0 ;
	/// 原API本文の終端byte
	std::size_t byteEnd = 0 ;
	/// 対応範囲の内容文字数
	std::size_t contentScalars = 0 ;
	/// 全最小経路で字面も一致した文字数
	std::size_t exactScalars = 0 ;
	/// 内部一意完全一致の最大長
	std::size_t anchorScalars = 0 ;
	/// 参照全文との固定編集距離
	std::size_t editDistance = 0 ;
	/// 全局所最小経路の最大連続不一致
	std::size_t maximumMismatch = 0 ;
	/// 不安全端から内側へ残した中立文字数
	std::size_t trimmedScalars = 0 ;
} ;
/** @brief API本文の原byteを指す表示専用partです。本文を複製しません */
struct SpeakerTextPart {
	/// 元のAPI完全応答番号
	std::size_t				 chunkIndex = 0 ;
	/// 原UTF-8半開byte範囲の先頭
	std::size_t				 byteBegin = 0 ;
	/// 原UTF-8半開byte範囲の終端
	std::size_t				 byteEnd = 0 ;
	/// 対象表示番号。中立は-1
	int						 speaker = -1 ;
	/// 当partを支持する単位。中立では空
	std::vector<std::size_t> referenceUnits ;
	/// 単一支持か中立理由
	SpeakerTextReason		 reason = SpeakerTextReason::UNMATCHED ;
} ;
/** @brief 全byte partitionと固定対応証拠を所有する一窓の結果です */
struct SpeakerTextWindowProjection {
	/// 空本文以外は全byteを一度だけ覆う
	std::vector<SpeakerTextPart>  parts ;
	/// 採用した単位だけの機械的証拠
	std::vector<SpeakerTextMatch> matches ;
	/// 上限時も本文全体を中立にする
	SpeakerTextReason			  status = SpeakerTextReason::UNMATCHED ;
	/// API内容文字数
	std::size_t					  contentScalars = 0 ;
	/// 新しく単一候補を表示する内容文字数
	std::size_t					  projectedScalars = 0 ;
	/// 主DPと局所照合で使用したcell数
	std::size_t					  matrixCells = 0 ;
	/// anchor実scalar比較数
	std::size_t					  featureComparisons = 0 ;
	/// 照合bufferの保守的上限。native全体のpeakではない
	std::size_t					  arenaBytes = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 通常CTCの許可語境界と対象全turnから参照を作ります
 * @param[in] samples 最大120秒の元sample窓
 * @param[in] segments 組立て前の元PCM時刻付きCTC結果。同期借用
 * @param[in] turns 対象APIの全file raw cluster区間。参照側の話者番号は使いません
 * @param[in] stop 協調取消し
 * @return 原順の全単位。token不正/上限は空unitsと固定理由、取消しは例外
 * @throws OperationError 不正な対象turnまたは取消し
 */
//------------------------------------------------------------------------------
[[nodiscard]] TextAlignmentReferenceWindow makeNativeTextReferenceWindow( AsrSampleWindow			  samples,
																		 std::span<const TextSegment> segments,
																		 std::span<const SpeakerTurn> turns,
																		 std::stop_token			  stop = {}) ;

//------------------------------------------------------------------------------
/**
 * @brief 全最小経路ownerと内部一意anchorにより方式3の表示範囲を返します
 * @param[in] chunkIndex 元API完全応答番号
 * @param[in] original 原UTF-8本文。同期borrow。変更・正規化して保存しません
 * @param[in] reference 中立を含む全参照単位。同期borrow
 * @param[in] stop 協調取消し
 * @return 原byteの全partition。上限/不正参照/OOMは全中立、取消しは例外
 * @throws OperationError 原API UTF-8不正または取消し
 * @note network/model/UI/保存I/Oを呼びません。threshold変更の引数は設けません
 */
//------------------------------------------------------------------------------
[[nodiscard]] SpeakerTextWindowProjection  projectSpeakerText( std::size_t chunkIndex, std::string_view original,
															  const TextAlignmentReferenceWindow &reference,
															  std::stop_token					  stop = {}) ;

//------------------------------------------------------------------------------
/**
 * @brief 原UTF-8境界、全partition、単位と話者候補を照合します
 * @param[in] original 原API本文
 * @param[in] projection 検証する表示範囲
 * @param[in] reference 同じ窓の音響参照
 * @return 全byteを一度だけ覆い、採用単位と単一候補が一致する場合true
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool						   validateSpeakerTextPartition( std::string_view					original,
																		const SpeakerTextWindowProjection  &projection,
																		const TextAlignmentReferenceWindow &reference) noexcept ;
} // namespace koefude
