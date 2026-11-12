//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file sherpa_diarizer.cpp
 * @brief CPUで話者区間とfile内clusterを推定します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 sherpa-onnxのCPU話者分離を接続
 * 0.1.0 Shunji Ogawa 2026/09/10 比較用APIの30分制限を実RAM・整数範囲の確認へ変更
 * 0.1.0 Shunji Ogawa 2026/09/23 診断用graph設定を従来方式へ混在させない
 * 0.1.0 Shunji Ogawa 2026/10/19 宣言の役割を明記
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 * @note 固定版のprogress callback戻り値では推論を中断できません
 */
//------------------------------------------------------------------------------
#include "inference/local_engines.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <windows.h>

#include <sherpa-onnx/c-api/c-api.h>

#include "inference/model_store.h"
#include "inference/spectral_diarizer.h"
#include "platform/win_helpers.h"

namespace koefude {
namespace {

/** @brief C APIへ借用させる進捗状態です */
struct DiarizationCallbackState {
	/// diarize呼出し中有効な通知先
	const ProgressCallback *callback = nullptr ;
	/// 通知例外をC ABI外へ漏らさず後で報告する状態
	bool					failed = false ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 処理済みwindow数を段階進捗へ変換します
 * @param[in] processed 処理済みwindow数
 * @param[in] total 総window数
 * @param[in] argument diarizeが所有する進捗状態
 * @return 0。固定版engineはこの値を取消し判定に利用しません
 */
//------------------------------------------------------------------------------
std::int32_t diarizationProgress( const std::int32_t processed, const std::int32_t total, void *argument) noexcept
{
	auto &state = *static_cast<DiarizationCallbackState *>( argument) ; // 呼出し中だけ有効な借用状態
	try {
		const int percent =
			total > 0
				? static_cast<int>( std::clamp<std::int64_t>( static_cast<std::int64_t>( processed) * 100 / total, 0, 99))
				: -1 ; // 実window数から計算した段階進捗
		reportProgress( *state.callback, Stage::DIARIZING, percent) ;
	} catch( ...) {
		// C callbackは例外を送出できないため、処理完了後に共通失敗へ変換する
		state.failed = true ;
	}
	return 0 ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 正規化PCMの話者区間を推定します
 * @param[in] audio 呼出し中有効な16kHz mono PCM
 * @param[in] options ジョブ開始時の設定
 * @param[in] stop 協調的取消し。中断不可区間では終了後に検出
 * @param[in] progress worker上の通知先。空を許可
 * @return 元音声の時刻を持つ所有区間列。無音なら空
 * @throws OperationError 推論失敗、入力上限超過、取消し
 * @note blocking処理。同一instanceへの並行呼出しは禁止
 */
//------------------------------------------------------------------------------
std::vector<SpeakerTurn> SherpaDiarizer::diarize( const AudioData &audio, const EngineOptions &options,
												 const std::stop_token stop, const ProgressCallback &progress)
{
	checkCancelled( stop) ;
	// 試用方式はclustering前の音響特徴を直接利用する
	if( options.speakerMethod == SpeakerMethod::SPECTRAL) {
		return spectralDiarize( audio, options, stop, progress) ;
	}
	// 未定義方式を従来設定として実行しない
	if( options.speakerMethod != SpeakerMethod::LEGACY_THRESHOLD ||
	   options.spectralGraphSelection != SpectralGraphSelection::COUPLED) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// 比較用C APIが要求する32bit sample数の表現境界だけを照合する
	if( audio.sampleCount() == 0 ||
	   audio.sampleCount() > static_cast<std::uint64_t>( std::numeric_limits<std::int32_t>::max())) {
		throw OperationError( ErrorKind::INPUT_TOO_LARGE) ;
	}
	MEMORYSTATUSEX memory{} ; // 全体処理を要求する比較用engineの実行前予算
	memory.dwLength = sizeof(memory) ;
	const auto pcmBytes = audio.sampleCount() * sizeof(float) ; // APIへ渡す連続PCMの実容量
	// 他processとengine内部処理の余裕を残し、全PCMを確保する前にRAMを確認する
	if( !GlobalMemoryStatusEx( &memory) || pcmBytes > std::min( memory.ullAvailPhys, memory.ullAvailPageFile) / 2) {
		throw OperationError( ErrorKind::INPUT_TOO_LARGE, ERROR_NOT_ENOUGH_MEMORY) ;
	}
	std::vector<float> samples( static_cast<std::size_t>( audio.sampleCount())) ; // 従来API専用の全体入力
	audio.readSamples( 0, samples) ;
	// 不正設定でengineが内部終了する経路へ到達しない
	if( options.threads < 1 || options.threads > 16 || options.expectedSpeakers < 0 || options.expectedSpeakers > 20 ||
	   !std::isfinite( options.speakerThreshold) || options.speakerThreshold <= 0 || options.speakerThreshold > 2) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	reportProgress( progress, Stage::DIARIZING, -1) ;
	// 検証済みsegmentationモデルのpathと読込み中の変更を防ぐ所有lock
	const auto		  segmentation = verifyModel( options.modelRoot, ModelId::SEGMENTATION, stop) ;
	// 検証済みembeddingモデルのpathと読込み中の変更を防ぐ所有lock
	const auto		  embedding = verifyModel( options.modelRoot, ModelId::EMBEDDING, stop) ;
	const std::string segmentationPath = toUtf8( segmentation.path.native()) ; // C APIへの所有UTF-8 path
	const std::string embeddingPath = toUtf8( embedding.path.native()) ; // C APIへの所有UTF-8 path
	SherpaOnnxOfflineSpeakerDiarizationConfig config{} ; // 未使用model familyを明示的に無効化する設定
	config.segmentation.pyannote.model = segmentationPath.c_str() ;
	config.segmentation.pyannote.window_shift_ratio = 0.1F ;
	config.segmentation.num_threads = options.threads ;
	config.segmentation.debug = 0 ;
	config.segmentation.provider = "cpu" ;
	config.embedding.model = embeddingPath.c_str() ;
	config.embedding.num_threads = options.threads ;
	config.embedding.debug = 0 ;
	config.embedding.provider = "cpu" ;
	config.clustering.num_clusters = options.expectedSpeakers ;
	config.clustering.threshold = options.speakerThreshold ;
	config.min_duration_on = 0.3F ;
	config.min_duration_off = 0.5F ;
	const std::unique_ptr<const SherpaOnnxOfflineSpeakerDiarization,
						  decltype(&SherpaOnnxDestroyOfflineSpeakerDiarization)>
		diarizer( SherpaOnnxCreateOfflineSpeakerDiarization( &config),
				 SherpaOnnxDestroyOfflineSpeakerDiarization) ; // C APIが所有するengineをRAII化
	// 初期化失敗時にNULL engineへ音声を渡さない
	if( !diarizer ||
	   SherpaOnnxOfflineSpeakerDiarizationGetSampleRate( diarizer.get()) != static_cast<std::int32_t>( K_SAMPLE_RATE)) {
		throw OperationError( ErrorKind::INFERENCE_FAILED) ;
	}
	checkCancelled( stop) ;
	DiarizationCallbackState callbackState{&progress, false} ; // 推論呼出し中の借用callback状態
	const std::unique_ptr<const SherpaOnnxOfflineSpeakerDiarizationResult,
						  decltype(&SherpaOnnxOfflineSpeakerDiarizationDestroyResult)>
		result( SherpaOnnxOfflineSpeakerDiarizationProcessWithCallback( diarizer.get(), samples.data(),
																	  static_cast<std::int32_t>( samples.size()),
																	  diarizationProgress, &callbackState),
			   SherpaOnnxOfflineSpeakerDiarizationDestroyResult) ; // C API結果の所有権
	// 固定版APIは途中cancelを無視するため、安全に戻った時点で結果を破棄する
	checkCancelled( stop) ;
	// callbackまたはengineの失敗を正常な無音と混同しない
	if( !result || callbackState.failed) {
		throw OperationError( ErrorKind::INFERENCE_FAILED) ;
	}
	// C APIが返す話者区間数。配列取得前に範囲を検証する
	const int count = SherpaOnnxOfflineSpeakerDiarizationResultGetNumSegments( result.get()) ;
	// 不正または過大な件数を配列viewへ変換する前に拒否する
	if( count < 0 || static_cast<std::size_t>( count) > K_MAXIMUM_SEGMENTS) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
	std::vector<SpeakerTurn> turns ; // UI pointerと独立した所有区間列
	// 無音の場合は空の区間列を正常な推定結果として返す
	if( count == 0) {
		reportProgress( progress, Stage::DIARIZING, 100) ;
		return turns ;
	}
	const std::unique_ptr<const SherpaOnnxOfflineSpeakerDiarizationSegment,
						  decltype(&SherpaOnnxOfflineSpeakerDiarizationDestroySegment)>
		segments( SherpaOnnxOfflineSpeakerDiarizationResultSortByStartTime( result.get()),
				 SherpaOnnxOfflineSpeakerDiarizationDestroySegment) ; // 配列の専用解放責任を保持
	// 件数があるのに配列を取得できない場合は参照しない
	if( !segments) {
		throw OperationError( ErrorKind::INFERENCE_FAILED) ;
	}
	// 元音声のsample数から求めた、話者区間の検証上限秒数
	const double durationSeconds = static_cast<double>( audio.sampleCount()) / K_SAMPLE_RATE ;
	// C APIの時刻とIDを検証し、元音声基準の整数msへ変換する
	for( int index = 0; index < count; ++index) {
		const auto &segment = segments.get()[index] ; // C APIが所有する有界配列の要素
		// NaN、逆転、範囲外時刻、負の話者IDを確定結果へ混入させない
		if( !std::isfinite( segment.start) || !std::isfinite( segment.end) || segment.start < 0 ||
		   segment.end < segment.start || segment.end > durationSeconds + 0.1 || segment.speaker < 0 ||
		   segment.speaker >= count) {
			throw OperationError( ErrorKind::INVALID_TIMELINE) ;
		}
		turns.push_back(
			{static_cast<std::int64_t>( std::llround( segment.start * 1000.0)),
			 static_cast<std::int64_t>( std::llround( std::min<double>( segment.end, durationSeconds) * 1000.0)),
			 segment.speaker}) ;
	}
	reportProgress( progress, Stage::DIARIZING, 100) ;
	return turns ;
}

} // namespace koefude
