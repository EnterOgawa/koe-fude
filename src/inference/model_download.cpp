//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file model_download.cpp
 * @brief 選択構成だけを検証・変換し、一つのcacheへ準備します
 * @version 1.0.0
 * @date 2026/09/13
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/13 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/13 共有artifact・metadata追記・最終再検査を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1397の固定用途/必要byte/検証commitを通知
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
#include "inference/model_download.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <stop_token>
#include <string_view>
#include <vector>

#include "core/types.h"
#include "inference/model_preparation.h"
#include "inference/model_store.h"
#include "platform/model_download_io.h"
#include "platform/win_helpers.h"

namespace koefude {
namespace {

//------------------------------------------------------------------------------
/**
 * @brief 容量合計のoverflowを拒否します
 * @param[in,out] total 合計byte数
 * @param[in] bytes 追加byte数
 */
//------------------------------------------------------------------------------
void addModelBytes( std::uint64_t &total, const std::uint64_t bytes)
{
	// 表示と空き容量照合で整数を周回させない
	if( bytes > std::numeric_limits<std::uint64_t>::max() - total) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_ARITHMETIC_OVERFLOW) ;
	}
	total += bytes ;
}

//------------------------------------------------------------------------------
/**
 * @brief 登録されていれば進捗をworker内で通知します
 * @param[in] callback 空を許す同期通知先
 * @param[in,out] progress 実測値を保持した最新状態
 * @param[in] stage 新しい段階
 */
