//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file cuda_device.cpp
 * @brief CUDA device 0のdriverとDXGI予算を安全に照合します
 * @version 1.0.0
 * @date 2026/09/11
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/11 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/11 固定driver APIによる同一GPU照合と予算不足の拒否を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 宣言の役割を明記
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
#include "inference/cuda_device.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <memory>
#include <string_view>

#include <Windows.h>

#if KOEFUDE_HAS_CUDA
#include <cuda.h>
#include <dxgi1_4.h>
#include <onnxruntime_cxx_api.h>
#include <wrl/client.h>
#endif

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief 同一deviceの観測値からoverflowしないGPU予算を作ります
 * @param[in] physical DXGIの専用物理容量（byte）
 * @param[in] cudaTotal CUDAが報告する総量（byte）
 * @param[in] cudaFree CUDAが報告する他process込みの空き（byte）
 * @param[in] adapterBudget DXGIがこのprocessへ提示する予算（byte）
 * @param[in] currentUsage DXGIが報告するこのprocessの現在使用量（byte）
 * @return 不明・矛盾値はknown=false。予算超過時はavailable=0
 * @see koefude::combineCudaBudget
 */
//------------------------------------------------------------------------------
GpuBudget combineCudaBudget( const std::uint64_t physical, const std::uint64_t cudaTotal, const std::uint64_t cudaFree,
							const std::uint64_t adapterBudget, const std::uint64_t currentUsage) noexcept
{
	// 不明な容量や総量を超える空きを、利用可能なGPUとして扱わない
	if( physical == 0 || cudaTotal == 0 || adapterBudget == 0 || cudaFree > cudaTotal) {
		return {} ;
	}
	// 差引き済みのprocess予算。使用量超過は0へ飽和させる
	const auto remaining = adapterBudget > currentUsage ? adapterBudget - currentUsage : 0 ;
	return {true, physical, std::min( {12 * K_GI_B, physical, cudaTotal, cudaFree, remaining})} ;
}

//------------------------------------------------------------------------------
/**
 * @brief 装置方針と検証済みprofileを、1GiBの余裕を残した予算へ照合します
 * @param[in] device 同一adapterの取得結果。provider不足時は予算不明
 * @param[in] backend ジョブ受付時に固定した実行方針
 * @param[in] required profileで確保する保守的な必要量（byte）
 * @param[in] validatedProfile Auto選択に合格したprofileならtrue
 * @return 明示CUDAまたは検証済みAutoで予算が足りる場合true
 * @see koefude::canUseCuda
 */
//------------------------------------------------------------------------------
bool canUseCuda( const CudaDeviceInfo &device, const Backend backend, const std::uint64_t required,
				const bool validatedProfile) noexcept
{
	return (backend == Backend::CUDA || (backend == Backend::AUTO && validatedProfile && device.validatedAdapter)) &&
		   fitsGpuBudget( device.budget, required, K_GI_B) ;
}

