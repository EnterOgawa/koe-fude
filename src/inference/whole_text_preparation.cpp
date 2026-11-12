//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_preparation.cpp
 * @brief runtimeを先に検証して補助WhisperとGemmaの準備状態を確認します
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1279の補正準備入口を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1280の明示導入・補助準備とcommit成功を保持
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1283の固定Direct取得と確定成功を接続
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
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
#include "inference/whole_text_preparation.h"
#include "core/types.h"
#include "inference/model_store.h"
#include <algorithm>
#include <utility>

namespace koefude {
namespace {
//------------------------------------------------------------------------------
/**
 * @brief 単一registryのmetadataを所有copyします
 * @param[in] role 用途
 * @return path以外も全て固定registry由来の説明
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextPreparationModel metadata( ModelId role)
{
	const auto &file = modelDefinition( role) ; // 単一weight定義
	const auto &download = modelDownloadDefinition( role) ; // 配布条件
	return {role,
			std::string( file.id),
			std::filesystem::path( file.file),
			file.bytes,
			std::string( download.revision),
			std::string( download.license),
			std::string( download.source),
			download.kind} ;
}
//------------------------------------------------------------------------------
/**
 * @brief 検査状態を変換します
 * @param[in] value 固定hash検査結果
 * @return 要素状態
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextPreparationState presence( ModelPresence value)
{
	// 固定size/hashの結果を不足と破損へ分けます
	switch( value) {
		case ModelPresence::VALID:
			return WholeTextPreparationState::READY ; // 内容一致
		case ModelPresence::MISSING:
			return WholeTextPreparationState::MISSING ; // 未配置
		case ModelPresence::CORRUPT:
			return WholeTextPreparationState::CORRUPT ; // 内容不一致
	}
	return WholeTextPreparationState::FAILURE ;
}
//------------------------------------------------------------------------------
/**
 * @brief 検査例外を要素へ保持し、取消しは操作境界へ渡します
 * @param[in,out] element 失敗した所有要素
 * @param[in] error 元例外
 * @param[in] runtime CPU専用codeを解釈する場合true
 * @throws OperationError 取消しだけを再送出します
 */
//------------------------------------------------------------------------------
void failure( WholeTextPreparationElement &element, const OperationError &error, bool runtime)
{
	// 取消しを検査失敗へまとめません
	if( error.kind() == ErrorKind::CANCELLED) {
		throw error ;
	}
	element.error = error.kind() ;
	element.code = error.code() ;
	element.state = WholeTextPreparationState::FAILURE ;
	// 未配置はI/O失敗と区別します
	if( error.kind() == ErrorKind::MODEL_MISSING) {
		element.state = WholeTextPreparationState::MISSING ;
	}
	// CPU契約不足だけをUnsupportedにします
	else if( runtime && error.code() == 0x57560001) {
		element.state = WholeTextPreparationState::UNSUPPORTED ;
	}
	// receipt・構成・属性の拒否を、配置済み有効とは扱いません
	else if( error.kind() == ErrorKind::MODEL_REJECTED) {
		element.state = WholeTextPreparationState::CORRUPT ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 実段階だけを同期通知します
 * @param[in] progress 空可の通知
 * @param[in] stage 現段階
 * @param[in] stop 停止
 */
//------------------------------------------------------------------------------
void notify( const WholeTextPreparationCallback &progress, WholeTextPreparationStage stage, std::stop_token stop)
{
	checkCancelled( stop) ;
	// 空の通知先は呼びません
	if( progress) {
		progress( stage) ;
	}
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 固定補助Whisper設定を返します
 * @return Quality/CPU4/日本語、VAD/話者/fallback無効
 * @see koefude::wholeTextPreparationOptions
 */
//------------------------------------------------------------------------------
EngineOptions wholeTextPreparationOptions()
{
	EngineOptions options ; // 通常ASR設定と共有しない所有値
	options.asrModel = AsrModel::WHISPER ;
	options.asrProfile = AsrProfile::QUALITY ;
	options.backend = Backend::CPU ;
	options.threads = 4 ;
	options.autoLanguage = false ;
	options.speechFilter = false ;
	options.diarization = false ;
	options.cpuFallback = false ;
	return options ;
}
//------------------------------------------------------------------------------
/**
 * @brief 製品の検査・明示準備境界を返します
 * @return worker専用の同期関数群
 * @see koefude::wholeTextPreparationServices
 */
//------------------------------------------------------------------------------
WholeTextPreparationServices wholeTextPreparationServices()
{
	return {
			//------------------------------------------------------------------------------
			/**
			 * @brief DLLをloadせず検証してleaseを解放します
			 * @param[in] request CPU方式とDLL先
			 * @param[in] stop 停止
			 */
			//------------------------------------------------------------------------------
			[]( const WholeTextPreparationRequest &request, std::stop_token stop)
			{
				const auto lease = verifyWholeTextRuntime( request.variant, request.runtimePath, stop) ;
				static_cast<void>( lease) ; // 返却時に所有leaseを解放します
			},
			//------------------------------------------------------------------------------
			/**
			 * @brief 既存準備器の読取り検査だけを使います
			 * @param[in] options 固定補助構成
			 * @param[in] bundle 同梱先
			 * @param[in] cache cache先
			 * @param[in] stop 停止
			 * @return 所有計画
			 */
			//------------------------------------------------------------------------------
			[]( const EngineOptions &options, const std::filesystem::path &bundle, const std::filesystem::path &cache,
		   std::stop_token stop)
			{
				LocalModelInspector inspector ; // workerだけが同期所有します
				return planModelPreparation( options, bundle, cache, inspector, stop) ;
			},
			//------------------------------------------------------------------------------
			/**
			 * @brief Gemmaの通常固定検査を一回行います
			 * @param[in] root 対象先
			 * @param[in] id 用途13
			 * @param[in] stop 停止
			 * @return size/SHAの検査結果
			 */
			//------------------------------------------------------------------------------
			[]( const std::filesystem::path &root, ModelId id, std::stop_token stop)
			{
				return inspectModelFile( root, modelDefinition( id), stop) ;
			},
			//------------------------------------------------------------------------------
			/**
			 * @brief 明示操作だけが既存の固定HTTPS取得を使います
			 * @param[in] options 固定補助条件
			 * @param[in] bundle 同梱先
			 * @param[in] cache cache先
			 * @param[in] stop 停止
			 * @param[in] progress 実byte通知
			 * @return 検証済み準備計画
			 */
			//------------------------------------------------------------------------------
			[]( const EngineOptions &options, const std::filesystem::path &bundle, const std::filesystem::path &cache,
		   std::stop_token stop, const ModelPreparationCallback &progress)
			{
				WinHttpModelTransport transport ; // worker所有の既存allowlist経路
				return downloadModels( options, bundle, cache, transport, stop, progress) ;
			},
		[]( const std::filesystem::path &source, const std::filesystem::path &cache, std::stop_token stop,
		   const WholeTextImportCallback &progress)
		{
			return importWholeTextModel( source, cache, stop, progress) ;
		},
			//------------------------------------------------------------------------------
			/**
			 * @brief 専用操作だけが既存HTTPS経路で固定Gemmaを取得します
			 * @param[in] bundle 同梱先
			 * @param[in] cache 標準cache
			 * @param[in] stop 停止
			 * @param[in] progress 実byte通知
			 * @return 所有結果
			 */
			//------------------------------------------------------------------------------
			[]( const std::filesystem::path &bundle, const std::filesystem::path &cache, std::stop_token stop,
		   const WholeTextDownloadCallback &progress)
			{
				WinHttpModelTransport transport ; // 同じworker上の既存allowlist経路
				return downloadWholeTextModel( bundle, cache, transport, stop, progress) ;
			},
		[]( WholeTextRuntimeVariant variant, const std::filesystem::path &source, const std::filesystem::path &cache,
		   std::stop_token stop, const WholeTextImportCallback &progress)
		{
			return importWholeTextModel( variant, source, cache, stop, progress) ;
		},
		[]( WholeTextRuntimeVariant variant, const std::filesystem::path &bundle, const std::filesystem::path &cache,
		   std::stop_token stop, const WholeTextDownloadCallback &progress)
		{
			WinHttpModelTransport transport ; // 明示準備だけのHTTPS
			return downloadWholeTextModel( variant, bundle, cache, transport, stop, progress) ;
		}} ;
}
//------------------------------------------------------------------------------
/**
 * @brief I/Oなしでregistryの用途・条件を表示用に所有します
 * @param[in] request 固定要求
 * @return 全要素未検査のmetadata。sourceは保持しません
 * @see koefude::wholeTextPreparationPreview
 */
//------------------------------------------------------------------------------
WholeTextPreparationPlan wholeTextPreparationPreview( const WholeTextPreparationRequest &request)
{
	WholeTextPreparationPlan plan ; // I/Oのない所有metadata
	plan.request = request ;
	plan.request.sourceFile.clear() ;
	plan.runtime.effectivePath = request.runtimePath ;
	// 固定補助条件から用途を一度だけ解決します
	for( const auto id : request.use == WholeTextPreparationUse::TEXT_ONLY_CONTEXT
							? std::vector<ModelId>{}
							: requiredModels( wholeTextPreparationOptions())) { // 補助用途
		plan.whisper.models.push_back( metadata( id)) ;
	}
	plan.gemma.models.push_back( metadata( wholeTextNativeModelId( request.variant))) ;
	return plan ;
}
//------------------------------------------------------------------------------
/**
 * @brief 要求の明示操作をruntime検査から順に実行します
 * @param[in] request 所有pathと明示CPU方式
 * @param[in] stop 協調停止
 * @param[in] progress 空を許す同期実段階通知
 * @param[in] services 注入する同期境界
 * @return 不足・拒否も状態として保持した所有snapshot
 * @throws OperationError 取消し。その他の要素失敗は元診断付きで保持します
 * @note worker専用。runtime前にモデル保存先の属性も調べません
 * @pre PrepareWhisperは表示した用途・容量・条件・通信先に対する明示操作に限ります
 * @see koefude::inspectWholeTextPreparation
 */
//------------------------------------------------------------------------------
WholeTextPreparationPlan inspectWholeTextPreparation( const WholeTextPreparationRequest &request, std::stop_token stop,
													 const WholeTextPreparationCallback &progress,
													 const WholeTextPreparationServices &services)
{
	// 文章専用は用途14だけを許可し、古い補助取得commandも拒否します
	if( request.use == WholeTextPreparationUse::TEXT_ONLY_CONTEXT &&
	   (request.variant != WholeTextRuntimeVariant::MINISTRAL14_B_AVX2_NO_REPACK16_GB ||
		request.action == WholeTextPreparationAction::PREPARE_WHISPER)) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	auto plan = wholeTextPreparationPreview( request) ; // 全成功metadataをI/O前に確保
	notify( progress, WholeTextPreparationStage::RUNTIME, stop) ;
	try {
		services.runtime( request, stop) ;
		checkCancelled( stop) ;
		plan.runtime.state = WholeTextPreparationState::READY ;
	} catch( const OperationError &error) {
		failure( plan.runtime, error, true) ;
		return plan ; // 前提拒否時はモデルI/Oも標準cache解決も0です
	}
	// runtime合格後だけモデル保存先を解決します
	if( plan.request.cacheRoot.empty()) {
		plan.request.cacheRoot = defaultModelCache() ;
	}
	// 文章専用では補助モデル検査・取得・状態昇格を行いません
	if( request.use == WholeTextPreparationUse::AUDIO_ASSISTED) {
		notify( progress, WholeTextPreparationStage::WHISPER, stop) ;
		try {
			auto whisper = services.whisper( wholeTextPreparationOptions(), request.bundleRoot, plan.request.cacheRoot,
											stop) ; // 表示済みの固定計画
			// 明示補助準備だけが既存取得を呼び、通常ASRの選択には触れません
			if( request.action == WholeTextPreparationAction::PREPARE_WHISPER) {
				whisper = services.prepareWhisper( wholeTextPreparationOptions(), request.bundleRoot,
												  plan.request.cacheRoot, stop,
					//------------------------------------------------------------------------------
					/**
					 * @brief 実転送byteだけを通知します
					 * @param[in] value 既存準備器の進捗
					 */
					//------------------------------------------------------------------------------
					[&]( ModelPreparationProgress value)
					{
						// callback不在でも取得器の取消し契約を保持します
						if( progress) {
							progress( {WholeTextPreparationStage::WHISPER_DOWNLOADING,
									  value.transferredBytes, value.transferBytes}) ;
						}
					}) ;
			}
			plan.whisperPlan = whisper ;
			plan.whisper.effectivePath = whisper.effectiveRoot ;
			plan.whisper.state = WholeTextPreparationState::MISSING ;
			// 両rootとも不一致なら破損を不足より優先して表示します
			for( const auto &entry : whisper.entries) { // 固定補助用途だけ
				// 利用可能なcopyもない不一致を破損とします
				if( entry.cache == ModelPresence::CORRUPT || entry.bundle == ModelPresence::CORRUPT) {
					plan.whisper.state = WholeTextPreparationState::CORRUPT ;
				}
			}
			// 既存計画の全依存Ready条件を再実装しません
			if( whisper.ready) {
				plan.whisper.state = WholeTextPreparationState::READY ;
			}
		} catch( const OperationError &error) {
			failure( plan.whisper, error, false) ;
		}
	}
	notify( progress, WholeTextPreparationStage::GEMMA, stop) ;
	try {
		const auto id = wholeTextNativeModelId( request.variant) ; // 単一registry用途13
		// 専用取得の確定後は追加検査・callback・allocationを行いません
		if( request.action == WholeTextPreparationAction::DOWNLOAD_GEMMA) {
			const auto download = [&]( const std::filesystem::path &bundle, const std::filesystem::path &cache,
									  std::stop_token token, const WholeTextDownloadCallback &callback)
			{
				if( id == ModelId::WHOLE_TEXT_MINISTRAL14_B) { // 新用途に旧Gemma seamを流用しません
					if( !services.downloadSelected) {
						throw OperationError( ErrorKind::MODEL_REJECTED) ;
					}
					return services.downloadSelected( request.variant, bundle, cache, token, callback) ;
				}
				return services.downloadGemma( bundle, cache, token, callback) ;
			} ;
			auto downloaded = download(
				request.bundleRoot, plan.request.cacheRoot, stop,
				//------------------------------------------------------------------------------
				/**
				 * @brief 転送・検証・確定の実byteを同じworker通知へ変換します
				 * @param[in] value 同期実進捗
				 */
				//------------------------------------------------------------------------------
				[&]( WholeTextDownloadProgress value)
				{
					constexpr WholeTextPreparationStage STAGES[] = {
						WholeTextPreparationStage::GEMMA_DOWNLOADING, WholeTextPreparationStage::DOWNLOAD_VERIFYING,
						WholeTextPreparationStage::DOWNLOAD_COMMITTING} ; // 固定段階対応
					// 空callbackは呼びません
					if( progress) {
						progress( {STAGES[static_cast<unsigned>( value.stage)], value.completedBytes, value.totalBytes}) ;
					}
				}) ;
			plan.gemma.effectivePath = std::move( downloaded.path) ;
			plan.downloadCommitted = downloaded.downloadCommitted ;
			plan.reusedCache = downloaded.reusedCache ;
			plan.reusedBundle = downloaded.reusedBundle ;
			plan.gemma.state = WholeTextPreparationState::READY ;
			plan.ready = plan.runtime.state == WholeTextPreparationState::READY &&
						 (request.use == WholeTextPreparationUse::TEXT_ONLY_CONTEXT ||
						  plan.whisper.state == WholeTextPreparationState::READY) ;
			return plan ;
		}
		// 導入成功後は再検査もcallbackも行わず事前確保したmetadataだけを返します
		if( request.action == WholeTextPreparationAction::IMPORT_GEMMA) {
			plan.gemma.effectivePath = plan.request.cacheRoot / modelDefinition( id).file ;
			const auto import = [&]( const std::filesystem::path &source, const std::filesystem::path &cache,
									std::stop_token token, const WholeTextImportCallback &callback)
			{
				if( id == ModelId::WHOLE_TEXT_MINISTRAL14_B) { // 新用途は明示選択seamだけです
					if( !services.importSelected) {
						throw OperationError( ErrorKind::MODEL_REJECTED) ;
					}
					return services.importSelected( request.variant, source, cache, token, callback) ;
				}
				return services.importGemma( source, cache, token, callback) ;
			} ;
			const auto imported = import(
				request.sourceFile, plan.request.cacheRoot, stop,
				//------------------------------------------------------------------------------
				/**
				 * @brief 実導入段階と実byteを既存worker通知へ変換します
				 * @param[in] value copyまたは検証の実byte
				 */
				//------------------------------------------------------------------------------
				[&]( WholeTextImportProgress value)
				{
					constexpr WholeTextPreparationStage STAGES[] = {
						WholeTextPreparationStage::SOURCE_VALIDATION, WholeTextPreparationStage::IMPORT_COPYING,
						WholeTextPreparationStage::IMPORT_VERIFYING,
						WholeTextPreparationStage::IMPORT_COMMITTING} ; // 固定段階対応
					// 空callbackへ通知せず、commit後の呼出しもありません
					if( progress) {
						progress( {STAGES[static_cast<unsigned>( value.stage)], value.completedBytes, value.totalBytes}) ;
					}
				}) ;
			plan.importCommitted = imported.importCommitted ;
			plan.reusedCache = imported.reusedCache ;
			plan.gemma.state = WholeTextPreparationState::READY ;
			plan.ready = plan.runtime.state == WholeTextPreparationState::READY &&
						 (request.use == WholeTextPreparationUse::TEXT_ONLY_CONTEXT ||
						  plan.whisper.state == WholeTextPreparationState::READY) ;
			return plan ;
		}
		auto root = plan.request.cacheRoot ; // cacheを先に一回検査します
		auto value = services.model( root, id, stop) ; // 不一致をfallbackで隠しません
		// cache欠落時だけ同梱rootを検査します
		if( value == ModelPresence::MISSING && root != request.bundleRoot) {
			root = request.bundleRoot ;
			value = services.model( root, id, stop) ;
		}
		plan.gemma.state = presence( value) ;
		plan.gemma.effectivePath = root / modelDefinition( id).file ;
	} catch( const OperationError &error) {
		failure( plan.gemma, error, false) ;
	}
	checkCancelled( stop) ;
	plan.ready = plan.runtime.state == WholeTextPreparationState::READY &&
				 (request.use == WholeTextPreparationUse::TEXT_ONLY_CONTEXT ||
				  plan.whisper.state == WholeTextPreparationState::READY) &&
				 plan.gemma.state == WholeTextPreparationState::READY ;
	return plan ;
}
//------------------------------------------------------------------------------
/**
 * @brief 通常workerの同期準備です
 * @param[in] request 固定要求
 * @param[in] stop 停止
 * @param[in] progress 実段階
 * @return 検査snapshot
 * @throws OperationError 取消し
 * @see koefude::runWholeTextPreparation
 */
//------------------------------------------------------------------------------
WholeTextPreparationPlan runWholeTextPreparation( const WholeTextPreparationRequest &request, std::stop_token stop,
												 const WholeTextPreparationCallback &progress)
{
	return inspectWholeTextPreparation( request, stop, progress, wholeTextPreparationServices()) ;
}
} // namespace koefude
