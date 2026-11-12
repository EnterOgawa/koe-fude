//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file chat_document.cpp
 * @brief 標準Rich Editへ話者別の背景を持つ発言ブロックを描画します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 連続発言の結合と一列tableの背景色を実装
 * 0.1.0 Shunji Ogawa 2026/09/09 冒頭余白を描画矩形ではなく段落間隔へ移動
 * 0.1.0 Shunji Ogawa 2026/09/09 group後の話者filterと現在言語のlabelを追加
 * 0.1.0 Shunji Ogawa 2026/09/09 音声長と合計・工程別時間の表示を追加
 * 0.1.0 Shunji Ogawa 2026/09/11 候補の理由と時間範囲を本文と区別して表示
 * 0.1.0 Shunji Ogawa 2026/10/04 元行投影と選択版のworker準備/所有streamを追加
 * 0.1.0 Shunji Ogawa 2026/10/19 宣言の役割を明記
 * 0.1.0 Shunji Ogawa 2026/10/19 証拠付き全文を元行から独立してworkerで準備
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1344の明示API補正と独立保存を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1346の通常API補正・保存モデル表示・限定ログを接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1352の話者ラベル表示と原文比較を追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1360の検証済み発言IDから話者色/表示名を適用
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1361の比較縦scroll同期と表示anchorを追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1363の単一2列表と元行ごとの共通高を追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1364の旧結果の話者カードと有界表示位置対応を追加
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372の独立話者投影と元原文保持を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1373の詳細推定区間一覧を全画面から除去
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "app/chat_document.h"
#include "app/api_text_provenance.h"
#include "app/comparison_alignment.h"
#include "app/comparison_scroll.h"
#include "app/localization.h"
#include "app/resource.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <richedit.h>
#include <span>
#include <string>
#include <string_view>

#include "core/api_text_speaker_turns.h"
#include "core/reading_export.h"
#include "core/transcript_export.h"
#include "platform/win_helpers.h"
#include <limits>
#include <memory>
#include <set>
#include <stop_token>
#include <utility>

