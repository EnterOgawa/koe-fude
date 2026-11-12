//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file model_preparation.cpp
 * @brief モデル依存・転送容量とlocal cacheの再利用を読取り専用で解決します
 * @version 1.0.0
 * @date 2026/09/13
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/13 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/13 選択構成の準備計画とfile検査を実装
 * 0.1.0 Shunji Ogawa 2026/09/13 取得前の保存先属性検査を共用APIへ接続
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372の独立話者投影と元原文保持を接続
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
#include "inference/model_preparation.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include <shlobj.h>

#include "core/types.h"
#include "inference/model_store.h"
#include "platform/win_helpers.h"

namespace koefude {
namespace {

//------------------------------------------------------------------------------
/**
 * @brief Windowsの予約device名をdirectory要素から除外します
 * @param[in] value NULを含まないpath要素の借用view
 * @return 拡張子付きも含めた予約名ならtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool isDeviceComponent( const std::wstring_view value)
{
	const auto stem = value.substr( 0, value.find( L'.')) ; // 拡張子より前のdevice識別部分
	constexpr std::array<std::wstring_view, 4> K_DEVICES{L"CON", L"PRN", L"AUX", L"NUL"} ; // 予約名
	// 短い固定device名をcase-insensitiveで照合する
	for( const auto device : K_DEVICES) { // 比較する固定名
		// サイズを限定してからWin32のint長へ渡す
		if( stem.size() == device.size() && CompareStringOrdinal( stem.data(), 3, device.data(), 3, TRUE) == CSTR_EQUAL) {
			return true ;
		}
	}
	return stem.size() == 4 && stem[3] >= L'0' && stem[3] <= L'9' &&
		   (CompareStringOrdinal( stem.data(), 3, L"COM", 3, TRUE) == CSTR_EQUAL ||
			CompareStringOrdinal( stem.data(), 3, L"LPT", 3, TRUE) == CSTR_EQUAL) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 未作成部分を許可しながら、local directoryの既存祖先を検査します
 * @param[in] path 検査する絶対path
 * @return 正規化した所有path
 * @throws OperationError 非local、非directory、reparse/cloud、属性取得失敗
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::filesystem::path validateModelRoot( const std::filesystem::path &path)
{
	const auto &input = path.native() ; // APIへ値として渡すUTF-16
	// UNC/device、drive相対、ADS、埋込みNULをOSへ渡さない
	if( !path.is_absolute() || input.size() < 3 || input.starts_with( L"\\\\") || input[1] != L':' ||
	   input.find( L':', 2) != std::wstring::npos || input.find( L'\0') != std::wstring::npos) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_INVALID_NAME) ;
	}
	// 曖昧な親参照や末尾空白等を正規化して別の保存先へ変えない
	for( const auto &component : path.relative_path()) { // 順に確認するpath要素
		const auto &value = component.native() ; // 要素のUTF-16
		// Windowsの別名解釈やwildcardを拒否する
		if( value == L"." || value == L".." || isDeviceComponent( value) ||
		   (!value.empty() && (value.back() == L'.' || value.back() == L' ')) ||
		   value.find_first_of( L"<>\"|?*") != std::wstring::npos ||
		   std::any_of( value.begin(), value.end(),
					   //------------------------------------------------------------------------------
					   /**
					    * @brief 制御文字を拒否します
					    * @param[in] unit UTF-16単位
					    * @return 制御文字ならtrue
					    */
					   //------------------------------------------------------------------------------
					   []( const wchar_t unit)
					   {
						   return unit < 32 ;
					   })) {
			throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_INVALID_NAME) ;
		}
	}
	const auto absolute = path.lexically_normal() ; // ..を含まない正規化path
	auto	   current = absolute.root_path() ; // 祖先属性の走査位置
	const UINT drive = GetDriveTypeW( current.c_str()) ; // mapped network driveも区別する
	// 通信を伴うdriveや未知のdriveを検査しない
	if( drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE && drive != DRIVE_RAMDISK && drive != DRIVE_CDROM) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_INVALID_DRIVE) ;
	}
	// 最初の未作成directoryまで検査し、junctionやcloudを経由しない
	for( const auto &component : absolute.relative_path()) { // 検査する次のdirectory名
		current /= component ;
		const DWORD attributes = GetFileAttributesW( current.c_str()) ; // data読取り前の属性
		// 欠落は初回準備を許可し、それ以外のOS失敗は計画を中止する
		if( attributes == INVALID_FILE_ATTRIBUTES) {
			const DWORD code = GetLastError() ; // 属性取得の元診断値
			// 未作成cacheの子孫を属性照会しない
			if( code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
				break ;
			}
			throw OperationError( ErrorKind::MODEL_REJECTED, code) ;
		}
		constexpr DWORD K_REJECTED = FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE |
									 FILE_ATTRIBUTE_RECALL_ON_OPEN |
									 FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS ; // 同期等を伴う属性
		// 通常directory以外をモデル保存先として扱わない
		if( (attributes & K_REJECTED) != 0 || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
			throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_ACCESS_DENIED) ;
		}
	}
	return absolute ;
}

