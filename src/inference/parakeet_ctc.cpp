//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file parakeet_ctc.cpp
 * @brief 固定前処理とORTでParakeet CTCのスコアを直接取得します
 * @version 1.0.0
 * @date 2026/09/11
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/11 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/11 greedyの再現と複数候補比較に必要なnative境界を追加
 * 0.1.0 Shunji Ogawa 2026/09/12 用語比較へ既存本文語彙を同じworkerで借用する境界を追加
 * 0.1.0 Shunji Ogawa 2026/09/20 従来の有効長を維持し全shapeを観測する診断境界を追加
 * 0.1.0 Shunji Ogawa 2026/09/20 同じ計算のlogと正規化値、別抽出の線形melを観測
 * 0.1.0 Shunji Ogawa 2026/09/20 正規化演算だけをfloat64へ変更する明示比較を追加
 * 0.1.0 Shunji Ogawa 2026/09/20 旧正規化を維持してlogの加算floorだけを比較
 * 0.1.0 Shunji Ogawa 2026/09/20 窓中心だけを80から0 sampleへ移す明示比較を追加
 * 0.1.0 Shunji Ogawa 2026/09/21 Hann窓の周期性だけを変更する明示比較を追加
 * 0.1.0 Shunji Ogawa 2026/10/07 限定CPU観測の手動API初期化をEnv作成より前へ分離
 * 0.1.0 Shunji Ogawa 2026/10/19 宣言の役割を明記。改行をLFへ統一
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実使用の標準headerを直接include
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 * @note 前処理の条件はADR 0031の固定sherpa offline経路へ合わせます
 */
//------------------------------------------------------------------------------
#include "inference/parakeet_ctc.h"

#include <span>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

#include <Eigen/Core>
#include <kaldi-native-fbank/csrc/feature-fbank.h>
#include <kaldi-native-fbank/csrc/feature-window.h>
#include <kaldi-native-fbank/csrc/online-feature.h>
#include <onnxruntime_cxx_api.h>

#include "core/asr_tokens.h"
#if !defined(KOEFUDE_LIMITED_CTC_CPU)
#include "inference/cuda_device.h"
#endif
#include "inference/gpu_budget.h"
#include "inference/model_store.h"
#include "platform/win_helpers.h"
#include "platform/word_boundaries.h"

