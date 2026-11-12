//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file speaker_text_application.h
 * @brief 明示した投影操作だけをidentity/参照/同writer保存へ接続します
 * @version 1.0.0
 * @date 2026/11/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372のfake差替え可能な逐次jobを追加
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
#include "core/speaker_text_record.h"
#include "inference/text_alignment_reference.h"
namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief 保存参照を現固定asset metadataとだけ照合します
 * @param[in] value 保存来歴
 * @return native再生成なしで再利用可能ならtrue。モデルfileを開きません
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool speakerTextReferenceCurrent( const SpeakerTextProjectionRecord &value) ;
//------------------------------------------------------------------------------
/**
 * @brief 同じ耐久sourceからPC内参照と方式3を一度だけ実行します
 * @param[in] request 小token/同global op
 * @param[in,out] persistence 同writer port
 * @param[in,out] producer worker専用の同期native/fake参照
 * @param[in] decoder local有界decode/fake
 * @param[in] stop 協調取消し
 * @param[in] progress 実窓数/実段階の通知。空可
 * @param[in,out] resources 通常factoryの監視control。nullは隔離fake用
 * @return 原APIを変更しない採用ACKまたは前採用/最新失敗
 */
//------------------------------------------------------------------------------
[[nodiscard]] SpeakerTextJobCompletion
runSpeakerTextJob( const SpeakerTextJobRequest &request, ISpeakerTextPersistencePort &persistence,
				  ITextAlignmentReferenceProducer &producer, const AudioDecoder &decoder, std::stop_token stop,
				  const ProgressCallback &progress = {}, IPipelineResourceControl *resources = nullptr) ;
} // namespace koefude
