//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file temporary_store.h
 * @brief 長時間処理用の専用一時fileと部分PCMの所有契約です
 * @version 1.0.0
 * @date 2026/09/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/10 時間に比例する作業dataをdelete-on-closeのfileへ分離
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1285の有界逐次補正を接続
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実契約・所有寿命・戻り値の意味を具体化
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string_view>

#include "core/types.h"
#include "core/whole_text_persistence.h"

namespace koefude {

/** @brief 同一workerで逐次使用し、OSが終了時に削除する一時fileです */
class TemporaryFile final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief localの専用ACLで乱数名のfileを作ります
	 * @param[in] directory 空はLocalAppData/koefude/temporary。試験は隔離先を指定できます
	 * @throws OperationError 保存先不正・権限・空き容量の異常
	 */
	//------------------------------------------------------------------------------
	explicit TemporaryFile( const std::filesystem::path &directory = {}) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief handleを閉じ、一時fileをOSへ回収させます
	 */
	//------------------------------------------------------------------------------
	~TemporaryFile() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有fileは複製できません
	 * @note 名前のない第1引数（const TemporaryFile &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	TemporaryFile( const TemporaryFile &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有fileはcopy代入できません
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const TemporaryFile &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	TemporaryFile			   &operator=( const TemporaryFile &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 書込み済みbyte数を返します
	 * @return 成功した追記の総byte数
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t size() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 元のdataの後へ全byteを追加します
	 * @param[in] bytes 同期借用する有限buffer
	 * @throws OperationError 失敗後のfileは利用せず破棄してください
	 */
	//------------------------------------------------------------------------------
	void						append( std::span<const std::byte> bytes) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 有効範囲のbyteを部分読込みします
	 * @param[in] offset file先頭からのbyte数
	 * @param[out] bytes 成功時は全域を設定。失敗時は内容未確定
	 * @throws OperationError 範囲外・I/O異常
	 */
	//------------------------------------------------------------------------------
	void						read( std::uint64_t offset, std::span<std::byte> bytes) const ;

	private:
	class Impl ; // Win32 handleと保存先の実装を隠す所有型
	/// 一時fileの単独所有権
	std::unique_ptr<Impl> impl_ ;
} ;

namespace detail {
/** @brief tiny合成試験専用の同期logical fileです。通常factoryは設定しません */
class WholeTextJournalTestIo {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief fake所有物を解放します
	 */
	//------------------------------------------------------------------------------
	virtual ~WholeTextJournalTestIo() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定extentを論理予約します
	 * @param[in] bytes extent
	 */
	//------------------------------------------------------------------------------
	virtual void create( std::uint64_t bytes) = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同期writeします
	 * @param[in] offset byte位置
	 * @param[in] bytes 全byte
	 */
	//------------------------------------------------------------------------------
	virtual void write( std::uint64_t offset, std::span<const std::uint8_t> bytes) = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同期readします
	 * @param[in] offset 位置
	 * @param[out] bytes 全域
	 */
	//------------------------------------------------------------------------------
	virtual void read( std::uint64_t offset, std::span<std::uint8_t> bytes) = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief logical flushします
	 */
	//------------------------------------------------------------------------------
	virtual void flush() = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief file identityを返します
	 * @return 固定volume/index
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual std::array<std::uint64_t, 2> identity() const = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同一fileのrenameを記録します
	 */
	//------------------------------------------------------------------------------
	virtual void									   rename() = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 既知fileを削除します
	 * @return 0成功/元code
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual std::uint32_t				   remove() noexcept = 0 ;
} ;
} // namespace detail
/** @brief U4が共有所有する履歴予約台帳です。再帰走査は行いません */
struct TransferableJournalQuota {
	/// 予約と返却だけを保護
	std::mutex	  mutex ;
	/// 既存・予約・残存の全byte
	std::uint64_t bytes = 0 ;
	/// 同じ全件数
	std::uint32_t entries = 0 ;
} ;
/** @brief file削除失敗でもquotaを勝手に返さない事前確保ownerです */
struct TransferableJournalCharge {
	/// U4と共有する台帳
	std::shared_ptr<TransferableJournalQuota>		ledger ;
	/// 物理予約byte
	std::uint64_t									bytes = 0 ;
	/// journal数とは別の新規履歴entry数
	std::uint32_t									entryDelta = 0 ;
	/// 元削除code
	std::uint32_t									cleanupCode = 0 ;
	/// 未返却予約
	bool											active = false ;
	/// closeでは返さない耐久charge
	bool											durable = false ;
	/// Reserved ownerと同じ乱数
	WholeTextJournalKey								journalKey{} ;
	/// 内部合成試験だけのlogical file
	std::shared_ptr<detail::WholeTextJournalTestIo> testIo ;
	//------------------------------------------------------------------------------
	/**
	 * @brief file作成前の失敗だけは予約を返します。残存fileは返しません
	 */
	//------------------------------------------------------------------------------
	~TransferableJournalCharge() noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 作成fileがない/削除済みの予約だけを返します
	 */
	//------------------------------------------------------------------------------
	void release() noexcept ;
} ;
/** @brief 元削除codeと残存予約を隠さない値です */
struct TransferableJournalCleanup {
	/// 一fileの削除を確認
	bool		  deleted = false ;
	/// 元OS code
	std::uint32_t error = 0 ;
	/// quotaに残したbyte
	std::uint64_t residualBytes = 0 ;
} ;
/** @brief PCMと別に同一fileをU4へ移動できる単独所有journalです */
class TransferableJournalFile final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief private固定stagingへ一fileを作り物理予約します
	 * @param[in] historyRoot 空なら既定history。試験は自分の隔離root
	 * @param[in] bytes 固定extent
	 * @param[in] charge 事前取得したquota
	 */
	//------------------------------------------------------------------------------
	TransferableJournalFile( const std::filesystem::path &historyRoot, std::uint64_t bytes,
							std::shared_ptr<TransferableJournalCharge> charge) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 未移譲なら自分が作成した一fileだけを削除します
	 */
	//------------------------------------------------------------------------------
	~TransferableJournalFile() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ一journal fileのhandle・quota charge・未移譲時の削除責任を二重所有しないため、copy構築を禁止します
	 * @note 名前のない第1引数（const TransferableJournalFile &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	TransferableJournalFile( const TransferableJournalFile &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ一journal fileのhandle・quota charge・未移譲時の削除責任を二重所有しないため、copy代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const TransferableJournalFile &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	TransferableJournalFile &operator=( const TransferableJournalFile &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じhandleと削除責任を移します
	 * @param[in,out] other 元owner
	 */
	//------------------------------------------------------------------------------
	TransferableJournalFile( TransferableJournalFile &&other) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 旧fileをcleanupして移します
	 * @param[in,out] other 元owner
	 * @return 自身
	 */
	//------------------------------------------------------------------------------
	TransferableJournalFile					  &operator=( TransferableJournalFile &&other) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 予約extent内だけを書きます
	 * @param[in] offset byte位置
	 * @param[in] bytes 同期borrow
	 */
	//------------------------------------------------------------------------------
	void									   write( std::uint64_t offset, std::span<const std::uint8_t> bytes) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 予約extent内だけを読みます
	 * @param[in] offset byte位置
	 * @param[out] bytes 成功時だけ全域有効
	 */
	//------------------------------------------------------------------------------
	void									   read( std::uint64_t offset, std::span<std::uint8_t> bytes) const ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 全先行writeのflush完了を確認します
	 */
	//------------------------------------------------------------------------------
	void									   flush() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有fileだけを明示cleanupします。再試行しません
	 * @return 元結果
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] TransferableJournalCleanup   cleanup() noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同volume history内へcopyなしでrenameします
	 * @param[in] name path構文を含まない新しいASCII名
	 */
	//------------------------------------------------------------------------------
	void									   renameWithinHistory( std::wstring_view name) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同一fileの検証用IDです
	 * @return volumeとfile index
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::array<std::uint64_t, 2> identity() const ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 自分が作成したpathを読みます
	 * @return owner寿命中の値参照
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const std::filesystem::path &path() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定予約byteを読みます
	 * @return 全extent
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t				   size() const noexcept ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 耐久採用後のdestructorをclose-onlyへ切り替えます
	 */
	//------------------------------------------------------------------------------
	void									   retainDurable() noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 元chargeを参照します
	 * @return 共有台帳枠
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::shared_ptr<TransferableJournalCharge> charge() const noexcept ;

	private:
	class Impl ; // privateな同一owner実装
	friend class WholeTextPersistenceStore ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定名と照合済みidentityだけで再openします
	 * @param[in] root 固定root
	 * @param[in] key 元key
	 * @param[in] location staging1/root2
	 * @param[in] identity 期待ID
	 * @param[in] charge 耐久charge
	 * @return 同じ単独file
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] static TransferableJournalFile reopenKnown( const std::filesystem::path &root,
															 const WholeTextJournalKey &key, unsigned location,
															 const std::array<std::uint64_t, 2>		   &identity,
															 std::shared_ptr<TransferableJournalCharge> charge) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief private implを移します
	 * @param[in] impl 検証済みowner
	 */
	//------------------------------------------------------------------------------
	explicit TransferableJournalFile( std::unique_ptr<Impl> impl) ;
	class Impl ; // private ACL/file handle/削除責任
	/// move-only owner
	std::unique_ptr<Impl> impl_ ;
} ;

/** @brief decode時は追記でき、engineへはIAudioSourceとして渡す一時PCMです */
class PcmStorage final : public IAudioSource {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 専用一時PCMを作ります
	 * @param[in] directory 空は既定、試験は隔離先
	 */
	//------------------------------------------------------------------------------
	explicit PcmStorage( const std::filesystem::path &directory = {}) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 全PCMのsample数を返します
	 * @return 16kHz monoの総数
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t size() const noexcept override ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 指定範囲のPCMだけを出力bufferへ読み込みます
	 * @param[in] offset 元音声基準の先頭sample番号
	 * @param[out] destination 借用出力。成功時は全要素、失敗時は途中まで変更される場合があります
	 * @note 同じworkerで逐次使用します。並行呼出しは禁止します
	 */
	//------------------------------------------------------------------------------
	void						read( std::uint64_t offset, std::span<float> destination) const override ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 検証・正規化済みsampleを連続して追加します
	 * @param[in] samples 有限な-1～1のPCM
	 * @throws OperationError 不正PCM・整数表現・一時領域の異常
	 */
	//------------------------------------------------------------------------------
	void						append( std::span<const float> samples) ;

	private:
	/// 音声sourceの最終所有者の解放で削除するPCM
	TemporaryFile file_ ;
} ;

} // namespace koefude
