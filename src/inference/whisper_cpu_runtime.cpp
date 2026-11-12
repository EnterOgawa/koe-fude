//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whisper_cpu_runtime.cpp
 * @brief baseline CPU確認後の固定Whisper DLLと同module contextを所有します
 * @version 1.0.0
 * @date 2026/10/21
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/21 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1301の検証・遅延読込み・一回解放を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1417のnative状態・出力・所有寿命を実装へ照合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "inference/whisper_cpu_runtime.h"
#include "whisper_cpu_runtime_manifest.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bcrypt.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <new>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <windows.h>

namespace koefude::whisper_cpu {
namespace {
std::atomic<bool> cleanupFailed = false ; // production最適化moduleの再利用拒否だけを保持
//------------------------------------------------------------------------------
/**
 * @brief 不成立の固定資産を読込み前に拒否します
 * @param[in] condition 成立条件
 * @param[in] code 内容を含まない診断
 */
//------------------------------------------------------------------------------
void			  require( bool condition, std::uint32_t code = 0x57430002)
{
	// 不明値や不一致をportable成功へ変更しません
	if( !condition) {
		throw OperationError( ErrorKind::MODEL_REJECTED, code) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 同じfileをwrite/delete拒否で所有します
 * @param[in] path 固定local絶対path
 * @return 検証中の所有handle
 */
//------------------------------------------------------------------------------
UniqueHandle leaseFile( const std::filesystem::path &path)
{
	try {
		static_cast<void>( validateLocalFile( path)) ;
	} catch( const OperationError &error) {
		// 欠落は配布不足、それ以外は不正資産へ区別します
		if( error.code() == ERROR_FILE_NOT_FOUND || error.code() == ERROR_PATH_NOT_FOUND) {
			throw OperationError( ErrorKind::MODEL_MISSING, 0x57430003) ;
		}
		throw OperationError( ErrorKind::MODEL_REJECTED, 0x57430002) ;
	}
	UniqueHandle handle( CreateFileW( path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
									FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
									nullptr)) ; // 変更と削除を拒否するlease
	// 属性確認とopenの間の変化も正常成功へ置き換えません
	if( !handle || handle.get() == INVALID_HANDLE_VALUE) {
		const auto code = GetLastError() ; // 失敗した同APIの値
		throw OperationError( code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND ? ErrorKind::MODEL_MISSING
																						  : ErrorKind::MODEL_REJECTED,
							 code) ;
	}
	BY_HANDLE_FILE_INFORMATION info{} ; // 開いた同一fileの属性
	require( GetFileType( handle.get()) == FILE_TYPE_DISK && GetFileInformationByHandle( handle.get(), &info) &&
			(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE |
									  FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS)) == 0) ;
	return handle ;
}
//------------------------------------------------------------------------------
/**
 * @brief 同じleaseを有界bufferで全量hashします
 * @param[in] file 所有handleの借用
 * @param[in] bytes 正確な全byte数
 * @param[in] stop 協調停止
 * @return 小文字SHA-256
 */
//------------------------------------------------------------------------------
std::string hashFile( HANDLE file, std::uint64_t bytes, std::stop_token stop)
{
	LARGE_INTEGER size{} ; // 同じfileの全長
	require( GetFileSizeEx( file, &size) && size.QuadPart >= 0 && static_cast<std::uint64_t>( size.QuadPart) == bytes) ;
	BCRYPT_ALG_HANDLE rawAlgorithm = nullptr ; // 同期SHA provider
	require( BCryptOpenAlgorithmProvider( &rawAlgorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0) ;
	const auto closeAlgorithm =
		//------------------------------------------------------------------------------
		/**
		 * @brief 同じCNG providerを閉じます
		 * @param[in] value 所有handle
		 */
		//------------------------------------------------------------------------------
		[]( void *value)
		{
			BCryptCloseAlgorithmProvider( value, 0) ;
		} ;
	const std::unique_ptr<void, decltype(closeAlgorithm)> algorithm( rawAlgorithm, closeAlgorithm) ;
	BCRYPT_HASH_HANDLE									  rawHash = nullptr ; // 同期hash state
	require( BCryptCreateHash( rawAlgorithm, &rawHash, nullptr, 0, nullptr, 0, 0) >= 0) ;
	const auto closeHash =
		//------------------------------------------------------------------------------
		/**
		 * @brief 同じCNG stateを閉じます
		 * @param[in] value 所有handle
		 */
		//------------------------------------------------------------------------------
		[]( void *value)
		{
			BCryptDestroyHash( value) ;
		} ;
	const std::unique_ptr<void, decltype(closeHash)> hash( rawHash, closeHash) ;
	std::array<std::uint8_t, 65536>					 buffer{} ; // 固定RAM上限
	std::uint64_t									 read = 0 ; // 実完了byte数
	// 全byteを読み、bufferごとに取消しを確認します
	while( read < bytes) {
		checkCancelled( stop) ;
		const auto wanted = static_cast<DWORD>( std::min<std::uint64_t>( buffer.size(), bytes - read)) ;
		DWORD	   actual = 0 ; // 同期readの実byte数
		require( ReadFile( file, buffer.data(), wanted, &actual, nullptr) && actual == wanted) ;
		require( BCryptHashData( rawHash, buffer.data(), actual, 0) >= 0) ;
		read += actual ;
	}
	std::array<std::uint8_t, 32> digest{} ; // 全SHA値
	require( BCryptFinishHash( rawHash, digest.data(), static_cast<ULONG>( digest.size()), 0) >= 0) ;
	constexpr char K_HEX[] = "0123456789abcdef" ; // 固定alphabet
	std::string	   text ; // 全64文字
	// 全digest byteを順序通りに変換します
	for( const auto value : digest) { // 現在の一byte
		text.push_back( K_HEX[value >> 4]) ;
		text.push_back( K_HEX[value & 15]) ;
	}
	return text ;
}
/** @brief module解放後まで固定DLL/receiptの二leaseを保持します */
struct Leases {
	/// write/delete拒否のDLL
	UniqueHandle dll ;
	/// write/delete拒否の固定receipt
	UniqueHandle receipt ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief completed receiptの全byte pinとDLL全量を照合します
 * @param[in] path 固定basenameの絶対path
 * @param[in] pin CMakeが厳密検証した値
 * @param[in] stop 協調停止
 * @return moduleより長く保持する二lease
 */
//------------------------------------------------------------------------------
std::shared_ptr<const void> verifyAssets( const std::filesystem::path &path, const Pin &pin, std::stop_token stop)
{
	checkCancelled( stop) ;
	require( pin.present && pin.bytes > 0 && pin.dllSha.size() == 64 && pin.receiptSha.size() == 64 &&
			pin.abiSha.size() == 64 && path.is_absolute() && path.filename() == manifest::K_DLL_NAME) ;
	auto leases = std::make_shared<Leases>() ; // 読込みより前に所有scopeを確保
	leases->dll = leaseFile( path) ;
	leases->receipt = leaseFile( path.parent_path() / manifest::K_RECEIPT_NAME) ;
	LARGE_INTEGER receiptBytes{} ; // 固定小receiptだけを有界に読みます
	require( GetFileSizeEx( leases->receipt.get(), &receiptBytes) && receiptBytes.QuadPart > 0 &&
			receiptBytes.QuadPart <= 8192) ;
	// configureでschema/全fieldを検証済みのreceiptと同じ全byteだけを受け入れます
	require( hashFile( leases->receipt.get(), static_cast<std::uint64_t>( receiptBytes.QuadPart), stop) == pin.receiptSha) ;
	require( hashFile( leases->dll.get(), pin.bytes, stop) == pin.dllSha) ;
	return leases ;
}
//------------------------------------------------------------------------------
/**
 * @brief host内の同期操作を一回実行し、C++例外を固定幅statusへ変換します
 * @tparam Action 引数なしで呼べる同期操作型
 * @param[in] action 値で受け取る操作。参照capture先は呼出し終了まで借用し、操作を保持しません
 * @return action実行の正常returnはStatus::OK、実行中のstd::bad_allocはOUT_OF_MEMORY、その他のC++例外はENGINE_FAILURE
 * @note outの初期化/引数検査/rollback/排他は行いません。上流の非0符号やnullはそのまま出力へ残します
 */
//------------------------------------------------------------------------------
template <class Action> Status guarded( Action action) noexcept
{
	try {
		action() ;
		return Status::OK ;
	} catch( const std::bad_alloc &) {
		return Status::OUT_OF_MEMORY ;
	} catch( ...) {
		return Status::ENGINE_FAILURE ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief bounded配列をNUL終端込みで完全比較します
 * @tparam Size 固定配列長
 * @param[in] actual module値
 * @param[in] expected compiled値
 * @return 全値と終端が一致する場合だけtrue
 */
//------------------------------------------------------------------------------
template <std::size_t Size> bool same( const char ( &actual)[Size], std::string_view expected) noexcept
{
	return expected.size() < Size && std::memcmp( actual, expected.data(), expected.size()) == 0 &&
		   actual[expected.size()] == '\0' ;
}
} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 現buildのcompiled pinを返します
 * @return 所有文字列のない固定値
 * @see koefude::whisper_cpu::compiledPin
 */
//------------------------------------------------------------------------------
Pin compiledPin() noexcept
{
	return {manifest::K_SLOT_PRESENT,	manifest::K_DLL_BYTES,		   manifest::K_DLL_SHA256,
			manifest::K_RECEIPT_SHA256, manifest::K_ABI_HEADER_SHA256, manifest::K_CONFIGURATION} ;
}
//------------------------------------------------------------------------------
/**
 * @brief 実CPUと固定EXE隣資産を使います
 * @return 同期worker所有のcallback群
 * @see koefude::whisper_cpu::productionServices
 */
//------------------------------------------------------------------------------
Services productionServices()
{
	Services services ; // 同期worker専用依存
	services.query = whole_text::cpu_candidate::queryHost ;
	services.verify = verifyAssets ;
	services.load =
		//------------------------------------------------------------------------------
		/**
		 * @brief 固定絶対pathを限定探索で読みます
		 * @param[in] path 検証済資産
		 * @return 所有module
		 */
		//------------------------------------------------------------------------------
		[]( const std::filesystem::path &path) -> void *
		{
			const auto module = LoadLibraryExW( path.c_str(), nullptr,
											   LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32) ; // 一参照
			// load失敗を別ISAの再試行へ変更しません
			if( !module) {
				throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
			}
			return module ;
		} ;
	services.getter =
		//------------------------------------------------------------------------------
		/**
		 * @brief 唯一のexportを同moduleから借用します
		 * @param[in] module 所有参照
		 * @return 固定ABI getter
		 */
		//------------------------------------------------------------------------------
		[]( void *module)
		{
			const auto symbol = GetProcAddress( static_cast<HMODULE>( module),
										   "koefude_whisper_cpu_get_api_v1") ; // 固定名だけ
			require( symbol != nullptr, 0x57430004) ;
			// Win32 procedure境界だけで型変換し、moduleより長く保持しません
			return reinterpret_cast<GetApiV1>( symbol) ;
		} ;
	services.close =
		//------------------------------------------------------------------------------
		/**
		 * @brief 自分の一参照だけを閉じます
		 * @param[in] module 所有参照
		 * @return 解放成立
		 */
		//------------------------------------------------------------------------------
		[]( void *module)
		{
			return FreeLibrary( static_cast<HMODULE>( module)) != 0 ;
		} ;
	std::array<wchar_t, 32768> executable{} ; // Windows pathの有限buffer
	const auto				   length = GetModuleFileNameW( nullptr, executable.data(),
														   static_cast<DWORD>( executable.size())) ; // 自EXEだけ
	require( length > 0 && length < executable.size()) ;
	services.path = std::filesystem::path( std::wstring( executable.data(), length)).parent_path() / manifest::K_DLL_NAME ;
	services.pin = compiledPin() ;
	services.enforceProcessLifetime = true ;
	return services ;
}
//------------------------------------------------------------------------------
/**
 * @brief statusをhost所有例外へ変換します
 * @param[in] status DLLの固定幅値
 * @throws std::bad_alloc OOM
 * @throws OperationError その他
 * @see koefude::whisper_cpu::checkStatus
 */
//------------------------------------------------------------------------------
void checkStatus( Status status)
{
	// DLL所有の例外objectをhostへ渡さず、host内でOOMを作ります
	if( status == Status::OUT_OF_MEMORY) {
		throw std::bad_alloc() ;
	}
	// 未知statusも成功としてoutを利用しません
	if( status != Status::OK) {
		throw OperationError( ErrorKind::INFERENCE_FAILED, 0x57431000U | static_cast<std::uint32_t>( status)) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief getter直後、初期化前に全metadataと14pointerを検証します
 * @param[in] api module生存中の表
 * @param[in] pin compiled値
 * @throws OperationError 不一致
 * @see koefude::whisper_cpu::checkApi
 */
//------------------------------------------------------------------------------
void checkApi( const ApiV1 &api, const Pin &pin)
{
	require( api.abiVersion == K_ABI_VERSION && api.tableBytes == sizeof(ApiV1) &&
				api.requiredFeatures == K_REQUIRED_FEATURES && api.xcr0Mask == K_XCR0_MASK &&
				api.mscFullVer == _MSC_FULL_VER && api.pointerBytes == sizeof(void *) &&
				api.contextParamsBytes == sizeof(whisper_context_params) &&
				api.contextParamsAlignment == alignof(whisper_context_params) &&
				api.fullParamsBytes == sizeof(whisper_full_params) &&
				api.fullParamsAlignment == alignof(whisper_full_params) &&
				api.tokenDataBytes == sizeof(whisper_token_data) &&
				api.tokenDataAlignment == alignof(whisper_token_data) && same( api.configuration, pin.configuration) &&
				same( api.engineCommit, K_ENGINE_COMMIT) && same( api.whisperHeaderSha256, K_WHISPER_HEADER_SHA256) &&
				same( api.ggmlHeaderSha256, K_GGML_HEADER_SHA256) &&
				same( api.ggmlCpuHeaderSha256, K_GGML_CPU_HEADER_SHA256) && same( api.abiHeaderSha256, pin.abiSha) &&
				same( api.cpuContract, K_CPU_CONTRACT) && api.logSet && api.contextDefaultParams && api.initFromFile &&
				api.freeContext && api.fullDefaultParams && api.full && api.nSegments && api.segmentText &&
				api.segmentT0 && api.segmentT1 && api.nTokens && api.tokenData && api.tokenText && api.tokenEot,
			0x57430005) ;
}
//------------------------------------------------------------------------------
/**
 * @brief static表を既定とし、実行済み選択だけを記録します
 * @param[in] portable process又は試験scopeで生存する表
 * @param[in] diagnostics null可、Runtimeより長く生存する受動capture
 * @see koefude::whisper_cpu::Runtime::Runtime(const koefude::whisper_cpu::ApiV1 &, koefude::CorrectionDiagnosticCapture *)
 */
//------------------------------------------------------------------------------
Runtime::Runtime( const ApiV1 &portable, CorrectionDiagnosticCapture *diagnostics)
	: api_( &portable), diagnostics_( diagnostics)
{
}
//------------------------------------------------------------------------------
/**
 * @brief 未完了scopeを一回だけ閉じ、失敗は診断に残します
 * @see koefude::whisper_cpu::Runtime::~Runtime
 */
//------------------------------------------------------------------------------
Runtime::~Runtime()
{
	static_cast<void>( closeOnce()) ;
}
//------------------------------------------------------------------------------
/**
 * @brief CPU gate後だけfast資産を読み込みます
 * @param[in] services 同期scopeの依存値
 * @param[in] stop 協調停止
 * @throws OperationError 資産/ABI/取消し。隠れportable再試行を行いません
 * @see koefude::whisper_cpu::Runtime::select
 */
//------------------------------------------------------------------------------
void Runtime::select( Services services, std::stop_token stop)
{
	checkCancelled( stop) ;
	require( !closed_ && !module_ && services.query) ;
	const auto cpu = services.query() ; // baseline queryを必ずloadより前に一回実行
	checkCancelled( stop) ; // 照会中に届いた取消しも資産検査より前に扱います
	// 非適格/不明では資産の検査も行いません
	if( !whole_text::cpu_candidate::supports( whole_text::cpu_candidate::Variant::AVX2, cpu)) {
		// optional診断だけへ実portable理由を記録します
		if( diagnostics_) {
			diagnostics_->whisperSelected( WhisperCpuVariant::PORTABLE, WhisperCpuReason::CPU_UNSUPPORTED, {}) ;
		}
		return ;
	}
	// 空slotは明示portable構成で、予定資産の欠落と区別します
	if( !services.pin.present) {
		// optional診断だけへ実portable理由を記録します
		if( diagnostics_) {
			diagnostics_->whisperSelected( WhisperCpuVariant::PORTABLE, WhisperCpuReason::SLOT_ABSENT, {}) ;
		}
		return ;
	}
	require( services.verify && services.load && services.getter && services.close) ;
	// 未解放context又は参照を残した同processで最適化moduleを再利用しません
	require( !services.enforceProcessLifetime || !cleanupFailed.load( std::memory_order_relaxed), 0x57430007) ;
	services_ = std::move( services) ;
	lease_ = services_.verify( services_.path, services_.pin, stop) ;
	require( lease_ != nullptr) ;
	checkCancelled( stop) ; // 検証後、static initializerより前の取消し境界
	module_ = services_.load( services_.path) ;
	require( module_ != nullptr, 0x57430004) ;
	const auto getter = services_.getter( module_) ; // gate後だけexportに触れます
	require( getter != nullptr, 0x57430004) ;
	const ApiV1 *api = nullptr ; // getter失敗outを利用しません
	checkStatus( getter( K_ABI_VERSION, sizeof(ApiV1), &api)) ;
	require( api != nullptr, 0x57430005) ;
	checkApi( *api, services_.pin) ;
	api_ = api ;
	// 全metadata成立後だけfastの実選択を記録します
	if( diagnostics_) {
		diagnostics_->whisperSelected( WhisperCpuVariant::AVX2, WhisperCpuReason::ELIGIBLE, services_.pin.dllSha) ;
	}
	checkCancelled( stop) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 選択した同module表を借用します
 * @return Runtime生存中の表
 * @see koefude::whisper_cpu::Runtime::api
 */
//------------------------------------------------------------------------------
const ApiV1 &Runtime::api() const noexcept
{
	return *api_ ;
}
//------------------------------------------------------------------------------
/**
 * @brief 初期化で返されたcontextを同表へ一回だけ帰属させます
 * @param[in] context 同表で作成した所有pointer、null可
 * @see koefude::whisper_cpu::Runtime::own
 */
//------------------------------------------------------------------------------
void Runtime::own( whisper_context *context) noexcept
{
	context_ = context ;
}
//------------------------------------------------------------------------------
/**
 * @brief destructorと明示closeに共通の一回cleanupです
 * @return 全解放成立ならtrue
 * @see koefude::whisper_cpu::Runtime::closeOnce
 */
//------------------------------------------------------------------------------
bool Runtime::closeOnce() noexcept
{
	// 明示close後のdestructorで同じfree/FreeLibraryを再試行しません
	if( closed_) {
		return closeOk_ ;
	}
	closed_ = true ;
	std::optional<std::uint32_t> freeStatus ; // context未作成ならUnknown
	std::optional<bool>			 moduleClosed ; // module未作成ならUnknown
	// contextは作った表でだけ一回freeします
	if( context_) {
		const auto context = std::exchange( context_, nullptr) ; // 失敗でも再free禁止
		const auto status = api_->freeContext( context) ; // 全thunkは非送出
		freeStatus = static_cast<std::uint32_t>( status) ;
		closeOk_ = status == Status::OK ;
	}
	// 未解放contextを残してunloadせず、OS参照をprocess終了まで残します
	if( module_) {
		moduleClosed = false ;
		const auto module = std::exchange( module_, nullptr) ; // 再試行しない一参照
		// free成立時だけmodule参照を解放します
		if( closeOk_) {
			try {
				moduleClosed = services_.close( module) ;
			} catch( ...) {
				moduleClosed = false ;
			}
		}
		closeOk_ = closeOk_ && moduleClosed.value() ;
		// productionだけは以後の適格fast要求をload前に拒否し、再起動を必要とします
		if( !closeOk_ && services_.enforceProcessLifetime) {
			cleanupFailed.store( true, std::memory_order_relaxed) ;
		}
	}
	lease_.reset() ; // context/free→module/closeの後にだけleaseを解放
	// cleanupの実観測を本文なしで保持します
	if( diagnostics_) {
		diagnostics_->whisperClosed( freeStatus, moduleClosed) ;
	}
	return closeOk_ ;
}
//------------------------------------------------------------------------------
/**
 * @brief context、module、leaseの順に明示解放します
 * @throws OperationError free/close失敗。再freeせずmodule参照をprocess終了まで残します
 * @see koefude::whisper_cpu::Runtime::close
 */
//------------------------------------------------------------------------------
void Runtime::close()
{
	// free/close失敗を正常認識returnへ昇格しません
	if( !closeOnce()) {
		throw OperationError( ErrorKind::INFERENCE_FAILED, 0x57430006) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 既存静的Whisperを同じ型の表で呼びます
 * @return process寿命の表
 * @see koefude::whisper_cpu::portableApi
 */
//------------------------------------------------------------------------------
const ApiV1 &portableApi() noexcept
{
	// static表も同じ14箇所だけを使い、共有loopと設定列を複製しません
	static const ApiV1 api =
		//------------------------------------------------------------------------------
		/**
		 * @brief host静的Whisperへの14 wrapperをApiV1の値として一度組み立てます
		 * @return 関数pointerを設定したApiV1の値。metadataは零で、借用table自体は返しません
		 * @note 返した値を関数localのstatic apiがprocess寿命で保持します。上流の呼出しは行いません
		 */
		//------------------------------------------------------------------------------
		[]
		{
			ApiV1 value{} ; // portable表はmodule metadata検証の対象外
			value.logSet =
				//------------------------------------------------------------------------------
				/**
				 * @brief host静的Whisper/ggmlのglobal log callbackを置き換えます
				 * @param[in] cb 非送出callback。nullは上流既定へ戻します。return後も登録が残ります
				 * @param[in] data cbへ渡す借用pointer。null可。所有権を渡さず、return後も登録が残ります
				 * @return 登録が戻ればStatus::OK、std::bad_allocはOUT_OF_MEMORY、その他のC++例外はENGINE_FAILURE
				 * @note cb/dataは置換と既存callback終了まで、又は利用終了まで生存させます。登録と出力の排他はcaller責務です
				 * @note 失敗時の登録状態を巻き戻しません
				 */
				//------------------------------------------------------------------------------
				[]( ggml_log_callback cb, void *data) noexcept
				{
					return guarded(
						//------------------------------------------------------------------------------
						/**
						 * @brief captureしたcb/dataをhostのWhisper/ggml globalへ登録します
						 * @note capture変数の参照はguarded終了まで借用します。登録するcallback/dataの参照先は登録呼出し終了後も必要です
						 */
						//------------------------------------------------------------------------------
						[&]
						{
							whisper_log_set( cb, data) ;
						}) ;
				} ;
			value.contextDefaultParams =
				//------------------------------------------------------------------------------
				/**
				 * @brief host静的Whisperのcontext既定設定を値copyします
				 * @param[out] out 非nullの書込先。成功時にwhisper_context_paramsをcopyし、解放APIは不要です
				 * @return copy完了はStatus::OK、std::bad_allocはOUT_OF_MEMORY、その他のC++例外はENGINE_FAILURE
				 * @note null検査/入口初期化はせず、例外時は以前の出力が残り得ます。既定DTW headsはnullです
				 */
				//------------------------------------------------------------------------------
				[]( whisper_context_params *out) noexcept
				{
					return guarded(
						//------------------------------------------------------------------------------
						/**
						 * @brief captureしたoutへcontext既定structを値copyします
						 * @note out変数と非null書込先をguarded終了まで借用します。初期化やnull検査は追加しません
						 */
						//------------------------------------------------------------------------------
						[&]
						{
							*out = whisper_context_default_params() ;
						}) ;
				} ;
			value.initFromFile =
				//------------------------------------------------------------------------------
				/**
				 * @brief host静的Whisperでモデルを同期読込みしcontextを生成します
				 * @param[in] path 非nullのUTF-8 NUL終端pathを同期借用します。空文字は上流file openに委ねます
				 * @param[in] params 非nullの設定structを同期借用し、上流へ値copyします
				 * @param[out] out 非nullの書込先。正常return時に上流のcontext又はnullをcopyします
				 * @return 上流が戻ればStatus::OKで、*out==nullもあり得ます。std::bad_allocはOUT_OF_MEMORY、その他はENGINE_FAILURE
				 * @note pointer検査/入口null化はせず、例外時は以前の出力が残り得ます。callerがstatusと非null生成を別々に確認します
				 * @note pathは上流が複製し、設定内のcustom DTW heads等はcontextの最終利用まで有効に保ちます
				 * @note 生成contextはこの表のfreeContextで一回解放し、同contextの操作を並行させません
				 */
				//------------------------------------------------------------------------------
				[]( const char *path, const whisper_context_params *params, whisper_context **out) noexcept
				{
					return guarded(
						//------------------------------------------------------------------------------
						/**
						 * @brief captureしたpath/paramsで同期生成し、上流pointerをoutへそのままcopyします
						 * @note capture変数/path/struct/書込先はguarded終了まで借用します。設定内参照先の寿命はouter契約に従います
						 */
						//------------------------------------------------------------------------------
						[&]
						{
							*out = whisper_init_from_file_with_params( path, *params) ;
						}) ;
				} ;
			value.freeContext =
				//------------------------------------------------------------------------------
				/**
				 * @brief host静的Whisperが生成したcontextを一回解放します
				 * @param[in] context 同表の所有pointer。nullは何も解放せず、解放済み/他moduleのpointerは禁止です
				 * @return 解放呼出し完了はStatus::OK、std::bad_allocはOUT_OF_MEMORY、その他のC++例外はENGINE_FAILURE
				 * @note 非OK後は解放完了が不確定なので再freeしません。他のcontext操作と並行させず、巻戻しは行いません
				 */
				//------------------------------------------------------------------------------
				[]( whisper_context *context) noexcept
				{
					return guarded(
						//------------------------------------------------------------------------------
						/**
						 * @brief captureしたcontextを生成元の静的Whisperで解放します
						 * @note pointer変数をguarded終了まで借用します。指す所有資源は解放され、例外時にも再freeしません
						 */
						//------------------------------------------------------------------------------
						[&]
						{
							whisper_free( context) ;
						}) ;
				} ;
			value.fullDefaultParams =
				//------------------------------------------------------------------------------
				/**
				 * @brief host静的Whisperの探索方式別既定設定を値copyします
				 * @param[in] strategy GREEDY又はBEAM_SEARCHをcallerが指定します。wrapperはenum値を検査しません
				 * @param[out] out 非nullの書込先。成功時はwhisper_full_paramsの値で、既定languageは静的借用文字列です
				 * @return copy完了はStatus::OK、std::bad_allocはOUT_OF_MEMORY、その他のC++例外はENGINE_FAILURE
				 * @note null検査/入口初期化はせず、例外時は以前の出力が残り得ます。structや静的文字列をfreeしません
				 */
				//------------------------------------------------------------------------------
				[]( whisper_sampling_strategy strategy, whisper_full_params *out) noexcept
				{
					return guarded(
						//------------------------------------------------------------------------------
						/**
						 * @brief captureしたstrategyの推論既定structをoutへ値copyします
						 * @note strategy/out変数と非null書込先をguarded終了まで借用し、enum検査や零化は行いません
						 */
						//------------------------------------------------------------------------------
						[&]
						{
							*out = whisper_full_default_params( strategy) ;
						}) ;
				} ;
			value.full =
				//------------------------------------------------------------------------------
				/**
				 * @brief host静的Whisperで一窓のPCMを同期推論し、元のnative符号を返します
				 * @param[in,out] context 非nullでこの表が生成したcontext。既存結果と推論状態を変更します
				 * @param[in] params 非nullの設定structを同期借用します。内部配列/文字列/callback/dataも使用終了まで有効に保ちます
				 * @param[in] samples 非nullでcount要素の16kHz mono float PCMを同期借用します
				 * @param[in] count 0以上のintで表すsample数。0は空PCMで、byte数/秒数ではありません
				 * @param[out] code 非nullの書込先。上流が戻ればwhisper_fullの符号付きintをそのままcopyします
				 * @return 上流の正常returnはStatus::OKで、code非0もあり得ます。std::bad_allocはOUT_OF_MEMORY、その他はENGINE_FAILURE
				 * @note pointer/count検査/出力初期化はしません。例外時に旧codeが残り得るため非OKの出力を使いません
				 * @note callbackは例外を外へ出さず、PCM/設定を処理中に変更せず、同contextの操作を並行させません
				 * @note Status::OKとcode==0を別々に確認します。失敗時の推論状態を巻き戻しません
				 */
				//------------------------------------------------------------------------------
				[]( whisper_context *context, const whisper_full_params *params, const float *samples, int count,
			   int *code) noexcept
				{
					return guarded(
						//------------------------------------------------------------------------------
						/**
						 * @brief captureしたcontext/設定/PCMで推論し、元のnative符号をcodeへcopyします
						 * @note capture変数と非null参照先はguarded終了まで借用します。callbackも同期処理終了まで有効に保ちます
						 */
						//------------------------------------------------------------------------------
						[&]
						{
							*code = whisper_full( context, *params, samples, count) ;
						}) ;
				} ;
			value.nSegments =
				//------------------------------------------------------------------------------
				/**
				 * @brief host静的Whisperの元のsegment件数を取得します
				 * @param[in] ctx 非nullでこの表が生成した生存中contextを借用します
				 * @param[out] out 非nullの書込先。上流の取得完了時に元segment件数をcopyします。0は結果なしで、認識成功自体を保証しません
				 * @return 取得完了はStatus::OK、std::bad_allocはOUT_OF_MEMORY、その他のC++例外はENGINE_FAILURE
				 * @note pointer/index検査や入口初期化はしません。例外時は旧出力が残り得るため、非OKの出力を使いません
				 * @note 同じcontextの推論・取得・解放を並行させません
				 */
				//------------------------------------------------------------------------------
				[]( whisper_context *ctx, int *out) noexcept
				{
					return guarded(
						//------------------------------------------------------------------------------
						/**
						 * @brief captureしたctxから元の取得値をoutへcopyします
						 * @note capture変数/context/非null書込先はguarded終了まで借用します。初期化や範囲検査は行いません
						 */
						//------------------------------------------------------------------------------
						[&]
						{
							*out = whisper_full_n_segments( ctx) ;
						}) ;
				} ;
			value.segmentText =
				//------------------------------------------------------------------------------
				/**
				 * @brief host静的Whisperのsegment本文を変更せず借用します
				 * @param[in] ctx 非nullでこの表が生成した生存中contextを借用します
				 * @param[in] index 0以上かつ取得済みsegment件数未満の番号。callerが範囲を保証します
				 * @param[out] out 非nullの書込先。上流の取得完了時にNUL終端本文byte列を借用します。空も保持します
				 * @return 取得完了はStatus::OK、std::bad_allocはOUT_OF_MEMORY、その他のC++例外はENGINE_FAILURE
				 * @note pointer/index検査や入口初期化はしません。例外時は旧出力が残り得るため、非OKの出力を使いません
				 * @note 本文は次のcontext変更API又はfreeまで借用し、hostでfreeしません。保持する場合は期限前にcopyします
				 * @note 本文はUTF-8を想定しますが、このgetterはencodingを検証しません
				 * @note 同じcontextの推論・取得・解放を並行させません
				 */
				//------------------------------------------------------------------------------
				[]( whisper_context *ctx, int index, const char **out) noexcept
				{
					return guarded(
						//------------------------------------------------------------------------------
						/**
						 * @brief captureしたctx/indexから借用文字列pointerをoutへcopyします
						 * @note capture変数/context/非null書込先はguarded終了まで借用します。本文の参照期限はouter契約に従います
						 */
						//------------------------------------------------------------------------------
						[&]
						{
							*out = whisper_full_get_segment_text( ctx, index) ;
						}) ;
				} ;
			value.segmentT0 =
				//------------------------------------------------------------------------------
				/**
				 * @brief host静的Whisperのsegmentの元開始時刻を取得します
				 * @param[in] ctx 非nullでこの表が生成した生存中contextを借用します
				 * @param[in] index 0以上かつ取得済みsegment件数未満の番号。callerが範囲を保証します
				 * @param[out] out 非nullの書込先。上流の取得完了時に開始時刻をcentisecond（1/100秒）でcopyします。窓の元値へfile原点を加算/補正しません
				 * @return 取得完了はStatus::OK、std::bad_allocはOUT_OF_MEMORY、その他のC++例外はENGINE_FAILURE
				 * @note pointer/index検査や入口初期化はしません。例外時は旧出力が残り得るため、非OKの出力を使いません
				 * @note 同じcontextの推論・取得・解放を並行させません
				 */
				//------------------------------------------------------------------------------
				[]( whisper_context *ctx, int index, std::int64_t *out) noexcept
				{
					return guarded(
						//------------------------------------------------------------------------------
						/**
						 * @brief captureしたctx/indexから元の取得値をoutへcopyします
						 * @note capture変数/context/非null書込先はguarded終了まで借用します。初期化や範囲検査は行いません
						 */
						//------------------------------------------------------------------------------
						[&]
						{
							*out = whisper_full_get_segment_t0( ctx, index) ;
						}) ;
				} ;
			value.segmentT1 =
				//------------------------------------------------------------------------------
				/**
				 * @brief host静的Whisperのsegmentの元終了時刻を取得します
				 * @param[in] ctx 非nullでこの表が生成した生存中contextを借用します
				 * @param[in] index 0以上かつ取得済みsegment件数未満の番号。callerが範囲を保証します
				 * @param[out] out 非nullの書込先。上流の取得完了時に終了時刻をcentisecond（1/100秒）でcopyします。窓の元値へfile原点を加算/補正しません
				 * @return 取得完了はStatus::OK、std::bad_allocはOUT_OF_MEMORY、その他のC++例外はENGINE_FAILURE
				 * @note pointer/index検査や入口初期化はしません。例外時は旧出力が残り得るため、非OKの出力を使いません
				 * @note 同じcontextの推論・取得・解放を並行させません
				 */
				//------------------------------------------------------------------------------
				[]( whisper_context *ctx, int index, std::int64_t *out) noexcept
				{
					return guarded(
						//------------------------------------------------------------------------------
						/**
						 * @brief captureしたctx/indexから元の取得値をoutへcopyします
						 * @note capture変数/context/非null書込先はguarded終了まで借用します。初期化や範囲検査は行いません
						 */
						//------------------------------------------------------------------------------
						[&]
						{
							*out = whisper_full_get_segment_t1( ctx, index) ;
						}) ;
				} ;
			value.nTokens =
				//------------------------------------------------------------------------------
				/**
				 * @brief host静的Whisperのsegment内の元token件数を取得します
				 * @param[in] ctx 非nullでこの表が生成した生存中contextを借用します
				 * @param[in] index 0以上かつ取得済みsegment件数未満の番号。callerが範囲を保証します
				 * @param[out] out 非nullの書込先。上流の取得完了時に元token件数をcopyします。0はtokenなしです
				 * @return 取得完了はStatus::OK、std::bad_allocはOUT_OF_MEMORY、その他のC++例外はENGINE_FAILURE
				 * @note pointer/index検査や入口初期化はしません。例外時は旧出力が残り得るため、非OKの出力を使いません
				 * @note 同じcontextの推論・取得・解放を並行させません
				 */
				//------------------------------------------------------------------------------
				[]( whisper_context *ctx, int index, int *out) noexcept
				{
					return guarded(
						//------------------------------------------------------------------------------
						/**
						 * @brief captureしたctx/indexから元の取得値をoutへcopyします
						 * @note capture変数/context/非null書込先はguarded終了まで借用します。初期化や範囲検査は行いません
						 */
						//------------------------------------------------------------------------------
						[&]
						{
							*out = whisper_full_n_tokens( ctx, index) ;
						}) ;
				} ;
			value.tokenData =
				//------------------------------------------------------------------------------
				/**
				 * @brief host静的Whisperの元tokenの固定型を値copyします
				 * @param[in] ctx 非nullでこの表が生成した生存中contextを借用します
				 * @param[in] row 0以上かつ取得済みsegment件数未満の番号。callerが範囲を保証します
				 * @param[in] token 0以上かつ当該segmentのtoken件数未満の番号。callerが範囲を保証します
				 * @param[out] out 非nullの書込先。上流の取得完了時にwhisper_token_dataを値copyします。時刻はcentisecondの元値/未取得sentinelを保持します
				 * @return 取得完了はStatus::OK、std::bad_allocはOUT_OF_MEMORY、その他のC++例外はENGINE_FAILURE
				 * @note pointer/index検査や入口初期化はしません。例外時は旧出力が残り得るため、非OKの出力を使いません
				 * @note 同じcontextの推論・取得・解放を並行させません
				 */
				//------------------------------------------------------------------------------
				[]( whisper_context *ctx, int row, int token, whisper_token_data *out) noexcept
				{
					return guarded(
						//------------------------------------------------------------------------------
						/**
						 * @brief captureしたctx/row/tokenから元の取得値をoutへcopyします
						 * @note capture変数/context/非null書込先はguarded終了まで借用します。初期化や範囲検査は行いません
						 */
						//------------------------------------------------------------------------------
						[&]
						{
							*out = whisper_full_get_token_data( ctx, row, token) ;
						}) ;
				} ;
			value.tokenText =
				//------------------------------------------------------------------------------
				/**
				 * @brief host静的Whisperの元token本文を変更せず借用します
				 * @param[in] ctx 非nullでこの表が生成した生存中contextを借用します
				 * @param[in] row 0以上かつ取得済みsegment件数未満の番号。callerが範囲を保証します
				 * @param[in] token 0以上かつ当該segmentのtoken件数未満の番号。callerが範囲を保証します
				 * @param[out] out 非nullの書込先。上流の取得完了時に元語彙のNUL終端byte列を借用します。空/特別tokenも保持します
				 * @return 取得完了はStatus::OK、std::bad_allocはOUT_OF_MEMORY、その他のC++例外はENGINE_FAILURE
				 * @note pointer/index検査や入口初期化はしません。例外時は旧出力が残り得るため、非OKの出力を使いません
				 * @note 本文は次のcontext変更API又はfreeまで借用し、hostでfreeしません。保持する場合は期限前にcopyします
				 * @note 単独tokenのUTF-8完全性は保証しません。語彙byte列を検証/変換せず返します
				 * @note 同じcontextの推論・取得・解放を並行させません
				 */
				//------------------------------------------------------------------------------
				[]( whisper_context *ctx, int row, int token, const char **out) noexcept
				{
					return guarded(
						//------------------------------------------------------------------------------
						/**
						 * @brief captureしたctx/row/tokenから借用文字列pointerをoutへcopyします
						 * @note capture変数/context/非null書込先はguarded終了まで借用します。本文の参照期限はouter契約に従います
						 */
						//------------------------------------------------------------------------------
						[&]
						{
							*out = whisper_full_get_token_text( ctx, row, token) ;
						}) ;
				} ;
			value.tokenEot =
				//------------------------------------------------------------------------------
				/**
				 * @brief host静的Whisperのcontext語彙のEOT token IDを取得します
				 * @param[in] ctx 非nullでこの表が生成した生存中contextを借用します
				 * @param[out] out 非nullの書込先。上流の取得完了時に語彙のEOT token IDをcopyします。認識件数や完了statusではありません
				 * @return 取得完了はStatus::OK、std::bad_allocはOUT_OF_MEMORY、その他のC++例外はENGINE_FAILURE
				 * @note pointer/index検査や入口初期化はしません。例外時は旧出力が残り得るため、非OKの出力を使いません
				 * @note 同じcontextの推論・取得・解放を並行させません
				 */
				//------------------------------------------------------------------------------
				[]( whisper_context *ctx, whisper_token *out) noexcept
				{
					return guarded(
						//------------------------------------------------------------------------------
						/**
						 * @brief captureしたctxから元の取得値をoutへcopyします
						 * @note capture変数/context/非null書込先はguarded終了まで借用します。初期化や範囲検査は行いません
						 */
						//------------------------------------------------------------------------------
						[&]
						{
							*out = whisper_token_eot( ctx) ;
						}) ;
				} ;
			return value ;
		}() ;
	return api ;
}
} // namespace koefude::whisper_cpu
