//------------------------------------------------------------------------------

/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file history.h
 * @brief 再起動後も保持する履歴の所有値と保存形式の境界を定義します
 * @version 1.0.0
 * @date 2026/09/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/10 永続履歴のID・状態・有界な形式を追加
 * 0.1.0 Shunji Ogawa 2026/10/04 形式5へ整形結果と表示意図・試行状態を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 自由全文と独立した表示意図を履歴へ追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1344の明示API補正と独立保存を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1345の動的モデル・凍結単価と旧履歴互換を接続
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1370の文脈分割と明示途中再開を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */

//------------------------------------------------------------------------------

#pragma once

#include "core/speaker_text_record.h"

#include <cstddef>

#include <cstdint>

#include <exception>

#include <filesystem>

#include <memory>

#include <span>

#include <vector>

#include "core/api_audio_transcription.h"

#include "core/api_text_correction.h"

#include "core/pipeline.h"

#include "core/reading_session.h"

#include "core/whole_text_correction.h"

#include "core/whole_text_persistence.h"

#include <stop_token>

namespace koefude {

constexpr std::size_t K_MAXIMUM_HISTORY = 32 ; // 明示削除まで保持する履歴件数の上限

constexpr std::size_t K_MAXIMUM_HISTORY_BYTES = 512ULL * 1024 * 1024 ; // 保存形式の上限（byte）

/** @brief UI言語や起動中のoperationから独立した履歴の状態です */

enum class HistoryState : std::uint32_t {

	/// 待機・実行中。再起動後は未完了として扱う
	PENDING,

	/// 文字起こし結果を確定済み
	COMPLETED,

	/// 本文を保持し、話者分離だけが失敗
	COMPLETED_WITH_WARNINGS,

	/// 処理は終了したが認識本文なし
	NO_TEXT,

	/// 処理失敗。診断分類とcodeを保持
	FAILURE,

	/// 協調的取消しで終了
	CANCELLED,

	/// 前回の処理が完了する前にアプリを終了
	INTERRUPTED

} ;

/** @brief 音声の複製を含まない、一回の文字起こしの所有記録です */

struct HistoryEntry {

	/// 起動と削除を跨いで再利用しない履歴ID
	std::uint64_t						  id = 0 ;

	/// 元音声のpath。復元時には音声を開かない
	std::filesystem::path				  input ;

	/// 永続化する処理結果の分類
	HistoryState						  state = HistoryState::PENDING ;

	/// 失敗・取消し時の分類
	ErrorKind							  error = ErrorKind::INTERNAL ;

	/// 元の診断code
	std::uint32_t						  code = 0 ;

	/// 本文・名前・時間を持つ共有不変結果
	std::shared_ptr<const Transcript>	  result ;

	/// 0は全話者、-1は不明、-2は混在、正数は話者ID
	int									  filter = 0 ;

	/// V8の音声API原文。文章改善採用とは別
	std::shared_ptr<const ApiAudioRecord> apiAudio ;

	/// V7には符号化せず専用sidecarに保存
	ApiTextCorrectionRecord				  apiText ;

	/// 有効全文・最新prefix・予約を独立保存
	WholeTextBoundedRecord				  bounded ;

	/// 最後の有効全文と非再開の試行状態
	WholeTextCorrectionRecord			  wholeText ;

	/// 通常表示の意図
	ResultPreference					  resultPreference = ResultPreference::AUTOMATIC ;

	/// 原文と別に保持する整形結果・表示意図・試行状態
	ReadingRecord						  reading ;

	/// 独立sidecar。V8/rawの符号化対象にしない
	SpeakerTextRecord					  speakerText ;
} ;

/** @brief 最大32件の履歴とIDの払い出し位置を所有します */

struct HistorySnapshot {

	/// 削除後も減らさない次の履歴ID。最大値では新規受付を停止
	std::uint64_t			  nextId = 1 ;

	/// 登録順の記録。本文はimmutable DTOを共有する
	std::vector<HistoryEntry> entries ;

	/// 起動を跨いで再利用しないoperation floor
	std::uint64_t			  nextOperationId = 1 ;

	/// 原文は保持しAPI保存だけを拒否
	bool					  apiSidecarCorrupt = false ;

	/// V7確定後のsidecar後半未完
	bool					  apiSidecarSyncFailed = false ;

	/// 原文を保持し、破損sidecarの通常上書きを拒否
	bool					  speakerTextCorrupt = false ;
} ;

/** @brief 音声の処理失敗と分けて表示する履歴保存の異常です */

enum class HistoryErrorKind {

