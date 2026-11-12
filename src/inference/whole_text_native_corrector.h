//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_native_corrector.h
 * @brief 不変の準備入力を既存全文workerへ接続するadapterを定義します
 * @version 1.0.0
 * @date 2026/10/19
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/19 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/19 通常providerへ登録しない内部接続を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 手組み入力を全量検証factoryの封印ownerへ置換
 * 0.1.0 Shunji Ogawa 2026/10/19 native解放まで共有する協調資源guardを追加
 * 0.1.0 Shunji Ogawa 2026/10/19 一回実測の受動観測と証拠保持を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 固定AVX2方式と通常workerの検証接続を追加
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
#pragma once
#include "core/reading_session.h"
#include "core/whole_text_correction.h"
#include "inference/whole_text_native_abi.h"
#include "inference/whole_text_native_preparation.h"
#include "inference/whole_text_resource_guard.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>

namespace koefude {
struct WholeTextApplicationObservation ; // hostの任意受動出力
namespace detail {
/** @brief host unitだけの注入seam。実DLL lifetimeはcorrectのscopeで保持します */
struct WholeTextNativeCalls {
	/// 非nullの固定情報関数
	decltype(&koefude_whole_text_info_v1)	  info ;
	/// 非nullの同期関数
	decltype(&koefude_whole_text_generate_v1) generate ;
} ;
/** @brief 内部試験だけの同期loader返却値です */
struct WholeTextNativeLoaded {
	/// 所有scope内だけ有効なC ABI
	WholeTextNativeCalls		   calls ;
	/// callback終了後に一回だけ解放、例外を投げません
	std::function<std::uint32_t()> close ;
} ;
// 内部試験だけが小さいfake moduleを使用します。通常factoryは常にOS loaderです
using WholeTextNativeLoader = std::function<WholeTextNativeLoaded( const std::filesystem::path &,
																  std::shared_ptr<const VerifiedWholeTextRuntime>)> ;
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
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextCorrectionResponse invokeWholeTextNative( const PreparedWholeTextNativeInput &input,
																std::shared_ptr<const Transcript>	source,
																const WholeTextCorrectionRequest   &request,
																const WholeTextNativeCalls &calls, std::stop_token stop,
																const ProgressCallback				   &progress,
																std::shared_ptr<WholeTextResourceGuard> guard = {},
																WholeTextApplicationObservation *observation = nullptr) ;
} // namespace detail
/** @brief worker限定の同期接続です。通常appの明示CPU試用で使用します */
class NativeWholeTextCorrector final : public IWholeTextCorrector {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 値とhandleだけmoveし、DLLやmodelを開きません
	 * @param[in] modulePath 絶対localの固有名DLL
	 * @param[in] input 準備済み所有値
	 * @param[in] guard 同期callと全解放まで保持する資源監視、旧内部用途だけnull可
	 * @param[out] observation null可。corrector破棄まで有効な受動出力
	 * @param[in] runtime AVX2で必須の検証owner
	 * @param[in] loader 通常は空、内部試験のみ
	 */
	//------------------------------------------------------------------------------
	NativeWholeTextCorrector( std::filesystem::path modulePath, PreparedWholeTextNativeInput input,
							 std::shared_ptr<WholeTextResourceGuard>		 guard = {},
							 WholeTextApplicationObservation				*observation = nullptr,
							 std::shared_ptr<const VerifiedWholeTextRuntime> runtime = {},
							 detail::WholeTextNativeLoader					 loader = {}) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 不変原文から自由な全文を返します
	 * @param[in] source 呼出し中有効な共有所有原文。null不可
	 * @param[in] request hostが固定した原文・ID・方式
	 * @param[in] stop 協調取消し
	 * @param[in] progress workerでの実処理通知
	 * @return 未検証の所有応答。失敗は例外で返します
	 * @note 同期呼出し内でengine資源を解放し、cleanup失敗を成功にしません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] WholeTextCorrectionResponse correct( std::shared_ptr<const Transcript> source,
													  const WholeTextCorrectionRequest &request, std::stop_token stop,
													  const ProgressCallback &progress) override ;

	private:
	/// 通常は空、内部試験の同期fakeだけ
	const detail::WholeTextNativeLoader					  loader_ ;
	/// FreeLibraryより長い両lease
	const std::shared_ptr<const VerifiedWholeTextRuntime> runtime_ ;
	/// 探索しない明示絶対path
	const std::filesystem::path							  modulePath_ ;
	/// callbackと全cleanupより長い監視owner
	const std::shared_ptr<WholeTextResourceGuard>		  guard_ ;
	/// 不変message/source/profile/model lease
	const PreparedWholeTextNativeInput					  input_ ;
	/// 非所有、同期終了まで有効
	WholeTextApplicationObservation *const				  observation_ ;
} ;
} // namespace koefude
