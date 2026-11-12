//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_correction.cpp
 * @brief 自由全文の全体検証と原文・旧有効結果を保持する試行を実装します
 * @version 1.0.0
 * @date 2026/10/19
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/19 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/19 model-freeの所有・容量・終端境界を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 歴史encodingを共用し証拠付き全文を検証
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1343の文章専用補正を接続
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
#include "core/whole_text_correction.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <windows.h>

// Win32基本型を定義してからCNG宣言を読み込みます
#include <bcrypt.h>

#include "platform/win_helpers.h"

namespace koefude {
namespace {
using Digest = std::array<std::uint8_t, 32> ; // SHA-256固定幅
//------------------------------------------------------------------------------
/**
 * @brief CNGの元codeを保持します
 * @param[in] status CNG NTSTATUS
 */
//------------------------------------------------------------------------------
void checkCrypto( const NTSTATUS status)
{
	// 未完成digestを比較へ渡しません
	if( status < 0) {
		throw OperationError( ErrorKind::INTERNAL, static_cast<std::uint32_t>( status)) ;
	}
}
/** @brief CNG providerのRAII解放です */
struct AlgorithmDeleter {
	//------------------------------------------------------------------------------
	/**
	 * @brief providerを閉じます
	 * @param[in] value 所有handle
	 */
	//------------------------------------------------------------------------------
	void operator()( BCRYPT_ALG_HANDLE value) const noexcept
	{
		BCryptCloseAlgorithmProvider( value, 0) ;
	}
} ;
/** @brief CNG hashのRAII解放です */
struct HashDeleter {
	//------------------------------------------------------------------------------
	/**
	 * @brief hashを閉じます
	 * @param[in] value 所有handle
	 */
	//------------------------------------------------------------------------------
	void operator()( BCRYPT_HASH_HANDLE value) const noexcept
	{
		BCryptDestroyHash( value) ;
	}
} ;
//------------------------------------------------------------------------------
/**
 * @brief 有界byte列を停止確認しながらCNG SHA-256へ渡します
 * @param[in] bytes 全byteのborrow
 * @param[in] stop 協調停止
 * @return 全量SHA
 */
//------------------------------------------------------------------------------
[[nodiscard]] Digest sha256( const std::span<const std::uint8_t> bytes, const std::stop_token stop)
{
	checkCancelled( stop) ;
	BCRYPT_ALG_HANDLE rawAlgorithm = nullptr ; // CNG受取先
	checkCrypto( BCryptOpenAlgorithmProvider( &rawAlgorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) ;
	const std::unique_ptr<void, AlgorithmDeleter> algorithm( rawAlgorithm) ; // provider所有
	BCRYPT_HASH_HANDLE							  rawHash = nullptr ; // CNG受取先
	checkCrypto( BCryptCreateHash( algorithm.get(), &rawHash, nullptr, 0, nullptr, 0, 0)) ;
	const std::unique_ptr<void, HashDeleter> hash( rawHash) ; // hash所有
	// 64KiBごとに停止し、巨大な単一hash呼出しを作りません
	for( std::size_t offset = 0; offset < bytes.size();) { // 全byte内の位置
		checkCancelled( stop) ;
		const auto count = std::min<std::size_t>( 65536, bytes.size() - offset) ; // 今回分
		// CNGの旧mutable型境界だけconstを外し、APIは入力を書き換えません
		checkCrypto(
			BCryptHashData( hash.get(), const_cast<PUCHAR>( bytes.data() + offset), static_cast<ULONG>( count), 0)) ;
		offset += count ;
	}
	Digest result{} ; // 全量SHAの出力
	checkCrypto( BCryptFinishHash( hash.get(), result.data(), static_cast<ULONG>( result.size()), 0)) ;
	checkCancelled( stop) ;
	return result ;
}
//------------------------------------------------------------------------------
/**
 * @brief UTF-8 byteを値変換なしでhashします
 * @param[in] text 元byte
 * @param[in] stop 協調停止
 * @return 全量SHA
 */
//------------------------------------------------------------------------------
[[nodiscard]] Digest sha256( const std::string_view text, const std::stop_token stop)
{
	// std::byte表現のborrowは呼出し中だけ元stringの寿命に束縛します
	return sha256( std::span( reinterpret_cast<const std::uint8_t *>( text.data()), text.size()), stop) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 不正な構造を本文なしの例外へ変換します
 * @param[in] valid 成立条件
 * @param[in] kind 元のerror分類
 */
//------------------------------------------------------------------------------
void requireWhole( const bool valid, const ErrorKind kind = ErrorKind::INVALID_INPUT)
{
	// 全体の検証が終わらない候補を部分採用しない
	if( !valid) {
		throw OperationError( kind) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 同じpointerと共有制御blockかを調べます
 * @param[in] left 原文owner
 * @param[in] right 比較する原文owner
 * @return 同じ非null ownerならtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool sameOwner( const std::shared_ptr<const Transcript> &left,
							 const std::shared_ptr<const Transcript> &right) noexcept
{
	return left && right && left.get() == right.get() && !left.owner_before( right) && !right.owner_before( left) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 固定要求の全fieldを照合します
 * @param[in] left 現要求
 * @param[in] right 照合する要求
 * @return 一致時true
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool sameRequest( const WholeTextCorrectionRequest &left, const WholeTextCorrectionRequest &right) noexcept
{
	return left.operationId == right.operationId && left.source == right.source && left.profileId == right.profileId &&
		   left.profileSha256 == right.profileSha256 ;
}

//------------------------------------------------------------------------------
/**
 * @brief 未終端の試行を判定します
 * @param[in] state 共通状態
 * @return 処理中ならtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool running( const ReadingState state) noexcept
{
	return state >= ReadingState::QUEUED && state <= ReadingState::CANCELLING ;
}

//------------------------------------------------------------------------------
/**
 * @brief Unicodeの空白だけの本文を拒否するための分類です
 * @param[in] unit 検証済みUTF-16 code unit
 * @return 空白ならtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool blankUnit( const wchar_t unit) noexcept
{
	return unit == L' ' || (unit >= L'\t' && unit <= L'\r') || unit == 0x00a0 || unit == 0x1680 ||
		   (unit >= 0x2000 && unit <= 0x200a) || unit == 0x2028 || unit == 0x2029 || unit == 0x202f || unit == 0x205f ||
		   unit == 0x3000 || unit == 0xfeff ;
}

//------------------------------------------------------------------------------
/**
 * @brief 元本文と全文本文を切り詰めず照合します
 * @param[in] source 検証済み原文
 * @param[in] request 固定要求
 * @param[in] result 未検証全文
 * @throws OperationError 構造/容量違反
 */
//------------------------------------------------------------------------------
void validateResult( const Transcript &source, const WholeTextCorrectionRequest &request,
					const WholeTextCorrection &result)
{
	requireWhole( result.source == request.source && result.contractVersion == 1 &&
				 result.profileId == request.profileId && result.profileSha256 == request.profileSha256) ;
	std::size_t remaining = K_MAXIMUM_TEXT_BYTES ; // 減算前に検査する元文＋全文の残量
	// 元行の順序や帰属を変更せず、本文byteだけを容量へ加える
	for( const auto &line : source.lines) {
		requireWhole( line.text.size() <= remaining, ErrorKind::RESULT_TOO_LARGE) ;
		remaining -= line.text.size() ;
	}
	requireWhole( result.text.size() <= remaining, ErrorKind::RESULT_TOO_LARGE) ;
	requireWhole( !result.text.empty() && result.text.find( '\0') == std::string::npos) ;
	const auto wide = toWide( result.text) ; // 厳密UTF-8の確認だけに使い元byteは変更しない
	requireWhole( std::any_of( wide.begin(), wide.end(),
							 //------------------------------------------------------------------------------
							 /**
							  * @brief 非空白の本文があるかを調べます
							  * @param[in] unit UTF-16 code unit
							  * @return 非空白ならtrue
							  */
							 //------------------------------------------------------------------------------
							 []( const wchar_t unit)
							 {
								 return !blankUnit( unit) ;
							 })) ;
}

//------------------------------------------------------------------------------
/**
 * @brief PCM metadataの正規形を検証します
 * @param[in] audio 小さい実測値
 */
//------------------------------------------------------------------------------
void validateAudio( const DecodedPcmIdentity &audio)
{
	requireWhole( audio.version == 1 && audio.sampleCount > 0 && audio.sampleCount <= K_MAXIMUM_TIMELINE_SAMPLES &&
				 audio.sampleRate == K_SAMPLE_RATE && audio.channels == 1 && audio.originSample == 0 &&
				 audio.pcmSha256 != Digest{}) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 証拠がある応答の全束縛を検査します
 * @param[in] source 元本文
 * @param[in] request 要求
 * @param[in] result 全文
 */
//------------------------------------------------------------------------------
void validateEvidence( const Transcript &source, const WholeTextCorrectionRequest &request,
					  const WholeTextCorrection &result)
{
	// 汎用model-free内部契約の証拠なし応答は引き続き許可します
	if( !result.evidence) {
		return ;
	}
	const auto &proof = *result.evidence ; // 候補を変更しないborrow
	const auto &binding = proof.binding ; // 完了時のhistorical要求
	requireWhole( proof.version == 1 && source.pcmIdentity && proof.audio == *source.pcmIdentity &&
				 sameRequest( binding.request, request)) ;
	requireWhole( !request.profileId.empty() && request.profileId.size() <= 64 && request.profileSha256 != Digest{}) ;
	// 保存された方式も固定ASCII識別子だけを許可します
	for( const unsigned char unit : request.profileId) {
		requireWhole( (unit >= 'a' && unit <= 'z') || (unit >= 'A' && unit <= 'Z') || (unit >= '0' && unit <= '9') ||
					 unit == '-' || unit == '_') ;
	}
	validateAudio( proof.audio) ;
	std::string original ; // 最大8MiBの直接連結
	// 元行を一度ずつ全byte追加し、前後空白も維持します
	for( const auto &line : source.lines) {
		original += line.text ;
	}
	requireWhole(
		binding.originalBytes == original.size() && binding.originalSha256 == sha256( original, {}) &&
		binding.originalSha256 != Digest{} && binding.auxiliarySha256 != Digest{} && binding.systemSha256 != Digest{} &&
		binding.userSha256 != Digest{} && binding.inputSha256 != Digest{} && binding.auxiliaryBytes > 0 &&
		binding.auxiliaryBytes <= K_MAXIMUM_TEXT_BYTES && binding.systemBytes > 0 &&
		binding.systemBytes <= K_MAXIMUM_TEXT_BYTES && binding.userBytes > 0 &&
		binding.userBytes <= K_MAXIMUM_TEXT_BYTES && binding.inputSha256 == fingerprintWholeTextInputBinding( binding)) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 行用reasonを使わず元errorを共通理由へ分類します
 * @param[in] error 元error
 * @return 試行の失敗理由
 */
//------------------------------------------------------------------------------
[[nodiscard]] ReadingReason failureReason( const ErrorKind error) noexcept
{
	return error == ErrorKind::MODEL_MISSING || error == ErrorKind::MODEL_REJECTED ? ReadingReason::MODEL_UNAVAILABLE
		   : error == ErrorKind::RESULT_TOO_LARGE || error == ErrorKind::GPU_UNAVAILABLE
			   ? ReadingReason::RESOURCE_UNAVAILABLE
		   : error == ErrorKind::INVALID_INPUT ? ReadingReason::INVALID_OUTPUT
											   : ReadingReason::ENGINE_FAILURE ;
}
} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 生成器呼出し前に要求と原文の全体を照合します
 * @param[in] source 共有所有原文。null不可
 * @param[in] request 受付ID設定済みの固定要求
 * @throws OperationError 構造/束縛不一致、または容量超過
 * @note hashと本文走査を行うworker限定APIです
 * @see koefude::validateWholeTextCorrectionRequest
 */
//------------------------------------------------------------------------------
void validateWholeTextCorrectionRequest( const std::shared_ptr<const Transcript> &source,
										const WholeTextCorrectionRequest		&request)
{
	requireWhole( source && !source->lines.empty() && request.operationId != 0 && request.source.historyId != 0 &&
				 !request.profileId.empty() && request.profileId.size() <= 64) ;
	// 既存profileと同じ固定ASCII識別子だけを受け付ける
	for( const unsigned char unit : request.profileId) {
		requireWhole( (unit >= 'a' && unit <= 'z') || (unit >= 'A' && unit <= 'Z') || (unit >= '0' && unit <= '9') ||
					 unit == '-' || unit == '_') ;
	}
	try {
		requireWhole( request.source.sha256 == fingerprintReadingSource( *source)) ;
	} catch( const OperationError &error) {
		// 原文時刻の構造違反も、この要求入口ではInvalidInputとして返す
		if( error.kind() == ErrorKind::INVALID_TIMELINE) {
			throw OperationError( ErrorKind::INVALID_INPUT, error.code()) ;
		}
		throw ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 自然終端・束縛・UTF-8・容量を全体検証し応答を保持します
 * @param[in] source 不変原文の共有owner。null不可
 * @param[in] request 受付ID設定済みの要求
 * @param[in] response 全文応答の共有owner。null不可
 * @throws OperationError 不正/不完はInvalidInput、容量超過はResultTooLarge
 * @note worker限定。原文と応答を所有し本文は応答へaliasします
 * @see koefude::ValidatedWholeTextCorrectionCompletion::ValidatedWholeTextCorrectionCompletion
 */
//------------------------------------------------------------------------------
ValidatedWholeTextCorrectionCompletion::ValidatedWholeTextCorrectionCompletion(
	std::shared_ptr<const Transcript> source, WholeTextCorrectionRequest request,
	std::shared_ptr<const WholeTextCorrectionResponse> response)
	: source_( std::move( source)), request_( std::move( request))
{
	validateWholeTextCorrectionRequest( source_, request_) ;
	requireWhole( response && response->operationId == request_.operationId &&
				 response->termination == WholeTextTermination::NATURAL_END) ;
	validateResult( *source_, request_, response->result) ;
	validateEvidence( *source_, request_, response->result) ;
	corrected_ = std::shared_ptr<const WholeTextCorrection>( response, &response->result) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 固定要求を参照します
 * @return ownerの寿命内で有効な要求
 * @see koefude::ValidatedWholeTextCorrectionCompletion::request
 */
//------------------------------------------------------------------------------
const WholeTextCorrectionRequest &ValidatedWholeTextCorrectionCompletion::request() const noexcept
{
	return request_ ;
}
//------------------------------------------------------------------------------
/**
 * @brief 原文ownerを参照します
 * @return 不変原文の共有所有値
 * @see koefude::ValidatedWholeTextCorrectionCompletion::source
 */
//------------------------------------------------------------------------------
const std::shared_ptr<const Transcript> &ValidatedWholeTextCorrectionCompletion::source() const noexcept
{
	return source_ ;
}
//------------------------------------------------------------------------------
/**
 * @brief 検証済み本文ownerを参照します
 * @return 応答へaliasする所有値
 * @see koefude::ValidatedWholeTextCorrectionCompletion::corrected
 */
//------------------------------------------------------------------------------
const std::shared_ptr<const WholeTextCorrection> &ValidatedWholeTextCorrectionCompletion::corrected() const noexcept
{
	return corrected_ ;
}

//------------------------------------------------------------------------------
/**
 * @brief 原文を検証して新要求を開始し、以前の全文を保持します
 * @param[in,out] record 直列化済みの試行状態。拒否/例外時は不変
 * @param[in] source 同じ履歴で保持する不変原文owner。null不可
 * @param[in] request 非0受付IDを持つ要求
 * @return 開始時true、進行中または本文なしならfalse
 * @throws OperationError 原文・方式・旧結果の束縛違反
 * @note hash走査を行うためworkerまたは非UI内部境界で使用します
 * @see koefude::beginWholeTextCorrection
 */
//------------------------------------------------------------------------------
bool beginWholeTextCorrection( WholeTextCorrectionRecord &record, std::shared_ptr<const Transcript> source,
							  const WholeTextCorrectionRequest &request)
{
	// 二重試行と本文なしは現在の状態を変更せず拒否する
	if( running( record.state) || !source || source->lines.empty()) {
		return false ;
	}
	validateWholeTextCorrectionRequest( source, request) ;
	// 一つのrecordへ別ownerや別履歴の旧結果を移植しない
	if( record.source) {
		requireWhole( sameOwner( record.source, source)) ;
	}
	// 以前の有効全文も同じ原文へ束縛したままにする
	if( record.corrected) {
		auto previous = request ; // 旧方式の結果も同原文なら新方式の試行中に保持できる
		previous.profileId = record.corrected->profileId ;
		previous.profileSha256 = record.corrected->profileSha256 ;
		validateWholeTextCorrectionRequest( source, previous) ;
		validateResult( *source, previous, *record.corrected) ;
	}
	record.active = request ; // allocationが成功してから状態を変更
	record.source = std::move( source) ;
	record.state = ReadingState::QUEUED ;
	record.reason = ReadingReason::NONE ;
	record.code = 0 ;
	return true ;
}

//------------------------------------------------------------------------------
/**
 * @brief 同じ要求の実進捗を反映します
 * @param[in,out] record 直列化済み状態
 * @param[in] request 固定要求
 * @param[in] progress 実処理の段階。取消し開始後は無視
 * @see koefude::updateWholeTextCorrection
 */
//------------------------------------------------------------------------------
void updateWholeTextCorrection( WholeTextCorrectionRecord &record, const WholeTextCorrectionRequest &request,
							   const Progress progress)
{
	// 古い進捗と取消し開始後の進捗で現在の試行を巻き戻さない
	if( !record.active || !sameRequest( *record.active, request) || record.state == ReadingState::CANCELLING) {
		return ;
	}
	record.phase = progress.wholeTextPhase ;
	record.stage = progress.stage ;
	record.state = progress.stage == Stage::LOADING_MODEL ? ReadingState::LOADING
				   : progress.stage == Stage::COMBINING	  ? ReadingState::VALIDATING
														  : ReadingState::FORMATTING ;
}

//------------------------------------------------------------------------------
/**
 * @brief 現要求だけを取消し待ちにします
 * @param[in,out] record 直列化済み状態
 * @see koefude::cancelWholeTextCorrection
 */
//------------------------------------------------------------------------------
void cancelWholeTextCorrection( WholeTextCorrectionRecord &record) noexcept
{
	// 旧有効全文を保持したまま現在の要求だけを取り消す
	if( record.active && running( record.state)) {
		record.state = ReadingState::CANCELLING ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 同じowner・要求の検証済み成功を反映し、失敗では旧全文を保持します
 * @param[in,out] record 直列化済み状態。stale時は不変
 * @param[in] source beginと同じ共有owner。nullならfalse
 * @param[in] request 受付時の固定要求
 * @param[in] completion worker検証済み成功。失敗時はnull
 * @param[in] error 元失敗分類。Cancelledは成功に優先
 * @param[in] code 本文を含まない元診断値
 * @return 同じ要求の終端を受領した場合true、stale時false
 * @see koefude::finishWholeTextCorrection
 */
//------------------------------------------------------------------------------
bool finishWholeTextCorrection( WholeTextCorrectionRecord &record, const std::shared_ptr<const Transcript> &source,
							   const WholeTextCorrectionRequest								&request,
							   std::shared_ptr<const ValidatedWholeTextCorrectionCompletion> completion,
							   const ErrorKind error, const std::uint32_t code)
{
	// 古い要求と原文owner違いは、診断値も含めてrecordを変更しない
	if( !sameOwner( record.source, source) || !record.active || !sameRequest( *record.active, request)) {
		return false ;
	}
	record.code = code ;
	// 取消し要求は遅い成功にも優先し、以前の有効全文を残す
	if( record.state == ReadingState::CANCELLING || error == ErrorKind::CANCELLED) {
		record.state = ReadingState::CANCELLED ;
		record.reason = ReadingReason::CANCELLED ;
	}
	// 同じsource ownerと固定要求を検証したcompletionだけを採用する
	else if( completion && sameOwner( source, completion->source()) && sameRequest( request, completion->request())) {
		record.corrected = completion->corrected() ;
		record.state = ReadingState::COMPLETED ;
		record.reason = ReadingReason::NONE ;
		record.code = 0 ;
	} else {
		record.state = ReadingState::FAILURE ;
		record.reason = failureReason( completion ? ErrorKind::INVALID_INPUT : error) ;
	}
	record.active.reset() ;
	record.phase = WholeTextPhase::NONE ;
	record.stage = Stage::VALIDATING ;
	return true ;
}

//------------------------------------------------------------------------------
/**
 * @brief 未終端をInterruptedへ移し、自動再生成せず旧ownerを保持します
 * @param[in,out] record 保存から復元した試行状態。active/phaseは常に消去
 * @see koefude::restoreWholeTextCorrection
 */
//------------------------------------------------------------------------------
void restoreWholeTextCorrection( WholeTextCorrectionRecord &record) noexcept
{
	// processを越えて要求を再開せず、以前の有効ownerを保持する
	if( running( record.state)) {
		record.state = ReadingState::INTERRUPTED ;
		record.reason = ReadingReason::INTERRUPTED ;
		record.code = 0 ;
	}
	record.active.reset() ;
	record.phase = WholeTextPhase::NONE ;
	record.stage = Stage::VALIDATING ;
}

//------------------------------------------------------------------------------
/**
 * @brief 束縛を歴史形式の全byteへ変換します
 * @param[in] binding 束縛値
 * @return 終端NUL込みdomainと既存順のbyte
 * @see koefude::encodeWholeTextInputBinding
 */
//------------------------------------------------------------------------------
std::vector<std::uint8_t> encodeWholeTextInputBinding( const WholeTextInputBinding &binding)
{
	requireWhole( !binding.request.profileId.empty() && binding.request.profileId.size() <= 64) ;
	std::vector<std::uint8_t> bytes ; // 可変profileを64byteまでに制限した小さい所有列
	const auto				  number =
		//------------------------------------------------------------------------------
		/**
		 * @brief 固定幅を追加します
		 * @param[in] value 固定fieldへ符号化する符号なし整数。下位byteからlittle endianで追加します
		 * @param[in] width 出力するbyte数。呼出側は整数型の幅以内の固定field幅を渡します
		 */
		//------------------------------------------------------------------------------
		[&]( const std::uint64_t value, const std::size_t width)
		{
			// little endianをhost表現へ依存させません
			for( std::size_t index = 0; index < width; ++index) { // byte位置
				bytes.push_back( static_cast<std::uint8_t>( value >> (index * 8))) ;
			}
		} ;
	const auto raw =
		//------------------------------------------------------------------------------
		/**
		 * @brief 配列全体を追加します
		 * @param[in] value 同期borrow
		 */
		//------------------------------------------------------------------------------
		[&]( const auto &value)
		{
			bytes.insert( bytes.end(), std::begin( value), std::end( value)) ;
		} ;
	constexpr char K_DOMAIN[] = "koefude.whole_text_native_input.v1" ; // 終端NUL込み
	raw( K_DOMAIN) ;
	number( binding.request.source.historyId, 8) ;
	raw( binding.request.source.sha256) ;
	number( binding.request.operationId, 8) ;
	number( binding.request.profileId.size(), 8) ;
	raw( binding.request.profileId) ;
	raw( binding.request.profileSha256) ;
	number( binding.originalBytes, 8) ;
	raw( binding.originalSha256) ;
	number( binding.auxiliaryBytes, 8) ;
	raw( binding.auxiliarySha256) ;
	number( binding.systemBytes, 8) ;
	raw( binding.systemSha256) ;
	number( binding.userBytes, 8) ;
	raw( binding.userSha256) ;
	return bytes ;
}
//------------------------------------------------------------------------------
/**
 * @brief canonical束縛のdigestを返します
 * @param[in] binding 束縛値
 * @param[in] stop 協調取消し
 * @return 全byte SHA
 * @see koefude::fingerprintWholeTextInputBinding
 */
//------------------------------------------------------------------------------
Digest fingerprintWholeTextInputBinding( const WholeTextInputBinding &binding, const std::stop_token stop)
{
	return sha256( encodeWholeTextInputBinding( binding), stop) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 保存したcanonical束縛を厳密に復元します
 * @param[in] bytes 最大512byteの全列
 * @return 全消費・再encode一致の束縛
 * @throws OperationError 不正な長さ/domain/field
 * @see koefude::decodeWholeTextInputBinding
 */
//------------------------------------------------------------------------------
WholeTextInputBinding decodeWholeTextInputBinding( const std::span<const std::uint8_t> bytes)
{
	requireWhole( bytes.size() <= 512) ;
	std::size_t position = 0 ; // 全消費を検査する位置
	const auto	number =
		//------------------------------------------------------------------------------
		/**
		 * @brief 固定幅を復元します
		 * @param[in] width byte幅
		 * @return 元整数
		 */
		//------------------------------------------------------------------------------
		[&]( const std::size_t width)
		{
			requireWhole( width <= bytes.size() - position) ;
			std::uint64_t value = 0 ; // unsigned bit列
			// 宣言幅の全byteを順番に復元します
			for( std::size_t index = 0; index < width; ++index) { // byte位置
				value |= static_cast<std::uint64_t>( bytes[position++]) << (index * 8) ;
			}
			return value ;
		} ;
	const auto digest =
		//------------------------------------------------------------------------------
		/**
		 * @brief SHAを復元します
		 * @param[out] value 32byte出力
		 */
		//------------------------------------------------------------------------------
		[&]( Digest &value)
		{
			requireWhole( value.size() <= bytes.size() - position) ;
			std::copy_n( bytes.begin() + position, value.size(), value.begin()) ;
			position += value.size() ;
		} ;
	constexpr char K_DOMAIN[] = "koefude.whole_text_native_input.v1" ; // 既存domain
	requireWhole( bytes.size() >= sizeof(K_DOMAIN) && std::memcmp( bytes.data(), K_DOMAIN, sizeof(K_DOMAIN)) == 0) ;
	position = sizeof(K_DOMAIN) ;
	WholeTextInputBinding result ; // 部分値は外へ返しません
	result.request.source.historyId = number( 8) ;
	digest( result.request.source.sha256) ;
	result.request.operationId = number( 8) ;
	const auto length = number( 8) ; // profileのbyte数
	requireWhole( length > 0 && length <= 64 && length <= bytes.size() - position) ;
	result.request.profileId.assign( reinterpret_cast<const char *>( bytes.data() + position),
									static_cast<std::size_t>( length)) ;
	position += static_cast<std::size_t>( length) ;
	digest( result.request.profileSha256) ;
	result.originalBytes = number( 8) ;
	digest( result.originalSha256) ;
	result.auxiliaryBytes = number( 8) ;
	digest( result.auxiliarySha256) ;
	result.systemBytes = number( 8) ;
	digest( result.systemSha256) ;
	result.userBytes = number( 8) ;
	digest( result.userSha256) ;
	const auto encoded = encodeWholeTextInputBinding( result) ; // canonical再照合
	requireWhole( position == bytes.size() && std::equal( bytes.begin(), bytes.end(), encoded.begin(), encoded.end())) ;
	return result ;
}
//------------------------------------------------------------------------------
/**
 * @brief 保存・export・表示workerで証拠付き全文を全検証します
 * @param[in] source 原文
 * @param[in] key 原文key
 * @param[in] corrected 候補
 * @param[in] stop 協調取消し
 * @throws OperationError 不正/容量超過
 * @see koefude::validatePersistedWholeTextCorrection
 */
//------------------------------------------------------------------------------
void validatePersistedWholeTextCorrection( const Transcript &source, const ReadingSourceKey &key,
										  const WholeTextCorrection &corrected, const std::stop_token stop)
{
	checkCancelled( stop) ;
	requireWhole( corrected.evidence.has_value()) ;
	const auto &request = corrected.evidence->binding.request ; // historical profile/opを維持
	requireWhole( request.operationId != 0 && request.source == key && key.historyId != 0 &&
				 key.sha256 == fingerprintReadingSource( source)) ;
	validateResult( source, request, corrected) ;
	validateEvidence( source, request, corrected) ;
	checkCancelled( stop) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 検証済み表示snapshotでop0のpendingを先に確保します
 * @param[in] record 元record、変更しません
 * @param[in] source 同じ不変owner
 * @param[in] view 検証済み表示
 * @param[in] request op0要求
 * @return 受付後IDだけを設定してnoexcept moveできるpending
 * @throws OperationError 進行中、owner/key不一致
 * @see koefude::prepareWholeTextCorrectionRecord
 */
//------------------------------------------------------------------------------
WholeTextCorrectionRecord prepareWholeTextCorrectionRecord( const WholeTextCorrectionRecord	&record,
														   std::shared_ptr<const Transcript> source,
														   const ReadingViewSnapshot		&view,
														   const WholeTextCorrectionRequest &request)
{
	requireWhole( !running( record.state) && source && !source->lines.empty() && source.get() == &view.source() &&
				 request.operationId == 0 && request.source == view.key() && source->pcmIdentity &&
				 (!record.source || sameOwner( record.source, source))) ;
	auto pending = record ; // submitより前に文字列/optionalを確保
	pending.source = std::move( source) ;
	pending.active = request ;
	pending.state = ReadingState::QUEUED ;
	pending.reason = ReadingReason::NONE ;
	pending.code = 0 ;
	pending.phase = WholeTextPhase::NONE ;
	return pending ;
}
//------------------------------------------------------------------------------
/**
 * @brief 存在する結果から表示意図を純粋に解決します
 * @param[in] preference 保存した意図
 * @param[in] whole 有効全文あり
 * @param[in] legacy 有効旧整形あり
 * @return 実際に表示する種類
 * @see koefude::resolveResultPreference
 */
//------------------------------------------------------------------------------
ResultPreference resolveResultPreference( const ResultPreference preference, const bool whole,
										 const bool legacy) noexcept
{
	// 明示Originalと存在しない専用選択は原文を表示します
	if( preference == ResultPreference::ORIGINAL) {
		return ResultPreference::ORIGINAL ;
	}
	// Automaticだけが有効全文を優先します
	if( whole && (preference == ResultPreference::AUTOMATIC || preference == ResultPreference::WHOLE_TEXT)) {
		return ResultPreference::WHOLE_TEXT ;
	}
	// 旧整形の明示/自動表示を保ちます
	if( legacy && (preference == ResultPreference::AUTOMATIC || preference == ResultPreference::LEGACY_FORMATTED)) {
		return ResultPreference::LEGACY_FORMATTED ;
	}
	return ResultPreference::ORIGINAL ;
}

//------------------------------------------------------------------------------
/**
 * @brief 新しいsource一件だけを待ちにします
 * @param[in] owner 原文owner、nullは待ちなし
 * @param[in] historyId 元カードID
 * @see koefude::AutomaticTextContextRequest::arm
 */
//------------------------------------------------------------------------------
void AutomaticTextContextRequest::arm( std::shared_ptr<const Transcript> owner, std::uint64_t historyId) noexcept
{
	source = std::move( owner) ;
	key = {historyId, {}} ;
	revision = 0 ;
	operationId = 0 ;
	saved = false ;
	waiting = source && !source->lines.empty() && historyId != 0 ;
}
//------------------------------------------------------------------------------
/**
 * @brief 集約先の保存revisionへbarrierを付け直します
 * @param[in] value 最新受付revision、0は未保存
 * @param[in] owner snapshotの原文owner
 * @see koefude::AutomaticTextContextRequest::saving
 */
//------------------------------------------------------------------------------
void AutomaticTextContextRequest::saving( std::uint64_t value, const std::shared_ptr<const Transcript> &owner) noexcept
{
	// 待ちsourceの差替えは旧待ちを閉じ、別ownerへ自動適用しません
	if( waiting && !sameOwner( source, owner)) {
		cancel() ;
	}
	// 最新snapshotの保存が終わるまでは以前のackを無効にします
	if( waiting) {
		revision = value ;
		saved = false ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 一致した耐久保存だけを受領します
 * @param[in] value Saved revision
 * @param[in] owner 保存した同owner
 * @param[in] sourceKey workerで照合したkey
 * @see koefude::AutomaticTextContextRequest::acknowledge
 */
//------------------------------------------------------------------------------
void AutomaticTextContextRequest::acknowledge( std::uint64_t value, const std::shared_ptr<const Transcript> &owner,
											  const ReadingSourceKey &sourceKey) noexcept
{
	// revision/owner/カードの全一致だけを耐久barrierとします
	if( waiting && value != 0 && value == revision && sameOwner( source, owner) && sourceKey.historyId == key.historyId &&
	   sourceKey.sha256 != decltype(key.sha256){}) {
		key = sourceKey ;
		saved = true ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 受付0なら待ちを残し非0だけ一回消費します
 * @param[in] id submit結果
 * @see koefude::AutomaticTextContextRequest::submitted
 */
//------------------------------------------------------------------------------
void AutomaticTextContextRequest::submitted( std::uint64_t id) noexcept
{
	// queue fullの0では有限待ちを消費しません
	if( ready() && id != 0) {
		operationId = id ;
		waiting = false ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 未受付の待ちだけを終端にし、受理済みoperationは保持します
 * @see koefude::AutomaticTextContextRequest::cancel
 */
//------------------------------------------------------------------------------
void AutomaticTextContextRequest::cancel() noexcept
{
	waiting = false ;
	saved = false ;
}
//------------------------------------------------------------------------------
/**
 * @brief barrier成立をI/Oなしで返します
 * @return 保存済み未受付ならtrue
 * @see koefude::AutomaticTextContextRequest::ready
 */
//------------------------------------------------------------------------------
bool AutomaticTextContextRequest::ready() const noexcept
{
	return waiting && saved && revision != 0 && operationId == 0 && source ;
}
//------------------------------------------------------------------------------
/**
 * @brief 保存workerのackに束縛した要求だけを表示viewなしで準備します
 * @param[in] record 前有効全文
 * @param[in] automatic 同owner/keyの保存barrier
 * @param[in] profile 新しい固定profile
 * @return 受付前op0の所有pending
 * @see koefude::prepareSavedTextContextRecord
 */
//------------------------------------------------------------------------------
WholeTextCorrectionRecord prepareSavedTextContextRecord( const WholeTextCorrectionRecord	  &record,
														const AutomaticTextContextRequest &automatic,
														const ReadingProfile			  &profile)
{
	requireWhole( automatic.ready() && !running( record.state) &&
				 (!record.source || sameOwner( record.source, automatic.source))) ;
	auto pending = record ; // 非0受付前に必要な小所有値だけを確保します
	pending.source = automatic.source ;
	pending.active = WholeTextCorrectionRequest{automatic.key, 0, profile.id, profile.sha256} ;
	pending.state = ReadingState::QUEUED ;
	pending.reason = ReadingReason::NONE ;
	pending.code = 0 ;
	pending.phase = WholeTextPhase::NONE ;
	return pending ;
}

} // namespace koefude
