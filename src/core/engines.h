//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file engines.h
 * @brief ASRと話者分離を差し替えるinterfaceを定義します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 worker専用engine契約を追加
 * 0.1.0 Shunji Ogawa 2026/09/09 品質優先のASR profileと発話区間検出を追加
 * 0.1.0 Shunji Ogawa 2026/09/09 日本語特化モデルとモデル別の品質を分離
 * 0.1.0 Shunji Ogawa 2026/09/10 CPUで動く日本語Parakeetを既存enumの末尾へ追加
 * 0.1.0 Shunji Ogawa 2026/09/11 ParakeetのCUDA実行方針と同じweightのCPU再試行を追加
 * 0.1.0 Shunji Ogawa 2026/09/23 近傍graphだけを分離する診断用設定を追加
 * 0.1.0 Shunji Ogawa 2026/10/09 実ASRの元sample窓・raw token・同期観測traceを保持
 * 0.1.0 Shunji Ogawa 2026/10/09 全文収集用途と通常の時刻付きsegment用途を分離
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372のnative/VAD context生成解放数を実行traceへ追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <vector>

#include "core/types.h"

namespace koefude {

/** @brief 利用者が選ぶ推論装置の方針です */
enum class Backend {
	/// 実測profileに適合するGPUを選択し、条件外ならCPU
	AUTO,
	/// GPUを利用せずCPUのみで推論
	CPU,
	/// 明示したWhisperのGPU実行。予算不足は失敗またはCPU fallback
	VULKAN,
	/// 明示したParakeetのCUDA実行。予算不足は失敗またはCPU fallback
	CUDA
} ;

/** @brief 全体話者数を推定する方式です */
enum class SpeakerMethod {
	/// NME-SCによる人数と近傍数の自動推定。試用方式
	SPECTRAL,
	/// 従来の距離しきい値。明示的な比較用
	LEGACY_THRESHOLD
} ;

/** @brief NME-SCの近傍graph選択方式です。Independentは開発診断専用です */
enum class SpectralGraphSelection {
	/// 人数支持投票を満たすgraphからNME最小を選ぶ製品既定
	COUPLED,
	/// 人数値は投票で維持し、graphだけを最小NMEから選ぶ診断候補
	INDEPENDENT
} ;

/** @brief 利用者が選択する文字起こしモデルの系統です */
enum class AsrModel {
	/// 多言語Whisperのlarge-v3またはturbo
	WHISPER,
	/// 日本語に特化したKotoba-Whisper v2.0
	KOTOBA,
	/// 日本語Parakeet CTCをCUDAまたはCPUで実行する構成
	PARAKEET,
	/// 明示操作で音声区間を送信するAPI。旧enum値は維持
	OPEN_AI_AUDIO
} ;

/** @brief 各モデルのregistryで具体的なweightへ解決する品質設定です */
enum class AsrProfile {
	/// 系統内で精度を優先する検証構成
	QUALITY,
	/// モデル別の軽量構成。精度の同等性は保証しない
	STANDARD
} ;

/** @brief ジョブ受付時に固定する推論設定です */
struct EngineOptions {
	/// 検証対象のモデルdirectory。worker内で使用
	std::filesystem::path  modelRoot ;
	/// 既定の装置選択方針
	Backend				   backend = Backend::AUTO ;
	/// trueならrecoverable GPU失敗時に同じモデルで1回CPU再試行
	bool				   cpuFallback = true ;
	/// trueならASR後に話者分離を行う
	bool				   diarization = true ;
	/// falseは日本語、trueはWhisperの自動判定
	bool				   autoLanguage = false ;
	/// 独立日本語評価を通った構成を既定にする
	AsrModel			   asrModel = AsrModel::PARAKEET ;
	/// 利用者指定の品質優先を既定にする
	AsrProfile			   asrProfile = AsrProfile::QUALITY ;
	/// trueならCPU VADで発話区間を抽出してから認識する
	bool				   speechFilter = true ;
	/// CPU推論thread数、許可範囲1～16
	int					   threads = 4 ;
	/// 0は自動推定、1～20は利用者が指定した人数
	int					   expectedSpeakers = 0 ;
	/// 利用者指定の研究手法を既定にする
	SpeakerMethod		   speakerMethod = SpeakerMethod::SPECTRAL ;
	/// 製品は現行graph選択を維持する
	SpectralGraphSelection spectralGraphSelection = SpectralGraphSelection::COUPLED ;
	/// 人数自動推定のclustering threshold、0より大きく2以下
	float				   speakerThreshold = 0.5F ;
} ;

/** @brief 全録音PCM内の実ASRへ渡した半開sample範囲です */
struct AsrSampleWindow {
	/// 元PCMの先頭sample。原点はfile先頭です
	std::uint64_t begin = 0 ;
	/// 元PCMの終端sample。beginより大きい値です
	std::uint64_t end = 0 ;
} ;

/** @brief 共有窓結果を利用する契約です。全文用途は研究producerだけが明示します */
enum class AsrTimelineUse {
	/// 通常の整数時刻segment。旧時刻拒否条件を維持します
	STRICT_SEGMENTS,
	/// 同期観測する全原行本文。TextSegmentを生成しません
	OBSERVED_WHOLE_TEXT
} ;

/** @brief engineが返したtokenの値を修正せず保持するsidecarです */
struct RawAsrToken {
	/// engine原token byteの所有copy。未完UTF-8を含み得る表示本文以外の値
	std::string text ;
	/// engineが返した開始値。unitで単位を示します
	double		start = 0 ;
	/// engineが返した終了値。負/不正値もsidecarで保持します
	double		end = 0 ;
	/// secondsまたはcentiseconds。時刻の合格は別の境界で検査します
	std::string unit ;
	/// native token IDが特別tokenの場合true。本文時刻として使いません
	bool		special = false ;
	/// CTC発火位置だけの場合true。継続時間を捏造しません
	bool		anchor = false ;
} ;

/** @brief 元行とraw時刻を保持し、整形や推測による修復を行わないsidecarです */
struct RawAsrRow {
	/// trim/正規化前のengine原本文
	std::string				 text ;
	/// 窓内の原開始ms。不正値をclampしません
	double					 startMs = 0 ;
	/// 窓内の原終了ms。不正値をclampしません
	double					 endMs = 0 ;
	/// この行を認識するために実際に渡した元sample窓
	AsrSampleWindow			 window ;
	/// 元token値。空の場合は時刻tokenを取得できません
	std::vector<RawAsrToken> tokens ;
} ;

/** @brief 実native呼出し内の構成と検証済み用途を記録します */
struct AsrExecutionTrace {
	/// 実際に初期化できたASR context数
	std::uint32_t			 nativeContextsCreated = 0 ;
	/// 成功返却前に明示解放したASR context数
	std::uint32_t			 nativeContextsReleased = 0 ;
	/// 当入口が作成したCPU VAD context数
	std::uint32_t			 vadContextsCreated = 0 ;
	/// 成功返却前に明示解放したCPU VAD context数
	std::uint32_t			 vadContextsReleased = 0 ;
	/// 実構成の系統。model_idと単一registryへ照合します
	AsrModel				 family = AsrModel::PARAKEET ;
	/// 実構成のprofile
	AsrProfile				 profile = AsrProfile::QUALITY ;
	/// 実native APIに設定した装置
	Backend					 backend = Backend::CPU ;
	/// 実native APIへ渡したcuda/cpu/vulkan
	std::string				 provider ;
	/// 実構成を単一registryから解決した用途
	std::string				 modelId ;
	/// 同じ呼出しがverify_modelした用途だけ
	std::vector<std::string> verifiedModelIds ;
	/// 実APIへ渡したthread数
	int						 threads = 0 ;
	/// 実Whisperの履歴長
	int						 historyTokens = 0 ;
	/// 実Whisperのbeam幅。CTCでは0
	int						 beamSize = 0 ;
	/// 実Whisperの初期温度
	float					 temperature = 0 ;
	/// 実Whisperの温度増分
	float					 temperatureInc = 0 ;
	/// 実contextへ渡したattention設定
	bool					 flashAttention = false ;
	/// trueは共有窓入口を使ったことを示します
	bool					 externalWindows = false ;
	/// この入口内でVADを使った場合true
	bool					 speechFilter = false ;
	/// native APIへ設定した言語
	std::string				 language ;
	/// 実nativeのgreedy_searchまたはbeam_search
	std::string				 decodingMethod ;
	/// CTC feature次元。Whisperでは0
	int						 featureDim = 0 ;
	/// CTC発火frame幅。Whisperでは0
	int						 frameMs = 0 ;
	/// Whisper区間文字数設定。CTCでは0
	int						 maxLen = 0 ;
	/// native token時刻設定
	bool					 tokenTimestamps = false ;
	/// 実Whisperの単語境界設定
	bool					 splitOnWord = false ;
	/// 実Whisperの非発話token抑制設定
	bool					 suppressNst = false ;
	/// API内で原行全文の明示観測用途を設定した場合true
	bool					 observedWholeText = false ;
} ;

/** @brief ASR本文、全実窓、元sidecarと実際に利用した実行経路です */
struct Recognition {
	/// 共有WhisperのObservedWholeTextのみ空。primaryの既存時刻付き本文は保持します
	std::vector<TextSegment>	 segments ;
	/// 選択したモデルのVulkan/CUDA経路で推論した場合true
	bool						 usedGpu = false ;
	/// trueは明示設定に従いCPUへ再試行したことを表す
	bool						 cpuRetried = false ;
	/// 元PCMへ実際に渡した窓。最大kMaximumSegments
	std::vector<AsrSampleWindow> inputWindows ;
	/// 研究観測時だけ保持する原行/token。有限件数/byteです
	std::vector<RawAsrRow>		 rawRows ;
	/// 実native内で設定したtrace。generic結果ではproviderが空です
	AsrExecutionTrace			 execution ;
} ;

using RecognitionObserver =
	std::function<void( const Recognition &)> ; // worker同期の借用通知。空可、pointerの保持は禁止

/** @brief workerから利用する同期ASR interfaceです */
class ITranscriptionEngine {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 派生engineの所有resourceを解放します
	 */
	//------------------------------------------------------------------------------
	virtual ~ITranscriptionEngine() = default ;

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
	[[nodiscard]] virtual Recognition transcribe( const AudioData &audio, const EngineOptions &options,
												 std::stop_token stop, const ProgressCallback &progress) = 0 ;
} ;

/** @brief ファイル内の話者を音声から推定する同期interfaceです */
class ISpeakerDiarizer {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 派生engineの所有resourceを解放します
	 */
	//------------------------------------------------------------------------------
	virtual ~ISpeakerDiarizer() = default ;

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
	[[nodiscard]] virtual std::vector<SpeakerTurn> diarize( const AudioData &audio, const EngineOptions &options,
														   std::stop_token stop, const ProgressCallback &progress) = 0 ;
} ;

} // namespace koefude
