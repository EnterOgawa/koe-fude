//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file types.h
 * @brief 音声処理の値型、上限、失敗情報を定義します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 ローカル音声処理の共通契約を追加
 * 0.1.0 Shunji Ogawa 2026/09/10 時間上限を除き、64bitの部分PCM読込みを追加
 * 0.1.0 Shunji Ogawa 2026/09/10 本文を分配せず話者交代で分ける任意token時刻を追加
 * 0.1.0 Shunji Ogawa 2026/09/10 CTC発火と継続区間を区別し、単語境界での分割許可を保持
 * 0.1.0 Shunji Ogawa 2026/09/11 本文を変えず話者候補の理由と推定範囲を保持
 * 0.1.0 Shunji Ogawa 2026/10/02 decoderの段階/入力説明を一般音声へ整合
 * 0.1.0 Shunji Ogawa 2026/10/19 既存Stage番号を保ち全文用の実phaseを追加
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1352の実区間進捗・採用保存を接続
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372の独立話者投影と元原文保持を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実契約・所有寿命・戻り値の意味を具体化
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415のoperator括弧と文書lambdaの書式を補修
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace koefude {

constexpr std::uint32_t K_SAMPLE_RATE = 16'000 ; // engineへ渡すmono PCMの周波数（Hz）
// Media Foundationの100ns時刻を符号付き64bitで表せるsample数。製品の録音時間制限ではない
constexpr std::uint64_t K_MAXIMUM_TIMELINE_SAMPLES =
	static_cast<std::uint64_t>( std::numeric_limits<std::int64_t>::max() - 1'000'000) / 625 ;
constexpr std::int64_t K_MAXIMUM_TIMELINE_MS =
	static_cast<std::int64_t>( K_MAXIMUM_TIMELINE_SAMPLES / 16) ; // 同じ表現境界のms
constexpr std::size_t K_MAXIMUM_SEGMENTS = 50'000 ; // 1jobの表示区間数上限
constexpr std::size_t K_MAXIMUM_TEXT_BYTES = 8 * 1024 * 1024 ; // 1jobのUTF-8本文上限
constexpr std::size_t K_MAXIMUM_ASR_TOKENS = 250'000 ; // 1jobで保持する任意token時刻の上限
constexpr std::size_t K_MAXIMUM_ATTRIBUTION_ITEMS = 250'000 ; // 1jobの推定範囲数と範囲内ID数の合計上限

/** @brief UI文字列と分離して保持する処理段階です */
enum class Stage {
	/// 入力またはmodelを検証中
	VALIDATING,
	/// 対応音声を内部PCMへ変換中
	DECODING,
	/// ASRモデルを読込み中
	LOADING_MODEL,
	/// 音声を文字へ変換中
	TRANSCRIBING,
	/// ファイル内の話者を推定中
	DIARIZING,
	/// 時刻と話者を結合中
	COMBINING,
	/// 利用者が指定した出力先へ保存中
	SAVING,
	/// 結果を確定済み
	COMPLETED
} ;

/** @brief OS診断値から独立した失敗分類です */
enum class ErrorKind {
	/// 対応形式のlocal通常fileの条件に適合しない入力
	INVALID_INPUT,
	/// APIの整数表現または利用可能なRAM容量を超える入力
	INPUT_TOO_LARGE,
	/// 未対応codecまたはchannel構成
	UNSUPPORTED_CODEC,
	/// Media Foundationで読込み・変換に失敗
	DECODE_FAILED,
	/// 不正timestampまたは時刻不連続
	INVALID_TIMELINE,
	/// 必要なモデルが存在しない
	MODEL_MISSING,
	/// hash、size等の検証に失敗
	MODEL_REJECTED,
	/// engineが推論を完了できない
	INFERENCE_FAILED,
	/// GPU未対応または必要メモリ不足
	GPU_UNAVAILABLE,
	/// 結果の行数または本文容量上限超過
	RESULT_TOO_LARGE,
	/// 利用者による協調的な取消し
	CANCELLED,
	/// 出力先に保存できない
	SAVE_FAILED,
	/// 予期しない内部異常
	INTERNAL,
	/// 一時領域の準備・容量・読書きの異常。既存enum番号の後へ追加
	TEMPORARY_STORAGE
} ;

/** @brief pathや認識本文を含めず失敗を境界へ伝える例外です */
class OperationError final : public std::exception {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief エラー分類と元の診断codeを保持します
	 * @param[in] kind UIで説明する分類
	 * @param[in] code 元のHRESULT等。該当なしは0
	 */
	//------------------------------------------------------------------------------
	explicit OperationError( ErrorKind kind, std::uint32_t code = 0) noexcept : kind_( kind), code_( code)
	{
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 分類を返します
	 * @return 構築時の失敗分類
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] ErrorKind kind() const noexcept
	{
		return kind_ ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 診断値を返します
	 * @return 元のcode、該当なしは0
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint32_t code() const noexcept
	{
		return code_ ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 内容を含まない固定説明を返します
	 * @return 静的文字列への非所有pointer
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const char *what() const noexcept override
	{
		return "KoeFude operation failed" ;
	}

	private:
	/// 利用者向け説明を選ぶ分類
	ErrorKind	  kind_ ;
	/// 元APIの診断code
	std::uint32_t code_ ;
} ;

/** @brief 既存Stageの番号を変えず全文処理の用途を示します */
enum class WholeTextPhase {
	/// 通常の処理
	NONE,
	/// 全PCMの同一性照合
	AUDIO_IDENTITY,
	/// 全窓の補助認識
	AUXILIARY_ASR,
	/// 検証leaseと入力準備
	NATIVE_PREPARATION,
	/// 補正全文の生成
	GENERATION,
	/// 全応答の検証
	OUTPUT_VALIDATION
} ;

/** @brief APIの実応答と耐久採用を区別する表示専用段階です */
enum class ApiTextPhase {
	/// 既存ASR/CLI/整形には追加表示しない
	NONE,
	/// 原文・設定と分割planを確認
	PREPARING,
	/// 明示操作に必要な認証資格を確認
	AUTHORIZING,
	/// 次の送信intentを耐久保存
	RESERVING,
	/// 指定区間を一回送信し応答を待機
	AWAITING,
	/// 実応答を受領し候補を保存
	SAVING_RESPONSE,
	/// 全文の採用と表示意図を耐久保存
	ADOPTING,
	/// 同じoperationの協調取消し待ち
	CANCELLING,
	/// 必要な保存ACKを受領した終端
	COMPLETED,
	/// 原文/前採用を保持する失敗終端
	FAILURE,
	/// 原文/前採用を保持する取消し終端
	CANCELLED
} ;
/** @brief 本文や秘密を持たない小さい所有API進捗です。永続化/C ABIには渡しません */
struct ApiTextProgress {
	/// 現実に開始した段階
	ApiTextPhase  phase = ApiTextPhase::NONE ;
	/// 完全応答数。保存中以外は耐久保存済みの数
	std::uint32_t completedUnits = 0 ;
	/// 検証済みplanの区間数、準備前は0
	std::uint32_t totalUnits = 0 ;
	/// 現在区間の1始まり番号、該当なし0
	std::uint32_t currentUnit = 0 ;
	/// steady_clock epochの実開始ms
	std::uint64_t startedMs = 0 ;
	/// 通知時刻。終端ではこの値で経過を凍結
	std::uint64_t observedMs = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief UIで同じ進捗tickの再表示を省けるかを比較します
	 * @param[in] other 比較する別の進捗通知。呼出し中だけ借用します
	 * @return phase、区間三数、startedMs/observedMsがすべて一致すればtrue。それ以外はfalseです
	 * @note 区間数は窓/区間単位、時刻はsteady_clock epochのmsです。本文・保存成功や残時間を比較する関数ではありません
	 */
	//------------------------------------------------------------------------------
	bool		  operator==( const ApiTextProgress &other) const = default ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 壁時計変更に影響されない進捗時刻を返します
 * @return steady_clock epochのms
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline std::uint64_t progressMonotonicMs() noexcept
{
	return static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now().time_since_epoch())
			.count()) ;
}

/** @brief UIとCLIが所有権付きで受け取る進捗値です */
struct Progress {
	/// 現在の段階
	Stage			stage = Stage::VALIDATING ;
	/// 当該段階の確定進捗0～100、総量不明は-1
	int				percent = 0 ;
	/// 用途別の実段階
	WholeTextPhase	wholeTextPhase = WholeTextPhase::NONE ;
	/// 既定Noneは従来処理を変更しない追加DTO
	ApiTextProgress apiText ;
	/// 音声API専用段階。文章改善進捗/採用とは分離
	ApiTextProgress apiAudio ;
	/// PC内話者本文対応の実窓数。音声API完了状態とは分離
	ApiTextProgress speakerText ;
} ;

using ProgressCallback = std::function<void( Progress)> ; // workerで呼び出す進捗通知

/** @brief worker専用の有界な部分読込みを提供するPCMの所有元です */
class IAudioSource {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有する一時PCM等を回収します
	 */
	//------------------------------------------------------------------------------
	virtual ~IAudioSource() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 全PCMのsample数を返します
	 * @return 16kHz monoの総数
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] virtual std::uint64_t size() const noexcept = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 指定範囲のPCMだけを出力bufferへ読み込みます
	 * @param[in] offset 元音声基準の先頭sample番号
	 * @param[out] destination 借用出力。成功時は全要素、失敗時は途中まで変更される場合があります
	 * @note 同じworkerで逐次使用します。並行呼出しは禁止します
	 */
	//------------------------------------------------------------------------------
	virtual void						read( std::uint64_t offset, std::span<float> destination) const = 0 ;
} ;

/** @brief 検証済み内部PCMの所有元と、内容を含まない入力属性を保持します */
struct AudioData {
	/// fake等の短い16kHz mono PCM。sourceとは同時使用しない
	std::vector<float>					samples ;
	/// 実音声の一時PCMを所有する部分読込み元
	std::shared_ptr<const IAudioSource> source ;
	/// 明示した冒頭試験のview終端
	std::uint64_t						sampleLimit = std::numeric_limits<std::uint64_t>::max() ;
	/// 元音声の周波数（Hz）
	std::uint32_t						sourceRate = 0 ;
	/// 元音声のchannel数
	std::uint32_t						sourceChannels = 0 ;
	/// 元fileのbyte数
	std::uint64_t						sourceBytes = 0 ;
	/// Media Foundationのprofile indication、未取得は0
	std::uint32_t						aacProfile = 0 ;

	//------------------------------------------------------------------------------
	/**
	 * @brief このviewで利用するsample数を返します
	 * @return 明示試験範囲内の総数
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t			sampleCount() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 連続した部分PCMを所有bufferへ読み込みます
	 * @param[in] offset view内のsample番号
	 * @param[out] destination 同期呼出し中だけ借用する出力。失敗時は内容未確定
	 * @throws OperationError 範囲外またはI/O異常
	 */
	//------------------------------------------------------------------------------
	void								readSamples( std::uint64_t offset, std::span<float> destination) const ;
} ;

/** @brief 所有本文の連続したbyte範囲と音声に基づく任意のtoken時刻です */
struct TextToken {
	/// 元音声基準の開始時刻（ms）
	std::int64_t startMs = 0 ;
	/// 正の時間範囲の終端（ms）。CTCでは発火frameの終端
	std::int64_t endMs = 0 ;
	/// 親本文のUTF-8 byte終端。先頭は直前tokenの終端、最初は0
	std::size_t	 textEnd = 0 ;
	/// falseなら単語途中等のため、このtoken終端で本文を切らない
	bool		 canSplitAfter = true ;
} ;

/** @brief 元音声基準の半開時間区間と、任意の対応tokenを持つUTF-8 textです */
struct TextSegment {
	/// 開始時刻（ms）
	std::int64_t		   startMs = 0 ;
	/// 終了時刻（ms）
	std::int64_t		   endMs = 0 ;
	/// 所有するUTF-8本文
	std::string			   text ;
	/// 空なら区間単位。非空なら全本文を順序通り一度だけ覆う
	std::vector<TextToken> tokens ;
	/// trueはCTC発火位置で、発話の継続時間ではない
	bool				   tokenTimesAreAnchors = false ;
} ;

/** @brief ファイル内の話者推定区間です */
struct SpeakerTurn {
	/// 元音声での開始時刻（ms）
	std::int64_t startMs = 0 ;
	/// 元音声での終了時刻（ms）
	std::int64_t endMs = 0 ;
	/// file内cluster ID、-1は不明
	int			 speaker = -1 ;
} ;

/** @brief 話者候補の時間的な根拠です。原音の正解や各文字の帰属を確定しません */
enum class AttributionKind : std::uint32_t {
	/// 旧履歴等で理由・範囲が記録されていない状態
	UNAVAILABLE,
	/// 採用できる話者候補がなく帰属未確定
	UNASSIGNED,
	/// 一つの候補にだけ対応する推定範囲を持つ状態
	SINGLE_SPEAKER,
	/// 複数候補が逐次現れ、本文内の割当てが未確定
	UNRESOLVED_CHANGE,
	/// 候補が同時に有効な範囲を含む状態。行全体の同時発話ではない
	ESTIMATED_OVERLAP
} ;

/** @brief 採用した候補集合が一定の、元音声基準の正の半開区間です */
struct AttributionSpan {
	/// 元音声の開始時刻（ms）
	std::int64_t	 startMs = 0 ;
	/// 元音声の終了時刻（ms）
	std::int64_t	 endMs = 0 ;
	/// 有効な候補を重複なしの昇順で所有。空集合の区間は保存しない
	std::vector<int> speakers ;
} ;

/** @brief 本文の文字対応と区別した話者推定の根拠を所有します */
struct SpeakerAttribution {
	/// 未記録を推測で補わない既定値
	AttributionKind				 kind = AttributionKind::UNAVAILABLE ;
	/// 重ならない時刻順の推定範囲。隙間の帰属は未確定
	std::vector<AttributionSpan> spans ;
} ;

/** @brief 表示・保存する一つの発話行です */
struct TranscriptLine {
	/// 元音声の開始時刻（ms）
	std::int64_t	   startMs = 0 ;
	/// 元音声の終了時刻（ms）
	std::int64_t	   endMs = 0 ;
	/// 初出順の1始まり話者番号。空は不明、複数は帰属候補
	std::vector<int>   speakers ;
	/// UTF-8本文。本値が所有
	std::string		   text ;
	/// 候補が複数になった理由と有効範囲。本文の分配は行わない
	SpeakerAttribution attribution ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 要求済み取消しを処理段階の境界で検出します
 * @param[in] stop 呼出側が所有するstop stateへのtoken
 * @throws OperationError 取消しが要求済みの場合
 */
//------------------------------------------------------------------------------
inline void checkCancelled( const std::stop_token stop)
{
	// 新たな外部API呼出しを始める前に、取消しを終端結果へ変換する
	if( stop.stop_requested()) {
		throw OperationError( ErrorKind::CANCELLED) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 通知先が登録されている場合だけ段階の進捗を渡します
 * @param[in] callback worker上の通知先。本呼出し中だけ参照
 * @param[in] stage 処理段階
 * @param[in] percent 段階内の0～100、総量不明は-1
 */
//------------------------------------------------------------------------------
inline void reportProgress( const ProgressCallback &callback, const Stage stage, const int percent)
{
	// CLIの無通知実行も許可し、空のstd::functionを呼ばない
	if( callback) {
		callback( {stage, percent}) ;
	}
}

} // namespace koefude
