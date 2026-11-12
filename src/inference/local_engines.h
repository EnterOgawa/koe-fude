//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file local_engines.h
 * @brief ローカルASRの選択とONNX話者分離のadapterを宣言します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 native推論adapterを追加
 * 0.1.0 Shunji Ogawa 2026/09/10 単一registryによるASR選択とCPUのParakeet adapterを追加
 * 0.1.0 Shunji Ogawa 2026/10/09 primary実窓の同期観測とsecondary共有窓入口を追加
 * 0.1.0 Shunji Ogawa 2026/10/09 共有窓の原行全文収集を厳密segment用途から分離
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1301の補助CPU接続と限定検証を追加
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372の通常CTC二段窓と同期token参照入口を追加
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
#pragma once

#include "core/engines.h"
#include <cstddef>
#include <functional>
#include <span>
#include <stop_token>

namespace koefude {
using CtcReferenceWindowSink = std::function<void( std::size_t, AsrSampleWindow,
												  const Recognition &)> ; // 同期窓結果の借用通知
class CorrectionDiagnosticCapture ; // 任意の本文なし補助CPU診断
namespace detail {
struct WhisperWindowServices ; // モデル不要の同期依存seam
}

/** @brief 単一registryが指定するnative ASRをworker内で実行します */
class LocalAsrEngine final : public ITranscriptionEngine {
	public:
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
	[[nodiscard]] Recognition transcribe( const AudioData &audio, const EngineOptions &options, std::stop_token stop,
										 const ProgressCallback &progress) override ;

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
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] Recognition transcribeObserved( const AudioData &audio, const EngineOptions &options,
												 std::stop_token stop, const ProgressCallback &progress,
												 const RecognitionObserver &observer) ;

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
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] Recognition transcribeWindows( const AudioData &audio, const EngineOptions &options,
												std::span<const AsrSampleWindow> windows, std::stop_token stop,
												const ProgressCallback	  &progress,
												const RecognitionObserver &observer = {},
												AsrTimelineUse timelineUse = AsrTimelineUse::STRICT_SEGMENTS,
												const detail::WhisperWindowServices *services = nullptr) ;
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
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] Recognition transcribeAuxiliaryWindows( const AudioData &audio, const EngineOptions &options,
														 std::span<const AsrSampleWindow> windows, std::stop_token stop,
														 const ProgressCallback				 &progress,
														 const RecognitionObserver			 &observer,
														 AsrTimelineUse						  timelineUse,
														 CorrectionDiagnosticCapture		 *diagnostics = nullptr,
														 const detail::WhisperWindowServices *services = nullptr) ;
} ;

/** @brief 固定Parakeet CTCと検出用gain付きVADをCPUで実行します */
class ParakeetEngine final : public ITranscriptionEngine {
	public:
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
	[[nodiscard]] Recognition transcribe( const AudioData &audio, const EngineOptions &options, std::stop_token stop,
										 const ProgressCallback &progress) override ;
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
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] Recognition transcribeObserved( const AudioData &audio, const EngineOptions &options,
												 std::stop_token stop, const ProgressCallback &progress,
												 const RecognitionObserver &observer) ;

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
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] AsrExecutionTrace transcribeReferenceWindows( const AudioData &audio, const EngineOptions &options,
															   std::span<const AsrSampleWindow> windows,
															   std::stop_token stop, const ProgressCallback &progress,
															   const CtcReferenceWindowSink &sink) ;
} ;

/** @brief 固定したwhisper.cppをworker内で実行します */
class WhisperEngine final : public ITranscriptionEngine {
	public:
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
	[[nodiscard]] Recognition transcribe( const AudioData &audio, const EngineOptions &options, std::stop_token stop,
										 const ProgressCallback &progress) override ;
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
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] Recognition transcribeWindows( const AudioData &audio, const EngineOptions &options,
												std::span<const AsrSampleWindow> windows, std::stop_token stop,
												const ProgressCallback	  &progress,
												const RecognitionObserver &observer = {},
												AsrTimelineUse timelineUse = AsrTimelineUse::STRICT_SEGMENTS,
												const detail::WhisperWindowServices *services = nullptr) ;
} ;

/** @brief sherpa-onnxのCPU経路でfile内の話者を推定します */
class SherpaDiarizer final : public ISpeakerDiarizer {
	public:
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
	[[nodiscard]] std::vector<SpeakerTurn> diarize( const AudioData &audio, const EngineOptions &options,
												   std::stop_token stop, const ProgressCallback &progress) override ;
} ;

} // namespace koefude
