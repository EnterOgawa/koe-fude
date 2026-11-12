//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_audio_pipeline.h
 * @brief 初回音声API jobをdecode、CPU話者、逐次耐久保存へ接続します
 * @version 1.0.0
 * @date 2026/11/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の通常worker経路とfake境界を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/api_audio_transcription.h"
#include "core/job_controller.h"
#include "inference/whole_text_resource_guard.h"
#include <functional>
namespace koefude {
class IApiCorrectionEventLog ; // 既存有限API sink、自由文を受け取りません
/** @brief 全file話者と有界VADだけを差し替える同期依存です */
struct ApiAudioPipelineServices {
	/// 通常Media Foundation、必須
	AudioDecoder	  decoder ;
	/// 全fileへ一回だけ呼ぶCPU adapter
	ISpeakerDiarizer &diarizer ;
	/// 元sample VAD、必須
	std::function<std::vector<AsrSampleWindow>( const AudioData &, const EngineOptions &, std::stop_token,
											   const ProgressCallback &)>
						   speech ;
	/// 空は通常16GB guard、fakeは人工probe
	WholeTextResourceProbe resourceProbe ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 同じ一workerでmetadataと音声providerを使います
 * @param[in] request 明示受付の固定ID/設定
 * @param[in,out] keys 既存共有target
 * @param[in,out] catalog 現キーrevisionの正本
 * @param[in,out] metadata 固定GET
 * @param[in,out] catalogPort 同じ履歴workerのmetadata port
 * @param[in,out] transcriber 一窓一回provider
 * @param[in,out] port 同じ一履歴writer
 * @param[in] stop 協調取消し
 * @param[in] progress 実段階、空可
 * @param[in] services nullは通常依存、非nullは同期試験用
 * @param[in,out] log 任意の既存有限API sink。失敗は認識/送信へ伝播しません
 * @return 部分原文も保持するtyped終端
 * @throws OperationError plan成立前の失敗
 */
//------------------------------------------------------------------------------
[[nodiscard]] JobOutput runApiAudioJob( const AsrJobRequest &request, IApiKeyStore &keys, ApiModelCatalog &catalog,
									   IApiCatalogTransport &metadata, IApiTextPersistencePort &catalogPort,
									   IApiAudioTranscriber &transcriber, IApiAudioPersistencePort &port,
									   std::stop_token stop, const ProgressCallback &progress = {},
									   ApiAudioPipelineServices *services = nullptr,
									   IApiCorrectionEventLog	*log = nullptr) ;
} // namespace koefude
