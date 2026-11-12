//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_application.cpp
 * @brief PCM照合から全補助資料・解放・native生成まで直列に実行します
 * @version 1.0.0
 * @date 2026/10/19
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/19 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/19 明示CPU試用の通常vertical workflowを追加
 * 0.1.0 Shunji Ogawa 2026/10/19 一回実測の受動観測と証拠保持を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 固定AVX2方式と通常workerの検証接続を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1285の有界逐次補正を接続
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1287の明示診断と隔離接続を追加
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1301の補助CPU接続と限定検証を追加
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1313の再decode前から共通RAM予算を接続
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1319の生成前照合と拒否時明示closeを追加
 * 0.1.0 Shunji Ogawa 2026/10/22 ADR1333の無進捗監視と保存前資源終端を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1343の文章専用補正を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1424の既知の行頭空白だけを整形
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "inference/whole_text_application.h"
#include "core/audio_identity.h"
#include "core/history.h"
#include "core/shared_asr_windows.h"
#include "inference/local_engines.h"
#include "inference/whisper_cpu_runtime.h"
#include "inference/whole_text_native_corrector.h"
#include "inference/whole_text_observation.h"
#include "platform/audio_decoder.h"
#include "platform/win_helpers.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <type_traits>
#include <utility>
#include <windows.h>

namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief 通常/有界factoryの実補助closureを一つに固定します
 * @param[in] diagnostics null可、closureが同期呼出し終了まで共有所有
 * @param[in] services nullは実依存。非nullはclosure呼出し終了まで生存する内部試験borrow
 * @return 補助専用入口から同じ共有loopへ渡すclosure。一般ASR入口を選びません
 * @see koefude::detail::makeAuxiliaryWhisperTranscriber
 */
//------------------------------------------------------------------------------
detail::AuxiliaryWhisperTranscriber
detail::makeAuxiliaryWhisperTranscriber( std::shared_ptr<CorrectionDiagnosticCapture> diagnostics,
										const detail::WhisperWindowServices			*services)
{
	return
		//------------------------------------------------------------------------------
		/**
		 * @brief 二factoryに共通の実補助入口です
		 * @param[in] audio 全PCM
		 * @param[in] options 固定設定
		 * @param[in] windows 全窓
		 * @param[in] stop 停止
		 * @param[in] progress 同期通知
		 * @param[in] observer 容量監視
		 * @param[in] use 全文用途
		 * @return context/module解放済みの全raw結果
		 */
		//------------------------------------------------------------------------------
		[diagnostics = std::move( diagnostics), services]( const AudioData &audio, const EngineOptions &options,
														 std::span<const AsrSampleWindow> windows, std::stop_token stop,
														 const ProgressCallback	   &progress,
														 const RecognitionObserver &observer, AsrTimelineUse use)
		{
			LocalAsrEngine engine ; // 同じ同期workerだけで使用
			return engine.transcribeAuxiliaryWindows( audio, options, windows, stop, progress, observer, use,
													 diagnostics.get(), services) ;
		} ;
}
namespace {
//------------------------------------------------------------------------------
/**
 * @brief 既存の同期通知へ同じguardの協調pollを接続します
 * @param[in] callback 元通知、空可
 * @param[in] guard 共有監視、nullは旧経路
 * @param[in] stop 利用者停止
 * @return 同期処理中だけ使う所有callback
 * @note Whisperの既存C callbackは通知例外を捕捉し、同期境界で分類し直します
 */
//------------------------------------------------------------------------------
[[nodiscard]] ProgressCallback resourceProgress( const ProgressCallback				   &callback,
												std::shared_ptr<WholeTextResourceGuard> guard,
												const std::stop_token					stop)
{
	return
		//------------------------------------------------------------------------------
		/**
		 * @brief 同期通知時に協調停止を確認します
		 * @param[in] value 元の実進捗
		 */
		//------------------------------------------------------------------------------
		[callback, guard = std::move( guard), stop]( const Progress &value)
		{
			// guardなしの旧経路では従来の通知だけを行います
			if( guard) {
				checkCancelled( stop) ;
				// 頻回callbackは既存の100ms throttleを維持し、sticky時だけ例外へ変換します
				if( guard->poll()) {
					guard->check( stop) ;
				}
			}
			// 通知なしでも上の資源確認は省略しません
			if( callback) {
				callback( value) ;
			}
		} ;
}
//------------------------------------------------------------------------------
/**
 * @brief 一回の開始snapshotと既存診断への受動copyをまとめます
 * @param[in] services 同期probe
 * @param[in] diagnostics 任意の共有診断、null可
 * @param[in] policy 固定する予算
 * @return 全段階が共有するguard
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::shared_ptr<WholeTextResourceGuard>
makeApplicationGuard( const detail::WholeTextApplicationServices			&services,
					 const std::shared_ptr<CorrectionDiagnosticCapture> &diagnostics,
					 const WholeTextResourcePolicy						 policy)
{
	WholeTextResourceProbe probe = services.resourceProbe ; // 通常probeはそのまま
	// 診断は同じ一標本だけをcopyし、追加照会を行いません
	if( diagnostics) {
		probe =
			//------------------------------------------------------------------------------
			/**
			 * @brief 同じprobeの結果と取得失敗だけを受動記録します
			 * @return 同じ標本
			 */
			//------------------------------------------------------------------------------
			[original = services.resourceProbe, diagnostics]
			{
				try {
					const auto sample = original() ; // 一回だけの既存照会
					diagnostics->sample( sample.workingSet, sample.privateBytes, sample.availablePhysical,
										sample.availableCommit) ;
					return sample ;
				} catch( ...) {
					diagnostics->probeFailed() ;
					throw ;
				}
			} ;
	}
	auto guard = std::make_shared<WholeTextResourceGuard>( std::move( probe), policy) ; // 一baseline
	// 開始時のread-only値を記録し、解放後も上限を再計算しません
	if( diagnostics) {
		const auto baseline = guard->baseline() ; // 固定snapshot
		diagnostics->guardLimits( baseline.workingSet, baseline.privateBytes, guard->workingLimit(),
								 guard->privateLimit(), baseline.usableTotalPhysical, baseline.availablePhysical,
								 baseline.availableCommit) ; // 取得済み一標本を受動copy。追加pollなし
	}
	return guard ;
}
/** @brief 解放後のguard値とscope終端を例外経路でも残します */
struct ObservationScope {
	/// 呼出しより長い任意出力
	WholeTextApplicationObservation				  *output ;
	/// scope内ownerの非所有参照
	const std::shared_ptr<WholeTextResourceGuard> &guard ;
	//------------------------------------------------------------------------------
	/**
	 * @brief callbackとcorrectorの終了後だけ小さい値を転記します
	 */
	//------------------------------------------------------------------------------
	~ObservationScope()
	{
		// 観測なしの通常呼出しでは何も取得しません
		if( output) {
			output->applicationEndUs = wholeTextObservationTime() ;
			output->pcmReleased = output->auxiliaryPcm.expired() ;
			// 正常decode後の例外でも、PCM scopeはこの観測より先に終了しています
			if( output->decodeEndUs != 0 && output->pcmScopeEndUs == 0) {
				output->pcmScopeEndUs = output->applicationEndUs ;
			}
			// scope終了時には後続localのcorrector/Preparedも破棄済みです
			if( output->verifyEndUs != 0 && output->correctorResetUs == 0) {
				output->correctorResetUs = output->applicationEndUs ;
			}
			// 開始済みguardだけの最初の診断を取得します
			if( guard) {
				output->resourceFailure = guard->failure() ;
				// callbackなし区間の終了後に初めて分かった失敗も区別して保持します
				if( output->resourceFailure.reason != 0) {
					std::uint64_t previous = 0 ; // 実際の停止発生時刻ではなく最初の観測時刻
					output->resourceFailureObservedUs.compare_exchange_strong( previous, output->applicationEndUs) ;
				}
			}
		}
	}
} ;
//------------------------------------------------------------------------------
/**
 * @brief phaseを固定して既存実進捗を包みます
 * @param[in] callback 通知、空可
 * @param[in] phase 用途
 * @return 所有callback
 */
