//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file model_store.cpp
 * @brief モデルをSHA-256とsizeで照合し、使用中の変更を防ぎます
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 固定モデルの検証とlockを実装
 * 0.1.0 Shunji Ogawa 2026/09/09 モデル系統と品質を生成registryで解決
 * 0.1.0 Shunji Ogawa 2026/09/10 CPU専用ASRの構成・依存tokenを単一registryで解決
 * 0.1.0 Shunji Ogawa 2026/09/13 取得metadataを公開し、内容不一致とI/O失敗を区別
 * 0.1.0 Shunji Ogawa 2026/10/19 単一lockからlocal全文用途を公開
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
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
#include "inference/model_store.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

#include <bcrypt.h>
#include <windows.h>

#include "core/types.h"
#include "model_manifest.h"

namespace koefude {
namespace {

/** @brief CNG algorithm providerを単独所有するdeleterです */
struct AlgorithmDeleter {
	//------------------------------------------------------------------------------
	/**
	 * @brief algorithm providerを閉じます
	 * @param[in] value 有効な所有handle
	 */
	//------------------------------------------------------------------------------
	void operator()( BCRYPT_ALG_HANDLE value) const noexcept
	{
		BCryptCloseAlgorithmProvider( value, 0) ;
	}
} ;

/** @brief CNG hash stateを単独所有するdeleterです */
struct HashDeleter {
	//------------------------------------------------------------------------------
	/**
	 * @brief hashの作業領域を解放します
	 * @param[in] value 有効な所有handle
	 */
	//------------------------------------------------------------------------------
	void operator()( BCRYPT_HASH_HANDLE value) const noexcept
	{
		BCryptDestroyHash( value) ;
	}
} ;

//------------------------------------------------------------------------------
/**
 * @brief CNG失敗の元codeを共通例外へ変換します
 * @param[in] status CNGのNTSTATUS
 * @throws OperationError statusが失敗の場合
 */
//------------------------------------------------------------------------------
void checkStatus( const NTSTATUS status)
{
	// providerの失敗時に未計算hashを正しい値として比較しない
	if( status < 0) {
		throw OperationError( ErrorKind::MODEL_REJECTED, static_cast<std::uint32_t>( status)) ;
	}
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 利用可能な全ASR構成を返します
 * @return process寿命中有効な不変view
 * @see koefude::asrDefinitions
 */
//------------------------------------------------------------------------------
std::span<const AsrDefinition> asrDefinitions() noexcept
{
	return K_ASR_DEFINITIONS ;
}

//------------------------------------------------------------------------------
/**
 * @brief 選択可能な構成が単一registryに存在するか確認します
 * @param[in] family モデル系統
 * @param[in] profile 系統内の構成
 * @return 存在すればprocess寿命の借用pointer、未定義ならNULL
 * @see koefude::findAsrDefinition
 */
//------------------------------------------------------------------------------
const AsrDefinition *findAsrDefinition( const AsrModel family, const AsrProfile profile) noexcept
{
	// 固定された構成を走査し、一致しない入力を別モデルへ置き換えない
	for( const auto &definition : K_ASR_DEFINITIONS) {
		// 系統と品質の両方が一致したweightだけを選ぶ
		if( definition.family == family && definition.profile == profile) {
			return &definition ;
		}
	}
	return nullptr ;
}

//------------------------------------------------------------------------------
/**
 * @brief モデルと品質を一つの固定weightへ解決します
 * @param[in] family 利用者が選択したモデル系統
 * @param[in] profile 選択した系統内の品質
 * @return process寿命中有効なregistryの不変参照
 * @throws OperationError 未定義の組合せならInvalidInput
 * @see koefude::asrDefinition
 */
//------------------------------------------------------------------------------
const AsrDefinition &asrDefinition( const AsrModel family, const AsrProfile profile)
{
	const auto *definition = findAsrDefinition( family, profile) ; // 単一registryの借用参照
	// 未定義構成を別の品質やモデルへ暗黙に置き換えない
	if( !definition) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	return *definition ;
}

//------------------------------------------------------------------------------
/**
 * @brief 用途に対応する単一registryの定義を返します
 * @param[in] id 固定されたモデル用途
 * @return processの寿命中有効な不変定義への参照
 * @throws OperationError 未定義の用途の場合
 * @see koefude::modelDefinition
 */
//------------------------------------------------------------------------------
const ModelDefinition &modelDefinition( const ModelId id)
{
	const auto index = static_cast<std::size_t>( id) ; // registry内の固定index
	// 未定義enumのcastで任意のmetadataを参照しない
	if( index >= K_MODEL_DEFINITIONS.size()) {
		throw OperationError( ErrorKind::MODEL_REJECTED) ;
	}
	return K_MODEL_DEFINITIONS[index] ;
}

//------------------------------------------------------------------------------
/**
 * @brief 固定sizeとSHA-256を確認し、engine終了までfile変更を防ぎます
 * @param[in] root 利用者が選んだlocal model directory
 * @param[in] id 検証する固定用途
 * @param[in] stop hash読取り中の取消し
 * @return 検証済みpathと所有lock。返り値をengine contextより長く保持すること
 * @throws OperationError 未準備、hash不一致、I/O失敗、取消し
 * @see koefude::verifyModel
 */
//------------------------------------------------------------------------------
VerifiedModel verifyModel( const std::filesystem::path &root, const ModelId id, const std::stop_token stop)
{
	auto verified = checkModelFile( root, modelDefinition( id), stop) ; // 内容一致時の所有lock
	// 内容不一致を既存engine契約のモデル拒否へ変換する
	if( !verified) {
		throw OperationError( ErrorKind::MODEL_REJECTED) ;
	}
	return std::move( *verified) ;
}

//------------------------------------------------------------------------------
/**
 * @brief local全文モデルの生成用途を返します
 * @return 単一lock由来の固定用途
 * @see koefude::wholeTextNativeModelId()
 */
//------------------------------------------------------------------------------
ModelId wholeTextNativeModelId() noexcept
{
	return K_WHOLE_TEXT_NATIVE_MODEL_ID ;
}
//------------------------------------------------------------------------------
/**
 * @brief 明示方式の単一registry用途を返します
 * @param[in] variant 固定方式
 * @return registryの対応用途。不明値は拒否します
 * @see koefude::wholeTextNativeModelId(koefude::WholeTextRuntimeVariant)
 */
//------------------------------------------------------------------------------
ModelId wholeTextNativeModelId( WholeTextRuntimeVariant variant)
{
	static_cast<void>( wholeTextRuntimeDescriptor( variant)) ; // 不明方式は用途解決前に拒否
	return variant == WholeTextRuntimeVariant::MINISTRAL14_B_AVX2_NO_REPACK16_GB ? K_WHOLE_TEXT_NATIVE16_GB_MODEL_ID
																				 : K_WHOLE_TEXT_NATIVE_MODEL_ID ;
}

//------------------------------------------------------------------------------
/**
 * @brief 共有用途を取得します
 * @return process寿命中有効な不変参照
 * @see koefude::sharedModelRoles
 */
//------------------------------------------------------------------------------
const SharedModelRoles &sharedModelRoles() noexcept
{
	return K_SHARED_MODEL_ROLES ;
}

//------------------------------------------------------------------------------
/**
 * @brief 固定用途の取得metadataを返します
 * @param[in] id 単一registryの用途
 * @return process寿命中有効な不変参照
 * @throws OperationError 未定義の用途の場合
 * @see koefude::modelDownloadDefinition
 */
//------------------------------------------------------------------------------
const ModelDownloadDefinition &modelDownloadDefinition( const ModelId id)
{
	const auto index = static_cast<std::size_t>( id) ; // 固定registry内のindex
	// 不正enumで配列外の取得先を参照しない
	if( index >= K_MODEL_DOWNLOADS.size()) {
		throw OperationError( ErrorKind::MODEL_REJECTED) ;
	}
	return K_MODEL_DOWNLOADS[index] ;
}

//------------------------------------------------------------------------------
/**
 * @brief 信頼済み定義のfileを固定size/hashで照合し変更拒否lockを所有します
 * @param[in] root local model directory
 * @param[in] definition 呼出し中有効なregistryまたはtestの信頼済み期待値。外部入力は禁止
 * @param[in] stop hash読取り中の取消し
 * @return 内容一致なら検証済みpathと所有lock、size/hash不一致なら空。engineより長く保持
 * @throws OperationError 欠落、属性拒否、I/O失敗、取消し。I/O失敗は空結果に変換しません
 * @note worker専用のblocking APIです。mutex保持中に呼び出しません
 * @see koefude::checkModelFile
 */
//------------------------------------------------------------------------------
std::optional<VerifiedModel> checkModelFile( const std::filesystem::path &root, const ModelDefinition &definition,
											const std::stop_token stop)
{
	checkCancelled( stop) ;
	std::filesystem::path path ; // 検証後だけengineへ渡す所有path
	try {
		path = validateLocalFile( root / definition.file) ;
	} catch( const OperationError &error) {
		// 未準備と属性拒否を区別し、モデルを音声入力エラーとして表示しない
		const bool missing = error.code() == ERROR_FILE_NOT_FOUND ||
							 error.code() == ERROR_PATH_NOT_FOUND ; // 初回取得で準備できる欠落だけの条件
		throw OperationError( missing ? ErrorKind::MODEL_MISSING : ErrorKind::MODEL_REJECTED, error.code()) ;
	}
	UniqueHandle file( CreateFileW( path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
								  FILE_FLAG_SEQUENTIAL_SCAN,
								  nullptr)) ; // write/deleteを拒否するlock
	// access denied等を未取得として隠さず、欠落とI/O失敗を区別する
	if( file.get() == INVALID_HANDLE_VALUE) {
		const DWORD code = GetLastError() ; // openの元診断値
		const bool	missing =
			code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND ; // open直前に消失したfileの条件
		throw OperationError( missing ? ErrorKind::MODEL_MISSING : ErrorKind::MODEL_REJECTED, code) ;
	}
	LARGE_INTEGER length{} ; // 開いたfileの実byte数
	// 属性取得失敗を内容破損と扱って再取得を勧めない
	if( !GetFileSizeEx( file.get(), &length)) {
		throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
	}
	// size不一致のfileをengineへ渡す前に拒否する
	if( length.QuadPart < 0 || static_cast<std::uint64_t>( length.QuadPart) != definition.bytes) {
		return std::nullopt ;
	}
	BCRYPT_ALG_HANDLE rawAlgorithm = nullptr ; // CNG作成時だけ使う受取handle
	checkStatus( BCryptOpenAlgorithmProvider( &rawAlgorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) ;
	const std::unique_ptr<void, AlgorithmDeleter> algorithm( rawAlgorithm) ; // providerを所有
	BCRYPT_HASH_HANDLE							  rawHash = nullptr ; // CNG作成時だけ使う受取hash state
	checkStatus( BCryptCreateHash( algorithm.get(), &rawHash, nullptr, 0, nullptr, 0, 0)) ;
	const std::unique_ptr<void, HashDeleter> hash( rawHash) ; // hash stateを所有
	std::array<unsigned char, 64 * 1024>	 buffer{} ; // 固定上限の読取りbuffer
	std::uint64_t							 total = 0 ; // sizeと照合する実読取りbyte数
	// EOFまたは取消しまで固定sizeのblockでhashし、モデル全体をRAMへ複製しない
	for(;;) {
		checkCancelled( stop) ;
		DWORD bytesRead = 0 ; // 今回読めたbyte数
		// I/O失敗時に部分hashでの比較を続けない
		if( !ReadFile( file.get(), buffer.data(), static_cast<DWORD>( buffer.size()), &bytesRead, nullptr)) {
			throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
		}
		// EOFでCNG hashを確定する
		if( bytesRead == 0) {
			break ;
		}
		total += bytesRead ;
		// size取得後に予想外の読取りが発生しても無制限に継続しない
		if( total > definition.bytes) {
			return std::nullopt ;
		}
		checkStatus( BCryptHashData( hash.get(), buffer.data(), bytesRead, 0)) ;
	}
	std::array<unsigned char, 32> digest{} ; // SHA-256の32byte結果
	checkStatus( BCryptFinishHash( hash.get(), digest.data(), static_cast<ULONG>( digest.size()), 0)) ;
	constexpr std::string_view K_HEX = "0123456789abcdef" ; // 固定小文字hexの変換表
	std::string				   actual ; // 比較する64桁のSHA-256
	actual.reserve( 64) ;
	// 全digest byteを同じ表現へ変換してregistryの期待値と照合する
	for( const unsigned char byte : digest) {
		actual.push_back( K_HEX[byte >> 4U]) ;
		actual.push_back( K_HEX[byte & 15U]) ;
	}
	// 読取り中の短縮や内容不一致をfail-closedで拒否する
	if( total != definition.bytes || actual != definition.sha256) {
		return std::nullopt ;
	}
	return VerifiedModel{path, std::move( file)} ;
}

} // namespace koefude
