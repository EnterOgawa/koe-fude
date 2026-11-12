//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file parakeet_engine.cpp
 * @brief 日本語CTCを有界なCUDA/CPU処理と同じweightの再試行で実行します
 * @version 1.0.0
 * @date 2026/09/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/10 独立評価で選定したParakeet CTCと元時刻のtokenを製品へ接続
 * 0.1.0 Shunji Ogawa 2026/09/10 発火を発話全体の交代へ対応付け、日本語の単語途中を保護
 * 0.1.0 Shunji Ogawa 2026/09/11 CUDA予算、外部FP32 weightのlock、解放後のCPU再試行を追加
 * 0.1.0 Shunji Ogawa 2026/09/14 VAD末尾の二段階丸めを比較器と共通の検証へ接続
 * 0.1.0 Shunji Ogawa 2026/10/09 実窓exportとraw行/tokenの同期観測を追加
 * 0.1.0 Shunji Ogawa 2026/10/09 実native内で原行全文の観測用途をtraceへ記録
 * 0.1.0 Shunji Ogawa 2026/10/19 宣言の役割を明記
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372で通常CTC一contextの有界窓参照を追加
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
#include "inference/local_engines.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Windows.h>

#include <sherpa-onnx/c-api/c-api.h>
#include <whisper.h>

#include "core/asr_tokens.h"
#include "core/asr_windows.h"
#include "core/audio_data.h"
#include "core/shared_asr_windows.h"
#include "inference/asr_retry.h"
#include "inference/cuda_device.h"
#include "inference/model_store.h"
#include "platform/win_helpers.h"
#include "platform/word_boundaries.h"

namespace koefude {
namespace {

constexpr std::size_t			 K_UTTERANCE_SAMPLES = 26 * K_SAMPLE_RATE ; // 比較・固定済みの認識入力上限

//------------------------------------------------------------------------------
/**
 * @brief 参照窓の原PCMを最大120秒だけ読みます
 * @param[in] audio 全PCM所有元
 * @param[in] window 検査済み元sample範囲
 * @return 元bitを保持する有限sample列
 * @throws OperationError 不正sample/I/O
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<float> readReferencePcm( const AudioData &audio, const AsrSampleWindow window)
{
	std::vector<float> samples( static_cast<std::size_t>( window.end - window.begin)) ; // 有界PCM
	audio.readSamples( window.begin, samples) ;
	// decoder後のPCMもnative API境界で有限性と正規化範囲を再確認する
	if( std::any_of( samples.begin(), samples.end(),
				   //------------------------------------------------------------------------------
				   /**
				    * @brief 外部PCMの不正値を検査します
				    * @param[in] sample 正規化sample
				    * @return 不正値ならtrue
				    */
				   //------------------------------------------------------------------------------
				   []( const float sample)
				   {
					   return !std::isfinite( sample) || std::abs( sample) > 1.0F ;
				   })) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	return samples ;
}

//------------------------------------------------------------------------------
/**
 * @brief VADの本文・pathを含み得る第三者logを抑止します
 * @param[in] level engineのlog level。保存しません
 * @param[in] message engine所有の文字列。内容を参照しません
 * @param[in] userData 未使用のcallback context
 */
//------------------------------------------------------------------------------
void quietVadLog( enum ggml_log_level level, const char *message, void *userData) noexcept
{
	static_cast<void>( level) ;
	static_cast<void>( message) ;
	static_cast<void>( userData) ;
}

