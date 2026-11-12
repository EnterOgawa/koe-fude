//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_native_preparation.h
 * @brief 全原文・全Whisper資料とlocal検証leaseを一要求へ封印します
 * @version 1.0.0
 * @date 2026/10/19
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/19 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/19 有界な全量準備とoperation封印の契約を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 歴史input bindingをcoreの単一encodingへ移動
 * 0.1.0 Shunji Ogawa 2026/10/19 固定AVX2方式と通常workerの検証接続を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1284の生成しない計数と有限計画の境界を追加
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実契約・所有寿命・戻り値の意味を具体化
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415のoperator括弧と文書lambdaの書式を補修
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/whole_text_plan.h"
#include "inference/whole_text_runtime.h"

#include "core/engines.h"
#include "core/pipeline.h"
#include "core/reading_session.h"
#include "core/reading_view.h"
#include "core/types.h"
#include "core/whole_text_correction.h"
#include "inference/model_store.h"
#include "inference/whole_text_native_abi.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace koefude {
/** @brief decoderが実測した同じPCMの整数識別値です */
struct WholeTextAudioIdentity {
	/// 原PCMの全byte識別値
	std::array<std::uint8_t, 32> pcmSha256{} ;
	/// 実16kHz mono sample数
	std::uint64_t				 sampleCount = 0 ;
	/// 正規化PCMのHz
	std::uint32_t				 sampleRate = K_SAMPLE_RATE ;
	/// file原点。今回0だけを受理
	std::uint64_t				 originSample = 0 ;
} ;
/** @brief observerの途中通知と同期正常返却を区別します */
enum class WholeTextCaptureState {
	/// 全context解放後の同期正常返却
	COMPLETE,
	/// 本文のない終端
	NO_SPEECH,
	/// observerの途中通知
	PARTIAL,
	/// 同期呼出し失敗
	FAILURE,
	/// 同期呼出し取消し
	CANCELLED
} ;
/** @brief 原sampleを共有したWhisperの最終返却値を全て所有します */
struct WholeTextWhisperCapture {
	/// 原文全体の識別値
	ReadingSourceKey			 source ;
	/// primary decoderの実PCM識別値
	WholeTextAudioIdentity		 primaryAudio ;
	/// auxiliaryへ渡した実PCM識別値
	WholeTextAudioIdentity		 auxiliaryAudio ;
	/// producerの終端事実
	WholeTextCaptureState		 state = WholeTextCaptureState::FAILURE ;
	/// 全PCMを被覆する予定窓
	std::vector<AsrSampleWindow> plannedWindows ;
	/// 同期ASRが正常返却した全原行/token/trace
	Recognition					 returned ;
} ;
using WholeTextNativeInputBinding = WholeTextInputBinding ; // 歴史encodingを共有する互換名
class WholeTextNativeMaterials ;
class PreparedWholeTextNativeInput ;
//------------------------------------------------------------------------------
/**
 * @brief 単一lockと固定CPU契約の方式を返します
 * @return body/path/op非依存の実効方式
 */
//------------------------------------------------------------------------------
[[nodiscard]] ReadingProfile		 wholeTextNativeProfile() ;
//------------------------------------------------------------------------------
/**
 * @brief 同じ生成設定の固定CPU方式を返します
 * @param[in] variant 二方式のCPU契約
 * @return 単一lock由来のidとdigest
 */
//------------------------------------------------------------------------------
[[nodiscard]] ReadingProfile		 wholeTextNativeProfile( WholeTextRuntimeVariant variant) ;
//------------------------------------------------------------------------------
/**
 * @brief profileとadapterで共有するABI情報です
 * @return process寿命の不変参照
 */
//------------------------------------------------------------------------------
[[nodiscard]] const KoefudeWtInfoV1 &wholeTextNativeExpectedInfo() noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 全入力を検査して不変ownerへ移します
 * @param[in] source 不変共有原文、null不可。mutable aliasによる変更は禁止
 * @param[in] sourceKey 非0履歴と原文fingerprint
 * @param[in] capture 同期正常返却後の全資料。成功時に全量所有します
 * @param[in] stop 定期照会する協調停止
 * @return 全資料の共有owner。borrowはこのowner寿命内で有効
 * @throws OperationError 不正、容量超過、取消し。原文は変更しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::shared_ptr<const WholeTextNativeMaterials>
