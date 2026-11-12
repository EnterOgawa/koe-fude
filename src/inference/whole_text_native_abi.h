//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_native_abi.h
 * @brief 固有CPU DLLとhost間の有界な所有権付きC ABIを定義します
 * @version 1.0.0
 * @date 2026/10/19
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/19 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/19 ADR1266の3関数とPOD境界を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1284の生成しない計数と有限計画の境界を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1285の有界逐次補正を接続
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1308のV2未完理由を既存診断欄へ追加
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1310のFinal1536・派生予算と限定検証へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// callbackは同期呼出し終了とnative解放まで有効。例外を投げず非0で停止する
using KoefudeWtStopV1 = std::uint32_t(__cdecl *)( void *) ;
// progressはentry workerだけで呼ばれ、total=0は不定を示す
using KoefudeWtProgressV1 = std::uint32_t(__cdecl *)( void *, std::uint32_t, std::uint32_t, std::uint32_t) ;

/** @brief ABI statusです。本文やpathを含みません */
enum class KoefudeWtStatus : std::uint32_t {
	/// 全体成功
	SUCCESS = 0,
	/// ABI不一致
	ABI_MISMATCH = 1,
	/// 全体入力違反
	INVALID_INPUT = 2,
	/// モデル不足
	MODEL_UNAVAILABLE = 3,
	/// 資源または容量不足
	RESOURCE_UNAVAILABLE = 4,
	/// 推論失敗
	INFERENCE_FAILED = 5,
	/// 自然終端なし
	INCOMPLETE = 6,
	/// 協調取消し
	CANCELLED = 7,
	/// 同時呼出し拒否
	BUSY = 8,
	/// 内部例外
	INTERNAL = 9
} ;
/** @brief 固定の診断分類です。Decodeだけはengine_codeも保持します */
enum class KoefudeWtDiagnostic : std::uint32_t {
	/// 成功
	NONE = 0,
	/// 構造違反
	ABI = 0x57540001,
	/// 文字列違反
	UTF8,
	/// prompt容量
	PROMPT_BYTES,
	/// prefix token容量
	PREFIX_TOKENS,
	/// 実context不一致
	CONTEXT,
	/// モデル準備
	MODEL_LOAD,
	/// 確保失敗
	ALLOCATION,
	/// 元decode値あり
	DECODE,
	/// token ID異常
	TOKEN,
	/// logits異常
	NONFINITE,
	/// lane構造
	FRAMING,
	/// 自然終端なし
	INCOMPLETE,
	/// 停止要求
	CANCELLED,
	/// 解放異常
	CLEANUP,
	/// guard拒否
	BUSY,
	/// callback異常
	CALLBACK_FAILURE,
	/// metadata不一致
	MODEL_METADATA,
	/// 出力容量
	OUTPUT_BYTES,
	/// V2でFinalへ移る前にEOGを取得
	ANALYSIS_EOG_BEFORE_FINAL = 0x57540013,
	/// そのruntimeのFinal上限個目が非EOG
	FINAL_TOKEN_LIMIT = 0x57540014
} ;

