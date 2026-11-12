//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file transcription_settings.h
 * @brief 適用済み推論選好だけを専用workerで保存・復元します
 * @version 1.0.0
 * @date 2026/11/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * --（未記録） Shunji Ogawa 2026/11/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1375の有限DTOと保存ACKを追加
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1378の明示更新と設定ACKを接続
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
#include "core/engines.h"
#include "core/history.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
namespace koefude {
/** @brief runtime pathや秘密を含まない、制約適用前の明示選好です */
struct TranscriptionPreferences {
	/// registryの系統
	AsrModel	  model = AsrModel::PARAKEET ;
	/// 系統内品質
	AsrProfile	  profile = AsrProfile::QUALITY ;
	/// CPU専用構成中にも保つAuto/CPU
	Backend		  backend = Backend::AUTO ;
	/// 日本語専用構成中にも保つ多言語選好
	bool		  autoLanguage = false ;
	/// 明示VAD
	bool		  speechFilter = true ;
	/// 明示話者分離
	bool		  diarization = true ;
	/// 自動0、任意1〜20
	int			  expectedSpeakers = 0 ;
	/// 明示人数推定方式
	SpeakerMethod method = SpeakerMethod::SPECTRAL ;
	/// 起動一回の固定GitHub確認
	bool		  checkUpdates = true ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 全選好を比較します
	 * @param[in] other 比較先
	 * @return 全値一致
	 */
	//------------------------------------------------------------------------------
	bool		  operator==( const TranscriptionPreferences &other) const = default ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief EngineOptionsの公開選好だけを抽出します
 * @param[in] options 制約前の値
 * @return runtime情報を含まない値
 */
//------------------------------------------------------------------------------
[[nodiscard]] TranscriptionPreferences transcriptionPreferences( const EngineOptions &options) ;
//------------------------------------------------------------------------------
/**
 * @brief 選好を既定の実行DTOへ戻します
 * @param[in] value 明示選好
 * @return pathなしの値
 */
//------------------------------------------------------------------------------
[[nodiscard]] EngineOptions			   transcriptionOptions( const TranscriptionPreferences &value) ;
//------------------------------------------------------------------------------
/**
 * @brief registry/enum/人数範囲を確認します
 * @param[in] value 選好
 * @return 全値が適格
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool					   validTranscriptionPreferences( const TranscriptionPreferences &value) noexcept ;
/** @brief 同期I/Oの単独所有契約です。UIから直接呼びません */
class ITranscriptionSettingsStorage {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief storage資源をworkerで解放します
	 */
	//------------------------------------------------------------------------------
	virtual ~ITranscriptionSettingsStorage() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 一度だけ読込み/lockを行います
	 * @return missingは従前既定
	 * @throws HistoryError 破損/読込み/lock失敗。原fileを変更しません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual TranscriptionPreferences load() = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 原子保存とflushを完了します
	 * @param[in] value 検証済み選好
	 * @throws HistoryError 保存失敗。置換前なら旧fileを維持します
	 */
	//------------------------------------------------------------------------------
	virtual void								   save( const TranscriptionPreferences &value) = 0 ;
} ;
/** @brief 一件のload/save終端だけを返す小ACKです */
struct TranscriptionSettingsEvent {
	/// 0は起動load、他は単調save ID
	std::uint64_t			 operation = 0 ;
	/// 正常完了だけtrue
	bool					 succeeded = false ;
	/// 成功時の保存/復元選好
	TranscriptionPreferences value ;
	/// 固定失敗分類
	HistoryErrorKind		 error = HistoryErrorKind::INTERNAL ;
	/// OS診断値、本文なし
	std::uint32_t			 code = 0 ;
} ;
/**
 * @brief 単一I/O・待機要求一件・ACK一件を所有する非同期設定controllerです
 * @note UIは非blocking APIだけを利用し、stopped後に破棄します
 */
class TranscriptionSettingsController final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有storageのloadを開始します
	 * @param[in] storage 非NULLの同期storage
	 * @param[in] notifier mutex外の起床通知。空可
	 */
	//------------------------------------------------------------------------------
	TranscriptionSettingsController( std::unique_ptr<ITranscriptionSettingsStorage> storage,
									std::function<void()>						   notifier = {}) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 受理済み保存をdrainしてjoinします。UIはstopped後に呼びます
	 */
	//------------------------------------------------------------------------------
	~TranscriptionSettingsController() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 単一設定storage/workerと一要求・一ACK・通知先の対応を複製しないため、copy構築を禁止します
	 * @note 名前のない第1引数（const TranscriptionSettingsController &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	TranscriptionSettingsController( const TranscriptionSettingsController &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 単一設定storage/workerと一要求・一ACK・通知先の対応を複製しないため、copy代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const TranscriptionSettingsController &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	TranscriptionSettingsController						   &operator=( const TranscriptionSettingsController &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 未読込/処理中/ACK待ちは拒否します
	 * @param[in] value 明示選好
	 * @return 新ID、拒否は0。自動retryなし
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t								save( TranscriptionPreferences value) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief ACKを一回だけ取得します
	 * @return 未更新はnullopt
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::optional<TranscriptionSettingsEvent> takeEvent() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 新規受付を止め、受理済み保存を完了して終了します
	 */
	//------------------------------------------------------------------------------
	void													requestStop() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief storage解放後の終端を確認します
	 * @return 終了時true
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool										stopped() const noexcept ;

	private:
	//------------------------------------------------------------------------------
	/**
	 * @brief UIへ借用resourceを渡さずI/Oを直列実行します
	 */
	//------------------------------------------------------------------------------
	void										   run() noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief load/saveと条件待機を単一threadで実行します
	 * @throws std::exception 同期基盤失敗
	 */
	//------------------------------------------------------------------------------
	void										   runIo() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 失敗してもI/O確定を巻き戻さない起床通知を行います
	 */
	//------------------------------------------------------------------------------
	void										   notify() noexcept ;
	/// workerだけが使用/解放
	std::unique_ptr<ITranscriptionSettingsStorage> storage_ ;
	/// HWNDを使うなら所有者がstoppedまで生存
	std::function<void()>						   notifier_ ;
	/// 小DTO/flagの短い移動だけ
	std::mutex									   mutex_ ;
	/// workerだけの待機
	std::condition_variable						   condition_ ;
	/// 最大一要求
	std::optional<TranscriptionSettingsEvent>	   pending_ ;
	/// 最大一ACK
	std::optional<TranscriptionSettingsEvent>	   event_ ;
	/// save ID、0はload
	std::uint64_t								   next_ = 1 ;
	/// load成功時だけ受付
	bool										   ready_ = false ;
	/// load/saveの終端までtrue
	bool										   busy_ = true ;
	/// 既存saveはdrain
	bool										   stopping_ = false ;
	/// storage解放後に公開
	std::atomic<bool>							   stopped_ = false ;
	/// 基盤失敗0なし/1未受領/2受領、mutexなしの診断
	std::atomic<unsigned>						   fatal_ = 0 ;
	/// 他member構築後に開始
	std::jthread								   worker_ ;
} ;
} // namespace koefude