freezeWholeTextNativeMaterials( std::shared_ptr<const Transcript> source, const ReadingSourceKey &sourceKey,
							   WholeTextWhisperCapture capture, std::stop_token stop) ;
//------------------------------------------------------------------------------
/**
 * @brief 入力照合後、一回の既存local検証でleaseを取得します
 * @param[in] modelRoot 明示local directory。空不可
 * @param[in] materials 凍結済み全資料、null不可
 * @param[in] request 非0operationと現在実効方式
 * @param[in] stop 協調停止
 * @return 一要求へ封印されたmove-only owner
 * @throws OperationError 入力/モデル検証失敗、取消し。部分ownerを返しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] PreparedWholeTextNativeInput
	 prepareWholeTextNativeInput( const std::filesystem::path					&modelRoot,
								 std::shared_ptr<const WholeTextNativeMaterials> materials,
								 const WholeTextCorrectionRequest &request, std::stop_token stop) ;
//------------------------------------------------------------------------------
/**
 * @brief native呼出し前にowner・全要求・現在方式・全量digestを照合します
 * @param[in] input 封印済みowner。leaseを取り出しません
 * @param[in] source 同じpointer/control blockの不変原文
 * @param[in] request 封印時と完全一致する要求
 * @param[in] stop 協調停止
 * @throws OperationError 不一致、容量超過、取消し。本文を変更しません
 */
//------------------------------------------------------------------------------
void validatePreparedWholeTextNativeInput( const PreparedWholeTextNativeInput	  &input,
										  const std::shared_ptr<const Transcript> &source,
										  const WholeTextCorrectionRequest &request, std::stop_token stop) ;
