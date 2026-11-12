//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_audio_pipeline.cpp
 * @brief 全file CPU話者とVAD planを一回送信、耐久prefixへ接続します
 * @version 1.0.0
 * @date 2026/11/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の有界初回API経路を追加
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1370の文脈分割と明示途中再開を接続
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
#include "inference/api_audio_pipeline.h"
#include "core/api_text_provenance.h"
#include "core/asr_windows.h"
#include "core/audio_data.h"
#include "inference/local_engines.h"
#include "inference/model_store.h"
#include "inference/primary_pipeline_application.h"
#include "platform/audio_decoder.h"
#include "platform/win_helpers.h"
#include <optional>
#include <algorithm>
#include <chrono>
#include <memory>
#include <set>
#include <utility>
#include <whisper.h>
#include <windows.h>

namespace koefude {
namespace {
//------------------------------------------------------------------------------
/**
 * @brief native VADが出すpath/自由文を参照せず破棄します
 * @param[in] level 未保存のlevel
 * @param[in] message 未参照のnative借用文字列、null可
 * @param[in] userData 未使用context
 * @note 同一job workerのnative初期化前に設定します
 */
//------------------------------------------------------------------------------
void quietAudioVadLog( enum ggml_log_level level, const char *message, void *userData) noexcept
{
	static_cast<void>( level) ;
	static_cast<void>( message) ;
	static_cast<void>( userData) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 既存registryのSilero条件を音声API用の最大600秒へ使います
 * @param[in] audio 全PCM部分読取元
 * @param[in] options 固定設定
 * @param[in] stop 実効取消し
 * @param[in] progress 実sample通知
 * @return 元sampleの検出範囲
 */
//------------------------------------------------------------------------------
std::vector<AsrSampleWindow> detectSpeech( const AudioData &audio, const EngineOptions &options, std::stop_token stop,
										  const ProgressCallback &progress)
{
	// VAD OFFなら全範囲をplan側で有界分割します
	if( !options.speechFilter) {
		return {{0, audio.sampleCount()}} ;
	}
	const auto &definition = asrDefinition( options.asrModel, options.asrProfile) ; // 唯一の条件
	const auto	model = verifyModel( options.modelRoot, sharedModelRoles().voiceActivity, stop) ; // 同期lock
	auto		config = whisper_vad_default_context_params() ; // 固定版のCPU検出
	config.use_gpu = false ;
	config.n_threads = options.threads ;
	const auto path = toUtf8( model.path.native()) ; // local nativeだけへのborrow
	whisper_log_set( quietAudioVadLog, nullptr) ; // 初回API単独実行でもnative path logを抑止
	std::unique_ptr<whisper_vad_context, decltype(&whisper_vad_free)> context(
		whisper_vad_init_from_file_with_params( path.c_str(), config),
		whisper_vad_free) ; // 全fileで一context
	// 初期化失敗を無言で全音声送信へ変更しません
	if( !context) {
		throw OperationError( ErrorKind::INFERENCE_FAILED) ;
	}
	auto params = whisper_vad_default_params() ; // localと同じregistry350/800/500/gain
	params.threshold = static_cast<float>( definition.vadThresholdMilli) / 1000.0F ;
	params.min_speech_duration_ms = 100 ;
	params.min_silence_duration_ms = definition.vadSilenceMs ;
	params.speech_pad_ms = definition.vadPaddingMs ;
	params.samples_overlap = 0 ;
	params.max_speech_duration_s = 600.0F ; // APIの文脈をlocal26秒へ縮めません
	std::vector<AsrSampleWindow> ranges ; // 元sampleの和集合前範囲
	std::uint64_t				 offset = 0 ; // 全長を固定bufferで進む位置
	// 検出copy/contextは関数終了で解放されPOST bufferと同時保持しません
	while( offset < audio.sampleCount()) {
		checkCancelled( stop) ;
		const auto	 samples = readAsrChunk( audio, offset) ; // 最大600秒
		const auto	 prepared = definition.vadGain ? prepareVadAudio( samples) : std::vector<float>{} ; // 検出だけgain
		const float *input = definition.vadGain ? prepared.data() : samples.data() ; // 同期borrow
		std::unique_ptr<whisper_vad_segments, decltype(&whisper_vad_free_segments)> detected(
			whisper_vad_segments_from_samples( context.get(), params, input, static_cast<int>( samples.size())),
			whisper_vad_free_segments) ; // LSTM resetを伴う固定版API
		checkCancelled( stop) ;
		// 正常0件と検出失敗を区別します
		if( !detected) {
			throw OperationError( ErrorKind::INFERENCE_FAILED) ;
		}
		const auto count = whisper_vad_segments_n_segments( detected.get()) ; // 第三者件数
		// 件数をallocation前に検証します
		if( count < 0 || static_cast<std::size_t>( count) > K_MAXIMUM_SEGMENTS - ranges.size()) {
			throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
		}
		// padding/丸めを既存検証へ通し元sampleへ一度戻します
		for( int i = 0; i < count; ++i) {
			const auto w = checkedVadWindow( whisper_vad_segments_get_segment_t0( detected.get(), i),
											whisper_vad_segments_get_segment_t1( detected.get(), i),
											samples.size()) ; // 有界範囲
			ranges.push_back( {offset + w.begin, offset + w.end}) ;
		}
		offset += samples.size() ;
		reportProgress( progress, Stage::COMBINING, -1) ;
	}
	return ranges ;
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 同じ一workerでmetadataと音声providerを使います
 * @param[in] received 明示受付の固定ID/設定
 * @param[in,out] keys 既存共有target
 * @param[in,out] catalog 現キーrevisionの正本
 * @param[in,out] metadata 固定GET
 * @param[in,out] catalogPort 同じ履歴workerのmetadata port
 * @param[in,out] transcriber 一窓一回provider
 * @param[in,out] port 同じ一履歴writer
 * @param[in] stop 協調取消し
 * @param[in] progress 実段階、空可
 * @param[in] injected nullは通常依存、非nullは同期試験用
 * @param[in,out] log 任意の既存有限API sink。失敗は認識/送信へ伝播しません
 * @return 部分原文も保持するtyped終端
 * @throws OperationError plan成立前の失敗
 * @see koefude::runApiAudioJob
 */
//------------------------------------------------------------------------------
JobOutput runApiAudioJob( const AsrJobRequest &received, IApiKeyStore &keys, ApiModelCatalog &catalog,
						 IApiCatalogTransport &metadata, IApiTextPersistencePort &catalogPort,
						 IApiAudioTranscriber &transcriber, IApiAudioPersistencePort &port, std::stop_token stop,
						 const ProgressCallback &progress, ApiAudioPipelineServices *injected,
						 IApiCorrectionEventLog *log)
{
	auto								  request = received ; // 小token/設定だけ、UIでrecordをcopyしません
	std::optional<ApiAudioResumeSnapshot> resume ; // 耐久正本の所有snapshot
	// 現在のASR選択に依存せず、明示行の保存設定で再開します
	if( request.audioResume) {
		resume = port.loadAudioResume( *request.audioResume) ;
		request.input = resume->input ;
		request.options.asrModel = AsrModel::OPEN_AI_AUDIO ;
		request.options.asrProfile = resume->source->asrProfile ;
		request.options.autoLanguage = resume->record.plan.autoLanguage ;
		request.options.diarization = resume->record.plan.diarization ;
		request.options.speechFilter = resume->record.plan.speechFilter ;
	}
	const auto &definition = asrDefinition( request.options.asrModel, request.options.asrProfile) ; // 正本
	// UI承認のないCLI/別engine、キーApply競合の古い受付をHTTP前に拒否します
	if( definition.engine != AsrEngine::OPEN_AI_TRANSCRIPTIONS || definition.modelId ||
	   definition.apiModelId != K_API_AUDIO_MODEL || !request.historyId || !request.operationId ||
	   !request.audioApiSelected || request.apiKeyRevision != catalog.keyRevision) {
		throw OperationError( ErrorKind::INVALID_INPUT, ERROR_REVISION_MISMATCH) ;
	}
	std::shared_ptr<const ApiKeySecret>	 key ; // 処理workerだけが所有する当操作の秘密
	std::optional<ApiAudioPriceSnapshot> price ; // PCM一致後に今回単価を凍結
	SherpaDiarizer						 diarizer ; // API経路はCPUで全file先行
	ApiAudioPipelineServices			 production{decodeInputAudio, diarizer, detectSpeech, {}} ; // local ASRなし
	auto								&services = injected ? *injected : production ; // 同期scopeの依存
	auto						  completion = std::make_shared<ApiAudioJobCompletion>() ; // 本文/終端を保持するholder
	std::optional<ApiAudioRecord> record = resume ? std::optional( resume->record) : std::nullopt ;
	std::shared_ptr<Transcript>	  source =
		  resume ? std::make_shared<Transcript>( *resume->source) : std::make_shared<Transcript>() ; // 不変prefixのcopy
	bool resumeStarted = false ; // 新Pending ACK前は旧record/sourceへ戻す
	// 準備中も完全空応答を含む耐久prefix数だけを表示します
	if( resume) {
		completion->persistence = {resume->record, resume->source, true, 0} ;
	}
	const auto	  started = progressMonotonicMs() ; // 壁時計変更の影響を受けない実経過
	std::uint32_t currentUnit = 0 ; // intent解放後の応答保存中も現在区間を保持
	std::uint32_t durablePrefix = resume ? apiAudioCompletePrefix( resume->record) : 0 ;
	const auto	  report =
		//------------------------------------------------------------------------------
		/**
		 * @brief 実段階と完全保存数を通知します
		 * @param[in] phase 現段階
		 */
		//------------------------------------------------------------------------------
		[&]( ApiTextPhase phase)
		{
			// 通知先がある場合だけ小さい所有DTOを渡します
			if( progress) {
				Progress p{phase == ApiTextPhase::COMPLETED ? Stage::COMPLETED : Stage::TRANSCRIBING, -1} ; // HTTP内%なし
				p.apiAudio = {
					phase,		 durablePrefix, record ? static_cast<std::uint32_t>( record->plan.windows.size()) : 0,
					currentUnit, started,		progressMonotonicMs()} ;
				progress( p) ;
			}
		} ;
	const auto logEvent =
		//------------------------------------------------------------------------------
		/**
		 * @brief 本文を受け取れない型だけを同じ有限sinkへ渡します
		 * @param[in] event 固定種類
		 */
		//------------------------------------------------------------------------------
		[&]( ApiCorrectionEvent event)
		{
			// 任意sink失敗は認識/保存/有料再送へ伝播しません
			if( !log) {
				return ;
			}
			ApiCorrectionEventData value ; // path/名前/本文/自由文を持たないDTO
			value.audio = true ;
			value.event = event ;
			value.history = request.historyId ;
			value.operation = request.operationId ;
			value.provenance.operation = request.operationId ;
			value.provenance.requested = {ApiProvenanceModel::GPT_TRANSCRIBE, 0} ;
			// plan成立後だけ実区間/試行量を追加します
			if( record && (!resume || resumeStarted)) {
				value.audioTerminal = record->terminal ;
				value.audioSamples =
					record->attempts.empty() ? record->attemptedSamples : record->attempts.back().attemptedSamples ;
				value.audioUnits = static_cast<std::uint32_t>( record->plan.windows.size()) ;
				value.saved = completion->persistence.committed ;
				value.chunk = record->intent ? *record->intent + 1 : static_cast<std::uint32_t>( record->chunks.size()) ;
				value.provenance.state = apiAudioActive( record->terminal)				   ? ApiTextState::SENDING
										 : record->terminal == ApiAudioTerminal::CANCELLED ? ApiTextState::CANCELLED
										 : record->terminal == ApiAudioTerminal::FAILURE   ? ApiTextState::FAILURE
																						   : ApiTextState::CANDIDATE_READY ;
				// 実応答未通知を要求IDで補完しません
				if( !record->chunks.empty() &&
				   (!resume || record->chunks.back().index >= record->attempts.back().startIndex)) {
					const auto &chunk = record->chunks.back() ; // 同indexのtyped診断だけ
					value.httpStatus = chunk.http ;
					value.code = chunk.code ;
					value.httpError = chunk.httpError ;
					value.audioBilling = chunk.billing ;
					value.provenance.actual = {
						chunk.modelReport == ApiAudioModelReport::VERIFIED	   ? ApiProvenanceModel::GPT_TRANSCRIBE
						: chunk.modelReport == ApiAudioModelReport::UNVERIFIED ? ApiProvenanceModel::UNVERIFIED
																			   : ApiProvenanceModel::MISSING,
						0} ;
				}
				// 応答未確定のintentは既知usageとは独立の不明課金です
				if( record->intent) {
					value.audioBilling = ApiTextBilling::OUTCOME_UNKNOWN ;
				}
			}
			static_cast<void>( log->append( value)) ;
		} ;
	logEvent( ApiCorrectionEvent::STARTED) ;
	const auto save =
		//------------------------------------------------------------------------------
		/**
		 * @brief 同じrecord/sourceを耐久保存します
		 * @param[in] begin 初回barrier
		 * @return 保存済み
		 */
		//------------------------------------------------------------------------------
		[&]( bool begin)
		{
			record->sourceSha = fingerprintReadingSource( *source) ; // 表示名はhashへ入らない既存契約
			syncApiAudioAttempt( *record) ;
			const auto complete =
				record->terminal == ApiAudioTerminal::COMPLETED || record->terminal == ApiAudioTerminal::NO_SPEECH ||
				record->terminal == ApiAudioTerminal::EMPTY_TRANSCRIPT ; // 完了置換前だけ取消しを受けます
			auto ack =
				begin && resume
					? port.beginAudioResume( *received.audioResume, resume->source, *record, resume->source)
					: port.commitAudio( *record, source, begin, complete ? stop : std::stop_token{}) ; // 同writer ACK
			// ACKが別op/planへ変わっていれば有料処理を止めます
			if( ack.record.historyId != record->historyId || ack.record.operationId != record->operationId ||
			   ack.record.plan.digest != record->plan.digest ||
			   (ack.source != source && !(begin && resume && ack.source == resume->source))) {
				throw OperationError( ErrorKind::SAVE_FAILED, ERROR_INVALID_DATA) ;
			}
			*record = ack.record ;
			// 未保存prefixを率へ含めず、保存失敗でも最後の耐久数を保持します
			if( ack.committed) {
				durablePrefix = apiAudioCompletePrefix( ack.record) ;
			}
			completion->persistence = std::move( ack) ;
			return completion->persistence.committed ;
		} ;
	detail::runPrimaryResourceTask(
		//------------------------------------------------------------------------------
		/**
		 * @brief 同じguardのscope内で全PCM/nativeを解放してから終端保存します
		 * @param[in,out] resources 既存16GB guard
		 */
		//------------------------------------------------------------------------------
		[&]( IPipelineResourceControl &resources)
		{
			const auto			   processing = resources.processingStop() ; // userと資源停止を受けるtoken
			const ProgressCallback guarded =
				//------------------------------------------------------------------------------
				/**
				 * @brief 同期通知で資源監視をpollします
				 * @param[in] value 実段階
				 */
				//------------------------------------------------------------------------------
				[&]( Progress value)
				{
					resources.pollProgress() ;
					if( progress) {
						progress( value) ;
					}
				} ;
			ErrorKind	  failure = ErrorKind::INTERNAL ; // plan成立後の固定終端分類
			std::uint32_t code = 0 ; // 元診断
			bool		  failed = false ; // 取得prefixを保存してから返す失敗
			bool		  persistenceFailed = false ; // 保存失敗後は再POSTしません
			try {
				const auto decodeStarted = std::chrono::steady_clock::now() ; // decode/全PCM同一性の実開始
				resources.checkpoint( PipelineMemoryBoundary::BEFORE_DECODE) ;
				const auto audio = services.decoder( request.input, processing, guarded) ; // 有界一時PCM owner
				resources.checkpoint( PipelineMemoryBoundary::AFTER_DECODE_BEFORE_IDENTITY) ;
				const auto identity = fingerprintDecodedPcm( audio, processing, guarded) ;
				// 改変/別音声/一sample差はPOST0、保存prefixを変更しません
				if( resume && identity != resume->record.plan.pcm) {
					throw OperationError( ErrorKind::INVALID_INPUT, ERROR_REVISION_MISMATCH) ;
				}
				source->pcmIdentity = identity ;
				source->decodeSeconds =
					std::chrono::duration<double>( std::chrono::steady_clock::now() - decodeStarted).count() ;
				source->durationSeconds = static_cast<double>( audio.sampleCount()) / K_SAMPLE_RATE ;
				std::vector<AsrSampleWindow> speech ; // 新規だけがVAD範囲を所有
				// 既存planでは話者/VADを再計算せず、そのまま続きを使います
				if( !resume) {
					source->asrModel = AsrModel::OPEN_AI_AUDIO ;
					source->asrProfile = request.options.asrProfile ;
					source->speechFilter = request.options.speechFilter ;
					source->diarizationRequested = request.options.diarization ;
					source->speakerMethod = request.options.speakerMethod ;
					source->apiAudioWindowTimes = true ;
					// 話者を全fileに一回先行し、API窓ごとの再clusterをしません
					if( request.options.diarization) {
						const auto diarizationStarted = std::chrono::steady_clock::now() ; // model検証を含むCPU処理
						reportProgress( guarded, Stage::DIARIZING, -1) ;
						try {
							source->speakerTurns =
								services.diarizer.diarize( audio, request.options, processing, guarded) ;
						} catch( const OperationError &error) {
							resources.checkpoint( PipelineMemoryBoundary::AFTER_DIARIZER, error.code()) ;
							// user/資源取消しは話者警告へ縮小しません
							if( error.kind() == ErrorKind::CANCELLED || error.kind() == ErrorKind::INPUT_TOO_LARGE) {
								throw ;
							}
							source->diarizationWarning = true ;
							source->diarizationError = error.kind() ;
							source->diarizationCode = error.code() ;
						}
						std::set<int> ids ; // 全fileのglobal cluster集合
						// 本文にない発話も含む全cluster数を保持します
						for( const auto &turn : source->speakerTurns) {
							ids.insert( turn.speaker) ;
						}
						source->speakerCount = static_cast<int>( ids.size()) ;
						source->diarizationSeconds =
							std::chrono::duration<double>( std::chrono::steady_clock::now() - diarizationStarted)
								.count() ;
					}
					resources.checkpoint( PipelineMemoryBoundary::AFTER_DIARIZER) ;
					report( ApiTextPhase::PREPARING) ;
					speech = services.speech( audio, request.options, processing,
											 guarded) ; // VAD copy/contextは返却時解放
					record.emplace() ;
					record->version = 2 ;
					record->historyId = request.historyId ;
					record->operationId = request.operationId ;
					record->keyRevision = request.apiKeyRevision ;
				}
				// 現キー資格と価格はPCM照合後、begin保存前にだけ凍結します
				checkCancelled( processing) ;
				key = keys.acquire() ; // workerだけが共有targetを使う秘密owner
				// キー登録だけで送信せず、明示音声操作の未準備だけを更新します
				if( !apiAudioPriceSnapshot( catalog, key != nullptr)) {
					ApiCatalogJobRequest refresh ; // 本文/pathなしの明示操作内metadata
					refresh.keyRevision = request.apiKeyRevision ;
					refresh.prices = true ;
					static_cast<void>( runApiCatalogJob( refresh, catalog, keys, metadata, catalogPort, processing)) ;
				}
				price = apiAudioPriceSnapshot( catalog, key != nullptr) ; // 現revisionを再照合
				// 未掲載/期限切れ/認証不可で有料POSTを始めません
				if( !price || request.apiKeyRevision != catalog.keyRevision) {
					throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_NOT_READY) ;
				}
				// 再開は旧診断/不明課金をattemptへ移してから新opへ進めます
				if( resume) {
					*record = prepareApiAudioResume( resume->record, *resume->source, request.operationId,
													request.apiKeyRevision, *price) ;
				} else {
					record->plan = makeApiAudioPlan( audio, speech, source->speakerTurns, request.options, *price,
													*source->pcmIdentity, processing) ;
					ApiAudioAttempt first ; // 初回の操作価格と本文なしsource identity
					first.operationId = request.operationId ;
					first.keyRevision = request.apiKeyRevision ;
					first.price = *price ;
					first.parentSourceSha = fingerprintReadingSource( *source) ;
					record->attempts.push_back( std::move( first)) ;
				}
				// 全planとPendingが耐久保存される前にPOSTしません
				if( !save( true)) {
					persistenceFailed = true ;
					throw OperationError( ErrorKind::SAVE_FAILED, completion->persistence.code) ;
				}
				resumeStarted = resume.has_value() ; // 新leaseはbegin ACK後だけ有効
				std::size_t textBytes = 0 ; // 過去本文も含む8MiB予算
				for( const auto &c : record->chunks) {
					textBytes += c.text.size() ;
				}
				// 一窓のintent/応答prefix ACK後だけ次へ進みます
				for( std::size_t i = apiAudioCompletePrefix( *record); i < record->plan.windows.size(); ++i) {
					resources.checkpoint( PipelineMemoryBoundary::AFTER_IDENTITY_BEFORE_ASR) ;
					const auto w = record->plan.windows[i] ; // ACKでrecordを置換しても有効な所有copy
					currentUnit = static_cast<std::uint32_t>( i + 1) ;
					record->terminal = ApiAudioTerminal::SENDING ;
					record->intent = static_cast<std::uint32_t>( i) ;
					report( ApiTextPhase::RESERVING) ;
					// intent保存失敗はPOST0です
					if( !save( false)) {
						persistenceFailed = true ;
						throw OperationError( ErrorKind::SAVE_FAILED, completion->persistence.code) ;
					}
					logEvent( ApiCorrectionEvent::STARTED) ; // 耐久intentと次送信の固定事実
					checkCancelled( processing) ;
					std::vector<float> samples( static_cast<std::size_t>( w.end - w.begin)) ; // 最大600秒
					audio.readSamples( w.begin, samples) ;
					report( ApiTextPhase::AWAITING) ;
					auto chunk = transcriber.transcribe( samples, request.options.autoLanguage, *key,
														processing) ; // 有料再送なし
					chunk.index = static_cast<std::uint32_t>( i) ;
					// 実送信試行量をusageや成功数から推測しません
					if( chunk.billing != ApiTextBilling::NOT_SENT) {
						auto &attempted = record->attempts.back().attemptedSamples ; // 今回操作だけ
						if( attempted > UINT64_MAX - (w.end - w.begin)) {
							throw OperationError( ErrorKind::RESULT_TOO_LARGE, ERROR_ARITHMETIC_OVERFLOW) ;
						}
						attempted += w.end - w.begin ;
					}
					record->intent.reset() ;
					// 全本文上限を超えた応答は本文を採用せず既知usageを保持します
					if( chunk.text.size() > K_MAXIMUM_TEXT_BYTES - textBytes) {
						chunk.text.clear() ;
						chunk.complete = false ;
						chunk.code = ERROR_FILE_TOO_LARGE ;
					}
					textBytes += chunk.text.size() ;
					record->chunks.push_back( std::move( chunk)) ;
					const auto &result = record->chunks.back() ; // 完全応答か固定失敗
					// APIが提供しないtoken時刻は付けず送信区間へ一本文だけ置きます
					if( result.complete && !result.text.empty()) {
						source = std::make_shared<Transcript>( *source) ; // 旧prefixはimmutable
						source->lines = assembleApiAudioTranscript( *record, source->speakerTurns) ;
					}
					// 非2xx/通信/parse失敗では後続窓を送信しません
					if( !result.complete) {
						throw OperationError( processing.stop_requested() ? ErrorKind::CANCELLED
																		 : ErrorKind::INFERENCE_FAILED,
											 result.code) ;
					}
					report( ApiTextPhase::SAVING_RESPONSE) ;
					// prefix保存ACKが失敗した時点で後続POSTを止めます
					if( !save( false)) {
						persistenceFailed = true ;
						throw OperationError( ErrorKind::SAVE_FAILED, completion->persistence.code) ;
					}
					logEvent( ApiCorrectionEvent::TERMINAL) ; // 区間保存ACK、要求全体の成功ではありません
					checkCancelled( processing) ;
				}
			} catch( const OperationError &error) {
				failed = true ;
				failure = error.kind() ;
				code = error.code() ;
			} catch( const std::bad_alloc &) {
				failed = true ;
				failure = ErrorKind::INPUT_TOO_LARGE ;
				code = ERROR_NOT_ENOUGH_MEMORY ;
			} catch( ...) {
				failed = true ;
				failure = ErrorKind::INTERNAL ;
				code = ERROR_INTERNAL_ERROR ;
			}
			const auto resourceFailure = resources.finish() ; // PCM/native/copy解放後の最終評価
			// user取消しと資源停止を完全成功へ変換しません
			if( stop.stop_requested()) {
				failed = true ;
				failure = ErrorKind::CANCELLED ;
			} else if( resourceFailure.reason != PipelineResourceStopReason::NONE) {
				failed = true ;
				failure = ErrorKind::INPUT_TOO_LARGE ;
				code = resourceFailure.code ;
			}
			// 準備失敗は旧本文/recordを保持し、今回診断へ旧HTTPを流用しません
			if( resume && !resumeStarted) {
				completion->persistence = {resume->record, resume->source, true, 0} ;
				completion->resumePreparationFailed = true ;
				completion->preparationError = failure ;
				completion->preparationCode = code ;
				completion->succeeded = false ;
				report( failure == ErrorKind::CANCELLED ? ApiTextPhase::CANCELLED : ApiTextPhase::FAILURE) ;
				return ;
			}
			// plan成立前の失敗は通常の音声失敗として返します
			if( !record || record->plan.pcm.sampleCount == 0) {
				throw OperationError( failure, code) ;
			}
			source = std::make_shared<Transcript>( *source) ; // 以前の耐久prefixのownerを変更しません
			source->totalSeconds = static_cast<double>( progressMonotonicMs() - started) / 1000 ;
			source->transcriptionSeconds =
				std::max( 0.0, source->totalSeconds - source->decodeSeconds - source->diarizationSeconds) ;
			record->error = failure ;
			record->code = code ;
			record->terminal =
				failed ? (failure == ErrorKind::CANCELLED ? ApiAudioTerminal::CANCELLED : ApiAudioTerminal::FAILURE)
				: record->plan.windows.empty() ? ApiAudioTerminal::NO_SPEECH
				: source->lines.empty()		   ? ApiAudioTerminal::EMPTY_TRANSCRIPT
											   : ApiAudioTerminal::COMPLETED ;
			report( ApiTextPhase::ADOPTING) ;
			// 保存失敗時もmemory prefixをtyped終端へ保持し、有料処理は再開しません
			if( persistenceFailed) {
				record->saveFailed = true ;
				completion->persistence = {*record, source, false, code} ;
			} else {
				static_cast<void>( save( false)) ;
			}
			completion->succeeded =
				!failed && completion->persistence.committed &&
				(record->terminal == ApiAudioTerminal::COMPLETED || record->terminal == ApiAudioTerminal::NO_SPEECH ||
				 record->terminal == ApiAudioTerminal::EMPTY_TRANSCRIPT) ;
			logEvent( ApiCorrectionEvent::TERMINAL) ;
			report( completion->succeeded							 ? ApiTextPhase::COMPLETED
				   : record->terminal == ApiAudioTerminal::CANCELLED ? ApiTextPhase::CANCELLED
																	 : ApiTextPhase::FAILURE) ;
		},
		stop, services.resourceProbe) ;
	JobOutput output( completion->persistence.source) ; // 完全/部分とも同一owner
	output.apiAudio = std::move( completion) ;
	return output ;
}
} // namespace koefude
