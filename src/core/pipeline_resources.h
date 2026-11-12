//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file pipeline_resources.h
 * @brief 主音声処理のOS非依存な資源停止と同期境界を定義します
 * @version 1.0.0
 * @date 2026/10/21
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/21 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1316の共有資源制御契約を追加
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
#include <stop_token>

namespace koefude {

/** @brief 最初の資源停止を表す、OS型を含まない分類です */
enum class PipelineResourceStopReason {
	/// 資源停止を観測していません
	NONE,
	/// 固定したRAM予算またはsnapshot整合性に違反しました
	BUDGET_EXCEEDED,
	/// 資源の照会自体が失敗しました
	PROBE_FAILED
} ;

/** @brief 本文や借用pointerを含まない最初の停止値です */
struct PipelineResourceFailure {
	/// stickyな分類
	PipelineResourceStopReason reason = PipelineResourceStopReason::NONE ;
	/// 最初の予算codeまたはprobeの元code
	std::uint32_t			   code = 0 ;
} ;

/** @brief 所有結果の成立と解放を区別する強制評価位置です */
enum class PipelineMemoryBoundary {
	/// 入力検証やdecodeより前
	BEFORE_DECODE,
	/// decode所有PCMが成立し、全PCM照合より前
	AFTER_DECODE_BEFORE_IDENTITY,
	/// 全PCM照合が成立し、ASRより前
	AFTER_IDENTITY_BEFORE_ASR,
	/// native ASRから戻り、所有本文の組立てより前
	AFTER_ASR_BEFORE_UNKNOWN_SNAPSHOT,
	/// Unknown本文と全metadataを結果が所有した直後
	ASR_ONLY_SNAPSHOT_COMMITTED,
	/// 依頼された話者処理の開始直前
	BEFORE_DIARIZER,
	/// 話者候補が返り、本文との結合より前
	AFTER_DIARIZER,
	/// 独立した話者付き候補が完成した直後
	AFTER_SPEAKER_COMBINE,
	/// native・PCM・Recognition解放と最後の評価が完了した後
	AFTER_CLEANUP_FINALIZED
} ;

/**
 * @brief 一回の同期pipelineだけが借用する資源制御です
 * @note OS照会・監視threadの実装はcoreの外で所有します。finish後は照会しません
 */
class IPipelineResourceControl {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 監視実装の所有物を解放します
	 */
	//------------------------------------------------------------------------------
	virtual ~IPipelineResourceControl() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 元user取消しと資源停止を受ける実効tokenです
	 * @return 同期呼出しの全処理と既存retryへ渡すtoken
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual std::stop_token processingStop() const noexcept = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 進捗や監視callbackから待たずに資源を確認します
	 * @note nativeのC callbackへ例外を送出しません。通知先は排他外で呼びます
	 */
	//------------------------------------------------------------------------------
	virtual void						  pollProgress() noexcept = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 元user取消しを優先し、同じ予算を強制評価します
	 * @param[in] boundary 同期所有権の境界
	 * @param[in] nativeCode 非0なら元native診断を例外codeへ保持します
	 * @throws OperationError 取消し、またはガード由来だけのInputTooLarge
	 */
	//------------------------------------------------------------------------------
	virtual void						  checkpoint( PipelineMemoryBoundary boundary, std::uint32_t nativeCode = 0) = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 実効停止要求の完了後に公開された最初の資源失敗を読みます
	 * @return 未停止ならNone。所有値なので返却後も有効です
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual PipelineResourceFailure failure() const noexcept = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief monitorを停止・joinし、最後の強制評価を一度だけ確定します
	 * @return 保存済みの最初の失敗。再呼出しは再join・再probeしません
	 * @note native・PCM・Recognition解放後に呼びます。例外経路も同じ寿命を守ります
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual PipelineResourceFailure finish() noexcept = 0 ;
} ;

} // namespace koefude
