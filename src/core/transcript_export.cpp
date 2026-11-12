//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file transcript_export.cpp
 * @brief 時刻・話者付きtextを生成し、明示出力先へ保存します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 Unicode対応のTXT/SRT/VTT保存を追加
 * 0.1.0 Shunji Ogawa 2026/09/10 録音時間の固定上限を除き、64bitの全体時刻を保持
 * 0.1.0 Shunji Ogawa 2026/09/11 交代未確定と重なり推定を区別し、根拠の範囲を保存
 * 0.1.0 Shunji Ogawa 2026/10/04 整形TXTも構築済み全文の同じatomic writerで保存
 * 0.1.0 Shunji Ogawa 2026/10/19 宣言の役割を明記
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
#include "core/transcript_export.h"
#include "core/speaker_attribution.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <windows.h>

// CNG宣言はWindows基本型の定義後にincludeする
#include <bcrypt.h>

#include "platform/win_helpers.h"

namespace koefude {
namespace {

//------------------------------------------------------------------------------
/**
 * @brief 字幕markupの記号をtextとして保持するためescapeします
 * @param[in] input 有効UTF-8本文
 * @return 所有escape済み文字列
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string escapeSubtitle( const std::string_view input)
{
	std::string output ; // 字幕parserがmarkupとして解釈しない本文
	// 全byteを確認し、ASCIIの特殊記号だけをentityへ置換する
	for( const char character : input) {
		// 字幕内のmarkupと本文の境界を保持する
		switch( character) {
			case '&': // entityの開始を本文として表示
				output += "&amp;" ;
				break ;
			case '<': // HTML風tagの開始を禁止
				output += "&lt;" ;
				break ;
			case '>': // timestamp矢印やtag終端との混同を防止
				output += "&gt;" ;
				break ;
			case '\r': // 改行はLFへ統一
				break ;
			case '\n': // 空行で字幕blockが分断されないよう本文内は空白へ変換
				output += ' ' ;
				break ;
			default: // そのほかのUTF-8 byteは保持
				output += character ;
				break ;
		}
	}
	return output ;
}

//------------------------------------------------------------------------------
/**
 * @brief 初出順の話者名だけをUTF-8で生成します
 * @param[in] speakers 1始まりの話者番号。空は不明、複数は候補
 * @param[in] names 利用者の表示名。未登録番号には既定名を使用
 * @return 表示用label
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string speakerLabel( const std::vector<int> &speakers, const std::map<int, std::string> &names)
{
	// 音声から帰属できなかったtextへ架空の話者を付けない
	if( speakers.empty()) {
		return "話者不明" ;
	}
	std::string label ; // 複数番号を保持する表示文字列
	// 候補の番号を順に示し、混在区間を一人へ強制しない
	for( const int speaker : speakers) {
		// 2人目以降は区切りを挿入する
		if( !label.empty()) {
			label += "・" ;
		}
		const auto name = names.find( speaker) ; // このfile内だけで有効な表示名
		label += name == names.end() ? "話者" + std::to_string( speaker) : name->second ;
	}
	return label ;
}

//------------------------------------------------------------------------------
/**
 * @brief 結合した単一候補の範囲を整列し、同じIDの重複部分をまとめます
 * @param[in,out] spans 所有する表示用copy。元の各行は変更しません
 */
//------------------------------------------------------------------------------
void coalesceSingleSpans( std::vector<AttributionSpan> &spans)
{
	std::sort( spans.begin(), spans.end(),
			  //------------------------------------------------------------------------------
			  /**
			   * @brief 重なるASR行の根拠も開始時刻順に整列します
			   * @param[in] left 比較する推定範囲
			   * @param[in] right 比較する推定範囲
			   * @return leftが先ならtrue
			   */
			  //------------------------------------------------------------------------------
			  []( const AttributionSpan &left, const AttributionSpan &right)
			  {
				  return left.startMs < right.startMs ;
			  }) ;
	std::size_t written = 0 ; // 和集合として確定した先頭側の範囲数
	// 一つの候補の接触・重複だけをまとめ、推定のない隙間は保持する
	for( std::size_t index = 0; index < spans.size(); ++index) { // 読み込む元範囲の番号
		// 最後の確定範囲と正の隙間がなければ終端だけを広げる
		if( written > 0 && spans[index].startMs <= spans[written - 1].endMs) {
			spans[written - 1].endMs = std::max( spans[written - 1].endMs, spans[index].endMs) ;
		} else {
			// 同じ要素へのmoveを避け、未読の範囲を破壊しない
			if( written != index) {
				spans[written] = std::move( spans[index]) ;
			}
			++written ;
		}
	}
	spans.resize( written) ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 連続する同じ一人の発言を改行でまとめます
 * @param[in] lines 元の時刻付き区間。変更しません
 * @return 表示とTXT用の所有区間。不明・混在・別話者を跨いで結合しません
 * @see koefude::groupConsecutiveSpeech
 */
//------------------------------------------------------------------------------
std::vector<TranscriptLine> groupConsecutiveSpeech( const std::span<const TranscriptLine> lines)
{
	std::vector<TranscriptLine> groups ; // 元区間を変更しない表示用の所有結果
	std::size_t					total = 0 ; // 改行を含む表示textの合計byte数
	std::size_t					attributionItems = 0 ; // copyする全範囲と候補IDの合計
	// 時系列順に走査し、間に別話者を挟まない場合だけ同じまとまりにする
	for( const auto &line : lines) {
		validateSpeakerAttribution( line, attributionItems) ;
		// 追加分を加算前に照合し、UI表示用copyも有界に保つ
		if( groups.size() >= K_MAXIMUM_SEGMENTS || line.text.size() >= 2 * K_MAXIMUM_TEXT_BYTES - total) {
			throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
		}
		total += line.text.size() + 1 ;
		// 不明・混在を同じ一人の連続発話と推測しない
		if( !groups.empty() && line.speakers.size() == 1 && groups.back().speakers == line.speakers &&
		   groups.back().attribution.kind == line.attribution.kind && line.startMs >= groups.back().startMs) {
			groups.back().text += "\n" + line.text ;
			groups.back().endMs = std::max( groups.back().endMs, line.endMs) ;
			auto &spans = groups.back().attribution.spans ; // 表示用copyの推定範囲
			spans.insert( spans.end(), line.attribution.spans.begin(), line.attribution.spans.end()) ;
		} else {
			groups.push_back( line) ;
		}
	}
	// 単一候補のまとめ後も、元の根拠のない時間を塗りつぶさない
	for( auto &group : groups) { // 表示とTXTだけが所有するまとまり
		// 同じ候補の複数ASR行だけが結合されているので、範囲の和集合を取れる
		if( group.attribution.kind == AttributionKind::SINGLE_SPEAKER) {
			coalesceSingleSpans( group.attribution.spans) ;
		}
	}
	return groups ;
}

//------------------------------------------------------------------------------
/**
 * @brief 話者番号と明示編集名から表示labelを作ります
 * @param[in] line 表示する区間
 * @param[in] transcript このfileの編集名mapを持つ結果
 * @return 帰属未確定、交代、推定重なり、理由未記録を区別するUTF-8 label
 * @see koefude::formatSpeakerLabel
 */
//------------------------------------------------------------------------------
std::string formatSpeakerLabel( const TranscriptLine &line, const Transcript &transcript)
{
	auto label = speakerLabel( line.speakers, transcript.speakerNames) ; // 元候補の全表示名
	// 候補数だけで同時発話を確定せず、記録した理由を表示する
	switch( line.attribution.kind) {
		case AttributionKind::UNAVAILABLE: // 旧結果には後から推定理由を与えない
			// 単一候補の名前表示は従来どおりとし、不明・複数候補に未記録を示す
			if( line.speakers.size() != 1) {
				label += "（理由未記録）" ;
			}
			break ;
		case AttributionKind::UNASSIGNED: // 候補の時間的根拠がない本文
			label += "（割当未確定）" ;
			break ;
		case AttributionKind::SINGLE_SPEAKER: // 一つの候補の通常表示
			break ;
		case AttributionKind::UNRESOLVED_CHANGE: // 重なりのない複数候補が残る本文
			label += "（話者交代・割当未確定）" ;
			break ;
		case AttributionKind::ESTIMATED_OVERLAP: // 一部に候補の同時範囲がある本文
			label += "（重なり推定・割当未確定）" ;
			break ;
		default: // 未知種別を正常な話者labelへ置き換えない
			throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
	return label ;
}

//------------------------------------------------------------------------------
/**
 * @brief 複数候補が有効な推定時間範囲を、本文と区別した注記へ変換します
 * @param[in] line 検証済みの推定情報を持つ区間
 * @param[in] transcript このfileで利用者が指定した表示名
 * @return 所有UTF-8注記。複数候補の根拠を記録していない行は空文字列
 * @throws OperationError 注記の出力容量上限超過
 * @see koefude::formatAttributionRanges
 */
//------------------------------------------------------------------------------
std::string formatAttributionRanges( const TranscriptLine &line, const Transcript &transcript)
{
	// 理由未記録や一人以下の通常行には、空の根拠欄を追加しない
	if( line.speakers.size() < 2 || line.attribution.kind == AttributionKind::UNAVAILABLE) {
		return {} ;
	}
	std::string annotation = "推定区間（本文との対応は未確定）: " ; // 本文と区別した注記
	bool		first = true ; // 最初の推定範囲には区切りを付けない
	// 各範囲の元時刻と、その時点の候補集合を全て保持する
	for( const auto &span : line.attribution.spans) { // 検証済みの半開区間
		// 二つ目以降の範囲は同じ注記内で区切る
		if( !first) {
			annotation += "; " ;
		}
		first = false ;
		annotation += "[" + formatTimestamp( span.startMs) + " - " + formatTimestamp( span.endMs) + "] " +
					  speakerLabel( span.speakers, transcript.speakerNames) ;
		// 大きな候補列や名前で注記だけが無制限に増えないようにする
		if( annotation.size() > 4 * K_MAXIMUM_TEXT_BYTES) {
			throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
		}
	}
	return annotation ;
}

//------------------------------------------------------------------------------
/**
 * @brief 元音声の時刻を固定形式へ変換します
 * @param[in] milliseconds 0以上の時刻（ms）
 * @param[in] comma trueはSRTの小数点表記
 * @return HH:MM:SS.mmmまたはHH:MM:SS,mmm
 * @see koefude::formatTimestamp
 */
//------------------------------------------------------------------------------
std::string formatTimestamp( const std::int64_t milliseconds, const bool comma)
{
	const auto			 value = std::max<std::int64_t>( 0, milliseconds) ; // 表示に使う非負時刻
	std::array<char, 48> buffer{} ; // 最大int64時刻にも余裕のある固定buffer
	const int			 written = std::snprintf(
		   buffer.data(), buffer.size(), "%02lld:%02lld:%02lld%c%03lld", static_cast<long long>( value / 3'600'000),
		   static_cast<long long>( value / 60'000 % 60), static_cast<long long>( value / 1000 % 60), comma ? ',' : '.',
		   static_cast<long long>( value % 1000)) ; // 実出力長
	// formatterがbufferへ収められなかった場合は部分値を返さない
	if( written < 0 || static_cast<std::size_t>( written) >= buffer.size()) {
		throw OperationError( ErrorKind::INTERNAL) ;
	}
	return std::string( buffer.data(), static_cast<std::size_t>( written)) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 表示とcopyに使うUTF-8文字列を生成します
 * @param[in] transcript 所有結果の借用参照
 * @param[in] format 出力形式
 * @return 所有UTF-8文字列
 * @throws OperationError 未定義形式・出力上限超過
 * @see koefude::renderTranscript
 */
//------------------------------------------------------------------------------
std::string renderTranscript( const Transcript &transcript, const ExportFormat format)
{
	// 未定義形式を既定のtext保存へ暗黙変換しない
	if( format != ExportFormat::PLAIN_TEXT && format != ExportFormat::SRT && format != ExportFormat::VTT) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	std::string output = format == ExportFormat::VTT ? "WEBVTT\n\n" : "" ; // 所有出力文字列
	std::size_t cue = 0 ; // SRT/VTTの1始まりcue番号
	std::size_t attributionItems = 0 ; // 字幕の元行も同じ根拠容量へ照合する合計
	const auto	grouped = format == ExportFormat::PLAIN_TEXT ? groupConsecutiveSpeech( transcript.lines)
															 : std::vector<TranscriptLine>{} ; // TXTだけの発言まとめ
	const auto &lines = format == ExportFormat::PLAIN_TEXT ? grouped : transcript.lines ; // 字幕のcueは保持
	// 時系列の全表示行を、選択形式のblockへ変換する
	for( const auto &line : lines) {
		validateSpeakerAttribution( line, attributionItems) ;
		// 外部DTOの時刻も出力直前に検証し、字幕終端の加算overflowを防ぐ
		if( line.startMs < 0 || line.endMs < line.startMs || line.endMs > K_MAXIMUM_TIMELINE_MS) {
			throw OperationError( ErrorKind::INVALID_TIMELINE) ;
		}
		const auto label = formatSpeakerLabel( line, transcript) ; // 名前と記録済みの理由
		const auto annotation = formatAttributionRanges( line, transcript) ; // 元時刻の推定範囲
		// text形式は原文の改行と句読点を保持する
		if( format == ExportFormat::PLAIN_TEXT) {
			output += "[" + formatTimestamp( line.startMs) + " - " + formatTimestamp( line.endMs) + "] " + label + "\n" +
					  line.text + "\n" ;
			// 推定範囲は本文の後の独立した注記として保存する
			if( !annotation.empty()) {
				output += annotation + "\n" ;
			}
			output += "\n" ;
		} else {
			output += std::to_string( ++cue) + "\n" + formatTimestamp( line.startMs, format == ExportFormat::SRT) +
					  " --> " + formatTimestamp( std::max( line.endMs, line.startMs + 1), format == ExportFormat::SRT) +
					  "\n" + escapeSubtitle( label) + ": " + escapeSubtitle( line.text) + "\n" ;
			// cueを分割せず、推定範囲もmarkupをescapeした同じ字幕内の注記にする
			if( !annotation.empty()) {
				output += escapeSubtitle( annotation) + "\n" ;
			}
			output += "\n" ;
		}
		// annotationやescapeによる増加にも別の上限を適用する
		if( output.size() > 4 * K_MAXIMUM_TEXT_BYTES) {
			throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
		}
	}
	return output ;
}

//------------------------------------------------------------------------------
/**
 * @brief 明示された出力先へ一時fileから置き換えて保存します
 * @param[in] transcript 変更しない所有結果
 * @param[in] output 利用者が選んだ出力path。音声拡張子は禁止
 * @param[in] format 保存形式
 * @throws OperationError path不正、書込み失敗、上限超過
 * @note blocking処理です。workerで実行し、入力音声へ書き込みません
 * @see koefude::saveTranscript
 */
//------------------------------------------------------------------------------
void saveTranscript( const Transcript &transcript, const std::filesystem::path &output, const ExportFormat format)
{
	const auto contents = renderTranscript( transcript, format) ; // 元形式の全文byte列
	saveRenderedTranscript( contents, output, format) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 構築済みUTF-8全文を原文と同じatomic writerで保存します
 * @param[in] contents 呼出し中だけ借用する全文。32MiB以内
 * @param[in] output 明示出力先。選択形式の拡張子を要求
 * @param[in] format 構築済み本文の形式
 * @throws OperationError 形式/Unicode/容量/書込み失敗。旧出力を保持
 * @note worker限定。原音声やengineを参照しません
 * @see koefude::saveRenderedTranscript
 */
//------------------------------------------------------------------------------
void saveRenderedTranscript( const std::string_view contents, const std::filesystem::path &output,
							const ExportFormat format)
{
	// 型境界で未知形式・過大な全文・埋込みNULを拒否してからfileを作る
	if( (format != ExportFormat::PLAIN_TEXT && format != ExportFormat::SRT && format != ExportFormat::VTT) ||
	   contents.size() > 4 * K_MAXIMUM_TEXT_BYTES || contents.find( '\0') != std::string_view::npos) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	static_cast<void>( toWide( contents)) ; // 不正UTF-8を保存前に拒否する
	std::wstring extension = output.extension().native() ; // 出力形式と照合する拡張子
	// 大文字小文字を正規化して形式を比較する
	for( wchar_t &character : extension) {
		character = static_cast<wchar_t>( std::towlower( character)) ;
	}
	const std::wstring expected = format == ExportFormat::PLAIN_TEXT ? L".txt"
								  : format == ExportFormat::SRT		 ? L".srt"
																	 : L".vtt" ; // 許可する拡張子
	// 原音声や別形式を同じpathで上書きしない
	if( output.empty() || extension != expected) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	std::array<unsigned char, 12> random{} ; // 一時fileの衝突を避ける乱数
	const NTSTATUS randomStatus = BCryptGenRandom( nullptr, random.data(), static_cast<ULONG>( random.size()),
												  BCRYPT_USE_SYSTEM_PREFERRED_RNG) ; // OS乱数の診断値
	// 乱数を得られない場合に予測可能な一時fileへ書き込まない
	if( randomStatus < 0) {
		throw OperationError( ErrorKind::SAVE_FAILED, static_cast<std::uint32_t>( randomStatus)) ;
	}
	std::wstring				suffix ; // 同じdirectory内の一時名
	constexpr std::wstring_view K_HEX = L"0123456789abcdef" ; // 乱数をfile名へ変換する表
	// 乱数の全byteをASCIIのhexへ変換する
	for( const auto byte : random) {
		suffix += K_HEX[byte >> 4U] ;
		suffix += K_HEX[byte & 15U] ;
	}
	const std::filesystem::path temporary( output.native() + L".partial-" + suffix) ; // 所有一時path
	UniqueHandle file( CreateFileW( temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
								  nullptr)) ; // 未使用名だけへ新規作成
	// 衝突やアクセス拒否で既存fileを開き直さない
	if( file.get() == INVALID_HANDLE_VALUE) {
		throw OperationError( ErrorKind::SAVE_FAILED, GetLastError()) ;
	}
	try {
		std::size_t offset = 0 ; // 完了した書込みbyte数
		// 全byteを書き終えるまで有限blockで進め、部分書込みも処理する
		while( offset < contents.size()) {
			// 一回のWriteFileへ要求する最大64KiBのbyte数
			const DWORD request = static_cast<DWORD>( std::min<std::size_t>( 64 * 1024, contents.size() - offset)) ;
			DWORD		written = 0 ; // 今回の実書込みbyte数
			// 無進行や失敗時は元の出力fileを保持する
			if( !WriteFile( file.get(), contents.data() + offset, request, &written, nullptr) || written == 0) {
				throw OperationError( ErrorKind::SAVE_FAILED, GetLastError()) ;
			}
			offset += written ;
		}
		// 置換前にI/Oの成功を確定させる
		if( !FlushFileBuffers( file.get())) {
			throw OperationError( ErrorKind::SAVE_FAILED, GetLastError()) ;
		}
		file.reset() ;
		// 同じdirectory内の検証済み一時fileで出力を置き換える
		if( !MoveFileExW( temporary.c_str(), output.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			throw OperationError( ErrorKind::SAVE_FAILED, GetLastError()) ;
		}
	} catch( ...) {
		// 失敗時に自分が作成した一時fileだけを回収し、元の失敗を保持する
		file.reset() ;
		DeleteFileW( temporary.c_str()) ;
		throw ;
	}
}

} // namespace koefude
