//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_model_import.cpp
 * @brief sourceを保持し固定hashに一致した一時fileだけを原子的に導入します
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1280のsource leaseとcommit境界を実装
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1424の既知の行頭空白だけを整形
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "inference/whole_text_model_import.h"
#include "core/types.h"
#include "inference/model_preparation.h"
#include "platform/model_download_io.h"
#include "platform/win_helpers.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace koefude {
namespace {
//------------------------------------------------------------------------------
/**
 * @brief Windowsの予約名を検査します
 * @param[in] value 一つのpath要素
 * @return device別名ならtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool reserved( std::wstring_view value)
{
	const auto stem = value.substr( 0, value.find( L'.')) ; // 拡張子前だけを比較
	// 固定device名は拡張子付きでも拒否します
	for( const auto name : {L"CON", L"PRN", L"AUX", L"NUL"}) { // 固定予約名
		// UTF-16の固定3文字だけを比較します
		if( stem.size() == 3 && CompareStringOrdinal( stem.data(), 3, name, 3, TRUE) == CSTR_EQUAL) {
			return true ;
		}
	}
	return stem.size() == 4 && stem[3] >= L'0' && stem[3] <= L'9' &&
		   (CompareStringOrdinal( stem.data(), 3, L"COM", 3, TRUE) == CSTR_EQUAL ||
			CompareStringOrdinal( stem.data(), 3, L"LPT", 3, TRUE) == CSTR_EQUAL) ;
}
//------------------------------------------------------------------------------
/**
 * @brief OSへ渡す前に曖昧なsource名を拒否します
 * @param[in] source 絶対Unicode path
 * @throws OperationError 非local/別名の拒否
 */
//------------------------------------------------------------------------------
void validateSourceName( const std::filesystem::path &source)
{
	const auto &value = source.native() ; // 値で渡す所有pathのborrow
	// namespace/ADS/NUL/相対pathを属性照会より前に拒否します
	if( !source.is_absolute() || value.size() < 3 || value.size() > 32700 || value.starts_with( L"\\\\") ||
	   value[1] != L':' || value.find( L':', 2) != std::wstring::npos || value.find( L'\0') != std::wstring::npos) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_INVALID_NAME) ;
	}
	// 正規化で別のfileへ変わる要素を受け付けません
	for( const auto &component : source.relative_path()) { // 次の要素
		const auto &text = component.native() ; // UTF-16要素
		// device別名、制御文字、末尾空白等を拒否します
		if( text.empty() || text == L"." || text == L".." || reserved( text) || text.back() == L'.' ||
		   text.back() == L' ' || text.find_first_of( L"<>\"|?*") != std::wstring::npos ||
		   std::any_of( text.begin(), text.end(),
					   //------------------------------------------------------------------------------
					   /**
					    * @brief 制御文字を調べます
					    * @param[in] unit UTF-16単位
					    * @return 制御文字ならtrue
					    */
					   //------------------------------------------------------------------------------
					   []( wchar_t unit)
					   {
						   return unit < 32 ;
					   })) {
			throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_INVALID_NAME) ;
		}
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 回収対象内のsourceをtransaction作成前に拒否します
 * @param[in] source 検査済み絶対名
 * @param[in] cache 検査済みcache
 * @throws OperationError 自作staging内のsource
 */
//------------------------------------------------------------------------------
void rejectStagingSource( const std::filesystem::path &source, const std::filesystem::path &cache)
{
	auto prefix = (cache / L".staging").native() ; // caseと区切りを正規化した回収境界
	prefix += L'\\' ;
	const auto name = source.lexically_normal().make_preferred().native() ; // Win32比較用
	// Windowsのcase別名でも自作回収領域を通しません
	if( name.size() >= prefix.size() && CompareStringOrdinal( name.data(), static_cast<int>( prefix.size()), prefix.data(),
															static_cast<int>( prefix.size()), TRUE) == CSTR_EQUAL) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_ACCESS_DENIED) ;
	}
}
} // namespace
static_assert( std::is_nothrow_move_constructible_v<WholeTextModelImportResult>) ;
static_assert( std::is_nothrow_move_assignable_v<WholeTextModelImportResult>) ;
//------------------------------------------------------------------------------
/**
 * @brief 正規取得済みfileを用途13の固定値で標準cacheへ導入します
 * @param[in] sourceFile 絶対local通常file。sourceを書換えず永続記録しません
 * @param[in] cacheRoot 信頼済みlocal cache。workerで標準先を解決済みです
 * @param[in] stop commit前までの協調取消し
 * @param[in] progress 空可の同期実byte通知。commit後は呼びません
 * @return 用途・確定path・byte・再利用/commitの所有結果
 * @throws OperationError path/容量/hash/I/O/排他/取消し。旧最終fileを保持します
 * @note worker専用。モデルload、通信、生成は行いません
 * @see koefude::importWholeTextModel(const std::filesystem::path &, const std::filesystem::path &, std::stop_token, const koefude::WholeTextImportCallback &)
 */
