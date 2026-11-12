//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whisper_cpu_abi.h
 * @brief 固定Whisper header専用の非送出CPU関数表を定義します
 * @version 1.0.0
 * @date 2026/10/21
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/21 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1301の単一module所有権と例外境界を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1417のnative状態・出力・所有寿命を実装へ照合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include <cstdint>
#include <type_traits>
#include <whisper.h>

namespace koefude::whisper_cpu {
// 異版との互換を提供しない私有ABIです
inline constexpr std::uint32_t K_ABI_VERSION = 1 ;
// 既存baseline CPUID契約の15bitです
inline constexpr std::uint32_t K_REQUIRED_FEATURES = 32767 ;
// XMM/YMMのOS保存が必要です
inline constexpr std::uint32_t K_XCR0_MASK = 6 ;
// 固定upstream commitです
inline constexpr char		   K_ENGINE_COMMIT[] = "371b5a7561823ab2bb32142d2751e35e7534727b" ;
// upstream型の値ABIを固定するheader digestです
inline constexpr char K_WHISPER_HEADER_SHA256[] = "43a82a692dccfa8b42cb3f1b5c80ee9d468c2b093a135ad27c11ffd760e07f2e" ;
// 固定ggml型のheader digestです
inline constexpr char K_GGML_HEADER_SHA256[] = "c8e29a587d77a25b859c3653ed4e2766090da2aa2b9df565373e24b02cc61376" ;
// 固定CPU backend headerのdigestです
inline constexpr char K_GGML_CPU_HEADER_SHA256[] = "696a54474002227a7f73b967b643055d6829853464244a8d3f4fe2e7d0b85abe" ;
// receiptと一致させるCPU構成です
inline constexpr char K_CPU_CONTRACT[] = "static-crt-avx2-repack-cpu-no-native-no-openmp-no-gpu-v1" ;

/** @brief module外へ例外objectを渡さない固定幅statusです */
enum class Status : std::uint32_t {
	/// 例外境界の操作が完了しました。native符号や生成pointerの確認は別に必要です
	OK = 0,
	/// DLL内のstd::bad_allocです
	OUT_OF_MEMORY = 1,
	/// その他のC++例外、又はDLL facadeの初期化NULLです
	ENGINE_FAILURE = 2,
	/// 必須pointer又は値が不正です
	INVALID_ARGUMENT = 3,
	/// 要求した版又はtable sizeが不一致です
	ABI_MISMATCH = 4
} ;

/**
 * @brief 同じbuildの同期workerが使う14関数と固定ABI metadataです
 * @note statusを先に検査します。fullはStatus::OKに加えnativeCode==0、initは非null生成も確認します
 * @note DLL facadeは非null出力を入口で零/nullにし、必須pointer/負index/探索enumを限定検査します
 * @note portable表はこれらを検査/初期化せず、初期化OK+NULLも返し得ます。非OK出力を使いません
 * @note contextは生成した表だけで一回解放します。free非OKは解放不確定で再freeせず、DLLもunloadしません
 * @note 全操作は同期です。同contextの推論/取得/解放、global log登録/利用の排他はcaller責務です
 * @note params structは呼出し中借用して値copyします。内部pointerの参照先は別に必要な寿命を保持します
 * @note full内のcallback/dataは同期利用終了まで有効に保ち、callbackから例外を外へ出しません
 * @note logSetはcallback/dataをglobalへ保持します。置換と実行中callback終了まで、又は利用終了まで有効に保ちます
 * @note textは次のcontext変更API又はfreeまで借用しhostでfreeしません。token data/設定structは値copyです
 * @note pointerは信頼済み同一build内の値です。context実在/同module所有とindex上限はcallerが保証します
 * @note out/nativeCodeは独立した書込領域とし、入力struct/PCM/context内部へ重ねません
 */
struct ApiV1 {
	/// K_ABI_VERSION
	std::uint32_t abiVersion ;
	/// sizeof(ApiV1)
	std::uint32_t tableBytes ;
	/// baseline CPU契約
	std::uint32_t requiredFeatures ;
	/// OSのXMM/YMM保存bit
	std::uint32_t xcr0Mask ;
	/// 固定compiler ABI
	std::uint32_t mscFullVer ;
	/// x64 pointer幅
	std::uint32_t pointerBytes ;
	/// sizeof(whisper_context_params)
	std::uint32_t contextParamsBytes ;
	/// alignof(whisper_context_params)
	std::uint32_t contextParamsAlignment ;
	/// sizeof(whisper_full_params)
	std::uint32_t fullParamsBytes ;
	/// alignof(whisper_full_params)
	std::uint32_t fullParamsAlignment ;
	/// sizeof(whisper_token_data)
	std::uint32_t tokenDataBytes ;
	/// alignof(whisper_token_data)
	std::uint32_t tokenDataAlignment ;
	/// Debug又はRelease、NUL終端
	char		  configuration[8] ;
	/// 固定commit、NUL終端
	char		  engineCommit[41] ;
	/// whisper.h、NUL終端
	char		  whisperHeaderSha256[65] ;
	/// ggml.h、NUL終端
	char		  ggmlHeaderSha256[65] ;
	/// ggml-cpu.h、NUL終端
	char		  ggmlCpuHeaderSha256[65] ;
	/// このheader、NUL終端
	char		  abiHeaderSha256[65] ;
	/// 固定構成、NUL終端
	char		  cpuContract[80] ;