namespace koefude {
namespace {

constexpr char K_CHAT_RTF_HEADER[] =
	"{\\rtf1\\ansi\\deff0\\uc1{\\fonttbl{\\f0 MS Gothic;}}"
	"{\\colortbl;\\red36\\green51\\blue62;\\red232\\green244\\blue241;"
	"\\red234\\green240\\blue252;\\red250\\green239\\blue224;\\red241\\green233\\blue250;"
	"\\red250\\green232\\blue238;\\red235\\green244\\blue225;"
	"\\red238\\green241\\blue243;\\red249\\green244\\blue219;\\red255\\green255\\blue255;}"
	"\\viewkind4\\f0\\fs22\\cf1\\pard\\sa160\\sb135 " ; // 全表示で同じfont/palette

/** @brief 同期stream中だけ有効なRTF読取り位置です */
struct StreamState {
	/// 呼出元が所有しEM_STREAMIN終了まで有効なRTF
	std::string_view remaining ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief Rich Editが要求する有限byte数だけを返します
 * @param[in] cookie 同期呼出中のStreamStateへの非所有pointer
 * @param[out] output controlが所有する書込みbuffer
 * @param[in] capacity outputの容量（byte）
 * @param[out] written 実際に返したbyte数
 * @return 成功0、引数異常1
 */
//------------------------------------------------------------------------------
DWORD CALLBACK streamRtf( DWORD_PTR cookie, LPBYTE output, LONG capacity, LONG *written) noexcept
{
	// OS callbackの上限とpointerを確認してからcopyする
	if( !cookie || !output || !written || capacity < 0) {
		return 1 ;
	}
	auto	  &state = *reinterpret_cast<StreamState *>( cookie) ; // 同期EM_STREAMINのborrow境界
	// 今回の同期stream要求で出力bufferへcopyできるbyte数
	const auto count = std::min( state.remaining.size(), static_cast<std::size_t>( capacity)) ;
	std::memcpy( output, state.remaining.data(), count) ;
	state.remaining.remove_prefix( count) ;
	*written = static_cast<LONG>( count) ;
	return 0 ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 実測秒数を時:分:秒の小数1桁へ丸めて表示します
 * @param[in] seconds 非負の秒数。24時間を超えても時間数を保持します
 * @return 所有UTF-16文字列。不正・非有限・10億秒超は現UI言語の未計測表示
 * @see koefude::formatElapsedTime
 */
//------------------------------------------------------------------------------
std::wstring formatElapsedTime( const double seconds)
{
	constexpr double K_MAXIMUM_DISPLAY_SECONDS = 1'000'000'000 ; // 約31年。整数変換を安全に制限
	// 非有限・負数・過大値を時刻に見せず、丸めと整数変換のoverflowも避ける
	if( !std::isfinite( seconds) || seconds < 0 || seconds > K_MAXIMUM_DISPLAY_SECONDS) {
		return resource( IDS_TIMING_UNAVAILABLE) ;
	}
	const auto tenths = static_cast<std::int64_t>( std::round( seconds * 10)) ; // 小数1桁へ丸めた値
	auto	   formatted = formatTimestamp( tenths * 100) ; // 桁上がりを反映した時分秒のASCII文字列
	formatted.resize( formatted.size() - 2) ; // millisecondの下二桁を除いて1/10秒を表示
	return toWide( formatted) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 音声長と処理時間を現UI言語の結果見出しとして組み立てます
 * @param[in] transcript 実測値を保持する不変結果。本文と時刻は変更しません
 * @return 改行で終わる所有UTF-16の説明。未実施と警告付き処理を区別します
 * @see koefude::processingTimeSummary
 */
//------------------------------------------------------------------------------
std::wstring processingTimeSummary( const Transcript &transcript)
{
	std::wstring speakerTime = resource( IDS_TIMING_SKIPPED) ; // 依頼していない工程の説明
	// 実行した話者分離だけに所要時間を表示し、失敗した場合も実測値を保持する
	if( transcript.diarizationRequested) {
		speakerTime = formatElapsedTime( transcript.diarizationSeconds) ;
		// 本文を保持した部分失敗を、正常完了の時間と区別する
		if( transcript.diarizationWarning) {
			speakerTime += resource( IDS_TIMING_WARNING) ;
		}
	}
	return resource( IDS_TIMING_TOTAL) + formatElapsedTime( transcript.totalSeconds) + L"  ·  " +
		   resource( IDS_TIMING_AUDIO) + formatElapsedTime( transcript.durationSeconds) + L"\r\n" +
		   resource( IDS_TIMING_DECODE) + formatElapsedTime( transcript.decodeSeconds) + L"  ·  " +
		   resource( IDS_TIMING_ASR) + formatElapsedTime( transcript.transcriptionSeconds) + L"  ·  " +
		   resource( IDS_TIMING_DIARIZATION) + speakerTime + L"\r\n" + resource( IDS_TIMING_NOTE) + L"\r\n" ;
}

//------------------------------------------------------------------------------
/**
 * @brief 文字列をRTF命令として解釈させないescapeを行います
 * @param[in] text UTF-16本文
 * @return ASCIIとRTF Unicode escapeで表した文字列
 * @see koefude::escapeRtf
 */
//------------------------------------------------------------------------------
std::string escapeRtf( const std::wstring_view text)
{
	std::string escaped ; // 外部文字列がRTF命令へ変わらない所有出力
	// UTF-16 code unitごとにencodeし、surrogate pairも順序を保つ
	for( const wchar_t character : text) {
		// RTF特殊文字、改行、Unicodeを明示変換する
		switch( character) {
			case L'\\':
			case L'{':
			case L'}': // 制御記号はliteralとしてescape
				escaped += '\\' ;
				escaped += static_cast<char>( character) ;
				break ;
			case L'\r':
				break ; // CRLFのCRを重複表示しない
			case L'\n':
				escaped += "\\line " ;
				break ; // 区間内の改行だけを保持
			default: // ASCII制御文字と日本語もUnicode escapeで安全に表す
				// 印字可能ASCIIだけをliteralとして出力する
				if( character >= 32 && character <= 126) {
					escaped += static_cast<char>( character) ;
				} else {
					escaped += "\\u" + std::to_string( static_cast<std::int16_t>( character)) + "?" ;
				}
				break ;
		}
	}
	return escaped ;
}

//------------------------------------------------------------------------------
/**
 * @brief 既定話者名だけをUI言語へ変換します
 * @param[in] speakers 元話者IDの列。本呼出し中だけ借用
 * @param[in] transcript 利用者が編集した名前を持つ不変結果
 * @return 名前と帰属を表す所有UTF-16文字列。認識本文は変更しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::wstring uiSpeakerNames( const std::span<const int> speakers, const Transcript &transcript)
{
	// 不明を架空の個別話者へ置き換えない
	if( speakers.empty()) {
		return resource( IDS_UNKNOWN) ;
	}
	std::wstring label ; // UI言語の話者列
	// 元のID順と利用者名を保って混在区間のlabelを構築する
	for( const int speaker : speakers) {
		// 二人目以降の前だけに言語別の区切りを置く
		if( !label.empty()) {
			label += resource( IDS_SPEAKER_SEPARATOR) ;
		}
		const auto name = transcript.speakerNames.find( speaker) ; // file内の明示名
		label += name == transcript.speakerNames.end() ? resource( IDS_SPEAKER) + std::to_wstring( speaker)
													   : toWide( name->second) ;
	}
	return label ;
}

//------------------------------------------------------------------------------
/**
 * @brief 話者名へ、記録された候補の理由だけを付けます
 * @param[in] line 検証済みの所有表示group
 * @param[in] transcript この録音の表示名を持つ所有結果
 * @return 現在のUI言語で表した名前と理由
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::wstring uiSpeakerLabel( const TranscriptLine &line, const Transcript &transcript)
{
	auto label = uiSpeakerNames( line.speakers, transcript) ; // 名前は候補を省略せず列挙する
	// 候補数から同時発話を確定せず、記録されている時間的理由を選ぶ
	switch( line.attribution.kind) {
		case AttributionKind::UNAVAILABLE: // 旧結果の理由は推測で補わない
			// 不明・複数候補の場合だけ、根拠の未記録を明示する
			if( line.speakers.size() != 1) {
				label += resource( IDS_ATTR_UNAVAILABLE_SUFFIX) ;
			}
			break ;
		case AttributionKind::UNASSIGNED: // 採用できる候補がない本文
			label += resource( IDS_ATTR_UNASSIGNED_SUFFIX) ;
			break ;
		case AttributionKind::SINGLE_SPEAKER: // 通常の単一候補の発言
			break ;
		case AttributionKind::UNRESOLVED_CHANGE: // 重なりのない交代の文字対応が未確定
			label += resource( IDS_ATTR_CHANGE_SUFFIX) ;
			break ;
		case AttributionKind::ESTIMATED_OVERLAP: // 行の一部で複数候補が同時に有効
			label += resource( IDS_ATTR_OVERLAP_SUFFIX) ;
			break ;
		default: // 不正種別をもっともらしい表示へ置き換えない
			throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
	return label ;
}

/** @brief 厳密な行頭ラベルの表示解釈です。本文は変更しません */
struct ApiSpeakerHeading {
	/// 話者系見出しだけが新しいblockを開始
	bool		boundary = false ;
	/// 原文に実在する単一IDのみ、ほかは中立
	int			speaker = 0 ;
	/// 検証できた明示複数話者だけを不明と区別
	bool		multiple = false ;
	/// 表示は保持し、位置照合からだけ除く厳密metadata長
	std::size_t matchConsumed = 0 ;
	/// 有効単一ラベルの長さ、無効は本文を全保持
	std::size_t consumed = 0 ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 厳密な不明/複数標準labelだけの長さを検証します
 * @param[in] line UTF-16行頭
 * @param[in] ids 元の実ID集合
 * @return 有効なmetadata終端。任意文字/未知ID/一人だけの複数labelは0
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::size_t neutralHeadingLength( std::wstring_view line, const std::set<int> &ids)
{
	// 不明labelは標準の完全な閉じ括弧までに限ります
	if( line.starts_with( L"[話者不明]")) {
		return std::wstring_view( L"[話者不明]").size() ;
	}
	// 複数以外の任意bracketや本文内引用をmetadataへ昇格させません
	if( !line.starts_with( L"[複数話者")) {
		return 0 ;
	}
	std::size_t at = std::wstring_view( L"[複数話者").size() ; // 番号列の開始
	// 番号なしでも明示された複数label自体は位置照合の本文から分離できます
	if( at < line.size() && line[at] == L']') {
		return at + 1 ;
	}
	std::set<int> seen ; // 原文にある番号だけ。最大64個で停止
	// legacyの空白区切り番号と標準comma区切り番号だけを認めます
	while( at < line.size() && seen.size() < 64) {
		// 区切り後は正のASCII十進数が必須です
		if( line[at] < L'1' || line[at] > L'9') {
			return 0 ;
		}
		int id = 0 ; // overflow検証付きの実番号
		// 数字列を一回だけ読み、未知IDは後で拒否します
		while( at < line.size() && line[at] >= L'0' && line[at] <= L'9') {
			const int digit = line[at++] - L'0' ; // 有効な一桁
			// 巨大値を既知IDへ丸めません
			if( id > (std::numeric_limits<int>::max() - digit) / 10) {
				return 0 ;
			}
			id = id * 10 + digit ;
		}
		// 未知/重複番号をmetadataとして剥がしません
		if( !ids.contains( id) || !seen.insert( id).second) {
			return 0 ;
		}
		bool spaced = false ; // legacy空白区切りの有無
		// 閉じ括弧前と番号間のASCII空白だけを許可します
		while( at < line.size() && (line[at] == L' ' || line[at] == L'\t')) {
			spaced = true ;
			++at ;
		}
		// 完全な複数labelだけを位置照合から分離します
		if( at < line.size() && line[at] == L']') {
			return seen.size() > 1 ? at + 1 : 0 ;
		}
		// commaは次の実番号を要求し、末尾commaを許可しません
		if( at < line.size() && line[at] == L',') {
			++at ;
		} else if( !spaced) { // 任意の文字や連結ラベルを剥がしません
			return 0 ;
		}
	}
	return 0 ;
}
//------------------------------------------------------------------------------
/**
 * @brief 行頭の標準bracket話者を原文の実IDだけへ照合します
 * @param[in] line 改行を除いたUTF-16行
 * @param[in] ids 原文に実在する正のID集合
 * @return 有界な境界と単一話者。引用中/行途中は境界にしません
 */
//------------------------------------------------------------------------------
[[nodiscard]] ApiSpeakerHeading apiSpeakerHeading( const std::wstring_view line, const std::set<int> &ids)
{
	ApiSpeakerHeading result ; // 未ラベル本文は前の明示block内の続き行
	// 任意Markdown/引用や空白indentを推測して見出しへ昇格しません
	if( !line.starts_with( L"[話者") && !line.starts_with( L"[複数話者")) {
		return result ;
	}
	result.boundary = true ; // 不正/不明/複数も前話者を引き継がない新しい中立block
	result.matchConsumed = neutralHeadingLength( line, ids) ; // 厳密metadataだけを分離
	result.multiple = result.matchConsumed && line.starts_with( L"[複数話者") ;
	// 複数や不明はラベルも含めて中立本文に保持します
	if( !line.starts_with( L"[話者")) {
		return result ;
	}
	std::size_t at = std::wstring_view( L"[話者").size() ; // 数字の先頭位置
	// 0/先頭0/未記載を実話者IDとして扱いません
	if( at == line.size() || line[at] < L'1' || line[at] > L'9') {
		return result ;
	}
	int speaker = 0 ; // int範囲で検証する表示ID
	// ASCII数字を終端まで読み、乗加算前にoverflowを拒否します
	while( at < line.size() && line[at] >= L'0' && line[at] <= L'9') {
		const int digit = line[at] - L'0' ; // 既知の十進1桁
		// 不正な巨大IDで前話者への色継承をしません
		if( speaker > (std::numeric_limits<int>::max() - digit) / 10) {
			return result ;
		}
		speaker = speaker * 10 + digit ;
		++at ;
	}
	// 元projectionにある閉じ括弧前のASCII空白だけを許可します
	while( at < line.size() && (line[at] == L' ' || line[at] == L'\t')) {
		++at ;
	}
	// 括弧不正/未定義IDは本文を捨てず中立のまま保全します
	if( at == line.size() || line[at] != L']' || !ids.contains( speaker)) {
		return result ;
	}
	result.speaker = speaker ;
	result.consumed = at + 1 ;
	result.matchConsumed = result.consumed ;
	return result ;
}
//------------------------------------------------------------------------------
/**
 * @brief 一つの検証済み話者blockを元paletteで出力します
 * @param[in] body UTF-16本文
 * @param[in] speaker 実在単一ID、0は中立
 * @param[in] source 元表示名
 * @param[in] widthTwips pane幅
 * @param[in,out] rtf 有界出力
 * @param[in] stop 協調取消し
 */
//------------------------------------------------------------------------------
void appendApiSpeakerBlock( std::wstring_view body, int speaker, const Transcript &source, int widthTwips,
						   std::string &rtf, std::stop_token stop)
{
	checkCancelled( stop) ;
	const int		 color = speaker ? 2 + (speaker - 1) % 6 : 8 ; // 原文と同じ安定palette
	const std::array single{speaker} ; // 同期spanの借用
	const auto		 name = speaker ? uiSpeakerNames( single, source) : resource( IDS_API_NEUTRAL) ;
	rtf += "\\trowd\\trgaph140\\trleft0\\trpaddl160\\trpaddr160"
		   "\\trpaddt100\\trpaddb100\\trpaddfl3\\trpaddfr3\\trpaddft3\\trpaddfb3\\clcbpat" +
		   std::to_string( color) + "\\cellx" + std::to_string( widthTwips) + "\\pard\\intbl\\sa120\\sb100\\b\\fs20 " +
		   escapeRtf( name) + "\\b0\\line\\fs22 " + escapeRtf( body) + "\\cell\\row\\pard\\fs8\\sa0\\sb0\\par " ;
	// 元候補を黙切断せず、表示容量超過を全体失敗にします
	if( rtf.size() > 16 * K_MAXIMUM_TEXT_BYTES) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief v3の機械IDだけで改善文を元話者へ対応させます
 * @param[in] attempt 検証済み完全採用
 * @param[in] source 同じ元原文
 * @param[in] widthTwips pane幅
 * @param[in,out] rtf 有界出力
 * @param[in] stop part/block間の協調取消し
 */
//------------------------------------------------------------------------------
void appendApiTurnBlocks( const ApiTextAttempt &attempt, const Transcript &source, int widthTwips, std::string &rtf,
						 std::stop_token stop)
{
	// 外部保存値の候補/投影と原文の対応を描画入口でも確認します
	if( attempt.draft.projection != apiTextSpeakerProjection( source) ||
	   attempt.results.size() != attempt.draft.chunks.size() ||
	   apiTextCombinedCandidate( attempt) != attempt.candidate) {
		throw OperationError( ErrorKind::INVALID_INPUT, K_API_TEXT_SPEAKER_STRUCTURE_ERROR) ;
	}
	const auto flush =
		//------------------------------------------------------------------------------
		/**
		 * @brief 完成blockを同じ元話者の表示へ出力します
		 * @param[in] body 表示本文
		 * @param[in] id 実在単一話者、0は中立
		 */
		//------------------------------------------------------------------------------
		[&]( std::wstring_view body, int id)
		{
			appendApiSpeakerBlock( body, id, source, widthTwips, rtf, stop) ;
		} ;
	std::wstring block ; // 次の話者までの有限本文
	int			 speaker = 0 ; // 不明/複数は個人の色にしません
	std::size_t	 sourceLine = 0 ; // 同じ不明/複数発言のpartだけ継続
	bool		 started = false ; // 未開始と中立blockを区別
	// 元順のchunkとpartを一回ずつ描画します
	for( std::size_t index = 0; index < attempt.results.size(); ++index) {
		checkCancelled( stop) ;
		const auto &result = attempt.results[index] ; // 保存済み応答のborrow
		// 不完全応答を表示だけで完全採用へ昇格しません
		if( !result.complete) {
			throw OperationError( ErrorKind::INVALID_INPUT, K_API_TEXT_SPEAKER_STRUCTURE_ERROR) ;
		}
		// 本文の通常bracketラベルを構文として再解釈しません
		for( const auto &part : parseApiTextSpeakerParts( attempt.draft, index, result.text)) {
			checkCancelled( stop) ;
			// 行IDが存在しない場合に近隣話者を推測しません
			if( part.sourceLine >= source.lines.size()) {
				throw OperationError( ErrorKind::INVALID_INPUT, K_API_TEXT_SPEAKER_STRUCTURE_ERROR) ;
			}
			const auto &speakers = source.lines[part.sourceLine].speakers ; // 元音声処理の正本
			const int	next = speakers.size() == 1 ? speakers.front() : 0 ; // 個人色は単一だけ
			const bool	same = started && (next ? speaker == next : !speaker && sourceLine == part.sourceLine) ;
			// 再登場/交代を正しく分け、連続同一話者と長発言partだけをまとめます
			if( started && !same) {
				flush( block, speaker) ;
				block.clear() ;
			}
			// 同じblockの発言/partは改行でつなぎます
			if( same) {
				// 同じ長発言のpart途中に架空の段落を追加しません
				if( sourceLine != part.sourceLine) {
					block += L"\n\n" ;
				}
			} else if( !next) {
				block += toWide( part.label) + L" " ; // 不明と複数の区別を文字でも保持
			}
			const auto text = toWide( part.text) ; // 今回partのUTF-16だけを数える
			block += text ;
			speaker = next ;
			sourceLine = part.sourceLine ;
			started = true ;
		}
	}
	// 全partを受領した最後のblockだけを出力します
	if( started) {
		flush( block, speaker) ;
	}
}
/** @brief 保存ラベルから得た表示専用blockです。本文と保存ownerは変更しません */
struct ApiSpeakerBlock {
	/// 表示本文。既知単一話者の標準labelだけを見出しへ分離
	std::wstring body ;
	/// 厳密な標準label metadataだけを除いた位置照合用本文
	std::wstring matchBody ;
	/// 元に実在する明示ID、0は未確定
	int			 speaker = 0 ;
	/// 明示複数label。個人へ割り当てません
	bool		 multiple = false ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief legacy改善文の明示話者blockを表示専用値へ分けます
 * @param[in] body 検証済み候補のUTF-16借用
 * @param[in] source 元話者の正本
 * @return 単独/比較で共用する原順block。保存本文を編集しません
 * @param[in] stop 行走査/各block間の協調取消し
 * @note preambleと不正見出しは中立。正しい見出し以後の継続行/空行/段落は次の見出しまで同block
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<ApiSpeakerBlock> apiSpeakerBlocks( const std::wstring_view body, const Transcript &source,
															const std::stop_token stop)
{
	std::set<int> ids ; // 元行で使われたIDだけ。新IDを音声から再推定しない
	// 元行の全候補を有界走査し、表示名だけではIDを新設しません
	for( const auto &line : source.lines) {
		checkCancelled( stop) ;
		ids.insert( line.speakers.begin(), line.speakers.end()) ;
	}
	std::vector<ApiSpeakerBlock> blocks ; // 全blockを原順に保持する有限DTO
	int							 speaker = 0 ; // 先頭preambleは中立
	bool						 multiple = false ; // 中立の理由を表示だけへ保持
	bool						 started = false ; // 空body/空見出しでも明示blockを保持
	std::wstring				 block ; // 最大候補全体までの所有作業buffer
	std::wstring				 matchBlock ; // metadata以外を削らない位置照合用buffer
	const auto					 flush =
		//------------------------------------------------------------------------------
		/**
		 * @brief 現blockを一回だけ出力し所有作業bufferを空にします
		 */
		//------------------------------------------------------------------------------
		[&]
		{
			// 最初の未開始bufferを架空の発言として出力しません
			if( !started) {
				return ;
			}
			blocks.push_back( {std::move( block), std::move( matchBlock), speaker, multiple}) ;
			matchBlock.clear() ;
			block.clear() ;
			// 最小blockのRTF命令だけで容量超過するDTOを無制限に作りません
			if( blocks.size() > 16 * K_MAXIMUM_TEXT_BYTES / 128) {
				throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
			}
		} ;
	std::size_t at = 0 ; // 候補内の行先頭位置
	// 全本文を一回走査し、実見出し以外の段落・空行・引用は同blockへ保持します
	while( at < body.size()) {
		checkCancelled( stop) ;
		const auto end = body.find( L'\n', at) ; // 改行終端、最後はnpos
		const auto length = end == std::wstring_view::npos ? body.size() - at : end - at ;
		auto	   line = body.substr( at, length) ; // 今回の行だけをborrow
		// CRLFのCRは構文判定から除き、本文改行は下で1回保持します
		if( !line.empty() && line.back() == L'\r') {
			line.remove_suffix( 1) ;
		}
		const auto heading = apiSpeakerHeading( line, ids) ; // 行頭だけを厳密解析
		// 別話者/中立見出しが来たら前blockを閉じ、同じ一人の連続見出しだけをgroup化
		if( heading.boundary && (!started || !heading.speaker || heading.speaker != speaker)) {
			flush() ;
			speaker = heading.speaker ;
			multiple = heading.multiple ;
			started = true ;
		}
		started = true ;
		block.append( line.substr( heading.consumed)) ; // 不明/不正ラベルはconsumed0で全保持
		matchBlock.append( line.substr( heading.matchConsumed)) ; // 厳密metadataだけを分離
		// 最後の行にない改行を創作せず、実改行だけを本文として保ちます
		if( end != std::wstring_view::npos) {
			block.push_back( L'\n') ;
			matchBlock.push_back( L'\n') ;
		}
		at += length + (end == std::wstring_view::npos ? 0 : 1) ;
	}
	flush() ;
	return blocks ;
}
//------------------------------------------------------------------------------
/**
 * @brief 単独改善表示も比較と同じ保存labelのblockを使います
 * @param[in] body 検証済み候補
 * @param[in] source 元話者の正本
 * @param[in] widthTwips pane幅
 * @param[in,out] rtf 有界出力
 * @param[in] stop 行/block間の取消し
 */
//------------------------------------------------------------------------------
void appendApiSpeakerBlocks( std::wstring_view body, const Transcript &source, int widthTwips, std::string &rtf,
							std::stop_token stop)
{
	// 単独表示の従来palette/名前/本文を保持し、解析だけを共用します
	for( const auto &block : apiSpeakerBlocks( body, source, stop)) {
		appendApiSpeakerBlock( block.body, block.speaker, source, widthTwips, rtf, stop) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 一つのnative行へ原文と改善文を上揃えで格納します
 * @param[in] left 生成済み原文セルRTF
 * @param[in] right 生成済み改善セルRTF
 * @param[in] color 元話者のpalette番号
 * @param[in] widthTwips 二列全幅
 * @param[in,out] rtf 所有出力
 * @param[in] heading 共通group見出しならtrue
 * @param[in] rightColor 右の独立palette。負数は左と同色
 * @throws OperationError 全体表示容量超過。セルの高さはnative tableが共有します
 */
//------------------------------------------------------------------------------
void appendComparisonRow( std::string_view left, std::string_view right, int color, int widthTwips, std::string &rtf,
						 bool heading = false, int rightColor = -1)
{
	const auto cell = "\\clvertalt\\clcbpat" + std::to_string( color) ; // 左は元帰属
	const auto otherCell =
		"\\clvertalt\\clcbpat" + std::to_string( rightColor < 0 ? color : rightColor) ; // 右は保存帰属
	const std::string paragraph =
		heading ? "\\pard\\intbl\\sa40\\sb80\\b\\fs20 " : "\\pard\\intbl\\sa80\\sb0\\b0\\fs22 " ; // 左右で同じ段落条件
	constexpr std::size_t K_LIMIT = 16 * K_MAXIMUM_TEXT_BYTES ; // 全表示の既存RTF上限
	// 固定命令の余裕を先に確保し、巨大な両cellの連結前に加算を検証します
	if( rtf.size() > K_LIMIT - 1024 || left.size() > K_LIMIT - rtf.size() - 1024 ||
	   right.size() > K_LIMIT - rtf.size() - 1024 - left.size()) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
	// 中央罫線のRTF状態で右背景がtomAutoColorになるため、色とcell余白で列を区別します
	rtf += "\\trowd\\trgaph140\\trleft0\\trpaddl160\\trpaddr160"
		   "\\trpaddt60\\trpaddb60\\trpaddfl3\\trpaddfr3\\trpaddft3\\trpaddfb3" +
		   cell + "\\cellx" + std::to_string( widthTwips / 2) + otherCell + "\\cellx" + std::to_string( widthTwips) +
		   paragraph + std::string( left) + "\\cell " + paragraph + std::string( right) + "\\cell\\row " ;
	// Unicode展開/両列/長注記も既存の有界表示容量へ含めます
	if( rtf.size() > 16 * K_MAXIMUM_TEXT_BYTES) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief source行から空完全応答も数えた原chunkのpartsを借用します
 * @param[in] request 共有所有要求
 * @param[in] line 非空原文行番号
 * @return 適用可能parts、非対象は空
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::span<const SpeakerTextPart> projectionPartsForLine( const ChatDocumentRequest &request,
																	  const std::size_t			 line)
{
	// 対象の原文と投影が揃う混在行だけを内部表示へ使う
	if( !request.speakerText || !request.speakerAudio || !request.source || line >= request.source->lines.size() ||
	   request.source->lines[line].speakers.size() == 1 ||
	   (!request.compareApiText && request.display != ReadingDisplay::ORIGINAL)) {
		return {} ;
	}
	const auto chunks = speakerTextSourceChunks( *request.speakerAudio) ; // 空prefixを単純同indexで扱わない
	// 空の完全応答を数えたmapが保存prefixを逸脱すれば拒否する
	if( line >= chunks.size() || chunks[line] >= request.speakerText->windows.size()) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	return request.speakerText->windows[chunks[line]].parts ;
}
//------------------------------------------------------------------------------
/**
 * @brief 原byteと対象の現表示名だけをセル内部の色付き段落へします
 * @param[in] request 同sourceの要求
 * @param[in] line 元行
 * @return 無編集本文のRTF
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string projectionCell( const ChatDocumentRequest &request, const std::size_t line)
{
	std::string out ; // 一つの既存左cell内だけで使うRTF
	const auto &original = request.source->lines[line] ; // APIの元chunk本文
	// 当chunkの全partitionを原順に既存左cell内へ描く
	for( const auto &part : projectionPartsForLine( request, line)) {
		const auto text =
			std::string_view( original.text).substr( part.byteBegin, part.byteEnd - part.byteBegin) ; // 原byte
		const int color = part.speaker > 0 ? 2 + (part.speaker - 1) % 6 : 8 ; // 中立に隣話者の色を移さない
		out += "\\pard\\intbl\\shading10000\\cbpat" + std::to_string( color) + "\\highlight" +
			   std::to_string( part.speaker > 0 ? color : 0) + "\\sa80\\sb40 " ;
		// 内容字0の中立区切りだけは独立見出しを作らない
		if( part.speaker > 0 || speakerTextHasContent( text)) {
			TranscriptLine label ; // 表示名は対象の現speaker_namesだけ
			// 確定した対象speakerだけを現表示名へ解決する
			if( part.speaker > 0) {
				label.speakers = {part.speaker} ;
			}
			out += "\\b " + escapeRtf( uiSpeakerLabel( label, *request.source)) + "\\b0\\line " ;
		}
		out += escapeRtf( toWide( text)) + "\\par " ;
	}
	return out + "\\highlight0\\cbpat0 " ;
}
//------------------------------------------------------------------------------
/**
 * @brief 投影した原行を通常カードへ出します
 * @param[in] request 同source要求
 * @param[in] line 元行
 * @param[in,out] rtf 出力
 * @return filter後に表示した内容があればtrue
 */
//------------------------------------------------------------------------------
bool appendProjectionCards( const ChatDocumentRequest &request, const std::size_t line, std::string &rtf)
{
	bool		displayed = false ; // 取消し/filterで本文を削った原文を保存しない
	const auto &original = request.source->lines[line] ; // 元送信窓の原byte
	// 部分ごとの語時刻は作らず、元送信窓の時刻だけを共通見出しへ残します
	rtf += "\\pard\\highlight0\\fs18 " +
		   escapeRtf( toWide( formatTimestamp( original.startMs)) + L" - " + toWide( formatTimestamp( original.endMs))) +
		   "\\par " ;
	// 当chunkのpartsをfilterへ照合し、原byteの順序を保つ
	for( const auto &part : projectionPartsForLine( request, line)) {
		const bool matches = request.filter == K_ALL_SPEAKERS ||
							 (part.speaker > 0 ? request.filter == part.speaker
											   : request.filter < 0) ; // 中立は不明/混在の両filterで残す
		// filterに合わない装飾/本文を表示へ混入しない
		if( !matches) {
			continue ;
		}
		displayed = true ;
		const auto text =
			std::string_view( original.text).substr( part.byteBegin, part.byteEnd - part.byteBegin) ; // 無編集
		// 内容字0は中立の区切り装飾。見出しカードも隣話者色も作らない
		if( part.speaker < 0 && !speakerTextHasContent( text)) {
			rtf += "\\pard\\highlight0\\fs22 " + escapeRtf( toWide( text)) + "\\par " ;
			continue ;
		}
		const int	   color = part.speaker > 0 ? 2 + (part.speaker - 1) % 6 : 8 ; // 対象候補のpalette
		TranscriptLine label ; // 部分本文へ語時刻を捏造しない
		// 参照の名前を移植せず対象番号から見出しを作る
		if( part.speaker > 0) {
			label.speakers = {part.speaker} ;
		}
		rtf += "\\trowd\\trgaph140\\trpaddl160\\trpaddr160\\clcbpat" + std::to_string( color) + "\\cellx" +
			   std::to_string( request.widthTwips) + "\\pard\\intbl\\b\\fs20 " +
			   escapeRtf( uiSpeakerLabel( label, *request.source)) + "\\b0\\line\\fs22 " + escapeRtf( toWide( text)) +
			   "\\cell\\row\\pard\\fs8\\par " ;
		// RTF展開量も通常文書の固定上限内へ抑える
		if( rtf.size() > 16 * K_MAXIMUM_TEXT_BYTES) {
			throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
		}
	}
	return displayed ;
}
/** @brief 原文/改善のそれぞれの帰属を保持する表示専用カードです */
struct ComparisonBlock {
	/// 元時刻は左だけ。右は保存labelに基づく名前
	std::wstring heading ;
	/// 類似性照合でも変更しない表示本文
	std::wstring body ;
	/// 原推定/保存labelに由来する独立palette
	int			 color = 8 ;
	/// 左cell内だけの独立投影。位置照合はbodyのまま
	std::string	 projectedBody ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 対応/未対応カードを同じnative行構造へ出力します
 * @param[in] left 元カード、NULLは未対応の空欄
 * @param[in] right 改善カード、NULLは空欄
 * @param[in] widthTwips 全幅
 * @param[in,out] rtf 有界所有出力
 */
//------------------------------------------------------------------------------
void appendComparisonCards( const ComparisonBlock *left, const ComparisonBlock *right, int widthTwips, std::string &rtf)
{
	const int leftColor = left ? left->color : 10 ; // 空欄を話者色にしません
	const int rightColor = right ? right->color : 10 ; // 両列の色を勝手に同一化しません
	appendComparisonRow( left ? escapeRtf( left->heading) : "", right ? escapeRtf( right->heading) : "", leftColor,
						widthTwips, rtf, true, rightColor) ;
	std::string original = left ? escapeRtf( left->body) : "" ; // 無編集本文だけをescape
	// 既存左cellだけを投影へ置換し、右候補を変更しない
	if( left && !left->projectedBody.empty()) {
		original = left->projectedBody ;
	}
	appendComparisonRow( original, right ? escapeRtf( right->body) : "", leftColor, widthTwips, rtf, false, rightColor) ;
	rtf += "\\pard\\fs8\\sa0\\sb0\\par " ; // 発言カード同士の小さい区切り
}
//------------------------------------------------------------------------------
/**
 * @brief 旧結果を色付きカードと有界な表示位置anchorで比較します
 * @param[in] request 同じ保存owner
 * @param[in] view 原文group
 * @param[in] corrected 保存候補全文
 * @param[in,out] rtf 有界出力
 * @param[in] stop workerの取消し
 */
//------------------------------------------------------------------------------
void appendLegacyComparison( const ChatDocumentRequest &request, const ReadingViewSnapshot &view,
							std::wstring_view corrected, std::string &rtf, std::stop_token stop)
{
	rtf += escapeRtf( resource( IDS_API_COMPARE_LEGACY)) + "\\par " ;
	std::vector<ComparisonBlock> originals ; // 既存ReadingViewの連続話者group
	std::vector<ComparisonBlock> improvements ; // 保存labelだけで得たカード
	std::vector<std::wstring>	 leftText ; // 話者/時刻を除く位置照合専用値
	std::vector<std::wstring>	 rightText ; // 保存本文の表示内容と独立の照合値
	// 原文の名前/時刻/色は通常表示と同じ正本から取得します
	for( const auto &group : view.groups()) {
		checkCancelled( stop) ;
		const auto	   &first = *view.lines()[group.lineIndices.front()].original ; // 元帰属
		ComparisonBlock block ; // 一つの原文カード
		block.color = first.speakers.empty() ? 8 : first.speakers.size() > 1 ? 9 : 2 + (first.speakers.front() - 1) % 6 ;
		block.heading = uiSpeakerLabel( first, view.source()) + L"  " + toWide( formatTimestamp( group.startMs)) + L" - " +
						toWide( formatTimestamp( group.endMs)) ;
		// 元行の実境界だけを保持し、本文を類似性に合わせて編集しません
		for( const auto index : group.lineIndices) {
			checkCancelled( stop) ;
			// 元行間の改行は通常表示と同じです
			if( !block.body.empty()) {
				block.body.push_back( L'\n') ;
			}
			block.body += toWide( view.lines()[index].text) ;
			// 保存投影がある場合だけ左本文の内部partsを選ぶ
			if( request.speakerText) {
				// 既存chunk内で借用する全byte partition
				const auto parts = projectionPartsForLine( request, index) ;
				block.projectedBody += parts.empty() ? escapeRtf( toWide( view.lines()[index].text)) + "\\line "
													 : projectionCell( request, index) ;
			}
		}
		leftText.push_back( block.body) ;
		originals.push_back( std::move( block)) ;
	}
	// 同一話者の連続見出し/再登場/不明/不正/段落も単独表示と同じ解析を使います
	for( auto &parsed : apiSpeakerBlocks( corrected, view.source(), stop)) {
		checkCancelled( stop) ;
		ComparisonBlock	 block ; // 一つの保存改善カード
		const std::array single{parsed.speaker} ; // 表示名照合の同期borrow
		block.color = parsed.speaker ? 2 + (parsed.speaker - 1) % 6 : parsed.multiple ? 9 : 8 ;
		block.heading = parsed.speaker ? uiSpeakerNames( single, view.source())
									   : resource( parsed.multiple ? IDS_MIXED : IDS_API_NEUTRAL) ;
		block.body = std::move( parsed.body) ;
		rightText.push_back( std::move( parsed.matchBody)) ;
		improvements.push_back( std::move( block)) ;
	}
	const auto	alignment = alignComparisonText( leftText, rightText, stop) ; // workerだけの解析
	std::size_t leftAt = 0 ; // 次の原文カード
	std::size_t rightAt = 0 ; // 次の改善カード
	const auto	unpaired =
		//------------------------------------------------------------------------------
		/**
		 * @brief 次anchorより前の未対応カードを片側ずつ全量保持します
		 * @param[in] leftEnd 左の未対応終端
		 * @param[in] rightEnd 右の未対応終端
		 */
		//------------------------------------------------------------------------------
		[&]( std::size_t leftEnd, std::size_t rightEnd)
		{
			// 双方の原順を保ち、n番同士を根拠なく同じ行へ置きません
			while( leftAt < leftEnd || rightAt < rightEnd) {
				checkCancelled( stop) ;
				// 原文未対応カードの反対は空欄にします
				if( leftAt < leftEnd) {
					appendComparisonCards( &originals[leftAt++], nullptr, request.widthTwips, rtf) ;
				}
				// 改善未対応カードも削らず、次の専用行へ出します
				if( rightAt < rightEnd) {
					appendComparisonCards( nullptr, &improvements[rightAt++], request.widthTwips, rtf) ;
				}
			}
		} ;
	// 高確度1:1だけ共通行へ置き、前後の未対応を飛ばしません
	for( const auto pair : alignment.pairs) {
		checkCancelled( stop) ;
		unpaired( pair.left, pair.right) ;
		appendComparisonCards( &originals[leftAt++], &improvements[rightAt++], request.widthTwips, rtf) ;
	}
	unpaired( originals.size(), improvements.size()) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 比較だけを一つのnative表へ構築します
 * @param[in] request 完全採用と原文を所有する要求
 * @param[in] view 同じ原文のgroup
 * @param[in] corrected 変更しない採用本文
 * @param[in] stop 協調取消し
 * @return 共通行高/一つの文書座標を持つ有界RTF
 * @throws OperationError 元ID/候補不整合または表示容量超過
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string comparisonTable( const ChatDocumentRequest &request, const ReadingViewSnapshot &view,
										  std::wstring_view corrected, std::stop_token stop)
{
	const auto &attempt = *request.apiText ; // 呼出元で完全採用/元keyを検証済み
	std::string rtf = std::string( K_CHAT_RTF_HEADER) + escapeRtf( request.heading) + "\\par " +
					  escapeRtf( apiTextProvenanceDisplay( attempt, true)) + "\\par " ;
	// 旧保存本文は明示labelの色と、高確度の表示位置だけを復元します
	if( !apiTextSpeakerProfile( attempt.draft.catalogSnapshot)) {
		appendLegacyComparison( request, view, corrected, rtf, stop) ;
		return rtf + "\\pard\\par}" ;
	}
	// 保存済みraw応答も同じ元投影・完全候補へ照合してから対応を使用します
	if( attempt.draft.projection != apiTextSpeakerProjection( view.source()) ||
	   attempt.results.size() != attempt.draft.chunks.size() ||
	   apiTextCombinedCandidate( attempt) != attempt.candidate) {
		throw OperationError( ErrorKind::INVALID_INPUT, K_API_TEXT_SPEAKER_STRUCTURE_ERROR) ;
	}
	std::vector<std::wstring> improved( view.lines().size()) ; // 元行別の有限改善本文
	// 全chunk/partの検証済み元IDへ一回だけ連結します
	for( std::size_t index = 0; index < attempt.results.size(); ++index) {
		checkCancelled( stop) ;
		const auto &result = attempt.results[index] ; // 同じ保存済み応答
		// 未完応答や未知IDを表示だけで完全採用にしません
		if( !result.complete) {
			throw OperationError( ErrorKind::INVALID_INPUT, K_API_TEXT_SPEAKER_STRUCTURE_ERROR) ;
		}
		// 長発言partの間に架空の区切りを挿入しません
		for( const auto &part : parseApiTextSpeakerParts( attempt.draft, index, result.text)) {
			// 元行範囲を外れた保存値を近隣発言へ割り当てません
			if( part.sourceLine >= improved.size()) {
				throw OperationError( ErrorKind::INVALID_INPUT, K_API_TEXT_SPEAKER_STRUCTURE_ERROR) ;
			}
			improved[part.sourceLine] += toWide( part.text) ;
		}
	}
	// 同話者のgroupは一つの見出し/連続背景を保ち、各元行だけを共通行高にします
	for( const auto &group : view.groups()) {
		checkCancelled( stop) ;
		const auto &first = *view.lines()[group.lineIndices.front()].original ; // 正本の帰属
		const int	color = first.speakers.empty()		? 8
							: first.speakers.size() > 1 ? 9
														: 2 + (first.speakers.front() - 1) % 6 ;
		const auto leftHeading = uiSpeakerLabel( first, view.source()) + L"  " + toWide( formatTimestamp( group.startMs)) +
								 L" - " + toWide( formatTimestamp( group.endMs)) ; // 元時刻は左だけ
		appendComparisonRow( escapeRtf( leftHeading), escapeRtf( uiSpeakerNames( first.speakers, view.source())), color,
							request.widthTwips, rtf, true) ;
		// 各機械IDを一つの2セル行へ対応させ、文長差を次の元行へ伝播させません
		for( const auto index : group.lineIndices) {
			checkCancelled( stop) ;
			// 完全なv3にない元行を空の成功セルとして隠しません
			if( improved[index].empty()) {
				throw OperationError( ErrorKind::INVALID_INPUT, K_API_TEXT_SPEAKER_STRUCTURE_ERROR) ;
			}
			auto original = escapeRtf( toWide( view.lines()[index].text)) ; // 本文は無編集
			// 同じ元行へ対応する投影がある場合だけ描画を替える
			if( !projectionPartsForLine( request, index).empty()) {
				original = projectionCell( request, index) ;
			}
			appendComparisonRow( original, escapeRtf( improved[index]), color, request.widthTwips, rtf) ;
		}
	}
	return rtf + "\\pard\\par}" ;
}

//------------------------------------------------------------------------------
/**
 * @brief 所有Rich Editの幅をtwipへ変換します
 * @param[in] control UI threadが所有するcontrol
 * @return table幅
 * @see koefude::chatDocumentWidth
 */
//------------------------------------------------------------------------------
int chatDocumentWidth( const HWND control)
{
	RECT area{} ; // 自分のRich Editだけのclient幅
	GetClientRect( control, &area) ;
	const int dpi = static_cast<int>( GetDpiForWindow( control)) ; // 論理DPI
	return std::max( 1800, MulDiv( area.right - 58, 1440, std::max( 1, dpi))) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 原文照合と本文/RTFの構築をworkerで行います
 * @param[in] request HWNDのない所有要求
 * @param[in] stop 協調取消し
 * @return 全行に対応する所有文書
 * @throws OperationError 不正要求/本文/容量/取消し
 * @note 音声/モデルを開かず、呼出しthreadの言語だけを設定します
 * @see koefude::prepareChatDocument
 */
//------------------------------------------------------------------------------
PreparedChatDocument prepareChatDocument( const ChatDocumentRequest &request, const std::stop_token stop)
{
	checkCancelled( stop) ;
	// HWND/元音声なしの要求でも、ID・幅・言語と元本文を確認する
	if( !request.source || request.historyId == 0 || request.widthTwips < 1800 || request.widthTwips > 1'000'000 ||
	   request.heading.size() > 65536 || !setUiLanguage( request.language)) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	ReadingSourceKey key{request.historyId, {}} ; // 検証をworkerで行う元の履歴key
	// 整形版は保存した原文hashを照合し、原文版は現在の原文を固定する
	if( !request.compareApiText && request.display == ReadingDisplay::FORMATTED && request.formatted) {
		key.sha256 = request.formatted->source.sha256 ;
	} else {
		key.sha256 = fingerprintReadingSource( *request.source) ;
	}
	PreparedChatDocument document ; // UIへ渡す全byteの所有者
	document.request = request ;
	document.view = std::make_shared<const ReadingViewSnapshot>(
		makeReadingView( request.source, key, request.compareApiText ? nullptr : request.formatted,
						request.compareApiText ? ReadingDisplay::ORIGINAL : request.display)) ;
	const auto &view = *document.view ; // 原文/置換のownerに閉じた投影
	// 再読/表示で推論せず、小bindingだけを先行検証する
	if( request.speakerText) {
		// 異なるsource/plan/prefixの派生値を表示へ使わない
		if( !request.speakerAudio || request.speakerText->target.sourceSha != key.sha256 ||
		   speakerTextTarget( *request.speakerAudio) != request.speakerText->target) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
	}
	// API全文はnative PCM証拠を要求せず、原文keyと候補SHAで照合します
	if( request.showApiText) {
		if( !request.apiText || request.apiText->draft.source != key ||
		   request.apiText->state != ApiTextState::CANDIDATE_READY ||
		   request.apiText->candidateSha != apiTextDigest( request.apiText->candidate)) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
		document.correctedFullText = toWide( request.apiText->candidate) ;
		document.fullText = document.correctedFullText ;
		// 比較は二文書の同期ではなく、単一表の同じ行へ左右を構築します
		if( request.compareApiText) {
			document.rtf = comparisonTable( request, view, document.correctedFullText, stop) ;
			checkCancelled( stop) ;
			return document ;
		}
		document.rtf = std::string( K_CHAT_RTF_HEADER) + escapeRtf( request.heading) + "\\par " +
					   escapeRtf( apiTextProvenanceDisplay( *request.apiText, true)) + "\\par " +
					   escapeRtf( resource( IDS_API_ADOPTED)) + "\\par " +
					   escapeRtf( resource( apiTextSpeakerProfile( request.apiText->draft.catalogSnapshot)
											  ? IDS_API_TURN_NOTICE
											  : IDS_API_SPEAKER_NOTICE)) +
					   "\\par " ;
		// 新規v3は機械ID、legacyは保存本文の明示ラベルだけで表示します
		if( apiTextSpeakerProfile( request.apiText->draft.catalogSnapshot)) {
			appendApiTurnBlocks( *request.apiText, *request.source, request.widthTwips, document.rtf, stop) ;
		} else {
			appendApiSpeakerBlocks( document.correctedFullText, *request.source, request.widthTwips, document.rtf, stop) ;
		}
		document.rtf += "}" ;
		checkCancelled( stop) ;
		return document ;
	}
	// bounded bodyはstorage検証済みownerでありUIからJを読みません
	if( request.bounded) {
		const auto &descriptor = request.bounded->descriptor() ; /* 保存証拠 */
		if( descriptor.result.request.source != key || !request.source->pcmIdentity ||
		   *request.source->pcmIdentity != descriptor.result.pcm || (request.wholeText && !request.showBoundedPrefix)) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( request.bounded->complete()) {
			document.correctedFullText = toWide( request.bounded->text()) ;
		}
	}
	// 明示prefix表示でもcopy対象は保存済み有効全文だけです
	if( request.effectiveBounded) {
		const auto &descriptor = request.effectiveBounded->descriptor() ; /* 保存証拠 */
		if( !request.effectiveBounded->complete() || descriptor.result.request.source != key ||
		   !request.source->pcmIdentity || *request.source->pcmIdentity != descriptor.result.pcm) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
		document.correctedFullText = toWide( request.effectiveBounded->text()) ;
	}
	// 既存V1の検証と変換は維持します
	if( request.wholeText) {
		validatePersistedWholeTextCorrection( *request.source, key, *request.wholeText, stop) ;
		document.correctedFullText = toWide( request.wholeText->text) ;
	}
	// 全文と明示prefixは時刻/話者を発明しない中立本文です
	if( request.showWholeText || request.showBoundedPrefix) {
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( (!request.wholeText && !request.bounded) || request.display != ReadingDisplay::ORIGINAL ||
		   (request.showBoundedPrefix && (!request.bounded || request.bounded->complete()))) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
		std::wstring heading = request.heading ; // 同期workerの表示用説明
		std::wstring prefix ; // 未完だけ明示label
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( request.showBoundedPrefix) {
			const auto &d = request.bounded->descriptor() ; /* 元終端 */
			const auto	state = d.state ; /* 表示分類 */
			prefix = resource( state == WholeTextBoundedAttemptState::CANCELLED	   ? IDS_BOUNDED_CANCELLED
							  : state == WholeTextBoundedAttemptState::INTERRUPTED ? IDS_BOUNDED_INTERRUPTED
																				   : IDS_BOUNDED_FAILED) +
					 L" " + std::to_wstring( d.result.committed.count) + L"/" +
					 std::to_wstring( d.result.binding.recordCount) + L"\r\n" ;
		}
		std::wstring prefixBody ; // 明示prefixだけ一回変換
		// Completeのcopy用UTF16を再複製せずRTFへ借用します
		if( request.showBoundedPrefix) {
			prefixBody = toWide( request.bounded->text()) ;
		}
		// この同期境界で所有する値、または確定状態の検証値です
		const std::wstring_view body = request.showBoundedPrefix
										   ? std::wstring_view( prefixBody)
										   : std::wstring_view( document.correctedFullText) ; // worker内借用
		document.rtf = "{\\rtf1\\ansi\\deff0\\uc1{\\fonttbl{\\f0 MS Gothic;}}" ;
		document.rtf += "\\viewkind4\\f0\\fs22\\pard " + escapeRtf( heading) + "\\par " +
						escapeRtf( prefix.empty() ? resource( IDS_WHOLE_TEXT_TITLE) : prefix) + "\\par " +
						escapeRtf( body) + "}" ;
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( document.rtf.size() > 16 * K_MAXIMUM_TEXT_BYTES) {
			throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
		}
		document.fullText = toWide( renderTranscript( *request.source, ExportFormat::PLAIN_TEXT)) ;
		checkCancelled( stop) ;
		return document ;
	}
	std::wstring heading = request.heading ; // 版表示は元の見出しと区別する
	// 整形版は元時刻/話者の説明と件数を明示する
	if( request.display == ReadingDisplay::FORMATTED) {
		std::size_t changed = 0 ; // 元行の変更数
		std::size_t retained = 0 ; // 元本文を理由付き保持した行数
		// 無変更行も分母の元行として保持し、出力された行を省略しない
		for( const auto &line : view.lines()) {
			changed += line.kind == ReadingLineKind::REPLACED ? 1 : 0 ;
			retained += line.kind == ReadingLineKind::ORIGINAL_RETAINED ? 1 : 0 ;
		}
		heading += resource( IDS_READING_TITLE) + L"\r\n" ;
		heading += resource( IDS_READING_COUNTS) + std::to_wstring( changed) + resource( IDS_READING_RETAINED_COUNT) +
				   std::to_wstring( retained) + L"\r\n" ;
	}
	document.rtf = std::string( K_CHAT_RTF_HEADER) + escapeRtf( heading) + "\\par " ;
	bool displayed = false ; // 話者filter後の表示有無
	// 元行groupを使い、別話者を跨ぐ本文の結合を防ぐ
	for( const auto &group : view.groups()) {
		checkCancelled( stop) ;
		const auto &first = *view.lines()[group.lineIndices.front()].original ; // 元の帰属
		// 元単一話者groupは従前全文を維持し、混在chunk行の内部だけを投影する
		if( request.speakerText && first.speakers.size() != 1 && request.display == ReadingDisplay::ORIGINAL) {
			// group内の各元行を独立に描き、空chunkで行番号をずらさない
			for( const auto line : group.lineIndices) {
				displayed = appendProjectionCards( request, line, document.rtf) || displayed ;
			}
			continue ;
		}
		// filterは表示だけへ適用し、元本文・全文出力を削らない
		if( !matchesSpeakerFilter( first.speakers, request.filter)) {
			continue ;
		}
		displayed = true ;
		// この同期境界で所有する値、または確定状態の検証値です
		const int color = first.speakers.empty()	  ? 8
						  : first.speakers.size() > 1 ? 9
													  : 2 + (first.speakers.front() - 1) % 6 ; // 同じ番号は同じ色
		document.rtf += "\\trowd\\trgaph140\\trleft0\\trpaddl160\\trpaddr160"
						"\\trpaddt100\\trpaddb100\\trpaddfl3\\trpaddfr3\\trpaddft3\\trpaddfb3\\clcbpat" +
						std::to_string( color) + "\\cellx" + std::to_string( request.widthTwips) +
						"\\pard\\intbl\\sa120\\sb100\\b\\fs20 " + escapeRtf( uiSpeakerLabel( first, view.source())) +
						"  \\b0\\fs18 " + formatTimestamp( group.startMs) + " - " + formatTimestamp( group.endMs) +
						"\\line\\fs22 " ;
		bool firstLine = true ; // 同じ話者の反復も一行ずつ残す
		// 元indexを保ち、置換/原文の本文だけを投影する
		for( const auto index : group.lineIndices) {
			checkCancelled( stop) ;
			// 元行の間にだけ改行を入れ、語句を連結しない
			if( !firstLine) {
				document.rtf += "\\line " ;
			}
			firstLine = false ;
			const auto text = toWide( view.lines()[index].text) ; // 同じ表示本文のUTF-16
			document.rtf += escapeRtf( text) ;
			// Unicode escapeの展開を含めて既存128MiB上限を守る
			if( document.rtf.size() > 16 * K_MAXIMUM_TEXT_BYTES) {
				throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
			}
		}
		document.rtf += "\\cell\\row\\pard\\fs8\\sa0\\sb0\\par " ;
		// groupのheaderの増加も同じ上限へ含める
		if( document.rtf.size() > 16 * K_MAXIMUM_TEXT_BYTES) {
			throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
		}
	}
	// 対象なしは元の失敗や無音と区別する
	if( !displayed) {
		document.rtf += escapeRtf( resource( IDS_FILTER_EMPTY)) + "\\par " ;
	}
	document.rtf += "}" ;
	ExportJobRequest exportRequest ; // filterとは独立した全話者の受付版
	exportRequest.source = request.source ;
	exportRequest.sourceKey = key ;
	exportRequest.formatted = request.formatted ;
	exportRequest.display = request.display ;
	document.fullText = toWide( renderReadingExport( exportRequest)) ;
	checkCancelled( stop) ;
	return document ;
}

//------------------------------------------------------------------------------
/**
 * @brief 準備済みRTFだけを所有Rich Editへ適用します
 * @param[in] control 自分のUI threadのcontrol
 * @param[in] document 所有文書の同期borrow
 * @param[in] preserveView trueなら選択/scrollを保持します
 * @throws OperationError stream失敗。原文/整形snapshotは変更しません
 * @see koefude::setPreparedChatDocument
 */
//------------------------------------------------------------------------------
void setPreparedChatDocument( const HWND control, const PreparedChatDocument &document, const bool preserveView)
{
	CHARRANGE selection{} ; // resize前の選択範囲
	POINT	  scroll{} ; // resize前のscroll位置
	// 単なるresizeでは読んでいる位置とcopy対象を保持する
	if( preserveView) {
		SendMessageW( control, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>( &selection)) ;
		SendMessageW( control, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>( &scroll)) ;
		scroll.y = comparisonScrollRange( control).position ; // 16bit返却の長文切断を避ける
	}
	StreamState state{document.rtf} ; // EM_STREAMIN呼出し終了まで有効な借用view
	EDITSTREAM	stream{} ; // 同期read callbackと診断値
	stream.dwCookie = reinterpret_cast<DWORD_PTR>( &state) ; // 同期APIの借用pointer境界
	stream.pfnCallback = streamRtf ;
	SendMessageW( control, WM_SETREDRAW, FALSE, 0) ;
	SendMessageW( control, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>( &stream)) ;
	// 比較では暫定rangeへ長文の保存位置をclampしないよう、復元前に末尾を確定します
	if( document.request.compareApiText) {
		completeComparisonScrollLayout( control) ;
	}
	SendMessageW( control, EM_SETMODIFY, FALSE, 0) ;
	SendMessageW( control, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>( &selection)) ;
	SendMessageW( control, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>( &scroll)) ;
	SendMessageW( control, WM_SETREDRAW, TRUE, 0) ;
	InvalidateRect( control, nullptr, TRUE) ;
	// 不完全なRTF読込みを正常な全本文表示と扱わない
	if( stream.dwError != 0 || !state.remaining.empty()) {
		throw OperationError( ErrorKind::INTERNAL, stream.dwError) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief ファイル説明と背景色付き発言を標準Rich Editへ設定します
 * @param[in] control 呼出しUI threadが所有するRich Edit HWND
 * @param[in] heading ファイル名と状態の所有文字列のborrow
 * @param[in] transcript 表示する不変結果
 * @param[in] preserveView trueならscrollとtext選択を維持します
 * @param[in] filter 全話者・不明・混在の定数または表示する話者ID
 * @note 同じ一人の連続発言は改行で結合し、推論結果そのものは変更しません
 * @see koefude::setChatDocument
 */
//------------------------------------------------------------------------------
void setChatDocument( const HWND control, const std::wstring_view heading, const Transcript &transcript,
					 const bool preserveView, const int filter)
{
	RECT area{} ; // 現在のchat本文幅を取得するclient矩形
	GetClientRect( control, &area) ;
	const int	dpi = static_cast<int>( GetDpiForWindow( control)) ; // table幅をtwipへ換算するDPI
	// この同期境界で所有する値、または確定状態の検証値です
	const int	width = std::max( 1800, MulDiv( area.right - 58, 1440, dpi)) ; // 右scrollbarと余白を除く幅
	// font・話者色・見出しから組み立てる所有RTF本文
	std::string rtf = std::string( K_CHAT_RTF_HEADER) + escapeRtf( heading) + "\\par " ;
	// この同期境界で所有する値、または確定状態の検証値です
	const auto	groups = groupConsecutiveSpeech( transcript.lines) ; // 同一話者が続く部分の表示copy
	bool		displayed = false ; // filter適用後に発話がある場合true
	// 発話ブロックの全横幅に色を付け、同じ話者番号は同じ色を再利用する
	for( const auto &group : groups) {
		// group作成後に除外し、非表示の話者を跨いで結合しない
		if( !matchesSpeakerFilter( group.speakers, filter)) {
			continue ;
		}
		displayed = true ;
		// この同期境界で所有する値、または確定状態の検証値です
		const int color = group.speakers.empty() ? 8
						  : group.speakers.size() > 1
							  ? 9
							  : 2 + (group.speakers.front() - 1) % 6 ; // 不明・混在は専用の中立色
		rtf += "\\trowd\\trgaph140\\trleft0\\trpaddl160\\trpaddr160\\trpaddt100\\trpaddb100"
			   "\\trpaddfl3\\trpaddfr3\\trpaddft3\\trpaddfb3\\clcbpat" +
			   std::to_string( color) + "\\cellx" + std::to_string( width) + "\\pard\\intbl\\sa120\\sb100\\b\\fs20 " ;
		rtf += escapeRtf( uiSpeakerLabel( group, transcript)) + "  \\b0\\fs18 " + formatTimestamp( group.startMs) + " - " +
			   formatTimestamp( group.endMs) + "\\line\\fs22 " + escapeRtf( toWide( group.text)) ;
		rtf += "\\cell\\row\\pard\\fs8\\sa0\\sb0\\par " ;
		// Unicode escapeによる展開分にも上限を設ける
		if( rtf.size() > 16 * K_MAXIMUM_TEXT_BYTES) {
			throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
		}
	}
	// 対象なしを空白や処理失敗と誤解させない
	if( !displayed) {
		rtf += escapeRtf( resource( IDS_FILTER_EMPTY)) + "\\par " ;
	}
	rtf += "}" ;
	PreparedChatDocument document ; // 従来呼出しも同じstream境界を使う
	document.rtf = std::move( rtf) ;
	setPreparedChatDocument( control, document, preserveView) ;
}

} // namespace koefude
