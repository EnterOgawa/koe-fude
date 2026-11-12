//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file model_download_io.cpp
 * @brief 検証中のfileを保護し、cacheへ原子的に確定します
 * @version 1.0.0
 * @date 2026/09/13
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/13 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/13 private一時file・cache排他・handle renameを実装
 * 0.1.0 Shunji Ogawa 2026/10/19 宣言の役割を明記
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1280の用途限定導入と実byte進捗を接続
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
#include "platform/model_download_io.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include <bcrypt.h>
#include <objbase.h>
#include <sddl.h>

#include "core/types.h"
#include "inference/model_preparation.h"
#include "platform/win_helpers.h"

namespace koefude {
namespace {

/** @brief 現在のユーザーとSYSTEMだけへ新規fileを公開するACLです */
class ModelSecurity final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 自分のtokenからprivate descriptorを作ります
	 */
	//------------------------------------------------------------------------------
	ModelSecurity()
	{
		HANDLE raw = nullptr ; // 読取り専用のprocess token
		// 管理者権限や他人のtokenを必要としない
		if( !OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &raw)) {
			throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
		}
		const UniqueHandle token( raw) ; // tokenの所有権
		DWORD			   bytes = 0 ; // TOKEN_USERの必要byte数
		// TOKEN_USERの必要容量照会結果。errorと必要byte数を併せて検証する
		const BOOL		   found = GetTokenInformation( token.get(), TokenUser, nullptr, 0, &bytes) ;
		// OSからの過大な長さを無制限allocationへ使わない
		if( found || GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0 || bytes > 65536) {
			throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_INVALID_DATA) ;
		}
		std::vector<std::byte> data( bytes) ; // OSが書込むTOKEN_USERの所有領域
		// 権限が確認できなければ共用ACLで作成しない
		if( !GetTokenInformation( token.get(), TokenUser, data.data(), bytes, &bytes)) {
			throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
		}
		// OSが設定した構造体を同期解釈する境界。SIDはdataの寿命中だけ有効
		const auto *user = reinterpret_cast<const TOKEN_USER *>( data.data()) ;
		LPWSTR		sid = nullptr ; // LocalFreeするSIDの文字列表現
		// 変換に失敗した状態でDACLを組立てない
		if( !ConvertSidToStringSidW( user->User.Sid, &sid)) {
			throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
		}
		const std::unique_ptr<void, decltype(&LocalFree)> ownedSid( sid, LocalFree) ; // SIDを所有
		// SYSTEMと現在利用者だけに継承可能な完全権限を与える所有SDDL
		const std::wstring	 sddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;" + std::wstring( sid) + L")" ;
		PSECURITY_DESCRIPTOR descriptor = nullptr ; // OSが検証する自己相対descriptor
		// 不正descriptorをCreateFileへ渡さない
		if( !ConvertStringSecurityDescriptorToSecurityDescriptorW( sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
			throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
		}
		descriptor_.reset( descriptor) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 新規作成時の非継承属性を返します
	 * @return object寿命中有効なborrow
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] SECURITY_ATTRIBUTES attributes() const noexcept
	{
		return {sizeof(SECURITY_ATTRIBUTES), descriptor_.get(), FALSE} ;
	}

	private:
	/// 所有DACL
	std::unique_ptr<void, decltype(&LocalFree)> descriptor_{nullptr, LocalFree} ;
} ;

/** @brief SHA-256 providerを解放します */
struct ModelAlgorithmDeleter {
	//------------------------------------------------------------------------------
	/**
	 * @brief providerを閉じます
	 * @param[in] handle 所有する有効handle
	 */
	//------------------------------------------------------------------------------
	void operator()( BCRYPT_ALG_HANDLE handle) const noexcept
	{
		BCryptCloseAlgorithmProvider( handle, 0) ;
	}
} ;
/** @brief SHA-256作業領域を解放します */
struct ModelHashDeleter {
	//------------------------------------------------------------------------------
	/**
	 * @brief hashを閉じます
	 * @param[in] handle 所有する有効handle
	 */
	//------------------------------------------------------------------------------
	void operator()( BCRYPT_HASH_HANDLE handle) const noexcept
	{
		BCryptDestroyHash( handle) ;
	}
} ;
//------------------------------------------------------------------------------
/**
 * @brief CNG失敗を元codeで通知します
 * @param[in] status CNGのNTSTATUS
 */
