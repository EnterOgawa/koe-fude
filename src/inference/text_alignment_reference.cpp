//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file text_alignment_reference.cpp
 * @brief 全PCM identity一致を通常native参照処理の開始条件にします
 * @version 1.0.0
 * @date 2026/11/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372の一context/逐次窓/対象turn再利用を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実使用の標準headerを直接include
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "inference/text_alignment_reference.h"
#include "inference/local_engines.h"
#include <span>

namespace koefude {
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
AsrExecutionTrace NativeTextAlignmentReferenceProducer::produce(
	const AudioData &audio, const DecodedPcmIdentity &expected, const std::span<const AsrSampleWindow> windows,
	const std::span<const SpeakerTurn> turns, const EngineOptions &options, const std::stop_token stop,
	const ProgressCallback &progress, const TextAlignmentReferenceSink &sink)
{
	checkCancelled( stop) ;
	// 必須sinkを検証して、結果を消費しない重い認識を始めない
	if( !sink) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	const auto actual = fingerprintDecodedPcm( audio, stop, progress) ; // 全PCMを有界に照合
	// sample数やpathだけの一致ではモデルを読み込まない
	if( actual != expected) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	ParakeetEngine engine ; // UIや他workerと共有しない同期adapter
	return engine.transcribeReferenceWindows(
		audio, options, windows, stop, progress,
		//------------------------------------------------------------------------------
		/**
		 * @brief 通常tokenを失う前に対象全turnから参照単位を作ります
		 * @param[in] index 外部窓番号
		 * @param[in] window 元sample窓
		 * @param[in] recognition 通常CTCの同期borrow。raw研究観測は空
		 */
		//------------------------------------------------------------------------------
		[&]( const std::size_t index, const AsrSampleWindow window, const Recognition &recognition)
		{
			const auto reference = makeNativeTextReferenceWindow( window, recognition.segments, turns,
																 stop) ; // 原順の全単位、中立も保持
			sink( index, reference) ;
			checkCancelled( stop) ;
		}) ;
}
} // namespace koefude
