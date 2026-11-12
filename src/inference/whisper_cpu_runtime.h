//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whisper_cpu_runtime.h
 * @brief 補助Whisper専用のCPU選択と同期module所有権を定義します
 * @version 1.0.0
 * @date 2026/10/21
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/21 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1301の補助専用host境界を追加
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
#include "../../native/whole_text/cpu_candidate_contract.h"
#include "core/correction_diagnostics.h"
#include "inference/model_store.h"
#include "inference/whisper_cpu_abi.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <stop_token>
#include <string_view>

namespace koefude::whisper_cpu {
/** @brief CMakeで完成資産へ固定した値です。文字列は同期scopeより長く生存します */
struct Pin {
	/// falseはportable専用buildです
	bool			 present = false ;
	/// DLLの全byte数
	std::uint64_t	 bytes = 0 ;
	/// DLLのSHA-256
	std::string_view dllSha ;
	/// 固定receipt全byteのSHA-256
	std::string_view receiptSha ;
	/// 私有ABI headerのSHA-256
	std::string_view abiSha ;
	/// Debug又はRelease
	std::string_view configuration ;
} ;
/** @brief 同期worker内だけで借用する、CPU/資産/loaderの最小seamです */
struct Services {
	/// baseline照会、一回
	std::function<whole_text::cpu_candidate::Snapshot()>													query ;
	/// write/delete拒否lease
	std::function<std::shared_ptr<const void>( const std::filesystem::path &, const Pin &, std::stop_token)> verify ;
	/// 絶対path限定、一参照を所有
	std::function<void *(const std::filesystem::path &)>													load ;
	/// module生存中の同期borrow
	std::function<GetApiV1( void *)>																			getter ;
	/// 同じ参照を一回解放、非成立はfalse
	std::function<bool( void *)>																				close ;
	/// 通常はEXE隣の固定basename
	std::filesystem::path																					path ;
	/// compiled slot
	Pin																										pin ;
	/// 実依存だけcleanup失敗後の同module再利用を拒否
	bool enforceProcessLifetime = false ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 現buildのcompiled pinを返します
 * @return 所有文字列のない固定値
 */
//------------------------------------------------------------------------------
[[nodiscard]] Pin		   compiledPin() noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 実CPUと固定EXE隣資産を使います
 * @return 同期worker所有のcallback群
 */
//------------------------------------------------------------------------------
[[nodiscard]] Services	   productionServices() ;
//------------------------------------------------------------------------------
/**
 * @brief 既存静的Whisperを同じ型の表で呼びます
 * @return process寿命の表
 */
//------------------------------------------------------------------------------
[[nodiscard]] const ApiV1 &portableApi() noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief statusをhost所有例外へ変換します
 * @param[in] status DLLの固定幅値
 * @throws std::bad_alloc OOM
 * @throws OperationError その他
 */
//------------------------------------------------------------------------------
void					   checkStatus( Status status) ;
//------------------------------------------------------------------------------
/**
 * @brief getter直後、初期化前に全metadataと14pointerを検証します
 * @param[in] api module生存中の表
 * @param[in] pin compiled値
 * @throws OperationError 不一致
 */
//------------------------------------------------------------------------------
void					   checkApi( const ApiV1 &api, const Pin &pin) ;
/** @brief lease、module、表、contextを同じ同期scopeで所有します */
class Runtime			   final {
	 public:
	 //------------------------------------------------------------------------------
	 /**
	  * @brief static表を既定とし、実行済み選択だけを記録します
	  * @param[in] portable process又は試験scopeで生存する表
	  * @param[in] diagnostics null可、Runtimeより長く生存する受動capture
	  */
	 //------------------------------------------------------------------------------
	 explicit Runtime( const ApiV1 &portable, CorrectionDiagnosticCapture *diagnostics = nullptr) ;
	 //------------------------------------------------------------------------------
	 /**
	  * @brief 未完了scopeを一回だけ閉じ、失敗は診断に残します
	  */
	 //------------------------------------------------------------------------------
	 ~Runtime() ;
	 //------------------------------------------------------------------------------
	 /**
	  * @brief Whisper context、DLL参照、資産leaseと対応する解放APIを二重所有しないため、copy構築を禁止します
	  * @note 名前のない第1引数（const Runtime &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	  */
	 //------------------------------------------------------------------------------
	 Runtime( const Runtime &) = delete ;
	 //------------------------------------------------------------------------------
	 /**
	  * @brief Whisper context、DLL参照、資産leaseと対応する解放APIを二重所有しないため、copy代入を禁止します
	  * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	  * @note 名前のない第1引数（const Runtime &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	  */
	 //------------------------------------------------------------------------------
	 Runtime				   &operator=( const Runtime &) = delete ;
	 //------------------------------------------------------------------------------
	 /**
	  * @brief CPU gate後だけfast資産を読み込みます
	  * @param[in] services 同期scopeの依存値
	  * @param[in] stop 協調停止
	  * @throws OperationError 資産/ABI/取消し。隠れportable再試行を行いません
	  */
	 //------------------------------------------------------------------------------
	 void						select( Services services, std::stop_token stop) ;
	 //------------------------------------------------------------------------------
	 /**
	  * @brief 選択した同module表を借用します
	  * @return Runtime生存中の表
	  */
	 //------------------------------------------------------------------------------
	 [[nodiscard]] const ApiV1 &api() const noexcept ;
	 //------------------------------------------------------------------------------
	 /**
	  * @brief 初期化で返されたcontextを同表へ一回だけ帰属させます
	  * @param[in] context 同表で作成した所有pointer、null可
	  */
	 //------------------------------------------------------------------------------
	 void						own( whisper_context *context) noexcept ;
	 //------------------------------------------------------------------------------
	 /**
	  * @brief context、module、leaseの順に明示解放します
	  * @throws OperationError free/close失敗。再freeせずmodule参照をprocess終了まで残します
	  */
	 //------------------------------------------------------------------------------
	 void						close() ;

