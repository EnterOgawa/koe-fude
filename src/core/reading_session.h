//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file reading_session.h
 * @brief 整形試行・表示意図・最後の有効結果の保持を定義します
 * @version 1.0.0
 * @date 2026/10/04
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/04 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/04 検証済み応答と軽量なアプリ反映の契約を追加
 * 0.1.0 Shunji Ogawa 2026/10/04 整形試行と永続結果を分離
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 * @note 呼出し側で直列化し、hash・検証を伴うAPIはworkerで使用します
 */
//------------------------------------------------------------------------------
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>

#include "core/reading_view.h"

namespace koefude {

/** @brief 実際の表示版と区別する利用者の選択意図です */
enum class ReadingPreference : std::uint32_t {
	/// 有効な整形結果があれば整形文
	AUTOMATIC,
	/// 結果の有無によらず原文
	ORIGINAL,
	/// 有効な整形結果を明示選択
	FORMATTED
} ;

/** @brief ASRの終端状態から独立した整形試行の状態です */
enum class ReadingState : std::uint32_t {
	/// 試行なし
	IDLE,
	/// worker待ち
	QUEUED,
	/// 生成器準備中
	LOADING,
	/// 本文生成中
	FORMATTING,
	/// 全体照合中
	VALIDATING,
	/// 協調取消し待ち
	CANCELLING,
	/// 検証済み整形結果を確定
	COMPLETED,
	/// 現試行失敗。旧結果は保持
	FAILURE,
	/// 現試行取消し
	CANCELLED,
	/// 再起動前に未完了
	INTERRUPTED
} ;

/** @brief 永続化する整形試行の終了理由です */
enum class ReadingReason : std::uint32_t {
	/// 理由なし
	NONE,
	/// 行単位理由の予約値。試行には使わない
	ATTRIBUTION_UNCERTAIN,
	/// 行単位理由の予約値。試行には使わない
	UNIT_TOO_LARGE,
	/// 行単位理由の予約値。試行には使わない
	CAPACITY,
	/// 不正な生成結果
	INVALID_OUTPUT,
	/// 採用済み生成器/モデルなし
	MODEL_UNAVAILABLE,
	/// 必要資源なし
	RESOURCE_UNAVAILABLE,
	/// 明示取消し
	CANCELLED,
	/// 前回未完了
	INTERRUPTED,
	/// 生成器内部の失敗
	ENGINE_FAILURE
} ;

/** @brief 保存可能な結果と非永続の進行中照合情報を所有します */
struct ReadingRecord {
	/// 既定の表示意図
	ReadingPreference						   preference = ReadingPreference::AUTOMATIC ;
	/// 最も新しい試行状態
	ReadingState							   state = ReadingState::IDLE ;
	/// 試行の終了理由
	ReadingReason							   reason = ReadingReason::NONE ;
	/// 本文を含まない診断値
	std::uint32_t							   code = 0 ;
	/// 最後の有効な整形結果
	std::shared_ptr<const FormattedTranscript> formatted ;
	/// process内だけで有効な試行。保存しない
	std::optional<ReadingRequest>			   active ;
} ;

/** @brief 採用・準備済み方式を指す小さい受付metadataです。モデル一覧ではありません */
struct ReadingProfile {
	/// 固定ASCII識別子、最大64byte
	std::string					 id ;
	/// 方式と実効設定の固定digest
	std::array<std::uint8_t, 32> sha256{} ;
} ;

/** @brief workerが原文と全行を検証した成功だけを所有します */
class ValidatedReadingCompletion final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 全体検証後に応答の所有権を保持します
	 * @param[in] source 同じ要求の不変原文、null不可
	 * @param[in] request 受付ID設定済みの固定要求
	 * @param[in] response 全行応答、null不可
	 * @throws OperationError 原文/ID/profile/行構造/容量の不一致
	 * @note hashと全行走査を行うためworker限定です
	 */
	//------------------------------------------------------------------------------
	ValidatedReadingCompletion( std::shared_ptr<const Transcript> source, ReadingRequest request,
							   std::shared_ptr<const ReadingResponse> response) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定要求を読みます
	 * @return このownerの寿命内で有効な要求
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const ReadingRequest							   &request() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 原文の共有ownerを読みます
	 * @return 不変原文の所有値
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const std::shared_ptr<const Transcript>		   &source() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 二重copyしない整形ownerを読みます
	 * @return 検証済み整形の所有値
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const std::shared_ptr<const FormattedTranscript> &formatted() const noexcept ;

