//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file temporary_store.cpp
 * @brief 一時PCMと声の特徴を専用ACLとOSの終了時削除で管理します
 * @version 1.0.0
 * @date 2026/09/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/10 有界bufferの追記・部分読込みと一時file回収を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 宣言の役割を明記
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1285の有界逐次補正を接続
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1409の役割・引数・戻り値と所有寿命を具体化
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
#include "platform/temporary_store.h"

#include <span>
#include <utility>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>

// Win32基本型の後に権限・乱数・known folderの宣言を読み込む
#include <bcrypt.h>
#include <objbase.h>
#include <sddl.h>
#include <shlobj.h>

#include "platform/win_helpers.h"

namespace koefude {
namespace {
constexpr DWORD K_REJECTED_ATTRIBUTES = FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE |
										FILE_ATTRIBUTE_RECALL_ON_OPEN |
										FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS ; // 自動通信と外部参照を避ける属性
constexpr std::wstring_view K_HEX = L"0123456789abcdef" ; // 乱数名に使う文字
/** @brief 現ユーザーとSYSTEMだけへ継承付きのアクセス権を設定する所有descriptorです */
class PrivateSecurity		final {
	  public:
	  //------------------------------------------------------------------------------
	  /**
	   * @brief processのユーザーSIDからprivate ACLを作ります
	   */
	  //------------------------------------------------------------------------------
	  PrivateSecurity()
	  {
		  HANDLE tokenHandle = nullptr ; // このscopeで解放するprocess token
		  // 管理者権限を要求せず、自分のtokenの読取りだけを行う
		  if( !OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &tokenHandle)) {
			  throw OperationError( ErrorKind::TEMPORARY_STORAGE, GetLastError()) ;
		  }
		  const UniqueHandle token( tokenHandle) ;
		  DWORD				 needed = 0 ; // TOKEN_USERに必要なbyte数
		  // TOKEN_USERの必要容量照会結果。errorと必要byte数を併せて検証する
		  const BOOL		 queried = GetTokenInformation( token.get(), TokenUser, nullptr, 0, &needed) ;
		  // 想定外の戻り値や過大tokenに基づくallocationを避ける
		  if( queried || GetLastError() != ERROR_INSUFFICIENT_BUFFER || needed == 0 || needed > 65536) {
			  throw OperationError( ErrorKind::TEMPORARY_STORAGE, GetLastError()) ;
		  }
		  std::vector<std::uint8_t> information( needed) ; // GetTokenInformation用の所有buffer
		  // 取得中の失敗もprivate ACLなしの保存へfallbackしない
		  if( !GetTokenInformation( token.get(), TokenUser, information.data(), needed, &needed)) {
			  throw OperationError( ErrorKind::TEMPORARY_STORAGE, GetLastError()) ;
		  }
		  // OSが書いたTOKEN_USERを同期解釈する境界。SIDはinformationの寿命中だけ借用する
		  const auto *user = reinterpret_cast<const TOKEN_USER *>( information.data()) ;
		  LPWSTR	  sid = nullptr ; // LocalFreeするSID文字列
		  // SID変換失敗時も共用領域へ権限を広げない
		  if( !ConvertSidToStringSidW( user->User.Sid, &sid)) {
			  throw OperationError( ErrorKind::TEMPORARY_STORAGE, GetLastError()) ;
		  }
		  const std::unique_ptr<void, decltype(&LocalFree)> ownedSid( sid, LocalFree) ;
		  // SYSTEMと現在利用者だけに継承可能な完全権限を与える所有SDDL
		  const std::wstring   sddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;" + std::wstring( sid) + L")" ;
		  PSECURITY_DESCRIPTOR descriptor = nullptr ; // LocalFreeする自己相対descriptor
		  // Windowsが検証したdescriptorだけをCreateFile/CreateDirectoryへ渡す
		  if( !ConvertStringSecurityDescriptorToSecurityDescriptorW( sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
			  throw OperationError( ErrorKind::TEMPORARY_STORAGE, GetLastError()) ;
		  }
		  descriptor_.reset( descriptor) ;
	  }
	  //------------------------------------------------------------------------------
	  /**
	   * @brief 新規作成APIへ渡す属性を返します
	   * @return 本instanceの寿命中だけ有効な借用属性
	   */
	  //------------------------------------------------------------------------------
	  [[nodiscard]] SECURITY_ATTRIBUTES attributes() const noexcept
	  {
		  return {sizeof(SECURITY_ATTRIBUTES), descriptor_.get(), FALSE} ;
	  }

