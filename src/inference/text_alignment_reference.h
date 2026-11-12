//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file text_alignment_reference.h
 * @brief identity照合と通常CPU token参照をworker専用interfaceへ隔離します
 * @version 1.0.0
 * @date 2026/11/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372の差替え可能な通常native参照producerを追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/audio_identity.h"
#include "core/engines.h"
#include "core/speaker_text_projection.h"
#include "core/types.h"
#include <cstddef>
#include <functional>
#include <span>
#include <stop_token>

namespace koefude {
using TextAlignmentReferenceSink =
	std::function<void( std::size_t, const TextAlignmentReferenceWindow &)> ; // 呼出し中だけ有効な窓参照

/** @brief API/key/UI/話者再解析へ接続しない同期参照producerです */
class ITextAlignmentReferenceProducer {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 派生producer所有resourceを回収します
	 */
	//------------------------------------------------------------------------------
	virtual ~ITextAlignmentReferenceProducer() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 全PCM一致後だけ予定窓を逐次処理し、全参照単位を同期通知します
	 * @param[in] audio decode済み全PCM所有元
	 * @param[in] expected 保存planの全PCM identity
	 * @param[in] windows 最大120秒の昇順非重複元sample窓
	 * @param[in] turns 対象APIの全file保存turn
	 * @param[in] options Quality/CPU/日本語/VAD有効/diarizationとfallback無効の固定値
	 * @param[in] stop 協調取消し
	 * @param[in] progress 実進捗、空可
	 * @param[in] sink 必須同期borrow。呼出し後に参照を保持しません
	 * @return contextとasset lockを解放後の実行trace
	 * @throws OperationError PCM不一致/不正構成/モデル不正/認識失敗/取消し
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual AsrExecutionTrace produce( const AudioData &audio, const DecodedPcmIdentity &expected,
													std::span<const AsrSampleWindow> windows,
													std::span<const SpeakerTurn> turns, const EngineOptions &options,
													std::stop_token stop, const ProgressCallback &progress,
													const TextAlignmentReferenceSink &sink) = 0 ;
} ;
/** @brief 一つの通常Parakeet Quality CPU contextからtoken参照を作ります */
class NativeTextAlignmentReferenceProducer final : public ITextAlignmentReferenceProducer {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 全PCM一致後だけ予定窓を逐次処理し、全参照単位を同期通知します
	 * @param[in] audio decode済み全PCM所有元
	 * @param[in] expected 保存planの全PCM identity
	 * @param[in] windows 最大120秒の昇順非重複元sample窓
	 * @param[in] turns 対象APIの全file保存turn
	 * @param[in] options Quality/CPU/日本語/VAD有効/diarizationとfallback無効の固定値
	 * @param[in] stop 協調取消し
	 * @param[in] progress 実進捗、空可
	 * @param[in] sink 必須同期borrow。呼出し後に参照を保持しません
	 * @return contextとasset lockを解放後の実行trace
	 * @throws OperationError PCM不一致/不正構成/モデル不正/認識失敗/取消し
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] AsrExecutionTrace produce( const AudioData &audio, const DecodedPcmIdentity &expected,
											std::span<const AsrSampleWindow> windows,
											std::span<const SpeakerTurn> turns, const EngineOptions &options,
											std::stop_token stop, const ProgressCallback &progress,
											const TextAlignmentReferenceSink &sink) override ;
} ;
} // namespace koefude