//------------------------------------------------------------------------------
void checkHashStatus( const NTSTATUS status)
{
	// hashを計算できない場合は未検証fileを確定しない
	if( status < 0) {
		throw OperationError( ErrorKind::MODEL_REJECTED, static_cast<std::uint32_t>( status)) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 自分の一時file名を生成します
 * @return UUIDを含む安全なbasename
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::wstring stagingName()
{
	GUID		  id{} ; // process間でも衝突しない名前
	const HRESULT status = CoCreateGuid( &id) ; // 追加権限を必要としないUUID
	// 乱数名を得られなければ予測可能な共通fileへ退避しない
	if( FAILED(status)) {
		throw OperationError( ErrorKind::MODEL_REJECTED, static_cast<std::uint32_t>( status)) ;
	}
	std::array<wchar_t, 40> text{} ; // GUIDの文字列表現と終端
	// 変換結果が全体を収めた場合だけ名前へ使う
	if( StringFromGUID2( id, text.data(), static_cast<int>( text.size())) == 0) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_INVALID_DATA) ;
	}
	return std::wstring( L"koefude-") + text.data() + L".part" ;
}

//------------------------------------------------------------------------------
/**
 * @brief 所有handleの最後のclose時の削除を変更します
 * @param[in] file DELETE権限を持つ所有handle
 * @param[in] remove trueは取消し/異常終了で自動削除、falseはrename前の解除
 * @throws OperationError OSによるdisposition変更失敗
 */
