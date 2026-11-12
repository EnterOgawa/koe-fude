//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file speaker_text_application.cpp
 * @brief begin耐久ACKとPCM一致を参照生成の開始条件にします
 * @version 1.0.0
 * @date 2026/11/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372のnative/cache来歴と前採用保持を接続
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
#include "inference/speaker_text_application.h"
#include "inference/model_store.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <exception>
#include <new>
#include <utility>
namespace koefude {
namespace {
//------------------------------------------------------------------------------
/**
 * @brief 実採取前に固定registry metadataを凍結します
 * @return 通常4asset
 */
//------------------------------------------------------------------------------
std::vector<SpeakerTextAsset> assets()
{
	const auto &definition = asrDefinition( AsrModel::PARAKEET, AsrProfile::QUALITY) ; // 正本registry
	// 固定Qualityの必須asset定義が欠けた構成を拒否する
	if( !definition.modelId || !definition.tokensModel || !definition.externalDataModel) {
		throw OperationError( ErrorKind::MODEL_REJECTED) ;
	}
	const std::array			  ids{*definition.modelId, *definition.tokensModel, *definition.externalDataModel,
						  ModelId::VOICE_ACTIVITY} ; // 実使用用途
	std::vector<SpeakerTextAsset> out ; // 小さい固定snapshot
	// 実使用するgraph/data/tokens/VADだけを凍結する
	for( const auto id : ids) {
		const auto &asset = modelDefinition( id) ; // モデルfileは開かない
		// revision/size/SHA欠落はnative開始より前に拒否する
		if( asset.revision.empty() || asset.sha256.size() != 64 || !asset.bytes) {
			throw OperationError( ErrorKind::MODEL_REJECTED) ;
		}
		out.push_back( {std::string( asset.id), std::string( asset.revision), asset.bytes, std::string( asset.sha256)}) ;
	}
	return out ;
}
//------------------------------------------------------------------------------
/**
 * @brief 保存参照が現固定資産と同じ場合だけ再利用します
 * @param[in] old 保存asset
 * @param[in] now 現採取pin
 * @return 全field一致
 */
//------------------------------------------------------------------------------
bool sameAssets( const std::vector<SpeakerTextAsset> &old, const std::vector<SpeakerTextAsset> &now)
{
	// asset数が違う参照を現モデルのcacheと扱わない
	if( old.size() != now.size()) {
		return false ;
	}
	// 4assetの全field一致だけを再利用条件にする
	for( const auto &asset : now) {
		// 同IDでも重みrevision等が違うcacheは使わない
		if( std::none_of( old.begin(), old.end(),
						//------------------------------------------------------------------------------
						/**
						 * @brief 過去の実採取pinを現用途へ照合します
						 * @param[in] previous 一過去asset
						 * @return 全field一致
						 */
						//------------------------------------------------------------------------------
						[&asset]( const auto &previous)
						{
							return previous.id == asset.id && previous.revision == asset.revision &&
								   previous.bytes == asset.bytes && previous.sha256 == asset.sha256 ;
						})) {
			return false ;
		}
	}
	return true ;
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 保存参照を現固定asset metadataとだけ照合します
 * @param[in] value 保存来歴
 * @return native再生成なしで再利用可能ならtrue。モデルfileを開きません
 * @see koefude::speakerTextReferenceCurrent
 */
//------------------------------------------------------------------------------
bool speakerTextReferenceCurrent( const SpeakerTextProjectionRecord &value)
{
	return value.parametersSha == speakerTextParametersSha() && sameAssets( value.assets, assets()) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 同じ耐久sourceからPC内参照と方式3を一度だけ実行します
 * @param[in] request 小token/同global op
 * @param[in,out] persistence 同writer port
 * @param[in,out] producer worker専用の同期native/fake参照
 * @param[in] decoder local有界decode/fake
 * @param[in] stop 協調取消し
 * @param[in] progress 実窓数/実段階の通知。空可
 * @param[in,out] resources 通常factoryの監視control。nullは隔離fake用
 * @return 原APIを変更しない採用ACKまたは前採用/最新失敗
 * @see koefude::runSpeakerTextJob
 */
//------------------------------------------------------------------------------
SpeakerTextJobCompletion runSpeakerTextJob( const SpeakerTextJobRequest	   &request,
										   ISpeakerTextPersistencePort	   &persistence,
										   ITextAlignmentReferenceProducer &producer, const AudioDecoder &decoder,
										   const std::stop_token stop, const ProgressCallback &progress,
										   IPipelineResourceControl *resources)
{
	SpeakerTextJobCompletion result ; // 前採用を失わない終端
	result.persistence.target = request.target ;
	SpeakerTextSnapshot snapshot ; // 同writerが返すimmutable原文
	bool				began = false ; // begin ACK前のモデル/音声アクセスを禁止
	bool				commitAttempted = false ; // 保存失敗の自動再試行をしない
	bool				resourceFinished = false ; // finish後の再probeを禁止する
	const auto			processingStop = resources ? resources->processingStop() : stop ; // 資源停止と元userを区別
	const auto			started = progressMonotonicMs() ; // 実操作の単調開始
	// 通知先へ渡す実窓数と単調時計
	ApiTextProgress		view ;
	view.startedMs = started ; // API音声の時計とは別
	const auto publish =
		//------------------------------------------------------------------------------
		/**
		 * @brief 実完了窓だけを通知します
		 * @param[in] phase 実段階
		 * @param[in] count 実窓数
		 */
		//------------------------------------------------------------------------------
		[&]( const ApiTextPhase phase, const std::size_t count)
		{
			// monitor終了後に新しいprobeを行わない
			if( resources && !resourceFinished) {
				resources->pollProgress() ;
			}
			// 通知先なしでも処理と保存の契約は維持する
			if( !progress) {
				return ;
			}
			view.phase = phase ;
			view.completedUnits = static_cast<std::uint32_t>( count) ;
			view.totalUnits = request.target.completePrefix ;
			view.observedMs = progressMonotonicMs() ;
			// 当境界で生成する所有値
			Progress value{Stage::COMBINING, -1} ;
			value.speakerText = view ;
			progress( value) ;
		} ;
	try {
		checkCancelled( stop) ;
		// 不正op/小target/decoder欠落を音声読取前に拒否する
		if( !request.operationId || request.operationId == UINT64_MAX || !request.target.historyId || !decoder) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
		publish( ApiTextPhase::PREPARING, 0) ;
		snapshot = persistence.loadSpeakerText( request.target) ;
		// writerの元PCM/source snapshotが揃わなければ開始しない
		if( !snapshot.source || !snapshot.audio || snapshot.target != request.target || !snapshot.source->pcmIdentity) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
		result.source = snapshot.source ;
		result.persistence.record = snapshot.record ;
		result.persistence.record.latest =
			SpeakerTextAttempt{request.operationId, request.target, SpeakerTextState::PENDING} ;
		result.persistence = persistence.commitSpeakerText( request.target, result.persistence.record, true) ;
		// global floorとbeginの耐久ACKなしにnativeへ進まない
		if( !result.persistence.committed) {
			throw OperationError( ErrorKind::SAVE_FAILED, result.persistence.code) ;
		}
		began = true ;
		const auto									 currentAssets = assets() ; // 欠落revisionは実推論より前に拒否
		std::shared_ptr<SpeakerTextProjectionRecord> value ; // workerだけが変更する所有採用候補
		// 同source/固定方式/資産一致の参照だけを再利用する
		if( snapshot.record.adopted && snapshot.record.adopted->target == request.target &&
		   snapshot.record.adopted->parametersSha == speakerTextParametersSha() &&
		   sameAssets( snapshot.record.adopted->assets, currentAssets)) {
			validateSpeakerTextRecord( *snapshot.record.adopted, *snapshot.source, *snapshot.audio, true, stop) ;
			value = std::make_shared<SpeakerTextProjectionRecord>( *snapshot.record.adopted) ;
			value->operationId = request.operationId ; // 参照生成opは過去のまま
			result.referenceReused = true ;
		} else {
			value = std::make_shared<SpeakerTextProjectionRecord>() ;
			value->operationId = request.operationId ;
			value->referenceGenerationOperation = request.operationId ;
			value->target = request.target ;
			value->pcm = snapshot.audio->plan.pcm ;
			value->turnsSha = speakerTextTurnsSha( snapshot.source->speakerTurns) ;
			value->parametersSha = speakerTextParametersSha() ;
			value->assets = currentAssets ;
			std::vector<AsrSampleWindow> windows ; // 完全prefixの全窓。空textも再送/欠落しない
			// 空完全応答も含む全prefixを同じproducerへ逐次渡す
			for( std::size_t at = 0; at < request.target.completePrefix; ++at) {
				windows.push_back( {snapshot.audio->plan.windows[at].begin, snapshot.audio->plan.windows[at].end}) ;
			}
			checkCancelled( stop) ;
			publish( ApiTextPhase::AWAITING, 0) ;
			// decode確保の前にavailable/commitの既存guardを確認する
			if( resources) {
				resources->checkpoint( PipelineMemoryBoundary::BEFORE_DECODE) ;
			}
			auto audio = decoder( snapshot.input, processingStop, {}) ; // local通常file検証はproduction decoderの責務
			// decode後のPCM資源を既存境界で確認する
			if( resources) {
				resources->checkpoint( PipelineMemoryBoundary::AFTER_DECODE_BEFORE_IDENTITY) ;
			}
			// 通常Quality CPU参照だけに固定する補助設定
			EngineOptions options ;
			options.modelRoot = request.modelRoot ;
			options.backend = Backend::CPU ;
			options.cpuFallback = false ;
			options.diarization = false ;
			options.asrModel = AsrModel::PARAKEET ;
			options.asrProfile = AsrProfile::QUALITY ;
			options.speechFilter = true ;
			options.autoLanguage = false ;
			std::size_t referenceBytes = 0 ; // 全録音を無制限に蓄積しない
			value->execution = producer.produce(
				audio, value->pcm, windows, snapshot.source->speakerTurns, options, processingStop,
				//------------------------------------------------------------------------------
				/**
				 * @brief 通常nativeの実進捗から資源停止を照合します
				 * @param[in] value producerの実進捗
				 */
				//------------------------------------------------------------------------------
				[&]( const Progress value)
				{
					static_cast<void>( value) ;
					// model/native確保の前にも資源停止を照合する
					if( resources) {
						resources->pollProgress() ;
						resources->checkpoint( PipelineMemoryBoundary::AFTER_IDENTITY_BEFORE_ASR) ;
					}
				},
				//------------------------------------------------------------------------------
				/**
				 * @brief 当窓だけをpure対応へ渡してからproducerへ返します
				 * @param[in] index 元chunk番号
				 * @param[in] reference 同期借用参照
				 */
				//------------------------------------------------------------------------------
				[&]( const std::size_t index, const TextAlignmentReferenceWindow &reference)
				{
					checkCancelled( processingStop) ;
					// 窓の参照確保前に同じ資源monitorを確認する
					if( resources) {
						resources->checkpoint( PipelineMemoryBoundary::AFTER_ASR_BEFORE_UNKNOWN_SNAPSHOT) ;
					}
					// 順序逆転/二重窓/範囲外sinkを採用候補へ含めない
					if( index != value->references.size() || index >= request.target.completePrefix) {
						throw OperationError( ErrorKind::INVALID_INPUT) ;
					}
					// 全参照の中立本文も総量へ数える
					for( const auto &unit : reference.units) {
						// 参照snapshotを固定8MiB以内に保つ
						if( unit.text.size() > K_SPEAKER_TEXT_REFERENCE_BYTES - referenceBytes) {
							throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
						}
						referenceBytes += unit.text.size() ;
					}
					publish( ApiTextPhase::SAVING_RESPONSE, index) ;
					value->windows.push_back(
						projectSpeakerText( index, snapshot.audio->chunks[index].text, reference, processingStop)) ;
					value->references.push_back( reference) ;
					publish( ApiTextPhase::AWAITING, index + 1) ;
				}) ;
			result.nativeContextsThisOperation = value->execution.nativeContextsCreated ;
			value->referenceSha = speakerTextReferenceSha( value->references) ;
		}
		validateSpeakerTextRecord( *value, *snapshot.source, *snapshot.audio, true, stop) ;
		static_cast<void>( encodeSpeakerTextRecord( *value)) ; // 保存総量を採用前に確認
		// native/PCMの所有scope解放後だけmonitorを止め、保存前に資源終端を確定する
		if( resources) {
			resources->checkpoint( PipelineMemoryBoundary::AFTER_SPEAKER_COMBINE) ;
			// 所有解放後に確定した資源停止診断
			const auto failure = resources->finish() ;
			resourceFinished = true ;
			// 資源停止があれば原文/前採用を保つ失敗へ変換する
			if( failure.reason != PipelineResourceStopReason::NONE) {
				throw OperationError( ErrorKind::INPUT_TOO_LARGE, failure.code) ;
			}
		}
		checkCancelled( stop) ;
		publish( ApiTextPhase::ADOPTING, request.target.completePrefix) ;
		// 今回の参照に新しい表示支持があるか
		const bool hasSupport = std::any_of( value->windows.begin(), value->windows.end(),
			//------------------------------------------------------------------------------
			/**
			 * @brief 新しい対応字の有無を判定します
			 * @param[in] window 固定方式の一窓
			 * @return 支持あり
			 */
			//------------------------------------------------------------------------------
			[]( const auto &window)
			{
				return window.projectedScalars != 0 ;
			}) ;
		result.persistence.record.latest->state = hasSupport ? SpeakerTextState::READY : SpeakerTextState::UNCHANGED ;
		// 新しい支持がある場合だけ前採用を置換する
		if( hasSupport) {
			result.persistence.record.adopted = std::move( value) ;
		}
		commitAttempted = true ;
		result.persistence = persistence.commitSpeakerText( request.target, result.persistence.record, false, stop) ;
		// 最終の原子保存ACKなしにReadyを表示しない
		if( !result.persistence.committed) {
			throw OperationError( ErrorKind::SAVE_FAILED, result.persistence.code) ;
		}
		publish( result.persistence.record.latest->state == SpeakerTextState::CANCELLED ? ApiTextPhase::CANCELLED
																					   : ApiTextPhase::COMPLETED,
				request.target.completePrefix) ;
		return result ;
	} catch( const OperationError &error) {
		// 所有解放後に確定した資源停止診断
		const auto failure = resources ? resources->finish() : PipelineResourceFailure{} ;
		resourceFinished = resources != nullptr ;
		// user取消しと資源停止を分離した固定診断
		const auto kind = !stop.stop_requested() && failure.reason != PipelineResourceStopReason::NONE
							  ? ErrorKind::INPUT_TOO_LARGE
							  : error.kind() ;
		// 今回操作の元診断整数値
		const auto code = kind == ErrorKind::INPUT_TOO_LARGE && failure.code ? failure.code : error.code() ;
		result.persistence.record.adopted = snapshot.record.adopted ;
		result.persistence.record.latest = SpeakerTextAttempt{
			request.operationId, request.target,
			kind == ErrorKind::CANCELLED ? SpeakerTextState::CANCELLED : SpeakerTextState::FAILURE, kind, code} ;
	} catch( const std::bad_alloc &) {
		// OOMでも所有モデル/PCM解放後の資源終端を確認する
		if( resources) {
			static_cast<void>( resources->finish()) ;
			resourceFinished = true ;
		}
		result.persistence.record.adopted = snapshot.record.adopted ;
		result.persistence.record.latest = SpeakerTextAttempt{request.operationId, request.target,
															  SpeakerTextState::FAILURE, ErrorKind::RESULT_TOO_LARGE} ;
	} catch( const std::exception &) {
		// 未知例外でもmonitorを終了し、前採用を保持する
		if( resources) {
			static_cast<void>( resources->finish()) ;
			resourceFinished = true ;
		}
		result.persistence.record.adopted = snapshot.record.adopted ;
		result.persistence.record.latest =
			SpeakerTextAttempt{request.operationId, request.target, SpeakerTextState::FAILURE, ErrorKind::INTERNAL} ;
	}
	result.persistence.committed = false ;
	// 推論/decodeの失敗だけを同writerへ残す。失敗した最終保存を自動再試行しない
	if( began && !commitAttempted) {
		try {
			result.persistence = persistence.commitSpeakerText( request.target, result.persistence.record, false, {}) ;
		} catch( const std::exception &) {
			result.persistence.code = ERROR_WRITE_FAULT ;
		}
	}
	publish( result.persistence.record.latest && result.persistence.record.latest->state == SpeakerTextState::CANCELLED
				? ApiTextPhase::CANCELLED
				: ApiTextPhase::FAILURE,
			0) ;
	return result ;
}
} // namespace koefude