	 private:
	 //------------------------------------------------------------------------------
	 /**
	  * @brief destructorと明示closeに共通の一回cleanupです
	  * @return 全解放成立ならtrue
	  */
	 //------------------------------------------------------------------------------
	 [[nodiscard]] bool			  closeOnce() noexcept ;
	 /// 同期borrow、moduleより短命
	 const ApiV1				 *api_ ;
	 /// 同期borrow、null可
	 CorrectionDiagnosticCapture *diagnostics_ ;
	 /// 検証からmodule解放後までの資産lease
	 std::shared_ptr<const void>  lease_ ;
	 /// loader参照とcompiled pin
	 Services					  services_ ;
	 /// 自分でloadした一参照
	 void						 *module_ = nullptr ;
	 /// 同表だけがfreeする所有値
	 whisper_context			 *context_ = nullptr ;
	 /// cleanupを二回試しません
	 bool						  closed_ = false ;
	 /// 明示closeの成否
	 bool						  closeOk_ = true ;
} ;
} // namespace koefude::whisper_cpu

namespace koefude::detail {
/** @brief 実モデルなしで共有loopを確認する同期依存です */
struct WhisperWindowServices {
	/// 補助専用入口でだけ使用
	whisper_cpu::Services																  runtime ;
	/// nullは既存static表
	const whisper_cpu::ApiV1															 *portable = nullptr ;
	/// モデルlease
	std::function<VerifiedModel( const std::filesystem::path &, ModelId, std::stop_token)> verify ;
	/// nullは既存device選択
	std::function<int( Backend, const AsrDefinition &)>									  gpu ;
} ;
// 二factoryが共用する実closureの型です。既存application seamと同じ契約です
using AuxiliaryWhisperTranscriber =
	std::function<Recognition( const AudioData &, const EngineOptions &, std::span<const AsrSampleWindow>,
							  std::stop_token, const ProgressCallback &, const RecognitionObserver &, AsrTimelineUse)> ;
//------------------------------------------------------------------------------
/**
 * @brief 通常/有界factoryの実補助closureを一つに固定します
 * @param[in] diagnostics null可、closureが同期呼出し終了まで共有所有
 * @param[in] services nullは実依存。非nullはclosure呼出し終了まで生存する内部試験borrow
 * @return 補助専用入口から同じ共有loopへ渡すclosure。一般ASR入口を選びません
 */
//------------------------------------------------------------------------------
[[nodiscard]] AuxiliaryWhisperTranscriber
makeAuxiliaryWhisperTranscriber( std::shared_ptr<CorrectionDiagnosticCapture> diagnostics,
								const WhisperWindowServices					*services = nullptr) ;
} // namespace koefude::detail
