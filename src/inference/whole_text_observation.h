//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_observation.h
 * @brief 全文実測の所有値と有限な受動観測を定義します
 * @version 1.0.0
 * @date 2026/10/19
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/19 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/19 通常経路の任意観測と同期callback集約を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 集約lock下の時刻取得で並行通知の巻戻りを防止
 * 0.1.0 Shunji Ogawa 2026/10/19 API期限監視用の受動atomic markerを追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 * @note atomic markerだけは実行中も読取り可能です。PCM、lease、DLLを所有しません
 */
//------------------------------------------------------------------------------
#pragma once
#include "inference/whole_text_native_preparation.h"
#include "inference/whole_text_resource_guard.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>

namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief 全phaseで共有する単調時刻を取得します
 * @return steady clockのmicrosecond
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline std::uint64_t wholeTextObservationTime() noexcept
{
	return static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::steady_clock::now().time_since_epoch())
			.count()) ;
}
/** @brief 一段階の最初と最後だけを保持します */
struct WholeTextStageObservation {
	/// 集約lock下で最初に観測した時刻
	std::uint64_t firstUs = 0 ;
	/// 集約lock下で最後に観測した時刻
	std::uint64_t lastUs = 0 ;
	/// 最初の実完了数
	std::uint32_t firstCompleted = 0 ;
	/// 最初の全量、0は不定
	std::uint32_t firstTotal = 0 ;
	/// 最後の実完了数
	std::uint32_t lastCompleted = 0 ;
	/// 最後の全量、0は不定
	std::uint32_t lastTotal = 0 ;
} ;
/** @brief pointerを含まないABI返却値です */
struct WholeTextResultObservation {
	/// 元size
	std::uint32_t structSize = 0 ;
	/// 元版
	std::uint32_t abiVersion = 0 ;
	/// host容量
	std::uint32_t textCapacity = 0 ;
	/// 元本文byte数
	std::uint32_t textBytes = 0 ;
	/// 元自然終端flag
	std::uint32_t naturalEog = 0 ;
	/// 元診断
	std::uint32_t diagnostic = 0 ;
	/// 元符号付き診断
	std::int32_t  engineCode = 0 ;
	/// 元prefix数
	std::uint32_t prefixTokens = 0 ;
	/// 元Analysis数
	std::uint32_t analysisSampledTokens = 0 ;
	/// 元closing数
	std::uint32_t closingControlTokens = 0 ;
	/// 元Final数
	std::uint32_t finalSampledTokens = 0 ;
	/// 元reserved
	std::uint32_t reserved = 0 ;
} ;
/** @brief 通常の結果を変更しない、一要求だけの観測ownerです */
struct WholeTextApplicationObservation {
	/// 全返却資料だけを凍結所有
	std::shared_ptr<const WholeTextNativeMaterials> materials ;
	/// PCMを延命しない弱参照
	std::weak_ptr<const IAudioSource>				auxiliaryPcm ;
	/// 補助decode開始
	std::uint64_t									decodeBeginUs = 0 ;
	/// 補助decode正常return
	std::uint64_t									decodeEndUs = 0 ;
	/// 全PCM照合完了
	std::uint64_t									identityEndUs = 0 ;
	/// Whisper正常return
	std::uint64_t									auxiliaryEndUs = 0 ;
	/// PCM scope終了
	std::uint64_t									pcmScopeEndUs = 0 ;
	/// 通常GGUF検証開始
	std::uint64_t									verifyBeginUs = 0 ;
	/// 通常GGUF検証完了
	std::uint64_t									verifyEndUs = 0 ;
	/// corrector/Prepared/leaseのscope終了
	std::uint64_t									correctorResetUs = 0 ;
	/// 成否を問わない通常関数終了
	std::uint64_t									applicationEndUs = 0 ;
	/// 再decodeの全sample数
	std::uint64_t									sampleCount = 0 ;
	/// 再decodeのHz
	std::uint32_t									sampleRate = 0 ;
	/// 元周波数Hz
	std::uint32_t									sourceRate = 0 ;
	/// 元channel数
	std::uint32_t									sourceChannels = 0 ;
	/// 元file byte
	std::uint64_t									sourceBytes = 0 ;
	/// 元AAC indication
	std::uint32_t									aacProfile = 0 ;
	/// 同期Whisper入口数
	std::uint32_t									auxiliaryAsrCalls = 0 ;
	/// scope後の弱参照失効
	bool											pcmReleased = false ;
	/// baselineが取得できた事実
	bool											guardObserved = false ;
	/// 実guard開始値
	WholeTextResourceSample							baseline ;
	/// 実予約byte
	std::uint64_t									physicalReserve = 0 ;
	/// 実予約byte
	std::uint64_t									commitReserve = 0 ;
	/// 実増分上限byte
	std::uint64_t									workingLimit = 0 ;
	/// 実増分上限byte
	std::uint64_t									privateLimit = 0 ;
	/// 最初の理由と元code
	WholeTextResourceFailure						resourceFailure ;
	/// 最初に失敗を読んだ時刻
	std::atomic<std::uint64_t>						resourceFailureObservedUs = 0 ;
	/// 最初に利用者停止を読んだ時刻
	std::atomic<std::uint64_t>						cancelObservedUs = 0 ;
	/// info API入口数
	std::uint32_t									nativeInfoCalls = 0 ;
	/// info返却status
	std::uint32_t									nativeInfoStatus = 0 ;
	/// pointerのないinfo全field
	KoefudeWtInfoV1									nativeInfo{} ;
	/// generate API入口数
	std::uint32_t									nativeGenerateCalls = 0 ;
	/// 実API直前、監視thread読取り可
	std::atomic<std::uint64_t>						generateEnteredUs = 0 ;
	/// 実API直後、監視thread読取り可
	std::atomic<std::uint64_t>						generateLeftUs = 0 ;
	/// generate開始
	std::uint64_t									generateBeginUs = 0 ;
	/// 全native cleanup後のreturn
	std::uint64_t									generateEndUs = 0 ;
	/// 失敗も含むscalar受領
	bool											nativeResultObserved = false ;
	/// generate返却status
	std::uint32_t									nativeStatus = 0 ;
	/// text pointer以外の全scalar
	WholeTextResultObservation						nativeResult ;
	/// 実LoadLibrary成功
	bool											moduleLoaded = false ;
	/// 実FreeLibrary入口
	bool											freeLibraryCalled = false ;
	/// 実解放結果、0成功
	std::uint32_t									freeLibraryCode = 0 ;
	/// 実FreeLibrary終了
	std::uint64_t									freeLibraryEndUs = 0 ;
	/// 有限4段階
	std::array<WholeTextStageObservation, 4>		stages{} ;
	/// callback更新だけの短い専用排他
	std::mutex										stageMutex ;
	/// 観測例外は成功採用を抑止
	std::atomic<bool>								failed = false ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 複数threadのcallbackを有界に集約します
	 * @param[in] stage 1〜4
	 * @param[in] completed 実完了数
	 * @param[in] total 実全量
	 * @note I/O/可変長確保なし。native呼出し中の排他はこの短い更新だけです
	 * 時刻は集約lock下の観測順であり、callback入口に到達した時刻ではありません
	 */
	//------------------------------------------------------------------------------
	void recordStage( std::uint32_t stage, std::uint32_t completed, std::uint32_t total) noexcept
	{
		try {
			// 未知段階を添字へ変換しません
			if( stage < 1 || stage > stages.size()) {
				failed.store( true) ;
				return ;
			}
			const std::lock_guard lock( stageMutex) ; // 集約更新中だけ保持
			const auto			  now = wholeTextObservationTime() ; // 集約lock下の観測時刻
			auto				 &slot = stages[stage - 1] ; // 固定slot
			// 最初の通知だけを保持します
			if( slot.firstUs == 0) {
				slot.firstUs = now ;
				slot.firstCompleted = completed ;
				slot.firstTotal = total ;
			}
			slot.lastUs = now ;
			slot.lastCompleted = completed ;
			slot.lastTotal = total ;
		} catch( ...) {
			failed.store( true) ;
		}
	}
} ;
} // namespace koefude
