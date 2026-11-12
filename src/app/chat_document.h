//------------------------------------------------------------------------------

/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file chat_document.h
 * @brief 同一話者をまとめた色付きチャット表示を宣言します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 発言全体の背景色と連続話者のまとまりを追加
 * 0.1.0 Shunji Ogawa 2026/09/09 表示filterとUI言語の契約を追加
 * 0.1.0 Shunji Ogawa 2026/09/09 結果の実測時間を時分秒と現UI言語で表示
 * 0.1.0 Shunji Ogawa 2026/10/04 所有表示要求/文書とworker準備の契約を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 中立全文表示と専用copyの所有値を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1344の明示API補正と独立保存を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1352の話者ラベル表示と原文比較を追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1361の比較縦scroll同期と表示anchorを追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1363の単一比較表と共通scroll契約へ更新
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1364の旧比較カード/表示位置対応を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の描画要求比較と共有pointer・本文同値・ownerの区別を明記
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415のoperator括弧と文書lambdaの書式を補修
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */

//------------------------------------------------------------------------------

#pragma once

#include "app/localization.h"

#include "app/speaker_filter.h"

#include "core/api_text_correction.h"

#include "core/pipeline.h"

#include "core/reading_view.h"

#include "core/speaker_text_record.h"

#include "core/whole_text_correction.h"

#include "core/whole_text_persistence.h"

#include <cstddef>

#include <cstdint>

#include <memory>

#include <stop_token>

#include <string>

#include <string_view>

#include <vector>

#include <windows.h>

namespace koefude {

/** @brief UIからworkerへ渡す表示専用の所有snapshotです */

struct ChatDocumentRequest {

	/// 元カードの非0 ID
	std::uint64_t								historyId = 0 ;

	/// 原文の共有所有値
	std::shared_ptr<const Transcript>			source ;

	/// 任意の最後の有効結果
	std::shared_ptr<const FormattedTranscript>	formatted ;

	/// 解決した受付版
	ReadingDisplay								display = ReadingDisplay::ORIGINAL ;

	/// UIが固定したfile/状態の見出し
	std::wstring								heading ;

	/// 表示だけの話者filter
	int											filter = K_ALL_SPEAKERS ;

	/// UIが測定したtable幅
	int											widthTwips = 1800 ;

	/// 要求時のUI言語
	UiLanguage									language = UiLanguage::JAPANESE ;

	/// 独立した自由全文owner
	std::shared_ptr<const WholeTextCorrection>	wholeText ;

	/// storage検証済みbodyだけ
	std::shared_ptr<const WholeTextBoundedText> bounded ;

	/// 表示prefixと独立した保存済みcopy対象
	std::shared_ptr<const WholeTextBoundedText>

													   effectiveBounded ;

	/// 未完の明示表示
	bool											   showBoundedPrefix = false ;

	/// sidecar検証済み採用全文
	std::shared_ptr<const ApiTextAttempt>			   apiText ;

	/// 原文選好と独立解決した表示
	bool											   showApiText = false ;

	/// UIだけの左右比較。保存schemaへ追加しない
	bool											   compareApiText = false ;

	/// 時刻/話者を推測せず中立表示
	bool											   showWholeText = false ;

	/// 検証済み原byte投影
	std::shared_ptr<const SpeakerTextProjectionRecord> speakerText ;

	/// 同targetの不変元record
	std::shared_ptr<const ApiAudioRecord>			   speakerAudio ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 表示要求の重複抑止・文書受領に使う条件を比較します
	 * @param[in] other 比較する表示要求。呼出し中だけの読取り借用で、保持しません
	 * @return historyId/display、UTF-16のheading、filter、widthTwips（twip）、language、
	 * 四表示flagが同じで、八shared_ptrの保持pointerもすべて一致すればtrue。それ以外はfalseです
	 * 両者の空pointerも一致として扱います。文書の構築・保存成功を返す関数ではありません
	 * @note shared_ptrはget()のpointerだけを比較し、参照先の本文・recordの深い同値を調べません
	 * 同じpointerでも別control blockなら等しくなり、同じcontrol blockでも別alias pointerなら異なります
	 * 同じpointerの本文変更は検出しないため、文書を再利用するcallerは各参照先を不変に保ちます
	 * sourceの所有control block同一性は、文書受領時のsameTranscriptOwner等で別に照合します
	 */
	//------------------------------------------------------------------------------

