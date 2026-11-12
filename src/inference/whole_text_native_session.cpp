//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_native_session.cpp
 * @brief 住所不変callbackとnative計数sessionの依存順を管理します
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1284の有限計画と生成しない計数を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1285の有界逐次補正を接続
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1311の生成資源停止をnative取消しより先に分類
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
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
#include "inference/whole_text_native_session.h"
#include "../../native/whole_text/generator.h"
#include "inference/whole_text_ministral_contract.h"
#include <span>
#include <algorithm>
#include <array>
#include <atomic>
#include <iterator>
#include <memory>
#include <utility>
#include <windows.h>
namespace koefude {
namespace {
std::atomic<bool> hostSessionActive = false ; // 一process一session、保持状態では再openしません
std::atomic<WholeTextNativeCountSession::State *> retainedState = nullptr ; // allocationなしの1件保持
//------------------------------------------------------------------------------
/**
 * @brief C ABI statusを本文なし例外へ変換します
 * @param[in] status 元status
 * @param[in] code 元診断
 * @param[in] stop user停止
 */
//------------------------------------------------------------------------------
void checkStatus( std::uint32_t status, std::uint32_t code, std::stop_token stop)
{
	checkCancelled( stop) ;
	requireWholeTextPlan( status <= 9, WholeTextPlanDiagnostic::COUNTER_RECEIPT) ;
	// 成功以外は元診断を保持します
	if( status != 0) {
		throw OperationError(
			status == 7 ? ErrorKind::CANCELLED
						: (status == 1 || status == 2 ? ErrorKind::INVALID_INPUT : ErrorKind::INFERENCE_FAILED),
			code) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief borrow byteをABIへ変換します
 * @param[in] s 同期text
 * @return POD
 */
//------------------------------------------------------------------------------
KoefudeWtBytesV1 bytes( std::string_view s)
{
	requireWholeTextPlan( s.size() <= 65536, WholeTextPlanDiagnostic::PLAN_BYTES) ;
	return {reinterpret_cast<const std::uint8_t *>( s.data()), static_cast<std::uint32_t>( s.size()), 0} ;
}
//------------------------------------------------------------------------------
/**
 * @brief 固定名だけをOS loaderで開きます
 * @param[in] path 検証済み絶対path
 * @return 関数とmodule owner
 */
//------------------------------------------------------------------------------
detail::WholeTextCounterApi loadCounter( const std::filesystem::path &path)
{
	// この同期境界だけで使用する検証値
	const auto handle =
		LoadLibraryExW( path.c_str(), nullptr,
					   LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32) ; // 任意PATH探索なし
	requireWholeTextPlan( handle != nullptr, WholeTextPlanDiagnostic::RUNTIME_CAPABILITY) ;
	detail::WholeTextCounterApi api ;
	api.module = std::shared_ptr<void>( handle,
		//------------------------------------------------------------------------------
		/**
		 * @brief shared_ptrが所有するDLLの初期参照を一回解放要求します
		 * @param[in] module 正常LoadLibraryExWで取得し、shared_ptrが最後まで保持した非NULLのHMODULEです
		 * @note FreeLibraryの返却codeは検査しません。unloadの成功確認ではなく、DLL内APIの有効期間はこのmodule ownerへ束縛されます
		 */
		//------------------------------------------------------------------------------
		[]( void *module)
		{
			FreeLibrary( static_cast<HMODULE>( module)) ;
		}) ;
	// 固定exportのfunction pointer変換だけに限定しmodule寿命で束縛します
	api.info = reinterpret_cast<decltype(api.info)>( GetProcAddress( handle, "koefude_whole_text_info_v2")) ;
	api.open = reinterpret_cast<decltype(api.open)>( GetProcAddress( handle, "koefude_whole_text_open_counter_v2")) ;
	api.measure = reinterpret_cast<decltype(api.measure)>( GetProcAddress( handle, "koefude_whole_text_measure_v2")) ;
	api.close = reinterpret_cast<decltype(api.close)>( GetProcAddress( handle, "koefude_whole_text_close_session_v2")) ;
	api.promote = reinterpret_cast<decltype(api.promote)>(
		GetProcAddress( handle, "koefude_whole_text_promote_v2")) ; // module寿命内だけ有効
	api.generate = reinterpret_cast<decltype(api.generate)>(
		GetProcAddress( handle, "koefude_whole_text_generate_chunk_v2")) ; // 同期entry
	return api ;
}
} // namespace
/** @brief callback/native/DLL/leaseの住所不変ownerです */
struct WholeTextNativeCountSession::State {
	/// 最後まで生存する固定baseline
	std::shared_ptr<WholeTextResourceGuard>			guard ;
	/// user停止
	std::stop_token									stop ;
	/// DLL lease
	std::shared_ptr<const VerifiedWholeTextRuntime> runtime ;
	/// model lease
	VerifiedModel									model ;
	/// DLL module
	detail::WholeTextCounterApi						api ;
	/// 明示用途
	WholeTextNativeExecutionUse						use = WholeTextNativeExecutionUse::COUNT_ONLY ;
	/// 不変方式
	WholeTextGenerationKind							kind = WholeTextGenerationKind::GEMMA_THOUGHT_FINAL ;
	/// weak通知だけ
	std::shared_ptr<WholeTextBoundedProgressGate>	progressGate ;
	/// 不変計画値
	WholeTextBoundedPromotion						promotion ;
	/// closeで上書きしない元native値
	WholeTextNativeFailureSnapshot					failure ;
	/// measure禁止への一方向遷移
	bool											promoted = false ;
	/// 生成済みattempt数
	std::uint32_t									generated = 0 ;
	/// 前の生成index
	std::uint32_t									lastIndex = 0 ;
	/// 前の生成core終端
	std::uint64_t									coreEnd = 0 ;
	/// live ID
	std::uint64_t									session = 0 ;
	/// host側の次receipt照合
	std::uint64_t									sequence = 0 ;
	/// 二重host entry拒否
	std::atomic_flag								busy = ATOMIC_FLAG_INIT ;
	/// 再計数禁止
	bool											failed = false ;
	/// 入口で取得した一session枠
	bool											hostSlot = false ;
	/// native所有権が残る、または不明
	bool											requiresCleanup = false ;
	/// cleanup再試行はしません
	bool											closeAttempted = false ;
	/// close元status
	std::uint32_t									closeStatus = 0 ;
	/// closeの全元POD
	KoefudeWtCloseResultV2							closeResult{} ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 有効で整合するclosed=1だけを解放確認にします
	 */
	//------------------------------------------------------------------------------
	void											attemptClose() noexcept
	{
		// 既に試行したcleanupを繰り返しません
		if( closeAttempted || !requiresCleanup) {
			return ;
		}
		closeAttempted = true ;
		// retained ownerから終端後の通知先へ触れません
		if( progressGate) {
			progressGate->disable() ;
		}
		// 不正openでIDが不明な場合はowner全体を保持します
		if( session == 0 || !api.close) {
			return ;
		}
		closeResult = {sizeof(closeResult), 2, 0, 0, 0, 0} ;
		closeStatus = api.close( session, &closeResult) ;
		const bool header =
			closeResult.structSize == sizeof(closeResult) && closeResult.abiVersion == 2 && closeResult.reserved == 0 ;
		const bool disposition =
			(closeStatus == 0 && closeResult.diagnostic == 0 && closeResult.engineCode == 0) ||
			(closeStatus == 9 && closeResult.diagnostic == static_cast<std::uint32_t>( KoefudeWtDiagnostic::CLEANUP)) ||
			(closeStatus == 7 && closeResult.diagnostic == static_cast<std::uint32_t>( KoefudeWtDiagnostic::CANCELLED)) ;
		// Busy/closed1等の矛盾は解放済みへ昇格しません
		if( header && disposition && closeResult.closed == 1) {
			requiresCleanup = false ;
			session = 0 ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief user停止/資源をC callbackで照会します
	 * @param[in] data 生存するState
	 * @return 停止1
	 */
	//------------------------------------------------------------------------------
	static std::uint32_t __cdecl shouldStop( void *data) noexcept
	{
		// 同じownerの同期借用
		const auto &s = *static_cast<State *>( data) ;
		return s.stop.stop_requested() || s.guard->poll() ? 1u : 0u ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief jobを所有しない同期通知です
	 * @param[in] data 生存State
	 * @param[in] stage 段階
	 * @param[in] completed 完了数
	 * @param[in] total 総数
	 * @return 停止1
	 */
	//------------------------------------------------------------------------------
	static std::uint32_t __cdecl progress( void *data, std::uint32_t stage, std::uint32_t completed,
										  std::uint32_t total) noexcept
	{
		const auto &s = *static_cast<State *>( data) ; // State住所は全native寿命で不変
		return s.progressGate ? s.progressGate->notify( stage, completed, total) : 0 ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 生のclose結果をownerなしの値へ写します
	 * @return 固定証拠
	 */
	//------------------------------------------------------------------------------
	WholeTextNativeCloseReceipt closeReceipt() const noexcept
	{
		const bool header = closeResult.structSize == sizeof(closeResult) && closeResult.abiVersion == 2 &&
							closeResult.reserved == 0 ; // 元header
		const bool disposition =
			(closeStatus == 0 && closeResult.diagnostic == 0 && closeResult.engineCode == 0) ||
			(closeStatus == 9 && closeResult.diagnostic == static_cast<std::uint32_t>( KoefudeWtDiagnostic::CLEANUP)) ||
			(closeStatus == 7 &&
			 closeResult.diagnostic == static_cast<std::uint32_t>( KoefudeWtDiagnostic::CANCELLED)) ; // 解放可能な結果
		return {requiresCleanup ? WholeTextNativeOwnership::RETAINED_UNKNOWN
								: WholeTextNativeOwnership::CONFIRMED_RELEASED,
				closeAttempted,
				header,
				disposition,
				closeAttempted && !requiresCleanup,
				requiresCleanup,
				closeStatus,
				closeResult.structSize,
				closeResult.abiVersion,
				closeResult.closed,
				closeResult.diagnostic,
				closeResult.engineCode,
				closeResult.reserved} ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 解放確認済みの依存資源だけを逆順で閉じます
	 */
	//------------------------------------------------------------------------------
	~State()
	{
		api = {} ;
		model = {} ;
		runtime.reset() ;
		guard.reset() ;
		// 保持状態はdestructorを呼ばず、この枠も保持し続けます
		if( hostSlot) {
			hostSessionActive.store( false) ;
		}
	}
} ;
namespace {
//------------------------------------------------------------------------------
/**
 * @brief 解放未確認ならallocationなしで一件だけownerを保持します
 * @param[in] state release済みの単独所有pointer。null可
 */
//------------------------------------------------------------------------------
void disposeCountState( WholeTextNativeCountSession::State *state) noexcept
{
	// move元の空ownerは処理しません
	if( !state) {
		return ;
	}
	state->attemptClose() ;
	// 未確認closeではDLL/callback/model/runtimeを先に破棄しません
	if( state->requiresCleanup) {
		state->failed = true ;
		retainedState.store( state) ;
		return ;
	}
	delete state ; // このprivate ownerの唯一の解放点
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 未確認cleanupでprocess寿命のownerを保持しているか読みます
 * @return restartが必要ならtrue。resetや自動close再試行の入口はありません
 * @see koefude::detail::wholeTextCounterRestartRequired
 */
//------------------------------------------------------------------------------
bool detail::wholeTextCounterRestartRequired() noexcept
{
	return retainedState.load() != nullptr ;
}
//------------------------------------------------------------------------------
/**
 * @brief 通常OS loaderの固定exportを一回解決します
 * @param[in] path 検証済み絶対DLL
 * @return module所有API
 * @see koefude::detail::loadWholeTextCounter
 */
//------------------------------------------------------------------------------
detail::WholeTextCounterApi detail::loadWholeTextCounter( const std::filesystem::path &path)
{
	return loadCounter( path) ;
}
//------------------------------------------------------------------------------
/**
 * @brief V2情報をmodel検証前に完全照合します
 * @param[in] info V2全field
 * @param[in] use CountOnlyはmask1、生成はmask3
 * @param[in] kind 照合する生成方式です。MINISTRAL_PLAINはPlain用、それ以外はGemma用の固定infoを選びます
 * @throws OperationError capability/固定値差
 * @see koefude::detail::validateWholeTextCounterInfo
 */
//------------------------------------------------------------------------------
void detail::validateWholeTextCounterInfo( const KoefudeWtInfoV2 &info, WholeTextNativeExecutionUse use,
										  WholeTextGenerationKind kind)
{
	// 宣言fieldだけを比較し、ABI paddingの不定byteを契約へ含めません
	const auto &expected = kind == WholeTextGenerationKind::MINISTRAL_PLAIN
							   ? K_MINISTRAL16_GB_NATIVE_INFO
							   : wholeTextNativeExpectedInfo() ; // V1のengine/生成予算は不変
	requireWholeTextPlan(
		info.structSize == sizeof(info) && info.abiVersion == 2 && info.common.structSize == expected.structSize &&
			info.common.abiVersion == expected.abiVersion && info.common.pointerBits == expected.pointerBits &&
			info.common.flags == expected.flags &&
			std::equal( std::begin( info.common.engineCommit), std::end( info.common.engineCommit),
					   std::begin( expected.engineCommit)) &&
			std::equal( std::begin( info.common.templateSha256), std::end( info.common.templateSha256),
					   std::begin( expected.templateSha256)) &&
			info.common.contextTokens == expected.contextTokens &&
			info.common.prefixTokensMax == expected.prefixTokensMax &&
			info.common.outputTokensMax == expected.outputTokensMax &&
			info.common.analysisSampledMax == expected.analysisSampledMax &&
			info.common.finalSampledMax == expected.finalSampledMax && info.capabilitiesMask != 0 &&
			(info.capabilitiesMask & ~3u) == 0 &&
			(info.capabilitiesMask & (use == WholeTextNativeExecutionUse::SERIAL_GENERATION ? 3u : 1u)) ==
				(use == WholeTextNativeExecutionUse::SERIAL_GENERATION ? 3u : 1u) &&
			info.accountingContract == 2 && info.planContract == 2 && info.sessionContract == 2 &&
			info.maximumRenderBytes == 65536 && info.maximumSessions == 1 && info.reserved[0] == 0 &&
			info.reserved[1] == 0,
		WholeTextPlanDiagnostic::RUNTIME_CAPABILITY) ;
}
//------------------------------------------------------------------------------
/**
 * @brief privateなopen結果だけを所有します
 * @param[in] state 住所不変owner
 * @see koefude::WholeTextNativeCountSession::WholeTextNativeCountSession(std::unique_ptr< koefude::WholeTextNativeCountSession::State >)
 */
//------------------------------------------------------------------------------
WholeTextNativeCountSession::WholeTextNativeCountSession( std::unique_ptr<State> state) noexcept
	: state_( std::move( state))
{
}
//------------------------------------------------------------------------------
/**
 * @brief native→DLL→model→runtime→callbackの順に解放します
 * @see koefude::WholeTextNativeCountSession::~WholeTextNativeCountSession
 */
//------------------------------------------------------------------------------
WholeTextNativeCountSession::~WholeTextNativeCountSession()
{
	disposeCountState( state_.release()) ;
}
//------------------------------------------------------------------------------
/**
 * @brief callback住所を変えず所有権を移します
 * @param[in,out] other 元owner
 * @see koefude::WholeTextNativeCountSession::WholeTextNativeCountSession(koefude::WholeTextNativeCountSession &&)
 */
//------------------------------------------------------------------------------
WholeTextNativeCountSession::WholeTextNativeCountSession( WholeTextNativeCountSession &&other) noexcept = default ;
//------------------------------------------------------------------------------
/**
 * @brief 自己代入以外では旧native Stateを終了処理し、住所不変のState所有権をotherから移します
 * @param[in,out] other 移動元のsession owner。自己代入時は不変、それ以外はStateを失い空になります
 * @return このobjectへの借用参照。object破棄後は使用できません
 * @note 旧Stateの解放が未確認ならdisposeCountStateの保持経路を使います。正常終了や資源回収を偽って保証せず、例外は送出しません
 */
//------------------------------------------------------------------------------
WholeTextNativeCountSession &WholeTextNativeCountSession::operator=( WholeTextNativeCountSession &&other) noexcept
{
	// 自身へのmoveでは現在のownerを閉じません
	if( this != &other) {
		disposeCountState( state_.release()) ;
		state_ = std::move( other.state_) ;
	}
	return *this ;
}
//------------------------------------------------------------------------------
/**
 * @brief primary native callの元値を読みます
 * @return closeと別の固定値
 * @see koefude::WholeTextNativeCountSession::failureSnapshot
 */
//------------------------------------------------------------------------------
WholeTextNativeFailureSnapshot WholeTextNativeCountSession::failureSnapshot() const noexcept
{
	return state_ ? state_->failure : WholeTextNativeFailureSnapshot{} ;
}
//------------------------------------------------------------------------------
/**
 * @brief native session IDを読みます
 * @return move元/close後0
 * @see koefude::WholeTextNativeCountSession::id
 */
//------------------------------------------------------------------------------
std::uint64_t WholeTextNativeCountSession::id() const noexcept
{
	return state_ ? state_->session : 0 ;
}
//------------------------------------------------------------------------------
/**
 * @brief 元close PODを固定値で返します。再試行しません
 * @return 解放証拠
 * @see koefude::WholeTextNativeCountSession::closeChecked
 */
//------------------------------------------------------------------------------
WholeTextNativeCloseReceipt WholeTextNativeCountSession::closeChecked() noexcept
{
	// moved/consumed ownerに二回目のnative closeを発行しません
	if( !state_) {
		return {} ;
	}
	auto &s = *state_ ; // 不変住所
	// 同期call内からの再entryではouter ownerを解放しません
	if( s.busy.test_and_set()) {
		WholeTextNativeCloseReceipt receipt ; // raw Busyを値で返すだけ
		receipt.rawStatus = 8 ;
		receipt.restartRequired = false ;
		return receipt ;
	}
	s.attemptClose() ;
	const auto receipt = s.closeReceipt() ; // 依存破棄前に元証拠を取得
	s.busy.clear() ;
	disposeCountState( state_.release()) ; // 未確認時はallocationなし保持
	return receipt ;
}
//------------------------------------------------------------------------------
/**
 * @brief 明示closeの失敗を呼出側へ返します
 * @throws OperationError 元code/取消し
 * @see koefude::WholeTextNativeCountSession::close
 */
//------------------------------------------------------------------------------
void WholeTextNativeCountSession::close()
{
	const auto stop = state_ ? state_->stop : std::stop_token{} ; // 依存解放前に旧停止値を保持
	const auto receipt = closeChecked() ; // 明示試行はここだけ
	requireWholeTextPlan( receipt.releaseConfirmed, WholeTextPlanDiagnostic::COUNTER_RECEIPT) ;
	checkStatus( receipt.rawStatus,
				receipt.rawEngineCode != 0 ? static_cast<std::uint32_t>( receipt.rawEngineCode) : receipt.rawDiagnostic,
				stop) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 同じmodel/vocabで全候補を計数します
 * @param[in] p 一候補
 * @return 検証receipt
 * @see koefude::WholeTextNativeCountSession::measure
 */
//------------------------------------------------------------------------------
WholeTextTokenReceipt WholeTextNativeCountSession::measure( const WholeTextChunkPrompt &p)
{
	requireWholeTextPlan( state_ && state_->session && !state_->failed && !state_->promoted,
						 WholeTextPlanDiagnostic::COUNTER_RECEIPT) ;
	// 同じownerの同期借用
	auto &s = *state_ ;
	// 二重entryは現在のcallの所有flagを維持して拒否します
	if( s.busy.test_and_set()) {
		throw OperationError( ErrorKind::INFERENCE_FAILED, static_cast<std::uint32_t>( KoefudeWtDiagnostic::BUSY)) ;
	}
	try {
		static_cast<void>( s.guard->poll( true)) ;
		s.guard->check( s.stop) ;
		KoefudeWtMeasureRequestV2 request{
			sizeof(request),	2,	   bytes( p.system), bytes( p.user), bytes( p.core), bytes( p.before), bytes( p.after),
			p.candidateBinding, {0, 0}} ;
		KoefudeWtMeasureResultV2 r{} ;
		r.structSize = sizeof(r) ;
		r.abiVersion = 2 ;
		// C ABIが返した元status
		const auto status = s.api.measure( s.session, &request, &r) ;
		s.failure = {status, r.diagnostic, r.engineCode} ;
		static_cast<void>( s.guard->poll( true)) ;
		s.guard->check( s.stop, r.diagnostic) ;
		checkStatus( status, r.engineCode != 0 ? static_cast<std::uint32_t>( r.engineCode) : r.diagnostic, s.stop) ;
		requireWholeTextPlan( r.structSize == sizeof(r) && r.abiVersion == 2 && r.sessionId == s.session &&
								 r.sequence == s.sequence + 1 && r.accountingContract == 2 && (r.flags & ~3u) == 0 &&
								 r.reserved[0] == 0 && r.reserved[1] == 0 && r.diagnostic == 0 && r.engineCode == 0,
							 WholeTextPlanDiagnostic::COUNTER_RECEIPT) ;
		WholeTextTokenReceipt				  out{r.sessionId,	  r.sequence,	 r.prefixTokens,	 r.coreTokens,
								  r.beforeTokens, r.afterTokens, (r.flags & 1) != 0, (r.flags & 2) != 0,
								  r.bytes,		  r.sha256,		 r.candidateBinding, r.tokenSha256,
								  r.receiptSha256} ;
		// この同期境界だけで使用する検証値
		const auto							  rendered = renderWholeTextGeneration( s.kind, p.system, p.user) ;
		// 元の無編集byteの借用
		const std::array<std::string_view, 6> text{p.system, p.user, rendered, p.core, p.before, p.after} ;
		// 全要素を元の順序で処理し、範囲の終端で停止します
		for( std::size_t i = 0; i < text.size(); ++i) {
			requireWholeTextPlan( out.bytes[i] == text[i].size() && out.sha256[i] == wholeTextSha256( text[i]),
								 WholeTextPlanDiagnostic::COUNTER_RECEIPT) ;
		}
		requireWholeTextPlan( out.candidateBinding == p.candidateBinding &&
								 out.receiptSha256 == wholeTextReceiptDigest( out),
							 WholeTextPlanDiagnostic::COUNTER_RECEIPT) ;
		++s.sequence ;
		s.busy.clear() ;
		return out ;
	} catch( ...) {
		s.failed = true ;
		s.busy.clear() ;
		throw ;
	}
}
namespace {
//------------------------------------------------------------------------------
/**
 * @brief 同じStateへDLLと全export/infoを一回だけ取得します
 * @param[in] runtime 両lease
 * @param[in] use 明示用途
 * @param[in] stop 停止
 * @param[in] loader 一回の同期loader
 * @param[out] failureNative null可。元info失敗を保存
 * @return model未取得の住所不変owner
 */
//------------------------------------------------------------------------------
std::unique_ptr<WholeTextNativeCountSession::State>
acquireCounterState( std::shared_ptr<const VerifiedWholeTextRuntime> runtime, WholeTextNativeExecutionUse use,
					std::stop_token stop, const detail::WholeTextCounterLoader &loader,
					WholeTextNativeFailureSnapshot *failureNative)
{
	requireWholeTextPlan( !detail::wholeTextCounterRestartRequired(), WholeTextPlanDiagnostic::RUNTIME_CAPABILITY) ;
	auto state = std::make_unique<WholeTextNativeCountSession::State>() ; // I/O前の一State
	requireWholeTextPlan( !hostSessionActive.exchange( true), WholeTextPlanDiagnostic::RUNTIME_CAPABILITY) ;
	state->hostSlot = true ;
	state->kind = runtime->variant() == WholeTextRuntimeVariant::MINISTRAL14_B_AVX2_NO_REPACK16_GB
					  ? WholeTextGenerationKind::MINISTRAL_PLAIN
					  : WholeTextGenerationKind::GEMMA_THOUGHT_FINAL ;
	state->runtime = std::move( runtime) ;
	state->stop = stop ;
	state->use = use ;
	try {
		state->api = loader( state->runtime->path()) ;
		requireWholeTextPlan( state->api.module && state->api.info && state->api.open && state->api.measure &&
								 state->api.close,
							 WholeTextPlanDiagnostic::RUNTIME_CAPABILITY) ;
		requireWholeTextPlan( use == WholeTextNativeExecutionUse::COUNT_ONLY ||
								 (state->api.promote && state->api.generate),
							 WholeTextPlanDiagnostic::RUNTIME_CAPABILITY) ;
		KoefudeWtInfoV2 info{} ; // この同期処理で使う所有値または検証値
		info.structSize = sizeof(info) ;
		info.abiVersion = 2 ;
		const auto infoStatus = state->api.info( &info) ; // infoには診断/engine fieldがありません
		state->failure = {infoStatus, 0, 0} ; // 非0元statusを取得前失敗でも保持
		requireWholeTextPlan( infoStatus == 0, WholeTextPlanDiagnostic::RUNTIME_CAPABILITY) ;
		detail::validateWholeTextCounterInfo( info, use, state->kind) ;
		requireWholeTextPlan( info.capabilitiesMask == state->runtime->capabilitiesMask(),
							 WholeTextPlanDiagnostic::RUNTIME_CAPABILITY) ;
		return state ;
	} catch( ...) {
		// DLL info/export段階の元値もStateの破棄より先に保存します
		if( failureNative) {
			*failureNative = state->failure ;
		}
		disposeCountState( state.release()) ;
		throw ;
	}
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 小verifier/loaderを同じopen境界へ注入します
 * @param[in] modelRoot local root
 * @param[in] runtime 両lease
 * @param[in] guard 同じ資源baseline
 * @param[in] binding 全束縛
 * @param[in] stop 停止
 * @param[in] verifier 一回だけ検証
 * @param[in] loader 同期DLL loader
 * @return live owner
 * @param[in] use 旧CountOnlyを既定とし生成は明示します
 * @param[out] failureClose null可。失敗時は元close証拠、native未取得なら空です
 * @param[in] progressGate null可。job targetへweakだけを持つ所有gate
 * @param[out] failureNative null可。失敗時の元native値をclose前に保存します
 * @see koefude::detail::openWholeTextCountSessionWithDependencies
 */
//------------------------------------------------------------------------------
WholeTextNativeCountSession detail::openWholeTextCountSessionWithDependencies(
	const std::filesystem::path &modelRoot, std::shared_ptr<const VerifiedWholeTextRuntime> runtime,
	std::shared_ptr<WholeTextResourceGuard> guard, const WholeTextDigest &binding, std::stop_token stop,
	const WholeTextModelVerifier &verifier, const WholeTextCounterLoader &loader, WholeTextNativeExecutionUse use,
	WholeTextNativeCloseReceipt *failureClose, std::shared_ptr<WholeTextBoundedProgressGate> progressGate,
	WholeTextNativeFailureSnapshot *failureNative)
{
	checkCancelled( stop) ;
	requireWholeTextPlan( runtime && runtime->accountingV2() && guard && binding != WholeTextDigest{} && verifier &&
							 loader,
						 WholeTextPlanDiagnostic::RUNTIME_CAPABILITY) ;
	requireWholeTextPlan( !wholeTextCounterRestartRequired(),
						 WholeTextPlanDiagnostic::RUNTIME_CAPABILITY) ; // 保持中はallocation/I/O前拒否
	requireWholeTextPlan( runtime->variant() != WholeTextRuntimeVariant::MINISTRAL14_B_AVX2_NO_REPACK16_GB ||
							 guard->policy() == WholeTextResourcePolicy::LOW_MEMORY16_GB,
						 WholeTextPlanDiagnostic::RUNTIME_CAPABILITY) ; // 全入口で同じ16GB policy
	static_cast<void>( guard->poll( true)) ;
	guard->check( stop) ;
	auto state = acquireCounterState( runtime, use, stop, loader,
									 failureNative) ; // PCM解放後、model検証前の一回load
	try {
		requireWholeTextPlan( state->runtime == runtime && state->use == use && state->session == 0 &&
								 !state->requiresCleanup,
							 WholeTextPlanDiagnostic::RUNTIME_CAPABILITY) ;
		state->guard = std::move( guard) ;
		state->stop = stop ;
		state->progressGate = std::move( progressGate) ;
		checkCancelled( stop) ;
		state->model = verifier( modelRoot, wholeTextNativeModelId( state->runtime->variant()),
								stop) ; // info拒否時は0回
		requireWholeTextPlan( state->model.lock && state->model.path.is_absolute(),
							 WholeTextPlanDiagnostic::RUNTIME_CAPABILITY) ;
		state->guard->check( stop) ; // verifier中の取消しではnative openを始めません
		// この同期境界だけで使用する検証値
		const auto &path = state->model.path.native() ;
		requireWholeTextPlan( !path.empty() && path.size() <= 32767, WholeTextPlanDiagnostic::PLAN_BYTES) ;
		KoefudeWtCounterOpenV2		 request{sizeof(request),
										 2,
										 path.c_str(),
										 static_cast<std::uint32_t>( path.size()),
										 0,
										 binding,
										 WholeTextNativeCountSession::State::shouldStop,
										 state->progressGate ? WholeTextNativeCountSession::State::progress : nullptr,
										 state.get()} ;
		KoefudeWtCounterOpenResultV2 result{sizeof(result), 2, 0, 0, 0} ;
		// C ABIが返した元status
		state->requiresCleanup = true ;
		const auto status = state->api.open( &request, &result) ;
		state->failure = {status, result.diagnostic, result.engineCode} ;
		// 成功後stopでも返ったsessionを最初に所有しcleanupを保証します
		state->session = result.sessionId ;
		// 正しい失敗PODでID0ならnative ownerがない契約を確認できます
		if( result.structSize == sizeof(result) && result.abiVersion == 2 && status > 0 && status <= 9 &&
		   result.sessionId == 0) {
			state->requiresCleanup = false ;
		}
		requireWholeTextPlan( result.structSize == sizeof(result) && result.abiVersion == 2,
							 WholeTextPlanDiagnostic::COUNTER_RECEIPT) ;
		static_cast<void>( state->guard->poll( true)) ;
		state->guard->check( stop, result.diagnostic) ;
		checkStatus( status, result.engineCode != 0 ? static_cast<std::uint32_t>( result.engineCode) : result.diagnostic,
					stop) ;
		requireWholeTextPlan( state->session != 0 && result.diagnostic == 0 && result.engineCode == 0,
							 WholeTextPlanDiagnostic::COUNTER_RECEIPT) ;
		return WholeTextNativeCountSession( std::move( state)) ;
	} catch( ...) {
		// 不整合や終了状態を分岐させ、未確定のまま後続へ進みません
		if( failureNative) {
			*failureNative = state->failure ;
		} // State解放/保持前に元値を所有
		state->attemptClose() ; // primary例外とは別に元close証拠を返します
		// 取得前の失敗は空receiptのままにします
		if( failureClose && state->closeAttempted) {
			*failureClose = state->closeReceipt() ;
		}
		disposeCountState( state.release()) ;
		throw ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 検証済みruntime→info→model一回→vocab一回の順に準備します
 * @param[in] modelRoot local root
 * @param[in] runtime runtime両lease
 * @param[in] guard 同operationの固定baseline
 * @param[in] binding 全要求束縛
 * @param[in] stop 停止
 * @return live計数session
 * @param[in] use 旧CountOnlyを既定とし生成は明示します
 * @param[out] failureClose null可。失敗時は元close証拠、native未取得なら空です
 * @param[in] progressGate null可。job targetへweakだけを持つ所有gate
 * @param[out] failureNative null可。失敗時の元native値をclose前に保存します
 * @see koefude::openWholeTextCountSession
 */
//------------------------------------------------------------------------------
WholeTextNativeCountSession openWholeTextCountSession(
	const std::filesystem::path &modelRoot, std::shared_ptr<const VerifiedWholeTextRuntime> runtime,
	std::shared_ptr<WholeTextResourceGuard> guard, const WholeTextDigest &binding, std::stop_token stop,
	WholeTextNativeExecutionUse use, WholeTextNativeCloseReceipt *failureClose,
	std::shared_ptr<WholeTextBoundedProgressGate> progressGate, WholeTextNativeFailureSnapshot *failureNative)
{
	return detail::openWholeTextCountSessionWithDependencies( modelRoot, std::move( runtime), std::move( guard), binding,
															 stop, verifyModel, detail::loadWholeTextCounter, use,
															 failureClose, std::move( progressGate), failureNative) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 有効なweak targetだけに同期通知します
 * @param[in] stage 段階
 * @param[in] completed 完了数
 * @param[in] total 総数
 * @return 停止なら1。例外は固定停止に変換します
 * @see koefude::WholeTextBoundedProgressGate::notify
 */
//------------------------------------------------------------------------------
std::uint32_t WholeTextBoundedProgressGate::notify( std::uint32_t stage, std::uint32_t completed,
												   std::uint32_t total) noexcept
{
	// 無効化後はweak targetをlockしません
	if( !enabled_.load()) {
		return 1 ;
	}
	try {
		const auto target = target_.lock() ; // 同期通知の間だけ生存
		return !target || target->notify( stage, completed, total) ? 1u : 0u ;
	} catch( ...) {
		enabled_.store( false) ;
		return 1 ;
	} // C ABIへ例外を出しません
}
//------------------------------------------------------------------------------
/**
 * @brief 計画とjournal封印後だけ昇格します
 * @param[in] p 全計画値
 * @see koefude::WholeTextNativeCountSession::promote
 */
//------------------------------------------------------------------------------
void WholeTextNativeCountSession::promote( const WholeTextBoundedPromotion &p)
{
	requireWholeTextBounded( state_ && state_->use == WholeTextNativeExecutionUse::SERIAL_GENERATION &&
								!state_->promoted && !state_->failed && state_->session != 0,
							WholeTextBoundedDiagnostic::CHUNK_RECEIPT) ;
	auto &s = *state_ ; // 同期owner
	// 同じStateへの二重entryを待機せず拒否します
	if( s.busy.test_and_set()) {
		throw OperationError( ErrorKind::INFERENCE_FAILED, static_cast<std::uint32_t>( KoefudeWtDiagnostic::BUSY)) ;
	}
	try {
		s.guard->check( s.stop) ;
		KoefudeWtPromoteRequestV2 r{sizeof(r),		 2,			 p.openBinding,	  p.planSha256,	 p.inputSha256,
									p.originalBytes, p.measures, p.measuredBytes, p.recordCount, p.generationCount,
									{0, 0}} ;
		KoefudeWtPromoteResultV2  out{sizeof(out), 2, 0, 0, 0, 0, 0} ; // 完全header
		const auto				  status = s.api.promote( s.session, &r, &out) ; // この同期処理で使う所有値または検証値
		s.failure = {status, out.diagnostic, out.engineCode} ; // 同model一回
		checkStatus( status, out.engineCode != 0 ? static_cast<std::uint32_t>( out.engineCode) : out.diagnostic, s.stop) ;
		requireWholeTextBounded( out.structSize == sizeof(out) && out.abiVersion == 2 && out.sessionId == s.session &&
									out.promoted == 1 && out.diagnostic == 0 && out.engineCode == 0 &&
									out.reserved == 0,
								WholeTextBoundedDiagnostic::CHUNK_RECEIPT) ;
		static_cast<void>( s.guard->poll( true)) ;
		s.guard->check( s.stop) ;
		s.promotion = p ;
		s.promoted = true ;
		s.busy.clear() ;
	} catch( ...) {
		s.failed = true ;
		s.busy.clear() ;
		throw ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 同じsessionで一coreだけ生成します
 * @param[in] p 同期borrow
 * @param[in] record 封印record
 * @param[in] input 入力SHA
 * @param[out] output 最大65536byteのhost領域
 * @return 本文を含まない成功receipt
 * @see koefude::WholeTextNativeCountSession::generateChunk
 */
//------------------------------------------------------------------------------
WholeTextBoundedChunkReceipt WholeTextNativeCountSession::generateChunk( const WholeTextChunkPrompt &p,
																		const WholeTextChunkRecord &record,
																		const WholeTextDigest	   &input,
																		std::span<std::uint8_t>		output)
{
	requireWholeTextBounded( state_ && state_->promoted && !state_->failed && record.receipt &&
								!record.verbatimWhitespace && output.size() > 0 && output.size() <= 65536,
							WholeTextBoundedDiagnostic::CHUNK_RECEIPT) ;
	auto &s = *state_ ; // 同一住所owner
	// 再entryではouter callのflagや資源を変更しません
	if( s.busy.test_and_set()) {
		throw OperationError( ErrorKind::INFERENCE_FAILED, static_cast<std::uint32_t>( KoefudeWtDiagnostic::BUSY)) ;
	}
	try {
		requireWholeTextBounded( input == s.promotion.inputSha256 && record.index < s.promotion.recordCount &&
									s.generated < s.promotion.generationCount &&
									(s.generated == 0 || record.index > s.lastIndex) && record.core.begin >= s.coreEnd,
								WholeTextBoundedDiagnostic::CHUNK_RECEIPT) ;
		const auto				 &receipt = *record.receipt ; // 受理済み計数値
		KoefudeWtMeasureResultV2  measured{sizeof(measured),
										   2,
										   receipt.session,
										   receipt.sequence,
										   2,
										   (receipt.fits ? 1u : 0u) | (receipt.completeTokenDigest ? 2u : 0u),
										   receipt.prefixTokens,
										   receipt.coreTokens,
										   receipt.beforeTokens,
										   receipt.afterTokens,
										   receipt.bytes,
										   receipt.sha256,
										   receipt.candidateBinding,
										   receipt.tokenSha256,
										   receipt.receiptSha256,
										   0,
										   0,
										   {0, 0}} ;
		KoefudeWtMeasureRequestV2 prompt{
			sizeof(prompt),		2,	   bytes( p.system), bytes( p.user), bytes( p.core), bytes( p.before), bytes( p.after),
			p.candidateBinding, {0, 0}} ; // 同期borrow
		KoefudeWtChunkRequestV2 request{sizeof(request), 2,		record.index,			0,		  record.core.begin,
										record.core.end, input, s.promotion.planSha256, measured, prompt,
										{0, 0}} ;
		KoefudeWtChunkResultV2	out{} ; // hostが先に確保した一bufferだけ
		out.structSize = sizeof(out) ;
		out.abiVersion = 2 ;
		out.text = output.data() ;
		out.textCapacity = static_cast<std::uint32_t>( output.size()) ;
		s.guard->check( s.stop) ;
		++s.generated ;
		s.lastIndex = record.index ;
		s.coreEnd = record.core.end ;
		const auto status = s.api.generate( s.session, &request, &out) ; // この同期処理で使う所有値または検証値
		s.failure = {status, out.diagnostic, out.engineCode} ; // retry0
		// 元native値を残し、実取消しと資源停止をnative statusの分類より先に確認します
		s.guard->check( s.stop, out.engineCode != 0 ? static_cast<std::uint32_t>( out.engineCode) : out.diagnostic) ;
		checkStatus( status, out.engineCode != 0 ? static_cast<std::uint32_t>( out.engineCode) : out.diagnostic, s.stop) ;
		requireWholeTextBounded(
			out.structSize == sizeof(out) && out.abiVersion == 2 && out.text == output.data() &&
				out.textCapacity == output.size() && out.textBytes <= output.size() && out.sessionId == s.session &&
				out.chunkIndex == record.index && out.naturalEog == 1 && out.coreBegin == record.core.begin &&
				out.coreEnd == record.core.end && out.planSha256 == s.promotion.planSha256 &&
				out.inputSha256 == input && out.expectedReceiptSha256 == receipt.receiptSha256 &&
				out.actualPrefixTokenSha256 == receipt.tokenSha256 && out.prefixTokens == receipt.prefixTokens &&
				out.renderedBytes == receipt.bytes[2] && out.renderedSha256 == receipt.sha256[2] &&
				out.reserved[0] == 0 && out.reserved[1] == 0 && out.diagnostic == 0 && out.engineCode == 0,
			WholeTextBoundedDiagnostic::CHUNK_RECEIPT) ;
		WholeTextBoundedChunkReceipt result{out.sessionId,
											out.chunkIndex,
											{out.coreBegin, out.coreEnd},
											out.planSha256,
											out.inputSha256,
											out.expectedReceiptSha256,
											out.actualPrefixTokenSha256,
											out.renderedSha256,
											out.bodySha256,
											out.generationReceiptSha256,
											out.renderedBytes,
											out.textBytes,
											out.prefixTokens,
											out.analysisSampledTokens,
											out.closingControlTokens,
											out.finalSampledTokens,
											true} ; // ownerなし
		validateWholeTextGeneratedReceipt(
			result, std::string_view( reinterpret_cast<const char *>( output.data()), out.textBytes),
			s.kind) ; // buffer寿命内だけ
		static_cast<void>( s.guard->poll( true)) ;
		s.guard->check( s.stop) ;
		s.busy.clear() ;
		return result ;
	} catch( ...) {
		s.failed = true ;
		s.busy.clear() ;
		throw ;
	}
}
} // namespace koefude
