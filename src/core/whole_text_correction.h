//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_correction.h
 * @brief 原文全体へ束縛する自由全文と非再開試行の契約を定義します
 * @version 1.0.0
 * @date 2026/10/19
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/19 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/19 モデルに依存しない自由全文境界を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 入力束縛・証拠・op0受付・永続検証と表示意図を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 自然終端の説明を実装済みnative固定EOG照合へ同期
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1343の文章専用補正を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 * @note 時刻・話者・行対応を生成本文へ推測しません。構造検証は意味品質の認定ではありません
 */
//------------------------------------------------------------------------------
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

#include "core/pipeline.h"
#include "core/reading_session.h"
#include "core/reading_view.h"
#include "core/types.h"

namespace koefude {

/** @brief 一回の自由全文要求を原文と実効方式へ固定します */
struct WholeTextCorrectionRequest {
	/// 原文全体と履歴の識別値
	ReadingSourceKey			 source ;
	/// submit前0、受付後は再利用しない非0値
	std::uint64_t				 operationId = 0 ;
	/// ASCII英数字・ハイフン・下線の識別子、1〜64byte
	std::string					 profileId ;
	/// 実効方式・設定のdigest
	std::array<std::uint8_t, 32> profileSha256{} ;
} ;

/** @brief 原文・全補助資料・全promptを一要求へ束縛する歴史encodingです */
struct WholeTextInputBinding {
	/// 全要求
	WholeTextCorrectionRequest	 request ;
	/// 原文直接連結byte数
	std::uint64_t				 originalBytes = 0 ;
	/// 原文SHA
	std::array<std::uint8_t, 32> originalSha256{} ;
	/// 全補助canonical byte数
	std::uint64_t				 auxiliaryBytes = 0 ;
	/// 全補助SHA
	std::array<std::uint8_t, 32> auxiliarySha256{} ;
	/// system全byte数
	std::uint64_t				 systemBytes = 0 ;
	/// system SHA
	std::array<std::uint8_t, 32> systemSha256{} ;
	/// user全byte数
	std::uint64_t				 userBytes = 0 ;
	/// user SHA
	std::array<std::uint8_t, 32> userSha256{} ;
	/// 全束縛のSHA
	std::array<std::uint8_t, 32> inputSha256{} ;
} ;
/** @brief 永続時も原文とPCMへの束縛を検証する小さい証拠です */
struct WholeTextCorrectionEvidence {
	/// 証拠版
	std::uint32_t		  version = 1 ;
	/// 元ASRと同じ全PCM
	DecodedPcmIdentity	  audio ;
	/// 生成要求の全digest
	WholeTextInputBinding binding ;
} ;
/** @brief 旧行対応と独立した表示意図です */
enum class ResultPreference : std::uint32_t {
	/// 有効全文、旧整形、原文の順
	AUTOMATIC,
	/// 常に原文
	ORIGINAL,
	/// 有効な旧整形、なければ原文
	LEGACY_FORMATTED,
	/// 有効全文、なければ原文
	WHOLE_TEXT,
	/// 未完prefixを明示選択
	BOUNDED_PREFIX
} ;
//------------------------------------------------------------------------------
/**
 * @brief 束縛を歴史形式の全byteへ変換します
 * @param[in] binding 束縛値
 * @return 終端NUL込みdomainと既存順のbyte
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<std::uint8_t> encodeWholeTextInputBinding( const WholeTextInputBinding &binding) ;
//------------------------------------------------------------------------------
/**
 * @brief canonical束縛のdigestを返します
 * @param[in] binding 束縛値
 * @param[in] stop 協調取消し
 * @return 全byte SHA
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::array<std::uint8_t, 32>
									fingerprintWholeTextInputBinding( const WholeTextInputBinding &binding,
																	 // この同期境界で所有する値、または確定状態の検証値です
																	 std::stop_token			  stop = {}) ;
//------------------------------------------------------------------------------
/**
 * @brief 保存したcanonical束縛を厳密に復元します
 * @param[in] bytes 最大512byteの全列
 * @return 全消費・再encode一致の束縛
 * @throws OperationError 不正な長さ/domain/field
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextInputBinding decodeWholeTextInputBinding( std::span<const std::uint8_t> bytes) ;

/** @brief 元行から独立したUTF-8全文だけを所有します */
struct WholeTextCorrection {
	/// 原文全体への束縛
	ReadingSourceKey						   source ;
	/// 自由全文の契約版
	std::uint32_t							   contractVersion = 1 ;
	/// 要求と一致する実効方式
	std::string								   profileId ;
	/// 要求と一致する実効設定
	std::array<std::uint8_t, 32>			   profileSha256{} ;
	/// 改行や前後空白も無編集で保持するUTF-8本文
	std::string								   text ;
	/// nativeでは必須、汎用内部fakeは省略可
	std::optional<WholeTextCorrectionEvidence> evidence ;
} ;

/** @brief adapterが観測した終端です。モデルの生成fieldではありません */
enum class WholeTextTermination : std::uint32_t {
	/// 自然終端を確認していないため成功へ渡せない
	INCOMPLETE,
	/// adapterが自然終端を観測。native経路では固定EOGも検証
	NATURAL_END
} ;

/** @brief 同期生成器の未検証応答を所有します */
struct WholeTextCorrectionResponse {
	/// 要求の受付ID
	std::uint64_t		 operationId = 0 ;
	/// adapterの終端事実
	WholeTextTermination termination = WholeTextTermination::INCOMPLETE ;
	/// 原文との行対応を持たない全文
	WholeTextCorrection	 result ;
} ;

/** @brief worker限定の同期境界です。明示CPU試用から実生成器を使用します */
class IWholeTextCorrector {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有資源を解放します
	 */
	//------------------------------------------------------------------------------
	virtual ~IWholeTextCorrector() = default ;
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
	[[nodiscard]] virtual WholeTextCorrectionResponse correct( std::shared_ptr<const Transcript> source,
															  const WholeTextCorrectionRequest &request,
															  // この同期境界で所有する値、または確定状態の検証値です
															  std::stop_token					stop,
															  const ProgressCallback		   &progress) = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 生成器呼出し前に要求と原文の全体を照合します
 * @param[in] source 共有所有原文。null不可
 * @param[in] request 受付ID設定済みの固定要求
 * @throws OperationError 構造/束縛不一致、または容量超過
 * @note hashと本文走査を行うworker限定APIです
 */
//------------------------------------------------------------------------------
void validateWholeTextCorrectionRequest( const std::shared_ptr<const Transcript> &source,
										const WholeTextCorrectionRequest		&request) ;

/** @brief 全体の構造検証を通過した成功だけを所有します */
class ValidatedWholeTextCorrectionCompletion final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 自然終端・束縛・UTF-8・容量を全体検証し応答を保持します
	 * @param[in] source 不変原文の共有owner。null不可
	 * @param[in] request 受付ID設定済みの要求
	 * @param[in] response 全文応答の共有owner。null不可
	 * @throws OperationError 不正/不完はInvalidInput、容量超過はResultTooLarge
	 * @note worker限定。原文と応答を所有し本文は応答へaliasします
	 */
	//------------------------------------------------------------------------------
	ValidatedWholeTextCorrectionCompletion( std::shared_ptr<const Transcript> source, WholeTextCorrectionRequest request,
										   std::shared_ptr<const WholeTextCorrectionResponse> response) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定要求を参照します
	 * @return ownerの寿命内で有効な要求
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const WholeTextCorrectionRequest				   &request() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 原文ownerを参照します
	 * @return 不変原文の共有所有値
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const std::shared_ptr<const Transcript>		   &source() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 検証済み本文ownerを参照します
	 * @return 応答へaliasする所有値
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const std::shared_ptr<const WholeTextCorrection> &corrected() const noexcept ;

