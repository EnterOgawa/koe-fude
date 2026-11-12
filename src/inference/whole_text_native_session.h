//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_native_session.h
 * @brief 住所不変callbackとnative計数sessionの依存順を管理します
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1284の有限計画と生成しない計数を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1285の有界逐次補正を接続
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
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
#include "core/whole_text_bounded_result.h"
#include "core/whole_text_plan.h"
#include "inference/whole_text_native_preparation.h"
#include "inference/whole_text_resource_guard.h"
#include <span>
#include <atomic>
#include <memory>
#include <utility>
namespace koefude {
/** @brief 旧計数だけの用途を暗黙に生成へ変えません */
enum class WholeTextNativeExecutionUse {
	/// 旧計数用途、暗黙昇格しない
	COUNT_ONLY,
	/// 封印計画と保存領域を持つ逐次生成
	SERIAL_GENERATION
} ;
/** @brief job側だけが所有しgateはweakで参照する通知先です */
class WholeTextBoundedProgressTarget {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 同期通知先を終了します
	 */
	//------------------------------------------------------------------------------
	virtual ~WholeTextBoundedProgressTarget() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 元の実段階だけを通知します
	 * @param[in] stage 段階
	 * @param[in] completed 完了数
	 * @param[in] total 総数
	 * @return 停止要求ならtrue
	 */
	//------------------------------------------------------------------------------
	virtual bool notify( std::uint32_t stage, std::uint32_t completed, std::uint32_t total) = 0 ;
} ;
/** @brief retained Stateからjobの寿命を延長しない有限通知gateです */
class WholeTextBoundedProgressGate final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 通知先をweakで保持します
	 * @param[in] target job側owner
	 */
	//------------------------------------------------------------------------------
	explicit WholeTextBoundedProgressGate( std::weak_ptr<WholeTextBoundedProgressTarget> target)
		: target_( std::move( target))
	{
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief close/terminal前に以後の通知を停止します
	 */
	//------------------------------------------------------------------------------
	void disable() noexcept
	{
		enabled_.store( false) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 有効なweak targetだけに同期通知します
	 * @param[in] stage 段階
	 * @param[in] completed 完了数
	 * @param[in] total 総数
	 * @return 停止なら1。例外は固定停止に変換します
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint32_t notify( std::uint32_t stage, std::uint32_t completed, std::uint32_t total) noexcept ;

	private:
	/// job延命なし
	std::weak_ptr<WholeTextBoundedProgressTarget> target_ ;
	/// close前に無効化
	std::atomic<bool>							  enabled_ = true ;
} ;
namespace detail {
/** @brief unitだけのloader seamです。module ownerは全callより長く生存します */
struct WholeTextCounterApi {
	/// 固定info
	decltype(&koefude_whole_text_info_v2)			info = nullptr ;
	/// 一回open
	decltype(&koefude_whole_text_open_counter_v2)	open = nullptr ;
	/// 同期borrow
	decltype(&koefude_whole_text_measure_v2)		measure = nullptr ;
	/// 一回close
	decltype(&koefude_whole_text_close_session_v2)	close = nullptr ;
	std::shared_ptr<void>							module ; // FreeLibrary用owner
	/// 一回昇格
	decltype(&koefude_whole_text_promote_v2)		promote = nullptr ;
	/// 逐次生成
	decltype(&koefude_whole_text_generate_chunk_v2) generate = nullptr ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 通常OS loaderの固定exportを一回解決します
 * @param[in] path 検証済み絶対DLL
 * @return module所有API
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextCounterApi loadWholeTextCounter( const std::filesystem::path &path) ;
using WholeTextCounterLoader = std::function<WholeTextCounterApi( const std::filesystem::path &)> ; // test seam
//------------------------------------------------------------------------------
/**
 * @brief V2情報をmodel検証前に完全照合します
 * @param[in] info V2全field
 * @param[in] use CountOnlyはmask1、生成はmask3
 * @param[in] kind 照合する生成方式です。MINISTRAL_PLAINはPlain用、それ以外はGemma用の固定infoを選びます
 * @throws OperationError capability/固定値差
 */
//------------------------------------------------------------------------------
void			   validateWholeTextCounterInfo( const KoefudeWtInfoV2	   &info,
												WholeTextNativeExecutionUse use = WholeTextNativeExecutionUse::COUNT_ONLY,
												WholeTextGenerationKind		kind = WholeTextGenerationKind::GEMMA_THOUGHT_FINAL) ;
//------------------------------------------------------------------------------
/**
 * @brief 未確認cleanupでprocess寿命のownerを保持しているか読みます
 * @return restartが必要ならtrue。resetや自動close再試行の入口はありません
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool wholeTextCounterRestartRequired() noexcept ;
} // namespace detail
/** @brief moveしてもcallback住所が変わらないlive計数sessionです */
class WholeTextNativeCountSession final {
	public:
	struct State ; // 所有実装を隠す型
	//------------------------------------------------------------------------------
	/**
	 * @brief privateなopen結果だけを所有します
	 * @param[in] state 住所不変owner
	 */
	//------------------------------------------------------------------------------
	explicit WholeTextNativeCountSession( std::unique_ptr<State> state) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief native→DLL→model→runtime→callbackの順に解放します
	 */
	//------------------------------------------------------------------------------
	~WholeTextNativeCountSession() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 住所不変State内のnative/DLL/model/runtime/callbackを二重解放しないため、copy構築を禁止します
	 * @note 名前のない第1引数（const WholeTextNativeCountSession &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	WholeTextNativeCountSession( const WholeTextNativeCountSession &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 住所不変State内のnative/DLL/model/runtime/callbackを二重解放しないため、copy代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const WholeTextNativeCountSession &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	WholeTextNativeCountSession &operator=( const WholeTextNativeCountSession &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief callback住所を変えず所有権を移します
	 * @param[in,out] other 元owner
	 */
	//------------------------------------------------------------------------------
	WholeTextNativeCountSession( WholeTextNativeCountSession &&other) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 自己代入以外では旧native Stateを終了処理し、住所不変のState所有権をotherから移します
	 * @param[in,out] other 移動元のsession owner。自己代入時は不変、それ以外はStateを失い空になります
	 * @return このobjectへの借用参照。object破棄後は使用できません
	 * @note 旧Stateの解放が未確認ならdisposeCountStateの保持経路を使います。正常終了や資源回収を偽って保証せず、例外は送出しません
	 */
	//------------------------------------------------------------------------------
	WholeTextNativeCountSession					&operator=( WholeTextNativeCountSession &&other) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じmodel/vocabで全候補を計数します
	 * @param[in] prompt 一候補
	 * @return 検証receipt
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] WholeTextTokenReceipt			 measure( const WholeTextChunkPrompt &prompt) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 明示closeの失敗を呼出側へ返します
	 * @throws OperationError 元code/取消し
	 */
	//------------------------------------------------------------------------------
	void										 close() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 元close PODを固定値で返します。再試行しません
	 * @return 解放証拠
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] WholeTextNativeCloseReceipt	 closeChecked() noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 計画とjournal封印後だけ昇格します
	 * @param[in] promotion 全計画値
	 */
	//------------------------------------------------------------------------------
	void										 promote( const WholeTextBoundedPromotion &promotion) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じsessionで一coreだけ生成します
	 * @param[in] prompt 同期borrow
	 * @param[in] record 封印record
	 * @param[in] input 入力SHA
	 * @param[out] output 最大65536byteのhost領域
	 * @return 本文を含まない成功receipt
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] WholeTextBoundedChunkReceipt	 generateChunk( const WholeTextChunkPrompt &prompt,
															   const WholeTextChunkRecord &record,
															   const WholeTextDigest	  &input,
															   std::span<std::uint8_t>	   output) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief primary native callの元値を読みます
	 * @return closeと別の固定値
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] WholeTextNativeFailureSnapshot failureSnapshot() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief native session IDを読みます
	 * @return move元/close後0
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t					 id() const noexcept ;

	private:
	/// callbackを内包する住所不変owner
	std::unique_ptr<State> state_ ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 検証済みruntime→info→model一回→vocab一回の順に準備します
 * @param[in] modelRoot local root
 * @param[in] runtime runtime両lease
 * @param[in] guard 同operationの固定baseline
 * @param[in] binding 全要求束縛
 * @param[in] stop 停止
 * @return live計数session
 * @param[in] use 旧CountOnlyを既定とし生成は明示します
 * @param[out] failureClose null可。失敗時は元close証拠、native未取得なら空です
 * @param[in] progressGate null可。job targetへweakだけを持つ所有gate
 * @param[out] failureNative null可。失敗時の元native値をclose前に保存します
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextNativeCountSession openWholeTextCountSession(
	const std::filesystem::path &modelRoot, std::shared_ptr<const VerifiedWholeTextRuntime> runtime,
	std::shared_ptr<WholeTextResourceGuard> guard, const WholeTextDigest &binding, std::stop_token stop,
	WholeTextNativeExecutionUse					  use = WholeTextNativeExecutionUse::COUNT_ONLY,
	WholeTextNativeCloseReceipt					 *failureClose = nullptr,
	std::shared_ptr<WholeTextBoundedProgressGate> progressGate = {},
	WholeTextNativeFailureSnapshot				 *failureNative = nullptr) ;
namespace detail {
//------------------------------------------------------------------------------
/**
 * @brief 小verifier/loaderを同じopen境界へ注入します
 * @param[in] modelRoot local root
 * @param[in] runtime 両lease
 * @param[in] guard 同じ資源baseline
 * @param[in] binding 全束縛
 * @param[in] stop 停止
 * @param[in] verifier 一回だけ検証
 * @param[in] loader 同期DLL loader
 * @return live owner
 * @param[in] use 旧CountOnlyを既定とし生成は明示します
 * @param[out] failureClose null可。失敗時は元close証拠、native未取得なら空です
 * @param[in] progressGate null可。job targetへweakだけを持つ所有gate
 * @param[out] failureNative null可。失敗時の元native値をclose前に保存します
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextNativeCountSession openWholeTextCountSessionWithDependencies(
	const std::filesystem::path &modelRoot, std::shared_ptr<const VerifiedWholeTextRuntime> runtime,
	std::shared_ptr<WholeTextResourceGuard> guard, const WholeTextDigest &binding, std::stop_token stop,
	const WholeTextModelVerifier &verifier, const WholeTextCounterLoader &loader,
	WholeTextNativeExecutionUse					  use = WholeTextNativeExecutionUse::COUNT_ONLY,
	WholeTextNativeCloseReceipt					 *failureClose = nullptr,
	std::shared_ptr<WholeTextBoundedProgressGate> progressGate = {},
	WholeTextNativeFailureSnapshot				 *failureNative = nullptr) ;
} // namespace detail
} // namespace koefude
