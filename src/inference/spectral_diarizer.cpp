//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file spectral_diarizer.cpp
 * @brief 局所話者の非重複音声からNME-SCで全体人数と区間を推定します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 固定modelの直接CPU推論とNME-SCを接続
 * 0.1.0 Shunji Ogawa 2026/09/10 特徴を一時記録し、全録音の代表と逐次投票で時間上限を撤廃
 * 0.1.0 Shunji Ogawa 2026/09/23 診断用graph選択を既定から分離して伝達
 * 0.1.0 Shunji Ogawa 2026/10/19 callbackの契約・宣言の役割を明記
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 * @note 論文との差と所有権の境界はADR 0009を参照します
 */
//------------------------------------------------------------------------------
#include "inference/spectral_diarizer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <onnxruntime_cxx_api.h>
#include <sherpa-onnx/c-api/c-api.h>

#include "inference/model_store.h"
#include "inference/speaker_stream.h"
#include "inference/spectral_clustering.h"
#include "platform/temporary_store.h"
#include "platform/win_helpers.h"
#include <limits>
#include <span>
#include <type_traits>

namespace koefude {
namespace {

constexpr std::size_t				  K_WINDOW_SAMPLES = 160000 ; // 固定modelの10秒入力
constexpr std::size_t				  K_WINDOW_STEP = 40000 ; // 2.5秒ごとのwindow開始間隔
constexpr std::size_t				  K_FRAME_SAMPLES = 270 ; // receptive fieldの移動sample数
constexpr std::size_t				  K_RECEPTIVE_SAMPLES = 991 ; // receptive fieldの幅
constexpr std::size_t				  K_FRAMES = 589 ; // (160000 - 991) / 270 + 1
constexpr std::size_t				  K_MINIMUM_VOICED_SAMPLES = 24000 ; // 1.5秒未満を人数の根拠にしない
constexpr std::array<std::uint8_t, 7> K_CLASS_MASK{0, 1, 2, 4, 3, 5, 6} ; // 局所3枠のpowerset

/** @brief 排他的な一時fileへ逐次記録する、pointerを含まない内部観測です */
struct StoredWindow {
	/// 元音声時刻と局所発話mask
	SpeakerWindow						   window{} ;
	/// 未取得はsentinel
	std::array<std::uint64_t, 3>		   ids{UINT64_MAX, UINT64_MAX, UINT64_MAX} ;
	/// 最大schema内の各局所特徴
	std::array<std::array<float, 1024>, 3> features{} ;
} ;
static_assert( std::is_trivially_copyable_v<StoredWindow>) ; // 同じprocess内だけの一時記録

//------------------------------------------------------------------------------
/**
 * @brief path等を含むruntime logを表示・保存しません
 * @param[in] parameter 未使用のcontext
 * @param[in] severity runtimeの重要度
 * @param[in] category 借用分類文字列
 * @param[in] identifier 借用識別文字列
 * @param[in] location 借用source位置
 * @param[in] message 借用説明。保持しません
 * @note 失敗はAPI codeから別途報告します
 */
//------------------------------------------------------------------------------
void ORT_API_CALL discardRuntimeLog( void *parameter, OrtLoggingLevel severity, const char *category,
									const char *identifier, const char *location, const char *message) noexcept
{
	static_cast<void>( parameter) ;
	static_cast<void>( severity) ;
	static_cast<void>( category) ;
	static_cast<void>( identifier) ;
	static_cast<void>( location) ;
	static_cast<void>( message) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 固定モデルのmetadataを実装schemaに照合します
 * @param[in] session hash検証済みmodelを開いたworker所有session
 */
//------------------------------------------------------------------------------
void validateModelSchema( const Ort::Session &session)
{
	// 入出力が異なる場合は推測して実行しない
	if( session.GetInputCount() != 1 || session.GetOutputCount() != 1) {
		throw OperationError( ErrorKind::INFERENCE_FAILED) ;
	}
	Ort::AllocatorWithDefaultOptions allocator ; // metadata文字列の専用allocator
	const auto						 metadata = session.GetModelMetadata() ; // RAII metadata
	// segmentation graphへ要求する固定metadata名と値の対応
	constexpr std::array<std::pair<const char *, const char *>, 7> K_EXPECTED{
		{{"sample_rate", "16000"},
		 {"window_size", "160000"},
		 {"receptive_field_size", "991"},
		 {"receptive_field_shift", "270"},
		 {"num_speakers", "3"},
		 {"powerset_max_classes", "2"},
		 {"num_classes", "7"}}} ; // 利用する固定schema
	// 各値を確認し、異なる時刻基準やpowersetを使わない
	for( const auto &[key, value] : K_EXPECTED) {
		// runtimeが所有するmetadata値を専用allocatorで解放する
		const auto actual = metadata.LookupCustomMetadataMapAllocated( key, allocator) ;
		// 欠落や未知schemaを既定値で埋めない
		if( !actual || std::string_view( actual.get()) != value) {
			throw OperationError( ErrorKind::INFERENCE_FAILED) ;
		}
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 検証したpowerset結果を局所話者bitへ変換します
 * @param[in] value 1×589×7のlog probability
 * @return 589frameの所有mask
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::array<std::uint8_t, K_FRAMES> speechMasks( const Ort::Value &value)
{
	// 非tensorを配列pointerで参照しない
	if( !value.IsTensor()) {
		throw OperationError( ErrorKind::INFERENCE_FAILED) ;
	}
	const auto information = value.GetTensorTypeAndShapeInfo() ; // tensorの型とshape
	// 時刻とclass配列の前提を満たす結果だけを使う
	if( information.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
	   information.GetShape() != std::vector<std::int64_t>{1, K_FRAMES, 7}) {
		throw OperationError( ErrorKind::INFERENCE_FAILED) ;
	}
	const float						  *data = value.GetTensorData<float>() ; // value生存中の借用配列
	std::array<std::uint8_t, K_FRAMES> result{} ; // 所有mask列
	// 各frameの最大classを選択する
	for( std::size_t frame = 0; frame < K_FRAMES; ++frame) {
		std::size_t selected = 0 ; // 最大log probabilityのclass
		// 有限な7classを検証し比較する
		for( std::size_t category = 0; category < K_CLASS_MASK.size(); ++category) {
			const float score = data[frame * 7 + category] ; // 比較中のmodel出力
			// 非有限値で比較が不定になる経路を許可しない
			if( !std::isfinite( score)) {
				throw OperationError( ErrorKind::INFERENCE_FAILED) ;
			}
			// 最大値のclassを更新する
			if( score > data[frame * 7 + selected]) {
				selected = category ;
			}
		}
		result[frame] = K_CLASS_MASK[selected] ;
	}
	return result ;
}

//------------------------------------------------------------------------------
/**
 * @brief 局所話者だけが発話したsampleを元PCMから集めます
 * @param[in] audio 現windowのpaddingを除いた部分PCM
 * @param[in] window 元sample位置と局所mask
 * @param[in] speaker 0～2の局所話者
 * @return paddingや同時発話を含まない所有音声copy
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<float> isolatedVoice( const std::span<const float> audio, const SpeakerWindow &window,
											   const int speaker)
{
	std::vector<float> samples ; // embedding抽出までの一時音声
	samples.reserve( K_WINDOW_SAMPLES) ;
	// receptive field中央に対応するcellを順に採用する
	for( std::size_t frame = 0; frame < K_FRAMES; ++frame) {
		// 単独発話だけを声の特徴へ渡す
		if( window.masks[frame] != static_cast<std::uint8_t>( 1U << speaker)) {
			continue ;
		}
		const std::size_t begin =
			frame * K_FRAME_SAMPLES + (K_RECEPTIVE_SAMPLES - K_FRAME_SAMPLES) / 2 ; // cell開始sample
		const std::size_t end = std::min( audio.size(), begin + K_FRAME_SAMPLES) ; // 実終端
		// 最終windowのpaddingを音声の根拠へ数えない
		if( begin >= end) {
			continue ;
		}
		samples.insert( samples.end(), audio.begin() + static_cast<std::ptrdiff_t>( begin),
					   audio.begin() + static_cast<std::ptrdiff_t>( end)) ;
	}
	return samples ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 検証済みモデルとNME-SCで人数と話者区間を推定します
 * @param[in] audio 16kHz mono PCMの所有元。部分読込みし固定時間上限を置きません
 * @param[in] options worker開始時に固定した設定
 * @param[in] stop 同期API境界で確認する取消し状態
 * @param[in] progress 数量だけをworkerへ通知するcallback
 * @return 元音声基準の所有区間。音声根拠がない場合は空
 * @throws OperationError 入力・モデル・数値計算の異常または取消し
 * @note 音声特徴は本呼出し中だけ保持し、UIやlogへ渡しません
 * @see koefude::spectralDiarize
 */
//------------------------------------------------------------------------------
std::vector<SpeakerTurn> spectralDiarize( const AudioData &audio, const EngineOptions &options,
										 const std::stop_token stop, const ProgressCallback &progress)
{
	checkCancelled( stop) ;
	// 録音時間ではなくsample数・thread・人数と診断設定をモデル読込み前に確認する
	if( audio.sampleCount() == 0 || audio.sampleCount() > K_MAXIMUM_TIMELINE_SAMPLES || options.threads < 1 ||
	   options.threads > 16 || options.expectedSpeakers < 0 || options.expectedSpeakers > 20 ||
	   (options.spectralGraphSelection != SpectralGraphSelection::COUPLED &&
		options.spectralGraphSelection != SpectralGraphSelection::INDEPENDENT) ||
	   (options.spectralGraphSelection == SpectralGraphSelection::INDEPENDENT && options.expectedSpeakers != 0)) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// sessionが解放されるまで検証済みmodelの書換えと削除を拒否する所有lock
	const auto segmentation = verifyModel( options.modelRoot, ModelId::SEGMENTATION, stop) ;
	// extractorの生存中だけ保持する特徴modelの所有lock
	const auto embedding = verifyModel( options.modelRoot, ModelId::EMBEDDING, stop) ;
	reportProgress( progress, Stage::DIARIZING, 0) ;
	try {
		// workerの推論中だけ生存し、本文やpathをlogへ流さないruntime環境
		Ort::Env environment( ORT_LOGGING_LEVEL_ERROR, "KoeFude", discardRuntimeLog, nullptr) ;
		environment.DisableTelemetryEvents() ;
		Ort::SessionOptions sessionOptions ; // CPUのみの同期推論設定
		sessionOptions.SetIntraOpNumThreads( options.threads) ;
		sessionOptions.SetInterOpNumThreads( 1) ;
		sessionOptions.SetGraphOptimizationLevel( GraphOptimizationLevel::ORT_ENABLE_ALL) ;
		// 固定modelをCPUで同期実行するworker所有session
		Ort::Session session( environment, segmentation.path.c_str(), sessionOptions) ;
		validateModelSchema( session) ;
		const std::string						  embeddingPath = toUtf8( embedding.path.native()) ; // C API用所有path
		SherpaOnnxSpeakerEmbeddingExtractorConfig extractorOptions{} ; // CPU embedding設定
		extractorOptions.model = embeddingPath.c_str() ;
		extractorOptions.num_threads = options.threads ;
		extractorOptions.provider = "cpu" ;
		const std::unique_ptr<const SherpaOnnxSpeakerEmbeddingExtractor,
							  decltype(&SherpaOnnxDestroySpeakerEmbeddingExtractor)>
			extractor( SherpaOnnxCreateSpeakerEmbeddingExtractor( &extractorOptions),
					  SherpaOnnxDestroySpeakerEmbeddingExtractor) ; // C API所有権
		// 初期化失敗時にNULLへAPIを呼ばない
		if( !extractor) {
			throw OperationError( ErrorKind::INFERENCE_FAILED) ;
		}
		// C APIから借用する特徴配列の要素数
		const int dimensions = SherpaOnnxSpeakerEmbeddingExtractorDim( extractor.get()) ;
		// 有界な特徴schemaだけをnative pointerからcopyする
		if( dimensions < 1 || dimensions > 1024) {
			throw OperationError( ErrorKind::INFERENCE_FAILED) ;
		}
		Ort::AllocatorWithDefaultOptions  allocator ; // 入出力名の専用allocator
		// 推論呼出しより長く生存させる所有入力名
		const auto						  inputName = session.GetInputNameAllocated( 0, allocator) ;
		// 結果取得に使う所有出力名
		const auto						  outputName = session.GetOutputNameAllocated( 0, allocator) ;
		const std::array<const char *, 1> inputNames{inputName.get()} ; // 借用入力名
		const std::array<const char *, 1> outputNames{outputName.get()} ; // 借用出力名
		const std::array<std::int64_t, 3> shape{1, 1, K_WINDOW_SAMPLES} ; // 固定入力shape
		// tensorが借用するbufferをCPU用として示すRAII記述子
		const auto						  memory = Ort::MemoryInfo::CreateCpu( OrtArenaAllocator, OrtMemTypeDefault) ;
		// 終端のpadding windowを含め、処理進捗の分母になる総window数
		const std::size_t				  count =
			audio.sampleCount() <= K_WINDOW_SAMPLES
								? 1
								: (audio.sampleCount() - K_WINDOW_SAMPLES + K_WINDOW_STEP - 1) / K_WINDOW_STEP + 1 ;
		TemporaryFile	   observations ; // 全windowのmask・特徴をRAM外へ順次記録し、終了時に回収
		SpeakerReservoir   reservoir ; // 全録音から最大4096特徴だけをRAMへ保持
		std::vector<float> buffer( K_WINDOW_SAMPLES, 0) ; // paddingを含む所有入力
		// 重なるwindowを順に推論し、同時実行を一つに保つ
		for( std::size_t index = 0; index < count; ++index) {
			checkCancelled( stop) ;
			StoredWindow record{} ; // 一回の推論だけに使う有界な一時記録
			auto		&window = record.window ; // recordが所有する時刻とmask
			window.startSample = index * K_WINDOW_STEP ;
			std::fill( buffer.begin(), buffer.end(), 0.0F) ;
			// paddingを除いて今回のwindowへcopyする実sample数
			const std::size_t copied = static_cast<std::size_t>(
				std::min<std::uint64_t>( K_WINDOW_SAMPLES, audio.sampleCount() - window.startSample)) ;
			audio.readSamples( window.startSample, std::span<float>( buffer).first( copied)) ;
			// 同期Run中に生存するbufferを借用したtensor
			auto input =
				Ort::Value::CreateTensor<float>( memory, buffer.data(), buffer.size(), shape.data(), shape.size()) ;
			const auto output = session.Run( Ort::RunOptions{nullptr}, inputNames.data(), &input, 1, outputNames.data(),
											1) ; // 同期CPU推論結果
			window.masks = speechMasks( output.front()) ;
			// 同時発話を除いた局所話者ごとの音声から特徴を抽出する
			for( int speaker = 0; speaker < 3; ++speaker) {
				checkCancelled( stop) ;
				const auto voice =
					isolatedVoice( std::span<const float>( buffer).first( copied), window, speaker) ; // 一時音声copy
				// 短すぎる声を人数増加の根拠にしない
				if( voice.size() < K_MINIMUM_VOICED_SAMPLES) {
					continue ;
				}
				const std::unique_ptr<const SherpaOnnxOnlineStream, decltype(&SherpaOnnxDestroyOnlineStream)> stream(
					SherpaOnnxSpeakerEmbeddingExtractorCreateStream( extractor.get()),
					SherpaOnnxDestroyOnlineStream) ; // 特徴bufferの所有権
				// 初期化失敗を音声なしに置き換えない
				if( !stream) {
					throw OperationError( ErrorKind::INFERENCE_FAILED) ;
				}
				SherpaOnnxOnlineStreamAcceptWaveform( stream.get(), K_SAMPLE_RATE, voice.data(),
													 static_cast<std::int32_t>( voice.size())) ;
				SherpaOnnxOnlineStreamInputFinished( stream.get()) ;
				// 必要frameが得られない場合は根拠不足として残す
				if( !SherpaOnnxSpeakerEmbeddingExtractorIsReady( extractor.get(), stream.get())) {
					continue ;
				}
				const std::unique_ptr<const float, decltype(&SherpaOnnxSpeakerEmbeddingExtractorDestroyEmbedding)>
					vector( SherpaOnnxSpeakerEmbeddingExtractorComputeEmbedding( extractor.get(), stream.get()),
						   SherpaOnnxSpeakerEmbeddingExtractorDestroyEmbedding) ; // 専用解放責任
				// 不正結果を別話者へ割り当てない
				if( !vector || !std::all_of( vector.get(), vector.get() + dimensions,
					//------------------------------------------------------------------------------
					/**
					 * @brief 話者埋込みの一要素が有限値かを検査します
					 * @param[in] value 専用APIから得た埋込みの一要素
					 * @return NaNでも無限大でもなければtrue
					 */
					//------------------------------------------------------------------------------
					[]( const float value)
					{
						return std::isfinite( value) ;
					})) {
					continue ;
				}
				const auto local = static_cast<std::size_t>( speaker) ; // 現windowの局所枠
				record.ids[local] =
					reservoir.observe( std::span<const float>( vector.get(), static_cast<std::size_t>( dimensions))) ;
				std::copy_n( vector.get(), dimensions, record.features[local].begin()) ;
			}
			observations.append( std::as_bytes( std::span<const StoredWindow>( &record, 1))) ;
			reportProgress( progress, Stage::DIARIZING, static_cast<int>( (index + 1) * 75 / count)) ;
		}
		// 声の特徴を外部へ渡さずに得た、このfile内だけの全体label
		const auto clusters =
			clusterSpeakers( reservoir.features(), options.expectedSpeakers, stop, options.spectralGraphSelection) ;
		reportProgress( progress, Stage::DIARIZING, 90) ;
		// 無音や根拠不足の音声に話者区間を追加しない
		if( clusters.count == 0) {
			reportProgress( progress, Stage::DIARIZING, 100) ;
			return {} ;
		}
		const SpeakerClassifier classifier( reservoir,
										   clusters) ; // 全録音で同じclusterを使う対応表
		SpeakerTimeline			timeline( audio.sampleCount(), clusters.count) ; // 589frame以内の逐次投票
		// 全観測を読み戻し、代表外の特徴にも音響類似度で共通clusterを割り当てる
		for( std::size_t index = 0; index < count; ++index) {
			checkCancelled( stop) ;
			StoredWindow record{} ; // 一回分だけの所有読込み先
			observations.read( static_cast<std::uint64_t>( index) * sizeof(StoredWindow),
							  std::as_writable_bytes( std::span<StoredWindow>( &record, 1))) ;
			// 音響根拠を得られた局所枠だけを全体話者へ戻す
			for( std::size_t local = 0; local < 3; ++local) {
				// 無音・短すぎる声には未知labelを維持する
				if( record.ids[local] != UINT64_MAX) {
					record.window.speakers[local] = classifier.classify(
						record.ids[local],
						std::span<const float>( record.features[local]).first( static_cast<std::size_t>( dimensions))) ;
				}
			}
			timeline.add( record.window) ;
			reportProgress( progress, Stage::DIARIZING, 90 + static_cast<int>( (index + 1) * 10 / count)) ;
		}
		auto turns = timeline.finish() ; // 元音声の全時刻を保持する有界な結果
		checkCancelled( stop) ;
		reportProgress( progress, Stage::DIARIZING, 100) ;
		return turns ;
	} catch( const Ort::Exception &error) {
		// path入りの例外説明を公開せず、失敗codeを共通結果へ渡す
		throw OperationError( ErrorKind::INFERENCE_FAILED, static_cast<std::uint32_t>( error.GetOrtErrorCode())) ;
	}
}

} // namespace koefude