	private:
	/// 検証した原文owner
	std::shared_ptr<const Transcript>		   source_ ;
	/// 検証した小さい固定metadata
	WholeTextCorrectionRequest				   request_ ;
	/// 全応答を所有する本文alias
	std::shared_ptr<const WholeTextCorrection> corrected_ ;
} ;

/** @brief 最後の有効全文と保存可能な試行状態を保持します。active/phaseは再開しません */
struct WholeTextCorrectionRecord {
	/// 最新試行の共通状態
	ReadingState							   state = ReadingState::IDLE ;
	/// 行専用値を使わない終了理由
	ReadingReason							   reason = ReadingReason::NONE ;
	/// 本文を含まない元診断値
	std::uint32_t							   code = 0 ;
	/// beginで束縛した不変原文owner
	std::shared_ptr<const Transcript>		   source ;
	/// 最後の有効な全文
	std::shared_ptr<const WholeTextCorrection> corrected ;
	/// 再起動時には再開しない要求
	std::optional<WholeTextCorrectionRequest>  active ;
	/// 最新の実phase。復元時は消去
	WholeTextPhase							   phase = WholeTextPhase::NONE ;
	/// phaseなしの再decode等の実段階。永続化しません
	Stage									   stage = Stage::VALIDATING ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 検証済み表示snapshotでop0のpendingを先に確保します
 * @param[in] record 元record、変更しません
 * @param[in] source 同じ不変owner
 * @param[in] view 検証済み表示
 * @param[in] request op0要求
 * @return 受付後IDだけを設定してnoexcept moveできるpending
 * @throws OperationError 進行中、owner/key不一致
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextCorrectionRecord prepareWholeTextCorrectionRecord( const WholeTextCorrectionRecord  &record,
																		 std::shared_ptr<const Transcript> source,
																		 const ReadingViewSnapshot		  &view,
																		 const WholeTextCorrectionRequest &request) ;
//------------------------------------------------------------------------------
/**
 * @brief 保存・export・表示workerで証拠付き全文を全検証します
 * @param[in] source 原文
 * @param[in] key 原文key
 * @param[in] corrected 候補
 * @param[in] stop 協調取消し
 * @throws OperationError 不正/容量超過
 */
//------------------------------------------------------------------------------
void validatePersistedWholeTextCorrection( const Transcript &source, const ReadingSourceKey &key,
										  const WholeTextCorrection &corrected,
										  // この同期境界で所有する値、または確定状態の検証値です
										  std::stop_token			 stop = {}) ;
//------------------------------------------------------------------------------
/**
 * @brief 存在する結果から表示意図を純粋に解決します
 * @param[in] preference 保存した意図
 * @param[in] whole 有効全文あり
 * @param[in] legacy 有効旧整形あり
 * @return 実際に表示する種類
 */
//------------------------------------------------------------------------------
[[nodiscard]] ResultPreference resolveResultPreference( ResultPreference preference, bool whole, bool legacy) noexcept ;

//------------------------------------------------------------------------------
/**
 * @brief 原文を検証して新要求を開始し、以前の全文を保持します
 * @param[in,out] record 直列化済みの試行状態。拒否/例外時は不変
 * @param[in] source 同じ履歴で保持する不変原文owner。null不可
 * @param[in] request 非0受付IDを持つ要求
 * @return 開始時true、進行中または本文なしならfalse
 * @throws OperationError 原文・方式・旧結果の束縛違反
 * @note hash走査を行うためworkerまたは非UI内部境界で使用します
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool beginWholeTextCorrection( WholeTextCorrectionRecord &record, std::shared_ptr<const Transcript> source,
											const WholeTextCorrectionRequest &request) ;
//------------------------------------------------------------------------------
/**
 * @brief 同じ要求の実進捗を反映します
 * @param[in,out] record 直列化済み状態
 * @param[in] request 固定要求
 * @param[in] progress 実処理の段階。取消し開始後は無視
 */
//------------------------------------------------------------------------------
void updateWholeTextCorrection( WholeTextCorrectionRecord &record, const WholeTextCorrectionRequest &request,
							   Progress progress) ;
//------------------------------------------------------------------------------
/**
 * @brief 現要求だけを取消し待ちにします
 * @param[in,out] record 直列化済み状態
 */
//------------------------------------------------------------------------------
void cancelWholeTextCorrection( WholeTextCorrectionRecord &record) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 同じowner・要求の検証済み成功を反映し、失敗では旧全文を保持します
 * @param[in,out] record 直列化済み状態。stale時は不変
 * @param[in] source beginと同じ共有owner。nullならfalse
 * @param[in] request 受付時の固定要求
 * @param[in] completion worker検証済み成功。失敗時はnull
 * @param[in] error 元失敗分類。Cancelledは成功に優先
 * @param[in] code 本文を含まない元診断値
 * @return 同じ要求の終端を受領した場合true、stale時false
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool finishWholeTextCorrection( WholeTextCorrectionRecord									  &record,
											 const std::shared_ptr<const Transcript>					  &source,
											 const WholeTextCorrectionRequest							  &request,
											 std::shared_ptr<const ValidatedWholeTextCorrectionCompletion> completion,
											 ErrorKind error = ErrorKind::INTERNAL, std::uint32_t code = 0) ;
//------------------------------------------------------------------------------
/**
 * @brief 未終端をInterruptedへ移し、自動再生成せず旧ownerを保持します
 * @param[in,out] record 保存から復元した試行状態。active/phaseは常に消去
 */
//------------------------------------------------------------------------------
void			   restoreWholeTextCorrection( WholeTextCorrectionRecord &record) noexcept ;

/** @brief 新ASR一件の保存barrierと一回受付を保持します。復元履歴では既定のままです */
struct AutomaticTextContextRequest {
	/// ASR完了時の同じ不変owner
	std::shared_ptr<const Transcript> source ;
	/// 保存workerが照合した全体key
	ReadingSourceKey				  key ;
	/// 最後に受付された通常保存revision
	std::uint64_t					  revision = 0 ;
	/// 非0受付後は自動再送しません
	std::uint64_t					  operationId = 0 ;
	/// 送信前だけ取消し可能な有限待ち
	bool							  waiting = false ;
	/// 最新revision・owner一致の保存ack
	bool							  saved = false ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 新しいsource一件だけを待ちにします
	 * @param[in] owner 原文owner、nullは待ちなし
	 * @param[in] historyId 元カードID
	 */
	//------------------------------------------------------------------------------
	void							  arm( std::shared_ptr<const Transcript> owner, std::uint64_t historyId) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 集約先の保存revisionへbarrierを付け直します
	 * @param[in] value 最新受付revision、0は未保存
	 * @param[in] owner snapshotの原文owner
	 */
	//------------------------------------------------------------------------------
	void			   saving( std::uint64_t value, const std::shared_ptr<const Transcript> &owner) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 一致した耐久保存だけを受領します
	 * @param[in] value Saved revision
	 * @param[in] owner 保存した同owner
	 * @param[in] sourceKey workerで照合したkey
	 */
	//------------------------------------------------------------------------------
	void			   acknowledge( std::uint64_t value, const std::shared_ptr<const Transcript> &owner,
								   const ReadingSourceKey &sourceKey) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 受付0なら待ちを残し非0だけ一回消費します
	 * @param[in] id submit結果
	 */
	//------------------------------------------------------------------------------
	void			   submitted( std::uint64_t id) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 未受付の待ちだけを終端にし、受理済みoperationは保持します
	 */
	//------------------------------------------------------------------------------
	void			   cancel() noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief barrier成立をI/Oなしで返します
	 * @return 保存済み未受付ならtrue
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool ready() const noexcept ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 保存workerのackに束縛した要求だけを表示viewなしで準備します
 * @param[in] record 前有効全文
 * @param[in] automatic 同owner/keyの保存barrier
 * @param[in] profile 新しい固定profile
 * @return 受付前op0の所有pending
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextCorrectionRecord prepareSavedTextContextRecord( const WholeTextCorrectionRecord	&record,
																	  const AutomaticTextContextRequest &automatic,
																	  const ReadingProfile				&profile) ;
/** @brief 最大64KiBの同期readで全文候補を検査します。本文を外部へ出しません */
using TextContextReader = std::function<void( std::uint64_t, std::span<std::uint8_t>)> ; // 全指定byteを同期充填
//------------------------------------------------------------------------------
/**
 * @brief 原文と候補の限定保護spanを出現順・字面で比較します
 * @param[in] source 不変原文と明示話者名
 * @param[in] bytes 候補の全byte数
 * @param[in] read 最大64KiBの同期読取り
 * @param[in] stop 協調停止
 * @return 表面保護spanが一致すればtrue。意味保存の保証ではありません
 * @throws OperationError quota/読取り失敗/停止。数値等の異常長spanも拒否します
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool textContextProtectedSpansMatch( const Transcript &source, std::uint64_t bytes,
												  const TextContextReader &read, std::stop_token stop = {}) ;

} // namespace koefude