//------------------------------------------------------------------------------
/**
 * @brief C API所有のNUL終端本文を上限付きの借用viewへ変換します
 * @param[in] value 結果objectが所有するUTF-8。NULL不可
 * @return 結果objectの寿命中だけ有効な借用view
 * @throws OperationError NULLまたは本文上限超過
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string_view boundedText( const char *value)
{
	// NULLを無音の空文字として扱わない
	if( !value) {
		throw OperationError( ErrorKind::INFERENCE_FAILED) ;
	}
	const auto length = strnlen_s( value, K_MAXIMUM_TEXT_BYTES + 1) ; // 上限までの文字列長（byte）
	// 上限超過の第三者結果を所有copyへ移さない
	if( length > K_MAXIMUM_TEXT_BYTES) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
	return {value, length} ;
}

//------------------------------------------------------------------------------
/**
 * @brief 元PCMを変えず、比較済みのVAD条件から発話入力を作ります
 * @param[in] samples 有限な正規化PCM。最大10分、呼出し中のみ借用
 * @param[in,out] context worker所有のCPU VAD。NULLなら全音声を有界に分けます
 * @param[in] definition registryに固定した検出条件
 * @param[in] stop 同期検出の前後で確認する取消し
 * @return 元PCMを参照する最大26秒の非重複範囲
 * @throws OperationError 取消し、推論異常、不正時刻
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<AsrWindow> utteranceWindows( const std::span<const float> samples,
													  whisper_vad_context *context, const AsrDefinition &definition,
													  const std::stop_token stop)
{
	checkCancelled( stop) ;
	// 利用者が発話検出を無効化した場合は音声を除外せず、入力上限だけを守る
	if( !context) {
		const std::array<AsrWindow, 1> whole{{{0, samples.size()}}} ; // 今回の全PCM範囲
		return splitAsrWindows( samples, whole, K_UTTERANCE_SAMPLES) ;
	}
	auto params = whisper_vad_default_params() ; // 固定engine版の検出設定
	params.threshold = static_cast<float>( definition.vadThresholdMilli) / 1000.0F ;
	params.min_speech_duration_ms = 100 ;
	params.min_silence_duration_ms = definition.vadSilenceMs ;
	params.speech_pad_ms = definition.vadPaddingMs ;
	params.samples_overlap = 0.0F ;
	params.max_speech_duration_s = 26.0F ;
	const auto	 prepared = definition.vadGain ? prepareVadAudio( samples) : std::vector<float>{} ; // VAD専用の所有copy
	const float *input = definition.vadGain ? prepared.data() : samples.data() ; // 同期借用
	checkCancelled( stop) ;
	const std::unique_ptr<whisper_vad_segments, decltype(&whisper_vad_free_segments)> detected(
		whisper_vad_segments_from_samples( context, params, input, static_cast<int>( samples.size())),
		whisper_vad_free_segments) ; // 呼出しごとにLSTMをresetするAPIの所有結果
	checkCancelled( stop) ;
	// 検出失敗と正常な0発話を区別する
	if( !detected) {
		throw OperationError( ErrorKind::INFERENCE_FAILED) ;
	}
	const int count = whisper_vad_segments_n_segments( detected.get()) ; // 第三者の区間数
	// allocation前に区間数の上限を確認する
	if( count < 0 || count > static_cast<int>( K_MAXIMUM_SEGMENTS)) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
	std::vector<AsrWindow> speech ; // centisecondから元sampleへ変換する所有範囲
	speech.reserve( static_cast<std::size_t>( count)) ;
	// 固定VADの窓切上げとcs丸めを共通検証し、元PCMのsample範囲へ戻す
	for( int index = 0; index < count; ++index) { // 現在の検出区間番号
		const double start = whisper_vad_segments_get_segment_t0( detected.get(), index) ; // 開始cs
		const double end = whisper_vad_segments_get_segment_t1( detected.get(), index) ; // 終了cs
		speech.push_back( checkedVadWindow( start, end, samples.size())) ;
	}
	return splitAsrWindows( samples, speech, K_UTTERANCE_SAMPLES) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 固定モデルを一度だけ読み、有界な発話入力を末尾まで認識します
 * @param[in] audio worker所有PCMの借用元
 * @param[in] options ジョブ受付時の固定条件
 * @param[in] definition 選択したCTCのregistry情報
 * @param[in] model 推論終了までlockする検証済みweight
 * @param[in] tokens 推論終了までlockする検証済みtoken file
 * @param[in] vad 検出用の検証済みweight。NULLならVAD無効
 * @param[in] useGpu 同一adapterの予算とCUDA providerの事前照合を通った場合true
 * @param[in] stop 同期native処理の前後で確認する取消し
 * @param[in] progress worker上で呼ぶ進捗通知。空を許可
 * @param[in] observer raw原行/実窓を借用通知するworker callback。空を許可
 * @param[in] referenceWindows 空は通常全録音、それ以外は参照用最大120秒窓
 * @param[in] windowSink 参照窓の同期borrow。通常処理は空
 * @return 元音声の時刻と対応tokenを持つ所有本文
 * @throws OperationError 初期化・認識・外部結果の失敗、取消し
 */
