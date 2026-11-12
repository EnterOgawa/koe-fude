//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_runtime.cpp
 * @brief 固定二方式のCPU契約と読込み前の完成runtime検証を管理します
 * @version 1.0.0
 * @date 2026/10/19
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/19 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/19 ADR1273の通常製品runtime契約を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1284の生成しない計数と有限計画の境界を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1291の生成slot契約を通常factoryの全memberへ伝播
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1343の文章専用補正を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実使用の標準headerを直接include
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1424の既知の行頭空白だけを整形
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "inference/whole_text_runtime.h"
#include "core/types.h"
#include "inference/whole_text_bounded_preparation.h"
#include "inference/whole_text_native_preparation.h"
#include "whole_text_runtime_manifest.h"
#include <array>
#include <algorithm>
#include <bcrypt.h>
#include <json.hpp>
#include <set>
#include <utility>
#include <vector>
#include <windows.h>

namespace koefude {
namespace {
using Json = nlohmann::json ; // 固定依存のstrict JSON
constexpr WholeTextRuntimeDescriptor K_PORTABLE{
	WholeTextRuntimeVariant::PORTABLE, "gemma4-31b-qat-q4-0-cpu-whole-text-v1",
	"static-crt-portable-cpu-no-native-no-openmp-no-gpu-v1", L"KoeFude.WholeText.dll",
	L"KoeFude.WholeText.runtime.json"} ; // 旧契約
constexpr WholeTextRuntimeDescriptor K_AVX2{
	WholeTextRuntimeVariant::AVX2_NO_REPACK, "gemma4-31b-qat-q4-0-cpu-avx2-norepack-whole-text-v1",
	"static-crt-avx2-norepack-cpu-no-native-no-openmp-no-gpu-v1", L"KoeFude.WholeText.Avx2NoRepack.dll",
	L"KoeFude.WholeText.Avx2NoRepack.runtime.json"} ; // 新契約
constexpr WholeTextRuntimeDescriptor K_MINISTRAL{
	WholeTextRuntimeVariant::MINISTRAL14_B_AVX2_NO_REPACK16_GB,
	"ministral3-14b-q4-k-m-cpu-avx2-norepack-16gb-bounded-v1",
	"static-crt-avx2-norepack-cpu-no-native-no-openmp-no-gpu-v1", L"KoeFude.WholeText.Ministral14B.Avx2NoRepack.dll",
	L"KoeFude.WholeText.Ministral14B.Avx2NoRepack.runtime.json"} ; // 独立した第三slot
//------------------------------------------------------------------------------
/**
 * @brief 検証失敗を固定診断で返します
 * @param[in] ok 成立条件
 * @param[in] code 用途別診断
 */
//------------------------------------------------------------------------------
void requireRuntime( bool ok, std::uint32_t code = 0x57560002)
{
	// 不明な構成を読込みへ渡しません
	if( !ok) {
		throw OperationError( ErrorKind::MODEL_REJECTED, code) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 同じlocal通常fileをread-only leaseします
 * @param[in] path 検証対象
 * @return write/delete拒否handle
 */
//------------------------------------------------------------------------------
UniqueHandle lease( const std::filesystem::path &path)
{
	try {
		static_cast<void>( validateLocalFile( path)) ;
	} catch( const OperationError &error) {
		// 欠落だけを配布不足へ写し、その他は構成拒否へ区別します
		if( error.code() == ERROR_FILE_NOT_FOUND || error.code() == ERROR_PATH_NOT_FOUND) {
			throw OperationError( ErrorKind::MODEL_MISSING, 0x57560003) ;
		}
		throw OperationError( ErrorKind::MODEL_REJECTED, 0x57560002) ;
	}
	UniqueHandle handle( CreateFileW( path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
									FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)) ; // 固定所有
	// 欠落と構成不正を区別します
	if( !handle || handle.get() == INVALID_HANDLE_VALUE) {
		throw OperationError( ErrorKind::MODEL_MISSING, 0x57560003) ;
	}
	BY_HANDLE_FILE_INFORMATION information{} ; // 同じ開いたfileの属性
	requireRuntime( GetFileInformationByHandle( handle.get(), &information) &&
				   (information.dwFileAttributes &
					(FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_OFFLINE |
					 FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS)) == 0) ;
	return handle ;
}
//------------------------------------------------------------------------------
/**
 * @brief 全byteを有界readと協調停止でSHA-256照合します
 * @param[in] file 同じlease
 * @param[in] bytes 期待byte数
 * @param[in] stop 停止
 * @param[out] captured null可、小sidecarだけ全量保持
 * @return lower-case SHA-256
 */
//------------------------------------------------------------------------------
std::string hashFile( HANDLE file, std::uint64_t bytes, std::stop_token stop, std::string *captured = nullptr)
{
	LARGE_INTEGER size{} ; // 実file長
	requireRuntime( GetFileSizeEx( file, &size) && size.QuadPart >= 0 &&
				   static_cast<std::uint64_t>( size.QuadPart) == bytes) ;
	BCRYPT_ALG_HANDLE algorithm = nullptr ; // SHA provider
	requireRuntime( BCryptOpenAlgorithmProvider( &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0) ;
	const auto closeAlgorithm =
		//------------------------------------------------------------------------------
		/**
		 * @brief unique_ptrが所有するCNG algorithm providerのcloseを要求します
		 * @param[in] value 正常BCryptOpenAlgorithmProviderで取得した非NULL handleです
		 * @note BCryptCloseAlgorithmProviderのNTSTATUSは検査せず、解放成功を確認した状態は返しません
		 */
		//------------------------------------------------------------------------------
		[]( void *value)
		{
			BCryptCloseAlgorithmProvider( value, 0) ;
		} ;
	const std::unique_ptr<void, decltype(closeAlgorithm)> provider( algorithm, closeAlgorithm) ;
	BCRYPT_HASH_HANDLE									  rawHash = nullptr ; // 同期hash state
	requireRuntime( BCryptCreateHash( algorithm, &rawHash, nullptr, 0, nullptr, 0, 0) >= 0) ;
	const auto closeHash =
		//------------------------------------------------------------------------------
		/**
		 * @brief unique_ptrが所有するCNG hash stateの破棄を要求します
		 * @param[in] value 正常BCryptCreateHashで取得した非NULL handleです
		 * @note BCryptDestroyHashのNTSTATUSは検査せず、破棄成功を確認した状態は返しません
		 */
		//------------------------------------------------------------------------------
		[]( void *value)
		{
			BCryptDestroyHash( value) ;
		} ;
	const std::unique_ptr<void, decltype(closeHash)> hash( rawHash, closeHash) ;
	std::array<std::uint8_t, 65536>					 buffer{} ; // RAM上限固定
	std::uint64_t									 read = 0 ; // 完了byte数
	// 期待全byteを一度読み、各chunkで停止を照会します
	while( read < bytes) {
		checkCancelled( stop) ;
		const auto wanted = static_cast<DWORD>( std::min<std::uint64_t>( buffer.size(), bytes - read)) ; // 有界
		DWORD	   actual = 0 ; // 同期read数
		requireRuntime( ReadFile( file, buffer.data(), wanted, &actual, nullptr) && actual == wanted) ;
		requireRuntime( BCryptHashData( rawHash, buffer.data(), actual, 0) >= 0) ;
		// sidecarだけを上限検証後に保持します
		if( captured) {
			captured->append( reinterpret_cast<const char *>( buffer.data()), actual) ;
		}
		read += actual ;
	}
	std::array<std::uint8_t, 32> digest{} ; // 全SHA
	requireRuntime( BCryptFinishHash( rawHash, digest.data(), 32, 0) >= 0) ;
	constexpr char K_HEX[] = "0123456789abcdef" ; // lower-case alphabet
	std::string	   text ; // 64byte
	// 全digestを固定順hexへ変換します
	for( const auto value : digest) { // SHAの一byte
		text.push_back( K_HEX[value >> 4]) ;
		text.push_back( K_HEX[value & 15]) ;
	}
	return text ;
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 固定descriptorを返します
 * @param[in] variant 二方式のいずれか
 * @return 静的寿命の契約
 * @see koefude::wholeTextRuntimeDescriptor
 */
//------------------------------------------------------------------------------
const WholeTextRuntimeDescriptor &wholeTextRuntimeDescriptor( WholeTextRuntimeVariant variant)
{
	// 各方式を固有descriptorへ束縛し、未知値を置換しません
	switch( variant) {
		case WholeTextRuntimeVariant::PORTABLE: // 既存portable
			return K_PORTABLE ;
		case WholeTextRuntimeVariant::AVX2_NO_REPACK: // 既存Gemma AVX2
			return K_AVX2 ;
		case WholeTextRuntimeVariant::MINISTRAL14_B_AVX2_NO_REPACK16_GB: // 固有Ministral
			return K_MINISTRAL ;
	}
	requireRuntime( false) ;
	return K_PORTABLE ;
}
//------------------------------------------------------------------------------
/**
 * @brief 新規要求だけをid/digestで完全照合します
 * @param[in] id profile名
 * @param[in] digest 全SHA
 * @return 一致時のみ静的契約、他はnull
 * @see koefude::findWholeTextRuntime
 */
//------------------------------------------------------------------------------
const WholeTextRuntimeDescriptor *findWholeTextRuntime( std::string_view id, const std::array<std::uint8_t, 32> &digest)
{
	// 小さい二方式だけを新規実行用に照合します。歴史表示へ適用しません
	for( const auto *descriptor : {&K_PORTABLE, &K_AVX2}) { // 固定候補
		// 名前と全digestが一致した要求だけを受け付けます
		if( descriptor->profileId == id && wholeTextNativeProfile( descriptor->variant).sha256 == digest) {
			return descriptor ;
		}
	}
	return nullptr ;
}
//------------------------------------------------------------------------------
/**
 * @brief 新計数profileだけを照合します
 * @param[in] id 新方式
 * @param[in] digest 全設定SHA
 * @return 固定descriptorかnull
 * @see koefude::findWholeTextBoundedRuntime
 */
//------------------------------------------------------------------------------
const WholeTextRuntimeDescriptor *findWholeTextBoundedRuntime( std::string_view					  id,
															  const std::array<std::uint8_t, 32> &digest)
{
	// 新profileも同じ固定第三slotだけを使い、native receipt値は変更しません
	if( id == K_TEXT_CONTEXT_PROFILE_ID && digest == textContextProfile().sha256) {
		return &K_MINISTRAL ;
	}
	static const WholeTextRuntimeDescriptor portable{
		WholeTextRuntimeVariant::PORTABLE, "gemma4-31b-qat-q4-0-cpu-bounded-whole-text-v2", K_PORTABLE.cpuContract,
		K_PORTABLE.dllName, K_PORTABLE.receiptName} ;
	static const WholeTextRuntimeDescriptor avx2{WholeTextRuntimeVariant::AVX2_NO_REPACK,
												 "gemma4-31b-qat-q4-0-cpu-avx2-norepack-bounded-v2", K_AVX2.cpuContract,
												 K_AVX2.dllName, K_AVX2.receiptName} ;
	// 二つの新方式だけを完全照合し、旧profileは拒否します
	for( const auto *descriptor : {&portable, &avx2, &K_MINISTRAL}) {
		// 成立する条件だけを処理し、未成立の値は元の状態へ残します
		if( id == descriptor->profileId && digest == wholeTextBoundedProfile( descriptor->variant).sha256) {
			return descriptor ;
		}
	}
	return nullptr ;
}
//------------------------------------------------------------------------------
/**
 * @brief 検証factoryだけがleaseを封印します
 * @param[in] variant CPU契約
 * @param[in] path 正規path
 * @param[in] dll DLLの所有lease
 * @param[in] receipt sidecar所有lease
 * @param[in] accountingV2 schema3を明示検証した事実
 * @param[in] capabilitiesMask 検証済みcapability全bit、旧契約は0
 * @see koefude::VerifiedWholeTextRuntime::VerifiedWholeTextRuntime(koefude::WholeTextRuntimeVariant, std::filesystem::path, koefude::UniqueHandle, koefude::UniqueHandle, bool, std::uint32_t)
 */
//------------------------------------------------------------------------------
VerifiedWholeTextRuntime::VerifiedWholeTextRuntime( WholeTextRuntimeVariant variant, std::filesystem::path path,
												   UniqueHandle dll, UniqueHandle receipt, bool accountingV2,
												   std::uint32_t capabilitiesMask)
	: variant_( variant), path_( std::move( path)), dll_( std::move( dll)), receipt_( std::move( receipt)),
	  capabilitiesMask_( capabilitiesMask), accountingV2_( accountingV2)
{
}
//------------------------------------------------------------------------------
/**
 * @brief 同じ方式とpathへの束縛を調べます
 * @param[in] variant 要求方式
 * @param[in] path 固定絶対path
 * @return 同じownerならtrue
 * @see koefude::VerifiedWholeTextRuntime::matches
 */
//------------------------------------------------------------------------------
bool VerifiedWholeTextRuntime::matches( WholeTextRuntimeVariant		variant,
									   const std::filesystem::path &path) const noexcept
{
	return variant_ == variant && path_ == path && dll_ && receipt_ ;
}
//------------------------------------------------------------------------------
/**
 * @brief 検証済み絶対pathを返します
 * @return owner寿命まで有効なpath
 * @see koefude::VerifiedWholeTextRuntime::path
 */
//------------------------------------------------------------------------------
const std::filesystem::path &VerifiedWholeTextRuntime::path() const noexcept
{
	return path_ ;
}
//------------------------------------------------------------------------------
/**
 * @brief compiled完成pinを返します
 * @param[in] variant 固定方式
 * @return pathを含まない構築時期待値
 * @see koefude::wholeTextRuntimePin
 */
//------------------------------------------------------------------------------
WholeTextRuntimePin wholeTextRuntimePin( WholeTextRuntimeVariant variant)
{
	// CMakeが検証した二slotのみを参照します
	if( variant == WholeTextRuntimeVariant::PORTABLE) {
		return {KOEFUDE_WT_PORTABLE_AVAILABLE,
				KOEFUDE_WT_PORTABLE_BYTES,
				KOEFUDE_WT_PORTABLE_SHA,
				KOEFUDE_WT_PORTABLE_RECEIPT_SHA,
				KOEFUDE_WT_CONFIGURATION,
				KOEFUDE_WT_PORTABLE_ACCOUNTING_V2,
				KOEFUDE_WT_PORTABLE_CAPABILITIES_MASK,
				KOEFUDE_WT_PORTABLE_ACCOUNTING_V2 ? KOEFUDE_WT_ABI_HEADER_SHA : ""} ;
	}
	// 未知variantを既存slotへ送らず固有pinへ対応付けます
	if( variant == WholeTextRuntimeVariant::MINISTRAL14_B_AVX2_NO_REPACK16_GB) {
		return {KOEFUDE_WT_MINISTRAL_AVAILABLE,
				KOEFUDE_WT_MINISTRAL_BYTES,
				KOEFUDE_WT_MINISTRAL_SHA,
				KOEFUDE_WT_MINISTRAL_RECEIPT_SHA,
				KOEFUDE_WT_CONFIGURATION,
				KOEFUDE_WT_MINISTRAL_ACCOUNTING_V2,
				KOEFUDE_WT_MINISTRAL_CAPABILITIES_MASK,
				KOEFUDE_WT_MINISTRAL_ACCOUNTING_V2 ? KOEFUDE_WT_ABI_HEADER_SHA : ""} ;
	}
	requireRuntime( variant == WholeTextRuntimeVariant::AVX2_NO_REPACK) ;
	return {KOEFUDE_WT_AVX2_AVAILABLE,
			KOEFUDE_WT_AVX2_BYTES,
			KOEFUDE_WT_AVX2_SHA,
			KOEFUDE_WT_AVX2_RECEIPT_SHA,
			KOEFUDE_WT_CONFIGURATION,
			KOEFUDE_WT_AVX2_ACCOUNTING_V2,
			KOEFUDE_WT_AVX2_CAPABILITIES_MASK,
			KOEFUDE_WT_AVX2_ACCOUNTING_V2 ? KOEFUDE_WT_ABI_HEADER_SHA : ""} ;
}
//------------------------------------------------------------------------------
/**
 * @brief baseline CPU gateの後にfile全byteを検証しownerを返します
 * @param[in] variant 固定方式
 * @param[in] path 固有名の絶対local DLL
 * @param[in] pin 構築時期待値
 * @param[in] cpu baseline収集値
 * @param[in] stop 協調停止
 * @return 全native callより長く保持するowner
 * @throws OperationError 拒否時はmodel/decode/ASR/DLL loadへ進みません
 * @note 小さい人工fileの内部試験も同じ境界を使用します
 * @see koefude::verifyWholeTextRuntimeWithPin
 */
//------------------------------------------------------------------------------
std::shared_ptr<const VerifiedWholeTextRuntime>
verifyWholeTextRuntimeWithPin( WholeTextRuntimeVariant variant, const std::filesystem::path &path,
							  const WholeTextRuntimePin &pin, const whole_text::cpu_candidate::Snapshot &cpu,
							  std::stop_token stop)
{
	checkCancelled( stop) ;
	const auto &descriptor = wholeTextRuntimeDescriptor( variant) ; // 固定basename
	// CPU拒否はfile探索・decode・モデル検証・loadより先です
	requireRuntime( whole_text::cpu_candidate::supports( variant == WholeTextRuntimeVariant::PORTABLE
														   ? whole_text::cpu_candidate::Variant::PORTABLE
														   : whole_text::cpu_candidate::Variant::AVX2,
													   cpu),
				   0x57560001) ;
	// 未配置slotをPATH等で補いません
	if( !pin.available) {
		throw OperationError( ErrorKind::MODEL_MISSING, 0x57560003) ;
	}
	requireRuntime( variant != WholeTextRuntimeVariant::MINISTRAL14_B_AVX2_NO_REPACK16_GB ||
				   (pin.accountingV2 && pin.capabilitiesMask == 3)) ;
	requireRuntime( path.is_absolute() && path.filename() == descriptor.dllName) ;
	auto		  dll = lease( path) ; // 全native call後まで保持
	const auto	  receiptPath = path.parent_path() / descriptor.receiptName ; // 固有sidecar
	auto		  receipt = lease( receiptPath) ; // 同じ所有scope
	LARGE_INTEGER receiptSize{} ; // 小receiptの実長
	requireRuntime( GetFileSizeEx( receipt.get(), &receiptSize) && receiptSize.QuadPart > 0 &&
				   receiptSize.QuadPart <= 8192) ;
	std::string bytes ; // 上限検証済みJSON全byte
	requireRuntime( hashFile( receipt.get(), static_cast<std::uint64_t>( receiptSize.QuadPart), stop, &bytes) ==
				   pin.receiptSha256) ;
	try {
		std::set<std::string> keys ; // rootの重複keyを拒否
		bool				  duplicate = false ; // parserの重複検出
		const auto			  json = Json::parse( bytes,
			//------------------------------------------------------------------------------
			/**
			 * @brief JSON最上位objectのkey重複を検出しながら全parse eventを採用します
			 * @param[in] depth parserが渡す構造の深さです。1のkey eventだけを調べます
			 * @param[in] event key/object/array/value等のparse event種別です
			 * @param[in] value 同期parse event中だけ借用するJSON値です。keyを文字列として読むだけで変更しません
			 * @return 常にtrueを返してparserによるeventの破棄を防ぎます。schemaの妥当性を示すboolではありません
			 * @note keysへの挿入とduplicateへのstickyな記録だけを行い、捕捉した局所変数は同期Json::parseより長く保持します
			 * @note 重複時もcallbackはtrueを返します。parse後のduplicate照合がOperationErrorでreceiptを拒否し、重複keyを採用成功へ変えません
			 */
			//------------------------------------------------------------------------------
			[&]( int depth, Json::parse_event_t event, Json &value)
			{
				// strict flat schemaのkeyだけを数えます
				if( depth == 1 && event == Json::parse_event_t::key) {
					duplicate = !keys.insert( value.get<std::string>()).second || duplicate ;
				}
				return true ;
			}) ; // 型も値も完全一致させます
		Json				  expected{{"schema", variant == WholeTextRuntimeVariant::PORTABLE ? 1 : 2},
									   {"configuration", pin.configuration},
									   {"architecture", "x64"},
									   {"engine_commit", "b1ff4ca23630ac7c5a275405353ddb5c486332c9"},
									   {"engine_tree", "69dd7b26d5ccd0e3e6ba9220847c040722757cb4"},
									   {"header_sha256", "b8424abc05b17c2ca1240f3d8a4b146e658f39d147042d4df5b5a3e6a755e555"},
									   {"cpu_contract", descriptor.cpuContract},
									   {"license_sha256", "bcd8ec749126d45cb06737d0690295d73df4b6e7e194205bcf91190368f27285"},
									   {"dll_name", toUtf8( std::filesystem::path( descriptor.dllName).wstring())},
									   {"dll_bytes", pin.dllBytes},
									   {"dll_sha256", pin.dllSha256}} ; // compiled pinとの照合
		// AVX2だけにschema2の五つの追加fieldを要求します
		if( variant != WholeTextRuntimeVariant::PORTABLE) {
			expected["variant"] = "avx2-norepack" ;
			expected["cpu_repack"] = false ;
			expected["required_features_mask"] = 32767 ;
			expected["xcr0_mask"] = 6 ;
			expected["abi_version"] = 1 ;
		}
		// schema3は明示V2 modeだけで認め、既存slotの意味を変えません
		if( pin.accountingV2) {
			requireRuntime( pin.capabilitiesMask != 0 && (pin.capabilitiesMask & ~3u) == 0 &&
						   (pin.capabilitiesMask & 1u) != 0 && pin.abiHeaderSha256 == KOEFUDE_WT_ABI_HEADER_SHA) ;
			expected["schema"] = 3 ;
			expected["variant"] = variant == WholeTextRuntimeVariant::MINISTRAL14_B_AVX2_NO_REPACK16_GB
									  ? "ministral14b-avx2-norepack"
									  : (variant == WholeTextRuntimeVariant::PORTABLE ? "portable" : "avx2-norepack") ;
			expected["cpu_repack"] = variant == WholeTextRuntimeVariant::PORTABLE ;
			expected["required_features_mask"] = variant == WholeTextRuntimeVariant::PORTABLE ? 0 : 32767 ;
			expected["xcr0_mask"] = variant == WholeTextRuntimeVariant::PORTABLE ? 0 : 6 ;
			expected["abi_version"] = 2 ;
			expected["capabilities_mask"] = pin.capabilitiesMask ;
			expected["accounting_contract"] = 2 ;
			expected["plan_contract"] = 2 ;
			expected["session_contract"] = 2 ;
			expected["abi_header_sha256"] = pin.abiHeaderSha256 ;
		}
		requireRuntime( !duplicate && json.is_object() && json.size() == expected.size()) ;
		// 数値と文字列・boolの相互変換を認めません
		for( const auto &item : expected.items()) { // 固定11/16field
			requireRuntime( json.contains( item.key()) && json.at( item.key()) == item.value() &&
						   (item.value().is_number_integer() ? json.at( item.key()).is_number_integer()
															 : json.at( item.key()).type() == item.value().type())) ;
		}
	} catch( const Json::exception &) {
		throw OperationError( ErrorKind::MODEL_REJECTED, 0x57560002) ;
	}
	requireRuntime( hashFile( dll.get(), pin.dllBytes, stop) == pin.dllSha256) ;
	checkCancelled( stop) ;
	// private constructor境界だけ直接newし直ちに共有ownerへ封印します
	return std::shared_ptr<const VerifiedWholeTextRuntime>( new VerifiedWholeTextRuntime(
		variant, path, std::move( dll), std::move( receipt), pin.accountingV2, pin.capabilitiesMask)) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 通常factoryでcompiled期待値と実CPUを検証します
 * @param[in] variant 要求方式
 * @param[in] path 固定絶対path
 * @param[in] stop 停止
 * @return 両fileの検証owner
 * @see koefude::verifyWholeTextRuntime
 */
//------------------------------------------------------------------------------
std::shared_ptr<const VerifiedWholeTextRuntime>
verifyWholeTextRuntime( WholeTextRuntimeVariant variant, const std::filesystem::path &path, std::stop_token stop)
{
	return verifyWholeTextRuntimeWithPin( variant, path, wholeTextRuntimePin( variant),
										 whole_text::cpu_candidate::queryHost(), stop) ;
}
} // namespace koefude
