//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file word_boundaries.h
 * @brief Windowsの日本語単語境界をCTCの本文終端へ対応付けます
 * @version 1.0.0
 * @date 2026/09/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/10 単語の途中で話者付き本文を切らないための境界を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1414の単語区切り失敗段階とOS診断を分離
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1416の未解決時全境界保護と未取得語数の意味を明記
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <cstdint>

#include "core/types.h"

namespace koefude {

/** @brief 単語境界取得の最終到達段階を表します */
enum class WordBoundaryStage : std::uint32_t {
	/// OS呼出し前。容量検証の例外時もこの値です
	VALIDATING,
	/// token終端が元本文のbyte範囲外です
	TOKEN_POSITION,
	/// WinRT apartment初期化です
	INITIALIZING,
	/// WordsSegmenter factory取得です
	ACTIVATING,
	/// jaを指定したsegmenter生成です
	CREATING,
	/// OSが実際に解決した言語の取得です
	RESOLVING_LANGUAGE,
	/// 解決言語が既存のja prefix条件を満たしません
	LANGUAGE_MISMATCH,
	/// 元UTF-16本文の単語列取得です
	TOKENIZING,
	/// 語数取得またはUTF-16長との上限照合です
	WORD_COUNT,
	/// 単語列の一要素取得です
	WORD_ACCESS,
	/// 単語の元UTF-16区間取得です
	WORD_POSITION,
	/// 区間範囲またはsurrogate境界の対応が不正です
	WORD_RANGE,
	/// 全対応に成功し、分割許可を一括設定しました
	COMPLETE,
} ;

/** @brief 実ResolvedLanguageを固定分類し、本文や任意OS文字列を保持しません */
enum class WordBoundaryLanguage : std::uint32_t {
	/// 言語取得に到達していません。空本文の成功も含みます
	UNOBSERVED,
	/// 実ResolvedLanguageが既存のja prefix条件を満たしました
	JAPANESE,
	/// 実ResolvedLanguageが正確にundでした。辞書不足の原因は断定しません
	UNDETERMINED,
	/// 取得成功した実ResolvedLanguageがja prefixでもundでもありません
	OTHER,
} ;

/** @brief 本文を含まない単語境界処理一回の有界な診断です */
struct WordBoundaryDiagnostic {
	/// 最終到達段階。COMPLETEだけが正常完了です
	WordBoundaryStage	 stage = WordBoundaryStage::VALIDATING ;
	/// 直前WinRT呼出しの実HRESULT。OS未呼出し時は0で、独立の成否ではありません
	std::int32_t		 hresult = 0 ;
	/// 実取得言語の固定分類
	WordBoundaryLanguage language = WordBoundaryLanguage::UNOBSERVED ;
	/// get_Sizeで返された語数。言語拒否等で未取得の場合は初期値0のままです
	std::uint32_t		 wordCount = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 日本語の単語境界に一致するtoken終端だけを分割可能にします
 * @param[in,out] segment 有効な所有UTF-8本文と対応token。本文と時刻は変更しません
 * @return 日本語規則の全照合成功または空本文/tokenならtrue。日本語未解決・OS/位置不正は全境界を禁止してfalse
 * @throws OperationError 本文の上限超過、不正UTF-8
 * @note worker上の同期処理。COMと結果は呼出し内で所有・解放し、通信しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool markJapaneseWordBoundaries( TextSegment &segment) ;

//------------------------------------------------------------------------------
/**
 * @brief 日本語単語境界を設定し、失敗したOS段階を本文なしで返します
 * @param[in,out] segment 所有UTF-8本文と対応token。容量検証後に分割許可を解除し、全対応成功時だけ許可を設定します
 * 本文・時刻は変更しません。容量超過時は分割許可も変更しません
 * @param[out] diagnostic 呼出し開始時に初期化する固定診断。借用を保持せず、false時も到達段階と実HRESULTを返します
 * @return 全対応成功または空本文/tokenならtrue。日本語未解決・取得・対応失敗時は全境界を禁止してfalse
 * @throws OperationError 容量超過または不正UTF-8。容量超過は既存tokenを変更しません
 * @note worker上の同期処理。診断に本文・path・任意言語文字列を含めず、通信・設定変更を行いません
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool markJapaneseWordBoundaries( TextSegment &segment, WordBoundaryDiagnostic &diagnostic) ;

} // namespace koefude
