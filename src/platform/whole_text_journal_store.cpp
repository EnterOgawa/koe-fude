//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_journal_store.cpp
 * @brief 同一fileへ封印資料と確定prefixだけを書きます
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1285の有界逐次補正を接続
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
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
#include "platform/whole_text_journal_store.h"
#include <span>
#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
#include <windows.h>

// Win32基本型の後にCNGの宣言を読み込みます
#include <bcrypt.h>
namespace koefude {
namespace {
constexpr std::uint64_t K_AUX_OFFSET = 65536 ; // 全aux開始
constexpr std::uint64_t K_PLAN_OFFSET = 8454144 ; // plan/receipt開始
constexpr std::uint64_t K_BODY_OFFSET = 16842752 ; // 本文開始
constexpr std::uint64_t K_SECTION_BYTES = 8388608 ; // 各section上限
//------------------------------------------------------------------------------
/**
 * @brief byte列へLE整数を書きます
 * @param[in,out] bytes 同期借用する書込buffer。成功時はoffsetからwidth byteだけ変更します
 * @param[in,out] offset 次の書込byte位置。成功時にwidthだけ進み、範囲拒否時は変更しません
 * @param[in] value 指定幅で下位byteから書く符号なし整数
 * @param[in] width 書込byte数。呼出側は1～8の固定field幅を指定します
 */
//------------------------------------------------------------------------------
void					put( std::span<std::uint8_t> bytes, std::size_t &offset, std::uint64_t value, unsigned width)
{
	requireWholeTextBounded( offset <= bytes.size() && width <= bytes.size() - offset,
							WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
	// 宣言幅の全byteを固定順に書きます
	for( unsigned index = 0; index < width; ++index) {
		bytes[offset++] = static_cast<std::uint8_t>( value >> (index * 8)) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 全byteをそのまま追加します
 * @param[in,out] bytes buffer
 * @param[in,out] offset 次位置
 * @param[in] value 全byte
 */
//------------------------------------------------------------------------------
void raw( std::span<std::uint8_t> bytes, std::size_t &offset, std::span<const std::uint8_t> value)
{
	requireWholeTextBounded( offset <= bytes.size() && value.size() <= bytes.size() - offset,
							WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
	std::copy( value.begin(), value.end(), bytes.begin() + static_cast<std::ptrdiff_t>( offset)) ;
	offset += value.size() ;
}
//------------------------------------------------------------------------------
/**
 * @brief UTF8を再encodeせずbyte借用にします
 * @param[in] s 全text
 * @return 同期byte借用
 */
//------------------------------------------------------------------------------
std::span<const std::uint8_t> textBytes( std::string_view s)
{
	return {reinterpret_cast<const std::uint8_t *>( s.data()), s.size()} ; // owner寿命内だけ
}
//------------------------------------------------------------------------------
/**
 * @brief 有界buffer内の固定幅little endian整数をuint64_tへ復元します
 * @param[in] bytes 呼出し中だけ借用する読取り専用buffer
 * @param[in] offset 読取り先頭の0始まりbyte位置
 * @param[in] width 読むbyte数。呼出側が1～8とoffset+widthの範囲内を先に確認します
 * @return 下位byteから復元した符号なし整数。bufferを変更せず、所有権も取得しません
 */
//------------------------------------------------------------------------------
std::uint64_t get( std::span<const std::uint8_t> bytes, std::size_t offset, unsigned width)
{
	std::uint64_t value = 0 ; // 復号する値
	// 固定field幅だけを読みます
	for( unsigned i = 0; i < width; ++i) {
		value |= static_cast<std::uint64_t>( bytes[offset + i]) << (i * 8) ;
	}
	return value ;
}
//------------------------------------------------------------------------------
/**
 * @brief OS hash codeを元のまま保持します
 * @param[in] status BCrypt元code
 */
//------------------------------------------------------------------------------
void hashOk( NTSTATUS status)
{
	// 不整合や終了状態を分岐させ、未確定のまま後続へ進みません
	if( status < 0) {
		throw OperationError( ErrorKind::TEMPORARY_STORAGE, static_cast<std::uint32_t>( status)) ;
	}
}
/** @brief algorithm/hash/object bufferを生成前に確保するstream SHAです */
class JournalHash final {
	public:
	/// OS algorithm owner
	BCRYPT_ALG_HANDLE		  algorithm = nullptr ;
	/// 累計hash
	BCRYPT_HASH_HANDLE		  hash = nullptr ;
	/// 生成前に確保したhash state
	std::vector<std::uint8_t> object ;
	/// finalize用の固定state
	std::vector<std::uint8_t> duplicateObject ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定SHA256の必要領域を確保します
	 */
	//------------------------------------------------------------------------------
	JournalHash()
	{
		try {
			hashOk( BCryptOpenAlgorithmProvider( &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) ;
			DWORD bytes = 0 ; // この同期処理で使う所有値または検証値
			DWORD copied = 0 ; // OS必要byteと返却byte
			hashOk( BCryptGetProperty( algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>( &bytes), sizeof(bytes),
									 &copied,
									 0)) ; // OS scalarだけの同期境界
			requireWholeTextBounded( bytes > 0 && bytes <= 65536 && copied == sizeof(bytes),
									WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
			object.resize( bytes) ;
			duplicateObject.resize( bytes) ;
			reset() ;
		} catch( ...) {
			// constructor途中でも取得済みのOS資源だけを一回解放します
			if( hash) {
				BCryptDestroyHash( hash) ;
				hash = nullptr ;
			}
			// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
			if( algorithm) {
				BCryptCloseAlgorithmProvider( algorithm, 0) ;
				algorithm = nullptr ;
			}
			throw ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief hash/algorithmを逆順に一回解放します
	 */
	//------------------------------------------------------------------------------
	~JournalHash()
	{
		// 所有と解放確認の状態に従い、未確認の資源を再解放しません
		if( hash) {
			BCryptDestroyHash( hash) ;
		}
		// 所有と解放確認の状態に従い、未確認の資源を再解放しません
		if( algorithm) {
			BCryptCloseAlgorithmProvider( algorithm, 0) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ事前確保objectでstreamを初期化します
	 */
	//------------------------------------------------------------------------------
	void reset()
	{
		// 所有と解放確認の状態に従い、未確認の資源を再解放しません
		if( hash) {
			BCryptDestroyHash( hash) ;
			hash = nullptr ;
		}
		hashOk( BCryptCreateHash( algorithm, &hash, object.data(), static_cast<ULONG>( object.size()), nullptr, 0, 0)) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 最大64KiBのbyteを追加します
	 * @param[in] bytes 同期borrow
	 */
	//------------------------------------------------------------------------------
	void append( std::span<const std::uint8_t> bytes)
	{
		requireWholeTextBounded( bytes.size() <= 65536, WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
		hashOk( BCryptHashData( hash, const_cast<PUCHAR>( bytes.data()), static_cast<ULONG>( bytes.size()), 0)) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 元streamを変えず候補byte追加後のSHAを返します
	 * @param[in] bytes 一chunk
	 * @return 全prefix SHA
	 */
	//------------------------------------------------------------------------------
	WholeTextDigest digest( std::span<const std::uint8_t> bytes = {}) const
	{
		BCRYPT_HASH_HANDLE copy = nullptr ; // 一回だけdestroyする複製hash
		auto			  &storage = const_cast<std::vector<std::uint8_t> &>( duplicateObject) ; // 同期worker専用
		hashOk( BCryptDuplicateHash( hash, &copy, storage.data(), static_cast<ULONG>( storage.size()), 0)) ;
		try {
			hashOk( BCryptHashData( copy, const_cast<PUCHAR>( bytes.data()), static_cast<ULONG>( bytes.size()), 0)) ;
			WholeTextDigest result{} ; // 固定値
			hashOk( BCryptFinishHash( copy, result.data(), static_cast<ULONG>( result.size()), 0)) ;
			BCryptDestroyHash( copy) ;
			return result ;
		} catch( ...) {
			BCryptDestroyHash( copy) ;
			throw ;
		}
	}
} ;
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief planと固定192B成功slotが同じ8MiBに収まるか検査します
 * @param[in] bytes 旧preimage byte
 * @param[in] records 全件数
 * @param[in] capacity 小さい試験だけ縮めるsection上限
 * @return checked適合
 * @see koefude::wholeTextJournalPlanFits
 */
//------------------------------------------------------------------------------
bool wholeTextJournalPlanFits( std::uint64_t bytes, std::uint64_t records, std::uint64_t capacity) noexcept
{
	return capacity > 0 && capacity <= 8388608 && records > 0 && records <= 16384 && records <= capacity / 192 &&
		   bytes <= capacity - records * 192 ;
}
//------------------------------------------------------------------------------
/**
 * @brief 64MiB/32件/512MiB/本文16MiBをchecked算術で予約します
 * @param[in] root 同volume history root。空は既定
 * @param[in] ledger U4が既存全量で初期化して保持する台帳
 * @param[in] usage 全使用量/上界
 * @see koefude::WholeTextBoundedStorageBudget::WholeTextBoundedStorageBudget(std::filesystem::path, std::shared_ptr< koefude::TransferableJournalQuota >, koefude::WholeTextBoundedStorageUsage)
 */
//------------------------------------------------------------------------------
WholeTextBoundedStorageBudget::WholeTextBoundedStorageBudget( std::filesystem::path					   root,
															 std::shared_ptr<TransferableJournalQuota> ledger,
															 WholeTextBoundedStorageUsage			   usage)
	: root_( std::move( root))
{
	const auto capacity = WholeTextBoundedDiagnostic::JOURNAL_CAPACITY ; // 本文なし分類
	requireWholeTextBounded( ledger && usage.metadataUpperBytes && usage.previousJournals <= 1 &&
								usage.previousJournalBytes <= K_WHOLE_TEXT_JOURNAL_BYTES &&
								(usage.previousJournals != 0 || usage.previousJournalBytes == 0) &&
								usage.retainedTextBytes <= 16777216,
							capacity, ErrorKind::RESULT_TOO_LARGE) ;
	std::uint64_t remaining = 67108864 - K_WHOLE_TEXT_JOURNAL_BYTES ; // 操作64MiB
	// 全旧payload/metadata/tempと新metadata上界を一つの予算へ計上します
	for( const auto amount : {usage.previousJournalBytes, usage.operationOtherBytes, *usage.metadataUpperBytes}) {
		requireWholeTextBounded( amount <= remaining, capacity, ErrorKind::RESULT_TOO_LARGE) ;
		remaining -= amount ;
	}
	retainedTextBytes_ = usage.retainedTextBytes ;
	bodyCapacity_ = std::min<std::uint64_t>( 8388608, 16777216 - usage.retainedTextBytes) ;
	requireWholeTextBounded( bodyCapacity_ > 0, capacity, ErrorKind::RESULT_TOO_LARGE) ;
	charge_ = std::make_shared<TransferableJournalCharge>() ; // native/生成前に確保
	charge_->ledger = std::move( ledger) ;
	charge_->bytes = K_WHOLE_TEXT_JOURNAL_BYTES + *usage.metadataUpperBytes ;
	charge_->entryDelta = usage.historyEntryDelta ;
	const std::lock_guard lock( charge_->ledger->mutex) ; // I/Oはlock外
	requireWholeTextBounded(
		charge_->ledger->bytes >= usage.previousJournalBytes && charge_->ledger->entries <= 32 &&
			charge_->entryDelta <= 32 - charge_->ledger->entries && charge_->ledger->bytes <= 536870912 &&
			K_WHOLE_TEXT_JOURNAL_BYTES <= 536870912 - charge_->ledger->bytes &&
			*usage.metadataUpperBytes <= 536870912 - charge_->ledger->bytes - K_WHOLE_TEXT_JOURNAL_BYTES,
		capacity, ErrorKind::RESULT_TOO_LARGE) ;
	charge_->ledger->bytes += charge_->bytes ;
	charge_->ledger->entries += charge_->entryDelta ;
	charge_->active = true ;
}
//------------------------------------------------------------------------------
/**
 * @brief fileへ渡していない予約だけを返します
 * @see koefude::WholeTextBoundedStorageBudget::~WholeTextBoundedStorageBudget
 */
//------------------------------------------------------------------------------
WholeTextBoundedStorageBudget::~WholeTextBoundedStorageBudget()
{
	// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
	if( charge_) {
		charge_->release() ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 単独予約を移します
 * @param[in,out] other 元owner
 * @see koefude::WholeTextBoundedStorageBudget::WholeTextBoundedStorageBudget(koefude::WholeTextBoundedStorageBudget &&)
 */
//------------------------------------------------------------------------------
WholeTextBoundedStorageBudget::WholeTextBoundedStorageBudget(
	// この同期境界で所有する値、または確定状態の検証値です
	WholeTextBoundedStorageBudget &&other) noexcept = default ;
//------------------------------------------------------------------------------
/**
 * @brief 現予約を返して移します
 * @param[in,out] other 元owner
 * @return 自身
 * @see koefude::WholeTextBoundedStorageBudget::operator=(koefude::WholeTextBoundedStorageBudget &&)
 */
//------------------------------------------------------------------------------
WholeTextBoundedStorageBudget &
// この同期境界で所有する値、または確定状態の検証値です
WholeTextBoundedStorageBudget::operator=( WholeTextBoundedStorageBudget &&other) noexcept
{
	// 所有と解放確認の状態に従い、未確認の資源を再解放しません
	if( this != &other) {
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( charge_) {
			charge_->release() ;
		}
		checkpoint_ = std::move( other.checkpoint_) ;
		root_ = std::move( other.root_) ;
		charge_ = std::move( other.charge_) ;
		bodyCapacity_ = other.bodyCapacity_ ;
		retainedTextBytes_ = other.retainedTextBytes_ ;
	}
	return *this ;
}
//------------------------------------------------------------------------------
/**
 * @brief 一回だけfileへ予約責任を移します
 * @return 単独fileと共有する事前確保charge
 * @see koefude::WholeTextBoundedStorageBudget::takeCharge
 */
//------------------------------------------------------------------------------
std::shared_ptr<TransferableJournalCharge> WholeTextBoundedStorageBudget::takeCharge() noexcept
{
	return std::move( charge_) ;
}
/** @brief 一fileと固定hash/read領域を所有します */
class WholeTextBoundedJournal::Impl final {
	public:
	/// 同じ4KiB slot
	std::shared_ptr<IWholeTextJournalCheckpoint> checkpoint ;
	/// 同じ一payload
	TransferableJournalFile						 file ;
	/// 確定prefix累計
	JournalHash									 hash ;
	/// 再読照合、生成前確保
	JournalHash									 readerHash ;
	/// 部分readだけ
	mutable std::array<std::uint8_t, 65536>		 buffer{} ;
	/// 借用を含まない人工小値
	std::shared_ptr<WholeTextJournalFault>		 fault ;
	/// header値
	WholeTextJournalBinding						 binding ;
	/// flush済みだけ
	WholeTextBoundedCursor						 cursor ;
	/// 本文残量制約
	std::uint64_t								 capacity ;
	/// plan直後
	std::uint64_t								 receiptOffset = 0 ;
	/// 封印section実byte
	std::uint64_t								 auxiliaryBytes = 0 ;
	/// 同じpreimage実byte
	std::uint64_t								 encodedPlanBytes = 0 ;
	/// SHAを含むpayload長
	std::uint32_t								 headerBytes = 0 ;
	/// 封印時の不変header SHA
	WholeTextDigest								 headerSha256{} ;
	/// 不変header成立
	bool										 sealed = false ;
	/// I/O再試行なし
	bool										 failed = false ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 全領域を生成前に確保します
	 * @param[in] budget 消費する予約
	 * @param[in] injection 人工小値
	 */
	//------------------------------------------------------------------------------
	Impl( WholeTextBoundedStorageBudget budget, std::shared_ptr<WholeTextJournalFault> injection)
		: checkpoint( budget.takeCheckpoint()), file( budget.root(), K_WHOLE_TEXT_JOURNAL_BYTES, budget.takeCharge()),
		  fault( std::move( injection)), capacity( budget.bodyCapacity())
	{
		cursor.bodySha256 = hash.digest() ;
		// headerを書く前にfile identityを耐久化します
		if( checkpoint) {
			checkpoint->created( file) ;
			file.retainDurable() ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief known fileの読取りownerを作ります
	 * @param[in] owned 同じfile
	 */
	//------------------------------------------------------------------------------
	explicit Impl( TransferableJournalFile owned) : file( std::move( owned)), capacity( 8388608)
	{
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 指定した一地点だけを元codeで失敗させます
	 * @param[in] stage 固定段階
	 * @param[in] index 元record順
	 */
	//------------------------------------------------------------------------------
	void inject( std::uint32_t stage, std::uint32_t index) const
	{
		// 不整合や終了状態を分岐させ、未確定のまま後続へ進みません
		if( fault && fault->stage == stage && fault->index == index) {
			throw OperationError( ErrorKind::TEMPORARY_STORAGE, fault->code) ;
		}
	}
} ;
//------------------------------------------------------------------------------
/**
 * @brief 生成前に一fileとstream hash/read領域を物理予約します
 * @param[in] budget 消費するquota
 * @param[in] fault 小さい共有人工失敗、通常null
 * @see koefude::WholeTextBoundedJournal::WholeTextBoundedJournal(koefude::WholeTextBoundedStorageBudget, std::shared_ptr< koefude::WholeTextJournalFault >)
 */
//------------------------------------------------------------------------------
WholeTextBoundedJournal::WholeTextBoundedJournal( WholeTextBoundedStorageBudget			budget,
												 std::shared_ptr<WholeTextJournalFault> fault)
	: impl_( std::make_unique<Impl>( std::move( budget), std::move( fault)))
{
}
//------------------------------------------------------------------------------
/**
 * @brief 未引渡しfileだけを通常cleanupします
 * @see koefude::WholeTextBoundedJournal::~WholeTextBoundedJournal
 */
//------------------------------------------------------------------------------
WholeTextBoundedJournal::~WholeTextBoundedJournal() = default ;
//------------------------------------------------------------------------------
/**
 * @brief 同じ一fileを移します
 * @param[in,out] other 元owner
 * @see koefude::WholeTextBoundedJournal::WholeTextBoundedJournal(koefude::WholeTextBoundedJournal &&)
 */
//------------------------------------------------------------------------------
WholeTextBoundedJournal::WholeTextBoundedJournal( WholeTextBoundedJournal &&other) noexcept = default ;
//------------------------------------------------------------------------------
/**
 * @brief 旧ownerをcleanupして移します
 * @param[in,out] other 元owner
 * @return 自身
 * @see koefude::WholeTextBoundedJournal::operator=(koefude::WholeTextBoundedJournal &&)
 */
//------------------------------------------------------------------------------
WholeTextBoundedJournal &
// この同期境界で所有する値、または確定状態の検証値です
	 WholeTextBoundedJournal::operator=( WholeTextBoundedJournal &&other) noexcept = default ;
//------------------------------------------------------------------------------
/**
 * @brief 全aux/同一plan/headerを一回write/flushし以後不変にします
 * @param[in] b 全値
 * @param[in] auxiliary 全lossless byte
 * @param[in] plan sealed plan
 * @see koefude::WholeTextBoundedJournal::seal
 */
//------------------------------------------------------------------------------
void WholeTextBoundedJournal::seal( const WholeTextJournalBinding &b, std::span<const std::uint8_t> auxiliary,
								   const WholeTextChunkPlan &plan)
{
	auto &s = *impl_ ; // 同じ一file
	requireWholeTextBounded( !s.sealed && !s.failed && auxiliary.size() <= K_SECTION_BYTES && !plan.chunks.empty() &&
								plan.chunks.size() <= 16384 && b.promotion.recordCount == plan.chunks.size() &&
								b.promotion.planSha256 == plan.planSha256 && b.originalSha256 == plan.originalSha256 &&
								wholeTextSha256( auxiliary) == plan.auxiliarySha256,
							WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
	const auto encoding = detail::wholeTextPlanEncoding( plan) ; // 同じpreimage一つだけ
	requireWholeTextBounded( wholeTextSha256( encoding) == plan.planSha256 &&
								wholeTextJournalPlanFits( encoding.size(), plan.chunks.size()),
							WholeTextBoundedDiagnostic::JOURNAL_CAPACITY, ErrorKind::RESULT_TOO_LARGE) ;
	s.receiptOffset = K_PLAN_OFFSET + encoding.size() ;
	static_cast<void>( wholeTextGenerationKind( b.request.profileId,
											  b.request.profileSha256)) ; // 保存前の方式照合
	s.binding = b ; // 小さいprofile文字列も生成前に所有
	auto &header = s.buffer ;
	header.fill( 0) ; // immutable headerの全末尾も0
	std::size_t	   n = 0 ; // 次のheader byte
	constexpr char HASH_DOMAIN[] = "koefude.whole_text_bounded_journal.v2" ; // NUL込み
	raw( header, n, {reinterpret_cast<const std::uint8_t *>( HASH_DOMAIN), sizeof(HASH_DOMAIN)}) ;
	put( header, n, 2, 4) ;
	const auto payloadOffset = n ; // この同期処理で使う所有値または検証値
	put( header, n, 0, 4) ; // payload長を後で固定
	put( header, n, b.request.source.historyId, 8) ;
	raw( header, n, b.request.source.sha256) ;
	put( header, n, b.request.operationId, 8) ;
	requireWholeTextBounded( !b.request.profileId.empty() && b.request.profileId.size() <= 64 &&
								std::all_of( b.request.profileId.begin(), b.request.profileId.end(),
		//------------------------------------------------------------------------------
		/**
		 * @brief profileのASCIIを検査します
		 * @param[in] c byte
		 * @return 正ならtrue
		 */
		//------------------------------------------------------------------------------
		[]( unsigned char c)
		{
			return c > 0 && c < 128 ;
		}),
							WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
	put( header, n, b.request.profileId.size(), 4) ;
	raw( header, n, textBytes( b.request.profileId)) ;
	raw( header, n, b.request.profileSha256) ;
	put( header, n, b.pcm.version, 4) ;
	put( header, n, b.pcm.sampleCount, 8) ;
	put( header, n, b.pcm.sampleRate, 4) ;
	put( header, n, b.pcm.channels, 4) ;
	put( header, n, b.pcm.originSample, 8) ;
	raw( header, n, b.pcm.pcmSha256) ;
	put( header, n, b.promotion.originalBytes, 8) ;
	raw( header, n, b.originalSha256) ;
	put( header, n, auxiliary.size(), 8) ;
	raw( header, n, plan.auxiliarySha256) ;
	put( header, n, encoding.size(), 8) ;
	raw( header, n, plan.planSha256) ;
	raw( header, n, b.promotion.openBinding) ;
	raw( header, n, b.promotion.inputSha256) ;
	put( header, n, b.promotion.recordCount, 4) ;
	put( header, n, b.promotion.generationCount, 4) ;
	put( header, n, 192, 4) ;
	put( header, n, s.receiptOffset, 8) ;
	put( header, n, s.capacity, 8) ;
	// section配置は固定し、可変planでbodyの位置を変えません
	for( const auto v :
		{65536ULL, K_AUX_OFFSET, K_SECTION_BYTES, K_PLAN_OFFSET, K_SECTION_BYTES, K_BODY_OFFSET, K_SECTION_BYTES}) {
		put( header, n, v, 8) ;
	}
	auto payloadCursor = payloadOffset ; // この同期処理で使う所有値または検証値
	put( header, payloadCursor, n + 32, 4) ; // SHAを含むpayload長
	const auto headerSha = wholeTextSha256( std::span( header).first( n)) ; // 全先行field
	raw( header, n, headerSha) ;
	s.headerSha256 = headerSha ;
	s.auxiliaryBytes = auxiliary.size() ;
	s.encodedPlanBytes = encoding.size() ;
	s.headerBytes = static_cast<std::uint32_t>( n) ;
	s.file.write( 0, header) ;
	s.file.write( K_AUX_OFFSET, auxiliary) ;
	s.file.write( K_PLAN_OFFSET, textBytes( encoding)) ;
	s.file.flush() ;
	// 不変headerのflush後にだけcanonical checkpointを確定します
	if( s.checkpoint) {
		s.checkpoint->sealed( b, s.headerSha256, s.cursor) ;
	}
	s.sealed = true ; // generation前だけの不可逆封印
}
//------------------------------------------------------------------------------
/**
 * @brief body→receipt→flushの後だけcursorを進めます
 * @param[in] r 次の封印record
 * @param[in] body 一coreだけ
 * @param[in] generated 生成成功receipt。空白copyではnull
 * @param[in] stop 追記確定前の取消し
 * @see koefude::WholeTextBoundedJournal::append
 */
//------------------------------------------------------------------------------
void WholeTextBoundedJournal::append( const WholeTextChunkRecord &r, std::string_view body,
									 const WholeTextBoundedChunkReceipt *generated, std::stop_token stop)
{
	auto &s = *impl_ ; // 同じ予約file
	requireWholeTextBounded( s.sealed && !s.failed && r.index == s.cursor.count &&
								r.index < s.binding.promotion.recordCount && r.core.begin == s.cursor.coreEnd &&
								r.core.begin < r.core.end && r.core.end <= s.binding.promotion.originalBytes &&
								!body.empty() && body.size() <= 65536 && body.size() <= s.capacity - s.cursor.bodyBytes,
							WholeTextBoundedDiagnostic::AGGREGATE_BYTES, ErrorKind::RESULT_TOO_LARGE) ;
	checkCancelled( stop) ;
	std::array<std::uint8_t, 192> receipt{} ; // 固定成功slot
	std::size_t					  n = 0 ; // encoding位置
	const auto					  bodySha = wholeTextSha256( body) ; // current一coreだけ
	WholeTextDigest				  countSha{} ; // この同期処理で使う所有値または検証値
	WholeTextDigest				  evidence{} ; // copyは計数非該当
	// 生成だけは全成功receiptを検証し、空白は無編集copy証拠にします
	if( generated) {
		requireWholeTextBounded( !r.verbatimWhitespace && r.receipt && generated->index == r.index &&
									generated->core == r.core && generated->countSha256 == r.receipt->receiptSha256 &&
									generated->planSha256 == s.binding.promotion.planSha256 &&
									generated->inputSha256 == s.binding.promotion.inputSha256,
								WholeTextBoundedDiagnostic::CHUNK_RECEIPT) ;
		validateWholeTextGeneratedReceipt(
			*generated, body, wholeTextGenerationKind( s.binding.request.profileId, s.binding.request.profileSha256)) ;
		countSha = generated->countSha256 ;
		evidence = generated->receiptSha256 ;
	} else {
		requireWholeTextBounded( r.verbatimWhitespace && !r.receipt && body.size() == r.core.end - r.core.begin,
								WholeTextBoundedDiagnostic::CHUNK_RECEIPT) ;
		evidence = wholeTextVerbatimReceiptDigest( s.binding.promotion.inputSha256, s.binding.promotion.planSha256,
												  r.index, r.core, body) ;
	}
	put( receipt, n, 2, 4) ;
	put( receipt, n, generated ? 1 : 2, 4) ;
	put( receipt, n, r.index, 4) ;
	put( receipt, n, generated ? 1 : 0, 4) ;
	put( receipt, n, r.core.begin, 8) ;
	put( receipt, n, r.core.end, 8) ;
	put( receipt, n, s.cursor.bodyBytes, 8) ;
	put( receipt, n, s.cursor.bodyBytes + body.size(), 8) ;
	raw( receipt, n, countSha) ;
	raw( receipt, n, bodySha) ;
	raw( receipt, n, evidence) ;
	put( receipt, n, generated ? generated->prefixTokens : 0, 4) ;
	put( receipt, n, generated ? generated->analysisTokens : 0, 4) ;
	put( receipt, n, generated ? generated->closingTokens : 0, 4) ;
	put( receipt, n, generated ? generated->finalTokens : 0, 4) ;
	constexpr char HASH_DOMAIN[] = "koefude.whole_text_journal_record.v2" ; // 固定record SHA domain
	std::array<std::uint8_t, sizeof(HASH_DOMAIN) + 160> preimage{} ; // 小さい固定hash
	std::memcpy( preimage.data(), HASH_DOMAIN, sizeof(HASH_DOMAIN)) ;
	std::copy_n( receipt.begin(), 160, preimage.begin() + sizeof(HASH_DOMAIN)) ;
	raw( receipt, n, wholeTextSha256( preimage)) ;
	const auto aggregate = s.hash.digest( textBytes( body)) ; // 旧hash不変の候補digest
	try {
		s.file.write( K_BODY_OFFSET + s.cursor.bodyBytes, textBytes( body)) ;
		s.inject( 1, r.index) ;
		s.file.write( s.receiptOffset + 192 * r.index, receipt) ;
		s.inject( 2, r.index) ;
		checkCancelled( stop) ;
		s.inject( 3, r.index) ;
		s.file.flush() ;
		checkCancelled( stop) ;
		s.hash.append( textBytes( body)) ; // hash失敗をcheckpoint確定より前へ置きます
		const WholeTextBoundedCursor candidate{s.cursor.count + 1, r.core.end, s.cursor.bodyBytes + body.size(),
											   aggregate} ; // 小さい候補
		// checkpointの原子置換成功がcursorのcommit点です
		if( s.checkpoint) {
			s.checkpoint->commit( candidate) ;
		}
		s.cursor = candidate ; // 直後のnoexcept代入。取消し/観測はこの後です
		s.inject( 5, r.index) ; // 内部試験の後発observer失敗でもcursorを戻しません
	} catch( ...) {
		s.failed = true ;
		throw ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 確定prefix内だけを部分readします
 * @param[in] offset 本文byte位置
 * @param[out] bytes 最大65536byte
 * @see koefude::WholeTextBoundedJournal::readPrefix
 */
//------------------------------------------------------------------------------
void WholeTextBoundedJournal::readPrefix( std::uint64_t offset, std::span<std::uint8_t> bytes) const
{
	const auto &s = *impl_ ; // 確定cursorだけを参照
	requireWholeTextBounded( bytes.size() <= 65536 && offset <= s.cursor.bodyBytes &&
								bytes.size() <= s.cursor.bodyBytes - offset,
							WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
	s.inject( 4, s.cursor.count) ;
	s.file.read( K_BODY_OFFSET + offset, bytes) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 全確定receiptとextentをstream照合します
 * @param[in] plan 同じ封印計画
 * @see koefude::WholeTextBoundedJournal::validatePrefix
 */
//------------------------------------------------------------------------------
void WholeTextBoundedJournal::validatePrefix( const WholeTextChunkPlan &plan) const
{
	auto &s = *impl_ ; // 同期worker専用のstream reader
	requireWholeTextBounded( s.sealed && plan.planSha256 == s.binding.promotion.planSha256 &&
								s.cursor.count <= plan.chunks.size(),
							WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
	s.file.read( 0, s.buffer) ; // 固定不変headerを先に検査
	// この同期境界で所有する値、または確定状態の検証値です
	const auto headerSha = wholeTextSha256( std::span( s.buffer).first( s.headerBytes - 32)) ; // 全先行field
	requireWholeTextBounded( s.headerBytes >= 32 && s.headerBytes <= s.buffer.size() && headerSha == s.headerSha256 &&
								std::equal( headerSha.begin(), headerSha.end(), s.buffer.begin() + s.headerBytes - 32) &&
								std::all_of( s.buffer.begin() + s.headerBytes, s.buffer.end(),
		//------------------------------------------------------------------------------
		/**
		 * @brief 不変header末尾を確認します
		 * @param[in] byte 元byte
		 * @return 0ならtrue
		 */
		//------------------------------------------------------------------------------
		[]( auto byte)
		{
			return byte == 0 ;
		}),
							WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
	// auxとplanを全量stream照合し、保存後の長さ/hash破損を検出します
	for( const auto section : std::array<std::array<std::uint64_t, 2>, 2>{
			{{K_AUX_OFFSET, s.auxiliaryBytes}, {K_PLAN_OFFSET, s.encodedPlanBytes}}}) {
		s.readerHash.reset() ;
		// 固定範囲の元順を維持し、終端までの対象だけを処理します
		for( std::uint64_t offset = 0; offset < section[1];) { // section内の次block
			// この同期境界で所有する値、または確定状態の検証値です
			const auto bytes = static_cast<std::size_t>( std::min<std::uint64_t>( s.buffer.size(), section[1] - offset)) ;
			s.file.read( section[0] + offset, std::span( s.buffer).first( bytes)) ;
			s.readerHash.append( std::span( s.buffer).first( bytes)) ;
			offset += bytes ;
		}
		requireWholeTextBounded( s.readerHash.digest() ==
									(section[0] == K_AUX_OFFSET ? plan.auxiliarySha256 : plan.planSha256),
								WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
	}
	s.readerHash.reset() ;
	std::uint64_t bodyEnd = 0 ; // この同期処理で使う所有値または検証値
	std::uint64_t coreEnd = 0 ; // 検証中の連続終端
	// 確定済みreceiptだけを読み、未使用tailを成功へ混ぜません
	for( std::uint32_t i = 0; i < s.cursor.count; ++i) { // 元record順
		std::array<std::uint8_t, 192> r{} ; // この同期処理で使う所有値または検証値
		s.file.read( s.receiptOffset + 192 * i, r) ; // 一slot
		constexpr char HASH_DOMAIN[] = "koefude.whole_text_journal_record.v2" ; // 同じdomain
		std::array<std::uint8_t, sizeof(HASH_DOMAIN) + 160> preimage{} ; // この同期処理で使う所有値または検証値
		std::memcpy( preimage.data(), HASH_DOMAIN, sizeof(HASH_DOMAIN)) ;
		std::copy_n( r.begin(), 160, preimage.begin() + sizeof(HASH_DOMAIN)) ;
		const auto digest = wholeTextSha256( preimage) ; // この同期処理で使う所有値または検証値
		requireWholeTextBounded( get( r, 0, 4) == 2 && get( r, 8, 4) == i && get( r, 16, 8) == coreEnd &&
									get( r, 24, 8) == plan.chunks[i].core.end && get( r, 32, 8) == bodyEnd &&
									get( r, 40, 8) > bodyEnd && get( r, 40, 8) <= s.cursor.bodyBytes &&
									std::equal( digest.begin(), digest.end(), r.begin() + 160),
								WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
		const auto &planned = plan.chunks[i] ; // 旧sealed recordの値
		const auto	end = get( r, 40, 8) ; // current本文終端
		requireWholeTextBounded( get( r, 4, 4) == (planned.verbatimWhitespace ? 2u : 1u) &&
									get( r, 12, 4) == (planned.verbatimWhitespace ? 0u : 1u) && end - bodyEnd <= 65536 &&
									get( r, 16, 8) == planned.core.begin,
								WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
		const auto size = static_cast<std::size_t>( end - bodyEnd) ; // 一coreだけ
		s.file.read( K_BODY_OFFSET + bodyEnd, std::span( s.buffer).first( size)) ;
		const std::string_view body( reinterpret_cast<const char *>( s.buffer.data()),
									size) ; // 同期borrow
		const auto			   bodySha = wholeTextSha256( body) ; // current extentの全SHA
		requireWholeTextBounded( std::equal( bodySha.begin(), bodySha.end(), r.begin() + 80),
								WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
		WholeTextDigest countSha{} ; // この同期処理で使う所有値または検証値
		WholeTextDigest evidence{} ; // kind2は非計測
		// 不整合や終了状態を分岐させ、未確定のまま後続へ進みません
		if( planned.verbatimWhitespace) {
			evidence =
				wholeTextVerbatimReceiptDigest( s.binding.promotion.inputSha256, plan.planSha256, i, planned.core, body) ;
			requireWholeTextBounded( get( r, 144, 8) == 0 && get( r, 152, 8) == 0,
									WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
		} else {
			// この同期境界で所有する値、または確定状態の検証値です
			const auto &count = *planned.receipt ;
			countSha = count.receiptSha256 ;
			// この同期境界で所有する値、または確定状態の検証値です
			WholeTextBoundedChunkReceipt generated{count.session,
												   i,
												   planned.core,
												   plan.planSha256,
												   s.binding.promotion.inputSha256,
												   countSha,
												   count.tokenSha256,
												   count.sha256[2],
												   bodySha,
												   {},
												   count.bytes[2],
												   static_cast<std::uint32_t>( size),
												   static_cast<std::uint32_t>( get( r, 144, 4)),
												   static_cast<std::uint32_t>( get( r, 148, 4)),
												   static_cast<std::uint32_t>( get( r, 152, 4)),
												   static_cast<std::uint32_t>( get( r, 156, 4)),
												   true} ;
			generated.receiptSha256 = wholeTextGeneratedReceiptDigest( generated) ;
			validateWholeTextGeneratedReceipt(
				generated, body, wholeTextGenerationKind( s.binding.request.profileId, s.binding.request.profileSha256)) ;
			evidence = generated.receiptSha256 ;
			requireWholeTextBounded( generated.prefixTokens == count.prefixTokens,
									WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
		}
		requireWholeTextBounded( std::equal( countSha.begin(), countSha.end(), r.begin() + 48) &&
									std::equal( evidence.begin(), evidence.end(), r.begin() + 112),
								WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
		s.readerHash.append( std::span( s.buffer).first( size)) ; // 元順の直接連結SHA
		bodyEnd = end ;
		coreEnd = get( r, 24, 8) ;
	}
	requireWholeTextBounded( bodyEnd == s.cursor.bodyBytes && coreEnd == s.cursor.coreEnd &&
								s.readerHash.digest() == s.cursor.bodySha256,
							WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 確定cursor値を読みます
 * @return 最後にflush済みの値
 * @see koefude::WholeTextBoundedJournal::cursor
 */
//------------------------------------------------------------------------------
WholeTextBoundedCursor WholeTextBoundedJournal::cursor() const noexcept
{
	return impl_->cursor ;
}
//------------------------------------------------------------------------------
/**
 * @brief 次chunkが使える本文残量を読みます
 * @return byte残量
 * @see koefude::WholeTextBoundedJournal::remaining
 */
//------------------------------------------------------------------------------
std::uint64_t WholeTextBoundedJournal::remaining() const noexcept
{
	return impl_->capacity - impl_->cursor.bodyBytes ;
}
//------------------------------------------------------------------------------
/**
 * @brief 同じhandle/pathのownerへアクセスします
 * @return 単独file
 * @see koefude::WholeTextBoundedJournal::file
 */
//------------------------------------------------------------------------------
TransferableJournalFile &WholeTextBoundedJournal::file() noexcept
{
	return impl_->file ;
}
//------------------------------------------------------------------------------
/**
 * @brief 封印状態を読みます
 * @return header確定時true
 * @see koefude::WholeTextBoundedJournal::sealed
 */
//------------------------------------------------------------------------------
bool WholeTextBoundedJournal::sealed() const noexcept
{
	return impl_->sealed ;
}
//------------------------------------------------------------------------------
/**
 * @brief 不変bindingを読みます
 * @return owner寿命中の小値
 * @see koefude::WholeTextBoundedJournal::binding
 */
//------------------------------------------------------------------------------
const WholeTextJournalBinding &WholeTextBoundedJournal::binding() const noexcept
{
	return impl_->binding ;
}
//------------------------------------------------------------------------------
/**
 * @brief header SHAを読みます
 * @return 封印値
 * @see koefude::WholeTextBoundedJournal::headerSha256
 */
//------------------------------------------------------------------------------
WholeTextDigest WholeTextBoundedJournal::headerSha256() const noexcept
{
	return impl_->headerSha256 ;
}
//------------------------------------------------------------------------------
/**
 * @brief stored planをstream復元します
 * @param[in] stop 停止
 * @return 保存plan
 * @see koefude::WholeTextBoundedJournal::storedPlan
 */
//------------------------------------------------------------------------------
WholeTextChunkPlan WholeTextBoundedJournal::storedPlan( std::stop_token stop) const
{
	const auto &s = *impl_ ; // 同じsealed owner
	requireWholeTextBounded( s.sealed, WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
	return detail::decodeStoredWholeTextPlan(
		//------------------------------------------------------------------------------
		/**
		 * @brief plan sectionだけを部分readします
		 * @param[in] offset 相対位置
		 * @param[out] bytes 全域
		 */
		//------------------------------------------------------------------------------
		[&s]( std::uint64_t offset, std::span<std::uint8_t> bytes)
		{
			s.file.read( K_PLAN_OFFSET + offset, bytes) ;
		},
		s.encodedPlanBytes, s.binding.promotion.planSha256, stop) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 検証済みimplをmoveします
 * @param[in] impl owner
 * @see koefude::WholeTextBoundedJournal::WholeTextBoundedJournal(std::unique_ptr< koefude::WholeTextBoundedJournal::Impl >)
 */
//------------------------------------------------------------------------------
WholeTextBoundedJournal::WholeTextBoundedJournal( std::unique_ptr<Impl> impl) : impl_( std::move( impl))
{
}
//------------------------------------------------------------------------------
/**
 * @brief 照合済みknown fileをcursorから復元します
 * @param[in] file 同一file owner
 * @param[in] descriptor checkpoint証拠
 * @return 未commit tailを読まないjournal
 * @see koefude::WholeTextBoundedJournal::reopen
 */
//------------------------------------------------------------------------------
std::unique_ptr<WholeTextBoundedJournal> WholeTextBoundedJournal::reopen( TransferableJournalFile		   file,
																		 const WholeTextBoundedDescriptor &descriptor)
{
	auto  impl = std::make_unique<Impl>( std::move( file)) ; // 同じ一handle
	auto &s = *impl ; // 検証前owner
	s.file.read( 0, s.buffer) ;
	const auto	   all = std::span<const std::uint8_t>( s.buffer) ; // 不変header
	constexpr char HASH_DOMAIN[] = "koefude.whole_text_bounded_journal.v2" ; // 元domain
	requireWholeTextBounded( std::equal( all.begin(), all.begin() + sizeof(HASH_DOMAIN),
									   reinterpret_cast<const std::uint8_t *>( HASH_DOMAIN)) &&
								get( all, sizeof(HASH_DOMAIN), 4) == 2,
							WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
	s.headerBytes = static_cast<std::uint32_t>( get( all, sizeof(HASH_DOMAIN) + 4, 4)) ;
	requireWholeTextBounded( s.headerBytes >= 32 && s.headerBytes <= 65536,
							WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
	s.headerSha256 = wholeTextSha256( all.first( s.headerBytes - 32)) ;
	requireWholeTextBounded(
		s.headerSha256 == descriptor.headerSha256 &&
			std::equal( s.headerSha256.begin(), s.headerSha256.end(), all.begin() + s.headerBytes - 32),
		WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
	std::size_t at = sizeof(HASH_DOMAIN) + 8 ; // 元request開始
	const auto &r = descriptor.result ; // checkpointが束縛する値
	requireWholeTextBounded( get( all, at, 8) == r.request.source.historyId,
							WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
	at += 8 ;
	requireWholeTextBounded( std::equal( r.request.source.sha256.begin(), r.request.source.sha256.end(),
									   all.begin() + static_cast<std::ptrdiff_t>( at)),
							WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
	at += 32 ;
	requireWholeTextBounded( get( all, at, 8) == r.request.operationId, WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
	at += 8 ;
	const auto profileBytes = get( all, at, 4) ; // ASCII長さ
	at += 4 ;
	requireWholeTextBounded( profileBytes == r.request.profileId.size() && profileBytes <= 64 &&
								std::equal( r.request.profileId.begin(), r.request.profileId.end(),
										   all.begin() + static_cast<std::ptrdiff_t>( at)),
							WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
	at += static_cast<std::size_t>( profileBytes) ;
	requireWholeTextBounded( std::equal( r.request.profileSha256.begin(), r.request.profileSha256.end(),
									   all.begin() + static_cast<std::ptrdiff_t>( at)),
							WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
	at += 32 ;
	requireWholeTextBounded( get( all, at, 4) == r.pcm.version && get( all, at + 4, 8) == r.pcm.sampleCount &&
								get( all, at + 12, 4) == r.pcm.sampleRate && get( all, at + 16, 4) == r.pcm.channels &&
								get( all, at + 20, 8) == r.pcm.originSample &&
								std::equal( r.pcm.pcmSha256.begin(), r.pcm.pcmSha256.end(),
										   all.begin() + static_cast<std::ptrdiff_t>( at + 28)),
							WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
	at += 60 ;
	requireWholeTextBounded( get( all, at, 8) == r.binding.originalBytes, WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
	at += 8 ;
	std::copy_n( all.begin() + static_cast<std::ptrdiff_t>( at), 32, s.binding.originalSha256.begin()) ;
	at += 32 ;
	s.auxiliaryBytes = get( all, at, 8) ;
	at += 8 ;
	const auto auxAt = at ; /* aux SHA位置 */
	at += 32 ;
	s.encodedPlanBytes = get( all, at, 8) ;
	at += 8 ;
	requireWholeTextBounded( std::equal( r.binding.planSha256.begin(), r.binding.planSha256.end(),
									   all.begin() + static_cast<std::ptrdiff_t>( at)),
							WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
	at += 32 ;
	requireWholeTextBounded( std::equal( r.binding.openBinding.begin(), r.binding.openBinding.end(),
									   all.begin() + static_cast<std::ptrdiff_t>( at)) &&
								std::equal( r.binding.inputSha256.begin(), r.binding.inputSha256.end(),
										   all.begin() + static_cast<std::ptrdiff_t>( at + 32)),
							WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
	at += 64 ;
	requireWholeTextBounded( get( all, at, 4) == r.binding.recordCount &&
								get( all, at + 4, 4) == r.binding.generationCount && get( all, at + 8, 4) == 192,
							WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
	at += 12 ;
	s.receiptOffset = get( all, at, 8) ;
	at += 8 ;
	s.capacity = get( all, at, 8) ;
	at += 8 ;
	// 不変layoutの全offset/extentを照合します
	for( const auto expected :
		{65536ULL, K_AUX_OFFSET, K_SECTION_BYTES, K_PLAN_OFFSET, K_SECTION_BYTES, K_BODY_OFFSET, K_SECTION_BYTES}) {
		requireWholeTextBounded( get( all, at, 8) == expected, WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
		at += 8 ;
	}
	WholeTextDigest auxiliarySha{} ; // 未整列headerから固定値へcopy
	std::copy_n( all.begin() + static_cast<std::ptrdiff_t>( auxAt), 32, auxiliarySha.begin()) ;
	requireWholeTextBounded( at + 32 == s.headerBytes && s.capacity > 0 && s.capacity <= 8388608 &&
								s.auxiliaryBytes <= 8388608 &&
								wholeTextJournalPlanFits( s.encodedPlanBytes, r.binding.recordCount) &&
								s.receiptOffset == K_PLAN_OFFSET + s.encodedPlanBytes &&
								detail::boundedOpenBinding( r.request, auxiliarySha) == r.binding.openBinding,
							WholeTextBoundedDiagnostic::JOURNAL_ENCODING) ;
	static_cast<void>(
		wholeTextGenerationKind( r.request.profileId, r.request.profileSha256)) ; // 再読もexact id/SHAだけPlain
	s.binding.request = r.request ;
	s.binding.pcm = r.pcm ;
	s.binding.promotion = r.binding ;
	s.cursor = r.committed ;
	s.sealed = true ;
	// この同期境界で所有する値、または確定状態の検証値です
	auto result =
		std::unique_ptr<WholeTextBoundedJournal>( new WholeTextBoundedJournal( std::move( impl))) ; // private検証factory
	const auto plan = result->storedPlan() ; // 全encodingのstream再読
	result->validatePrefix( plan) ;
	// 以後appendしない復元ownerですがhashを確定prefixへ合わせます
	return result ;
}
} // namespace koefude
