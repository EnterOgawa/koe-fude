//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file reading_view.cpp
 * @brief 確定原文のfingerprintと整形結果の全体検証・表示投影を実装します
 * @version 1.0.0
 * @date 2026/09/28
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/28 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/28 合成入力で使う原文対応と行投影を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 callbackの契約・宣言の役割を明記
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
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
#include "core/reading_view.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <windows.h>

// Windows基本型を定義した後に暗号APIのhandleと定数を参照する
#include <bcrypt.h>

#include "core/speaker_attribution.h"
#include "platform/win_helpers.h"

namespace koefude {
namespace {

constexpr std::string_view K_SOURCE_DOMAIN = "KoeFude/reading-source/v1" ; // hashの形式識別子
constexpr std::size_t	   K_DIGEST_BUFFER_BYTES = 4096 ; // CNGへ渡す有界の集約buffer

//------------------------------------------------------------------------------
/**
 * @brief 不成立の整形契約を本文を含めず拒否します
 * @param[in] valid 満たすべき条件
 * @param[in] kind 違反時の既存error分類
 */
//------------------------------------------------------------------------------
void					   requireReading( const bool valid, const ErrorKind kind = ErrorKind::INVALID_INPUT)
{
	// 未検証の結果を途中まで表示・保存しない
	if( !valid) {
		throw OperationError( kind) ;
	}
}

/** @brief CNG providerの所有handleを閉じます */
struct AlgorithmCloser {
	//------------------------------------------------------------------------------
	/**
	 * @brief providerを閉じます
	 * @param[in] handle このscopeが所有するhandle
	 */
	//------------------------------------------------------------------------------
	void operator()( const BCRYPT_ALG_HANDLE handle) const noexcept
	{
		BCryptCloseAlgorithmProvider( handle, 0) ;
	}
} ;

/** @brief CNG hash contextの所有handleを閉じます */
struct HashCloser {
	//------------------------------------------------------------------------------
	/**
	 * @brief contextを閉じます
	 * @param[in] handle このscopeが所有するhandle
	 */
	//------------------------------------------------------------------------------
	void operator()( const BCRYPT_HASH_HANDLE handle) const noexcept
	{
		BCryptDestroyHash( handle) ;
	}
} ;

//------------------------------------------------------------------------------
/**
 * @brief CNGの失敗を内容を含まない診断値へ変換します
 * @param[in] status OSのNTSTATUS
 */
//------------------------------------------------------------------------------
void checkCrypto( const NTSTATUS status)
{
	// 未確定のdigestを原文IDとして使わない
	if( status < 0) {
		throw OperationError( ErrorKind::INTERNAL, static_cast<std::uint32_t>( status)) ;
	}
}

/** @brief 小さい値を集約してSHA-256へ順序どおり渡します */
class DigestWriter final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief CNGのSHA-256 contextを開始します
	 */
	//------------------------------------------------------------------------------
	DigestWriter()
	{
		BCRYPT_ALG_HANDLE algorithm = nullptr ; // 成功後にprovider_へ所有権を移すhandle
		checkCrypto( BCryptOpenAlgorithmProvider( &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) ;
		provider_.reset( algorithm) ;
		BCRYPT_HASH_HANDLE hash = nullptr ; // 成功後にhash_へ所有権を移すhandle
		checkCrypto( BCryptCreateHash( provider_.get(), &hash, nullptr, 0, nullptr, 0, 0)) ;
		hash_.reset( hash) ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief byteを有界bufferへ追加します
	 * @param[in] value 書き込む元byte
	 */
	//------------------------------------------------------------------------------
	void byte( const std::uint8_t value)
	{
		// いっぱいのbufferをflushしてから新しいbyteを追加する
		if( used_ == buffer_.size()) {
			flush() ;
		}
		buffer_[used_++] = value ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief unsigned値をlittle endianで追加します
	 * @param[in] value 書き込む数値
	 * @param[in] width 4または8byte
	 */
	//------------------------------------------------------------------------------
	void number( const std::uint64_t value, const std::size_t width)
	{
		// 設計上の固定幅以外をhashへ混ぜない
		requireReading( width == 4 || width == 8) ;
		// byte順をC++ object表現・CPUのendianへ依存させない
		for( std::size_t index = 0; index < width; ++index) {
			byte( static_cast<std::uint8_t>( value >> (index * 8))) ;
		}
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 長さ付きUTF-8 byte列を正規化せず追加します
	 * @param[in] value 検証済みの文字列
	 */
	//------------------------------------------------------------------------------
	void text( const std::string_view value)
	{
		requireReading( value.size() <= std::numeric_limits<std::uint32_t>::max(), ErrorKind::RESULT_TOO_LARGE) ;
		number( value.size(), 4) ;
		// signed charでも下位8bitを元byteとして記録する
		for( const char character : value) {
			byte( static_cast<std::uint8_t>( character)) ;
		}
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 残りを送ってdigestを返します
	 * @return 32byteのSHA-256
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::array<std::uint8_t, 32> finish()
	{
		flush() ;
		std::array<std::uint8_t, 32> result{} ; // CNGが埋める固定長の結果
		checkCrypto( BCryptFinishHash( hash_.get(), result.data(), static_cast<ULONG>( result.size()), 0)) ;
		return result ;
	}

	private:
	//------------------------------------------------------------------------------
	/**
	 * @brief 現在のbufferだけをCNGへ渡し、再利用可能にします
	 */
	//------------------------------------------------------------------------------
	void flush()
	{
		// 空bufferの送信を避け、呼出し中だけAPIへ所有bufferを借用させる
		if( used_ != 0) {
			checkCrypto( BCryptHashData( hash_.get(), buffer_.data(), static_cast<ULONG>( used_), 0)) ;
			used_ = 0 ;
		}
	}

	/// providerの単一所有者
	std::unique_ptr<void, AlgorithmCloser>			provider_ ;
	/// providerより先に破棄されるcontext
	std::unique_ptr<void, HashCloser>				hash_ ;
	/// 集約する固定領域
	std::array<std::uint8_t, K_DIGEST_BUFFER_BYTES> buffer_{} ;
	/// buffer内で検証済みのbyte数
	std::size_t										used_ = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 検証済みの話者番号列を固定幅でhashへ渡します
 * @param[in,out] writer 原文fingerprintの状態
 * @param[in] speakers 元の順序を保った話者番号
 */
//------------------------------------------------------------------------------
void writeSpeakers( DigestWriter &writer, const std::span<const int> speakers)
{
	writer.number( speakers.size(), 4) ;
	// 認識・帰属の番号を並び替えず、各番号を4byteで追加する
	for( const int speaker : speakers) {
		writer.number( static_cast<std::uint32_t>( speaker), 4) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 原文のhashに入れる範囲と内部整合性を確かめます
 * @param[in] source 確定原文の借用参照
 */
//------------------------------------------------------------------------------
void validateSource( const Transcript &source)
{
	requireReading( source.speakerCount >= 0 && source.speakerCount <= static_cast<int>( K_MAXIMUM_SEGMENTS) &&
				   static_cast<unsigned>( source.speakerMethod) <= 1 && static_cast<unsigned>( source.asrProfile) <= 1 &&
				   static_cast<unsigned>( source.asrModel) <= 3 && std::isfinite( source.durationSeconds) &&
				   source.durationSeconds > 0 &&
				   source.durationSeconds <= static_cast<double>( K_MAXIMUM_TIMELINE_MS) / 1000) ;
	requireReading( source.lines.size() <= K_MAXIMUM_SEGMENTS && source.speakerTurns.size() <= K_MAXIMUM_SEGMENTS,
				   ErrorKind::RESULT_TOO_LARGE) ;
	// 元音声の秒数から1msの丸め余裕を含めた時刻検証上限
	const auto	 durationMs = static_cast<std::int64_t>( source.durationSeconds * 1000 + 1) ;
	std::size_t	 totalBytes = 0 ; // 原文全行のUTF-8 byte合計
	std::size_t	 attributionItems = 0 ; // 推定spanと候補の合計
	std::int64_t previousStart = -1 ; // 直前に確認した開始時刻
	// 元行の順序・時刻・本文とspeaker番号をhash前に検証する
	for( const auto &line : source.lines) {
		requireReading( line.startMs >= 0 && line.startMs >= previousStart && line.endMs >= line.startMs &&
						   line.endMs <= durationMs && !line.text.empty() &&
						   line.speakers.size() <= static_cast<std::size_t>( source.speakerCount),
					   ErrorKind::INVALID_TIMELINE) ;
		previousStart = line.startMs ;
		requireReading( line.text.size() <= K_MAXIMUM_TEXT_BYTES - totalBytes, ErrorKind::RESULT_TOO_LARGE) ;
		totalBytes += line.text.size() ;
		requireReading( line.text.find( '\0') == std::string::npos) ;
		static_cast<void>( toWide( line.text)) ;
		int previousSpeaker = 0 ; // 同じ行で直前に検証した話者番号
		// 不明を空のまま認め、存在する番号だけを一度ずつ保持する
		for( const int speaker : line.speakers) {
			requireReading( speaker > previousSpeaker && speaker <= source.speakerCount) ;
			previousSpeaker = speaker ;
		}
		validateSpeakerAttribution( line, attributionItems) ;
	}
	std::set<int> turnSpeakers ; // 元音声に実在したclusterの集合
	// 区間の値とspeaker_countを照合し、余分なturnをhashしない
	for( const auto &turn : source.speakerTurns) {
		requireReading( turn.startMs >= 0 && turn.endMs > turn.startMs && turn.endMs <= durationMs &&
						   turn.speaker >= 0 && turn.speaker < static_cast<int>( K_MAXIMUM_SEGMENTS),
					   ErrorKind::INVALID_TIMELINE) ;
		turnSpeakers.insert( turn.speaker) ;
	}
	requireReading( turnSpeakers.size() == static_cast<std::size_t>( source.speakerCount)) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 方式識別子をhistoryで使える有界ASCIIへ限定します
 * @param[in] value 検証対象のprofile ID
 * @return 全文字が識別子に使える場合true
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool validProfileId( const std::string_view value)
{
	// 空または過大なIDを受け付けず、別の保存規約と一致させる
	if( value.empty() || value.size() > 64) {
		return false ;
	}
	// 制御文字や空白を含む任意文字列を識別子として使わない
	for( const char character : value) {
		// profile識別子として許可するASCII英数字・区切り記号か
		const bool allowed = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
							 (character >= '0' && character <= '9') || character == '.' || character == '-' ||
							 character == '_' ;
		// 一文字でも規約に合わなければID全体を拒否する
		if( !allowed) {
			return false ;
		}
	}
	return true ;
}

//------------------------------------------------------------------------------
/**
 * @brief 語句ではなく空白だけの置換を拒否します
 * @param[in] unit 検証済みUTF-16 code unit
 * @return Unicodeの代表的な空白ならtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool blankUnit( const wchar_t unit)
{
	return unit == L' ' || (unit >= L'\t' && unit <= L'\r') || unit == 0x00a0 || unit == 0x1680 ||
		   (unit >= 0x2000 && unit <= 0x200a) || unit == 0x2028 || unit == 0x2029 || unit == 0x202f || unit == 0x205f ||
		   unit == 0x3000 || unit == 0xfeff ;
}

//------------------------------------------------------------------------------
/**
 * @brief UTF-8をscalarとUTF-16境界へ読み替えます
 * @param[in] text 既に厳密UTF-8であることを検証した本文
 * @param[out] offsets 各scalar開始と末尾のUTF-16単位位置
 * @return 順序どおりのUnicode scalar
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<std::uint32_t> decodeScalars( const std::string_view text, std::vector<std::size_t> &offsets)
{
	std::vector<std::uint32_t> scalars ; // 差分比較に使う有界のcode point列
	scalars.reserve( text.size()) ;
	offsets.reserve( text.size() + 1) ;
	offsets.push_back( 0) ;
	// 妥当性確認済みのbyte列だけを一度走査し、各UTF-16境界を作る
	for( std::size_t index = 0; index < text.size();) {
		const auto		  lead = static_cast<std::uint8_t>( text[index]) ; // 今回の先頭byte
		// 検証済みUTF-8先頭byteが示す一scalarのbyte長
		const std::size_t length = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4 ;
		// 先頭byteのpayloadから続きbyteを合成するUnicode scalar
		std::uint32_t	  scalar = lead & (length == 1 ? 0x7fU : length == 2 ? 0x1fU : length == 3 ? 0x0fU : 0x07U) ;
		// UTF-8の続きbyteを合成し、元の本文を正規化しない
		for( std::size_t part = 1; part < length; ++part) {
			scalar = (scalar << 6) | (static_cast<std::uint8_t>( text[index + part]) & 0x3fU) ;
		}
		scalars.push_back( scalar) ;
		offsets.push_back( offsets.back() + (scalar > 0xffffU ? 2U : 1U)) ;
		index += length ;
	}
	return scalars ;
}

//------------------------------------------------------------------------------
/**
 * @brief 元文と整形文の共通両端を除いた一つの変更範囲を求めます
 * @param[in] original 厳密UTF-8の元本文
 * @param[in] formatted 厳密UTF-8の異なる本文
 * @return scalarとUTF-16両方の半開範囲
 */
//------------------------------------------------------------------------------
[[nodiscard]] ReadingDiff lineDiff( const std::string_view original, const std::string_view formatted)
{
	std::vector<std::size_t> originalOffsets ; // 元文各scalarのUTF-16開始位置
	std::vector<std::size_t> formattedOffsets ; // 整形文各scalarのUTF-16開始位置
	const auto				 oldValues = decodeScalars( original, originalOffsets) ; // 元文の比較値
	const auto				 newValues = decodeScalars( formatted, formattedOffsets) ; // 新文の比較値
	std::size_t				 prefix = 0 ; // 一致する先頭scalar数
	// 双方の先頭が一致する間だけ進める
	while( prefix < oldValues.size() && prefix < newValues.size() && oldValues[prefix] == newValues[prefix]) {
		++prefix ;
	}
	std::size_t suffix = 0 ; // prefixへ重ならない末尾一致数
	// 複数編集の中間も一範囲とし、位置を推測で分割しない
	while( suffix < oldValues.size() - prefix && suffix < newValues.size() - prefix &&
		  oldValues[oldValues.size() - suffix - 1] == newValues[newValues.size() - suffix - 1]) {
		++suffix ;
	}
	const auto oldEnd = oldValues.size() - suffix ; // 元文の変更終端
	const auto newEnd = newValues.size() - suffix ; // 整形文の変更終端
	return {prefix,
			oldEnd,
			prefix,
			newEnd,
			originalOffsets[prefix],
			originalOffsets[oldEnd],
			formattedOffsets[prefix],
			formattedOffsets[newEnd]} ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 元本文・時刻・帰属を検証して固定形式のSHA-256を計算します
 * @param[in] source workerが所有する確定原文。名前等はfingerprintから除外
 * @return 設計版1の32byte digest
 * @throws OperationError 不正な原文、容量超過、暗号APIの失敗
 * @note UI threadから呼ばず、元音声fileは開きません
 * @see koefude::fingerprintReadingSource
 */
//------------------------------------------------------------------------------
std::array<std::uint8_t, 32> fingerprintReadingSource( const Transcript &source)
{
	validateSource( source) ;
	DigestWriter writer ; // 64bit環境でも固定幅だけを入れるhash state
	// 別用途の同じbyte列と衝突する解釈を防ぐためdomain終端を追加する
	for( const char character : K_SOURCE_DOMAIN) {
		writer.byte( static_cast<std::uint8_t>( character)) ;
	}
	writer.byte( 0) ;
	writer.number( static_cast<std::uint32_t>( source.speakerCount), 4) ;
	writer.number( static_cast<std::uint32_t>( source.speakerMethod), 4) ;
	writer.number( static_cast<std::uint32_t>( source.asrProfile), 4) ;
	writer.number( static_cast<std::uint32_t>( source.asrModel), 4) ;
	writer.number( source.speechFilter ? 1 : 0, 4) ;
	writer.number( source.diarizationRequested ? 1 : 0, 4) ;
	writer.number( source.diarizationWarning ? 1 : 0, 4) ;
	writer.number( std::bit_cast<std::uint64_t>( source.durationSeconds), 8) ;
	writer.number( source.lines.size(), 4) ;
	// 元行の配列順、話者番号、本文、帰属範囲を漏れなく固定する
	for( const auto &line : source.lines) {
		writer.number( static_cast<std::uint64_t>( line.startMs), 8) ;
		writer.number( static_cast<std::uint64_t>( line.endMs), 8) ;
		writeSpeakers( writer, line.speakers) ;
		writer.text( line.text) ;
		writer.number( static_cast<std::uint32_t>( line.attribution.kind), 4) ;
		writer.number( line.attribution.spans.size(), 4) ;
		// 帰属の隙間と候補順を含む全範囲を元の順序で固定する
		for( const auto &span : line.attribution.spans) {
			writer.number( static_cast<std::uint64_t>( span.startMs), 8) ;
			writer.number( static_cast<std::uint64_t>( span.endMs), 8) ;
			writeSpeakers( writer, span.speakers) ;
		}
	}
	writer.number( source.speakerTurns.size(), 4) ;
	// 話者推定区間の別の版へ整形結果を移植しない
	for( const auto &turn : source.speakerTurns) {
		writer.number( static_cast<std::uint64_t>( turn.startMs), 8) ;
		writer.number( static_cast<std::uint64_t>( turn.endMs), 8) ;
		writer.number( std::bit_cast<std::uint32_t>( static_cast<std::int32_t>( turn.speaker)), 4) ;
	}
	return writer.finish() ;
}

//------------------------------------------------------------------------------
/**
 * @brief 合成済み整形結果を原文と照合し、部分結果の採用を防ぎます
 * @param[in] source 不変の確定原文
 * @param[in] key 履歴IDと原文fingerprint
 * @param[in] formatted 全行の候補。失敗時も変更しません
 * @throws OperationError source不一致・不正本文・行対応/容量違反
 * @see koefude::validateFormattedTranscript
 */
//------------------------------------------------------------------------------
void validateFormattedTranscript( const Transcript &source, const ReadingSourceKey &key,
								 const FormattedTranscript &formatted)
{
	requireReading( key.historyId != 0 && formatted.source == key && fingerprintReadingSource( source) == key.sha256) ;
	requireReading( formatted.contractVersion == K_READING_CONTRACT_VERSION && validProfileId( formatted.profileId) &&
				   !source.lines.empty() && formatted.lines.size() == source.lines.size()) ;
	std::size_t rawBytes = 0 ; // 元本文のbyte数
	// 保存済み原文の容量を、生成本文の合計へ加える前に再確認する
	for( const auto &line : source.lines) {
		requireReading( line.text.size() <= K_MAXIMUM_TEXT_BYTES - rawBytes, ErrorKind::RESULT_TOO_LARGE) ;
		rawBytes += line.text.size() ;
	}
	std::size_t replacementBytes = 0 ; // 元文とは別に保存する置換本文の合計
	// すべての行を元indexの順に照合し、一行だけの部分採用をしない
	for( std::size_t index = 0; index < source.lines.size(); ++index) {
		const auto &original = source.lines[index] ; // 対応する元行
		const auto &candidate = formatted.lines[index] ; // 同じ番号の候補
		requireReading( candidate.lineIndex == index && static_cast<std::uint32_t>( candidate.kind) <= 2 &&
					   static_cast<std::uint32_t>( candidate.reason) <= 4) ;
		// 元の行で単一話者を確定できず、本文置換を保留する条件
		const bool uncertain =
			original.speakers.size() != 1 || original.attribution.kind != AttributionKind::SINGLE_SPEAKER ;
		// 話者が確定しない行は理由を保ち、編集された本文を採用しない
		if( uncertain) {
			requireReading( candidate.kind == ReadingLineKind::ORIGINAL_RETAINED &&
						   candidate.reason == ReadingLineReason::ATTRIBUTION_UNCERTAIN && candidate.text.empty()) ;
			continue ;
		}
		// 生成単位を超える原文は途中で切らず、そのまま保持する
		if( original.text.size() > K_MAXIMUM_READING_UNIT_BYTES) {
			requireReading( candidate.kind == ReadingLineKind::ORIGINAL_RETAINED &&
						   candidate.reason == ReadingLineReason::UNIT_TOO_LARGE && candidate.text.empty()) ;
			continue ;
		}
		// 無変更行は元文の参照だけとし、同じ本文を重複して保存しない
		if( candidate.kind == ReadingLineKind::NO_CHANGE) {
			requireReading( candidate.reason == ReadingLineReason::NONE && candidate.text.empty()) ;
			continue ;
		}
		// 容量不足または単位出力不正は、内容を捨てて元行へ戻す
		if( candidate.kind == ReadingLineKind::ORIGINAL_RETAINED) {
			requireReading( (candidate.reason == ReadingLineReason::CAPACITY ||
							candidate.reason == ReadingLineReason::INVALID_OUTPUT) &&
						   candidate.text.empty()) ;
			continue ;
		}
		requireReading( candidate.kind == ReadingLineKind::REPLACED && candidate.reason == ReadingLineReason::NONE &&
					   !candidate.text.empty() && candidate.text != original.text &&
					   candidate.text.size() <= K_MAXIMUM_FORMATTED_LINE_BYTES &&
					   candidate.text.find( '\0') == std::string::npos) ;
		const auto wide = toWide( candidate.text) ; // 厳密UTF-8と空白だけの本文を検査
		requireReading( std::any_of( wide.begin(), wide.end(),
			//------------------------------------------------------------------------------
			/**
			 * @brief 整形候補に空白以外のUTF-16 code unitがあるかを検査します
			 * @param[in] unit 検証済み本文の一code unit
			 * @return 空白として扱う文字以外ならtrue
			 */
			//------------------------------------------------------------------------------
			[]( const wchar_t unit)
			{
				return !blankUnit( unit) ;
			})) ;
		requireReading( candidate.text.size() <= K_MAXIMUM_TEXT_BYTES - rawBytes - replacementBytes,
					   ErrorKind::RESULT_TOO_LARGE) ;
		replacementBytes += candidate.text.size() ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 実行IDと方式も照合して、遅延・別要求の結果を拒否します
 * @param[in] source 受付対象の確定原文
 * @param[in] request 受付時に固定したID・方式
 * @param[in] response workerが返した全行候補
 * @throws OperationError 一致しないID/方式、または本文契約違反
 * @see koefude::validateReadingResponse
 */
//------------------------------------------------------------------------------
void validateReadingResponse( const Transcript &source, const ReadingRequest &request, const ReadingResponse &response)
{
	// 旧実行の応答と別の方式を、本文照合前に拒否する
	requireReading( request.operationId != 0 && response.operationId == request.operationId &&
				   response.result.source == request.source && response.result.profileId == request.profileId &&
				   response.result.profileSha256 == request.profileSha256) ;
	validateFormattedTranscript( source, request.source, response.result) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 所有結果から、元行番号を残した表示用の借用viewを構築します
 * @param[in] source 呼出し後もsnapshotが共有所有する確定原文。null不可
 * @param[in] key 永続履歴IDと原文fingerprint
 * @param[in] formatted 整形表示時の候補。原文表示では参照しない。null可
 * @param[in] display 要求された表示版
 * @return 元本文と整形本文の寿命を保持した所有snapshot
 * @throws OperationError source/整形の不一致、行数・容量・Unicode違反
 * @see koefude::makeReadingView
 */
//------------------------------------------------------------------------------
ReadingViewSnapshot makeReadingView( std::shared_ptr<const Transcript> source, const ReadingSourceKey &key,
									std::shared_ptr<const FormattedTranscript> formatted, const ReadingDisplay display)
{
	requireReading( source != nullptr && key.historyId != 0 && static_cast<std::uint32_t>( display) <= 1) ;
	// 原文でもsource keyを照合し、別録音への表示を防ぐ
	requireReading( fingerprintReadingSource( *source) == key.sha256) ;
	// 整形表示だけ候補を参照する。原文表示では未検証候補を読み込まない
	if( display == ReadingDisplay::FORMATTED) {
		requireReading( formatted != nullptr) ;
		validateFormattedTranscript( *source, key, *formatted) ;
	}
	ReadingViewSnapshot view ; // ownerを先に確保して借用viewの寿命を固定する
	view.key_ = key ;
	view.display_ = display ;
	view.source_ = std::move( source) ;
	view.formatted_ = display == ReadingDisplay::FORMATTED ? std::move( formatted) : nullptr ;
	view.lines_.reserve( view.source_->lines.size()) ;
	view.groups_.reserve( view.source_->lines.size()) ;
	// 行のindexと元metaを維持し、話者filterより前のgroupへ登録する
	for( std::size_t index = 0; index < view.source_->lines.size(); ++index) {
		const auto	   &original = view.source_->lines[index] ; // このsnapshotが所有する元行
		ReadingLineView line ; // 投影する一つの借用行
		line.lineIndex = static_cast<std::uint32_t>( index) ;
		line.original = &original ;
		line.text = original.text ;
		// 整形版では置換行だけを別本文へ向け、原文は変更しない
		if( view.formatted_) {
			const auto &candidate = view.formatted_->lines[index] ; // 検証済み対応行
			line.kind = candidate.kind ;
			line.reason = candidate.reason ;
			// 語句が違う行だけ、両方の本文を比較可能な範囲を付ける
			if( candidate.kind == ReadingLineKind::REPLACED) {
				line.text = candidate.text ;
				line.diff = lineDiff( original.text, candidate.text) ;
			}
		}
		view.lines_.push_back( line) ;
		bool join = false ; // 直前のgroupへ同じ話者として結合できる場合true
		// 不明・混在や別話者を跨いで、表示上だけ同じ人に統合しない
		if( !view.groups_.empty() && original.speakers.size() == 1) {
			const auto &group = view.groups_.back() ; // 直前の表示group
			const auto &first = view.source_->lines[group.lineIndices.front()] ; // groupの先頭行
			join = first.speakers == original.speakers && first.attribution.kind == original.attribution.kind &&
				   original.startMs >= group.startMs ;
		}
		// 既存の連続発話条件と同じ時だけ、元indexを保持してまとめる
		if( join) {
			auto &group = view.groups_.back() ; // この行を足すgroup
			group.lineIndices.push_back( line.lineIndex) ;
			group.endMs = std::max( group.endMs, original.endMs) ;
		} else {
			ReadingGroup group ; // 新しい不明/混在/別話者または最初のgroup
			group.startMs = original.startMs ;
			group.endMs = original.endMs ;
			group.lineIndices.push_back( line.lineIndex) ;
			view.groups_.push_back( std::move( group)) ;
		}
	}
	return view ;
}

} // namespace koefude
