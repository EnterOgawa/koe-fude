//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whisper_engine.cpp
 * @brief 固定Whisperモデルの検証、装置選択、取消し対応推論を実行します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 whisper.cppによるローカルASRを接続
 * 0.1.0 Shunji Ogawa 2026/09/09 large-v3 FP16とCPU VADを検証・lockして使用
 * 0.1.0 Shunji Ogawa 2026/09/09 日本語認識の文字間空白を表示・保存前に整形
 * 0.1.0 Shunji Ogawa 2026/09/09 Kotobaの日本語固定とregistryごとのweight・予算選択を追加
 * 0.1.0 Shunji Ogawa 2026/09/10 同じcontextで部分PCMを順次認識し、時刻と進捗を全体へ戻す
 * 0.1.0 Shunji Ogawa 2026/09/10 profileごとの文脈とVAD設定で反復と発話の欠落を抑制
 * 0.1.0 Shunji Ogawa 2026/09/10 別engineの構成がWhisperへ渡された場合を入力時に拒否
 * 0.1.0 Shunji Ogawa 2026/09/11 明示CUDA指定を別backendのVulkan成功へ置き換えない
 * 0.1.0 Shunji Ogawa 2026/10/09 primary共有窓を原PCMから読み、原行/token時刻を保持
 * 0.1.0 Shunji Ogawa 2026/10/09 明示全文用途で不確かな時刻を保持し、全窓の本文を収集
 * 0.1.0 Shunji Ogawa 2026/10/19 宣言の役割を明記
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1301の補助CPU接続と限定検証を追加
 * 0.1.0 Shunji Ogawa 2026/10/21 補助CPUだけへ遅延関数表を接続し共有loopを保持
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1304の拒否値を元窓/native値と受動copy
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
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
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

#include <ggml-backend.h>
#include <whisper.h>

#include "core/audio_data.h"
#include "core/observed_asr_timeline.h"
#include "core/shared_asr_windows.h"
#include "core/text_spacing.h"
#include "inference/gpu_budget.h"
#include "inference/model_store.h"
#include "inference/whisper_cpu_runtime.h"
#include "platform/win_helpers.h"