//------------------------------------------------------------------------------
void setDeleteDisposition( const HANDLE file, const bool remove)
{
	FILE_DISPOSITION_INFO disposition{static_cast<BOOLEAN>( remove)} ; // 解除可能な削除指定
	// FILE_FLAG_DELETE_ON_CLOSEは解除不可なので使用しない
	if( !SetFileInformationByHandle( file, FileDispositionInfo, &disposition, sizeof(disposition))) {
		throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
	}
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 検査済みlocal directoryを一段ずつ作成します
 * @param[in] root local絶対directory
 * @throws OperationError 属性拒否、作成失敗
 * @see koefude::createModelDirectory
 */
//------------------------------------------------------------------------------
void createModelDirectory( const std::filesystem::path &root)
{
	const auto			checked = validateModelDirectory( root) ; // 非localや曖昧な名前を拒否したpath
	const ModelSecurity security ; // 新規作成部分だけへ使うprivate ACL
	auto				current = checked.root_path() ; // 祖先から作成する位置
	// 未作成部分だけを一段ずつ作り、作成競合時も属性を再確認する
	for( const auto &component : checked.relative_path()) { // 次のdirectory要素
		current /= component ;
		// 既存directoryのACLや内容を変更しない
		if( GetFileAttributesW( current.c_str()) == INVALID_FILE_ATTRIBUTES) {
			const DWORD code = GetLastError() ; // 属性取得の元code
			// 欠落以外のI/O失敗を新規作成へ置き換えない
			if( code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) {
				throw OperationError( ErrorKind::MODEL_REJECTED, code) ;
			}
			auto attributes = security.attributes() ; // CreateDirectory中だけの借用属性
			// 同時作成の競合以外は失敗として返す
			if( !CreateDirectoryW( current.c_str(), &attributes) && GetLastError() != ERROR_ALREADY_EXISTS) {
				throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
			}
		}
		static_cast<void>( validateModelDirectory( current)) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief local cacheを作り、同時取得を拒否して空き容量を照合します
 * @param[in] root 信頼済みのlocal絶対directory
 * @param[in] requiredBytes 余裕を含めた追加空き容量（byte）
 * @throws OperationError 属性・容量・排他・保存先の失敗
 * @note 回収走査は4096件まで。自作prefix/UUIDの通常fileだけを削除し、他は保持します
 * @see koefude::ModelCacheTransaction::ModelCacheTransaction(const std::filesystem::path &, std::uint64_t)
 */
//------------------------------------------------------------------------------
ModelCacheTransaction::ModelCacheTransaction( const std::filesystem::path &root, const std::uint64_t requiredBytes)
{
	createModelDirectory( root) ;
	const auto			lockPath = root / L".prepare.lock" ; // 別起動との同時取得を防ぐ専用file
	const ModelSecurity security ; // 新規lockのprivate ACL
	auto				attributes = security.attributes() ; // CreateFile中だけ有効な属性
	lock_.reset( CreateFileW( lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, &attributes, OPEN_ALWAYS,
							FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)) ;
	// 二重取得や権限失敗をworker待機で隠さない
	if( lock_.get() == INVALID_HANDLE_VALUE) {
		throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
	}
	BY_HANDLE_FILE_INFORMATION information{} ; // 実際に開いたlockの属性
	// reparseや別fileのhardlinkを排他fileとして利用しない
	if( !GetFileInformationByHandle( lock_.get(), &information) || information.nNumberOfLinks != 1 ||
	   (information.dwFileAttributes &
		(FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_OFFLINE |
		 FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS)) != 0) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_ACCESS_DENIED) ;
	}
	stagingRoot_ = root / L".staging" ;
	createModelDirectory( stagingRoot_) ;
	std::size_t inspected = 0 ; // 異常な大量fileで回収時間を無制限にしない件数
	// crashの小さい確定前区間に残った自作の通常fileだけを排他中に回収する
	for( const auto &entry : std::filesystem::directory_iterator( stagingRoot_)) { // 回収候補
		// 通常は数個の残骸だけを扱い、大量の外部fileがある場合は明示失敗とする
		if( ++inspected > 4096) {
			throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_TOO_MANY_NAMES) ;
		}
		const auto name = entry.path().filename().native() ; // 専用領域内のbasename
		// 指定prefix/UUID長/拡張子以外へは削除操作を広げない
		if( name.size() != 51 || !name.starts_with( L"koefude-{") || !name.ends_with( L"}.part")) {
			continue ;
		}
		const auto text = name.substr( 8, 38) ; // bracesを含む固定長GUIDだけを解析する
		GUID	   id{} ; // 自作の命名規則を満たすか確認する出力値
		// 同じ長さの名前でもUUIDでなければ削除しない
		if( FAILED(CLSIDFromString(text.c_str(), &id))) {
			continue ;
		}
		static_cast<void>( validateLocalFile( entry.path())) ;
		// 古い一時fileがまだ使用中なら無理に取得を開始しない
		if( !DeleteFileW( entry.path().c_str())) {
			throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
		}
	}
	ULARGE_INTEGER available{} ; // このユーザーが利用できる空きbyte数
	// 他processの使用量を含む実際の空き容量へ余裕込みの予定量を照合する
	if( !GetDiskFreeSpaceExW( root.c_str(), &available, nullptr, nullptr)) {
		throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
	}
	// 事前に不足している場合は通信と大きい書込みを開始しない
	if( available.QuadPart < requiredBytes) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_DISK_FULL) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief cache排他を解放します
 * @see koefude::ModelCacheTransaction::~ModelCacheTransaction
 */
//------------------------------------------------------------------------------
ModelCacheTransaction::~ModelCacheTransaction() = default ;

//------------------------------------------------------------------------------
/**
 * @brief 一時領域を返します
 * @return このtransaction中有効な借用path
 * @see koefude::ModelCacheTransaction::stagingRoot
 */
//------------------------------------------------------------------------------
const std::filesystem::path &ModelCacheTransaction::stagingRoot() const noexcept
{
	return stagingRoot_ ;
}

