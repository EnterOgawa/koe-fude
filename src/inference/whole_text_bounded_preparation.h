//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_bounded_preparation.h
 * @brief 全量材料と局所計画を新profileへ封印します
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1284の有限計画と生成しない計数を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1285の有界逐次補正を接続
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1319の取得後returnを非送出moveへ限定
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1343の文章専用補正を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実契約・所有寿命・戻り値の意味を具体化し、標準headerを直接include
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415のoperator括弧と文書lambdaの書式を補修
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "inference/whole_text_native_session.h"
#include <span>
namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief 新system固定byteを返します
 * @return LF、末尾LFなし1865byte
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string_view	wholeTextBoundedSystem() noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 旧profileとは別の固定方式を返します
 * @param[in] variant CPU契約
 * @return 全設定digest、資源/品質合格を意味しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] ReadingProfile	wholeTextBoundedProfile( WholeTextRuntimeVariant variant) ;
//------------------------------------------------------------------------------
/**
 * @brief 音声を使わない独立profileを返します
 * @return 固定全設定digest
 */
//------------------------------------------------------------------------------
[[nodiscard]] ReadingProfile	textContextProfile() ;
/** @brief 全原文/capture/canonicalを一つだけ保持する不変ownerです */
class WholeTextBoundedMaterials final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 原文/capture/canonicalの所有実体と、それらを指す内部borrowを分離・複製しないため、copy構築を禁止します
	 * @note 名前のない第1引数（const WholeTextBoundedMaterials &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	WholeTextBoundedMaterials( const WholeTextBoundedMaterials &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 原文/capture/canonicalの所有実体と、それらを指す内部borrowを分離・複製しないため、copy代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const WholeTextBoundedMaterials &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	WholeTextBoundedMaterials							  &operator=( const WholeTextBoundedMaterials &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ原文ownerを読みます
	 * @return 不変共有owner
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const std::shared_ptr<const Transcript> &source() const noexcept
	{
		return source_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 元keyを読みます
	 * @return 不変参照
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const ReadingSourceKey &sourceKey() const noexcept
	{
		return key_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 全captureを読みます
	 * @return 無編集参照
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const WholeTextWhisperCapture &capture() const noexcept
	{
		return capture_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 全canonicalを読みます
	 * @return owner寿命内borrow
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::span<const std::uint8_t> auxiliary() const noexcept
	{
		return canonical_.bytes ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 全field境界を読みます
	 * @return owner寿命内borrow
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::span<const WholeTextCanonicalSpan> spans() const noexcept
	{
		return canonical_.spans ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 要求固有binding付きviewを作ります
	 * @param[in] binding 要求全体
	 * @param[in] kind 返すviewへcopyする生成方式です。材料と本文ownerを変更せず、この関数内では方式の検証や推論を始めません
	 * @return owner寿命内borrow
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] WholeTextPlanInputView
		 view( const WholeTextDigest	 &binding,
			  WholeTextGenerationKind kind = WholeTextGenerationKind::GEMMA_THOUGHT_FINAL) const ;
	//------------------------------------------------------------------------------
	/**
	 * @brief mutable aliasと全canonicalを再照合します
	 * @param[in] stop 停止
	 */
	//------------------------------------------------------------------------------
	void validate( std::stop_token stop) const ;

	private:
	friend std::shared_ptr<const WholeTextBoundedMaterials>
	freezeBoundedWholeTextMaterials( std::shared_ptr<const Transcript>, const ReadingSourceKey &,
									WholeTextWhisperCapture, std::stop_token) ;
	friend std::shared_ptr<const WholeTextBoundedMaterials>
	freezeTextContextMaterials( std::shared_ptr<const Transcript>, const ReadingSourceKey &, std::stop_token) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 検証済み全量を一回moveします
	 * @param[in] source 同じ原文
	 * @param[in] key 元key
	 * @param[in] capture 全返却
	 * @param[in] canonical 共通writer全byte
	 */
	//------------------------------------------------------------------------------
	WholeTextBoundedMaterials( std::shared_ptr<const Transcript> source, ReadingSourceKey key,
							  WholeTextWhisperCapture capture, detail::WholeTextCanonicalMaterials canonical) ;
	/// 音声captureを検証しない独立材料種別
	bool								 textOnly_ = false ;
	/// 原文同control block
	std::shared_ptr<const Transcript>	 source_ ;
	/// 全行/時刻/話者束縛
	ReadingSourceKey					 key_ ;
	/// コピーしない全capture
	WholeTextWhisperCapture				 capture_ ;
	/// 全lossless byteと境界
	detail::WholeTextCanonicalMaterials	 canonical_ ;
	/// sourceへ借用
	std::vector<WholeTextOriginalOrigin> original_ ;
	/// captureへ借用
	std::vector<WholeTextRawOrigin>		 raw_ ;
	/// 全canonical
	WholeTextDigest						 auxiliarySha256_{} ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 旧全userを作らず全量を保持します
 * @param[in] source 原文
 * @param[in] key 全体key
 * @param[in] capture 全captureをmove
 * @param[in] stop 停止
 * @return 不変owner
 * @throws OperationError 不正/上限/停止
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::shared_ptr<const WholeTextBoundedMaterials>
freezeBoundedWholeTextMaterials( std::shared_ptr<const Transcript> source, const ReadingSourceKey &key,
								WholeTextWhisperCapture capture, std::stop_token stop) ;
//------------------------------------------------------------------------------
/**
 * @brief 不変原文だけを凍結し音声入力を持ちません
 * @param[in] source 原文owner、null不可
 * @param[in] key 元全fieldのkey
 * @param[in] stop 協調停止
 * @return raw/canonical/auxiliaryが空の不変owner
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::shared_ptr<const WholeTextBoundedMaterials>
freezeTextContextMaterials( std::shared_ptr<const Transcript> source, const ReadingSourceKey &key, std::stop_token stop) ;
class PreparedBoundedWholeTextPlan ; // factoryの戻り値だけを先行宣言
namespace detail {
//------------------------------------------------------------------------------
/**
 * @brief 同じ準備へunitの小verifier/loaderだけを注入します
 * @param[in] modelRoot root
 * @param[in] materials 全材料
 * @param[in] request 全要求
 * @param[in] runtime 両lease
 * @param[in] guard baseline
 * @param[in] stop 停止
 * @param[in] verifier 一回検証
 * @param[in] loader V2関数
 * @return U3所有物
 * @param[in] use 旧CountOnlyを既定とし生成は明示します
 * @param[out] failureClose null可。失敗時は元close証拠、native未取得なら空です
 * @param[in] progressGate null可。job targetへweakだけを持つ所有gate
 * @param[out] failureNative null可。失敗時の元native値をclose前に保存します
 */
//------------------------------------------------------------------------------
[[nodiscard]] PreparedBoundedWholeTextPlan prepareBoundedWholeTextPlanWithDependencies(
	const std::filesystem::path &modelRoot, std::shared_ptr<const WholeTextBoundedMaterials> materials,
	const WholeTextCorrectionRequest &request, std::shared_ptr<const VerifiedWholeTextRuntime> runtime,
	std::shared_ptr<WholeTextResourceGuard> guard, std::stop_token stop, const WholeTextModelVerifier &verifier,
	const WholeTextCounterLoader &loader, WholeTextNativeExecutionUse use = WholeTextNativeExecutionUse::COUNT_ONLY,
	WholeTextNativeCloseReceipt					 *failureClose = nullptr,
	std::shared_ptr<WholeTextBoundedProgressGate> progressGate = {},
	WholeTextNativeFailureSnapshot				 *failureNative = nullptr) ;
} // namespace detail
/** @brief U3へ材料/封印plan/live sessionを一緒に渡すmove-only ownerです */
class PreparedBoundedWholeTextPlan final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 全材料・封印planとlive native sessionを同じ一operationだけが所有するため、copy構築を禁止します
	 * @note 名前のない第1引数（const PreparedBoundedWholeTextPlan &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	PreparedBoundedWholeTextPlan( const PreparedBoundedWholeTextPlan &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 全材料・封印planとlive native sessionを同じ一operationだけが所有するため、copy代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const PreparedBoundedWholeTextPlan &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	PreparedBoundedWholeTextPlan &operator=( const PreparedBoundedWholeTextPlan &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 全材料の共有所有権、要求、封印plan/digest、live sessionを一組でmove構築します
	 * @param[in,out] other 同じworkerで移譲する準備済みowner。移譲後のfield値や再使用可能状態は保証しません
	 * @note live資源を複製せず、保存・生成・native再openは行いません
	 */
	//------------------------------------------------------------------------------
	PreparedBoundedWholeTextPlan( PreparedBoundedWholeTextPlan &&other) noexcept = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 全材料の共有所有権、要求、封印plan/digest、live sessionを一組でmove代入します
	 * @param[in,out] other 同じworkerで移譲する準備済みowner。移譲後のfield値や再使用可能状態は保証しません
	 * @return 移譲先である*thisへの借用参照。移譲先の寿命まで有効です
	 * @note 既存memberは各型のmove代入で解放・置換します。live資源を複製せず、保存・生成・native再openは行いません
	 */
	//------------------------------------------------------------------------------
	PreparedBoundedWholeTextPlan &operator=( PreparedBoundedWholeTextPlan &&other) noexcept = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 全材料ownerを読みます
	 * @return 不変owner
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const auto	 &materials() const noexcept
	{
		return materials_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 元の要求を読みます
	 * @return 同じ要求値
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const WholeTextCorrectionRequest &request() const noexcept
	{
		return request_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief sealed planを読みます
	 * @return pointerを持たない全plan
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const WholeTextChunkPlan &plan() const noexcept
	{
		return plan_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同workerで所有sessionを継続使用します
	 * @return live owner
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] WholeTextNativeCountSession &session() noexcept
	{
		return session_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 最終input束縛を読みます
	 * @return request/plan全SHA
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const WholeTextDigest &inputSha256() const noexcept
	{
		return inputSha256_ ;
	}

	private:
	friend PreparedBoundedWholeTextPlan detail::prepareBoundedWholeTextPlanWithDependencies(
		const std::filesystem::path &, std::shared_ptr<const WholeTextBoundedMaterials>,
		const WholeTextCorrectionRequest &, std::shared_ptr<const VerifiedWholeTextRuntime>,
		std::shared_ptr<WholeTextResourceGuard>, std::stop_token, const detail::WholeTextModelVerifier &,
		const detail::WholeTextCounterLoader &, WholeTextNativeExecutionUse, WholeTextNativeCloseReceipt *,
		std::shared_ptr<WholeTextBoundedProgressGate>, WholeTextNativeFailureSnapshot *) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief factoryが完成ownerを移します
	 * @param[in] materials 全量
	 * @param[in] request 要求
	 * @param[in] plan 全plan
	 * @param[in] session 同じlive計数
	 * @param[in] digest 新input束縛
	 */
	//------------------------------------------------------------------------------
	PreparedBoundedWholeTextPlan( std::shared_ptr<const WholeTextBoundedMaterials> materials,
								 WholeTextCorrectionRequest request, WholeTextChunkPlan plan,
								 WholeTextNativeCountSession session, WholeTextDigest digest) noexcept ;

	private:
	/// 同じ全量owner
	std::shared_ptr<const WholeTextBoundedMaterials> materials_ ;
	/// profile/source/op
	WholeTextCorrectionRequest						 request_ ;
	/// 全coverageとreceipt
	WholeTextChunkPlan								 plan_ ;
	/// 最後に全planを束縛
	WholeTextDigest									 inputSha256_ ;
	/// 最内側で先に解放
	WholeTextNativeCountSession						 session_ ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 計画だけを完成させて生成せず返します
 * @param[in] modelRoot local root
 * @param[in] materials 凍結全量
 * @param[in] request 新profileの要求
 * @param[in] runtime 両lease
 * @param[in] guard 同じ資源baseline
 * @param[in] stop 停止
 * @return U3所有物
 * @param[in] use 旧CountOnlyを既定とし生成は明示します
 * @param[out] failureClose null可。失敗時は元close証拠、native未取得なら空です
 * @param[in] progressGate null可。job targetへweakだけを持つ所有gate
 * @param[out] failureNative null可。失敗時の元native値をclose前に保存します
 */
//------------------------------------------------------------------------------
[[nodiscard]] PreparedBoundedWholeTextPlan prepareBoundedWholeTextPlan(
	const std::filesystem::path &modelRoot, std::shared_ptr<const WholeTextBoundedMaterials> materials,
	const WholeTextCorrectionRequest &request, std::shared_ptr<const VerifiedWholeTextRuntime> runtime,
	std::shared_ptr<WholeTextResourceGuard> guard, std::stop_token stop,
	WholeTextNativeExecutionUse					  use = WholeTextNativeExecutionUse::COUNT_ONLY,
	WholeTextNativeCloseReceipt					 *failureClose = nullptr,
	std::shared_ptr<WholeTextBoundedProgressGate> progressGate = {},
	WholeTextNativeFailureSnapshot				 *failureNative = nullptr) ;

} // namespace koefude