	bool											   operator==( const ChatDocumentRequest &other) const = default ;
} ;

/** @brief 本文をUIで再検証/変換しない所有表示文書です */

struct PreparedChatDocument {

	/// 受付source/版/描画条件
	ChatDocumentRequest						   request ;

	/// 元行の所有権を保つ投影
	std::shared_ptr<const ReadingViewSnapshot> view ;

	/// Rich Editへ同期streamする有界ASCII
	std::string								   rtf ;

	/// 全話者の明示copy用UTF-16。UIで作らない
	std::wstring							   fullText ;

	/// 専用copyの本文無編集UTF-16
	std::wstring							   correctedFullText ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 原文照合と本文/RTFの構築をworkerで行います
 * @param[in] request HWNDのない所有要求
 * @param[in] stop 協調取消し
 * @return 全行に対応する所有文書
 * @throws OperationError 不正要求/本文/容量/取消し
 * @note 音声/モデルを開かず、呼出しthreadの言語だけを設定します
 */
//------------------------------------------------------------------------------

[[nodiscard]] PreparedChatDocument prepareChatDocument( const ChatDocumentRequest &request,

													   std::stop_token			  stop) ;

//------------------------------------------------------------------------------
/**
 * @brief 所有Rich Editの幅をtwipへ変換します
 * @param[in] control UI threadが所有するcontrol
 * @return table幅
 */
//------------------------------------------------------------------------------

[[nodiscard]] int				   chatDocumentWidth( HWND control) ;

//------------------------------------------------------------------------------
/**
 * @brief 準備済みRTFだけを所有Rich Editへ適用します
 * @param[in] control 自分のUI threadのcontrol
 * @param[in] document 所有文書の同期borrow
 * @param[in] preserveView trueなら選択/scrollを保持します
 * @throws OperationError stream失敗。原文/整形snapshotは変更しません
 */
//------------------------------------------------------------------------------

void							   setPreparedChatDocument( HWND control, const PreparedChatDocument &document,

														   // この同期境界で所有する値、または確定状態の検証値です

														   bool preserveView = false) ;

//------------------------------------------------------------------------------
/**
 * @brief 文字列をRTF命令として解釈させないescapeを行います
 * @param[in] text UTF-16本文
 * @return ASCIIとRTF Unicode escapeで表した文字列
 */
//------------------------------------------------------------------------------

[[nodiscard]] std::string		   escapeRtf( std::wstring_view text) ;

//------------------------------------------------------------------------------
/**
 * @brief 実測秒数を時:分:秒の小数1桁へ丸めて表示します
 * @param[in] seconds 非負の秒数。24時間を超えても時間数を保持します
 * @return 所有UTF-16文字列。不正・非有限・10億秒超は現UI言語の未計測表示
 */
//------------------------------------------------------------------------------

[[nodiscard]] std::wstring		   formatElapsedTime( double seconds) ;

//------------------------------------------------------------------------------
/**
 * @brief 音声長と処理時間を現UI言語の結果見出しとして組み立てます
 * @param[in] transcript 実測値を保持する不変結果。本文と時刻は変更しません
 * @return 改行で終わる所有UTF-16の説明。未実施と警告付き処理を区別します
 */
//------------------------------------------------------------------------------

[[nodiscard]] std::wstring		   processingTimeSummary( const Transcript &transcript) ;

//------------------------------------------------------------------------------
/**
 * @brief ファイル説明と背景色付き発言を標準Rich Editへ設定します
 * @param[in] control 呼出しUI threadが所有するRich Edit HWND
 * @param[in] heading ファイル名と状態の所有文字列のborrow
 * @param[in] transcript 表示する不変結果
 * @param[in] preserveView trueならscrollとtext選択を維持します
 * @param[in] filter 全話者・不明・混在の定数または表示する話者ID
 * @note 同じ一人の連続発言は改行で結合し、推論結果そのものは変更しません
 */
//------------------------------------------------------------------------------

void setChatDocument( HWND control, std::wstring_view heading, const Transcript &transcript,

					 // この同期境界で所有する値、または確定状態の検証値です

					 bool preserveView = false, int filter = K_ALL_SPEAKERS) ;

} // namespace koefude
