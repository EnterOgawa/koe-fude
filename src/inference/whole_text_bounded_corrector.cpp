//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_bounded_corrector.cpp
 * @brief 原文を変更せずcore順に生成と空白copyを確定します
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1285の有界逐次補正を接続
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1287の明示診断と隔離接続を追加
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1343の文章専用補正を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実使用の標準headerを直接include
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "inference/whole_text_bounded_corrector.h"
#include <span>
#include <algorithm>
#include <array>
#include <new>
#include <utility>
namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief 全計画→promotion→逐次core→明示closeを一回実行します
 * @param[in] prepared 材料/計画/live sessionをmoveするowner
 * @param[in] terminal 生成前に全通知値/journalを確保した保存前terminal
 * @param[in] guard 同じbaseline
 * @param[in] gate weak通知gate、null可
 * @param[in] stop 停止
 * @param[in] diagnostics 任意の固定scalar capture、null可
 * @return native所有物のない未保存terminal
 * @see koefude::NativeBoundedWholeTextCorrector::execute
 */
//------------------------------------------------------------------------------
WholeTextBoundedTerminal
NativeBoundedWholeTextCorrector::execute( PreparedBoundedWholeTextPlan prepared, WholeTextBoundedTerminal terminal,
										 std::shared_ptr<WholeTextResourceGuard>	   guard,
										 std::shared_ptr<WholeTextBoundedProgressGate> gate, std::stop_token stop,
										 std::shared_ptr<CorrectionDiagnosticCapture> diagnostics)
{
	std::array<std::uint8_t, 65536> output{} ; // 一currentだけ、生成前に確保
	auto						   &result = terminal.result ; // 通知用小領域も生成前に所有
	const auto					   &plan = prepared.plan() ; // 一つのsealed計画
	const auto					   &materials = *prepared.materials() ; // source/raw/trace全量の同じowner
	const auto						view = materials.view( plan.inputBinding,
														  wholeTextGenerationKind( prepared.request().profileId,
																				  prepared.request().profileSha256)) ; // 同期中だけ有効
	try {
		requireWholeTextBounded( terminal.journal && guard && terminal.source == materials.source(),
								WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
		requireWholeTextBounded( result.request.source == prepared.request().source &&
									result.request.operationId == prepared.request().operationId &&
									result.request.profileId == prepared.request().profileId &&
									result.request.profileSha256 == prepared.request().profileSha256 &&
									terminal.source->pcmIdentity && result.pcm == *terminal.source->pcmIdentity,
								WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
		materials.validate( stop) ;
		validateWholeTextChunkPlan( view, plan, stop) ;
		guard->check( stop) ;
		result.binding = {plan.inputBinding,
						  plan.planSha256,
						  prepared.inputSha256(),
						  plan.chunks.back().core.end,
						  plan.measures,
						  plan.measuredBytes,
						  static_cast<std::uint32_t>( plan.chunks.size()),
						  0} ;
		// 全生成件数を昇格前に固定します
		for( const auto &record : plan.chunks) { // 元plan順
			// 不整合や終了状態を分岐させ、未確定のまま後続へ進みません
			if( !record.verbatimWhitespace) {
				++result.binding.generationCount ;
			}
		}
		terminal.journal->seal( {prepared.request(), result.pcm, result.binding, plan.originalSha256},
							   materials.auxiliary(), plan) ; // write/flush済みでなければpromotion0
		guard->check( stop) ;
		prepared.session().promote( result.binding) ; // 一tensorだけ
		// 一coreだけをmaterializeし、KV/Analysis/Finalを次へ持越しません
		for( const auto &record : plan.chunks) { // sealed元順、失敗時は直ちに終端
			result.failedIndex = record.index ;
			result.failedCore = record.core ;
			static_cast<void>( guard->poll( true)) ;
			guard->check( stop) ;
			// whitespaceは原sliceの同じbyteだけを無編集copyします
			if( record.verbatimWhitespace) {
				std::size_t size = 0 ; // 一core出力byte
				// 全一次sliceを元順で連結し、原文の反復も残します
				for( const auto &slice : record.original) { // 元slice順
					const auto text = view.original[slice.origin].text.substr(
						static_cast<std::size_t>( slice.bytes.begin),
						static_cast<std::size_t>( slice.bytes.end - slice.bytes.begin)) ; // 同期borrow
					requireWholeTextBounded( text.size() <= output.size() - size,
											WholeTextBoundedDiagnostic::AGGREGATE_BYTES, ErrorKind::RESULT_TOO_LARGE) ;
					std::copy( text.begin(), text.end(), output.begin() + static_cast<std::ptrdiff_t>( size)) ;
					size += text.size() ;
				}
				terminal.journal->append( record, std::string_view( reinterpret_cast<const char *>( output.data()), size),
										 nullptr, stop) ;
			} else {
				const auto prompt = materializeWholeTextChunk( view, record, stop) ; // 一候補だけ
				const auto capacity = static_cast<std::size_t>(
					std::min<std::uint64_t>( output.size(),
											terminal.journal->remaining())) ; // aggregate残量も同時検査
				requireWholeTextBounded( capacity > 0, WholeTextBoundedDiagnostic::AGGREGATE_BYTES,
										ErrorKind::RESULT_TOO_LARGE) ;
				// 実callの直前だけ既知call数を増やします
				if( diagnostics) {
					diagnostics->nativeCall() ;
				}
				const auto receipt = prepared.session().generateChunk( prompt, record, prepared.inputSha256(),
																	  std::span( output).first( capacity)) ;
				// 成功receiptだけを実CPU生成として記録します
				if( diagnostics) {
					diagnostics->nativeCompleted() ;
				}
				guard->check( stop) ;
				terminal.journal->append(
					record, std::string_view( reinterpret_cast<const char *>( output.data()), receipt.bodyBytes),
					&receipt, stop) ;
			}
			result.committed = terminal.journal->cursor() ; // flush済みだけ
			// 確定した元core byteからだけ進捗を通知します
			if( gate && gate->notify( 5, static_cast<std::uint32_t>( result.committed.coreEnd),
									static_cast<std::uint32_t>( result.binding.originalBytes)) != 0) {
				throw OperationError( ErrorKind::CANCELLED,
									 static_cast<std::uint32_t>( KoefudeWtDiagnostic::CALLBACK_FAILURE)) ;
			}
		}
		terminal.journal->validatePrefix( plan) ; // 全receiptと本文SHA
		requireWholeTextBounded( result.committed.count == plan.chunks.size() &&
									result.committed.coreEnd == result.binding.originalBytes,
								WholeTextBoundedDiagnostic::CHUNK_RECEIPT) ;
		// 新文章専用profileだけ全文flush後に限定字面保護を検査します
		if( prepared.request().profileId == K_TEXT_CONTEXT_PROFILE_ID) {
			materials.validate( stop) ; // 生成後も原文全fieldのfingerprintを再照合します
			requireWholeTextBounded( textContextProtectedSpansMatch(
										*terminal.source, result.committed.bodyBytes,
				//------------------------------------------------------------------------------
				/**
				 * @brief 同じJの確定候補を有界に読みます
				 * @param[in] offset 本文位置
				 * @param[out] bytes 最大64KiBの同期領域
				 */
				//------------------------------------------------------------------------------
				[&]( std::uint64_t offset, std::span<std::uint8_t> bytes)
				{
					terminal.journal->readPrefix( offset, bytes) ;
				},
										stop),
									WholeTextBoundedDiagnostic::PROTECTED_SPAN_CHANGED) ;
		}
		result.failedIndex.reset() ;
		result.failedCore = {} ;
		guard->check( stop) ;
		result.outcome = WholeTextBoundedOutcome::READY_FOR_PERSISTENCE ; // 有効全文への適用なし
		result.errorCode = 0 ;
	} catch( const OperationError &error) {
		result.errorKind = stop.stop_requested() ? ErrorKind::CANCELLED : error.kind() ;
		result.errorCode = error.code() ;
		result.outcome = result.errorKind == ErrorKind::CANCELLED ? WholeTextBoundedOutcome::CANCELLED
																  : WholeTextBoundedOutcome::FAILURE ;
	} catch( const std::bad_alloc &) {
		result.errorKind = stop.stop_requested() ? ErrorKind::CANCELLED : ErrorKind::RESULT_TOO_LARGE ;
		result.errorCode = static_cast<std::uint32_t>( WholeTextBoundedDiagnostic::JOURNAL_CAPACITY) ;
		result.outcome = stop.stop_requested() ? WholeTextBoundedOutcome::CANCELLED : WholeTextBoundedOutcome::FAILURE ;
	} catch( ...) {
		result.errorKind = stop.stop_requested() ? ErrorKind::CANCELLED : ErrorKind::INTERNAL ;
		result.errorCode = static_cast<std::uint32_t>( WholeTextBoundedDiagnostic::CHUNK_RECEIPT) ;
		result.outcome = stop.stop_requested() ? WholeTextBoundedOutcome::CANCELLED : WholeTextBoundedOutcome::FAILURE ;
	}
	// 終端前にweak通知を無効化し、全経路で明示closeを一回だけ確認します
	if( gate) {
		gate->disable() ;
	}
	const auto native = prepared.session().failureSnapshot() ; // close前に元call値を所有
	result.nativeStatus = native.status ;
	result.nativeDiagnostic = native.diagnostic ;
	result.nativeEngineCode = native.engineCode ;
	result.close = prepared.session().closeChecked() ;
	result.restartRequired = result.close.restartRequired ;
	// close内で経過した累計時間と資源も終端成功判定前に照会します
	if( guard) {
		static_cast<void>( guard->poll( true)) ;
	}
	const auto failure =
		guard ? guard->failure() : WholeTextResourceFailure{} ; // この同期処理で使う所有値または検証値
	result.resourceFailure = {failure.reason, failure.code} ;
	// 遅い取消しもprimaryにし、元native/cleanup失敗の別snapshotを保持します
	if( stop.stop_requested()) {
		result.outcome = WholeTextBoundedOutcome::CANCELLED ;
		result.errorKind = ErrorKind::CANCELLED ;
		result.errorCode = 0 ; // raw native/close codeはそれぞれのfieldへ残します
		// 不整合や終了状態を分岐させ、未確定のまま後続へ進みません
	} else if( result.outcome == WholeTextBoundedOutcome::READY_FOR_PERSISTENCE) {
		// 最初のguard失敗をrestartrequiredに読み替えません
		if( failure.reason != 0) {
			result.outcome = WholeTextBoundedOutcome::FAILURE ;
			result.errorKind = ErrorKind::RESULT_TOO_LARGE ;
			result.errorCode = failure.code ;
			// 所有と解放確認の状態に従い、未確認の資源を再解放しません
		} else if( !result.close.releaseConfirmed || result.close.rawStatus != 0) {
			result.outcome = WholeTextBoundedOutcome::FAILURE ;
			result.errorKind = ErrorKind::INFERENCE_FAILED ;
			result.errorCode = result.close.rawEngineCode != 0
								   ? static_cast<std::uint32_t>( result.close.rawEngineCode)
								   : (result.close.rawDiagnostic != 0
										  ? result.close.rawDiagnostic
										  : static_cast<std::uint32_t>( WholeTextBoundedDiagnostic::RESTART_REQUIRED)) ;
		}
	}
	// 保存されるprefixとnative retained Stateを別のownerに保ちます
	if( terminal.journal) {
		result.committed = terminal.journal->cursor() ;
	}
	return terminal ; // move-only、材料/sessionはこのscopeで破棄
}
} // namespace koefude