//------------------------------------------------------------------------------
[[nodiscard]] ProgressCallback phased( const ProgressCallback &callback, const WholeTextPhase phase)
{
	return
		//------------------------------------------------------------------------------
		/**
		 * @brief 実進捗に用途だけを追加します
		 * @param[in] value 元進捗
		 */
		//------------------------------------------------------------------------------
		[callback, phase]( Progress value)
		{
			// 元段階の率は変更しません
			if( callback) {
				value.wholeTextPhase = phase ;
				callback( value) ;
			}
		} ;
}
//------------------------------------------------------------------------------
/**
 * @brief 二つの既知rootだけでGemma通常fileの存在を解決します
 * @param[in] request 明示root、空可
 * @param[in] config 標準root
 * @return 後段で一回検証するroot
 * @throws OperationError 不足/属性違反
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::filesystem::path resolveWholeRoot( const WholeTextCorrectionJobRequest &request,
													 const WholeTextApplicationConfig	 &config)
{
	// 明示local値も通常の既存directory検証を通します
	if( !request.modelRoot.empty()) {
		return validateModelDirectory( request.modelRoot) ;
	}
	const std::array roots{config.cacheRoot, config.bundleRoot} ; // 探索対象はこの二つだけ
	// cache優先で最初に存在する通常fileを選びます。破損時の別root fallbackはしません
	for( const auto &root : roots) {
		const auto valid = validateModelDirectory( root) ; // 祖先属性も検証
		// この同期境界で所有する値、または確定状態の検証値です
		const auto file =
			valid /
			modelDefinition(
				wholeTextNativeModelId(
					findWholeTextBoundedRuntime( request.request.profileId, request.request.profileSha256)
						? findWholeTextBoundedRuntime( request.request.profileId, request.request.profileSha256)->variant
						: findWholeTextRuntime( request.request.profileId,
											   request.request.profileSha256)
							  ->variant))
				.file ; // registryのみ
		const auto attributes = GetFileAttributesW( file.c_str()) ; // size/hashは後段一回だけ
		// 不在以外の読取異常をMissingへ隠しません
		if( attributes == INVALID_FILE_ATTRIBUTES) {
			const auto code = GetLastError() ; // 元code
			// 存在しない候補だけを次へ進めます
			if( code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
				continue ;
			}
			throw OperationError( ErrorKind::MODEL_REJECTED, code) ;
		}
		static_cast<void>( validateLocalFile( file)) ;
		return valid ;
	}
	throw OperationError( ErrorKind::MODEL_MISSING, 0x57550009) ;
}
//------------------------------------------------------------------------------
/**
 * @brief observer内で全量容量だけを監視します
 * @param[in] value 借用部分返却値
 * @param[in] stop 協調停止
 */
