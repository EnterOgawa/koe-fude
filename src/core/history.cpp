//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file history.cpp
 * @brief 履歴の保存形式と外部byte列の件数・文字・時刻を検証します
 * @version 1.0.0
 * @date 2026/09/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/10 固定幅・little endianの有界な履歴形式を追加
 * 0.1.0 Shunji Ogawa 2026/09/10 録音時間の固定上限を除き、64bitの全体時刻を保持
 * 0.1.0 Shunji Ogawa 2026/09/10 旧enumを保って日本語CTCの履歴を追加し、CPU専用条件を検証
 * 0.1.0 Shunji Ogawa 2026/09/11 CUDAとFP32の結果を保持し、旧CTC履歴をINT8として復元
 * 0.1.0 Shunji Ogawa 2026/09/11 形式4へ話者理由・推定範囲を保存し、旧形式は未記録を保持
 * 0.1.0 Shunji Ogawa 2026/10/04 形式5の整形状態を検証し、旧形式と中断結果を保持
 * 0.1.0 Shunji Ogawa 2026/10/19 宣言の役割を明記
 * 0.1.0 Shunji Ogawa 2026/10/19 V5原文・旧整形を保持するV6拡張を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1370の文脈分割と明示途中再開を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実使用の標準headerを直接include
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "core/history.h"
#include "core/job_controller.h"
#include "core/speaker_attribution.h"
#include "platform/whole_text_journal_store.h"

#include <span>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "platform/win_helpers.h"