	private:
	/// 検証した元のowner
	std::shared_ptr<const Transcript>		   source_ ;
	/// 検証したID/source/profile
	ReadingRequest							   request_ ;
	/// 応答全体を保持するalias
	std::shared_ptr<const FormattedTranscript> formatted_ ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 検証済み投影のkeyを使って受付状態を準備します
 * @param[in,out] record 既に検証済みの保存状態。失敗時は不変
 * @param[in] view 完成した原文/整形投影。同じ原文ownerであることを呼出側で確認します
 * @param[in] request 非0の仮受付IDを持つ小さい固定要求
 * @return 開始できた場合true。処理中/本文なしならfalse
 * @note UIでhash/全行を再走査しません。受付後、実IDをactiveへ設定します
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool beginPreparedReading( ReadingRecord &record, const ReadingViewSnapshot &view,
										const ReadingRequest &request) ;

//------------------------------------------------------------------------------
/**
 * @brief 検証済み成功を軽量に反映し、失敗/取消しでは旧結果を保持します
 * @param[in,out] record 同じoperationの状態
 * @param[in] source 受付時と同じ不変原文owner。null不可
 * @param[in] request ID設定済みの受付要求
 * @param[in] completion workerで検証した成功、失敗ならnull
 * @param[in] error 元の失敗分類。Cancelledは成功より優先
 * @param[in] code 本文を含まない元診断
 * @return 同じ試行の終端を受領した場合true、古い試行ならfalse
 * @note UIで本文のhash/全行検証を行いません。owner/固定metadataを照合します
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool finishPreparedReading( ReadingRecord &record, const std::shared_ptr<const Transcript> &source,
										 const ReadingRequest							  &request,
										 std::shared_ptr<const ValidatedReadingCompletion> completion,
										 ErrorKind error = ErrorKind::INTERNAL, std::uint32_t code = 0) ;

/** @brief worker限定・同期の生成器境界です。今回、製品用実装は登録しません */
class ITranscriptFormatter {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有資源を解放します
	 */
	//------------------------------------------------------------------------------
	virtual ~ITranscriptFormatter() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 不変原文を対象に契約版1の全行結果を生成します
	 * @param[in] source 呼出し中有効な共有所有原文。null不可
	 * @param[in] request hostが固定したID・方式・原文key
	 * @param[in] stop 協調取消し
	 * @param[in] progress LoadingModel/Transcribingで実処理を通知するcallback
	 * @return 全行の未検証結果。失敗時は例外とし部分成功を返しません
	 * @note engine contextはこの同期呼出し中にworkerで所有・解放します
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual ReadingResponse format( std::shared_ptr<const Transcript> source,
												 const ReadingRequest &request, std::stop_token stop,
												 const ProgressCallback &progress) = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 保存状態と原文への束縛を全体検証します
 * @param[in] source 原文のborrow。結果なしの場合はnull可
 * @param[in] historyId 非0の履歴ID
 * @param[in] record 変更しない保存/進行中状態
 * @throws OperationError 不整合な状態・ID・結果・容量
 */
//------------------------------------------------------------------------------
void validateReadingRecord( const Transcript *source, std::uint64_t historyId, const ReadingRecord &record) ;

//------------------------------------------------------------------------------
/**
 * @brief 新しい試行を開始し、旧有効結果を保持します
 * @param[in,out] record 直列化済みの試行状態
 * @param[in] source 不変原文。workerだけで検証します
 * @param[in] request 受付IDを設定済みの要求
 * @return 受付時true、既に処理中/本文なしならfalse
 * @throws OperationError 要求/原文/旧結果が不整合。失敗時は元状態を保持
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool beginReading( ReadingRecord &record, const Transcript &source, const ReadingRequest &request) ;

//------------------------------------------------------------------------------
/**
 * @brief 同じ試行の実進捗だけを反映します
 * @param[in,out] record 直列化済みの状態
 * @param[in] request 対象の受付情報
 * @param[in] progress 実進捗
 */
//------------------------------------------------------------------------------
void			   updateReading( ReadingRecord &record, const ReadingRequest &request, Progress progress) ;

//------------------------------------------------------------------------------
/**
 * @brief 進行中の試行を取消し待ちにします
 * @param[in,out] record 旧有効結果を保持する状態
 * @note workerのstop要求は呼出し側が同じoperationへ別途通知します
 */
//------------------------------------------------------------------------------
void			   cancelReading( ReadingRecord &record) ;

//------------------------------------------------------------------------------
/**
 * @brief 終端を照合し、検証成功した全結果だけをinstallします
 * @param[in,out] record 現試行と旧有効結果
 * @param[in] source 要求に対応する不変原文
 * @param[in] request 受付情報
 * @param[in] response 成功応答の共有所有値。失敗時はnull
 * @param[in] error 失敗分類。responseありの場合もCancelledは優先します
 * @param[in] code 元の診断値
 * @return 同じ試行の終端を処理した場合true。古い/別試行はfalse
 * @note 不正出力はFailedに変換し、原文と旧有効結果を保持します
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool finishReading( ReadingRecord &record, const Transcript &source, const ReadingRequest &request,
								 std::shared_ptr<const ReadingResponse> response, ErrorKind error = ErrorKind::INTERNAL,
								 std::uint32_t code = 0) ;

//------------------------------------------------------------------------------
/**
 * @brief 保存された表示意図を推論なしに変更します
 * @param[in,out] record 変更する表示意図
 * @param[in] preference 新しい選択
 * @return 有効な選択ならtrue。結果なしのFormatted/未知値はfalse
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool selectReading( ReadingRecord &record, ReadingPreference preference) ;

//------------------------------------------------------------------------------
/**
 * @brief 保存DTOから実際の表示版を決めます
 * @param[in] record 検証済みの状態
 * @return 原文または整形文。推論/取得を呼びません
 */
//------------------------------------------------------------------------------
[[nodiscard]] ReadingDisplay resolveReadingDisplay( const ReadingRecord &record) noexcept ;

//------------------------------------------------------------------------------
/**
 * @brief 再起動時の未完了試行を中断へ変え、旧結果を保持します
 * @param[in,out] record 全体検証後の保存状態
 */
//------------------------------------------------------------------------------
void						 restoreReading( ReadingRecord &record) noexcept ;

} // namespace koefude