	/// 履歴fileの読み込みに失敗
	READ,

	/// 一時保存・flush・置換に失敗
	WRITE,

	/// 不正な内容・SHA-256・整合しないID
	CORRUPT,

	/// このアプリが読み込めない保存形式
	VERSION,

	/// 同じ履歴を別の起動が使用中
	BUSY,

	/// ユーザー用local保存先の条件に不適合
	LOCATION,

	/// 件数・本文・保存byteの上限超過
	TOO_LARGE,

	/// 予期しない内部異常
	INTERNAL

} ;

/** @brief 本文・pathを含めず、履歴workerの失敗を伝えます */

class HistoryError final : public std::exception {

	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 履歴の失敗分類と診断codeを保持します
	 * @param[in] kind 利用者向けの説明に使う分類
	 * @param[in] code 元のWin32等のcode。該当なしは0
	 * @param[in] mainCommitted V7置換後のsidecar失敗だけtrue
	 */
	//------------------------------------------------------------------------------

	explicit HistoryError( HistoryErrorKind kind, std::uint32_t code = 0,

						  bool mainCommitted = false) noexcept

		: kind_( kind), code_( code), mainCommitted_( mainCommitted)

	{
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 失敗分類を返します
	 * @return 構築時の分類
	 */
	//------------------------------------------------------------------------------

	[[nodiscard]] HistoryErrorKind kind() const noexcept

	{

		return kind_ ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 元の診断codeを返します
	 * @return 構築時のcode
	 */
	//------------------------------------------------------------------------------

	[[nodiscard]] std::uint32_t code() const noexcept

	{

		return code_ ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief V7置換済みの後半失敗を返します
	 * @return main履歴が確定済み
	 */
	//------------------------------------------------------------------------------

	[[nodiscard]] bool mainCommitted() const noexcept

	{

		return mainCommitted_ ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 秘密情報を含まない説明を返します
	 * @return 静的文字列の非所有pointer
	 */
	//------------------------------------------------------------------------------

	[[nodiscard]] const char *what() const noexcept override

	{

		return "KoeFude history failed" ;
	}

	private:
	/// 履歴I/Oに限定した失敗分類
	HistoryErrorKind kind_ ;

	/// OS・形式検証の診断値
	std::uint32_t	 code_ ;

	/// sidecar失敗で原文正本を巻き戻さない確定値
	bool			 mainCommitted_ = false ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 所有履歴を検証してversion付きの保存byte列へ変換します
 * @param[in] snapshot 同期呼出し中だけ参照する履歴
 * @return 512MiB以下の所有byte列。SHA-256とfile I/Oはstorageの責務
 * @throws HistoryError 上限超過、不正ID・状態・文字・時刻
 */
//------------------------------------------------------------------------------

[[nodiscard]] std::vector<std::uint8_t> encodeHistory( const HistorySnapshot &snapshot) ;

//------------------------------------------------------------------------------
/**
 * @brief 同じfield writerの計数sinkで保存長を測ります
 * @param[in] snapshot 全履歴
 * @return SHA32を除くchecked byte数
 */
//------------------------------------------------------------------------------

[[nodiscard]] std::uint64_t				measureEncodedHistory( const HistorySnapshot &snapshot) ;

//------------------------------------------------------------------------------
/**
 * @brief 外部の保存byte列を検証して所有履歴へ変換します
 * @param[in] bytes 呼出し中だけ有効な512MiB以下の保存byte列
 * @return 検証済みの履歴。不正時に部分結果を返しません
 * @throws HistoryError 不正形式、未知version、上限超過
 * @note 元音声・モデルは開かず、pathは表示用の値としてだけ復元します
 */
//------------------------------------------------------------------------------

[[nodiscard]] HistorySnapshot			decodeHistory( std::span<const std::uint8_t> bytes) ;

/** @brief 専用workerから同期実行し、fakeへ差し替え可能な履歴storageです */

class IHistoryStorage {

	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief storageが所有する排他・file resourceを解放します
	 */
	//------------------------------------------------------------------------------

	virtual ~IHistoryStorage() = default ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 保存先の排他を取得して履歴を読み込みます
	 * @return 保存fileがない場合は空履歴。それ以外は検証済みの所有結果
	 * @throws HistoryError 既存履歴は失敗時も変更しません
	 * @note 一つの専用workerが最初に一度だけ呼び出します
	 */
	//------------------------------------------------------------------------------

	[[nodiscard]] virtual HistorySnapshot load() = 0 ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 前の有効な保存を保持しながら履歴を置き換えます
	 * @param[in] snapshot 呼出し中だけ有効な所有結果のborrow
	 * @throws HistoryError 失敗時は前の有効fileを保持します
	 * @pre 同じworkerでloadに成功していること
	 */
	//------------------------------------------------------------------------------

	virtual void						  save( const HistorySnapshot &snapshot) = 0 ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 音声API終端だけを取消しcheckpoint付きで保存します
	 * @param[in] snapshot 同じwriterの候補
	 * @param[in] stop 完了置換前の取消し
	 * @param[out] receipt 原子置換直後の事実
	 * @note 既存local/文章保存はsaveのままです
	 */
	//------------------------------------------------------------------------------

	virtual void						  saveAudio( const HistorySnapshot &snapshot, std::stop_token stop,

													HistorySaveReceipt &receipt)

	{

		checkCancelled( stop) ;

		save( snapshot) ;

		receipt.committed = true ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 同じworker/lockでAPI sidecarだけを保存します
	 * @param[in] snapshot 同じ原文正本
	 */
	//------------------------------------------------------------------------------

	virtual void saveApiText( const HistorySnapshot &snapshot)

	{

		static_cast<void>( snapshot) ;

		throw HistoryError( HistoryErrorKind::WRITE) ; // 未対応storageはHTTP前に拒否
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 同じwriter/lockで独立投影だけを原子保存します
	 * @param[in] snapshot durable候補
	 * @param[in] stop 置換直前取消し
	 * @param[out] receipt 原子置換直後の事実
	 */
	//------------------------------------------------------------------------------

	virtual void saveSpeakerText( const HistorySnapshot &snapshot, std::stop_token stop,

								 HistorySaveReceipt &receipt)

	{

		static_cast<void>( snapshot) ;

		static_cast<void>( stop) ;

		static_cast<void>( receipt) ;

		throw HistoryError( HistoryErrorKind::WRITE) ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ排他でmetadata cacheを読みます
	 * @return 未取得は空
	 */
	//------------------------------------------------------------------------------

	[[nodiscard]] virtual ApiModelCatalog loadCatalog()

	{

		return {} ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief metadata専用cacheを書きます
	 * @param[in] catalog 検証済み値
	 */
	//------------------------------------------------------------------------------

	virtual void saveCatalog( const ApiModelCatalog &catalog)

	{

		static_cast<void>( catalog) ;

		throw HistoryError( HistoryErrorKind::WRITE) ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 生成前のPendingとquotaを耐久予約します
	 * @param[in,out] durable workerの正本
	 * @param[in] request 固定要求
	 * @param[in] stop 協調停止
	 * @return 一回予約。未対応fakeは拒否します
	 */
	//------------------------------------------------------------------------------

	[[nodiscard]] virtual WholeTextBoundedStorageBudget

	reserveBounded( HistorySnapshot &durable, const WholeTextCorrectionJobRequest &request,

				   std::stop_token stop) ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 同一terminalの保存を原子的に確定します
	 * @param[in,out] durable 正本
	 * @param[in,out] terminal 失敗時も非消費
	 * @param[out] receipt 置換直後のscalar
	 * @param[in] stop precommit停止
	 * @return 所有ack。保存前例外では旧結果を保持します
	 */
	//------------------------------------------------------------------------------

	[[nodiscard]] virtual std::shared_ptr<const WholeTextBoundedPersistenceCompletion>

	persistBounded( HistorySnapshot &durable, WholeTextBoundedTerminal &terminal,

				   HistorySaveReceipt &receipt, std::stop_token stop) ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 事前確保した未保存prefix ackを返します
	 * @param[in] kind 保存失敗分類
	 * @param[in] code 元code
	 * @return allocationなしのack/null
	 */
	//------------------------------------------------------------------------------

	[[nodiscard]] virtual std::shared_ptr<const WholeTextBoundedPersistenceCompletion>

	boundedFailure( HistoryErrorKind kind, std::uint32_t code) noexcept ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 明示した未完試行だけを削除します
	 * @param[in,out] durable 正本
	 * @param[in] source 元key
	 * @param[in] operationId 元op
	 * @param[out] receipt commit値
	 * @return cleanupと保存を独立した結果
	 */
	//------------------------------------------------------------------------------

	[[nodiscard]] virtual WholeTextBoundedCleanupResult

	discardBounded( HistorySnapshot &durable, const ReadingSourceKey &source,

				   std::uint64_t operationId, HistorySaveReceipt &receipt) ;
} ;

} // namespace koefude