/** @brief hostが同期call中所有するUTF-8 byte借用です */
struct KoefudeWtBytesV1 {
	/// bytes=0だけnull可、NULを含まない
	const std::uint8_t *data ;
	/// UTF-8 byte数
	std::uint32_t		bytes ;
	/// 常に0
	std::uint32_t		reserved ;
} ;
/** @brief 呼出し側はsize/versionだけ初期化し、DLLが固定情報を返します */
struct KoefudeWtInfoV1 {
	/// 144byte
	std::uint32_t structSize ;
	/// 1
	std::uint32_t abiVersion ;
	/// 64
	std::uint32_t pointerBits ;
	/// CPU/static/two-textの7
	std::uint32_t flags ;
	/// NUL終端の固定commit
	char		  engineCommit[41] ;
	/// NUL終端の固定SHA
	char		  templateSha256[65] ;
	/// 6912
	std::uint32_t contextTokens ;
	/// 4096
	std::uint32_t prefixTokensMax ;
	/// 2816
	std::uint32_t outputTokensMax ;
	/// 1279
	std::uint32_t analysisSampledMax ;
	/// 1536、自然EOG込み
	std::uint32_t finalSampledMax ;
} ;
/** @brief 全pointerの所有権をhostへ残す同期要求です */
struct KoefudeWtRequestV1 {
	/// 80byte
	std::uint32_t		structSize ;
	/// 1
	std::uint32_t		abiVersion ;
	/// 終端NUL付きUTF-16、native解放まで有効
	const wchar_t	   *modelPath ;
	/// 終端を除く1〜32767単位
	std::uint32_t		modelPathUnits ;
	/// 0
	std::uint32_t		reserved ;
	/// 不変system
	KoefudeWtBytesV1	system ;
	/// 不変user
	KoefudeWtBytesV1	user ;
	/// null可、native threadからも呼ぶ
	KoefudeWtStopV1		shouldStop ;
	/// null可、workerだけで呼ぶ
	KoefudeWtProgressV1 progress ;
	/// 全cleanup終了までhostが所有
	void			   *callbackData ;
} ;
/** @brief 成功時だけhost bufferへ一回copyし、失敗時bufferは不変です */
struct KoefudeWtResultV1 {
	/// 56byte
	std::uint32_t structSize ;
	/// 1
	std::uint32_t abiVersion ;
	/// host所有、capacity=0だけnull可
	std::uint8_t *text ;
	/// NUL終端を含まない容量
	std::uint32_t textCapacity ;
	/// 失敗時0
	std::uint32_t textBytes ;
	/// generate成功時だけ1
	std::uint32_t naturalEog ;
	/// 固定分類
	std::uint32_t diagnostic ;
	/// decodeの元符号
	std::int32_t  engineCode ;
	/// 実prefix数
	std::uint32_t prefixTokens ;
	/// openingと自然closingを含む
	std::uint32_t analysisSampledTokens ;
	/// 0または1
	std::uint32_t closingControlTokens ;
	/// 自然EOG込み
	std::uint32_t finalSampledTokens ;
	/// 0
	std::uint32_t reserved ;
} ;

static_assert( sizeof(void *) == 8 && sizeof(wchar_t) == 2) ;
static_assert( sizeof(KoefudeWtBytesV1) == 16 && alignof(KoefudeWtBytesV1) == 8) ;
static_assert( sizeof(KoefudeWtInfoV1) == 144 && alignof(KoefudeWtInfoV1) == 4) ;
static_assert( sizeof(KoefudeWtRequestV1) == 80 && alignof(KoefudeWtRequestV1) == 8) ;
static_assert( sizeof(KoefudeWtResultV1) == 56 && alignof(KoefudeWtResultV1) == 8) ;
static_assert( offsetof( KoefudeWtRequestV1, system) == 24) ;
static_assert( offsetof( KoefudeWtRequestV1, shouldStop) == 56) ;
static_assert( offsetof( KoefudeWtResultV1, text) == 8) ;
static_assert( offsetof( KoefudeWtResultV1, naturalEog) == 24) ;
static_assert( std::is_trivial_v<KoefudeWtResultV1> && std::is_standard_layout_v<KoefudeWtRequestV1>) ;

#if defined(KOEFUDE_WHOLE_TEXT_EXPORTS)
#define KOEFUDE_WT_EXPORT __declspec(dllexport)
#else
#define KOEFUDE_WT_EXPORT
#endif

//------------------------------------------------------------------------------
/**
 * @brief modelを開かず、当DLL方式の固定engine/template/生成容量情報を取得します
 * @param[in,out] info 呼出し中有効なhost所有POD。非nullでstructSizeとabiVersion=1を設定します。成功時だけ全fieldを置換します
 * @return 0は情報取得成功、1はnull/size/版の不一致、9はlog sink設定等の内部例外です。非0ではinfoを置換しません
 * @note 動的model/contextを作らず、返す配列はPOD内の所有値です。C++例外をmodule外へ出しません
 */
//------------------------------------------------------------------------------
extern "C" KOEFUDE_WT_EXPORT std::uint32_t __cdecl koefude_whole_text_info_v1( KoefudeWtInfoV1 *info) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 当DLL方式の固定templateで二つのUTF-8本文を描画し、完成したprefixだけhostへcopyします
 * @param[in] system 非nullのPODと、そのdata/bytesが示すsystem本文を同期借用します。終端不要、reserved=0、空列だけdata=null可です
 * @param[in] user 非nullのPODとuser本文を同期借用します。UTF-8/NUL/容量を検査し、systemと同じ固定描画式へ渡します
 * @param[in,out] result host所有の非null出力。size/ABI=1/reserved=0とtext/textCapacityを設定します。textは容量byteの書込領域で、
 *        容量0だけnull可です
 * @return KoefudeWtStatusの整数。0は全prefixのcopy成功、1はABI不一致、2は入力文字列違反、4は容量/確保不足、9は内部例外です
 * @note 正しい出力PODでは先に結果metadataを零化します。失敗時text bufferは変更せず、診断fieldだけ更新し得ます。model/tokenizerを呼ばず、C++例外を越えません
 */
