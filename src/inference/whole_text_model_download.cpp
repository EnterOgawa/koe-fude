//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_model_download.cpp
 * @brief 既存transportと一時handleを使い固定Gemmaを原子的に取得します
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1283の用途限定Direct取得を追加
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
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
#include "inference/whole_text_model_download.h"
#include "core/types.h"
#include "inference/model_preparation.h"
#include "platform/model_download_io.h"
#include "platform/win_helpers.h"
#include <span>
#include <limits>
#include <type_traits>

namespace koefude {
static_assert( std::is_nothrow_move_constructible_v<WholeTextModelDownloadResult>) ;
static_assert( std::is_nothrow_move_assignable_v<WholeTextModelDownloadResult>) ;
//------------------------------------------------------------------------------
/**
 * @brief 固定Gemmaだけを検証・再利用または明示取得します
 * @param[in] bundleRoot 読取り専用同梱先。欠落cacheの場合だけ検査します
 * @param[in] cacheRoot 信頼済みlocal先。workerで標準先を解決済みです
 * @param[in,out] transport 既存同期HTTPS境界
 * @param[in] stop commit前までの協調取消し
 * @param[in] progress 空可の同期通知。commit後は呼びません
 * @return 確定前に所有値を確保した結果
 * @throws OperationError 通信・容量・保存・hash・排他・取消し。旧fileは保持します
 * @pre 表示した用途・容量・条件・保存先・通信先を確認した明示操作に限ります
 * @note worker専用。再試行・モデルload・生成・commit後の再検査を行いません
 * @see koefude::downloadWholeTextModel(const std::filesystem::path &, const std::filesystem::path &, koefude::IModelTransport &, std::stop_token, const koefude::WholeTextDownloadCallback &)
 */
//------------------------------------------------------------------------------
WholeTextModelDownloadResult downloadWholeTextModel( const std::filesystem::path &bundleRoot,
													const std::filesystem::path &cacheRoot, IModelTransport &transport,
													std::stop_token stop, const WholeTextDownloadCallback &progress)
{
	const auto id = wholeTextNativeModelId() ; // 単一registry用途13だけ
	return WholeTextModelDownloader::run( bundleRoot, cacheRoot, transport, modelDefinition( id),
										 modelDownloadDefinition( id), true, stop, progress, {}) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 明示方式の単一registryモデルだけを準備します
 * @param[in] variant 固定方式
 * @param[in] bundleRoot 同梱先
 * @param[in] cacheRoot 標準cache
 * @param[in,out] transport 同期transport
 * @param[in] stop 停止
 * @param[in] progress 実byte通知
 * @return 固定用途の所有結果
 * @see koefude::downloadWholeTextModel(koefude::WholeTextRuntimeVariant, const std::filesystem::path &, const std::filesystem::path &, koefude::IModelTransport &, std::stop_token, const koefude::WholeTextDownloadCallback &)
 */
//------------------------------------------------------------------------------
WholeTextModelDownloadResult downloadWholeTextModel( WholeTextRuntimeVariant		 variant,
													const std::filesystem::path &bundleRoot,
													const std::filesystem::path &cacheRoot, IModelTransport &transport,
													std::stop_token stop, const WholeTextDownloadCallback &progress)
{
	const auto role = wholeTextNativeModelId( variant) ; // 単一registry
	auto	   result = WholeTextModelDownloader::run( bundleRoot, cacheRoot, transport, modelDefinition( role),
													  modelDownloadDefinition( role), role == ModelId::WHOLE_TEXT_GEMMA, stop,
													  progress, {}) ; // 14は通常8GiB staging
	result.role = role ;
	return result ;
}
//------------------------------------------------------------------------------
/**
 * @brief 製品固定値または信頼済み小fixtureを同じ保存境界へ渡します
 * @param[in] bundle 同梱先
 * @param[in] cache cache先
 * @param[in,out] transport 同期転送
 * @param[in] file 最終期待値
 * @param[in] download 転送期待値
 * @param[in] production 用途13factory選択
 * @param[in] stop 停止
 * @param[in] progress 同期通知
 * @param[in] hooks 人工境界
 * @return 所有DTO
 * @throws OperationError 検証・通信・保存・取消し
 * @see koefude::WholeTextModelDownloader::run
 */
//------------------------------------------------------------------------------
WholeTextModelDownloadResult
WholeTextModelDownloader::run( const std::filesystem::path &bundle, const std::filesystem::path &cache,
							  IModelTransport &transport, const ModelDefinition &file,
							  const ModelDownloadDefinition &download, bool production, std::stop_token stop,
							  const WholeTextDownloadCallback &progress, const Hooks &hooks)
{
	checkCancelled( stop) ;
	constexpr std::uint64_t K_HEADROOM = 256ULL * 1024 * 1024 ; // 新規取得時だけの追加余裕
	// fixtureも含め期待値をI/O前に検査し、容量加算のoverflowを防ぎます
	if( file.bytes == 0 || file.bytes > static_cast<std::uint64_t>( std::numeric_limits<std::int64_t>::max()) ||
	   file.bytes > std::numeric_limits<std::uint64_t>::max() - K_HEADROOM ||
	   download.kind != ModelTransferKind::DIRECT || download.transferBytes != file.bytes ||
	   download.transferSha256 != file.sha256 || !download.archiveMember.empty() || !download.suffixHex.empty()) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_BAD_LENGTH) ;
	}
	validateModelTransfer( download) ;
	WholeTextModelDownloadResult result ; // 成功DTOのpath等を確定前に全て確保します
	result.path = cache / file.file ;
	result.bytes = file.bytes ;
	const ModelCacheTransaction transaction( cache, 0) ; // 再利用は追加容量を要求しません
	const auto					cached = inspectModelFile( cache, file, stop) ; // cache検証は一回だけ
	// 有効cacheは通信も新規容量確認もせず再利用します
	if( cached == ModelPresence::VALID) {
		result.reusedCache = true ;
		return result ;
	}
	// 破損cacheを同梱で隠さず、欠落時だけ読取り専用同梱先を検証します
	if( cached == ModelPresence::MISSING && cache != bundle &&
	   inspectModelFile( bundle, file, stop) == ModelPresence::VALID) {
		result.path = bundle / file.file ;
		result.reusedBundle = true ;
		return result ;
	}
	const auto required = file.bytes + K_HEADROOM ; // 上の表現境界内の追加必要byte
	// 試験の小fixture以外は実空き容量を一回確認します
	if( hooks.space) {
		hooks.space( required) ;
	} else {
		ULARGE_INTEGER available{} ; // 現利用者が使えるbyte
		// 不明または不足なら転送を開始しません
		if( !GetDiskFreeSpaceExW( cache.c_str(), &available, nullptr, nullptr)) {
			throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
		}
		// 全取得sizeと余裕を満たせない場合は旧fileを保持します
		if( available.QuadPart < required) {
			throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_DISK_FULL) ;
		}
	}
	const auto createStaging =
		//------------------------------------------------------------------------------
		/**
		 * @brief 固定用途の一時資源を直接構築します
		 * @return 単独所有の非可動file
		 */
		//------------------------------------------------------------------------------
		[&]() -> ModelStagingFile
		{
			// 製品は17GB任意値を渡せる通常constructorを使いません
			if( production) {
				return ModelStagingFile::forWholeTextImport( transaction.stagingRoot()) ;
			}
			return ModelStagingFile( transaction.stagingRoot(), file.bytes) ;
		} ; // 小期待値の選択はfriendの自試験だけです
	auto	   staged = createStaging() ; // 同volume、UUID、一時handleを所有します
	const auto notify =
		//------------------------------------------------------------------------------
		/**
		 * @brief commit前だけ実byteを通知します
		 * @param[in] stage 実段階
		 * @param[in] completed 実byte
		 * @param[in] total 総byte、0は不明
		 */
		//------------------------------------------------------------------------------
		[&]( WholeTextDownloadStage stage, std::uint64_t completed, std::uint64_t total)
		{
			checkCancelled( stop) ;
			// 空callbackは呼びません
			if( progress) {
				progress( {stage, completed, total}) ;
			}
		} ; // 同期呼出し中だけ有効なborrow
	std::uint64_t received = 0 ; // 全量をRAMへ保持しない64bit累計
	notify( WholeTextDownloadStage::DOWNLOADING, 0, file.bytes) ;
	transport.download(
		download,
		//------------------------------------------------------------------------------
		/**
		 * @brief 加算前に検査した有界blockだけを追記します
		 * @param[in] bytes 同期呼出し中だけ有効な受信byte
		 */
		//------------------------------------------------------------------------------
		[&]( std::span<const std::byte> bytes)
		{
			checkCancelled( stop) ;
			const auto next = advanceModelDownloadBytes( received, bytes.size(),
														file.bytes) ; // fakeも同じ境界
			// 人工短書はtest accessだけが設定します
			if( hooks.append) {
				hooks.append( staged, bytes) ;
			} else {
				staged.append( bytes) ;
			}
			received = next ;
			notify( WholeTextDownloadStage::DOWNLOADING, received, file.bytes) ;
		},
		stop) ;
	// fakeの短応答もhash前に拒否します
	if( received != file.bytes) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_BAD_LENGTH) ;
	}
	notify( WholeTextDownloadStage::VERIFYING, 0, file.bytes) ;
	staged.verify( file.bytes, file.sha256, stop,
				  //------------------------------------------------------------------------------
				  /**
				   * @brief 所有handleから検証したbyteだけを通知します
				   * @param[in] bytes 実検証byte
				   */
				  //------------------------------------------------------------------------------
				  [&]( std::uint64_t bytes)
				  {
					  notify( WholeTextDownloadStage::VERIFYING, bytes, file.bytes) ;
				  }) ;
	notify( WholeTextDownloadStage::COMMITTING, 0, 0) ;
	checkCancelled( stop) ; // callbackによる停止も反映する最後の取消し境界
	// 実renameの成功後はcallback・hash・Inspect・allocationを追加しません
	if( hooks.commit) {
		hooks.commit( staged, result.path) ;
	} else {
		staged.commit( result.path) ;
	}
	result.downloadCommitted = true ; // 実rename成功の事実だけを保持します
	return result ; // noexceptの所有moveのみ
}
} // namespace koefude