namespace koefude {
namespace {

constexpr std::size_t	K_MEL_BINS = 80 ; // 固定graphの周波数次元
constexpr std::size_t	K_VOCABULARY = 3073 ; // 日本語token 3072とblank
constexpr std::int32_t	K_BLANK = 3072 ; // 同じregistryのtokens.txtで照合するblank ID
constexpr std::size_t	K_MAXIMUM_WINDOW_SAMPLES = 30 * K_SAMPLE_RATE ; // 比較窓の元PCM上限
constexpr std::size_t	K_MINIMUM_WINDOW_SAMPLES = 1280 ; // 少なくとも有効出力1frameを持つ80ms
constexpr std::uint64_t K_COMPARISON_GPU_BYTES = 8 * K_GI_B ; // 新経路の明示比較で確保する保守的予算

//------------------------------------------------------------------------------
/**
 * @brief 第三者のpath入りlogを公開せずAPI errorだけを保持します
 * @param[in] parameter 未使用context
 * @param[in] severity log重要度
 * @param[in] category 分類
 * @param[in] identifier 識別子
 * @param[in] location source位置
 * @param[in] message 内容を保存しない説明
 */
//------------------------------------------------------------------------------
void ORT_API_CALL discardCtcLog( void *parameter, OrtLoggingLevel severity, const char *category, const char *identifier,
								const char *location, const char *message) noexcept
{
	static_cast<void>( parameter) ;
	static_cast<void>( severity) ;
	static_cast<void>( category) ;
	static_cast<void>( identifier) ;
	static_cast<void>( location) ;
	static_cast<void>( message) ;
}

#if defined(KOEFUDE_LIMITED_CTC_CPU)
//------------------------------------------------------------------------------
/**
 * @brief 限定観測の安全境界後にだけC++ APIと環境を初期化します
 * @return 同じworkerがsessionより後に解放する環境
 * @throws OperationError C API baseまたは要求版が取得できない場合
 * @pre 呼出し元が実DLLの絶対path/SHA/版/APIを検証済みであること
 * @note 専用targetは手動初期化でcompileし、ORTをdelay importします
 */
//------------------------------------------------------------------------------
[[nodiscard]] Ort::Env limited_ctc_environment()
{
	const auto *base = OrtGetApiBase() ; // 検証済み実moduleへのdelay import
	// C++ wrapperへNULLや不正なcallbackを渡さない
	if( !base || !base->GetApi || !base->GetVersionString) {
		throw OperationError( ErrorKind::InferenceFailed) ;
	}
	const auto *api = base->GetApi( ORT_API_VERSION) ; // headerと同じ版だけを要求
	// 未対応APIをEnvのconstructorで参照させない
	if( !api) {
		throw OperationError( ErrorKind::InferenceFailed) ;
	}
	Ort::InitApi( api) ;
	return Ort::Env( ORT_LOGGING_LEVEL_ERROR, "koefude-ctc", discard_ctc_log, nullptr) ;
}
#endif

//------------------------------------------------------------------------------
/**
 * @brief registryで検証した3073語彙を順序付きの所有列へ読みます
 * @param[in] verified 使用中の変更を禁止したtoken file
 * @return sherpaと同じ先頭空白処理を行った所有token列
 * @throws OperationError 語彙形式、重複、件数、blankの不一致
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<std::string> readCtcTokens( const VerifiedModel &verified)
{
	std::ifstream			 stream( verified.path, std::ios::binary) ; // 検証と同じfileを借用する入力stream
	std::vector<std::string> tokens( K_VOCABULARY) ; // IDで引く固定件数の所有語彙
	std::string				 line ; // registryのsize検証を通った一行
	std::size_t				 count = 0 ; // 重複や不足を検出する通常token数
	// 全行を順序に依存せず読み、正しいIDへ一度だけ格納する
	while( std::getline( stream, line)) {
		// CRLFの終端だけを取り除き、本文内の空白を勝手に正規化しない
		if( !line.empty() && line.back() == '\r') {
			line.pop_back() ;
		}
		const auto separator = line.rfind( ' ') ; // 最後のASCII空白がtokenとIDの境界
		// 巨大・空・separator不足の語彙は外部形式不正とする
		if( line.size() > 256 || separator == std::string::npos || separator == 0) {
			throw OperationError( ErrorKind::MODEL_REJECTED) ;
		}
		std::int32_t id = -1 ; // 全桁を読んでから利用するID
		const auto	 parsed = std::from_chars( line.data() + separator + 1, line.data() + line.size(),
											  id) ; // ASCII十進ID
		// 同じIDへの上書きや語彙数超過を禁止する
		if( parsed.ec != std::errc{} || parsed.ptr != line.data() + line.size() || id < 0 ||
		   static_cast<std::size_t>( id) >= tokens.size() || !tokens[id].empty()) {
			throw OperationError( ErrorKind::MODEL_REJECTED) ;
		}
		auto piece = line.substr( 0, separator) ; // 本文として所有するtoken
		// SentencePieceの語頭記号だけを固定sherpaと同じASCII空白へ戻す
		if( piece.starts_with( "\xe2\x96\x81")) {
			piece.replace( 0, 3, " ") ;
		}
		tokens[id] = std::move( piece) ;
		++count ;
	}
	// 読込失敗や欠落を空語彙として成功させない
	if( !stream.eof() || count != K_VOCABULARY || tokens[K_BLANK] != "<blk>") {
		throw OperationError( ErrorKind::MODEL_REJECTED) ;
	}
	return tokens ;
}

//------------------------------------------------------------------------------
/**
 * @brief 窓中心だけを明示し、同じknfの窓処理・FFT・melを実行します
 * @param[in] samples 検証済み1280〜480000 sampleの借用PCM
 * @param[in] options 現行と同じ窓形状・log等の固定条件
 * @param[in] center 最初の中心sample。旧対照80または候補0
 * @param[in] stop 各frameの前に確認する取消し
 * @return frame優先の所有mel/log列。失敗時は部分結果を返しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<float> centeredFbank( const std::span<const float> samples, const knf::FbankOptions &options,
											   const std::int32_t center, const std::stop_token stop)
{
	// 未定義の中心や窓設定を固定比較へ混入させない
	if( (center != 0 && center != 80) || options.frame_opts.WindowSize() != 400 ||
	   options.frame_opts.WindowShift() != 160 || options.frame_opts.PaddedWindowSize() != 512) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	const auto						 size = static_cast<std::int32_t>( samples.size()) ; // 検証済みの元窓長
	const auto						 count = (size + 80) / 160 ; // 旧round規則を中心から独立して維持
	std::vector<float>				 result( static_cast<std::size_t>( count) * K_MEL_BINS) ; // 所有する全frame
	std::vector<float>				 window( 512, 0) ; // FFTが上書きする一窓の所有作業領域
	const knf::FeatureWindowFunction windowFunction( options.frame_opts) ; // 同じ周期hann
	knf::FbankComputer				 computer( options) ; // 一窓ずつ処理する同じFFT・mel実装
	// 有効frame数だけ進み、PCMを変更せずreflectionの参照位置だけを移す
	for( std::int32_t frame = 0; frame < count; ++frame) { // 出力のframe番号
		checkCancelled( stop) ;
		std::fill( window.begin(), window.end(), 0.0F) ; // 前回のFFT値を末尾paddingへ残さない
		// 400 sampleを元PCMへ写像し、端点外でも同じreflectionを維持する
		for( std::int32_t offset = 0; offset < 400; ++offset) { // 窓内のsample位置
			auto source = frame * 160 + center - 200 + offset ; // 元PCM基準の候補位置
			// 左端では-1を0へ写す、端点を繰り返すreflectionを使う
			if( source < 0) {
				source = -source - 1 ;
			}
			// 右端でも同じ規則を使う。最小1280 sampleなので再反射は不要
			if( source >= size) {
				source = 2 * size - source - 1 ;
			}
			window[static_cast<std::size_t>( offset)] = samples[static_cast<std::size_t>( source)] ;
		}
		knf::ProcessWindow( options.frame_opts, windowFunction, window.data(), nullptr) ;
		computer.Compute( 0.0F, 1.0F, &window, result.data() + static_cast<std::size_t>( frame) * K_MEL_BINS) ;
	}
	return result ;
}

//------------------------------------------------------------------------------
/**
 * @brief 元PCMを変更せず固定sherpaのoffline特徴と同じ順序で正規化します
 * @param[in] samples 検証済み16kHz mono PCM。最大30秒
 * @param[in] stop 特徴生成の前後で確認する取消し
 * @param[out] observation 非NULLなら診断値を所有copy。失敗時は未完了で利用不可
 * @param[in] useFloat64 正規化だけをdoubleで計算して最後にfloatへ戻す場合true
 * @param[in] useAdditiveLog logだけを加算floorへ変更する場合true
 * @param[in] windowCenter -1は現行OnlineFbank、0/80は中心を明示する開発比較
 * @param[in] symmetricHann 窓形状だけ非周期Hannにする場合true
 * @return 80×frameのチャンネル優先で所有するfloat32特徴
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<float> ctcFeatures( const std::span<const float> samples, const std::stop_token stop,
											 ParakeetFeatureObservation *observation = nullptr,
											 const bool useFloat64 = false, const bool useAdditiveLog = false,
											 const std::int32_t windowCenter = -1, const bool symmetricHann = false)
{
	knf::FbankOptions options ; // 固定knf 1.22.3のoffline Fbankと同じ既定条件
	options.frame_opts.dither = 0 ;
	options.frame_opts.snip_edges = false ;
	options.frame_opts.samp_freq = K_SAMPLE_RATE ;
	options.frame_opts.frame_shift_ms = 10 ;
	options.frame_opts.frame_length_ms = 25 ;
	options.frame_opts.remove_dc_offset = false ;
	options.frame_opts.preemph_coeff = 0.97F ;
	options.frame_opts.window_type = symmetricHann ? "hanning" : "hann" ;
	options.mel_opts.num_bins = static_cast<std::int32_t>( K_MEL_BINS) ;
	options.mel_opts.high_freq = 0 ;
	options.mel_opts.low_freq = 0 ;
	options.mel_opts.is_librosa = true ;
	options.use_log_fbank = !useAdditiveLog ;
	knf::OnlineFbank   extractor( options) ; // 一つの窓だけで所有する特徴buffer
	std::vector<float> centeredFrames ; // 明示比較時だけ所有するframe優先列
	checkCancelled( stop) ;
	// 明示中心では切出しだけを変更し、旧経路は従来の呼出しを維持する
	if( windowCenter >= 0) {
		centeredFrames = centeredFbank( samples, options, windowCenter, stop) ;
	} else {
		extractor.AcceptWaveform( K_SAMPLE_RATE, samples.data(), static_cast<std::int32_t>( samples.size())) ;
		extractor.InputFinished() ;
	}
	checkCancelled( stop) ;
	const auto count = windowCenter >= 0 ? static_cast<std::int32_t>( centeredFrames.size() / K_MEL_BINS)
										 : extractor.NumFramesReady() ; // 同じ有効frame数
	// 外部の不正件数でallocationやframe参照を始めない
	if( count < 8 || count > 3001) {
		throw OperationError( ErrorKind::INFERENCE_FAILED) ;
	}
	using FeatureMatrix = Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> ;
	// 固定sherpaの正規化と同じframe優先の行列
	FeatureMatrix features( count, K_MEL_BINS) ; // 有界な一窓の特徴行列
	// 全frameを一度ずつcopyし、第三者objectの寿命を正規化の外へ漏らさない
	for( std::int32_t frame = 0; frame < count; ++frame) { // 特徴frame番号
		checkCancelled( stop) ;
		const float *source = windowCenter >= 0 ? centeredFrames.data() + static_cast<std::size_t>( frame) * K_MEL_BINS
												: extractor.GetFrame( frame) ; // copy中だけ借用する一frame
		std::copy_n( source, K_MEL_BINS, features.data() + static_cast<std::size_t>( frame) * K_MEL_BINS) ;
	}
	// 加算floorの比較では同じ線形melにだけ別のlog式を適用する
	if( useAdditiveLog) {
		constexpr float K_LOG_FLOOR = 0x1p-24F ; // 固定参照器の加算floor
		// 時刻順の各melへfloat加算とfloat logを適用し、他の前処理を混ぜない
		for( Eigen::Index index = 0; index < features.size(); ++index) { // 所有行列の要素
			const float value = features.data()[index] ; // log前の非負mel
			// 不正値をlogや正規化へ渡さない
			if( !std::isfinite( value) || value < 0) {
				throw OperationError( ErrorKind::INFERENCE_FAILED) ;
			}
			features.data()[index] = std::log( value + K_LOG_FLOOR) ;
		}
	}
	// 明示した診断時だけ追加抽出し、通常推論へ観測bufferを持ち込まない
	if( observation != nullptr) {
		observation->frames = static_cast<std::size_t>( count) ;
		observation->mel.resize( observation->frames * K_MEL_BINS) ;
		observation->logMel.resize( observation->frames * K_MEL_BINS) ;
		options.use_log_fbank = false ;
		knf::OnlineFbank   linear( options) ; // logだけを無効にした同じmel計算
		std::vector<float> centeredLinear ; // 同じ中心・logなしの診断列
		// 線形mel観測にも同じ中心だけを適用する
		if( windowCenter >= 0) {
			centeredLinear = centeredFbank( samples, options, windowCenter, stop) ;
		} else {
			linear.AcceptWaveform( K_SAMPLE_RATE, samples.data(), static_cast<std::int32_t>( samples.size())) ;
			linear.InputFinished() ;
		}
		checkCancelled( stop) ;
		// 別抽出の有効長が一致しない場合は対応を推測しない
		if( (windowCenter >= 0 ? static_cast<std::int32_t>( centeredLinear.size() / K_MEL_BINS)
							  : linear.NumFramesReady()) != count) {
			throw OperationError( ErrorKind::INFERENCE_FAILED) ;
		}
		// 全frame・全binを所有copyし、有限性と線形melの非負を確認する
		for( std::int32_t frame = 0; frame < count; ++frame) { // 同じ位置のframe
			checkCancelled( stop) ;
			// frame優先の抽出器からgraphと同じbin優先順へ移す
			for( std::size_t bin = 0; bin < K_MEL_BINS; ++bin) { // 周波数bin
				// 観測配列の所有要素の位置
				const auto	index = bin * observation->frames + static_cast<std::size_t>( frame) ;
				const float value = windowCenter >= 0
										? centeredLinear[static_cast<std::size_t>( frame) * K_MEL_BINS + bin]
										: linear.GetFrame( frame)[bin] ; // log前の同じ中心のmel
				const float logged = features( frame, static_cast<Eigen::Index>( bin)) ; // 実log
				// 不正値を参照器との差として採点しない
				if( !std::isfinite( value) || value < 0 || !std::isfinite( logged)) {
					throw OperationError( ErrorKind::INFERENCE_FAILED) ;
				}
				observation->mel[index] = value ;
				observation->logMel[index] = logged ;
			}
		}
	}
	// 明示比較時だけ演算精度を変え、mel・logや有効frame数は共有する
	if( useFloat64) {
		constexpr double K_EPSILON = static_cast<double>( 1e-5F) ; // 元float32定数の値を保持
		const double	 divisor = static_cast<double>( count) ; // 従来と同じ分散分母T
		// 周波数ごとに時刻順で集計し、他binの更新値を参照しない
		for( Eigen::Index bin = 0; bin < static_cast<Eigen::Index>( K_MEL_BINS); ++bin) {
			// 現在の周波数bin
			checkCancelled( stop) ;
			double sum = 0 ; // 全frameのdouble和
			// float32の元logを拡張して平均を計算する
			for( std::int32_t frame = 0; frame < count; ++frame) { // 時刻順のframe番号
				sum += static_cast<double>( features( frame, bin)) ;
			}
			const double mean = sum / divisor ; // 逆数の乗算を避ける直接除算の平均
			double		 squares = 0 ; // 平均差の二乗和
			// 同じ元logから中心化分散を集計し、分母はTのままにする
			for( std::int32_t frame = 0; frame < count; ++frame) { // 分散へ加えるframe
				// 桁落ちを避ける平均からの差
				const double centered = static_cast<double>( features( frame, bin)) - mean ;
				squares += centered * centered ;
			}
			// 定数binも同じ式で扱い、正規化の分母を0にしない
			const double denominator = std::sqrt( squares / divisor) + K_EPSILON ;
			// 一つのbinを正規化し、ONNXへ渡す前にfloat32へ戻す
			for( std::int32_t frame = 0; frame < count; ++frame) { // 出力先のframe番号
				features( frame, bin) =
					static_cast<float>( (static_cast<double>( features( frame, bin)) - mean) / denominator) ;
			}
		}
	} else {
		// 旧経路の式と評価順序を維持して対照を再現する
		const Eigen::RowVectorXf mean = features.colwise().mean() ; // 固定順序の周波数別平均
		const Eigen::RowVectorXf variance =
			(features.rowwise() - mean).array().square().colwise().mean() ; // 中心化分散
		const Eigen::RowVectorXf inverse = (variance.array().sqrt() + 1e-5F).inverse() ; // 固定sherpaと同じepsilon
		features.array() = (features.array().rowwise() - mean.array()).rowwise() * inverse.array() ;
	}
	std::vector<float> result( static_cast<std::size_t>( count) * K_MEL_BINS) ; // graphへ渡す80×T
	// graphの入力順へtransposeしながら有限性を検証する
	for( std::size_t bin = 0; bin < K_MEL_BINS; ++bin) { // 周波数bin
		// 各周波数のframeを開始順に配置する
		for( std::int32_t frame = 0; frame < count; ++frame) { // 当該周波数の時刻
			const float value = features( frame, static_cast<Eigen::Index>( bin)) ; // 正規化済み特徴
			// 無音や極小分散で非有限値を生じた場合は推論へ渡さない
			if( !std::isfinite( value)) {
				throw OperationError( ErrorKind::INFERENCE_FAILED) ;
			}
			result[bin * static_cast<std::size_t>( count) + static_cast<std::size_t>( frame)] = value ;
		}
	}
	return result ;
}

//------------------------------------------------------------------------------
/**
 * @brief 固定graphの入力名とshapeを照合します
 * @param[in] session 検証済みgraphの所有session
 * @throws OperationError 契約に合わないgraph metadata
 */
//------------------------------------------------------------------------------
void validateCtcSchema( const Ort::Session &session)
{
	// 入出力の数が変わったgraphを推測で利用しない
	if( session.GetInputCount() != 2 || session.GetOutputCount() != 1) {
		throw OperationError( ErrorKind::MODEL_REJECTED) ;
	}
	Ort::AllocatorWithDefaultOptions allocator ; // runtimeの文字列を解放するallocator
	const auto						 audioName = session.GetInputNameAllocated( 0, allocator) ; // 音声特徴のAPI名
	const auto						 lengthName = session.GetInputNameAllocated( 1, allocator) ; // 有効frame長のAPI名
	const auto						 outputName = session.GetOutputNameAllocated( 0, allocator) ; // CTC出力のAPI名
	const auto						 audioType = session.GetInputTypeInfo( 0) ; // 借用tensor情報より長く保つ型情報
	const auto						 lengthType = session.GetInputTypeInfo( 1) ; // int64長の型情報
	const auto						 outputType = session.GetOutputTypeInfo( 0) ; // 出力型情報
	const auto						 audio = audioType.GetTensorTypeAndShapeInfo() ; // 入力の借用shape
	const auto						 length = lengthType.GetTensorTypeAndShapeInfo() ; // 長さの借用shape
	const auto						 output = outputType.GetTensorTypeAndShapeInfo() ; // 出力の借用shape
	const auto						 audioShape = audio.GetShape() ; // 検証中に所有する次元列
	const auto						 outputShape = output.GetShape() ; // 出力の語彙数を含む次元列
	// 型と次元の全条件を満たした固定モデルだけに入力bufferを渡す
	if( std::string_view( audioName.get()) != "audio_signal" || std::string_view( lengthName.get()) != "length" ||
	   std::string_view( outputName.get()) != "logprobs" ||
	   audio.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || audioShape.size() != 3 ||
	   audioShape[1] != K_MEL_BINS || length.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64 ||
	   length.GetShape().size() != 1 || output.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
	   outputShape.size() != 3 || outputShape[2] != K_VOCABULARY) {
		throw OperationError( ErrorKind::MODEL_REJECTED) ;
	}
}

} // namespace

namespace {

//------------------------------------------------------------------------------
/**
 * @brief 両精度の観測を同じ入力検証と所有権で実行します
 * @param[in] samples 呼出し中だけ借用する一窓のPCM
 * @param[in] stop 抽出前後とframe処理中の取消し
 * @param[in] useFloat64 正規化だけdoubleで計算する場合true
 * @param[in] useAdditiveLog logだけを加算floorにする場合true
 * @param[in] windowCenter -1は旧経路、0/80は明示中心の比較
 * @param[in] symmetricHann 窓形状だけ非周期Hannにする場合true
 * @return 成功時だけ返す所有観測
 */
//------------------------------------------------------------------------------
[[nodiscard]] ParakeetFeatureObservation observeFeatures( const std::span<const float> samples,
														 const std::stop_token stop, const bool useFloat64,
														 const bool			useAdditiveLog = false,
														 const std::int32_t windowCenter = -1,
														 const bool			symmetricHann = false)
{
	checkCancelled( stop) ;
	// 同じ入力予算を適用してから第三者の抽出器へ渡す
	if( samples.size() < K_MINIMUM_WINDOW_SAMPLES || samples.size() > K_MAXIMUM_WINDOW_SAMPLES) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// 非有限と正規化範囲外を処理開始前に検出する
	for( const float value : samples) { // 入力の一sample
		// 不正PCMは特徴量を一部だけ返さず明示失敗にする
		if( !std::isfinite( value) || std::abs( value) > 1.0F) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
	}
	ParakeetFeatureObservation result ; // 成功後にだけ所有権を返す診断値
	result.normalized = ctcFeatures( samples, stop, &result, useFloat64, useAdditiveLog, windowCenter, symmetricHann) ;
	checkCancelled( stop) ;
	return result ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief モデルを読まず、実推論と同じ前処理の全値を観測します
 * @param[in] samples 呼出し中だけ借用する16kHz mono PCM。有限の[-1,1]で1280〜480000 sample
 * @param[in] stop 抽出前後とframe処理中に確認する取消し
 * @return 一窓だけの所有配列。失敗時は部分観測を返しません
 * @throws OperationError 不正入力・非有限出力・取消し
 * @note 開発診断専用です。製品の前処理設定を変更しません
 * @see koefude::observeParakeetFeatures
 */
//------------------------------------------------------------------------------
ParakeetFeatureObservation observeParakeetFeatures( const std::span<const float> samples, const std::stop_token stop)
{
	return observeFeatures( samples, stop, false) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 正規化の演算だけをfloat64へ変えた開発用特徴量を観測します
 * @param[in] samples 呼出し中だけ借用する有限な[-1,1]の16kHz mono PCM。1280〜480000 sample
 * @param[in] stop 抽出前後と正規化中に確認する取消し
 * @return 最終値をfloat32へ戻した所有観測。失敗時に部分値を返しません
 * @throws OperationError 不正入力・非有限値・取消し
 * @note 分散分母Tと1e-5Fの値、mel/log・窓の全設定を維持する比較専用APIです
 * @see koefude::observeParakeetFeaturesFloat64
 */
//------------------------------------------------------------------------------
ParakeetFeatureObservation observeParakeetFeaturesFloat64( const std::span<const float> samples,
														  const std::stop_token		   stop)
{
	return observeFeatures( samples, stop, true) ;
}

/** @brief model lockとORT sessionを正しい破棄順序で所有する実装です */
class ParakeetCtcSession::Impl {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief registry検証後に一回のCPU/CUDA sessionを準備します
	 * @param[in] options 固定モデルの設定
	 * @param[in] useTf32 明示したCUDAの計算条件
	 * @param[in] stop 構築前後の取消し
	 */
	//------------------------------------------------------------------------------
	Impl( const EngineOptions &options, const bool useTf32, const std::stop_token stop)
#if defined(KOEFUDE_LIMITED_CTC_CPU)
        : environment_( limited_ctc_environment())
#else
        : environment_( ORT_LOGGING_LEVEL_ERROR, "koefude-ctc", discardCtcLog, nullptr)
#endif
    {
		checkCancelled( stop) ;
		// 未選定のモデルや暗黙の装置選択を比較経路へ混ぜない
		if( options.asrModel != AsrModel::PARAKEET || options.asrProfile != AsrProfile::QUALITY ||
		   (options.backend != Backend::CPU && options.backend != Backend::CUDA) || options.threads < 1 ||
		   options.threads > 16) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
		const auto &definition = asrDefinition( options.asrModel, options.asrProfile) ; // 単一registry
		// graphとweightの組が固定されていることをAPI境界でも確認する
		if( !definition.tokensModel || !definition.externalDataModel || definition.modelId != ModelId::PARAKEET_FLOAT) {
			throw OperationError( ErrorKind::MODEL_REJECTED) ;
		}
		model_ = verifyModel( options.modelRoot, definition.modelId.value(), stop) ;
		external_ = verifyModel( options.modelRoot, *definition.externalDataModel, stop) ;
		tokenModel_ = verifyModel( options.modelRoot, *definition.tokensModel, stop) ;
		tokens = readCtcTokens( tokenModel_) ;
		Ort::SessionOptions sessionOptions ; // 固定sherpaと同じthread数・既定graph最適化
		sessionOptions.SetIntraOpNumThreads( options.threads) ;
		sessionOptions.SetInterOpNumThreads( options.threads) ;
		sessionOptions.SetLogSeverityLevel( 3) ;
		providerSettings = "CPUExecutionProvider;threads=" + std::to_string( options.threads) ;
		// 明示したGPUだけで予算とproviderを照合し、失敗時にCPUへ静かに戻らない
		if( options.backend == Backend::CUDA) {
#if defined(KOEFUDE_LIMITED_CTC_CPU)
            // 限定CPU targetに未使用の旧CUDA/ORT objectを混在させない
			static_cast<void>( use_tf32) ;
			throw OperationError( ErrorKind::GpuUnavailable) ;
#else
            const auto device = queryCudaDevice() ; // device 0と同じadapterの実予算
			// 未検証経路の比較は8GiBに加え1GiBの余裕を要求する
			if( !canUseCuda( device, Backend::CUDA, K_COMPARISON_GPU_BYTES, false)) {
				throw OperationError( ErrorKind::GPU_UNAVAILABLE) ;
			}
			const auto providers = Ort::GetAvailableProviders() ; // 利用可能な実runtimeのEP一覧
			// runtime自体がCUDAを持たない場合にGPU成功と記録しない
			if( std::find( providers.begin(), providers.end(), "CUDAExecutionProvider") == providers.end()) {
				throw OperationError( ErrorKind::GPU_UNAVAILABLE) ;
			}
			Ort::CUDAProviderOptions						   cuda ; // V2 APIの所有provider設定
			const std::unordered_map<std::string, std::string> settings{
				{"device_id", "0"},
				{"cudnn_conv_algo_search", "HEURISTIC"},
				{"use_tf32", useTf32 ? "1" : "0"}} ; // 既存経路との差を明示する設定
			cuda.Update( settings) ;
			sessionOptions.AppendExecutionProvider_CUDA_V2( *cuda) ;
			providerSettings = "CUDAExecutionProvider;" + cuda.GetCUDAProviderOptionsAsString() ;
#endif
        }
		checkCancelled( stop) ;
		session_ = std::make_unique<Ort::Session>( environment_, model_.path.c_str(), sessionOptions) ;
		validateCtcSchema( *session_) ;
		checkCancelled( stop) ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 検証済みPCM窓の従来有効長または全shapeを所有copyします
	 * @param[in] samples 呼出し中だけ借用するPCM
	 * @param[in] firstSample 元fileの位置
	 * @param[in] stop 同期推論前後の取消し
	 * @param[in] observeTail 未判定の末尾を含む全shapeを観測する場合true
	 * @param[in] useFloat64 正規化だけdoubleで計算する明示比較ならtrue
	 * @param[in] useAdditiveLog logだけを加算floorにする明示比較ならtrue
	 * @param[in] windowCenter -1は現行、0は窓中心だけを変える候補
	 * @param[in] symmetricHann 窓形状だけ非周期Hannにする場合true
	 * @return 一窓だけの所有確率
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] ParakeetCtcOutput infer( const std::span<const float> samples, const std::uint64_t firstSample,
										  const std::stop_token stop, const bool observeTail,
										  const bool useFloat64 = false, const bool useAdditiveLog = false,
										  const std::int32_t windowCenter = -1, const bool symmetricHann = false)
	{
		checkCancelled( stop) ;
		// 境界を検証してから前処理のallocationと整数変換を行う
		if( samples.size() < K_MINIMUM_WINDOW_SAMPLES || samples.size() > K_MAXIMUM_WINDOW_SAMPLES ||
		   firstSample > K_MAXIMUM_TIMELINE_SAMPLES || samples.size() > K_MAXIMUM_TIMELINE_SAMPLES - firstSample) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
		// 元音声の全sampleを確認し、型が正しいだけの不正な入力を通さない
		for( const float value : samples) { // 現在の元音声sample
			// NaNと正規化範囲外をORTへ渡さない
			if( !std::isfinite( value) || std::abs( value) > 1.0F) {
				throw OperationError( ErrorKind::INVALID_INPUT) ;
			}
		}
		// 一窓の80×T特徴を所有し、明示した一要因だけを変更する
		auto features = ctcFeatures( samples, stop, nullptr, useFloat64, useAdditiveLog, windowCenter, symmetricHann) ;
		std::int64_t length = static_cast<std::int64_t>( features.size() / K_MEL_BINS) ; // 入力frame長
		const std::array<std::int64_t, 3> shape{1, K_MEL_BINS, length} ; // 固定graph入力
		const std::array<std::int64_t, 1> lengthShape{1} ; // batch 1の有効長
		const auto memory = Ort::MemoryInfo::CreateCpu( OrtArenaAllocator, OrtMemTypeDefault) ; // CPU借用
		// Runが戻るまで特徴とlengthの所有変数を保持する借用tensor
		std::array<Ort::Value, 2> inputs{
			Ort::Value::CreateTensor<float>( memory, features.data(), features.size(), shape.data(), shape.size()),
			Ort::Value::CreateTensor<std::int64_t>( memory, &length, 1, lengthShape.data(), lengthShape.size())} ;
		constexpr std::array<const char *, 2> K_INPUTS{"audio_signal", "length"} ; // 検証済みAPI名
		constexpr std::array<const char *, 1> K_OUTPUTS{"logprobs"} ; // 検証済み出力名
		checkCancelled( stop) ;
		const auto outputs = session_->Run( Ort::RunOptions{nullptr}, K_INPUTS.data(), inputs.data(), inputs.size(),
										   K_OUTPUTS.data(), K_OUTPUTS.size()) ; // batch 1
		checkCancelled( stop) ;
		// 不正objectをfloat pointerとして参照しない
		if( outputs.size() != 1 || !outputs[0].IsTensor()) {
			throw OperationError( ErrorKind::INFERENCE_FAILED) ;
		}
		const auto information = outputs[0].GetTensorTypeAndShapeInfo() ; // 所有出力のmetadata
		const auto outputShape = information.GetShape() ; // 実frame数と語彙数
		const auto validFrames = static_cast<std::size_t>( length / 8) ; // 固定sherpaの有効長
		// 従来の切り出し長が存在し、全shapeも有界なfloat32であることを確認する
		if( information.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || outputShape.size() != 3 ||
		   outputShape[0] != 1 || outputShape[1] < static_cast<std::int64_t>( validFrames) ||
		   outputShape[1] > static_cast<std::int64_t>( K_MAXIMUM_CTC_FRAMES) || outputShape[2] != K_VOCABULARY ||
		   validFrames == 0 || validFrames > K_MAXIMUM_CTC_FRAMES) {
			throw OperationError( ErrorKind::INFERENCE_FAILED) ;
		}
		const float		 *values = outputs[0].GetTensorData<float>() ; // 所有outputの寿命内で借用
		ParakeetCtcOutput result ; // 明示された範囲だけを所有する返却DTO
		result.featureFrames = static_cast<std::size_t>( length) ;
		result.frames = observeTail ? static_cast<std::size_t>( outputShape[1]) : validFrames ;
		result.firstSample = firstSample ;
		result.sampleCount = samples.size() ;
		result.logProbabilities.assign( values, values + result.frames * K_VOCABULARY) ;
		// decoderと同じ境界で全語彙の有限性・確率和を確認し、異常は返す前に検出する
		static_cast<void>( decodeCtcGreedy( result.view(), stop)) ;
		return result ;
	}

	/// model lockと同じrevisionの所有token列
	std::vector<std::string> tokens ;
	/// 実際に渡した設定。path・本文・tokenは含まない
	std::string				 providerSettings ;

	private:
	/// sessionより後まで保持するgraph lock
	VerifiedModel				  model_ ;
	/// graphが参照する外部weightのlock
	VerifiedModel				  external_ ;
	/// 語彙fileのlock
	VerifiedModel				  tokenModel_ ;
	/// sessionより後に解放するruntime環境
	Ort::Env					  environment_ ;
	/// model lockより先に解放する推論context
	std::unique_ptr<Ort::Session> session_ ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 所有出力をdecoderへ借用します
 * @return 本objectの寿命中だけ有効なview
 * @see koefude::ParakeetCtcOutput::view
 */
//------------------------------------------------------------------------------
CtcMatrixView ParakeetCtcOutput::view() const noexcept
{
	return {logProbabilities, frames, K_VOCABULARY, K_BLANK} ;
}

//------------------------------------------------------------------------------
/**
 * @brief registryのFP32 graph・外部weight・tokenを検証して推論を準備します
 * @param[in] options Parakeet Quality、CPUまたはCUDAを明示した設定
 * @param[in] useTf32 CUDAの対象演算でTF32を許可する場合true
 * @param[in] stop 検証と初期化の前後に確認する取消し
 * @throws OperationError モデル不正、GPU予算不足、初期化失敗、取消し
 * @see koefude::ParakeetCtcSession::ParakeetCtcSession(const koefude::EngineOptions &, bool, std::stop_token)
 */
//------------------------------------------------------------------------------
ParakeetCtcSession::ParakeetCtcSession( const EngineOptions &options, const bool useTf32, const std::stop_token stop)
{
	try {
		implementation_ = std::make_unique<Impl>( options, useTf32, stop) ;
	} catch( const Ort::Exception &error) {
		// runtimeのpath入り説明を保持せず、呼出し元のretry判定へ元codeを渡す
		throw OperationError( ErrorKind::INFERENCE_FAILED, static_cast<std::uint32_t>( error.GetOrtErrorCode())) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief sessionを先に解放し、その後にモデルの所有lockを閉じます
 * @see koefude::ParakeetCtcSession::~ParakeetCtcSession
 */
//------------------------------------------------------------------------------
ParakeetCtcSession::~ParakeetCtcSession() = default ;

//------------------------------------------------------------------------------
/**
 * @brief 同じ前処理で一つのPCM窓を推論し、全語彙のCTC出力を所有copyします
 * @param[in] samples 有限な[-1,1]の16kHz mono PCM。80ms～30秒
 * @param[in] firstSample 元fileの先頭位置。返す時刻の基準で、音声は変更しません
 * @param[in] stop 前処理と同期推論の前後で確認する取消し
 * @return 有効frameだけの所有log確率。失敗時は部分結果を返しません
 * @throws OperationError 入力・出力shape不正、推論失敗、取消し
 * @see koefude::ParakeetCtcSession::infer
 */
//------------------------------------------------------------------------------
ParakeetCtcOutput ParakeetCtcSession::infer( const std::span<const float> samples, const std::uint64_t firstSample,
											const std::stop_token stop)
{
	try {
		return implementation_->infer( samples, firstSample, stop, false) ;
	} catch( const Ort::Exception &error) {
		// CUDA失敗等の元codeを保持し、取消しと空本文へ置き換えない
		throw OperationError( ErrorKind::INFERENCE_FAILED, static_cast<std::uint32_t>( error.GetOrtErrorCode())) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 開発診断用に同じ推論の全出力shapeを所有copyします
 * @param[in] samples 有限な[-1,1]の16kHz mono PCM。80ms～30秒
 * @param[in] firstSample 元fileの先頭sample。音声を変更しません
 * @param[in] stop 前処理と同期推論の前後で確認する取消し
 * @return 最大512 frameの全出力。末尾が有効音声かpaddingかは未判定です
 * @throws OperationError 入力・出力shape不正、非有限確率、推論失敗、取消し
 * @note 同じworkerの開発比較だけで使用し、製品経路から呼びません
 * @see koefude::ParakeetCtcSession::inferObserved
 */
//------------------------------------------------------------------------------
ParakeetCtcOutput ParakeetCtcSession::inferObserved( const std::span<const float> samples,
													const std::uint64_t firstSample, const std::stop_token stop)
{
	try {
		return implementation_->infer( samples, firstSample, stop, true) ;
	} catch( const Ort::Exception &error) {
		// 通常APIと同じ分類へ変換し、pathを含む第三者説明を公開しない
		throw OperationError( ErrorKind::INFERENCE_FAILED, static_cast<std::uint32_t>( error.GetOrtErrorCode())) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 正規化だけfloat64で計算し、従来のfloor長でCTCを比較します
 * @param[in] samples 借用する有限な[-1,1]の16kHz mono PCM。80ms〜30秒
 * @param[in] firstSample 元fileの先頭sample。返却時刻の基準
 * @param[in] stop 特徴生成と同期推論の前後で確認する取消し
 * @return 最終float32特徴を同じモデルへ渡した有効frameの所有log確率
 * @throws OperationError 入力・出力shape不正、推論失敗、取消し
 * @note 開発比較専用で、モデル・log・窓・epsilon・分散分母を変更しません
 * @see koefude::ParakeetCtcSession::inferFloat64
 */
//------------------------------------------------------------------------------
ParakeetCtcOutput ParakeetCtcSession::inferFloat64( const std::span<const float> samples,
												   const std::uint64_t firstSample, const std::stop_token stop)
{
	try {
		return implementation_->infer( samples, firstSample, stop, false, true) ;
	} catch( const Ort::Exception &error) {
		// 同じerror分類を保持し、path入り説明を返さない
		throw OperationError( ErrorKind::INFERENCE_FAILED, static_cast<std::uint32_t>( error.GetOrtErrorCode())) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief logだけを加算floorへ変更した開発用特徴量を観測します
 * @param[in] samples 呼出し中だけ借用する有限な[-1,1]の16kHz mono PCM。1280〜480000 sample
 * @param[in] stop 抽出前後とframe処理中に確認する取消し
 * @return 成功時だけ返す一窓の所有観測
 * @throws OperationError 不正入力・非有限値・取消し
 * @note log(mel + 2^-24)以外の設定と旧float32正規化を維持する比較専用APIです
 * @see koefude::observeParakeetFeaturesLogadd
 */
//------------------------------------------------------------------------------
ParakeetFeatureObservation observeParakeetFeaturesLogadd( const std::span<const float> samples,
														 const std::stop_token		  stop)
{
	return observeFeatures( samples, stop, false, true) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 同じ切出し実装で中心80の再現と中心0だけの変更を観測します
 * @param[in] samples 借用する16kHz mono PCM。有限な[-1,1]で1280〜480000 sample
 * @param[in] zeroCenter trueは中心0、falseは中心80の旧対照再現
 * @param[in] stop 前処理とframeごとに確認する取消し
 * @return 成功時だけ返す一窓の所有観測
 * @throws OperationError 入力不正・非有限出力・取消し
 * @note log・正規化・padding・有効長を変えない開発比較専用APIです
 * @see koefude::observeParakeetFeaturesCenter
 */
//------------------------------------------------------------------------------
ParakeetFeatureObservation observeParakeetFeaturesCenter( const std::span<const float> samples, const bool zeroCenter,
														 const std::stop_token stop)
{
	return observeFeatures( samples, stop, false, false, zeroCenter ? 0 : 80) ;
}

//------------------------------------------------------------------------------
/**
 * @brief Hann窓の周期性だけを切り替えた開発用特徴量を観測します
 * @param[in] samples 借用する16kHz mono PCM。有限な[-1,1]で1280〜480000 sample
 * @param[in] periodic trueは現行の分母400、falseは非周期窓の分母399
 * @param[in] stop 抽出前後とframeごとに確認する取消し
 * @return 成功時だけ返す一窓の所有観測
 * @throws OperationError 不正入力・非有限出力・取消し
 * @note 中心80・reflection・現行log・float32正規化・frame数を維持する比較専用APIです
 * @see koefude::observeParakeetFeaturesHann
 */
//------------------------------------------------------------------------------
ParakeetFeatureObservation observeParakeetFeaturesHann( const std::span<const float> samples, const bool periodic,
													   const std::stop_token stop)
{
	return observeFeatures( samples, stop, false, false, -1, !periodic) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 窓形状だけを非周期Hannへ変更して同じCTC有効長で比較します
 * @param[in] samples 借用する有限な[-1,1]の16kHz mono PCM。1280〜480000 sample
 * @param[in] firstSample 元file基準の窓先頭sample。音声と時刻の基準は不変
 * @param[in] stop 前処理と同期推論の前後で確認する取消し
 * @return 一窓の所有CTC行列。失敗時は部分結果を返しません
 * @throws OperationError 不正入力・時刻・取消し・推論失敗
 * @note 中心80・現行log・正規化・モデル・時刻換算を維持する開発専用APIです
 * @see koefude::ParakeetCtcSession::inferHannSymmetric
 */
//------------------------------------------------------------------------------
ParakeetCtcOutput ParakeetCtcSession::inferHannSymmetric( const std::span<const float> samples,
														 const std::uint64_t firstSample, const std::stop_token stop)
{
	try {
		return implementation_->infer( samples, firstSample, stop, false, false, false, -1, true) ;
	} catch( const Ort::Exception &error) {
		// 比較でも元codeを保持し、第三者のpath入り説明を返さない
		throw OperationError( ErrorKind::INFERENCE_FAILED, static_cast<std::uint32_t>( error.GetOrtErrorCode())) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 窓中心だけを0 sampleへ変更し同じCTC floor長で明示比較します
 * @param[in] samples 借用する有限な[-1,1]の16kHz mono PCM。1280〜480000 sample
 * @param[in] firstSample 元file基準の窓先頭sample。音声と返却時刻の基準は不変
 * @param[in] stop 前処理と推論の前後で確認する取消し
 * @return 一窓の所有CTC行列。失敗時は部分結果を返しません
 * @throws OperationError 不正入力・時刻・取消し・推論失敗
 * @note 現行clamp・float32正規化・モデル・時刻換算を維持する開発専用APIです
 * @see koefude::ParakeetCtcSession::inferCenterZero
 */
//------------------------------------------------------------------------------
ParakeetCtcOutput ParakeetCtcSession::inferCenterZero( const std::span<const float> samples,
													  const std::uint64_t firstSample, const std::stop_token stop)
{
	try {
		return implementation_->infer( samples, firstSample, stop, false, false, false, 0) ;
	} catch( const Ort::Exception &error) {
		// 元codeを保持し、第三者のpath入り説明を返さない
		throw OperationError( ErrorKind::INFERENCE_FAILED, static_cast<std::uint32_t>( error.GetOrtErrorCode())) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief logの加算floorだけを変え、有効長floorのCTCを明示比較します
 * @param[in] samples 呼出し中だけ借用する16kHz mono PCM。1280〜480000 sample
 * @param[in] firstSample 元file基準の窓先頭sample
 * @param[in] stop 前処理と推論の前後に確認する取消し
 * @return 所有する有限なCTC行列。失敗時に部分結果を返しません
 * @throws OperationError 不正入力・時刻・取消し・推論失敗
 * @note 同じモデル・旧float32正規化を使う開発専用APIです
 * @see koefude::ParakeetCtcSession::inferLogadd
 */
//------------------------------------------------------------------------------
ParakeetCtcOutput ParakeetCtcSession::inferLogadd( const std::span<const float> samples, const std::uint64_t firstSample,
												  const std::stop_token stop)
{
	try {
		return implementation_->infer( samples, firstSample, stop, false, false, true) ;
	} catch( const Ort::Exception &error) {
		// 比較経路でも元codeを保持し、第三者のpath入り説明を返さない
		throw OperationError( ErrorKind::INFERENCE_FAILED, static_cast<std::uint32_t>( error.GetOrtErrorCode())) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 元語彙のtoken列と発火位置を従来と同じ本文DTOへ戻します
 * @param[in] tokens 順序付き非blank ID。512個以内
 * @param[in] frames 各tokenの相対発火frame。tokensと同数
 * @param[in] firstSample 元音声窓の先頭sample
 * @param[in] sampleCount 元音声窓のsample数
 * @return 元file基準の時刻・本文・日本語境界。空tokenなら空本文の区間です
 * @throws OperationError 不正ID、時刻不正、UTF-8不正
 * @see koefude::ParakeetCtcSession::segment
 */
//------------------------------------------------------------------------------
TextSegment ParakeetCtcSession::segment( const std::span<const std::int32_t> tokens,
										const std::span<const std::size_t> frames, const std::uint64_t firstSample,
										const std::size_t sampleCount) const
{
	// 元sampleの整数範囲と対応件数を確認してから表示時刻を作る
	if( tokens.size() != frames.size() || tokens.size() > K_MAXIMUM_CTC_FRAMES ||
	   firstSample > K_MAXIMUM_TIMELINE_SAMPLES || sampleCount == 0 ||
	   sampleCount > K_MAXIMUM_TIMELINE_SAMPLES - firstSample) {
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
	// 固定decoderと同じ丸めで作る元file時刻の所有本文
	TextSegment					  result{static_cast<std::int64_t>( firstSample / 16),
						 static_cast<std::int64_t>( (firstSample + sampleCount) / 16),
										 {},
										 {}} ;
	std::vector<std::string_view> pieces ; // implementationの寿命内だけ借用する語彙
	std::vector<float>			  seconds ; // C APIと同じfloat秒から既存token処理へ渡す
	pieces.reserve( tokens.size()) ;
	seconds.reserve( tokens.size()) ;
	// 語彙を一度ずつ復号して本文と発火位置を対応付ける
	for( std::size_t index = 0; index < tokens.size(); ++index) { // 本文内のtoken番号
		const auto id = tokens[index] ; // 固定語彙のID
		// 不正IDと窓外の発火を本文へ追加しない
		if( id < 0 || id >= K_BLANK || frames[index] >= K_MAXIMUM_CTC_FRAMES || frames[index] * 1280 >= sampleCount) {
			throw OperationError( ErrorKind::INVALID_TIMELINE) ;
		}
		const auto &piece = implementation_->tokens[id] ; // 語頭記号を戻した所有token
		result.text += piece ;
		pieces.push_back( piece) ;
		seconds.push_back( 0.08F * static_cast<float>( frames[index])) ;
	}
	// 固定sherpaが取り除く先頭一つのASCII空白だけを合わせる
	if( !result.text.empty() && result.text.front() == ' ') {
		result.text.erase( 0, 1) ;
	}
	static_cast<void>( toWide( result.text)) ; // 不正UTF-8をUIへ渡す前に検出する
	// 空本文は有効な空結果とし、架空のtokenを生成しない
	if( !result.text.empty()) {
		// 本文とtokenの完全対応が失われた比較結果は成功にしない
		if( !bindCtcTokens( result, pieces, seconds, 80)) {
			throw OperationError( ErrorKind::INVALID_TIMELINE) ;
		}
		static_cast<void>( markJapaneseWordBoundaries( result)) ;
	}
	return result ;
}

//------------------------------------------------------------------------------
/**
 * @brief 実sessionのprovider設定を診断へ返します
 * @return 秘密情報やpathのない設定文字列
 * @see koefude::ParakeetCtcSession::providerSettings
 */
//------------------------------------------------------------------------------
std::string ParakeetCtcSession::providerSettings() const
{
	return implementation_->providerSettings ;
}

//------------------------------------------------------------------------------
/**
 * @brief 用語比較へ既存本文生成と同じ検証済み語彙を借用します
 * @return ID順の本文piece。blankを含み、本sessionの寿命内だけ有効です
 * @note 同じworkerの同期呼出し専用です。用語contextは構築中に所有copyします
 * @see koefude::ParakeetCtcSession::tokenPieces
 */
//------------------------------------------------------------------------------
std::span<const std::string> ParakeetCtcSession::tokenPieces() const noexcept
{
	return implementation_->tokens ;
}

} // namespace koefude