//------------------------------------------------------------------------------
extern "C" KOEFUDE_WT_EXPORT std::uint32_t __cdecl koefude_whole_text_render_v1( const KoefudeWtBytesV1 *system,
																				const KoefudeWtBytesV1 *user,
																				KoefudeWtResultV1 *result) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief Gemma V1の一要求をCPUで同期生成し、自然終端と全資源解放を確認したFinalだけcopyします
 * @param[in] request 非nullのsize/ABI=1/reserved=0の要求POD。UTF-16 model path、UTF-8二本文、
 *        停止/進捗callbackとdataを全native解放まで同期借用します
 * @param[in,out] result host所有の非null出力POD。size/ABI=1/reserved=0と容量byteのtext bufferを渡します。容量0だけtext=null可です
 *        成功時は本文と実counterを設定します
 * @return KoefudeWtStatusの整数。0は自然終端・解放済みの完成本文、1はABI/方式不一致、2は入力違反、3はmodel利用不可、4は容量/確保不足、5は推論失敗、6は未完、7は取消し、8は競合、
 *        9は内部/解放失敗です
 * @note 正しい出力PODは結果metadataを先に零化し、非0ではtext bufferを変更しません。Plain固有DLLはmodel I/O前に拒否します。元診断/engineCodeを保持しC++例外を越えません
 */
//------------------------------------------------------------------------------
extern "C" KOEFUDE_WT_EXPORT std::uint32_t __cdecl koefude_whole_text_generate_v1( const KoefudeWtRequestV1 *request,
																				  KoefudeWtResultV1 *result) noexcept ;