/** @brief KnownFolder APIの返す文字列を必ず解放します */
struct TaskMemoryDeleter {
	//------------------------------------------------------------------------------
	/**
	 * @brief COM task領域を解放します
	 * @param[in] value 所有文字列。NULL可
	 */
	//------------------------------------------------------------------------------
	void operator()( wchar_t *value) const noexcept
	{
		CoTaskMemFree( value) ;
	}
} ;

//------------------------------------------------------------------------------
/**
 * @brief 容量加算のoverflowを拒否します
 * @param[in,out] total 既存の合計byte数
 * @param[in] bytes 追加byte数
 * @throws OperationError 表現可能な合計を超えた場合
 */
//------------------------------------------------------------------------------
void addBytes( std::uint64_t &total, const std::uint64_t bytes)
{
	// 容量表示を小さい値へ周回させない
	if( bytes > std::numeric_limits<std::uint64_t>::max() - total) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_ARITHMETIC_OVERFLOW) ;
	}
	total += bytes ;
}

//------------------------------------------------------------------------------
/**
 * @brief 重複しない製品用の依存用途を追加します
 * @param[in,out] ids 所有する依存用途列
 * @param[in] id 追加する固定用途
 * @throws OperationError 比較専用または未知の用途
 */
//------------------------------------------------------------------------------
void addRequired( std::vector<ModelId> &ids, const ModelId id)
{
	// 比較専用LM等を通常取得へ混入させない
	if( modelDownloadDefinition( id).kind == ModelTransferKind::UNAVAILABLE) {
		throw OperationError( ErrorKind::MODEL_REJECTED) ;
	}
	// 同じ用途を複数の処理が要求してもfile数を増やさない
	if( std::find( ids.begin(), ids.end(), id) == ids.end()) {
		ids.push_back( id) ;
	}
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 選択ASRと有効な処理から必要fileを一意に解決します
 * @param[in] options ジョブまたは準備操作で固定した設定
 * @return registryの用途だけを保持する所有列
 * @throws OperationError 未定義構成や準備対象外の依存
 * @see koefude::requiredModels
 */
//------------------------------------------------------------------------------
std::vector<ModelId> requiredModels( const EngineOptions &options)
{
	const auto			&asr = asrDefinition( options.asrModel, options.asrProfile) ; // 選択ASRの依存
	const auto			&roles = sharedModelRoles() ; // 単一registryの共有用途
	std::vector<ModelId> ids ; // 最大6fileの重複しない依存列
	// APIにはASR weightがなく、選択した共有用途だけを準備します
	if( asr.modelId) {
		addRequired( ids, *asr.modelId) ;
	}
	// graphと外部weightを同時に準備対象とする
	if( asr.externalDataModel) {
		addRequired( ids, *asr.externalDataModel) ;
	}
	// CTCの文字tokenをweightと同じ計画で解決する
	if( asr.tokensModel) {
		addRequired( ids, *asr.tokensModel) ;
	}
	// VADを無効にした操作では不要な取得を要求しない
	if( options.speechFilter) {
		addRequired( ids, roles.voiceActivity) ;
	}
	// 話者分離を行う操作だけに共有の区間・埋込みmodelを加える
	if( options.diarization) {
		addRequired( ids, roles.segmentation) ;
		addRequired( ids, roles.embedding) ;
	}
	// 日本語Qualityの明示音声操作はPOST前に後続のPC内参照モデルも準備する
	if( options.asrModel == AsrModel::OPEN_AI_AUDIO && options.asrProfile == AsrProfile::QUALITY &&
	   !options.autoLanguage && options.diarization) {
		const auto &reference = asrDefinition( AsrModel::PARAKEET, AsrProfile::QUALITY) ; // 同じregistry
		// 通常Qualityの補助assetが未定義なら音声POSTより前に拒否する
		if( !reference.modelId || !reference.tokensModel || !reference.externalDataModel) {
			throw OperationError( ErrorKind::MODEL_REJECTED) ;
		}
		addRequired( ids, *reference.modelId) ;
		addRequired( ids, *reference.tokensModel) ;
		addRequired( ids, *reference.externalDataModel) ;
		addRequired( ids, roles.voiceActivity) ;
	}
	return ids ;
}

//------------------------------------------------------------------------------
/**
 * @brief 保存先と既存祖先のlocal・通常directory属性を確認します
 * @param[in] root local絶対directory。未作成部分を許可
 * @return 正規化した所有path。file作成は行いません
 * @throws OperationError 非対応path、reparse/cloud、属性取得失敗
 * @see koefude::validateModelDirectory
 */
//------------------------------------------------------------------------------
std::filesystem::path validateModelDirectory( const std::filesystem::path &root)
{
	return validateModelRoot( root) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 標準ユーザーcacheのlocal絶対pathを取得し、既存の親属性を確認します
 * @return LocalAppData配下のkoefude/models。directoryは作成しません
 * @throws OperationError OS取得失敗、network/reparse/cloud等の非対応保存先
 * @see koefude::defaultModelCache
 */
//------------------------------------------------------------------------------
std::filesystem::path defaultModelCache()
{
	PWSTR		  rawPath = nullptr ; // KnownFolder APIから受け取る所有文字列
	const HRESULT result = SHGetKnownFolderPath( FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, nullptr,
												&rawPath) ; // 作成や同期確認をしない
	const std::unique_ptr<wchar_t, TaskMemoryDeleter> path( rawPath) ; // 失敗時も解放する
	// OSが保存先を返せない場合にworking directoryへ退避しない
	if( FAILED(result) || !path) {
		throw OperationError( ErrorKind::MODEL_REJECTED, static_cast<std::uint32_t>( result)) ;
	}
	return validateModelRoot( std::filesystem::path( path.get()) / L"koefude" / L"models") ;
}

//------------------------------------------------------------------------------
/**
 * @brief 同じ検証境界で合成fileや固定modelを検査します
 * @param[in] root local絶対directory
 * @param[in] definition 呼出し中有効な信頼済み期待値。外部入力は禁止
 * @param[in] stop 協調取消し
 * @return 検証時点の状態。返却時に検査lockは解放済みです
 * @throws OperationError 属性拒否、I/O失敗、取消し
 * @see koefude::inspectModelFile
 */
//------------------------------------------------------------------------------
ModelPresence inspectModelFile( const std::filesystem::path &root, const ModelDefinition &definition,
							   const std::stop_token stop)
{
	checkCancelled( stop) ;
	const auto checkedRoot = validateModelRoot( root) ; // 通信前に親directory属性を確認
	try {
		const auto verified = checkModelFile( checkedRoot, definition, stop) ; // 検査中の所有lock
		checkCancelled( stop) ;
		return verified ? ModelPresence::VALID : ModelPresence::CORRUPT ;
	} catch( const OperationError &error) { // 未取得だけを準備可能な状態へ変換する
		// 属性拒否・access denied・取消しはそのまま呼出し元へ伝える
		if( error.kind() == ErrorKind::MODEL_MISSING &&
		   (error.code() == ERROR_FILE_NOT_FOUND || error.code() == ERROR_PATH_NOT_FOUND)) {
			return ModelPresence::MISSING ;
		}
		throw ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 単一fileの有無と内容を検査します
 * @param[in] root 呼出し中有効な検査済みlocal絶対directory
 * @param[in] id 固定registryの用途
 * @param[in] stop 協調取消し
 * @return size/hashの検査結果。I/O失敗をMissingへ変換しません
 * @throws OperationError 属性拒否、I/O失敗、取消し
 * @see koefude::LocalModelInspector::inspect
 */
//------------------------------------------------------------------------------
ModelPresence LocalModelInspector::inspect( const std::filesystem::path &root, const ModelId id,
										   const std::stop_token stop)
{
	return inspectModelFile( root, modelDefinition( id), stop) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 必要fileだけを検査し、既取得の再利用と不足分の準備計画を作成します
 * @param[in] options 選択構成。model_rootは使わず明示root引数を検査します
 * @param[in] bundleRoot 同梱のlocal絶対directory。欠落を許可
 * @param[in] cacheRoot cacheのlocal絶対directory。欠落を許可
 * @param[in,out] inspector 同じworkerが同期呼出しする検査実装。呼出し中有効
 * @param[in] stop 検査中の協調取消し
 * @return 全検査成功時だけ返す所有計画。copy/取得/書込みは実行しません
 * @throws OperationError 不正root、未知構成、I/O失敗、取消し
 * @note worker専用。engineは使用時に固定hashと所有lockを再取得します
 * @see koefude::planModelPreparation
 */
//------------------------------------------------------------------------------
ModelPreparationPlan planModelPreparation( const EngineOptions &options, const std::filesystem::path &bundleRoot,
										  const std::filesystem::path &cacheRoot, IModelInspector &inspector,
										  const std::stop_token stop)
{
	checkCancelled( stop) ;
	ModelPreparationPlan plan ; // 完全な検査成功時だけ返す所有結果
	plan.bundleRoot = validateModelRoot( bundleRoot) ;
	plan.cacheRoot = validateModelRoot( cacheRoot) ;
	const auto ids = requiredModels( options) ; // 選択構成だけの固定用途
	bool	   allCache = true ; // cacheだけで全依存が揃う条件
	bool	   allBundle = true ; // 同梱だけで全依存が揃う条件
	// 必要fileを一度ずつ検査し、未選択modelへ触れない
	for( const ModelId id : ids) { // 今回検査する用途
		checkCancelled( stop) ;
		const auto cached = inspector.inspect( plan.cacheRoot, id, stop) ; // cacheの確認結果
		const auto bundled = plan.cacheRoot == plan.bundleRoot
								 ? cached
								 : inspector.inspect( plan.bundleRoot, id, stop) ; // 同梱の確認結果
		allCache = allCache && cached == ModelPresence::VALID ;
		allBundle = allBundle && bundled == ModelPresence::VALID ;
		plan.entries.push_back( {id, cached, bundled, ModelPreparationAction::DOWNLOAD}) ;
		addBytes( plan.requiredBytes, modelDefinition( id).bytes) ;
	}
	plan.ready = allCache || allBundle ;
	plan.effectiveRoot = allCache || !allBundle ? plan.cacheRoot : plan.bundleRoot ;
	// 一つのrootに揃う操作を割り当て、必要な転送だけを集計する
	for( auto &entry : plan.entries) { // 検査済みの用途と操作
		// cacheが完全なら同梱fileのcopyを不要とする
		if( allCache) {
			entry.action = ModelPreparationAction::REUSE_CACHE ;
		}
		// 同梱が完全なら全fileを同じ同梱rootから使う
		else if( allBundle) {
			entry.action = ModelPreparationAction::REUSE_BUNDLE ;
		}
		// 部分cacheの有効fileを保持する
		else if( entry.cache == ModelPresence::VALID) {
			entry.action = ModelPreparationAction::REUSE_CACHE ;
		}
		// 同梱から揃えられるfileは再取得しない
		else if( entry.bundle == ModelPresence::VALID) {
			entry.action = ModelPreparationAction::COPY_BUNDLE ;
			addBytes( plan.copyBytes, modelDefinition( entry.id).bytes) ;
		} else { // 両方で欠落または内容不一致の場合だけ取得予定にする
			const auto &download = modelDownloadDefinition( entry.id) ; // 固定artifactの借用参照
			const bool	existing = std::any_of( plan.transfers.begin(), plan.transfers.end(),
				//------------------------------------------------------------------------------
				/**
				 * @brief 同じ転送を照合します
				 * @param[in] id 登録済みの代表用途
				 * @return 同一URL/size/hashならtrue
				 */
				//------------------------------------------------------------------------------
				[&download]( const ModelId id)
				{
					const auto &other = modelDownloadDefinition( id) ; // 既登録のartifact
					return other.url == download.url &&
						   other.transferBytes == download.transferBytes &&
						   other.transferSha256 == download.transferSha256 ;
				}) ; // archive共有による二重転送を避ける条件
			// 初めて必要になったartifactだけを転送容量へ加算する
			if( !existing) {
				plan.transfers.push_back( entry.id) ;
				addBytes( plan.downloadBytes, download.transferBytes) ;
			}
		}
	}
	checkCancelled( stop) ;
	return plan ;
}

} // namespace koefude
