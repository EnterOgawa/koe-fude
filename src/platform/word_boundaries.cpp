//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file word_boundaries.cpp
 * @brief Windows標準の日本語単語区切りを元UTF-8のtokenへ結び付けます
 * @version 1.0.0
 * @date 2026/09/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/10 OS境界取得とUTF-16/UTF-8の有界な対応付けを追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1414の実WinRT段階・HRESULT・解決言語分類を返却
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1416の同期range readerと共有対応処理を分離
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "platform/word_boundaries.h"

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <Windows.h>
#include <roapi.h>
#include <windows.data.text.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>

#include "platform/win_helpers.h"
#include "platform/word_boundary_ranges.h"

namespace koefude {
namespace {

/** @brief WinRTが所有する単語列を呼出しscope内で参照するinterface型です */
using NativeWordCollection =
	ABI::Windows::Foundation::Collections::IVectorView<ABI::Windows::Data::Text::WordSegment *> ;

//------------------------------------------------------------------------------
/**
 * @brief 容量検証後に全許可を解除し、token位置とUTF-8を検証します
 * @param[in,out] segment 同期中に排他所有する本文/token。容量超過前は変更せず、以後は全許可を解除します
 * @param[out] diagnostic 初期化する固定診断。位置不正はTOKEN_POSITION、空本文/tokenはCOMPLETEです
 * @param[out] wide 元本文を変換した所有UTF-16。開始時に空にし、変換前失敗時は空のままです
 * @return 準備成功ならtrue。token終端が本文byte数を超える時は全許可falseでfalseです
 * @throws OperationError 容量超過または不正UTF-8。不正UTF-8時は全tokenの許可解除後です
 * @note 本文・時刻は変更しません。OSを呼ばず、借用や出力への参照を保持しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool prepareWordBoundaries( TextSegment &segment, WordBoundaryDiagnostic &diagnostic, std::wstring &wide)
{
	diagnostic = {} ;
	wide.clear() ;
	// 外部本文を走査やOS処理へ渡す前に製品の有界な容量を確認する
	if( segment.text.size() > K_MAXIMUM_TEXT_BYTES || segment.tokens.size() > K_MAXIMUM_ASR_TOKENS) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
	bool validPositions = true ; // 全分割許可を消してから不正tokenの有無を判定する
	// 部分的な失敗が以前の分割許可を残さないよう、先に全tokenを保護する
	for( auto &token : segment.tokens) { // 本文は変更しない所有対応情報
		token.canSplitAfter = false ;
		validPositions = validPositions && token.textEnd <= segment.text.size() ;
	}
	// 不正な対応位置では一部の分割許可も残さず保守的に戻す
	if( !validPositions) {
		diagnostic.stage = WordBoundaryStage::TOKEN_POSITION ;
		return false ;
	}
	wide = toWide( segment.text) ;
	// 本文またはtokenが空なら、OSもrange readerも呼ばない
	if( wide.empty() || segment.tokens.empty()) {
		diagnostic.stage = WordBoundaryStage::COMPLETE ;
	}
	return true ;
}

//------------------------------------------------------------------------------
/**
 * @brief 準備済み本文へ全rangeを照合し、成功時だけtokenの分割許可を一括設定します
 * @param[in,out] segment 容量/UTF-8/token位置検証済みで全許可falseの排他所有DTO。本文・時刻は変更しません
 * @param[in] wideUnits 同じ元本文のUTF-16符号単位数。準備処理の出力長だけを渡します
 * @param[in] wordCount 取得した語数。wideUnits超過はWORD_COUNTとしてreader呼出し前に拒否します
 * @param[in] reader 一語を同期取得する非null借用callback。取得失敗時の診断も返します
 * @param[in,out] context readerの非null借用状態。呼出し全体で有効とし、ここでは型や所有を変更しません
 * @param[in,out] diagnostic 直前OSのHRESULT/言語を保持し、語数と最終到達段階を更新する固定診断
 * @return 全range検証成功ならtrue。語数/取得/範囲不正時は全tokenの許可をfalseのままfalseです
 * @note 0語は全許可falseで成功します。語数に比例するcopyを作らず、offset/bitmapだけを所有します
 * readerや確保の例外は全許可falseのまま伝播し、所有作業領域はscopeで解放します
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool mapWordBoundaryRanges( TextSegment &segment, const std::size_t wideUnits,
										 const std::uint32_t						 wordCount,
										 const word_boundary_detail::WordRangeReader reader, void *context,
										 WordBoundaryDiagnostic &diagnostic)
{
	diagnostic.stage = WordBoundaryStage::WORD_COUNT ;
	diagnostic.wordCount = wordCount ;
	// UTF-16符号単位数を超える語数は作業領域の確保やreader呼出しより前に拒否する
	if( wordCount > wideUnits) {
		return false ;
	}
	constexpr auto			 K_INVALID_OFFSET = std::numeric_limits<std::size_t>::max() ; // surrogate中間
	std::vector<std::size_t> offsets( wideUnits + 1, K_INVALID_OFFSET) ; // UTF-16→元byte
	std::size_t				 byte = 0 ; // 検証済みUTF-8の現在位置
	std::size_t				 unit = 0 ; // 同じ文字のUTF-16開始位置
	// 各Unicode文字を一度だけ走査し、二乗時間のprefix再変換を避ける
	while( byte < segment.text.size()) {
		offsets[unit] = byte ;
		const auto		  lead = static_cast<unsigned char>( segment.text[byte]) ; // 有効文字の先頭byte
		// 検証済み先頭byteから同じUnicode文字の符号単位数を求める
		const std::size_t length = lead < 0x80U ? 1 : lead < 0xe0U ? 2 : lead < 0xf0U ? 3 : 4 ;
		byte += length ;
		unit += length == 4 ? 2 : 1 ;
	}
	offsets[unit] = byte ;
	std::vector<bool> boundaries( segment.text.size() + 1, false) ; // 元本文内の許可byte終端
	// 語頭と語末を対応付け、句読点や空白を含むtoken終端も保持する
	for( std::uint32_t index = 0; index < wordCount; ++index) { // 取得元の現在の単語番号
		word_boundary_detail::WordRange source{} ; // 一語だけを保持する元UTF-16の開始と長さ
		// 一語の取得失敗では、先行rangeの許可もtokenへ反映しない
		if( !reader( context, index, source, diagnostic)) {
			return false ;
		}
		diagnostic.stage = WordBoundaryStage::WORD_RANGE ;
		// 不正な単語位置やsurrogate途中を推測で元byteへ戻さない
		if( source.start > wideUnits || source.length > wideUnits - source.start ||
		   offsets[source.start] == K_INVALID_OFFSET || offsets[source.start + source.length] == K_INVALID_OFFSET) {
			return false ;
		}
		boundaries[offsets[source.start]] = true ;
		boundaries[offsets[source.start + source.length]] = true ;
	}
	// 全取得結果が正常な場合だけtokenへ分割許可を設定する
	for( auto &token : segment.tokens) { // 親本文の終端と比較するCTC対応情報
		const auto next = std::string_view( segment.text).substr( token.textEnd) ; // 後続本文
		token.canSplitAfter = boundaries[token.textEnd] && !next.starts_with( "、") && !next.starts_with( "。") &&
							  !next.starts_with( ",") && !next.starts_with( ".") && !next.starts_with( "！") &&
							  !next.starts_with( "？") && !next.starts_with( "!") && !next.starts_with( "?") ;
	}
	diagnostic.stage = WordBoundaryStage::COMPLETE ;
	return true ;
}

//------------------------------------------------------------------------------
/**
 * @brief 同期借用するCOM単語列から一語の元UTF-16位置だけを取得します
 * @param[in,out] context 呼出しscopeがComPtrで所有するNativeWordCollectionへの非null借用pointer
 * @param[in] index get_Sizeで確認した語数未満の要素番号
 * @param[out] range 成功時だけ元UTF-16の開始と長さを設定する出力。借用COM参照を含めません
 * @param[in,out] diagnostic WORD_ACCESS/WORD_POSITIONと実HRESULTを更新し、言語/語数は保持します
 * @return 両OS取得成功ならtrue。false時も一時COM参照をscopeで解放し、到達診断を保持します
 * @note 列とcallbackは同じapartmentで同期使用し、借用pointerを保存しません。本文/tokenへ触れません
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool readNativeWordRange( void *context, const std::uint32_t index, word_boundary_detail::WordRange &range,
									   WordBoundaryDiagnostic &diagnostic)
{
	// 型消去したcallback境界を戻す。所有ComPtrの寿命が全読取りを覆い、別threadへ渡さない
	auto &words = *static_cast<NativeWordCollection *>( context) ;
	Microsoft::WRL::ComPtr<ABI::Windows::Data::Text::IWordSegment> word ; // 現在単語の所有参照
	ABI::Windows::Data::Text::TextSegment						   source{} ; // 元UTF-16の開始と長さ
	diagnostic.stage = WordBoundaryStage::WORD_ACCESS ;
	diagnostic.hresult = words.GetAt( index, &word) ;
	// 一要素の取得失敗は後続の位置APIへ渡さない
	if( FAILED(diagnostic.hresult)) {
		return false ;
	}
	diagnostic.stage = WordBoundaryStage::WORD_POSITION ;
	diagnostic.hresult = word->get_SourceTextSegment( &source) ;
	// 位置取得失敗と有効なHRESULTでの不正範囲を別段階として残す
	if( FAILED(diagnostic.hresult)) {
		return false ;
	}
	range = {source.StartPosition, source.Length} ;
	return true ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 診断を永続化せず、同じ日本語単語境界処理の成否だけを返します
 * @param[in,out] segment 同期中に排他所有するUTF-8本文とtoken対応。本文・時刻は変更しません
 * @return 全対応成功または空本文/tokenならtrue。OS/位置不正は全境界falseでfalseです
 * @throws OperationError 容量上限超過またはUTF-8検証失敗。容量超過は既存tokenを変更しません
 */
//------------------------------------------------------------------------------
bool markJapaneseWordBoundaries( TextSegment &segment)
{
	WordBoundaryDiagnostic diagnostic ; // 通常callerは診断を永続化せず、同じ実装の成否だけ使います
	return markJapaneseWordBoundaries( segment, diagnostic) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 全境界を先に保護し、各WinRT段階の実診断を固定分類して返します
 * @param[in,out] segment 本呼出し中に排他所有する元UTF-8本文とtoken対応。本文・時刻を変更しません
 * @param[out] diagnostic 先に初期化する所有出力。false時も最終段階・直前HRESULTを残します
 * @return 全対応成功または空入力ならtrue。OS/位置不正は全境界falseのままfalseです
 * @throws OperationError 容量上限またはUTF-8検証失敗。OS資源はscopeで解放します
 */
//------------------------------------------------------------------------------
bool markJapaneseWordBoundaries( TextSegment &segment, WordBoundaryDiagnostic &diagnostic)
{
	std::wstring wide ; // 容量とUTF-8を検証した所有UTF-16
	// 準備中の拒否はOSを呼ばず、共有処理の診断と全境界falseを保持する
	if( !prepareWordBoundaries( segment, diagnostic, wide)) {
		return false ;
	}
	// 空本文/tokenは準備だけで完了し、OSのactivationを不要にする
	if( diagnostic.stage == WordBoundaryStage::COMPLETE) {
		return true ;
	}
	Microsoft::WRL::Wrappers::RoInitializeWrapper apartment( RO_INIT_MULTITHREADED) ; // 対称解放
	const HRESULT initialized = apartment ; // 既存STAも利用し、成功時だけRAIIで対称に終了
	diagnostic.stage = WordBoundaryStage::INITIALIZING ;
	diagnostic.hresult = initialized ;
	// OS初期化に失敗した環境では単語途中の分割へfallbackしない
	if( FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
		return false ;
	}
	Microsoft::WRL::ComPtr<ABI::Windows::Data::Text::IWordsSegmenterFactory> factory ; // OS factory
	diagnostic.stage = WordBoundaryStage::ACTIVATING ;
	diagnostic.hresult = RoGetActivationFactory(
		Microsoft::WRL::Wrappers::HStringReference( RuntimeClass_Windows_Data_Text_WordsSegmenter).Get(),
		IID_PPV_ARGS(&factory)) ;
	// Windows標準のfactoryを借用せず、本scopeで所有する
	if( FAILED(diagnostic.hresult)) {
		return false ;
	}
	Microsoft::WRL::ComPtr<ABI::Windows::Data::Text::IWordsSegmenter> segmenter ; // 日本語の区切り器
	diagnostic.stage = WordBoundaryStage::CREATING ;
	diagnostic.hresult =
		factory->CreateWithLanguage( Microsoft::WRL::Wrappers::HStringReference( L"ja").Get(), &segmenter) ;
	// 日本語を明示し、OS表示言語に依存する暗黙の選択を避ける
	if( FAILED(diagnostic.hresult)) {
		return false ;
	}
	Microsoft::WRL::Wrappers::HString resolved ; // OSが実際に選択した言語の所有値
	diagnostic.stage = WordBoundaryStage::RESOLVING_LANGUAGE ;
	diagnostic.hresult = segmenter->get_ResolvedLanguage( resolved.GetAddressOf()) ;
	// 日本語辞書がない場合のUnicode文字単位fallbackを単語境界と呼ばない
	if( FAILED(diagnostic.hresult)) {
		return false ;
	}
	unsigned int			languageSize = 0 ; // OSが返すUTF-16符号単位数
	const auto				language = resolved.GetRawBuffer( &languageSize) ; // HStringの寿命中だけ借用
	const std::wstring_view resolvedLanguage( language, languageSize) ; // OSが返した実分類だけに使用
	diagnostic.language = resolvedLanguage.starts_with( L"ja") ? WordBoundaryLanguage::JAPANESE
						  : resolvedLanguage == L"und"		  ? WordBoundaryLanguage::UNDETERMINED
															  : WordBoundaryLanguage::OTHER ;
	// 未解決言語や別言語規則では全区間の保守的な表示を維持する
	if( diagnostic.language != WordBoundaryLanguage::JAPANESE) {
		diagnostic.stage = WordBoundaryStage::LANGUAGE_MISMATCH ;
		return false ;
	}
	Microsoft::WRL::ComPtr<NativeWordCollection> words ; // OS所有単語列の参照を保持
	diagnostic.stage = WordBoundaryStage::TOKENIZING ;
	diagnostic.hresult = segmenter->GetTokens(
		Microsoft::WRL::Wrappers::HStringReference( wide.c_str(), static_cast<unsigned int>( wide.size())).Get(), &words) ;
	// UTF-16本文は同期API中だけ借用し、OS結果を別の所有参照で受け取る
	if( FAILED(diagnostic.hresult)) {
		return false ;
	}
	unsigned int count = 0 ; // OSの単語数を無制限に信用しないための検証値
	diagnostic.stage = WordBoundaryStage::WORD_COUNT ;
	diagnostic.hresult = words->get_Size( &count) ;
	diagnostic.wordCount = count ;
	// 語数の取得失敗では値を対応処理へ渡さず、実HRESULTを残す
	if( FAILED(diagnostic.hresult)) {
		return false ;
	}
	return mapWordBoundaryRanges( segment, wide.size(), count, readNativeWordRange, words.Get(), diagnostic) ;
}

namespace word_boundary_detail {

//------------------------------------------------------------------------------
/**
 * @brief 内部range入口をnativeと同じ容量/位置/UTF-8準備と一括対応処理へ接続します
 * @param[in,out] segment 排他所有する元本文/token。容量超過前は非変更、以後の失敗時は全許可falseです
 * @param[in] wordCount readerの語数。元UTF-16符号単位数超過は読取り前に拒否します
 * @param[in] reader 全読取り中に有効な同期callback。読取りが必要なら非nullとします
 * @param[in,out] context 同期読取り中だけ借用する非nullのreader状態。呼出し後に保持しません
 * @param[out] diagnostic 開始時に初期化する段階/語数。OS未使用のため言語はUNOBSERVED、HRESULTはreader失敗時だけ変わります
 * @return 全対応または空本文/tokenならtrue。位置/語数/range/reader失敗時は全許可falseでfalseです
 * @throws OperationError 本文/token上限超過または不正UTF-8。reader/確保の例外も伝播します
 */
//------------------------------------------------------------------------------
bool markWordBoundariesFromRanges( TextSegment &segment, const std::uint32_t wordCount, const WordRangeReader reader,
								  void *context, WordBoundaryDiagnostic &diagnostic)
{
	std::wstring wide ; // 共通のUTF-8検証から得た所有UTF-16
	// 準備中に拒否した場合はreaderへ一度も進まず、その診断を返す
	if( !prepareWordBoundaries( segment, diagnostic, wide)) {
		return false ;
	}
	// nativeと同じ空入力の完了条件を保持し、架空の語数取得を記録しない
	if( diagnostic.stage == WordBoundaryStage::COMPLETE) {
		return true ;
	}
	return mapWordBoundaryRanges( segment, wide.size(), wordCount, reader, context, diagnostic) ;
}

} // namespace word_boundary_detail
} // namespace koefude