/** @brief V1を変更しないV2の固定情報です */
struct KoefudeWtInfoV2 {
	/// V2実size
	std::uint32_t	structSize ;
	/// 2
	std::uint32_t	abiVersion ;
	/// 同じengine/template/生成予算、ABI欄は1
	KoefudeWtInfoV1 common ;
	/// U2は1、既知maskは3
	std::uint32_t	capabilitiesMask ;
	/// 2
	std::uint32_t	accountingContract ;
	/// 2
	std::uint32_t	planContract ;
	/// 2
	std::uint32_t	sessionContract ;
	/// 65536
	std::uint32_t	maximumRenderBytes ;
	/// 1
	std::uint32_t	maximumSessions ;
	/// 0
	std::uint32_t	reserved[2] ;
} ;
/** @brief UTF-16 pathとcallbackを同期で渡す計数開始要求です */
struct KoefudeWtCounterOpenV2 {
	/// 実size
	std::uint32_t				 structSize ;
	/// 2
	std::uint32_t				 abiVersion ;
	/// 同期open中だけ借用、終端NUL付き
	const wchar_t				*modelPath ;
	/// 1..32767
	std::uint32_t				 modelPathUnits ;
	/// 0
	std::uint32_t				 reserved ;
	/// 材料/request/profile束縛
	std::array<std::uint8_t, 32> bindingSha256 ;
	/// closeまでcallback ownerは生存
	KoefudeWtStopV1				 shouldStop ;
	/// worker上だけ通知
	KoefudeWtProgressV1			 progress ;
	/// moveでも住所不変
	void						*callbackData ;
} ;
/** @brief 非0の再利用しないIDだけを返します */
struct KoefudeWtCounterOpenResultV2 {
	/// 実size
	std::uint32_t structSize ;
	/// 2
	std::uint32_t abiVersion ;
	/// 成功時だけ非0
	std::uint64_t sessionId ;
	/// 元分類
	std::uint32_t diagnostic ;
	/// 元符号
	std::int32_t  engineCode ;
} ;
/** @brief 同じlive vocabへの同期borrowです */
struct KoefudeWtMeasureRequestV2 {
	/// 実size
	std::uint32_t				 structSize ;
	/// 2
	std::uint32_t				 abiVersion ;
	/// 同期borrow
	KoefudeWtBytesV1			 system ;
	/// 同期borrow
	KoefudeWtBytesV1			 user ;
	/// plain text
	KoefudeWtBytesV1			 core ;
	/// 空可
	KoefudeWtBytesV1			 before ;
	/// 空可
	KoefudeWtBytesV1			 after ;
	/// 候補全byte
	std::array<std::uint8_t, 32> candidateBinding ;
	/// 0
	std::uint32_t				 reserved[2] ;
} ;
/** @brief allocator所有権を渡さない全計数receiptです */
struct KoefudeWtMeasureResultV2 {
	/// 実size
	std::uint32_t								structSize ;
	/// 2
	std::uint32_t								abiVersion ;
	/// live ID
	std::uint64_t								sessionId ;
	/// 1始まり
	std::uint64_t								sequence ;
	/// 2
	std::uint32_t								accountingContract ;
	/// bit0 fits、bit1 全token digest
	std::uint32_t								flags ;
	/// 独立全prefix
	std::uint32_t								prefixTokens ;
	/// 独立core
	std::uint32_t								coreTokens ;
	/// 独立前文脈
	std::uint32_t								beforeTokens ;
	/// 独立後文脈
	std::uint32_t								afterTokens ;
	/// system/user/rendered/core/before/after
	std::array<std::uint64_t, 6>				bytes ;
	/// 同順の全SHA
	std::array<std::array<std::uint8_t, 32>, 6> sha256 ;
	/// 元候補
	std::array<std::uint8_t, 32>				candidateBinding ;
	/// 全prefix ID LE u32
	std::array<std::uint8_t, 32>				tokenSha256 ;
	/// 固定receipt全体
	std::array<std::uint8_t, 32>				receiptSha256 ;
	/// 固定診断
	std::uint32_t								diagnostic ;
	/// 元符号
	std::int32_t								engineCode ;
	/// 0
	std::uint32_t								reserved[2] ;
} ;
/** @brief close済みなら取消し中でもIDを消費します */
struct KoefudeWtCloseResultV2 {
	/// 実size
	std::uint32_t structSize ;
	/// 2
	std::uint32_t abiVersion ;
	/// Busyは0、解放後は1
	std::uint32_t closed ;
	/// cleanup失敗も保持
	std::uint32_t diagnostic ;
	/// 元符号
	std::int32_t  engineCode ;
	/// 0
	std::uint32_t reserved ;
} ;
static_assert( sizeof(KoefudeWtInfoV2) == 184 && alignof(KoefudeWtInfoV2) == 4) ;
static_assert( sizeof(KoefudeWtCounterOpenV2) == 80 && alignof(KoefudeWtCounterOpenV2) == 8) ;
static_assert( sizeof(KoefudeWtCounterOpenResultV2) == 24) ;
static_assert( sizeof(KoefudeWtMeasureRequestV2) == 128) ;
static_assert( sizeof(KoefudeWtMeasureResultV2) == 400 && alignof(KoefudeWtMeasureResultV2) == 8) ;
static_assert( sizeof(KoefudeWtCloseResultV2) == 24) ;
static_assert( offsetof( KoefudeWtMeasureRequestV2, system) == 8) ;
static_assert( offsetof( KoefudeWtMeasureResultV2, bytes) == 48) ;
static_assert( offsetof( KoefudeWtCounterOpenV2, callbackData) == 72) ;
static_assert( std::is_trivial_v<KoefudeWtMeasureResultV2> && std::is_standard_layout_v<KoefudeWtCounterOpenV2>) ;
//------------------------------------------------------------------------------
/**
 * @brief modelを開かず、V1共通情報とV2計数/計画/session能力を一つのPODへ取得します
 * @param[in,out] info 非nullのhost所有POD。structSizeとabiVersion=2だけを初期化します。成功時だけ全fieldを置換します
 * @return 0は固定情報取得成功、1はnull/size/版不一致、V1情報取得失敗時はその非0 status（内部例外は9）です。失敗時infoは不変です
 * @note POD内の配列と整数を返すだけで、modelやsessionの所有権を生成しません
 */
