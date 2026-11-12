//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_text_corrector.cpp
 * @brief 単一OpenAI Responses profileをHTTP境界の背後へ接続します
 * @version 1.0.0
 * @date 2026/11/08
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/08 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/08 API補正interfaceと固定providerを追加
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1345の動的モデル・凍結単価と旧履歴互換を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1350の厳密モデルと実在日付suffixを照合
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1360の応答発言ID検証で後続送信と採用を停止
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
#include "core/api_text_correction.h"
#include "core/api_text_provenance.h"
#include "core/api_text_speaker_turns.h"
#include <chrono>
#include <cstddef>
#include <memory>
#include <stop_token>
namespace koefude {
namespace {
/** @brief UIから呼ばない同期OpenAI providerです */
class ApiTextCorrector final : public IApiTextCorrector {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief worker専用transportを借用します
	 * @param[in,out] transport 長寿命owner
	 */
	//------------------------------------------------------------------------------
	explicit ApiTextCorrector( IApiHttpTransport &transport) : transport_( transport)
	{
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 確認済みの一chunkを補正します
	 * @param[in] draft 不変投影
	 * @param[in] index 0始まりのplan位置
	 * @param[in] key 秘密の同期borrow
	 * @param[in] stop 協調取消し
	 * @param[in] deadline 全operationの期限
	 * @return usage/部分本文を保持する終端
	 */
	//------------------------------------------------------------------------------
	ApiTextChunkResult correctChunk( const ApiTextDraft &draft, std::size_t index, const ApiKeySecret &key,
									std::stop_token stop, std::chrono::steady_clock::time_point deadline) override
	{
		auto result = parseApiTextResponse(
			transport_.post( apiTextRequestBody( draft, index), key, stop, deadline)) ; // known本文/usageを維持
		// 新profileだけ厳密実modelとStandard scopeを照合します
		if( !draft.catalogSnapshot.empty()) {
			const auto requested = apiCatalogSnapshotModel( draft.catalogSnapshot) ; // 凍結した厳密ID
			result.priceApplicable = apiProvenanceMatches( requested, result.model) &&
									 (!result.serviceTier || *result.serviceTier == "default") ;
			// 実scope不一致・usage矛盾は後続と全文採用を停止します
			if( !result.priceApplicable || !apiTextUsageCost( result.usage, draft.catalogSnapshot)) {
				result.complete = false ;
			}
		} else if( result.model != "gpt-6.1-sol" && !result.model.starts_with( "gpt-6.1-sol-")) { // 凍結legacy許可範囲
			result.complete = false ;
		}
		// 完全応答でも発言区切りの欠落/順序違いを成功として採用しません
		if( result.complete && apiTextSpeakerProfile( draft.catalogSnapshot)) {
			try {
				static_cast<void>( parseApiTextSpeakerParts( draft, index, result.text)) ;
			} catch( const OperationError &error) {
				result.complete = false ;
				result.code = error.code() ; // 実usage/model/本文/課金状態は保持
			}
		}
		return result ;
	}

	private:
	/// callerが補正器より長く所有
	IApiHttpTransport &transport_ ;
} ;
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 固定OpenAI providerを作ります
 * @param[in,out] transport 全寿命内のborrow
 * @return 同期worker専用補正器
 * @see koefude::makeApiTextCorrector
 */
//------------------------------------------------------------------------------
std::unique_ptr<IApiTextCorrector> makeApiTextCorrector( IApiHttpTransport &transport)
{
	return std::make_unique<ApiTextCorrector>( transport) ;
}
} // namespace koefude