namespace koefude {
namespace {

// 全文metadata境界の固定sample rateと2 hop格子を実native headerへ照合します
static_assert( WHISPER_SAMPLE_RATE == K_SAMPLE_RATE && 2 * WHISPER_HOP_LENGTH == K_WHISPER_TIMESTAMP_QUANTUM_SAMPLES) ;

constexpr std::uint64_t K_RESERVE_GPU_BYTES = K_GI_B ; // 他processとdriver用に残す余裕

/** @brief C callbackから参照する、推論呼出し中だけ有効な状態です */
struct CallbackState {
	/// UI/controllerが所有するstop stateを参照
	std::stop_token			stop ;
	/// transcribe呼出し中だけ有効な通知先
	const ProgressCallback *progress = nullptr ;
	/// callback内例外をC ABI外へ出さず終了後に通知
	bool					callbackFailed = false ;
	/// 録音全体のsample数
	std::uint64_t			totalSamples = 1 ;
	/// 既に認識が完了した先頭部分
	std::uint64_t			completedSamples = 0 ;
	/// 今回のwhisper_fullへ渡す要素数
	std::uint64_t			chunkSamples = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 第三者engineからの本文・pathを含む診断出力を抑止します
 * @param[in] level engineのlog level
 * @param[in] text engineが所有する診断text。内容を保存しません
 * @param[in] userData 未使用のcontext
 * @note 処理失敗は推論APIの戻り値から別途報告します
 */
//------------------------------------------------------------------------------
void quietLog( enum ggml_log_level level, const char *text, void *userData) noexcept
{
	static_cast<void>( level) ;
	static_cast<void>( text) ;
	static_cast<void>( userData) ;
}

//------------------------------------------------------------------------------
/**
 * @brief Whisperの安全な中断境界で取消し状態を返します
 * @param[in] userData transcribeが所有するCallbackState。NULL不可
 * @return 取消しまたは通知失敗があればtrue
 */
//------------------------------------------------------------------------------
bool abortInference( void *userData) noexcept
{
	const auto &state = *static_cast<CallbackState *>( userData) ; // C APIから戻された借用状態
	return state.stop.stop_requested() || state.callbackFailed ;
}

//------------------------------------------------------------------------------
/**
 * @brief engineの確定進捗をworkerの通知へ変換します
 * @param[in] context engine所有context。保持しません
 * @param[in] whisperState engine内部state。保持しません
 * @param[in] percent 処理した音声に基づく段階進捗
 * @param[in] userData 本呼出し中に有効なCallbackState
 */
//------------------------------------------------------------------------------
void updateProgress( whisper_context *context, whisper_state *whisperState, const int percent, void *userData) noexcept
{
	static_cast<void>( context) ;
	static_cast<void>( whisperState) ;
	auto &state = *static_cast<CallbackState *>( userData) ; // transcribeが所有する状態
	try {
		// 現chunkの完了分を、既に処理した全体sampleへ加える
		const auto processed =
			state.completedSamples + state.chunkSamples * static_cast<unsigned>( std::clamp( percent, 0, 99)) / 100 ;
		reportProgress( *state.progress, Stage::TRANSCRIBING,
					   static_cast<int>( std::min<std::uint64_t>( 99, processed * 100 / state.totalSamples))) ;
	} catch( ...) {
		// 例外をC callbackの外へ漏らさず、推論を停止して呼出し元へ失敗を返す
		state.callbackFailed = true ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief Vulkanの同一deviceで得た空き量とprofileを照合します
 * @param[in] backend 利用者が選択した方針
 * @param[in] definition 選択したモデル構成と検証済み予算
 * @return whisper.cppのGPU index。利用不可またはCPU選択は-1
 * @note 予算の事前判定でありdriver allocationのhard limitではありません
 */
//------------------------------------------------------------------------------
[[nodiscard]] int selectGpu( const Backend backend, const AsrDefinition &definition)
{
	// CPU指定またはVulkanを組み込まないbuildではGPUの初期化を要求しない
	if( backend == Backend::CPU || backend == Backend::CUDA || !KOEFUDE_HAS_VULKAN) {
		return -1 ;
	}
	int gpuIndex = 0 ; // whisper.cppがGPU/IGPUに付けるindex
	// 登録済みbackendだけを確認し、任意directoryからDLLを探索しない
	for( std::size_t index = 0; index < ggml_backend_dev_count(); ++index) {
		const auto device = ggml_backend_dev_get( index) ; // registry所有の非所有device
		const auto type = ggml_backend_dev_type( device) ; // GPU/IGPUの区別
		// CPUはwhisper.cppのGPU indexに含まれない
		if( type != GGML_BACKEND_DEVICE_TYPE_GPU && type != GGML_BACKEND_DEVICE_TYPE_IGPU) {
			continue ;
		}
		const std::string_view registryName =
			ggml_backend_reg_name( ggml_backend_dev_backend_reg( device)) ; // 登録backend名
		const std::string_view description = ggml_backend_dev_description( device) ; // 比較専用名
		// Autoで評価済みprofileを選ぶ対象adapter名と一致するか
		const bool			   validatedAdapter = description == "NVIDIA GeForce RTX 3080 Ti" ;
		// 初期Autoは評価対象adapterに限定し、明示Vulkan指定だけで未評価機の試験を許可する
		if( registryName == "Vulkan" && type == GGML_BACKEND_DEVICE_TYPE_GPU &&
		   (backend == Backend::VULKAN || (validatedAdapter && definition.gpuValidated))) {
			ggml_backend_dev_props properties{} ; // 同一GPUのPCI ID照合に使う属性
			ggml_backend_dev_get_props( device, &properties) ;
			// 同じPCI IDのGPUについて取得した物理量・予算・使用量
			const auto budget = queryGpuBudget( properties.device_id ? properties.device_id : "") ;
			// 不明値・差引きunderflow・12GiB超の予算を利用しない
			if( fitsGpuBudget( budget, definition.gpuBytes, K_RESERVE_GPU_BYTES)) {
				return gpuIndex ;
			}
		}
		++gpuIndex ;
	}
	return -1 ;
}

//------------------------------------------------------------------------------
/**
 * @brief 一つの装置でcontextを作成し認識を完了します
 * @param[in] audio 所有PCMの借用view
 * @param[in] options 固定された推論設定
 * @param[in] model 検証済みでlockされたモデル
 * @param[in] vadModel 検証・lock済みVAD。NULLなら発話区間検出を使用しません
 * @param[in] gpuIndex 使用するGPU、-1はCPU
 * @param[in] stop 協調的取消し
 * @param[in] progress worker上の通知先
 * @return engine resourceを持たない所有結果
 * @throws OperationError 初期化失敗、推論失敗、不正結果、取消し
 */
//------------------------------------------------------------------------------
[[nodiscard]] Recognition runOnce( const AudioData &audio, const EngineOptions &options, const VerifiedModel &model,
								  const VerifiedModel *vadModel, const int gpuIndex, const std::stop_token stop,
								  const ProgressCallback &progress)
{
	checkCancelled( stop) ;
	reportProgress( progress, Stage::LOADING_MODEL, -1) ;
	const std::string modelPath = toUtf8( model.path.native()) ; // engineへ渡すUTF-8 path
	auto			  contextOptions = whisper_context_default_params() ; // 固定したengineのcontext初期値
	contextOptions.use_gpu = gpuIndex >= 0 ;
	contextOptions.gpu_device = std::max( gpuIndex, 0) ;
	contextOptions.flash_attn = gpuIndex >= 0 ;
	const std::unique_ptr<whisper_context, decltype(&whisper_free)> context(
		whisper_init_from_file_with_params( modelPath.c_str(), contextOptions), whisper_free) ;
	// メモリ不足等でcontextを作れない場合は所有resourceを解放して呼出し元へ返す
	if( !context) {
		throw OperationError( ErrorKind::INFERENCE_FAILED) ;
	}
	checkCancelled( stop) ;
	CallbackState callbackState{stop, &progress, false} ; // whisper_full終了まで有効なC callback状態
	auto		  params = whisper_full_default_params( WHISPER_SAMPLING_BEAM_SEARCH) ; // 精度優先の探索設定
	params.n_threads = options.threads ;
	const auto &definition = asrDefinition( options.asrModel, options.asrProfile) ; // 比較・固定済みの認識設定
	// 品質優先と特化モデルは前区間の誤認識をpromptへ引き継がず、反復の伝播を抑える
	params.n_max_text_ctx = definition.historyTokens ;
	params.language = options.autoLanguage ? "auto" : "ja" ;
	params.translate = false ;
	params.print_progress = false ;
	params.print_realtime = false ;
	params.print_timestamps = false ;
	params.print_special = false ;
	params.debug_mode = false ;
	params.beam_search.beam_size = 5 ;
	params.token_timestamps = true ;
	params.max_len = 80 ;
	params.split_on_word = true ;
	params.suppress_nst = true ;
	const std::string vadPath = vadModel ? toUtf8( vadModel->path.native()) : std::string{} ; // 同期推論中の借用元
	params.vad = vadModel != nullptr ;
	params.vad_model_path = vadModel ? vadPath.c_str() : nullptr ;
	// 短い相づちと語頭・語尾を残し、細かい無音ごとに文脈を切断しない
	params.vad_params.min_speech_duration_ms = 100 ;
	params.vad_params.threshold = static_cast<float>( definition.vadThresholdMilli) / 1000.0F ;
	params.vad_params.min_silence_duration_ms = definition.vadSilenceMs ;
	params.vad_params.speech_pad_ms = definition.vadPaddingMs ;
	params.vad_params.samples_overlap = 0.0F ;
	params.abort_callback = abortInference ;
	params.abort_callback_user_data = &callbackState ;
	params.progress_callback = updateProgress ;
	params.progress_callback_user_data = &callbackState ;
	reportProgress( progress, Stage::TRANSCRIBING, 0) ;
	Recognition recognition ; // 全chunkで共通のモデルと装置の所有結果
	recognition.usedGpu = gpuIndex >= 0 ;
	std::size_t	  textBytes = 0 ; // 全録音の本文容量
	std::uint64_t offset = 0 ; // 次に認識する元音声のsample位置
	callbackState.totalSamples = audio.sampleCount() ;
	// 固定RAMの連続PCMを末尾まで処理し、同じcontextのモデルweightを再利用する
	while( offset < audio.sampleCount()) {
		checkCancelled( stop) ;
		const auto chunk = readAsrChunk( audio, offset) ; // 内部処理単位。録音全長の制限にはしない
		callbackState.completedSamples = offset ;
		callbackState.chunkSamples = chunk.size() ;
		const int status = whisper_full( context.get(), params, chunk.data(),
										static_cast<int>( chunk.size())) ; // 有界なint API入力
		checkCancelled( stop) ;
		// 中断を正常な空文字起こしと混同せず、元のAPI codeを保持する
		if( status != 0 || callbackState.callbackFailed) {
			throw OperationError( ErrorKind::INFERENCE_FAILED, static_cast<std::uint32_t>( status)) ;
		}
		const int	count = whisper_full_n_segments( context.get()) ; // engineが確定したtext区間数
		const char *language = whisper_lang_str( whisper_full_lang_id( context.get())) ; // engineの判定言語
		const bool	japanese =
			!options.autoLanguage || (language && std::string_view( language) == "ja") ; // 整形対象の言語
		// 不正な件数や上限超過をvector確保前に拒否する
		if( count < 0 || static_cast<std::size_t>( count) > K_MAXIMUM_SEGMENTS - recognition.segments.size()) {
			throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
		}
		const auto durationMs = static_cast<std::int64_t>( chunk.size() / 16) ; // 今回の相対終端ms
		const auto offsetMs = static_cast<std::int64_t>( offset / 16) ; // 元音声基準へ戻す開始ms
		// engineの所有textを有限のDTOへコピーし、外部pointerをUIへ渡さない
		for( int index = 0; index < count; ++index) {
			const char *rawText = whisper_full_get_segment_text( context.get(), index) ; // context所有text
			// NULLを空の成功textに置き換えずengine異常として扱う
			if( !rawText) {
				throw OperationError( ErrorKind::INFERENCE_FAILED) ;
			}
			std::string text( rawText) ; // contextから独立した所有本文
			const auto	first = text.find_first_not_of( " \r\n\t") ; // 表示不要な先頭空白の終端
			// 空白だけの区間を表示行へ追加しない
			if( first == std::string::npos) {
				continue ;
			}
			text = text.substr( first, text.find_last_not_of( " \r\n\t") - first + 1) ;
			static_cast<void>( toWide( text)) ; // 不正UTF-8をUIへ渡す前に拒否
			// 日本語と判定した本文だけを整え、英語等の単語間空白を変更しない
			if( japanese) {
				text = normalizeJapaneseSpacing( text) ;
			}
			// 加算前に残容量で比較し、結果の無制限蓄積を防ぐ
			if( text.size() > K_MAXIMUM_TEXT_BYTES - textBytes) {
				throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
			}
			textBytes += text.size() ;
			// engineから得た区間開始時刻。単位は10msで、換算前に検証する
			const std::int64_t rawStart = whisper_full_get_segment_t0( context.get(), index) ;
			// engineから得た区間終了時刻。単位は10msで、換算前に検証する
			const std::int64_t rawEnd = whisper_full_get_segment_t1( context.get(), index) ;
			// engineの外部時刻は単位換算前に上限を確認し、乗算overflowを防ぐ
			if( rawStart < 0 || rawEnd < rawStart || rawEnd > (durationMs + 1000) / 10) {
				throw OperationError( ErrorKind::INVALID_TIMELINE) ;
			}
			const std::int64_t start = rawStart * 10 ; // 今回のchunk先頭を基準にした開始ms
			const std::int64_t end = rawEnd * 10 ; // 今回のchunk先頭を基準にした終了ms
			// 解析不能な時刻をfile基準として誤表示しない
			if( start < 0 || end < start || start > durationMs || end > durationMs + 1000) {
				throw OperationError( ErrorKind::INVALID_TIMELINE) ;
			}
			recognition.segments.push_back( {offsetMs + start, offsetMs + std::min( end, durationMs), std::move( text)}) ;
		}
		offset += chunk.size() ; // 読取り位置は実際に認識した範囲だけ進め、重複・欠落を作らない
		reportProgress( progress, Stage::TRANSCRIBING, static_cast<int>( offset * 100 / audio.sampleCount())) ;
	}
	reportProgress( progress, Stage::TRANSCRIBING, 100) ;
	return recognition ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 単一registryのWhisperへprimaryの同じ原PCM窓を渡します
 * @param[in] audio primaryと同一の全長16kHz mono原PCM。gainを掛けません
 * @param[in] options Whisper Quality/内部VAD falseの固定設定
 * @param[in] windows 元sampleの昇順非重複窓。空はNoSpeechで、別窓を生成しません
 * @param[in] stop 協調取消し
 * @param[in] progress worker通知。空可
 * @param[in] observer 同期借用の部分結果通知。空可
 * @param[in] timelineUse 既定は旧segment契約。全文用途はobserver必須です
 * @param[in] services nullは実依存。非nullは同期scopeだけ借用する内部試験用です
 * @return contextを解放済みの所有結果。input_windowsは渡した窓と一致します
 * ObservedWholeTextはraw_rowsを全量保持し、segmentsを生成しません
 * @throws OperationError 不正窓/時刻/構成、失敗/取消し。元値は通知sidecarへ保持します
 * @see koefude::LocalAsrEngine::transcribeWindows
 */
//------------------------------------------------------------------------------
Recognition LocalAsrEngine::transcribeWindows( const AudioData &audio, const EngineOptions &options,
											  const std::span<const AsrSampleWindow> windows,
											  const std::stop_token stop, const ProgressCallback &progress,
											  const RecognitionObserver &observer, const AsrTimelineUse timelineUse,
											  const detail::WhisperWindowServices *services)
{
	checkCancelled( stop) ;
	checkSharedAsrWindows( windows, audio.sampleCount()) ;
	// 独立whole-file認識や別系統の窓生成を共有窓入口へ混ぜません
	if( asrDefinition( options.asrModel, options.asrProfile).engine != AsrEngine::WHISPER_CPP) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	WhisperEngine engine ; // このworker scopeだけのadapter
	return engine.transcribeWindows( audio, options, windows, stop, progress, observer, timelineUse, services) ;
}

namespace {
//------------------------------------------------------------------------------
/**
 * @brief status確認後だけ固定型のoutを値として使います
 * @tparam Value 固定値型
 * @tparam Function 同期API型
 * @tparam Args 入力型
 * @param[in] function 選択した表の非送出API
 * @param[in] args 同期借用の入力
 * @return host所有の値
 * @throws std::bad_alloc OOM
 * @throws OperationError その他
 */
//------------------------------------------------------------------------------
template <class Value, class Function, class... Args> Value whisperValue( Function function, Args... args)
{
	Value value{} ; // 失敗outを利用しません
	whisper_cpu::checkStatus( function( args..., &value)) ;
	return value ;
}
//------------------------------------------------------------------------------
/**
 * @brief 一つの共有窓loopへstatic又は補助専用の表を渡します
 * @param[in] audio 全PCM
 * @param[in] options 固定設定
 * @param[in] windows 予定窓
 * @param[in] stop 停止
 * @param[in] progress 同期通知
 * @param[in] observer 容量監視
 * @param[in] timelineUse 用途
 * @param[in] auxiliary 補助専用の明示選択
 * @param[in] diagnostics null可の受動capture
 * @param[in] services nullは実依存
 * @return context/moduleを解放済みの所有結果
 */
//------------------------------------------------------------------------------
Recognition runSharedWindows( const AudioData &audio, const EngineOptions &options,
							 const std::span<const AsrSampleWindow> windows, const std::stop_token stop,
							 const ProgressCallback &progress, const RecognitionObserver &observer,
							 const AsrTimelineUse timelineUse, bool auxiliary, CorrectionDiagnosticCapture *diagnostics,
							 const detail::WhisperWindowServices *services)
{
	checkCancelled( stop) ;
	checkAsrTimelineUse( timelineUse, static_cast<bool>( observer)) ;
	checkSharedAsrWindows( windows, audio.sampleCount()) ;
	const auto &definition = asrDefinition( options.asrModel, options.asrProfile) ; // 単一registry
	// 本工程で固定したQuality/日本語/内部VAD false/fallback falseだけを接続します
	if( definition.engine != AsrEngine::WHISPER_CPP || options.asrModel != AsrModel::WHISPER ||
	   options.asrProfile != AsrProfile::QUALITY || options.speechFilter || options.autoLanguage ||
	   options.cpuFallback || options.threads != 4 || definition.historyTokens != 0 ||
	   (options.backend != Backend::VULKAN && options.backend != Backend::CPU)) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	Recognition recognition ; // 原PCM窓とsidecarを有限に所有します
	// primaryで正常0窓の場合は別VADや認識窓を作らず、そのまま空結果を返します
	if( windows.empty()) {
		return recognition ;
	}
	std::optional<VerifiedModel> model ; // 例外時もcontext/moduleより後にleaseを解放します
	whisper_cpu::Runtime runtime( services && services->portable ? *services->portable : whisper_cpu::portableApi(),
								 auxiliary ? diagnostics : nullptr) ; // worker同期所有
	// 明示した補助CPUだけがload前gateと最適化表を選びます
	if( auxiliary && options.backend == Backend::CPU) {
		runtime.select( services ? services->runtime : whisper_cpu::productionServices(), stop) ;
	}
	const auto &api = runtime.api() ; // contextと同じmoduleの表
	whisper_cpu::checkStatus( api.logSet( quietLog, nullptr)) ;
	model.emplace( services && services->verify ? services->verify( options.modelRoot, definition.modelId.value(), stop)
											   : verifyModel( options.modelRoot, definition.modelId.value(),
															 stop)) ; // 実weight lock
	const auto gpuIndex =
		services && services->gpu ? services->gpu( options.backend, definition) : selectGpu( options.backend, definition) ;
	// 明示VulkanをCPU成功へ置換しません
	if( options.backend == Backend::VULKAN && gpuIndex < 0) {
		throw OperationError( ErrorKind::GPU_UNAVAILABLE) ;
	}
	const auto modelPath = toUtf8( model->path.native()) ; // context初期化中有効なpath
	auto contextOptions = whisperValue<whisper_context_params>( api.contextDefaultParams) ; // 既存Legacy attention
	contextOptions.use_gpu = gpuIndex >= 0 ;
	contextOptions.gpu_device = std::max( gpuIndex, 0) ;
	contextOptions.flash_attn = gpuIndex >= 0 ;
	whisper_context *context = nullptr ; // 作成した表だけが所有memoryを解放します
	const auto		 initStatus = api.initFromFile( modelPath.c_str(), &contextOptions, &context) ;
	runtime.own( context) ; // status確認で例外になっても一回cleanup
	whisper_cpu::checkStatus( initStatus) ;
	// context初期化失敗は無音と区別し、lockと所有contextをRAIIで回収します
	if( !context) {
		throw OperationError( ErrorKind::INFERENCE_FAILED) ;
	}
	CallbackState callbackState{stop, &progress, false} ; // 同期C callbackだけが借用します
	callbackState.totalSamples = audio.sampleCount() ;
	auto params =
		whisperValue<whisper_full_params>( api.fullDefaultParams, WHISPER_SAMPLING_BEAM_SEARCH) ; // 既存beam構成
	params.n_threads = options.threads ;
	params.n_max_text_ctx = definition.historyTokens ;
	params.language = "ja" ;
	params.translate = false ;
	params.print_progress = false ;
	params.print_realtime = false ;
	params.print_timestamps = false ;
	params.print_special = false ;
	params.debug_mode = false ;
	params.beam_search.beam_size = 5 ;
	params.temperature = 0.0F ;
	params.temperature_inc = 0.2F ;
	params.token_timestamps = true ;
	params.max_len = 80 ;
	params.split_on_word = true ;
	params.suppress_nst = true ;
	params.vad = false ; // primary実窓以外の内部VADを禁止します
	params.vad_model_path = nullptr ;
	params.abort_callback = abortInference ;
	params.abort_callback_user_data = &callbackState ;
	params.progress_callback = updateProgress ;
	params.progress_callback_user_data = &callbackState ;
	recognition.usedGpu = gpuIndex >= 0 ;
	recognition.execution.backend = gpuIndex >= 0 ? Backend::VULKAN : Backend::CPU ;
	recognition.execution.family = options.asrModel ;
	recognition.execution.profile = options.asrProfile ;
	recognition.execution.provider = gpuIndex >= 0 ? "vulkan" : "cpu" ;
	recognition.execution.modelId = modelDefinition( definition.modelId.value()).id ;
	recognition.execution.verifiedModelIds = {std::string( modelDefinition( definition.modelId.value()).id)} ;
	recognition.execution.threads = params.n_threads ;
	recognition.execution.historyTokens = params.n_max_text_ctx ;
	recognition.execution.beamSize = params.beam_search.beam_size ;
	recognition.execution.temperature = params.temperature ;
	recognition.execution.temperatureInc = params.temperature_inc ;
	recognition.execution.flashAttention = contextOptions.flash_attn ;
	recognition.execution.externalWindows = true ;
	recognition.execution.language = params.language ;
	recognition.execution.decodingMethod = "beam_search" ;
	recognition.execution.maxLen = params.max_len ;
	recognition.execution.tokenTimestamps = params.token_timestamps ;
	recognition.execution.splitOnWord = params.split_on_word ;
	recognition.execution.suppressNst = params.suppress_nst ;
	recognition.execution.observedWholeText = timelineUse == AsrTimelineUse::OBSERVED_WHOLE_TEXT ;
	std::size_t textBytes = 0 ; // 全原行のUTF-8容量
	std::size_t tokenCount = 0 ; // 全raw token件数
	std::size_t tokenBytes = 0 ; // 全raw token textのbyte境界
	// primaryで確定した各元窓を同じ順に一回ずつ処理します
	for( const auto &window : windows) { // 同一原PCMの現在の共有窓
		checkCancelled( stop) ;
		const auto samples = readSharedAsrWindow( audio, window) ; // gainなしの有界copy
		recognition.inputWindows.push_back( window) ;
		// 次の中断不可native APIに入る前に、実渡し窓と前の原行を保持します
		if( observer) {
			observer( recognition) ;
		}
		checkCancelled( stop) ; // 窓準備中の要求を次のnative呼出しより前に扱います
		callbackState.completedSamples = window.begin ;
		callbackState.chunkSamples = samples.size() ;
		const auto status = whisperValue<int>( api.full, context, &params, samples.data(),
											  static_cast<int>( samples.size())) ; // 同じcontext、batch1、同じ原PCM窓
		checkCancelled( stop) ;
		// native失敗やcallback異常を空成功へ変更しません
		if( status != 0 || callbackState.callbackFailed) {
			throw OperationError( ErrorKind::INFERENCE_FAILED, static_cast<std::uint32_t>( status)) ;
		}
		const auto count = whisperValue<int>( api.nSegments, context) ; // 実APIの元行数
		// 件数境界をallocation前に検査します
		if( count < 0 || static_cast<std::size_t>( count) > K_MAXIMUM_SEGMENTS - recognition.rawRows.size()) {
			throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
		}
		// 全元行を順序通り保持し、trim/日本語正規化/時刻clipを行いません
		for( int index = 0; index < count; ++index) { // 現在の元行番号
			const auto rawText = whisperValue<const char *>( api.segmentText, context, index) ; // 同期借用
			// NULLと過大な外部本文を拒否します
			if( !rawText || strnlen_s( rawText, K_MAXIMUM_TEXT_BYTES + 1) > K_MAXIMUM_TEXT_BYTES - textBytes) {
				throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
			}
			const auto rawStart = whisperValue<std::int64_t>( api.segmentT0, context, index) ; // 窓内cs
			const auto rawEnd = whisperValue<std::int64_t>( api.segmentT1, context, index) ; // 窓内cs
			RawAsrRow  raw{std::string( rawText),
						   static_cast<double>( rawStart) * 10.0,
						   static_cast<double>( rawEnd) * 10.0,
						   window,
						   {}} ; // 原値を修正しないsidecar
			textBytes += raw.text.size() ;
			recognition.rawRows.push_back( std::move( raw)) ;
			// token取得が失敗しても、取得済み本文と元時刻を先に保持します
			if( observer) {
				observer( recognition) ;
			}
			const auto countTokens = whisperValue<int>( api.nTokens, context, index) ; // 元token件数
			// 不正/過大token数でも行自体は残してwriter入場を止めます
			if( countTokens < 0 || static_cast<std::size_t>( countTokens) > K_MAXIMUM_ASR_TOKENS - tokenCount) {
				// 例外の前に原本文を保持します
				if( observer) {
					observer( recognition) ;
				}
				throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
			}
			// 特別tokenもrawに保持し、本文時刻へはnative IDの区別を使います
			for( int tokenIndex = 0; tokenIndex < countTokens; ++tokenIndex) { // 元token番号
				const auto data = whisperValue<whisper_token_data>( api.tokenData, context, index,
																   tokenIndex) ; // 原値
				const auto tokenText = whisperValue<const char *>( api.tokenText, context, index,
																  tokenIndex) ; // 借用
				// 外部token textの欠落/容量超過は成功へ昇格しません
				if( !tokenText || strnlen_s( tokenText, K_MAXIMUM_TEXT_BYTES + 1) > K_MAXIMUM_TEXT_BYTES) {
					// 当該本文と既に取得した原tokenを保持してから失敗します
					if( observer) {
						observer( recognition) ;
					}
					throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
				}
				const auto tokenLength = strnlen_s( tokenText, K_MAXIMUM_TEXT_BYTES + 1) ; // 検査済みbyte
				// 原tokenごとの上限だけで無制限合計にならないよう検査します
				if( tokenLength > K_MAXIMUM_TEXT_BYTES - tokenBytes) {
					// 未保存部分を推測せず、取得済み原行/tokenだけを保持します
					if( observer) {
						observer( recognition) ;
					}
					throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
				}
				tokenBytes += tokenLength ;
				recognition.rawRows.back().tokens.push_back(
					{std::string( tokenText), static_cast<double>( data.t0), static_cast<double>( data.t1), "centiseconds",
					 data.id >= whisperValue<whisper_token>( api.tokenEot, context)}) ;
			}
			tokenCount += recognition.rawRows.back().tokens.size() ;
			// 元時刻と本文を先に保持し、用途別のguardへ原値のまま渡します
			if( observer) {
				observer( recognition) ;
			}
			std::optional<WhisperTimelineGuardFailure> timelineFailure ; // 当該行だけの未観測out
			// 受動captureは明示補助の全文用途だけへ接続します
			const bool observeFailure = auxiliary && diagnostics && timelineUse == AsrTimelineUse::OBSERVED_WHOLE_TEXT ;
			try {
				checkWhisperRowTimeline( recognition.rawRows.back(), timelineUse,
										observeFailure ? &timelineFailure : nullptr) ;
			} catch( ...) {
				// 同じguardが拒否した時だけ既存値をcopyし、元例外をそのまま伝播します
				if( observeFailure && timelineFailure) {
					const auto actualSamples = window.end - window.begin ; // 検査済み窓長
					const auto ticks =
						actualSamples / K_WHISPER_TIMESTAMP_QUANTUM_SAMPLES +
						(actualSamples % K_WHISPER_TIMESTAMP_QUANTUM_SAMPLES != 0 ? 1U : 0U) ; // 元guardと同じceil格子
					diagnostics->whisperTimelineFailed(
						{*timelineFailure, static_cast<std::uint64_t>( recognition.inputWindows.size() - 1),
						 window.begin, window.end, actualSamples, static_cast<std::uint64_t>( index), ticks * 2,
						 rawStart, rawEnd}) ;
				}
				throw ;
			}
			// 通常用途だけに旧整数時刻segmentを作り、全文用途は原行を正本にします
			if( timelineUse == AsrTimelineUse::STRICT_SEGMENTS) {
				recognition.segments.push_back( {static_cast<std::int64_t>( window.begin / 16) + rawStart * 10,
												static_cast<std::int64_t>( window.begin / 16) + rawEnd * 10,
												recognition.rawRows.back().text,
												{}}) ;
			}
		}
		reportProgress( progress, Stage::TRANSCRIBING, static_cast<int>( window.end * 100 / audio.sampleCount())) ;
	}
	checkCancelled( stop) ;
	runtime.close() ; // Gemmaの計数/planより前にfreeとFreeLibraryの成立を確認
	// model lockも呼出し側へ返る前に解放されます
	return recognition ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 単一registryのWhisperへprimaryの同じ原PCM窓を渡します
 * @param[in] audio primaryと同一の全長16kHz mono原PCM。gainを掛けません
 * @param[in] options Whisper Quality/内部VAD falseの固定設定
 * @param[in] windows 元sampleの昇順非重複窓。空はNoSpeechで、別窓を生成しません
 * @param[in] stop 協調取消し
 * @param[in] progress worker通知。空可
 * @param[in] observer 同期借用の部分結果通知。空可
 * @param[in] timelineUse 既定は旧segment契約。全文用途はobserver必須です
 * @param[in] services nullは実依存。非nullは同期scopeだけ借用する内部試験用です
 * @return contextを解放済みの所有結果。input_windowsは渡した窓と一致します
 * ObservedWholeTextはraw_rowsを全量保持し、segmentsを生成しません
 * @throws OperationError 不正窓/時刻/構成、失敗/取消し。元値は通知sidecarへ保持します
 * @see koefude::WhisperEngine::transcribeWindows
 */
//------------------------------------------------------------------------------
Recognition WhisperEngine::transcribeWindows( const AudioData &audio, const EngineOptions &options,
											 std::span<const AsrSampleWindow> windows, std::stop_token stop,
											 const ProgressCallback &progress, const RecognitionObserver &observer,
											 AsrTimelineUse timelineUse, const detail::WhisperWindowServices *services)
{
	return runSharedWindows( audio, options, windows, stop, progress, observer, timelineUse, false, nullptr, services) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 通常文章補正専用にCPU最適化表を選び、同じ共有窓loopを実行します
 * @param[in] audio 全16kHz mono PCM
 * @param[in] options 固定Quality設定
 * @param[in] windows 全予定窓
 * @param[in] stop 協調停止
 * @param[in] progress 同期通知
 * @param[in] observer 全文容量監視、空不可
 * @param[in] timelineUse 全文用途
 * @param[in] diagnostics null可、呼出し中生存する受動capture
 * @param[in] services nullは実依存。非nullは同期借用の内部試験seam
 * @return context/module/leaseを明示解放済みの所有結果
 * @throws OperationError 資産不正/推論/取消し/解放失敗。別ISA再試行は行いません
 * @see koefude::LocalAsrEngine::transcribeAuxiliaryWindows
 */
//------------------------------------------------------------------------------
Recognition LocalAsrEngine::transcribeAuxiliaryWindows( const AudioData &audio, const EngineOptions &options,
													   std::span<const AsrSampleWindow> windows, std::stop_token stop,
													   const ProgressCallback	 &progress,
													   const RecognitionObserver &observer, AsrTimelineUse timelineUse,
													   CorrectionDiagnosticCapture		   *diagnostics,
													   const detail::WhisperWindowServices *services)
{
	// 補助の全文観測以外を新しい選択入口へ混ぜません
	if( timelineUse != AsrTimelineUse::OBSERVED_WHOLE_TEXT) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	return runSharedWindows( audio, options, windows, stop, progress, observer, timelineUse, true, diagnostics,
							services) ;
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
Recognition WhisperEngine::transcribe( const AudioData &audio, const EngineOptions &options, const std::stop_token stop,
									  const ProgressCallback &progress)
{
	// 録音時間ではなくPCMの表現境界と推論設定を検証する
	if( audio.sampleCount() == 0 || audio.sampleCount() > K_MAXIMUM_TIMELINE_SAMPLES || options.threads < 1 ||
	   options.threads > 16 ||
	   (options.backend != Backend::AUTO && options.backend != Backend::CPU && options.backend != Backend::VULKAN &&
		options.backend != Backend::CUDA)) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	const auto &definition = asrDefinition( options.asrModel, options.asrProfile) ; // 不明な組合せはI/O前に拒否
	// 別engineの固定weightをWhisperとして開かない
	if( definition.engine != AsrEngine::WHISPER_CPP) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	auto effectiveOptions = options ; // 日本語固定を呼出側の設定を書き換えず適用するsnapshot
	effectiveOptions.autoLanguage = options.autoLanguage && !definition.japaneseOnly ;
	whisper_log_set( quietLog, nullptr) ;
	reportProgress( progress, Stage::VALIDATING, -1) ;
	// 選択されたweightを検証し、別profileへの暗黙の切替を許可しない
	const auto					 model = verifyModel( options.modelRoot, definition.modelId.value(),
													 stop) ; // contextより長寿命のlock
	std::optional<VerifiedModel> vadModel ; // VAD終了とCPU再試行まで変更を拒否するlock
	// 発話区間検出を選んだ場合だけ追加modelを必須とする
	if( options.speechFilter) {
		vadModel.emplace( verifyModel( options.modelRoot, ModelId::VOICE_ACTIVITY, stop)) ;
	}
	const int gpuIndex = selectGpu( options.backend, definition) ; // 同一deviceの予算で選択したindex
	// 明示GPU指定でfallbackも無効なら、別backendやCPUへ黙って切り替えない
	if( (options.backend == Backend::VULKAN || options.backend == Backend::CUDA) && gpuIndex < 0 &&
	   !options.cpuFallback) {
		throw OperationError( ErrorKind::GPU_UNAVAILABLE) ;
	}
	try {
		return runOnce( audio, effectiveOptions, model, vadModel ? &*vadModel : nullptr, gpuIndex, stop, progress) ;
	} catch( const OperationError &error) {
		// recoverable GPU推論失敗だけを同じmodelで最大1回CPUへ再試行する
		if( gpuIndex >= 0 && options.cpuFallback && error.kind() == ErrorKind::INFERENCE_FAILED &&
		   !stop.stop_requested()) {
			auto recognition = runOnce( audio, effectiveOptions, model, vadModel ? &*vadModel : nullptr, -1, stop,
									   progress) ; // 前context解放後の再試行
			recognition.cpuRetried = true ;
			return recognition ;
		}
		throw ;
	}
}

} // namespace koefude