//------------------------------------------------------------------------------
extern "C" KOEFUDE_WT_EXPORT std::uint32_t __cdecl koefude_whole_text_info_v2( KoefudeWtInfoV2 *info) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 一つのvocab-only modelを開き、再利用しない非0計数session IDを発行します
 * @param[in] request 非nullのABI=2要求をreturnまで借用します。非空/NUL終端UTF-16 model path、非零binding SHA、
 *        callback/dataはsessionの最終解放まで有効に保ちます
 * @param[in,out] result 非nullのhost所有size/ABI=2初期化済みPOD。開始時に結果を初期化し、成功時sessionIdを返します。失敗でも解放未確認なら非0 IDを保持します
 * @return KoefudeWtStatusの整数。0はopen成功、1はABI不一致、2は要求違反、3はmodel利用不可、4は確保/ID容量不足、7は取消し、8は同時entry/既存sessionとの競合、
 *        9は内部/解放異常です
 * @note 失敗時の非0 IDもhostの後続close責任を表します。失敗をID0や解放済みへ読み替えません。C++例外は固定診断へ変換します
 */
//------------------------------------------------------------------------------
extern "C" KOEFUDE_WT_EXPORT
	std::uint32_t __cdecl koefude_whole_text_open_counter_v2( const KoefudeWtCounterOpenV2 *request,
															 KoefudeWtCounterOpenResultV2 *result) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 同じlive sessionの語彙で全prefixとcore/before/afterを独立計数し、照合receiptを返します
 * @param[in] sessionId openで受け取った非0のlive ID。同じDLLの一sessionへだけ使用します
 * @param[in] request 非nullのABI=2計数要求とUTF-8各byte範囲をreturnまで借用します。生成や入力の変更は行いません
 * @param[in,out] result 非nullのhost所有size/ABI=2初期化済みPOD。開始時に結果を零化し、成功時だけ全計数/hash/flagsを設定します
 * @return KoefudeWtStatusの整数。0は計数完了で、予算適合は別のflagsで示します。1はABI不一致、2はID/要求違反、4は容量/確保不足、7は取消し、8は競合、9は内部異常です
 * @note 失敗してもsessionの所有責任はhostに残ります。C++例外を越えず、再計数・生成を自動実行しません
 */
//------------------------------------------------------------------------------
extern "C" KOEFUDE_WT_EXPORT std::uint32_t __cdecl koefude_whole_text_measure_v2(
	std::uint64_t sessionId, const KoefudeWtMeasureRequestV2 *request, KoefudeWtMeasureResultV2 *result) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 対象sessionの全native資源を閉じ、解放を確認できたIDだけを消費します
 * @param[in] sessionId openで受け取った非0のlive ID。別IDや消費済みIDは拒否します
 * @param[in,out] result 非nullのhost所有size/ABI=2初期化済みPOD。closed=1はownerの解放確認、diagnostic/engineCodeは元cleanup異常を示します
 * @return KoefudeWtStatusの整数。0は全解放成功、1は出力ABI不一致、2はID違反、8は同時entry、9は内部/cleanup失敗です。closed=1でも元cleanup codeが非0なら9です
 * @note 解放未確認ではclosed=0と同じowner/IDを保持し、destructorへ迂回しません。取消し状態でもcleanupを進め、C++例外は外へ出しません
 */
//------------------------------------------------------------------------------
extern "C" KOEFUDE_WT_EXPORT
	std::uint32_t __cdecl koefude_whole_text_close_session_v2( std::uint64_t			  sessionId,
															  KoefudeWtCloseResultV2 *result) noexcept ;

