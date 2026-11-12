//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_native_corrector.cpp
 * @brief 固有DLLの安全な同期呼出しと全文応答の束縛を実装します
 * @version 1.0.0
 * @date 2026/10/19
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/19 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/19 ABI検証・取消し・callback・module解放を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 全量封印・operationと現在実効方式を入口で照合
 * 0.1.0 Shunji Ogawa 2026/10/19 封印証拠と資源停止・元engine診断を接続
 * 0.1.0 Shunji Ogawa 2026/10/19 一回実測の受動観測と証拠保持を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 固定AVX2方式と通常workerの検証接続を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 任意観測のatomic markerでnative API区間を公開
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1310のFinal1536・派生予算と限定検証へ整合
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
#include "inference/whole_text_native_corrector.h"
#include "core/types.h"
#include "inference/whole_text_observation.h"
#include "platform/win_helpers.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>

namespace koefude {
namespace {
constexpr std::uint32_t K_ABI = static_cast<std::uint32_t>( KoefudeWtDiagnostic::ABI) ; // 固定構造診断
constexpr std::uint32_t K_CALLBACK =
	static_cast<std::uint32_t>( KoefudeWtDiagnostic::CALLBACK_FAILURE) ; // callback診断
//------------------------------------------------------------------------------
/**
 * @brief 不成立を本文なしで拒否します
 * @param[in] valid 条件
 * @param[in] kind 失敗分類
 * @param[in] code 元診断
 */
//------------------------------------------------------------------------------
void requireHost( const bool valid, const ErrorKind kind, const std::uint32_t code = K_ABI)
{
	// 不完全候補を既存workerへ成功として渡さない
	if( !valid) {
		throw OperationError( kind, code) ;
	}
}
/** @brief callbackの失敗flagと借用通知をcall終了まで保持します */
struct CallbackOwner {
	/// atomicな停止参照
	std::stop_token							stop ;
	/// entry workerだけが呼ぶ
	const ProgressCallback				   &progress ;
	/// 同時callbackの協調監視
	std::shared_ptr<WholeTextResourceGuard> guard ;
	/// native CPU threadから読める例外flag
	std::atomic<bool>						failed{false} ;
	/// callback終了までの借用出力
	WholeTextApplicationObservation		   *observation = nullptr ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief CPU threadから例外なしで停止を照会します
 * @param[in] data call終了まで有効なowner、非null
 * @return 非0なら停止
 */
//------------------------------------------------------------------------------
std::uint32_t __cdecl shouldStop( void *data) noexcept
{
	const auto &owner = *static_cast<CallbackOwner *>( data) ; // call stack ownerの借用
	const auto	cancelled = owner.stop.stop_requested() ; // 元の停止優先順を維持
	// user/callback/観測失敗では元どおりguardを照会せず解放へ進みます
	if( cancelled || owner.failed.load() || (owner.observation && owner.observation->failed.load())) {
		// 利用者停止だけを資源診断と別の時刻へ記録します
		if( cancelled && owner.observation) {
			std::uint64_t previous = 0 ; // 最初の観測だけを採用
			owner.observation->cancelObservedUs.compare_exchange_strong( previous, wholeTextObservationTime()) ;
		}
		return 1U ;
	}
	const auto resourceStop = owner.guard && owner.guard->poll() ; // 元の100ms policy
	// pollで得た最初の失敗をcallback内のatomic値だけへ保存します
	if( resourceStop && owner.observation) {
		std::uint64_t previous = 0 ; // 最初の観測だけを採用
		owner.observation->resourceFailureObservedUs.compare_exchange_strong( previous, wholeTextObservationTime()) ;
	}
	return resourceStop ? 1U : 0U ;
}
//------------------------------------------------------------------------------
/**
 * @brief worker上の実段階を既存通知へ変換し、例外を捕捉します
 * @param[in] data call stack owner
 * @param[in] stage 固定段階
 * @param[in] completed 実完了数
 * @param[in] total 0なら不定
 * @return 非0なら解放要求
 */
//------------------------------------------------------------------------------
std::uint32_t __cdecl nativeProgress( void *data, const std::uint32_t stage, const std::uint32_t completed,
									 const std::uint32_t total) noexcept
{
	auto &owner = *static_cast<CallbackOwner *>( data) ; // worker限定の通知借用
	try {
		requireHost( stage >= 1 && stage <= 4 && (total == 0 || completed <= total), ErrorKind::INFERENCE_FAILED,
					K_CALLBACK) ;
		// 観測の排他は集約更新だけで解放し、下の通知を包みません
		if( owner.observation) {
			owner.observation->recordStage( stage, completed, total) ;
		}
		// 通知先がある場合だけ、実進捗または不定を伝える
		if( owner.progress) {
			const auto percent = total == 0 ? -1
											: static_cast<int>( (static_cast<std::uint64_t>( completed) * 100) /
															   total) ; // overflowしない整数計算
			owner.progress( {stage == 1	 ? Stage::LOADING_MODEL
							: stage == 4 ? Stage::COMBINING
										 : Stage::TRANSCRIBING,
							percent}) ;
		}
	} catch( ...) {
		owner.failed.store( true) ;
	} // 例外本文を保存せず停止flagだけ保持
	return shouldStop( data) ;
}
//------------------------------------------------------------------------------
/**
 * @brief statusを既存errorへ写像します
 * @param[in] status 固定整数
 * @return error分類、成功/未知はInvalidInput
 */
//------------------------------------------------------------------------------
[[nodiscard]] ErrorKind errorKind( const std::uint32_t status) noexcept
{
	// ABI statusごとの製品分類を固定する
	switch( static_cast<KoefudeWtStatus>( status)) {
		case KoefudeWtStatus::ABI_MISMATCH:
			return ErrorKind::MODEL_REJECTED ; // metadata違反
		case KoefudeWtStatus::MODEL_UNAVAILABLE:
			return ErrorKind::MODEL_MISSING ; // 未配置
		case KoefudeWtStatus::RESOURCE_UNAVAILABLE:
			return ErrorKind::RESULT_TOO_LARGE ; // 容量不足
		case KoefudeWtStatus::CANCELLED:
			return ErrorKind::CANCELLED ; // 協調停止
		case KoefudeWtStatus::BUSY:
		case KoefudeWtStatus::INFERENCE_FAILED:
		case KoefudeWtStatus::INTERNAL:
			return ErrorKind::INFERENCE_FAILED ; // engine/内部異常
		default:
			return ErrorKind::INVALID_INPUT ; // 不正/不完/未知/偽成功
	}
}
/** @brief moduleを同期scope内だけ所有します */
class ModuleOwner final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief load済みhandleを受け取ります
	 * @param[in] handle 非nullの所有module
	 * @param[out] observation null可、解放まで有効
	 */
	//------------------------------------------------------------------------------
	explicit ModuleOwner( HMODULE handle, WholeTextApplicationObservation *observation) noexcept
		: handle_( handle), observation_( observation)
	{
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 例外時にもmoduleを解放します
	 */
	//------------------------------------------------------------------------------
	~ModuleOwner()
	{
		static_cast<void>( close()) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 明示解放し戻り値を保持します
	 * @return 0なら解放成功、非0は元code
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint32_t close() noexcept
	{
		const auto module = std::exchange( handle_, nullptr) ; // 二重freeを防止
		// 所有したmoduleだけを一度解放する
		if( module) {
			const auto code = FreeLibrary( module) ? 0U : GetLastError() ; // 実結果を一回取得
			// destructor経路も実解放結果を保存します
			if( observation_) {
				observation_->freeLibraryCalled = true ;
				observation_->freeLibraryCode = code ;
				observation_->freeLibraryEndUs = wholeTextObservationTime() ;
			}
			return code ;
		}
		return 0 ;
	}

	private:
	/// nativeの全cleanupより長い所有scope
	HMODULE							 handle_ ;
	/// 解放より長い非所有出力
	WholeTextApplicationObservation *observation_ ;
} ;
} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 所有・info・出力を照合し既存全文応答へ変換します
 * @param[in] input 不変の準備owner
 * @param[in] source 同じ共有原文owner
 * @param[in] request 固定要求
 * @param[in] calls 同期中有効な関数表
 * @param[in] stop 協調取消し
 * @param[in] progress worker通知
 * @param[in] guard callbackと解放より長く保持する資源監視、旧内部用途だけnull可
 * @param[out] observation null可。同期returnまで有効で、callback終了後だけ読みます
 * @return 全体検証前の自然終端応答
 * @throws OperationError 固定診断付き失敗
 * @see koefude::detail::invokeWholeTextNative
 */
//------------------------------------------------------------------------------
WholeTextCorrectionResponse detail::invokeWholeTextNative( const PreparedWholeTextNativeInput &input,
														  std::shared_ptr<const Transcript>	  source,
														  const WholeTextCorrectionRequest	 &request,
														  const WholeTextNativeCalls &calls, const std::stop_token stop,
														  const ProgressCallback				 &progress,
														  std::shared_ptr<WholeTextResourceGuard> guard,
														  WholeTextApplicationObservation		 *observation)
{
	// 準備後の資源境界もnative呼出し前に確認します
	if( guard) {
		guard->check( stop) ;
	}
	validatePreparedWholeTextNativeInput( input, source, request, stop) ;
	requireHost( calls.info && calls.generate, ErrorKind::MODEL_REJECTED) ;
	requireHost( !stop.stop_requested(), ErrorKind::CANCELLED,
				static_cast<std::uint32_t>( KoefudeWtDiagnostic::CANCELLED)) ;
	KoefudeWtInfoV1 info{sizeof(info), 1} ; // 出力情報、全fieldを後で照合
	// 実APIの入口だけを呼出し数へ数えます
	if( observation) {
		++observation->nativeInfoCalls ;
	}
	const auto infoStatus = calls.info( &info) ; // modelを開かない同期呼出し
	// 不正infoも成功判定前に保存します
	if( observation) {
		observation->nativeInfo = info ;
		observation->nativeInfoStatus = infoStatus ;
	}
	const auto &expected = wholeTextNativeExpectedInfo() ; // profileと同じ固定ABI情報
	requireHost( infoStatus == 0 && info.structSize == expected.structSize && info.abiVersion == expected.abiVersion &&
					info.pointerBits == expected.pointerBits && info.flags == expected.flags &&
					std::memcmp( info.engineCommit, expected.engineCommit, sizeof(info.engineCommit)) == 0 &&
					std::memcmp( info.templateSha256, expected.templateSha256, sizeof(info.templateSha256)) == 0 &&
					info.contextTokens == expected.contextTokens && info.prefixTokensMax == expected.prefixTokensMax &&
					info.outputTokensMax == expected.outputTokensMax &&
					info.analysisSampledMax == expected.analysisSampledMax &&
					info.finalSampledMax == expected.finalSampledMax,
				ErrorKind::MODEL_REJECTED) ;
	std::size_t remaining = K_MAXIMUM_TEXT_BYTES ; // 原文＋全文の容量残量
	// 減算前に元行のbyteを照合し、過大な加算を避ける
	for( const auto &line : source->lines) { // 元行の不変参照
		requireHost( line.text.size() <= remaining, ErrorKind::RESULT_TOO_LARGE) ;
		remaining -= line.text.size() ;
	}
	std::vector<std::uint8_t> output( remaining) ; // hostだけが確保/解放するbuffer
	CallbackOwner			  callbacks{stop, progress, guard, false, observation} ; // 全native thread終了まで生存
	const auto				 &path = input.model().path.native() ; // UTF-16の不変borrow
	// C ABIのbyte pointerは同期call中だけ元stringを借用し、書き換えない
	const KoefudeWtRequestV1  nativeRequest{
		 sizeof(nativeRequest),
		 1,
		 path.c_str(),
		 static_cast<std::uint32_t>( path.size()),
		 0,
		 {reinterpret_cast<const std::uint8_t *>( input.materials()->systemUtf8().data()),
		  static_cast<std::uint32_t>( input.materials()->systemUtf8().size()), 0},
		 {reinterpret_cast<const std::uint8_t *>( input.materials()->userUtf8().data()),
		  static_cast<std::uint32_t>( input.materials()->userUtf8().size()), 0},
		 shouldStop,
		 nativeProgress,
		 &callbacks} ;
	KoefudeWtResultV1 result{sizeof(result), 1, output.data(), static_cast<std::uint32_t>( output.size())} ; // POD
	// 生成入口を記録してから既存の同期APIを一度呼びます
	if( observation) {
		++observation->nativeGenerateCalls ;
		observation->generateBeginUs = wholeTextObservationTime() ;
		observation->generateEnteredUs.store( observation->generateBeginUs) ;
	}
	const auto status = calls.generate( &nativeRequest, &result) ; // 戻る時にはnative全資源解放済み
	// 失敗値もpointerを除いた全scalarだけを保存します
	if( observation) {
		observation->generateEndUs = wholeTextObservationTime() ;
		observation->generateLeftUs.store( observation->generateEndUs) ;
		observation->nativeStatus = status ;
		observation->nativeResultObserved = true ;
		observation->nativeResult = {result.structSize,
									 result.abiVersion,
									 result.textCapacity,
									 result.textBytes,
									 result.naturalEog,
									 result.diagnostic,
									 result.engineCode,
									 result.prefixTokens,
									 result.analysisSampledTokens,
									 result.closingControlTokens,
									 result.finalSampledTokens,
									 result.reserved} ;
	}
	const auto code = result.engineCode != 0 ? static_cast<std::uint32_t>( result.engineCode)
											 : result.diagnostic ; // 元符号を32bit保持
	requireHost( !stop.stop_requested(), ErrorKind::CANCELLED, code) ;
	// 全native資源解放後に資源停止を分類し、元engine codeを保持します
	if( guard) {
		guard->check( stop, static_cast<std::uint32_t>( result.engineCode)) ;
	}
	requireHost( !callbacks.failed.load(), ErrorKind::INFERENCE_FAILED, K_CALLBACK) ;
	requireHost( !observation || !observation->failed.load(), ErrorKind::INFERENCE_FAILED, K_CALLBACK) ;
	requireHost( status == 0, errorKind( status), code) ;
	requireHost( result.structSize == sizeof(result) && result.abiVersion == 1 && result.reserved == 0 &&
					result.text == output.data() && result.textCapacity == output.size() && result.textBytes > 0 &&
					result.textBytes <= output.size() && result.naturalEog == 1 && result.diagnostic == 0 &&
					result.engineCode == 0 && result.prefixTokens >= 1 && result.prefixTokens <= 4096 &&
					result.analysisSampledTokens >= 3 && result.analysisSampledTokens <= 1279 &&
					result.closingControlTokens <= 1 &&
					(result.closingControlTokens == 0 || result.analysisSampledTokens == 1279) &&
					result.finalSampledTokens >= 2 && result.finalSampledTokens <= 1536 &&
					result.analysisSampledTokens + result.closingControlTokens + result.finalSampledTokens <= 2816,
				ErrorKind::INVALID_INPUT, K_ABI) ;
	WholeTextCorrectionResponse response ; // 既存DTOだけへFinalを写す
	response.operationId = request.operationId ;
	response.termination = WholeTextTermination::NATURAL_END ;
	response.result.source = request.source ;
	response.result.profileId = request.profileId ;
	response.result.profileSha256 = request.profileSha256 ;
	WholeTextCorrectionEvidence evidence ; // Preparedから持ち出す小さい証拠
	evidence.binding = input.binding() ;
	const auto &audio = input.materials()->capture().primaryAudio ; // 元ASRと照合済み
	evidence.audio = {1, audio.sampleCount, audio.sampleRate, 1, audio.originSample, audio.pcmSha256} ;
	response.result.evidence = std::move( evidence) ;
	response.result.text.assign( reinterpret_cast<const char *>( output.data()), result.textBytes) ;
	// 既存workerの全体検証も省略せず、入口seam単独でも不正UTF-8を採用しない
	const ValidatedWholeTextCorrectionCompletion verified(
		source, request,
		std::make_shared<const WholeTextCorrectionResponse>( response)) ; // 本文は検証だけ
	return response ;
}
//------------------------------------------------------------------------------
/**
 * @brief 値とhandleだけmoveし、DLLやmodelを開きません
 * @param[in] modulePath 絶対localの固有名DLL
 * @param[in] input 準備済み所有値
 * @param[in] guard 同期callと全解放まで保持する資源監視、旧内部用途だけnull可
 * @param[out] observation null可。corrector破棄まで有効な受動出力
 * @param[in] runtime AVX2で必須の検証owner
 * @param[in] loader 通常は空、内部試験のみ
 * @see koefude::NativeWholeTextCorrector::NativeWholeTextCorrector
 */
//------------------------------------------------------------------------------
NativeWholeTextCorrector::NativeWholeTextCorrector( std::filesystem::path modulePath, PreparedWholeTextNativeInput input,
												   std::shared_ptr<WholeTextResourceGuard>		   guard,
												   WholeTextApplicationObservation				  *observation,
												   std::shared_ptr<const VerifiedWholeTextRuntime> runtime,
												   detail::WholeTextNativeLoader				   loader)
	: loader_( std::move( loader)), runtime_( std::move( runtime)), modulePath_( std::move( modulePath)),
	  guard_( std::move( guard)), input_( std::move( input)), observation_( observation)
{
}
//------------------------------------------------------------------------------
/**
 * @brief 不変原文から自由な全文を返します
 * @param[in] source 呼出し中有効な共有所有原文。null不可
 * @param[in] request hostが固定した原文・ID・方式
 * @param[in] stop 協調取消し
 * @param[in] progress workerでの実処理通知
 * @return 未検証の所有応答。失敗は例外で返します
 * @note 同期呼出し内でengine資源を解放し、cleanup失敗を成功にしません
 * @see koefude::NativeWholeTextCorrector::correct
 */
//------------------------------------------------------------------------------
WholeTextCorrectionResponse NativeWholeTextCorrector::correct( std::shared_ptr<const Transcript> source,
															  const WholeTextCorrectionRequest &request,
															  const std::stop_token				stop,
															  const ProgressCallback		   &progress)
{
	validatePreparedWholeTextNativeInput( input_, source, request, stop) ;
	const auto *descriptor = findWholeTextRuntime( request.profileId, request.profileSha256) ; // 固定方式
	requireHost( descriptor != nullptr, ErrorKind::INVALID_INPUT) ;
	requireHost( modulePath_.is_absolute() && modulePath_.filename() == descriptor->dllName, ErrorKind::INVALID_INPUT) ;
	// AVX2では未検証ownerからのloadを禁止します。旧portable内部用途は互換です
	if( descriptor->variant == WholeTextRuntimeVariant::AVX2_NO_REPACK || runtime_) {
		requireHost( runtime_ && runtime_->matches( descriptor->variant, modulePath_), ErrorKind::MODEL_REJECTED,
					0x57560002) ;
	}
	// 内部fake loaderでも同じowner照合とinfo/generate検証を省略しません
	if( loader_) {
		auto loaded = loader_( modulePath_, runtime_) ; // ownerを同じ同期scopeへ共有
		requireHost( static_cast<bool>( loaded.close), ErrorKind::INTERNAL) ;
		WholeTextCorrectionResponse response ; // 正常解放後だけ返す所有値
		try {
			response = detail::invokeWholeTextNative( input_, source, request, loaded.calls, stop, progress, guard_,
													 observation_) ;
		} catch( ...) {
			static_cast<void>( loaded.close()) ; // 失敗時もcallback終了後に一度だけ解放
			throw ;
		}
		const auto cleanup = loaded.close() ; // 全callback完了後、owner保持中
		requireHost( !stop.stop_requested(), ErrorKind::CANCELLED, cleanup) ;
		// 解放後の資源停止を成功へ変更しません
		if( guard_) {
			guard_->check( stop) ;
		}
		requireHost( cleanup == 0, ErrorKind::INFERENCE_FAILED, cleanup) ;
		return response ;
	}
	std::filesystem::path absolute ; // 検証後だけmoduleへ渡すlocal絶対path
	try {
		absolute = validateLocalFile( modulePath_) ;
	} catch( const OperationError &error) {
		// 欠落DLLだけをモデル不足へ写像し、非local等の拒否は維持する
		if( error.code() == ERROR_FILE_NOT_FOUND || error.code() == ERROR_PATH_NOT_FOUND) {
			throw OperationError( ErrorKind::MODEL_MISSING, error.code()) ;
		}
		throw ;
	}
	UniqueHandle lock( CreateFileW( absolute.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
								  FILE_ATTRIBUTE_NORMAL,
								  nullptr)) ; // write/delete拒否lease
	requireHost( lock && lock.get() != INVALID_HANDLE_VALUE, ErrorKind::MODEL_MISSING, GetLastError()) ;
	const auto handle = LoadLibraryExW( absolute.c_str(), nullptr,
									   LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32) ; // PATH非依存
	requireHost( handle != nullptr, ErrorKind::MODEL_MISSING, GetLastError()) ;
	ModuleOwner module( handle, observation_) ; // 全callback/engine解放後まで保持
	// 成功したmodule取得だけを記録します
	if( observation_) {
		observation_->moduleLoaded = true ;
	}
	// GetProcAddressの固定C関数pointerへ変換し、module lifetime内だけ使うAPI境界
	const detail::WholeTextNativeCalls calls{
		reinterpret_cast<decltype(&koefude_whole_text_info_v1)>( GetProcAddress( handle, "koefude_whole_text_info_v1")),
		reinterpret_cast<decltype(&koefude_whole_text_generate_v1)>(
			GetProcAddress( handle, "koefude_whole_text_generate_v1"))} ;
	auto response = detail::invokeWholeTextNative( input_, std::move( source), request, calls, stop, progress, guard_,
												  observation_) ; // 同期所有応答
	const auto cleanup = module.close() ; // FreeLibrary失敗は成功を抑止
	requireHost( !stop.stop_requested(), ErrorKind::CANCELLED, cleanup) ;
	// user停止、資源停止、解放失敗の優先順を維持します
	if( guard_) {
		guard_->check( stop) ;
	}
	requireHost( cleanup == 0, ErrorKind::INFERENCE_FAILED, cleanup) ;
	return response ;
}
} // namespace koefude
