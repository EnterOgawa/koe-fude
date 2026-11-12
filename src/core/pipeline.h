//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file pipeline.h
 * @brief decoder、ASR、話者分離を接続する共通処理を宣言します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 UIとCLIで共有する縦断処理を追加
 * 0.1.0 Shunji Ogawa 2026/09/09 結果に実行時のASR profileと発話区間設定を保持
 * 0.1.0 Shunji Ogawa 2026/09/09 結果に受付時のモデル系統を保持
 * 0.1.0 Shunji Ogawa 2026/09/09 キュー待機を除く合計処理時間を結果へ保持
 * 0.1.0 Shunji Ogawa 2026/09/10 CTCは音声の交代と本文の許可境界で発話全体を結合
 * 0.1.0 Shunji Ogawa 2026/09/11 候補の推定範囲と理由を付け、組立て中も取消しを検出
 * 0.1.0 Shunji Ogawa 2026/10/02 decoder差替えの契約を一般音声へ整合
 * 0.1.0 Shunji Ogawa 2026/10/19 元ASRと同じ全PCMの証拠を小さい所有値として追加
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1316の任意資源制御と成立境界を追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

#include "core/audio_identity.h"
#include "core/engines.h"
#include "core/pipeline_resources.h"
#include "core/types.h"

namespace koefude {

using AudioDecoder = std::function<AudioData( const std::filesystem::path &, std::stop_token,
											 const ProgressCallback &)> ; // 差し替え可能なdecoder

/** @brief UI・保存先へ返す所有結果です */
struct Transcript {
	/// 元ASRと同じ全PCMの小さい証拠
	std::optional<DecodedPcmIdentity> pcmIdentity ;
	/// 利用者が明示編集したfile内の話者表示名
	std::map<int, std::string>		  speakerNames ;
	/// 時刻順の表示行
	std::vector<TranscriptLine>		  lines ;
	/// file内の話者推定区間。診断export用
	std::vector<SpeakerTurn>		  speakerTurns ;
	/// 推定clusterの数。未実施・不明は0
	int								  speakerCount = 0 ;
	/// この結果に使った方式
	SpeakerMethod					  speakerMethod = SpeakerMethod::SPECTRAL ;
	/// この結果に使ったモデル内の品質構成
	AsrProfile						  asrProfile = AsrProfile::QUALITY ;
	/// この結果に使ったモデル系統
	AsrModel						  asrModel = AsrModel::WHISPER ;
	/// この結果で発話区間検出を使用した設定
	bool							  speechFilter = true ;
	/// trueなら話者分離を依頼した結果
	bool							  diarizationRequested = true ;
	/// trueならASR成功・話者分離失敗
	bool							  diarizationWarning = false ;
	/// 警告時の分類
	ErrorKind						  diarizationError = ErrorKind::INTERNAL ;
	/// 警告時の元診断code
	std::uint32_t					  diarizationCode = 0 ;
	/// ASRがGPU経路を使用した場合true
	bool							  usedGpu = false ;
	/// GPUからCPUへ再試行した場合true
	bool							  cpuRetried = false ;
	/// API音声の元送信区間。語時刻ではない。V8音声recordから復元
	bool							  apiAudioWindowTimes = false ;
	/// decode済み音声の長さ
	double							  durationSeconds = 0 ;
	/// workerのpipeline開始から結果確定までの秒数。待機・保存を除外
	double							  totalSeconds = 0 ;
	/// decodeに要したwall-clock秒
	double							  decodeSeconds = 0 ;
	/// 検証・model loadを含むASR秒数
	double							  transcriptionSeconds = 0 ;
	/// model検証・推定・本文結合を含む話者分離秒数
	double							  diarizationSeconds = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 時刻の重なりから話者を注釈し、曖昧な区間は複数・不明で残します
 * @param[in] text 元音声基準のASR区間。対応tokenがあれば交代で分割。呼出し中のみ借用
 * @param[in] turns 元音声基準の話者区間。呼出し中のみ借用
 * @param[in] durationMs 元音声の有効時間（ms）、0より大きいこと
 * @param[in] stop 組立てと推定範囲の走査で確認する協調的取消し
 * @return 初出順の話者番号を持つ所有行。入力は変更しません
 * @throws OperationError 不正時刻・token対応、過大な行数・text・token数・区間比較回数、取消し
 * @note CTC発火は継続区間として扱わず、音声交代に近い単語等の許可境界で分割します
 * 発話内の候補と推定範囲を保持し、単語途中へ話者を推測配分しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<TranscriptLine> assembleTranscript( std::span<const TextSegment> text,
															 std::span<const SpeakerTurn> turns,
															 std::int64_t durationMs, std::stop_token stop = {}) ;

//------------------------------------------------------------------------------
/**
 * @brief 一つの音声ジョブを読み込みから話者付きtextまで処理します
 * @param[in] input 利用者が指定したlocal path
 * @param[in] options ジョブ受付時に固定した設定
 * @param[in] decoder 呼出し中に有効な同期decoder
 * @param[in,out] transcriber worker専用ASR instance
 * @param[in,out] diarizer worker専用話者分離instance
 * @param[in] stop 元user取消し。資源停止とは区別します
 * @param[in] progress worker上の通知先。空を許可
 * @param[in,out] resources 任意の同期borrow。nullは従来どおりOS照会・監視threadなし
 * native・PCM・Recognition解放後にfinishし、呼出し終了後は保持しません
 * @return 完了結果。話者分離だけの失敗時は本文と警告を保持
 * @throws OperationError decode/ASR/結合失敗、取消し
 */
//------------------------------------------------------------------------------
[[nodiscard]] Transcript				  runPipeline( const std::filesystem::path &input, const EngineOptions &options,
													  const AudioDecoder &decoder, ITranscriptionEngine &transcriber,
													  ISpeakerDiarizer &diarizer, std::stop_token stop,
													  const ProgressCallback   &progress = {},
													  IPipelineResourceControl *resources = nullptr) ;

namespace detail {
//------------------------------------------------------------------------------
/**
 * @brief 事前確保した所有結果を同じpipeline本体で直接構築します
 * @param[out] result 呼出し前に確保した空の所有holder。失敗時は未公開の部分値です
 * @param[in] input local入力path
 * @param[in] options 受付時の固定設定
 * @param[in] decoder 必須の同期decoder
 * @param[in,out] transcriber 同期ASR
 * @param[in,out] diarizer 同期話者処理
 * @param[in] stop 元user取消し
 * @param[in] progress 任意の同期通知
 * @param[in,out] resources 任意の同期資源制御
 * @note MSVCのmap move構築は確保を伴うため、製品factoryは値返却を経由しません
 * finish後に結果のmove構築・copy・共有holder確保を行わない同一本体です
 */
//------------------------------------------------------------------------------
void runPipelineInto( Transcript &result, const std::filesystem::path &input, const EngineOptions &options,
					 const AudioDecoder &decoder, ITranscriptionEngine &transcriber, ISpeakerDiarizer &diarizer,
					 std::stop_token stop, const ProgressCallback &progress, IPipelineResourceControl *resources) ;
} // namespace detail

} // namespace koefude
