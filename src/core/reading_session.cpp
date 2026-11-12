//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file reading_session.cpp
 * @brief 整形試行の照合、取消し、旧結果保持と表示意図を実装します
 * @version 1.0.0
 * @date 2026/10/04
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/04 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/04 workerの検証済み成功をUIで再走査せず反映
 * 0.1.0 Shunji Ogawa 2026/10/04 原文と旧有効結果を保持する状態接続を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "core/reading_session.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace koefude {
namespace {
//------------------------------------------------------------------------------
/**
 * @brief 不正状態を内容なしの例外へ変えます
 * @param[in] value 成立条件
 */
//------------------------------------------------------------------------------
void requireRecord( const bool value)
{
	// 不整合な状態を推測で修正しない
	if( !value) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 未終端を判定します
 * @param[in] state 状態
 * @return 処理中ならtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool running( const ReadingState state) noexcept
{
	return state >= ReadingState::QUEUED && state <= ReadingState::CANCELLING ;
}

//------------------------------------------------------------------------------
/**
 * @brief 同じ試行を照合します
 * @param[in] record 現状態
 * @param[in] request 受付情報
 * @return 一致時true
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool matches( const ReadingRecord &record, const ReadingRequest &request)
{
	return record.active && record.active->operationId == request.operationId &&
		   record.active->source == request.source && record.active->profileId == request.profileId &&
		   record.active->profileSha256 == request.profileSha256 ;
}

//------------------------------------------------------------------------------
/**
 * @brief 要求のIDと方式を検証します
 * @param[in] request hostの要求
 */
//------------------------------------------------------------------------------
void validateRequest( const ReadingRequest &request)
{
	requireRecord( request.operationId != 0 && request.source.historyId != 0 && !request.profileId.empty() &&
				  request.profileId.size() <= 64) ;
	// ASCIIの固定識別子だけを許し、pathや本文を方式名として扱わない
	for( const unsigned char unit : request.profileId) {
		requireRecord( (unit >= 'a' && unit <= 'z') || (unit >= 'A' && unit <= 'Z') || (unit >= '0' && unit <= '9') ||
					  unit == '-' || unit == '_') ;
	}
}
} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 保存状態と原文への束縛を全体検証します
 * @param[in] source 原文のborrow。結果なしの場合はnull可
 * @param[in] historyId 非0の履歴ID
 * @param[in] record 変更しない保存/進行中状態
 * @throws OperationError 不整合な状態・ID・結果・容量
 * @see koefude::validateReadingRecord
 */
//------------------------------------------------------------------------------
void validateReadingRecord( const Transcript *source, const std::uint64_t historyId, const ReadingRecord &record)
{
	requireRecord( historyId != 0 && static_cast<std::uint32_t>( record.preference) <= 2 &&
				  static_cast<std::uint32_t>( record.state) <= 9 && static_cast<std::uint32_t>( record.reason) <= 9) ;
	requireRecord( record.preference != ReadingPreference::FORMATTED || record.formatted != nullptr) ;
	// 終端理由はstateに対応するものだけを保存する
	if( record.state == ReadingState::FAILURE) {
		requireRecord(
			record.reason == ReadingReason::INVALID_OUTPUT || record.reason == ReadingReason::MODEL_UNAVAILABLE ||
			record.reason == ReadingReason::RESOURCE_UNAVAILABLE || record.reason == ReadingReason::ENGINE_FAILURE) ;
	}
	// 明示取消しの終端だけに取消し理由を許可する
	else if( record.state == ReadingState::CANCELLED) {
		requireRecord( record.reason == ReadingReason::CANCELLED) ;
	}
	// 前processの未完了は中断理由として区別する
	else if( record.state == ReadingState::INTERRUPTED) {
		requireRecord( record.reason == ReadingReason::INTERRUPTED) ;
	} else {
		requireRecord( record.reason == ReadingReason::NONE && record.code == 0) ;
	}
	requireRecord( record.state != ReadingState::COMPLETED || record.formatted != nullptr) ;
	// 本文なしには生成結果や試行を結び付けない
	if( !source || source->lines.empty()) {
		requireRecord( !record.formatted && !record.active && record.state == ReadingState::IDLE) ;
		return ;
	}
	// 最後の有効結果は現在の履歴と不変原文へ結び付いている必要がある
	if( record.formatted) {
		validateFormattedTranscript( *source, {historyId, record.formatted->source.sha256}, *record.formatted) ;
	}
	// process内の進行中情報があれば、永続値と同じ対象であることを検査する
	if( record.active) {
		validateRequest( *record.active) ;
		requireRecord( running( record.state) && record.active->source.historyId == historyId &&
					  record.active->source.sha256 == fingerprintReadingSource( *source)) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 新しい試行を開始し、旧有効結果を保持します
 * @param[in,out] record 直列化済みの試行状態
 * @param[in] source 不変原文。workerだけで検証します
 * @param[in] request 受付IDを設定済みの要求
 * @return 受付時true、既に処理中/本文なしならfalse
 * @throws OperationError 要求/原文/旧結果が不整合。失敗時は元状態を保持
 * @see koefude::beginReading
 */
//------------------------------------------------------------------------------
bool beginReading( ReadingRecord &record, const Transcript &source, const ReadingRequest &request)
{
	// 同じ結果への二重試行や本文なしの生成を受け付けない
	if( running( record.state) || source.lines.empty()) {
		return false ;
	}
	validateRequest( request) ;
	validateReadingRecord( &source, request.source.historyId, record) ;
	requireRecord( request.source.sha256 == fingerprintReadingSource( source)) ;
	record.active = request ; // allocationを終えてから状態を変更する
	record.state = ReadingState::QUEUED ;
	record.reason = ReadingReason::NONE ;
	record.code = 0 ;
	return true ;
}

//------------------------------------------------------------------------------
/**
 * @brief 全体検証後に応答の所有権を保持します
 * @param[in] source 同じ要求の不変原文、null不可
 * @param[in] request 受付ID設定済みの固定要求
 * @param[in] response 全行応答、null不可
 * @throws OperationError 原文/ID/profile/行構造/容量の不一致
 * @note hashと全行走査を行うためworker限定です
 * @see koefude::ValidatedReadingCompletion::ValidatedReadingCompletion
 */
//------------------------------------------------------------------------------
ValidatedReadingCompletion::ValidatedReadingCompletion( std::shared_ptr<const Transcript> source, ReadingRequest request,
													   std::shared_ptr<const ReadingResponse> response)
	: source_( std::move( source)), request_( std::move( request))
{
	requireRecord( source_ != nullptr && response != nullptr) ;
	validateReadingResponse( *source_, request_, *response) ;
	formatted_ = std::shared_ptr<const FormattedTranscript>( response, &response->result) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 固定要求を読みます
 * @return このownerの寿命内で有効な要求
 * @see koefude::ValidatedReadingCompletion::request
 */
//------------------------------------------------------------------------------
const ReadingRequest &ValidatedReadingCompletion::request() const noexcept
{
	return request_ ;
}
//------------------------------------------------------------------------------
/**
 * @brief 原文の共有ownerを読みます
 * @return 不変原文の所有値
 * @see koefude::ValidatedReadingCompletion::source
 */
//------------------------------------------------------------------------------
const std::shared_ptr<const Transcript> &ValidatedReadingCompletion::source() const noexcept
{
	return source_ ;
}
//------------------------------------------------------------------------------
/**
 * @brief 二重copyしない整形ownerを読みます
 * @return 検証済み整形の所有値
 * @see koefude::ValidatedReadingCompletion::formatted
 */
//------------------------------------------------------------------------------
const std::shared_ptr<const FormattedTranscript> &ValidatedReadingCompletion::formatted() const noexcept
{
	return formatted_ ;
}
//------------------------------------------------------------------------------
/**
 * @brief 検証済み投影のkeyを使って受付状態を準備します
 * @param[in,out] record 既に検証済みの保存状態。失敗時は不変
 * @param[in] view 完成した原文/整形投影。同じ原文ownerであることを呼出側で確認します
 * @param[in] request 非0の仮受付IDを持つ小さい固定要求
 * @return 開始できた場合true。処理中/本文なしならfalse
 * @note UIでhash/全行を再走査しません。受付後、実IDをactiveへ設定します
 * @see koefude::beginPreparedReading
 */
//------------------------------------------------------------------------------
bool beginPreparedReading( ReadingRecord &record, const ReadingViewSnapshot &view, const ReadingRequest &request)
{
	// 進行中の試行と本文なしには別の受付状態を重ねない
	if( running( record.state) || view.lines().empty()) {
		return false ;
	}
	validateRequest( request) ;
	requireRecord( request.source == view.key()) ;
	requireRecord( static_cast<std::uint32_t>( record.preference) <= 2 && static_cast<std::uint32_t>( record.state) <= 9 &&
				  (record.preference != ReadingPreference::FORMATTED || record.formatted)) ;
	// 旧結果も同じ原文keyに結び付いている保存状態だけを受け付ける
	if( record.formatted) {
		requireRecord( record.formatted->source == view.key()) ;
	}
	record.active = request ; // 小さいcopyの確保が成功してから状態を進める
	record.state = ReadingState::QUEUED ;
	record.reason = ReadingReason::NONE ;
	record.code = 0 ;
	return true ;
}
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
 * @see koefude::finishPreparedReading
 */
//------------------------------------------------------------------------------
bool finishPreparedReading( ReadingRecord &record, const std::shared_ptr<const Transcript> &source,
						   const ReadingRequest &request, std::shared_ptr<const ValidatedReadingCompletion> completion,
						   const ErrorKind error, const std::uint32_t code)
{
	// 消去済み/古い試行と原文なしへ成功をinstallしない
	if( !source || !matches( record, request)) {
		return false ;
	}
	// 取消しと応答なしは、全行検証をしない既存の終端経路へ渡す
	if( record.state == ReadingState::CANCELLING || error == ErrorKind::CANCELLED || !completion) {
		return finishReading( record, *source, request, nullptr, error, code) ;
	}
	const auto &fixed = completion->request() ; // workerが検証した小さい受付metadata
	// 同じ本文でも別owner/履歴/方式/operationの成功は採用しない
	if( completion->source() != source || fixed.operationId != request.operationId || fixed.source != request.source ||
	   fixed.profileId != request.profileId || fixed.profileSha256 != request.profileSha256) {
		return finishReading( record, *source, request, nullptr, ErrorKind::INVALID_INPUT, code) ;
	}
	record.formatted = completion->formatted() ; // 検証済み所有aliasを本文copyなしで移す
	record.state = ReadingState::COMPLETED ;
	record.reason = ReadingReason::NONE ;
	record.code = 0 ;
	record.active.reset() ;
	return true ;
}

//------------------------------------------------------------------------------
/**
 * @brief 同じ試行の実進捗だけを反映します
 * @param[in,out] record 直列化済みの状態
 * @param[in] request 対象の受付情報
 * @param[in] progress 実進捗
 * @see koefude::updateReading
 */
//------------------------------------------------------------------------------
void updateReading( ReadingRecord &record, const ReadingRequest &request, const Progress progress)
{
	// 別試行と取消し開始後の進捗で状態を巻き戻さない
	if( !matches( record, request) || record.state == ReadingState::CANCELLING) {
		return ;
	}
	record.state = progress.stage == Stage::LOADING_MODEL ? ReadingState::LOADING
				   : progress.stage == Stage::COMBINING	  ? ReadingState::VALIDATING
														  : ReadingState::FORMATTING ;
}

//------------------------------------------------------------------------------
/**
 * @brief 進行中の試行を取消し待ちにします
 * @param[in,out] record 旧有効結果を保持する状態
 * @note workerのstop要求は呼出し側が同じoperationへ別途通知します
 * @see koefude::cancelReading
 */
//------------------------------------------------------------------------------
void cancelReading( ReadingRecord &record)
{
	// 現在の試行だけを取消し待ちへ進め、最後の有効結果を変更しない
	if( record.active && running( record.state)) {
		record.state = ReadingState::CANCELLING ;
	}
}

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
 * @see koefude::finishReading
 */
//------------------------------------------------------------------------------
bool finishReading( ReadingRecord &record, const Transcript &source, const ReadingRequest &request,
				   std::shared_ptr<const ReadingResponse> response, const ErrorKind error, const std::uint32_t code)
{
	// 終端受領済み・別source・別方式の結果を現在の試行へ適用しない
	if( !matches( record, request)) {
		return false ;
	}
	record.code = code ;
	// 取消し競合では成功応答も捨て、旧結果を残す
	if( record.state == ReadingState::CANCELLING || error == ErrorKind::CANCELLED) {
		record.state = ReadingState::CANCELLED ;
		record.reason = ReadingReason::CANCELLED ;
	}
	// 未取消しの全行応答だけを検証し、成功時に旧結果を置き換える
	else if( response) {
		try {
			validateReadingResponse( source, request, *response) ;
			// 応答のownerを共有するaliasで置換本文の二重copyを避ける
			record.formatted = std::shared_ptr<const FormattedTranscript>( response, &response->result) ;
			record.state = ReadingState::COMPLETED ;
			record.reason = ReadingReason::NONE ;
			record.code = 0 ;
		} catch( const OperationError &failure) {
			record.state = ReadingState::FAILURE ;
			record.reason = ReadingReason::INVALID_OUTPUT ;
			record.code = failure.code() ;
		}
	} else {
		record.state = ReadingState::FAILURE ;
		record.reason = error == ErrorKind::MODEL_MISSING || error == ErrorKind::MODEL_REJECTED
							? ReadingReason::MODEL_UNAVAILABLE
						: error == ErrorKind::GPU_UNAVAILABLE || error == ErrorKind::RESULT_TOO_LARGE
							? ReadingReason::RESOURCE_UNAVAILABLE
						: error == ErrorKind::INVALID_INPUT ? ReadingReason::INVALID_OUTPUT
															: ReadingReason::ENGINE_FAILURE ;
	}
	record.active.reset() ;
	return true ;
}

//------------------------------------------------------------------------------
/**
 * @brief 保存された表示意図を推論なしに変更します
 * @param[in,out] record 変更する表示意図
 * @param[in] preference 新しい選択
 * @return 有効な選択ならtrue。結果なしのFormatted/未知値はfalse
 * @see koefude::selectReading
 */
//------------------------------------------------------------------------------
bool selectReading( ReadingRecord &record, const ReadingPreference preference)
{
	// 結果なしの整形選択と未知の意図を保存しない
	if( static_cast<std::uint32_t>( preference) > 2 ||
	   (preference == ReadingPreference::FORMATTED && !record.formatted)) {
		return false ;
	}
	record.preference = preference ;
	return true ;
}

//------------------------------------------------------------------------------
/**
 * @brief 保存DTOから実際の表示版を決めます
 * @param[in] record 検証済みの状態
 * @return 原文または整形文。推論/取得を呼びません
 * @see koefude::resolveReadingDisplay
 */
//------------------------------------------------------------------------------
ReadingDisplay resolveReadingDisplay( const ReadingRecord &record) noexcept
{
	return record.preference != ReadingPreference::ORIGINAL && record.formatted ? ReadingDisplay::FORMATTED
																				: ReadingDisplay::ORIGINAL ;
}

//------------------------------------------------------------------------------
/**
 * @brief 再起動時の未完了試行を中断へ変え、旧結果を保持します
 * @param[in,out] record 全体検証後の保存状態
 * @see koefude::restoreReading
 */
//------------------------------------------------------------------------------
void restoreReading( ReadingRecord &record) noexcept
{
	// 前processの未終端は再開せず、旧有効結果を残した中断状態にする
	if( running( record.state)) {
		record.state = ReadingState::INTERRUPTED ;
		record.reason = ReadingReason::INTERRUPTED ;
		record.code = 0 ;
	}
	record.active.reset() ;
}

} // namespace koefude