/** @brief 有界逐次補正の固定144byte PODです */
struct KoefudeWtPromoteRequestV2 {
	/// 固定PODの全byte数
	std::uint32_t				 structSize ;
	/// 加算ABI版2
	std::uint32_t				 abiVersion ;
	/// 封印された同名の契約値
	std::array<std::uint8_t, 32> openBindingSha256 ;
	/// 封印された同名の契約値
	std::array<std::uint8_t, 32> planSha256 ;
	/// 封印された同名の契約値
	std::array<std::uint8_t, 32> inputSha256 ;
	/// 封印された同名の契約値
	std::uint64_t				 originalBytes ;
	/// 封印された同名の契約値
	std::uint64_t				 planMeasures ;
	/// 封印された同名の契約値
	std::uint64_t				 planMeasuredBytes ;
	/// 封印された同名の契約値
	std::uint32_t				 recordCount ;
	/// 封印された同名の契約値
	std::uint32_t				 generationCount ;
	/// 常に0
	std::array<std::uint32_t, 2> reserved ;
} ;
static_assert( sizeof(KoefudeWtPromoteRequestV2) == 144 && alignof(KoefudeWtPromoteRequestV2) == 8) ;
static_assert( std::is_trivial_v<KoefudeWtPromoteRequestV2> && std::is_standard_layout_v<KoefudeWtPromoteRequestV2>) ;
static_assert( offsetof( KoefudeWtPromoteRequestV2, structSize) == 0) ;
static_assert( offsetof( KoefudeWtPromoteRequestV2, abiVersion) == 4) ;
static_assert( offsetof( KoefudeWtPromoteRequestV2, openBindingSha256) == 8) ;
static_assert( offsetof( KoefudeWtPromoteRequestV2, planSha256) == 40) ;
static_assert( offsetof( KoefudeWtPromoteRequestV2, inputSha256) == 72) ;
static_assert( offsetof( KoefudeWtPromoteRequestV2, originalBytes) == 104) ;
static_assert( offsetof( KoefudeWtPromoteRequestV2, planMeasures) == 112) ;
static_assert( offsetof( KoefudeWtPromoteRequestV2, planMeasuredBytes) == 120) ;
static_assert( offsetof( KoefudeWtPromoteRequestV2, recordCount) == 128) ;
static_assert( offsetof( KoefudeWtPromoteRequestV2, generationCount) == 132) ;
static_assert( offsetof( KoefudeWtPromoteRequestV2, reserved) == 136) ;
/** @brief 有界逐次補正の固定32byte PODです */
struct KoefudeWtPromoteResultV2 {
	/// 固定PODの全byte数
	std::uint32_t structSize ;
	/// 加算ABI版2
	std::uint32_t abiVersion ;
	/// 同じ非0 session値
	std::uint64_t sessionId ;
	/// 成功時だけ1
	std::uint32_t promoted ;
	/// 元の固定診断
	std::uint32_t diagnostic ;
	/// 元の符号付きengine値
	std::int32_t  engineCode ;
	/// 常に0
	std::uint32_t reserved ;
} ;
static_assert( sizeof(KoefudeWtPromoteResultV2) == 32 && alignof(KoefudeWtPromoteResultV2) == 8) ;
static_assert( std::is_trivial_v<KoefudeWtPromoteResultV2> && std::is_standard_layout_v<KoefudeWtPromoteResultV2>) ;
static_assert( offsetof( KoefudeWtPromoteResultV2, structSize) == 0) ;
static_assert( offsetof( KoefudeWtPromoteResultV2, abiVersion) == 4) ;
static_assert( offsetof( KoefudeWtPromoteResultV2, sessionId) == 8) ;
static_assert( offsetof( KoefudeWtPromoteResultV2, promoted) == 16) ;
static_assert( offsetof( KoefudeWtPromoteResultV2, diagnostic) == 20) ;
static_assert( offsetof( KoefudeWtPromoteResultV2, engineCode) == 24) ;
static_assert( offsetof( KoefudeWtPromoteResultV2, reserved) == 28) ;
/** @brief 有界逐次補正の固定632byte PODです */
struct KoefudeWtChunkRequestV2 {
	/// 固定PODの全byte数
	std::uint32_t				 structSize ;
	/// 加算ABI版2
	std::uint32_t				 abiVersion ;
	/// 封印された同名の契約値
	std::uint32_t				 chunkIndex ;
	/// 常に0
	std::uint32_t				 reserved0 ;
	/// 封印された同名の契約値
	std::uint64_t				 coreBegin ;
	/// 封印された同名の契約値
	std::uint64_t				 coreEnd ;
	/// 封印された同名の契約値
	std::array<std::uint8_t, 32> inputSha256 ;
	/// 封印された同名の契約値
	std::array<std::uint8_t, 32> planSha256 ;
	/// 同じsessionの受理済み計数証拠
	KoefudeWtMeasureResultV2	 expectedReceipt ;
	/// returnまでの同期byte借用
	KoefudeWtMeasureRequestV2	 prompt ;
	/// 全要素0
	std::array<std::uint32_t, 2> reserved1 ;
} ;
static_assert( sizeof(KoefudeWtChunkRequestV2) == 632 && alignof(KoefudeWtChunkRequestV2) == 8) ;
static_assert( std::is_trivial_v<KoefudeWtChunkRequestV2> && std::is_standard_layout_v<KoefudeWtChunkRequestV2>) ;
static_assert( offsetof( KoefudeWtChunkRequestV2, structSize) == 0) ;
static_assert( offsetof( KoefudeWtChunkRequestV2, abiVersion) == 4) ;
static_assert( offsetof( KoefudeWtChunkRequestV2, chunkIndex) == 8) ;
static_assert( offsetof( KoefudeWtChunkRequestV2, reserved0) == 12) ;
static_assert( offsetof( KoefudeWtChunkRequestV2, coreBegin) == 16) ;
static_assert( offsetof( KoefudeWtChunkRequestV2, coreEnd) == 24) ;
static_assert( offsetof( KoefudeWtChunkRequestV2, inputSha256) == 32) ;
static_assert( offsetof( KoefudeWtChunkRequestV2, planSha256) == 64) ;
static_assert( offsetof( KoefudeWtChunkRequestV2, expectedReceipt) == 96) ;
static_assert( offsetof( KoefudeWtChunkRequestV2, prompt) == 496) ;
static_assert( offsetof( KoefudeWtChunkRequestV2, reserved1) == 624) ;
/** @brief 有界逐次補正の固定320byte PODです */
struct KoefudeWtChunkResultV2 {
	/// 固定PODの全byte数
	std::uint32_t				 structSize ;
	/// 加算ABI版2
	std::uint32_t				 abiVersion ;
	/// returnまでhostが所有する書込み先
	std::uint8_t				*text ;
	/// 最大65536byte、集約残量以下
	std::uint32_t				 textCapacity ;
	/// 成功時の正の本文byte、失敗時0
	std::uint32_t				 textBytes ;
	/// 同じ非0 session値
	std::uint64_t				 sessionId ;
	/// 封印された同名の契約値
	std::uint32_t				 chunkIndex ;
	/// 成功時だけ1、失敗時0
	std::uint32_t				 naturalEog ;
	/// 封印された同名の契約値
	std::uint32_t				 prefixTokens ;
	/// 封印された同名の契約値
	std::uint32_t				 analysisSampledTokens ;
	/// 封印された同名の契約値
	std::uint32_t				 closingControlTokens ;
	/// 封印された同名の契約値
	std::uint32_t				 finalSampledTokens ;
	/// 封印された同名の契約値
	std::uint64_t				 renderedBytes ;
	/// 封印された同名の契約値
	std::uint64_t				 coreBegin ;
	/// 封印された同名の契約値
	std::uint64_t				 coreEnd ;
	/// 封印された同名の契約値
	std::array<std::uint8_t, 32> planSha256 ;
	/// 封印された同名の契約値
	std::array<std::uint8_t, 32> inputSha256 ;
	/// 封印された同名の契約値
	std::array<std::uint8_t, 32> expectedReceiptSha256 ;
	/// 封印された同名の契約値
	std::array<std::uint8_t, 32> actualPrefixTokenSha256 ;
	/// 封印された同名の契約値
	std::array<std::uint8_t, 32> renderedSha256 ;
	/// 封印された同名の契約値
	std::array<std::uint8_t, 32> bodySha256 ;
	/// 封印された同名の契約値
	std::array<std::uint8_t, 32> generationReceiptSha256 ;
	/// 元の固定診断
	std::uint32_t				 diagnostic ;
	/// 元の符号付きengine値
	std::int32_t				 engineCode ;
	/// 常に0
	std::array<std::uint32_t, 2> reserved ;
} ;
static_assert( sizeof(KoefudeWtChunkResultV2) == 320 && alignof(KoefudeWtChunkResultV2) == 8) ;
static_assert( std::is_trivial_v<KoefudeWtChunkResultV2> && std::is_standard_layout_v<KoefudeWtChunkResultV2>) ;
static_assert( offsetof( KoefudeWtChunkResultV2, structSize) == 0) ;
static_assert( offsetof( KoefudeWtChunkResultV2, abiVersion) == 4) ;
static_assert( offsetof( KoefudeWtChunkResultV2, text) == 8) ;
static_assert( offsetof( KoefudeWtChunkResultV2, textCapacity) == 16) ;
static_assert( offsetof( KoefudeWtChunkResultV2, textBytes) == 20) ;
static_assert( offsetof( KoefudeWtChunkResultV2, sessionId) == 24) ;
static_assert( offsetof( KoefudeWtChunkResultV2, chunkIndex) == 32) ;
static_assert( offsetof( KoefudeWtChunkResultV2, naturalEog) == 36) ;
static_assert( offsetof( KoefudeWtChunkResultV2, prefixTokens) == 40) ;
static_assert( offsetof( KoefudeWtChunkResultV2, analysisSampledTokens) == 44) ;
static_assert( offsetof( KoefudeWtChunkResultV2, closingControlTokens) == 48) ;
static_assert( offsetof( KoefudeWtChunkResultV2, finalSampledTokens) == 52) ;
static_assert( offsetof( KoefudeWtChunkResultV2, renderedBytes) == 56) ;
static_assert( offsetof( KoefudeWtChunkResultV2, coreBegin) == 64) ;
static_assert( offsetof( KoefudeWtChunkResultV2, coreEnd) == 72) ;
static_assert( offsetof( KoefudeWtChunkResultV2, planSha256) == 80) ;
static_assert( offsetof( KoefudeWtChunkResultV2, inputSha256) == 112) ;
static_assert( offsetof( KoefudeWtChunkResultV2, expectedReceiptSha256) == 144) ;
static_assert( offsetof( KoefudeWtChunkResultV2, actualPrefixTokenSha256) == 176) ;
static_assert( offsetof( KoefudeWtChunkResultV2, renderedSha256) == 208) ;
static_assert( offsetof( KoefudeWtChunkResultV2, bodySha256) == 240) ;
static_assert( offsetof( KoefudeWtChunkResultV2, generationReceiptSha256) == 272) ;
static_assert( offsetof( KoefudeWtChunkResultV2, diagnostic) == 304) ;
static_assert( offsetof( KoefudeWtChunkResultV2, engineCode) == 308) ;
static_assert( offsetof( KoefudeWtChunkResultV2, reserved) == 312) ;
//------------------------------------------------------------------------------
/**
 * @brief 封印済みplanとjournalのbindingを照合し、同じ計数sessionをtensor付きCPU modelへ一度昇格します
 * @param[in] sessionId hostが所有する非0のlive計数session ID
 * @param[in] request ABI=2の昇格要求とその参照範囲をreturnまで借用します。plan/input/receiptの封印値を同じsessionへ照合します
 * @param[in,out] result 非nullのhost所有size/ABI=2/reserved=0のPOD。開始時に結果を初期化し、成功時だけ同sessionIdとpromoted=1を設定します
 * @return KoefudeWtStatusの整数。0は昇格完了、1はABI/metadata不一致、2はID/封印違反、3はmodel利用不可、4は資源不足、7は取消し、8は競合、9は内部/解放異常です
 * @note 非0でもsessionの解放責任はhostに残ります。本文生成は行わず、C++例外を固定診断へ変換します
 */