//------------------------------------------------------------------------------
/**
 * @brief cacheの専用領域へ未使用の一時fileを作ります
 * @param[in] root 排他transactionの一時directory
 * @param[in] limitBytes 追記可能な最大byte数、1～8GiB
 * @throws OperationError 作成・上限の失敗
 * @see koefude::ModelStagingFile::ModelStagingFile(const std::filesystem::path &, std::uint64_t)
 */
//------------------------------------------------------------------------------
ModelStagingFile::ModelStagingFile( const std::filesystem::path &root, const std::uint64_t limitBytes)
	: ModelStagingFile( root,
					   // 通常取得の8GiB上限を用途限定factoryと共有しません
					   limitBytes > 0 && limitBytes <= 8ULL * 1024 * 1024 * 1024 ? limitBytes : 0, WholeTextTag{})
{
}

//------------------------------------------------------------------------------
/**
 * @brief 用途13のregistry容量だけを許す導入・直接取得用fileを作ります
 * @param[in] root transaction所有の一時directory
 * @return 単独所有の一時file
 * @throws OperationError registry・作成・属性の拒否
 * @see koefude::ModelStagingFile::forWholeTextImport
 */
//------------------------------------------------------------------------------
ModelStagingFile ModelStagingFile::forWholeTextImport( const std::filesystem::path &root)
{
	const auto bytes = modelDefinition( wholeTextNativeModelId()).bytes ; // 用途13だけの上限
	// signed64のfile長で表せないregistry値は作成前に拒否します
	if( bytes == 0 || bytes > static_cast<std::uint64_t>( std::numeric_limits<std::int64_t>::max())) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_INVALID_DATA) ;
	}
	return ModelStagingFile( root, bytes, WholeTextTag{}) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 確認済み容量を上限として専用の一時model fileを作成します
 * @param[in] root 呼出し中だけ借用するlocal一時directory。検証済みroot内のランダム名で作成します
 * @param[in] limitBytes 0以外の最大書込byte数。以後の受信/検証の容量上限として保持します
 * @throws OperationError 容量0、rootの属性違反、または一時file作成失敗
 * @note 名前のない第3引数WholeTextTagはprivate factoryだけが使う用途限定tagです。同期呼出し後に借用せず、未commit fileの削除責任をこのobjectが所有します
 */
//------------------------------------------------------------------------------
ModelStagingFile::ModelStagingFile( const std::filesystem::path &root, const std::uint64_t limitBytes, WholeTextTag)
	: limitBytes_( limitBytes)
{
	// 呼出し元で上限を検査済みでも0は拒否します
	if( limitBytes == 0) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_INVALID_DATA) ;
	}
	const auto checkedRoot = validateModelDirectory( root) ; // private一時領域の確認値
	// transactionが所有する一時領域以外へfileを作らない
	if( checkedRoot.filename() != L".staging") {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_INVALID_NAME) ;
	}
	cacheRoot_ = checkedRoot.parent_path() ;
	const auto			path = checkedRoot / stagingName() ; // UUIDの安全な新規file
	const ModelSecurity security ; // 一時fileにも明示するprivate ACL
	auto				attributes = security.attributes() ; // 非継承の作成属性
	file_.reset( CreateFileW( path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ, &attributes,
							CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)) ;
	// 既存の同名fileを上書きせず、衝突も明示失敗とする
	if( file_.get() == INVALID_HANDLE_VALUE) {
		throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
	}
	setDeleteDisposition( file_.get(), true) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 未確定fileをhandleで削除し、全資源を解放します
 * @see koefude::ModelStagingFile::~ModelStagingFile
 */
//------------------------------------------------------------------------------
ModelStagingFile::~ModelStagingFile() = default ;

//------------------------------------------------------------------------------
/**
 * @brief 上限を超えない全byteを追記します
 * @param[in] bytes 呼出し中のborrow、最大64KiB
 * @throws OperationError 上限・書込み失敗。失敗後は確定せず破棄します
 * @see koefude::ModelStagingFile::append
 */
