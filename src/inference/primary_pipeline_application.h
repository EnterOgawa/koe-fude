//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file primary_pipeline_application.h
 * @brief 通常の主ASR・話者処理を固定RAM予算へ接続します
 * @version 1.0.0
 * @date 2026/10/21
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/21 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1316の通常factoryと必須同期依存を追加
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

#include "core/pipeline.h"
#include "core/pipeline_resources.h"
#include "inference/whole_text_resource_guard.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <stop_token>

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief UIとCLIで同じ16GB向け工学予算を適用します
 * @param[in] input 通常decoderへ渡すlocal入力path
 * @param[in] options 受付時に固定した設定。モデルやGPU方針を変更しません
 * @param[in] stop 元user取消し。資源停止はこのsourceを変更しません
 * @param[in] progress 同期worker通知先。空可
 * @param[in] decoder 任意の同期decoder。空なら通常decoderを使います
 * @return PCMとnative所有物を解放したconst共有結果
 * @throws OperationError 取消し、入力・推論失敗、またはRAM予算不足
 * @note 割当てhard limitや搭載16GB実機受入を保証するAPIではありません
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::shared_ptr<const Transcript>
runPrimaryPipelineJob( const std::filesystem::path &input, const EngineOptions &options, std::stop_token stop,
					  const ProgressCallback &progress = {}, const AudioDecoder &decoder = {}) ;

namespace detail {

//------------------------------------------------------------------------------
/**
 * @brief 同じ16GB guardの所有scopeで音声APIを実行します
 * @param[in] task 同期中だけcontrolを借用する処理
 * @param[in] stop 元user取消し
 * @param[in] probe 空なら通常OS probe
 * @throws OperationError guard成立前の資源失敗
 */
//------------------------------------------------------------------------------
void runPrimaryResourceTask( const std::function<void( IPipelineResourceControl &)> &task, std::stop_token stop,
							const WholeTextResourceProbe &probe = {}) ;

using PipelineResourceTick = std::function<void()> ; // 呼出し先はnoexcept、同期借用のtick

/** @brief controlより先に停止・joinされる監視所有物です */
class IPipelineResourceMonitor {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有する監視実装を破棄します
	 */
	//------------------------------------------------------------------------------
	virtual ~IPipelineResourceMonitor() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 停止を要求し、進行中の最後のtickも完了させます
	 * @note noexcept。返却後のcallback呼出しは禁止します
	 */
	//------------------------------------------------------------------------------
	virtual void stopAndJoin() noexcept = 0 ;
} ;

/** @brief 取得済みの値だけを受動通知し、追加probeを要しない観測です */
struct PrimaryPipelineResourceObservation {
	/// 所有権の境界
	PipelineMemoryBoundary	  boundary = PipelineMemoryBoundary::BEFORE_DECODE ;
	/// 開始時の不変snapshot、byte単位
	WholeTextResourceSnapshot baseline ;
	/// 開始時に固定したbyte上限
	std::uint64_t			  workingLimit = 0 ;
	/// 開始時に固定したbyte上限
	std::uint64_t			  privateLimit = 0 ;
	/// この時点の最初の失敗
	PipelineResourceFailure	  failure ;
} ;

/**
 * @brief 通常factoryと同じ実装本体へ注入する必須の同期依存です
 * @note 全借用先とtickはrunPrimaryPipelineWithServices終了まで生存させます
 */
struct PrimaryPipelineServices {
	/// 必須のdecoder、通常はdecode_input_audio
	AudioDecoder																   decoder ;
	/// native contextを持たない軽いworker専用adapter
	ITranscriptionEngine														  &transcriber ;
	/// 同期worker専用adapter
	ISpeakerDiarizer															  &diarizer ;
	/// 必須、通常は実OS probe。fakeはOSを照会しません
	WholeTextResourceProbe														   resourceProbe ;
	/// 必須のmonitor factory。空・null・開始失敗を無監視で続行しません
	std::function<std::unique_ptr<IPipelineResourceMonitor>( PipelineResourceTick)> startMonitor ;
	/// 任意の受動通知。本文・PCM・pathを含みません。全通知で例外を送出しない契約です
	std::function<void( const PrimaryPipelineResourceObservation &)>				   observe ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 通常入口と同じguard・core・結果返却を注入依存で実行します
 * @param[in] input 入力path
 * @param[in] options 固定設定
 * @param[in,out] services 同期終了まで有効な必須依存と任意の受動観測
 * @param[in] stop 元user取消し
 * @param[in] progress 任意の同期通知先
 * @return 事前確保したholderで所有するconst共有結果
 * @throws OperationError 通常入口と同じ失敗。bad_allocはInputTooLarge/8です
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::shared_ptr<const Transcript> runPrimaryPipelineWithServices( const std::filesystem::path &input,
																			   const EngineOptions		   &options,
																			   PrimaryPipelineServices	   &services,
																			   std::stop_token				stop,
																			   const ProgressCallback &progress = {}) ;

} // namespace detail
} // namespace koefude