namespace detail {
/** @brief V1とV2で共有する全量canonical検証結果です */
struct WholeTextCanonicalMaterials {
	/// 全token/traceを含む無編集canonical
	std::vector<std::uint8_t>			bytes ;
	/// 全物理byteを隙間なく分けた境界
	std::vector<WholeTextCanonicalSpan> spans ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 旧userを作らず全材料を同じ検証とwriterで検査します
 * @param[in] source 原文owner
 * @param[in] key source key
 * @param[in] capture 全capture
 * @param[in] stop 停止
 * @return 全lossless canonicalと物理境界
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextCanonicalMaterials canonicalWholeTextMaterials( const std::shared_ptr<const Transcript> &source,
																	  const ReadingSourceKey				  &key,
																	  const WholeTextWhisperCapture			  &capture,
																	  std::stop_token						   stop) ;
using WholeTextModelVerifier =
	std::function<VerifiedModel( const std::filesystem::path &, ModelId, std::stop_token)> ; // unitだけの同期検証seam
//------------------------------------------------------------------------------
/**
 * @brief unit用verifierを準備の同じ境界へ注入します
 * @param[in] modelRoot 明示local directory
 * @param[in] materials 凍結owner
 * @param[in] request 全要求
 * @param[in] stop 協調停止
 * @param[in] verifier 同期中有効な検証関数。通常factoryはverifyModelだけを渡します
 * @return 一要求の封印owner
 * @throws OperationError 検証失敗時は元分類/codeを保持
 */
//------------------------------------------------------------------------------
[[nodiscard]] PreparedWholeTextNativeInput prepareWholeTextNativeInputWithVerifier(
	const std::filesystem::path &modelRoot, std::shared_ptr<const WholeTextNativeMaterials> materials,
	const WholeTextCorrectionRequest &request, std::stop_token stop, const WholeTextModelVerifier &verifier) ;
//------------------------------------------------------------------------------
/**
 * @brief 固定順profile encodingを試験で独立照合します
 * @return 単一lockと現在契約だけから作る全byte。設定変更入口ではありません
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<std::uint8_t> wholeTextNativeProfileEncoding() ;
//------------------------------------------------------------------------------
/**
 * @brief idとCPU契約だけが異なる固定encodingを返します
 * @param[in] variant 固定CPU方式
 * @return 全byte所有値
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<std::uint8_t> wholeTextNativeProfileEncoding( WholeTextRuntimeVariant variant) ;
} // namespace detail

/** @brief factoryだけが作る、原文と全返却値の不変共有ownerです */
class WholeTextNativeMaterials final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 共有された不変原文/captureと全量system/user投影の実体を固定するため、copy構築を禁止します
	 * @note 名前のない第1引数（const WholeTextNativeMaterials &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	WholeTextNativeMaterials( const WholeTextNativeMaterials &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 共有された不変原文/captureと全量system/user投影の実体を固定するため、copy代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const WholeTextNativeMaterials &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	WholeTextNativeMaterials &operator=( const WholeTextNativeMaterials &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 共有された不変原文/captureと全量system/user投影の実体を固定するため、move構築を禁止します
	 * @note 既存の共有ownerと借用viewの参照先を維持するため、実体を移動しません
	 * @note 名前のない第1引数（WholeTextNativeMaterials &&）は移動元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	WholeTextNativeMaterials( WholeTextNativeMaterials &&) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 共有された不変原文/captureと全量system/user投影の実体を固定するため、move代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 既存の共有ownerと借用viewの参照先を維持するため、実体を移動しません
	 * @note 名前のない第1引数（WholeTextNativeMaterials &&）は移動元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	WholeTextNativeMaterials							  &operator=( WholeTextNativeMaterials &&) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 原文ownerを読みます
	 * @return このowner寿命内の不変参照
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const std::shared_ptr<const Transcript> &source() const noexcept
	{
		return source_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 原文keyを読みます
	 * @return 不変参照
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const ReadingSourceKey &sourceKey() const noexcept
	{
		return sourceKey_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief systemを借用します
	 * @return owner寿命内の全UTF-8 byte
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::string_view systemUtf8() const noexcept
	{
		return system_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief userを借用します
	 * @return owner寿命内の全UTF-8 byte
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::string_view userUtf8() const noexcept
	{
		return user_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 全補助返却値を読みます
	 * @return owner寿命内の不変参照
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const WholeTextWhisperCapture &capture() const noexcept
	{
		return capture_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 原文byte数を返します
	 * @return 直接連結の長さ
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t originalBytes() const noexcept
	{
		return digests_.originalBytes ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 補助byte数を返します
	 * @return canonical全体の長さ
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t auxiliaryBytes() const noexcept
	{
		return digests_.auxiliaryBytes ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief system長を返します
	 * @return UTF-8 byte数
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t systemBytes() const noexcept
	{
		return digests_.systemBytes ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief user長を返します
	 * @return UTF-8 byte数
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t userBytes() const noexcept
	{
		return digests_.userBytes ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 原文SHAを読みます
	 * @return 不変32byte
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const std::array<std::uint8_t, 32> &originalSha256() const noexcept
	{
		return digests_.originalSha256 ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 補助SHAを読みます
	 * @return 不変32byte
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const std::array<std::uint8_t, 32> &auxiliarySha256() const noexcept
	{
		return digests_.auxiliarySha256 ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief systemSHAを読みます
	 * @return 不変32byte
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const std::array<std::uint8_t, 32> &systemSha256() const noexcept
	{
		return digests_.systemSha256 ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief userSHAを読みます
	 * @return 不変32byte
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const std::array<std::uint8_t, 32> &userSha256() const noexcept
	{
		return digests_.userSha256 ;
	}

	private:
	friend std::shared_ptr<const WholeTextNativeMaterials>
	freezeWholeTextNativeMaterials( std::shared_ptr<const Transcript>, const ReadingSourceKey &, WholeTextWhisperCapture,
								   std::stop_token) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 検査済み全量だけをmoveします
	 * @param[in] source 原文owner
	 * @param[in] key 原文key
	 * @param[in] capture 全返却値
	 * @param[in] system canonical全byte
	 * @param[in] user 全投影byte
	 * @param[in] digests 検査済み長さとSHA
	 */
	//------------------------------------------------------------------------------
	WholeTextNativeMaterials( std::shared_ptr<const Transcript> source, ReadingSourceKey key,
							 WholeTextWhisperCapture capture, std::string system, std::string user,
							 WholeTextNativeInputBinding digests) ;
	/// 不変原文の共有所有権
	std::shared_ptr<const Transcript> source_ ;
	/// 行分割・話者を含む全体key
	ReadingSourceKey				  sourceKey_ ;
	/// raw値を変更しない全量所有権
	WholeTextWhisperCapture			  capture_ ;
	/// canonical1971byte
	std::string						  system_ ;
	/// strict JSONの全量投影
	std::string						  user_ ;
	/// operationを持たない資料digest
	WholeTextNativeInputBinding		  digests_ ;
} ;
/** @brief 準備関数だけが作る、一operationのmodel leaseと全量封印です */
class PreparedWholeTextNativeInput final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 一operationの資料/方式/封印と検証済みmodel leaseを一組で所有するため、copy構築を禁止します
	 * @note 名前のない第1引数（const PreparedWholeTextNativeInput &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	PreparedWholeTextNativeInput( const PreparedWholeTextNativeInput &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 一operationの資料/方式/封印と検証済みmodel leaseを一組で所有するため、copy代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const PreparedWholeTextNativeInput &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	PreparedWholeTextNativeInput &operator=( const PreparedWholeTextNativeInput &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 資料の共有所有権、方式、要求封印、model leaseを一組でmove構築します
	 * @param[in,out] other 同じworkerで移譲する準備済みowner。移譲後のfield値や再使用可能状態は保証しません
	 * @note live資源を複製せず、保存・生成・native再openは行いません
	 */
	//------------------------------------------------------------------------------
	PreparedWholeTextNativeInput( PreparedWholeTextNativeInput &&other) noexcept = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 資料の共有所有権、方式、要求封印、model leaseを一組でmove代入します
	 * @param[in,out] other 同じworkerで移譲する準備済みowner。移譲後のfield値や再使用可能状態は保証しません
	 * @return 移譲先である*thisへの借用参照。移譲先の寿命まで有効です
	 * @note 既存memberは各型のmove代入で解放・置換します。live資源を複製せず、保存・生成・native再openは行いません
	 */
	//------------------------------------------------------------------------------
	PreparedWholeTextNativeInput &operator=( PreparedWholeTextNativeInput &&other) noexcept = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 全資料ownerを読みます
	 * @return このowner寿命内の不変参照
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const std::shared_ptr<const WholeTextNativeMaterials> &materials() const noexcept
	{
		return materials_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 封印方式を読みます
	 * @return 不変参照
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const ReadingProfile &profile() const noexcept
	{
		return profile_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 要求固有封印を読みます
	 * @return 不変参照
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const WholeTextNativeInputBinding &binding() const noexcept
	{
		return binding_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief leaseを所有したまま参照します
	 * @return handleをmoveできない不変参照
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const VerifiedModel &model() const noexcept
	{
		return model_ ;
	}

	private:
	friend PreparedWholeTextNativeInput detail::prepareWholeTextNativeInputWithVerifier(
		const std::filesystem::path &, std::shared_ptr<const WholeTextNativeMaterials>,
		const WholeTextCorrectionRequest &, std::stop_token, const detail::WholeTextModelVerifier &) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 全量封印とleaseをmoveします
	 * @param[in] materials 不変資料
	 * @param[in] profile 実効方式
	 * @param[in] binding 全要求封印
	 * @param[in] model 検証済みlease
	 */
	//------------------------------------------------------------------------------
	PreparedWholeTextNativeInput( std::shared_ptr<const WholeTextNativeMaterials> materials, ReadingProfile profile,
								 WholeTextNativeInputBinding binding, VerifiedModel model) ;
	/// 全原文/全補助のowner
	std::shared_ptr<const WholeTextNativeMaterials> materials_ ;
	/// 静的方式digest
	ReadingProfile									profile_ ;
	/// operation固有digest
	WholeTextNativeInputBinding						binding_ ;
	/// native解放より長く所有する読取りlease
	VerifiedModel									model_ ;
} ;
} // namespace koefude