	/// Whisper/ggml globalへcallback/dataを保持します。callback nullは上流既定、data null可
	/// return後も借用が続きます。置換/利用の排他と生存期間はclass noteに従い、OKは登録完了です
	Status (*logSet)( ggml_log_callback callback, void *userData) noexcept ;
	/// 非null outへcontext既定structをcopyします。成功時のDTW headsはnullで、structの解放は不要です
	/// DLL失敗時は零、portable例外時は旧値が残り得ます。OKはcopy完了です
	Status (*contextDefaultParams)( whisper_context_params *out) noexcept ;
	/// 非nullのUTF-8 NUL終端path/設定を同期借用し、非null outへ所有contextを返します。空pathは上流openに委ねます
	/// pathは上流が複製します。設定内custom DTW heads等はcontextの最終利用まで生存させます
	/// DLLは失敗時null/上流NULLをENGINE_FAILURE、portableはOK+NULLもあり得ます。同表freeContextで解放します
	Status (*initFromFile)( const char *path, const whisper_context_params *params, whisper_context **out) noexcept ;
	/// 同表生成contextを一回解放します。nullは何も解放せず、OKは呼出し完了です
	/// 非OKは解放不確定のため再freeせず、contextに対応するDLLもunloadしません
	Status (*freeContext)( whisper_context *context) noexcept ;
	/// GREEDY又はBEAM_SEARCHの既定設定を非null outへcopyします。既定languageは静的借用文字列です
	/// DLLはenum/nullを検査して失敗時零、portableは検査/零化せず例外時に旧値が残り得ます
	Status (*fullDefaultParams)( whisper_sampling_strategy strategy, whisper_full_params *out) noexcept ;
	/// 非null context/paramsとcount要素の非null 16kHz mono float PCMを同期処理し、推論状態を変更します
	/// countは0以上のsample数。設定内配列/文字列/callback/dataも同期利用終了まで有効に保ちます
	/// 非null nativeCodeへ上流の符号付きintをcopyします。Status::OKでも非0なら認識失敗です
	/// DLL非OKの出力は入口の0、portable例外では旧値もあり得ます。失敗時のcontextを巻き戻しません
	Status (*full)( whisper_context *context, const whisper_full_params *params, const float *samples, int count,
				   int *nativeCode) noexcept ;
	/// 非null context/outで元segment件数をcopyします。0は結果なしで、OKは取得完了だけを表します
	Status (*nSegments)( whisper_context *context, int *out) noexcept ;
	/// 取得済み件数内のindexから非null outへ上流のNUL終端本文byte列を借用します。空も保持します
	/// 次のcontext変更API又はfreeまで有効でhost free不可。OKは取得完了です
	/// 本文はUTF-8を想定しますが、このgetterはencodingを検証しません
	Status (*segmentText)( whisper_context *context, int index, const char **out) noexcept ;
	/// 取得済み件数内のindexから非null outへ開始centisecond（1/100秒）をcopyします
	/// 上流の窓時刻を補正せず、元fileへの原点加算はhost責務です。OKは取得完了です
	Status (*segmentT0)( whisper_context *context, int index, std::int64_t *out) noexcept ;
	/// 取得済み件数内のindexから非null outへ終了centisecond（1/100秒）をcopyします
	/// 上流の窓時刻を補正せず、元fileへの原点加算はhost責務です。OKは取得完了です
	Status (*segmentT1)( whisper_context *context, int index, std::int64_t *out) noexcept ;
	/// 取得済み件数内のindexから非null outへsegment内token件数をcopyします。0はtokenなしです
	Status (*nTokens)( whisper_context *context, int index, int *out) noexcept ;
	/// 取得済み件数内のsegment/tokenから非null outへ固定型whisper_token_dataを値copyします
	/// 時刻はcentisecondの元値/未取得sentinelを保持し、所有資源を移譲しません。OKは取得完了です
	Status (*tokenData)( whisper_context *context, int segment, int token, whisper_token_data *out) noexcept ;
	/// 取得済み件数内のsegment/tokenから非null outへ元語彙のNUL終端byte列を借用します
	/// 空文字/特別tokenも保持し、次のcontext変更API又はfreeまで有効でhost free不可です
	/// 単独tokenのUTF-8完全性は保証せず、語彙byte列を検証/変換しません
	Status (*tokenText)( whisper_context *context, int segment, int token, const char **out) noexcept ;
	/// 非null context/outで語彙のEOT token IDをcopyします。件数や推論成功ではなく語彙の識別値です
	Status (*tokenEot)( whisper_context *context, whisper_token *out) noexcept ;
} ;
static_assert( std::is_standard_layout_v<ApiV1>) ;
static_assert( sizeof(Status) == 4) ;
// CPU gate後にだけ解決・呼出す唯一のproduction exportです
using GetApiV1 = Status (*)( std::uint32_t version, std::uint32_t bytes, const ApiV1 **out) noexcept ;
} // namespace koefude::whisper_cpu