//------------------------------------------------------------------------------
[[nodiscard]] Recognition runCtc( const AudioData &audio, const EngineOptions &options, const AsrDefinition &definition,
								 const VerifiedModel &model, const VerifiedModel &tokens, const VerifiedModel *vad,
								 const bool useGpu, const std::stop_token stop, const ProgressCallback &progress,
								 const RecognitionObserver			   &observer,
								 const std::span<const AsrSampleWindow> referenceWindows = {},
								 const CtcReferenceWindowSink		   &windowSink = {})
{
	reportProgress( progress, Stage::LOADING_MODEL, -1) ;
	const auto						  modelPath = toUtf8( model.path.native()) ; // 同期初期化中に有効なUTF-8 path
	const auto						  tokensPath = toUtf8( tokens.path.native()) ; // 同じrevisionの文字定義
	SherpaOnnxOfflineRecognizerConfig config{} ; // 未使用の別モデルを設定しないゼロ初期化
	config.feat_config.sample_rate = K_SAMPLE_RATE ;
	config.feat_config.feature_dim = 80 ;
	config.model_config.nemo_ctc.model = modelPath.c_str() ;
	config.model_config.tokens = tokensPath.c_str() ;
	config.model_config.num_threads = options.threads ;
	config.model_config.provider = useGpu ? "cuda" : "cpu" ;
	config.model_config.debug = false ;
	config.decoding_method = "greedy_search" ;
	checkCancelled( stop) ;
	std::unique_ptr<const SherpaOnnxOfflineRecognizer, decltype(&SherpaOnnxDestroyOfflineRecognizer)> engine(
		SherpaOnnxCreateOfflineRecognizer( &config),
		SherpaOnnxDestroyOfflineRecognizer) ; // 同期推論中だけcontextを所有
	checkCancelled( stop) ;
	// 初期化失敗を空の認識結果へ変えない
	if( !engine) {
		throw OperationError( ErrorKind::INFERENCE_FAILED) ;
	}
	std::unique_ptr<whisper_vad_context, decltype(&whisper_vad_free)> vadContext( nullptr,
																				 whisper_vad_free) ; // 検出context
	// 選択された場合だけ発話検出を初期化し、全録音の各chunkでweightを再利用する
	if( vad) {
		auto vadOptions = whisper_vad_default_context_params() ; // CPUに限定した検出設定
		vadOptions.n_threads = options.threads ;
		vadOptions.use_gpu = false ;
		const auto vadPath = toUtf8( vad->path.native()) ; // 同期初期化中の借用元
		vadContext.reset( whisper_vad_init_from_file_with_params( vadPath.c_str(), vadOptions)) ;
		// 検出modelの失敗時に無言で検出なしへ切り替えない
		if( !vadContext) {
			throw OperationError( ErrorKind::INFERENCE_FAILED) ;
		}
	}
	Recognition recognition ; // この一回の試行だけの所有結果
	recognition.execution.nativeContextsCreated = 1 ;
	recognition.execution.vadContextsCreated = vadContext ? 1U : 0U ;
	recognition.execution.backend = useGpu ? Backend::CUDA : Backend::CPU ;
	recognition.execution.family = options.asrModel ;
	recognition.execution.profile = options.asrProfile ;
	recognition.execution.provider = config.model_config.provider ;
	recognition.execution.modelId = modelDefinition( definition.modelId.value()).id ;
	recognition.execution.threads = config.model_config.num_threads ;
	recognition.execution.speechFilter = vad != nullptr ;
	recognition.execution.language = "Japanese" ;
	recognition.execution.decodingMethod = config.decoding_method ;
	recognition.execution.featureDim = config.feat_config.feature_dim ;
	recognition.execution.frameMs = definition.frameMs ;
	recognition.execution.tokenTimestamps = true ;
	recognition.execution.observedWholeText = static_cast<bool>( observer) ;
	recognition.execution.externalWindows = static_cast<bool>( windowSink) ;
	recognition.execution.verifiedModelIds = {std::string( modelDefinition( definition.modelId.value()).id),
											  std::string( modelDefinition( *definition.tokensModel).id)} ;
	// 外部weightを実際に検証した構成だけをtraceへ加えます
	if( definition.externalDataModel) {
		recognition.execution.verifiedModelIds.emplace_back( modelDefinition( *definition.externalDataModel).id) ;
	}
	// 実際に選択・検証したVADを記録し、synthetic属性から合格を作りません
	if( vad) {
		recognition.execution.verifiedModelIds.emplace_back( modelDefinition( ModelId::VOICE_ACTIVITY).id) ;
	}
	std::uint64_t offset = 0 ; // 次の元PCMを読み込むsample位置
	std::size_t	  textBytes = 0 ; // 全録音で保持するUTF-8容量
	std::size_t	  tokenCount = 0 ; // 全録音で保持する任意token数
	std::size_t	  rawTokenCount = 0 ; // 観測sidecarのraw token数。整列失敗でも加算します
	std::size_t	  rawTokenBytes = 0 ; // raw token本文copyの合計byte境界
	std::size_t	  referenceIndex = 0 ; // 同じCPU contextへ渡す外部窓番号
	reportProgress( progress, Stage::TRANSCRIBING, 0) ;
	// 一度に最大10分のPCMを読み、実際に処理したsample分だけ末尾へ進む
	while( windowSink ? referenceIndex < referenceWindows.size() : offset < audio.sampleCount()) {
		checkCancelled( stop) ;
		// 外部窓の元sample原点を一度だけ設定し、tokenの時刻補正へ共用する
		if( windowSink) {
			offset = referenceWindows[referenceIndex].begin ;
		}
		const auto chunk = windowSink ? readReferencePcm( audio, referenceWindows[referenceIndex])
									  : readAsrChunk( audio, offset) ; // 参照時は最大120秒
		const auto windows = utteranceWindows( chunk, vadContext.get(), definition, stop) ; // 当該PCMの発話範囲
		// 新export vectorの件数とbyteを追加前に有界にします
		if( observer && windows.size() > K_MAXIMUM_SEGMENTS - recognition.inputWindows.size()) {
			throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
		}
		// model contextは共有し、各発話のstreamだけをその終了時に解放する
		for( const auto &window : windows) { // 正解情報を使わずに検出した元音声範囲
			checkCancelled( stop) ;
			const std::unique_ptr<const SherpaOnnxOfflineStream, decltype(&SherpaOnnxDestroyOfflineStream)> stream(
				SherpaOnnxCreateOfflineStream( engine.get()), SherpaOnnxDestroyOfflineStream) ;
			// この発話だけの入力と認識状態を所有
			// stream不足は無音と区別して失敗させる
			if( !stream) {
				throw OperationError( ErrorKind::INFERENCE_FAILED) ;
			}
			SherpaOnnxOfflineStreamSetOption( stream.get(), "language", "Japanese") ;
			SherpaOnnxAcceptWaveformOffline( stream.get(), K_SAMPLE_RATE, chunk.data() + window.begin,
											static_cast<int>( window.end - window.begin)) ;
			// 実AcceptWaveform後の元窓を明示観測時だけ有界追加します。通常成功条件は維持します
			captureAsrWindow( recognition, {offset + window.begin, offset + window.end}, audio.sampleCount(), observer) ;
			checkCancelled( stop) ;
			SherpaOnnxDecodeOfflineStream( engine.get(), stream.get()) ; // batch 1の同期認識
			checkCancelled( stop) ;
			const std::unique_ptr<const SherpaOnnxOfflineRecognizerResult,
								  decltype(&SherpaOnnxDestroyOfflineRecognizerResult)>
				result( SherpaOnnxGetOfflineStreamResult( stream.get()),
					   SherpaOnnxDestroyOfflineRecognizerResult) ; // streamからcopyした所有結果
			// 不正なobjectを空認識として利用しない
			if( !result) {
				throw OperationError( ErrorKind::INFERENCE_FAILED) ;
			}
			const auto text = boundedText( result->text) ; // 結果object内だけで有効なUTF-8
			// 本文とtokenの集計上限をcopy前に確認する
			if( text.size() > K_MAXIMUM_TEXT_BYTES - textBytes || result->count < 0 ||
			   static_cast<std::size_t>( result->count) > K_MAXIMUM_ASR_TOKENS - tokenCount ||
			   static_cast<std::size_t>( result->count) > K_MAXIMUM_ASR_TOKENS - rawTokenCount ||
			   recognition.segments.size() >= K_MAXIMUM_SEGMENTS) {
				throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
			}
			// 正常な空出力は追加せず、正常/失敗の判定はnative APIの結果契約で分ける
			if( !text.empty()) {
				static_cast<void>( toWide( text)) ; // 外部UTF-8をUIへ渡す前に検証
				TextSegment segment{static_cast<std::int64_t>( (offset + window.begin) / 16),
									static_cast<std::int64_t>( (offset + window.end) / 16),
									std::string( text),
									{}} ; // 元音声時刻へ戻した所有本文
				// token textの欠落/容量失敗より前に原本文を観測へ保存します
				if( observer) {
					recognition.rawRows.push_back( {std::string( text),
												   0.0,
												   static_cast<double>( window.end - window.begin) / 16.0,
												   {offset + window.begin, offset + window.end},
												   {}}) ;
					observer( recognition) ;
				}
				// CTCが返すべきtoken情報を、NULLや不足で静かに成功させない
				if( result->count <= 0 || !result->tokens_arr || !result->timestamps) {
					throw OperationError( ErrorKind::INFERENCE_FAILED) ;
				}
				std::vector<std::string_view> pieces ; // 結果objectの寿命内だけの借用token
				pieces.reserve( static_cast<std::size_t>( result->count)) ;
				// C APIのtokenを有界なviewにして、本文との完全対応を別関数で検証する
				for( int index = 0; index < result->count; ++index) { // 現在のtoken番号
					try {
						pieces.push_back( boundedText( result->tokens_arr[index])) ;
					} catch( ...) {
						// 当該本文と既に取得した原tokenを失わない通知を行って停止します
						if( observer) {
							observer( recognition) ;
						}
						throw ;
					}
					// 研究観測でだけraw tokenをcopyし、製品の追加RAMを抑えます
					if( observer) {
						// raw token copyの合計byteも整列結果に依存せず検査します
						if( pieces.back().size() > K_MAXIMUM_TEXT_BYTES - rawTokenBytes) {
							observer( recognition) ; // 取得済みの原行/tokenだけを保持します
							throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
						}
						rawTokenBytes += pieces.back().size() ;
						recognition.rawRows.back().tokens.push_back(
							{std::string( pieces.back()), static_cast<double>( result->timestamps[index]),
							 static_cast<double>( result->timestamps[index]), "seconds", false, true}) ;
					}
				}
				// token整列の成否に関わらず、原本文とraw時刻を失わない観測を先に行います
				if( observer) {
					rawTokenCount += recognition.rawRows.back().tokens.size() ;
					observer( recognition) ;
				}
				static_cast<void>( bindCtcTokens( segment, pieces,
												std::span( result->timestamps, static_cast<std::size_t>( result->count)),
												definition.frameMs)) ; // 対応しない本文は全区間として保持する
				static_cast<void>( markJapaneseWordBoundaries( segment)) ;
				// OS規則が使えない場合は分割許可なしの区間全体を保持する
				textBytes += segment.text.size() ;
				tokenCount += segment.tokens.size() ;
				recognition.segments.push_back( std::move( segment)) ;
				// 正当な製品行も次の外部呼出し前に有限DTOとして観測できます
				if( observer) {
					observer( recognition) ;
				}
			}
			reportProgress( progress, Stage::TRANSCRIBING,
						   windowSink ? -1 : static_cast<int>( (offset + window.end) * 100 / audio.sampleCount())) ;
		}
		offset += chunk.size() ; // VADで処理済みと判断した無音も含め、読み込み範囲だけ進む
		// tokenがassemble_transcriptで失われる前に同期借用し、当窓の所有結果を回収する
		if( windowSink) {
			windowSink( referenceIndex, referenceWindows[referenceIndex], recognition) ;
			recognition.segments.clear() ;
			textBytes = 0 ;
			tokenCount = 0 ;
			++referenceIndex ;
		}
		reportProgress( progress, Stage::TRANSCRIBING,
					   windowSink ? static_cast<int>( referenceIndex * 100 / referenceWindows.size())
								  : static_cast<int>( offset * 100 / audio.sampleCount())) ;
	}
	checkCancelled( stop) ;
	// 成功traceを返す前にnative所有物を解放し、参照用途では一contextの生存境界を記録する
	vadContext.reset() ;
	recognition.execution.vadContextsReleased = recognition.execution.vadContextsCreated ;
	engine.reset() ;
	recognition.execution.nativeContextsReleased = recognition.execution.nativeContextsCreated ;
	reportProgress( progress, Stage::TRANSCRIBING, 100) ;
	return recognition ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 正規化PCMを原言語の時刻付きtextへ変換します
 * @param[in] audio 呼出し中有効な16kHz mono PCM
 * @param[in] options ジョブ開始時に固定した設定
 * @param[in] stop 協調的取消し
 * @param[in] progress worker上の通知先。空を許可
 * @return engine contextを含まない所有結果
 * @throws OperationError 入力不正、推論失敗、取消し
 * @note blocking処理。同一instanceへの並行呼出しは禁止
 */
//------------------------------------------------------------------------------
Recognition LocalAsrEngine::transcribe( const AudioData &audio, const EngineOptions &options, const std::stop_token stop,
									   const ProgressCallback &progress)
{
	return transcribeObserved( audio, options, stop, progress, {}) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 実primaryの元窓と原行を同期観測し、registryから認識します
 * @param[in] audio 呼出し中有効な16kHz mono原PCM。全sample範囲を使います
 * @param[in] options 固定されたregistry/model/profile設定
 * @param[in] stop 協調取消し
 * @param[in] progress worker通知。空可
 * @param[in] observer 同期借用通知。空可。通知値やengine pointerを借用のまま保持しません
 * @return 全contextを解放済みの所有結果
 * @throws OperationError 失敗/取消し。最後に通知した正当な部分結果は呼出し側が保持できます
 * @note observerが空なら通常認識として窓export/raw sidecarの観測を要求しません
 * @see koefude::LocalAsrEngine::transcribeObserved
 */
//------------------------------------------------------------------------------
Recognition LocalAsrEngine::transcribeObserved( const AudioData &audio, const EngineOptions &options,
											   const std::stop_token stop, const ProgressCallback &progress,
											   const RecognitionObserver &observer)
{
	checkCancelled( stop) ;
	// UIからengineを直接選ばず、検証済みregistryが指定する一つの実装へ振り分ける
	switch( asrDefinition( options.asrModel, options.asrProfile).engine) {
		case AsrEngine::WHISPER_CPP: { // 既存のWhisper/Kotoba構成
			WhisperEngine engine ; // 同期呼出し内の所有adapter
			// 観測付きprimaryはParakeetだけに限定し、独立Whisper実行を共有窓と呼びません
			if( observer) {
				throw OperationError( ErrorKind::INVALID_INPUT) ;
			}
			return engine.transcribe( audio, options, stop, progress) ;
		}
		case AsrEngine::OPEN_AI_TRANSCRIPTIONS: // APIは専用worker経路以外から実行しません
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		case AsrEngine::PARAKEET_CTC: { // 選定済みの日本語CTC構成
			ParakeetEngine engine ; // 同期呼出し内の所有adapter
			return engine.transcribeObserved( audio, options, stop, progress, observer) ;
		}
	}
	throw OperationError( ErrorKind::INVALID_INPUT) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 正規化PCMを原言語の時刻付きtextへ変換します
 * @param[in] audio 呼出し中有効な16kHz mono PCM
 * @param[in] options ジョブ開始時に固定した設定
 * @param[in] stop 協調的取消し
 * @param[in] progress worker上の通知先。空を許可
 * @return engine contextを含まない所有結果
 * @throws OperationError 入力不正、推論失敗、取消し
 * @note blocking処理。同一instanceへの並行呼出しは禁止
 */
//------------------------------------------------------------------------------
Recognition ParakeetEngine::transcribe( const AudioData &audio, const EngineOptions &options, const std::stop_token stop,
									   const ProgressCallback &progress)
{
	return transcribeObserved( audio, options, stop, progress, {}) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 実primaryの元窓と原行を同期観測し、registryから認識します
 * @param[in] audio 呼出し中有効な16kHz mono原PCM。全sample範囲を使います
 * @param[in] options 固定されたregistry/model/profile設定
 * @param[in] stop 協調取消し
 * @param[in] progress worker通知。空可
 * @param[in] observer 同期借用通知。空可。通知値やengine pointerを借用のまま保持しません
 * @return 全contextを解放済みの所有結果
 * @throws OperationError 失敗/取消し。最後に通知した正当な部分結果は呼出し側が保持できます
 * @note observerが空なら通常認識として窓export/raw sidecarの観測を要求しません
 * @see koefude::ParakeetEngine::transcribeObserved
 */
//------------------------------------------------------------------------------
Recognition ParakeetEngine::transcribeObserved( const AudioData &audio, const EngineOptions &options,
											   const std::stop_token stop, const ProgressCallback &progress,
											   const RecognitionObserver &observer)
{
	checkCancelled( stop) ;
	// native APIへ渡せる音声表現とthread数、装置の明示値を検証する
	if( audio.sampleCount() == 0 || audio.sampleCount() > K_MAXIMUM_TIMELINE_SAMPLES || options.threads < 1 ||
	   options.threads > 16 ||
	   (options.backend != Backend::AUTO && options.backend != Backend::CPU && options.backend != Backend::VULKAN &&
		options.backend != Backend::CUDA)) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	const auto &definition = asrDefinition( options.asrModel, options.asrProfile) ; // 固定構成
	// 別のweightや不完全なregistryをCTCとして読まない
	if( definition.engine != AsrEngine::PARAKEET_CTC || !definition.tokensModel) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// 別engine用のVulkan指定を、CUDA成功や未許可のCPU実行へ置き換えない
	if( options.backend == Backend::VULKAN && !options.cpuFallback) {
		throw OperationError( ErrorKind::GPU_UNAVAILABLE) ;
	}
	whisper_log_set( quietVadLog, nullptr) ;
	reportProgress( progress, Stage::VALIDATING, -1) ;
	const auto model = verifyModel( options.modelRoot, definition.modelId.value(), stop) ; // weight lock
	const auto tokens = verifyModel( options.modelRoot, *definition.tokensModel, stop) ; // 文字定義
	std::optional<VerifiedModel> externalData ; // graphが参照する外部weightの所有lock
	// graphのSHAだけで、別fileのweightを検証した扱いにしない
	if( definition.externalDataModel) {
		externalData.emplace( verifyModel( options.modelRoot, *definition.externalDataModel, stop)) ;
	}
	std::optional<VerifiedModel> vad ; // 使用中のVADを変更・削除から保護する所有lock
	// 利用者が有効にした追加処理のモデルだけを必須とする
	if( options.speechFilter) {
		vad.emplace( verifyModel( options.modelRoot, ModelId::VOICE_ACTIVITY, stop)) ;
	}
	// CPU選択とAuto対象外の軽量構成ではCUDA driverを初期化しない
	const bool probeCuda =
		options.backend == Backend::CUDA || (options.backend == Backend::AUTO && definition.gpuValidated) ;
	const auto device = probeCuda ? queryCudaDevice() : CudaDeviceInfo{} ; // 同一GPUの実予算
	const bool useGpu = canUseCuda( device, options.backend, definition.gpuBytes,
								   definition.gpuValidated) ; // 今回の実行先
	// 明示CUDAでCPU移行も禁止した場合は、利用不可の理由を失敗分類で返す
	if( options.backend == Backend::CUDA && !useGpu && !options.cpuFallback) {
		throw OperationError( ErrorKind::GPU_UNAVAILABLE) ;
	}
	const auto attempt =
		//------------------------------------------------------------------------------
		/**
		 * @brief 同じ検証済みweightを、指定された一回の実行先で処理します
		 * @param[in] gpu trueは予算照合済みCUDA、falseはCPU
		 * @return 所有contextを解放した後の一回分の本文
		 */
		//------------------------------------------------------------------------------
		[&]( const bool gpu)
		{
			return runCtc( audio, options, definition, model, tokens, vad ? &*vad : nullptr, gpu, stop, progress, observer) ;
		} ; // 全試行を通じて同じgraph・weight・tokenのlockを保持する
	return runAsrWithCpuRetry( useGpu, options.cpuFallback, stop, attempt) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 一つの通常CPU contextで最大120秒窓を逐次処理し組立て前tokenを渡します
 * @param[in] audio identity照合済み全PCM。全録音をRAMへ読みません
 * @param[in] options Parakeet Quality/CPU/日本語/VAD有効/話者再解析無効の固定設定
 * @param[in] windows 元sampleの昇順非重複窓、各120秒以下
 * @param[in] stop 協調取消し
 * @param[in] progress 同期進捗。空可
 * @param[in] sink 必須同期borrow。返却後は当窓segments/tokenを解放します
 * @return 全context/stream/asset lockを解放済みの実行trace
 * @throws OperationError 不正構成/モデル不正/認識失敗/取消し。別構成へ再試行しません
 * @note 通常26秒VAD/token binding/word boundaryを共用し研究observerを呼びません
 * @see koefude::ParakeetEngine::transcribeReferenceWindows
 */
//------------------------------------------------------------------------------
AsrExecutionTrace ParakeetEngine::transcribeReferenceWindows( const AudioData &audio, const EngineOptions &options,
															 const std::span<const AsrSampleWindow> windows,
															 const std::stop_token					stop,
															 const ProgressCallback				   &progress,
															 const CtcReferenceWindowSink		   &sink)
{
	checkCancelled( stop) ;
	// 補助経路の構成は明示CPU Qualityへ限定し、別weightへのfallbackを許可しない
	if( !sink || audio.sampleCount() == 0 || audio.sampleCount() > K_MAXIMUM_TIMELINE_SAMPLES ||
	   options.asrModel != AsrModel::PARAKEET || options.asrProfile != AsrProfile::QUALITY ||
	   options.backend != Backend::CPU || options.cpuFallback || options.diarization || options.autoLanguage ||
	   !options.speechFilter || options.threads < 1 || options.threads > 16 || windows.size() > K_MAXIMUM_SEGMENTS) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	std::uint64_t previous = 0 ; // 逐次窓の元sample終端
	// 全予定窓を検査してからモデルへアクセスする
	for( const auto window : windows) { // 最大120秒の元PCM窓
		// 範囲外/交差/逆順や過大窓をnativeへ渡さない
		if( window.begin < previous || window.end <= window.begin || window.end > audio.sampleCount() ||
		   window.end - window.begin > 120ULL * K_SAMPLE_RATE) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
		previous = window.end ;
	}
	// 空予定ではモデルを読み込まない
	if( windows.empty()) {
		return {} ;
	}
	const auto &definition = asrDefinition( AsrModel::PARAKEET, AsrProfile::QUALITY) ; // 通常registry
	// registryの不完全定義を別weightへ読み替えない
	if( definition.engine != AsrEngine::PARAKEET_CTC || !definition.modelId || !definition.tokensModel) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	whisper_log_set( quietVadLog, nullptr) ;
	reportProgress( progress, Stage::VALIDATING, -1) ;
	const auto					 model = verifyModel( options.modelRoot, *definition.modelId, stop) ; // graph lock
	const auto					 tokens = verifyModel( options.modelRoot, *definition.tokensModel, stop) ; // 語彙lock
	std::optional<VerifiedModel> externalData ; // graphと同revisionの外部weight lock
	// 外部weightがある構成は全assetを検証する
	if( definition.externalDataModel) {
		externalData.emplace( verifyModel( options.modelRoot, *definition.externalDataModel, stop)) ;
	}
	const auto vad = verifyModel( options.modelRoot, ModelId::VOICE_ACTIVITY, stop) ; // 通常CPU VAD
	return runCtc( audio, options, definition, model, tokens, &vad, false, stop, progress, {}, windows, sink).execution ;
}

} // namespace koefude