namespace koefude {
namespace {
constexpr std::uint64_t K_HISTORY_MAGIC = 0x3154534948464f4bULL ; // ASCIIのKOFHIST1識別子
constexpr std::uint32_t K_HISTORY_VERSION =
	8 ; // 音声APIの部分原文拡張。PCMと自由全文の独立拡張。形式1〜5の原文・旧整形を保持
constexpr std::size_t K_MAXIMUM_PATH_BYTES = 32767 * 4 ; // UTF-8で保持できるpathのbyte上限
constexpr std::size_t K_MAXIMUM_NAME_BYTES = 256 ; // UIが受け付ける64 UTF-16単位のUTF-8上限

//------------------------------------------------------------------------------
/**
 * @brief 不正内容を共通の履歴破損として拒否します
 * @param[in] valid 成立すべき保存形式の条件
 */
//------------------------------------------------------------------------------
void				  requireHistory( const bool valid)
{
	// 検証に失敗した履歴から部分結果を返さない
	if( !valid) {
		throw HistoryError( HistoryErrorKind::CORRUPT) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief UTF-8とNULの有無を検証してUTF-16の長さを返します
 * @param[in] value 保存する文字列のborrow
 * @return UTF-16 code unit数
 * @throws HistoryError 不正UTF-8、埋込みNUL
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::size_t validateText( const std::string_view value)
{
	requireHistory( value.find( '\0') == std::string_view::npos) ;
	try {
		return toWide( value).size() ;
	} catch( const OperationError &error) {
		// Unicode変換の異常を履歴fileの分類へ変換し、本文を診断へ含めない
		throw HistoryError( HistoryErrorKind::CORRUPT, error.code()) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief pathのUTF-16異常を履歴の形式エラーとして返します
 * @param[in] path 内容を開かず値だけを検証するpath
 * @return 所有UTF-8文字列
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string pathText( const std::filesystem::path &path)
{
	try {
		return toUtf8( path.native()) ;
	} catch( const OperationError &error) {
		// 不正surrogateを別のpathへ置換せず、保存形式の分類で拒否する
		throw HistoryError( HistoryErrorKind::CORRUPT, error.code()) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 履歴へ持ち込むTranscriptの容量と契約を検証します
 * @param[in] result immutableな確定結果
 */
//------------------------------------------------------------------------------
void validateResult( const Transcript &result)
{
	requireHistory( result.speakerCount >= 0 && result.speakerCount <= static_cast<int>( K_MAXIMUM_SEGMENTS) &&
				   static_cast<unsigned>( result.speakerMethod) <= 1 && static_cast<unsigned>( result.asrProfile) <= 1 &&
				   static_cast<unsigned>( result.asrModel) <= 3 &&
				   static_cast<unsigned>( result.diarizationError) <=
					   static_cast<unsigned>( ErrorKind::TEMPORARY_STORAGE)) ;
	requireHistory( !(result.usedGpu && result.cpuRetried)) ; // 再試行の成功先はCPUに限定する
	requireHistory( std::isfinite( result.durationSeconds) && result.durationSeconds > 0 &&
				   result.durationSeconds <= static_cast<double>( K_MAXIMUM_TIMELINE_MS) / 1000) ;
	// 各工程と合計の時計値を、負数・非有限・実用上あり得ない値から保護する
	for( const double seconds :
		{result.totalSeconds, result.decodeSeconds, result.transcriptionSeconds, result.diarizationSeconds}) {
		requireHistory( std::isfinite( seconds) && seconds >= 0 && seconds <= 1'000'000'000) ;
	}
	// 件数を検証してから表示用loopや所有containerへ値を渡す
	if( result.lines.size() > K_MAXIMUM_SEGMENTS || result.speakerTurns.size() > K_MAXIMUM_SEGMENTS ||
	   result.speakerNames.size() > static_cast<std::size_t>( result.speakerCount)) {
		throw HistoryError( HistoryErrorKind::TOO_LARGE) ;
	}
	// この同期境界で所有する値、または確定状態の検証値です
	const auto	 durationMs = static_cast<std::int64_t>( result.durationSeconds * 1000 + 1) ; // 有効な終端ms
	std::size_t	 textBytes = 0 ; // この結果で受け入れた本文のbyte数
	std::size_t	 attributionItems = 0 ; // 全行の推定範囲数と範囲内ID数の合計
	std::int64_t previousStart = -1 ; // 時系列を確認する直前の開始ms
	// 文字起こし全行の時刻、話者帰属、UTF-8、合計本文容量を確認する
	for( const auto &line : result.lines) {
		requireHistory( line.startMs >= 0 && line.startMs >= previousStart && line.endMs >= line.startMs &&
					   line.endMs <= durationMs && !line.text.empty() &&
					   line.speakers.size() <= static_cast<std::size_t>( result.speakerCount)) ;
		previousStart = line.startMs ;
		// 行ごとの上限だけではなく、file全体の本文上限を維持する
		if( line.text.size() > K_MAXIMUM_TEXT_BYTES - textBytes) {
			throw HistoryError( HistoryErrorKind::TOO_LARGE) ;
		}
		textBytes += line.text.size() ;
		static_cast<void>( validateText( line.text)) ;
		int previousSpeaker = 0 ; // 同じ行の帰属は重複なしの番号順
		// 不明は空のままとし、実在する初出順の番号だけを受理する
		for( const int speaker : line.speakers) {
			requireHistory( speaker > previousSpeaker && speaker <= result.speakerCount) ;
			previousSpeaker = speaker ;
		}
		try {
			validateSpeakerAttribution( line, attributionItems) ;
		} catch( const OperationError &error) {
			// 推定情報の不整合も履歴の分類へ変換し、部分結果を復元しない
			throw HistoryError( error.kind() == ErrorKind::RESULT_TOO_LARGE ? HistoryErrorKind::TOO_LARGE
																		   : HistoryErrorKind::CORRUPT,
							   error.code()) ;
		}
	}
	std::set<int> speakers ; // 永続化する推定区間のcluster集合
	// 元音声の範囲外や不正clusterを保存結果へ含めない
	for( const auto &turn : result.speakerTurns) {
		requireHistory( turn.startMs >= 0 && turn.endMs > turn.startMs && turn.endMs <= durationMs &&
					   turn.speaker >= 0 && turn.speaker < static_cast<int>( K_MAXIMUM_SEGMENTS)) ;
		speakers.insert( turn.speaker) ;
	}
	requireHistory( speakers.size() == static_cast<std::size_t>( result.speakerCount)) ;
	// 未知話者の名前やUIの長さ制限を超える値を復元しない
	for( const auto &[speaker, name] : result.speakerNames) {
		requireHistory( speaker >= 1 && speaker <= result.speakerCount && !name.empty() &&
					   name.size() <= K_MAXIMUM_NAME_BYTES && validateText( name) <= 64) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief IDと各entryの分類・文字・所有結果を検証します
 * @param[in] snapshot 検証する所有履歴
 */
//------------------------------------------------------------------------------
void validateSnapshot( const HistorySnapshot &snapshot)
{
	requireHistory( snapshot.nextId != 0 && snapshot.nextOperationId != 0) ;
	// 上限でも過去の履歴を切り捨てず、今回の保存を失敗として返す
	if( snapshot.entries.size() > K_MAXIMUM_HISTORY) {
		throw HistoryError( HistoryErrorKind::TOO_LARGE) ;
	}
	std::uint64_t previousId = 0 ; // 登録順とID再利用を照合する直前ID
	// 全entryを順に検証してから保存・表示へ進める
	for( const auto &entry : snapshot.entries) {
		requireHistory( entry.id > previousId && entry.id < snapshot.nextId &&
					   static_cast<unsigned>( entry.state) <= static_cast<unsigned>( HistoryState::INTERRUPTED) &&
					   static_cast<unsigned>( entry.error) <= static_cast<unsigned>( ErrorKind::TEMPORARY_STORAGE)) ;
		previousId = entry.id ;
		const auto path = pathText( entry.input) ; // pathのUnicodeも明示的に検証する
		requireHistory( !path.empty() && path.size() <= K_MAXIMUM_PATH_BYTES && validateText( path) <= 32767) ;
		// この同期境界で所有する値、または確定状態の検証値です
		const bool completed = entry.state == HistoryState::COMPLETED ||
							   entry.state == HistoryState::COMPLETED_WITH_WARNINGS ||
							   entry.state == HistoryState::NO_TEXT ; // 確定結果を必要とする分類
		// API音声だけはPending/失敗/取消し/中断にも整合する原文prefixを持てます
		if( entry.apiAudio) {
			try {
				validateApiAudioRecord( *entry.apiAudio, entry.result.get()) ;
			} catch( const OperationError &error) {
				throw HistoryError( HistoryErrorKind::CORRUPT, error.code()) ;
			}
			const auto terminal = entry.apiAudio->terminal ; // recordと履歴分類を一致させます
			const auto expected =
				terminal == ApiAudioTerminal::COMPLETED
					? (entry.result && entry.result->diarizationWarning ? HistoryState::COMPLETED_WITH_WARNINGS
																		: HistoryState::COMPLETED)
				: terminal == ApiAudioTerminal::NO_SPEECH || terminal == ApiAudioTerminal::EMPTY_TRANSCRIPT
					? HistoryState::NO_TEXT
				: terminal == ApiAudioTerminal::FAILURE		? HistoryState::FAILURE
				: terminal == ApiAudioTerminal::CANCELLED	? HistoryState::CANCELLED
				: terminal == ApiAudioTerminal::INTERRUPTED ? HistoryState::INTERRUPTED
															: HistoryState::PENDING ;
			requireHistory( entry.state == expected) ;
			requireHistory( entry.apiAudio->historyId == entry.id &&
						   entry.apiAudio->operationId < snapshot.nextOperationId && entry.result &&
						   (completed || entry.state == HistoryState::PENDING || entry.state == HistoryState::FAILURE ||
							entry.state == HistoryState::CANCELLED || entry.state == HistoryState::INTERRUPTED)) ;
		} else {
			requireHistory( completed == static_cast<bool>( entry.result)) ;
		}
		requireHistory( !entry.result || entry.result->asrModel != AsrModel::OPEN_AI_AUDIO ||
					   static_cast<bool>( entry.apiAudio)) ;
		// 本文を持つ完了項目だけに話者filterを適用する
		if( entry.result) {
			validateResult( *entry.result) ;
			requireHistory( entry.filter >= -2 && entry.filter <= entry.result->speakerCount) ;
			requireHistory( !completed ||
						   (entry.state == HistoryState::COMPLETED_WITH_WARNINGS) == entry.result->diarizationWarning ||
						   entry.state == HistoryState::NO_TEXT) ;
			requireHistory( entry.state != HistoryState::NO_TEXT || entry.result->lines.empty()) ;
			requireHistory( entry.state != HistoryState::COMPLETED || !entry.result->lines.empty()) ;
		} else {
			requireHistory( entry.filter == 0) ; // 未完了項目に架空の個別話者を持たせない
		}
		try {
			const auto &bounded = entry.bounded ; // 保存正本の小さい証拠
			static_cast<void>( detail::encodeBoundedRecord( entry.id, bounded)) ;
			// descriptorと元のfingerprint/PCMを独立して照合します
			for( const auto *value : {&bounded.effective, &bounded.latest}) {
				// 本文なしで証拠を有効化しません
				if( *value) {
					requireHistory( entry.result && entry.result->pcmIdentity &&
								   (*value)->result.request.source.sha256 == fingerprintReadingSource( *entry.result) &&
								   (*value)->result.pcm == *entry.result->pcmIdentity &&
								   (*value)->result.request.operationId < snapshot.nextOperationId) ;
				}
			}
			// 予約と最新終端にも永続ID上限を適用します
			if( bounded.inFlight) {
				requireHistory( bounded.inFlight->request.operationId < snapshot.nextOperationId) ;
			}
			// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
			if( bounded.latestRequest) {
				requireHistory( bounded.latestRequest->operationId < snapshot.nextOperationId) ;
			}
			validateReadingRecord( entry.result.get(), entry.id, entry.reading) ;
			requireHistory( static_cast<std::uint32_t>( entry.resultPreference) <= 4 &&
						   static_cast<std::uint32_t>( entry.wholeText.state) <= 9 &&
						   static_cast<std::uint32_t>( entry.wholeText.reason) <= 9) ;
			const auto &whole = entry.wholeText ; // 状態と理由を別々に推測しません
			// 失敗理由は全文で許される四分類だけです
			if( whole.state == ReadingState::FAILURE) {
				requireHistory( whole.reason == ReadingReason::INVALID_OUTPUT ||
							   whole.reason == ReadingReason::MODEL_UNAVAILABLE ||
							   whole.reason == ReadingReason::RESOURCE_UNAVAILABLE ||
							   whole.reason == ReadingReason::ENGINE_FAILURE) ;
			}
			// 取消し終端と中断終端は固有の理由へ束縛します
			else if( whole.state == ReadingState::CANCELLED) {
				requireHistory( whole.reason == ReadingReason::CANCELLED) ;
			} else if( whole.state == ReadingState::INTERRUPTED) {
				requireHistory( whole.reason == ReadingReason::INTERRUPTED) ;
			} else {
				requireHistory( whole.reason == ReadingReason::NONE && whole.code == 0) ;
			}
			requireHistory( whole.state != ReadingState::COMPLETED || whole.corrected != nullptr) ;
			// 本文なしへ実行状態や全文ownerを結び付けません
			if( !entry.result || entry.result->lines.empty()) {
				requireHistory( !whole.corrected && !whole.active && whole.state == ReadingState::IDLE) ;
			}
			// 生存中の要求も同じowner/keyを必須にします。保存ではactive自体を省略します
			if( whole.active) {
				requireHistory( whole.state >= ReadingState::QUEUED && whole.state <= ReadingState::CANCELLING &&
							   whole.source == entry.result && whole.active->operationId != 0 &&
							   whole.active->source.historyId == entry.id &&
							   whole.active->source.sha256 == fingerprintReadingSource( *entry.result)) ;
			}
			// 結果なしrecordへPCMや全文を付けません
			if( !entry.result) {
				requireHistory( !entry.wholeText.corrected) ;
			}
			// 元PCM metadataも結果と独立に検証します
			if( entry.result && entry.result->pcmIdentity) {
				const auto &audio = *entry.result->pcmIdentity ; // 小さい証拠
				requireHistory( audio.version == 1 && audio.sampleCount > 0 &&
							   audio.sampleCount <= K_MAXIMUM_TIMELINE_SAMPLES && audio.sampleRate == K_SAMPLE_RATE &&
							   audio.channels == 1 && audio.originSample == 0 &&
							   audio.pcmSha256 != std::array<std::uint8_t, 32>{}) ;
			}
			// 最新失敗stateでも最後の有効全文を独立に保持します
			if( entry.wholeText.corrected) {
				requireHistory( entry.result && entry.wholeText.source && entry.wholeText.source == entry.result) ;
				validatePersistedWholeTextCorrection( *entry.result, {entry.id, fingerprintReadingSource( *entry.result)},
													 *entry.wholeText.corrected) ;
				requireHistory( entry.wholeText.corrected->evidence->binding.request.operationId <
							   snapshot.nextOperationId) ;
				// この同期境界で所有する値、または確定状態の検証値です
				std::size_t remaining = 2 * K_MAXIMUM_TEXT_BYTES ; // 原文・旧置換・全文を共存する16MiB上限
				// 原文を一度だけ数えます
				for( const auto &line : entry.result->lines) {
					requireHistory( line.text.size() <= remaining) ;
					remaining -= line.text.size() ;
				}
				// 有効な旧置換も捨てず容量へ含めます
				if( entry.reading.formatted) {
					// 無変更行は本文を複製しません
					for( const auto &line : entry.reading.formatted->lines) {
						requireHistory( line.text.size() <= remaining) ;
						remaining -= line.text.size() ;
					}
				}
				// 容量超過時はlive結果を削除せず保存だけ失敗します
				if( entry.wholeText.corrected->text.size() > remaining) {
					throw HistoryError( HistoryErrorKind::TOO_LARGE) ;
				}
			}

		} catch( const OperationError &error) {
			// 原文との不一致や整形容量違反を履歴全体の異常として拒否する
			throw HistoryError( error.kind() == ErrorKind::RESULT_TOO_LARGE ? HistoryErrorKind::TOO_LARGE
																		   : HistoryErrorKind::CORRUPT,
							   error.code()) ;
		}
	}
}

/** @brief 保存上限を超えるallocationを避けるlittle endian書込み器です */
class Writer final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じfield writerを計数専用にも使います
	 * @param[in] counting allocationなしのsink
	 */
	//------------------------------------------------------------------------------
	explicit Writer( bool counting = false) : counting_( counting)
	{
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 出力長を読みます
	 * @return SHAを含まないbyte数
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t size() const noexcept
	{
		return size_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief raw byteを同じsinkへ書きます
	 * @param[in] value 同期borrow
	 */
	//------------------------------------------------------------------------------
	void raw( std::span<const std::uint8_t> value)
	{
		reserveBytes( value.size()) ;
		// 計数sinkは大きなbyte列を確保しません
		if( !counting_) {
			bytes_.insert( bytes_.end(), value.begin(), value.end()) ;
		}
		size_ += value.size() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定幅整数をlittle endianで追加します
	 * @param[in] value unsignedの値
	 * @param[in] width 1/4/8の固定byte数
	 */
	//------------------------------------------------------------------------------
	void number( const std::uint64_t value, const std::size_t width = 4)
	{
		reserveBytes( width) ;
		size_ += width ;
		// 計数sinkは同じ固定幅だけを加算します
		if( counting_) {
			return ;
		}
		// 下位byteから順に書き、ABIやpaddingへ保存形式を依存させない
		for( std::size_t index = 0; index < width; ++index) {
			bytes_.push_back( static_cast<std::uint8_t>( value >> (index * 8))) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief IEEE 754 doubleのbit列を固定幅で保存します
	 * @param[in] value 検証済みの秒数
	 */
	//------------------------------------------------------------------------------
	void seconds( const double value)
	{
		number( std::bit_cast<std::uint64_t>( value), 8) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief UTF-8の長さとbyte列を追加します
	 * @param[in] value 検証済みの本文・path・名前
	 */
	//------------------------------------------------------------------------------
	void text( const std::string_view value)
	{
		number( value.size()) ;
		reserveBytes( value.size()) ;
		// 計数時にも同じ長さを一度だけ追加します
		if( !counting_) {
			bytes_.insert( bytes_.end(), value.begin(), value.end()) ;
		}
		size_ += value.size() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有byte列を取り出します
	 * @return 書込み済みの所有列
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::vector<std::uint8_t> finish()
	{
		return std::move( bytes_) ;
	}

	private:
	//------------------------------------------------------------------------------
	/**
	 * @brief 今回の追加分が保存上限へ収まることを確認します
	 * @param[in] count 追加するbyte数
	 */
	//------------------------------------------------------------------------------
	void reserveBytes( const std::size_t count) const
	{
		// 加算前に残量を比較し、integer overflowと過大な出力を防ぐ
		if( count > K_MAXIMUM_HISTORY_BYTES - size_) {
			throw HistoryError( HistoryErrorKind::TOO_LARGE) ;
		}
	}
	/// allocationしない計数sink
	bool					  counting_ = false ;
	/// 両sinkに共通のbyte数
	std::uint64_t			  size_ = 0 ;
	/// このwriterが所有する有界な出力
	std::vector<std::uint8_t> bytes_ ;
} ;

/** @brief 長さと残量を先に照合する保存形式の読取り器です */
class Reader final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 呼出し元のbyte列を同期読取りの間だけ借用します
	 * @param[in] bytes 検証対象の保存byte列
	 */
	//------------------------------------------------------------------------------
	explicit Reader( const std::span<const std::uint8_t> bytes) : remaining_( bytes)
	{
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定幅のunsigned整数を読み取ります
	 * @param[in] width 1/4/8の固定byte数
	 * @return little endianを復元した値
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t number( const std::size_t width = 4)
	{
		requireHistory( width <= remaining_.size()) ;
		std::uint64_t value = 0 ; // 復元途中のunsigned値
		// 有効範囲内の固定byte数だけを整数へ合成する
		for( std::size_t index = 0; index < width; ++index) {
			value |= static_cast<std::uint64_t>( remaining_[index]) << (index * 8) ;
		}
		remaining_ = remaining_.subspan( width) ;
		return value ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 0または1の真偽値を読みます
	 * @return 検証済みの真偽値
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool flag()
	{
		const auto value = number() ; // 未定義のflag値を拒否する元値
		requireHistory( value <= 1) ;
		return value != 0 ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief doubleのbit列を読みます
	 * @return 検証前の秒数
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] double seconds()
	{
		return std::bit_cast<double>( number( 8)) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief allocation前に件数と最低限必要な残量を確認します
	 * @param[in] maximum 各containerに許可する最大件数
	 * @param[in] minimumBytes 一件が必ず消費する最低byte数
	 * @return 走査可能な件数
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::size_t count( const std::size_t maximum, const std::size_t minimumBytes)
	{
		const auto value = number() ; // 外部入力に含まれる件数
		// 架空の長さで大きなcontainerを確保しない
		if( value > maximum) {
			throw HistoryError( HistoryErrorKind::TOO_LARGE) ;
		}
		requireHistory( value <= remaining_.size() / minimumBytes) ;
		return static_cast<std::size_t>( value) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 最大byte数以内の長さ付き文字列を所有copyします
	 * @param[in] maximum 今回の文字列に許可するbyte数
	 * @return UTF-8検証前の所有文字列
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::string text( const std::size_t maximum)
	{
		const auto		  length = count( maximum, 1) ; // copy前に照合したbyte数
		// std::stringへ同じbyte列をcopyする境界。pointerは本呼出し中だけ借用する
		const std::string value( reinterpret_cast<const char *>( remaining_.data()), length) ;
		remaining_ = remaining_.subspan( length) ;
		return value ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 未読範囲を借用します
	 * @return 同期borrow
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::span<const std::uint8_t> remaining() const noexcept
	{
		return remaining_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 検証済みbyte数を進めます
	 * @param[in] count 消費数
	 */
	//------------------------------------------------------------------------------
	void consume( std::size_t count)
	{
		requireHistory( count <= remaining_.size()) ;
		remaining_ = remaining_.subspan( count) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 余分な後続データの有無を確認します
	 * @return 全byteを消費した場合true
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool empty() const noexcept
	{
		return remaining_.empty() ;
	}

	private:
	/// 呼出し元が所有する未読byte列
	std::span<const std::uint8_t> remaining_ ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 一つの確定結果を固定順で書き込みます
 * @param[in,out] writer 所有byte列を組み立てる書込み器
 * @param[in] result 検証済みの共有不変結果
 */
//------------------------------------------------------------------------------
void writeResult( Writer &writer, const Transcript &result)
{
	writer.number( static_cast<std::uint32_t>( result.speakerCount)) ;
	writer.number( static_cast<std::uint32_t>( result.speakerMethod)) ;
	writer.number( static_cast<std::uint32_t>( result.asrProfile)) ;
	writer.number( static_cast<std::uint32_t>( result.asrModel)) ;
	writer.number( result.speechFilter) ;
	writer.number( result.diarizationRequested) ;
	writer.number( result.diarizationWarning) ;
	writer.number( static_cast<std::uint32_t>( result.diarizationError)) ;
	writer.number( result.diarizationCode) ;
	writer.number( result.usedGpu) ;
	writer.number( result.cpuRetried) ;
	writer.seconds( result.durationSeconds) ;
	writer.seconds( result.totalSeconds) ;
	writer.seconds( result.decodeSeconds) ;
	writer.seconds( result.transcriptionSeconds) ;
	writer.seconds( result.diarizationSeconds) ;
	writer.number( result.speakerNames.size()) ;
	// 話者名mapを番号順で保存する
	for( const auto &[speaker, name] : result.speakerNames) {
		writer.number( static_cast<std::uint32_t>( speaker)) ;
		writer.text( name) ;
	}
	writer.number( result.speakerTurns.size()) ;
	// 推定区間も現在の順序と元音声基準の時刻を維持する
	for( const auto &turn : result.speakerTurns) {
		writer.number( static_cast<std::uint64_t>( turn.startMs), 8) ;
		writer.number( static_cast<std::uint64_t>( turn.endMs), 8) ;
		writer.number( static_cast<std::uint32_t>( turn.speaker)) ;
	}
	writer.number( result.lines.size()) ;
	// 表示とexportに使う元の各区間を、結合やfilterをせずに保存する
	for( const auto &line : result.lines) {
		writer.number( static_cast<std::uint64_t>( line.startMs), 8) ;
		writer.number( static_cast<std::uint64_t>( line.endMs), 8) ;
		writer.number( line.speakers.size()) ;
		// 混在する全帰属を番号順のまま保存する
		for( const int speaker : line.speakers) {
			writer.number( static_cast<std::uint32_t>( speaker)) ;
		}
		writer.text( line.text) ;
		writer.number( static_cast<std::uint32_t>( line.attribution.kind)) ;
		writer.number( line.attribution.spans.size()) ;
		// 本文との文字対応を捏造せず、候補の推定範囲をそのまま保存する
		for( const auto &span : line.attribution.spans) { // 検証済みの元時刻と候補集合
			writer.number( static_cast<std::uint64_t>( span.startMs), 8) ;
			writer.number( static_cast<std::uint64_t>( span.endMs), 8) ;
			writer.number( span.speakers.size()) ;
			// 同時に有効な候補IDを、重複なしの順序で保存する
			for( const int speaker : span.speakers) { // 行の候補に含まれる番号
				writer.number( static_cast<std::uint32_t>( speaker)) ;
			}
		}
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 一つの結果を有界なcontainerへ復元します
 * @param[in,out] reader 未読byte列を保持する読取り器
 * @param[in] version 読取りを許可した形式。1/2は旧CTCの品質、1/2/3は理由未記録を保持
 * @return 全項目を所有する結果。意味上の整合性はsnapshot検証で確認します
 */
//------------------------------------------------------------------------------
[[nodiscard]] Transcript readResult( Reader &reader, const std::uint64_t version)
{
	Transcript result ; // UIへ借用bufferを渡さない所有結果
	result.speakerCount = static_cast<int>( reader.count( K_MAXIMUM_SEGMENTS, 1)) ;
	result.speakerMethod = static_cast<SpeakerMethod>( reader.number()) ;
	result.asrProfile = static_cast<AsrProfile>( reader.number()) ;
	result.asrModel = static_cast<AsrModel>( reader.number()) ;
	result.speechFilter = reader.flag() ;
	result.diarizationRequested = reader.flag() ;
	result.diarizationWarning = reader.flag() ;
	result.diarizationError = static_cast<ErrorKind>( reader.number()) ;
	result.diarizationCode = static_cast<std::uint32_t>( reader.number()) ;
	result.usedGpu = reader.flag() ;
	result.cpuRetried = reader.flag() ;
	// 旧形式のQualityはINT8を表し、新しいFP32結果と誤表示しない
	if( version < 3 && result.asrModel == AsrModel::PARAKEET) {
		requireHistory( result.asrProfile == AsrProfile::QUALITY && !result.usedGpu && !result.cpuRetried) ;
		result.asrProfile = AsrProfile::STANDARD ;
	}
	result.durationSeconds = reader.seconds() ;
	result.totalSeconds = reader.seconds() ;
	result.decodeSeconds = reader.seconds() ;
	result.transcriptionSeconds = reader.seconds() ;
	result.diarizationSeconds = reader.seconds() ;
	// file内の最大話者数と残byte数で検証した表示名の件数
	const auto names = reader.count( static_cast<std::size_t>( result.speakerCount), 8) ;
	// 名前は最大話者数まで読み、同じIDの二重定義を拒否する
	for( std::size_t index = 0; index < names; ++index) {
		const auto speaker = static_cast<int>( reader.number()) ; // 名前を持つ話者番号
		auto	   name = reader.text( K_MAXIMUM_NAME_BYTES) ; // 上限確認済みのUTF-8名前
		requireHistory( result.speakerNames.emplace( speaker, std::move( name)).second) ;
	}
	const auto turns = reader.count( K_MAXIMUM_SEGMENTS, 20) ; // 固定幅の推定区間数
	result.speakerTurns.reserve( turns) ;
	// 残量で検証した区間数だけを復元する
	for( std::size_t index = 0; index < turns; ++index) {
		SpeakerTurn turn ; // 次に追加する所有推定区間
		turn.startMs = std::bit_cast<std::int64_t>( reader.number( 8)) ;
		turn.endMs = std::bit_cast<std::int64_t>( reader.number( 8)) ;
		turn.speaker = static_cast<int>( reader.number()) ;
		result.speakerTurns.push_back( turn) ;
	}
	const auto lines = reader.count( K_MAXIMUM_SEGMENTS, 24) ; // 可変長行の最低限の固定部分
	result.lines.reserve( lines) ;
	std::size_t remainingText = K_MAXIMUM_TEXT_BYTES ; // このfileで残るUTF-8本文容量
	std::size_t remainingAttribution = K_MAXIMUM_ATTRIBUTION_ITEMS ; // 推定範囲とIDの残り容量
	// 行ごとのallocation前に本文残量と帰属数を確認する
	for( std::size_t index = 0; index < lines; ++index) {
		TranscriptLine line ; // 元時刻と全帰属を保持する所有行
		line.startMs = std::bit_cast<std::int64_t>( reader.number( 8)) ;
		line.endMs = std::bit_cast<std::int64_t>( reader.number( 8)) ;
		// この行へ付与された話者IDの件数。file内の最大話者数以下
		const auto speakers = reader.count( static_cast<std::size_t>( result.speakerCount), 4) ;
		line.speakers.reserve( speakers) ;
		// 未知話者は空列のまま、混在は全IDを保持する
		for( std::size_t speakerIndex = 0; speakerIndex < speakers; ++speakerIndex) {
			line.speakers.push_back( static_cast<int>( reader.number())) ;
		}
		line.text = reader.text( remainingText) ;
		remainingText -= line.text.size() ;
		// 旧形式には存在しない情報を読み足したり、現在の推定規則で補ったりしない
		if( version >= 4) {
			line.attribution.kind = static_cast<AttributionKind>( reader.number()) ;
			const auto spans = reader.count( remainingAttribution, 20) ; // 各範囲の最低固定幅
			remainingAttribution -= spans ;
			line.attribution.spans.reserve( spans) ;
			// allocation前に件数・残量を確認した範囲だけを復元する
			for( std::size_t spanIndex = 0; spanIndex < spans; ++spanIndex) { // 推定範囲の番号
				AttributionSpan span ; // この行が所有する元音声の推定範囲
				span.startMs = std::bit_cast<std::int64_t>( reader.number( 8)) ;
				span.endMs = std::bit_cast<std::int64_t>( reader.number( 8)) ;
				// この同期境界で所有する値、または確定状態の検証値です
				const auto ids =
					reader.count( std::min( remainingAttribution, static_cast<std::size_t>( result.speakerCount)),
								 4) ; // 同時に有効なID数。全jobの容量にも照合する
				remainingAttribution -= ids ;
				span.speakers.reserve( ids) ;
				// 話者名や確率を推定せず、記録された候補番号だけを読む
				for( std::size_t idIndex = 0; idIndex < ids; ++idIndex) { // 範囲内のID番号
					span.speakers.push_back( static_cast<int>( reader.number())) ;
				}
				line.attribution.spans.push_back( std::move( span)) ;
			}
		}
		result.lines.push_back( std::move( line)) ;
	}
	return result ;
}
//------------------------------------------------------------------------------
/**
 * @brief 整形拡張recordを検証済みの順序で保存します
 * @param[in,out] writer 有界な保存byte列
 * @param[in] entry 原文と整形結果を共有所有する履歴
 */
//------------------------------------------------------------------------------
void writeReading( Writer &writer, const HistoryEntry &entry)
{
	const auto &record = entry.reading ; // 永続部分だけを参照する整形状態
	writer.number( entry.id, 8) ;
	writer.number( static_cast<std::uint32_t>( record.preference)) ;
	writer.number( static_cast<std::uint32_t>( record.state)) ;
	writer.number( static_cast<std::uint32_t>( record.reason)) ;
	writer.number( record.code) ;
	writer.number( record.formatted != nullptr) ;
	// 最後の有効結果がないときは、元本文を複製しない
	if( !record.formatted) {
		return ;
	}
	const auto &formatted = *record.formatted ; // 全行を検証済みの置換結果
	writer.number( formatted.contractVersion) ;
	// hashを固定32byteとして保存し、ABIの構造体paddingを含めない
	for( const auto byte : formatted.source.sha256) {
		writer.number( byte, 1) ;
	}
	writer.text( formatted.profileId) ;
	// 実効方式のdigestも同じ固定長で保存する
	for( const auto byte : formatted.profileSha256) {
		writer.number( byte, 1) ;
	}
	writer.number( formatted.lines.size()) ;
	// 元index昇順の全行を保存し、無変更行の本文は複製しない
	for( const auto &line : formatted.lines) {
		writer.number( line.lineIndex) ;
		writer.number( static_cast<std::uint32_t>( line.kind)) ;
		writer.number( static_cast<std::uint32_t>( line.reason)) ;
		writer.text( line.text) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 同じ履歴IDの整形拡張recordを有界に復元します
 * @param[in,out] reader 外部byte列の未読部分
 * @param[in,out] entry 原文を読取り済みの履歴
 * @note 全体照合まで部分結果を呼出し側へ返しません
 */
//------------------------------------------------------------------------------
void readReading( Reader &reader, HistoryEntry &entry)
{
	requireHistory( reader.number( 8) == entry.id) ;
	auto &record = entry.reading ; // 現entryだけへ束縛する整形状態
	record.preference = static_cast<ReadingPreference>( reader.number()) ;
	record.state = static_cast<ReadingState>( reader.number()) ;
	record.reason = static_cast<ReadingReason>( reader.number()) ;
	record.code = static_cast<std::uint32_t>( reader.number()) ;
	requireHistory( static_cast<std::uint32_t>( record.preference) <= 2 &&
				   static_cast<std::uint32_t>( record.state) <= 9 && static_cast<std::uint32_t>( record.reason) <= 9) ;
	// 結果なしは元本文への参照だけとし、追加containerを確保しない
	if( !reader.flag()) {
		return ;
	}
	auto formatted = std::make_shared<FormattedTranscript>() ; // 完全検証前の所有結果
	formatted->source.historyId = entry.id ;
	formatted->contractVersion = static_cast<std::uint32_t>( reader.number()) ;
	requireHistory( formatted->contractVersion == K_READING_CONTRACT_VERSION) ;
	// 32byteの全hashを復元し、本文との照合は全体検証で行う
	for( auto &byte : formatted->source.sha256) {
		byte = static_cast<std::uint8_t>( reader.number( 1)) ;
	}
	formatted->profileId = reader.text( 64) ;
	// 方式のdigestを固定長で復元する
	for( auto &byte : formatted->profileSha256) {
		byte = static_cast<std::uint8_t>( reader.number( 1)) ;
	}
	const auto count = reader.count( K_MAXIMUM_SEGMENTS, 16) ; // 一行の最低固定byte数
	requireHistory( entry.result && count == entry.result->lines.size()) ;
	formatted->lines.reserve( count) ;
	std::size_t textBytes = 0 ; // copy済み置換本文の合計容量
	// 件数と一行上限を照合しながら元index順の所有値を復元する
	for( std::size_t index = 0; index < count; ++index) {
		FormattedLine line ; // 今回復元する一行
		line.lineIndex = static_cast<std::uint32_t>( reader.number()) ;
		line.kind = static_cast<ReadingLineKind>( reader.number()) ;
		line.reason = static_cast<ReadingLineReason>( reader.number()) ;
		requireHistory( line.lineIndex == index && static_cast<std::uint32_t>( line.kind) <= 2 &&
					   static_cast<std::uint32_t>( line.reason) <= 4) ;
		line.text = reader.text( std::min( K_MAXIMUM_FORMATTED_LINE_BYTES, K_MAXIMUM_TEXT_BYTES - textBytes)) ;
		textBytes += line.text.size() ;
		formatted->lines.push_back( std::move( line)) ;
	}
	record.formatted = std::move( formatted) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 固定SHA全byteを保存します
 * @param[in,out] out writer
 * @param[in] digest SHA
 */
//------------------------------------------------------------------------------
void writeDigest( Writer &out, const std::array<std::uint8_t, 32> &digest)
{
	// 構造体paddingを含めず32byteだけ保存します
	for( const auto byte : digest) {
		out.number( byte, 1) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 固定SHA全byteを復元します
 * @param[in,out] in reader
 * @return 32byte
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::array<std::uint8_t, 32> readDigest( Reader &in)
{
	std::array<std::uint8_t, 32> digest{} ; // 全消費まで外へ返しません
	// 固定32byteを一つずつ復元します
	for( auto &byte : digest) {
		byte = static_cast<std::uint8_t>( in.number( 1)) ;
	}
	return digest ;
}
//------------------------------------------------------------------------------
/**
 * @brief 全PCM metadataを固定順で保存します
 * @param[in,out] out writer
 * @param[in] audio PCM値
 */
//------------------------------------------------------------------------------
void writeAudioIdentity( Writer &out, const DecodedPcmIdentity &audio)
{
	out.number( audio.version) ;
	out.number( audio.sampleCount, 8) ;
	out.number( audio.sampleRate) ;
	out.number( audio.channels) ;
	out.number( audio.originSample, 8) ;
	writeDigest( out, audio.pcmSha256) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 全PCM metadataを復元します
 * @param[in,out] in reader
 * @return 未検証小値
 */
//------------------------------------------------------------------------------
[[nodiscard]] DecodedPcmIdentity readAudioIdentity( Reader &in)
{
	DecodedPcmIdentity audio ; // ABIへ依存しない値
	audio.version = static_cast<std::uint32_t>( in.number()) ;
	audio.sampleCount = in.number( 8) ;
	audio.sampleRate = static_cast<std::uint32_t>( in.number()) ;
	audio.channels = static_cast<std::uint32_t>( in.number()) ;
	audio.originSample = in.number( 8) ;
	audio.pcmSha256 = readDigest( in) ;
	return audio ;
}
//------------------------------------------------------------------------------
/**
 * @brief V5 blockの後ろへ独立application拡張を保存します
 * @param[in,out] out writer
 * @param[in] entry 全体検証済み履歴
 */
//------------------------------------------------------------------------------
void writeApplication( Writer &out, const HistoryEntry &entry)
{
	out.number( entry.id, 8) ;
	out.number( static_cast<std::uint32_t>( entry.resultPreference)) ;
	out.number( entry.result && entry.result->pcmIdentity) ;
	// 元ASRで採取した証拠だけ保存します
	if( entry.result && entry.result->pcmIdentity) {
		writeAudioIdentity( out, *entry.result->pcmIdentity) ;
	}
	const auto &record = entry.wholeText ; // 永続部分だけのborrow
	out.number( static_cast<std::uint32_t>( record.state)) ;
	out.number( static_cast<std::uint32_t>( record.reason)) ;
	out.number( record.code) ;
	out.number( record.corrected != nullptr) ;
	// 最後の有効結果がない場合は拡張固定部だけです
	if( !record.corrected) {
		return ;
	}
	const auto &text = *record.corrected ; // 検証済み証拠付き全文
	out.number( text.contractVersion) ;
	writeDigest( out, text.source.sha256) ;
	out.text( text.profileId) ;
	writeDigest( out, text.profileSha256) ;
	out.number( text.evidence->version) ;
	writeAudioIdentity( out, text.evidence->audio) ;
	const auto binding = encodeWholeTextInputBinding( text.evidence->binding) ; // 最大512byte
	out.number( binding.size()) ;
	// historical canonical encodingをそのまま保存します
	for( const auto byte : binding) {
		out.number( byte, 1) ;
	}
	writeDigest( out, text.evidence->binding.inputSha256) ;
	out.text( text.text) ;
}
//------------------------------------------------------------------------------
/**
 * @brief V6の同ID application拡張を復元します
 * @param[in,out] in reader
 * @param[in,out] entry V5まで読取済みの履歴
 */
//------------------------------------------------------------------------------
void readApplication( Reader &in, HistoryEntry &entry)
{
	requireHistory( in.number( 8) == entry.id) ;
	entry.resultPreference = static_cast<ResultPreference>( in.number()) ;
	// PCMを反映してから不変ownerを作り直します
	if( in.flag()) {
		requireHistory( entry.result != nullptr) ;
		// この同期境界で所有する値、または確定状態の検証値です
		auto source = std::make_shared<Transcript>( *entry.result) ; // worker内だけのmutable builder
		source->pcmIdentity = readAudioIdentity( in) ;
		entry.result = std::move( source) ;
	}
	auto &record = entry.wholeText ; // active/phaseは既定空
	record.state = static_cast<ReadingState>( in.number()) ;
	record.reason = static_cast<ReadingReason>( in.number()) ;
	record.code = static_cast<std::uint32_t>( in.number()) ;
	record.source = entry.result ;
	// 結果なしでも試行stateは保持します
	if( !in.flag()) {
		return ;
	}
	requireHistory( entry.result != nullptr) ;
	auto text = std::make_shared<WholeTextCorrection>() ; // 全検証までworker内所有
	text->contractVersion = static_cast<std::uint32_t>( in.number()) ;
	text->source = {entry.id, readDigest( in)} ;
	text->profileId = in.text( 64) ;
	text->profileSha256 = readDigest( in) ;
	WholeTextCorrectionEvidence proof ; // 小さい証拠
	proof.version = static_cast<std::uint32_t>( in.number()) ;
	proof.audio = readAudioIdentity( in) ;
	const auto				  count = in.count( 512, 1) ; // allocation前の固定最大
	std::vector<std::uint8_t> binding ; // bounded canonical encoding
	binding.reserve( count) ;
	// 全binding byteを順番に読みます
	for( std::size_t index = 0; index < count; ++index) {
		binding.push_back( static_cast<std::uint8_t>( in.number( 1))) ;
	}
	try {
		proof.binding = decodeWholeTextInputBinding( binding) ;
	} catch( const OperationError &error) {
		throw HistoryError( HistoryErrorKind::CORRUPT, error.code()) ;
	}
	proof.binding.inputSha256 = readDigest( in) ;
	text->evidence = std::move( proof) ;
	text->text = in.text( K_MAXIMUM_TEXT_BYTES) ;
	record.corrected = std::move( text) ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 共通field writerへ全履歴を書きます
 * @param[in] snapshot 正本
 * @param[in,out] writer sink
 */
//------------------------------------------------------------------------------
void writeHistory( const HistorySnapshot &snapshot, Writer &writer)
{
	validateSnapshot( snapshot) ;
	writer.number( K_HISTORY_MAGIC, 8) ;
	writer.number( K_HISTORY_VERSION) ;
	writer.number( snapshot.nextId, 8) ;
	writer.number( snapshot.entries.size()) ;
	// 検証済みの履歴を登録順に記録する
	for( const auto &entry : snapshot.entries) {
		writer.number( entry.id, 8) ;
		writer.number( static_cast<std::uint32_t>( entry.state)) ;
		writer.number( static_cast<std::uint32_t>( entry.error)) ;
		writer.number( entry.code) ;
		writer.number( std::bit_cast<std::uint32_t>( static_cast<std::int32_t>( entry.filter))) ;
		writer.text( pathText( entry.input)) ;
		writer.number( static_cast<bool>( entry.result)) ;
		// 成功結果がある項目だけに本文の所有snapshotを続ける
		if( entry.result) {
			writeResult( writer, *entry.result) ;
		}
	}
	writer.number( snapshot.entries.size()) ; // 全entryに一つずつ拡張recordを持たせる
	// 原文の全entryの後で、同じID順の整形拡張を保存する
	for( const auto &entry : snapshot.entries) {
		writeReading( writer, entry) ;
	}
	writer.number( snapshot.entries.size()) ; // V6独立拡張件数
	// 元と同じID順でapplication拡張を書きます
	for( const auto &entry : snapshot.entries) {
		writeApplication( writer, entry) ;
	}
	constexpr char HASH_DOMAIN[] = "koefude.history_bounded.v7" ; // NUL込み拡張識別子
	writer.raw( {reinterpret_cast<const std::uint8_t *>( HASH_DOMAIN), sizeof(HASH_DOMAIN)}) ;
	writer.number( 1) ;
	writer.number( snapshot.nextOperationId, 8) ;
	writer.number( snapshot.entries.size()) ;
	// 元と同じ昇順IDで有界拡張を追加します
	for( const auto &entry : snapshot.entries) {
		writer.raw( detail::encodeBoundedRecord( entry.id, entry.bounded)) ;
	}
	constexpr char AUDIO_DOMAIN[] = "koefude.history_api_audio.v8" ; // V7以前のfield順は変更しません
	writer.raw( {reinterpret_cast<const std::uint8_t *>( AUDIO_DOMAIN), sizeof(AUDIO_DOMAIN)}) ;
	writer.number( 1) ;
	writer.number( snapshot.entries.size()) ;
	// 同じ登録順/IDへ有無付き音声recordを追加します
	for( const auto &entry : snapshot.entries) {
		writer.number( entry.id, 8) ;
		writer.number( static_cast<bool>( entry.apiAudio)) ;
		// 音声APIで作成した行だけが追加recordを持ちます
		if( entry.apiAudio) {
			writer.text( encodeApiAudioRecord( *entry.apiAudio)) ;
		}
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 所有履歴を検証してversion付きの保存byte列へ変換します
 * @param[in] snapshot 同期呼出し中だけ参照する履歴
 * @return 512MiB以下の所有byte列。SHA-256とfile I/Oはstorageの責務
 * @throws HistoryError 上限超過、不正ID・状態・文字・時刻
 * @see koefude::encodeHistory
 */
//------------------------------------------------------------------------------
std::vector<std::uint8_t> encodeHistory( const HistorySnapshot &snapshot)
{
	Writer writer ; /* 所有sink */
	writeHistory( snapshot, writer) ;
	return writer.finish() ;
}
//------------------------------------------------------------------------------
/**
 * @brief 同じfield writerの計数sinkで保存長を測ります
 * @param[in] snapshot 全履歴
 * @return SHA32を除くchecked byte数
 * @see koefude::measureEncodedHistory
 */
//------------------------------------------------------------------------------
std::uint64_t measureEncodedHistory( const HistorySnapshot &snapshot)
{
	Writer writer( true) ; /* 計数sink */
	writeHistory( snapshot, writer) ;
	return writer.size() ;
}

//------------------------------------------------------------------------------
/**
 * @brief 外部の保存byte列を検証して所有履歴へ変換します
 * @param[in] bytes 呼出し中だけ有効な512MiB以下の保存byte列
 * @return 検証済みの履歴。不正時に部分結果を返しません
 * @throws HistoryError 不正形式、未知version、上限超過
 * @note 元音声・モデルは開かず、pathは表示用の値としてだけ復元します
 * @see koefude::decodeHistory
 */
//------------------------------------------------------------------------------
HistorySnapshot decodeHistory( const std::span<const std::uint8_t> bytes)
{
	// 大きな外部fileを形式解釈へ渡さず、割当てと走査を制限する
	if( bytes.size() > K_MAXIMUM_HISTORY_BYTES) {
		throw HistoryError( HistoryErrorKind::TOO_LARGE) ;
	}
	Reader reader( bytes) ; // 呼出し中だけ有効な入力byte列のborrow
	requireHistory( reader.number( 8) == K_HISTORY_MAGIC) ;
	const auto version = reader.number() ; // 並びが同じ旧版との互換性を確認する版番号
	// 対応する旧版だけを明示して許可し、未知の新形式を推測で読まない
	if( version != 1 && version != 2 && version != 3 && version != 4 && version != 5 && version != 6 && version != 7 &&
	   version != K_HISTORY_VERSION) {
		throw HistoryError( HistoryErrorKind::VERSION) ;
	}
	HistorySnapshot snapshot ; // 完全検証後にだけ呼出し元へ返す所有履歴
	snapshot.nextId = reader.number( 8) ;
	const auto entries = reader.count( K_MAXIMUM_HISTORY, 32) ; // entryの最低固定byte数
	snapshot.entries.reserve( entries) ;
	// 上限内の各entryを復元し、途中の失敗では全体を破棄する
	for( std::size_t index = 0; index < entries; ++index) {
		HistoryEntry entry ; // 一つの元音声への所有記録
		entry.id = reader.number( 8) ;
		entry.state = static_cast<HistoryState>( reader.number()) ;
		entry.error = static_cast<ErrorKind>( reader.number()) ;
		entry.code = static_cast<std::uint32_t>( reader.number()) ;
		entry.filter = std::bit_cast<std::int32_t>( static_cast<std::uint32_t>( reader.number())) ;
		const auto path = reader.text( K_MAXIMUM_PATH_BYTES) ; // 残量検証済みのUTF-8 path
		static_cast<void>( validateText( path)) ;
		entry.input = toWide( path) ;
		// 所有結果を伴う項目だけで追加のcontainerを読み込む
		if( reader.flag()) {
			entry.result = std::make_shared<const Transcript>( readResult( reader, version)) ;
		}
		snapshot.entries.push_back( std::move( entry)) ;
	}
	// 旧形式には整形拡張がなく、Automatic/Idleの初期値を維持する
	if( version >= 5) {
		requireHistory( reader.count( K_MAXIMUM_HISTORY, 28) == snapshot.entries.size()) ;
		// 読取り済みの各entryへID一致を要求して拡張を束縛する
		for( auto &entry : snapshot.entries) {
			readReading( reader, entry) ;
			entry.resultPreference = static_cast<ResultPreference>( entry.reading.preference) ;
		}
	}
	// V6だけにPCMと自由全文が存在します
	if( version >= 6) {
		requireHistory( reader.count( K_MAXIMUM_HISTORY, 32) == snapshot.entries.size()) ;
		// 未知/重複/逆順IDはread_applicationで全体拒否します
		for( auto &entry : snapshot.entries) {
			readApplication( reader, entry) ;
		}
	}
	// V7はV6の全fieldの末尾へ追加します
	if( version >= 7) {
		constexpr char HASH_DOMAIN[] = "koefude.history_bounded.v7" ; // NUL込み
		const auto	   remaining = reader.remaining() ; // 検証前byte
		requireHistory( remaining.size() >= sizeof(HASH_DOMAIN) &&
					   std::equal( remaining.begin(), remaining.begin() + sizeof(HASH_DOMAIN),
								  reinterpret_cast<const std::uint8_t *>( HASH_DOMAIN))) ;
		reader.consume( sizeof(HASH_DOMAIN)) ;
		requireHistory( reader.number() == 1) ;
		snapshot.nextOperationId = reader.number( 8) ;
		requireHistory( reader.count( K_MAXIMUM_HISTORY, 44) == snapshot.entries.size()) ;
		// 未知ID/件数/descriptorを全体拒否します
		for( auto &entry : snapshot.entries) {
			std::size_t consumed = 0 ; /* 小さいentry encoding長 */
			entry.bounded = detail::decodeBoundedRecord( reader.remaining(), entry.id, consumed) ;
			reader.consume( consumed) ;
		}
	} else {
		// 旧版の表示意図は0..3だけです
		for( const auto &entry : snapshot.entries) {
			requireHistory( static_cast<std::uint32_t>( entry.resultPreference) <= 3) ;
			// V6の有効V1証拠も再利用禁止のoperation floorへ引き継ぎます
			if( entry.wholeText.corrected && entry.wholeText.corrected->evidence) {
				const auto operationId =
					entry.wholeText.corrected->evidence->binding.request.operationId ; // 旧版が確定した要求番号
				// 最大値の次番号をwrapさせず、明示的に容量境界として拒否します
				if( operationId == std::numeric_limits<std::uint64_t>::max()) {
					throw HistoryError( HistoryErrorKind::TOO_LARGE) ;
				}
				snapshot.nextOperationId = std::max( snapshot.nextOperationId, operationId + 1) ;
			}
		}
	}
	// V8は旧拡張の後で音声API recordを同じID順へ束縛します
	if( version >= 8) {
		constexpr char HASH_DOMAIN[] = "koefude.history_api_audio.v8" ; // NUL込み
		const auto	   rest = reader.remaining() ; // 検証前byte
		requireHistory( rest.size() >= sizeof(HASH_DOMAIN) &&
					   std::equal( rest.begin(), rest.begin() + sizeof(HASH_DOMAIN),
								  reinterpret_cast<const std::uint8_t *>( HASH_DOMAIN))) ;
		reader.consume( sizeof(HASH_DOMAIN)) ;
		requireHistory( reader.number() == 1) ;
		requireHistory( reader.count( K_MAXIMUM_HISTORY, 12) == snapshot.entries.size()) ;
		// 未知/重複/逆順IDや未知recordを拒否します
		for( auto &entry : snapshot.entries) {
			requireHistory( reader.number( 8) == entry.id) ;
			// 無recordのlocal原文は既存値を維持します
			if( reader.flag()) {
				try {
					entry.apiAudio =
						std::make_shared<const ApiAudioRecord>( decodeApiAudioRecord( reader.text( 40 * 1024 * 1024))) ;
				} catch( const OperationError &error) {
					throw HistoryError( HistoryErrorKind::CORRUPT, error.code()) ;
				}
				// 保存bodyを再生成せず、表示時刻のbasisだけをrecordから復元します
				if( entry.result) {
					auto source = std::make_shared<Transcript>( *entry.result) ;
					source->apiAudioWindowTimes = true ;
					entry.result = std::move( source) ;
				}
			}
		}
	}
	requireHistory( reader.empty()) ;
	validateSnapshot( snapshot) ;
	// 破損を検証し終えてから未終端試行だけを中断へ変更し、再生成しない
	for( auto &entry : snapshot.entries) {
		// 未完の音声要求は最後の耐久prefixへ中断復元し、自動再送しません
		if( entry.apiAudio && apiAudioActive( entry.apiAudio->terminal)) {
			auto interrupted = std::make_shared<ApiAudioRecord>( *entry.apiAudio) ; // workerだけがclone
			interrupted->terminal = ApiAudioTerminal::INTERRUPTED ;
			syncApiAudioAttempt( *interrupted) ;
			entry.apiAudio = std::move( interrupted) ;
			entry.state = HistoryState::INTERRUPTED ;
		}
		restoreReading( entry.reading) ;
		restoreWholeTextCorrection( entry.wholeText) ;
	}
	return snapshot ;
}

//------------------------------------------------------------------------------
/**
 * @brief 有界保存を実装しない基底経路として、予約要求を常に拒否します
 * @return 正常復帰せず、予約値は返しません
 * @throws OperationError MODEL_REJECTED/PERSISTENCE_UNAVAILABLEを常に送出します
 * @note 名前のない第1引数HistorySnapshot &は変更しないworker正本、第2引数const WholeTextCorrectionJobRequest &は受理しない要求、
 *        第3引数std::stop_tokenは参照しない停止指定です。保存や資源予約は行いません
 */
//------------------------------------------------------------------------------
WholeTextBoundedStorageBudget IHistoryStorage::reserveBounded( HistorySnapshot &, const WholeTextCorrectionJobRequest &,
															  std::stop_token)
{
	throw OperationError( ErrorKind::MODEL_REJECTED,
						 static_cast<std::uint32_t>( WholeTextBoundedDiagnostic::PERSISTENCE_UNAVAILABLE)) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 有界終端保存が未実装の基底経路として、正本やterminalを消費せず常に拒否します
 * @return 正常復帰せず、保存ACKは返しません
 * @throws HistoryError INTERNALを常に送出します
 * @note 名前のない第1引数HistorySnapshot &は変更しない正本、第2引数WholeTextBoundedTerminal &は非消費の終端、第3引数HistorySaveReceipt &は未変更の出力、
 *        第4引数std::stop_tokenは参照しない停止指定です
 */
//------------------------------------------------------------------------------
std::shared_ptr<const WholeTextBoundedPersistenceCompletion>
IHistoryStorage::persistBounded( HistorySnapshot &, WholeTextBoundedTerminal &, HistorySaveReceipt &, std::stop_token)
{
	throw HistoryError( HistoryErrorKind::INTERNAL) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 未実装の基底経路では未保存prefix ACKを確保せず、空の共有pointerを返します
 * @return 常にnullptr相当の空shared_ptrです。保存成功やACK成立を示しません
 * @note 名前のない第1引数HistoryErrorKindは使用しない保存失敗分類、第2引数std::uint32_tは使用しない元診断codeです。状態変更・確保・例外送出を行いません
 */
//------------------------------------------------------------------------------
std::shared_ptr<const WholeTextBoundedPersistenceCompletion> IHistoryStorage::boundedFailure( HistoryErrorKind,
																							 std::uint32_t) noexcept
{
	return {} ;
}
//------------------------------------------------------------------------------
/**
 * @brief 未完試行の削除を実装しない基底経路として、正本とreceiptを変更せず拒否します
 * @return 正常復帰せず、cleanup結果は返しません
 * @throws HistoryError INTERNALを常に送出します
 * @note 名前のない第1引数HistorySnapshot &は未変更の正本、第2引数const ReadingSourceKey &は対象原文key、第3引数std::uint64_tは対象operation ID、
 *        第4引数HistorySaveReceipt &は未変更の出力です。fileを削除しません
 */
//------------------------------------------------------------------------------
WholeTextBoundedCleanupResult IHistoryStorage::discardBounded( HistorySnapshot &, const ReadingSourceKey &,
															  std::uint64_t, HistorySaveReceipt &)
{
	throw HistoryError( HistoryErrorKind::INTERNAL) ;
}
} // namespace koefude
