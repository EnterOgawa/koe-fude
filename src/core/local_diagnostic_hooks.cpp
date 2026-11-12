//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file local_diagnostic_hooks.cpp
 * @brief 設定、モデル、音声/API処理の固定metadata投影を実装します
 * @version 1.0.0
 * @date 2026/11/11
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 1.0.0 Shunji Ogawa 2026/11/11 新規作成
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1397のpath/本文非参照projectionを追加
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
#include "core/local_diagnostic_hooks.h"

#include <cmath>
#include <cstddef>
#include <optional>
#include <variant>

#include "core/api_model_catalog.h"
#include "core/job_controller.h"
#include "inference/model_preparation_controller.h"

namespace koefude {
namespace {
//------------------------------------------------------------------------------
/**
 * @brief 非有限/負値/表現外の時間を未観測に保ちます
 * @param[in] seconds 元の秒数
 * @return 有効なmsまたはnull
 */
//------------------------------------------------------------------------------
std::optional<std::uint64_t> milliseconds( const double seconds) noexcept
{
	// 音声/経過metadataの不正値から整数overflowを作らない
	if( !std::isfinite( seconds) || seconds < 0 || seconds > 9.0e12) {
		return {} ;
	}
	return static_cast<std::uint64_t>( seconds * 1000.0) ;
}
//------------------------------------------------------------------------------
/**
 * @brief file存在検査の結果だけを固定終端へ変換します
 * @param[in] presence 検査済み状態
 * @return ready/missing/invalid
 */
//------------------------------------------------------------------------------
LocalDiagnosticOutcome presenceOutcome( const ModelPresence presence) noexcept
{
	return presence == ModelPresence::VALID		? LocalDiagnosticOutcome::READY
		   : presence == ModelPresence::CORRUPT ? LocalDiagnosticOutcome::INVALID
												: LocalDiagnosticOutcome::MISSING ;
}
} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 設定の固定enumだけを投影します
 * @param[in] options 同期borrow、pathは読まない
 * @return 小さい固定値
 * @see koefude::localConfiguration
 */
//------------------------------------------------------------------------------
LocalDiagnosticRecord localConfiguration( const EngineOptions &options) noexcept
{
	LocalDiagnosticRecord record ; // path/人数/表示名をcopyしない固定snapshot
	record.model = options.asrModel ;
	record.profile = options.asrProfile ;
	record.requestedBackend = options.backend ;
	const auto *definition = findAsrDefinition( options.asrModel, options.asrProfile) ; // 単一registry
	// 具体的な選択weightを固定用途としてだけ記録し、file名/pathをコピーしない
	if( definition) {
		record.asset = definition->modelId ;
	}
	// APIの要求IDをlocal weight用途へ偽装しない
	if( options.asrModel == AsrModel::OPEN_AI_AUDIO) {
		record.apiModel.model = ApiProvenanceModel::GPT_TRANSCRIBE ;
	}
	return record ;
}

//------------------------------------------------------------------------------
/**
 * @brief 要求の用途/モデル/IDだけを投影します
 * @param[in] id 操作ID
 * @param[in] request 同期borrow、秘密/本文を出力しない
 * @return 本文を所有しない小metadata。例外文字列は破棄
 * @see koefude::localJobStarted
 */
//------------------------------------------------------------------------------
LocalDiagnosticRecord localJobStarted( const std::uint64_t id, const JobRequest &request) noexcept
{
	LocalDiagnosticRecord record ; // 要求payloadをloggerへ保持しない
	record.event = LocalDiagnosticEvent::JOB_STARTED ;
	record.operation = id ;
	record.purpose = LocalDiagnosticPurpose::OTHER ;
	// 音声では受付構成とIDだけを読む。入力pathは参照しない
	if( const auto *asr = std::get_if<AsrJobRequest>( &request.payload)) {
		record = localConfiguration( asr->options) ;
		record.event = LocalDiagnosticEvent::JOB_STARTED ;
		record.operation = id ;
		record.history = asr->historyId ;
		record.purpose = asr->audioResume || asr->options.asrModel == AsrModel::OPEN_AI_AUDIO
							 ? LocalDiagnosticPurpose::AUDIO_API
							 : LocalDiagnosticPurpose::LOCAL_ASR ;
		// API要求IDはregistryから固定済みの用途であり、実modelとは呼ばない
		if( record.purpose == LocalDiagnosticPurpose::AUDIO_API) {
			record.apiModel.model = ApiProvenanceModel::GPT_TRANSCRIBE ;
		}
	} else if( const auto *api = std::get_if<ApiTextCorrectionJobRequest>( &request.payload)) {
		record.purpose = LocalDiagnosticPurpose::TEXT_API ;
		record.history = api->key.historyId ;
		record.apiAction = api->action ;
		try {
			// 検証済みsnapshotの厳密IDだけを既知enumへ落とし、元JSONを出力しない
			if( !api->catalogSnapshot.empty()) {
				record.apiModel = apiProvenanceId( apiCatalogSnapshotModel( api->catalogSnapshot)) ;
			}
		} catch( ...) {
			// 元要求の検証はrunnerに任せ、ログだけは未観測として本処理を変えない
			record.apiModel.model = ApiProvenanceModel::UNVERIFIED ;
		}
	} else if( std::holds_alternative<ApiCatalogJobRequest>( request.payload)) {
		record.purpose = LocalDiagnosticPurpose::METADATA ;
	}
	return record ;
}

//------------------------------------------------------------------------------
/**
 * @brief controllerが確定した終端だけを投影します
 * @param[in] event 同期borrow。本文/話者名/pathは読まない
 * @return 小さい固定値。未観測backendはnull
 * @see koefude::localJobTerminal
 */
//------------------------------------------------------------------------------
LocalDiagnosticRecord localJobTerminal( const JobEvent &event) noexcept
{
	LocalDiagnosticRecord record ; // 元本文/応答/例外を出力せずscalarだけを読む
	record.event = LocalDiagnosticEvent::JOB_FINISHED ;
	record.operation = event.id ;
	record.stage = event.progress.stage ;
	record.code = event.code ;
	record.outcome = event.succeeded					   ? LocalDiagnosticOutcome::COMPLETED
					 : event.error == ErrorKind::CANCELLED ? LocalDiagnosticOutcome::CANCELLED
														   : LocalDiagnosticOutcome::DIAGNOSTIC_ERROR ;
	// 失敗の元分類は成功と区別し、APIの未観測codeは0のままとする
	if( !event.succeeded) {
		record.error = event.error ;
	}
	// ASR結果を返す終端だけで実backend/時間を確定する。文章改善の元sourceを誤用しない
	if( event.result && !event.apiText && !event.speakerText && !event.wholeText && !event.wholeTextCompletion &&
	   !event.reading && !event.readingCompletion && !event.boundedPersistence) {
		const auto &result = *event.result ; // 大本文を走査せず件数/実行metadataだけ
		record.model = result.asrModel ;
		record.profile = result.asrProfile ;
		record.durationMs = milliseconds( result.durationSeconds) ;
		record.processingMs = milliseconds( result.totalSeconds) ;
		record.count = result.lines.size() ;
		// 音声APIはCPU ASRを実行したとは記録しない
		if( result.asrModel != AsrModel::OPEN_AI_AUDIO) {
			record.actualBackend = !result.usedGpu						   ? Backend::CPU
								   : result.asrModel == AsrModel::PARAKEET ? Backend::CUDA
																		   : Backend::VULKAN ;
			record.cpuRetried = result.cpuRetried ;
		}
		// 成功の警告/本文なしだけを区別し、部分失敗を成功へ昇格しない
		if( event.succeeded) {
			record.outcome = result.diarizationWarning ? LocalDiagnosticOutcome::WARNING
							 : result.lines.empty()	   ? LocalDiagnosticOutcome::NO_SPEECH
													   : LocalDiagnosticOutcome::COMPLETED ;
		}
	}
	return record ;
}

//------------------------------------------------------------------------------
/**
 * @brief 同じstage/phaseのtickを省きます
 * @param[in] left 前通知
 * @param[in] right 現通知
 * @return 同じ実段階ならtrue
 * @see koefude::sameLocalDiagnosticStage
 */
//------------------------------------------------------------------------------
bool sameLocalDiagnosticStage( const Progress &left, const Progress &right) noexcept
{
	return left.stage == right.stage && left.wholeTextPhase == right.wholeTextPhase &&
		   left.apiText.phase == right.apiText.phase && left.apiAudio.phase == right.apiAudio.phase &&
		   left.speakerText.phase == right.speakerText.phase ;
}

//------------------------------------------------------------------------------
/**
 * @brief 準備workerの開始を投影します
 * @param[in] id 操作ID
 * @param[in] request 固定要求
 * @return pathなしmetadata
 * @see koefude::localModelStarted
 */
//------------------------------------------------------------------------------
LocalDiagnosticRecord localModelStarted( const std::uint64_t id, const ModelPreparationRequest &request) noexcept
{
	auto record = localConfiguration( request.options) ; // path非参照の小値
	record.event = LocalDiagnosticEvent::MODEL_STARTED ;
	record.operation = id ;
	record.reason = request.reason ;
	record.download = request.download ;
	return record ;
}

//------------------------------------------------------------------------------
/**
 * @brief 準備終端と領域別の計画を有限件数で記録します
 * @param[in] log null可
 * @param[in] request 固定要求
 * @param[in] event 同op終端
 * @see koefude::logLocalModelTerminal
 */
//------------------------------------------------------------------------------
void logLocalModelTerminal( LocalDiagnosticLog *log, const ModelPreparationRequest &request,
						   const ModelPreparationEvent &event) noexcept
{
	// 旧内部試験はsinkを渡さず通常領域を触らない
	if( !log) {
		return ;
	}
	auto record = localModelStarted( event.id, request) ; // 同じop/triggerを終端まで保持
	record.event = LocalDiagnosticEvent::MODEL_FINISHED ;
	record.modelStage = event.progress.stage ;
	record.code = event.code ;
	record.outcome = event.progress.stage == ModelPreparationStage::CANCELLED
						 ? LocalDiagnosticOutcome::CANCELLED
						 : LocalDiagnosticOutcome::DIAGNOSTIC_ERROR ;
	// 正常planがあるときだけ検査済みの領域種別と件数を投影する
	if( event.plan) {
		const auto &plan = *event.plan ; // パスは同一性比較だけ。文字列化/出力はしない
		record.outcome = plan.ready ? LocalDiagnosticOutcome::READY : LocalDiagnosticOutcome::MISSING ;
		record.storage = !plan.ready							 ? LocalDiagnosticStorage::NONE
						 : plan.effectiveRoot == plan.cacheRoot	 ? LocalDiagnosticStorage::USER_CACHE
						 : plan.effectiveRoot == plan.bundleRoot ? LocalDiagnosticStorage::APP_MODELS
																 : LocalDiagnosticStorage::NONE ;
		record.count = plan.entries.size() ;
		record.bytes = plan.downloadBytes ;
		record.downloads = plan.transfers.size() ;
		// 最大registry用途数だけを記録し、任意に増えたfake/破損planも有界に保つ
		for( std::size_t index = 0; index < plan.entries.size() && index < 32; ++index) {
			const auto &entry = plan.entries[index] ; // 固定用途の検査metadata
			const bool	reused = entry.action == ModelPreparationAction::REUSE_CACHE ||
								entry.action == ModelPreparationAction::REUSE_BUNDLE ;
			record.reused += reused ? 1 : 0 ;
			record.copies += entry.action == ModelPreparationAction::COPY_BUNDLE ? 1 : 0 ;
			// 準備不能planに検証不一致があればMissingと区別する
			if( !plan.ready && (entry.cache == ModelPresence::CORRUPT || entry.bundle == ModelPresence::CORRUPT)) {
				record.outcome = LocalDiagnosticOutcome::INVALID ;
			}
			auto asset = localModelStarted( event.id, request) ; // pathを持たない用途metadata
			asset.event = LocalDiagnosticEvent::MODEL_ASSET_PLAN ;
			asset.asset = entry.id ;
			asset.count = 1 ;
			asset.assetAction = entry.action ;
			asset.storage = LocalDiagnosticStorage::USER_CACHE ;
			asset.outcome = presenceOutcome( entry.cache) ;
			log->append( asset) ;
			asset.storage = LocalDiagnosticStorage::APP_MODELS ;
			asset.outcome = presenceOutcome( entry.bundle) ;
			log->append( asset) ;
		}
		auto summary = record ; // 終端とは別に計画結果を明示する小copy
		summary.event = LocalDiagnosticEvent::MODEL_PLAN ;
		log->append( summary) ;
	} else {
		record.error = event.error ;
	}
	log->append( record) ;
}
} // namespace koefude