//------------------------------------------------------------------------------
extern "C" KOEFUDE_WT_EXPORT std::uint32_t __cdecl koefude_whole_text_promote_v2(
	std::uint64_t sessionId, const KoefudeWtPromoteRequestV2 *request, KoefudeWtPromoteResultV2 *result) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 昇格済み同modelで封印された一coreを再計数・生成し、chunk資源の解放確認後だけhostへcopyします
 * @param[in] sessionId hostが所有する非0のlive昇格済みsession ID
 * @param[in] request 非nullのABI=2一core要求。prefix/plan/input/計数receiptのbyte範囲をreturnまで同期借用します
 * @param[in,out] result 非nullのhost所有size/ABI=2/reserved零のPOD。textはtextCapacity byteの書込領域で、容量0だけnull可です
 *        成功時に本文と全生成receiptを設定します
 * @return KoefudeWtStatusの整数。0は自然終端・chunk解放済み、1はABI不一致、2はID/封印違反、4は容量不足、5は推論失敗、6は未完、7は取消し、8は競合、9は内部/解放異常です
 * @note 正しい出力PODはbufferと容量を保持して結果metadataを零化します。非0ではtext bufferを変更せず、session/modelのclose責任をhostに残します
 *        自動retryせずC++例外を越えません
 */
//------------------------------------------------------------------------------
extern "C" KOEFUDE_WT_EXPORT std::uint32_t __cdecl koefude_whole_text_generate_chunk_v2(
	std::uint64_t sessionId, const KoefudeWtChunkRequestV2 *request, KoefudeWtChunkResultV2 *result) noexcept ;
