//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file parakeet_ctc.h
 * @brief 同じFP32 Parakeetから候補比較用CTC出力を取得します
 * @version 1.0.0
 * @date 2026/09/11
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/11 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/11 固定前処理・CUDA/CPU・有効frameの直接推論を追加
 * 0.1.0 Shunji Ogawa 2026/09/12 用語context構築へ検証済み本文語彙を借用可能にする
 * 0.1.0 Shunji Ogawa 2026/09/20 有効長を変えず全出力を観測する開発用APIを追加
 * 0.1.0 Shunji Ogawa 2026/09/20 同じ前処理のmelと正規化値を所有する観測境界を追加
 * 0.1.0 Shunji Ogawa 2026/09/20 正規化だけをfloat64で比較する明示APIを追加
 * 0.1.0 Shunji Ogawa 2026/09/20 logの加算floorだけを比較する明示APIを追加
 * 0.1.0 Shunji Ogawa 2026/09/20 窓中心だけを80から0 sampleへ移す明示比較を追加
 * 0.1.0 Shunji Ogawa 2026/09/21 Hann窓の周期性だけを変更する明示比較を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 改行をLFへ統一
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415のoperator括弧と文書lambdaの書式を補修
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

#include "core/ctc_decoder.h"
#include "core/engines.h"

namespace koefude {

/** @brief 開発診断用の一窓の前処理です。全配列は80×frameのbin優先順です */
struct ParakeetFeatureObservation {
	/// log前の非負mel。同じ設定の別抽出で観測する
	std::vector<float> mel ;
	/// 実推論の正規化直前の値
	std::vector<float> logMel ;
	/// 実推論へ渡す値。呼出し元が所有する
	std::vector<float> normalized ;
	/// 10ms単位の特徴量frame数
	std::size_t		   frames = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief モデルを読まず、実推論と同じ前処理の全値を観測します
 * @param[in] samples 呼出し中だけ借用する16kHz mono PCM。有限の[-1,1]で1280〜480000 sample
 * @param[in] stop 抽出前後とframe処理中に確認する取消し
 * @return 一窓だけの所有配列。失敗時は部分観測を返しません
 * @throws OperationError 不正入力・非有限出力・取消し
 * @note 開発診断専用です。製品の前処理設定を変更しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] ParakeetFeatureObservation observeParakeetFeatures( std::span<const float> samples, std::stop_token stop) ;

//------------------------------------------------------------------------------
/**
 * @brief 正規化の演算だけをfloat64へ変えた開発用特徴量を観測します
 * @param[in] samples 呼出し中だけ借用する有限な[-1,1]の16kHz mono PCM。1280〜480000 sample
 * @param[in] stop 抽出前後と正規化中に確認する取消し
 * @return 最終値をfloat32へ戻した所有観測。失敗時に部分値を返しません
 * @throws OperationError 不正入力・非有限値・取消し
 * @note 分散分母Tと1e-5Fの値、mel/log・窓の全設定を維持する比較専用APIです
 */
//------------------------------------------------------------------------------
[[nodiscard]] ParakeetFeatureObservation observeParakeetFeaturesFloat64( std::span<const float> samples,
																		std::stop_token		   stop) ;

//------------------------------------------------------------------------------
/**
 * @brief logだけを加算floorへ変更した開発用特徴量を観測します
 * @param[in] samples 呼出し中だけ借用する有限な[-1,1]の16kHz mono PCM。1280〜480000 sample
 * @param[in] stop 抽出前後とframe処理中に確認する取消し
 * @return 成功時だけ返す一窓の所有観測
 * @throws OperationError 不正入力・非有限値・取消し
 * @note log(mel + 2^-24)以外の設定と旧float32正規化を維持する比較専用APIです
 */
//------------------------------------------------------------------------------
[[nodiscard]] ParakeetFeatureObservation observeParakeetFeaturesLogadd( std::span<const float> samples,
																	   std::stop_token		  stop) ;

//------------------------------------------------------------------------------
/**
 * @brief 同じ切出し実装で中心80の再現と中心0だけの変更を観測します
 * @param[in] samples 借用する16kHz mono PCM。有限な[-1,1]で1280〜480000 sample
 * @param[in] zeroCenter trueは中心0、falseは中心80の旧対照再現
 * @param[in] stop 前処理とframeごとに確認する取消し
 * @return 成功時だけ返す一窓の所有観測
 * @throws OperationError 入力不正・非有限出力・取消し
 * @note log・正規化・padding・有効長を変えない開発比較専用APIです
 */
//------------------------------------------------------------------------------
[[nodiscard]] ParakeetFeatureObservation observeParakeetFeaturesCenter( std::span<const float> samples, bool zeroCenter,
																	   std::stop_token stop) ;

//------------------------------------------------------------------------------
/**
 * @brief Hann窓の周期性だけを切り替えた開発用特徴量を観測します
 * @param[in] samples 借用する16kHz mono PCM。有限な[-1,1]で1280〜480000 sample
 * @param[in] periodic trueは現行の分母400、falseは非周期窓の分母399
 * @param[in] stop 抽出前後とframeごとに確認する取消し
 * @return 成功時だけ返す一窓の所有観測
 * @throws OperationError 不正入力・非有限出力・取消し
 * @note 中心80・reflection・現行log・float32正規化・frame数を維持する比較専用APIです
 */
//------------------------------------------------------------------------------
[[nodiscard]] ParakeetFeatureObservation observeParakeetFeaturesHann( std::span<const float> samples, bool periodic,
																	 std::stop_token stop) ;

/** @brief 一窓のCTC log確率と音声位置です。全出力観測時は未判定の末尾も含みます */
struct ParakeetCtcOutput {
	/// 有効frame×3073。処理後に呼出し元が解放する
	std::vector<float>			logProbabilities ;
	/// 10ms単位の前処理frame数
	std::size_t					featureFrames = 0 ;
	/// 保持する80ms単位のframe数。全出力観測時はshapeの長さ
	std::size_t					frames = 0 ;
	/// 元file基準での窓の先頭sample
	std::uint64_t				firstSample = 0 ;
	/// 元音声窓のsample数。最大30秒
	std::size_t					sampleCount = 0 ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 所有出力をdecoderへ借用します
	 * @return 本objectの寿命中だけ有効なview
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] CtcMatrixView view() const noexcept ;
} ;