#if KOEFUDE_HAS_CUDA
namespace {

/** @brief System32から読み込んだdriver moduleを解放します */
struct ModuleDeleter {
	//------------------------------------------------------------------------------
	/**
	 * @brief 最後の借用API呼出し後にDLL参照を解放します
	 * @param[in] module 所有する有効なmodule handle
	 */
	//------------------------------------------------------------------------------
	void operator()( HINSTANCE__ *module) const noexcept
	{
		FreeLibrary( module) ;
	}
} ;

//------------------------------------------------------------------------------
/**
 * @brief 固定CUDA headerの型でSystem32 driverのexportを取得します
 * @tparam Function cuda.hが宣言する関数pointer型
 * @param[in] module 呼出しから返ったpointerの使用終了まで所有するDLL。NULL不可
 * @param[in] name 固定したASCII export名。外部入力は渡しません
 * @return export不足ならNULL。moduleの寿命内だけ有効な非所有pointer
 */
//------------------------------------------------------------------------------
template <typename Function> [[nodiscard]] Function cudaSymbol( const HMODULE module, const char *name) noexcept
{
	// Windows API境界だけで同sizeの関数pointer表現を変換する
	return std::bit_cast<Function>( GetProcAddress( module, name)) ;
}

/** @brief primary contextの保持とworkerのcurrent context復元を単独所有します */
class CudaContext {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief device 0のprimary contextを保持してworkerのstackへ積みます
	 * @param[in] device 初期化済みCUDA device 0
	 * @param[in] retain 所有moduleの寿命中有効な保持API。NULL不可
	 * @param[in] release 所有moduleの寿命中有効な解放API。NULL不可
	 * @param[in] push 所有moduleの寿命中有効なstack追加API。NULL不可
	 * @param[in] pop 所有moduleの寿命中有効なstack復元API。NULL不可
	 */
	//------------------------------------------------------------------------------
	CudaContext( const CUdevice device, decltype(&cuDevicePrimaryCtxRetain) retain,
				decltype(&cuDevicePrimaryCtxRelease) release, decltype(&cuCtxPushCurrent) push,
				decltype(&cuCtxPopCurrent) pop) noexcept
		: device_( device), release_( release), pop_( pop)
	{
		// 保持の失敗時に未所有のcontextをstackへ積まない
		if( retain( &context_, device_) == CUDA_SUCCESS) {
			pushed_ = push( context_) == CUDA_SUCCESS ;
		}
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 保持したcontextをstackから外し、参照を解放します
	 */
	//------------------------------------------------------------------------------
	~CudaContext()
	{
		static_cast<void>( close()) ; // 早期return経路も同じ所有権の解放を行う
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief driver参照とthread状態の二重所有を禁止します
	 * @note 名前のない第1引数（const CudaContext &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	CudaContext( const CudaContext &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief driver参照とthread状態の二重所有を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const CudaContext &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	CudaContext		  &operator=( const CudaContext &) = delete ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 予算照会可能なcurrent contextがあるか返します
	 * @return 保持とstack追加が成功した場合true
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool ready() const noexcept
	{
		return pushed_ ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief workerのstackを戻して所有参照を解放します
	 * @return stack復元・所有参照解放が全て成功した場合true
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool close() noexcept
	{
		bool success = true ; // 解放APIの異常も照会不成立として返す
		// 自分が積んだcontextだけを取り出し、既存current contextを復元する
		if( pushed_) {
			CUcontext removed = nullptr ; // 取り出した非所有context
			success = pop_( &removed) == CUDA_SUCCESS && removed == context_ ;
			pushed_ = false ;
		}
		// primary contextの参照を、保持の成功回数と一致させて解放する
		if( context_) {
			success = release_( device_) == CUDA_SUCCESS && success ;
			context_ = nullptr ;
		}
		return success ;
	}

	private:
	/// 保持した参照が属するdevice 0
	CUdevice							 device_ ;
	/// moduleに従属する解放API
	decltype(&cuDevicePrimaryCtxRelease) release_ ;
	/// moduleに従属するstack復元API
	decltype(&cuCtxPopCurrent)			 pop_ ;
	/// primary contextの所有参照
	CUcontext							 context_ = nullptr ;
	/// workerのstackへ追加した場合だけtrue
	bool								 pushed_ = false ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 固定ONNX RuntimeにCUDA providerが実装されているか確認します
 * @return API不足・列挙失敗・CUDA不足ではfalse
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool cudaProviderAvailable()
{
	const auto &api = Ort::GetApi() ; // 既にlink済みの固定runtimeのC API
	char	  **providers = nullptr ; // APIが確保し、同じAPIで解放する一覧
	int			count = 0 ; // APIが返す一覧の件数
	OrtStatus  *status = api.GetAvailableProviders( &providers, &count) ; // 失敗時だけ所有する状態
	// runtime照会の異常をGPU利用成功に置き換えない
	if( status) {
		api.ReleaseStatus( status) ;
		return false ;
	}
	bool found = false ; // CUDA providerとの完全一致がある場合だけ有効
	// 想定外の件数やNULL配列は走査せず、所有一覧を解放する
	if( providers && count >= 0 && count <= 64) {
		// 有界な全providerからCUDAの実装を探す
		for( int index = 0; index < count; ++index) { // 現在のprovider番号
			found = found || (providers[index] && std::strcmp( providers[index], "CUDAExecutionProvider") == 0) ;
		}
	}
	status = api.ReleaseAvailableProviders( providers, count) ;
	// 解放の異常も予算不明へ変換し、GPU実行を許可しない
	if( status) {
		api.ReleaseStatus( status) ;
		return false ;
	}
	return found ;
}

} // namespace
#endif

//------------------------------------------------------------------------------
/**
 * @brief CUDA device 0のLUIDをDXGIへ照合して実行前の空きを取得します
 * @return CUDA provider/driver/API/照合の不足時はbudget.known=false
 * @note worker専用の同期照会です。保持したprimary contextは戻る前に解放します
 * 以後の割当てを制限するhard limitではありません
 * @see koefude::queryCudaDevice
 */
//------------------------------------------------------------------------------
CudaDeviceInfo queryCudaDevice()
{
#if KOEFUDE_HAS_CUDA
    // CPU版runtimeの暗黙fallbackをGPU成功と記録しない
	if( !cudaProviderAvailable()) {
		return {} ;
	}
	const std::unique_ptr<HINSTANCE__, ModuleDeleter> driver(
		LoadLibraryExW( L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) ; // OSのdriverだけ
	// driverなしのPCでは例外や任意DLL検索を行わずCPUの選択へ戻る
	if( !driver) {
		return {} ;
	}
	const auto initialize = cudaSymbol<decltype(&cuInit)>( driver.get(), "cuInit") ; // 初期化
	// device ordinalの照会
	const auto getDevice = cudaSymbol<decltype(&cuDeviceGet)>( driver.get(), "cuDeviceGet") ;
	const auto getLuid = cudaSymbol<decltype(&cuDeviceGetLuid)>( driver.get(), "cuDeviceGetLuid") ;
	// DXGIと照合するWindows LUID
	const auto retain = cudaSymbol<decltype(&cuDevicePrimaryCtxRetain)>(
		driver.get(), "cuDevicePrimaryCtxRetain") ; // primary context保持
	const auto release = cudaSymbol<decltype(&cuDevicePrimaryCtxRelease)>(
		driver.get(), "cuDevicePrimaryCtxRelease_v2") ; // primary context解放
	// workerのcontext追加
	const auto push = cudaSymbol<decltype(&cuCtxPushCurrent)>( driver.get(), "cuCtxPushCurrent_v2") ;
	const auto pop = cudaSymbol<decltype(&cuCtxPopCurrent)>( driver.get(), "cuCtxPopCurrent_v2") ;
	// workerのcontext復元
	const auto memory = cudaSymbol<decltype(&cuMemGetInfo)>( driver.get(), "cuMemGetInfo_v2") ;
	// 他process込みの実際の空き
	// 全ての必須APIを取得してからdriverを使用する
	if( !initialize || !getDevice || !getLuid || !retain || !release || !push || !pop || !memory ||
	   initialize( 0) != CUDA_SUCCESS) {
		return {} ;
	}
	CUdevice					   device = 0 ; // sherpa-onnxの既定CUDA providerと同じordinal
	std::array<char, sizeof(LUID)> luidBytes{} ; // CUDAが書き込むLUIDのbyte表現
	unsigned int				   nodeMask = 0 ; // Windowsのnode mask。複数nodeは未検証として拒否
	// ordinal 0を照合できない場合、別adapterの空きで許可しない
	if( getDevice( &device, 0) != CUDA_SUCCESS || getLuid( luidBytes.data(), &nodeMask, device) != CUDA_SUCCESS ||
	   nodeMask != 1) {
		return {} ;
	}
	LUID luid{} ; // DXGIの型へcopyした同一adapter識別子。UI/logへ渡さない
	std::memcpy( &luid, luidBytes.data(), sizeof(luid)) ;
	Microsoft::WRL::ComPtr<IDXGIFactory4> factory ; // 同期照会内だけで所有するfactory
	Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter ; // LUIDが一致したadapter
	DXGI_ADAPTER_DESC1					  description{} ; // 専用VRAM容量と検証済み機種の照合用属性
	// 照合不能・software adapter・専用容量なしをfail-closedで拒否する
	if( FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
	   FAILED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter))) || FAILED(adapter->GetDesc1(&description)) ||
	   (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 || description.VendorId != 0x10de ||
	   description.DedicatedVideoMemory == 0) {
		return {} ;
	}
	CudaContext context( device, retain, release, push, pop) ; // 同じdeviceの照会用参照
	// context初期化に失敗した場合、空き容量を推測しない
	if( !context.ready()) {
		return {} ;
	}
	std::size_t					 freeBytes = 0 ; // CUDAが報告する利用可能量
	std::size_t					 totalBytes = 0 ; // CUDAが報告する総量
	DXGI_QUERY_VIDEO_MEMORY_INFO adapterMemory{} ; // processごとの予算と現在使用量
	// CUDAの空き量と同一adapterのDXGI予算を両方取得できたか
	const bool					 observed = memory( &freeBytes, &totalBytes) == CUDA_SUCCESS &&
						  SUCCEEDED(adapter->QueryVideoMemoryInfo(
                              0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &adapterMemory)) ;
	// 二つの照会とcontext復元の全てが成功した場合だけ予算を返す
	if( !context.close() || !observed) {
		return {} ;
	}
	// NUL終端の範囲内だけを参照する機種名の長さ
	const auto nameLength = wcsnlen_s( description.Description, std::size( description.Description)) ;
	return {combineCudaBudget( description.DedicatedVideoMemory, totalBytes, freeBytes, adapterMemory.Budget,
							  adapterMemory.CurrentUsage),
			std::wstring_view( description.Description, nameLength) == L"NVIDIA GeForce RTX 3080 Ti"} ;
#else
    return {} ; // CPU buildではCUDA driverの導入や初期化を要求しない
#endif
}

} // namespace koefude
