//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file speaker_text_projection.cpp
 * @brief 全最小編集経路と単一の音響候補だけをAPIの無編集byte範囲へ対応させます
 * @version 1.0.0
 * @date 2026/11/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372方式3と語境界atomの保守的対応を追加
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
#include "core/speaker_text_projection.h"

#include <span>
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <utility>

namespace koefude {
namespace {
constexpr std::size_t K_MINIMUM_CONTENT = 24 ; // 採用範囲と完全一致の最小内容文字数
constexpr std::size_t K_MINIMUM_ANCHOR = 12 ; // 一意な内部連続完全一致の最小長
constexpr std::size_t K_MINIMUM_GRAMS = 8 ; // anchor内の異なる4gramの最小数
constexpr std::size_t K_MAXIMUM_MISMATCH = 12 ; // 連続不一致/削除gapの最大長
constexpr std::size_t K_MAXIMUM_TRIM = 12 ; // 不安全端から内側へ移動できる内容文字数
constexpr std::size_t K_MAXIMUM_POSTINGS = 8 ; // 頻出4gramをanchor根拠から除く上限
constexpr std::size_t K_MAXIMUM_FEATURES = 262144 ; // 実scalar比較とindex特徴の固定予算
constexpr std::size_t K_MAXIMUM_TURN_CHECKS = 32 * 1024 * 1024 ; // 窓ごとの候補照合上限
constexpr std::size_t K_NO_INDEX = std::numeric_limits<std::size_t>::max() ; // 対応なし

/** @brief 固定解析上限を本文なしで内部へ伝えます */
struct Limited		  final {} ;
/** @brief strict UTF-8の一scalarと元byte範囲です */
struct Scalar {
	/// 正規化しないUnicode scalar
	std::uint32_t value = 0 ;
	/// 元UTF-8先頭byte
	std::size_t	  begin = 0 ;
	/// 元UTF-8終端byte
	std::size_t	  end = 0 ;
	/// 参照単位、API側では未使用
	std::size_t	  owner = K_NO_INDEX ;
} ;
/** @brief 4gramの実scalarをkeyとしhash衝突を作りません */
using Gram = std::array<std::uint32_t, 4> ;
/** @brief 頻出特徴を固定数で打切るpostingです */
struct Posting {
	/// 実位置、上限超過後は使わない
	std::array<std::size_t, K_MAXIMUM_POSTINGS> positions{} ;
	/// 最大9。9は頻出で使用不可
	std::size_t									count = 0 ;
} ;
using GramIndex = std::map<Gram, Posting> ; // 最大4093特徴の有界index
/** @brief 全最小経路の一API scalarに関する合意です */
struct Ownership {
	/// 競合や境界挿入は中立
	std::size_t owner = K_NO_INDEX ;
	/// 全経路で一致する参照scalar位置
	std::size_t reference = K_NO_INDEX ;
	/// 全消費遷移が完全一致の対角の場合だけtrue
	bool		exact = false ;
} ;
/** @brief 連続完全一致の参照/API範囲です */
struct ExactRun {
	/// API内容文字の先頭
	std::size_t begin = 0 ;
	/// API内容文字の終端
	std::size_t end = 0 ;
	/// 両全文で一意かつ十分に異なる特徴を持つ
	bool		unique = false ;
} ;
/** @brief 局所最小距離と全最小経路の最大連続不一致です */
struct EditMeasure {
	/// 参照単位全文との距離
	std::size_t distance = 0 ;
	/// 全局所最小経路の最大gap
	std::size_t mismatch = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief strict UTF-8境界を判定します
 * @param[in] text 元byte
 * @param[in] at byte位置
 * @return 端またはscalar先頭ならtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool byteBoundary( const std::string_view text, const std::size_t at) noexcept
{
	return at <= text.size() && (at == text.size() || (static_cast<unsigned char>( text[at]) & 0xc0U) != 0x80U) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 一scalarを厳密復号します
 * @param[in] text 有界元UTF-8
 * @param[in,out] at 先頭byte、成功時は次へ進む
 * @return 元scalar
 * @throws OperationError 不正UTF-8
 */
//------------------------------------------------------------------------------
[[nodiscard]] Scalar nextScalar( const std::string_view text, std::size_t &at)
{
	Scalar result ; // 元byteと値を一緒に返す
	result.begin = at ;
	const auto	  first = static_cast<unsigned char>( text[at++]) ; // 符号なしの先頭byte
	std::size_t	  extra = 0 ; // 後続byteの必要数
	std::uint32_t minimum = 0 ; // 最短表現を検証する下限
	// ASCIIを追加変換せずそのまま保持する
	if( first <= 0x7fU) {
		result.value = first ;
	} else if( first >= 0xc2U && first <= 0xdfU) { // 二byteの最短表現
		result.value = first & 0x1fU ;
		extra = 1 ;
		minimum = 0x80 ;
	} else if( first >= 0xe0U && first <= 0xefU) { // 三byteの最短表現
		result.value = first & 0x0fU ;
		extra = 2 ;
		minimum = 0x800 ;
	} else if( first >= 0xf0U && first <= 0xf4U) { // 四byteのUnicode範囲
		result.value = first & 0x07U ;
		extra = 3 ;
		minimum = 0x10000 ;
	} else {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// 未完byte列を領域外から読まない
	if( extra > text.size() - at) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// 必要な後続byteだけを検証してscalarへ戻す
	for( std::size_t index = 0; index < extra; ++index) { // 現在の後続番号
		const auto byte = static_cast<unsigned char>( text[at++]) ; // 次のbyte
		// 独立した先頭byteやASCIIを後続として受理しない
		if( (byte & 0xc0U) != 0x80U) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
		result.value = (result.value << 6U) | (byte & 0x3fU) ;
	}
	// 過長表現、surrogate、Unicode範囲外を全て拒否する
	if( result.value == 0 || result.value < minimum || result.value > 0x10ffffU ||
	   (result.value >= 0xd800U && result.value <= 0xdfffU)) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	result.end = at ;
	return result ;
}

//------------------------------------------------------------------------------
/**
 * @brief ADR1364と同じ明示separatorだけを照合用から除きます
 * @param[in] value Unicode scalar
 * @return 除外対象ならtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool separator( const std::uint32_t value) noexcept
{
	return value <= 0x20 || value == U'　' || value == U'。' || value == U'、' || value == U'，' || value == U'．' ||
		   value == U',' || value == U'.' || value == U'!' || value == U'?' || value == U'！' || value == U'？' ||
		   value == U'「' || value == U'」' || value == U'『' || value == U'』' || value == U'"' || value == U'\'' ;
}

//------------------------------------------------------------------------------
/**
 * @brief 全byteを検証し内容scalarだけを有界mapへ追加します
 * @param[in] text 無編集本文
 * @param[in] owner 参照単位
 * @param[in,out] out 連結内容map
 * @param[in] stop 協調取消し
 */
//------------------------------------------------------------------------------
void appendContent( const std::string_view text, const std::size_t owner, std::vector<Scalar> &out,
				   const std::stop_token stop)
{
	// separatorだけの巨大入力もprefix切捨てで受理しない
	if( text.size() > K_SPEAKER_TEXT_WINDOW_BYTES) {
		throw Limited{} ;
	}
	// 原byteを一度ずつ復号し、除外文字にもUTF-8検証を適用する
	for( std::size_t at = 0; at < text.size();) { // 次の原byte位置
		checkCancelled( stop) ;
		auto scalar = nextScalar( text, at) ; // 原UTF-8へのmap
		// 表示本文には触れず、照合用mapからだけ明示separatorを省く
		if( !separator( scalar.value)) {
			// 固定内容上限を超える窓を全中立へ戻す
			if( out.size() == K_SPEAKER_TEXT_SCALARS) {
				throw Limited{} ;
			}
			scalar.owner = owner ;
			out.push_back( scalar) ;
		}
	}
}

//------------------------------------------------------------------------------
/**
 * @brief token発火の全範囲を一候補が被覆するか確認します
 * @param[in] token 元PCM発火範囲
 * @param[in] turns 開始順の対象turn
 * @param[in] ids raw clusterから初出順へのmap
 * @param[in,out] checks 固定比較予算
 * @return 単一候補番号と中立理由
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::pair<int, SpeakerTextReason> tokenCandidate( const TextToken					 &token,
															   const std::span<const SpeakerTurn> turns,
															   const std::map<int, int> &ids, std::size_t &checks)
{
	int			 candidate = -1 ; // 未被覆または候補競合は中立
	std::int64_t covered = token.startMs ; // 同一候補による連続被覆終端
	bool		 unknown = false ; // 不明turnを既知候補で上塗りしない
	// 発火より前のturnも終了時刻で照合し、重なりを見落とさない
	for( const auto &turn : turns) { // 対象全fileの一turn
		// 検証量が超過した窓を有限に停止する
		if( ++checks > K_MAXIMUM_TURN_CHECKS) {
			throw Limited{} ;
		}
		// 開始順に並んでいるため、以後のturnは発火と重ならない
		if( turn.startMs >= token.endMs) {
			break ;
		}
		// 発火前に終わったturnは候補へ加えない
		if( turn.endMs <= token.startMs) {
			continue ;
		}
		// 不明は単一既知話者へ吸収しない
		if( turn.speaker < 0) {
			unknown = true ;
			continue ;
		}
		const int normalized = ids.at( turn.speaker) ; // 対象表示と同じ初出順番号
		// 異なる話者が発火範囲に存在すればatomを保留する
		if( candidate > 0 && candidate != normalized) {
			return {-1, SpeakerTextReason::OVERLAP} ;
		}
		candidate = normalized ;
		// 二つの同一話者turnの間の未被覆gapも中立に残す
		if( turn.startMs > covered) {
			unknown = true ;
		}
		covered = std::max( covered, std::min( token.endMs, turn.endMs)) ;
	}
	// 端の被覆不足と未知候補を成功に変換しない
	if( unknown || candidate < 1 || covered < token.endMs) {
		return {-1, SpeakerTextReason::UNKNOWN} ;
	}
	return {candidate, SpeakerTextReason::SINGLE_CANDIDATE} ;
}

//------------------------------------------------------------------------------
/**
 * @brief atom間へ別話者や不明turnが介在する結合を防ぎます
 * @param[in] begin 前atom末尾
 * @param[in] end 次atom先頭
 * @param[in] speaker 正規化済み候補
 * @param[in] turns 全対象turn
 * @param[in] ids 初出順map
 * @return 中間に他候補がなければtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool joinable( const std::int64_t begin, const std::int64_t end, const int speaker,
							const std::span<const SpeakerTurn> turns, const std::map<int, int> &ids)
{
	// 時刻間へ他候補が介在する場合だけ分離し、無音を文字へ按分しない
	for( const auto &turn : turns) { // 元fileの一turn
		// 昇順の後続turnは中間へ介在しない
		if( turn.startMs >= end) {
			break ;
		}
		// 中間に不明または別話者があれば同一候補でも一単位にしない
		if( turn.endMs > begin && (turn.speaker < 0 || ids.at( turn.speaker) != speaker)) {
			return false ;
		}
	}
	return true ;
}

//------------------------------------------------------------------------------
/**
 * @brief 指定内容位置の4gramを実値で作ります
 * @param[in] values 内容map
 * @param[in] at 四文字を読める位置
 * @return 実scalarのkey
 */
//------------------------------------------------------------------------------
[[nodiscard]] Gram gramAt( const std::span<const Scalar> values, const std::size_t at)
{
	return {values[at].value, values[at + 1].value, values[at + 2].value, values[at + 3].value} ;
}

//------------------------------------------------------------------------------
/**
 * @brief 全参照を含む固定posting indexを作ります
 * @param[in] values 内容map
 * @param[in,out] features 特徴予算
 * @return 全有界posting
 */
//------------------------------------------------------------------------------
[[nodiscard]] GramIndex gramIndex( const std::span<const Scalar> values, std::size_t &features)
{
	GramIndex result ; // 実scalarをkeyとするindex
	// 全4gramを登録し、中立の反復も一意性判定へ残す
	for( std::size_t at = 0; at + 4 <= values.size(); ++at) { // 現在のgram先頭
		// 窓内の総特徴量を固定する
		if( ++features > K_MAXIMUM_FEATURES) {
			throw Limited{} ;
		}
		auto &posting = result[gramAt( values, at)] ; // 当該実4gramのposting
		// 頻出は9をsentinelにして、これ以上の配列を持たない
		if( posting.count < K_MAXIMUM_POSTINGS) {
			posting.positions[posting.count] = at ;
		}
		posting.count = std::min( posting.count + 1, K_MAXIMUM_POSTINGS + 1) ;
	}
	return result ;
}

//------------------------------------------------------------------------------
/**
 * @brief 原全文に含まれる実scalar一致数を固定postingから求めます
 * @param[in] haystack 全内容
 * @param[in] needle 連続完全一致anchor
 * @param[in] index 全文posting
 * @param[in] offset anchor内で選んだgram位置
 * @param[in,out] comparisons 実scalar比較予算
 * @return 一意の場合true
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool occursOnce( const std::span<const Scalar> haystack, const std::span<const Scalar> needle,
							  const GramIndex &index, const std::size_t offset, std::size_t &comparisons)
{
	const auto found = index.find( gramAt( needle, offset)) ; // hashでなく実値を照合
	// 頻出特徴は全文比較を増やさずanchorを保留する
	if( found == index.end() || found->second.count > K_MAXIMUM_POSTINGS) {
		return false ;
	}
	std::size_t count = 0 ; // 実全文で確認できた出現数
	// 有界postingだけを候補にして、全anchorを実scalarで確認する
	for( std::size_t number = 0; number < found->second.count; ++number) { // posting番号
		const auto position = found->second.positions[number] ; // 選んだgramの全文位置
		// anchorの前後が全文から逸脱する候補を除く
		if( position < offset || needle.size() > haystack.size() - (position - offset)) {
			continue ;
		}
		bool same = true ; // 全scalarで一致した場合だけ一出現
		// 文字列hashの一致をそのまま一意の根拠へ使わない
		for( std::size_t at = 0; at < needle.size(); ++at) { // anchor内の文字
			// 比較予算上限なら窓全体を保留する
			if( ++comparisons > K_MAXIMUM_FEATURES) {
				throw Limited{} ;
			}
			// 最初の相違で次のpostingへ進む
			if( needle[at].value != haystack[position - offset + at].value) {
				same = false ;
				break ;
			}
		}
		// 二度現れたanchorを一方の話者へ恣意的に割当てない
		if( same && ++count > 1) {
			return false ;
		}
	}
	return count == 1 ;
}

//------------------------------------------------------------------------------
/**
 * @brief 異なる特徴を持つ長い完全一致が両全文で一意か検証します
 * @param[in] needle 候補完全一致列
 * @param[in] reference 全参照内容
 * @param[in] api 全API内容
 * @param[in] ri 参照index
 * @param[in] ai API index
 * @param[in,out] comparisons 実比較予算
 * @return 内部anchor条件を満たせばtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool uniqueAnchor( const std::span<const Scalar> needle, const std::span<const Scalar> reference,
								const std::span<const Scalar> api, const GramIndex &ri, const GramIndex &ai,
								std::size_t &comparisons)
{
	// 短応答を長い根拠として扱わない
	if( needle.size() < K_MINIMUM_ANCHOR) {
		return false ;
	}
	std::set<Gram> different ; // 最大4093個の実4gram
	std::size_t	   selected = K_NO_INDEX ; // 両全文でposting8以下の特徴
	std::size_t	   bestCount = 2 * K_MAXIMUM_POSTINGS + 1 ; // 最も少ない候補数
	// anchor全体で異なる特徴数と有界な照合起点を選ぶ
	for( std::size_t at = 0; at + 4 <= needle.size(); ++at) { // anchor内gram位置
		const auto gram = gramAt( needle, at) ; // 実4scalar
		different.insert( gram) ;
		const auto r = ri.find( gram) ; // 中立を含む全参照のposting
		const auto a = ai.find( gram) ; // 同じAPI窓のposting
		// 両側で頻出でない特徴だけを全文比較起点へ使う
		if( r != ri.end() && a != ai.end() && r->second.count <= K_MAXIMUM_POSTINGS &&
		   a->second.count <= K_MAXIMUM_POSTINGS && r->second.count + a->second.count < bestCount) {
			selected = at ;
			bestCount = r->second.count + a->second.count ;
		}
	}
	return different.size() >= K_MINIMUM_GRAMS && selected != K_NO_INDEX &&
		   occursOnce( reference, needle, ri, selected, comparisons) &&
		   occursOnce( api, needle, ai, selected, comparisons) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 距離の全最小経路からAPI scalarのowner合意だけを抽出します
 * @param[in] reference 全参照
 * @param[in] api API内容
 * @param[in] stop 協調取消し
 * @param[in,out] result 固定資源使用量
 * @return 全経路の合意値
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<Ownership> optimalOwners( const std::span<const Scalar> reference,
												   const std::span<const Scalar> api, const std::stop_token stop,
												   SpeakerTextWindowProjection &result)
{
	const std::size_t width = api.size() + 1 ; // API方向のrow幅
	const std::size_t cells = (reference.size() + 1) * width ; // overflowしない固定scalar上限内
	// 主距離配列の確保前に固定cell予算を検証する
	if( cells > K_SPEAKER_TEXT_MATRIX_CELLS) {
		throw Limited{} ;
	}
	result.matrixCells = cells ;
	result.arenaBytes =
		cells * 2 * sizeof(std::uint16_t) + (reference.size() + api.size() + 2) * 256 ; // 全map/局所row等の保守量
	std::vector<std::uint16_t> forward( cells) ; // 最大距離8192の前向き距離
	std::vector<std::uint16_t> backward( cells) ; // 同じgraphの後向き距離
	// 参照prefixと空APIの距離を初期化する
	for( std::size_t i = 0; i <= reference.size(); ++i) { // 参照prefix長
		forward[i * width] = static_cast<std::uint16_t>( i) ;
	}
	// 空参照とAPI prefixの距離を初期化する
	for( std::size_t j = 0; j <= api.size(); ++j) { // API prefix長
		forward[j] = static_cast<std::uint16_t>( j) ;
	}
	// 一致0、削除/挿入/置換1を全参照単位に適用する
	for( std::size_t i = 1; i <= reference.size(); ++i) { // 参照prefix長
		checkCancelled( stop) ;
		// 中立の参照も含め、全API prefixへの最短距離を求める
		for( std::size_t j = 1; j <= api.size(); ++j) { // API prefix長
			const int diagonal =
				forward[(i - 1) * width + j - 1] + (reference[i - 1].value == api[j - 1].value ? 0 : 1) ; // 対角cost
			forward[i * width + j] = static_cast<std::uint16_t>(
				std::min( {diagonal, forward[(i - 1) * width + j] + 1, forward[i * width + j - 1] + 1})) ;
		}
	}
	// 末尾から空APIへの距離を初期化する
	for( std::size_t i = 0; i <= reference.size(); ++i) { // 参照suffix先頭
		backward[i * width + api.size()] = static_cast<std::uint16_t>( reference.size() - i) ;
	}
	// 空参照へのAPI suffix距離を初期化する
	for( std::size_t j = 0; j <= api.size(); ++j) { // API suffix先頭
		backward[reference.size() * width + j] = static_cast<std::uint16_t>( api.size() - j) ;
	}
	// 逆順に距離を作り、全最小経路上の辺を後で判定できるようにする
	for( std::size_t i = reference.size(); i-- > 0;) { // 参照suffix先頭
		checkCancelled( stop) ;
		// 右/下/右下の既知距離から現位置を求める
		for( std::size_t j = api.size(); j-- > 0;) { // API suffix先頭
			const int diagonal =
				backward[(i + 1) * width + j + 1] + (reference[i].value == api[j].value ? 0 : 1) ; // 対角cost
			backward[i * width + j] = static_cast<std::uint16_t>(
				std::min( {diagonal, backward[(i + 1) * width + j] + 1, backward[i * width + j + 1] + 1})) ;
		}
	}
	const auto			   distance = forward.back() ; // 全窓の最短距離
	std::vector<Ownership> owners( api.size()) ; // API一文字ごとの全経路合意
	// 全API消費辺を調べ、一つのtracebackだけを選ばない
	for( std::size_t j = 0; j < api.size(); ++j) { // 消費するAPI内容文字
		checkCancelled( stop) ;
		bool	   seen = false ; // 最初の消費辺で合意値を初期化する
		const auto include =
			//------------------------------------------------------------------------------
			/**
			 * @brief 全最小経路上の一消費辺を合意へ追加します
			 * @param[in] owner 参照単位
			 * @param[in] exact 完全一致対角
			 * @param[in] position 参照scalar位置、挿入はK_NO_INDEX
			 */
			//------------------------------------------------------------------------------
			[&]( const std::size_t owner, const bool exact, const std::size_t position)
			{
				// 初回の候補以後は一つでも異なる候補があれば中立へ固定する
				if( !seen) {
					owners[j] = {owner, position, exact} ;
					seen = true ;
				} else {
					// 同点別ownerを見落とさず中立にする
					if( owners[j].owner != owner) {
						owners[j].owner = K_NO_INDEX ;
					}
					// 字面一致と位置一致を別々に検証する
					if( owners[j].reference != position) {
						owners[j].reference = K_NO_INDEX ;
					}
					owners[j].exact = owners[j].exact && exact ;
				}
			} ; // 借用はこのAPI文字の走査中だけ
		// 各参照位置からの挿入/対角が全最短距離に含まれるか調べる
		for( std::size_t i = 0; i <= reference.size(); ++i) { // 参照内容位置
			// 挿入は前後が同じ単位の場合だけowner候補を持つ
			if( forward[i * width + j] + 1 + backward[i * width + j + 1] == distance) {
				include( i > 0 && i < reference.size() && reference[i - 1].owner == reference[i].owner
							? reference[i].owner
							: K_NO_INDEX,
						false, K_NO_INDEX) ;
			}
			// 対角の一致/置換は消費した参照単位を候補にする
			if( i < reference.size()) {
				const bool exact = reference[i].value == api[j].value ; // 字面の一致
				// 全最小経路上の対角だけを合意へ含める
				if( forward[i * width + j] + (exact ? 0 : 1) + backward[(i + 1) * width + j + 1] == distance) {
					include( reference[i].owner, exact, exact ? i : K_NO_INDEX) ;
				}
			}
		}
	}
	return owners ;
}

//------------------------------------------------------------------------------
/**
 * @brief 完全一致の連続範囲と両全文一意性を求めます
 * @param[in] owners 全最適合意
 * @param[in] reference 参照
 * @param[in] api API
 * @param[in] ri 参照index
 * @param[in] ai API index
 * @param[in,out] comparisons 実比較予算
 * @return 完全一致範囲。単位境界を跨ぐ場合も境界検証だけに利用できます
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<ExactRun> exactRuns( const std::span<const Ownership> owners,
											  const std::span<const Scalar>	   reference,
											  const std::span<const Scalar> api, const GramIndex &ri,
											  const GramIndex &ai, std::size_t &comparisons)
{
	std::vector<ExactRun> runs ; // 最大4096の完全一致範囲
	// 位置も全経路で一意な対角を連続範囲として保持する
	for( std::size_t at = 0; at < owners.size();) { // 次のAPI内容位置
		// 曖昧な文字を完全anchorへ含めない
		if( !owners[at].exact || owners[at].reference == K_NO_INDEX) {
			++at ;
			continue ;
		}
		const auto begin = at++ ; // 新しい完全一致範囲の先頭
		// 元参照でも隣接している完全一致だけを延ばす
		while( at < owners.size() && owners[at].exact && owners[at].reference != K_NO_INDEX &&
			  owners[at].reference == owners[at - 1].reference + 1) {
			++at ;
		}
		runs.push_back( {begin, at, uniqueAnchor( api.subspan( begin, at - begin), reference, api, ri, ai, comparisons)}) ;
	}
	return runs ;
}

//------------------------------------------------------------------------------
/**
 * @brief ASCII単語内部の切断を拒否します
 * @param[in] value scalar
 * @return ASCII英数字/underscoreならtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool asciiWord( const std::uint32_t value) noexcept
{
	return (value >= U'a' && value <= U'z') || (value >= U'A' && value <= U'Z') || (value >= U'0' && value <= U'9') ||
		   value == U'_' ;
}

//------------------------------------------------------------------------------
/**
 * @brief API separatorまたは一意完全対応の許可語境界を確認します
 * @param[in] at API内容境界
 * @param[in] api API map
 * @param[in] owners 全最適owner
 * @param[in] allowed 参照内容境界の分割許可
 * @param[in] runs 完全一致の全範囲
 * @return 固定の安全境界ならtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool safeBoundary( const std::size_t at, const std::span<const Scalar> api,
								const std::span<const Ownership> owners, const std::vector<bool> &allowed,
								const std::span<const ExactRun> runs)
{
	// 元本文の端は既存の文書境界として扱う
	if( at == 0 || at == api.size()) {
		return true ;
	}
	// 明示separatorの位置で区切るときもASCII単語の途中を切らない
	if( api[at - 1].end < api[at].begin) {
		return true ;
	}
	// ASCII語の連続部分は参照語境界があっても切らない
	if( asciiWord( api[at - 1].value) && asciiWord( api[at].value)) {
		return false ;
	}
	// 同点の位置差または参照削除がある境界を許可しない
	if( owners[at - 1].reference == K_NO_INDEX || owners[at].reference == K_NO_INDEX ||
	   owners[at].reference != owners[at - 1].reference + 1 || !allowed[owners[at].reference]) {
		return false ;
	}
	return std::any_of( runs.begin(), runs.end(),
					   //------------------------------------------------------------------------------
					   /**
					    * @brief 境界を跨ぐ一意完全一致を検査します
					    * @param[in] run 一致範囲
					    * @return 境界の両側が同じ根拠内ならtrue
					    */
					   //------------------------------------------------------------------------------
					   [at]( const ExactRun &run)
					   {
						   return run.unique && run.begin < at && at < run.end ;
					   }) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 局所最適経路の距離と最悪の連続不一致を有界rowで測ります
 * @param[in] reference 参照単位全文
 * @param[in] api 対応候補
 * @param[in,out] cells 窓全体cell予算
 * @param[in] stop 協調取消し
 * @return 全局所最小経路を含む固定診断値
 */
//------------------------------------------------------------------------------
[[nodiscard]] EditMeasure editMeasure( const std::span<const Scalar> reference, const std::span<const Scalar> api,
									  std::size_t &cells, const std::stop_token stop)
{
	const auto needed = (reference.size() + 1) * (api.size() + 1) ; // 追加計算cell数
	// 部分単位だけ成功させず、窓全体を固定予算内へ収める
	if( needed > K_SPEAKER_TEXT_MATRIX_CELLS - cells) {
		throw Limited{} ;
	}
	cells += needed ;
	/** @brief 最短prefixの最悪gapを別々に伝播します */
	struct Cell {
		std::uint16_t cost = 0 ; // 最短prefix距離
		std::uint16_t trailing = 0 ; // 全最短prefixの末尾不一致最大
		std::uint16_t maximum = 0 ; // 全最短prefixの内部不一致最大
	} ;
	std::vector<Cell> previous( api.size() + 1) ; // 前参照row
	std::vector<Cell> current( api.size() + 1) ; // 現参照row
	// 空参照に対する挿入列を初期化する
	for( std::size_t j = 0; j <= api.size(); ++j) { // API prefix長
		previous[j] = {static_cast<std::uint16_t>( j), static_cast<std::uint16_t>( j), static_cast<std::uint16_t>( j)} ;
	}
	// 最短局所編集経路の同点も最大gapへ含める
	for( std::size_t i = 1; i <= reference.size(); ++i) { // 参照prefix長
		checkCancelled( stop) ;
		current[0] = {static_cast<std::uint16_t>( i), static_cast<std::uint16_t>( i), static_cast<std::uint16_t>( i)} ;
		// 既知三方向の最小costを求め、その全同点を集約する
		for( std::size_t j = 1; j <= api.size(); ++j) { // API prefix長
			const bool same = reference[i - 1].value == api[j - 1].value ; // 完全一致
			const int  cost = std::min(
				 {previous[j].cost + 1, current[j - 1].cost + 1, previous[j - 1].cost + (same ? 0 : 1)}) ; // 局所距離
			Cell	   cell{static_cast<std::uint16_t>( cost), 0, 0} ; // 全同点の最大値を集める
			const auto include =
				//------------------------------------------------------------------------------
				/**
				 * @brief 一つの最適prefix遷移をgapへ追加します
				 * @param[in] prior 遷移前
				 * @param[in] matched 完全一致対角ならtrue
				 */
				//------------------------------------------------------------------------------
				[&]( const Cell prior, const bool matched)
				{
					const auto trailing = static_cast<std::uint16_t>( matched ? 0 : prior.trailing + 1) ;
					cell.trailing = std::max( cell.trailing, trailing) ;
					cell.maximum = std::max( {cell.maximum, prior.maximum, trailing}) ;
				} ; // 同期rowだけを借用
			// 最適削除経路の連続gapを引継ぐ
			if( previous[j].cost + 1 == cost) {
				include( previous[j], false) ;
			}
			// 最適挿入経路の連続gapを引継ぐ
			if( current[j - 1].cost + 1 == cost) {
				include( current[j - 1], false) ;
			}
			// 最適対角の完全一致はgapを止め、置換は延ばす
			if( previous[j - 1].cost + (same ? 0 : 1) == cost) {
				include( previous[j - 1], same) ;
			}
			current[j] = cell ;
		}
		previous.swap( current) ;
	}
	return {previous.back().cost, previous.back().maximum} ;
}

//------------------------------------------------------------------------------
/**
 * @brief 固定範囲に完全一致anchorが存在するか測ります
 * @param[in] begin API内容先頭
 * @param[in] end API内容終端
 * @param[in] runs 完全一致列
 * @param[in] reference 全参照
 * @param[in] api 全API
 * @param[in] ri 参照index
 * @param[in] ai API index
 * @param[in,out] comparisons 実比較予算
 * @return 最大一意anchor長
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::size_t internalAnchor( const std::size_t begin, const std::size_t end,
										 const std::span<const ExactRun> runs, const std::span<const Scalar> reference,
										 const std::span<const Scalar> api, const GramIndex &ri, const GramIndex &ai,
										 std::size_t &comparisons)
{
	std::size_t longest = 0 ; // 候補範囲内で実証した最大内部anchor
	// 単位の外へ跨ぐ完全一致を切り、内部範囲で一意性を再確認する
	for( const auto &run : runs) { // 全最適位置が同じ完全一致列
		const auto first = std::max( begin, run.begin) ; // 範囲内の先頭
		const auto last = std::min( end, run.end) ; // 範囲内の終端
		// 十分長い内部完全一致だけをanchor候補へする
		if( last > first && last - first >= K_MINIMUM_ANCHOR &&
		   uniqueAnchor( api.subspan( first, last - first), reference, api, ri, ai, comparisons)) {
			longest = std::max( longest, last - first) ;
		}
	}
	return longest ;
}
} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 通常CTCの許可語境界と対象全turnから参照を作ります
 * @param[in] samples 最大120秒の元sample窓
 * @param[in] segments 組立て前の元PCM時刻付きCTC結果。同期借用
 * @param[in] turns 対象APIの全file raw cluster区間。参照側の話者番号は使いません
 * @param[in] stop 協調取消し
 * @return 原順の全単位。token不正/上限は空unitsと固定理由、取消しは例外
 * @throws OperationError 不正な対象turnまたは取消し
 * @see koefude::makeNativeTextReferenceWindow
 */
//------------------------------------------------------------------------------
TextAlignmentReferenceWindow makeNativeTextReferenceWindow( const AsrSampleWindow			  samples,
														   const std::span<const TextSegment> segments,
														   const std::span<const SpeakerTurn> turns,
														   const std::stop_token			  stop)
{
	TextAlignmentReferenceWindow result ; // 失敗時は空unitsで窓を中立に保つ
	result.samples = samples ;
	checkCancelled( stop) ;
	// 参照の原点と対象turnの上限は推論前後で一貫して検証する
	if( samples.end <= samples.begin || samples.end > K_MAXIMUM_TIMELINE_SAMPLES ||
	   samples.end - samples.begin > K_SPEAKER_TEXT_WINDOW_SAMPLES || turns.size() > K_MAXIMUM_SEGMENTS) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	std::vector<SpeakerTurn> ordered( turns.begin(), turns.end()) ; // 対象のraw IDだけを使う
	std::sort( ordered.begin(), ordered.end(),
			  //------------------------------------------------------------------------------
			  /**
			   * @brief 初出時刻とraw IDで表示番号を固定します
			   * @param[in] a 比較左
			   * @param[in] b 比較右
			   * @return 初出順ならtrue
			   */
			  //------------------------------------------------------------------------------
			  []( const SpeakerTurn &a, const SpeakerTurn &b)
			  {
				  return std::pair( a.startMs, a.speaker) < std::pair( b.startMs, b.speaker) ;
			  }) ;
	std::map<int, int> ids ; // raw IDから対象表示番号へのmap
	// 全file turnの初出順を使い、選んだ二窓内の番号へ振り直さない
	for( const auto &turn : ordered) { // 対象の保存turn
		checkCancelled( stop) ;
		// 不正対象turnから音響支持を作らない
		if( turn.startMs < 0 || turn.endMs <= turn.startMs || turn.endMs > K_MAXIMUM_TIMELINE_MS || turn.speaker < -1) {
			throw OperationError( ErrorKind::INVALID_TIMELINE) ;
		}
		// 未知clusterは番号へ昇格しない
		if( turn.speaker >= 0 && !ids.contains( turn.speaker)) {
			ids.emplace( turn.speaker, static_cast<int>( ids.size()) + 1) ;
		}
	}
	try {
		std::size_t	 tokens = 0 ; // 全窓のtoken数
		std::size_t	 textBytes = 0 ; // 全参照UTF-8容量
		std::size_t	 turnChecks = 0 ; // 全token候補照合予算
		std::int64_t previousStart = -1 ; // 全segmentに渡る発火開始の単調性
		std::int64_t previousEnd = -1 ; // 終端の単調性。隣接frameの重なりは許す
		// 通常nativeの元segment順とtoken許可境界を保持する
		for( const auto &segment : segments) { // 組立て前の通常CTC結果
			checkCancelled( stop) ;
			// tokenのない補助本文を時間按分で対応させない
			if( segment.text.empty() || segment.tokens.empty() || !segment.tokenTimesAreAnchors || segment.startMs < 0 ||
			   segment.endMs <= segment.startMs || segment.startMs < static_cast<std::int64_t>( samples.begin / 16) ||
			   segment.endMs > static_cast<std::int64_t>( (samples.end + 15) / 16)) {
				throw OperationError( ErrorKind::INVALID_TIMELINE) ;
			}
			// 全参照の固定容量をcopy前に検証する
			if( segment.tokens.size() > K_SPEAKER_TEXT_TOKENS - tokens ||
			   segment.text.size() > K_SPEAKER_TEXT_WINDOW_BYTES - textBytes) {
				throw Limited{} ;
			}
			tokens += segment.tokens.size() ;
			textBytes += segment.text.size() ;
			std::vector<Scalar> validation ; // strict UTF-8と内容上限の検証用
			appendContent( segment.text, 0, validation, stop) ;
			std::size_t				   byteBegin = 0 ; // 次token本文の先頭
			TextAlignmentReferenceUnit atom ; // can_split_afterまでの不可分語
			bool					   first = true ; // atom最初の候補を採用する
			// 発火範囲を個別に照合し、単語内候補変化を中立に残す
			for( const auto &token : segment.tokens) { // 本文を順に覆うtoken
				checkCancelled( stop) ;
				// 未被覆/逆順/不正byte境界や時刻を補完しない
				if( token.textEnd <= byteBegin || token.textEnd > segment.text.size() ||
				   !byteBoundary( segment.text, token.textEnd) || token.startMs < segment.startMs ||
				   token.endMs > segment.endMs || token.endMs <= token.startMs || token.startMs < previousStart ||
				   token.endMs < previousEnd) {
					throw OperationError( ErrorKind::INVALID_TIMELINE) ;
				}
				previousStart = token.startMs ;
				previousEnd = token.endMs ;
				const auto candidate = tokenCandidate( token, ordered, ids, turnChecks) ; // 発火支持
				// 語頭の候補を保持し、続く曖昧発火で中立へ戻す
				if( first) {
					atom.speaker = candidate.first ;
					atom.reason = candidate.second ;
					first = false ;
				} else if( atom.speaker != candidate.first || candidate.first < 1) {
					atom.speaker = -1 ;
					atom.reason = candidate.second == SpeakerTextReason::OVERLAP ? SpeakerTextReason::OVERLAP
																				 : SpeakerTextReason::CANDIDATE_CHANGE ;
				}
				atom.text.append( segment.text, byteBegin, token.textEnd - byteBegin) ;
				auto owned = token ; // atom内相対byte終端へ一度だけ戻す
				owned.textEnd = atom.text.size() ;
				atom.tokens.push_back( owned) ;
				byteBegin = token.textEnd ;
				// 不可分語の途中では候補ごとのunitを作らない
				if( !token.canSplitAfter && byteBegin != segment.text.size()) {
					continue ;
				}
				// 同一候補の隣接atomだけを許可終端でまとめる
				if( atom.speaker > 0 && !result.units.empty() && result.units.back().speaker == atom.speaker &&
				   result.units.back().tokens.back().canSplitAfter &&
				   joinable( result.units.back().tokens.back().endMs, atom.tokens.front().startMs, atom.speaker, ordered,
							ids)) {
					auto	  &unit = result.units.back() ; // 同じ対象候補の直前単位
					const auto base = unit.text.size() ; // token byte終端の補正原点
					unit.text += atom.text ;
					// 既存本文末尾を新token終端へ足し、二重補正しない
					for( auto part : atom.tokens) { // 現atomの一発火token
						part.textEnd += base ;
						unit.tokens.push_back( part) ;
					}
				} else {
					// 中立単位も省略せず、固定単位上限を守る
					if( result.units.size() == K_SPEAKER_TEXT_UNITS) {
						throw Limited{} ;
					}
					result.units.push_back( std::move( atom)) ;
				}
				atom = {} ;
				first = true ;
			}
			// 全byteをtokenで一度だけ覆った結果に限る
			if( byteBegin != segment.text.size()) {
				throw OperationError( ErrorKind::INVALID_TIMELINE) ;
			}
		}
	} catch( const Limited &) {
		result.units.clear() ;
		result.status = SpeakerTextReason::RESOURCE_LIMIT ;
	} catch( const std::bad_alloc &) {
		result.units.clear() ;
		result.status = SpeakerTextReason::RESOURCE_LIMIT ;
	} catch( const OperationError &error) {
		// 取消しを参照不足として隠さず操作へ返す
		if( error.kind() == ErrorKind::CANCELLED) {
			throw ;
		}
		result.units.clear() ;
		result.status = SpeakerTextReason::INVALID_TOKENS ;
	}
	return result ;
}

//------------------------------------------------------------------------------
/**
 * @brief 全最小経路ownerと内部一意anchorにより方式3の表示範囲を返します
 * @param[in] chunkIndex 元API完全応答番号
 * @param[in] original 原UTF-8本文。同期borrow。変更・正規化して保存しません
 * @param[in] reference 中立を含む全参照単位。同期borrow
 * @param[in] stop 協調取消し
 * @return 原byteの全partition。上限/不正参照/OOMは全中立、取消しは例外
 * @throws OperationError 原API UTF-8不正または取消し
 * @note network/model/UI/保存I/Oを呼びません。threshold変更の引数は設けません
 * @see koefude::projectSpeakerText
 */
//------------------------------------------------------------------------------
SpeakerTextWindowProjection projectSpeakerText( const std::size_t chunkIndex, const std::string_view original,
											   const TextAlignmentReferenceWindow &reference,
											   const std::stop_token			   stop)
{
	checkCancelled( stop) ;
	SpeakerTextWindowProjection fallback ; // 重い確保前に全byte中立partを準備する
	bool						originalValidated = false ; // 参照不正と原API UTF-8不正を区別する
	// 正常な空API応答も空partitionとして保持する
	if( !original.empty()) {
		fallback.parts.push_back( {chunkIndex, 0, original.size(), -1, {}, SpeakerTextReason::UNMATCHED}) ;
	}
	try {
		std::vector<Scalar> api ; // 原byteへ戻せる内容map
		appendContent( original, K_NO_INDEX, api, stop) ;
		originalValidated = true ;
		fallback.contentScalars = api.size() ;
		// 不正/上限参照や正常空参照を推測で補わない
		if( reference.status != SpeakerTextReason::SINGLE_CANDIDATE || reference.units.empty()) {
			fallback.status = reference.status == SpeakerTextReason::SINGLE_CANDIDATE ? SpeakerTextReason::UNMATCHED
																					  : reference.status ;
			// 早期保留も固定理由をpartへ残す
			for( auto &part : fallback.parts) { // 最大一つの全文part
				part.reason = fallback.status ;
			}
			return fallback ;
		}
		// 単位上限を越えた参照のprefixだけで採用しない
		if( reference.units.size() > K_SPEAKER_TEXT_UNITS) {
			throw Limited{} ;
		}
		std::vector<Scalar>								 ref ; // 中立を含む全内容map
		std::vector<std::pair<std::size_t, std::size_t>> ranges ; // 単位ごとの内容半開範囲
		std::vector<bool>								 allowed( K_SPEAKER_TEXT_SCALARS + 1, false) ; // 参照語境界
		allowed[0] = true ;
		std::size_t bytes = 0 ; // 全参照本文容量
		std::size_t tokenCount = 0 ; // 全参照token容量
		// 全単位を残して一つの原順graphへ入れる
		for( std::size_t unit = 0; unit < reference.units.size(); ++unit) { // 参照単位番号
			checkCancelled( stop) ;
			const auto &source = reference.units[unit] ; // 検証対象の単位
			// native来歴のtoken欠落を旧保存行として黙って利用しない
			if( reference.origin == TextReferenceOrigin::NATIVE_PARAKEET_TOKENS && source.tokens.empty()) {
				throw OperationError( ErrorKind::INVALID_TIMELINE) ;
			}
			// 不正speaker/理由や容量を照合前に拒否する
			if( source.speaker == 0 || source.speaker < -1 ||
			   (source.speaker > 0 && source.reason != SpeakerTextReason::SINGLE_CANDIDATE) ||
			   source.text.size() > K_SPEAKER_TEXT_WINDOW_BYTES - bytes ||
			   source.tokens.size() > K_SPEAKER_TEXT_TOKENS - tokenCount) {
				throw Limited{} ;
			}
			bytes += source.text.size() ;
			tokenCount += source.tokens.size() ;
			const auto begin = ref.size() ; // 全参照内容の単位開始
			appendContent( source.text, unit, ref, stop) ;
			ranges.emplace_back( begin, ref.size()) ;
			std::size_t position = begin ; // token byte終端を内容境界へ戻す位置
			std::size_t previous = 0 ; // token本文の前終端
			// 保存snapshotの不正tokenを表示時の語境界へ使わない
			for( const auto &token : source.tokens) { // 対象単位内の一token
				// 親本文を越えたりbyte途中の境界は採用しない
				if( token.textEnd <= previous || token.textEnd > source.text.size() ||
				   !byteBoundary( source.text, token.textEnd)) {
					throw OperationError( ErrorKind::INVALID_TIMELINE) ;
				}
				// 許可token終端までの内容scalar数を確定する
				while( position < ref.size() && ref[position].end <= token.textEnd) {
					++position ;
				}
				// CTCの語途中終端を分割境界へ昇格しない
				if( token.canSplitAfter) {
					allowed[position] = true ;
				}
				previous = token.textEnd ;
			}
			// tokenがある単位は全文被覆を必須とする
			if( !source.tokens.empty() && previous != source.text.size()) {
				throw OperationError( ErrorKind::INVALID_TIMELINE) ;
			}
			allowed[ref.size()] = source.tokens.empty() || source.tokens.back().canSplitAfter ;
		}
		// separatorだけの片側から話者支持を作らない
		if( api.empty() || ref.empty()) {
			return fallback ;
		}
		SpeakerTextWindowProjection result ; // 全条件成立後だけfallbackと入替える
		result.contentScalars = api.size() ;
		const auto	owners = optimalOwners( ref, api, stop, result) ; // 全最小経路の合意
		std::size_t features = 0 ; // 両側indexの総特徴数
		const auto	ri = gramIndex( ref, features) ; // 中立を除かない参照index
		const auto	ai = gramIndex( api, features) ; // 同じAPI窓のindex
		const auto	runs = exactRuns( owners, ref, api, ri, ai, result.featureComparisons) ; // 全完全列
		// 一単位に対し一つの連続owner範囲がある場合だけ採用を検討する
		for( std::size_t unit = 0; unit < reference.units.size(); ++unit) { // 参照単位番号
			checkCancelled( stop) ;
			// 中立と短発言はgraphに保持したまま、話者採用だけ保留する
			if( reference.units[unit].speaker < 1) {
				continue ;
			}
			std::size_t begin = K_NO_INDEX ; // 全経路owner一致の先頭
			std::size_t end = 0 ; // 全経路owner一致の終端
			std::size_t count = 0 ; // 候補文字数。穴があればend-beginと一致しない
			// 全API内容から当単位の所有候補だけを数える
			for( std::size_t at = 0; at < owners.size(); ++at) { // API内容位置
				// 同一単位に全経路が一致する文字だけを含める
				if( owners[at].owner == unit) {
					begin = std::min( begin, at) ;
					end = at + 1 ;
					++count ;
				}
			}
			// 曖昧な内部穴を飛越して長い発言へまとめない
			if( begin == K_NO_INDEX || count < K_MINIMUM_CONTENT || end - begin != count) {
				continue ;
			}
			const auto originalBegin = begin ; // trim量検証の開始値
			const auto originalEnd = end ; // trim量検証の終了値
			// 不安全な先頭は内側12内容字まで中立に残す
			while( begin < end && begin - originalBegin <= K_MAXIMUM_TRIM &&
				  !safeBoundary( begin, api, owners, allowed, runs)) {
				++begin ;
			}
			// 不安全な末尾も同じ固定量だけ内側へ動かす
			while( end > begin && originalEnd - end <= K_MAXIMUM_TRIM &&
				  !safeBoundary( end, api, owners, allowed, runs)) {
				--end ;
			}
			// trim後も両端/最小量を再確認し、安全な端がなければ単位を保留する
			if( begin - originalBegin > K_MAXIMUM_TRIM || originalEnd - end > K_MAXIMUM_TRIM ||
			   end - begin < K_MINIMUM_CONTENT || !safeBoundary( begin, api, owners, allowed, runs) ||
			   !safeBoundary( end, api, owners, allowed, runs)) {
				continue ;
			}
			const auto exact = static_cast<std::size_t>( std::count_if( owners.begin() + begin, owners.begin() + end,
				//------------------------------------------------------------------------------
				/**
				 * @brief 全経路で字面が一致した文字を数えます
				 * @param[in] owner 一文字の合意
				 * @return 全消費辺が完全一致ならtrue
				 */
				//------------------------------------------------------------------------------
				[]( const Ownership &owner)
				{
					return owner.exact ;
				})) ; // 完全一致字数
			// 修復語だけの長さを完全一致根拠へ数えない
			if( exact < K_MINIMUM_CONTENT) {
				continue ;
			}
			const auto anchor = internalAnchor( begin, end, runs, ref, api, ri, ai,
											   result.featureComparisons) ; // 内部一意anchor
			// 同じ文の反復や単調文字列を発言支持としない
			if( anchor < K_MINIMUM_ANCHOR) {
				continue ;
			}
			const auto [rbegin, rend] = ranges[unit] ; // 参照全文の内容範囲
			const auto measured =
				editMeasure( std::span( ref).subspan( rbegin, rend - rbegin), std::span( api).subspan( begin, end - begin),
							result.matrixCells, stop) ; // 固定編集条件
			// 全文編集20%と最大12字gapを両方満たす場合だけ採用する
			if( measured.distance * 5 > std::max( rend - rbegin, end - begin) || measured.mismatch > K_MAXIMUM_MISMATCH) {
				continue ;
			}
			result.matches.push_back( {unit, api[begin].begin, api[end - 1].end, end - begin, exact, anchor,
									  measured.distance, measured.mismatch, begin - originalBegin + originalEnd - end}) ;
			result.projectedScalars += end - begin ;
		}
		std::sort( result.matches.begin(), result.matches.end(),
				  //------------------------------------------------------------------------------
				  /**
				   * @brief API原byteの昇順へ固定します
				   * @param[in] a 左範囲
				   * @param[in] b 右範囲
				   * @return 原順ならtrue
				   */
				  //------------------------------------------------------------------------------
				  []( const SpeakerTextMatch &a, const SpeakerTextMatch &b)
				  {
					  return a.byteBegin < b.byteBegin ;
				  }) ;
		std::size_t cursor = 0 ; // 次にpartitionへ入れる原byte
		// 支持範囲と中立補集合で全原byteを一度だけ覆う
		for( const auto &match : result.matches) { // 採用した原順の単位
			// 曖昧な範囲が内部に入った場合は後続実装の不変条件違反として保留する
			if( match.byteBegin < cursor) {
				throw OperationError( ErrorKind::INVALID_TIMELINE) ;
			}
			// 短い中立/句読点/空段落も削除せず独立partへ残す
			if( match.byteBegin > cursor) {
				result.parts.push_back( {chunkIndex, cursor, match.byteBegin, -1, {}, SpeakerTextReason::UNMATCHED}) ;
			}
			const int speaker = reference.units[match.referenceUnit].speaker ; // 対象候補番号
			// 中立を挟まない連続同話者だけを一blockにする
			if( !result.parts.empty() && result.parts.back().speaker == speaker &&
			   result.parts.back().byteEnd == match.byteBegin) {
				result.parts.back().byteEnd = match.byteEnd ;
				result.parts.back().referenceUnits.push_back( match.referenceUnit) ;
			} else {
				result.parts.push_back( {chunkIndex,
										match.byteBegin,
										match.byteEnd,
										speaker,
										{match.referenceUnit},
										SpeakerTextReason::SINGLE_CANDIDATE}) ;
			}
			cursor = match.byteEnd ;
		}
		// 原文末尾の改行/emoji/未対応本文もそのまま残す
		if( cursor < original.size()) {
			result.parts.push_back( {chunkIndex, cursor, original.size(), -1, {}, SpeakerTextReason::UNMATCHED}) ;
		}
		result.status = result.matches.empty() ? SpeakerTextReason::UNMATCHED : SpeakerTextReason::SINGLE_CANDIDATE ;
		// 全byte連結不変を成立条件にして、部分表示だけを成功へしない
		if( !validateSpeakerTextPartition( original, result, reference)) {
			throw OperationError( ErrorKind::INVALID_TIMELINE) ;
		}
		return result ;
	} catch( const Limited &) {
		fallback.status = SpeakerTextReason::RESOURCE_LIMIT ;
	} catch( const std::bad_alloc &) {
		fallback.status = SpeakerTextReason::RESOURCE_LIMIT ;
	} catch( const OperationError &error) {
		// 取消しと原API UTF-8不正を結果なし/上限として隠さない
		if( error.kind() == ErrorKind::CANCELLED || (error.kind() == ErrorKind::INVALID_INPUT && !originalValidated)) {
			throw ;
		}
		fallback.status = SpeakerTextReason::INVALID_TOKENS ;
	}
	// 上限や参照失敗の固定理由は中立partにも保持する
	for( auto &part : fallback.parts) { // 最大一つの全文part
		part.reason = fallback.status ;
	}
	return fallback ;
}

//------------------------------------------------------------------------------
/**
 * @brief 原UTF-8境界、全partition、単位と話者候補を照合します
 * @param[in] original 原API本文
 * @param[in] projection 検証する表示範囲
 * @param[in] reference 同じ窓の音響参照
 * @return 全byteを一度だけ覆い、採用単位と単一候補が一致する場合true
 * @see koefude::validateSpeakerTextPartition
 */
//------------------------------------------------------------------------------
bool validateSpeakerTextPartition( const std::string_view original, const SpeakerTextWindowProjection &projection,
								  const TextAlignmentReferenceWindow &reference) noexcept
{
	try {
		// no-allocのstrict復号により不正scalar/NULも公開validate境界で拒否する
		for( std::size_t at = 0; at < original.size();) { // 次の原byte
			static_cast<void>( nextScalar( original, at)) ;
		}
	} catch( const OperationError &) {
		return false ; // 固定検証失敗へ変換し、noexcept境界を越えない
	}
	std::size_t cursor = 0 ; // 検証済み原byte終端
	std::size_t chunk = K_NO_INDEX ; // 全partsで共通の元chunk番号
	// 原順の正のbyte範囲を検査し、空白や中立も含め全byte被覆を保証する
	for( const auto &part : projection.parts) { // 連結する表示part
		// byte途中/欠落/重複/未知speakerを拒否する
		if( part.byteBegin != cursor || part.byteEnd <= cursor || part.byteEnd > original.size() ||
		   !byteBoundary( original, part.byteBegin) || !byteBoundary( original, part.byteEnd) || part.speaker == 0 ||
		   part.speaker < -1 || (chunk != K_NO_INDEX && chunk != part.chunkIndex)) {
			return false ;
		}
		chunk = part.chunkIndex ;
		// 中立partへ参照支持を偽装しない
		if( part.speaker < 0 && !part.referenceUnits.empty()) {
			return false ;
		}
		// 話者付きpartには単一の対象候補支持を必須とする
		if( part.speaker > 0 && (part.referenceUnits.empty() || part.reason != SpeakerTextReason::SINGLE_CANDIDATE)) {
			return false ;
		}
		// 当該unitが同じ対象候補を持つことを全て確認する
		for( const auto index : part.referenceUnits) { // 支持参照単位番号
			// 未知番号や中立を既知候補へ昇格しない
			if( index >= reference.units.size() || reference.units[index].speaker != part.speaker ||
			   reference.units[index].reason != SpeakerTextReason::SINGLE_CANDIDATE) {
				return false ;
			}
		}
		cursor = part.byteEnd ;
	}
	return cursor == original.size() ;
}
} // namespace koefude