//------------------------------------------------------------------------------
void ModelStagingFile::append( const std::span<const std::byte> bytes)
{
	// 境界超過や確定後の追記をfileへ反映しない
	if( committed_ || bytes.size() > 65536 || bytes.size() > limitBytes_ - size_) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_FILE_TOO_LARGE) ;
	}
	verified_ = false ;
	DWORD written = 0 ; // 実際の書込みbyte数
	// 部分書込みやdisk fullを検証済みfileにしない
	if( !WriteFile( file_.get(), bytes.data(), static_cast<DWORD>( bytes.size()), &written, nullptr) ||
	   written != bytes.size()) {
		throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
	}
	size_ += written ;
}

//------------------------------------------------------------------------------
/**
 * @brief 読取り位置を先頭へ戻します
 * @throws OperationError seek失敗
 * @see koefude::ModelStagingFile::rewind
 */
//------------------------------------------------------------------------------
void ModelStagingFile::rewind()
{
	const LARGE_INTEGER beginning{} ; // byte位置0
	// 検証やarchive読取りを途中のfile位置から始めない
	if( !SetFilePointerEx( file_.get(), beginning, nullptr, FILE_BEGIN)) {
		throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 標準入力へ複製するhandleを返します
 * @return object寿命中だけ有効なborrow
 * @see koefude::ModelStagingFile::handle
 */
//------------------------------------------------------------------------------
HANDLE ModelStagingFile::handle() const noexcept
{
	return file_.get() ;
}

//------------------------------------------------------------------------------
/**
 * @brief 所有handleから固定size/hashを確認します
 * @param[in] bytes 正確な期待byte数
 * @param[in] sha256 小文字64桁の期待hash
 * @param[in] stop 読取り中の取消し
 * @param[in] progress 空可の同期通知。検証済みの実読取りbyte数を渡します
 * @throws OperationError 内容不一致、I/O、取消し
 * @see koefude::ModelStagingFile::verify
 */
//------------------------------------------------------------------------------
void ModelStagingFile::verify( const std::uint64_t bytes, const std::string_view sha256, const std::stop_token stop,
							  const std::function<void( std::uint64_t)> &progress)
{
	checkCancelled( stop) ;
	// EOFやsizeの不一致をhash計算前に拒否する
	if( size_ != bytes || sha256.size() != 64) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_BAD_LENGTH) ;
	}
	rewind() ;
	BCRYPT_ALG_HANDLE rawAlgorithm = nullptr ; // CNG作成時だけの受取先
	checkHashStatus( BCryptOpenAlgorithmProvider( &rawAlgorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) ;
	const std::unique_ptr<void, ModelAlgorithmDeleter> algorithm( rawAlgorithm) ; // provider所有権
	BCRYPT_HASH_HANDLE								   rawHash = nullptr ; // CNG hashの受取先
	checkHashStatus( BCryptCreateHash( algorithm.get(), &rawHash, nullptr, 0, nullptr, 0, 0)) ;
	const std::unique_ptr<void, ModelHashDeleter> hash( rawHash) ; // hash stateを所有
	std::array<unsigned char, 65536>			  buffer{} ; // モデル全体を保持しない固定buffer
	std::uint64_t								  total = 0 ; // 実際の読取り量
	// EOF/取消しまで所有handleだけからhashを計算する
	for(;;) {
		checkCancelled( stop) ;
		DWORD read = 0 ; // 今回読んだbyte数
		// 部分hashやI/O失敗を成功へ変換しない
		if( !ReadFile( file_.get(), buffer.data(), static_cast<DWORD>( buffer.size()), &read, nullptr)) {
			throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
		}
		// 全byteを消費した位置でhashを確定する
		if( read == 0) {
			break ;
		}
		total += read ;
		// 不意のfile拡張を無制限に読み続けない
		if( total > bytes) {
			throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_BAD_LENGTH) ;
		}
		checkHashStatus( BCryptHashData( hash.get(), buffer.data(), read, 0)) ;
		// 実際にhashへ渡したbyteだけを同期通知します
		if( progress) {
			progress( total) ;
		}
	}
	std::array<unsigned char, 32> digest{} ; // SHA-256の結果
	checkHashStatus( BCryptFinishHash( hash.get(), digest.data(), static_cast<ULONG>( digest.size()), 0)) ;
	constexpr std::string_view K_HEX = "0123456789abcdef" ; // 小文字表現の固定表
	std::string				   actual ; // 照合用の64桁hash
	// 全digestをregistryと同じ表現へ変換する
	for( const auto byte : digest) { // 次のdigest byte
		actual.push_back( K_HEX[byte >> 4U]) ;
		actual.push_back( K_HEX[byte & 15U]) ;
	}
	// 内容一致したfileだけを後続のrenameへ許可する
	if( total != bytes || actual != sha256) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_CRC) ;
	}
	verified_ = true ;
}