	  private:
	  /// private ACLの所有権
	  std::unique_ptr<void, decltype(&LocalFree)> descriptor_{nullptr, LocalFree} ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief Windowsのユーザー専用LocalAppDataを取得します
 * @return 一時処理専用directoryの絶対path
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::filesystem::path defaultDirectory()
{
	const HRESULT com = CoInitializeEx( nullptr, COINIT_MULTITHREADED) ; // このworkerのCOM初期化
	// 既存の別apartmentを壊してknown folder処理を続けない
	if( FAILED(com)) {
		throw OperationError( ErrorKind::TEMPORARY_STORAGE, static_cast<std::uint32_t>( com)) ;
	}
	PWSTR		  raw = nullptr ; // 成否にかかわらずCoTaskMemFreeするpath
	// 現在利用者のLocalAppDataのpathを取得したHRESULT
	const HRESULT status = SHGetKnownFolderPath( FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw) ;
	const std::unique_ptr<void, decltype(&CoTaskMemFree)> owned( raw, CoTaskMemFree) ;
	CoUninitialize() ; // 値だけを保持し、COM参照を別のthreadへ渡さない
	// system側で取得できないpathを環境変数の推測へ置き換えない
	if( FAILED(status) || !raw) {
		throw OperationError( ErrorKind::TEMPORARY_STORAGE, static_cast<std::uint32_t>( status)) ;
	}
	return std::filesystem::path( raw) / L"koefude" / L"temporary" ;
}

//------------------------------------------------------------------------------
/**
 * @brief local属性を上位から確認して専用folderを準備します
 * @param[in] input 値として指定された保存先
 * @param[in] security 新規folderだけへ適用するprivate属性
 * @param[in] create trueなら存在しないdirectoryを作成します
 * @return 正規化した絶対path
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::filesystem::path prepareDirectory( const std::filesystem::path &input,
													 const PrivateSecurity &security, const bool create)
{
	const auto native = input.native() ; // 未解釈のUTF-16入力値
	// 外部参照、device namespace、NUL、過大pathをOSのfile APIへ渡さない
	if( native.empty() || native.size() > 32700 || native.starts_with( L"\\\\") ||
	   native.find( L"://") != std::wstring::npos || native.find( L'\0') != std::wstring::npos) {
		throw OperationError( ErrorKind::TEMPORARY_STORAGE) ;
	}
	const auto	absolute = std::filesystem::absolute( input).lexically_normal() ; // 検証する絶対path
	const auto	root = absolute.root_path() ; // drive種別を検証するroot
	const DWORD drive = GetDriveTypeW( root.c_str()) ; // network driveを拒否するOS情報
	// 書込み可能なlocal drive上のfolderに限定し、drive自体とADSも拒否する
	if( absolute.native().size() < 4 || absolute.native()[1] != L':' ||
	   absolute.native().find( L':', 2) != std::wstring::npos || absolute == root ||
	   (drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE && drive != DRIVE_RAMDISK)) {
		throw OperationError( ErrorKind::TEMPORARY_STORAGE) ;
	}
	auto current = root ; // 上位から確認しているfolder
	// 未検証の祖先を跨いでcreate_directoriesせず、各構成要素を順に確認する
	for( const auto &component : absolute.relative_path()) {
		current /= component ;
		DWORD attributes = GetFileAttributesW( current.c_str()) ; // 書込み前の属性
		// 既存folderを再利用し、ない場合だけprivate ACLで作成する
		if( attributes == INVALID_FILE_ATTRIBUTES) {
			const DWORD error = GetLastError() ; // 存在なしと権限失敗を区別する診断
			// 読込専用確認時や未知の異常では新しいfolderを作らない
			if( !create || (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)) {
				throw OperationError( ErrorKind::TEMPORARY_STORAGE, error) ;
			}
			auto access = security.attributes() ; // CreateDirectoryが同期借用する属性
			// 他processとの作成競合以外は保存先準備の失敗として返す
			if( !CreateDirectoryW( current.c_str(), &access) && GetLastError() != ERROR_ALREADY_EXISTS) {
				throw OperationError( ErrorKind::TEMPORARY_STORAGE, GetLastError()) ;
			}
			attributes = GetFileAttributesW( current.c_str()) ;
		}
		// reparse・同期属性・非directoryを保存の祖先として使わない
		if( attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
		   (attributes & K_REJECTED_ATTRIBUTES) != 0) {
			throw OperationError( ErrorKind::TEMPORARY_STORAGE) ;
		}
	}
	return absolute ;
}

} // namespace

/** @brief 同期I/Oのhandleと実際に書き込めた長さを所有します */
class TemporaryFile::Impl final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 検証済みの一時fileを作ります
	 * @param[in] requested 空または隔離した保存先
	 */
	//------------------------------------------------------------------------------
	explicit Impl( const std::filesystem::path &requested)
	{
		const PrivateSecurity security ; // 現ユーザーとSYSTEMだけの新規file権限
		directory = prepareDirectory( requested.empty() ? defaultDirectory() : requested, security, true) ;
		std::array<std::uint8_t, 16> random{} ; // 128bitのOS乱数
		// 予測不能な一時file名に使うOS乱数の生成結果
		const auto					 status =
			BCryptGenRandom( nullptr, random.data(), static_cast<ULONG>( random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) ;
		// 乱数を作れない場合に予測可能な一時名を使用しない
		if( status < 0) {
			throw OperationError( ErrorKind::TEMPORARY_STORAGE, static_cast<std::uint32_t>( status)) ;
		}
		std::wstring name = L"koefude-work-" ; // 自分の領域内だけの一時名
		// 乱数の各byteをpath構文を含まない16進文字にする
		for( const auto byte : random) {
			name += K_HEX[byte >> 4] ;
			name += K_HEX[byte & 15] ;
		}
		const auto path = directory / name ; // constructor内だけで使う所有path
		auto	   access = security.attributes() ; // 同期CreateFileへ借用するACL
		file.reset( CreateFileW( path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, &access, CREATE_NEW,
							   FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_OPEN_REPARSE_POINT,
							   nullptr)) ;
		// 既存fileを開かず、作成失敗を別の共用領域へfallbackしない
		if( file.get() == INVALID_HANDLE_VALUE) {
			throw OperationError( ErrorKind::TEMPORARY_STORAGE, GetLastError()) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 元fileの指定位置へ移動します
	 * @param[in] offset 検証済みbyte位置
	 */
	//------------------------------------------------------------------------------
	void seek( const std::uint64_t offset) const
	{
		LARGE_INTEGER position{} ; // 同期file pointerへ渡す符号付き位置
		position.QuadPart = static_cast<LONGLONG>( offset) ;
		// 別の位置へ読み書きして成功扱いにしない
		if( !SetFilePointerEx( file.get(), position, nullptr, FILE_BEGIN)) {
			throw OperationError( ErrorKind::TEMPORARY_STORAGE, GetLastError()) ;
		}
	}
	/// 最終handle closeとprocess終了のどちらでもOSがfileを削除する
	UniqueHandle		  file ;
	/// 空き容量だけを照会するユーザー専用directory
	std::filesystem::path directory ;
	/// 全て成功した追記の総byte数
	std::uint64_t		  size = 0 ;
	/// 直近の空き容量照会から書込みを認めるbyte数
	std::uint64_t		  allowance = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief localの専用ACLで乱数名のfileを作ります
 * @param[in] directory 空はLocalAppData/koefude/temporary。試験は隔離先を指定できます
 * @throws OperationError 保存先不正・権限・空き容量の異常
 * @see koefude::TemporaryFile::TemporaryFile(const std::filesystem::path &)
 */
//------------------------------------------------------------------------------
TemporaryFile::TemporaryFile( const std::filesystem::path &directory) : impl_( std::make_unique<Impl>( directory))
{
}

//------------------------------------------------------------------------------
/**
 * @brief handleを閉じ、一時fileをOSへ回収させます
 * @see koefude::TemporaryFile::~TemporaryFile
 */
//------------------------------------------------------------------------------
TemporaryFile::~TemporaryFile() = default ;

//------------------------------------------------------------------------------
/**
 * @brief 書込み済みbyte数を返します
 * @return 成功した追記の総byte数
 * @see koefude::TemporaryFile::size
 */
//------------------------------------------------------------------------------
std::uint64_t TemporaryFile::size() const noexcept
{
	return impl_->size ;
}

//------------------------------------------------------------------------------
/**
 * @brief 元のdataの後へ全byteを追加します
 * @param[in] bytes 同期借用する有限buffer
 * @throws OperationError 失敗後のfileは利用せず破棄してください
 * @see koefude::TemporaryFile::append
 */
//------------------------------------------------------------------------------
void TemporaryFile::append( const std::span<const std::byte> bytes)
{
	// Win32のfile位置を符号付き64bitで表せる範囲だけを扱う
	if( bytes.size() > static_cast<std::uint64_t>( std::numeric_limits<LONGLONG>::max()) - impl_->size) {
		throw OperationError( ErrorKind::TEMPORARY_STORAGE, ERROR_FILE_TOO_LARGE) ;
	}
	impl_->seek( impl_->size) ;
	std::size_t offset = 0 ; // 今回書き込んだbyte数
	// OSの部分書込みを扱い、全bufferを書けた時だけsizeを確定する
	while( offset < bytes.size()) {
		// 今回のPCM追記で要求する最大64KiBのbyte数
		const DWORD count = static_cast<DWORD>( std::min<std::size_t>( 65536, bytes.size() - offset)) ;
		// 大きな追記bufferでも最大8MiBごとに空き量を再確認する
		if( count > impl_->allowance) {
			ULARGE_INTEGER available{} ; // 現ユーザーが利用できる実際の空きbyte数
			// volume照会の失敗を無制限な空き領域として扱わない
			if( !GetDiskFreeSpaceExW( impl_->directory.c_str(), &available, nullptr, nullptr)) {
				throw OperationError( ErrorKind::TEMPORARY_STORAGE, GetLastError()) ;
			}
			constexpr std::uint64_t K_RESERVE = 512ULL * 1024 * 1024 ; // 他process用の余裕
			// 減算前に余裕を確認し、実際に書くblockの空きがない場合は停止する
			if( available.QuadPart <= K_RESERVE || count > available.QuadPart - K_RESERVE) {
				throw OperationError( ErrorKind::TEMPORARY_STORAGE, ERROR_DISK_FULL) ;
			}
			impl_->allowance = std::min<std::uint64_t>( 8ULL * 1024 * 1024, available.QuadPart - K_RESERVE) ;
		}
		DWORD written = 0 ; // 今回の実書込みbyte数
		// 実際の容量不足とI/O異常も元codeで通知する
		if( !WriteFile( impl_->file.get(), bytes.data() + offset, count, &written, nullptr) || written == 0) {
			throw OperationError( ErrorKind::TEMPORARY_STORAGE, GetLastError()) ;
		}
		offset += written ;
		impl_->allowance -= written ;
	}
	impl_->size += bytes.size() ;
}

//------------------------------------------------------------------------------
/**
 * @brief 有効範囲のbyteを部分読込みします
 * @param[in] offset file先頭からのbyte数
 * @param[out] bytes 成功時は全域を設定。失敗時は内容未確定
 * @throws OperationError 範囲外・I/O異常
 * @see koefude::TemporaryFile::read
 */
//------------------------------------------------------------------------------
void TemporaryFile::read( const std::uint64_t offset, const std::span<std::byte> bytes) const
{
	// 全量allocationをせず、要求された範囲だけを読み込む
	if( offset > impl_->size || bytes.size() > impl_->size - offset) {
		throw OperationError( ErrorKind::TEMPORARY_STORAGE, ERROR_INVALID_PARAMETER) ;
	}
	impl_->seek( offset) ;
	std::size_t readTotal = 0 ; // 今回の出力bufferへ書いたbyte数
	// 固定blockで進め、予期しないEOFを成功にしない
	while( readTotal < bytes.size()) {
		// 今回のPCM読込みで要求する最大64KiBのbyte数
		const DWORD count = static_cast<DWORD>( std::min<std::size_t>( 65536, bytes.size() - readTotal)) ;
		DWORD		read = 0 ; // 今回の実読込みbyte数
		// 同期読込みが失敗した場合は部分dataを利用させない
		if( !ReadFile( impl_->file.get(), bytes.data() + readTotal, count, &read, nullptr) || read == 0) {
			throw OperationError( ErrorKind::TEMPORARY_STORAGE, GetLastError()) ;
		}
		readTotal += read ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 専用一時PCMを作ります
 * @param[in] directory 空は既定、試験は隔離先
 * @see koefude::PcmStorage::PcmStorage
 */
//------------------------------------------------------------------------------
PcmStorage::PcmStorage( const std::filesystem::path &directory) : file_( directory)
{
}

//------------------------------------------------------------------------------
/**
 * @brief 全PCMのsample数を返します
 * @return 16kHz monoの総数
 * @see koefude::PcmStorage::size
 */
//------------------------------------------------------------------------------
std::uint64_t PcmStorage::size() const noexcept
{
	return file_.size() / sizeof(float) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 指定範囲のPCMだけを出力bufferへ読み込みます
 * @param[in] offset 元音声基準の先頭sample番号
 * @param[out] destination 借用出力。成功時は全要素、失敗時は途中まで変更される場合があります
 * @note 同じworkerで逐次使用します。並行呼出しは禁止します
 * @see koefude::PcmStorage::read
 */
//------------------------------------------------------------------------------
void PcmStorage::read( const std::uint64_t offset, const std::span<float> destination) const
{
	// byte換算前にPCMの有効範囲を検証する
	if( offset > size() || destination.size() > size() - offset) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	file_.read( offset * sizeof(float), std::as_writable_bytes( destination)) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 検証・正規化済みsampleを連続して追加します
 * @param[in] samples 有限な-1～1のPCM
 * @throws OperationError 不正PCM・整数表現・一時領域の異常
 * @see koefude::PcmStorage::append
 */
//------------------------------------------------------------------------------
void PcmStorage::append( const std::span<const float> samples)
{
	// 全sampleの表現境界と値域を検証し、不正PCMを一時領域へ蓄積しない
	if( samples.size() > K_MAXIMUM_TIMELINE_SAMPLES - size() || !std::all_of( samples.begin(), samples.end(),
		//------------------------------------------------------------------------------
		/**
		 * @brief 書込み前のPCM一sampleが正規化振幅の範囲内か検証します
		 * @param[in] value float一sampleの振幅。許容範囲は両端を含む[-1, 1]です
		 * @return 有限かつ許容範囲内ならtrue、NaN・無限大・範囲外ならfalse
		 * @note sampleの補正、clamp、fileへの保存は行いません
		 */
		//------------------------------------------------------------------------------
		[]( const float value)
		{
			return std::isfinite( value) &&
				   value >= -1.0F && value <= 1.0F ;
		})) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	file_.append( std::as_bytes( samples)) ;
}

//------------------------------------------------------------------------------
/**
 * @brief file作成前の失敗だけは予約を返します。残存fileは返しません
 * @see koefude::TransferableJournalCharge::~TransferableJournalCharge
 */
//------------------------------------------------------------------------------
TransferableJournalCharge::~TransferableJournalCharge() noexcept
{
	// 削除失敗のquotaはU4台帳へ残します
	if( cleanupCode == 0 && !durable) {
		release() ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 作成fileがない/削除済みの予約だけを返します
 * @see koefude::TransferableJournalCharge::release
 */
//------------------------------------------------------------------------------
void TransferableJournalCharge::release() noexcept
{
	// 失敗削除の予約を返しません
	if( !active || !ledger) {
		return ;
	}
	const std::lock_guard lock( ledger->mutex) ; // I/Oを含まない有限更新
	ledger->bytes -= bytes ;
	ledger->entries -= entryDelta ;
	active = false ;
}
/** @brief 一つの通常local fileとその削除責任です */
class TransferableJournalFile::Impl final {
	public:
	/// 検証済みhistory root
	std::filesystem::path					   root ;
	/// 自分がCREATE_NEWした一file
	std::filesystem::path					   path ;
	/// 他processへ共有しないhandle
	UniqueHandle							   file ;
	/// 失敗削除の予約
	std::shared_ptr<TransferableJournalCharge> charge ;
	/// 固定extent
	std::uint64_t							   size = 0 ;
	/// 耐久採用後はdestructorでcloseのみ
	bool									   retained = false ;
	/// 再試行禁止
	bool									   cleanupAttempted = false ;
	/// 元結果
	TransferableJournalCleanup				   cleanupResult ;
	//------------------------------------------------------------------------------
	/**
	 * @brief private ACLと乱数名で一fileだけ作成します
	 * @param[in] requested history root
	 * @param[in] quota 取得済み予約
	 */
	//------------------------------------------------------------------------------
	Impl( const std::filesystem::path &requested, std::shared_ptr<TransferableJournalCharge> quota)
		: charge( std::move( quota))
	{
		// test専用依存はOSの大きいJを作りません
		if( charge && charge->testIo) {
			root = requested ;
			path = root / L"logical-journal" ;
			return ;
		}
		const PrivateSecurity security ; // PCMと同じprivate実装
		root = prepareDirectory( requested.empty() ? defaultDirectory().parent_path() / L"history" : requested, security,
								true) ;
		// この同期境界で所有する値、または確定状態の検証値です
		const auto staging = prepareDirectory( root / L"whole-text-staging", security, true) ; // 固定子
		// この同期境界で所有する値、または確定状態の検証値です
		std::array<std::uint8_t, 16> random = charge ? charge->journalKey : WholeTextJournalKey{} ; // Reserved key
		// U3単独APIの旧互換だけはここで乱数を生成します
		if( random == WholeTextJournalKey{}) {
			// この同期境界で所有する値、または確定状態の検証値です
			const auto code = BCryptGenRandom( nullptr, random.data(), static_cast<ULONG>( random.size()),
											  BCRYPT_USE_SYSTEM_PREFERRED_RNG) ; // 名前へ原音/pathを使いません
			// 乱数失敗を予測可能名へfallbackしません
			if( code < 0) {
				throw OperationError( ErrorKind::TEMPORARY_STORAGE, static_cast<std::uint32_t>( code)) ;
			}
		}
		std::wstring name = L"koefude-journal-" ; // 自分の領域内だけ
		// 全乱数byteをpath構文なしの16進へ変換します
		for( const auto byte : random) {
			name += K_HEX[byte >> 4] ;
			name += K_HEX[byte & 15] ;
		}
		path = staging / name ;
		auto access = security.attributes() ; // CreateFileの同期borrow
		file.reset( CreateFileW( path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, &access, CREATE_NEW,
							   FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)) ;
		// 作成失敗時は既存fileの削除責任を取得しません
		if( file.get() == INVALID_HANDLE_VALUE) {
			path.clear() ;
			throw OperationError( ErrorKind::TEMPORARY_STORAGE, GetLastError()) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 既知keyの二固定候補だけをopenします
	 * @param[in] requested root
	 * @param[in] quota charge
	 * @param[in] key 元key
	 * @param[in] location 固定位置
	 */
	//------------------------------------------------------------------------------
	Impl( const std::filesystem::path &requested, std::shared_ptr<TransferableJournalCharge> quota,
		 const WholeTextJournalKey &key, unsigned location)
		: root( requested), charge( std::move( quota)), retained( true)
	{
		const PrivateSecurity security ; // private root検証
		static_cast<void>( prepareDirectory( root, security, false)) ;
		std::wstring name = L"koefude-journal-" ; // 同じ固定名
		// 全key byteを固定小文字hexへ変換します
		for( const auto b : key) {
			name += K_HEX[b >> 4] ;
			name += K_HEX[b & 15] ;
		}
		path = (location == 1 ? root / L"whole-text-staging" : root) / name ;
		// testでは保存済みlogical ownerを再使用します
		if( charge->testIo) {
			size = 25231360 ;
			return ;
		}
		file.reset( CreateFileW( path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, OPEN_EXISTING,
							   FILE_FLAG_OPEN_REPARSE_POINT, nullptr)) ;
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( file.get() == INVALID_HANDLE_VALUE) {
			throw OperationError( ErrorKind::TEMPORARY_STORAGE, GetLastError()) ;
		}
		FILE_ATTRIBUTE_TAG_INFO attributes{} ; // 同じhandleの安全属性
		FILE_STANDARD_INFO		standard{} ; // 実allocationとextent
		constexpr DWORD			REJECTED = FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE |
								   FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_RECALL_ON_OPEN |
								   FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE ; // 同期通信なし
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( !GetFileInformationByHandleEx( file.get(), FileAttributeTagInfo, &attributes, sizeof(attributes)) ||
		   !GetFileInformationByHandleEx( file.get(), FileStandardInfo, &standard, sizeof(standard)) ||
		   (attributes.FileAttributes & REJECTED) != 0 || standard.EndOfFile.QuadPart != 25231360 ||
		   standard.AllocationSize.QuadPart > 25231360 || standard.AllocationSize.QuadPart < 0) {
			throw OperationError( ErrorKind::TEMPORARY_STORAGE, ERROR_INVALID_DATA) ;
		}
		size = 25231360 ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 自分の一fileだけを削除します
	 */
	//------------------------------------------------------------------------------
	~Impl()
	{
		// 耐久採用後のcloseはquotaやfileを削除しません
		if( !retained) {
			static_cast<void>( cleanup()) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief journal一fileの削除と容量charge解放を一度だけ試み、その結果をcacheします
	 * @return 削除済みの真偽、error code、残存byte数を持つTransferableJournalCleanupのcopyです。空pathは削除済みです
	 * @note 実fileはhandleを閉じてDeleteFileW、人工I/Oはremoveを使います。削除確認時だけchargeを解放し、失敗時はerrorと残存容量を保持します。再呼出しはcacheを返し、OS削除を再試行しません
	 */
	//------------------------------------------------------------------------------
	TransferableJournalCleanup cleanup() noexcept
	{
		// destructorから同じ削除を再試行しません
		if( cleanupAttempted) {
			return cleanupResult ;
		}
		cleanupAttempted = true ;
		// logical fileでも削除成功確認までchargeを保持します
		if( charge && charge->testIo) {
			cleanupResult.error = charge->testIo->remove() ;
			cleanupResult.deleted = cleanupResult.error == 0 ;
			cleanupResult.residualBytes = cleanupResult.deleted ? 0 : charge->bytes ;
			// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
			if( cleanupResult.deleted) {
				charge->release() ;
			} else {
				charge->cleanupCode = cleanupResult.error ;
			}
			return cleanupResult ;
		}
		file.reset() ; // 書込みhandleを閉じてから同じpathだけを削除
		// CREATE_NEWできた一file以外へ削除を広げません
		if( path.empty() || DeleteFileW( path.c_str())) {
			cleanupResult.deleted = true ;
			// 所有と解放確認の状態に従い、未確認の資源を再解放しません
			if( charge) {
				charge->release() ;
			}
		} else {
			cleanupResult.error = GetLastError() ;
			cleanupResult.residualBytes = charge ? charge->bytes : size ;
			// 所有と解放確認の状態に従い、未確認の資源を再解放しません
			if( charge) {
				charge->cleanupCode = cleanupResult.error ;
			}
		}
		return cleanupResult ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同期file位置を検証済みoffsetへ動かします
	 * @param[in] offset byte位置
	 */
	//------------------------------------------------------------------------------
	void seek( std::uint64_t offset) const
	{
		LARGE_INTEGER value{} ; // この同期処理で使う所有値または検証値
		value.QuadPart = static_cast<LONGLONG>( offset) ; // 固定25MiB内
		// 不整合や終了状態を分岐させ、未確定のまま後続へ進みません
		if( !SetFilePointerEx( file.get(), value, nullptr, FILE_BEGIN)) {
			throw OperationError( ErrorKind::TEMPORARY_STORAGE, GetLastError()) ;
		}
	}
} ;
//------------------------------------------------------------------------------
/**
 * @brief private固定stagingへ一fileを作り物理予約します
 * @param[in] root 空なら既定history。試験は自分の隔離root
 * @param[in] bytes 固定extent
 * @param[in] charge 事前取得したquota
 * @see koefude::TransferableJournalFile::TransferableJournalFile(const std::filesystem::path &, std::uint64_t, std::shared_ptr< koefude::TransferableJournalCharge >)
 */
//------------------------------------------------------------------------------
TransferableJournalFile::TransferableJournalFile( const std::filesystem::path &root, std::uint64_t bytes,
												 std::shared_ptr<TransferableJournalCharge> charge)
	: impl_( std::make_unique<Impl>( root, std::move( charge)))
{
	// 任意の大file予約や未取得quotaでは作成を完遂しません
	if( bytes == 0 || bytes > 25231360 || !impl_->charge || !impl_->charge->active || impl_->charge->bytes < bytes) {
		throw OperationError( ErrorKind::TEMPORARY_STORAGE) ;
	}
	// 内部fakeは固定extentの論理予約だけを行います
	if( impl_->charge->testIo) {
		impl_->size = bytes ;
		impl_->charge->testIo->create( bytes) ;
		return ;
	}
	FILE_ALLOCATION_INFO allocation{} ; // この同期処理で使う所有値または検証値
	allocation.AllocationSize.QuadPart = static_cast<LONGLONG>( bytes) ;
	// 不整合や終了状態を分岐させ、未確定のまま後続へ進みません
	if( !SetFileInformationByHandle( impl_->file.get(), FileAllocationInfo, &allocation, sizeof(allocation))) {
		throw OperationError( ErrorKind::TEMPORARY_STORAGE, GetLastError()) ;
	}
	impl_->size = bytes ;
	impl_->seek( bytes) ;
	// 不整合や終了状態を分岐させ、未確定のまま後続へ進みません
	if( !SetEndOfFile( impl_->file.get())) {
		throw OperationError( ErrorKind::TEMPORARY_STORAGE, GetLastError()) ;
	}
	std::array<std::uint8_t, 65536> zeros{} ; // 物理予約を触る一blockだけ
	// sparse/free-space promiseだけでなく全extentへwrite/flushします
	for( std::uint64_t offset = 0; offset < bytes;) { // 次の予約block
		// この同期境界で所有する値、または確定状態の検証値です
		const auto count = static_cast<std::size_t>( std::min<std::uint64_t>( zeros.size(), bytes - offset)) ;
		write( offset, std::span( zeros).first( count)) ;
		offset += count ;
	}
	flush() ;
	FILE_STANDARD_INFO actual{} ; // 実allocationを検査
	// 不整合や終了状態を分岐させ、未確定のまま後続へ進みません
	if( !GetFileInformationByHandleEx( impl_->file.get(), FileStandardInfo, &actual, sizeof(actual)) ||
	   actual.AllocationSize.QuadPart < static_cast<LONGLONG>( bytes) ||
	   actual.AllocationSize.QuadPart > static_cast<LONGLONG>( bytes)) {
		throw OperationError( ErrorKind::TEMPORARY_STORAGE, ERROR_DISK_FULL) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 未移譲なら自分が作成した一fileだけを削除します
 * @see koefude::TransferableJournalFile::~TransferableJournalFile
 */
//------------------------------------------------------------------------------
TransferableJournalFile::~TransferableJournalFile() = default ;
//------------------------------------------------------------------------------
/**
 * @brief 同じhandleと削除責任を移します
 * @param[in,out] other 元owner
 * @see koefude::TransferableJournalFile::TransferableJournalFile(koefude::TransferableJournalFile &&)
 */
//------------------------------------------------------------------------------
TransferableJournalFile::TransferableJournalFile( TransferableJournalFile &&other) noexcept = default ;
//------------------------------------------------------------------------------
/**
 * @brief 旧fileをcleanupして移します
 * @param[in,out] other 元owner
 * @return 自身
 * @see koefude::TransferableJournalFile::operator=(koefude::TransferableJournalFile &&)
 */
//------------------------------------------------------------------------------
TransferableJournalFile &TransferableJournalFile::operator=( TransferableJournalFile &&other) noexcept = default ;
//------------------------------------------------------------------------------
/**
 * @brief 予約extent内だけを書きます
 * @param[in] offset byte位置
 * @param[in] bytes 同期borrow
 * @see koefude::TransferableJournalFile::write
 */
//------------------------------------------------------------------------------
void					 TransferableJournalFile::write( std::uint64_t offset, std::span<const std::uint8_t> bytes)
{
	// 不正範囲やcleanup後のhandleをI/Oへ渡しません
	if( !impl_ || impl_->cleanupAttempted || offset > impl_->size || bytes.size() > impl_->size - offset) {
		throw OperationError( ErrorKind::TEMPORARY_STORAGE, ERROR_INVALID_PARAMETER) ;
	}
	// test seamは同期ownerだけへ渡します
	if( impl_->charge && impl_->charge->testIo) {
		impl_->charge->testIo->write( offset, bytes) ;
		return ;
	}
	impl_->seek( offset) ;
	// 部分writeを全byte完了まで進め、0byte成功を拒否します
	while( !bytes.empty()) {
		DWORD	   written = 0 ; // 今回の元write数
		// この同期境界で所有する値、または確定状態の検証値です
		const auto count =
			static_cast<DWORD>( std::min<std::size_t>( 65536, bytes.size())) ; // この同期処理で使う所有値または検証値
		// 不整合や終了状態を分岐させ、未確定のまま後続へ進みません
		if( !WriteFile( impl_->file.get(), bytes.data(), count, &written, nullptr) || written == 0 || written > count) {
			throw OperationError( ErrorKind::TEMPORARY_STORAGE, GetLastError()) ;
		}
		bytes = bytes.subspan( written) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 予約extent内だけを読みます
 * @param[in] offset byte位置
 * @param[out] bytes 成功時だけ全域有効
 * @see koefude::TransferableJournalFile::read
 */
//------------------------------------------------------------------------------
void TransferableJournalFile::read( std::uint64_t offset, std::span<std::uint8_t> bytes) const
{
	// extent外やcleanup後から本文を読ませません
	if( !impl_ || impl_->cleanupAttempted || offset > impl_->size || bytes.size() > impl_->size - offset) {
		throw OperationError( ErrorKind::TEMPORARY_STORAGE, ERROR_INVALID_PARAMETER) ;
	}
	// test seamは同期ownerだけへ渡します
	if( impl_->charge && impl_->charge->testIo) {
		impl_->charge->testIo->read( offset, bytes) ;
		return ;
	}
	impl_->seek( offset) ;
	// 全要求byteを有界なblockで読むまで繰返します
	while( !bytes.empty()) {
		DWORD	   count = 0 ; // 実read数
		// この同期境界で所有する値、または確定状態の検証値です
		const auto requested =
			static_cast<DWORD>( std::min<std::size_t>( 65536, bytes.size())) ; // この同期処理で使う所有値または検証値
		// 不整合や終了状態を分岐させ、未確定のまま後続へ進みません
		if( !ReadFile( impl_->file.get(), bytes.data(), requested, &count, nullptr) || count == 0 || count > requested) {
			throw OperationError( ErrorKind::TEMPORARY_STORAGE, GetLastError()) ;
		}
		bytes = bytes.subspan( count) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 全先行writeのflush完了を確認します
 * @see koefude::TransferableJournalFile::flush
 */
//------------------------------------------------------------------------------
void TransferableJournalFile::flush()
{
	// test seamで物理I/Oを実行しません
	if( impl_ && impl_->charge && impl_->charge->testIo) {
		impl_->charge->testIo->flush() ;
		return ;
	}
	// 所有と解放確認の状態に従い、未確認の資源を再解放しません
	if( !impl_ || impl_->cleanupAttempted || !FlushFileBuffers( impl_->file.get())) {
		throw OperationError( ErrorKind::TEMPORARY_STORAGE, GetLastError()) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 所有fileだけを明示cleanupします。再試行しません
 * @return 元結果
 * @see koefude::TransferableJournalFile::cleanup
 */
//------------------------------------------------------------------------------
TransferableJournalCleanup TransferableJournalFile::cleanup() noexcept
{
	return impl_ ? impl_->cleanup() : TransferableJournalCleanup{true, 0, 0} ;
}
//------------------------------------------------------------------------------
/**
 * @brief 自分が作成したpathを読みます
 * @return owner寿命中の値参照
 * @see koefude::TransferableJournalFile::path
 */
//------------------------------------------------------------------------------
const std::filesystem::path &TransferableJournalFile::path() const noexcept
{
	return impl_->path ;
}
//------------------------------------------------------------------------------
/**
 * @brief 固定予約byteを読みます
 * @return 全extent
 * @see koefude::TransferableJournalFile::size
 */
//------------------------------------------------------------------------------
std::uint64_t TransferableJournalFile::size() const noexcept
{
	return impl_ ? impl_->size : 0 ;
}
//------------------------------------------------------------------------------
/**
 * @brief 同一fileの検証用IDです
 * @return volumeとfile index
 * @see koefude::TransferableJournalFile::identity
 */
//------------------------------------------------------------------------------
std::array<std::uint64_t, 2> TransferableJournalFile::identity() const
{
	// logical fileのidentityも同じ契約で照合します
	if( impl_->charge && impl_->charge->testIo) {
		return impl_->charge->testIo->identity() ;
	}
	BY_HANDLE_FILE_INFORMATION info{} ; // 同一handleのOS ID
	// 不整合や終了状態を分岐させ、未確定のまま後続へ進みません
	if( !GetFileInformationByHandle( impl_->file.get(), &info)) {
		throw OperationError( ErrorKind::TEMPORARY_STORAGE, GetLastError()) ;
	}
	return {info.dwVolumeSerialNumber, (static_cast<std::uint64_t>( info.nFileIndexHigh) << 32) | info.nFileIndexLow} ;
}
//------------------------------------------------------------------------------
/**
 * @brief 同volume history内へcopyなしでrenameします
 * @param[in] name path構文を含まない新しいASCII名
 * @see koefude::TransferableJournalFile::renameWithinHistory
 */
//------------------------------------------------------------------------------
void TransferableJournalFile::renameWithinHistory( std::wstring_view name)
{
	// file名だけを受け取り、同じhistory rootから外へ出しません
	if( name.empty() || name.size() > 128 || name == L"." || name == L".." ||
	   !std::all_of( name.begin(), name.end(),
					//------------------------------------------------------------------------------
					/**
					 * @brief ASCIIのfile名構成文字だけを受け付けます
					 * @param[in] unit 一文字
					 * @return basename文字ならtrue
					 */
					//------------------------------------------------------------------------------
					[]( wchar_t unit)
					{
						return (unit >= L'a' && unit <= L'z') || (unit >= L'A' && unit <= L'Z') ||
							   (unit >= L'0' && unit <= L'9') || unit == L'-' || unit == L'_' || unit == L'.' ;
					})) {
		throw OperationError( ErrorKind::TEMPORARY_STORAGE, ERROR_INVALID_NAME) ;
	}
	// fakeでもpayload copyなしの同一identityを保ちます
	if( impl_->charge && impl_->charge->testIo) {
		impl_->charge->testIo->rename() ;
		impl_->path = impl_->root / std::wstring( name) ;
		return ;
	}
	const PrivateSecurity security ; // 同じprivate ancestry検査
	static_cast<void>( prepareDirectory( impl_->root, security, false)) ;
	auto					  target = impl_->root / std::wstring( name) ; // 同じvolume、payload copyなし
	const auto				 &native = target.native() ; // API同期borrow
	std::vector<std::uint8_t> storage( sizeof(FILE_RENAME_INFO) + native.size() * sizeof(wchar_t)) ;
	auto *rename = reinterpret_cast<FILE_RENAME_INFO *>( storage.data()) ; // API用の有界aligned heap
	rename->ReplaceIfExists = FALSE ;
	rename->RootDirectory = nullptr ;
	rename->FileNameLength = static_cast<DWORD>( native.size() * sizeof(wchar_t)) ;
	std::copy( native.begin(), native.end(), rename->FileName) ;
	// 不整合や終了状態を分岐させ、未確定のまま後続へ進みません
	if( !SetFileInformationByHandle( impl_->file.get(), FileRenameInfo, rename, static_cast<DWORD>( storage.size()))) {
		throw OperationError( ErrorKind::TEMPORARY_STORAGE, GetLastError()) ;
	}
	impl_->path = std::move( target) ; // 同fileの削除責任だけを更新
}
//------------------------------------------------------------------------------
/**
 * @brief 耐久採用後のdestructorをclose-onlyへ切り替えます
 * @see koefude::TransferableJournalFile::retainDurable
 */
//------------------------------------------------------------------------------
void TransferableJournalFile::retainDurable() noexcept
{
	impl_->retained = true ;
	// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
	if( impl_->charge) {
		impl_->charge->durable = true ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 元chargeを参照します
 * @return 共有台帳枠
 * @see koefude::TransferableJournalFile::charge
 */
//------------------------------------------------------------------------------
std::shared_ptr<TransferableJournalCharge> TransferableJournalFile::charge() const noexcept
{
	return impl_->charge ;
}
//------------------------------------------------------------------------------
/**
 * @brief private implを移します
 * @param[in] impl 検証済みowner
 * @see koefude::TransferableJournalFile::TransferableJournalFile(std::unique_ptr< koefude::TransferableJournalFile::Impl >)
 */
//------------------------------------------------------------------------------
TransferableJournalFile::TransferableJournalFile( std::unique_ptr<Impl> impl) : impl_( std::move( impl))
{
}
//------------------------------------------------------------------------------
/**
 * @brief 固定名と照合済みidentityだけで再openします
 * @param[in] root 固定root
 * @param[in] key 元key
 * @param[in] location staging1/root2
 * @param[in] expected 期待ID
 * @param[in] charge 耐久charge
 * @return 同じ単独file
 * @see koefude::TransferableJournalFile::reopenKnown
 */
//------------------------------------------------------------------------------
TransferableJournalFile TransferableJournalFile::reopenKnown( const std::filesystem::path &root,
															 const WholeTextJournalKey &key, unsigned location,
															 const std::array<std::uint64_t, 2>		   &expected,
															 std::shared_ptr<TransferableJournalCharge> charge)
{
	// 再openには固定keyと既知identityが必須です
	if( key == WholeTextJournalKey{} || (location != 1 && location != 2) || expected[0] == 0 || expected[1] == 0) {
		throw OperationError( ErrorKind::TEMPORARY_STORAGE, ERROR_INVALID_DATA) ;
	}
	charge->journalKey = key ;
	unsigned actualLocation = location ; // 二つの固定候補だけ照合
	// logical fake以外は両名の存在を先に確認し、重複/未知identityを保護します
	if( !charge->testIo) {
		std::wstring name = L"koefude-journal-" ; // 同じbasenameだけ
		// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
		for( const auto byte : key) {
			name += K_HEX[byte >> 4] ;
			name += K_HEX[byte & 15] ;
		}
		const auto staging = root / L"whole-text-staging" / name ; // 固定候補1
		const auto history = root / name ; // 固定候補2
		const auto a = GetFileAttributesW( staging.c_str()) ; // 同期属性だけ
		// この同期境界で所有する値、または確定状態の検証値です
		const auto aError = a == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS ; // 元code
		const auto b = GetFileAttributesW( history.c_str()) ; // 同期属性だけ
		// この同期境界で所有する値、または確定状態の検証値です
		const auto bError = b == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS ; // 元code
		const bool first = a != INVALID_FILE_ATTRIBUTES ; // 存在1
		const bool second = b != INVALID_FILE_ATTRIBUTES ; // 存在2
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( (!first && aError != ERROR_FILE_NOT_FOUND && aError != ERROR_PATH_NOT_FOUND) ||
		   (!second && bError != ERROR_FILE_NOT_FOUND && bError != ERROR_PATH_NOT_FOUND) || first == second) {
			throw OperationError( ErrorKind::TEMPORARY_STORAGE, ERROR_INVALID_DATA) ;
		}
		actualLocation = first ? 1u : 2u ;
	}
	// fixed候補の同一file IDだけを採用します
	auto impl = std::make_unique<Impl>( root, std::move( charge), key, actualLocation) ; // known owner
	TransferableJournalFile result( std::move( impl)) ; // 同一file owner
	// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
	if( result.identity() != expected) {
		result.retainDurable() ;
		throw OperationError( ErrorKind::TEMPORARY_STORAGE, ERROR_INVALID_DATA) ;
	}
	result.retainDurable() ;
	return result ;
}
} // namespace koefude
