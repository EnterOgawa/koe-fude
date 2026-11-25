//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file nemotron_diarizer.cpp
 * @brief 公式NeMo Speechの固定CPU C ABIで話者を推定
 * @version 1.1.2
 * @date 2026/11/24
 * @author Shunji Ogawa
 *
 * @par 更新履歴
 * <pre>
 * 1.1.1 Shunji Ogawa 2026/11/24 新規作成
 * 1.1.2 Shunji Ogawa 2026/11/25 ADR1456の正式版へ試用時の変更を反映
 * </pre>
 */
//------------------------------------------------------------------------------
#include "inference/nemotron_diarizer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <windows.h>

#include <nemo_speech/diar.h>

#include "../../native/whole_text/cpu_candidate_contract.h"
#include "inference/model_store.h"
#include "platform/win_helpers.h"

namespace koefude {
namespace {

constexpr std::size_t	K_MAXIMUM_SPEAKERS = 8 ; // 固定V3モデルの出力slot数
constexpr std::size_t	K_PCM_BLOCK_SAMPLES = 2 * K_SAMPLE_RATE ; // 2秒ずつ有界に読込み
constexpr std::uint64_t K_SEGMENT_CHECK_SAMPLES = 120 * K_SAMPLE_RATE ; // 長時間の区間件数検査の間隔

using Module = std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(&FreeLibrary)> ; // DLL参照の所有元

//------------------------------------------------------------------------------
/**
 * @brief 実行ファイルに付属する固定NeMo CPU DLLを限定探索で開く
 * @return プロセス寿命まで固定したDLLへの借用参照
 * @throws OperationError CPU未対応、自EXEのパスが不明、DLL不足、またはnative読込み失敗
 */
//------------------------------------------------------------------------------
HMODULE loadNemotron()
{
	namespace cpu = whole_text::cpu_candidate ;
	// 公式CPU版が想定するx86-64-v3とOSのXMM/YMM保存を、DLLの実行前に確認する
	if( !cpu::supports( cpu::Variant::AVX2, cpu::queryHost())) {
		throw OperationError( ErrorKind::INFERENCE_FAILED, ERROR_NOT_SUPPORTED) ;
	}
	std::array<wchar_t, 32768> executable{} ; // Windowsの有限長UTF-16パス
	// 自EXEのパス長。0は失敗、バッファー長と同一なら切り詰め
	const DWORD length = GetModuleFileNameW( nullptr, executable.data(), static_cast<DWORD>( executable.size())) ;
	// 固定相対位置が特定できないときに任意の探索先を使わない
	if( length == 0 || length >= executable.size()) {
		throw OperationError( ErrorKind::INFERENCE_FAILED, GetLastError()) ;
	}
	const auto root = std::filesystem::path( std::wstring( executable.data(), length)).parent_path() ; // 自EXEの親
	const auto path = validateLocalFile( root / L"nemotron-3" / L"nemo_speech_asr_c.dll") ; // 同梱DLLの絶対パス
	Module	   module(
		LoadLibraryExW( path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32),
		FreeLibrary) ; // 固定DLLを検証してpinするまでの一時所有参照
	// 不足や依存DLLの不整合を実推定失敗へ伝える
	if( !module) {
		throw OperationError( ErrorKind::INFERENCE_FAILED, GetLastError()) ;
	}
	HMODULE pinned = nullptr ; // Windowsがプロセス終了まで保持する参照
	// ggmlのOpenMP workerはstream解放後も残るため、実行中コードをアンロードしない
	if( !GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
						   std::bit_cast<LPCWSTR>( module.get()), &pinned)) {
		throw OperationError( ErrorKind::INFERENCE_FAILED, GetLastError()) ;
	}
	return pinned ;
}

//------------------------------------------------------------------------------
/**
 * @brief 固定ヘッダーと同型のNeMo C ABI exportを取得
 * @param[in] module 呼出し期間中有効なDLL参照
 * @param[in] name 固定されたNULL終端ASCII export名
 * @return moduleの解放まで有効な借用関数ポインター
 * @throws OperationError 必須exportが不足する場合
 */
//------------------------------------------------------------------------------
template <class Symbol> Symbol nemotronSymbol( HMODULE module, const char *name)
{
	const auto address = GetProcAddress( module, name) ; // loaderから借用するWin32関数アドレス
	// 必須exportを解決できない場合はABI不一致として呼出しを止める
	if( !address) {
		throw OperationError( ErrorKind::INFERENCE_FAILED, GetLastError()) ;
	}
	return std::bit_cast<Symbol>( address) ;
}

//------------------------------------------------------------------------------
/**
 * @brief C ABIの固定statusを内部エラーに変換
 * @param[in] status 固定NeMo C ABIが返した値。成功は0
 * @throws OperationError メモリ不足または推論失敗
 */
//------------------------------------------------------------------------------
void requireNemotronSuccess( nemo_speech_asr_status status)
{
	// nativeの自由文を診断・ログへ渡さず固定エラーだけを返す
	if( status != NEMO_SPEECH_ASR_OK) {
		throw OperationError( ErrorKind::INFERENCE_FAILED, status == NEMO_SPEECH_ASR_ERROR_OUT_OF_MEMORY
															  ? ERROR_NOT_ENOUGH_MEMORY
															  : static_cast<std::uint32_t>( status)) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 推定区間を元音声基準の整数msと初出話者IDに変換
 * @param[in,out] segments 同期中に開始順へ並べ替える有界なnative区間
 * @param[in] durationSeconds PCMの秒単位の実長
 * @return 元音声内の正の長さを持つ区間列。順序と重なりを保持
 * @throws OperationError 時刻または話者slotが不正
 */
//------------------------------------------------------------------------------
std::vector<SpeakerTurn> convertNemotronSegments( std::span<nemo_speech_diar_segment> segments,
												 const double						 durationSeconds)
{
	// NaNを並び替えの比較に渡さず、終端のnative frameの丸め幅だけを許容する
	for( const auto &segment : segments) {
		// 未定義時刻やモデルの容量外slotを製品の話者IDへ変換しない
		if( !std::isfinite( segment.start_time) || !std::isfinite( segment.end_time) || segment.start_time < 0 ||
		   segment.end_time < segment.start_time || segment.end_time > durationSeconds + 0.25 || segment.speaker < 1 ||
		   static_cast<std::size_t>( segment.speaker) > K_MAXIMUM_SPEAKERS) {
			throw OperationError( ErrorKind::INVALID_TIMELINE) ;
		}
	}
	std::ranges::sort( segments,
		//------------------------------------------------------------------------------
		/**
		 * @brief 同時刻なら元slot順で初出話者の順序を固定
		 * @param[in] left 比較する検証済み区間
		 * @param[in] right 比較相手の検証済み区間
		 * @return 左区間が開始時刻、slot、終了時刻の順で前ならtrue
		 */
		//------------------------------------------------------------------------------
		[]( const nemo_speech_diar_segment &left, const nemo_speech_diar_segment &right)
		{
			return std::tie( left.start_time, left.speaker, left.end_time) <
				   std::tie( right.start_time, right.speaker, right.end_time) ;
		}) ;
	std::vector<SpeakerTurn> turns ; // native結果の寿命から独立した所有区間列
	turns.reserve( segments.size()) ;
	std::array<int, K_MAXIMUM_SPEAKERS> speakerIds{} ; // slotごとの製品話者ID。-1は未出現
	speakerIds.fill( -1) ;
	int nextSpeaker = 0 ; // 次の初出話者へ割り当てる0始まりID
	// 開始順に時刻を整え、元音声に重なる区間がある話者だけを連番にする
	for( const auto &segment : segments) {
		// terminal paddingを除いた元音声上の開始時刻と終了時刻
		const auto startMs = std::llround( std::min( segment.start_time, durationSeconds) * 1000.0) ;
		const auto endMs = std::llround( std::min( segment.end_time, durationSeconds) * 1000.0) ;
		// ファイル終端のpaddingだけの区間とms以下の空区間を採用しない
		if( endMs <= startMs) {
			continue ;
		}
		auto &speaker = speakerIds[static_cast<std::size_t>( segment.speaker - 1)] ; // 同一slotの所有ID
		// 最初に出現したときだけ製品の話者IDを割り当てる
		if( speaker < 0) {
			speaker = nextSpeaker++ ;
		}
		turns.push_back( {startMs, endMs, speaker}) ;
	}
	return turns ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 公式CPU streamへ有界PCMを投入してファイル全体の話者を推定
 * @param[in] audio 呼出し中に部分読込みする16 kHz mono float PCM
 * @param[in] options モデルルートを含むジョブ受付時の設定
 * @param[in] stop PCM読込みとnative推論の間で確認する取消し状態
 * @param[in] progress 同期worker上の進捗通知先。空を許可
 * @return 元音声基準で初出番号を持つ所有区間。無音なら空
 * @throws OperationError 取消し、読込み・推論・検証失敗、または有界結果の超過
 */
//------------------------------------------------------------------------------
std::vector<SpeakerTurn> nemotronDiarize( const AudioData &audio, const EngineOptions &options,
										 const std::stop_token stop, const ProgressCallback &progress)
{
	checkCancelled( stop) ;
	const auto totalSamples = audio.sampleCount() ; // 元音声の実sample数
	// 空PCMでnative handleを作らない
	if( totalSamples == 0) {
		return {} ;
	}
	reportProgress( progress, Stage::DIARIZING, -1) ;
	static const auto module = loadNemotron() ; // modelとstreamとexportより長く保持する固定参照
	// モデル・stream・結果を管理する同一モジュールのABI関数
	const auto		  create = nemotronSymbol<decltype(&nemo_speech_diar_create)>( module, "nemo_speech_diar_create") ;
	const auto		  destroy = nemotronSymbol<decltype(&nemo_speech_diar_destroy)>( module, "nemo_speech_diar_destroy") ;
	const auto open = nemotronSymbol<decltype(&nemo_speech_diar_stream_open)>( module, "nemo_speech_diar_stream_open") ;
	const auto close =
		nemotronSymbol<decltype(&nemo_speech_diar_stream_close)>( module, "nemo_speech_diar_stream_close") ;
	const auto push =
		nemotronSymbol<decltype(&nemo_speech_diar_stream_push_f32)>( module, "nemo_speech_diar_stream_push_f32") ;
	const auto finish =
		nemotronSymbol<decltype(&nemo_speech_diar_stream_finish)>( module, "nemo_speech_diar_stream_finish") ;
	const auto segments = nemotronSymbol<decltype(&nemo_speech_diar_segments)>( module, "nemo_speech_diar_segments") ;
	const auto frameCount =
		nemotronSymbol<decltype(&nemo_speech_diar_frame_count)>( module, "nemo_speech_diar_frame_count") ;
	const auto frameSeconds =
		nemotronSymbol<decltype(&nemo_speech_diar_seconds_per_frame)>( module, "nemo_speech_diar_seconds_per_frame") ;

	const auto weight = verifyModel( options.modelRoot, sharedModelRoles().nemotron, stop) ; // 読込み中の置換を防ぐ
	const auto weightPath = toUtf8( weight.path.native()) ; // モデル作成中借用されるUTF-8絶対パス
	nemo_speech_diar_model_config config{} ; // 比較と同一のCPU preset。geometry上書きなし
	config.size = sizeof(config) ;
	config.model_path = weightPath.c_str() ;
	config.gpu = -1 ;
	config.preset = "v3-offline" ;
	config.left_context_frames = -1 ;
	nemo_speech_diar_model *modelHandle = nullptr ; // createの出力。直後にRAIIへ移す
	const auto				createStatus = create( &config, &modelHandle) ; // nativeのモデル初期化結果
	const std::unique_ptr<nemo_speech_diar_model, decltype(destroy)> model( modelHandle, destroy) ; // モデル所有元
	requireNemotronSuccess( createStatus) ;
	// 成功statusでもmodelが無効ならstreamを開かない
	if( !model) {
		throw OperationError( ErrorKind::INFERENCE_FAILED) ;
	}
	checkCancelled( stop) ;

	nemo_speech_diar_stream *streamHandle = nullptr ; // openの出力。直後にRAIIへ移す
	const auto				 openStatus = open( model.get(), &streamHandle) ; // nativeのstream初期化結果
	const std::unique_ptr<nemo_speech_diar_stream, decltype(close)> stream( streamHandle, close) ; // stream所有元
	requireNemotronSuccess( openStatus) ;
	// 成功statusでもstreamが無効ならPCMを渡さない
	if( !stream) {
		throw OperationError( ErrorKind::INFERENCE_FAILED) ;
	}
	const double secondsPerFrame = frameSeconds( model.get()) ; // nativeが実際に消化した時刻を換算
	// 有限かつ正のframe幅だけを進捗に使う
	if( !std::isfinite( secondsPerFrame) || secondsPerFrame <= 0) {
		throw OperationError( ErrorKind::INFERENCE_FAILED) ;
	}
	const double	   durationSeconds = static_cast<double>( totalSamples) / K_SAMPLE_RATE ; // PCMの元音声長
	std::vector<float> buffer( K_PCM_BLOCK_SAMPLES) ; // 全PCMを展開せず再利用する2秒分buffer
	std::uint64_t	   offset = 0 ; // 次の未送信sample
	std::uint64_t	   checkpoint = 0 ; // 前回の区間件数検査時の投入sample数
	int				   lastPercent = -1 ; // 同じ進捗通知をまとめる
	// 同じstreamに順序どおり投入し、推定済みframeの進捗と中断要求を反映する
	while( offset < totalSamples) {
		checkCancelled( stop) ;
		// 末尾の部分chunkを含む今回の実読込み数
		const auto length = static_cast<std::size_t>( std::min<std::uint64_t>( buffer.size(), totalSamples - offset)) ;
		const auto block = std::span<float>( buffer).first( length) ; // 最終回は残りsampleだけを読込み
		audio.readSamples( offset, block) ;
		checkCancelled( stop) ;
		requireNemotronSuccess( push( stream.get(), block.data(), block.size(), K_SAMPLE_RATE)) ;
		offset += length ;
		checkCancelled( stop) ;
		const auto frames = frameCount( stream.get()) ; // nativeが処理を終えたframe数
		// native処理済み時刻を割合に換算。終端のflush前は99%以下
		const auto percent = static_cast<int>(
			std::clamp( static_cast<double>( frames) * secondsPerFrame * 100.0 / durationSeconds, 0.0, 99.0)) ;
		// 新しいframeの結果で段階進捗が更新された場合だけ通知する
		if( percent != lastPercent) {
			reportProgress( progress, Stage::DIARIZING, percent) ;
			lastPercent = percent ;
		}
		// 長時間では出力区間の上限を最後まで待たず検査する
		if( offset - checkpoint >= K_SEGMENT_CHECK_SAMPLES) {
			std::size_t count = 0 ; // nativeが現在返せる区間数
			requireNemotronSuccess( segments( stream.get(), nullptr, nullptr, 0, &count)) ;
			// 過大な途中結果の推論と区間蓄積を打ち切る
			if( count > K_MAXIMUM_SEGMENTS) {
				throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
			}
			checkpoint = offset ;
		}
	}
	checkCancelled( stop) ;
	requireNemotronSuccess( finish( stream.get())) ;
	checkCancelled( stop) ;
	std::size_t count = 0 ; // 終端のflushを含む確定区間数
	requireNemotronSuccess( segments( stream.get(), nullptr, nullptr, 0, &count)) ;
	// 外部の件数を割当てに使う前に製品の結果上限を検査する
	if( count > K_MAXIMUM_SEGMENTS) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
	std::vector<nemo_speech_diar_segment> output( count) ; // 検査済みの一時出力
	// 区間がある場合だけnative結果のコピーを要求する
	if( !output.empty()) {
		requireNemotronSuccess( segments( stream.get(), nullptr, output.data(), output.size(), &count)) ;
		// 確定済みの同一streamからサイズが変わる場合は処理失敗とする
		if( count != output.size()) {
			throw OperationError( ErrorKind::INVALID_TIMELINE) ;
		}
	}
	checkCancelled( stop) ;
	auto turns = convertNemotronSegments( output, durationSeconds) ; // 他engineと共通の区間表現
	reportProgress( progress, Stage::DIARIZING, 100) ;
	return turns ;
}

} // namespace koefude