//------------------------------------------------------------------------------
/**
 * @brief 同じvolumeの最終pathへhandleを保ったまま原子的にrenameします
 * @param[in] path registryの検査済み保存先。検証済みの親directoryを作成します
 * @throws OperationError 未検証、属性、rename、既存fileの競合
 * @see koefude::ModelStagingFile::commit
 */
//------------------------------------------------------------------------------
void ModelStagingFile::commit( const std::filesystem::path &path)
{
	const auto relative = path.lexically_relative( cacheRoot_) ; // cacheからの相対位置
	// 未検証/二重確定を拒否し、最終fileへ部分内容を公開しない
	if( !verified_ || committed_ || path.native().size() > 32700 || relative.empty() || relative.is_absolute() ||
	   relative.begin()->native() == L".." || relative.begin()->native() == L".staging" ||
	   relative == L".prepare.lock") {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_INVALID_DATA) ;
	}
	createModelDirectory( path.parent_path()) ;
	const auto attributes = GetFileAttributesW( path.c_str()) ; // 上書き対象の属性確認
	// 既存のfile以外や同期を伴う保存先を置き換えない
	if( attributes != INVALID_FILE_ATTRIBUTES) {
		static_cast<void>( validateLocalFile( path)) ;
	}
	// 未作成leafも予約名や曖昧なpathを拒否する
	else {
		static_cast<void>( validateModelDirectory( path)) ;
	}
	const auto nameBytes = path.native().size() * sizeof(wchar_t) ; // UTF-16のbyte数
	const auto allocation =
		offsetof( FILE_RENAME_INFO, FileName) + nameBytes + sizeof(wchar_t) ; // Win32のpath解釈用に終端NULも含める
	std::vector<std::byte> data( allocation) ; // OSが同期借用する整列済み所有領域
	// 可変長Win32構造体の境界。dataは呼出し終了まで移動・解放しない
	auto				  *rename = reinterpret_cast<FILE_RENAME_INFO *>( data.data()) ;
	rename->ReplaceIfExists = TRUE ;
	rename->RootDirectory = nullptr ;
	rename->FileNameLength = static_cast<DWORD>( nameBytes) ;
	std::memcpy( rename->FileName, path.c_str(), nameBytes + sizeof(wchar_t)) ;
	// rename前に書込み完了を確認し、disk fullを確定成功にしない
	if( !FlushFileBuffers( file_.get())) {
		throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
	}
	FILE_BASIC_INFO basic{} ; // 確定fileに一時属性を残さない
	basic.FileAttributes = FILE_ATTRIBUTE_NORMAL ;
	// 属性更新に失敗した場合も一時fileの削除指定を維持する
	if( !SetFileInformationByHandle( file_.get(), FileBasicInfo, &basic, sizeof(basic))) {
		throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
	}
	setDeleteDisposition( file_.get(), false) ;
	// 所有handleを閉じずに同一volume内で原子的に置換する
	if( !SetFileInformationByHandle( file_.get(), FileRenameInfo, rename, static_cast<DWORD>( data.size()))) {
		const DWORD code = GetLastError() ; // 既存fileを保持したrename失敗
		setDeleteDisposition( file_.get(), true) ;
		throw OperationError( ErrorKind::MODEL_REJECTED, code) ;
	}
	committed_ = true ;
}

} // namespace koefude