//------------------------------------------------------------------------------
void reportModelProgress( const ModelPreparationCallback &callback, ModelPreparationProgress &progress,
						 const ModelPreparationStage stage)
{
	progress.stage = stage ;
	// 通知先のないCLI/試験でも処理を継続できる
	if( callback) {
		callback( progress) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 同じ固定artifactを参照しているか確認します
 * @param[in] left 一方の取得metadata
 * @param[in] right 他方の取得metadata
 * @return URL/size/hashが一致する場合true
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool sameModelTransfer( const ModelDownloadDefinition &left, const ModelDownloadDefinition &right)
{
	return left.url == right.url && left.transferBytes == right.transferBytes &&
		   left.transferSha256 == right.transferSha256 ;
}

//------------------------------------------------------------------------------
/**
 * @brief 小文字hexを値へ変換します
 * @param[in] unit registryのASCII文字
 * @return 0～15の値
 * @throws OperationError 不正なhex
 */
//------------------------------------------------------------------------------
[[nodiscard]] unsigned int hexValue( const char unit)
{
	// 十進の範囲をそのまま対応付ける
	if( unit >= '0' && unit <= '9') {
		return static_cast<unsigned int>( unit - '0') ;
	}
	// registryと同じ小文字表現だけを受け付ける
	if( unit >= 'a' && unit <= 'f') {
		return static_cast<unsigned int>( unit - 'a' + 10) ;
	}
	throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_INVALID_DATA) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 検証済み同梱fileを一時fileへ有界copyします
 * @param[in] root 読取り専用の同梱root
 * @param[in] id 固定用途
 * @param[in,out] output cache側の有界一時file
 * @param[in] stop 協調取消し
 */
//------------------------------------------------------------------------------
void copyVerifiedModel( const std::filesystem::path &root, const ModelId id, ModelStagingFile &output,
					   const std::stop_token stop)
{
	const auto			verified = verifyModel( root, id, stop) ; // copy完了までwrite/deleteを拒否するlock
	const LARGE_INTEGER beginning{} ; // 読取り位置を先頭へ戻す値
	// hash計算後のEOF位置からcopyしない
	if( !SetFilePointerEx( verified.lock.get(), beginning, nullptr, FILE_BEGIN)) {
		throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
	}
	std::array<std::byte, 65536> bytes{} ; // model全体をRAMへ置かないcopy buffer
	// EOFまたは取消しまで固定blockで転送する
	for(;;) {
		checkCancelled( stop) ;
		DWORD count = 0 ; // 今回の実読取り量
		// copy元のI/O失敗で部分fileを確定しない
		if( !ReadFile( verified.lock.get(), bytes.data(), static_cast<DWORD>( bytes.size()), &count, nullptr)) {
			throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
		}
		// sourceの全byteを消費した場合だけ最終検証へ進む
		if( count == 0) {
			return ;
		}
		output.append( std::span<const std::byte>( bytes).first( count)) ;
	}
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief cache準備に必要な追加空き容量の保守的な値を算出します
 * @param[in] plan 固定registryから生成した準備計画
 * @return 最終出力・最大一時archive・256MiBの余裕の合計byte数。Readyなら0
 * @throws OperationError 未知用途、容量の表現上限超過
 * @see koefude::modelPreparationDiskBytes
 */
//------------------------------------------------------------------------------
std::uint64_t modelPreparationDiskBytes( const ModelPreparationPlan &plan)
{
	// 全依存が揃う既存rootはcacheへの追加保存を要求しない
	if( plan.ready) {
		return 0 ;
	}
	std::uint64_t required = 256ULL * 1024 * 1024 ; // 他processの変動を考慮する固定余裕
	// 新規copy/取得する最終fileだけを予定量へ加える
	for( const auto &entry : plan.entries) { // 必要な一つのfile
		// 再利用fileの既存容量を追加の必要量として数えない
		if( entry.action == ModelPreparationAction::COPY_BUNDLE || entry.action == ModelPreparationAction::DOWNLOAD) {
			addModelBytes( required, modelDefinition( entry.id).bytes) ;
		}
	}
	std::uint64_t largestArchive = 0 ; // 逐次処理する一つの最大archive
	// 同時保持しないarchive容量を合計せず最大値で扱う
	for( const ModelId id : plan.transfers) { // 転送を代表する用途
		const auto &definition = modelDownloadDefinition( id) ; // 固定転送metadata
		// direct/追記は受信した一時file自体を最終fileへrenameする
		if( definition.kind == ModelTransferKind::TAR_BZIP2) {
			largestArchive = std::max( largestArchive, definition.transferBytes) ;
		}
	}
	addModelBytes( required, largestArchive) ;
	return required ;
}

//------------------------------------------------------------------------------
/**
 * @brief 許可済みの計画を再検査し、必要分だけをcacheへ確定します
 * @param[in] options 操作開始時に固定した選択構成
 * @param[in] bundleRoot 読取り専用の同梱root
 * @param[in] cacheRoot 検証したlocal cache root。未作成を許可
 * @param[in,out] transport 明示取得を担う同期境界。呼出し中有効
 * @param[in] stop コピー・通信・展開・検証の協調取消し
 * @param[in] progress 空を許すworker上の進捗通知
 * @return 全必要fileを一つのrootで検証した計画
 * @throws OperationError 取得境界、通信、file、hash、展開、競合、容量、取消し
 * @pre 利用者が選択構成の取得と通信に同意済みであること
 * @see koefude::downloadModels
 */
//------------------------------------------------------------------------------
ModelPreparationPlan downloadModels( const EngineOptions &options, const std::filesystem::path &bundleRoot,
									const std::filesystem::path &cacheRoot, IModelTransport &transport,
									const std::stop_token stop, const ModelPreparationCallback &progress)
{
	LocalModelInspector		 inspector ; // 固定size/hashを読む実検査
	ModelPreparationProgress status ; // fakeの時間では進めない実転送状態
	reportModelProgress( progress, status, ModelPreparationStage::INSPECTING) ;
	auto plan = planModelPreparation( options, bundleRoot, cacheRoot, inspector, stop) ; // 最新状態
	// 既取得ならcache作成や通信をせず検証済みrootを返す
	if( plan.ready) {
		return plan ;
	}
	const ModelCacheTransaction transaction( cacheRoot,
											modelPreparationDiskBytes( plan)) ; // 排他と容量
	plan = planModelPreparation( options, bundleRoot, cacheRoot, inspector, stop) ;
	// 排他取得直前に別起動が準備を完了していても再取得しない
	if( plan.ready) {
		return plan ;
	}
	status.transferBytes = plan.downloadBytes ;
	// 同梱から再利用できるfileを先にcacheへ揃える
	for( const auto &entry : plan.entries) { // copy対象の候補
		// 未選択・既取得fileへcopy操作を増やさない
		if( entry.action != ModelPreparationAction::COPY_BUNDLE) {
			continue ;
		}
		checkCancelled( stop) ;
		const auto &definition = modelDefinition( entry.id) ; // 最終fileの期待値
		status.asset = entry.id ;
		status.requiredBytes = definition.bytes ;
		status.verified = false ;
		ModelStagingFile output( transaction.stagingRoot(),
								definition.bytes) ; // 取消し時に自動削除
		reportModelProgress( progress, status, ModelPreparationStage::COPYING) ;
		copyVerifiedModel( bundleRoot, entry.id, output, stop) ;
		reportModelProgress( progress, status, ModelPreparationStage::VERIFYING) ;
		output.verify( definition.bytes, definition.sha256, stop) ;
		checkCancelled( stop) ;
		output.commit( cacheRoot / definition.file) ;
		status.verified = true ;
		reportModelProgress( progress, status, ModelPreparationStage::VERIFYING) ;
	}
	// 同じarchiveを一度だけ取得し、必要なmemberだけを逐次確定する
	for( const ModelId representative : plan.transfers) { // 今回取得する固定artifact
		checkCancelled( stop) ;
		const auto &download = modelDownloadDefinition( representative) ; // 転送metadata
		const auto &definition = modelDefinition( representative) ; // 最終fileの期待値
		status.asset = representative ;
		status.requiredBytes = download.transferBytes ;
		status.verified = false ;
		const auto		 limit = download.kind == ModelTransferKind::METADATA_APPEND
									 ? definition.bytes
									 : download.transferBytes ; // 追記を含む一時容量
		ModelStagingFile artifact( transaction.stagingRoot(),
								  limit) ; // 他処理へ未検証pathを渡さない
		reportModelProgress( progress, status, ModelPreparationStage::DOWNLOADING) ;
		transport.download(
			download,
			//------------------------------------------------------------------------------
			/**
			 * @brief 実受信量を数えて一時fileへ追記します
			 * @param[in] bytes callback中だけ有効な有界byte列
			 */
			//------------------------------------------------------------------------------
			[&]( const std::span<const std::byte> bytes)
			{
				checkCancelled( stop) ;
				artifact.append( bytes) ;
				addModelBytes( status.transferredBytes, bytes.size()) ;
				reportModelProgress( progress, status, ModelPreparationStage::DOWNLOADING) ;
			},
			stop) ;
		reportModelProgress( progress, status, ModelPreparationStage::VERIFYING) ;
		artifact.verify( download.transferBytes, download.transferSha256, stop) ;
		// 検証済みarchiveから各memberをdiskの任意pathへ展開せず取り出す
		if( download.kind == ModelTransferKind::TAR_BZIP2) {
			// この転送に依存する不足fileだけを一つずつ処理する
			for( const auto &entry : plan.entries) { // 必要なmemberの候補
				const auto &memberDownload = modelDownloadDefinition( entry.id) ; // 用途の転送対応
				// 既取得fileや別artifactを再展開しない
				if( entry.action != ModelPreparationAction::DOWNLOAD || !sameModelTransfer( download, memberDownload)) {
					continue ;
				}
				const auto &member = modelDefinition( entry.id) ; // 出力の固定size/hash
				status.asset = entry.id ;
				status.requiredBytes = member.bytes ;
				status.verified = false ;
				ModelStagingFile output( transaction.stagingRoot(),
										member.bytes) ; // member単位の有界出力
				reportModelProgress( progress, status, ModelPreparationStage::EXTRACTING) ;
				extractModelArchive(
					artifact, memberDownload.archiveMember,
					//------------------------------------------------------------------------------
					/**
					 * @brief 固定sizeを超えないmemberだけを追記します
					 * @param[in] bytes callback中のborrow
					 */
					//------------------------------------------------------------------------------
					[&]( const std::span<const std::byte> bytes)
					{
						checkCancelled( stop) ;
						output.append( bytes) ;
					},
					stop) ;
				reportModelProgress( progress, status, ModelPreparationStage::VERIFYING) ;
				output.verify( member.bytes, member.sha256, stop) ;
				checkCancelled( stop) ;
				output.commit( cacheRoot / member.file) ;
				status.verified = true ;
				reportModelProgress( progress, status, ModelPreparationStage::VERIFYING) ;
			}
		} else { // direct/metadata追記は同じ所有fileを確定して二重保存を避ける
			// source hash確認前に変換済みモデルを作らない
			if( download.kind == ModelTransferKind::METADATA_APPEND) {
				reportModelProgress( progress, status, ModelPreparationStage::EXTRACTING) ;
				const auto hex = download.suffixHex ; // 固定の小さい追記metadata
				// registry外の巨大・不正な追記を拒否する
				if( hex.empty() || hex.size() > 16384 || hex.size() % 2 != 0) {
					throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_INVALID_DATA) ;
				}
				std::vector<std::byte> suffix ; // 最大8192byteの所有追記
				// 全hex pairを一度ずつ変換する
				for( std::size_t index = 0; index < hex.size(); index += 2) { // 次のhex位置
					suffix.push_back( static_cast<std::byte>( (hexValue( hex[index]) << 4U) | hexValue( hex[index + 1]))) ;
				}
				artifact.append( suffix) ;
			}
			reportModelProgress( progress, status, ModelPreparationStage::VERIFYING) ;
			artifact.verify( definition.bytes, definition.sha256, stop) ;
			checkCancelled( stop) ;
			artifact.commit( cacheRoot / definition.file) ;
			status.verified = true ;
			reportModelProgress( progress, status, ModelPreparationStage::VERIFYING) ;
		}
	}
	status.asset.reset() ;
	status.requiredBytes = 0 ;
	status.verified = false ;
	reportModelProgress( progress, status, ModelPreparationStage::VERIFYING) ;
	plan = planModelPreparation( options, bundleRoot, cacheRoot, inspector, stop) ;
	// 全依存が同じrootで揃う場合だけUIとengineへReadyを返す
	if( !plan.ready) {
		throw OperationError( ErrorKind::MODEL_MISSING) ;
	}
	reportModelProgress( progress, status, ModelPreparationStage::READY) ;
	return plan ;
}

} // namespace koefude
