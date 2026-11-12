//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file asr_context_windows.h
 * @brief 同じVAD範囲の文脈窓と、元tokenの中心所属を定義します
 * @version 1.0.0
 * @date 2026/09/11
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/11 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/11 別窓比較の有界な配置と本文保持を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <cstddef>
#include <span>
#include <stop_token>
#include <vector>

#include "core/asr_windows.h"
#include "core/types.h"

namespace koefude {

constexpr std::size_t K_MAXIMUM_CONTEXT_WINDOWS = 7500 ; // 10分を80msごとに分けた件数上限
constexpr std::size_t K_MAXIMUM_CONTEXT_TOKENS = 512 ; // 一窓に対応するtoken数上限
constexpr std::size_t K_MAXIMUM_CONTEXT_TEXT_BYTES = 64 * 1024 ; // 一窓の所有UTF-8上限

/** @brief 同じPCMに対する、認識入力と非重複な本文所属範囲です */
struct AsrContextWindow {
	/// 左右最大3秒を含む、最大26秒の入力sample範囲
	AsrWindow input ;
	/// 各tokenの発火anchorで所属を決める、最大20秒の半開範囲
	AsrWindow core ;
} ;

/** @brief 元の本文と時刻を変えず、中心へ所属させた確認用候補です */
struct ContextSelection {
	/// 入力範囲と元token時刻を保持。中心外の本文だけを除いた所有copy
	TextSegment				 segment ;
	/// 対応する元token index。文字列による重複除去なし
	std::vector<std::size_t> sourceTokens ;
	/// 直前のtokenが日本語分割を許可していない場合true
	bool					 cutsWordBefore = false ;
	/// 後続があるのに最後のtokenが分割を許可していない場合true
	bool					 cutsWordAfter = false ;
	/// 所属tokenの発火frame終端が中心末尾を超えた場合true
	bool					 anchorCrossesEnd = false ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief VADの和集合を保って中心20秒・左右3秒の比較窓を配置します
 * @param[in] speech 開始順の非重複範囲。隣接のみ結合し、隙間は埋めません。空を許可
 * @param[in] sampleCount 元16kHz mono PCMの要素数。80ms～10分
 * @param[in] shifted trueなら各連続範囲の最初の中心だけ10秒、以降は20秒
 * @param[in] stop 検証と各窓の作成時に確認する取消し
 * @return 中心がVAD範囲を過不足なく覆う所有窓列。失敗時に部分結果を返しません
 * @throws OperationError 不正範囲、上限超過、取消し
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<AsrContextWindow> makeAsrContextWindows( std::span<const AsrWindow> speech,
																  std::size_t sampleCount, bool shifted,
																  std::stop_token stop = {}) ;

//------------------------------------------------------------------------------
/**
 * @brief tokenの元byteと発火anchorで、一つの中心に属する候補を取り出します
 * @param[in] raw 入力窓全体の所有本文。非空なら全UTF-8を覆う512個以内のtokenが必須
 * @param[in] window 同じPCMに対する入力と中心。時刻は既存と同じsample/16のms
 * @param[in] stop 検証とtoken選択時に確認する取消し
 * @return 元本文の連続範囲とtoken時刻、元index、切断の確認理由。rawは変更しません
 * @throws OperationError 不正時刻、UTF-8不正、本文被覆なし、上限超過、取消し
 * @note 分割許可のない位置も比較候補として保持し、製品本文への自動採用は行いません
 * 空本文は空候補です。失敗時に部分結果を返しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] ContextSelection				selectContextTokens( const TextSegment &raw, AsrContextWindow window,
																std::stop_token stop = {}) ;

} // namespace koefude