//------------------------------------------------------------------------------
void checkCaptureCapacity( const Recognition &value, const std::stop_token stop)
{
	checkCancelled( stop) ;
	// 件数は確保済み返却値にも必ず適用します
	if( value.inputWindows.size() > K_MAXIMUM_SEGMENTS || value.rawRows.size() > K_MAXIMUM_SEGMENTS) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
	std::size_t remaining = K_MAXIMUM_TEXT_BYTES ; // 全raw/token byteの残量
	std::size_t tokens = 0 ; // 全token数
	// 途中値をcopy/cacheせず上限だけを確認します
	for( const auto &row : value.rawRows) {
		checkCancelled( stop) ;
		// 引算前に全raw byteとtoken数を検査します
		if( row.text.size() > remaining || row.tokens.size() > K_MAXIMUM_ASR_TOKENS - tokens) {
			throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
		}
		remaining -= row.text.size() ;
		tokens += row.tokens.size() ;
		// 特別tokenを含む全byteを容量へ含めます
		for( const auto &token : row.tokens) {
			// 部分token切捨てで続行しません
			if( token.text.size() > remaining) {
				throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
			}
			remaining -= token.text.size() ;
		}
	}
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 明示試験だけMissing用途を標準cacheへ読取り参照します
 * @param[in] options 固定用途
 * @param[in] reference 読取り専用root
 * @param[in] cache 専用書込みroot
 * @param[in,out] inspector worker同期検査
 * @param[in] stop 停止
 * @return 単一rootで安全に使える計画。混在はcopyなしの明示準備です
 * @see koefude::planCorrectionDiagnosticModels
 */
//------------------------------------------------------------------------------
ModelPreparationPlan planCorrectionDiagnosticModels( const EngineOptions			&options,
													const std::filesystem::path &reference,
													const std::filesystem::path &cache, IModelInspector &inspector,
													std::stop_token stop)
{
	/** @brief この一回のworker計画だけで既存検査をcache-firstへ制限します */
	class MissingReferenceInspector final : public IModelInspector {
		public:
		IModelInspector								  &original ; // 同期borrow
		std::filesystem::path						   writeRoot ; // 検証済みcache
		std::vector<std::pair<ModelId, ModelPresence>> cached ; // 必要用途だけ
		//------------------------------------------------------------------------------
		/**
		 * @brief 同期adapterを作ります
		 * @param[in,out] value 既存検査
		 * @param[in] root cache
		 */
		//------------------------------------------------------------------------------
		MissingReferenceInspector( IModelInspector &value, std::filesystem::path root)
			: original( value), writeRoot( std::move( root))
		{
		}
		//------------------------------------------------------------------------------
		/**
		 * @brief cacheの一用途一回の実検査を再利用し、Missing時だけ参照先を検査します
		 * @param[in] root 呼出し中有効な検査済みlocal絶対directory
		 * @param[in] id 固定registryの用途
		 * @param[in] token 協調取消し
		 * @return cacheなら保存済みまたは実検査したpresence、参照先はcacheがMissingの場合だけ実検査した値です。その他はMISSINGです
		 * @see koefude::IModelInspector::inspect
		 */
		//------------------------------------------------------------------------------
		ModelPresence inspect( const std::filesystem::path &root, ModelId id, std::stop_token token) override
		{
			const auto found = std::find_if( cached.begin(), cached.end(),
				//------------------------------------------------------------------------------
				/**
				 * @brief 同じ用途を照合します
				 * @param[in] value cache値
				 * @return 一致
				 */
				//------------------------------------------------------------------------------
				[id]( const auto &value)
				{
					return value.first == id ;
				}) ; // 固定用途
			// cacheは一用途一回だけ実検査します
			if( root == writeRoot) {
				// 既存plannerの再投影では同じ実観測を使います
				if( found != cached.end()) {
					return found->second ;
				}
				const auto presence = original.inspect( root, id, token) ; // 既存I/O一回
				cached.emplace_back( id, presence) ;
				return presence ;
			}
			// 非Missingはreferenceを実検査せず、同梱全量成立も防ぎます
			if( found == cached.end() || found->second != ModelPresence::MISSING) {
				return ModelPresence::MISSING ;
			}
			return original.inspect( root, id, token) ; // 読取り専用Missing参照だけ
		}
	} ;
	const auto				  validatedCache = validateModelDirectory( cache) ; // local検証
	MissingReferenceInspector adapter( inspector, validatedCache) ; // 一計画の局所owner
	// 全cache用途を先に確認し、reference検査に先立ちCorruptも保持します
	for( const auto id : requiredModels( options)) {
		static_cast<void>( adapter.inspect( validatedCache, id, stop)) ;
	}

	auto plan = planModelPreparation( options, reference, validatedCache, adapter, stop) ; // 既存API
	// 単一rootが未完成ならreference copy計画を表示/実行しません
	if( !plan.ready) {
		auto isolated = planModelPreparation( options, validatedCache, validatedCache, adapter,
											 stop) ; // 保存済みcache観測だけ
		// 元referenceのCorrupt/Missing情報は案内へ保持します
		for( std::size_t index = 0; index < plan.entries.size(); ++index) {
			isolated.entries[index].bundle = plan.entries[index].bundle ;
		}
		isolated.bundleRoot = plan.bundleRoot ;
		return isolated ;
	}
	return plan ;
}
//------------------------------------------------------------------------------
/**
 * @brief 全PCMを26秒以下で隙間なく被覆します
 * @param[in] sampleCount 実全sample数
 * @return 最大50000の連続窓
 * @throws OperationError 0/表現境界/窓数上限違反、確保前に拒否
 * @see koefude::makeWholeTextApplicationWindows
 */
//------------------------------------------------------------------------------
std::vector<AsrSampleWindow> makeWholeTextApplicationWindows( const std::uint64_t sampleCount)
{
	// 表現境界と件数をallocation前に拒否します
	if( sampleCount == 0 || sampleCount > K_MAXIMUM_TIMELINE_SAMPLES ||
	   1 + (sampleCount - 1) / K_SHARED_ASR_WINDOW_SAMPLES > K_MAXIMUM_SEGMENTS) {
		throw OperationError( ErrorKind::INPUT_TOO_LARGE, 0x57550004) ;
	}
	std::vector<AsrSampleWindow> windows ; // 件数が確定した有界列
	windows.reserve( static_cast<std::size_t>( 1 + (sampleCount - 1) / K_SHARED_ASR_WINDOW_SAMPLES)) ;
	// 最終端数も含め[0,N)を一度ずつ被覆します
	for( std::uint64_t begin = 0; begin < sampleCount;) { // 次窓の原点
		// この同期境界で所有する値、または確定状態の検証値です
		const auto end = begin + std::min( K_SHARED_ASR_WINDOW_SAMPLES, sampleCount - begin) ; // overflowなし
		windows.push_back( {begin, end}) ;
		begin = end ;
	}
	checkSharedAsrWindows( windows, sampleCount) ;
	return windows ;
}
//------------------------------------------------------------------------------
/**
 * @brief PCM/context解放後の一captureだけを返す共通境界です
 * @param[in] request 全要求
 * @param[in,out] services 同期依存
 * @param[in] options 固定Whisper設定
 * @param[in] windows 全予定窓
 * @param[in] stop 停止
 * @param[in] progress 通知
 * @param[out] observation 任意観測
 * @return 全raw/token/trace所有capture
 * @param[in] guard 共有予算、nullなら従来のcapture。return前にPCMを解放します
 * @param[in] processingStop decoder/Whisper専用停止。省略時は元stopを維持します
 * @see koefude::detail::collectWholeTextApplicationCapture
 */
//------------------------------------------------------------------------------
WholeTextWhisperCapture detail::collectWholeTextApplicationCapture(
	const WholeTextCorrectionJobRequest &request, WholeTextApplicationServices &services, const EngineOptions &options,
	std::span<const AsrSampleWindow> windows, std::stop_token stop, const ProgressCallback &progress,
	WholeTextApplicationObservation *observation, std::shared_ptr<WholeTextResourceGuard> guard,
	const std::optional<std::stop_token> processingStop)
{
	const auto guardedProgress = resourceProgress( progress, guard, stop) ; // 同じ共有監視
	const auto effectiveStop = processingStop.value_or( stop) ; // 処理だけの独立停止
	try {
		// decoder開始前に固定予算を確認します
		if( guard) {
			guard->check( stop) ;
		}
		// 実decoder入口と正常returnだけを記録します
		if( observation) {
			observation->decodeBeginUs = wholeTextObservationTime() ;
		}
		// この同期境界で所有する値、または確定状態の検証値です
		const auto audio = services.decoder( request.input, effectiveStop,
											guardedProgress) ; // decodeとidentityの段階を分離
		// 同期decodeの中断不能区間から戻った直後にも確認します
		if( guard) {
			guard->check( stop) ;
		}
		// weak sourceはPCMを延命しません
		if( observation) {
			observation->decodeEndUs = wholeTextObservationTime() ;
			observation->auxiliaryPcm = audio.source ;
			observation->sampleCount = audio.sampleCount() ;
			observation->sampleRate = K_SAMPLE_RATE ;
			observation->sourceRate = audio.sourceRate ;
			observation->sourceChannels = audio.sourceChannels ;
			observation->sourceBytes = audio.sourceBytes ;
			observation->aacProfile = audio.aacProfile ;
		}
		const auto identity = fingerprintDecodedPcm( audio, stop, guardedProgress) ; // 同じ算法
		// 同pathでも1bit/count/版差は補助認識前に拒否します
		if( identity != *request.source->pcmIdentity) {
			throw OperationError( ErrorKind::INVALID_INPUT, 0x57550003) ;
		}
		// identity照合後もWhisper前の資源減少を後段へ持ち込みません
		if( guard) {
			guard->check( stop) ;
		}
		// 全量照合完了を補助認識開始と分離します
		if( observation) {
			observation->identityEndUs = wholeTextObservationTime() ;
		}
		const RecognitionObserver observer =
			//------------------------------------------------------------------------------
			/**
			 * @brief 取消しと共通予算を確認してから部分文字起こしの容量を検査します
			 * @param[in] partial 呼出し中だけ借用する部分認識結果です。内容や完了状態を変更しません
			 * @note 捕捉したstop/guardを使い、guardが空なら共通予算確認を省略します
			 * @throws OperationError 取消し、共通予算違反または部分結果の容量超過時に呼出し元へ通知します
			 */
			//------------------------------------------------------------------------------
			[stop, guard]( const Recognition &partial)
			{
				// 部分結果の既存容量確認より先に利用者取消しと共通予算を確認します
				if( guard) {
					guard->check( stop) ;
				}
				checkCaptureCapacity( partial, stop) ;
			} ;
		// 補助認識の実入口直前に同じ予算を確認します
		if( guard) {
			guard->check( stop) ;
		}
		// 同期Whisper呼出しの実入口だけを数えます
		if( observation) {
			++observation->auxiliaryAsrCalls ;
		}
		// この同期境界で所有する値、または確定状態の検証値です
		auto recognition = services.whisper( audio, options, windows, effectiveStop,
											phased( guardedProgress, WholeTextPhase::AUXILIARY_ASR), observer,
											AsrTimelineUse::OBSERVED_WHOLE_TEXT) ; // 一回同期
		// context解放を契約した同期Whisperのreturn後に共通予算を確認します
		if( guard) {
			guard->check( stop) ;
		}
		// 正常returnが成立した場合だけ終了時刻を保存します
		if( observation) {
			observation->auxiliaryEndUs = wholeTextObservationTime() ;
		}
		checkCaptureCapacity( recognition, stop) ;
		// segmentsはこの用途で空なのでraw全量だけで本文なしを判定します
		if( std::none_of( recognition.rawRows.begin(), recognition.rawRows.end(),
						//------------------------------------------------------------------------------
						/**
						 * @brief 元byteに本文があるかを調べます
						 * @param[in] row 原行
						 * @return 非空ならtrue
						 */
						//------------------------------------------------------------------------------
						[]( const RawAsrRow &row)
						{
							return !row.text.empty() ;
						})) {
			throw OperationError( ErrorKind::INFERENCE_FAILED, 0x57550007) ;
		}
		WholeTextWhisperCapture capture ; // 正常return/context解放後だけComplete
		capture.source = request.request.source ;
		capture.primaryAudio = {identity.pcmSha256, identity.sampleCount, identity.sampleRate, identity.originSample} ;
		capture.auxiliaryAudio = capture.primaryAudio ;
		capture.state = WholeTextCaptureState::COMPLETE ;
		capture.plannedWindows.assign( windows.begin(), windows.end()) ;
		capture.returned = std::move( recognition) ;
		return capture ; // PCM/contextはreturn後の呼出側freezeより先に解放
	} catch( const OperationError &error) {
		// C callbackが資源通知の例外を取消しへ変換しても元利用者停止だけを優先します
		if( guard) {
			guard->check( stop, error.code()) ;
		}
		throw ; // 資源停止がなければ既存の分類と診断を保持します
	} catch( ...) {
		// 非OperationErrorも資源停止と利用者停止の優先を確定してから元例外を戻します
		if( guard) {
			guard->check( stop) ;
		}
		throw ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 同じ通常workflowの同期依存だけを注入します
 * @param[in] request 非0op
 * @param[in] config 隔離root
 * @param[in,out] services 同期中有効
 * @param[in] stop 停止
 * @param[in] progress 通知
 * @param[out] observation 任意の受動出力、null可。同期終了後だけ読みます
 * @param[in] policy 明示予算。既定の研究policyはPCM解放後から監視します
 * @return 通常と同じ所有応答
 * @throws OperationError 元分類/codeを維持
 * @see koefude::detail::runWholeTextApplicationWithServices
 */
//------------------------------------------------------------------------------
JobOutput detail::runWholeTextApplicationWithServices( const WholeTextCorrectionJobRequest &request,
													  const WholeTextApplicationConfig	  &config,
													  WholeTextApplicationServices		  &services,
													  const std::stop_token stop, const ProgressCallback &progress,
													  WholeTextApplicationObservation *observation,
													  const WholeTextResourcePolicy	   policy)
{
	const auto kind = wholeTextGenerationKind( request.request.profileId,
											  request.request.profileSha256) ; // model I/O前
	// APIから直接入っても新方式のpolicyを固定し、一baselineを全段階へ渡します
	if( kind == WholeTextGenerationKind::MINISTRAL_PLAIN && policy != WholeTextResourcePolicy::LOW_MEMORY16_GB) {
		return runWholeTextApplicationWithServices( request, config, services, stop, progress, observation,
												   WholeTextResourcePolicy::LOW_MEMORY16_GB) ;
	}
	// boundedは保存portを伴う別型へ明示dispatchします
	if( findWholeTextBoundedRuntime( request.request.profileId, request.request.profileSha256)) {
		return detail::runBoundedWithPersistence(
			request, config.persistence,
			//------------------------------------------------------------------------------
			/**
			 * @brief 既存typed実装を一回だけ呼びます
			 * @param[in] budget 一予約
			 * @return 所有terminal
			 */
			//------------------------------------------------------------------------------
			[&]( WholeTextBoundedStorageBudget budget)
			{
				return detail::runBoundedWholeTextApplicationWithServices(
					request, config, std::move( budget), services, detail::loadWholeTextCounter, stop, progress, policy) ;
			},
			stop) ;
	}
	std::shared_ptr<WholeTextResourceGuard> guard ; // scope観測より長いguard owner
	const ObservationScope					scope{observation, guard} ; // 後続localの解放後に実行
	checkCancelled( stop) ;
	validateWholeTextCorrectionRequest( request.source, request.request) ;
	// 明示16GB予算だけは音声処理より前のsnapshotを全段階で共有します
	if( policy == WholeTextResourcePolicy::LOW_MEMORY16_GB) {
		guard = makeApplicationGuard( services, request.diagnostics, policy) ;
		guard->check( stop) ;
	}
	// この同期境界で所有する値、または確定状態の検証値です
	const auto *descriptor = findWholeTextRuntime( request.request.profileId,
												  request.request.profileSha256) ; // 要求から一意に解決
	// 未知方式のために前処理やモデル検査を始めません
	if( !descriptor) {
		throw OperationError( ErrorKind::INVALID_INPUT, 0x5755000a) ;
	}
	const auto										profile = wholeTextNativeProfile( descriptor->variant) ; // 要求固定
	// この同期境界で所有する値、または確定状態の検証値です
	const auto									   &module = descriptor->variant == WholeTextRuntimeVariant::PORTABLE
																 ? config.nativeModule
																 : (descriptor->variant == WholeTextRuntimeVariant::MINISTRAL14_B_AVX2_NO_REPACK16_GB
																		? config.ministral16gbNativeModule
																		: config.avx2NativeModule) ; // UI一時選択を参照しません
	std::shared_ptr<const VerifiedWholeTextRuntime> runtime ; // FreeLibrary後まで保持
	// 通常factoryと新AVX2は必ず前処理より先に検証します
	if( services.runtimePreflight) {
		runtime = services.runtimePreflight( descriptor->variant, module, stop) ;
		// 注入seamでも要求の異なるownerを受け付けません
		if( !runtime || !runtime->matches( descriptor->variant, module)) {
			throw OperationError( ErrorKind::MODEL_REJECTED, 0x57560002) ;
		}
		// AVX2は検証ownerなしの内部seamも受け付けません
	} else if( descriptor->variant != WholeTextRuntimeVariant::PORTABLE) {
		throw OperationError( ErrorKind::MODEL_REJECTED, 0x57560002) ;
	}
	// 過去のPCM証拠を再decodeから捏造しません
	if( !request.source->pcmIdentity) {
		throw OperationError( ErrorKind::INVALID_INPUT, 0x57550002) ;
	}
	// 元ASRと方式/seamの不正は証拠未記録の診断へ混ぜません
	if( request.source->asrModel != AsrModel::PARAKEET || request.source->asrProfile != AsrProfile::QUALITY ||
	   request.request.profileId != profile.id || request.request.profileSha256 != profile.sha256 ||
	   !services.decoder || !services.whisper || !services.verifier || !services.corrector) {
		throw OperationError( ErrorKind::INVALID_INPUT, 0x5755000a) ;
	}
	// この同期境界で所有する値、または確定状態の検証値です
	const auto windows = makeWholeTextApplicationWindows( request.source->pcmIdentity->sampleCount) ; // allocate前上限
	EngineOptions options ; // 補助ASRだけの固定構成
	options.asrModel = AsrModel::WHISPER ;
	options.asrProfile = AsrProfile::QUALITY ;
	options.backend = Backend::CPU ;
	options.threads = 4 ;
	options.speechFilter = false ;
	options.autoLanguage = false ;
	options.cpuFallback = false ;
	options.diarization = false ;
	// この同期境界で所有する値、または確定状態の検証値です
	const auto plan =
		config.diagnosticReferenceOnly
			? planCorrectionDiagnosticModels( options, config.bundleRoot, config.cacheRoot, services.inspector, stop)
			: planModelPreparation( options, config.bundleRoot, config.cacheRoot, services.inspector,
								   stop) ; // 取得0
	// 未完了copy/download計画を実行せず用途不足として返します
	if( !plan.ready) {
		throw OperationError( ErrorKind::MODEL_MISSING, 0x57550005) ;
	}
	options.modelRoot = plan.effectiveRoot ;
	const auto modelRoot = resolveWholeRoot( request, config) ; // 音声処理前の有限存在解決
	// この同期境界で所有する値、または確定状態の検証値です
	auto capture = collectWholeTextApplicationCapture( request, services, options, windows, stop, progress, observation,
													  guard) ; // 全一時PCM/contextはこのreturn前に解放済み
	// captureのPCMと補助contextを解放した後も同じ開始予算を使います
	if( guard) {
		guard->check( stop) ;
	}
	// この同期境界で所有する値、または確定状態の検証値です
	auto materials = freezeWholeTextNativeMaterials( request.source, request.request.source, std::move( capture),
													stop) ; // V1のsystem/user/bindingは同じ算法
	// 受動観測だけが材料ownerを別に保持します
	if( observation) {
		observation->materials = materials ;
	}
	// 実scopeの後に弱参照の失効を確認します
	if( observation) {
		observation->pcmScopeEndUs = wholeTextObservationTime() ;
		observation->pcmReleased = observation->auxiliaryPcm.expired() ;
	}
	// 旧研究経路だけは従来どおりPCM/context解放後に半分予約を固定します
	if( !guard) {
		guard = std::make_shared<WholeTextResourceGuard>( services.resourceProbe, policy) ;
	}
	// policyを再計算せず実guardのread-only値を写します
	if( observation) {
		observation->guardObserved = true ;
		observation->baseline = guard->baseline() ;
		observation->physicalReserve = guard->physicalReserve() ;
		observation->commitReserve = guard->commitReserve() ;
		observation->workingLimit = guard->workingLimit() ;
		observation->privateLimit = guard->privateLimit() ;
	}
	guard->check( stop) ;
	// この同期境界で所有する値、または確定状態の検証値です
	const auto preparationProgress = phased( progress, WholeTextPhase::NATIVE_PREPARATION) ; // 検証段階
	reportProgress( preparationProgress, Stage::LOADING_MODEL, -1) ;
	// 通常の一回のlease検証だけを計測します
	if( observation) {
		observation->verifyBeginUs = wholeTextObservationTime() ;
	}
	// この同期境界で所有する値、または確定状態の検証値です
	auto input = prepareWholeTextNativeInputWithVerifier( modelRoot, std::move( materials), request.request, stop,
														 services.verifier) ; // verify一回
	// 成功した準備の終端だけを記録します
	if( observation) {
		observation->verifyEndUs = wholeTextObservationTime() ;
	}
	guard->check( stop) ;
	// この同期境界で所有する値、または確定状態の検証値です
	auto corrector = services.corrector( module, std::move( input), guard, runtime) ; // 実通常factory
	// null factoryを成功やModelMissingへ偽装しません
	if( !corrector) {
		throw OperationError( ErrorKind::INTERNAL) ;
	}
	// この同期境界で所有する値、または確定状態の検証値です
	auto output = runWholeTextCorrectionJob( request, *corrector, stop,
											phased( progress, WholeTextPhase::GENERATION)) ; // 同じcontroller検証へ
	corrector.reset() ; // 全lease/context解放後に終端判定
	// 明示破棄後の時刻を例外時のscope記録と区別します
	if( observation) {
		observation->correctorResetUs = wholeTextObservationTime() ;
	}
	guard->check( stop) ;
	return output ;
}
//------------------------------------------------------------------------------
/**
 * @brief 実decoder/ASR/検証/nativeを一回の通常workerへ接続します
 * @param[in] request 非0opと原文/元path
 * @param[in] config 不変root
 * @param[in] stop 協調停止
 * @param[in] progress phase付き通知
 * @param[out] observation 任意出力、null可。同期終了まで有効、終了後だけ読取可能
 * @return 全量を処理した未検証応答、controllerで最終検証
 * @see koefude::runWholeTextApplicationJob
 */
//------------------------------------------------------------------------------
JobOutput runWholeTextApplicationJob( const WholeTextCorrectionJobRequest &request,
									 const WholeTextApplicationConfig &config, const std::stop_token stop,
									 const ProgressCallback &progress, WholeTextApplicationObservation *observation)
{
	const auto kind = wholeTextGenerationKind( request.request.profileId, request.request.profileSha256) ;
	// 文章専用profileは音声依存を構築する前に独立入口へ送ります
	if( request.request.profileId == K_TEXT_CONTEXT_PROFILE_ID) {
		return detail::runBoundedWithPersistence(
			request, config.persistence,
			//------------------------------------------------------------------------------
			/**
			 * @brief 同じ予約を文章専用入口へ一回渡します
			 * @param[in] budget 所有予約
			 * @return 保存前terminal
			 */
			//------------------------------------------------------------------------------
			[&]( WholeTextBoundedStorageBudget budget)
			{
				return runTextContextCorrectionApplicationJob( request, config, std::move( budget), stop, progress) ;
			},
			stop) ;
	}
	// 明示新方式は通常保存portから16GB固定の公開jobだけへ進めます
	if( kind == WholeTextGenerationKind::MINISTRAL_PLAIN) {
		return detail::runBoundedWithPersistence(
			request, config.persistence,
			[&]( WholeTextBoundedStorageBudget budget)
			{
				return runBoundedWholeTextApplication16gbJob( request, config, std::move( budget), stop, progress) ;
			},
			stop) ;
	}
	LocalModelInspector					 inspector ; // worker専用、UIから差し替えません
	detail::WholeTextApplicationServices services{
		decodeInputAudio, detail::makeAuxiliaryWhisperTranscriber( request.diagnostics), inspector, verifyModel,
		//------------------------------------------------------------------------------
		/**
		 * @brief 封印ownerを実nativeへ移します
		 * @param[in] path 固定DLL
		 * @param[in] input 封印owner
		 * @param[in] guard 共有監視
		 * @param[in] runtime 全callback/FreeLibrary後まで保持する検証owner
		 * @return worker限定native生成器
		 */
		//------------------------------------------------------------------------------
		[observation]( const std::filesystem::path &path, PreparedWholeTextNativeInput input,
					  std::shared_ptr<WholeTextResourceGuard>		  guard,
					  std::shared_ptr<const VerifiedWholeTextRuntime> runtime) -> std::unique_ptr<IWholeTextCorrector>
		{
			return std::make_unique<NativeWholeTextCorrector>( path, std::move( input), std::move( guard), observation,
															  std::move( runtime)) ;
		},
		probeWholeTextResources, verifyWholeTextRuntime} ;
	return detail::runWholeTextApplicationWithServices( request, config, services, stop, progress, observation) ;
}

namespace {
/** @brief job通知callbackはこのscopeだけが強所有します */
class BoundedApplicationTarget final : public WholeTextBoundedProgressTarget {
	public:
	/// retained Stateには渡さない同期通知
	ProgressCallback callback ;
	//------------------------------------------------------------------------------
	/**
	 * @brief worker通知値を所有します
	 * @param[in] value 同期callback
	 */
	//------------------------------------------------------------------------------
	explicit BoundedApplicationTarget( ProgressCallback value) : callback( std::move( value))
	{
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 元の実段階だけを通知します
	 * @param[in] stage 段階
	 * @param[in] completed 完了数
	 * @param[in] total 総数
	 * @return 停止要求ならtrue
	 */
	//------------------------------------------------------------------------------
	bool notify( std::uint32_t stage, std::uint32_t completed, std::uint32_t total) override
	{
		// 元coreの確定byteだけを率へ変換し、token上限を時間率にしません
		const auto percent = stage == 5 && total > 0 && completed <= total
								 ? static_cast<int>( static_cast<std::uint64_t>( completed) * 100 / total)
								 : -1 ; // 生成長が不明な区間は不定表示
		reportProgress( callback, Stage::TRANSCRIBING, percent) ;
		return false ;
	}
} ;
//------------------------------------------------------------------------------
/**
 * @brief 限定検査の最初の拒否理由を固定して例外へ変換します
 * @param[in] valid 成立条件
 * @param[in,out] receipt 同期固定出力
 * @param[in] reason 最初の理由
 * @param[in] kind 終端の分類
 */
//------------------------------------------------------------------------------
void requireAdmission( bool valid, detail::WholeTextPlanAdmissionReceipt &receipt,
					  detail::WholeTextPlanAdmissionReason reason, ErrorKind kind = ErrorKind::INVALID_INPUT)
{
	// 不一致だけを拒否とし、未観測の値を受理へ変えません
	if( !valid) {
		receipt.state = detail::WholeTextPlanAdmissionState::REJECTED ;
		receipt.reason = reason ;
		throw OperationError( kind, static_cast<std::uint32_t>( WholeTextBoundedDiagnostic::CHUNK_RECEIPT)) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 有限期待値の容量と欠測をnative取得前に検証します
 * @param[in] expected 本文を持たない不変期待値
 * @param[in,out] receipt 固定出力
 */
//------------------------------------------------------------------------------
void validateAdmissionRequirements( const detail::WholeTextSingleCoreAdmission &expected,
								   detail::WholeTextPlanAdmissionReceipt	  &receipt)
{
	requireAdmission( expected.source.historyId > 0 &&
						 expected.source.historyId < std::numeric_limits<std::uint64_t>::max() &&
						 expected.source.sha256 != WholeTextDigest{} && expected.originalRecordBytes > 0 &&
						 expected.originalRecordBytes <= 65536 && expected.originalRecordSha256 != WholeTextDigest{} &&
						 expected.originalTapeBytes > 0 && expected.originalTapeBytes <= 4096 &&
						 expected.originalTapeSha256 != WholeTextDigest{} && expected.auxiliaryBytes > 0 &&
						 expected.auxiliaryBytes <= 8388608 && expected.auxiliarySha256 != WholeTextDigest{} &&
						 expected.rawRows > 0 && expected.rawRows <= 50000 && expected.canonicalSpans > 0 &&
						 expected.canonicalSpans <= 200004,
					 receipt, detail::WholeTextPlanAdmissionReason::INVALID_REQUIREMENTS) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 同じ準備ownerの全量と一coreの実fitを再計数なしで検査します
 * @param[in] request 同じ原文/input
 * @param[in] expected 不変期待値
 * @param[in,out] receipt 固定同期出力
 * @param[in,out] prepared 同じlive owner
 * @param[in] guard 同じbaseline
 * @param[in] stop 実利用者停止
 */
//------------------------------------------------------------------------------
void admitPreparedPlan( const WholeTextCorrectionJobRequest		  &request,
					   const detail::WholeTextSingleCoreAdmission &expected,
					   detail::WholeTextPlanAdmissionReceipt &receipt, PreparedBoundedWholeTextPlan &prepared,
					   const std::shared_ptr<WholeTextResourceGuard> &guard, std::stop_token stop)
{
	using Reason = detail::WholeTextPlanAdmissionReason ; // 固定した不成立境界
	receipt.gateEntered = true ;
	receipt.state = detail::WholeTextPlanAdmissionState::UNKNOWN ;
	guard->check( stop) ;
	receipt.reason = Reason::COUNTS_OR_COVERAGE ;
	prepared.materials()->validate( stop) ;
	const auto &plan = prepared.plan() ; // 同じsessionが封印した計画
	const auto	view = prepared.materials()->view(
		 plan.inputBinding, wholeTextGenerationKind( request.request.profileId, request.request.profileSha256)) ;
	validateWholeTextChunkPlan( view, plan, stop) ;
	receipt.coverageValid = true ;
	receipt.rawRows = static_cast<std::uint32_t>( view.raw.size()) ;
	receipt.canonicalSpans = static_cast<std::uint32_t>( view.canonicalSpans.size()) ;
	receipt.records = static_cast<std::uint32_t>( plan.chunks.size()) ;
	receipt.planSha256 = plan.planSha256 ;
	receipt.inputSha256 = prepared.inputSha256() ;
	receipt.planValid = true ;
	const auto &key = prepared.materials()->sourceKey() ; // 履歴IDも別途照合
	const auto	reading = fingerprintReadingSource( *prepared.materials()->source()) ; // 全行
	receipt.sourceHistoryId = key.historyId ;
	receipt.readingSourceSha256 = reading ;
	receipt.sourceValid = reading != WholeTextDigest{} ;
	requireAdmission( receipt.sourceValid && key.historyId == expected.source.historyId &&
						 key.sha256 == expected.source.sha256 && reading == expected.source.sha256,
					 receipt, Reason::SOURCE_IDENTITY) ;
	receipt.reason = Reason::ORIGINAL_RECORD ;
	try {
		HistorySnapshot snapshot ; // 比較専用、実履歴のfloorを変更しません
		snapshot.nextId = key.historyId + 1 ;
		HistoryEntry entry ; // 他fieldは元probeと同じ既定値
		entry.id = key.historyId ;
		entry.input = request.input ;
		entry.state = HistoryState::COMPLETED ;
		entry.result = prepared.materials()->source() ;
		snapshot.entries.push_back( std::move( entry)) ;
		const auto bytes = measureEncodedHistory( snapshot) ; // 確保より前に実長を照合
		requireAdmission( bytes == expected.originalRecordBytes && bytes <= 65536, receipt, Reason::ORIGINAL_RECORD) ;
		const auto encoded = encodeHistory( snapshot) ; // storageの32B footerを含めません
		receipt.originalRecordBytes = encoded.size() ;
		receipt.originalRecordSha256 = wholeTextSha256( encoded) ;
		receipt.originalRecordValid = true ;
		requireAdmission( receipt.originalRecordBytes == expected.originalRecordBytes &&
							 receipt.originalRecordSha256 == expected.originalRecordSha256,
						 receipt, Reason::ORIGINAL_RECORD) ;
	} catch( const HistoryError &error) {
		receipt.historyErrorValid = true ;
		receipt.historyErrorKind = static_cast<std::uint32_t>( error.kind()) ;
		receipt.historyErrorCode = error.code() ;
		throw OperationError( error.kind() == HistoryErrorKind::TOO_LARGE ? ErrorKind::RESULT_TOO_LARGE
																		 : ErrorKind::INVALID_INPUT,
							 error.code()) ;
	}
	std::uint64_t tapeBytes = 0 ; // 長さ照合より前に全tapeを確保しません
	// validator済み原行を元順で数え、限定上限を越えた時点で拒否します
	for( const auto &line : view.original) {
		requireAdmission( line.text.size() <= 4096 - tapeBytes, receipt, Reason::ORIGINAL_TAPE) ;
		tapeBytes += line.text.size() ;
	}
	requireAdmission( tapeBytes == expected.originalTapeBytes, receipt, Reason::ORIGINAL_TAPE) ;
	std::string tape ; // 限定済み4096B以下だけを連結
	tape.reserve( static_cast<std::size_t>( tapeBytes)) ;
	// 区切りを追加せず全原文byteを一度ずつ連結します
	for( const auto &line : view.original) {
		tape.append( line.text) ;
	}
	receipt.originalTapeBytes = tape.size() ;
	receipt.originalTapeSha256 = wholeTextSha256( tape) ;
	receipt.originalTapeValid = true ;
	requireAdmission( receipt.originalTapeSha256 == expected.originalTapeSha256, receipt, Reason::ORIGINAL_TAPE) ;
	receipt.auxiliaryBytes = view.auxiliary.size() ;
	receipt.auxiliarySha256 = wholeTextSha256( view.auxiliary) ;
	receipt.auxiliaryValid = true ;
	requireAdmission( receipt.auxiliaryBytes == expected.auxiliaryBytes &&
						 receipt.auxiliarySha256 == expected.auxiliarySha256,
					 receipt, Reason::AUXILIARY) ;
	requireAdmission( receipt.rawRows == expected.rawRows && receipt.canonicalSpans == expected.canonicalSpans, receipt,
					 Reason::COUNTS_OR_COVERAGE) ;
	requireAdmission( plan.chunks.size() == 1, receipt, Reason::PLAN_SHAPE) ;
	const auto &chunk = plan.chunks.front() ; // 件数確認後だけ参照
	receipt.core = chunk.core ;
	receipt.before = chunk.before ;
	receipt.after = chunk.after ;
	requireAdmission( chunk.index == 0 && !chunk.verbatimWhitespace && chunk.core == WholeTextByteRange{0, tapeBytes} &&
						 chunk.before == WholeTextByteRange{0, 0} &&
						 chunk.after == WholeTextByteRange{tapeBytes, tapeBytes} &&
						 std::all_of( plan.canonical.begin(), plan.canonical.end(),
		//------------------------------------------------------------------------------
		/**
		 * @brief 全packetの一次ownerを確認します
		 * @param[in] owner 同じplanの一packet
		 * @return owner0ならtrue
		 */
		//------------------------------------------------------------------------------
		[]( const auto &owner)
		{
			return owner.chunk == 0 ;
		}),
					 receipt, Reason::PLAN_SHAPE) ;
	requireAdmission( chunk.receipt.has_value(), receipt, Reason::COUNTER_RECEIPT) ;
	receipt.count = *chunk.receipt ;
	receipt.countValid = true ;
	const auto &count = receipt.count ; // 全component/bindingは直前の既存validatorも照合
	requireAdmission( count.session == prepared.session().id() && count.session != 0 && count.sequence != 0 &&
						 count.fits && count.completeTokenDigest && count.tokenSha256 != WholeTextDigest{} &&
						 count.coreTokens > 0 && count.coreTokens <= 512 && count.prefixTokens > 0 &&
						 count.prefixTokens <= 4096 && count.beforeTokens == 0 && count.afterTokens == 0 &&
						 count.prefixTokens + 1536 <= 5632 && count.receiptSha256 == wholeTextReceiptDigest( count),
					 receipt, Reason::COUNTER_RECEIPT) ;
	receipt.reason = Reason::STOPPED ;
	guard->check( stop) ;
	receipt.reason = Reason::NONE ;
	receipt.state = detail::WholeTextPlanAdmissionState::ACCEPTED ;
}
//------------------------------------------------------------------------------
/**
 * @brief 戻ったterminalの非所有固定値をallocationなしで転記します
 * @param[in] terminal 同じ終端
 * @param[in,out] receipt 同期出力
 * @param[in] stop 元stop
 */
//------------------------------------------------------------------------------
void finishAdmissionReceipt( const WholeTextBoundedTerminal &terminal, detail::WholeTextPlanAdmissionReceipt &receipt,
							std::stop_token stop) noexcept
{
	static_assert( std::is_trivially_copyable_v<detail::WholeTextPlanAdmissionReceipt>) ;
	receipt.userStopRequested = stop.stop_requested() ;
	receipt.native = {terminal.result.nativeStatus, terminal.result.nativeDiagnostic, terminal.result.nativeEngineCode} ;
	receipt.close = terminal.result.close ;
	receipt.resourceFailure = terminal.result.resourceFailure ;
	receipt.errorKind = terminal.result.errorKind ;
	receipt.errorCode = terminal.result.errorCode ;
}
//------------------------------------------------------------------------------
/**
 * @brief 通常入口と必須受入入口を同じ所有scopeへ接続します
 * @param[in] request 元要求
 * @param[in] config 固定root
 * @param[in] storageBudget 一予約
 * @param[in,out] services 同期依存
 * @param[in] loader 固定export
 * @param[in] stop 元stop
 * @param[in] progress 既存通知
 * @param[in] policy 元予算
 * @param[in] admission 期待値、nullは通常
 * @param[out] receipt 同期出力、同時nullだけ許可
 * @return 同じterminal。未指定時は追加照合・pollを行いません
 */
//------------------------------------------------------------------------------
WholeTextBoundedTerminal runBoundedApplication( const WholeTextCorrectionJobRequest	&request,
											   const WholeTextApplicationConfig		&config,
											   WholeTextBoundedStorageBudget		 storageBudget,
											   detail::WholeTextApplicationServices &services,
											   const detail::WholeTextCounterLoader &loader, std::stop_token stop,
											   const ProgressCallback &progress, WholeTextResourcePolicy policy,
											   const detail::WholeTextSingleCoreAdmission *admission,
											   detail::WholeTextPlanAdmissionReceipt	  *receipt)
{
	const auto kind = wholeTextGenerationKind( request.request.profileId, request.request.profileSha256) ;
	// 直接typed入口でもResearch予算へ迂回させません
	if( kind == WholeTextGenerationKind::MINISTRAL_PLAIN && policy != WholeTextResourcePolicy::LOW_MEMORY16_GB) {
		policy = WholeTextResourcePolicy::LOW_MEMORY16_GB ;
	}
	const bool				 textOnly = request.request.profileId == K_TEXT_CONTEXT_PROFILE_ID ; // 独立入力契約
	WholeTextBoundedTerminal terminal ; // この同期処理で使う所有値または検証値
	terminal.source = request.source ; // 同じ原文owner
	terminal.result.request = request.request ; // 生成前に文字列も確保
	std::shared_ptr<WholeTextResourceGuard>						 guard ; // 単一baseline
	std::unique_ptr<detail::WholeTextApplicationResourceControl> resources ; // LowMemory所有監視
	std::shared_ptr<WholeTextBoundedProgressGate>				 gate ; // weak通知
	WholeTextNativeFailureSnapshot								 preparationFailure ; // prepare失敗の元native値
	bool correctorReturned = false ; // 元native終端を上書きしない同期成立値
	try {
		requireWholeTextBounded( (admission == nullptr) == (receipt == nullptr),
								WholeTextBoundedDiagnostic::CHUNK_RECEIPT) ;
		// 期待値がある限定入口だけで検証し、通常経路の計数とpollを増やしません
		if( admission) {
			*receipt = {} ;
			receipt->operationId = request.request.operationId ;
			validateAdmissionRequirements( *admission, *receipt) ;
		}
		checkCancelled( stop) ;
		requireWholeTextBounded( !detail::wholeTextCounterRestartRequired(),
								WholeTextBoundedDiagnostic::RESTART_REQUIRED, ErrorKind::INFERENCE_FAILED) ;
		validateWholeTextCorrectionRequest( request.source, request.request) ;
		// この同期境界で所有する値、または確定状態の検証値です
		const auto *descriptor = findWholeTextBoundedRuntime( request.request.profileId,
															 request.request.profileSha256) ; // 新方式だけ
		// metadata欠落はモデル不足と分け、音声や生成へ進みません
		requireWholeTextBounded( request.source->pcmIdentity.has_value(),
								WholeTextBoundedDiagnostic::SOURCE_METADATA_MISSING) ;
		requireWholeTextBounded( descriptor && services.runtimePreflight && services.verifier && loader &&
									(textOnly || (request.source->asrModel == AsrModel::PARAKEET &&
												  request.source->asrProfile == AsrProfile::QUALITY &&
												  services.decoder && services.whisper)),
								WholeTextBoundedDiagnostic::CHUNK_RECEIPT) ;
		std::uint64_t originalBytes = 0 ; // 原文の実byteだけを無編集計上
		// 固定範囲の元順を維持し、終端までの対象だけを処理します
		for( const auto &line : request.source->lines) {
			originalBytes += line.text.size() ;
		}
		requireWholeTextBounded( storageBudget.retainedTextBytes() >= originalBytes,
								WholeTextBoundedDiagnostic::JOURNAL_CAPACITY, ErrorKind::RESULT_TOO_LARGE) ;
		terminal.result.pcm = *request.source->pcmIdentity ;
		// 明示16GB入口はdecodeより前に予算を固定し、全後段へ同一ownerを渡します
		if( policy == WholeTextResourcePolicy::LOW_MEMORY16_GB) {
			guard = makeApplicationGuard( services, request.diagnostics, policy) ;
			resources = std::make_unique<detail::WholeTextApplicationResourceControl>( guard, stop) ;
			resources->start( services.resourceMonitor) ;
			guard->check( stop) ;
		}
		// この同期境界で所有する値、または確定状態の検証値です
		const auto &module = descriptor->variant == WholeTextRuntimeVariant::PORTABLE
								 ? config.nativeModule
								 : (descriptor->variant == WholeTextRuntimeVariant::MINISTRAL14_B_AVX2_NO_REPACK16_GB
										? config.ministral16gbNativeModule
										: config.avx2NativeModule) ; // 固定variant
		// この同期境界で所有する値、または確定状態の検証値です
		auto		runtime = services.runtimePreflight( descriptor->variant, module, stop) ; // PCM前schema3
		// 無進捗preflight中に観測した不足の後でdecodeを開始しません
		if( resources) {
			guard->check( stop) ;
		}
		requireWholeTextBounded( runtime && runtime->matches( descriptor->variant, module) && runtime->accountingV2() &&
									runtime->capabilitiesMask() == 3,
								WholeTextBoundedDiagnostic::CHUNK_RECEIPT, ErrorKind::MODEL_REJECTED) ;
		const auto modelRoot = resolveWholeRoot( request, config) ; // 後段で固定size/hash検証
		std::shared_ptr<const WholeTextBoundedMaterials> materials ; // 入力契約別の不変owner
		// 文章専用ではdecode/Whisper/音声窓/補助モデル検査を一切呼びません
		if( textOnly) {
			materials = freezeTextContextMaterials( request.source, request.request.source, stop) ;
		} else {
			EngineOptions options ;
			options.asrModel = AsrModel::WHISPER ;
			options.asrProfile = AsrProfile::QUALITY ;
			options.backend = Backend::CPU ;
			options.threads = 4 ;
			options.speechFilter = false ;
			options.autoLanguage = false ;
			options.cpuFallback = false ;
			options.diarization = false ;
			// この同期境界で所有する値、または確定状態の検証値です
			const auto preparation = config.diagnosticReferenceOnly
										 ? planCorrectionDiagnosticModels( options, config.bundleRoot, config.cacheRoot,
																		  services.inspector, stop)
										 : planModelPreparation( options, config.bundleRoot, config.cacheRoot,
																services.inspector, stop) ; // 取得0
			requireWholeTextBounded( preparation.ready, WholeTextBoundedDiagnostic::CHUNK_RECEIPT,
									ErrorKind::MODEL_MISSING) ;
			options.modelRoot = preparation.effectiveRoot ;
			// この同期境界で所有する値、または確定状態の検証値です
			const auto windows = makeWholeTextApplicationWindows(
				terminal.result.pcm.sampleCount) ; // この同期処理で使う所有値または検証値
			// この同期境界で所有する値、または確定状態の検証値です
			auto capture = detail::collectWholeTextApplicationCapture(
				request, services, options, windows, stop, progress, nullptr, guard,
				resources ? std::optional( resources->processingToken()) : std::nullopt) ;
			// PCM/contextのRAII終端後も開始snapshotとlimitを再計算しません
			if( guard) {
				guard->check( stop) ;
			}
			// この同期境界で所有する値、または確定状態の検証値です
			materials = freezeBoundedWholeTextMaterials( request.source, request.request.source, std::move( capture),
														stop) ; // PCM/contextはすでに解放
		}
		// 研究入口だけは従来の解放後開始を維持し、16GBのguardを作り直しません
		if( !guard) {
			guard = makeApplicationGuard( services, request.diagnostics, policy) ;
		}
		guard->check( stop) ;
		// この同期境界で所有する値、または確定状態の検証値です
		auto target = std::make_shared<BoundedApplicationTarget>( phased(
			policy == WholeTextResourcePolicy::LOW_MEMORY16_GB ? resourceProgress( progress, guard, stop) : progress,
			WholeTextPhase::GENERATION)) ;
		gate = std::make_shared<WholeTextBoundedProgressGate>( target) ; // Stateはweak経路だけ
		terminal.journal = std::make_unique<WholeTextBoundedJournal>( std::move( storageBudget)) ;
		// この同期境界で所有する値、または確定状態の検証値です
		auto prepared = detail::prepareBoundedWholeTextPlanWithDependencies(
			modelRoot, std::move( materials), request.request, std::move( runtime), guard, stop, services.verifier,
			loader, WholeTextNativeExecutionUse::SERIAL_GENERATION, &terminal.result.close, gate, &preparationFailure) ;
		// preparedがまだ同scopeでliveの間だけ拒否・例外のclose証拠を固定します
		if( admission) {
			try {
				admitPreparedPlan( request, *admission, *receipt, prepared, guard, stop) ;
			} catch( ...) {
				gate->disable() ;
				preparationFailure = prepared.session().failureSnapshot() ;
				terminal.result.close = prepared.session().closeChecked() ;
				static_cast<void>( guard->poll( true)) ; // cleanup後のsticky停止も元理由を維持
				// 明示不一致以外は検査の成立を不明として保持します
				if( receipt->state != detail::WholeTextPlanAdmissionState::REJECTED) {
					receipt->reason = stop.stop_requested() || guard->failure().reason != 0
										  ? detail::WholeTextPlanAdmissionReason::STOPPED
										  : detail::WholeTextPlanAdmissionReason::EXCEPTION ;
				}
				throw ;
			}
			receipt->correctorHandoffStarted = true ;
		}
		// 有限probeだけ全planの生成数をpromotion前に拒否し、同じsessionを明示closeします
		if( textOnly && config.textContextGenerationLimit != 0 &&
		   std::count_if( prepared.plan().chunks.begin(), prepared.plan().chunks.end(),
						 //------------------------------------------------------------------------------
						 /**
						  * @brief 空白copyを生成回数へ含めません
						  * @param[in] chunk 封印core
						  * @return 実生成ならtrue
						  */
						 //------------------------------------------------------------------------------
						 []( const auto &chunk)
						 {
							 return !chunk.verbatimWhitespace ;
						 }) > config.textContextGenerationLimit) {
			gate->disable() ;
			preparationFailure = prepared.session().failureSnapshot() ;
			terminal.result.close = prepared.session().closeChecked() ;
			throw OperationError( ErrorKind::RESULT_TOO_LARGE,
								 static_cast<std::uint32_t>( WholeTextBoundedDiagnostic::FINITE_GENERATION_LIMIT)) ;
		}
		NativeBoundedWholeTextCorrector corrector ; // V1 interfaceへ変換しません
		terminal = corrector.execute( std::move( prepared), std::move( terminal), guard, std::move( gate), stop,
									 request.diagnostics) ;
		correctorReturned = true ; // applicationにも最終照合用の同guard所有を残します
	} catch( const OperationError &error) {
		terminal.result.errorKind = stop.stop_requested() ? ErrorKind::CANCELLED : error.kind() ;
		terminal.result.errorCode = error.code() ;
	} catch( const std::bad_alloc &) {
		terminal.result.errorKind = stop.stop_requested() ? ErrorKind::CANCELLED : ErrorKind::RESULT_TOO_LARGE ;
		terminal.result.errorCode = static_cast<std::uint32_t>( WholeTextBoundedDiagnostic::JOURNAL_CAPACITY) ;
	} catch( ...) {
		terminal.result.errorKind = stop.stop_requested() ? ErrorKind::CANCELLED : ErrorKind::INTERNAL ;
		terminal.result.errorCode = static_cast<std::uint32_t>( WholeTextBoundedDiagnostic::CHUNK_RECEIPT) ;
	}
	// 有効な通知経路だけを使用し、終端後のjobへ触れません
	if( gate) {
		gate->disable() ;
	} // 以後targetには触れません
	// try scopeのPCM/context/preparedとnative closeはすべて同期終了済みです
	if( !correctorReturned) {
		terminal.result.outcome = terminal.result.errorKind == ErrorKind::CANCELLED ? WholeTextBoundedOutcome::CANCELLED
																					: WholeTextBoundedOutcome::FAILURE ;
	}
	// この同期境界で所有する値、または確定状態の検証値です
	const auto failure = resources ? resources->finish() : (guard ? guard->failure() : WholeTextResourceFailure{}) ;
	// join後に限りuser/resource/nativeの保存前優先順位を確定します
	if( resources || receipt) {
		// 最後に観測した実利用者停止は元codeを持たない取消しです
		if( stop.stop_requested()) {
			terminal.result.errorKind = ErrorKind::CANCELLED ;
			terminal.result.errorCode = 0 ;
			terminal.result.outcome = WholeTextBoundedOutcome::CANCELLED ;
		}
		// 最初の資源codeをcleanup失敗で上書きしません
		else if( failure.reason != 0) {
			terminal.result.errorKind = ErrorKind::RESULT_TOO_LARGE ;
			terminal.result.errorCode = failure.code ;
			terminal.result.outcome = WholeTextBoundedOutcome::FAILURE ;
		}
	}
	terminal.result.resourceFailure = {failure.reason, failure.code} ;
	terminal.result.restartRequired = terminal.result.close.restartRequired ;
	// correctorのraw値は保持し、prepare失敗時だけその独立snapshotを転記します
	if( !correctorReturned) {
		terminal.result.nativeStatus = preparationFailure.status ;
		terminal.result.nativeDiagnostic = preparationFailure.diagnostic ;
		terminal.result.nativeEngineCode = preparationFailure.engineCode ;
	}
	// 不整合や終了状態を分岐させ、未確定のまま後続へ進みません
	if( terminal.journal) {
		terminal.result.committed = terminal.journal->cursor() ;
	}
	// 未到達も含めて同期終了の元native/close/resourceを固定します
	if( receipt) {
		finishAdmissionReceipt( terminal, *receipt, stop) ;
	}
	return terminal ; // 取得済みprefixの所有権はnative保持と独立
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 同じtyped入口の人工依存seamです
 * @param[in] request 全要求
 * @param[in] config 隔離root
 * @param[in] storageBudget 一回予約
 * @param[in,out] services 同じ通常サービス
 * @param[in] loader V2固定export
 * @param[in] stop 停止
 * @param[in] progress 同期通知
 * @param[in] policy 明示予算。既定は従来研究用
 * @return native所有物のない未保存terminal
 * @see koefude::detail::runBoundedWholeTextApplicationWithServices
 */
//------------------------------------------------------------------------------
WholeTextBoundedTerminal detail::runBoundedWholeTextApplicationWithServices(
	const WholeTextCorrectionJobRequest &request, const WholeTextApplicationConfig &config,
	WholeTextBoundedStorageBudget storageBudget, WholeTextApplicationServices &services,
	const WholeTextCounterLoader &loader, std::stop_token stop, const ProgressCallback &progress,
	WholeTextResourcePolicy policy)
{
	return runBoundedApplication( request, config, std::move( storageBudget), services, loader, stop, progress, policy,
								 nullptr, nullptr) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 同じlive計画の全量一致と単一実fitを生成前に必須検査します
 * @param[in] request 原文と元path
 * @param[in] config 固定root
 * @param[in] storageBudget 一回予約
 * @param[in,out] services 同期七依存
 * @param[in] loader 固定V2 export
 * @param[in] admission 不変期待値、終了まで有効
 * @param[out] receipt 固定同期出力、終了まで有効。nativeへ借用しません
 * @param[in] stop 利用者停止
 * @param[in] progress 既存同期通知
 * @return 原文を保持する通常terminal。16GB品質/搭載量の合格ではありません
 * @see koefude::detail::runBoundedWholeTextApplication16gbWithAdmission
 */
//------------------------------------------------------------------------------
WholeTextBoundedTerminal detail::runBoundedWholeTextApplication16gbWithAdmission(
	const WholeTextCorrectionJobRequest &request, const WholeTextApplicationConfig &config,
	WholeTextBoundedStorageBudget storageBudget, WholeTextApplicationServices &services,
	const WholeTextCounterLoader &loader, const WholeTextSingleCoreAdmission &admission,
	WholeTextPlanAdmissionReceipt &receipt, std::stop_token stop, const ProgressCallback &progress)
{
	return runBoundedApplication( request, config, std::move( storageBudget), services, loader, stop, progress,
								 WholeTextResourcePolicy::LOW_MEMORY16_GB, &admission, &receipt) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 実16GB入口と同じ固定policyを人工依存で検証します
 * @param[in] request 原文と元PCM identity
 * @param[in] config 隔離root
 * @param[in] storageBudget 一回予約
 * @param[in,out] services 同期中有効な依存
 * @param[in] loader V2固定export
 * @param[in] stop 利用者停止
 * @param[in] progress 通知
 * @return 同じbounded処理の未保存terminal
 * @see koefude::detail::runBoundedWholeTextApplication16gbWithServices
 */
//------------------------------------------------------------------------------
WholeTextBoundedTerminal detail::runBoundedWholeTextApplication16gbWithServices(
	const WholeTextCorrectionJobRequest &request, const WholeTextApplicationConfig &config,
	WholeTextBoundedStorageBudget storageBudget, WholeTextApplicationServices &services,
	const WholeTextCounterLoader &loader, const std::stop_token stop, const ProgressCallback &progress)
{
	return runBoundedWholeTextApplicationWithServices( request, config, std::move( storageBudget), services, loader, stop,
													  progress, WholeTextResourcePolicy::LOW_MEMORY16_GB) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 音声を開かず原文だけで有界補正します
 * @param[in] request 新profileとPCM由来metadata付き原文
 * @param[in] config 固定root
 * @param[in] storageBudget 保存予約
 * @param[in] stop 停止
 * @param[in] progress 実進捗
 * @return 保存前terminal。decoder/Whisperを構築せず、既存monitor/closeを完了します
 * @see koefude::runTextContextCorrectionApplicationJob
 */
//------------------------------------------------------------------------------
WholeTextBoundedTerminal runTextContextCorrectionApplicationJob( const WholeTextCorrectionJobRequest &request,
																const WholeTextApplicationConfig	&config,
																WholeTextBoundedStorageBudget		 storageBudget,
																std::stop_token stop, const ProgressCallback &progress)
{
	requireWholeTextBounded( request.request.profileId == K_TEXT_CONTEXT_PROFILE_ID,
							WholeTextBoundedDiagnostic::CHUNK_RECEIPT) ;
	LocalModelInspector					 inspector ; // 文章専用では呼びませんが既存seamの参照寿命を満たします
	detail::WholeTextApplicationServices services{{},
												  {},
												  inspector,
												  verifyModel,
												  {},
												  probeWholeTextResources,
												  verifyWholeTextRuntime,
												  detail::makeWholeTextApplicationResourceMonitor} ;
	return detail::runBoundedWholeTextApplication16gbWithServices( request, config, std::move( storageBudget), services,
																  detail::loadWholeTextCounter, stop, progress) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 通常実依存で有界補正を接続し、保存前terminalだけを返します
 * @param[in] request 原文/元PCM
 * @param[in] config 固定root
 * @param[in] storageBudget 一回消費する全使用量予約
 * @param[in] stop 停止
 * @param[in] progress 同期通知
 * @return 未保存の単独所有terminal。通常dispatchは保存portを経由します
 * @see koefude::runBoundedWholeTextApplicationJob
 */
//------------------------------------------------------------------------------
WholeTextBoundedTerminal runBoundedWholeTextApplicationJob( const WholeTextCorrectionJobRequest &request,
														   const WholeTextApplicationConfig	   &config,
														   WholeTextBoundedStorageBudget		storageBudget,
														   std::stop_token stop, const ProgressCallback &progress)
{
	LocalModelInspector					 inspector ; // UIと共有しないworker owner
	detail::WholeTextApplicationServices services{decodeInputAudio,
												  detail::makeAuxiliaryWhisperTranscriber( request.diagnostics),
												  inspector,
												  verifyModel,
												  {},
												  probeWholeTextResources,
												  verifyWholeTextRuntime,
												  detail::makeWholeTextApplicationResourceMonitor} ;
	return detail::runBoundedWholeTextApplicationWithServices( request, config, std::move( storageBudget), services,
															  detail::loadWholeTextCounter, stop, progress) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 16GB用policyを固定し、再decodeからcloseまで同じ予算で実行します
 * @param[in] request 原文と元PCM identity
 * @param[in] config 固定root
 * @param[in] storageBudget 一回消費する保存予約
 * @param[in] stop 利用者停止
 * @param[in] progress 同期通知
 * @return 原文を保持する未保存terminal
 * @note application APIだけの入口です。実16GB品質受入や割当てhard limitではありません
 * @see koefude::runBoundedWholeTextApplication16gbJob
 */
//------------------------------------------------------------------------------
WholeTextBoundedTerminal runBoundedWholeTextApplication16gbJob( const WholeTextCorrectionJobRequest &request,
															   const WholeTextApplicationConfig	   &config,
															   WholeTextBoundedStorageBudget		storageBudget,
															   const std::stop_token				stop,
															   const ProgressCallback			   &progress)
{
	LocalModelInspector					 inspector ; // 通常workerだけの同期依存
	detail::WholeTextApplicationServices services{decodeInputAudio,
												  detail::makeAuxiliaryWhisperTranscriber( request.diagnostics),
												  inspector,
												  verifyModel,
												  {},
												  probeWholeTextResources,
												  verifyWholeTextRuntime,
												  detail::makeWholeTextApplicationResourceMonitor} ;
	return detail::runBoundedWholeTextApplication16gbWithServices( request, config, std::move( storageBudget), services,
																  detail::loadWholeTextCounter, stop, progress) ;
}
} // namespace koefude

//------------------------------------------------------------------------------
/**
 * @brief reserve前のfallbackとtyped全体catchで必ず同leaseをsettleします
 * @param[in] request 固定要求
 * @param[in] port 共有保存port
 * @param[in] dispatch 一回typed処理
 * @param[in] stop 停止
 * @return 別型の保存ack。V1応答へ変換しません
 * @see koefude::detail::runBoundedWithPersistence
 */
//------------------------------------------------------------------------------
koefude::JobOutput koefude::detail::runBoundedWithPersistence( const WholeTextCorrectionJobRequest			   &request,
															  const std::shared_ptr<IWholeTextPersistencePort> &port,
															  const WholeTextBoundedDispatch &dispatch,
															  std::stop_token				  stop)
{
	// portなし・未知profileは全model/J I/O前に拒否します
	if( !port || !findWholeTextBoundedRuntime( request.request.profileId, request.request.profileSha256) || !dispatch) {
		throw OperationError( ErrorKind::MODEL_REJECTED,
							 static_cast<std::uint32_t>( WholeTextBoundedDiagnostic::PERSISTENCE_UNAVAILABLE)) ;
	}
	WholeTextBoundedTerminal fallback ; // 予約前に全request/sourceを確保
	// metadataなしの旧原文をモデル不足へまとめず、予約・生成前に独立診断で拒否します
	if( request.request.profileId == K_TEXT_CONTEXT_PROFILE_ID && request.source && !request.source->pcmIdentity) {
		throw OperationError( ErrorKind::INVALID_INPUT,
							 static_cast<std::uint32_t>( WholeTextBoundedDiagnostic::SOURCE_METADATA_MISSING)) ;
	}
	fallback.source = request.source ;
	fallback.result.request = request.request ;
	auto budget = port->reserve( request, stop) ; // Pending/watermark耐久化後だけtyped入口
	// 予約成立後の実dispatchだけを記録します
	if( request.diagnostics) {
		request.diagnostics->dispatchStarted() ;
	}
	std::optional<WholeTextBoundedTerminal> terminal ; // 同期owner
	try {
		terminal = dispatch( std::move( budget)) ;
	} catch( const OperationError &error) {
		fallback.result.errorKind = error.kind() ;
		fallback.result.errorCode = error.code() ;
	} catch( const std::bad_alloc &) {
		fallback.result.errorKind = ErrorKind::RESULT_TOO_LARGE ;
		fallback.result.errorCode = static_cast<std::uint32_t>( WholeTextBoundedDiagnostic::JOURNAL_CAPACITY) ;
	} catch( ...) {
		fallback.result.errorKind = ErrorKind::INTERNAL ;
		fallback.result.errorCode = static_cast<std::uint32_t>( WholeTextBoundedDiagnostic::CHUNK_RECEIPT) ;
	}
	// typed成功はfallbackへ置換せず同じJを渡します
	if( terminal) {
		// portが例外/失敗ackでも生成事実はこの境界で固定済みです
		if( request.diagnostics) {
			request.diagnostics->seal( terminal->result) ;
		}
		return JobOutput{request.source, port->receive( std::move( *terminal), stop)} ;
	}
	fallback.result.outcome = fallback.result.errorKind == ErrorKind::CANCELLED ? WholeTextBoundedOutcome::CANCELLED
																				: WholeTextBoundedOutcome::FAILURE ;
	// fallbackも保存へmoveする前に同じcaptureへ固定します
	if( request.diagnostics) {
		request.diagnostics->seal( fallback.result) ;
	}
	return JobOutput{request.source, port->receive( std::move( fallback), {})} ; // 同leaseへ必ずsettle
}