/**
 * @brief 固定モデルとCUDA/CPU sessionをworkerだけで所有します
 * @note 構築から破棄まで同じworkerで使用し、並行呼出しを禁止します
 * 一回のGPU試行を表す型で、CPU再試行は呼出し元の既存retry契約で行います
 */
class ParakeetCtcSession final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief registryのFP32 graph・外部weight・tokenを検証して推論を準備します
	 * @param[in] options Parakeet Quality、CPUまたはCUDAを明示した設定
	 * @param[in] useTf32 CUDAの対象演算でTF32を許可する場合true
	 * @param[in] stop 検証と初期化の前後に確認する取消し
	 * @throws OperationError モデル不正、GPU予算不足、初期化失敗、取消し
	 */
	//------------------------------------------------------------------------------
	ParakeetCtcSession( const EngineOptions &options, bool useTf32, std::stop_token stop) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief sessionを先に解放し、その後にモデルの所有lockを閉じます
	 */
	//------------------------------------------------------------------------------
	~ParakeetCtcSession() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief worker専有のcontextの複製を禁止します
	 * @param[in] other 複製元
	 */
	//------------------------------------------------------------------------------
	ParakeetCtcSession( const ParakeetCtcSession &other) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有contextの代入を禁止します
	 * @param[in] other 代入元
	 * @return 利用不可
	 */
	//------------------------------------------------------------------------------
	ParakeetCtcSession			   &operator=( const ParakeetCtcSession &other) = delete ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ前処理で一つのPCM窓を推論し、全語彙のCTC出力を所有copyします
	 * @param[in] samples 有限な[-1,1]の16kHz mono PCM。80ms～30秒
	 * @param[in] firstSample 元fileの先頭位置。返す時刻の基準で、音声は変更しません
	 * @param[in] stop 前処理と同期推論の前後で確認する取消し
	 * @return 有効frameだけの所有log確率。失敗時は部分結果を返しません
	 * @throws OperationError 入力・出力shape不正、推論失敗、取消し
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] ParakeetCtcOutput infer( std::span<const float> samples, std::uint64_t firstSample,
										  std::stop_token stop) ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 開発診断用に同じ推論の全出力shapeを所有copyします
	 * @param[in] samples 有限な[-1,1]の16kHz mono PCM。80ms～30秒
	 * @param[in] firstSample 元fileの先頭sample。音声を変更しません
	 * @param[in] stop 前処理と同期推論の前後で確認する取消し
	 * @return 最大512 frameの全出力。末尾が有効音声かpaddingかは未判定です
	 * @throws OperationError 入力・出力shape不正、非有限確率、推論失敗、取消し
	 * @note 同じworkerの開発比較だけで使用し、製品経路から呼びません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] ParakeetCtcOutput inferObserved( std::span<const float> samples, std::uint64_t firstSample,
												  std::stop_token stop) ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 正規化だけfloat64で計算し、従来のfloor長でCTCを比較します
	 * @param[in] samples 借用する有限な[-1,1]の16kHz mono PCM。80ms〜30秒
	 * @param[in] firstSample 元fileの先頭sample。返却時刻の基準
	 * @param[in] stop 特徴生成と同期推論の前後で確認する取消し
	 * @return 最終float32特徴を同じモデルへ渡した有効frameの所有log確率
	 * @throws OperationError 入力・出力shape不正、推論失敗、取消し
	 * @note 開発比較専用で、モデル・log・窓・epsilon・分散分母を変更しません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] ParakeetCtcOutput inferFloat64( std::span<const float> samples, std::uint64_t firstSample,
												 std::stop_token stop) ;

	//------------------------------------------------------------------------------
	/**
	 * @brief logの加算floorだけを変え、有効長floorのCTCを明示比較します
	 * @param[in] samples 呼出し中だけ借用する16kHz mono PCM。1280〜480000 sample
	 * @param[in] firstSample 元file基準の窓先頭sample
	 * @param[in] stop 前処理と推論の前後に確認する取消し
	 * @return 所有する有限なCTC行列。失敗時に部分結果を返しません
	 * @throws OperationError 不正入力・時刻・取消し・推論失敗
	 * @note 同じモデル・旧float32正規化を使う開発専用APIです
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] ParakeetCtcOutput inferLogadd( std::span<const float> samples, std::uint64_t firstSample,
												std::stop_token stop) ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 窓中心だけを0 sampleへ変更し同じCTC floor長で明示比較します
	 * @param[in] samples 借用する有限な[-1,1]の16kHz mono PCM。1280〜480000 sample
	 * @param[in] firstSample 元file基準の窓先頭sample。音声と返却時刻の基準は不変
	 * @param[in] stop 前処理と推論の前後で確認する取消し
	 * @return 一窓の所有CTC行列。失敗時は部分結果を返しません
	 * @throws OperationError 不正入力・時刻・取消し・推論失敗
	 * @note 現行clamp・float32正規化・モデル・時刻換算を維持する開発専用APIです
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] ParakeetCtcOutput inferCenterZero( std::span<const float> samples, std::uint64_t firstSample,
													std::stop_token stop) ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 窓形状だけを非周期Hannへ変更して同じCTC有効長で比較します
	 * @param[in] samples 借用する有限な[-1,1]の16kHz mono PCM。1280〜480000 sample
	 * @param[in] firstSample 元file基準の窓先頭sample。音声と時刻の基準は不変
	 * @param[in] stop 前処理と同期推論の前後で確認する取消し
	 * @return 一窓の所有CTC行列。失敗時は部分結果を返しません
	 * @throws OperationError 不正入力・時刻・取消し・推論失敗
	 * @note 中心80・現行log・正規化・モデル・時刻換算を維持する開発専用APIです
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] ParakeetCtcOutput inferHannSymmetric( std::span<const float> samples, std::uint64_t firstSample,
													   std::stop_token stop) ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 元語彙のtoken列と発火位置を従来と同じ本文DTOへ戻します
	 * @param[in] tokens 順序付き非blank ID。512個以内
	 * @param[in] frames 各tokenの相対発火frame。tokensと同数
	 * @param[in] firstSample 元音声窓の先頭sample
	 * @param[in] sampleCount 元音声窓のsample数
	 * @return 元file基準の時刻・本文・日本語境界。空tokenなら空本文の区間です
	 * @throws OperationError 不正ID、時刻不正、UTF-8不正
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] TextSegment		segment( std::span<const std::int32_t> tokens, std::span<const std::size_t> frames,
											std::uint64_t firstSample, std::size_t sampleCount) const ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 実sessionのprovider設定を診断へ返します
	 * @return 秘密情報やpathのない設定文字列
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::string		providerSettings() const ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 用語比較へ既存本文生成と同じ検証済み語彙を借用します
	 * @return ID順の本文piece。blankを含み、本sessionの寿命内だけ有効です
	 * @note 同じworkerの同期呼出し専用です。用語contextは構築中に所有copyします
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::span<const std::string> tokenPieces() const noexcept ;

	private:
	class Impl ; // ORTとモデルlockを公開headerへ漏らさない実装
	/// workerの寿命内で所有する一つのsession
	std::unique_ptr<Impl> implementation_ ;
} ;

} // namespace koefude