//------------------------------------------------------------------------------
WholeTextModelImportResult importWholeTextModel( const std::filesystem::path &sourceFile,
												const std::filesystem::path &cacheRoot, std::stop_token stop,
												const WholeTextImportCallback &progress)
{
	return WholeTextModelImporter::run( sourceFile, cacheRoot, modelDefinition( wholeTextNativeModelId()), true, stop,
									   progress, {}) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 明示方式の単一registryモデルだけを準備します
 * @param[in] variant 固定方式
 * @param[in] sourceFile 元file
 * @param[in] cacheRoot 標準cache
 * @param[in] stop 停止
 * @param[in] progress 実byte通知
 * @return 固定用途の所有結果
 * @see koefude::importWholeTextModel(koefude::WholeTextRuntimeVariant, const std::filesystem::path &, const std::filesystem::path &, std::stop_token, const koefude::WholeTextImportCallback &)
 */
//------------------------------------------------------------------------------
WholeTextModelImportResult importWholeTextModel( WholeTextRuntimeVariant		 variant,
												const std::filesystem::path &sourceFile,
												const std::filesystem::path &cacheRoot, std::stop_token stop,
												const WholeTextImportCallback &progress)
{
	const auto role = wholeTextNativeModelId( variant) ; // 単一registry
	auto	   result =
		WholeTextModelImporter::run( sourceFile, cacheRoot, modelDefinition( role), role == ModelId::WHOLE_TEXT_GEMMA,
									stop, progress, {}) ; // 14は通常8GiB staging
	result.role = role ;
	return result ;
}
//------------------------------------------------------------------------------
/**
 * @brief 固定値または自試験の信頼済み小fileだけを処理します
 * @param[in] sourceFile 元file
 * @param[in] cacheRoot cache
 * @param[in] definition 信頼済み期待値
 * @param[in] production trueは用途13factory
 * @param[in] stop 停止
 * @param[in] progress 実段階通知
 * @param[in] hooks 人工境界
 * @return 所有導入結果
 * @throws OperationError 検証・I/O・取消し
 * @see koefude::WholeTextModelImporter::run
 */
//------------------------------------------------------------------------------
WholeTextModelImportResult WholeTextModelImporter::run( const std::filesystem::path &sourceFile,
													   const std::filesystem::path &cacheRoot,
													   const ModelDefinition &definition, bool production,
													   std::stop_token stop, const WholeTextImportCallback &progress,
													   const Hooks &hooks)
{
	checkCancelled( stop) ;
	validateSourceName( sourceFile) ;
	const auto cache = validateModelDirectory( cacheRoot) ; // 回収前の安全なlocal先
	rejectStagingSource( sourceFile, cache) ;
	WholeTextModelImportResult result ; // 成功時の全所有値はcommit前に用意します
	result.path = cache / definition.file ;
	result.bytes = definition.bytes ;
	// 期待値のoverflowを空き容量加算とfile I/Oより前に拒否します
	if( definition.bytes == 0 ||
	   definition.bytes > static_cast<std::uint64_t>( std::numeric_limits<std::int64_t>::max())) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_BAD_LENGTH) ;
	}
	ModelCacheTransaction transaction( cache, 0) ; // 一度だけ検査し再利用は追加容量0
	// 検証済みcacheはsourceの存在確認/data/copyをせず再利用します
	if( inspectModelFile( cache, definition, stop) == ModelPresence::VALID) {
		result.reusedCache = true ;
		return result ;
	}
	// 実段階通知は全てcommit前にだけ行います
	const auto notify =
		//------------------------------------------------------------------------------
		/**
		 * @brief commit前だけ実byteを通知します
		 * @param[in] stage 実段階
		 * @param[in] completed 実処理byte
		 * @param[in] total 全byte、0は不明
		 */
		//------------------------------------------------------------------------------
		[&]( WholeTextImportStage stage, std::uint64_t completed, std::uint64_t total)
		{
			checkCancelled( stop) ;
			// 空callbackを呼ばず実処理byteだけを渡します
			if( progress) {
				progress( {stage, completed, total}) ;
			}
		} ; // この同期呼出し内だけ有効なborrow
	notify( WholeTextImportStage::SOURCE_VALIDATION, 0, 0) ;
	static_cast<void>( validateModelDirectory( sourceFile.parent_path())) ;
	const auto	 source = validateLocalFile( sourceFile) ; // 祖先・leafのcloud/reparseを拒否
	UniqueHandle lease( CreateFileW( source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
								   FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
								   nullptr)) ; // コピーの全期間write/deleteを拒否
	// 元Win32 codeを保持してsource競合を通知します
	if( lease.get() == INVALID_HANDLE_VALUE) {
		throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
	}
	BY_HANDLE_FILE_INFORMATION info{} ; // 実handleの属性とlink数
	// 属性照会の失敗をサイズ不一致と混同しません
	if( !GetFileInformationByHandle( lease.get(), &info)) {
		throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
	}
	constexpr DWORD K_REJECTED = FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_OFFLINE |
								 FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS ; // 暗黙の通信等
	// hardlink、非disk、差替え済みreparse等をdata読取り前に拒否します
	if( GetFileType( lease.get()) != FILE_TYPE_DISK || info.nNumberOfLinks != 1 ||
	   (info.dwFileAttributes & K_REJECTED) != 0) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_ACCESS_DENIED) ;
	}
	LARGE_INTEGER length{} ; // 正確なsigned64実長
	// OSの長さ照会失敗は元codeを保持します
	if( !GetFileSizeEx( lease.get(), &length)) {
		throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
	}
	// size違いのfileはcopy/hashしません
	if( length.QuadPart < 0 || static_cast<std::uint64_t>( length.QuadPart) != definition.bytes) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_BAD_LENGTH) ;
	}
	constexpr std::uint64_t K_HEADROOM = 256ULL * 1024 * 1024 ; // コピー前の空き余裕
	const auto				required = definition.bytes + K_HEADROOM ; // signed64上限検査により加算可能
	// 小fixture試験以外は実disk空き容量を検査します
	if( hooks.space) {
		hooks.space( required) ;
	} else {
		ULARGE_INTEGER available{} ; // 現利用者の利用可能byte
		// 空き容量不明なら大きい書込みを始めません
		if( !GetDiskFreeSpaceExW( cache.c_str(), &available, nullptr, nullptr)) {
			throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
		}
		// 不足を通信やcopy開始前に拒否します
		if( available.QuadPart < required) {
			throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_DISK_FULL) ;
		}
	}
	const auto createStaging =
		//------------------------------------------------------------------------------
		/**
		 * @brief 非可動資源を直接構築します
		 * @return 単独所有の一時file
		 */
		//------------------------------------------------------------------------------
		[&]() -> ModelStagingFile
		{
			// 製品経路は用途13の固定factoryだけを使います
			if( production) {
				return ModelStagingFile::forWholeTextImport( transaction.stagingRoot()) ;
			}
			return ModelStagingFile( transaction.stagingRoot(), definition.bytes) ;
		} ; // 小fixtureの通常上限を製品へ公開しません
	auto						 staged = createStaging() ; // 返却値の直接構築で所有handleを複製しません
	std::array<std::byte, 65536> buffer{} ; // 全weightを保持しない固定作業領域
	std::uint64_t				 total = 0 ; // 成功したcopyの64bit累計
	notify( WholeTextImportStage::COPYING, 0, definition.bytes) ;
	// 固定長sourceのEOFまたは取消しまで有界blockを一つずつcopyします
	for(;;) {
		checkCancelled( stop) ;
		DWORD read = 0 ; // 今回の実読取りbyte
		// 部分的なI/O失敗は成功にしません
		if( !ReadFile( lease.get(), buffer.data(), static_cast<DWORD>( buffer.size()), &read, nullptr)) {
			throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
		}
		// EOFで正確な全長を照合します
		if( read == 0) {
			break ;
		}
		// 加算前に上限を確認しoverflowも拒否します
		if( read > definition.bytes - total) {
			throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_BAD_LENGTH) ;
		}
		// 人工失敗の注入は一時file確定前だけです
		if( hooks.copying) {
			hooks.copying( total) ;
		}
		// 小fixtureだけは短書を注入でき、製品は常に実全書込みを使います
		if( hooks.append) {
			hooks.append( staged, std::span<const std::byte>( buffer.data(), read)) ;
		} else {
			staged.append( std::span<const std::byte>( buffer.data(), read)) ;
		}
		total += read ;
		notify( WholeTextImportStage::COPYING, total, definition.bytes) ;
	}
	// 短いsourceを成功にしません
	if( total != definition.bytes) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_BAD_LENGTH) ;
	}
	notify( WholeTextImportStage::VERIFYING, 0, definition.bytes) ;
	staged.verify( definition.bytes, definition.sha256, stop,
				  //------------------------------------------------------------------------------
				  /**
				   * @brief 実hash進捗を通知します
				   * @param[in] bytes 検証済みbyte
				   */
				  //------------------------------------------------------------------------------
				  [&]( std::uint64_t bytes)
				  {
					  notify( WholeTextImportStage::VERIFYING, bytes, definition.bytes) ;
				  }) ;
	notify( WholeTextImportStage::COMMITTING, 0, 0) ;
	checkCancelled( stop) ; // ここが最後の取消し境界。callback後も再確認します
	// 試験の同期commit seamでも成功renameの後は例外を追加しません
	if( hooks.commit) {
		hooks.commit( staged, result.path) ;
	} else {
		staged.commit( result.path) ;
	}
	result.importCommitted = true ; // 実rename成功の後だけ設定します
	return result ; // callback/再hash/allocationなし、noexcept所有moveだけ
}
} // namespace koefude
