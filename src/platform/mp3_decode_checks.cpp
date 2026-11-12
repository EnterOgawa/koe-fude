//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file mp3_decode_checks.cpp
 * @brief MP3のnative属性と実出力数の不整合を拒否します
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 COMなしで検査できるMP3停止条件を追加
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
#include "platform/mp3_decode_checks.h"

#include <cstdint>

#include "core/types.h"

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief 検証済みframeとMFのnative属性を照合します
 * @param[in] info parser成功時のmetadata。呼出し中だけ借用します
 * @param[in] rate MFが解釈したsample周波数（Hz）
 * @param[in] channels MFが解釈したchannel数
 * @param[in] align 圧縮streamのblock alignment（byte）
 * @throws OperationError 不一致はDecodeFailed。入力metadataを変更しません
 * @note Audio major typeとMP3 subtypeはCOM境界で別途検証します
 * @see koefude::validateMp3NativeFormat
 */
//------------------------------------------------------------------------------
void validateMp3NativeFormat( const Mp3InputInfo &info, const std::uint32_t rate, const std::uint32_t channels,
							 const std::uint32_t align)
{
	// 欠落した属性や別の解釈をPCM変換へ渡さない
	if( rate != info.rate || channels != info.channels || align != 1) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 追記予定の実PCM数がnominalより過大でないことを確認します
 * @param[in] info parser成功時のmetadata。呼出し中だけ借用します
 * @param[in] actual 追記後の16kHz mono sample数。途中の0を許可します
 * @throws OperationError nominalより1,600 sampleを超える場合InvalidTimeline
 * @note 大小で減算を選び、nominalへの加算overflowを避けます
 * @see koefude::validateMp3OutputUpperBound
 */
//------------------------------------------------------------------------------
void validateMp3OutputUpperBound( const Mp3InputInfo &info, const std::uint64_t actual)
{
	// 途中の短い出力は許し、上側だけを追記前に照合する
	if( actual > info.nominalOutputSamples && actual - info.nominalOutputSamples > 1600) {
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 終端の実PCM数をdelay/paddingを含む保守的な停止条件へ照合します
 * @param[in] info parser成功時のmetadata。呼出し中だけ借用します
 * @param[in] actual 成功候補の16kHz mono sample数。空出力は許可しません
 * @throws OperationError 空PCM/不正属性はDecodeFailed、数差超過はInvalidTimeline
 * @note 不足許容はceil((8190 + frame_samples) * 16000 / rate) + 1600です
 * gapless補正や音声payload/許容内の欠落を保証する条件ではありません
 * @see koefude::validateMp3OutputSamples
 */
//------------------------------------------------------------------------------
void validateMp3OutputSamples( const Mp3InputInfo &info, const std::uint64_t actual)
{
	// 取消し/空streamを短い成功音声へ読み替えない
	if( actual == 0 || (info.version != 1 && info.version != 2)) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	const std::uint64_t frameSamples = info.version == 1 ? 1152 : 576 ; // 元rateのframe sample数
	const auto			deficit = mp3NominalOutputSamples( 8190 + frameSamples, info.rate) + 1600 ; // 固定不足許容数
	validateMp3OutputUpperBound( info, actual) ;
	// nominal以下の場合だけ減算し、符号なしのunderflowを避ける
	if( actual < info.nominalOutputSamples && info.nominalOutputSamples - actual > deficit) {
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
}

} // namespace koefude
