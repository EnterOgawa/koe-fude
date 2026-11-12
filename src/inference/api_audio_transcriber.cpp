//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_audio_transcriber.cpp
 * @brief 固定音声HTTPを有界応答providerへ接続します
 * @version 1.0.0
 * @date 2026/11/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の一窓一回送信を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実使用の標準headerを直接include
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "core/api_audio_transcription.h"
#include <span>
#include <chrono>
#include <memory>
namespace koefude {
namespace {
/** @brief requestごとに600秒の期限を持つworker専用adapterです */
class ApiAudioTranscriber final : public IApiAudioTranscriber {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief transportを借用します
	 * @param[in,out] http 本objectより長寿命
	 */
	//------------------------------------------------------------------------------
	explicit ApiAudioTranscriber( IApiAudioHttpTransport &http) : http_( http)
	{
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 一窓を一回送信し検証します
	 * @param[in] samples 有界PCM
	 * @param[in] autoLanguage 言語選択
	 * @param[in] key 秘密borrow
	 * @param[in] stop 取消し
	 * @return 完全本文または本文なしの固定診断。自動再送しません
	 */
	//------------------------------------------------------------------------------
	ApiAudioChunkResult transcribe( std::span<const float> samples, bool autoLanguage, const ApiKeySecret &key,
								   std::stop_token stop) override
	{
		const auto response =
			http_.post( samples, autoLanguage, key, stop,
					   std::chrono::steady_clock::now() + std::chrono::seconds( 600)) ; // 一requestの期限
		return parseApiAudioResponse( response, samples.size()) ;
	}

	private:
	/// worker同期scopeのborrow
	IApiAudioHttpTransport &http_ ;
} ;
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 同期providerを作ります
 * @param[in,out] http 呼出しより長寿命のborrow
 * @return 所有provider
 * @see koefude::makeApiAudioTranscriber
 */
//------------------------------------------------------------------------------
std::unique_ptr<IApiAudioTranscriber> makeApiAudioTranscriber( IApiAudioHttpTransport &http)
{
	return std::make_unique<ApiAudioTranscriber>( http) ;
}
} // namespace koefude
