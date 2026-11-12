//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file ctc_text_alignment.h
 * @brief 同じ表記を構成する全CTC token化とframe経路を有界に採点します
 * @version 1.0.0
 * @date 2026/09/12
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/12 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/12 別系統ASRの本文を共通音響出力へ整列する契約を追加
 * 0.1.0 Shunji Ogawa 2026/09/12 明示用語の大小文字を保持する厳密表記の整列を追加
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
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "core/ctc_decoder.h"

namespace koefude {

/** @brief 本文の表現不能と資源制限を、認識成功から区別します */
enum class CtcTextStatus {
	/// 全経路確率と最良経路が利用可能
	READY,
	/// モデル制御用の山括弧表記を含む
	RESERVED_TEXT,
	/// 固定語彙で本文を構成できない
	UNREPRESENTABLE,
	/// 表現は可能だが有効frame内のCTC経路がない
	FRAME_INSUFFICIENT,
	/// graph・遷移・tracebackの明示上限を超える
	RESOURCE_LIMIT,
} ;

/** @brief 指定表記の確率と、その表記を生成する最良音響経路を所有します */
struct CtcTextAlignment {
	/// 適用外の具体的な理由
	CtcTextStatus			  status = CtcTextStatus::UNREPRESENTABLE ;
	/// Ready時の全token化・全frame経路のlog確率
	double					  logProbability = 0 ;
	/// Ready時の最良一経路のlog確率
	double					  pathLogProbability = 0 ;
	/// 最良経路の非blank ID。最大512個
	std::vector<std::int32_t> tokens ;
	/// tokensと同数の相対frame区間
	std::vector<CtcFrameSpan> spans ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief ADR 0040の比較用表記を作り、元本文の所有権を変更しません
 * @param[in] text 最大4096byteのUTF-8。外側ASCII space/tabだけを除きます
 * @return ASCII大文字だけを小文字へ対応させた所有文字列
 * @throws OperationError 不正UTF-8、内部制御文字、byte上限超過
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string normalizeCrossAsrText( std::string_view text) ;

/**
 * @brief 同じ窓の検証済み行列と語彙を借用して複数本文を採点します
 * @note 行列と語彙の所有元は本objectより長く生存し、変更してはいけません
 * worker専有で逐次使用します。音声やtokenizerを生成しません
 */
class CtcTextAligner	  final {
	 public:
	 //------------------------------------------------------------------------------
	 /**
	  * @brief 行列と語彙の整合・容量・UTF-8を一度検証します
	  * @param[in] matrix 呼出し後も借用するCTC行列
	  * @param[in] pieces matrix語彙順の本文piece。先頭SentencePiece空白は変換済み
	  * @param[in] stop 検証中の取消し
	  * @throws OperationError 不正入力・上限超過・取消し
	  */
	 //------------------------------------------------------------------------------
	 CtcTextAligner( CtcMatrixView matrix, std::span<const std::string> pieces, std::stop_token stop = {}) ;

	 //------------------------------------------------------------------------------
	 /**
	  * @brief 指定表記へ連結する全CTC経路と決定的な最良時刻を求めます
	  * @param[in] text 最大4096byte、正規化済みのUTF-8。空本文を許可
	  * @param[in] allowLeadingSpace trueなら先頭一ASCII空白付き経路も合算
	  * @param[in] stop graph構築・各frame・tracebackの取消し
	  * @return 所有結果。適用外は空token/span、負の無限大の確率
	  * @throws OperationError 不正本文・取消し。部分結果は返しません
	  */
	 //------------------------------------------------------------------------------
	 [[nodiscard]] CtcTextAlignment align( std::string_view text, bool allowLeadingSpace = true,
											  std::stop_token stop = {}) const ;

	 //------------------------------------------------------------------------------
	 /**
	  * @brief 大小文字や外側空白を変更しない正確な表記へ整列します
	  * @param[in] text 最大4096byteのUTF-8。空本文を許可し、制御文字は拒否します
	  * @param[in] allowLeadingSpace 先頭一ASCII空白を加えた別経路も許可する場合true
	  * @param[in] stop graph構築・各frame・tracebackの取消し
	  * @return 所有結果。適用外の表記は空token/spanと負の無限大の確率
	  * @throws OperationError 不正本文、byte上限超過、取消し。部分結果なし
	  */
	 //------------------------------------------------------------------------------
	 [[nodiscard]] CtcTextAlignment alignLiteral( std::string_view text, bool allowLeadingSpace = true,
													 std::stop_token stop = {}) const ;

	 private:
	 /// 所有元が変更しない借用行列
	 CtcMatrixView				  matrix_ ;
	 /// 行列と同じ順序の借用語彙
	 std::span<const std::string> pieces_ ;
} ;

} // namespace koefude
