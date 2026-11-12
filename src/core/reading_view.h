//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file reading_view.h
 * @brief 原文に対応する整形結果を検証し、行対応を保った表示viewを定義します
 * @version 1.0.0
 * @date 2026/09/28
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/28 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/28 原文照合・整形結果・表示投影の基盤を追加
 * 0.1.0 Shunji Ogawa 2026/10/04 所有投影のアプリ接続を反映して未接続の注記を修正
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実契約・所有寿命・戻り値の意味を具体化
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415のoperator括弧と文書lambdaの書式を補修
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 * @note 所有投影をアプリの表示workerから使用します。実生成器は別interfaceで未採用です
 */
//------------------------------------------------------------------------------
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/pipeline.h"

namespace koefude {

constexpr std::uint32_t K_READING_CONTRACT_VERSION = 1 ; // 整形結果の保存契約版
constexpr std::size_t	K_MAXIMUM_READING_UNIT_BYTES = 4096 ; // 対象原文一行のbyte上限
constexpr std::size_t	K_MAXIMUM_FORMATTED_LINE_BYTES = 8192 ; // 置換本文一行のbyte上限

/** @brief 同じ履歴の確定原文を識別します */
struct ReadingSourceKey {
	/// 削除後も再利用しない履歴ID
	std::uint64_t				 historyId = 0 ;
	/// 表示名を除く原文と帰属のSHA-256
	std::array<std::uint8_t, 32> sha256{} ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 表示・保存要求を同じ履歴行と原文fingerprintへ束縛できるかを比較します
	 * @param[in] other 比較する別の原文key。呼出し中だけ借用します
	 * @return historyIdとsha256の32byteが両方一致すればtrue。それ以外はfalseです
	 * @note 表示名を除く本文・帰属のdigest比較であり、Transcriptの保持pointerやshared_ptr control blockの同一性は示しません
	 */
	//------------------------------------------------------------------------------
	bool						 operator==( const ReadingSourceKey &other) const = default ;
} ;

/** @brief 元行に対する整形文の扱いです */
enum class ReadingLineKind : std::uint32_t {
	/// 異なる本文を持つ行
	REPLACED,
	/// 元本文を参照する無変更行
	NO_CHANGE,
	/// 理由を添えて元本文を保持する行
	ORIGINAL_RETAINED
} ;

/** @brief 整形を適用しなかった理由です */
enum class ReadingLineReason : std::uint32_t {
	/// 置換または無変更
	NONE,
	/// 話者の根拠が未確定
	ATTRIBUTION_UNCERTAIN,
	/// 一行が生成単位の上限を超えた
	UNIT_TOO_LARGE,
	/// 原文と置換本文の合計容量が不足
	CAPACITY,
	/// adapterが一行の不正出力を破棄
	INVALID_OUTPUT
} ;

/** @brief 原文の一行に一対一で対応する整形結果です */
struct FormattedLine {
	/// 原文linesの0始まり番号
	std::uint32_t	  lineIndex = 0 ;
	/// 本文の採用状態
	ReadingLineKind	  kind = ReadingLineKind::NO_CHANGE ;
	/// 保持理由、適用時はNone
	ReadingLineReason reason = ReadingLineReason::NONE ;
	/// Replacedだけが所有するUTF-8本文
	std::string		  text ;
} ;

/** @brief 原文を複製せず、元行との対応と変更本文だけを所有します */
struct FormattedTranscript {
	/// 対象とする確定原文
	ReadingSourceKey			 source ;
	/// 解釈する契約版
	std::uint32_t				 contractVersion = K_READING_CONTRACT_VERSION ;
	/// 採用方式の固定識別子。合成試験では合成用の値
	std::string					 profileId ;
	/// 実効方式・設定のdigest
	std::array<std::uint8_t, 32> profileSha256{} ;
	/// 原文と同数・同順の行
	std::vector<FormattedLine>	 lines ;
} ;

/** @brief 実行中の一つの整形要求の固定情報です */
struct ReadingRequest {
	/// 受付時点の原文
	ReadingSourceKey			 source ;
	/// 同一process内で再利用しない要求ID
	std::uint64_t				 operationId = 0 ;
	/// 受付時点の方式
	std::string					 profileId ;
	/// 受付時点の方式設定
	std::array<std::uint8_t, 32> profileSha256{} ;
} ;

/** @brief workerから返る候補と実行IDです */
struct ReadingResponse {
	/// 要求と照合するID
	std::uint64_t		operationId = 0 ;
	/// 全行を持つ未検証の候補
	FormattedTranscript result ;
} ;

/** @brief 表示に選んだ本文です */
enum class ReadingDisplay : std::uint32_t {
	/// 元の確定本文
	ORIGINAL,
	/// 照合済みの整形本文
	FORMATTED
} ;

/** @brief 一行内の広めの変更範囲です。最小編集列ではありません */
struct ReadingDiff {
	/// 元本文のUnicode scalar開始
	std::size_t originalScalarBegin = 0 ;
	/// 元本文のUnicode scalar終端
	std::size_t originalScalarEnd = 0 ;
	/// 整形本文のUnicode scalar開始
	std::size_t formattedScalarBegin = 0 ;
	/// 整形本文のUnicode scalar終端
	std::size_t formattedScalarEnd = 0 ;
	/// 元本文のUTF-16 code unit開始
	std::size_t originalUtf16Begin = 0 ;
	/// 元本文のUTF-16 code unit終端
	std::size_t originalUtf16End = 0 ;
	/// 整形本文のUTF-16 code unit開始
	std::size_t formattedUtf16Begin = 0 ;
	/// 整形本文のUTF-16 code unit終端
	std::size_t formattedUtf16End = 0 ;
} ;

/** @brief 所有snapshot内だけで有効な一行の借用表示情報です */
struct ReadingLineView {
	/// 原文への対応番号
	std::uint32_t			   lineIndex = 0 ;
	/// snapshot.sourceが所有する元行
	const TranscriptLine	  *original = nullptr ;
	/// 原文またはsnapshot.formattedが所有する本文
	std::string_view		   text ;
	/// 表示中の行の状態
	ReadingLineKind			   kind = ReadingLineKind::NO_CHANGE ;
	/// 原文保持時の理由
	ReadingLineReason		   reason = ReadingLineReason::NONE ;
	/// Replacedだけの変更範囲
	std::optional<ReadingDiff> diff ;
} ;

/** @brief 別話者を跨がない連続行と元時刻です */
struct ReadingGroup {
	/// 先頭元行の開始時刻
	std::int64_t			   startMs = 0 ;
	/// 構成行の最大終了時刻
	std::int64_t			   endMs = 0 ;
	/// 順序を保った元行番号
	std::vector<std::uint32_t> lineIndices ;
} ;

/** @brief 本文の所有権と表示用borrowを同じ寿命に閉じ込めます */
class ReadingViewSnapshot final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ不変本文の所有権を共有します
	 * @note 名前のない第1引数（const ReadingViewSnapshot &）は複製元です。不変本文の共有ownerと同じ本文を指すviewをmember単位で複製します
	 */
	//------------------------------------------------------------------------------
	ReadingViewSnapshot( const ReadingViewSnapshot &) = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 本文所有権とborrowをまとめて移します
	 * @note 名前のない第1引数（ReadingViewSnapshot &&）は移動元です。共有所有memberと借用viewをmember単位で移します。移動後の元objectは破棄・再代入だけに使います
	 */
	//------------------------------------------------------------------------------
	ReadingViewSnapshot( ReadingViewSnapshot &&) noexcept = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ不変本文の所有権を共有します
	 * @return このsnapshot
	 * @note 名前のない第1引数（const ReadingViewSnapshot &）は複製元です。不変本文の共有ownerと同じ本文を指すviewをmember単位で複製します
	 */
	//------------------------------------------------------------------------------
	ReadingViewSnapshot					 &operator=( const ReadingViewSnapshot &) = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 本文所有権とborrowをまとめて移します
	 * @return このsnapshot
	 * @note 名前のない第1引数（ReadingViewSnapshot &&）は移動元です。共有所有memberと借用viewをmember単位で移します。移動後の元objectは破棄・再代入だけに使います
	 */
	//------------------------------------------------------------------------------
	ReadingViewSnapshot					 &operator=( ReadingViewSnapshot &&) noexcept = default ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 元の履歴と本文を返します
	 * @return 有効な原文key
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const ReadingSourceKey &key() const noexcept
	{
		return key_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 表示中の版を返します
	 * @return 原文または整形文
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] ReadingDisplay display() const noexcept
	{
		return display_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 原文の所有値を返します
	 * @return snapshotが生きる間有効な元結果
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const Transcript &source() const noexcept
	{
		return *source_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 整形結果を返します
	 * @return 原文表示ではnullptr
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const FormattedTranscript *formatted() const noexcept
	{
		return formatted_.get() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 元順の借用行を返します
	 * @return snapshotの寿命内で有効な行view
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::span<const ReadingLineView> lines() const noexcept
	{
		return lines_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief filter前の発話groupを返します
	 * @return snapshotの寿命内で有効なgroup
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::span<const ReadingGroup> groups() const noexcept
	{
		return groups_ ;
	}

	private:
	//------------------------------------------------------------------------------
	/**
	 * @brief 検証済み所有結果だけからsnapshotを構築します
	 */
	//------------------------------------------------------------------------------
	ReadingViewSnapshot() = default ;
	friend ReadingViewSnapshot makeReadingView( std::shared_ptr<const Transcript> source, const ReadingSourceKey &key,
											   std::shared_ptr<const FormattedTranscript> formatted,
											   ReadingDisplay							  display) ;

	/// この表示に使った元の履歴と本文
	ReadingSourceKey		   key_ ;
	/// 現在の表示版
	ReadingDisplay			   display_ = ReadingDisplay::ORIGINAL ;
	/// 借用行の所有元
	std::shared_ptr<const Transcript>		   source_ ;
	/// 整形時だけ本文を保持
	std::shared_ptr<const FormattedTranscript> formatted_ ;
	/// 元行と同順の借用view
	std::vector<ReadingLineView>			   lines_ ;
	/// 話者filterをかける前のgroup
	std::vector<ReadingGroup>				   groups_ ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 元本文・時刻・帰属を検証して固定形式のSHA-256を計算します
 * @param[in] source workerが所有する確定原文。名前等はfingerprintから除外
 * @return 設計版1の32byte digest
 * @throws OperationError 不正な原文、容量超過、暗号APIの失敗
 * @note UI threadから呼ばず、元音声fileは開きません
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::array<std::uint8_t, 32> fingerprintReadingSource( const Transcript &source) ;

//------------------------------------------------------------------------------
/**
 * @brief 合成済み整形結果を原文と照合し、部分結果の採用を防ぎます
 * @param[in] source 不変の確定原文
 * @param[in] key 履歴IDと原文fingerprint
 * @param[in] formatted 全行の候補。失敗時も変更しません
 * @throws OperationError source不一致・不正本文・行対応/容量違反
 */
//------------------------------------------------------------------------------
void validateFormattedTranscript( const Transcript &source, const ReadingSourceKey &key,
								 const FormattedTranscript &formatted) ;

//------------------------------------------------------------------------------
/**
 * @brief 実行IDと方式も照合して、遅延・別要求の結果を拒否します
 * @param[in] source 受付対象の確定原文
 * @param[in] request 受付時に固定したID・方式
 * @param[in] response workerが返した全行候補
 * @throws OperationError 一致しないID/方式、または本文契約違反
 */
//------------------------------------------------------------------------------
void validateReadingResponse( const Transcript &source, const ReadingRequest &request, const ReadingResponse &response) ;

//------------------------------------------------------------------------------
/**
 * @brief 所有結果から、元行番号を残した表示用の借用viewを構築します
 * @param[in] source 呼出し後もsnapshotが共有所有する確定原文。null不可
 * @param[in] key 永続履歴IDと原文fingerprint
 * @param[in] formatted 整形表示時の候補。原文表示では参照しない。null可
 * @param[in] display 要求された表示版
 * @return 元本文と整形本文の寿命を保持した所有snapshot
 * @throws OperationError source/整形の不一致、行数・容量・Unicode違反
 */
//------------------------------------------------------------------------------
[[nodiscard]] ReadingViewSnapshot makeReadingView( std::shared_ptr<const Transcript> source, const ReadingSourceKey &key,
												  std::shared_ptr<const FormattedTranscript> formatted,
												  ReadingDisplay							 display) ;

} // namespace koefude
