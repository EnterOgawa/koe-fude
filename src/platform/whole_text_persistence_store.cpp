//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_persistence_store.cpp
 * @brief checkpointと同一Jを検証して有界全文の保存を確定します
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1343の文章専用補正を接続
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
#include "platform/whole_text_persistence_store.h"
#include "inference/whole_text_runtime.h"
#include "platform/win_helpers.h"
#include <optional>
#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <windows.h>
namespace koefude {
namespace {
constexpr DWORD K_UNSAFE = FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_OPEN |
						   FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_DIRECTORY |
						   FILE_ATTRIBUTE_DEVICE ; // 同期通信とlinkを拒否
//------------------------------------------------------------------------------
/**
 * @brief storageの必須条件を確認します
 * @param[in] ok 条件
 * @param[in] kind 履歴分類
 * @param[in] code 元code
 */
//------------------------------------------------------------------------------
void require( bool ok, HistoryErrorKind kind = HistoryErrorKind::CORRUPT, std::uint32_t code = 0)
{
	// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
	if( !ok) {
		throw HistoryError( kind, code) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief OS呼出し後に元errorを採取します
 * @param[in] ok API結果
 * @param[in] kind 分類
 */
//------------------------------------------------------------------------------
void requireOs( bool ok, HistoryErrorKind kind)
{
	// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
	if( !ok) {
		throw HistoryError( kind, GetLastError()) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 同一keyをpath構文なしの小文字hexへ変換します
 * @param[in] key 全key
 * @return 32文字
 */
//------------------------------------------------------------------------------
std::wstring keyHex( const WholeTextJournalKey &key)
{
	constexpr wchar_t HEX[] = L"0123456789abcdef" ; /* 固定表 */
	std::wstring	  s ; /* 小さい名前 */
	s.reserve( 32) ;
	// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
	for( const auto b : key) {
		s += HEX[b >> 4] ;
		s += HEX[b & 15] ;
	}
	return s ;
}
//------------------------------------------------------------------------------
/**
 * @brief 固定owner名だけを解釈します
 * @param[in] name basename
 * @param[out] key 成功時のkey
 * @return 正規名ならtrue
 */
//------------------------------------------------------------------------------
bool parseOwner( std::wstring_view name, WholeTextJournalKey &key)
{
	constexpr std::wstring_view PREFIX = L"whole-text-owner-" ; /* 固定名 */
	if( name.size() != PREFIX.size() + 32 + 4 || !name.starts_with( PREFIX) || !name.ends_with( L".bin")) {
		return false ;
	}
	// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
	for( std::size_t i = 0; i < 16; ++i) {
		unsigned value = 0 ; /* 一byte */
		for( unsigned n = 0; n < 2; ++n) {
			const auto c = name[PREFIX.size() + i * 2 + n] ; /* hex文字 */
			if( c >= L'0' && c <= L'9') {
				value = value * 16 + static_cast<unsigned>( c - L'0') ;
			} else if( c >= L'a' && c <= L'f') {
				value = value * 16 + static_cast<unsigned>( c - L'a' + 10) ;
			} else {
				return false ;
			}
		}
		key[i] = static_cast<std::uint8_t>( value) ;
	}
	return key != WholeTextJournalKey{} ;
}
//------------------------------------------------------------------------------
/**
 * @brief checked allocation上界を返します
 * @param[in] n 論理byte
 * @param[in] unit cluster byte
 * @return 丸めた上界
 */
//------------------------------------------------------------------------------
std::uint64_t rounded( std::uint64_t n, std::uint64_t unit)
{
	require( unit > 0 && unit <= 1048576 && n <= UINT64_MAX - (unit - 1), HistoryErrorKind::TOO_LARGE) ;
	return ((n + unit - 1) / unit) * unit ;
}
//------------------------------------------------------------------------------
/**
 * @brief 全量をoverflowなしに加算します
 * @param[in] a 元量
 * @param[in] b 追加
 * @return checked和
 */
//------------------------------------------------------------------------------
std::uint64_t add( std::uint64_t a, std::uint64_t b)
{
	require( b <= UINT64_MAX - a, HistoryErrorKind::TOO_LARGE) ;
	return a + b ;
}
//------------------------------------------------------------------------------
/**
 * @brief 通常local fileの実allocationを読みます
 * @param[in] path 既知固定path
 * @return max(logical,allocation)
 */
//------------------------------------------------------------------------------
std::uint64_t fileBytes( const std::filesystem::path &path)
{
	const auto attributes = GetFileAttributesW( path.c_str()) ; /* 元属性 */
	if( attributes == INVALID_FILE_ATTRIBUTES) {
		const auto code = GetLastError() ; /* 元照会code */
		if( code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
			return 0 ;
		}
		throw HistoryError( HistoryErrorKind::READ, code) ;
	}
	require( (attributes & K_UNSAFE) == 0, HistoryErrorKind::LOCATION) ;
	const UniqueHandle file( CreateFileW( path.c_str(), FILE_READ_ATTRIBUTES,
										FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
										FILE_FLAG_OPEN_REPARSE_POINT, nullptr)) ; /* 同期照会 */
	requireOs( file.get() != INVALID_HANDLE_VALUE, HistoryErrorKind::READ) ;
	FILE_STANDARD_INFO size{} ; /* 実使用量 */
	requireOs( GetFileInformationByHandleEx( file.get(), FileStandardInfo, &size, sizeof(size)) &&
				  size.EndOfFile.QuadPart >= 0 && size.AllocationSize.QuadPart >= 0,
			  HistoryErrorKind::READ) ;
	return std::max( static_cast<std::uint64_t>( size.EndOfFile.QuadPart),
					static_cast<std::uint64_t>( size.AllocationSize.QuadPart)) ;
}
/** @brief 4KiB owner形式を構成する小さいwriterです */
class OwnerWriter {
	public:
	/// padding0を含む固定全域
	std::array<std::uint8_t, 4096> bytes{} ;
	/// 次field
	std::size_t					   at = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定幅値を書きます
	 * @param[in] n 固定fieldへ符号化する符号なし整数。下位byteからlittle endianで追加します
	 * @param[in] width 出力するbyte数。呼出側は整数型の幅以内の固定field幅を渡します
	 */
	//------------------------------------------------------------------------------
	void						   number( std::uint64_t n, unsigned width = 4)
	{
		require( width <= bytes.size() - at) ;
		// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
		for( unsigned i = 0; i < width; ++i) {
			bytes[at++] = static_cast<std::uint8_t>( n >> (i * 8)) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief rawを書きます
	 * @param[in] value 全byte
	 */
	//------------------------------------------------------------------------------
	void raw( std::span<const std::uint8_t> value)
	{
		require( value.size() <= bytes.size() - at) ;
		std::copy( value.begin(), value.end(), bytes.begin() + static_cast<std::ptrdiff_t>( at)) ;
		at += value.size() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 要求を固定順で書きます
	 * @param[in] r 要求
	 */
	//------------------------------------------------------------------------------
	void request( const WholeTextCorrectionRequest &r)
	{
		number( r.source.historyId, 8) ;
		raw( r.source.sha256) ;
		number( r.operationId, 8) ;
		number( r.profileId.size()) ;
		raw( {reinterpret_cast<const std::uint8_t *>( r.profileId.data()), r.profileId.size()}) ;
		raw( r.profileSha256) ;
	}
} ;
/** @brief owner形式の固定幅読取り器です */
class OwnerReader {
	public:
	/// 同期借用
	std::span<const std::uint8_t> bytes ;
	/// 次field
	std::size_t					  at = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief byteを借用します
	 * @param[in] b 全4KiB
	 */
	//------------------------------------------------------------------------------
	explicit OwnerReader( std::span<const std::uint8_t> b) : bytes( b)
	{
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 指定byte幅のlittle endian整数をreaderの現位置から復元します
	 * @param[in] width 読むbyte数です。現callerは4または8を渡します。0なら0を返しcursorを進めません
	 * @return 最大64bitの符号なし復元値です。成功時はcursorをwidth byte進めます
	 * @throws HistoryError 必要byteが残っていない場合はCORRUPTを伝えます
	 * @note widthは8以下を前提とし、この上限自体を実行時検証しません。先頭の残長検査が失敗した場合はcursorを変更しません
	 */
	//------------------------------------------------------------------------------
	std::uint64_t number( unsigned width = 4)
	{
		require( at <= bytes.size() && width <= bytes.size() - at) ;
		std::uint64_t n = 0 ; /* 元値 */
		for( unsigned i = 0; i < width; ++i) {
			n |= static_cast<std::uint64_t>( bytes[at++]) << (i * 8) ;
		}
		return n ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief boolを読みます
	 * @return 検証済みbool
	 */
	//------------------------------------------------------------------------------
	bool flag()
	{
		const auto n = number() ; /* 外部値 */
		require( n <= 1) ;
		return n != 0 ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief rawを読みます
	 * @param[out] out 全域
	 */
	//------------------------------------------------------------------------------
	void raw( std::span<std::uint8_t> out)
	{
		require( out.size() <= bytes.size() - at) ;
		std::copy_n( bytes.begin() + static_cast<std::ptrdiff_t>( at), out.size(), out.begin()) ;
		at += out.size() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 元要求を読みます
	 * @return checked要求
	 */
	//------------------------------------------------------------------------------
	WholeTextCorrectionRequest request()
	{
		WholeTextCorrectionRequest r ; /* 小さい原値 */
		r.source.historyId = number( 8) ;
		raw( r.source.sha256) ;
		r.operationId = number( 8) ;
		const auto n = number() ; /* 外部ASCII長 */
		require( n > 0 && n <= 64 && n <= bytes.size() - at) ;
		r.profileId.assign( reinterpret_cast<const char *>( bytes.data() + at), static_cast<std::size_t>( n)) ;
		at += static_cast<std::size_t>( n) ;
		raw( r.profileSha256) ;
		require( r.source.historyId > 0 && r.operationId > 0 &&
				std::all_of( r.profileId.begin(), r.profileId.end(),
							//------------------------------------------------------------------------------
							/**
							 * @brief ASCIIを確認します
							 * @param[in] c byte
							 * @return 有効
							 */
							//------------------------------------------------------------------------------
							[]( unsigned char c)
							{
								return c > 0 && c < 128 ;
							})) ;
		return r ;
	}
} ;
/** @brief 一つの耐久owner/checkpointを単独worker間で移します */
class Owner final : public IWholeTextJournalCheckpoint {
	public:
	/// lock取得済みprivate root
	std::filesystem::path								   root ;
	/// OS乱数
	WholeTextJournalKey									   key{} ;
	/// 不変要求
	WholeTextCorrectionRequest							   request ;
	/// Reserved0/Sealed1/Terminal2/Residual3/DeleteIntent4
	unsigned											   state = 0 ;
	/// 原子的更新回数
	std::uint64_t										   sequence = 0 ;
	/// staging1/root2
	unsigned											   location = 1 ;
	/// file作成窓を区別
	bool												   identityPresent = false ;
	/// 同じfile
	std::array<std::uint64_t, 2>						   identity{} ;
	/// 作成前0、作成後固定J
	std::uint64_t										   extent = 0 ;
	/// canonical cursorだけ
	std::optional<WholeTextBoundedDescriptor>			   descriptor ;
	/// payload量
	std::shared_ptr<TransferableJournalCharge>			   journalCharge ;
	/// metadata/temp予約
	std::shared_ptr<TransferableJournalCharge>			   metadataCharge ;
	/// testだけ
	std::shared_ptr<detail::WholeTextPersistenceTestHooks> hooks ;
	/// 元削除失敗
	std::uint32_t										   cleanupCode = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定rootのowner名を返します
	 * @return canonical path
	 */
	//------------------------------------------------------------------------------
	std::filesystem::path								   path() const
	{
		return root / (L"whole-text-owner-" + keyHex( key) + L".bin") ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 指定fail地点を一回もretryせず拒否します
	 * @param[in] stage 固定地点
	 */
	//------------------------------------------------------------------------------
	void fault( unsigned stage)
	{
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( hooks && stage < hooks->hits.size()) {
			++hooks->hits[stage] ;
			// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
			if( hooks->failStage == stage) {
				throw HistoryError( HistoryErrorKind::WRITE, hooks->code) ;
			}
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief ownerの全fieldをcanonical化します
	 * @param[in] seq 次sequence
	 * @return 全4KiB
	 */
	//------------------------------------------------------------------------------
	std::array<std::uint8_t, 4096> encode( std::uint64_t seq) const
	{
		OwnerWriter	   w ; /* 固定buffer */
		constexpr char HASH_DOMAIN[] = "koefude.whole_text_owner.v1" ; /* NUL込み */
		w.raw( {reinterpret_cast<const std::uint8_t *>( HASH_DOMAIN), sizeof(HASH_DOMAIN)}) ;
		w.number( 1) ;
		const auto sizeAt = w.at ; /* payload長位置 */
		w.number( 0) ;
		w.raw( key) ;
		w.number( state) ;
		w.number( seq, 8) ;
		w.request( request) ;
		w.number( location) ;
		w.number( identityPresent) ;
		w.number( identity[0], 8) ;
		w.number( identity[1], 8) ;
		w.number( extent, 8) ;
		w.number( descriptor.has_value()) ;
		w.raw( descriptor ? descriptor->headerSha256 : WholeTextDigest{}) ;
		w.number( descriptor.has_value()) ;
		// この同期境界で所有する値、または確定状態の検証値です
		const auto d = descriptor ? detail::encodeBoundedDescriptor( *descriptor)
								  : std::vector<std::uint8_t>{} ; /* 最大2048byte */
		w.number( d.size()) ;
		w.raw( d) ;
		w.number( cleanupCode) ;
		w.number( journalCharge && journalCharge->active ? journalCharge->bytes : 0, 8) ;
		w.number( metadataCharge && metadataCharge->active ? metadataCharge->bytes : 0, 8) ;
		const auto payload = w.at + 32 ; /* SHA込み */
		const auto end = w.at ; /* SHA前 */
		w.at = sizeAt ;
		w.number( payload) ;
		w.at = end ;
		w.raw( wholeTextSha256( std::span( w.bytes).first( end))) ;
		return w.bytes ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じdirectoryのtempをflushしてatomic置換します
	 */
	//------------------------------------------------------------------------------
	void write()
	{
		require( sequence < UINT64_MAX, HistoryErrorKind::TOO_LARGE) ;
		const auto next = sequence + 1 ; /* no-wrap */
		const auto bytes = encode( next) ; /* 置換前確保 */
		const auto canonical = path() ; /* 小さいpath */
		auto	   temp = canonical ; /* 同じkeyのtemp */
		temp.replace_extension( L".partial") ;
		const auto old = fileBytes( temp) ; /* 残存tempも計上済み */
		require( old == 0, HistoryErrorKind::BUSY) ;
		UniqueHandle file( CreateFileW( temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_FLAG_OPEN_REPARSE_POINT,
									  nullptr)) ; /* private親からACL継承 */
		requireOs( file.get() != INVALID_HANDLE_VALUE, HistoryErrorKind::WRITE) ;
		try {
			fault( 11) ;
			DWORD written = 0 ; /* 元write数 */
			requireOs( WriteFile( file.get(), bytes.data(), static_cast<DWORD>( bytes.size()), &written, nullptr) &&
						  written == bytes.size(),
					  HistoryErrorKind::WRITE) ;
			fault( 12) ;
			requireOs( FlushFileBuffers( file.get()), HistoryErrorKind::WRITE) ;
			// 書込みhandleを閉じて同directoryだけで置き換えます
			file.reset() ;
			fault( 13) ;
			requireOs( MoveFileExW( temp.c_str(), canonical.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH),
					  HistoryErrorKind::WRITE) ;
			sequence = next ; // atomic成功直後noexcept
		} catch( ...) {
			file.reset() ;
			// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
			if( !DeleteFileW( temp.c_str())) {
				cleanupCode = GetLastError() ;
			}
			throw ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief header前に作成identityを耐久化します
	 * @param[in,out] file 同一J
	 */
	//------------------------------------------------------------------------------
	void created( TransferableJournalFile &file) override
	{
		const auto observed = file.identity() ; /* header前のidentity */
		identity = observed ;
		identityPresent = true ;
		extent = file.size() ;
		try {
			write() ;
			file.retainDurable() ;
		} catch( ...) {
			identity = {} ;
			identityPresent = false ;
			extent = 0 ;
			throw ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief J flush後に封印cursor0を確定します
	 * @param[in] binding 封印値
	 * @param[in] header header SHA
	 * @param[in] cursor 候補cursor
	 */
	//------------------------------------------------------------------------------
	void sealed( const WholeTextJournalBinding &binding, const WholeTextDigest &header,
				const WholeTextBoundedCursor &cursor) override
	{
		WholeTextBoundedDescriptor d ; /* cursor0候補 */
		d.journalKey = key ;
		d.fileIdentity = identity ;
		d.headerSha256 = header ;
		d.state = WholeTextBoundedAttemptState::INTERRUPTED ;
		d.result.request = binding.request ;
		d.result.pcm = binding.pcm ;
		d.result.binding = binding.promotion ;
		d.result.committed = cursor ;
		const auto previous = descriptor ; /* 失敗時の元checkpoint */
		const auto oldState = state ; /* 元状態 */
		descriptor = std::move( d) ;
		state = 1 ;
		try {
			write() ;
		} catch( ...) {
			descriptor = previous ;
			state = oldState ;
			throw ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief J flush後に候補cursorを原子的に確定します
	 * @param[in] cursor 候補
	 */
	//------------------------------------------------------------------------------
	void commit( const WholeTextBoundedCursor &cursor) override
	{
		require( descriptor.has_value()) ;
		const auto old = descriptor->result.committed ; /* 前canonical cursor */
		descriptor->result.committed = cursor ;
		try {
			write() ;
		} catch( ...) {
			descriptor->result.committed = old ;
			throw ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief checksum-valid canonical ownerだけ復元します
	 * @param[in] bytes 4096byte
	 */
	//------------------------------------------------------------------------------
	void decode( std::span<const std::uint8_t> bytes)
	{
		require( bytes.size() == 4096) ;
		OwnerReader	   r( bytes) ; /* 同期reader */
		constexpr char HASH_DOMAIN[] = "koefude.whole_text_owner.v1" ; /* 元domain */
		require( std::equal( bytes.begin(), bytes.begin() + sizeof(HASH_DOMAIN),
						   reinterpret_cast<const std::uint8_t *>( HASH_DOMAIN))) ;
		r.at = sizeof(HASH_DOMAIN) ;
		require( r.number() == 1) ;
		const auto payload = r.number() ; /* 正確payload長 */
		require( payload <= 4096 && payload >= 32) ;
		r.raw( key) ;
		state = static_cast<unsigned>( r.number()) ;
		sequence = r.number( 8) ;
		request = r.request() ;
		location = static_cast<unsigned>( r.number()) ;
		identityPresent = r.flag() ;
		identity = {r.number( 8), r.number( 8)} ;
		extent = r.number( 8) ;
		const bool		sealed = r.flag() ; /* sealed header presence */
		WholeTextDigest header{} ; /* header SHA */
		r.raw( header) ;
		const bool hasDescriptor = r.flag() ; /* descriptor presence */
		const auto length = r.number() ; /* 小さい外部長 */
		require( hasDescriptor == sealed && length <= 2048 && length <= bytes.size() - r.at &&
				(hasDescriptor || length == 0)) ;
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( hasDescriptor) {
			descriptor = detail::decodeBoundedDescriptor( bytes.subspan( r.at, static_cast<std::size_t>( length))) ;
			require( descriptor->journalKey == key && descriptor->fileIdentity == identity &&
					descriptor->headerSha256 == header) ;
		} else {
			require( header == WholeTextDigest{}) ;
		}
		r.at += static_cast<std::size_t>( length) ;
		cleanupCode = static_cast<std::uint32_t>( r.number()) ;
		journalCharge = std::make_shared<TransferableJournalCharge>() ;
		metadataCharge = std::make_shared<TransferableJournalCharge>() ;
		journalCharge->bytes = r.number( 8) ;
		metadataCharge->bytes = r.number( 8) ;
		const auto		hashAt = r.at ; /* SHA前位置 */
		WholeTextDigest sha{} ; /* 元SHA */
		r.raw( sha) ;
		require( r.at == payload && sha == wholeTextSha256( bytes.first( hashAt)) &&
				std::all_of( bytes.begin() + static_cast<std::ptrdiff_t>( payload), bytes.end(),
							//------------------------------------------------------------------------------
							/**
							 * @brief padding0を確認します
							 * @param[in] b byte
							 * @return 0
							 */
							//------------------------------------------------------------------------------
							[]( auto b)
							{
								return b == 0 ;
							}) &&
				state <= 4 && sequence > 0 && (location == 1 || location == 2) &&
				((identityPresent && identity[0] != 0 && identity[1] != 0 && extent == 25231360) ||
				 (!identityPresent && identity == std::array<std::uint64_t, 2>{} && extent == 0)) &&
				(!sealed || identityPresent)) ;
		journalCharge->active = journalCharge->bytes != 0 ;
		metadataCharge->active = metadataCharge->bytes != 0 ;
		journalCharge->durable = true ;
		metadataCharge->durable = true ;
		const auto canonical = encode( sequence) ; /* 同一canonical buffer */
		require( std::equal( canonical.begin(), canonical.end(), bytes.begin())) ;
	}
} ;
//------------------------------------------------------------------------------
/**
 * @brief 一sourceの元本文と全保存本文を重複Jなしで数えます
 * @param[in] snapshot 全履歴
 * @param[in] id 対象entry
 * @return 対象のchecked byte数
 */
//------------------------------------------------------------------------------
std::uint64_t retainedText( const HistorySnapshot &snapshot, std::uint64_t id)
{
	std::vector<detail::WholeTextRetainedEntryAmount> amounts ; // 最大32個の既知量
	amounts.reserve( snapshot.entries.size()) ;
	// 全entryの量を別々に計数して対象だけへ上限を適用します
	for( const auto &e : snapshot.entries) {
		std::uint64_t n = 0 ; // 一entryだけの本文量
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( e.result) {
			// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
			for( const auto &line : e.result->lines) {
				n = add( n, line.text.size()) ;
			}
		}
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( e.reading.formatted) {
			// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
			for( const auto &line : e.reading.formatted->lines) {
				n = add( n, line.text.size()) ;
			}
		}
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( e.wholeText.corrected) {
			n = add( n, e.wholeText.corrected->text.size()) ;
		}
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( e.bounded.effective) {
			n = add( n, e.bounded.effective->result.committed.bodyBytes) ;
		}
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( e.bounded.latest &&
		   (!e.bounded.effective || e.bounded.latest->journalKey != e.bounded.effective->journalKey)) {
			n = add( n, e.bounded.latest->result.committed.bodyBytes) ;
		}
		amounts.push_back( {e.id, n}) ; // 他entryと16MiB上限を分離
	}
	return detail::persistenceRetainedEntryBytes( amounts, id) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 要求に対応する正本entryを返します
 * @param[in,out] snapshot 正本
 * @param[in] key source
 * @return 同じentry
 */
//------------------------------------------------------------------------------
HistoryEntry &entryFor( HistorySnapshot &snapshot, const ReadingSourceKey &key)
{
	// この同期境界で所有する値、または確定状態の検証値です
	const auto found = std::find_if( snapshot.entries.begin(), snapshot.entries.end(),
		//------------------------------------------------------------------------------
		/**
		 * @brief 同IDを検索します
		 * @param[in] e entry
		 * @return 同ID
		 */
		//------------------------------------------------------------------------------
		[&key]( const auto &e)
		{
			return e.id == key.historyId ;
		}) ; /* 最大32件 */
	require( found != snapshot.entries.end() && found->result && fingerprintReadingSource( *found->result) == key.sha256) ;
	return *found ;
}
//------------------------------------------------------------------------------
/**
 * @brief 全原文sliceを元tapeへ照合します
 * @param[in] source 原文
 * @param[in] plan 保存plan
 */
//------------------------------------------------------------------------------
void validateSourcePlan( const Transcript &source, const WholeTextChunkPlan &plan)
{
	std::vector<std::uint64_t> ends( source.lines.size()) ; /* 元行ごとのcoverage */
	std::uint64_t			   total = 0 ; /* 全tape */
	for( const auto &line : source.lines) {
		total = add( total, line.text.size()) ;
	}
	std::uint64_t covered = 0 ; /* 全slice byte */
	for( const auto &c : plan.chunks) {
		std::uint64_t local = 0 ; /* 一coreのcoverage */
		for( const auto &part : c.original) {
			require( part.origin < source.lines.size()) ;
			const auto &text = source.lines[part.origin].text ; /* 元無編集byte */
			require( part.bytes.begin == ends[part.origin] && part.bytes.end <= text.size() &&
					part.bytes.begin < part.bytes.end &&
					(part.bytes.begin == text.size() ||
					 (static_cast<unsigned char>( text[static_cast<std::size_t>( part.bytes.begin)]) & 0xc0) != 0x80) &&
					(part.bytes.end == text.size() ||
					 (static_cast<unsigned char>( text[static_cast<std::size_t>( part.bytes.end)]) & 0xc0) != 0x80)) ;
			ends[part.origin] = part.bytes.end ;
			local = add( local, part.bytes.end - part.bytes.begin) ;
		}
		require( local == c.core.end - c.core.begin) ;
		covered = add( covered, local) ;
	}
	require( covered == total && plan.chunks.back().core.end == total) ;
	// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
	for( std::size_t i = 0; i < ends.size(); ++i) {
		require( ends[i] == source.lines[i].text.size()) ;
	}
	// この同期境界で所有する値、または確定状態の検証値です
	const auto actual = detail::wholeTextStreamSha256(
		//------------------------------------------------------------------------------
		/**
		 * @brief 原文tapeを全連結bufferなしで読みます
		 * @param[in] offset tape位置
		 * @param[out] out 最大64KiB
		 */
		//------------------------------------------------------------------------------
		[&source]( std::uint64_t offset, std::span<std::uint8_t> out)
		{
			// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
			for( const auto &line : source.lines) {
				// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
				if( offset >= line.text.size()) {
					offset -= line.text.size() ;
					continue ;
				}
				// この同期境界で所有する値、または確定状態の検証値です
				const auto n = std::min<std::size_t>(
					out.size(), line.text.size() - static_cast<std::size_t>( offset)) ; /* 今回slice */
				std::copy_n( reinterpret_cast<const std::uint8_t *>( line.text.data()) + offset, n, out.begin()) ;
				out = out.subspan( n) ;
				offset = 0 ;
				// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
				if( out.empty()) {
					break ;
				}
			}
			require( out.empty()) ;
		},
		total) ; /* 元tapeの全SHA */
	require( actual == plan.originalSha256) ;
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 対象entryだけの保持textをchecked計数します
 * @param[in] entries 最大32件の既知量
 * @param[in] id 対象ID
 * @return 対象の本文合計
 * @see koefude::detail::persistenceRetainedEntryBytes
 */
//------------------------------------------------------------------------------
std::uint64_t detail::persistenceRetainedEntryBytes( std::span<const WholeTextRetainedEntryAmount> entries,
													std::uint64_t								  id)
{
	require( entries.size() <= 32, HistoryErrorKind::TOO_LARGE) ;
	std::uint64_t bytes = 0 ; // 対象entryだけ
	// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
	for( const auto &entry : entries) {
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( entry.id == id) {
			bytes = add( bytes, entry.bytes) ;
		}
	}
	return bytes ;
}
//------------------------------------------------------------------------------
/**
 * @brief 固定inventory境界とknown属性を照合します
 * @param[in] probe 現probe0始まり
 * @param[in] slots 既存slot数
 * @param[in] known 名前/ID/header/台帳の一致
 * @param[in] unsafe 同期/link等
 * @param[in] sizeKnown 実使用量の照会成功
 * @param[in] live 他owner使用中
 * @return 読取り/cleanupの候補として安全な時true
 * @see koefude::detail::persistenceInventoryCandidate
 */
//------------------------------------------------------------------------------
bool detail::persistenceInventoryCandidate( std::uint32_t probe, std::uint32_t slots, bool known, bool unsafe,
										   bool sizeKnown, bool live) noexcept
{
	return probe < 128 && slots < 128 && known && !unsafe && sizeKnown && !live ;
}
//------------------------------------------------------------------------------
/**
 * @brief 全加算と上限を生成前に検査します
 * @param[in] a 全使用量
 * @param[in] op 縮小だけ
 * @param[in] history 縮小だけ
 * @param[in] text 縮小だけ
 * @return 全条件適合
 * @see koefude::detail::persistenceAmountsFit
 */
//------------------------------------------------------------------------------
bool detail::persistenceAmountsFit( const WholeTextPersistenceAmounts &a, std::uint64_t op, std::uint64_t history,
								   std::uint64_t text) noexcept
{
	// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
	if( op > 67108864 || history > 536870912 || text > 16777216 || a.entries > 32 || a.entryDelta > 32 - a.entries ||
	   a.textExisting >= text) {
		return false ;
	}
	std::uint64_t left = op ; /* checked残量 */
	for( const auto n : {a.previousJournal, a.newJournal, a.oldMetadata, a.newMetadata}) {
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( n > left) {
			return false ;
		}
		left -= n ;
	}
	// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
	if( a.historyExisting > history || a.newJournal > history - a.historyExisting ||
	   a.newMetadata > history - a.historyExisting - a.newJournal) {
		return false ;
	}
	return true ;
}
/** @brief 全slotと予約の台帳を一workerに閉じます */
class WholeTextPersistenceStore::Impl {
	public:
	/// 固定root
	std::filesystem::path								   root ;
	/// 同期metadata writer
	Commit												   commit ;
	/// testだけ
	std::shared_ptr<detail::WholeTextPersistenceTestHooks> hooks ;
	// この同期境界で所有する値、または確定状態の検証値です
	/// 全保持量
	std::shared_ptr<TransferableJournalQuota>			   ledger = std::make_shared<TransferableJournalQuota>() ;
	/// 最大128固定slot
	std::array<std::shared_ptr<Owner>, 128>				   owners{} ;
	/// 一つの実行中owner
	std::shared_ptr<Owner>								   reservation ;
	/// pointer/control block
	std::shared_ptr<const Transcript>					   acceptedSource ;
	/// 予約前確保の未保存ack
	std::shared_ptr<WholeTextBoundedPersistenceCompletion> failedCompletion ;
	/// 予約前確保ack
	std::shared_ptr<WholeTextBoundedPersistenceCompletion> completion ;
	/// 検証済みcluster
	std::uint64_t										   allocationUnit = 4096 ;
	/// 全snapshot/backup/partial allocation
	std::uint64_t										   metadataExisting = 0 ;
	/// ordinary増加もこの上界へ照合
	std::uint64_t										   reservedMetadataUpper = 0 ;
	/// 公開済みimmutable ackを再変更しません
	bool												   failedPublished = false ;
	/// 未知inventoryでは新規予約禁止
	bool												   blocked = false ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 値を所有します
	 * @param[in] r root
	 * @param[in] c writer
	 * @param[in] h test
	 */
	//------------------------------------------------------------------------------
	Impl( std::filesystem::path r, Commit c, std::shared_ptr<detail::WholeTextPersistenceTestHooks> h)
		: root( std::move( r)), commit( std::move( c)), hooks( std::move( h))
	{
	}
	/// 置換後例外を元成功とは独立して記録
	std::uint32_t postCommitWarning = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief atomic成功receipt後の例外でも確定を維持します
	 * @param[in] snapshot 事前確保候補
	 * @param[out] receipt 固定scalar
	 */
	//------------------------------------------------------------------------------
	void		  commitSnapshot( const HistorySnapshot &snapshot, HistorySaveReceipt &receipt)
	{
		try {
			commit( snapshot, receipt) ;
		} catch( const HistoryError &error) {
			// この条件の対象だけを処理し、確定済みの所有状態を保ちます
			if( !receipt.committed) {
				throw ;
			}
			postCommitWarning = error.code() ? error.code() : ERROR_WRITE_FAULT ;
		} catch( const std::bad_alloc &) {
			// この条件の対象だけを処理し、確定済みの所有状態を保ちます
			if( !receipt.committed) {
				throw ;
			}
			postCommitWarning = ERROR_NOT_ENOUGH_MEMORY ;
		} catch( ...) {
			// この条件の対象だけを処理し、確定済みの所有状態を保ちます
			if( !receipt.committed) {
				throw ;
			}
			postCommitWarning = ERROR_UNHANDLED_EXCEPTION ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 現metadata全量を有界に照会します
	 * @return 全allocation
	 */
	//------------------------------------------------------------------------------
	std::uint64_t metadataBytes()
	{
		std::uint64_t n = 0 ; /* 全量 */
		for( const auto *name : {L"history.bin", L"history.pre-v5.bin", L"history.pre-v6.bin", L"history.pre-v7.bin"}) {
			n = add( n, fileBytes( root / name)) ;
		}
		WIN32_FIND_DATAW data{} ; // legacy専用partialだけの有界照会
		// この同期境界で所有する値、または確定状態の検証値です
		const HANDLE	 found = FindFirstFileW( (root / L"history.partial-*").c_str(), &data) ; // 固定pattern
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( found != INVALID_HANDLE_VALUE) {
			const std::unique_ptr<void, decltype(&FindClose)> search( found, FindClose) ; /* 検索owner */
			for( unsigned probe = 0; probe < 65; ++probe) {
				require( probe < 64, HistoryErrorKind::TOO_LARGE) ;
				require( (data.dwFileAttributes & K_UNSAFE) == 0, HistoryErrorKind::LOCATION) ;
				n = add( n, fileBytes( root / data.cFileName)) ;
				// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
				if( !FindNextFileW( found, &data)) {
					break ;
				}
			}
		}
		return n ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じkeyのslotを検索します
	 * @param[in] key key
	 * @return 所有slot/null
	 */
	//------------------------------------------------------------------------------
	std::shared_ptr<Owner> find( const WholeTextJournalKey &key) const
	{
		// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
		for( const auto &slot : owners) {
			// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
			if( slot && slot->key == key) {
				return slot ;
			}
		}
		return {} ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 空いている固定slotへ保持します
	 * @param[in] owner owner
	 */
	//------------------------------------------------------------------------------
	void insert( std::shared_ptr<Owner> owner)
	{
		// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
		for( auto &slot : owners) {
			// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
			if( !slot) {
				slot = std::move( owner) ;
				return ;
			}
		}
		throw HistoryError( HistoryErrorKind::TOO_LARGE) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 参照されているkeyを照合します
	 * @param[in] snapshot 正本
	 * @param[in] key key
	 * @return live参照
	 */
	//------------------------------------------------------------------------------
	bool referenced( const HistorySnapshot &snapshot, const WholeTextJournalKey &key) const
	{
		// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
		for( const auto &e : snapshot.entries) {
			// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
			if( (e.bounded.effective && e.bounded.effective->journalKey == key) ||
			   (e.bounded.latest && e.bounded.latest->journalKey == key) ||
			   (e.bounded.inFlight && e.bounded.inFlight->reservationKey == key)) {
				return true ;
			}
		}
		return false ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief known fileだけ削除し、失敗時chargeを維持します
	 * @param[in,out] slot owner
	 * @return 元code
	 */
	//------------------------------------------------------------------------------
	std::uint32_t cleanup( std::shared_ptr<Owner> &slot) noexcept
	{
		try {
			slot->state = 4 ;
			slot->write() ;
			// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
			if( slot->identityPresent) {
				bool absent = false ; // 既知DeleteIntentの二固定候補だけで不存在を確認
				// 元Jの削除成功後にowner削除だけ失敗した窓を安全に回収します
				if( !slot->journalCharge->testIo) {
					const auto name = L"koefude-journal-" + keyHex( slot->key) ; // 同一key名だけ
					const auto a = GetFileAttributesW( (root / L"whole-text-staging" / name).c_str()) ; // staging属性
					const auto ae = a == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS ; // 元code
					const auto b = GetFileAttributesW( (root / name).c_str()) ; // root属性
					const auto be = b == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS ; // 元code
					absent = a == INVALID_FILE_ATTRIBUTES && b == INVALID_FILE_ATTRIBUTES &&
							 (ae == ERROR_FILE_NOT_FOUND || ae == ERROR_PATH_NOT_FOUND) &&
							 (be == ERROR_FILE_NOT_FOUND || be == ERROR_PATH_NOT_FOUND) ;
				}
				// 参照除去/DeleteIntent後の不存在確認だけでpayload chargeを返します
				if( absent) {
					slot->journalCharge->release() ;
				} else {

					// この同期境界で所有する値、または確定状態の検証値です
					auto file = TransferableJournalFile::reopenKnown( root, slot->key, slot->location, slot->identity,
																	 slot->journalCharge) ; /* 排他known file */
					const auto result = file.cleanup() ; /* 一回だけ */
					if( !result.deleted) {
						slot->state = 3 ;
						slot->cleanupCode = result.error ;
						slot->write() ;
						return result.error ;
					}
				}
			} else if( slot->journalCharge) {
				slot->journalCharge->release() ;
			}
			// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
			if( !DeleteFileW( slot->path().c_str())) {
				const auto code = GetLastError() ; /* 元code */
				slot->cleanupCode = code ;
				return code ;
			}
			// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
			if( slot->metadataCharge) {
				slot->metadataCharge->release() ;
			}
			slot.reset() ;
			return 0 ;
		} catch( const HistoryError &e) {
			return e.code() ? e.code() : ERROR_INVALID_DATA ;
		} catch( const OperationError &e) {
			return e.code() ? e.code() : ERROR_INVALID_DATA ;
		} catch( ...) {
			return ERROR_INVALID_DATA ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief bodyを複製せずsource/plan/receiptを検証します
	 * @param[in,out] journal 同じJ
	 * @param[in] source 原文
	 */
	//------------------------------------------------------------------------------
	void verify( WholeTextBoundedJournal &journal, const Transcript &source)
	{
		const auto plan = journal.storedPlan() ; /* 全receiptの保存plan */
		validateSourcePlan( source, plan) ;
		const auto &binding = journal.binding() ; /* 封印 */
		// 空補助planの保存を新文章専用profileへ限定し、旧音声契約は維持します
		require( (binding.request.profileId == K_TEXT_CONTEXT_PROFILE_ID) == plan.canonical.empty()) ;
		require( plan.inputBinding == binding.promotion.openBinding &&
				detail::boundedOpenBinding( binding.request, plan.auxiliarySha256) == binding.promotion.openBinding &&
				detail::boundedInputBinding( binding.promotion.openBinding, plan.planSha256) ==
					binding.promotion.inputSha256 &&
				plan.measures == binding.promotion.measures && plan.measuredBytes == binding.promotion.measuredBytes &&
				plan.chunks.size() == binding.promotion.recordCount &&
				static_cast<std::uint64_t>( std::count_if( plan.chunks.begin(), plan.chunks.end(),
			//------------------------------------------------------------------------------
			/**
			 * @brief 実生成coreだけ数えます
			 * @param[in] record 保存record
			 * @return 非空白ならtrue
			 */
			//------------------------------------------------------------------------------
			[]( const auto &record)
			{
				return !record.verbatimWhitespace ;
			})) == binding.promotion.generationCount) ;
		journal.validatePrefix( plan) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 保存bodyを一回だけ準備します
	 * @param[in,out] journal 既知J
	 * @param[in] source 原文
	 * @return 一つの無編集body
	 */
	//------------------------------------------------------------------------------
	std::string readBody( WholeTextBoundedJournal &journal, const Transcript &source)
	{
		verify( journal, source) ;
		const auto	cursor = journal.cursor() ; /* canonicalだけ */
		std::string body( static_cast<std::size_t>( cursor.bodyBytes), '\0') ; /* 唯一のbody確保 */
		for( std::uint64_t offset = 0; offset < cursor.bodyBytes;) {
			// この同期境界で所有する値、または確定状態の検証値です
			const auto n =
				static_cast<std::size_t>( std::min<std::uint64_t>( 65536, cursor.bodyBytes - offset)) ; /* 有界read */
			journal.readPrefix( offset, {reinterpret_cast<std::uint8_t *>( body.data() + offset), n}) ;
			offset += n ;
		}
		require( wholeTextSha256( body) == cursor.bodySha256) ;
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( !body.empty()) {
			// UTF8の妥当性だけを数え、検証用UTF16の全文copyを作りません
			require( MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, body.data(), static_cast<int>( body.size()),
										nullptr, 0) > 0) ;
			require( body.find( '\0') == std::string::npos && body.find( "<|im_start|>") == std::string::npos &&
					body.find( "<|im_end|>") == std::string::npos && body.find( "<think>") == std::string::npos &&
					body.find( "</think>") == std::string::npos) ;
		}
		return body ;
	}
} ;
//------------------------------------------------------------------------------
/**
 * @brief 固定rootとatomic writerを借用せず保持します
 * @param[in] root 検証済みprivate history root
 * @param[in] commit 同期writer
 * @param[in] hooks 内部試験だけ。通常null
 * @anchor koefude_whole_text_persistence_store_construct_contract
 * @see \ref koefude_whole_text_persistence_store_construct_contract "koefude::WholeTextPersistenceStore::WholeTextPersistenceStore(std::filesystem::path, koefude::WholeTextPersistenceStore::Commit, std::shared_ptr< koefude::detail::WholeTextPersistenceTestHooks >)"
 */
//------------------------------------------------------------------------------
WholeTextPersistenceStore::WholeTextPersistenceStore( std::filesystem::path root, Commit commit,
													 std::shared_ptr<detail::WholeTextPersistenceTestHooks> hooks)
	: impl_( std::make_unique<Impl>( std::move( root), std::move( commit), std::move( hooks)))
{
}
//------------------------------------------------------------------------------
/**
 * @brief 耐久ownerをclose-onlyで解放します
 * @see koefude::WholeTextPersistenceStore::~WholeTextPersistenceStore
 */
//------------------------------------------------------------------------------
WholeTextPersistenceStore::~WholeTextPersistenceStore() = default ;
//------------------------------------------------------------------------------
/**
 * @brief lock後に有界inventoryと復元を行います
 * @param[in,out] durable 検証済み正本。原文は失敗時も保持
 * @see koefude::WholeTextPersistenceStore::load
 */
//------------------------------------------------------------------------------
void WholeTextPersistenceStore::load( HistorySnapshot &durable)
{
	auto &s = *impl_ ; // 一worker
	// allocation上界を推測せず実volumeから取得します
	if( s.hooks) {
		s.allocationUnit = s.hooks->allocationUnit ;
	} else {
		std::array<wchar_t, 32768> volume{} ; /* root volume */
		requireOs( GetVolumePathNameW( s.root.c_str(), volume.data(), static_cast<DWORD>( volume.size())),
				  HistoryErrorKind::LOCATION) ;
		DWORD sectors = 0, bytes = 0, freeClusters = 0, totalClusters = 0 ; /* OS容量単位 */
		requireOs( GetDiskFreeSpaceW( volume.data(), &sectors, &bytes, &freeClusters, &totalClusters),
				  HistoryErrorKind::READ) ;
		s.allocationUnit = static_cast<std::uint64_t>( sectors) * bytes ;
	}
	static_cast<void>( rounded( 1, s.allocationUnit)) ;
	try {
		s.metadataExisting = s.metadataBytes() ;
	} catch( ...) {
		s.blocked = true ;
		s.metadataExisting = 0 ;
	}
	s.ledger->bytes = s.metadataExisting ;
	s.ledger->entries = static_cast<std::uint32_t>( durable.entries.size()) ;
	// canonical ownerだけ最大129probeし、128slotへ制限します
	WIN32_FIND_DATAW data{} ; // entry属性
	// この同期境界で所有する値、または確定状態の検証値です
	const HANDLE	 found = FindFirstFileW( (s.root / L"whole-text-owner-*").c_str(), &data) ; // 固定pattern
	// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
	if( found != INVALID_HANDLE_VALUE) {
		const std::unique_ptr<void, decltype(&FindClose)> search( found, FindClose) ; /* 有界列挙owner */
		for( unsigned probe = 0; probe < 129; ++probe) {
			// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
			if( probe == 128) {
				s.blocked = true ;
				break ;
			}
			WholeTextJournalKey		key{} ; /* basename key */
			const std::wstring_view name( data.cFileName) ; /* 同期借用 */
			if( !detail::persistenceInventoryCandidate( probe, probe, parseOwner( name, key),
													  (data.dwFileAttributes & K_UNSAFE) != 0, true, false)) {
				s.blocked = true ;
			} else {
				try {
					const auto path = s.root / std::wstring( name) ; /* 自分の固定名 */
					require( fileBytes( path) <= rounded( 4096, s.allocationUnit), HistoryErrorKind::TOO_LARGE) ;
					const UniqueHandle file( CreateFileW( path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
														FILE_FLAG_OPEN_REPARSE_POINT, nullptr)) ; /* ownerだけread */
					requireOs( file.get() != INVALID_HANDLE_VALUE, HistoryErrorKind::READ) ;
					std::array<std::uint8_t, 4096> buffer{} ; /* 最大4KiB */
					DWORD						   read = 0 ; /* 元read数 */
					requireOs( ReadFile( file.get(), buffer.data(), static_cast<DWORD>( buffer.size()), &read, nullptr) &&
								  read == buffer.size(),
							  HistoryErrorKind::READ) ;
					auto owner = std::make_shared<Owner>() ; /* 固定slot */
					owner->root = s.root ;
					owner->hooks = s.hooks ;
					owner->decode( buffer) ;
					require( owner->key == key && !s.find( key)) ;
					owner->journalCharge->ledger = s.ledger ;
					owner->journalCharge->journalKey = key ;
					owner->metadataCharge->ledger = s.ledger ;
					// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
					if( s.hooks) {
						owner->journalCharge->testIo = s.hooks->journal ;
					}
					s.ledger->bytes =
						add( s.ledger->bytes, add( owner->journalCharge->bytes, owner->metadataCharge->bytes)) ;
					require( owner->request.operationId < UINT64_MAX, HistoryErrorKind::TOO_LARGE) ;
					durable.nextOperationId = std::max( durable.nextOperationId, owner->request.operationId + 1) ;
					s.insert( std::move( owner)) ;
				} catch( ...) {
					s.blocked = true ;
				}
			}
			// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
			if( !FindNextFileW( found, &data)) {
				break ;
			}
		}
	}
	// 二つの固定directoryだけでknown Jと照合します。未知fileを削除しません
	for( const auto &directory : {s.root, s.root / L"whole-text-staging"}) {
		WIN32_FIND_DATAW file{} ; /* 有界J列挙 */
		const HANDLE	 search = FindFirstFileW( (directory / L"koefude-journal-*").c_str(), &file) ; /* 固定pattern */
		if( search == INVALID_HANDLE_VALUE) {
			continue ;
		}
		const std::unique_ptr<void, decltype(&FindClose)> owned( search, FindClose) ; /* 閉じる検索handle */
		for( unsigned probe = 0; probe < 129; ++probe) {
			// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
			if( probe == 128) {
				s.blocked = true ;
				break ;
			}
			const std::wstring_view name( file.cFileName) ; /* basename */
			const auto				known = std::find_if( s.owners.begin(), s.owners.end(),
				//------------------------------------------------------------------------------
				/**
				 * @brief known identity保持slotだけ検索します
				 * @param[in] slot owner
				 * @return 同名known
				 */
				//------------------------------------------------------------------------------
				[&name]( const auto &slot)
				{
					return slot && slot->identityPresent &&
						   name == L"koefude-journal-" + keyHex( slot->key) ;
				}) ; /* 最大128 */
			if( known == s.owners.end() || (file.dwFileAttributes & K_UNSAFE) != 0) {
				s.blocked = true ;
			}
			// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
			if( !FindNextFileW( search, &file)) {
				break ;
			}
		}
	}
	HistorySnapshot recovered = durable ; // metadata確定前の別候補
	bool			interrupted = false ; // 非再開の復元更新
	// 完全参照だけを有効化し、in_flightはcanonical checkpointへ限定します
	for( auto &entry : recovered.entries) {
		auto	  &record = entry.bounded ; /* 独立record */
		const auto restore =
			//------------------------------------------------------------------------------
			/**
			 * @brief 検証済み同一fileからbodyを復元します
			 * @param[in] descriptor 保存証拠
			 * @return immutable body
			 */
			//------------------------------------------------------------------------------
			[&]( const WholeTextBoundedDescriptor &descriptor) -> std::shared_ptr<const WholeTextBoundedText>
		{
			const auto owner = s.find( descriptor.journalKey) ; /* 必須known owner */
			require( owner && owner->identityPresent && owner->identity == descriptor.fileIdentity &&
					owner->descriptor && owner->descriptor->headerSha256 == descriptor.headerSha256 &&
					owner->request.operationId == descriptor.result.request.operationId &&
					owner->request.source == descriptor.result.request.source) ;
			// この同期境界で所有する値、または確定状態の検証値です
			auto file = TransferableJournalFile::reopenKnown( s.root, owner->key, owner->location, owner->identity,
															 owner->journalCharge) ; /* 同一ID */
			owner->location = file.path().parent_path() == s.root ? 2u : 1u ; // rename後crashの固定候補を追認
			// この同期境界で所有する値、または確定状態の検証値です
			auto journal = WholeTextBoundedJournal::reopen( std::move( file), descriptor) ; /* tail不参照 */
			auto body = s.readBody( *journal, *entry.result) ; /* 唯一buffer */
			return std::shared_ptr<const WholeTextBoundedText>(
				new WholeTextBoundedText( descriptor, std::make_shared<const std::string>( std::move( body)), true)) ;
		} ;
		try {
			// 有効Jの破損はそのbodyだけを利用不能にします
			if( record.effective) {
				record.effectiveText = restore( *record.effective) ;
			}
		} catch( ...) {
			s.blocked = true ;
			record.effectiveText.reset() ;
		}
		try {
			// 最新prefixの破損で別Jの有効全文を消しません
			if( record.latest) {
				// この条件の対象だけを処理し、確定済みの所有状態を保ちます
				if( record.effective && record.effective->journalKey == record.latest->journalKey) {
					require( detail::encodeBoundedDescriptor( *record.latest) ==
							detail::encodeBoundedDescriptor( *record.effective)) ;
					record.latestText = record.effectiveText ;
				} else {
					record.latestText = restore( *record.latest) ;
				}
			}
		} catch( ...) {
			s.blocked = true ;
			record.latestText.reset() ;
			record.prefixSelectionEligible = false ;
		}
		// この条件の対象だけを処理し、確定済みの所有状態を保ちます
		if( record.inFlight) {
			interrupted = true ;
			const auto flight = *record.inFlight ; /* 元予約を保存 */
			const auto owner = s.find( flight.reservationKey) ; /* canonicalだけ */
			record.latestState = WholeTextBoundedAttemptState::INTERRUPTED ;
			record.latestRequest = flight.request ;
			record.latestTerminal = WholeTextBoundedResult{} ;
			record.latestTerminal->request = flight.request ;
			record.latestTerminal->errorKind = ErrorKind::INTERNAL ;
			record.latest.reset() ;
			record.latestText.reset() ;
			record.pcmObserved = false ;
			record.bindingObserved = false ;
			record.prefixSelectionEligible = false ;
			record.inFlight.reset() ;
			// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
			if( owner && owner->descriptor) {
				try {
					auto descriptor = *owner->descriptor ; /* canonical cursorだけ */
					descriptor.state = WholeTextBoundedAttemptState::INTERRUPTED ;
					record.latestText = restore( descriptor) ;
					record.latest = descriptor ;
					record.latestTerminal = descriptor.result ;
					record.pcmObserved = true ;
					record.bindingObserved = true ;
					record.prefixSelectionEligible = descriptor.result.committed.count > 0 ;
				} catch( ...) {
					s.blocked = true ;
				}
			}
		}
	}
	// 復元Interruptedのmetadataが確定してから前の不要prefixを回収します
	if( interrupted) {
		HistorySaveReceipt receipt ; /* scalar */
		try {
			s.commitSnapshot( recovered, receipt) ;
			require( receipt.committed) ;
			durable = std::move( recovered) ;
		} catch( ...) {
			s.blocked = true ; /* 元in_flightと両ownerを保持し再試行や昇格をしません */
			return ;
		}
	} else {
		durable = std::move( recovered) ;
	}
	ordinaryCommitted( durable) ;
}
//------------------------------------------------------------------------------
/**
 * @brief Pendingと全quotaを確定します
 * @param[in,out] durable 正本
 * @param[in] request 全要求
 * @param[in] stop 停止
 * @return 生成前の一回予約
 * @see koefude::WholeTextPersistenceStore::reserve
 */
//------------------------------------------------------------------------------
WholeTextBoundedStorageBudget WholeTextPersistenceStore::reserve( HistorySnapshot					 &durable,
																 const WholeTextCorrectionJobRequest &request,
																 std::stop_token					  stop)
{
	auto &s = *impl_ ; // 一worker
	checkCancelled( stop) ;
	require( !s.blocked && !s.reservation, HistoryErrorKind::BUSY) ;
	validateWholeTextCorrectionRequest( request.source, request.request) ;
	auto &saved = entryFor( durable, request.request.source) ; // 保存済みsource
	require( detail::sameTranscriptOwner( saved.result, request.source) && saved.result->pcmIdentity &&
				findWholeTextBoundedRuntime( request.request.profileId, request.request.profileSha256) &&
				request.request.operationId >= durable.nextOperationId && request.request.operationId < UINT64_MAX,
			HistoryErrorKind::CORRUPT) ;
	std::uint32_t previousCount = 0 ; // residual含む全J数
	std::uint64_t previousBytes = 0 ; // 同sourceの旧payload
	// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
	for( const auto &owner : s.owners) {
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( owner && owner->request.source.historyId == saved.id) {
			++previousCount ;
			previousBytes = add( previousBytes,
								owner->journalCharge && owner->journalCharge->active ? owner->journalCharge->bytes : 0) ;
		}
	}
	require( previousCount <= 1, HistoryErrorKind::TOO_LARGE) ;
	const auto retained = retainedText( durable, request.request.source.historyId) ; // 全保持text
	s.metadataExisting = s.metadataBytes() ;
	const auto measured = measureEncodedHistory( durable) ; // 同field計数sink
	// この同期境界で所有する値、または確定状態の検証値です
	const auto metadataUpper = add( rounded( add( add( measured, 32), 8192ULL * durable.entries.size()), s.allocationUnit),
								   2 * rounded( 4096, s.allocationUnit)) ; // full新snapshotとowner/temp
	std::uint64_t backup = 0 ; // 未移行backup候補
	// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
	if( fileBytes( s.root / L"history.pre-v7.bin") == 0) {
		backup = fileBytes( s.root / L"history.bin") ;
	}
	const auto upper = add( metadataUpper, backup) ; // migrationも事前計上
	// この同期境界で所有する値、または確定状態の検証値です
	const auto other =
		s.ledger->bytes >= previousBytes ? s.ledger->bytes - previousBytes : UINT64_MAX ; // 全旧metadata/residual
	detail::WholeTextPersistenceAmounts amounts{previousBytes,
												25231360,
												other,
												upper,
												s.ledger->bytes,
												retained,
												static_cast<std::uint32_t>( durable.entries.size()),
												0} ; // checked入力
	require( detail::persistenceAmountsFit( amounts, s.hooks ? s.hooks->operationLimit : 67108864,
										  s.hooks ? s.hooks->historyLimit : 536870912,
										  s.hooks ? s.hooks->textLimit : 16777216),
			HistoryErrorKind::TOO_LARGE) ;
	// 予約前にfallback ackと全metadata所有値を確保します
	auto completion =
		std::shared_ptr<WholeTextBoundedPersistenceCompletion>( new WholeTextBoundedPersistenceCompletion) ; // 一ack
	completion->source_ = request.source ;
	completion->request_ = request.request ;
	completion->record_ = saved.bounded ;
	// この同期境界で所有する値、または確定状態の検証値です
	auto failed = std::shared_ptr<WholeTextBoundedPersistenceCompletion>(
		new WholeTextBoundedPersistenceCompletion) ; // 事前失敗ack
	failed->source_ = request.source ;
	failed->request_ = request.request ;
	failed->record_ = saved.bounded ;
	failed->record_.latestRequest = request.request ;
	failed->record_.latestTerminal = WholeTextBoundedResult{} ;
	failed->record_.latestTerminal->request = request.request ;
	failed->record_.latestState = WholeTextBoundedAttemptState::FAILURE ;
	failed->record_.latest.reset() ;
	failed->record_.latestText.reset() ;
	failed->record_.inFlight.reset() ;
	failed->record_.prefixSelectionEligible = false ;
	auto owner = std::make_shared<Owner>() ; // 固定slot候補
	owner->root = s.root ;
	owner->request = request.request ;
	owner->hooks = s.hooks ;
	// この同期境界で所有する値、または確定状態の検証値です
	const auto random = BCryptGenRandom( nullptr, owner->key.data(), static_cast<ULONG>( owner->key.size()),
										BCRYPT_USE_SYSTEM_PREFERRED_RNG) ; // OS乱数だけ
	require( random >= 0, HistoryErrorKind::INTERNAL, static_cast<std::uint32_t>( random)) ;
	require( !s.find( owner->key)) ;
	WholeTextBoundedStorageUsage usage ; // 全量/上界
	usage.previousJournalBytes = previousBytes ;
	usage.previousJournals = previousCount ;
	usage.operationOtherBytes = other ;
	usage.metadataUpperBytes = upper ;
	usage.retainedTextBytes = retained ;
	WholeTextBoundedStorageBudget budget( s.root, s.ledger, usage) ; // ここで全quota取得
	owner->journalCharge = budget.charge() ;
	owner->journalCharge->journalKey = owner->key ;
	owner->metadataCharge = std::make_shared<TransferableJournalCharge>() ;
	owner->metadataCharge->ledger = s.ledger ;
	owner->metadataCharge->bytes = upper ;
	owner->metadataCharge->active = true ;
	owner->journalCharge->bytes -= upper ; // 二重課金なしでmetaを分割移管
	// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
	if( s.hooks) {
		owner->journalCharge->testIo = s.hooks->journal ;
	}
	HistorySnapshot candidate = durable ; // 予約前候補
	auto		   &pending = entryFor( candidate, request.request.source).bounded ; // 元終端を保ちin_flightだけ追加
	pending.inFlight = WholeTextBoundedInFlight{WholeTextBoundedAttemptState::PENDING, request.request, owner->key} ;
	candidate.nextOperationId = request.request.operationId + 1 ;
	try {
		owner->write() ;
		owner->metadataCharge->durable = true ;
		HistorySaveReceipt receipt ; /* 予約commit */
		s.commitSnapshot( candidate, receipt) ;
		require( receipt.committed) ;
		durable = std::move( candidate) ;
		s.insert( owner) ;
		s.reservation = owner ;
		s.acceptedSource = request.source ;
		s.completion = std::move( completion) ;
		s.failedCompletion = std::move( failed) ;
		s.failedPublished = false ;
		s.reservedMetadataUpper = upper ;
		budget.setCheckpoint( owner) ;
		return budget ;
	} catch( ...) {
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( owner->sequence > 0) {
			owner->metadataCharge->durable = true ;
			owner->journalCharge->durable = true ;
			try {
				s.insert( owner) ;
			} catch( ...) {
				s.blocked = true ;
			}
		}
		throw ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 同じterminalを検証してmetadataを確定します
 * @param[in,out] durable 正本
 * @param[in,out] terminal 一J
 * @param[out] receipt 原子置換結果
 * @param[in] stop precommit停止
 * @return 所有immutable completion。失敗時terminalは非消費
 * @see koefude::WholeTextPersistenceStore::persist
 */
//------------------------------------------------------------------------------
std::shared_ptr<const WholeTextBoundedPersistenceCompletion>
WholeTextPersistenceStore::persist( HistorySnapshot &durable, WholeTextBoundedTerminal &terminal,
								   HistorySaveReceipt &receipt, std::stop_token stop)
{
	auto &s = *impl_ ; // 一worker
	require( s.reservation && s.completion && detail::sameTranscriptOwner( s.acceptedSource, terminal.source)) ;
	const auto owner = s.reservation ; // 同leaseを保持
	// 明示再保存は新しい小ackをI/O前に確保し、以前の公開DTOを不変に保ちます
	if( s.failedPublished) {
		auto next = std::shared_ptr<WholeTextBoundedPersistenceCompletion>(
			new WholeTextBoundedPersistenceCompletion) ; /* 新しい小envelope */
		next->source_ = s.failedCompletion->source_ ;
		next->request_ = s.failedCompletion->request_ ;
		next->record_ = s.failedCompletion->record_ ;
		s.failedCompletion = std::move( next) ;
		s.failedPublished = false ;
	}

	const auto &request = terminal.result.request ; // 元要求
	// 生成中の取消しは元terminalに記録され、保存直前取消しもprefixとして残します
	if( stop.stop_requested() && terminal.result.outcome == WholeTextBoundedOutcome::READY_FOR_PERSISTENCE) {
		terminal.result.outcome = WholeTextBoundedOutcome::CANCELLED ;
		terminal.result.errorKind = ErrorKind::CANCELLED ;
	}
	// 事前失敗ackに元terminalの観測値を設定します。文字列容量は予約前に確保済みです
	s.failedCompletion->record_.latestTerminal = terminal.result ;
	s.failedCompletion->record_.pcmObserved = terminal.result.pcm.sampleCount != 0 ;
	s.failedCompletion->record_.bindingObserved = terminal.journal && terminal.journal->sealed() ;
	s.failedCompletion->record_.latestState = terminal.result.outcome == WholeTextBoundedOutcome::CANCELLED
												  ? WholeTextBoundedAttemptState::CANCELLED
												  : WholeTextBoundedAttemptState::FAILURE ;
	// 明示discard後の保存失敗でも同一IDの既知Jを再openできます
	if( !terminal.journal && owner->descriptor) {
		// この同期境界で所有する値、または確定状態の検証値です
		auto file = TransferableJournalFile::reopenKnown( s.root, owner->key, owner->location, owner->identity,
														 owner->journalCharge) ;
		terminal.journal = WholeTextBoundedJournal::reopen( std::move( file), *owner->descriptor) ;
	}
	require( request.source == owner->request.source && request.operationId == owner->request.operationId &&
			request.profileId == owner->request.profileId && request.profileSha256 == owner->request.profileSha256) ;
	auto &previous = entryFor( durable, request.source) ; // 最新成功正本
	require( previous.bounded.inFlight && previous.bounded.inFlight->reservationKey == owner->key &&
			detail::sameTranscriptOwner( previous.result, terminal.source)) ;
	// terminalの全promotionを同じimmutable headerへ照合します
	if( terminal.journal && terminal.journal->sealed()) {
		const auto &a = terminal.journal->binding().promotion ; // headerが束縛する値
		const auto &b = terminal.result.binding ; // 受領した小値
		require( a.openBinding == b.openBinding && a.planSha256 == b.planSha256 && a.inputSha256 == b.inputSha256 &&
				a.originalBytes == b.originalBytes && a.measures == b.measures && a.measuredBytes == b.measuredBytes &&
				a.recordCount == b.recordCount && a.generationCount == b.generationCount) ;
	}
	HistorySnapshot candidate = durable ; // commit前だけ確保
	auto		   &record = entryFor( candidate, request.source).bounded ; // 別record
	record.latestState = terminal.result.outcome == WholeTextBoundedOutcome::CANCELLED
							 ? WholeTextBoundedAttemptState::CANCELLED
							 : WholeTextBoundedAttemptState::FAILURE ;
	record.latestRequest = request ;
	record.latestTerminal = terminal.result ;
	record.pcmObserved = terminal.result.pcm.sampleCount != 0 ;
	record.bindingObserved = terminal.journal && terminal.journal->sealed() ;
	record.latest.reset() ;
	record.latestText.reset() ;
	record.prefixSelectionEligible = false ;
	record.inFlight.reset() ;
	record.saveErrorPresent = false ;
	record.saveErrorKind = 7 ;
	record.saveErrorCode = 0 ;
	record.saveState = WholeTextBoundedSaveState::COMMITTED ;
	// 確定prefixだけ検証し、currentや未処理原文tailを足しません
	if( record.bindingObserved) {
		require( owner->descriptor && terminal.journal->cursor().count == terminal.result.committed.count &&
				terminal.journal->cursor().coreEnd == terminal.result.committed.coreEnd &&
				terminal.journal->cursor().bodyBytes == terminal.result.committed.bodyBytes &&
				terminal.journal->cursor().bodySha256 == terminal.result.committed.bodySha256 &&
				terminal.journal->headerSha256() == owner->descriptor->headerSha256 &&
				terminal.journal->file().identity() == owner->identity &&
				terminal.journal->binding().request.operationId == request.operationId &&
				terminal.journal->binding().pcm == terminal.result.pcm && terminal.source->pcmIdentity &&
				*terminal.source->pcmIdentity == terminal.result.pcm) ;
		WholeTextBoundedDescriptor descriptor ; // 小さい保存証拠
		descriptor.journalKey = owner->key ;
		descriptor.fileIdentity = owner->identity ;
		descriptor.headerSha256 = terminal.journal->headerSha256() ;
		descriptor.result = terminal.result ;
		descriptor.state = record.latestState ;
		// Completeは全coverage/正常close/全receiptがそろう候補だけです
		if( terminal.result.outcome == WholeTextBoundedOutcome::READY_FOR_PERSISTENCE) {
			descriptor.state = WholeTextBoundedAttemptState::COMPLETE ;
			detail::validateBoundedDescriptor( descriptor) ;
			record.latestState = WholeTextBoundedAttemptState::COMPLETE ;
		}
		std::shared_ptr<const std::string> bodyOwner ; // 再保存は既存bodyを共有
		// 同じkey/cursorの未保存bodyを再利用し追加8MiB bufferを確保しません
		if( s.failedCompletion->record_.latestText && s.failedCompletion->record_.latest &&
		   s.failedCompletion->record_.latest->journalKey == descriptor.journalKey &&
		   s.failedCompletion->record_.latest->result.committed.bodySha256 == descriptor.result.committed.bodySha256) {
			s.verify( *terminal.journal, *terminal.source) ;
			bodyOwner = s.failedCompletion->record_.latestText->body_ ;
		} else {
			auto body = s.readBody( *terminal.journal, *terminal.source) ; /* 唯一のbody確保 */
			bodyOwner = std::make_shared<const std::string>( std::move( body)) ;
		}
		record.latest = descriptor ;
		record.latestText =
			std::shared_ptr<const WholeTextBoundedText>( new WholeTextBoundedText( descriptor, bodyOwner, true)) ;
		record.prefixSelectionEligible = descriptor.result.committed.count > 0 ;
		auto incomplete = descriptor ; // 同じbodyを未保存として別型表示
		incomplete.state = s.failedCompletion->record_.latestState ;
		s.failedCompletion->record_.latest = incomplete ;
		s.failedCompletion->record_.latestText =
			std::shared_ptr<const WholeTextBoundedText>( new WholeTextBoundedText( incomplete, bodyOwner, false)) ;
		s.failedCompletion->record_.prefixSelectionEligible = incomplete.result.committed.count > 0 ;
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( record.latestState == WholeTextBoundedAttemptState::COMPLETE) {
			record.effective = descriptor ;
			record.effectiveText = record.latestText ;
			record.active = WholeTextActiveFullKind::BOUNDED ;
		}
		// 同handleを同volumeへrenameしcopy fallbackしません
		if( owner->location == 1) {
			owner->fault( 20) ;
			terminal.journal->file().renameWithinHistory( L"koefude-journal-" + keyHex( owner->key)) ;
			owner->location = 2 ;
		}
		owner->descriptor = descriptor ;
		owner->state = 2 ;
		owner->write() ;
	} else {
		require( terminal.result.committed.count == 0 && terminal.result.committed.bodyBytes == 0) ;
	}
	require( retainedText( candidate, request.source.historyId) <= 16777216, HistoryErrorKind::TOO_LARGE) ;
	// body/completion/candidateを全確保した後だけatomic保存します
	s.completion->record_ = record ;
	s.completion->source_ = terminal.source ;
	s.completion->request_ = request ;
	// 置換前に到着した取消しは有効全文を変更せずCancelled prefixを保存します
	if( stop.stop_requested() && record.latestState == WholeTextBoundedAttemptState::COMPLETE) {
		record.latestState = WholeTextBoundedAttemptState::CANCELLED ;
		record.latestTerminal->outcome = WholeTextBoundedOutcome::CANCELLED ;
		record.latestTerminal->errorKind = ErrorKind::CANCELLED ;
		record.active = previous.bounded.active ;
		record.effective = previous.bounded.effective ;
		record.effectiveText = previous.bounded.effectiveText ;
		record.latest = s.failedCompletion->record_.latest ;
		record.latest->state = WholeTextBoundedAttemptState::CANCELLED ;
		record.latest->result = *record.latestTerminal ;
		record.latestText = std::shared_ptr<const WholeTextBoundedText>(
			new WholeTextBoundedText( *record.latest, s.failedCompletion->record_.latestText->body_,
									 true)) ; // 同bodyでCancelled証拠だけ
		s.completion->record_ = record ;
	}
	owner->fault( 21) ;
	s.commitSnapshot( candidate, receipt) ;
	require( receipt.committed) ;
	s.completion->receipt_ = receipt ;
	durable = std::move( candidate) ; // atomic成功後noexcept所有移動だけ
	auto completion = std::move( s.completion) ; // 追加allocation0
	// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
	if( terminal.journal) {
		terminal.journal->file().retainDurable() ;
		terminal.journal.reset() ;
	}
	s.reservation.reset() ;
	s.acceptedSource.reset() ;
	s.reservedMetadataUpper = 0 ;
	ordinaryCommitted( durable) ;
	return completion ;
}
//------------------------------------------------------------------------------
/**
 * @brief 事前確保した未保存prefixを返します
 * @param[in] kind 保存分類
 * @param[in] code 元code
 * @return 旧有効全文を保つ未保存ack
 * @see koefude::WholeTextPersistenceStore::failure
 */
//------------------------------------------------------------------------------
std::shared_ptr<const WholeTextBoundedPersistenceCompletion>
WholeTextPersistenceStore::failure( HistoryErrorKind kind, std::uint32_t code) noexcept
{
	auto &ack = impl_->failedCompletion ; /* 予約前owner */
	if( !ack) {
		return {} ;
	}
	// 新ackの確保失敗時は既に公開した旧失敗をそのまま返しownerを保持します
	if( impl_->failedPublished) {
		return ack ;
	}
	ack->record_.saveState = WholeTextBoundedSaveState::SAVE_FAILED ;
	ack->record_.saveErrorPresent = true ;
	ack->record_.saveErrorKind = static_cast<std::uint32_t>( kind) ;
	ack->record_.saveErrorCode = code ;
	impl_->failedPublished = true ;
	return ack ;
}
//------------------------------------------------------------------------------
/**
 * @brief 未完参照を原子的に除去してからcleanupします
 * @param[in,out] durable 正本
 * @param[in] source 元key
 * @param[in] op 元op
 * @param[out] receipt 保存結果
 * @return 独立cleanup結果
 * @see koefude::WholeTextPersistenceStore::discard
 */
//------------------------------------------------------------------------------
WholeTextBoundedCleanupResult WholeTextPersistenceStore::discard( HistorySnapshot		&durable,
																 const ReadingSourceKey &source, std::uint64_t op,
																 HistorySaveReceipt &receipt)
{
	auto	  &s = *impl_ ; // 一worker
	auto	  &old = entryFor( durable, source) ; // 正本
	// この同期境界で所有する値、または確定状態の検証値です
	const bool unsaved = s.reservation && s.reservation->request.source == source &&
						 s.reservation->request.operationId == op ; // terminal受領済みの同lease
	require( unsaved ||
				(!old.bounded.inFlight && old.bounded.latestRequest && old.bounded.latestRequest->operationId == op &&
				 old.bounded.latestState != WholeTextBoundedAttemptState::COMPLETE),
			HistoryErrorKind::BUSY) ;
	HistorySnapshot candidate = durable ; // 参照除去候補
	auto		   &record = entryFor( candidate, source).bounded ; // 有効全文は独立保持
	record.inFlight.reset() ;
	record.latest.reset() ;
	record.latestText.reset() ;
	record.latestTerminal.reset() ;
	record.latestRequest.reset() ;
	record.latestState = WholeTextBoundedAttemptState::IDLE ;
	record.prefixSelectionEligible = false ;
	record.pcmObserved = false ;
	record.bindingObserved = false ;
	record.saveErrorPresent = false ;
	record.saveErrorKind = 7 ;
	record.saveErrorCode = 0 ;
	record.saveState = WholeTextBoundedSaveState::COMMITTED ;
	s.commitSnapshot( candidate, receipt) ;
	require( receipt.committed) ;
	durable = std::move( candidate) ;
	s.reservation.reset() ;
	s.acceptedSource.reset() ;
	s.completion.reset() ;
	s.failedCompletion.reset() ;
	WholeTextBoundedCleanupResult result{true, true, 7, 0, 0} ; // commit後にrollbackしません
	// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
	for( auto &owner : s.owners) {
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( owner && owner->request.source.historyId == source.historyId && !s.referenced( durable, owner->key)) {
			const auto code = s.cleanup( owner) ; /* knownだけ */
			if( code) {
				result.cleanupComplete = false ;
				result.code = code ;
				result.residualBytes =
					add( result.residualBytes, owner->journalCharge->bytes + owner->metadataCharge->bytes) ;
			}
		}
	}
	return result ;
}
//------------------------------------------------------------------------------
/**
 * @brief 普通save前にも全snapshot増加を予約上界へ照合します
 * @param[in] candidate 候補
 * @see koefude::WholeTextPersistenceStore::checkOrdinary
 */
//------------------------------------------------------------------------------
void WholeTextPersistenceStore::checkOrdinary( const HistorySnapshot &candidate)
{
	auto	  &s = *impl_ ; // 一worker
	// この同期境界で所有する値、または確定状態の検証値です
	const auto bytes = rounded( add( measureEncodedHistory( candidate), 32), s.allocationUnit) ; // 全snapshot上界
	// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
	for( const auto &entry : candidate.entries) {
		require( retainedText( candidate, entry.id) <= 16777216, HistoryErrorKind::TOO_LARGE) ;
	}
	// 生成中の普通snapshot増加も予約枠を超えて書きません
	if( s.reservation) {
		require( bytes <= s.reservedMetadataUpper - 2 * rounded( 4096, s.allocationUnit), HistoryErrorKind::TOO_LARGE) ;
		// この同期境界で所有する値、または確定状態の検証値です
		const auto growth = bytes > s.metadataExisting ? bytes - s.metadataExisting : 0 ; // 普通saveの新しい実使用上界
		const auto oldMeta = s.metadataBytes() ; // 全backupを含む現metadata
		const auto operationLimit = s.hooks ? s.hooks->operationLimit : 67108864 ; // 縮小だけ
		const auto historyLimit = s.hooks ? s.hooks->historyLimit : 536870912 ; // 縮小だけ
		require( s.ledger->bytes <= historyLimit && growth <= historyLimit - s.ledger->bytes,
				HistoryErrorKind::TOO_LARGE) ;
		std::uint64_t operation = 25231360 ; // 新Jと対象の旧J/全metadataを数えます
		// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
		for( const auto &owner : s.owners) {
			// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
			if( owner && owner != s.reservation &&
			   owner->request.source.historyId == s.reservation->request.source.historyId) {
				operation = add( operation, owner->journalCharge->bytes) ;
			}
		}
		operation = add( operation, add( add( oldMeta, growth), s.reservedMetadataUpper)) ;
		require( operation <= operationLimit, HistoryErrorKind::TOO_LARGE) ;
	} else {
		const auto current = s.metadataBytes() ; /* 現全量 */
		require( current <= 536870912 && bytes <= 536870912 - current && s.ledger->bytes <= 536870912 - bytes,
				HistoryErrorKind::TOO_LARGE) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 成功した普通save後に既知の参照外Jだけを回収します
 * @param[in] durable 新正本
 * @see koefude::WholeTextPersistenceStore::ordinaryCommitted
 */
//------------------------------------------------------------------------------
void WholeTextPersistenceStore::ordinaryCommitted( const HistorySnapshot &durable) noexcept
{
	auto &s = *impl_ ; // 置換後は失敗で巻き戻しません
	try {
		// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
		for( auto &owner : s.owners) {
			// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
			if( owner && owner != s.reservation && !s.referenced( durable, owner->key)) {
				const auto code = s.cleanup( owner) ; /* 確定参照外だけ */
				if( code) {
					s.blocked = true ;
				}
			}
		}
		// 現snapshotと耐久ownerへ予約を移し、closeだけでは解放しません
		std::uint64_t total = s.metadataBytes() ; // 全実metadata
		// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
		for( auto &owner : s.owners) {
			// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
			if( owner) {
				// この同期境界で所有する値、または確定状態の検証値です
				const auto minimum = 2 * rounded( 4096, s.allocationUnit) ; /* owner/current+temp上界 */
				if( owner != s.reservation && owner->metadataCharge && owner->metadataCharge->bytes > minimum) {
					owner->metadataCharge->bytes = minimum ;
					owner->write() ;
				}
				total = add( total, add( owner->journalCharge ? owner->journalCharge->bytes : 0,
									   owner->metadataCharge ? owner->metadataCharge->bytes : 0)) ;
			}
		}
		s.ledger->bytes = total ;
		s.ledger->entries = static_cast<std::uint32_t>( durable.entries.size()) ;
		s.metadataExisting = s.metadataBytes() ;
	} catch( ...) {
		s.blocked = true ; /* residualと元chargeを保持し、確定済みmetadataを戻しません */
	}
}
} // namespace koefude