#ifdef KOEFUDE_WHISPER_CPU_EXPORTS
#define KOEFUDE_WHISPER_CPU_EXPORT __declspec(dllexport)
#else
#define KOEFUDE_WHISPER_CPU_EXPORT
#endif
//------------------------------------------------------------------------------
/**
 * @brief CPU gate後に同moduleの不変な固定関数表を借用します
 * @param[in] version 要求ABI版。K_ABI_VERSIONとの完全一致を要求します
 * @param[in] bytes hostのsizeof(ApiV1)をbyte数で渡し、完全一致を要求します
 * @param[out] out 非nullの書込先。検査前にnull化し、成功時に同moduleのK_APIを指す借用pointerを返します
 * @return out nullはINVALID_ARGUMENT、版/size不一致はABI_MISMATCH、両条件一致ならStatus::OK
 * @note tableはmodule生存中不変で、hostでfreeせずmoduleを閉じた後は使いません。model生成やcallback登録はしません
 */
//------------------------------------------------------------------------------
extern "C" KOEFUDE_WHISPER_CPU_EXPORT koefude::whisper_cpu::Status
									  koefude_whisper_cpu_get_api_v1( std::uint32_t version, std::uint32_t bytes,
																	 const koefude::whisper_cpu::ApiV1 **out) noexcept ;
