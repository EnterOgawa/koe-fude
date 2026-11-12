//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file main_window.cpp
 * @brief 対応音声を投入して話者付き文字起こしを読むWin32画面を実装します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 drag/drop、非同期処理、結果編集と保存を追加
 * 0.1.0 Shunji Ogawa 2026/09/09 chatの描画範囲を上下端へ合わせscroll残像を防止
 * 0.1.0 Shunji Ogawa 2026/09/09 品質優先modelと発話区間検出を次jobへ固定
 * 0.1.0 Shunji Ogawa 2026/09/09 品質選択を常設し、同梱modelと下部の再描画を統一
 * 0.1.0 Shunji Ogawa 2026/09/09 モデル別品質と日本語特化モデルの言語固定を表示
 * 0.1.0 Shunji Ogawa 2026/09/09 アイコン・版・話者filter・日本語英語UIを追加
 * 0.1.0 Shunji Ogawa 2026/09/09 履歴の右クリックから現在設定で再度文字起こしを追加
 * 0.1.0 Shunji Ogawa 2026/09/09 完了結果の見出しへ音声長と実測処理時間を表示
 * 0.1.0 Shunji Ogawa 2026/09/10 履歴を非同期に永続化し、再起動後の表示と個別削除を追加
 * 0.1.0 Shunji Ogawa 2026/09/10 日本語CTCを既定にし、単一品質・CPU固定と以前の設定復元を表示
 * 0.1.0 Shunji Ogawa 2026/09/11 FP32品質とINT8軽量を選択し、CUDA・CPU再試行を表示
 * 0.1.0 Shunji Ogawa 2026/09/13 モデルの非同期準備・選択照合・結果を保持した取得と終了を接続
 * 0.1.0 Shunji Ogawa 2026/09/27 本文のTab移動と8欄の目的名・翻訳・解除を追加
 * 0.1.0 Shunji Ogawa 2026/09/28 試験時だけの本文なし診断イベントを追加
 * 0.1.0 Shunji Ogawa 2026/10/02 WAVの画面接続・入力説明と検証を追加
 * 0.1.0 Shunji Ogawa 2026/10/02 MP3の選択filterと入力案内を一般workerへ接続
 * 0.1.0 Shunji Ogawa 2026/10/02 保存受付を分離し、非表示の内部イベント試験を追加
 * 0.1.0 Shunji Ogawa 2026/10/03 受付設定と結果DTOの実行経路を診断ログへ接続
 * 0.1.0 Shunji Ogawa 2026/10/04 整形受付・取消し・検証済み結果反映を接続
 * 0.1.0 Shunji Ogawa 2026/10/04 保存済み両結果の非同期表示/明示選択/選択版保存を接続
 * 0.1.0 Shunji Ogawa 2026/10/04 型付きASR/export要求へ呼出しを移行
 * 0.1.0 Shunji Ogawa 2026/10/19 未登録の自由全文要求をASRへ落とさず明示拒否
 * 0.1.0 Shunji Ogawa 2026/10/19 分岐意図・callbackの契約・宣言の役割を明記
 * 0.1.0 Shunji Ogawa 2026/10/19 明示CPU試用の通常dispatch・保存ack・全文表示と原文切替を接続
 * 0.1.0 Shunji Ogawa 2026/10/19 固定AVX2方式と通常workerの検証接続を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 補正準備の明示検査menuと独立worker終了を接続
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1280の明示モデル導入と同一枠の操作排他を接続
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1316の通常主処理RAM予算factoryへ接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1343の文章専用補正を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1344の明示API補正と独立保存を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1345の動的モデル・凍結単価と旧履歴互換を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1346の通常API補正・保存モデル表示・限定ログを接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1347の全幅音声テーブルと行API操作・設定分離を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1348の行メニュー直接実行と通常表示を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1351のAPI専用HTTP失敗・保存状態を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1352の実区間進捗・採用保存を接続
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1353の実工程バーを同じ行/sourceの説明へ同期
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1354の縦一覧/統合tab/固定行contextへ簡素化
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1355の標準行tableと可視headerへ修正
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1356の可変境界とmonitor内初期横幅を追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1357の旧新領域を配置後に同期更新
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1358の記号状態・実モデル列と完全詳細を全更新経路へ接続
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1359の採用保存成功後の自動比較と行別保留を追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1361の比較縦scroll同期と表示anchorを追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1362の可視境界追従と有限scroll観測の世代を追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1363の単一比較表と共通scroll契約へ更新
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1365の改善列/tooltip全更新を耐久採用の有無へ統一
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1370の文脈分割と明示途中再開を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372の独立話者投影と元原文保持を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1375の適用済み選好の保存と復元を追加
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1376のParakeet保存ACK後案内と明示続行を追加
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1378の明示更新境界を追加
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1380の短い状態と一回の詳細通知を追加
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1382の案内fontをMS ゴシックへ統一
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1385の日本語専用入口と旧選好互換を反映
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1397の通常ログと操作起点を標準処理へ接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1400の取得中dialogの標準keyboard処理を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1407の保存/copy対象・失敗時状態・UI thread契約を具体化
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1409の役割・引数・戻り値と所有寿命を具体化
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1424の既知の行頭空白だけを整形
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 * @note UIはengineを所有せず、worker終了まで通知先Windowを維持します
 */
//------------------------------------------------------------------------------
#include "app/compact_status.h"
#include "app/parakeet_api_followup.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// 試験用依存は専用targetだけに組み込み、通常製品へ公開しない
#if defined(KOEFUDE_WINDOW_TESTING)
#include "window_test_hooks.h"
#endif

#include <windows.h>

// Win32基本型を定義してから標準controlとCOM宣言を読み込む
#include <commctrl.h>
#include <richedit.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <windowsx.h>
#include <wrl/client.h>

#include "app/api_audio_status.h"
#include "app/api_text_correction_window.h"
#include "app/api_text_provenance.h"
#include "app/api_text_status.h"
#include "app/button_icons.h"
#include "app/chat_document.h"
#include "app/chat_document_controller.h"
#include "app/chat_keyboard.h"
#include "app/compact_table_status.h"
#include "app/comparison_scroll.h"
#include "app/control_accessibility.h"
#include "app/diagnostic_log.h"
#include "app/history_table.h"
#include "app/input_messages.h"
#include "app/localization.h"
#include "app/main_pane_layout.h"
#include "app/main_pane_repaint.h"
#include "app/message_dialog.h"
#include "app/model_preparation_window.h"
#include "app/resource.h"
#include "app/settings_window.h"
#include "app/smooth_scroll.h"
#include "app/speaker_filter.h"
#include "app/status_progress.h"
#include "app/whole_text_preparation_window.h"
#include "core/api_text_provenance.h"
#include "core/app_update.h"
#include "core/history_controller.h"
#include "core/job_controller.h"
#include "core/local_diagnostic_hooks.h"
#include "core/local_diagnostics.h"
#include "core/reading_export.h"
#include "core/reading_session.h"
#include "core/transcription_settings.h"
#include "core/whole_text_export.h"
#include "inference/api_audio_pipeline.h"
#include "inference/model_preparation_controller.h"
#include "inference/model_store.h"
#include "inference/primary_pipeline_application.h"
#include "inference/speaker_text_application.h"
#include "inference/text_alignment_reference.h"
#include "inference/whole_text_application.h"
#include "platform/audio_decoder.h"
#include "platform/history_store.h"
#include "platform/win_helpers.h"
#include "version.h"
#include <optional>

namespace koefude {
namespace {

using Microsoft::WRL::ComPtr ; // dialogのCOM参照を所有する型
constexpr UINT			   K_WAKE_MESSAGE = WM_APP + 1 ; // 所有pointerを運ばない結果回収通知
constexpr LRESULT		   K_COMPARE_READING_CHOICE = 100 ; // 保存enum外のUIだけのmode
constexpr UINT_PTR		   K_POLL_TIMER = 1 ; // PostMessage失敗とworker終了を回収するtimer

//------------------------------------------------------------------------------
/**
 * @brief 失敗分類と安全な再操作の案内を生成します
 * @param[in] kind 処理の失敗分類
 * @param[in] code 元APIの診断値
 * @return pathや本文を含まない所有説明
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::wstring errorText( const ErrorKind kind, const std::uint32_t code)
{
	UINT		   id = IDS_PROCESS_ERROR ; // 既定の再実行案内
	const unsigned inputId = inputErrorResource( kind) ; // 入力の理由別の表示先
	// モデル未準備は音声認識失敗と区別し、準備手順へ案内する
	if( kind == ErrorKind::MODEL_MISSING || kind == ErrorKind::MODEL_REJECTED) {
		id = IDS_MODEL_HELP ;
	}
	// 形式・破損・上限・時刻を区別して復旧へ案内する
	else if( inputId != 0) {
		id = inputId ;
	}
	// 保存先の復旧案内を示す
	else if( kind == ErrorKind::SAVE_FAILED) {
		id = IDS_SAVE_ERROR ;
	}
	// 一時領域の容量・権限の復旧へ案内する
	else if( kind == ErrorKind::TEMPORARY_STORAGE) {
		id = IDS_TEMPORARY_STORAGE_ERROR ;
	}
	// 取消しを失敗と呼ばない
	else if( kind == ErrorKind::CANCELLED) {
		return resource( IDS_CANCELLED) ;
	}
	std::array<wchar_t, 64> diagnostic{} ; // 本文を含まない分類番号と元code
	swprintf_s( diagnostic.data(), diagnostic.size(), L"\r\n[%u / 0x%08X]", static_cast<unsigned>( kind), code) ;
	return resource( id) + diagnostic.data() ;
}

//------------------------------------------------------------------------------
/**
 * @brief 履歴の保存・復元失敗を現UI言語と診断codeへ変換します
 * @param[in] kind 履歴に限定した異常分類
 * @param[in] code 元の診断値
 * @param[in] loading trueは起動時の読込失敗
 * @return 本文を含めず、復旧操作を案内する所有UTF-16文字列
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::wstring historyErrorText( const HistoryErrorKind kind, const std::uint32_t code, const bool loading)
{
	UINT id = loading ? IDS_HISTORY_LOAD_ERROR : IDS_HISTORY_SAVE_ERROR ; // 操作に応じた既定案内
	// 履歴の保護理由と利用者の対応を分類ごとに説明する
	switch( kind) {
		case HistoryErrorKind::BUSY:
			id = IDS_HISTORY_IN_USE ;
			break ; // 別の起動による上書きを防止
		case HistoryErrorKind::CORRUPT:
			id = IDS_HISTORY_CORRUPT ;
			break ; // 不正な保存内容を変更せず保持
		case HistoryErrorKind::VERSION:
			id = IDS_HISTORY_VERSION ;
			break ; // 未対応形式を推測で解釈しない
		case HistoryErrorKind::TOO_LARGE:
			id = IDS_HISTORY_TOO_LARGE ;
			break ; // 古い履歴を自動削除して上限を回避しない
		default:
			break ; // I/O・保存先・内部例外は今回の操作に対応する案内
	}
	std::array<wchar_t, 64> diagnostic{} ; // 音声やpathを含まない履歴用の診断値
	swprintf_s( diagnostic.data(), diagnostic.size(), L" [H%u / 0x%08X]", static_cast<unsigned>( kind), code) ;
	return resource( id) + diagnostic.data() ;
}

//------------------------------------------------------------------------------
/**
 * @brief 正規化済み音声pathをWindowsの大文字小文字規則で比較します
 * @param[in] left 同期比較中に借用するpath
 * @param[in] right 同期比較中に借用するpath
 * @return 同じ綴りのlocal pathならtrue。音声fileの読取りは行いません
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool sameInput( const std::filesystem::path &left, const std::filesystem::path &right)
{
	// Win32の長さ引数へ収まらない異常pathは一致として扱わない
	if( left.native().size() > 32767 || right.native().size() > 32767) {
		return false ;
	}
	return CompareStringOrdinal( left.c_str(), static_cast<int>( left.native().size()), right.c_str(),
								static_cast<int>( right.native().size()), TRUE) == CSTR_EQUAL ;
}

//------------------------------------------------------------------------------
/**
 * @brief 同じworkerで推論または明示保存を行います
 * @param[in] request 固定された処理要求
 * @param[in] stop 協調取消しの状態
 * @param[in] progress worker上の進捗通知
 * @param[in] config EXE由来の固定DLLと標準二root。通常runnerが値で所有します
 * @param[in] wholeServices 専用試験targetだけの同期依存注入、通常製品には存在しません
 * @return UIへ渡す所有応答。全文は明示CPU試用、保存時は入力snapshot
 */
//------------------------------------------------------------------------------
[[nodiscard]] JobOutput executeJob( const JobRequest &request, const std::stop_token stop,
								   const ProgressCallback			&progress,
								   // この同期境界で所有する値、または確定状態の検証値です
								   const WholeTextApplicationConfig &config = {}
#if defined(KOEFUDE_WINDOW_TESTING)
                                    ,
								   detail::WholeTextApplicationServices *wholeServices = nullptr
#endif
)
{
	checkCancelled( stop) ;
	// 自由全文専用TXTも同じ有限workerで保存します
	if( const auto *save = std::get_if<WholeTextExportJobRequest>( &request.payload)) {
		reportProgress( progress, Stage::SAVING, -1) ;
		saveWholeTextExport( *save) ;
		return save->source ;
	}
	// 保存も同じ有限queueで扱い、UI threadでfile I/Oを行わない
	if( const auto *save = std::get_if<ExportJobRequest>( &request.payload)) {
		reportProgress( progress, Stage::SAVING, -1) ;
		saveReadingExport( *save) ;
		return save->source ;
	}
	// 現在、採用済みの製品生成器はない。未採用方式を試用入口から実行しない
	if( std::holds_alternative<ReadingJobRequest>( request.payload)) {
		throw OperationError( ErrorKind::MODEL_MISSING) ;
	}
	// 明示CPU試用だけを実PCMと全補助資料の通常workflowへ渡します
	if( const auto *whole = std::get_if<WholeTextCorrectionJobRequest>( &request.payload)) {
#if defined(KOEFUDE_WINDOW_TESTING)
        // 同じdispatch分岐の同期依存だけを専用試験で注入します
		if( wholeServices) {
			return detail::runWholeTextApplicationWithServices( *whole, config, *wholeServices, stop, progress) ;
		}
#endif
        return runWholeTextApplicationJob( *whole, config, stop, progress) ;
	}
	return runPrimaryPipelineJob( request.asr().input, request.asr().options, stop, progress) ;
}

//------------------------------------------------------------------------------
/**
 * @brief chat領域のdropを親Windowの同じ受付処理へ渡します
 * @param[in] window subclass対象control
 * @param[in] message Win32 message
 * @param[in] wparam message固有値
 * @param[in] lparam message固有値
 * @param[in] subclassId 登録識別子
 * @param[in] reference 未使用の登録値
 * @return 処理済みなら0、そのほかは標準処理結果
 */
//------------------------------------------------------------------------------
LRESULT CALLBACK chatSubclass( HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR subclassId,
							  DWORD_PTR reference) noexcept
{
	static_cast<void>( reference) ;
	// Tabだけを標準dialog移動へ渡し、矢印や本文の選択要求は維持する
	if( message == WM_GETDLGCODE) {
		return chatDialogCode( DefSubclassProc( window, message, wparam, lparam), wparam) ;
	}
	// HDROPは親で一度だけDragFinishし、Rich Editへfile本文を挿入させない
	if( message == WM_DROPFILES) {
		return SendMessageW( GetParent( window), message, wparam, lparam) ;
	}
	// control破棄時に自身のsubclass登録を解放する
	if( message == WM_NCDESTROY) {
		RemoveWindowSubclass( window, chatSubclass, subclassId) ;
	}
	return DefSubclassProc( window, message, wparam, lparam) ;
}

/** @brief 翻訳文字列と独立して保持する状態と診断値です */
struct UiStatus {
	/// 0は診断付き失敗、そのほかは状態resource
	UINT							  resourceId = IDS_READY ;
	/// 実測進捗率。負数は総量不明
	int								  percent = -1 ;
	/// 失敗時だけ使う分類
	ErrorKind						  error = ErrorKind::INTERNAL ;
	/// 失敗時の元API診断値
	std::uint32_t					  code = 0 ;
	/// API専用終端、音声復旧案内へ渡しません
	std::optional<ApiTextStatusView>  api ;
	/// 音声APIだけの固定段階/終端
	std::optional<ApiAudioStatusView> audio ;
	/// 原API完了とは別のPC内進捗
	std::optional<ApiTextProgress>	  projection ;
	/// 更新専用診断、音声errorへ流しません
	std::optional<UpdateEvent>		  update ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 常設status用の短い状態を作ります
	 * @return 実進捗以外の数値を足さない表示
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::wstring		  compactText() const
	{
		if( update) {
			const auto state = update->state ;
			return resource( state == UpdateState::CHECKING		? IDS_SHORT_UPDATE_CHECKING
							: state == UpdateState::AVAILABLE	? IDS_SHORT_UPDATE_AVAILABLE
							: state == UpdateState::CURRENT		? IDS_SHORT_UPDATE_CURRENT
							: state == UpdateState::DOWNLOADING ? IDS_SHORT_UPDATE_DOWNLOADING
							: state == UpdateState::READY		? IDS_SHORT_UPDATE_READY
							: state == UpdateState::CANCELLED	? IDS_SHORT_CANCELLED
							: state == UpdateState::FAILURE		? IDS_SHORT_UPDATE_FAILED
																: IDS_SHORT_UPDATE_UNCONFIRMED) ;
		}
		if( projection) {
			const auto &p = *projection ;
			return compactApiProgress( resource( IDS_SHORT_DIARIZING) + L": " + resource( compactApiPhase( p.phase)), p,
									  p.startedMs && p.observedMs >= p.startedMs ? (p.observedMs - p.startedMs) / 1000
																				 : 0) ;
		}
		if( audio) {
			const auto &a = *audio ;
			const auto	id =
				 !a.ended									   ? compactApiPhase( a.progress.phase)
				 : a.saveFailed || a.preparationFailed		   ? IDS_SHORT_FAILED
				 : a.terminal == ApiAudioTerminal::COMPLETED   ? IDS_SHORT_DONE
				 : a.terminal == ApiAudioTerminal::CANCELLED   ? IDS_SHORT_CANCELLED
				 : a.terminal == ApiAudioTerminal::INTERRUPTED ? IDS_SHORT_INTERRUPTED
				 : a.terminal == ApiAudioTerminal::NO_SPEECH || a.terminal == ApiAudioTerminal::EMPTY_TRANSCRIPT
					 ? IDS_SHORT_NO_TEXT
					 : IDS_SHORT_FAILED ;
			return compactApiProgress( resource( IDS_SHORT_AUDIO) + L": " + resource( id), a.progress,
									  a.progress.startedMs && a.progress.observedMs >= a.progress.startedMs
										  ? (a.progress.observedMs - a.progress.startedMs) / 1000
										  : 0) ;
		}
		if( api) {
			const auto state = compactApiState( *api) ;
			const auto id = state == CompactTableState::ACTIVE			? compactApiPhase( api->progress.phase)
							: state == CompactTableState::COMPLETE		? IDS_SHORT_DONE
							: state == CompactTableState::DISPLAY_SAVED ? IDS_SHORT_SAVED
							: state == CompactTableState::FAILURE		? IDS_SHORT_FAILED
							: state == CompactTableState::STOPPED		? IDS_SHORT_CANCELLED
							: state == CompactTableState::IDLE			? IDS_SHORT_READY
																		: IDS_SHORT_WARNING ;
			return compactApiProgress( resource( IDS_SHORT_API) + L": " + resource( id), api->progress,
									  api->elapsedSeconds) ;
		}
		const auto id = resourceId == 0 ? (error == ErrorKind::CANCELLED ? IDS_SHORT_CANCELLED : IDS_SHORT_FAILED)
										: compactStatusResource( resourceId) ;
		return resource( id) + (percent >= 0 ? L" " + std::to_wstring( percent) + L"%" : L"") ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 現UI言語で状態を描画します
	 * @return pathや本文を含まない所有説明
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::wstring text() const
	{
		// 更新は固定分類/元codeだけを示し、音声やAPIの再投入を案内しません
		if( update) {
			auto value = resource( resourceId) ; // 現在言語の固定説明
			// 実候補版を持つ段階だけ版を併記します
			if( update->state == UpdateState::AVAILABLE || update->state == UpdateState::READY ||
			   update->state == UpdateState::CURRENT) {
				value += L" " + toWide( updateVersionText( update->version)) ;
			}
			// 未確認/通信/信頼の失敗を固定enumと元数値で診断可能にします
			if( update->error != UpdateError::NONE) {
				std::array<wchar_t, 96> diagnostic{} ; // 本文/URL/pathなし
				swprintf_s( diagnostic.data(), diagnostic.size(), L" [U%u / %u / 0x%08X]",
						   static_cast<unsigned>( update->error), update->code, update->code) ;
				value += diagnostic.data() ;
			}
			return value ;
		}
		// 投影の進捗を音声原文の完了状態から分離する
		if( projection) {
			const auto &p = *projection ; // 実窓数と単調経過だけ
			// 同じownerへ束縛する安定識別値
			const auto	id = p.phase == ApiTextPhase::COMPLETED	  ? IDS_SPEAKER_TEXT_DONE
							 : p.phase == ApiTextPhase::FAILURE	  ? IDS_SPEAKER_TEXT_FAILED
							 : p.phase == ApiTextPhase::CANCELLED ? IDS_SPEAKER_TEXT_CANCELLED
							 : p.phase == ApiTextPhase::PREPARING ? IDS_SPEAKER_TEXT_PREPARING
																  : IDS_SPEAKER_TEXT_ACTIVE ;
			// 当境界で生成する所有値
			auto		value = (audio ? audio->text() + L" | " : L"") + resource( id) ;
			// 実窓数が分かる場合だけ完了数を付ける
			if( p.totalUnits) {
				value += L" " + std::to_wstring( p.completedUnits) + L"/" + std::to_wstring( p.totalUnits) ;
			}
			// 単調時計の実観測範囲だけを経過表示へ使う
			if( p.startedMs && p.observedMs >= p.startedMs) {
				value += L" / " + formatElapsedTime( static_cast<double>( p.observedMs - p.startedMs) / 1000.0) ;
			}
			if( p.phase == ApiTextPhase::FAILURE && code) {
				value += L" [code=" + std::to_wstring( code) + L"]" ;
			}
			return value ;
		}
		// 音声API失敗もGPU/codec向け案内へ渡しません
		if( audio) {
			return audio->text() ;
		}
		// APIの固定診断は音声処理のerror_textへ渡しません
		if( api) {
			return api->text() ;
		}
		// 診断値を保持した失敗だけを再操作の案内へ変換する
		if( resourceId == 0) {
			return errorText( error, code) ;
		}
		// 表示だけの失敗を音声の再投入案内へ変えず、元の診断値を残す
		if( resourceId == IDS_READING_FAILED || resourceId == IDS_READING_GENERATION_FAILED ||
		   resourceId == IDS_WHOLE_PCM_MISMATCH || resourceId == IDS_WHOLE_NO_PCM ||
		   resourceId == IDS_WHOLE_RESOURCES || resourceId == IDS_WHOLE_MODELS ||
		   resourceId == IDS_WHOLE_CPU_UNSUPPORTED || resourceId == IDS_WHOLE_RUNTIME_REJECTED ||
		   resourceId == IDS_WHOLE_RUNTIME_MISSING) {
			std::array<wchar_t, 64> diagnostic{} ; // 内容/pathを含まない分類と元code
			swprintf_s( diagnostic.data(), diagnostic.size(), L" [%u / 0x%08X]", static_cast<unsigned>( error), code) ;
			return resource( resourceId) + diagnostic.data() ;
		}
		return resource( resourceId) + (percent >= 0 ? L"  " + std::to_wstring( percent) + L"%" : L"") ;
	}
} ;

/** @brief 発生箇所だけで予約する通知用途です。進捗/表示操作から予約しません */
enum class StatusNoticeKind : std::size_t {
	/// 音声認識jobの終端通知
	ASR,
	/// API文章改善jobの終端通知
	API,
	/// PC内文章整形jobの終端通知
	READING,
	/// PC内全文補正jobの終端通知
	WHOLE,
	/// 保存原文への話者projection試行の終端通知
	PROJECTION,
	/// 明示保存jobの終端通知
	EXPORT,
	/// 履歴読込みの終端通知
	HISTORY_LOAD,
	/// 履歴書込みの終端通知
	HISTORY_SAVE,
	/// 明示更新確認または更新取得の終端通知
	UPDATE,
	/// 通知用途別の予約配列の要素数。通知用途として使用しません
	COUNT
} ;
/** @brief modalへiteratorを持ち越さない一回の型付き詳細通知です */
struct StatusNotice {
	/// 同じ用途内のopだけを比較
	StatusNoticeKind				  kind = StatusNoticeKind::ASR ;
	/// 同じDTO再配送を拒否
	std::uint64_t					  operation = 0 ;
	/// 0は行に属さない操作
	std::uint64_t					  history = 0 ;
	/// 同じ原文owner。本文copy/hashなし
	std::shared_ptr<const Transcript> source ;
	/// 現言語で表示する固定説明と診断
	UiStatus						  status ;
	/// 音声errorと分離
	HistoryErrorKind				  historyError = HistoryErrorKind::INTERNAL ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 整形状態をASRと別の説明へ変換します
 * @param[in] record 保存可能な状態
 * @param[in] percent 実通知、総量不明なら負数
 * @return 原文再投入を案内しない整形専用の説明
 */
//------------------------------------------------------------------------------
[[nodiscard]] UiStatus readingStatus( const ReadingRecord &record, const int percent = -1)
{
	// 整形の実段階と終端をASRのCompleted/Failedから分離する
	switch( record.state) {
		case ReadingState::QUEUED: // 同じ直列workerの順番待ち
			return {IDS_READING_QUEUED} ;
		case ReadingState::LOADING: // 同期生成器の実準備
			return {IDS_READING_LOADING, percent} ;
		case ReadingState::FORMATTING: // 実処理通知だけの進捗
			return {IDS_READING_FORMATTING, percent} ;
		case ReadingState::VALIDATING: // workerの全体検証
			return {IDS_READING_VALIDATING} ;
		case ReadingState::CANCELLING: // 同期処理の協調終端待ち
			return {IDS_READING_CANCELLING} ;
		case ReadingState::COMPLETED: // 原文を残した成功
			return {IDS_READING_DONE} ;
		case ReadingState::FAILURE: // 原文と前回の成功を残した失敗
			return {IDS_READING_GENERATION_FAILED, -1,
					record.reason == ReadingReason::INVALID_OUTPUT		   ? ErrorKind::INVALID_INPUT
					: record.reason == ReadingReason::MODEL_UNAVAILABLE	   ? ErrorKind::MODEL_MISSING
					: record.reason == ReadingReason::RESOURCE_UNAVAILABLE ? ErrorKind::RESULT_TOO_LARGE
																		   : ErrorKind::INFERENCE_FAILED,
					record.code} ;
		case ReadingState::CANCELLED: // 結果を消さない明示取消し
			return {IDS_READING_CANCELLED} ;
		case ReadingState::INTERRUPTED: // 再生成なしの履歴復元
			return {IDS_READING_INTERRUPTED} ;
		case ReadingState::IDLE: // 未実行を成功へ変えない
			return {IDS_READY} ;
	}
	return {IDS_READING_GENERATION_FAILED, -1, ErrorKind::INTERNAL} ;
}

/** @brief 一つの投入fileのUI履歴です。音声PCMは保持しません */
struct FileCard {
	/// 再起動と削除を跨いで再利用しない履歴ID
	std::uint64_t						  id = 0 ;
	/// UIで表示する利用者のlocal file
	std::filesystem::path				  path ;
	/// 言語切替後も再描画できる段階・診断
	UiStatus							  status ;
	/// 所有不変結果
	std::shared_ptr<const Transcript>	  result ;
	/// worker正本の有効全文/最新prefix
	WholeTextBoundedRecord				  bounded ;
	/// 原文から独立した最後の有効全文
	WholeTextCorrectionRecord			  wholeText ;
	/// 音声APIの原文/部分終端。改善採用とは別
	std::shared_ptr<const ApiAudioRecord> apiAudio ;
	/// generic準備失敗でも既存prefixを保持
	bool								  audioResumeRequested = false ;
	/// 新規local Parakeetの終端で一度だけ判定
	bool								  parakeetOfferRequested = false ;
	/// このsessionのplan成立前失敗もAPI専用説明へ分離
	bool								  audioApiRequested = false ;
	/// API候補・採用・表示は独立sidecar
	ApiTextCorrectionRecord				  apiText ;
	/// 今sessionの成功原文ownerだけ
	std::shared_ptr<const Transcript>	  pendingCompareSource ;
	/// 次選択で一度消費する採用
	std::shared_ptr<const ApiTextAttempt> pendingCompareCandidate ;
	/// 新ASRだけの有限待ち、復元では未arm
	AutomaticTextContextRequest			  automaticText ;
	/// 通常UIの表示意図
	ResultPreference					  resultPreference = ResultPreference::AUTOMATIC ;
	/// 原文/整形の表示意図と最後の有効結果
	ReadingRecord						  reading ;
	/// 進行中の原文owner。終端で解放
	std::shared_ptr<const Transcript>	  readingSource ;
	/// 終端event未受領ならtrue
	bool								  busy = true ;
	/// 利用者が取消しを要求した場合true
	bool								  cancelling = false ;
	/// fileごとに保持する表示話者IDまたは専用値
	int									  filter = K_ALL_SPEAKERS ;
	/// 起動中だけ有効な推論要求ID。未実行・終端では0
	std::uint64_t						  operationId = 0 ;
	/// 同writerの独立採用/最新試行
	SpeakerTextRecord					  speakerText ;
	/// 非永続の用途別既予約op、履歴再読では通知しません
	std::array<std::uint64_t, static_cast<std::size_t>( StatusNoticeKind::COUNT)> noticeOperations{} ;
	/// raw ASR終端として扱わない補助job
	bool																		 projectionActive = false ;
	/// 今sessionの明示音声操作だけの一回意図
	bool																		 projectionFollowup = false ;
	/// POST前に準備済みの補助資産root
	std::filesystem::path														 projectionModelRoot ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief カードの表示意図を有効な結果へ解決します
 * @param[in] card UI所有値
 * @return 実表示種類。本文hashは行いません
 */
//------------------------------------------------------------------------------
[[nodiscard]] ResultPreference cardResultDisplay( const FileCard &card) noexcept
{
	// APIの原文選択中は旧local補正文へ戻さず、表示・copy・保存を原文へ統一します
	if( apiTextOriginalSelected( card.apiText)) {
		return ResultPreference::ORIGINAL ;
	}
	return resolveBoundedDisplay( card.resultPreference, card.bounded, card.wholeText.corrected != nullptr,
								 card.reading.formatted != nullptr) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 保存済みAPI表示意図を通常本文へ解決します
 * @param[in] card UI所有値
 * @return 採用済み改善本文の表示ならtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool cardShowsApiText( const FileCard &card) noexcept
{
	return card.apiText.adopted && !card.apiText.showOriginal &&
		   (card.resultPreference != ResultPreference::ORIGINAL ||
			card.apiText.adopted->consent.intent == ApiTextIntent::ROW_MENU) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 旧export/viewへ渡す行表示を解決します
 * @param[in] card UI所有値
 * @return 原文または旧整形
 */
//------------------------------------------------------------------------------
[[nodiscard]] ReadingDisplay cardReadingDisplay( const FileCard &card) noexcept
{
	return cardResultDisplay( card) == ResultPreference::LEGACY_FORMATTED ? ReadingDisplay::FORMATTED
																		 : ReadingDisplay::ORIGINAL ;
}
//------------------------------------------------------------------------------
/**
 * @brief 全文試行の実phaseと終端を説明します
 * @param[in] record 全文record
 * @param[in] percent 実測率
 * @return 内容なしの状態
 */
//------------------------------------------------------------------------------
[[nodiscard]] UiStatus wholeTextStatus( const WholeTextCorrectionRecord &record,
									   // この同期境界で所有する値、または確定状態の検証値です
									   const int						percent = -1)
{
	// 専用の原因と対応を元診断code付きで示します
	if( record.state == ReadingState::FAILURE) {
		// この同期境界で所有する値、または確定状態の検証値です
		const UINT id = record.code == 0x57560001	? IDS_WHOLE_CPU_UNSUPPORTED
						: record.code == 0x57560002 ? IDS_WHOLE_RUNTIME_REJECTED
						: record.code == 0x57560003 ? IDS_WHOLE_RUNTIME_MISSING
						: record.code == 0x57550003 ? IDS_WHOLE_PCM_MISMATCH
						: record.code == 0x57550002 ? IDS_WHOLE_NO_PCM
						: record.code == static_cast<std::uint32_t>( WholeTextBoundedDiagnostic::SOURCE_METADATA_MISSING)
							? IDS_TEXT_CONTEXT_METADATA
						: record.code == static_cast<std::uint32_t>( WholeTextBoundedDiagnostic::PROTECTED_SPAN_CHANGED)
							? IDS_TEXT_CONTEXT_PROTECTED
						: record.reason == ReadingReason::RESOURCE_UNAVAILABLE ? IDS_WHOLE_RESOURCES
						: record.reason == ReadingReason::MODEL_UNAVAILABLE
							? IDS_WHOLE_MODELS
							: IDS_READING_GENERATION_FAILED ; // 有限理由
		return {id, -1,
				record.reason == ReadingReason::RESOURCE_UNAVAILABLE ? ErrorKind::RESULT_TOO_LARGE
																	 : ErrorKind::INFERENCE_FAILED,
				record.code} ;
	}
	ReadingRecord legacy ; // 状態と理由だけを共通終端説明へ渡します
	legacy.state = record.state ;
	legacy.reason = record.reason ;
	legacy.code = record.code ;
	// 明示取消し中はphaseより停止待ちを優先します
	if( record.active && record.state != ReadingState::CANCELLING) {
		const auto phase = record.phase ; // 本文を走査しない小値
		// PCM、補助認識、準備、生成、全体検証を有限resourceへ分けます
		if( phase != WholeTextPhase::NONE) {
			return {IDS_WHOLE_PHASES + static_cast<UINT>( phase) - 1, percent} ;
		}
		// 再decodeの100%からidentityの0%へ同じ説明を巻き戻しません
		if( record.state != ReadingState::QUEUED) {
			return {IDS_STAGES + static_cast<UINT>( record.stage), percent} ;
		}
	}
	return readingStatus( legacy, percent) ;
}

/** @brief 標準controlの配置と非同期に永続化する文字起こし履歴を管理します */
class MainWindow final {
#if defined(KOEFUDE_WINDOW_TESTING)
    friend struct MainWindowTestAccess ; // 同じ試験process内だけで所有状態を照合する
	friend struct SimpleUi1354TestAccess ; // 所有SW_HIDEの固定三caseだけ
	friend struct Followup1376TestAccess ; // 同意/ACK/失効の自所有有限試験だけ
	friend struct Settings1375TestAccess ; // 保存復元と起動barrierの有限試験だけ
	friend struct Notices1380TestAccess ; // 自所有の短い状態/通知の有限試験だけ
	friend struct Update1378TestAccess ; // 更新fakeと所有SW_HIDEの有限試験だけ
	friend struct Speaker1372TestAccess ; // 自所有の投影縦断caseだけ
	friend struct Resizable1356TestAccess ; // capture/cursorを使わないdirect内部境界
	friend struct Compact1358TestAccess ; // 自所有の有限表示検証だけ
	friend struct AutoCompare1359TestAccess ; // 採用ACKと行別保留の有限試験だけ
	friend struct Unified1363TestAccess ; // 自所有単一比較表の有限内部試験
	friend struct LiveRepaint1357TestAccess ; // 実同期API境界の有限観測だけ
#endif
  public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 起動時のモデルdirectoryを保持します
	 * @param[in] modelRoot モデルの所有path
	 * @param[in] historyRoot 履歴の所有path。空はユーザー専用の既定先
	 * @param[in] initialInput 復元後に開く明示入力。空は追加なし
	 * @param[in] diagnosticLog 試験起動時だけ有効な診断ログの借用
	 * @param[in] testHooks 専用試験targetにだけ存在する、全worker終了まで有効な借用依存
	 * @param[in] localDiagnostics 通常ログ。全workerより長寿命、旧試験はnull
	 * @note 専用試験targetでは追加のtestHooksを全worker/Window破棄まで借用します
	 */
	//------------------------------------------------------------------------------
	MainWindow( std::filesystem::path modelRoot, std::filesystem::path historyRoot, std::filesystem::path initialInput,
			   DiagnosticLog &diagnosticLog
#if defined(KOEFUDE_WINDOW_TESTING)
               ,
			   MainWindowTestHooks &testHooks
#endif
               ,
			   LocalDiagnosticLog *localDiagnostics = nullptr)
		: modelRoot_( std::move( modelRoot)), historyRoot_( std::move( historyRoot)),
		  initialInput_( std::move( initialInput)), diagnosticLog_( diagnosticLog), localDiagnostics_( localDiagnostics)
#if defined(KOEFUDE_WINDOW_TESTING)
          ,
		  testHooks_( testHooks)
#endif
    {
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief worker終了後にGDI resourceを解放します
	 */
	//------------------------------------------------------------------------------
	~MainWindow()
	{
		updater_.reset() ; // 通知先HWNDより先に取消し/closingを完了します
		transcriptionSettings_.reset() ; // 初期化失敗時も自所有通知をWindowより先に終了
		documents_.reset() ; // 初期化失敗でも通知終了を確認してからWindowを解放する
		preparation_.reset() ;
		controller_.reset() ;
		history_.reset() ;
		// 初期化例外の場合も、全通知終了後に残ったWindowを解放する
		if( IsWindow( window_)) {
			DestroyWindow( window_) ;
		}
		// Windowから外れたfontだけを破棄する
		if( font_) {
			DeleteObject( font_) ;
		}
		// 見出しfontは通常fontと独立した所有resource
		if( titleFont_) {
			DeleteObject( titleFont_) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief Windowを作成して表示します
	 * @param[in] instance 本exeの非所有module handle
	 * @param[in] show 起動時の表示方法
	 * @return 作成成功時true
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool create( const HINSTANCE instance, const int show)
	{
		// 日本語専用版は旧English選好を読まず、起動だけでは保存もしません
		static_cast<void>( setUiLanguage( UiLanguage::JAPANESE)) ;
		WNDCLASSEXW definition{} ; // 登録するWindow classの値
		definition.cbSize = sizeof(definition) ;
		definition.style = CS_HREDRAW | CS_VREDRAW ;
		definition.hInstance = instance ;
		definition.lpfnWndProc = windowProc ;
		definition.lpszClassName = L"KoeFudeMainWindow" ;
		definition.hCursor = LoadCursorW( nullptr, IDC_ARROW) ;
		definition.hIcon = LoadIconW( instance, MAKEINTRESOURCEW(IDI_APP)) ;
		definition.hIconSm = definition.hIcon ;
		definition.hbrBackground = GetSysColorBrush( COLOR_BTNFACE) ;
		definition.lpszMenuName = nullptr ; // 選択言語のmenuを初期化時に明示設定する
		// 同じclassの重複登録は本processでは行わない
		if( !RegisterClassExW( &definition)) {
			return false ;
		}
		window_ = CreateWindowExW(
			WS_EX_CONTROLPARENT, definition.lpszClassName, (resource( IDS_TITLE) + L" " + KOEFUDE_VERSION_WIDE).c_str(),
			WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, paneScale( 1560, 96, GetDpiForSystem()),
			paneScale( 860, 96, GetDpiForSystem()), nullptr, nullptr, instance, this) ;
		// WM_CREATE失敗時にはmessage loopへ入らない
		if( !window_) {
			return false ;
		}
		MONITORINFO monitor{sizeof(monitor)} ; // 作成されたWindowが属するmonitorだけを読取る
		// 表示前に実DPIと作業領域へ収め、SW_HIDE/最大化の指定は後のShowWindowへ渡します
		if( GetMonitorInfoW( MonitorFromWindow( window_, MONITOR_DEFAULTTONEAREST), &monitor)) {
			const auto &work = monitor.rcWork ; // OS所有値のcopy元
			const auto	bounds = initialPaneWindow( {work.left, work.top, work.right, work.bottom},
												   GetDpiForWindow( window_)) ; // 有界外寸
			SetWindowPos( window_, nullptr, bounds.x, bounds.y, bounds.width, bounds.height,
						 SWP_NOZORDER | SWP_NOACTIVATE) ;
		}
		ShowWindow( window_, show) ;
		UpdateWindow( window_) ;
		return true ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief dialog keyboard navigationに使うWindowを返します
	 * @return 非所有HWND
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] HWND handle() const noexcept
	{
		return window_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 準備画面と本画面の標準keyboard移動を処理します
	 * @param[in,out] message 現在のOS message。同期呼出し中のborrow
	 * @return いずれかの画面が処理した場合true
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool dialogMessage( MSG &message)
	{
		const HWND preparation = preparationWindow_.handle() ; // UIだけが使用する非所有HWND
		const auto whole = wholeTextPreparationWindow_.handle() ; // 補正準備の非所有HWND
		const auto catalogProgress = apiTextWindow_.catalogProgressHandle() ; // 自所有dialog
		// 取得中dialogのEsc/標準移動をmain acceleratorより先に処理します
		if( catalogProgress && IsWindowVisible( catalogProgress) && IsDialogMessageW( catalogProgress, &message)) {
			return true ;
		}
		// acceleratorは自Windowのmessage loop内だけへ適用します
		if( TranslateAcceleratorW( window_, accelerators_, &message)) {
			return true ;
		}
		// 人手Escはdialog navigationより前にdragの取消しへ渡します
		if( message.hwnd == control( IDC_MAIN_SPLITTER) && message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE &&
		   splitter_.dragging) {
			finishSplitter( true) ;
			return true ;
		}
		const auto unified = settingsWindow_.handle() ; // 四tabの所有dialog
		const auto settings = apiTextWindow_.settingsHandle() ; // 旧adapterの独立設定だけ
		const auto api = apiTextWindow_.handle() ; // 自分のAPI確認画面
		return (unified && IsWindowVisible( unified) && IsDialogMessageW( unified, &message)) ||
			   (renameWindow_ && IsWindowVisible( renameWindow_) && IsDialogMessageW( renameWindow_, &message)) ||
			   (settings && GetParent( settings) != unified && IsWindowVisible( settings) &&
				IsDialogMessageW( settings, &message)) ||
			   (api && IsWindowVisible( api) && IsDialogMessageW( api, &message)) ||
			   (whole && IsWindowVisible( whole) && IsDialogMessageW( whole, &message)) ||
			   (preparation && IsWindowVisible( preparation) && IsDialogMessageW( preparation, &message)) ||
			   IsDialogMessageW( window_, &message) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief fileカードを作成して有限queueへ投入します
	 * @param[in] input 利用者の指定path。workerで改めて検証
	 * @param[in] newRun trueは明示再実行。falseは登録済みの最新履歴を開きます
	 */
	//------------------------------------------------------------------------------
	void addFile( const std::filesystem::path &input, const bool newRun = false)
	{
		// 復元前/失敗時の明示dropも既定設定で誤受付しません
		if( !transcriptionSettingsReady_) {
			showMessage( transcriptionSettingsLoadNotice_) ;
			return ;
		}
		// drop経路も導入中に新しい認識を受け付けません
		if( wholeTextPreparationWindow_.busy()) {
			setStatus( {IDS_WTP_BUSY}) ;
			return ;
		}
		// 読込完了前や既存履歴の異常時に、新しい空履歴で上書きしない
		if( !historyReady_) {
			showMessage( historyFailed_ ? IDS_HISTORY_LOAD_ERROR : IDS_HISTORY_LOADING) ;
			return ;
		}
		// この同期境界で所有する値、または確定状態の検証値です
		auto normalized = std::filesystem::absolute( input).lexically_normal() ; // 内容を開かない正規化
		normalized.make_preferred() ;
		// 通常の再投入は二重認識せず、最新の同じpathの履歴を表示する
		if( !newRun && !closing_) {
			// 最新の登録から逆順に探し、比較用の複数実行があっても対象を一つ選ぶ
			for( std::size_t index = cards_.size(); index > 0; --index) {
				// 登録済みなら件数上限でも既存結果を開ける
				if( sameInput( cards_[index - 1].path, normalized)) {
					selected_ = index - 1 ;
					refreshHistory() ;
					showCard() ;
					setStatus( selectedStatus()) ;
					return ;
				}
			}
		}
		// 停止中または履歴上限では、新しいaudio所有結果を増やさない
		if( closing_ || cards_.size() >= K_MAXIMUM_HISTORY ||
		   nextHistoryId_ == std::numeric_limits<std::uint64_t>::max()) {
			showMessage( IDS_FULL) ;
			return ;
		}
		JobRequest request ; // 受付時のUI設定snapshot
		// 準備不足を認識失敗の履歴へ追加せず、明示取得の画面へ案内する
		if( !modelReady_) {
			showModelPreparation() ;
			return ;
		}
		const bool audioApi = selectedModelOptions().asrModel == AsrModel::OPEN_AI_AUDIO ;
		// キーApply途中や未準備のAPIを別設定/キーへ読み替えません
		if( audioApi && !apiTextWindow_.audioReady()) {
			setStatus( {IDS_AUDIO_NOT_READY}) ;
			return ;
		}
		request.asr().historyId = nextHistoryId_ ;
		request.asr().audioApiSelected = audioApi ;
		request.asr().apiKeyRevision = audioApi ? apiTextWindow_.audioCatalog().keyRevision : 0 ;
		request.asr().input = normalized ;
		request.asr().options.modelRoot = modelEvent_.plan->effectiveRoot ;
		request.asr().options = selectedModelOptions() ; // Apply済みの値だけを固定
		request.asr().options.modelRoot = modelEvent_.plan->effectiveRoot ;
		FileCard card ; // 受付前にpathの所有copyを済ませる新しい記録
		card.id = nextHistoryId_ ;
		card.audioApiRequested = audioApi ;
		card.parakeetOfferRequested = !audioApi && request.asr().options.asrModel == AsrModel::PARAKEET ;
		card.projectionFollowup = audioApi && request.asr().options.asrProfile == AsrProfile::QUALITY &&
								  !request.asr().options.autoLanguage && request.asr().options.diarization ;
		card.projectionModelRoot = request.asr().options.modelRoot ;
		card.path = normalized ;
		card.status = {IDS_QUEUED} ;
		// この同期境界で所有する値、または確定状態の検証値です
		const Backend requestedBackend = request.asr().options.backend ; // 受付時の要求値をmove前に固定
		const auto	  id = controller_->submit( std::move( request)) ; // 起動中だけのoperation ID
		// queueが満杯なら履歴に実行不能なカードを作らない
		if( id == 0) {
			showMessage( IDS_FULL) ;
			return ;
		}
		card.operationId = id ;
		++nextHistoryId_ ;
		cards_.push_back( std::move( card)) ;
		diagnosticLog_.append( DiagnosticEvent::JOB_ACCEPTED, id, cards_.size(), static_cast<std::uint64_t>( asrModel_)) ;
		diagnosticLog_.append( DiagnosticEvent::JOB_CONFIGURED, id, static_cast<std::uint64_t>( asrModel_),
							  static_cast<std::uint64_t>( asrProfile_), static_cast<std::uint32_t>( requestedBackend)) ;
		selected_ = cards_.size() - 1 ;
		refreshHistory() ;
		showCard() ;
		saveHistory() ;
	}

	private:
	/** @brief 行menuとdialogの対象をID/source/表示版/取消opへ固定します */
	struct RowActionTarget {
		/// 永続履歴ID、0は対象なし
		std::uint64_t								id = 0 ;
		/// 同IDの別原文を拒否するowner
		std::shared_ptr<const Transcript>			source ;
		/// 保存/copy対象の表示版
		std::shared_ptr<const PreparedChatDocument> document ;
		/// menu開始時の取消対象、0は受付なし
		std::uint64_t								operation = 0 ;
		/// worker受付前のlocal待機
		bool										automaticWaiting = false ;
		/// popup開始時の小CAS
		std::optional<ApiAudioResumeToken>			audioResume ;
		/// popup前後の同じ原文
		std::optional<SpeakerTextTarget>			projectionTarget ;
	} ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ行に属する取消対象opを返します
	 * @param[in] card 行
	 * @return 実受付op、取消対象なし0
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t rowOperation( const FileCard &card) const
	{
		// metadata/設定のbusyを前の行へ帰属させません
		if( apiTextWindow_.busy() && apiTextWindow_.directEvent( apiTextWindow_.operationId()) &&
		   apiTextWindow_.historyId() == card.id) {
			return apiTextWindow_.operationId() ;
		}
		// 全文生成と通常ASRの取消しを混同しません
		if( card.wholeText.active) {
			return card.wholeText.active->operationId ;
		}
		// 旧整形も同じ専用operationへ固定します
		if( card.reading.active) {
			return card.reading.active->operationId ;
		}
		return card.busy ? card.operationId : 0 ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 現選択または明示IDの不変対象をcaptureします
	 * @param[in] id 0は現選択
	 * @return 対象snapshot、無効時はID0
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] RowActionTarget rowTarget( std::uint64_t id = 0) const
	{
		// 無選択を先頭行へ補完しません
		if( !id && selected_ >= cards_.size()) {
			return {} ;
		}
		id = id ? id : cards_[selected_].id ;
		const auto found = std::find_if( cards_.begin(), cards_.end(),
			//------------------------------------------------------------------------------
			/**
			 * @brief 同じIDだけを検索します
			 * @param[in] card 候補
			 * @return 一致
			 */
			//------------------------------------------------------------------------------
			[id]( const FileCard &card)
			{
				return card.id == id ;
			}) ; // 最大32件
		// 消えた行を別の選択へ読み替えません
		if( found == cards_.end()) {
			return {} ;
		}
		return {id,
				found->result,
				selected_ < cards_.size() && cards_[selected_].id == id && documentReady() ? document_ : nullptr,
				rowOperation( *found),
				found->automaticText.waiting ||
					(pendingProjectionModel_ && pendingProjectionModel_->target.historyId == id),
				found->apiAudio ? apiAudioResumeToken( *found->apiAudio) : std::nullopt,
				found->apiAudio ? speakerTextTarget( *found->apiAudio) : std::nullopt} ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定対象を現ownerへ再照合します
	 * @param[in] target 固定値
	 * @param[in] document 保存/copy用に同じ表示版も要求するか
	 * @return 同じ有効行index、拒否時cards_.size()
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::size_t resolveRow( const RowActionTarget &target, bool document = false) const
	{
		// close/無効IDは新規操作を作りません
		if( closing_ || !target.id) {
			return cards_.size() ;
		}
		// 件数上限内でID/sourceの両方が同じ行だけを探します
		for( std::size_t index = 0; index < cards_.size(); ++index) {
			const auto &card = cards_[index] ; // 候補行のborrow
			// 表示版を使う操作ではselection/表示ownerも同じ時だけ許可します
			if( card.id == target.id &&
			   ((!card.result && !target.source) || detail::sameTranscriptOwner( card.result, target.source)) &&
			   (!document ||
				(selected_ == index && documentReady() && target.document && document_ == target.document))) {
				return index ;
			}
		}
		return cards_.size() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ行/opだけへ協調取消しを要求します
	 * @param[in] target menu受付値
	 */
	//------------------------------------------------------------------------------
	void cancelRow( const RowActionTarget &target)
	{
		// 同じ行の補助モデル準備だけを取消し対象にする
		if( pendingProjectionModel_ && pendingProjectionModel_->target.historyId == target.id) {
			pendingProjectionModel_.reset() ;
			projectionModelOptions_.reset() ;
			// 自所有のmodel要求がある場合だけ協調停止する
			if( modelOperation_) {
				preparation_->cancel( modelOperation_) ;
			}
			// captureした元行の現在位置
			const auto index = resolveRow( target) ;
			// 準備中だった元行に独立取消しを残す
			if( index < cards_.size() && cards_[index].status.projection) {
				cards_[index].status.projection->phase = ApiTextPhase::CANCELLED ;
				cards_[index].status.projection->observedMs = progressMonotonicMs() ;
			}
			return ;
		}
		const auto index = resolveRow( target) ; // 選択変更後も元ID/sourceだけ
		// 古いmenuの取消しを新しいopへ読み替えません
		if( index >= cards_.size() || rowOperation( cards_[index]) != target.operation) {
			return ;
		}
		auto &card = cards_[index] ; // 同じ所有カード
		// 通常行APIの取消しは区間数/開始を維持します
		if( target.operation && apiTextWindow_.directEvent( target.operation)) {
			apiTextWindow_.cancelCurrent() ;
			// 同じopだけを取消し待ちへ遷移させます
			if( apiClock_.active && apiClock_.operation == target.operation && apiStatus_.api) {
				apiClock_.value.phase = ApiTextPhase::CANCELLING ;
				apiClock_.decorate( *apiStatus_.api) ;
			}
		} else if( target.automaticWaiting && card.automaticText.waiting) {
			// 未受付のlocal待機はworker要求を作らず取消します
			card.automaticText.cancel() ;
		} else if( card.wholeText.active && target.operation) {
			// 原文成功を保ち全文生成だけを止めます
			cancelWholeTextCorrection( card.wholeText) ;
			controller_->cancel( target.operation) ;
			saveHistory() ;
		} else if( card.reading.active && target.operation) {
			// 旧整形もそのoperationだけを止めます
			cancelReading( card.reading) ;
			controller_->cancel( target.operation) ;
			saveHistory() ;
		} else if( card.busy && target.operation) {
			// ASRの所有bufferはworkerの協調終了に委ねます
			diagnosticLog_.append( DiagnosticEvent::CANCEL_REQUESTED, target.operation) ;
			controller_->cancel( target.operation) ;
			card.cancelling = true ;
			const auto audio = card.status.audio ; // 同じrow/opの実区間/開始を保持
			card.status = {IDS_CANCELLING} ;
			if( audio) {
				card.status.audio = audio ;
				card.status.audio->progress.phase = ApiTextPhase::CANCELLING ;
			}
		}
		refreshHistory() ;
		// 別行へ切替済みなら現在行のstatus/本文を維持します
		if( selected_ == index) {
			showCard() ;
			setStatus( selectedStatus()) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 明示再開を同じ行/source/updateへ束縛します
	 * @param[in] target 小CAS付き対象
	 * @param[in] modelsPrepared trueは同じ明示再開の補助モデル準備後の続行です。課金不明の確認と準備要求を重ねず、falseでは必要な確認/準備を先行します
	 */
	//------------------------------------------------------------------------------
	void resumeAudio( const RowActionTarget &target, const bool modelsPrepared = false)
	{
		const auto revision = apiTextWindow_.audioCatalog().keyRevision ; // dialog前のキー版
		const auto eligible =
			//------------------------------------------------------------------------------
			/**
			 * @brief menu後/確認後の同一性を検証します
			 * @return 有効index、拒否はsize
			 */
			//------------------------------------------------------------------------------
			[&]()
			{
				const auto index = resolveRow( target) ; // rename/delete/source差替えを拒否
				// 準備中/保存未確定/処理中の行から新しい有料要求を作りません
				if( index >= cards_.size() || !target.audioResume || !historyReady_ || historyDirty_ || historyFailed_ ||
				   !apiTextWindow_.audioReady() || apiTextWindow_.audioCatalog().keyRevision != revision ||
				   rowOperation( cards_[index]) || !cards_[index].apiAudio ||
				   apiAudioResumeToken( *cards_[index].apiAudio) != target.audioResume) {
					return cards_.size() ;
				}
				return index ;
			} ;
		// 明確未送信には課金不明の確認を要求しません
		if( eligible() >= cards_.size()) {
			return ;
		}
		// 再開の不明課金確認を準備後に重複して出さない
		if( !modelsPrepared && target.audioResume->uncertain &&
		   showPrompt( IDS_AUDIO_RESUME_UNCERTAIN, MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) != IDYES) {
			return ;
		}
		const auto index = eligible() ; // modal中の同じupdate/owner/キー版を再照合
		if( index >= cards_.size()) {
			return ;
		}
		JobRequest job ; // 巨大recordや原文をcopyしない小さい要求
		auto	  &q = job.asr() ; // 同じASR workerの単一受付枠
		q.historyId = target.id ;
		q.input = cards_[index].path ;
		q.options = selectedModelOptions() ; // model_root等のlocal依存だけを使用
		q.options.asrModel = AsrModel::OPEN_AI_AUDIO ;
		q.options.asrProfile = AsrProfile::QUALITY ; // workerが保存plan/元profileへ戻す
		const auto &savedPlan = cards_[index].apiAudio->plan ; // 再開は保存された方式を正とする
		q.options.asrProfile = savedPlan.version == 2 ? AsrProfile::QUALITY : AsrProfile::STANDARD ;
		q.options.autoLanguage = savedPlan.autoLanguage ;
		q.options.diarization = savedPlan.diarization ;
		q.options.speechFilter = savedPlan.speechFilter ;
		// 保存planに従う今回だけの後続条件
		const bool followup = savedPlan.version == 2 && !savedPlan.autoLanguage && savedPlan.diarization ;
		// 音声POSTより前に必要な補助モデルを準備する
		if( followup && !modelsPrepared) {
			pendingProjectionResume_ = target ;
			projectionModelOptions_ = q.options ;
			requestModelCheck( true) ;
			return ;
		}
		// 後続は準備済みの同じmodel rootを使う
		if( followup && modelEvent_.plan) {
			q.options.modelRoot = modelEvent_.plan->effectiveRoot ;
		}
		q.audioApiSelected = true ;
		q.apiKeyRevision = revision ;
		q.audioResume = target.audioResume ;
		const auto projectionRoot = q.options.modelRoot ; // move後も同じ準備済みrootを後続へ保持
		const auto operation = controller_->submit( std::move( job)) ; // 二重/API並行も拒否
		if( !operation) {
			return ;
		}
		auto &card = cards_[index] ; // 同じ行の取消対象だけ新opへ更新
		card.operationId = operation ;
		card.busy = true ;
		card.cancelling = false ;
		card.audioApiRequested = true ;
		card.audioResumeRequested = true ;
		card.projectionFollowup = followup ;
		card.projectionModelRoot = projectionRoot ;
		card.speakerText = {} ;
		card.projectionActive = false ;
		ApiAudioStatusView view ; // 旧失敗HTTPを今回へ流用しません
		view.progress.phase = ApiTextPhase::PREPARING ;
		view.progress.completedUnits = target.audioResume->completePrefix ;
		view.progress.totalUnits = static_cast<std::uint32_t>( card.apiAudio->plan.windows.size()) ;
		view.progress.startedMs = view.progress.observedMs = progressMonotonicMs() ;
		card.status = {IDS_AUDIO_PREPARING} ;
		card.status.audio = view ;
		refreshHistory() ;
		// 別行閲覧中は文書/scroll/statusを変更しません
		if( selected_ == index) {
			setStatus( card.status) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ耐久原文と小targetを照合し、明示PC内の話者投影要求を単一workerへ一度受け付けます
	 * @param[in] q 準備済みmodel rootと対象history/source/updateを値で所有する要求。受付時にpayloadへ移譲し、発行opを対応する行へ反映します
	 * @note close/未準備/stale/破損/処理中の対象やqueue拒否では開始しません。受理時だけ行を処理中にし、API送信をこの関数からは行いません
	 */
	//------------------------------------------------------------------------------
	void submitProjection( SpeakerTextJobRequest q)
	{
		// 対象IDまたは表示名を持つ既存owner
		auto found = std::find_if( cards_.begin(), cards_.end(),
			//------------------------------------------------------------------------------
			/**
			 * @brief 明示要求の対象IDだけを選びます
			 * @param[in] c 現在行
			 * @return 同じ対象ならtrue
			 */
			//------------------------------------------------------------------------------
			[&q]( const auto &c)
			{
				return c.id == q.target.historyId ;
			}) ; // 同じ行
		// close/stale/破損派生fileへ新しい投影を開始しない
		if( closing_ || !historyReady_ || historyFailed_ || speakerTextCorrupt_ || found == cards_.end() ||
		   found->busy || !found->apiAudio || speakerTextTarget( *found->apiAudio) != q.target) {
			return ;
		}
		// workerへ所有移譲する今回の型付き要求
		JobRequest request ;
		request.payload = std::move( q) ; // 本文なしの小要求
		// global floorから受付時に発行された別op
		const auto		operation = controller_->submit( std::move( request)) ;
		// 音声APIとは独立した補助用途の実進捗
		ApiTextProgress progress ;
		progress.phase = operation ? ApiTextPhase::PREPARING : ApiTextPhase::FAILURE ;
		progress.startedMs = progress.observedMs = progressMonotonicMs() ;
		progress.totalUnits = apiAudioCompletePrefix( *found->apiAudio) ;
		found->status.projection = progress ;
		// 受付成功時だけ元行へ新opを束縛する
		if( operation) {
			found->projectionActive = true ;
			found->busy = true ;
			found->operationId = operation ;
			found->cancelling = false ;
		}
		// 閲覧中の別行へ処理状態を表示しない
		if( selected_ < cards_.size() && cards_[selected_].id == found->id) {
			setStatus( found->status) ;
		}
		refreshHistory() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 明示行操作だけに補助モデルを準備します
	 * @param[in] target popupで捕捉した同じ行
	 */
	//------------------------------------------------------------------------------
	void prepareProjection( const RowActionTarget &target)
	{
		const auto index = resolveRow( target) ; // 最初のmenu後のsource ownerを照合
		// menu前後で元行と小targetが変わった要求を拒否する
		if( index >= cards_.size() || !target.projectionTarget || !cards_[index].apiAudio ||
		   speakerTextTarget( *cards_[index].apiAudio) != target.projectionTarget || rowOperation( cards_[index]) ||
		   !historyReady_ || historyDirty_ || historyFailed_ || speakerTextCorrupt_ || pendingProjectionModel_ ||
		   pendingProjectionResume_) {
			return ;
		}
		// 本文をcopyしない小target付き投影要求
		SpeakerTextJobRequest q ;
		q.target = *target.projectionTarget ; // 原文をUIでcopy/hashしない
		// 現固定資産に束縛された参照は再推論せず使う
		if( cards_[index].speakerText.adopted && cards_[index].speakerText.adopted->target == q.target &&
		   speakerTextReferenceCurrent( *cards_[index].speakerText.adopted)) {
			submitProjection( std::move( q)) ;
			return ; // 検証済みcacheはworkerで来歴/SHA照合後に再利用
		}
		pendingProjectionModel_ = q ;
		// 通常Quality CPU参照だけに固定する補助設定
		EngineOptions options ;
		options.asrModel = AsrModel::PARAKEET ;
		options.asrProfile = AsrProfile::QUALITY ;
		options.backend = Backend::CPU ;
		options.cpuFallback = false ;
		options.diarization = false ;
		options.speechFilter = true ;
		options.autoLanguage = false ;
		projectionModelOptions_ = options ;
		// 音声APIとは独立した補助用途の実進捗
		ApiTextProgress progress ;
		progress.phase = ApiTextPhase::PREPARING ;
		progress.startedMs = progress.observedMs = progressMonotonicMs() ;
		cards_[index].status.projection = progress ;
		// 準備状態は現在見ている同じ行だけへ反映する
		if( selected_ == index) {
			setStatus( cards_[index].status) ;
		}
		requestModelCheck( true) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 明示話者付きTXTだけを既存atomic exportへ渡します
	 * @param[in] path 利用者選択先
	 */
	//------------------------------------------------------------------------------
	void queueProjectionSave( const std::filesystem::path &path)
	{
		// 明示TXTは完成した同source投影だけを受付ける
		if( selected_ >= cards_.size() || !documentReady() || !cards_[selected_].speakerText.adopted ||
		   !cards_[selected_].apiAudio) {
			return ;
		}
		const auto		&card = cards_[selected_] ; // 現表示名のowner
		ExportJobRequest save ;
		save.output = path ;
		save.source = card.result ;
		save.sourceKey = document_->view->key() ;
		save.speakerText = card.speakerText.adopted ;
		save.speakerAudio = card.apiAudio ;
		save.format = ExportFormat::PLAIN_TEXT ;
		// 既存単一workerへ渡す所有DTO
		JobRequest job ;
		job.payload = std::move( save) ;
		// 受付済みexportの操作番号
		const auto op = controller_->submit( std::move( job)) ;
		// 受理したexportだけを保存完了の追跡へ加える
		if( op) {
			saves_.emplace( op, card.id) ;
		}
	}
	void invokeRowAction( int id, const RowActionTarget &target)
	{
		const auto index = resolveRow( target) ; // 追跡中に消えた行/sourceを拒否
		// popup取消しや無効対象は別行へ適用しません
		if( !id || index >= cards_.size()) {
			return ;
		}
		// 各操作を同じcaptureへ固定し、現在選択からIDを作り直しません
		switch( id) {
			case IDC_SAVE: // 保存dialog後も同じ表示版を再照合します
			case IDC_COPY: // 全文copyは表示filterとは独立した同版です
				// 別選択や本文版の変更を古い操作へ適用しません
				if( resolveRow( target, true) < cards_.size()) {
					id == IDC_SAVE ? chooseSave() : copyResult() ;
				}
				break ;
			case IDC_CANCEL: // menu開始時の同じopだけを取消します
				cancelRow( target) ;
				break ;
			case IDC_AUDIO_RESUME: // 明示行の保存planを使います
				resumeAudio( target) ;
				break ;
			case IDC_SPEAKER_TEXT: // 認証/価格更新なしの明示PC内処理
				prepareProjection( target) ;
				break ;
			case IDC_SPEAKER_TEXT_SAVE: // 通常TXT/copyとは別の明示label付き保存
				// dialogを開く前にも同じ行の表示版を照合する
				if( resolveRow( target, true) < cards_.size()) {
					chooseSave( false, true) ;
				}
				break ;
			case IDC_RETRANSCRIBE: // 既存受付の上限/準備条件を再確認します
				retranscribe( target.id) ;
				break ;
			case IDC_API_TEXT_OPEN: // 選択行全文の一回だけの明示要求
				openApi( target.id, false) ;
				break ;
			case IDC_HISTORY_REMOVE: // 原音声を保持し履歴だけを削除
				removeHistory( target.id) ;
				break ;
			case IDC_RENAME: // sourceを小dialogへ固定します
				showSpeakerRename( target) ;
				break ;
			case IDC_API_RETRY: { // 保存だけを再試行しHTTPを増やしません
				const auto &card = cards_[index] ; // 同じ固定行
				// 元の保存失敗が解消済みなら再操作しません
				if( card.apiText.saveFailed) {
					ApiTextCorrectionJobRequest retry ; // 同じsourceと候補
					retry.source = target.source ;
					retry.key.historyId = target.id ;
					retry.record = card.apiText ;
					static_cast<void>( apiTextWindow_.direct( std::move( retry), ApiTextAction::RETRY_SAVE)) ;
				}
				break ;
			}
			default:
				break ; // 未知commandは拒否します
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定metadataだけを通常sinkへ渡します
	 * @param[in] record 自由文なしの小値
	 */
	//------------------------------------------------------------------------------
	void logLocal( const LocalDiagnosticRecord record) noexcept
	{
		// 旧内部試験や無効sinkでは通常領域へfallbackしない
		if( localDiagnostics_) {
			localDiagnostics_->append( record) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 全設定を一つのtab画面へ開きます
	 */
	//------------------------------------------------------------------------------
	void showSettings()
	{
		auto record = localConfiguration( selectedModelOptions()) ; // 固定選択値だけ
		record.event = LocalDiagnosticEvent::SETTINGS_OPENED ;
		record.reason = LocalDiagnosticReason::SETTINGS_MENU ;
		logLocal( record) ;
		// 復元失敗の原fileを既定値で上書きせず、保存中は新sessionを開始しません
		if( !transcriptionSettingsReady_ || transcriptionSettingsOperation_) {
			showMessage( transcriptionSettingsFailed_	  ? transcriptionSettingsLoadNotice_
						: transcriptionSettingsOperation_ ? IDS_TRANSCRIPTION_SAVING
														  : IDS_TRANSCRIPTION_LOADING) ;
			return ;
		}
		auto draft = selectedModelOptions() ; // 制約の間も次の系統へ戻す明示選択を保持
		draft.backend = preferredBackend_ == 1 ? Backend::CPU : Backend::AUTO ;
		draft.autoLanguage = multilingualAutoLanguage_ ;
		settingsWindow_.show(
			window_, draft, uiLanguage(),
			//------------------------------------------------------------------------------
			/**
			 * @brief 選好の保存を一回受け付け、ACK前は以前の次job設定を保ちます
			 * @param[in] options 未適用snapshot
			 * @param[in] language 選択UI言語
			 */
			//------------------------------------------------------------------------------
			[this]( const EngineOptions &options, UiLanguage language)
			{
				auto preferences = transcriptionPreferences( options) ; // 同じApply snapshot
				preferences.checkUpdates = settingsWindow_.checkUpdates() ;
				const auto operation = transcriptionSettings_->save( preferences) ; // 小DTOだけ
				// 受付拒否を正常Applyへ変えません
				if( !operation) {
					throw OperationError( ErrorKind::SAVE_FAILED, ERROR_BUSY) ;
				}
				transcriptionSettingsOperation_ = operation ;
				transcriptionSettingsSession_ = settingsWindow_.session() ;
				transcriptionSettingsLanguage_ = language ;
				transcriptionSettingsSubmitted_ = preferences ;
			},
			//------------------------------------------------------------------------------
			/**
			 * @brief 適用済み構成の既存準備だけを開きます
			 * @param[in] id command
			 */
			//------------------------------------------------------------------------------
			[this]( int id)
			{
				command( id, 0) ;
			},
			true, checkUpdates_) ;
		// 設定close後に届いた失敗は、一度だけ同じ草稿へ戻して明示retryを可能にします
		if( transcriptionSettingsFailedDraft_) {
			transcriptionSettingsFailedDraft_ = false ;
			settingsWindow_.restoreLocalDraft( transcriptionOptions( transcriptionSettingsSubmitted_),
											  transcriptionSettingsLanguage_,
											  transcriptionSettingsSubmitted_.checkUpdates) ;
		}
		lockModelControls() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 依存fileの解決に必要な現在設定だけを所有copyします
	 * @return 固定設定値
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] EngineOptions selectedModelOptions() const
	{
		EngineOptions options ; // UI threadの選択から作る準備用snapshot
		options.asrModel = asrModel_ ;
		options.asrProfile = asrProfile_ ;
		options.speechFilter = speechFilter_ ;
		options.diarization = diarizationEnabled_ ;
		options.expectedSpeakers = expectedSpeakers_ ;
		options.backend = cpuOnlyAsr_ || preferredBackend_ == 1 ? Backend::CPU : Backend::AUTO ;
		options.autoLanguage = false ; // 新規投入/再認識は日本語専用。旧再開は保存planを復元
		options.speakerMethod = speakerMethod_ ;
		return options ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 依存fileを変える設定だけを比較します
	 * @param[in] left 一つ目の構成
	 * @param[in] right 二つ目の構成
	 * @return 同じモデル依存ならtrue
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] static bool sameModelOptions( const EngineOptions &left, const EngineOptions &right) noexcept
	{
		return left.asrModel == right.asrModel && left.asrProfile == right.asrProfile &&
			   left.speechFilter == right.speechFilter && left.diarization == right.diarization &&
			   left.autoLanguage == right.autoLanguage ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 条件表示と準備状態を現在の所有通知から更新します
	 */
	//------------------------------------------------------------------------------
	void refreshModelPreparation()
	{
		preparationWindow_.update( projectionModelOptions_ ? *projectionModelOptions_ : selectedModelOptions(),
								  modelEvent_, modelOperation_ != 0, projectionModelOptions_.has_value()) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 結果閲覧を止めずに準備画面を表示します
	 * @param[in] reason 表示を要求した入口。通信や取得の意図とは別
	 */
	//------------------------------------------------------------------------------
	void showModelPreparation( const LocalDiagnosticReason reason = LocalDiagnosticReason::MISSING_FOR_JOB)
	{
		auto record = localConfiguration( selectedModelOptions()) ; // 表示だけの固定metadata
		record.event = LocalDiagnosticEvent::PREPARATION_OPENED ;
		record.reason = reason ;
		record.operation = modelOperation_ ;
		logLocal( record) ;
#if defined(KOEFUDE_WINDOW_TESTING)
        ++testHooks_.notices ;
		testHooks_.lastNotice = IDS_PREP_TITLE ;
#else
        preparationWindow_.show( window_) ;
		refreshModelPreparation() ;
#endif
    }
	//------------------------------------------------------------------------------
	/**
	 * @brief 選択変更を最新一つへ集約し、通信なしの検査を開始します
	 * @param[in] force 同じ構成の失敗・取消し後も再検査する場合true
	 * @param[in] reason 今回の検査を要求した入口。取得の許可を兼ねません
	 */
	//------------------------------------------------------------------------------
	void requestModelCheck( const bool force = false, LocalDiagnosticReason reason = LocalDiagnosticReason::REFRESH)
	{
		// 初期化途中と終了中は新しいworker要求を増やさない
		if( !preparation_ || !transcriptionSettingsReady_ || closing_ || modelDownloadActive_) {
			return ;
		}
		const auto options = projectionModelOptions_ ? *projectionModelOptions_
													 : selectedModelOptions() ; // 補助準備でも適用設定を変更しない
		// 言語再表示等では同じモデルのhashを不要に読み直さない
		if( !force && !modelCheckPending_ && sameModelOptions( options, modelOptions_) &&
		   (modelOperation_ != 0 || modelEvent_.terminal)) {
			return ;
		}
		modelReady_ = false ;
		modelEvent_ = {} ;
		// 待機検査の集約で起動/Apply等の元理由を再表示へ置き換えない
		if( reason == LocalDiagnosticReason::REFRESH && modelCheckPending_) {
			reason = modelCheckReason_ ;
		}
		modelCheckReason_ = reason ;
		// 古い検査の終端を受領してから最新の構成だけを検査する
		if( modelOperation_ != 0) {
			modelCheckPending_ = true ;
			preparation_->cancel( modelOperation_) ;
			refreshModelPreparation() ;
			return ;
		}
		modelOptions_ = options ;
		modelCheckPending_ = false ;
		ModelPreparationRequest request ; // UIのpathは値としてworkerへ渡す
		request.options = options ;
		request.reason = reason ;
		request.bundleRoot = diagnosticModelReference() ;
		request.cacheRoot = diagnosticModelCache() ;
		modelOperation_ = preparation_->submit( std::move( request)) ;
		modelEvent_.id = modelOperation_ ;
		refreshModelPreparation() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 取得中だけ依存構成を固定し、結果の操作は継続できるようにします
	 */
	//------------------------------------------------------------------------------
	void lockModelControls()
	{
		settingsWindow_.lock( !transcriptionSettingsReady_ || modelDownloadActive_ ||
							 wholeTextPreparationWindow_.busy()) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 表示済み構成の明示取得を開始します
	 */
	//------------------------------------------------------------------------------
	void startModelDownload()
	{
		// 古い操作messageや検査未完了では通信を開始しない
		if( modelOperation_ != 0 || !modelEvent_.plan || modelEvent_.plan->ready ||
		   !sameModelOptions( modelOptions_,
							 projectionModelOptions_ ? *projectionModelOptions_ : selectedModelOptions())) {
			return ;
		}
		ModelPreparationRequest request ; // 条件表示に対応する所有要求
		request.options = modelOptions_ ;
		request.bundleRoot = diagnosticModelReference() ;
		request.cacheRoot = diagnosticModelCache() ;
		request.cacheRoot = modelEvent_.plan->cacheRoot ;
		request.download = true ; // 利用者が取得buttonを押した経路だけで許可する
		request.reason = LocalDiagnosticReason::EXPLICIT_DOWNLOAD ; // 受信commandの事実、物理clickの証明ではない
		modelOperation_ = preparation_->submit( std::move( request)) ;
		modelDownloadActive_ = modelOperation_ != 0 ;
		// 明示取得の受付だけを記録し、通信先や資格情報は書かない
		if( modelDownloadActive_) {
			diagnosticLog_.append( DiagnosticEvent::MODEL_DOWNLOAD_STARTED, modelOperation_) ;
		}
		modelEvent_.id = modelOperation_ ;
		modelEvent_.terminal = false ;
		modelEvent_.progress = {} ;
		lockModelControls() ;
		refreshModelPreparation() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 準備通知を現在の操作番号と構成へ照合して受領します
	 */
	//------------------------------------------------------------------------------
	void collectModelEvent()
	{
		// WM_CREATEの途中では準備workerがまだ存在しない
		if( !preparation_) {
			return ;
		}
		const auto event = preparation_->takeEvent() ; // 最大一件の所有通知
		// 古い・未知の操作を現在構成のReadyへ適用しない
		if( !event || event->id != modelOperation_) {
			return ;
		}
		// この同期境界で所有する値、または確定状態の検証値です
		const bool current =
			!modelCheckPending_ &&
			sameModelOptions( modelOptions_, projectionModelOptions_ ? *projectionModelOptions_
																	: selectedModelOptions()) ; // 同じ準備意図
		// 現在の構成だけで状態を表示し、進捗更新では条件欄の計画を保持する
		if( current) {
			const auto plan = modelEvent_.plan ; // 取得中も不足容量と条件を表示する所有参照
			modelEvent_ = *event ;
			// 進捗・失敗・取消しに新計画がない場合は表示済み条件を残す
			if( !modelEvent_.plan) {
				modelEvent_.plan = plan ;
			}
			modelReady_ = event->terminal && event->plan && event->plan->ready ;
		}
		// 終端を受領してから設定を解放し、必要なら最新一構成の検査へ進む
		if( event->terminal) {
			diagnosticLog_.append( DiagnosticEvent::MODEL_CHECKED, event->id, current && modelReady_ ? 1 : 0,
								  static_cast<std::uint64_t>( event->progress.stage), event->code) ;
			modelOperation_ = 0 ;
			modelDownloadActive_ = false ;
			// 明示準備の一回意図だけを消費し、起動/Apply/再読からは補助認識を作らない
			if( current && modelReady_ && !closing_ && pendingProjectionModel_) {
				// 本文をcopyしない小target付き投影要求
				auto q = std::move( *pendingProjectionModel_) ;
				pendingProjectionModel_.reset() ;
				q.modelRoot = modelEvent_.plan->effectiveRoot ;
				projectionModelOptions_.reset() ;
				submitProjection( std::move( q)) ;
				requestModelCheck( true) ;
			}
			// 準備完了後は保持した明示再開要求だけを再評価する
			if( current && modelReady_ && !closing_ && pendingProjectionResume_) {
				// 元行/source/保存版に束縛した小CAS
				const auto target = *pendingProjectionResume_ ;
				pendingProjectionResume_.reset() ;
				// model準備待機後の現在行token
				const auto fresh = rowTarget( target.id) ;
				// model待機中に変わった音声recordへ旧要求を流用しない
				if( fresh.audioResume == target.audioResume) {
					resumeAudio( fresh, true) ;
				}
				projectionModelOptions_.reset() ;
				requestModelCheck( true) ;
			}
			refreshAsrControls() ;
			// 準備不足・検査失敗は認識とは別の画面に案内する
			if( current && !closing_ &&
			   (event->progress.stage == ModelPreparationStage::MISSING ||
				event->progress.stage == ModelPreparationStage::FAILURE)) {
				showModelPreparation( LocalDiagnosticReason::MISSING_AFTER_CHECK) ;
			}
			// 起動引数の明示音声は履歴とモデルの両方が揃ってから一度だけ受け付ける
			if( modelReady_ && historyReady_ && !closing_ && !initialInput_.empty()) {
				const auto input = std::exchange( initialInput_, {}) ; // 一度だけ使う所有path
				addFile( input) ;
			}
		}
		refreshModelPreparation() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 明示modeだけ専用書込み先を返します
	 * @return state/models、通常は空
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::filesystem::path diagnosticModelCache() const
	{
		return diagnosticLog_.currentMode() ? historyRoot_.parent_path() / L"models" : std::filesystem::path{} ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 明示modeだけ既存cacheを読取り参照します
	 * @return 通常bundleまたは検証済みreference
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::filesystem::path diagnosticModelReference() const
	{
		// 通常起動のモデルrootは従来どおりです
		if( !diagnosticLog_.currentMode()) {
			return modelRoot_ ;
		}
#if defined(KOEFUDE_WINDOW_TESTING)
        return modelRoot_ ; // 専用fake以外の実cacheを開きません
#else
        return defaultModelCache() ; // 既存のlocal検証、書込みなし
#endif
    }
	//------------------------------------------------------------------------------
	/**
	 * @brief UI通知先を保つ推論workerを作成します。終了取消し後の再開にも使います
	 */
	//------------------------------------------------------------------------------
	void startJobController()
	{
		WholeTextApplicationConfig config ; // lambdaが値で所有する有限root
		config.nativeModule = modelRoot_.parent_path() / L"KoeFude.WholeText.dll" ;
		config.avx2NativeModule = modelRoot_.parent_path() / L"KoeFude.WholeText.Avx2NoRepack.dll" ;
		config.ministral16gbNativeModule =
			modelRoot_.parent_path() /
			wholeTextRuntimeDescriptor( WholeTextRuntimeVariant::MINISTRAL14_B_AVX2_NO_REPACK16_GB).dllName ;
		config.bundleRoot = diagnosticModelReference() ;
		config.persistence = history_ ? history_->wholeTextPort() : nullptr ;
		config.diagnosticReferenceOnly = diagnosticLog_.currentMode() ;
#if defined(KOEFUDE_WINDOW_TESTING)
        config.cacheRoot = diagnosticLog_.currentMode() ? diagnosticModelCache() : modelRoot_ ; // fake専用root
#else
        config.cacheRoot = diagnosticModelCache().empty() ? defaultModelCache() : diagnosticModelCache() ;
#endif
		auto apiKeys = std::shared_ptr<IApiKeyStore>( makeApiKeyStore()) ; // I/Oはworkerに限定
		auto apiHttp = std::shared_ptr<IApiHttpTransport>( makeApiTextHttp()) ; // fixed provider
		auto apiCatalogHttp = std::shared_ptr<IApiCatalogTransport>( makeApiCatalogHttp()) ; // metadata GET専用
		auto apiCatalog = std::make_shared<ApiModelCatalog>() ; // 同workerだけが変更
		auto audioPort = history_->apiAudioPort() ; // 通常と同じ一履歴writer
		auto projectionPort = history_->speakerTextPort() ; // API/keyから独立した同writer
		auto apiPort = history_->apiTextPort() ; // 同じ履歴worker/lock
		auto apiLog = std::shared_ptr<IApiCorrectionEventLog>(
			makeLocalDiagnosticApiLog( makeApiCorrectionEventLog( historyRoot_),
									  localDiagnostics_)) ; // 旧sinkの返値を維持
		JobRunner runner =
			//------------------------------------------------------------------------------
			/**
			 * @brief HWNDを捕捉せず単一job worker上で要求variantを対応する同期coreへ振り分けます
			 * @param[in] request 呼出し中だけ借用する不変JobRequest。payloadは話者投影、metadata、文章改善、選択済み音声API、その他の通常jobを区別します
			 *        共有原文ownerは必要な応答へ保持します
			 * @param[in] stop JobControllerが渡す当operationの協調停止token。各coreへ渡し、実処理/保存barrierの停止契約を維持します
			 * @param[in] progress worker上で同期通知する実段階/進捗callback。通知値の借用やengine資源をUIへ保持させません
			 * @return variantに対応する原文/文章改善/metadata/話者投影の所有JobOutput。部分原文・固定診断・保存ACKも各coreの結果として保持します
			 * @note configとport/providerの共有ownerを値captureし、request/progressの参照は呼出し後に保持しません
			 *        coreの例外はJobControllerのworker境界へ伝えます
			 */
			//------------------------------------------------------------------------------
			[config, apiKeys, apiHttp, apiPort, apiCatalogHttp, apiCatalog, apiLog, audioPort,
			 projectionPort]( const JobRequest &request, std::stop_token stop, const ProgressCallback &progress)
			{
				// 投影要求は音声/文章APIから独立したworker入口へ渡す
				if( const auto *q = std::get_if<SpeakerTextJobRequest>( &request.payload)) {
					NativeTextAlignmentReferenceProducer producer ; // 今回一worker、一context
					SpeakerTextJobCompletion			 completion ; // 監視終了後だけ返す
					detail::runPrimaryResourceTask(
						//------------------------------------------------------------------------------
						/**
						 * @brief 通常available/commit監視へ補助処理を接続します
						 * @param[in,out] resources 今回のworker専用control
						 */
						//------------------------------------------------------------------------------
						[&]( IPipelineResourceControl &resources)
						{
							completion = runSpeakerTextJob( *q, *projectionPort, producer, decodeInputAudio, stop, progress,
														   &resources) ;
						},
						stop) ;
					// 原文を上書きしない型付きworker終端
					JobOutput output ;
					output.speakerText = std::make_shared<const SpeakerTextJobCompletion>( std::move( completion)) ;
					return output ;
				}
				// 本文なしmetadataは明示画面操作の専用要求でだけ取得します
				if( const auto *catalog = std::get_if<ApiCatalogJobRequest>( &request.payload)) {
					auto	  result = runApiCatalogJob( *catalog, *apiCatalog, *apiKeys, *apiCatalogHttp, *apiPort, stop) ;
					JobOutput output ; // Transcript/pathなし
					output.apiCatalog = std::make_shared<const ApiCatalogJobCompletion>( std::move( result)) ;
					return output ;
				}
				// API専用DTOはnative音声経路へ渡しません
				if( const auto *api = std::get_if<ApiTextCorrectionJobRequest>( &request.payload)) {
					auto result =
						runApiTextJobLogged( *api, *apiKeys, *apiHttp, *apiPort, stop, progress, apiCatalog.get(), *apiLog,
											apiCatalogHttp.get()) ; // 同じ単一worker
					// 大きいUTF-8/UTF-16変換もUIでは行いません
					if( result.draft) {
						result.preview = toWide( result.draft->projection) ;
					}
					const auto &record =
						result.persistence.operationId ? result.persistence.record : api->record ; // 所有結果
					if( record.latest) {
						result.candidate = toWide( record.latest->candidate) ;
					}
					JobOutput output( api->source) ; // 同じsource control block
					output.apiText = std::make_shared<const ApiTextJobCompletion>( std::move( result)) ;
					return output ;
				}
				// 単一registryのAPIだけを専用同期providerへ接続しlocal ASRを呼びません
				if( const auto *asr = std::get_if<AsrJobRequest>( &request.payload) ;
				   asr && (asr->audioResume || asrDefinition( asr->options.asrModel, asr->options.asrProfile).engine ==
												   AsrEngine::OPEN_AI_TRANSCRIPTIONS)) {
					auto apiAudioHttp = makeApiAudioHttp() ; // 一jobだけのworker owner
					auto transcriber = makeApiAudioTranscriber( *apiAudioHttp) ; // 同scopeのborrow
					return runApiAudioJob( *asr, *apiKeys, *apiCatalog, *apiCatalogHttp, *apiPort, *transcriber, *audioPort,
										  stop, progress, nullptr, apiLog.get()) ;
				}
				return executeJob( request, stop, progress, config) ;
			} ;
#if defined(KOEFUDE_WINDOW_TESTING)
        runner = testHooks_.jobRunner ;
#endif
        controller_ = std::make_unique<JobController>(
			std::move( runner),
			[target = window_]
			{
				// pointerを運ばず、失敗時はUI timerで所有eventを回収する
				PostMessageW( target, K_WAKE_MESSAGE, 0, 0) ;
			},
			localDiagnostics_) ;
		// 再起動したworkerでも既知operationを再利用しません
		if( !controller_->seedNextId( nextOperationId_)) {
			throw HistoryError( HistoryErrorKind::CORRUPT) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief モデルworkerを作り、専用試験だけは実fileを開かない依存へ差し替えます
	 */
	//------------------------------------------------------------------------------
	void startModelController()
	{
		ModelPreparationRunner runner = runModelPreparation ; // 通常製品の検査・取得処理
		// 明示試験だけreferenceのMissing限定規則をworker内へ適用します
		if( diagnosticLog_.currentMode()) {
			runner =
				//------------------------------------------------------------------------------
				/**
				 * @brief 補正診断用モデルの準備要求を、明示取得またはローカル検査へ振り分けます
				 * @param[in] request worker呼出し中だけ借用する構成・同梱参照先・cache先と明示取得指定です
				 * @param[in] stop 取得または検査へそのまま渡す協調停止tokenです
				 * @param[in] progress 明示取得中の段階を同期通知するcallbackのborrowです
				 * @return 取得時はcacheを取得元/取得先とした準備計画、検査時は同梱参照先とcacheを照合した計画です。Readyの事実を各処理から引き継ぎます
				 */
				//------------------------------------------------------------------------------
				[]( const ModelPreparationRequest &request, std::stop_token stop,
				   const ModelPreparationCallback &progress)
				{
					const auto &cache = request.cacheRoot ; // 必須の所有state/models
					// 明示取得もreferenceへtransaction/lock/copyを行いません
					if( request.download) {
						WinHttpModelTransport transport ; /* 既存通信 */
						return downloadModels( request.options, cache, cache, transport, stop, progress) ;
					}
					LocalModelInspector inspector ; // workerだけの読取り検査
					return planCorrectionDiagnosticModels( request.options, request.bundleRoot, cache, inspector, stop) ;
				} ;
		}

#if defined(KOEFUDE_WINDOW_TESTING)
        runner = testHooks_.modelRunner ;
#endif
        preparation_ = std::make_unique<ModelPreparationController>( std::move( runner), localDiagnostics_) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 推論なしの表示準備workerを開始します
	 */
	//------------------------------------------------------------------------------
	void startDocumentController()
	{
		ChatDocumentRunner runner = prepareChatDocument ; // 通常版の純粋準備器
#if defined(KOEFUDE_WINDOW_TESTING)
        // 専用試験だけに遅延/失敗を差し替え、通常EXEへ入口を出さない
		if( testHooks_.documentRunner) {
			runner = testHooks_.documentRunner ;
		}
#endif
        documentOperation_ = 0 ;
		documentRequest_.reset() ;
		document_.reset() ;
		documents_ = std::make_unique<ChatDocumentController>( std::move( runner),
				//------------------------------------------------------------------------------
				/**
				 * @brief 所有文書を渡さず自分のアプリへ起床を通知します
				 */
				//------------------------------------------------------------------------------
				[target = window_]
				{
					PostMessageW( target, K_WAKE_MESSAGE, 0, 0) ;
				}) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 選好の復元だけを専用workerで開始し、モデル/通信を開始しません
	 */
	//------------------------------------------------------------------------------
	void startTranscriptionSettings()
	{
		auto storage = makeFileTranscriptionSettingsStorage( historyRoot_.empty() ? std::filesystem::path{}
																				 : historyRoot_.parent_path() /
																					   L"settings") ; // 明示modeも隔離
#if defined(KOEFUDE_WINDOW_TESTING)
        // 遅延/失敗caseは本人領域へ到達できないfakeだけへ置き換えます
		if( testHooks_.transcriptionSettingsStorage) {
			storage = testHooks_.transcriptionSettingsStorage() ;
		}
#endif
        transcriptionSettings_ =
			std::make_unique<TranscriptionSettingsController>( std::move( storage),
				//------------------------------------------------------------------------------
				/**
				 * @brief 値/所有pointerを渡さず自Windowを起床します
				 */
				//------------------------------------------------------------------------------
				[target = window_]
				{
					PostMessageW( target, K_WAKE_MESSAGE, 0, 0) ;
				}) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 正常復元/保存値だけを次job選好へ確定します
	 * @param[in] value 検証済み値
	 * @param[in] reason 起動復元または明示Apply
	 */
	//------------------------------------------------------------------------------
	void commitTranscriptionPreferences( const TranscriptionPreferences &value, const LocalDiagnosticReason reason)
	{
		checkUpdates_ = value.checkUpdates ;
		asrModel_ = value.model ;
		asrProfile_ = value.profile ;
		speakerMethod_ = value.method ;
		speechFilter_ = value.speechFilter ;
		diarizationEnabled_ = value.diarization ;
		expectedSpeakers_ = value.expectedSpeakers ;
		preferredBackend_ = value.backend == Backend::CPU ? 1 : 0 ;
		multilingualAutoLanguage_ = value.autoLanguage ;
		refreshAsrControls( reason) ; // 復元/耐久ACK後の構成だけで初回/変更check
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 小ACKを回収し、load/同じ保存IDだけを確定します
	 */
	//------------------------------------------------------------------------------
	void collectTranscriptionSettings()
	{
		// 初期化途中はstorageへ触れず、所有workerの存在だけを確認します
		if( !transcriptionSettings_) {
			return ;
		}
		const auto event = transcriptionSettings_->takeEvent() ; // 最大一ACK
		// 起床のみ/古い保存通知から選好を変更しません
		if( !event || (event->operation && event->operation != transcriptionSettingsOperation_)) {
			return ;
		}
		auto record = localConfiguration( transcriptionOptions( event->value)) ; // 自由文なし選好
		record.event =
			event->operation ? LocalDiagnosticEvent::SETTINGS_APPLIED : LocalDiagnosticEvent::SETTINGS_RESTORED ;
		record.reason = event->operation ? LocalDiagnosticReason::SETTINGS_APPLY : LocalDiagnosticReason::STARTUP ;
		record.operation = event->operation ;
		record.outcome =
			event->succeeded ? LocalDiagnosticOutcome::COMPLETED : LocalDiagnosticOutcome::DIAGNOSTIC_ERROR ;
		record.code = event->code ;
		// 失敗時の既定DTOを、実際に復元/適用した選択と呼ばない
		if( !event->succeeded) {
			record.model.reset() ;
			record.profile.reset() ;
			record.requestedBackend.reset() ;
		}
		logLocal( record) ;
		// loadは一度だけ採用し、失敗時には受付/Applyを止めたまま案内します
		if( !event->operation) {
			// 稀なworker同期基盤失敗も未完保存として終端化し、終了待ちを残しません
			if( !event->succeeded && transcriptionSettingsOperation_) {
				transcriptionSettingsOperation_ = 0 ;
				settingsWindow_.completeLocal( transcriptionSettingsSession_, false, event->code) ;
				transcriptionSettingsCloseFailed_ = closing_ && transcriptionSettingsCloseWaiting_ ;
			}
			transcriptionSettingsReady_ = event->succeeded ;
			transcriptionSettingsFailed_ = !event->succeeded ;
			transcriptionSettingsLoadNotice_ =
				event->error == HistoryErrorKind::BUSY ? IDS_TRANSCRIPTION_LOAD_BUSY
				: event->error == HistoryErrorKind::CORRUPT || event->error == HistoryErrorKind::VERSION
					? IDS_TRANSCRIPTION_LOAD_FAILED
					: IDS_TRANSCRIPTION_LOAD_IO ;
			// 成功値だけを使い、破損を既定値へ読み替えません
			if( event->succeeded) {
				commitTranscriptionPreferences( event->value, LocalDiagnosticReason::STARTUP) ;
				// 保存済み選好の復元後に起動一回だけ確認し、通常起動を待たせません
				if( !startupUpdateAttempted_) {
					startupUpdateAttempted_ = true ;
					// OFF/試験無効seamではGET0です。Applyや再読で追加確認しません
					if( checkUpdates_ && updater_ && !closing_) {
						updateOperation_ = updater_->check() ;
						updateEvent_.state = UpdateState::CHECKING ;
					}
				}
			} else if( !closing_) {
				showMessage( transcriptionSettingsLoadNotice_) ;
			}
			lockModelControls() ;
			return ;
		}
		transcriptionSettingsOperation_ = 0 ;
		// 失敗時は以前の耐久選好とdirty草稿を保持します
		if( event->succeeded) {
			transcriptionSettingsCloseFailed_ = false ;
			transcriptionSettingsFailedDraft_ = false ;
			commitTranscriptionPreferences( event->value, LocalDiagnosticReason::SETTINGS_APPLY) ;
			// UI言語は従前の別保存方式を使い、未変更では再保存しません
			if( transcriptionSettingsLanguage_ != uiLanguage()) {
				applyUiLanguage( transcriptionSettingsLanguage_, true) ;
			}
		}
		settingsWindow_.completeLocal( transcriptionSettingsSession_, event->succeeded, event->code) ;
		// closeで破棄済みの画面にACKを流用せず、次の明示open用の小草稿だけを保持します
		if( !event->succeeded && settingsWindow_.session() != transcriptionSettingsSession_) {
			transcriptionSettingsFailedDraft_ = true ;
		}
		// 終了中の失敗も黙って破棄せず、停止済みworker回収後に選択を求めます
		if( !event->succeeded && closing_ && transcriptionSettingsCloseWaiting_) {
			transcriptionSettingsCloseFailed_ = true ;
		}
		// 閉じた設定の失敗も固定案内へ残し、API保存成功と混同しません
		if( !event->succeeded && !closing_) {
			showMessage( IDS_TRANSCRIPTION_SAVE_FAILED) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 保存先の検証と復元を新しい専用workerで開始します
	 */
	//------------------------------------------------------------------------------
	void startHistoryController()
	{
		// 新しい読込受付だけで前試行の予約を失効します。同一終端の再配送ではresetしません
		noticeOperations_[static_cast<std::size_t>( StatusNoticeKind::HISTORY_LOAD)] = 0 ;
		std::erase_if( statusNotices_,
					  []( const auto &notice)
					  {
						  return notice.kind == StatusNoticeKind::HISTORY_LOAD ;
					  }) ;
		historyReady_ = false ;
		historyFailed_ = false ;
		historyStopRequested_ = false ;
		history_ = std::make_unique<HistoryController>( makeFileHistoryStorage( historyRoot_),
													   [target = window_]
													   {
														   // 所有snapshotはcontroller内に保持する
														   // UIへは起床だけを通知する
														   PostMessageW( target, K_WAKE_MESSAGE, 0, 0) ;
													   }) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 現在の全履歴を、本文を複製せず保存用の所有snapshotにします
	 * @return 最大32件のimmutable snapshot
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::shared_ptr<const HistorySnapshot> historySnapshot() const
	{
		auto snapshot = std::make_shared<HistorySnapshot>() ; // UIからworkerへ渡す所有値
		snapshot->nextId = nextHistoryId_ ;
		snapshot->nextOperationId = nextOperationId_ ;
		snapshot->entries.reserve( cards_.size()) ;
		// 各カードの終端状態と結果を、翻訳resourceやoperation IDから分離する
		for( const auto &card : cards_) {
			HistoryEntry entry ; // 一つの履歴IDへ対応する保存値
			entry.id = card.id ;
			entry.input = card.path ;
			entry.error = card.status.error ;
			entry.code = card.status.code ;
			entry.result = card.result ;
			entry.reading = card.reading ;
			entry.wholeText = card.wholeText ;
			entry.apiAudio = card.apiAudio ;
			entry.speakerText = card.speakerText ;
			entry.apiText = card.apiText ;
			entry.bounded = card.bounded ;
			entry.resultPreference = card.resultPreference ;
			entry.filter = card.result ? card.filter : K_ALL_SPEAKERS ;
			// 待機・処理中は再起動後に自動再実行せず未完了として表示する
			if( card.busy) {
				entry.state = HistoryState::PENDING ;
			}
			// 本文の有無と話者分離の部分失敗を区別する
			else if( card.apiAudio) {
				const auto terminal = card.apiAudio->terminal ; // 部分原文を成功へ変えません
				entry.state =
					terminal == ApiAudioTerminal::COMPLETED
						? (card.result && card.result->diarizationWarning ? HistoryState::COMPLETED_WITH_WARNINGS
																		  : HistoryState::COMPLETED)
					: terminal == ApiAudioTerminal::NO_SPEECH || terminal == ApiAudioTerminal::EMPTY_TRANSCRIPT
						? HistoryState::NO_TEXT
					: terminal == ApiAudioTerminal::CANCELLED	? HistoryState::CANCELLED
					: terminal == ApiAudioTerminal::INTERRUPTED ? HistoryState::INTERRUPTED
																: HistoryState::FAILURE ;
			} else if( card.result) {
				entry.state = card.result->diarizationWarning ? HistoryState::COMPLETED_WITH_WARNINGS
							  : card.result->lines.empty()	  ? HistoryState::NO_TEXT
															  : HistoryState::COMPLETED ;
			}
			// 前回中断の状態を保持
			else if( card.status.resourceId == IDS_HISTORY_INTERRUPTED) {
				entry.state = HistoryState::INTERRUPTED ;
			}
			// 利用者の取消しは異常終了や認識失敗から分ける
			else if( card.status.resourceId == IDS_CANCELLED ||
					(card.status.resourceId == 0 && card.status.error == ErrorKind::CANCELLED)) {
				entry.state = HistoryState::CANCELLED ;
			} else {
				entry.state = HistoryState::FAILURE ; // 元の診断分類とcodeを添えて保存する
			}
			snapshot->entries.push_back( std::move( entry)) ;
		}
		return snapshot ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 現在の履歴を非同期保存し、受付失敗時も未保存状態を可視化します
	 */
	//------------------------------------------------------------------------------
	void saveHistory()
	{
		// 未復元の履歴や終了済みstorageを新しいsnapshotで上書きしない
		if( !historyReady_ || !history_ || historyStopRequested_) {
			return ;
		}
		historyDirty_ = true ;
		historyRevision_ = 0 ; // snapshot確保に失敗しても、前回の保存通知で未保存を消さない
		// 受付失敗時も以前のSavedを新sourceのbarrierへ流用しません
		for( auto &card : cards_) {
			card.automaticText.saving( 0, card.result) ;
		}
		try {
			const auto revision = history_->save( historySnapshot()) ; // 最新要求へ集約する受付
			// 要求を受理できなかった場合を保存済みと表示しない
			if( revision == 0) {
				throw HistoryError( HistoryErrorKind::WRITE) ;
			}
			historyRevision_ = revision ;
			// 集約された最新保存revisionへ全待ちカードを付け直します
			for( auto &card : cards_) {
				card.automaticText.saving( revision, card.result) ;
			}
			historyFailed_ = false ;
		} catch( const HistoryError &error) {
			historyFailed_ = true ;
			historyError_ = error.kind() ;
			historyCode_ = error.code() ;
		} catch( ...) {
			// snapshot確保失敗でも現在の結果を保持し、再試行できる状態に残す
			historyFailed_ = true ;
			historyError_ = HistoryErrorKind::INTERNAL ;
			historyCode_ = 0 ;
		}
		setStatus( currentStatus_) ;
		updateHistoryActions() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 履歴の保存失敗は現在値を再保存し、読込失敗は保存先から再読込します
	 */
	//------------------------------------------------------------------------------
	void retryHistory()
	{
		// 終了処理と復元中のworkerを重ねて起動しない
		if( closing_ || !historyFailed_) {
			return ;
		}
		// 保存失敗後は画面に残した変更を優先し、古いdisk内容で戻さない
		if( historyReady_) {
			saveHistory() ;
		}
		// 読込失敗したworkerの解放を待って再開する
		else if( history_ && history_->stopped()) {
			history_.reset() ;
			startHistoryController() ;
			showCard() ;
			setStatus( currentStatus_) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 保存された状態を現UIで表示する状態値へ戻します
	 * @param[in] entry 形式検証済みの履歴
	 * @return 翻訳前の表示状態。処理途中は未完了として返します
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] UiStatus restoredStatus( const HistoryEntry &entry) const
	{
		// 保存時の状態を推論開始の命令にせず、表示だけへ変換する
		switch( entry.state) {
			case HistoryState::COMPLETED:
				return {IDS_COMPLETED} ; // 本文を伴う完了
			case HistoryState::COMPLETED_WITH_WARNINGS:
				return {IDS_WARNING} ; // ASRの成功本文を保持した部分失敗
			case HistoryState::NO_TEXT:
				return {IDS_NO_TEXT} ; // 処理完了・認識本文なし
			case HistoryState::FAILURE:
				return {0, -1, entry.error, entry.code} ; // 原因と診断codeを復元
			case HistoryState::CANCELLED:
				return {IDS_CANCELLED, -1, entry.error, entry.code} ; // 利用者の取消し
			case HistoryState::PENDING:
			case HistoryState::INTERRUPTED:
				return {IDS_HISTORY_INTERRUPTED} ; // 前回の途中状態を自動実行しない
		}
		return {0, -1, ErrorKind::INTERNAL} ; // 未知の状態を成功扱いにしない
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 履歴workerの復元・保存結果をrevisionで照合します
	 */
	//------------------------------------------------------------------------------
	void collectHistoryEvent()
	{
		// 初期化途中は履歴workerへアクセスしない
		if( !history_) {
			return ;
		}
		const auto event = history_->takeEvent() ; // 一度だけ受領する所有通知
		// 変更なしで選択や入力中の名前を再描画しない
		if( !event) {
			return ;
		}
		// 専用controlの結果を普通revisionのackから分離します
		if( event->actionId != 0) {
			DiagnosticCorrectionControl value ; // 元ID/actionは失敗でも保持
			value.historyId = event->historyId ;
			value.operationId = event->operationId ;
			value.actionId = event->actionId ;
			value.action = event->action ;
			value.committed = event->controlReceipt.committed ;
			value.save.state = value.committed ? 1 : 2 ;
			value.save.revision =
				value.committed ? std::optional<std::uint64_t>( event->controlReceipt.storageRevision) : std::nullopt ;
			// 同じUI owner/旧全文から保持を照合します
			for( const auto &card : cards_) {
				// 元履歴だけを対象にします
				if( card.id == event->historyId) {
					value.save.originalRetained =
						!event->bounded || detail::sameTranscriptOwner( card.result, event->bounded->source()) ;
					// 旧有効全文がある場合だけ保持を観測します
					if( card.bounded.effectiveText || card.wholeText.corrected) {
						value.save.previousRetained =
							!event->bounded ||
							(card.bounded.effectiveText
								 ? card.bounded.effectiveText == event->bounded->record().effectiveText
								 : event->bounded->record().active == WholeTextActiveFullKind::V1) ;
					}
				}
			}
			// 返却された実recordだけを保存状態とします
			if( event->bounded) {
				value.save.attempt = static_cast<std::uint32_t>( event->bounded->record().latestState) ;
			}
			// cleanupのmetadata成功と残留fileは別です
			if( event->cleanup) {
				value.cleanupComplete = event->cleanup->cleanupComplete ;
				value.residualBytes = event->cleanup->residualBytes ;
				// cleanup失敗の元値を保持します
				if( !event->cleanup->cleanupComplete) {
					value.save.error = event->cleanup->errorKind ;
					value.save.code = event->cleanup->code ;
				}
			}
			// 保存自体の失敗はcleanupと別の元分類です
			if( !value.committed) {
				value.save.error = static_cast<std::uint32_t>( event->error) ;
				value.save.code = event->code ;
			}
			diagnosticLog_.controlTerminal( value) ;
			// 失敗controlで普通history dirty/revisionをackしません
			if( event->kind == HistoryEventKind::SAVE_FAILED) {
				setStatus( currentStatus_) ;
				updateHistoryActions() ;
				return ;
			}
		}
		// loadとsaveの通知を同じ意味の完了として扱わない
		switch( event->kind) {
			case HistoryEventKind::LOADED: {
				cards_.clear() ;
				cards_.reserve( K_MAXIMUM_HISTORY) ;
				nextHistoryId_ = event->loaded->nextId ;
				nextOperationId_ = event->loaded->nextOperationId ;
				speakerTextCorrupt_ = event->loaded->speakerTextCorrupt ;
				// 保存floorをoutstandingがない起動直後だけseedします
				if( controller_ && !controller_->seedNextId( nextOperationId_)) {
					throw HistoryError( HistoryErrorKind::CORRUPT) ;
				}
				// 所有結果を共有し、元音声へアクセスせず登録順に復元する
				for( const auto &entry : event->loaded->entries) {
					FileCard card ; // 復元した完了・未完了の表示カード
					card.id = entry.id ;
					card.path = entry.input ;
					card.status = restoredStatus( entry) ;
					card.result = entry.result ;
					card.reading = entry.reading ;
					card.wholeText = entry.wholeText ;
					card.apiAudio = entry.apiAudio ;
					card.speakerText = entry.speakerText ;
					// 再読は保存済み試行の状態だけを復元し、新しい処理を開始しません
					if( card.speakerText.latest) {
						ApiTextProgress restored ; // 過去の経過時間を捏造しない終端状態
						const auto		state = card.speakerText.latest->state ; // 保存済み最新試行
						restored.phase = state == SpeakerTextState::READY || state == SpeakerTextState::UNCHANGED
											 ? ApiTextPhase::COMPLETED
										 : state == SpeakerTextState::CANCELLED ? ApiTextPhase::CANCELLED
																				: ApiTextPhase::FAILURE ;
						card.status.projection = restored ;
					}
					if( card.apiAudio) {
						card.status.audio = apiAudioStatus( *card.apiAudio) ;
					}
					card.apiText = entry.apiText ;
					card.bounded = entry.bounded ;
					card.resultPreference = entry.resultPreference ;
					card.filter = entry.filter ;
					card.busy = false ;
					// 保存済み試行の復元を新規生成へ数えません
					if( card.bounded.latestRequest) {
						diagnosticLog_.correctionRestored(
							card.id, card.bounded.latestRequest->operationId,
							static_cast<std::uint32_t>( card.bounded.latestState),
							static_cast<std::uint32_t>( card.bounded.active),
							card.bounded.latest
								? std::optional<std::uint32_t>( card.bounded.latest->result.committed.count)
								: std::nullopt,
							card.bounded.effectiveText && card.bounded.effectiveText->complete()) ;
					}
					cards_.push_back( std::move( card)) ;
				}
				selected_ = cards_.empty() ? 0 : cards_.size() - 1 ;
				historyReady_ = true ;
				historyFailed_ = false ;
				historyDirty_ = false ;
				historyRevision_ = 0 ;
				openApi( 0, false, true) ; // workerでlocal設定/キー有無だけを復元、HTTP0
				refreshHistory() ;
				showCard() ;
				setStatus( cards_.empty() ? UiStatus{IDS_READY} : selectedStatus()) ;
				diagnosticLog_.append( DiagnosticEvent::HISTORY_LOADED, 0, cards_.size()) ;
				// 明示起動optionも復元を終えてから既存履歴との照合を行う
				if( !closing_ && modelReady_ && !initialInput_.empty()) {
					const auto input = std::exchange( initialInput_, {}) ; // 一度だけ使う所有path
					addFile( input) ;
				}
				break ;
			}
			case HistoryEventKind::LOAD_FAILED:
				historyFailed_ = true ;
				historyError_ = event->error ;
				historyCode_ = event->code ;
				diagnosticLog_.append( DiagnosticEvent::HISTORY_LOAD_FAILED, 0, static_cast<std::uint64_t>( event->error),
									  0, event->code) ;
				showCard() ;
				queueStatusNotice( StatusNoticeKind::HISTORY_LOAD, 1,
								  {IDS_HISTORY_LOAD_ERROR, -1, ErrorKind::INTERNAL, event->code}, nullptr,
								  event->error) ;
				break ; // 既存の履歴fileを保護したまま操作を停止する
			case HistoryEventKind::SAVED:
				// 古いsnapshotの完了で、さらに新しい未保存変更を消さない
				if( event->revision == historyRevision_) {
					historyDirty_ = false ;
					historyFailed_ = false ;
					// 選択documentに依存せず、同じ保存owner/keyだけへackを渡します
					for( auto &card : cards_) {
						// 最大32件の耐久snapshot内で同じカードを照合します
						for( const auto &original : event->savedOriginals) {
							// 別IDの保存成功をこのカードへ適用しません
							if( original.key.historyId == card.id) {
								parakeetFollowup_.acknowledge( card.id, original.source) ;
								card.automaticText.acknowledge( event->revision, original.source, original.key) ;
							}
						}
					}
					diagnosticLog_.append( DiagnosticEvent::HISTORY_SAVED, event->revision, cards_.size()) ;
				}
				break ;
			case HistoryEventKind::BOUNDED_SAVED:
				// 再保存と未完削除は推論を呼ばず同じカードへ反映します
				if( event->bounded) {
					// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
					for( auto &card : cards_) {
						// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
						if( card.id == event->bounded->request().source.historyId &&
						   detail::sameTranscriptOwner( card.result, event->bounded->source())) {
							card.bounded = event->bounded->record() ;
						}
					}
				}
				// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
				if( event->loaded) {
					// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
					for( auto &card : cards_) {
						// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
						for( const auto &entry : event->loaded->entries) {
							// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
							if( card.id == entry.id) {
								card.bounded = entry.bounded ;
							}
						}
					}
				}
				showCard() ;
				break ;
			case HistoryEventKind::SAVE_FAILED:
				// 削除metadata失敗は消したカードを正本から戻します
				if( event->loaded) {
					// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
					for( const auto &entry : event->loaded->entries) {
						// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
						if( std::none_of( cards_.begin(), cards_.end(),
							//------------------------------------------------------------------------------
							/**
							 * @brief 復元予定の履歴行と同じIDのカードが既に一覧にあるか調べます
							 * @param[in] card 一覧にあるカードの読取り専用借用。none_ofの同期呼出し中だけ参照します
							 * @return card.idが復元中entry.idと等しければtrue、異なればfalse
							 * @note 二重カードの追加を防ぐID照合であり、本文や共有ownerの一致は検証しません
							 */
							//------------------------------------------------------------------------------
							[&entry]( const auto &card)
							{
								return card.id == entry.id ;
							})) {
							FileCard card ; /* 復元する一カード */
							card.id = entry.id ;
							card.path = entry.input ;
							card.result = entry.result ;
							card.reading = entry.reading ;
							card.wholeText = entry.wholeText ;
							card.apiAudio = entry.apiAudio ;
							card.speakerText = entry.speakerText ;
							if( card.apiAudio) {
								card.status.audio = apiAudioStatus( *card.apiAudio) ;
							}
							card.apiText = entry.apiText ;
							card.bounded = entry.bounded ;
							card.resultPreference = entry.resultPreference ;
							card.filter = entry.filter ;
							card.status = restoredStatus( entry) ;
							card.busy = false ;
							cards_.push_back( std::move( card)) ;
						}
					}
					std::sort( cards_.begin(), cards_.end(),
							  //------------------------------------------------------------------------------
							  /**
							   * @brief 復元後の一覧を履歴IDの昇順へ並べるため、二つのカードを比較します
							   * @param[in] a 前に置くか判定するカードの読取り専用借用。比較中だけ有効です
							   * @param[in] b 比較相手のカードの読取り専用借用。比較中だけ有効です
							   * @return a.idがb.idより小さい場合だけtrue。同じIDまたは大きい場合はfalse
							   * @note カードや本文を変更しないstrictな比較述語です
							   */
							  //------------------------------------------------------------------------------
							  []( const auto &a, const auto &b)
							  {
								  return a.id < b.id ;
							  }) ;
					refreshHistory() ;
					showCard() ;
				}
				// 次の変更が既に待機している場合は、その結果の通知を待つ
				if( event->revision == historyRevision_) {
					historyFailed_ = true ;
					historyError_ = event->error ;
					historyCode_ = event->code ;
					diagnosticLog_.append( DiagnosticEvent::HISTORY_SAVE_FAILED, event->revision,
										  static_cast<std::uint64_t>( event->error), 0, event->code) ;
					queueStatusNotice( StatusNoticeKind::HISTORY_SAVE, event->revision + 1,
									  {IDS_HISTORY_SAVE_ERROR, -1, ErrorKind::INTERNAL, event->code}, nullptr,
									  event->error) ;
				}
				break ;
		}
		setStatus( currentStatus_) ;
		updateHistoryActions() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 履歴の読込・処理・保存状態に合わせて管理操作を有効にします
	 */
	//------------------------------------------------------------------------------
	void updateHistoryActions()
	{
		const bool editable = historyReady_ && !closing_ ; // 読込前と終了中は履歴を変更しない
		// 選択カードが通常処理・整形とも停止し、履歴削除を受け付けられる条件
		const bool removable = editable && selected_ < cards_.size() && !cards_[selected_].busy &&
							   !cards_[selected_].reading.active && !cards_[selected_].wholeText.active ;
		// 全カードと保存要求の完了後にだけ全履歴削除を許可する条件
		const bool canClear = editable && !apiTextWindow_.busy() && !cards_.empty() && saves_.empty() &&
							  std::none_of( cards_.begin(), cards_.end(),
								  //------------------------------------------------------------------------------
								  /**
								   * @brief 履歴の全削除を保留すべき処理中・未保存状態がカードにあるか調べます
								   * @param[in] card 一覧から同期借用する読取り専用カード。状態を変更しません
								   * @return 音声処理中、行整形中、全文補正中、有界補正の実行予約あり、または有界補正の保存失敗ならtrue
								   *         falseはこれら五条件がないことを示し、他の全削除条件の成立までは示しません
								   */
								  //------------------------------------------------------------------------------
								  []( const FileCard &card)
								  {
									  return card.busy || card.reading.active.has_value() ||
											 card.wholeText.active.has_value() ||
											 card.bounded.inFlight.has_value() ||
											 card.bounded.saveState == WholeTextBoundedSaveState::SAVE_FAILED ;
								  }) ;
		EnableWindow( control( IDC_OPEN), editable) ;
		EnableWindow( control( IDC_CLEAR), canClear) ;
		EnableMenuItem( GetMenu( window_), IDC_OPEN, MF_BYCOMMAND | (editable ? MF_ENABLED : MF_GRAYED)) ;
		EnableMenuItem( GetMenu( window_), IDC_CLEAR, MF_BYCOMMAND | (canClear ? MF_ENABLED : MF_GRAYED)) ;
		EnableMenuItem( GetMenu( window_), IDC_HISTORY_REMOVE, MF_BYCOMMAND | (removable ? MF_ENABLED : MF_GRAYED)) ;
		EnableMenuItem( GetMenu( window_), IDC_HISTORY_RETRY,
					   MF_BYCOMMAND | (historyFailed_ && !closing_ ? MF_ENABLED : MF_GRAYED)) ;
		const bool canApi =
			editable && !historyDirty_ && !historyFailed_ && selected_ < cards_.size() && cards_[selected_].result &&
			!cards_[selected_].result->lines.empty() && !cards_[selected_].busy && !cards_[selected_].reading.active &&
			!cards_[selected_].wholeText.active && !cards_[selected_].apiText.saveFailed && apiTextWindow_.ready() &&
			(!cards_[selected_].apiAudio ||
			 cards_[selected_].apiAudio->terminal == ApiAudioTerminal::COMPLETED) ; // 完全音声原文だけ
		EnableMenuItem( GetMenu( window_), IDC_API_TEXT_OPEN, MF_BYCOMMAND | (canApi ? MF_ENABLED : MF_GRAYED)) ;
		DragAcceptFiles( window_, editable) ;
		DragAcceptFiles( chat_, editable) ;
		refreshWholeTextActions() ;
		refreshReadingActions() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 一項目の履歴だけを削除し、原音声や明示exportを保持します
	 * @param[in] sourceId 操作開始時の履歴ID。既に削除されていれば無視します
	 */
	//------------------------------------------------------------------------------
	void removeHistory( const std::uint64_t sourceId)
	{
		// 未復元と終了中の一覧を変更しない
		if( !historyReady_ || closing_) {
			return ;
		}
		const auto found = std::find_if( cards_.begin(), cards_.end(),
			//------------------------------------------------------------------------------
			/**
			 * @brief 削除要求の元IDと一致するカードを照合します
			 * @param[in] card 同期find_if中だけ借用する既存カード
			 * @return 値で捕捉した元IDと同じならtrue
			 */
			//------------------------------------------------------------------------------
			[sourceId]( const FileCard &card)
			{
				return card.id == sourceId ;
			}) ; // 元IDで照合
		// 古いpopup操作を別項目へ適用しない
		if( found == cards_.end()) {
			return ;
		}
		// 動作中のworkerを見えないまま残さず、先に取消しを依頼してもらう
		if( found->busy || found->reading.active || found->wholeText.active ||
		   (apiTextWindow_.busy() && apiTextWindow_.historyId() == sourceId) || found->apiText.saveFailed ||
		   found->bounded.inFlight || found->bounded.saveState == WholeTextBoundedSaveState::SAVE_FAILED) {
			showMessage( IDS_HISTORY_REMOVE_BUSY) ;
			return ;
		}
		const auto index = static_cast<std::size_t>( found - cards_.begin()) ; // 削除前の位置
		parakeetFollowup_.cancel( sourceId) ; // 削除後の同意を別行へ移しません
		cards_.erase( found) ;
		// 先行する行だけが消えた場合は、同じ選択項目を維持する
		if( selected_ > index) {
			--selected_ ;
		}
		selected_ = cards_.empty() ? 0 : std::min( selected_, cards_.size() - 1) ;
		refreshHistory() ;
		showCard() ;
		saveHistory() ;
		setStatus( {IDS_HISTORY_REMOVED}) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 終了済みの履歴全件を、明示確認後に削除します
	 */
	//------------------------------------------------------------------------------
	void clearHistory()
	{
		// active結果と明示保存を暗黙に捨てず、空の一覧には確認を出さない
		if( !historyReady_ || closing_ || cards_.empty() || !saves_.empty() ||
		   std::any_of( cards_.begin(), cards_.end(),
					   []( const FileCard &card)
					   {
						   return card.busy || card.reading.active.has_value() || card.wholeText.active.has_value() ||
								  card.bounded.inFlight.has_value() ||
								  card.bounded.saveState == WholeTextBoundedSaveState::SAVE_FAILED ;
					   })) {
			return ;
		}
		// modalの全削除確認に対する利用者の選択
		const int chosen = showPrompt( IDS_HISTORY_CLEAR_CONFIRM, MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) ;
		// modal中の状態変化も照合し、取消しや新しい処理を全削除しない
		if( chosen != IDYES || closing_ || !saves_.empty() ||
		   std::any_of( cards_.begin(), cards_.end(),
					   //------------------------------------------------------------------------------
					   /**
					    * @brief 全削除の前に処理中または整形中のカードを検出します
					    * @param[in] card 同期any_of中だけ借用する既存カード
					    * @return 通常処理または整形が継続中ならtrue
					    */
					   //------------------------------------------------------------------------------
					   []( const FileCard &card)
					   {
						   return card.busy || card.reading.active.has_value() || card.wholeText.active.has_value() ||
								  card.bounded.inFlight.has_value() ||
								  card.bounded.saveState == WholeTextBoundedSaveState::SAVE_FAILED ;
					   })) {
			return ;
		}
		parakeetFollowup_.clear() ; // 全削除では未実行の案内/続行も終端にします
		cards_.clear() ;
		selected_ = 0 ;
		refreshHistory() ;
		showCard() ;
		saveHistory() ;
		setStatus( {IDS_HISTORY_REMOVED}) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 既存controlの成立だけを専用IDへ接続します
	 * @param[in] history 元ID
	 * @param[in] operation 元op
	 * @param[in] discard 明示削除
	 * @return 受付時true
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool queueBoundedControl( std::uint64_t history, std::uint64_t operation, bool discard)
	{
		std::uint64_t action = 0 ; // 生成と別の単調系列
		const bool	  accepted = discard ? history_->discardBoundedAttempt( history, operation, &action)
										 : history_->retryBoundedSave( history, operation, &action) ; // 同じworker
		// controller拒否を受付として記録しません
		if( accepted) {
			DiagnosticCorrectionControl value ; /* scalarだけ */
			value.historyId = history ;
			value.operationId = operation ;
			value.actionId = action ;
			value.action = discard ? 1 : 0 ;
			diagnosticLog_.controlAccepted( value) ;
		}
		return accepted ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 更新workerは通常製品だけnative、全試験は無効またはfakeです
	 */
	//------------------------------------------------------------------------------
	void startUpdateController()
	{
		std::unique_ptr<IUpdatePlatform> platform ; // 試験既定は通信できない空値
#if defined(KOEFUDE_WINDOW_TESTING)
        // 明示fake注入なしに既存UI試験をnetworkへ接続しません
		if( testHooks_.updatePlatform) {
			platform = testHooks_.updatePlatform() ;
		}
#else
        // 隔離CLI/診断modeも従来のnetwork0契約を保ちます
		if( !historyRoot_.empty() || diagnosticLog_.currentMode()) {
			return ;
		}
		platform = makeNativeUpdatePlatform() ;
#endif
        // 無効seamはworkerも起動しません
		if( !platform) {
			return ;
		}
		updater_ =
			std::make_unique<AppUpdateController>( *parseUpdateVersion( KOEFUDE_VERSION_STRING), std::move( platform),
				//------------------------------------------------------------------------------
				/**
				 * @brief 小DTOをmessageに載せず自Windowだけを起床します
				 */
				//------------------------------------------------------------------------------
				[target = window_]
				{
					PostMessageW( target, K_WAKE_MESSAGE, 0, 0) ;
				}) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 更新が通常仕事を中断せず終了できるかを確認します
	 * @return 全仕事/保存がidle
	 */
	//------------------------------------------------------------------------------
	bool updateIdle() const
	{
		return !settingsWindow_.opened() && historyReady_ && !historyDirty_ && !historyFailed_ &&
			   transcriptionSettingsReady_ && !transcriptionSettingsOperation_ && !transcriptionSettingsCloseFailed_ &&
			   !modelDownloadActive_ && !wholeTextPreparationWindow_.busy() && !apiTextWindow_.busy() &&
			   !parakeetFollowup_.pending() && saves_.empty() &&
			   std::none_of( cards_.begin(), cards_.end(),
							//------------------------------------------------------------------------------
							/**
							 * @brief 実行中/未保存を維持します
							 * @param[in] card 同期borrow
							 * @return 未終端
							 */
							//------------------------------------------------------------------------------
							[]( const FileCard &card)
							{
								return card.busy || card.reading.active || card.wholeText.active ||
									   card.bounded.inFlight || card.apiText.saveFailed ||
									   card.bounded.saveState == WholeTextBoundedSaveState::PERSISTING ||
									   card.bounded.saveState == WholeTextBoundedSaveState::SAVE_FAILED ;
							}) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定段階を短いstatusへ変換します
	 * @return resource ID
	 */
	//------------------------------------------------------------------------------
	UINT updateNotice() const noexcept
	{
		// 更新専用段階だけを扱い、ASR/APIの成功/失敗へ変換しません
		switch( updateEvent_.state) {
			case UpdateState::CHECKING:
				return IDS_UPDATE_CHECKING ; // 固定feed待機
			case UpdateState::AVAILABLE:
				return IDS_UPDATE_AVAILABLE ; // 取得は未実行
			case UpdateState::CURRENT:
				return IDS_UPDATE_CURRENT ; // 正規応答だけ
			case UpdateState::DOWNLOADING:
				return IDS_UPDATE_DOWNLOADING ; // 実処理中
			case UpdateState::READY:
				return IDS_UPDATE_READY ; // 明示終了待ち
			case UpdateState::CANCELLED:
				return IDS_UPDATE_CANCELLED ; // 更新だけ取消し
			case UpdateState::FAILURE:
				return IDS_UPDATE_FAILED ; // dataを保持
			default:
				return IDS_UPDATE_UNAVAILABLE ; // 未確認を最新と表示しません
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 更新の小DTOから既存statusを設定します
	 */
	//------------------------------------------------------------------------------
	void setUpdateStatus()
	{
		UiStatus status{updateNotice()} ; // 更新専用typed小DTO
		status.update = updateEvent_ ;
		setStatus( status) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 現在の小DTOに合わせ既存設定menuだけを更新します
	 */
	//------------------------------------------------------------------------------
	void updateMenu()
	{
		const bool active = updateEvent_.state == UpdateState::CHECKING ||
							updateEvent_.state == UpdateState::DOWNLOADING ||
							updateEvent_.state == UpdateState::LAUNCHING ; // 実段階
		const auto menu = GetMenu( window_) ; // 自所有menuのborrow
		EnableMenuItem( menu, IDC_UPDATE_CHECK,
					   MF_BYCOMMAND | (updater_ && !active && !closing_ ? MF_ENABLED : MF_GRAYED)) ;
		EnableMenuItem( menu, IDC_UPDATE_CANCEL,
					   MF_BYCOMMAND | (updater_ && active && !closing_ ? MF_ENABLED : MF_GRAYED)) ;
		EnableMenuItem( menu, IDC_UPDATE_INSTALL,
					   MF_BYCOMMAND | (updater_ && !closing_ &&
											   (updateEvent_.state == UpdateState::AVAILABLE ||
												updateEvent_.state == UpdateState::READY)
										   ? MF_ENABLED
										   : MF_GRAYED)) ;
		auto label =
			resource( updateEvent_.state == UpdateState::READY ? IDS_UPDATE_INSTALL : IDS_UPDATE_GET) ; // 固定文字列
		MENUITEMINFOW item{sizeof(item)} ; // Win32同期borrow
		item.fMask = MIIM_STRING ;
		item.dwTypeData = label.data() ;
		SetMenuItemInfoW( menu, IDC_UPDATE_INSTALL, FALSE, &item) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じopの終端だけを回収し、起動/取得/終了は自動で始めません
	 */
	//------------------------------------------------------------------------------
	void collectUpdate()
	{
		// 試験無効seam/初期化前は何もしません
		if( !updater_) {
			return ;
		}
		const auto event = updater_->takeEvent() ; // 小ACK一件
		// 別op/古い通知は現在の更新へ適用しません
		if( event && (event->operation == updateOperation_ || event->operation == 0)) {
			updateEvent_ = *event ;
			// 音声/APIの実行中statusへ割り込ませません
			if( !closing_ && updateIdle()) {
				setUpdateStatus() ;
			}
			// 起動確認の非致命失敗はpopupを出さず、手動確認の結果だけ一度案内します
			if( event->operation &&
			   (event->operation == manualUpdateOperation_ || event->state == UpdateState::FAILURE) &&
			   (event->state == UpdateState::AVAILABLE || event->state == UpdateState::CURRENT ||
				event->state == UpdateState::UNAVAILABLE || event->state == UpdateState::FAILURE ||
				event->state == UpdateState::READY)) {
				UiStatus status{updateNotice()} ;
				status.update = *event ;
				queueStatusNotice( StatusNoticeKind::UPDATE, event->operation, status) ;
			}
		}
		updateMenu() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 更新用の確認/明示取得/明示終了だけを処理します
	 * @param[in] id 固定command
	 */
	//------------------------------------------------------------------------------
	void updateCommand( int id)
	{
		// 初期化失敗/試験無効seamから通信を開始しません
		if( !updater_ || closing_) {
			return ;
		}
		// 通常closeの取消し後は新しいworkerで手動確認できます
		if( updater_->stopped()) {
			startUpdateController() ;
			updateEvent_ = {} ;
		}
		// 確認は一回のGETだけ、取得を伴いません
		if( id == IDC_UPDATE_CHECK) {
			const auto op = updater_->check() ; // 二重受付は0
			// 受付済みのopだけを表示へ確定します
			if( op) {
				updateOperation_ = op ;
				manualUpdateOperation_ = op ; // 起動経路では設定しません
				updateEvent_ = {} ;
				updateEvent_.operation = op ;
				updateEvent_.state = UpdateState::CHECKING ;
				setUpdateStatus() ;
			}
		} else if( id == IDC_UPDATE_CANCEL) {
			// 取消しは更新workerだけを対象にします
			updater_->cancel() ;
		} else if( id == IDC_UPDATE_INSTALL) {
			const auto previous = updateEvent_ ; // modal後のstale照合用小値
			// 既に準備済みの場合も実仕事/保存を終えてから明示終了します
			if( previous.state == UpdateState::READY && !updateIdle()) {
				showMessage( IDS_UPDATE_BUSY) ;
				return ;
			}
			// 未取得/準備済み以外の古いcommandは同意画面にも進めません
			if( previous.state != UpdateState::AVAILABLE && previous.state != UpdateState::READY) {
				return ;
			}
			const auto chosen = updatePrompt( previous.state == UpdateState::AVAILABLE ? IDS_UPDATE_GET_PROMPT
																					  : IDS_UPDATE_CLOSE_PROMPT) ;
			// 同意前のdownload/launchは0、modal中のclose/stale/二重操作を拒否します
			if( chosen != IDYES || closing_ || updateEvent_.operation != previous.operation ||
			   updateEvent_.state != previous.state) {
				return ;
			}
			// 新規取得だけを明示同意後に受け付けます
			if( previous.state == UpdateState::AVAILABLE) {
				const auto op = updater_->download( previous.operation) ; // 前段op照合
				// 重複拒否を成功状態へ変換しません
				if( op) {
					updateOperation_ = op ;
					updateEvent_.operation = op ;
					updateEvent_.state = UpdateState::DOWNLOADING ;
					setUpdateStatus() ;
				}
			} else if( updateIdle()) {
				// modal後にもidleを再確認し、既存の協調終了へ束縛します
				updateCloseIntent_ = true ;
				// 同UI threadで受付停止まで確定し、後続dropを先に受付けません
				SendMessageW( window_, WM_CLOSE, 0, 0) ;
				return ; // 同期callback後は破棄済みの可能性があるHWNDへ触れません
			} else {
				showMessage( IDS_UPDATE_BUSY) ; // modal中に始まった仕事も保持します
			}
		}
		updateMenu() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 更新だけの小さい明示案内を出します
	 * @param[in] notice 固定resource
	 * @return Yes/No、失敗は同意しません
	 */
	//------------------------------------------------------------------------------
	int updatePrompt( UINT notice)
	{
		FollowupModalScope modal( followupModalDepth_) ; // 試験seamの再入も実modalと同じ境界
#if defined(KOEFUDE_WINDOW_TESTING)
        // 自所有試験では画面を出さず、再入/closeだけを有限注入できます
		if( testHooks_.updatePrompt) {
			return testHooks_.updatePrompt() ;
		}
#endif
        return showPrompt( notice, MB_YESNO | MB_DEFBUTTON2 | MB_ICONINFORMATION) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 更新の最終起動失敗時に、保存済み結果のある通常画面へ戻します
	 */
	//------------------------------------------------------------------------------
	void abortUpdateClose()
	{
		updateCloseIntent_ = false ;
		closing_ = false ;
		startJobController() ;
		startModelController() ;
		startDocumentController() ;
		modelOperation_ = 0 ;
		modelDownloadActive_ = false ;
		requestModelCheck( true) ;
		apiTextWindow_.resumeAfterUpdateFailure() ; // 既存キー/設定を再読/送信せず受付だけ復帰
		showCard() ;
		UiStatus status{IDS_UPDATE_FAILED} ; // 正常画面へ戻ってから元診断も一度案内します
		status.update = updateEvent_ ;
		queueStatusNotice( StatusNoticeKind::UPDATE, updateEvent_.operation, status) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 推論と最後の履歴保存を回収してからWindowを閉じます
	 */
	//------------------------------------------------------------------------------
	void finishClose()
	{
		// 推論workerの最後の通知と、既に開いている終了確認を待つ
		if( !closing_ || !controller_->stopped() || (preparation_ && !preparation_->stopped()) ||
		   !wholeTextPreparationWindow_.stopped() || (documents_ && !documents_->stopped()) || closePromptOpen_) {
			return ;
		}
		// Apply済みと表示する前の保存は、終了時もACKを回収してから判断します
		if( transcriptionSettingsOperation_) {
			return ;
		}
		// 終了と競合した保存失敗だけ、明示再試行/旧値で終了/取消しを提供します
		if( transcriptionSettingsCloseFailed_) {
			// 更新は保存失敗を無視してinstallerを起動しません
			if( updateCloseIntent_) {
				abortUpdateClose() ;
				return ;
			}
			closePromptOpen_ = true ;
			const auto chosen = showPrompt( IDS_TRANSCRIPTION_EXIT_FAILED,
										   MB_YESNOCANCEL | MB_DEFBUTTON3 | MB_ICONWARNING) ; // 小さい固定案内
			closePromptOpen_ = false ;
			// Yesは当該小設定の保存だけを明示再試行し、API要求を重ねません
			if( chosen == IDYES) {
				transcriptionSettingsOperation_ = transcriptionSettings_->save( transcriptionSettingsSubmitted_) ;
				transcriptionSettingsCloseFailed_ = !transcriptionSettingsOperation_ ;
				return ;
			}
			transcriptionSettingsCloseFailed_ = false ;
			transcriptionSettingsCloseWaiting_ = false ;
			// Noだけが以前の耐久値のままの終了を明示承認します
			if( chosen != IDNO) {
				closing_ = false ;
				startJobController() ;
				startModelController() ;
				startDocumentController() ;
				modelOperation_ = 0 ;
				modelDownloadActive_ = false ;
				requestModelCheck( true) ;
				showSettings() ;
				settingsWindow_.restoreLocalDraft( transcriptionOptions( transcriptionSettingsSubmitted_),
												  transcriptionSettingsLanguage_,
												  transcriptionSettingsSubmitted_.checkUpdates) ;
				showCard() ;
				return ;
			}
		}
		// この同期境界で所有する値、または確定状態の検証値です
		const bool boundedUnsaved =
			std::any_of( cards_.begin(), cards_.end(),
						//------------------------------------------------------------------------------
						/**
						 * @brief 終了時に未保存の有界補正について確認が必要なカードを探します
						 * @param[in] card 一覧から同期借用する読取り専用カード
						 * @return 有界補正のsave_stateがSaveFailedならtrue、それ以外はfalse
						 * @note 通常履歴の保存失敗とは別の判定で、保存や終了操作は行いません
						 */
						//------------------------------------------------------------------------------
						[]( const auto &card)
						{
							return card.bounded.saveState == WholeTextBoundedSaveState::SAVE_FAILED ;
						}) ; // 未保存terminalを別管理
		// この同期境界で所有する値、または確定状態の検証値です
		const bool boundedSaving =
			std::any_of( cards_.begin(), cards_.end(),
						//------------------------------------------------------------------------------
						/**
						 * @brief 有界補正の保存ACKを待って終了を保留すべきカードを探します
						 * @param[in] card 一覧から同期借用する読取り専用カード
						 * @return 有界補正のsave_stateがPersistingならtrue、それ以外はfalse
						 * @note ACKの待機状態を照会するだけで、保存や終了操作は行いません
						 */
						//------------------------------------------------------------------------------
						[]( const auto &card)
						{
							return card.bounded.saveState == WholeTextBoundedSaveState::PERSISTING ;
						}) ; // 同一J再保存のack待ち
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( boundedSaving && !allowUnsavedClose_) {
			return ;
		}
		// 実際の保存失敗時だけ、再試行・明示破棄・画面復帰を選べるようにする
		if( ((historyDirty_ && historyFailed_) || boundedUnsaved) && !allowUnsavedClose_) {
			// 明示終了更新でも旧結果を捨てず、更新だけを中止します
			if( updateCloseIntent_) {
				abortUpdateClose() ;
				return ;
			}
			closePromptOpen_ = true ;
			// 未保存の履歴を再試行・破棄・終了取消しする利用者の選択
			const int chosen = showPrompt( boundedUnsaved ? IDS_BOUNDED_EXIT_FAILED : IDS_HISTORY_EXIT_FAILED,
										  MB_YESNOCANCEL | MB_DEFBUTTON3 | MB_ICONWARNING) ;
			closePromptOpen_ = false ;
			// 現在の所有結果を維持したまま、保存要求を新しいrevisionで再試行する
			if( chosen == IDYES) {
				// 終端済み未保存Jは同じopで保存だけを再試行します
				if( boundedUnsaved) {
					// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
					for( auto &card : cards_) {
						// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
						if( card.bounded.saveState == WholeTextBoundedSaveState::SAVE_FAILED &&
						   card.bounded.latestRequest &&
						   queueBoundedControl( card.id, card.bounded.latestRequest->operationId, false)) {
							card.bounded.saveState = WholeTextBoundedSaveState::PERSISTING ;
						}
					}
				} else {
					saveHistory() ;
				}
				return ;
			}
			// 利用者が未保存分の破棄を明示した場合だけ終了を継続する
			if( chosen == IDNO) {
				allowUnsavedClose_ = true ;
			} else {
				closing_ = false ;
				startJobController() ; // 旧workerの停止後に新しいoperation受付を開始する
				startModelController() ;
				startDocumentController() ;
				modelOperation_ = 0 ;
				modelDownloadActive_ = false ;
				requestModelCheck( true) ;
				showCard() ;
				setStatus( cards_.empty() ? UiStatus{IDS_READY} : selectedStatus()) ;
				return ;
			}
		}
		// 通常終了では最新revisionの保存成功まで、UI応答を維持して待つ
		if( historyDirty_ && !allowUnsavedClose_) {
			return ;
		}
		// 保存完了後にだけworkerへ最終起動を渡し、UIでhash/trustをしません
		if( updateCloseIntent_) {
			// 初回だけReady ownerを移し、後続pollは同じopの起動終端を待ちます
			if( updateEvent_.state == UpdateState::READY) {
				updateOperation_ = updater_->launch( updateEvent_.operation) ;
				// 起動受付失敗も通常画面へ復帰します
				if( !updateOperation_) {
					abortUpdateClose() ;
					return ;
				}
				updateEvent_.state = UpdateState::LAUNCHING ;
				return ;
			}
			// OS起動の完了前には親appを破棄しません
			if( updateEvent_.state == UpdateState::LAUNCHING) {
				return ;
			}
			// 不正/失敗/取消しでinstallerが動いたことにしません
			if( updateEvent_.state != UpdateState::LAUNCHED || allowUnsavedClose_) {
				abortUpdateClose() ;
				return ;
			}
		}
		// 最後の保存結果を受領してから、履歴workerの受付を停止する
		if( history_ && !historyStopRequested_) {
			history_->requestStop() ;
			historyStopRequested_ = true ;
		}
		// 両workerのnotifierと排他解放が完了した後だけHWNDを破棄する
		if( !history_ || history_->stopped()) {
			// 通知先HWNDより先に更新workerの全callbackと資源を解放します
			if( updater_) {
				updater_->requestStop() ;
				// UIでjoinせずtimerで完了を回収します
				if( !updater_->stopped()) {
					return ;
				}
			}
			// 未適用草稿は保存せず、既に受け付けた一件だけをdrainします
			if( transcriptionSettings_) {
				transcriptionSettings_->requestStop() ;
				// UIはjoinせず通常event/timerで終了を待ちます
				if( !transcriptionSettings_->stopped()) {
					return ;
				}
				collectTranscriptionSettings() ; // 最後の保存事実も破棄前に回収
			}
			collectHistoryEvent() ; // 専用controlの最後の未受領slot
			collectHistoryEvent() ; // 独立した普通通知slot
			std::uint32_t unsaved = 0 ; // 実UI所有状態だけ
			// 最大32カードの未保存を数えます
			for( const auto &card : cards_) {
				// 成立した条件だけを処理し、未観測値や別操作へ適用しません
				if( card.bounded.saveState == WholeTextBoundedSaveState::SAVE_FAILED) {
					++unsaved ;
				}
			}
			diagnosticLog_.finishCurrent( unsaved, allowUnsavedClose_ ? 1 : 0) ;
			DestroyWindow( window_) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 再実行を受け付けられない理由を現在の履歴から判定します
	 * @param[in] input 選択した登録済み音声path。同期判定中のborrow
	 * @return 受付可能なら0、それ以外は理由を示す表示resource ID
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] UINT retranscribeBlocked( const std::filesystem::path &input) const
	{
		// 終了開始後は、menu表示中に完了通知が届いても再投入しない
		if( closing_ || !historyReady_ || !transcriptionSettingsReady_) {
			return closing_ ? IDS_STOPPING : IDS_HISTORY_LOAD_ERROR ;
		}
		std::size_t outstanding = saves_.size() ; // 保存要求も同じ受付枠へ含める
		// 最大32件を確認し、同一pathの二重処理と未受領の終端枠を守る
		for( const auto &card : cards_) {
			// ASRと整形の待機・実行・取消し待ちだけを処理中として数える
			if( card.busy || card.reading.active || card.wholeText.active) {
				++outstanding ;
				// 登録済みの同じpathを重ねて再投入しない
				if( sameInput( card.path, input)) {
					return IDS_RETRANSCRIBE_BUSY ;
				}
			}
		}
		// 結果比較のために元履歴を消して受付枠を作ることはしない
		if( cards_.size() >= K_MAXIMUM_HISTORY || outstanding >= K_MAXIMUM_OUTSTANDING_JOBS) {
			return IDS_RETRANSCRIBE_LIMIT ;
		}
		return 0 ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 前の履歴を保持し、現在の設定で同じ音声を新jobへ投入します
	 * @param[in] sourceId 再実行元の登録済みjob ID。古い・未知IDは無視します
	 */
	//------------------------------------------------------------------------------
	void retranscribe( const std::uint64_t sourceId)
	{
		const auto matchesSource =
			//------------------------------------------------------------------------------
			/**
			 * @brief menuを開いた時のjob IDと候補の履歴を照合します
			 * @param[in] card 検索中の履歴。同期呼出し中のborrow
			 * @return 同じjob IDならtrue
			 */
			//------------------------------------------------------------------------------
			[sourceId]( const FileCard &card)
			{
				return card.id == sourceId ;
			} ;
		// この同期境界で所有する値、または確定状態の検証値です
		const auto source = std::find_if( cards_.begin(), cards_.end(), matchesSource) ; // 元IDで照合
		// menu表示中に消えた履歴を別のfileへ読み替えない
		if( source == cards_.end()) {
			return ;
		}
		const UINT blocked = retranscribeBlocked( source->path) ; // 操作確定時にも受付条件を再確認
		// 実行中・上限・終了中の理由を示し、以前の結果を維持する
		if( blocked != 0) {
			showMessage( blocked) ;
			return ;
		}
		const std::filesystem::path input = source->path ; // vectorの再確保前にpathを所有copyする
		parakeetFollowup_.cancel( sourceId) ; // 明示再認識へ旧原文の送信意図を渡しません
		addFile( input, true) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 安定履歴IDの文章操作、またはsource不要設定を開きます
	 * @param[in] sourceId 行contextで固定した履歴ID。設定では0
	 * @param[in] settings 独立設定を開くか
	 * @param[in] local 起動時local準備だけか
	 */
	//------------------------------------------------------------------------------
	void openApi( std::uint64_t sourceId, bool settings, bool local = false)
	{
		// 旧設定commandも同じ一入口へ集約し、本文要求を作りません
		if( settings) {
			showSettings() ;
			return ;
		}
		ApiTextCorrectionJobRequest value ; // 設定ではsourceを保持しません
		// 通常保存完了前と終了中は新しい文書操作を受付けません
		if( !settings && !local) {
			const auto found = std::find_if( cards_.begin(), cards_.end(),
				//------------------------------------------------------------------------------
				/**
				 * @brief 同じIDだけを検索します
				 * @param[in] card 候補
				 * @return 一致
				 */
				//------------------------------------------------------------------------------
				[sourceId]( const FileCard &card)
				{
					return card.id == sourceId ;
				}) ; // 最大32件
			// 消えた行・未完了行は別の選択行へ読み替えません
			if( closing_ || !historyReady_ || historyDirty_ || historyFailed_ || found == cards_.end() ||
			   !found->result || found->result->lines.empty() || found->busy || found->wholeText.active ||
			   found->reading.active || !apiTextWindow_.ready() || found->apiText.saveFailed ||
			   (found->apiAudio && found->apiAudio->terminal != ApiAudioTerminal::COMPLETED)) {
				setStatus( {IDS_API_BUSY}) ;
				return ;
			}
			value.source = found->result ;
			value.key.historyId = sourceId ; // SHAは同じworkerで計算
			value.record = found->apiText ;
			found->pendingCompareSource.reset() ; // 新しい明示要求へ旧完了予約を渡しません
			found->pendingCompareCandidate.reset() ;
			apiTextWindow_.setTargetName( found->path.filename().native()) ;
		}
		// 同じcallbacksを二画面で共有し、送信中はcontrollerが凍結します
		const auto open = [&]( auto submit, auto cancel, auto catalogSubmit)
		{
			// 履歴0件でもsettings要求に本文を追加しません
			if( local) {
				apiTextWindow_.initialize( submit, cancel, catalogSubmit) ;
			} else {
				const auto operation = apiTextWindow_.direct( value, ApiTextAction::IMPROVE) ; // 画面0
				// 受付後だけ通常進捗と取消しを表示します
				if( operation) {
					parakeetFollowup_.cancel( sourceId) ; // 手動/後続の先行受付で全意図を消費
					apiStatusHistory_ = sourceId ;
					apiStatusSource_ = value.source ;
					apiClock_.begin( sourceId, operation, value.source, progressMonotonicMs()) ;
					apiStatus_ = {IDS_API_PREPARING} ;
					apiStatus_.api = ApiTextStatusView{} ;
					apiClock_.decorate( *apiStatus_.api) ;
					setStatus( apiStatus_) ;
					refreshHistory() ;
					showCard() ;
				}
			}
		} ;
		open(
			//------------------------------------------------------------------------------
			/**
			 * @brief 同じ単一workerへ明示操作をenqueueします
			 * @param[in] api 専用要求
			 * @return 受付ID
			 */
			//------------------------------------------------------------------------------
			[this]( ApiTextCorrectionJobRequest api)
			{
				if( closing_ || ((api.action == ApiTextAction::SEND || api.action == ApiTextAction::IMPROVE) &&
								(historyDirty_ || historyFailed_))) {
					return std::uint64_t{0} ;
				}
				JobRequest job ;
				job.payload = std::move( api) ; // 同じ有界queue
				const auto id = controller_->submit( std::move( job)) ; // 単調operation
				// 正常受付のIDだけを次回floorへ反映します
				if( id) {
					nextOperationId_ = std::max( nextOperationId_, id + 1) ;
				}
				return id ;
			},
			//------------------------------------------------------------------------------
			/**
			 * @brief 自分のoperationだけ取消します
			 * @param[in] id 受付ID
			 */
			//------------------------------------------------------------------------------
			[this]( std::uint64_t id)
			{
				controller_->cancel( id) ;
			},
			//------------------------------------------------------------------------------
			/**
			 * @brief 本文なしmetadataを同workerへ渡します
			 * @param[in] catalog 明示要求
			 * @return 単一queueが発行した受付operation ID。close中または受付拒否なら0です。metadata取得や保存の完了を示しません
			 */
			//------------------------------------------------------------------------------
			[this]( ApiCatalogJobRequest catalog)
			{
				// 終了後の取得を開始しません
				if( closing_) {
					return std::uint64_t{0} ;
				}
				JobRequest job ; // 別DTOで原文/pathを含めません
				job.payload = catalog ;
				const auto id = controller_->submit( std::move( job)) ; // 同じbounded queue
				// 次回起動でも単調operationを維持します
				if( id) {
					nextOperationId_ = std::max( nextOperationId_, id + 1) ;
				}
				return id ;
			}) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 音声履歴の対象行へ再実行用の標準popup menuを表示します
	 * @param[in] position 右クリックのscreen座標。両方-1はkeyboardからの要求
	 * @note menuの追跡中もworker通知を回収し、確定後はjob IDを再照合します
	 */
	//------------------------------------------------------------------------------
	void showHistoryMenu( POINT position)
	{
		// 終了中と空の履歴には操作対象を作らない
		if( closing_ || !historyReady_ || cards_.empty()) {
			return ;
		}
		const HWND list = control( IDC_HISTORY) ; // 親Windowが所有する履歴control
		const bool keyboard = position.x == -1 && position.y == -1 ; // keyboardからの要求か
		int		   item = ListView_GetNextItem( list, -1, LVNI_SELECTED) ; // 実際の選択行、未選択-1
		RECT	   row{} ; // 対象行のclient矩形
		// マウス操作では現在の選択と独立に、実際に右クリックした行を求める
		if( !keyboard) {
			POINT local = position ; // hit test用のclient座標
			// 座標変換失敗を先頭行への操作にしない
			if( !ScreenToClient( list, &local)) {
				return ;
			}
			LVHITTESTINFO hit{} ; // native reportの実行hit test
			hit.pt = local ;
			item = ListView_HitTest( list, &hit) ;
			// report余白とheaderは既存選択行へ読み替えません
			if( item < 0 || !(hit.flags & LVHT_ONITEM) || static_cast<std::size_t>( item) >= cards_.size()) {
				return ;
			}
		}
		// keyboard操作でも未選択や古いindexから再実行しない
		if( item < 0 || static_cast<std::size_t>( item) >= cards_.size()) {
			return ;
		}
		refreshingHistory_ = true ; // 選択を一度の所有操作として確定
		ListView_SetItemState( list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED) ;
		ListView_SetItemState( list, item, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED) ;
		refreshingHistory_ = false ;
		// 自所有hidden試験でOSのfocusを操作しません
#if !defined(KOEFUDE_WINDOW_TESTING)
        SetFocus( list) ;
#endif
        const auto index = static_cast<std::size_t>( item) ; // 検証済みの対象行index
		const auto targetId = historyTableTarget( list, {}, true) ; // 実item data
		// list/model更新の不一致を別行へ適用しません
		if( !targetId || cards_[index].id != targetId) {
			return ;
		}
		// 別の行を対象にした場合だけ表示を変更し、同じ行の入力中の名前を保持する
		if( selected_ != index) {
			selected_ = index ;
			showCard() ;
		}
		// Shift+F10等は選択行のそばへmenuを配置する
		if( keyboard) {
			// 選択位置を取得できない場合は不正なscreen座標へmenuを出さない
			if( !ListView_GetItemRect( list, item, &row, LVIR_BOUNDS)) {
				return ;
			}
			position = {row.left + 12, row.bottom} ;
			// 負座標のmonitorにも対応するscreen座標へ変換する
			if( !ClientToScreen( list, &position)) {
				return ;
			}
		}
		const auto			target = rowTarget( targetId) ; // 同じsource/表示版/opをcapture
		const std::uint64_t sourceId = targetId ; // modal追跡中も変化しない照合ID
		const UINT			blocked = retranscribeBlocked( cards_[index].path) ; // menuを開く時の受付条件
		// この同期境界で所有する値、または確定状態の検証値です
		const auto			label = resource( blocked == 0 ? IDS_RETRANSCRIBE : blocked) ; // 現UI言語の操作名
		const std::unique_ptr<std::remove_pointer_t<HMENU>, decltype(&DestroyMenu)> menu(
			CreatePopupMenu(), DestroyMenu) ; // 全終了経路で解放する所有popup
		// 作成・登録失敗を操作ができたものとして扱わない
		if( !menu || !AppendMenuW( menu.get(), MF_STRING | (blocked == 0 ? MF_ENABLED : MF_GRAYED), IDC_RETRANSCRIBE,
								 label.c_str())) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		// この同期境界で所有する値、または確定状態の検証値です
		const bool removable = !(apiTextWindow_.busy() && apiTextWindow_.historyId() == sourceId) &&
							   !cards_[index].busy && !cards_[index].reading.active &&
							   !cards_[index].wholeText.active ; // 全終端後
		// 処理完了の有無に対応した履歴削除menuの表示文言
		const auto removeLabel = resource( removable ? IDS_HISTORY_REMOVE : IDS_HISTORY_REMOVE_BUSY) ;
		const bool canApi =
			apiTextWindow_.ready() &&
			(!cards_[index].apiAudio || cards_[index].apiAudio->terminal == ApiAudioTerminal::COMPLETED) &&
			!cards_[index].apiText.saveFailed && historyReady_ && !historyDirty_ && !historyFailed_ &&
			cards_[index].result && !cards_[index].result->lines.empty() && !cards_[index].busy &&
			!cards_[index].reading.active && !cards_[index].wholeText.active ; // 明示送信前提
		const bool ready = resolveRow( target, true) < cards_.size() ; // 同じ準備済み本文
		const bool rename = cards_[index].result && cards_[index].result->speakerCount > 0 &&
							!rowOperation( cards_[index]) ; // 未処理sourceを変更しません
		const std::array operations{
			std::tuple{IDC_SAVE, IDS_SAVE, ready}, std::tuple{IDC_COPY, IDS_COPY, ready},
			std::tuple{IDC_CANCEL, IDS_CANCEL, target.operation != 0 || target.automaticWaiting},
			std::tuple{IDC_RENAME, IDS_RENAME_MENU, rename}} ; // 常設buttonから移した操作
		// 四操作の可否を同じtarget snapshotへ固定します
		for( const auto &[id, text, enabled] : operations) {
			// 登録失敗を操作可能として扱いません
			if( !AppendMenuW( menu.get(), MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED), id, resource( text).c_str())) {
				throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
			}
		}
		// API操作も追跡開始時の同じhistory IDへ固定します
		if( !AppendMenuW( menu.get(), MF_STRING | (canApi ? MF_ENABLED : MF_GRAYED), IDC_API_TEXT_OPEN,
						resource( IDS_API_OPEN).c_str())) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		// 部分終端だけへ同じ行の明示再開を追加します
		if( target.audioResume) {
			const bool enabled =
				historyReady_ && !historyDirty_ && !historyFailed_ && !target.operation && apiTextWindow_.audioReady() ;
			if( !AppendMenuW( menu.get(), MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED), IDC_AUDIO_RESUME,
							resource( IDS_AUDIO_RESUME).c_str())) {
				throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
			}
		}
		// 取得済みprefixのPC内処理にキー/価格の可否を持ち込まない
		if( target.projectionTarget) {
			// 同じ行の現在状態に基づくmenu可否
			const bool enabled =
				historyReady_ && !historyDirty_ && !historyFailed_ && !speakerTextCorrupt_ && !target.operation ;
			// 標準menuの追加失敗を無言で無視しない
			if( !AppendMenuW( menu.get(), MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED), IDC_SPEAKER_TEXT,
							resource( IDS_SPEAKER_TEXT_MENU).c_str())) {
				throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
			}
		}
		// 採用済み投影がある行だけに明示TXT操作を置く
		if( cards_[index].speakerText.adopted) {
			// 明示保存menuの構築失敗も既存UI診断へ返す
			if( !AppendMenuW( menu.get(), MF_STRING | (ready ? MF_ENABLED : MF_GRAYED), IDC_SPEAKER_TEXT_SAVE,
							resource( IDS_SPEAKER_TEXT_SAVE).c_str())) {
				throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
			}
		}
		// 保存失敗時だけ同じ結果のHTTP0再保存を出します
		if( cards_[index].apiText.saveFailed && !apiTextWindow_.busy()) {
			// menu登録失敗を操作可能表示へ変換しません
			if( !AppendMenuW( menu.get(), MF_STRING, IDC_API_RETRY, resource( IDS_API_MORE_RETRY).c_str())) {
				throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
			}
		}
		// 既存の再実行と分けて、履歴だけの削除操作を追加する
		if( !AppendMenuW( menu.get(), MF_SEPARATOR, 0, nullptr) ||
		   !AppendMenuW( menu.get(), MF_STRING | (removable ? MF_ENABLED : MF_GRAYED), IDC_HISTORY_REMOVE,
						removeLabel.c_str())) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		FollowupModalScope modal( followupModalDepth_) ; // popup中の新しい案内を止めます
#if defined(KOEFUDE_WINDOW_TESTING)
        const int chosen = testHooks_.historyMenuSelection ? testHooks_.historyMenuSelection( menu.get(), sourceId) : 0 ;
#else
        const int chosen = TrackPopupMenuEx( menu.get(), TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, position.x,
											position.y, window_, nullptr) ; // 取消0
#endif
        invokeRowAction( chosen, target) ; // 追跡後に同じID/source/表示版/opを再照合
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief IDに対応する子controlを取得します
	 * @param[in] id 本Windowに属するcontrol ID
	 * @return Windowが所有する非所有HWND
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] HWND control( const int id) const noexcept
	{
		return GetDlgItem( window_, id) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 短いresource案内をmodal dialogへ表示します
	 * @param[in] id 表示するresource
	 */
	//------------------------------------------------------------------------------
	void showMessage( const UINT id) const
	{
		static_cast<void>( showPrompt( id, MB_OK)) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 処理の発生箇所で一終端一度だけ予約します
	 * @param[in] kind 用途
	 * @param[in] operation 同じ用途のop/revision
	 * @param[in] status 詳細DTO
	 * @param[in,out] card 行に属する時だけ現在のカード。本文をcopyしません
	 * @param[in] historyError 履歴用途の終端通知へ保持するHistoryErrorKind。既定はINTERNALで、他用途の本文や診断を追加しません
	 */
	//------------------------------------------------------------------------------
	void queueStatusNotice( StatusNoticeKind kind, std::uint64_t operation, const UiStatus &status,
						   FileCard *card = nullptr, HistoryErrorKind historyError = HistoryErrorKind::INTERNAL)
	{
		if( closing_ || !operation) {
			return ;
		}
		auto &previous = (card ? card->noticeOperations : noticeOperations_)[static_cast<std::size_t>( kind)] ;
		if( operation <= previous) {
			return ;
		}
		previous = operation ; // modalより先に消費し、再入/再配送でも復活させません
		StatusNotice notice{kind, operation, card ? card->id : 0, card ? card->result : nullptr, status, historyError} ;
		// 各行/用途の最新通知だけを保持し、UI停滞でも有限にします
		const auto	 found = std::find_if( statusNotices_.begin(), statusNotices_.end(),
										  [&notice]( const auto &value)
										  {
											  return value.kind == notice.kind && value.history == notice.history ;
										  }) ;
		if( found != statusNotices_.end()) {
			*found = std::move( notice) ;
		} else if( statusNotices_.size() < 64) {
			statusNotices_.push_back( std::move( notice)) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief pollのiterator解放後に詳細を一度だけ表示します。表示/進捗からは呼びません
	 */
	//------------------------------------------------------------------------------
	void pumpStatusNotice()
	{
		if( closing_ || !window_ || !IsWindow( window_)) {
			statusNotices_.clear() ;
			return ;
		}
		if( followupModalDepth_ || !IsWindowEnabled( window_) || settingsWindow_.opened() ||
		   preparationWindow_.opened() || wholeTextPreparationWindow_.opened()) {
			return ;
		}
		while( !statusNotices_.empty()) {
			auto notice = std::move( statusNotices_.front()) ; // modalを跨ぐ所有DTOだけ
			statusNotices_.erase( statusNotices_.begin()) ;
			if( notice.history) {
				const auto found = std::find_if( cards_.begin(), cards_.end(),
												[&notice]( const auto &card)
												{
													return card.id == notice.history ;
												}) ;
				// 削除/再認識/新しい同用途opには前ownerの通知を移しません
				if( found == cards_.end() || found->busy || found->reading.active || found->wholeText.active ||
				   (apiTextWindow_.busy() && apiTextWindow_.historyId() == found->id) ||
				   found->result != notice.source || found->result.owner_before( notice.source) ||
				   notice.source.owner_before( found->result) ||
				   found->noticeOperations[static_cast<std::size_t>( notice.kind)] != notice.operation) {
					continue ;
				}
			} else if( notice.kind == StatusNoticeKind::UPDATE && (notice.operation != updateEvent_.operation ||
																  notice.status.update->state != updateEvent_.state)) {
				continue ;
			} else if( notice.kind == StatusNoticeKind::HISTORY_SAVE &&
					  (!historyFailed_ || notice.operation != historyRevision_ + 1)) {
				continue ;
			} else if( notice.kind == StatusNoticeKind::HISTORY_LOAD && historyReady_) {
				continue ;
			}
			auto text = notice.kind == StatusNoticeKind::HISTORY_LOAD || notice.kind == StatusNoticeKind::HISTORY_SAVE
							? historyErrorText( notice.historyError, notice.status.code,
											   notice.kind == StatusNoticeKind::HISTORY_LOAD)
							: notice.status.text() ;
			if( notice.history) {
				text = L"#" + std::to_wstring( notice.history) + L"\r\n\r\n" + text ;
			}
			FollowupModalScope modal( followupModalDepth_) ; // nested pollでは新通知を出しません
#if defined(KOEFUDE_WINDOW_TESTING)
            ++testHooks_.statusNotices ;
			testHooks_.lastStatusNotice = text ;
			if( testHooks_.statusNotice) {
				testHooks_.statusNotice() ;
			}
#else
            showMessageDialog( window_, text.c_str(), resource( IDS_TITLE).c_str(), MB_OK | MB_ICONINFORMATION) ;
#endif
            return ; // 一回のpollで一件だけ。確認dialogと同時に重ねません
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 案内を表示し、専用試験では画面を出さず応答を記録します
	 * @param[in] id 案内のresource ID
	 * @param[in] flags MessageBoxのbutton・icon・既定応答
	 * @return OSの応答、専用試験では固定した応答値
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] int showPrompt( const UINT id, const UINT flags) const
	{
		FollowupModalScope modal( followupModalDepth_) ; // nested pollでは新しい案内を出しません
#if defined(KOEFUDE_WINDOW_TESTING)
        static_cast<void>( flags) ;
		++testHooks_.notices ;
		testHooks_.lastNotice = id ;
		return testHooks_.promptResult ;
#else
        return showMessageDialog( window_, resource( id).c_str(), resource( IDS_TITLE).c_str(), flags) ;
#endif
    }
	//------------------------------------------------------------------------------
	/**
	 * @brief callbackで捕捉した例外を説明し、専用試験では診断値だけを保持します
	 * @param[in] kind 元の失敗分類
	 * @param[in] code 元のAPI診断値
	 * @param[in] unexpected trueは非定型の内部例外
	 * @note OS callbackから例外を漏らしません
	 */
	//------------------------------------------------------------------------------
	void showCallbackError( const ErrorKind kind, const std::uint32_t code,
						   // この同期境界で所有する値、または確定状態の検証値です
						   const bool unexpected = false) const noexcept
	{
#if defined(KOEFUDE_WINDOW_TESTING)
        static_cast<void>( unexpected) ;
		++testHooks_.errors ;
		testHooks_.lastError = kind ;
		testHooks_.lastCode = code ;
#else
        try {
			// 非定型例外には内部情報を含めず、従来の固定説明を表示する
			const int shown = unexpected ? showMessageDialog( window_, resource( IDS_INTERNAL_ERROR).c_str(), L"KoeFude",
															 MB_OK | MB_ICONERROR)
										 : showMessageDialog( window_, errorText( kind, code).c_str(),
															 resource( IDS_ERROR).c_str(), MB_OK | MB_ICONERROR) ;
			// 共通dialogの確保失敗も固定bufferによる最終通知へ落とします
			if( shown) {
				return ;
			}
		} catch( ...) {
			// 文字列確保失敗も同じallocation不要の最終通知へ進めます
		}
		std::array<wchar_t, 256> fallback{} ; // allocationなしで読む既存resource
		LoadStringW( GetModuleHandleW( nullptr), IDS_INTERNAL_ERROR, fallback.data(), static_cast<int>( fallback.size())) ;
		MessageBoxW( window_, fallback.data(), L"KoeFude", MB_OK | MB_ICONERROR) ;

#endif
    }
	//------------------------------------------------------------------------------
	/**
	 * @brief 標準controlを作り、所有Windowへ関連付けます
	 * @param[in] className OSのcontrol class
	 * @param[in] id 操作識別子
	 * @param[in] text 表示resource ID。0は空
	 * @param[in] style 追加style
	 * @return Windowが所有するcontrol handle
	 */
	//------------------------------------------------------------------------------
	HWND makeControl( const wchar_t *className, const int id, const UINT text, const DWORD style)
	{
		// Win32のchild IDをHMENU引数へ符号幅を維持して渡すAPI境界
		const DWORD extended = id == IDC_CHAT || id == IDC_CHAT_COMPARE || id == IDC_HISTORY ? WS_EX_CLIENTEDGE : 0 ;
		// 親Windowに所有させるchild。作成失敗はNULL
		const HWND	child = CreateWindowExW(
			 extended, className, text ? resource( text).c_str() : L"", WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1,
			 window_, reinterpret_cast<HMENU>( static_cast<INT_PTR>( id)), GetModuleHandleW( nullptr), nullptr) ;
		// 標準control作成失敗を空handleのまま後続へ流さない
		if( !child) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		children_.push_back( child) ;
		// 本文以外の固定labelを言語切替時に同じresource IDから更新する
		if( text != 0 && id != IDC_CHAT) {
			textControls_.emplace( id, text) ;
		}
		return child ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 標準controlとworkerを初期化します
	 */
	//------------------------------------------------------------------------------
	void initialize()
	{
		title_ = makeControl( L"STATIC", 2001, IDS_FILES, 0) ;
		const auto splitter = makeControl( L"STATIC", IDC_MAIN_SPLITTER, 0,
										  WS_TABSTOP | SS_NOTIFY) ; // 狭いnative境界
		// selfはchildの破棄まで存続し、NCDESTROYでsubclass参照を外します
		if( !SetWindowSubclass( splitter, splitterSubclass, 1, reinterpret_cast<DWORD_PTR>( this))) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		makeControl( L"STATIC", IDC_FILTER_LABEL, IDS_FILTER_LABEL, 0) ;
		makeControl( L"COMBOBOX", IDC_FILTER, 0, WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL) ;
		makeControl( L"STATIC", IDC_EXPORT_SCOPE, IDS_EXPORT_SCOPE, 0) ;
		makeControl( L"STATIC", IDC_READING_LABEL, IDS_READING_LABEL, 0) ;
		makeControl( L"COMBOBOX", IDC_READING_VIEW, 0, WS_TABSTOP | CBS_DROPDOWNLIST) ;
		const auto table = makeControl( WC_LISTVIEWW, IDC_HISTORY, 0,
									   WS_TABSTOP | WS_VSCROLL | LVS_REPORT | LVS_SINGLESEL |
										   LVS_SHOWSELALWAYS) ; // 1fileを1行で表示する標準report
		ListView_SetExtendedListViewStyle( table, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP |
													 LVS_EX_INFOTIP) ;
		const std::array columns{IDS_TABLE_NAME, IDS_TABLE_DURATION, IDS_TABLE_STATE, IDS_TABLE_CORRECTION,
								 IDS_TABLE_MODEL} ; // 有限5列
		const int		 dpi = static_cast<int>( GetDpiForWindow( window_)) ; // 現DPI
		columnsDpi_ = dpi ;
		// 最初だけ列幅を設定し、その後の利用者の列幅調整を保持します
		for( std::size_t i = 0; i < columns.size(); ++i) {
			auto	  text = resource( columns[i]) ; // controlが同期copy
			LVCOLUMNW column{} ; // Unicode列
			column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT ;
			column.fmt = i == 2 || i == 3 ? LVCFMT_CENTER : LVCFMT_LEFT ; // 状態だけ中央寄せ
			column.pszText = text.data() ;
			column.cx = paneScale( K_HISTORY_COLUMN_WIDTHS[i], 96, dpi) ;
			column.iSubItem = static_cast<int>( i) ;
			ListView_InsertColumn( table, static_cast<int>( i), &column) ;
		}
		ListView_SetView( table, LV_VIEW_DETAILS) ; // 標準font相応の一行table
		chat_ = makeControl( MSFTEDIT_CLASS, IDC_CHAT, IDS_HINT, WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY) ;
		makeControl( L"STATIC", IDC_COMPARE_LEFT_LABEL, IDS_API_COMPARE_ORIGINAL, 0) ;
		makeControl( L"STATIC", IDC_COMPARE_RIGHT_LABEL, IDS_API_COMPARE_CORRECTED, 0) ;
		ShowWindow( control( IDC_COMPARE_LEFT_LABEL), SW_HIDE) ;
		ShowWindow( control( IDC_COMPARE_RIGHT_LABEL), SW_HIDE) ;
		status_ = makeControl( STATUSCLASSNAMEW, 2003, 0, SBARS_SIZEGRIP) ;
		setStatus( {IDS_READY}) ;
		// 標準status textを維持したまま帯全体へ進捗を重ねる
		if( !attachStatusProgress( status_, statusProgress_)) {
			throw OperationError( ErrorKind::INTERNAL) ;
		}
		SendMessageW( chat_, EM_EXLIMITTEXT, 0, 32 * K_MAXIMUM_TEXT_BYTES) ;
		SendMessageW( chat_, EM_SETBKGNDCOLOR, 0, GetSysColor( COLOR_WINDOW)) ;
		DragAcceptFiles( window_, TRUE) ;
		DragAcceptFiles( chat_, TRUE) ;
		// dropを親へ渡すsubclassがなければ初期化失敗とする
		if( !SetWindowSubclass( chat_, chatSubclass, 1, 0)) {
			throw OperationError( ErrorKind::INTERNAL) ;
		}
		// 滑らかなscrollも同じUI threadで動かし、workerの完了を待機させない
		if( !attachSmoothScroll( chat_, smoothScroll_)) {
			throw OperationError( ErrorKind::INTERNAL) ;
		}
		startUpdateController() ;
#if !defined(KOEFUDE_WINDOW_TESTING)
        appMutex_.reset( CreateMutexW( nullptr, FALSE,
									 L"KoeFude-App-6B3A1531-0794-4E2A-A6B3-DCC78F5D79F4")) ; // Innoの実行中保護
		// mutex保護なしに同じ導入先の更新を許可しません
		if( !appMutex_) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
#endif
        startTranscriptionSettings() ;
		startHistoryController() ;
		startJobController() ;
		startDocumentController() ;
		startModelController() ;
		// timerが作れない場合は終了通知を取りこぼせるためWindowを成立させない
		if( !SetTimer( window_, K_POLL_TIMER, 100, nullptr)) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		applyUiLanguage( uiLanguage(), false) ;
		updateFonts() ;
		accelerators_ = LoadAcceleratorsW( GetModuleHandleW( nullptr), MAKEINTRESOURCEW(IDR_MAIN_ACCEL)) ;
		// 標準file選択のkeyboard導線が欠けた状態で起動しません
		if( !accelerators_) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		layout() ;
		showCard() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief comboへ所有copyされる選択肢を追加します
	 * @param[in] id comboのID
	 * @param[in] text 呼出し中有効な表示文字列
	 */
	//------------------------------------------------------------------------------
	void addChoice( const int id, const std::wstring &text)
	{
		SendMessageW( control( id), CB_ADDSTRING, 0,
					 reinterpret_cast<LPARAM>( text.c_str())) ; // comboが文字列をcopyする境界
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 本文を変更せず、閲覧欄と境界の目的名を現UI言語へ更新します
	 */
	//------------------------------------------------------------------------------
	void refreshAccessibleNames()
	{
		const std::array<std::pair<int, UINT>, 4> names{
			{{IDC_HISTORY, IDS_FILES},
			 {IDC_CHAT, IDS_CONVERSATION},
			 {IDC_READING_VIEW, IDS_READING_LABEL},
			 {IDC_MAIN_SPLITTER, IDS_MAIN_SPLITTER}}} ; // 閲覧controlと境界の目的
		// 同じ四つのHWNDへ翻訳を更新し、設定値を増やさない
		for( const auto &[id, text_id] : names) {
			// 現在の翻訳名をcontrolへ設定したHRESULT
			const HRESULT result = accessibility_.setName( control( id), resource( text_id)) ;
			// COM・容量・threadの失敗を既存のUI境界で診断へ変換する
			if( FAILED(result)) {
				throw OperationError( ErrorKind::INTERNAL, static_cast<std::uint32_t>( result)) ;
			}
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 操作値を保ったまま日本語UI全体を更新し、旧English要求を無視します
	 * @param[in] language 日本語だけを受理する旧互換引数
	 * @param[in] persist trueは利用者の明示選択で、UI言語だけを保存します
	 */
	//------------------------------------------------------------------------------
	void applyUiLanguage( const UiLanguage language, const bool persist)
	{
		// 内部の旧言語要求も日本語専用の製品UIを切り替えません
		if( language != UiLanguage::JAPANESE) {
			return ;
		}
		const HMENU next = loadUiMenu( language) ; // 成功後は親Windowへ所有権を移すmenu
		// resource不足の場合は現在のmenuと言語を保つ
		if( !next) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		const HMENU old = GetMenu( window_) ; // 差替え成功後だけ解放する旧menu
		// SetMenu失敗時は新menuの所有権を回収する
		if( !SetMenu( window_, next)) {
			const DWORD code = GetLastError() ; // cleanup前の診断値
			DestroyMenu( next) ;
			throw OperationError( ErrorKind::INTERNAL, code) ;
		}
		// Windowから外れたmenuだけを解放する
		if( old) {
			DestroyMenu( old) ;
		}
		static_cast<void>( setUiLanguage( language)) ;
		refreshAccessibleNames() ;
		const std::array columns{IDS_TABLE_NAME, IDS_TABLE_DURATION, IDS_TABLE_STATE, IDS_TABLE_CORRECTION,
								 IDS_TABLE_MODEL} ; // 可視header5列
		// 表示言語だけを更新し、利用者の列幅は変更しません
		for( std::size_t index = 0; index < columns.size(); ++index) {
			auto	  label = resource( columns[index]) ; // 同期copy
			LVCOLUMNW column{} ; // text以外の列属性を保持
			column.mask = LVCF_TEXT ;
			column.pszText = label.data() ;
			ListView_SetColumn( control( IDC_HISTORY), static_cast<int>( index), &column) ;
		}
		// 固定labelは本文や利用者の編集文字列と分けて翻訳する
		for( const auto &[id, text_id] : textControls_) {
			SetWindowTextW( control( id), resource( text_id).c_str()) ;
		}
		refreshAsrControls() ; // Apply済み構成だけを準備workerへ照合
		setStatus( currentStatus_) ;
		showCard() ;
		DrawMenuBar( window_) ;
		layout() ;
		// 起動時の読取りは保存操作と扱わず、明示変更だけを永続化する
		if( persist) {
#if defined(KOEFUDE_WINDOW_TESTING)
            const LSTATUS saved = testHooks_.uiLanguageSaveResult ; // 本人HKCUなしの専用seam
#else
            const LSTATUS saved = saveUiLanguage( language) ; // UI言語のみの保存結果
#endif
            // 保存失敗でも今回の表示変更は維持し、再起動後へ残せないことを伝える
			if( saved != ERROR_SUCCESS) {
				// 今回の表示言語を永続化できないこととOS診断を示す所有文字列
				const auto text = resource( IDS_LANGUAGE_SAVE_ERROR) + L"\r\n[" + std::to_wstring( saved) + L"]" ;
				showMessageDialog( window_, text.c_str(), resource( IDS_TITLE).c_str(), MB_OK | MB_ICONWARNING) ;
			}
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 選択fileの話者IDと表示名から表示filterの候補を更新します
	 */
	//------------------------------------------------------------------------------
	void refreshFilter()
	{
		SendMessageW( control( IDC_FILTER), CB_RESETCONTENT, 0, 0) ;
		// この同期境界で所有する値、または確定状態の検証値です
		const bool available = selected_ < cards_.size() && cards_[selected_].result != nullptr ; // 完了結果の有無
		// この同期境界で所有する値、または確定状態の検証値です
		const int  selected = available ? cards_[selected_].filter : K_ALL_SPEAKERS ; // file単位の保持値
		const auto add =
			//------------------------------------------------------------------------------
			/**
			 * @brief 候補の表示名と安定した話者IDをcomboへ登録します
			 * @param[in] value 話者IDまたは全話者・不明・混在の専用値
			 * @param[in] text 同期APIがcopyする候補名
			 */
			//------------------------------------------------------------------------------
			[this, selected]( const int value, const std::wstring &text)
			{
				// この同期境界で所有する値、または確定状態の検証値です
				const auto item = SendMessageW( control( IDC_FILTER), CB_ADDSTRING, 0,
											   reinterpret_cast<LPARAM>( text.c_str())) ; // comboがcopyする候補
				SendMessageW( control( IDC_FILTER), CB_SETITEMDATA, static_cast<WPARAM>( item), value) ;
				// 表示名変更後も元の話者IDを選択し続ける
				if( selected == value) {
					SendMessageW( control( IDC_FILTER), CB_SETCURSEL, static_cast<WPARAM>( item), 0) ;
				}
			} ; // 各候補へ安定したIDを関連付ける同期操作
		add( K_ALL_SPEAKERS, resource( IDS_FILTER_ALL)) ;
		// 結果がない間は架空の個別話者を表示しない
		if( available) {
			const auto &transcript = *cards_[selected_].result ; // UI所有snapshotのborrow
			// 初出順の番号を維持し、利用者の表示名も候補へ添える
			for( int speaker = 1; speaker <= transcript.speakerCount; ++speaker) {
				// この同期境界で所有する値、または確定状態の検証値です
				std::wstring label = resource( IDS_SPEAKER) + std::to_wstring( speaker) ; // 元話者番号
				const auto	 name = transcript.speakerNames.find( speaker) ; // 明示編集された名前
				// 名前がある場合もIDを残し、同名の候補を区別する
				if( name != transcript.speakerNames.end()) {
					label += L" · " + toWide( name->second) ;
				}
				add( speaker, label) ;
			}
		}
		add( K_UNKNOWN_SPEAKERS, resource( IDS_UNKNOWN)) ;
		add( K_MIXED_SPEAKERS, resource( IDS_MIXED)) ;
		EnableWindow( control( IDC_FILTER), available && historyReady_ && !closing_) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 選択モデルの品質候補と日本語固定を同じregistryから反映します
	 * @param[in] reason 依存検査の入口。省略は既存再表示
	 */
	//------------------------------------------------------------------------------
	void refreshAsrControls( const LocalDiagnosticReason reason = LocalDiagnosticReason::REFRESH)
	{
		// 架空の品質へ切り替わらないよう既存registryを確認します
		if( !findAsrDefinition( asrModel_, asrProfile_)) {
			asrProfile_ = AsrProfile::QUALITY ;
		}
		const auto &definition = asrDefinition( asrModel_, asrProfile_) ; // 適用済み構成
		cpuOnlyAsr_ = definition.engine == AsrEngine::PARAKEET_CTC && (!KOEFUDE_HAS_CUDA || !definition.gpuValidated) ;
		requestModelCheck( false, reason) ;
		lockModelControls() ;
		refreshModelPreparation() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief DPIに合うfontへ全controlを更新します
	 */
	//------------------------------------------------------------------------------
	void updateFonts()
	{
		const UINT	dpi = GetDpiForWindow( window_) ; // 現在monitorの論理DPI
		// この同期境界で所有する値、または確定状態の検証値です
		const HFONT next =
			CreateFontW( -MulDiv( 11, static_cast<int>( dpi), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
						SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH,
						L"ＭＳ ゴシック") ; // 利用者指定の読みやすい所有font
		// GDI確保失敗時は現在のfontを維持する
		if( !next) {
			return ;
		}
		// controlはfontを所有しないため、差替え完了後に旧fontを削除する
		for( const HWND child : children_) {
			SendMessageW( child, WM_SETFONT, reinterpret_cast<WPARAM>( next), TRUE) ;
		}
		DeleteObject( font_) ;
		font_ = next ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 現在の親client論理幅を返します
	 * @return 96DPIの非負幅
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] int paneClientWidth() const noexcept
	{
		RECT area{} ; // 自Windowの矩形値だけを読取る
		GetClientRect( window_, &area) ;
		return paneScale( area.right, GetDpiForWindow( window_), 96) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 人手dragまたは内部試験の開始を受け付けます
	 * @param[in] x 親clientの96DPI基準x
	 */
	//------------------------------------------------------------------------------
	void beginSplitter( const int x)
	{
		// 停止中/最小化/二重beginではcaptureや基準幅を変更しません
		if( closing_ || paneMinimized_ || IsIconic( window_) || splitter_.dragging) {
			return ;
		}
		splitter_.begin( x, paneClientWidth()) ;
#if !defined(KOEFUDE_WINDOW_TESTING)
        const auto child = control( IDC_MAIN_SPLITTER) ; // 自所有境界だけ
		SetCapture( child) ; // 人手mouse downの製品経路専用
		// captureを取得できない場合は開始前へ戻し、押下を継続したことにしません
		if( GetCapture() != child) {
			finishSplitter( true) ;
			return ;
		}
		SetFocus( child) ; // keyboardによるEscと矢印操作の対象
#endif
        InvalidateRect( control( IDC_MAIN_SPLITTER), nullptr, TRUE) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 有効dragの最新幅だけを配置します
	 * @param[in] x 親client論理x
	 */
	//------------------------------------------------------------------------------
	void moveSplitter( const int x)
	{
		// capture終了後の遅延通知から余分なRTF要求を作りません
		if( !splitter_.dragging || closing_) {
			return ;
		}
		splitter_.update( x, paneClientWidth()) ;
		layout() ; // drag中はgeometryだけ、document workerへの投入なし
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief captureを解放し最新幅で一回だけ文書を整えます
	 * @param[in] cancel trueは開始前の希望幅へ戻す
	 * @param[in] refresh falseはclose/DPI/最小化側で後の配置を担当する
	 */
	//------------------------------------------------------------------------------
	void finishSplitter( const bool cancel, const bool refresh = true)
	{
		// 先にstateを終端にし、ReleaseCaptureによるcapture-lost再入を無害にします
		if( !splitter_.end( cancel)) {
			return ;
		}
#if !defined(KOEFUDE_WINDOW_TESTING)
        // 他のcontrol/processへ移ったcaptureは解除しません
		if( GetCapture() == control( IDC_MAIN_SPLITTER)) {
			ReleaseCapture() ;
		}
#endif
        // close/DPI/最小化では呼出し側の最新配置まで再生成を保留します
		if( refresh && !closing_) {
			layout() ;
		}
		InvalidateRect( control( IDC_MAIN_SPLITTER), nullptr, TRUE) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 人手の左右矢印を小幅の調整へ変換します
	 * @param[in] delta 96DPIの符号付き移動量
	 */
	//------------------------------------------------------------------------------
	void stepSplitter( const int delta)
	{
		// keyboard調整でmouse captureを取り直さず、停止中も開始しません
		if( closing_ || paneMinimized_ || IsIconic( window_)) {
			return ;
		}
		finishSplitter( false, false) ;
		splitter_.begin( 0, paneClientWidth()) ;
		moveSplitter( delta) ;
		finishSplitter( false) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief DPI変更で現在の列幅だけを一度換算します
	 * @param[in] dpi 新DPI。通常resize/言語変更/dragは呼びません
	 */
	//------------------------------------------------------------------------------
	void scaleHistoryColumns( const int dpi)
	{
		// 同じDPIの重複通知で丸めを累積しません
		if( dpi == columnsDpi_) {
			return ;
		}
		// 5列の利用者調整済みpixel幅を旧→新DPI比で換算します
		for( int index = 0; index < static_cast<int>( K_HISTORY_COLUMN_WIDTHS.size()); ++index) {
			const int width = ListView_GetColumnWidth( control( IDC_HISTORY), index) ; // 現pixel幅
			ListView_SetColumnWidth( control( IDC_HISTORY), index, paneScale( width, columnsDpi_, dpi)) ;
		}
		columnsDpi_ = dpi ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 自所有境界の人手mouse/keyboardと標準描画を処理します
	 * @param[in] child 自境界の非所有HWND
	 * @param[in] message OS message
	 * @param[in] wparam message値
	 * @param[in] lparam message値
	 * @param[in] id subclass ID
	 * @param[in] reference child破棄まで生存するMainWindow
	 * @return 処理済み値または標準subclass結果
	 * @note 試験はこのmouse/key通知を注入せずdirect内部APIだけを使います
	 */
	//------------------------------------------------------------------------------
	static LRESULT CALLBACK splitterSubclass( HWND child, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id,
											 DWORD_PTR reference)
	{
		// SetWindowSubclassが同じUI thread/child寿命だけ保持する自己参照
		auto *self = reinterpret_cast<MainWindow *>( reference) ; // 所有権移動なし
		try {
			// 境界自身の操作だけを扱い、Tab等は標準controlへ戻します
			switch( message) {
				case WM_LBUTTONDOWN:
				case WM_MOUSEMOVE:
				case WM_LBUTTONUP: { // OSが通知する人手の親client座標へ変換
					POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)} ; // child基準
					MapWindowPoints( child, self->window_, &point, 1) ;
					const int x = paneScale( point.x, GetDpiForWindow( child), 96) ; // 論理値
					// 押下は一回開始し、更新と解放は有効dragにだけ適用します
					if( message == WM_LBUTTONDOWN) {
						self->beginSplitter( x) ;
					} else {
						self->moveSplitter( x) ;
						// button-upはcaptureを必ず終端へ進めます
						if( message == WM_LBUTTONUP) {
							self->finishSplitter( false) ;
						}
					}
					return 0 ;
				}
				case WM_CAPTURECHANGED: // 外部へcaptureが移ったら現在幅を確定
					self->finishSplitter( false) ;
					return 0 ;
				case WM_CANCELMODE: // OSの取消しは開始前へ戻す
					self->finishSplitter( true) ;
					return 0 ;
				case WM_SETCURSOR: // 境界上だけの人手resize cursor
#if !defined(KOEFUDE_WINDOW_TESTING)
                    SetCursor( LoadCursorW( nullptr, IDC_SIZEWE)) ;
#endif
                    return TRUE ;
				case WM_GETDLGCODE: // Tab移動を保ったまま左右矢印を受ける
					return DLGC_WANTARROWS ;
				case WM_KEYDOWN: // 自境界にfocusがある人手keyboardだけ
					// 左右は16論理px刻み、Escは開始前へ復帰します
					if( wparam == VK_LEFT || wparam == VK_RIGHT) {
						self->stepSplitter( wparam == VK_LEFT ? -16 : 16) ;
						return 0 ;
					}
					// message loopを通らない通常dispatchにも取消しを適用します
					if( wparam == VK_ESCAPE) {
						self->finishSplitter( true) ;
						return 0 ;
					}
					break ;
				case WM_SETFOCUS:
				case WM_KILLFOCUS: // focus枠を自control内だけ再描画
					InvalidateRect( child, nullptr, TRUE) ;
					break ;
				case WM_PAINT: { // 標準system色の細線とkeyboard focus枠
					PAINTSTRUCT paint{} ; // Begin/EndPaintで対応するOS描画借用
					const auto	dc = BeginPaint( child, &paint) ; // 画面取得ではなく通常paint
					RECT		area{} ; // 自control領域
					GetClientRect( child, &area) ;
					FillRect( dc, &area, GetSysColorBrush( COLOR_BTNFACE)) ;
					RECT line{(area.right - 1) / 2, 0, (area.right - 1) / 2 + 1, area.bottom} ;
					FillRect( dc, &line, GetSysColorBrush( COLOR_3DSHADOW)) ;
					// 自境界focusだけを色以外の標準枠で示します
					if( GetFocus() == child) {
						DrawFocusRect( dc, &area) ;
					}
					EndPaint( child, &paint) ;
					return 0 ;
				}
				case WM_NCDESTROY: // selfの寿命をchild破棄後へ延長しません
					self->finishSplitter( false, false) ;
					RemoveWindowSubclass( child, splitterSubclass, id) ;
					break ;
				default:
					break ; // 標準accessibility/Tab navigationを維持
			}
		} catch( ...) {
			self->finishSplitter( true, false) ;
			self->showCallbackError( ErrorKind::INTERNAL, 0, true) ;
		}
		return DefSubclassProc( child, message, wparam, lparam) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 現在のclient矩形とDPIに応じて標準controlを配置します
	 */
	//------------------------------------------------------------------------------
	void layout()
	{
		// 最小化の0×0へcontrolを重ねず、復元時のWM_SIZEで最新配置へ戻します
		if( paneMinimized_ || IsIconic( window_) || paneLayoutActive_) {
			return ;
		}
		MainPaneRepaint		 repaint( window_, paneLayoutActive_) ; // 再入抑止と旧新regionのRAII
		std::array<POINT, 1> scroll{} ; // 単一文書の配置前のscroll位置
		const std::array	 panesToPlace{chat_} ; // 通常/比較で同じ一つのRichEdit
		// live resizeでRichEditが選択行へscrollし直す前に単一文書の位置を捕捉します
		for( std::size_t index = 0; index < panesToPlace.size(); ++index) {
			SendMessageW( panesToPlace[index], EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>( &scroll[index])) ;
			scroll[index].y = comparisonScrollRange( panesToPlace[index]).position ;
		}
		RECT area{} ; // Windowが所有するclient領域
		GetClientRect( window_, &area) ;
		const int  dpi = static_cast<int>( GetDpiForWindow( window_)) ; // 論理座標変換用DPI
		const int  width = MulDiv( area.right, 96, dpi) ; // 論理client幅
		const int  height = MulDiv( area.bottom, 96, dpi) ; // 論理client高
		const auto place =
			//------------------------------------------------------------------------------
			/**
			 * @brief 全controlへ同じDPI変換を適用して配置します
			 * @param[in] child 親Windowが所有する非NULLのcontrol
			 * @param[in] x 96DPI基準の左端座標
			 * @param[in] y 96DPI基準の上端座標
			 * @param[in] w 96DPI基準の幅。実配置時は1以上に補正します
			 * @param[in] h 96DPI基準の高さ。実配置時は1以上に補正します
			 * @note 戻り値はなく、値で捕捉した現在DPIをUI threadの同期配置に使います
			 */
			//------------------------------------------------------------------------------
			[dpi, &repaint]( HWND child, int x, int y, int w, int h)
			{
				const int  left = MulDiv( x, dpi, 96) ; // 親clientのpixel左端
				const int  top = MulDiv( y, dpi, 96) ; // 親clientのpixel上端
				const RECT next{left, top, left + MulDiv( std::max( 1, w), dpi, 96),
								top + MulDiv( std::max( 1, h), dpi, 96)} ; // 最終pixel配置
				repaint.place( child, next) ;
			} ; // 全controlの中間描画を止め、最終配置だけを同期paintする操作
		SendMessageW( status_, WM_SIZE, 0, 0) ;
		setStatus( currentStatus_) ; // resizeは短い表示幅だけを更新し、通知を生成しません
		RECT statusArea{} ; // status実高さ
		GetWindowRect( status_, &statusArea) ;
		const int  bottom = height - MulDiv( statusArea.bottom - statusArea.top, 96, dpi) ;
		const auto panes = mainPaneLayout( width, splitter_.preferred) ; // 希望幅を壊さない配置
		const int  filesWidth = panes.filesWidth ; // 最低幅と右本文から求めた実幅
		const int  textX = panes.textX ; // 掴める境界の右端
		const int  textWidth = panes.textWidth ; // 右本文領域
		place( title_, panes.filesX, 8, filesWidth, 20) ;
		place( control( IDC_HISTORY), panes.filesX, 32, filesWidth, bottom - 40) ;
		place( control( IDC_MAIN_SPLITTER), panes.splitterX, 8, panes.splitterWidth, bottom - 16) ;
		const int filterWidth = std::max( 1, std::min( readingControlsShown_ ? 168 : 250,
													 textWidth - 152)) ; // 小さいwork areaでも表示comboの幅を残す
		place( control( IDC_FILTER_LABEL), textX + textWidth - filterWidth - 44, 12, 40, 20) ;
		place( control( IDC_FILTER), textX + textWidth - filterWidth, 6, filterWidth, 280) ;
		place( control( IDC_READING_LABEL), textX, 12, 48, 20) ;
		place( control( IDC_READING_VIEW), textX + 52, 6, textWidth - filterWidth - 104, 180) ;
		const bool compare = comparisonActive() ; // 同じsource/保存採用の左右比較だけ
		const int  paneWidth = compare ? (textWidth - 8) / 2 : textWidth ;
		const int  paneY = compare ? 62 : 40 ; // 比較時だけ短い左右label
		const int  scopeHeight = compare ? 38 : 22 ; // TXT/copy対象を本文の下へ明示
		const int  paneHeight = std::max( 40, bottom - paneY - scopeHeight - 8) ;
		place( chat_, textX, paneY, textWidth, paneHeight) ;
		place( control( IDC_COMPARE_LEFT_LABEL), textX, paneY - 22, paneWidth, 20) ;
		place( control( IDC_COMPARE_RIGHT_LABEL), textX + paneWidth + 8, paneY - 22, paneWidth, 20) ;
		place( control( IDC_EXPORT_SCOPE), textX, bottom - scopeHeight - 2, textWidth, scopeHeight) ;
		// 通常/比較で同じRichEditの左右余白をclient矩形へ適用します
		for( std::size_t index = 0; index < panesToPlace.size(); ++index) {
			const auto pane = panesToPlace[index] ; // 当該paneの非所有HWND
			RECT	   margin{} ; // 当該RichEditだけの実client領域
			GetClientRect( pane, &margin) ;
			margin.left = MulDiv( 12, dpi, 96) ;
			margin.right = std::max( margin.left + 1, margin.right - MulDiv( 15, dpi, 96)) ;
			SendMessageW( pane, EM_SETRECTNP, 0,
						 reinterpret_cast<LPARAM>( &margin)) ; // 描画なしcopy
			// 比較の幅変更後は末尾一座標で実rangeを確定し、長文位置を復元します
			if( comparisonShown_) {
				completeComparisonScrollLayout( pane) ;
			}
			SendMessageW( pane, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>( &scroll[index])) ;
		}
		// 発話背景を右本文の新しい幅へ追随させ、読んでいる位置を維持する
		if( !splitter_.dragging && selected_ < cards_.size() && cards_[selected_].result) {
			requestChatDocument( true) ;
		}
		// 余白/scroll復元まで確定してから、変更領域だけをmove内で一回同期更新します
		if( repaint.changed()) {
#if defined(KOEFUDE_WINDOW_TESTING)
            // 実APIを置換せず、その直前直後を所有試験だけが観測します
			if( paneRepaintObserver_) {
				paneRepaintObserver_( repaint.region(), K_MAIN_PANE_REDRAW_FLAGS, -1) ;
			}
#endif
            const BOOL	updated = repaint.flush() ; // WM_PAINT直接送信/worker待機なし
			const DWORD updateError = updated ? ERROR_SUCCESS : GetLastError() ; // 元API診断値
#if defined(KOEFUDE_WINDOW_TESTING)
            // SW_HIDEでpaintが抑制されてもAPI呼出し完了と描画完了を混同しません
			if( paneRepaintObserver_) {
				paneRepaintObserver_( repaint.region(), K_MAIN_PANE_REDRAW_FLAGS, updated ? 1 : 0) ;
			}
#endif
            // 同期更新失敗はcallbackの既存エラー処理へ戻します
			if( !updated) {
				throw OperationError( ErrorKind::INTERNAL, updateError) ;
			}
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 従来の分:秒形式を共有します
	 * @param[in] card 認識結果を持つ行
	 * @return 実測分:秒、未取得なら—
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::wstring historyDuration( const FileCard &card) const
	{
		return card.result && card.result->durationSeconds > 0
				   ? std::to_wstring( static_cast<std::uint64_t>( card.result->durationSeconds) / 60) + L":" +
						 (static_cast<std::uint64_t>( card.result->durationSeconds) % 60 < 10 ? L"0" : L"") +
						 std::to_wstring( static_cast<std::uint64_t>( card.result->durationSeconds) % 60)
				   : std::wstring( L"—") ; // 実測値だけ
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ行/原文のAPI状態だけを返します
	 * @param[in] card UI所有行
	 * @return live同opの表示、または保存済み試行の固定状態
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] ApiTextStatusView tableApiStatus( const FileCard &card) const
	{
		// 原文ownerが変わった行へ直近状態を適用しません
		if( apiStatusHistory_ == card.id && detail::sameTranscriptOwner( apiStatusSource_, card.result)) {
			return apiStatus_.api ? *apiStatus_.api : ApiTextStatusView{apiStatus_.resourceId} ;
		}
		return apiTextRecordStatus( card.apiText) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 記号で省略した状態と出所を再構築します
	 * @param[in] card UI所有行
	 * @return filenameと完全状態/採用/最新出所。本文と親pathは含めません
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::wstring historyDetails( const FileCard &card) const
	{
		auto text = card.path.filename().native() ; // 従来のfilenameだけ
		if( card.apiAudio) {
			text += L"\r\n" + apiAudioDetails( *card.apiAudio) ;
		}
		// 原文と異なる補助用途と独立終端をtooltipへ残す
		if( card.speakerText.latest || card.speakerText.adopted) {
			text += L"\r\n" + resource( IDS_SPEAKER_TEXT_NOTICE) + L"\r\n" + card.status.text() ;
		}
		text += L"\r\n" + resource( IDS_TABLE_DURATION) + L": " + historyDuration( card) ;
		text += L"\r\n" + resource( IDS_TABLE_STATE) + L": " +
				compactTableSymbol( compactAsrState( card.status.resourceId, card.status.error)) + L" " +
				card.status.text() ; // 認識の段階/診断を保持
		const auto api = tableApiStatus( card) ; // 直近操作の完全説明は有無と独立です
		const auto improvement = compactImprovementState( card.apiText, card.id) ; // 耐久採用
		text += L"\r\n" + resource( IDS_TABLE_CORRECTION) + L": " + compactTableSymbol( improvement) + L" " +
				resource( improvement == CompactTableState::COMPLETE ? IDS_TABLE_IMPROVEMENT_PRESENT
																	: IDS_TABLE_IMPROVEMENT_ABSENT) +
				L"\r\n" + api.text() ;
		// 採用本文の出所と最新試行を分け、失敗時の旧採用保持も示します
		if( card.apiText.adopted) {
			text += L"\r\n" + apiTextProvenanceDisplay( *card.apiText.adopted, true) ;
		}
		// 同じ採用を重ねず、異なる最新試行だけを完全表示します
		if( card.apiText.latest && !sameApiTextAttempt( card.apiText.latest, card.apiText.adopted)) {
			text += L"\r\n" + apiTextProvenanceDisplay( *card.apiText.latest, false) ;
		}
		return text ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief file名と状態を有限の履歴listへ反映します
	 */
	//------------------------------------------------------------------------------
	void refreshHistory()
	{
		const auto list = control( IDC_HISTORY) ; // UI所有table
		const auto selectedId = selected_ < cards_.size() ? cards_[selected_].id : 0 ; // 安定ID
		refreshingHistory_ = true ; // DeleteAllItemsの同期通知では本文を変えません
		SendMessageW( list, WM_SETREDRAW, FALSE, 0) ;
		ListView_DeleteAllItems( list) ;
		int index = 0 ; // 最大32件のrow位置
		// 同じ履歴順で値をcopyし、IDをitem dataへ保存します
		for( const auto &card : cards_) {
			auto	name = card.path.filename().native() ; // 表示だけ、送信しません
			LVITEMW item{} ; // 同期copy
			item.mask = LVIF_TEXT | LVIF_PARAM ;
			item.iItem = index ;
			item.pszText = name.data() ;
			item.lParam = static_cast<LPARAM>( card.id) ;
			ListView_InsertItem( list, &item) ;
			const auto duration = historyDuration( card) ; // 従来の形式だけ
			auto	   state =
				compactTableSymbol( compactAsrState( card.status.resourceId, card.status.error)) ; // 認識の確定分類
			auto			 correction = compactTableSymbol( compactImprovementState( card.apiText, card.id)) ;
			auto			 model = compactApiModel( card.apiText) ; // 保存済み実応答だけ
			const std::array values{duration, state, correction, model} ; // 残り4列
			// 各列文字列はListViewが同期copyします
			for( std::size_t col = 0; col < values.size(); ++col) {
				ListView_SetItemText( list, index, static_cast<int>( col + 1),
									 const_cast<wchar_t *>( values[col].c_str())) ;
			}
			// 復元はindex推測でなく同じhistory IDに限定します
			if( card.id == selectedId) {
				selected_ = static_cast<std::size_t>( index) ;
				ListView_SetItemState( list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED) ;
			}
			++index ;
		}
		refreshingHistory_ = false ;
		SendMessageW( list, WM_SETREDRAW, TRUE, 0) ;
		InvalidateRect( list, nullptr, TRUE) ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 標準status barへ現在段階を表示します
	 * @param[in] status 翻訳前の状態・進捗・診断値
	 */
	//------------------------------------------------------------------------------
	void setStatus( const UiStatus &status)
	{
		statusProgress_.activity = !closing_ && status.api && status.api->progress.phase > ApiTextPhase::NONE &&
								   status.api->progress.phase < ApiTextPhase::COMPLETED ; // 実行中だけの不定帯
		// 更新は実段階だけを不定帯へ反映し、架空の率/残り時間を作りません
		if( status.update) {
			statusProgress_.activity = !closing_ && (status.update->state == UpdateState::CHECKING ||
													 status.update->state == UpdateState::DOWNLOADING ||
													 status.update->state == UpdateState::LAUNCHING) ;
			statusProgress_.percent = 0 ;
		}
		// APIの説明と塗りは同じ適用境界で更新し、別行へ旧工程を残しません
		if( status.audio) {
			const auto &value = *status.audio ;
			statusProgress_.activity = !closing_ && !value.ended ;
			statusProgress_.percent =
				value.ended && value.terminal == ApiAudioTerminal::COMPLETED && !value.saveFailed ? 100
				: !value.ended && value.progress.totalUnits
					? static_cast<int>(
						  std::min<std::uint64_t>( 99, static_cast<std::uint64_t>( value.progress.completedUnits) * 100 /
														  value.progress.totalUnits))
					: 0 ;
		} else if( status.api) {
			statusProgress_.percent = status.api->steps().percent ;
		} else if( status.resourceId == IDS_COMPLETED || status.resourceId == IDS_WARNING ||
				  status.resourceId == IDS_NO_TEXT || status.resourceId == IDS_READING_DONE) {
			// 同じlocal処理の確定終端でも、直前sample通知の率を残しません
			statusProgress_.percent = 100 ;
		} else if( status.resourceId == 0 || status.resourceId == IDS_CANCELLED || status.resourceId == IDS_READY ||
				  status.resourceId == IDS_QUEUED) {
			statusProgress_.percent = 0 ; // 失敗/取消し/新規待機を直前の完了率へ見せません
		} else if( currentStatus_.api || currentStatus_.audio) {
			// APIから音声/localへ戻る時は、その実通知か確定終端だけへ復帰します
			statusProgress_.percent = status.percent >= 0 ? status.percent
									  : status.resourceId == IDS_COMPLETED || status.resourceId == IDS_WARNING ||
											  status.resourceId == IDS_READING_DONE
										  ? 100
										  : 0 ;
		}
		currentStatus_ = status ;
		std::wstring owned = status.compactText() ; // 詳細は型付きDTOと通知へ保持
		std::wstring history ; // 補助状態は幅不足時に一単位で省きます
		// 読込前・読込失敗時には、新規入力より先に既存履歴の状態を示す
		if( !historyReady_) {
			owned = resource( historyFailed_ ? IDS_SHORT_FAILED : IDS_SHORT_HISTORY_LOADING) ;
		}
		// 処理の進捗で未保存の警告を隠さない
		else if( historyFailed_) {
			history = resource( IDS_SHORT_HISTORY_UNSAVED) ;
			// 未保存の警告は補助状態の幅不足でも失いません
			owned = history + L" | " + owned ;
			history.clear() ;
		}
		// 完了通知前に保存済みとは表示しない
		else if( historyDirty_) {
			history = resource( IDS_SHORT_HISTORY_SAVING) ;
		}
		// 復元・保存後の保持状態を明示する
		else if( !cards_.empty() || historyRevision_ != 0) {
			history = resource( IDS_SHORT_HISTORY_SAVED) ;
		}
		// 標準keyboard選択でも記号の完全状態を追えます。現在段階/経過は先頭のままです
		std::wstring improvement ; // 採用有無を最新失敗や生成率へ読み替えません
		if( selected_ < cards_.size()) {
			const auto &card = cards_[selected_] ; // 同じ選択行の状態だけ
			improvement = resource( compactImprovementState( card.apiText, card.id) == CompactTableState::COMPLETE
									   ? IDS_SHORT_IMPROVED
									   : IDS_SHORT_NOT_IMPROVED) ;
		}
		RECT area{} ; // 自statusの幅。desktopや他processは参照しません
		GetClientRect( status_, &area) ;
		const HDC dc = GetDC( status_) ; // 自controlのfont計測だけ、画面取得なし
		if( dc) {
			const auto previous = SelectObject( dc, reinterpret_cast<HFONT>( SendMessageW( status_, WM_GETFONT, 0, 0))) ;
			owned = fitCompactStatus(
				std::move( owned), history, improvement,
				std::max( 0L, area.right - area.left - MulDiv( 24, GetDpiForWindow( status_), 96)),
				[dc]( const std::wstring &text)
				{
					SIZE size{} ;
					return GetTextExtentPoint32W( dc, text.c_str(), static_cast<int>( text.size()), &size) ? size.cx
																										 : LONG_MAX ;
				}) ;
			SelectObject( dc, previous) ;
			ReleaseDC( status_, dc) ;
		}
		statusProgress_.text = owned ;
		SendMessageW( status_, SB_SETTEXTW, SBT_NOBORDERS,
					 reinterpret_cast<LPARAM>( owned.c_str())) ; // Win32文字列の借用境界
		InvalidateRect( status_, nullptr, FALSE) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 完全保存済みAPI採用の比較を選択できるか返します
	 * @return 受付可ならtrue
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool comparisonAvailable() const
	{
		// 原文なし/未採用/保存失敗/処理中/closeには左右候補を作りません
		if( closing_ || selected_ >= cards_.size()) {
			return false ;
		}
		const auto &card = cards_[selected_] ; // 同じ選択行だけ
		return card.result && !card.result->lines.empty() && card.apiText.adopted &&
			   card.apiText.adopted->state == ApiTextState::CANDIDATE_READY && !card.apiText.saveFailed && !card.busy &&
			   !card.reading.active && !card.wholeText.active &&
			   !(apiTextWindow_.busy() && apiTextWindow_.historyId() == card.id) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief UI比較modeが同じ原文/候補ownerへ束縛されているか返します
	 * @return 有効時true
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool comparisonActive() const
	{
		return comparisonAvailable() && compareHistory_ == cards_[selected_].id &&
			   detail::sameTranscriptOwner( compareSource_, cards_[selected_].result) &&
			   compareCandidate_ == cards_[selected_].apiText.adopted &&
			   !compareCandidate_.owner_before( cards_[selected_].apiText.adopted) &&
			   !cards_[selected_].apiText.adopted.owner_before( compareCandidate_) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief UIだけの比較ownerを解放します。履歴やAPI表示意図は変更しません
	 */
	//------------------------------------------------------------------------------
	void clearComparison()
	{
		// 比較を閉じたとき旧文書の移動先を次の表示へ渡しません
		if( compareHistory_) {
			cancelDocumentScroll() ;
		}
		compareHistory_ = 0 ;
		compareSource_.reset() ;
		compareCandidate_.reset() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 手動/自動で同じ保存済み採用ownerを比較へ束縛します
	 */
	//------------------------------------------------------------------------------
	void selectComparison()
	{
		// busy解除前・未保存・終了中は比較へ入れません
		if( !comparisonAvailable()) {
			return ;
		}
		const auto &card = cards_[selected_] ; // 現在選択中の同じ完全採用
		compareHistory_ = card.id ;
		compareSource_ = card.result ;
		compareCandidate_ = card.apiText.adopted ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 選択行のsession内完了予約を一度だけ照合し消費します
	 */
	//------------------------------------------------------------------------------
	void consumePendingComparison()
	{
		// 未選択には比較対象がありません
		if( selected_ >= cards_.size()) {
			return ;
		}
		auto	  &card = cards_[selected_] ; // 最大32行に各一件だけの有界予約
		const auto source = std::move( card.pendingCompareSource) ; // 不一致でも一度で破棄
		const auto candidate = std::move( card.pendingCompareCandidate) ; // 保存ownerを借用しない
		// 別原文・別採用・新処理・closeの古い予約を現在表示へ読み替えません
		if( source && candidate && detail::sameTranscriptOwner( source, card.result) &&
		   candidate == card.apiText.adopted && !candidate.owner_before( card.apiText.adopted) &&
		   !card.apiText.adopted.owner_before( candidate)) {
			selectComparison() ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 文書差替え時に旧文書のsmooth移動先を破棄します
	 */
	//------------------------------------------------------------------------------
	void cancelDocumentScroll() noexcept
	{
		cancelSmoothScroll( chat_, smoothScroll_) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じownerの単一文書を適用します
	 * @param[in] preserveView 同版resize時の保持
	 */
	//------------------------------------------------------------------------------
	void installChatDocument( const bool preserveView)
	{
		// 別mode/別ownerへ準備した本文をstreamしません
		if( !document_ || document_->request.compareApiText != comparisonActive()) {
			return ;
		}
		cancelDocumentScroll() ;
		setPreparedChatDocument( chat_, *document_, preserveView) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 選択済みカードの準備文書が最新か確認します
	 * @return 一致時true
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool documentReady() const
	{
		return documentOperation_ == 0 && document_ && documentRequest_ && document_->request == *documentRequest_ &&
			   selected_ < cards_.size() && document_->request.historyId == cards_[selected_].id &&
			   document_->request.source == cards_[selected_].result &&
			   document_->request.apiText == cards_[selected_].apiText.adopted &&
			   document_->request.speakerText == cards_[selected_].speakerText.adopted &&
			   document_->request.speakerAudio == cards_[selected_].apiAudio &&
			   document_->request.compareApiText == comparisonActive() &&
			   document_->request.showApiText == (comparisonActive() || cardShowsApiText( cards_[selected_])) &&
			   (document_->request.showApiText ||
				(document_->request.formatted == cards_[selected_].reading.formatted &&
				 document_->request.display == cardReadingDisplay( cards_[selected_]) &&
				 document_->request.bounded == (cardResultDisplay( cards_[selected_]) == ResultPreference::BOUNDED_PREFIX
													? cards_[selected_].bounded.latestText
													: (boundedActions( cards_[selected_].bounded).effective
														   ? cards_[selected_].bounded.effectiveText
														   : nullptr)) &&
				 document_->request.wholeText == (boundedActions( cards_[selected_].bounded).effective
													  ? nullptr
													  : cards_[selected_].wholeText.corrected) &&
				 document_->request.showWholeText ==
					 (cardResultDisplay( cards_[selected_]) == ResultPreference::WHOLE_TEXT))) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 全文copy/保存を最新の準備状態に対応させます
	 */
	//------------------------------------------------------------------------------
	void refreshDocumentActions()
	{
		const bool available = documentReady() && !closing_ ; // 別版を混入させない操作条件
		// 準備済みの同じ表示版だけをmenuから保存/copyできるようにする
		for( const int id : {IDC_COPY, IDC_SAVE}) {
			EnableWindow( control( id), available) ;
			EnableMenuItem( GetMenu( window_), id, MF_BYCOMMAND | (available ? MF_ENABLED : MF_GRAYED)) ;
		}
		refreshWholeTextActions() ;
		refreshReadingActions() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 明示CPU試用と専用copy/TXTの受付条件を更新します
	 */
	//------------------------------------------------------------------------------
	void refreshWholeTextActions()
	{
		const auto menu = GetSubMenu( GetMenu( window_), 0) ; // 所有アプリのfile menu
		const bool valid = selected_ < cards_.size() ; // 有限カード
		// この同期境界で所有する値、または確定状態の検証値です
		const bool ready =
			valid && documentReady() && historyReady_ && !historyDirty_ && !historyFailed_ && !closing_ &&
			!cards_[selected_].busy && !cards_[selected_].reading.active && !cards_[selected_].wholeText.active &&
			cards_[selected_].result && !cards_[selected_].result->lines.empty() &&
			(!cards_[selected_].apiAudio || cards_[selected_].apiAudio->terminal == ApiAudioTerminal::COMPLETED) &&
			cards_[selected_].result->pcmIdentity && cards_[selected_].result->asrModel == AsrModel::PARAKEET &&
			cards_[selected_].result->asrProfile == AsrProfile::QUALITY ;
		// 二つの明示作成とcopy・保存の四操作を言語変更後も同じIDで更新します
		for( const auto id : {IDC_TEXT_CONTEXT_CREATE, IDC_WHOLE_CREATE, IDC_WHOLE_CREATE_AVX2,
							 IDC_WHOLE_CREATE_MINISTRAL16GB, IDC_WHOLE_COPY, IDC_WHOLE_SAVE}) { // 有限command
			// この同期境界で所有する値、または確定状態の検証値です
			const auto label = resource( id == IDC_TEXT_CONTEXT_CREATE		   ? IDS_TEXT_CONTEXT_CREATE
										: id == IDC_WHOLE_CREATE_MINISTRAL16GB ? IDS_WHOLE_CREATE_MINISTRAL16GB
										: id == IDC_WHOLE_CREATE_AVX2		   ? IDS_WHOLE_CREATE_AVX2
										: id == IDC_WHOLE_CREATE			   ? IDS_WHOLE_CREATE
										: id == IDC_WHOLE_COPY				   ? IDS_WHOLE_COPY
																			   : IDS_WHOLE_SAVE) ; // 表示名
			// この同期境界で所有する値、または確定状態の検証値です
			const bool enabled =
				(id == IDC_WHOLE_CREATE || id == IDC_WHOLE_CREATE_AVX2 || id == IDC_WHOLE_CREATE_MINISTRAL16GB)
					? ready && boundedActions( cards_[selected_].bounded).recreate
				: id == IDC_TEXT_CONTEXT_CREATE
					? valid && documentReady() && historyReady_ && !historyDirty_ && !historyFailed_ && !closing_ &&
						  !cards_[selected_].busy && !cards_[selected_].reading.active &&
						  !cards_[selected_].wholeText.active && cards_[selected_].result &&
						  !cards_[selected_].result->lines.empty() && boundedActions( cards_[selected_].bounded).recreate
					: valid && documentReady() && !closing_ &&
						  (cards_[selected_].wholeText.corrected ||
						   boundedActions( cards_[selected_].bounded).exportComplete) ; // 保存済み有効結果だけ
			const UINT flags = MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED) ; // 実受付条件
			const bool present = GetMenuState( menu, id, MF_BYCOMMAND) != 0xFFFFFFFFU ; // 既登録
			// 自分のmenu以外へ操作しません
			if( !(present ? ModifyMenuW( menu, id, MF_BYCOMMAND | flags, id, label.c_str())
						 : AppendMenuW( menu, flags, id, label.c_str()))) {
				throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
			}
		}
		// 実装済みの保存再試行と未完削除だけを追加します
		for( const auto command : {IDC_BOUNDED_RETRY, IDC_BOUNDED_DISCARD}) {
			// この同期境界で所有する値、または確定状態の検証値です
			const auto actions =
				valid ? boundedActions( cards_[selected_].bounded) : WholeTextBoundedActions{} ; /* pure flags */
			const bool enabled = valid && !closing_ &&
								 (command == IDC_BOUNDED_RETRY ? actions.retrySave : actions.discard) ; /* 受付条件 */
			const auto label =
				resource( command == IDC_BOUNDED_RETRY ? IDS_BOUNDED_RETRY : IDS_BOUNDED_DISCARD) ; /* resource集約 */
			const UINT flags = MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED) ; /* menu状態 */
			if( GetMenuState( menu, command, MF_BYCOMMAND) != 0xFFFFFFFFU) {
				static_cast<void>( ModifyMenuW( menu, command, MF_BYCOMMAND | flags, command, label.c_str())) ;
			} else {
				static_cast<void>( AppendMenuW( menu, flags, command, label.c_str())) ;
			}
		}
		// この同期境界で所有する値、または確定状態の検証値です
		const bool whole = valid && !(document_ && document_->request.showApiText) &&
						   cardResultDisplay( cards_[selected_]) == ResultPreference::WHOLE_TEXT ; // 旧出力の対象を明示
		// 元操作の対象をbuttonとmenuの両方へ明示し、原文へ戻る時も更新します
		for( const auto id : {IDC_COPY, IDC_SAVE}) { // 二つの既存操作
			// この同期境界で所有する値、または確定状態の検証値です
			const auto label = resource( id == IDC_COPY ? (whole ? IDS_WHOLE_ORIGINAL_COPY : IDS_COPY)
													   : (whole ? IDS_WHOLE_ORIGINAL_SAVE : IDS_SAVE)) ; // 明示対象
			SetWindowTextW( control( id), label.c_str()) ;
			// この同期境界で所有する値、または確定状態の検証値です
			const auto state = GetMenuState( GetMenu( window_), id, MF_BYCOMMAND) ; // 有効/無効は維持
			static_cast<void>( ModifyMenuW( GetMenu( window_), id,
										  MF_BYCOMMAND | MF_STRING | (state & (MF_DISABLED | MF_GRAYED)), id,
										  label.c_str())) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 保存barrierを満たすカードを選択に依存せず一回送ります
	 */
	//------------------------------------------------------------------------------
	void pumpAutomaticTextContext()
	{
		// close・準備・未保存中に新しいnative処理を要求しません
		if( closing_ || !historyReady_ || historyDirty_ || historyFailed_ || !controller_ ||
		   wholeTextPreparationWindow_.busy()) {
			return ;
		}
		bool changed = false ; // 新受付又は未実行理由の表示更新
		bool saveReason = false ; // 生成要求の耐久予約と通常saveを競合させません
		// 実行中の全文要求がある間は次の待ちを保ち、通常保存の終端を先に回収します
		if( std::any_of( cards_.begin(), cards_.end(),
					   //------------------------------------------------------------------------------
					   /**
					    * @brief 現在の受理済み全文を調べます
					    * @param[in] card 有限カード
					    * @return 未終端要求があればtrue
					    */
					   //------------------------------------------------------------------------------
					   []( const FileCard &card)
					   {
						   return card.wholeText.active.has_value() ;
					   })) {
			return ;
		}
		// 最大32カードだけを元順に確認し、queue fullでspinしません
		for( auto &card : cards_) {
			// 同ownerの耐久barrierと既存二journal制約を満たす対象だけ送ります
			if( !card.automaticText.ready() || card.busy || card.reading.active || card.wholeText.active ||
			   !boundedActions( card.bounded).recreate ||
			   !detail::sameTranscriptOwner( card.result, card.automaticText.source)) {
				continue ;
			}
			// PCM metadataなし旧履歴へ架空証拠を作らず、独立理由と原文を残します
			if( !card.result->pcmIdentity) {
				card.automaticText.cancel() ;
				card.wholeText.state = ReadingState::FAILURE ;
				card.wholeText.code = static_cast<std::uint32_t>( WholeTextBoundedDiagnostic::SOURCE_METADATA_MISSING) ;
				card.status = {IDS_TEXT_CONTEXT_METADATA} ;
				changed = true ;
				saveReason = true ; // 未実行理由も原文と一緒に通常保存します
				continue ;
			}
			const auto profile = textContextProfile() ; // 新しい独立固定profile
			auto	   pending = prepareSavedTextContextRecord( card.wholeText, card.automaticText,
															   profile) ; // 非0受付前に全確保
			JobRequest request ; // 音声pathを持たない文章専用要求
			request.payload = WholeTextCorrectionJobRequest{card.result, *pending.active, {}, {}} ;
			const auto id = controller_->submit( std::move( request)) ; // 唯一のID発行元
			card.automaticText.submitted( id) ;
			// 満杯では待ちを維持し、次の通常eventへ戻します
			if( id == 0) {
				break ;
			}
			pending.active->operationId = id ;
			card.wholeText = std::move( pending) ;
			changed = true ;
			break ; // 次カードはこの要求と通常保存の終端を回収した後のeventへ残します
		}
		// 受付を履歴へ反映し、別待ちsourceも新snapshotへ束縛し直します
		if( changed) {
			// workerのreserveがPending/opを耐久化するため受付直後の重複saveは不要です
			if( saveReason) {
				saveHistory() ;
			}
			showCard() ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 選択済み旧履歴だけを明示的に文章専用入口へ送ります
	 */
	//------------------------------------------------------------------------------
	void queueTextContext()
	{
		// 初期復元から一括実行せず、保存済み選択原文だけを対象にします
		if( closing_ || !documentReady() || historyDirty_ || historyFailed_ || selected_ >= cards_.size() ||
		   cards_[selected_].busy || cards_[selected_].reading.active || cards_[selected_].wholeText.active) {
			return ;
		}
		auto &card = cards_[selected_] ; // 利用者が明示選択した一カード
		card.automaticText.arm( card.result, card.id) ;
		card.automaticText.saving( historyRevision_, card.result) ;
		card.automaticText.acknowledge( historyRevision_, card.result, document_->view->key()) ;
		// 起動復元直後のrevision0では通常保存ackを一度待ちます
		if( historyRevision_ == 0) {
			saveHistory() ;
		}
		pumpAutomaticTextContext() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 最新原文の保存成功ack後だけop0を通常workerへ送ります
	 * @param[in] variant 受付時に固定するCPU契約
	 */
	//------------------------------------------------------------------------------
	void queueWholeText( const WholeTextRuntimeVariant variant = WholeTextRuntimeVariant::PORTABLE)
	{
		// 未保存/停止/二重要求を一つの受付条件で拒否します
		if( closing_ || !historyReady_ || historyDirty_ || historyFailed_ || !documentReady() ||
		   selected_ >= cards_.size() || cards_[selected_].busy || cards_[selected_].reading.active ||
		   cards_[selected_].wholeText.active) {
			return ;
		}
		auto &card = cards_[selected_] ; // UI所有カード
		// 未完/残存が二Jを占める場合は明示削除後だけ再作成します
		if( !boundedActions( card.bounded).recreate) {
			return ;
		}
		// 過去の原文へPCM証拠を後付けしません
		if( !card.result->pcmIdentity) {
			showMessage( IDS_WHOLE_NO_PCM) ;
			return ;
		}
		// 認識本文なしは全文作成の対象にせず、満杯の案内にも混同しません
		if( card.result->lines.empty()) {
			return ;
		}
		// 選定済み原文だけをこの試用へ渡します
		if( card.result->asrModel != AsrModel::PARAKEET || card.result->asrProfile != AsrProfile::QUALITY) {
			return ;
		}
		const auto				   profile = wholeTextBoundedProfile( variant) ; // 二補正操作をboundedへ明示固定
		// この同期境界で所有する値、または確定状態の検証値です
		WholeTextCorrectionRequest fixed{document_->view->key(), 0, profile.id, profile.sha256} ; // 仮IDなし
		// この同期境界で所有する値、または確定状態の検証値です
		auto	   pending = prepareWholeTextCorrectionRecord( card.wholeText, card.result, *document_->view,
															  fixed) ; // submit前に確保
		JobRequest request ; // 値だけの所有要求
		const auto capture =
			diagnosticLog_.currentMode() ? std::make_shared<CorrectionDiagnosticCapture>() : nullptr ; // 受付前確保
		request.payload = WholeTextCorrectionJobRequest{card.result, fixed, card.path, {}, capture} ;
		const auto id = controller_->submit( std::move( request)) ; // 非0opはcontrollerだけが発行
		// 受付失敗なら元recordを変更しません
		if( id == 0) {
			showMessage( IDS_FULL) ;
			return ;
		}
		diagnosticLog_.correctionAccepted( card.id, id, static_cast<std::uint32_t>( variant),
										  card.bounded.effectiveText ? 2
										  : card.wholeText.corrected ? 1
																	 : 0) ;
		pending.active->operationId = id ;
		static_assert( std::is_nothrow_move_assignable_v<WholeTextCorrectionRecord>) ;
		card.wholeText = std::move( pending) ; // 受付後はnoexceptな反映だけ
		statusProgress_.percent = 0 ; // 新しい明示試行を前の完了率から開始しません
		showCard() ;
		saveHistory() ;
		setStatus( wholeTextStatus( card.wholeText)) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同op/ownerの検証済み全文を受領します
	 * @param[in] event worker所有通知
	 * @return 全文通知ならtrue
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool collectWholeTextEvent( const JobEvent &event)
	{
		// この同期境界で所有する値、または確定状態の検証値です
		const auto found =
			std::find_if( cards_.begin(), cards_.end(),
						 //------------------------------------------------------------------------------
						 /**
						  * @brief active opを照合します
						  * @param[in] card カード
						  * @return 同じopならtrue
						  */
						 //------------------------------------------------------------------------------
						 [&event]( const FileCard &card)
						 {
							 return card.wholeText.active && card.wholeText.active->operationId == event.id ;
						 }) ; // 最大32件
		// staleは他カードの結果へ適用しません
		if( found == cards_.end()) {
			return false ;
		}
		const auto fixed = *found->wholeText.active ; // finishによるresetより長い小copy
		// 型付きackのowner/op不一致は原文にも診断成功にも適用しません
		if( event.terminal && event.boundedPersistence &&
		   (!detail::sameTranscriptOwner( found->result, event.boundedPersistence->source()) ||
			event.boundedPersistence->request().operationId != fixed.operationId ||
			event.boundedPersistence->request().source != fixed.source ||
			event.boundedPersistence->request().profileId != fixed.profileId ||
			event.boundedPersistence->request().profileSha256 != fixed.profileSha256)) {
			return true ;
		}
		// 補正通知はASR branchを通らないためここで専用scalarを採ります
		if( event.terminal && diagnosticLog_.currentMode()) {
			const auto snapshot =
				event.correctionDiagnostics.value_or( CorrectionDiagnosticSnapshot{}) ; // 未dispatchはNotStarted
			DiagnosticCorrectionSave save ; // 保存不観測は3/nullのまま
			const auto				*ack = event.boundedPersistence.get() ; // 同期borrow、logへ渡しません
			// 実保存receiptだけでcommitを記録します
			if( ack) {
				save.state = ack->receipt().committed											 ? 1
							 : ack->record().saveState == WholeTextBoundedSaveState::SAVE_FAILED ? 2
																								 : 3 ;
				save.attempt = static_cast<std::uint32_t>( ack->record().latestState) ;
				save.revision = ack->receipt().committed ? std::optional<std::uint64_t>( ack->receipt().storageRevision)
														 : std::nullopt ;
				save.originalRetained = detail::sameTranscriptOwner( found->result, ack->source()) ;
				save.activeKind = static_cast<std::uint32_t>( ack->record().active) ;
				// 前の有効全文がなければ保持成功を作りません
				if( found->bounded.effectiveText || found->wholeText.corrected) {
					save.previousRetained = found->bounded.effectiveText
												? found->bounded.effectiveText == ack->record().effectiveText
												: ack->record().active == WholeTextActiveFullKind::V1 ;
				}
				// 元保存失敗の分類/codeだけを保持します
				if( ack->record().saveErrorPresent) {
					save.error = ack->record().saveErrorKind ;
					save.code = ack->record().saveErrorCode ;
				}
			} else {
				save.state = snapshot.dispatchStarted ? 3 : 0 ; // 実保存未要求と未観測を区別
				save.originalRetained = true ;
				// 保存未要求でも前の有効全文は同じUI ownerのままです
				if( found->bounded.effectiveText || found->wholeText.corrected) {
					save.previousRetained = true ;
				}
				save.activeKind = found->bounded.effectiveText ? 2 : found->wholeText.corrected ? 1 : 0 ;
			}

			const bool complete = ack && ack->complete() && ack->record().effectiveText &&
								  ack->record().effectiveText->complete() ; // 全量実commit
			const std::uint32_t terminal = complete														  ? 1
										   : save.state == 2											  ? 4
										   : snapshot.outcome == CorrectionDiagnosticOutcome::NOT_STARTED ? 5
										   : event.error == ErrorKind::CANCELLED						  ? 3
																				 : 2 ; // 生成候補と別
			diagnosticLog_.correctionTerminal( found->id, fixed.operationId, snapshot, save, terminal,
											  complete ? std::nullopt : std::optional<ErrorKind>( event.error),
											  complete ? std::optional<std::uint32_t>( 0)
													   : std::optional<std::uint32_t>( event.code)) ;
		}

		// この同期境界で所有する値、または確定状態の検証値です
		const auto previousPhase = found->wholeText.phase ; // 総量不明の新段階へ前段階の率を流用しません
		// 取消し/失敗でも前の有効全文を保持します
		if( event.terminal && event.boundedPersistence) {
			const auto &completion = *event.boundedPersistence ; // 別型の保存ack
			// UIでも同じowner/opをscalar照合して適用します
			if( detail::sameTranscriptOwner( found->result, completion.source()) &&
			   completion.request().operationId == fixed.operationId) {
				found->bounded = completion.record() ;
				nextOperationId_ = std::max( nextOperationId_, fixed.operationId + 1) ;
				found->wholeText.active.reset() ;
				found->wholeText.phase = WholeTextPhase::NONE ;
				found->wholeText.state = found->wholeText.corrected ? ReadingState::COMPLETED : ReadingState::IDLE ;
				found->wholeText.reason = ReadingReason::NONE ;
				found->wholeText.code = 0 ;
			}
		} else if( event.terminal) {
			static_cast<void>( finishWholeTextCorrection( found->wholeText, found->result, fixed,
														event.succeeded ? event.wholeTextCompletion : nullptr,
														event.error, event.code)) ;
		} else {
			updateWholeTextCorrection( found->wholeText, fixed, event.progress) ;
		}
		// 終端の確定率は検証済み成功だけを100%にします
		if( event.terminal) {
			statusProgress_.percent = found->wholeText.state == ReadingState::COMPLETED ? 100 : 0 ;
		}
		// 現在phaseの実測値だけをstatus全幅の塗り幅へ反映します
		else if( event.progress.percent >= 0 && found->wholeText.state != ReadingState::CANCELLING) {
			statusProgress_.percent = event.progress.percent ;
		}
		// 総量不明の別phaseへ移った時は前phaseの塗りを消します
		else if( event.progress.wholeTextPhase != previousPhase) {
			statusProgress_.percent = 0 ;
		}
		const auto status = wholeTextStatus( found->wholeText, event.terminal ? -1 : event.progress.percent) ;
		setStatus( status) ;
		if( event.terminal && !event.succeeded && event.error != ErrorKind::CANCELLED) {
			queueStatusNotice( StatusNoticeKind::WHOLE, event.id, status, &*found) ;
		}
		return true ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 専用全文TXTを不変snapshotで受け付けます
	 * @param[in] path 明示出力先
	 * @param[in] format TXTだけ
	 */
	//------------------------------------------------------------------------------
	void queueWholeTextSave( const std::filesystem::path &path, const ExportFormat format)
	{
		// 専用字幕は推測せず原文保存へ案内します
		if( format != ExportFormat::PLAIN_TEXT) {
			showMessage( IDS_READING_TEXT_ONLY) ;
			return ;
		}
		// API自由本文の専用TXTも通常の明示保存へ渡します
		if( documentReady() && document_->request.showApiText && format == ExportFormat::PLAIN_TEXT) {
			queueSave( path, format) ;
			return ;
		}
		// 準備文書と有効全文が揃った明示操作だけを受け付けます
		if( !documentReady() ||
		   (!cards_[selected_].wholeText.corrected && !boundedActions( cards_[selected_].bounded).exportComplete)) {
			return ;
		}
		JobRequest request ; // 受付後に本文を差し替えません
		const auto bounded = cards_[selected_].bounded.effectiveText ; // 保存済みownerだけ
		request.payload =
			WholeTextExportJobRequest{path, cards_[selected_].result,
									  bounded && bounded->complete() ? nullptr : cards_[selected_].wholeText.corrected,
									  bounded && bounded->complete() ? bounded : nullptr} ;
		const auto id = controller_->submit( std::move( request)) ; // 同じ有限worker
		// 満杯は成功扱いしません
		if( id == 0) {
			showMessage( IDS_FULL) ;
			return ;
		}
		saves_.emplace( id, cards_[selected_].id) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 採用・準備済み方式の受付metadataを読みます
	 * @return UI thread内だけのborrow。現在の通常providerはnull
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const ReadingProfile *readingProfile() const noexcept
	{
#if defined(KOEFUDE_WINDOW_TESTING)
        return testHooks_.readingProfile ? &*testHooks_.readingProfile : nullptr ;
#else
        return nullptr ; // 品質未成立の方式を通常版へ登録しない
#endif
    }
	//------------------------------------------------------------------------------
	/**
	 * @brief 採用・準備済み方式がある場合だけ作成menuを表示します
	 */
	//------------------------------------------------------------------------------
	void refreshReadingActions()
	{
		const HMENU menu = GetMenu( window_) ; // 自分のWindowが所有するmenu
		// 整形の作成commandが現在menuへ登録されているか
		const bool	present = GetMenuState( menu, IDC_READING_CREATE, MF_BYCOMMAND) != 0xFFFFFFFFU ;
		// 空providerでは操作を出さず、古いcommandもqueue_readingで拒否する
		if( !readingProfile()) {
			// 試験providerを外した場合も前の操作を残さない
			if( present && !DeleteMenu( menu, IDC_READING_CREATE, MF_BYCOMMAND)) {
				throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
			}
			return ;
		}
		const bool valid = selected_ < cards_.size() ; // 同じ選択カードにだけ作成する
		// 選択原文・保存・処理状態が整形の受付条件を全て満たすか
		const bool enabled =
			valid && documentReady() && historyReady_ && !historyDirty_ && !historyFailed_ && !closing_ &&
			!cards_[selected_].busy && !cards_[selected_].reading.active && !cards_[selected_].wholeText.active &&
			(!cards_[selected_].apiAudio || cards_[selected_].apiAudio->terminal == ApiAudioTerminal::COMPLETED) ;
		const UINT flags = MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED) ; // 単一受付条件
		// 整形結果の有無に対応する作成または再作成のmenu文言
		const auto label =
			resource( valid && cards_[selected_].reading.formatted ? IDS_READING_RECREATE : IDS_READING_CREATE) ;
		// 作成commandの追加または更新が成功したか
		const BOOL changed =
			present ? ModifyMenuW( menu, IDC_READING_CREATE, MF_BYCOMMAND | flags, IDC_READING_CREATE, label.c_str())
					: AppendMenuW( GetSubMenu( menu, 0), flags, IDC_READING_CREATE, label.c_str()) ;
		// menu失敗を利用可能な入口として扱わない
		if( !changed) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 完成投影のkeyを使い、一つの整形要求を有限queueへ送ります
	 */
	//------------------------------------------------------------------------------
	void queueReading()
	{
		const auto *profile = readingProfile() ; // 受付中だけ有効な小さい方式情報
		// 未採用/未準備方式の専用commandでも生成器へ進めない
		if( !profile) {
			showMessage( IDS_READING_UNAVAILABLE) ;
			return ;
		}
		// 未保存の原文と停止/二重操作を新しい生成へ渡さない
		if( closing_ || !historyReady_ || !documentReady() || selected_ >= cards_.size() || cards_[selected_].busy ||
		   cards_[selected_].reading.active || cards_[selected_].wholeText.active ||
		   (cards_[selected_].apiAudio && cards_[selected_].apiAudio->terminal != ApiAudioTerminal::COMPLETED)) {
			return ;
		}
		// 原文の先行保存が失敗/未完了なら、旧履歴を残して受付しない
		if( historyDirty_ || historyFailed_) {
			showMessage( historyFailed_ ? IDS_HISTORY_SAVE_ERROR : IDS_HISTORY_SAVING) ;
			return ;
		}
		auto		  &card = cards_[selected_] ; // UI threadだけが変更する同じカード
		ReadingRequest fixed ; // worker検証済み表示から固定する小さい要求
		fixed.source = document_->view->key() ;
		fixed.profileId = profile->id ;
		fixed.profileSha256 = profile->sha256 ;
		fixed.operationId = 1 ; // 受付前の状態準備だけに使い、queueへは0を送る
		ReadingRecord pending = card.reading ; // 本文をcopyしない小さい受付準備
		// 状態準備が失敗した時はworker要求を作らず、元状態を残す
		if( !beginPreparedReading( pending, *document_->view, fixed)) {
			return ;
		}
		fixed.operationId = 0 ;
		JobRequest request ; // ASRと同じworkerへ送る型付き所有要求
		request.payload = ReadingJobRequest{card.result, std::move( fixed)} ;
		const auto id = controller_->submit( std::move( request)) ; // 二重/満杯もcontrollerで照合
		// 受付拒否を実行中や成功として履歴へ保存しない
		if( id == 0) {
			showMessage( IDS_FULL) ;
			return ;
		}
		pending.active->operationId = id ;
		card.reading = std::move( pending) ;
		card.readingSource = card.result ;
		statusProgress_.percent = 0 ;
		showCard() ;
		saveHistory() ;
		setStatus( readingStatus( card.reading)) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 整形eventだけを同じ受付ID/source ownerへ反映します
	 * @param[in] event 有限workerの所有通知
	 * @return 整形通知を受領した場合true
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool collectReadingEvent( const JobEvent &event)
	{
		// この同期境界で所有する値、または確定状態の検証値です
		const auto found = std::find_if( cards_.begin(), cards_.end(),
			//------------------------------------------------------------------------------
			/**
			 * @brief 同じ整形operationを検索します
			 * @param[in] card 同じUIのカード
			 * @return 未受領の同じoperationならtrue
			 */
			//------------------------------------------------------------------------------
			[&event]( const FileCard &card)
			{
				return card.reading.active && card.reading.active->operationId == event.id ;
			}) ; // 32件以内の検索。別ASRの終端へ適用しない
		// 古い/未受付IDは通常のASR/export受領へ委ねる
		if( found == cards_.end()) {
			return false ;
		}
		const auto &fixed = *found->reading.active ; // 関数内でだけ借用する受付metadata
		// 終端だけが全体検証済みoverlayを置き換えられる
		if( event.terminal) {
			// この同期境界で所有する値、または確定状態の検証値です
			const auto error =
				found->result == found->readingSource ? event.error : ErrorKind::INVALID_INPUT ; // 元ownerを照合
			// 同じ原文ownerへ束縛されている場合だけ保持する検証済み整形結果
			const auto completion =
				event.succeeded && found->result == found->readingSource ? event.readingCompletion : nullptr ;
			static_cast<void>(
				finishPreparedReading( found->reading, found->readingSource, fixed, completion, error, event.code)) ;
			found->readingSource.reset() ;
		} else {
			updateReading( found->reading, fixed, event.progress) ;
		}
		setStatus( event.terminal && found->reading.state == ReadingState::FAILURE
					  ? UiStatus{IDS_READING_GENERATION_FAILED, -1, event.error, event.code}
					  : readingStatus( found->reading, event.terminal ? -1 : event.progress.percent)) ;
		if( event.terminal && !event.succeeded && event.error != ErrorKind::CANCELLED) {
			queueStatusNotice( StatusNoticeKind::READING, event.id, readingStatus( found->reading), &*found) ;
		}
		// 既存の実進捗だけを反映し、総量不明では推測の率を追加しない
		if( !event.terminal && event.progress.percent >= 0) {
			statusProgress_.percent = event.progress.percent ;
		}
		return true ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 存在する結果だけを安定したitem dataで表示します
	 */
	//------------------------------------------------------------------------------
	void refreshReadingSelection()
	{
		const bool priorCompare = comparisonShown_ ; // stale時も旧左右を残さない
		consumePendingComparison() ; // ACK反映後/次回選択時だけの一回適用
		// 行切替/候補変更/原文差替え/失敗/取消し/closeで古い比較ownerを解放します
		if( !comparisonActive()) {
			clearComparison() ;
		}
		comparisonShown_ = comparisonActive() ;
		ShowWindow( control( IDC_COMPARE_LEFT_LABEL), comparisonShown_ ? SW_SHOWNA : SW_HIDE) ;
		ShowWindow( control( IDC_COMPARE_RIGHT_LABEL), comparisonShown_ ? SW_SHOWNA : SW_HIDE) ;
		SetWindowTextW( control( IDC_EXPORT_SCOPE),
					   resource( comparisonShown_ ? IDS_API_COMPARE_SCOPE : IDS_EXPORT_SCOPE).c_str()) ;
		const bool valid = selected_ < cards_.size() ; // 選択範囲
		// この同期境界で所有する値、または確定状態の検証値です
		const bool shown = valid && (cards_[selected_].reading.formatted || cards_[selected_].reading.active ||
									 cards_[selected_].wholeText.corrected || cards_[selected_].wholeText.active ||
									 cards_[selected_].bounded.effectiveText || cards_[selected_].bounded.latestText ||
									 cards_[selected_].apiText.adopted) ; // 切替対象
		const bool changed = readingControlsShown_ != shown ; // 必要時だけ再配置
		readingControlsShown_ = shown ;
		ShowWindow( control( IDC_READING_VIEW), shown ? SW_SHOWNA : SW_HIDE) ;
		ShowWindow( control( IDC_READING_LABEL), shown ? SW_SHOWNA : SW_HIDE) ;
		EnableWindow( control( IDC_READING_VIEW), shown && !closing_) ;
		SendMessageW( control( IDC_READING_VIEW), CB_RESETCONTENT, 0, 0) ;
		const auto add =
			//------------------------------------------------------------------------------
			/**
			 * @brief 安定した意図を候補へ束縛します
			 * @param[in] preference 表示意図
			 * @param[in] resourceId 表示名
			 */
			//------------------------------------------------------------------------------
			[&]( const ResultPreference preference, const UINT resourceId)
			{
				const auto label = resource( resourceId) ; // 同期copy文字列
				// この同期境界で所有する値、または確定状態の検証値です
				const auto index = SendMessageW( control( IDC_READING_VIEW), CB_ADDSTRING, 0,
												reinterpret_cast<LPARAM>( label.c_str())) ; // 自controlのみ
				SendMessageW( control( IDC_READING_VIEW), CB_SETITEMDATA, index, static_cast<LPARAM>( preference)) ;
				// 存在しない選択の保存意図は変更せず、候補だけを省略します
				if( valid && !comparisonShown_ && cards_[selected_].resultPreference == preference) {
					SendMessageW( control( IDC_READING_VIEW), CB_SETCURSEL, index, 0) ;
				}
			} ;
		add( ResultPreference::AUTOMATIC, IDS_READING_AUTO) ;
		add( ResultPreference::ORIGINAL, IDS_READING_ORIGINAL) ;
		// 未完結果は明示選択だけに加えます
		if( valid && boundedActions( cards_[selected_].bounded).prefix) {
			add( ResultPreference::BOUNDED_PREFIX, IDS_BOUNDED_PREFIX) ;
		}
		// 旧整形ありの場合だけ候補を追加します
		if( valid && cards_[selected_].reading.formatted) {
			add( ResultPreference::LEGACY_FORMATTED, IDS_READING_FORMATTED) ;
		}
		// 自由全文ありの場合だけ候補を追加します
		if( valid && (cards_[selected_].wholeText.corrected || boundedActions( cards_[selected_].bounded).effective)) {
			add( ResultPreference::WHOLE_TEXT, IDS_WHOLE_VIEW) ;
		}
		// 保存schema外のUI項目を明示追加し、HTTP0の左右比較だけを行います
		if( comparisonAvailable()) {
			const auto label = resource( IDS_API_COMPARE) ; // 同期copy
			const auto index =
				SendMessageW( control( IDC_READING_VIEW), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>( label.c_str())) ;
			SendMessageW( control( IDC_READING_VIEW), CB_SETITEMDATA, index, K_COMPARE_READING_CHOICE) ;
			// 現在の比較modeだけを選択し、永続preferenceへ100を書きません
			if( comparisonShown_) {
				SendMessageW( control( IDC_READING_VIEW), CB_SETCURSEL, index, 0) ;
			}
		}
		// 切替行の高さだけが変わった時に再配置します
		if( changed || priorCompare != comparisonShown_) {
			layout() ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 最新の所有表示要求をworkerへ渡します
	 * @param[in] preserveView 同じ版のresizeなら選択/scrollを保持します
	 */
	//------------------------------------------------------------------------------
	void requestChatDocument( const bool preserveView)
	{
		// drag中の再配置要求は終了時の最新幅へまとめます。別版の明示表示は従来通り
		if( splitter_.dragging && preserveView) {
			return ;
		}
		// 終了中/本文なしには新しい準備を受け付けない
		if( closing_ || !documents_ || selected_ >= cards_.size() || !cards_[selected_].result) {
			return ;
		}
		const auto		   &card = cards_[selected_] ; // UIの不変snapshotのborrow
		ChatDocumentRequest request ; // HWND/engineを持たない描画snapshot
		request.historyId = card.id ;
		request.source = card.result ;
		request.formatted = card.reading.formatted ;
		request.display = cardReadingDisplay( card) ;
		const auto display = cardResultDisplay( card) ; // 保存意図の解決
		request.bounded =
			display == ResultPreference::BOUNDED_PREFIX
				? card.bounded.latestText
				: ((card.bounded.effectiveText && card.bounded.effectiveText->complete()) ? card.bounded.effectiveText
																						  : nullptr) ;
		request.effectiveBounded =
			card.bounded.effectiveText && card.bounded.effectiveText->complete() ? card.bounded.effectiveText : nullptr ;
		request.showBoundedPrefix = display == ResultPreference::BOUNDED_PREFIX ;
		request.wholeText = request.effectiveBounded ? nullptr : card.wholeText.corrected ;
		request.showWholeText = cardResultDisplay( card) == ResultPreference::WHOLE_TEXT ;
		request.apiText = card.apiText.adopted ;
		request.speakerText = card.speakerText.adopted ;
		request.speakerAudio = card.apiAudio ;
		request.compareApiText = comparisonActive() ;
		request.showApiText = request.compareApiText || cardShowsApiText( card) ;
		// API自由本文に時刻/話者や旧local補正を混ぜません
		if( request.showApiText) {
			request.formatted.reset() ;
			request.display = ReadingDisplay::ORIGINAL ;
			request.bounded.reset() ;
			request.effectiveBounded.reset() ;
			request.wholeText.reset() ;
			request.showWholeText = false ;
			request.showBoundedPrefix = false ;
		}
		request.heading = chatHeading_ ;
		// 保存された原文表示意図を補正文の出所と混同しません
		if( !request.showApiText && (card.apiText.adopted || card.apiText.latest) &&
		   (card.apiText.showOriginal || card.resultPreference == ResultPreference::ORIGINAL)) {
			request.heading += resource( IDS_API_PROVENANCE_ORIGINAL) + L"\r\n" ;
		}
		request.filter = request.showWholeText || request.showApiText ? K_ALL_SPEAKERS : card.filter ;
		request.widthTwips = chatDocumentWidth( chat_) ;
		request.language = uiLanguage() ;
		// 同じ要求は再hash/再構築せず、完成文書だけを再利用する
		if( documentRequest_ && request == *documentRequest_) {
			// 未到着の文書へ重複要求を送らない
			if( documentOperation_ != 0) {
				return ;
			}
			// 成功文書がある場合だけ同期streamを再利用する
			if( document_ && document_->request == request) {
				installChatDocument( preserveView) ;
				refreshDocumentActions() ;
				return ;
			}
		}
		preserveDocumentView_ =
			preserveView && document_ && document_->request.historyId == request.historyId &&
			document_->request.source == request.source && document_->request.apiText == request.apiText &&
			document_->request.speakerText == request.speakerText &&
			document_->request.speakerAudio == request.speakerAudio &&
			document_->request.showApiText == request.showApiText &&
			document_->request.compareApiText == request.compareApiText &&
			document_->request.formatted == request.formatted && document_->request.display == request.display &&
			document_->request.wholeText == request.wholeText && document_->request.bounded == request.bounded &&
			document_->request.effectiveBounded == request.effectiveBounded &&
			document_->request.showBoundedPrefix == request.showBoundedPrefix &&
			document_->request.showWholeText == request.showWholeText && document_->request.filter == request.filter &&
			document_->request.language == request.language ;
		cancelDocumentScroll() ; // 待機中の旧文書操作を次の完成文書へ流用しません
		// 別カード/版の待機中に旧本文をその版として見せない
		if( !preserveDocumentView_) {
			document_.reset() ;
			// 見出しと整形表示の準備状態を示す所有文字列
			const auto preparing = chatHeading_ + resource( IDS_READING_PREPARING) ;
			SetWindowTextW( chat_, preparing.c_str()) ;
		}
		documentOperation_ = documents_->submit( request) ;
		documentRequest_ = std::move( request) ;
		refreshDocumentActions() ;
		// 停止/ID枯渇時に準備完了を装わない
		if( documentOperation_ == 0) {
			setStatus( {IDS_READING_FAILED, -1, ErrorKind::INTERNAL}) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 最新の表示IDだけを受領し、原文/整形snapshotを変更せず適用します
	 */
	//------------------------------------------------------------------------------
	void collectDocumentEvent()
	{
		// 初期化中には存在しないworkerを呼ばない
		if( !documents_) {
			return ;
		}
		auto event = documents_->takeEvent() ; // 未受領の最新終端1件
		// 別カード/clear/古い要求の終端を現在本文へ適用しない
		if( !event || event->id != documentOperation_ || closing_) {
			return ;
		}
		documentOperation_ = 0 ;
		// 全体構築が成功した文書だけをinstallする
		if( event->document && documentRequest_ && event->document->request == *documentRequest_ &&
		   selected_ < cards_.size() && event->document->request.historyId == cards_[selected_].id &&
		   detail::sameTranscriptOwner( event->document->request.source, cards_[selected_].result) &&
		   event->document->request.apiText == cards_[selected_].apiText.adopted &&
		   event->document->request.speakerText == cards_[selected_].speakerText.adopted &&
		   event->document->request.speakerAudio == cards_[selected_].apiAudio &&
		   event->document->request.compareApiText == comparisonActive()) {
			document_ = std::move( event->document) ;
			installChatDocument( preserveDocumentView_) ;
		} else {
			setStatus( {IDS_READING_FAILED, -1, event->error, event->code}) ;
		}
		refreshDocumentActions() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 選択行のAPI終端と元音声状態を分離します
	 * @return 選択行の固定状態
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] UiStatus selectedStatus() const
	{
		// 未選択時は以前の行のAPI終端を表示しません
		if( selected_ >= cards_.size()) {
			return {IDS_READY} ;
		}
		const auto &card = cards_[selected_] ; // UIが所有する同じ行
		// 実行中の音声・local補正は各段階の表示を維持します
		if( card.busy || card.reading.active || card.wholeText.active) {
			return card.status ;
		}
		// liveの事前拒否も同じ行の今回の結果を優先します
		if( apiStatusHistory_ == card.id && detail::sameTranscriptOwner( apiStatusSource_, card.result)) {
			return apiStatus_ ;
		}
		// 再起動では保存済みの最後の試行だけから診断します。通信は行いません
		if( card.apiText.latest || card.apiText.adopted) {
			UiStatus status ; // 言語切替に追従するtyped状態
			status.api = apiTextRecordStatus( card.apiText) ;
			return status ;
		}
		return card.status ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 選択fileのチャットと操作可能状態を表示します
	 */
	//------------------------------------------------------------------------------
	void showCard()
	{
		refreshReadingSelection() ;
		const bool valid = selected_ < cards_.size() ; // 選択indexの有効性
		const bool result = valid && cards_[selected_].result != nullptr ; // 保存可能な結果有無
		const bool ready = result && documentReady() && !closing_ ; // 保存/copy対象版
		EnableMenuItem( GetMenu( window_), IDC_SAVE, MF_BYCOMMAND | (ready ? MF_ENABLED : MF_GRAYED)) ;
		EnableMenuItem( GetMenu( window_), IDC_COPY, MF_BYCOMMAND | (ready ? MF_ENABLED : MF_GRAYED)) ;
		const bool cancellable =
			valid && !closing_ && (rowOperation( cards_[selected_]) || cards_[selected_].automaticText.waiting) ;
		EnableMenuItem( GetMenu( window_), IDC_CANCEL, MF_BYCOMMAND | (cancellable ? MF_ENABLED : MF_GRAYED)) ;
		const bool rename =
			result && cards_[selected_].result->speakerCount > 0 && !rowOperation( cards_[selected_]) && !closing_ ;
		EnableMenuItem( GetMenu( window_), IDC_RENAME, MF_BYCOMMAND | (rename ? MF_ENABLED : MF_GRAYED)) ;
		const UINT rerun = valid ? retranscribeBlocked( cards_[selected_].path) : IDS_INVALID ;
		EnableMenuItem( GetMenu( window_), IDC_RETRANSCRIBE, MF_BYCOMMAND | (rerun == 0 ? MF_ENABLED : MF_GRAYED)) ;
		updateHistoryActions() ;
		refreshFilter() ;
		// 自由全文には話者対応を付けず、元filterはカードへ保持します
		if( valid && (cardResultDisplay( cards_[selected_]) == ResultPreference::WHOLE_TEXT || comparisonActive() ||
					 cardShowsApiText( cards_[selected_]))) {
			EnableWindow( control( IDC_FILTER), FALSE) ;
		}
		// 未投入またはclear直後はfile投入の案内を表示する
		if( !valid) {
			// 未選択へAPIの説明と塗りを持ち越しません
			if( currentStatus_.api) {
				setStatus( {IDS_READY}) ;
			}
			documentOperation_ = 0 ;
			documentRequest_.reset() ;
			document_.reset() ;
			// この同期境界で所有する値、または確定状態の検証値です
			const auto hint = historyReady_	   ? resource( IDS_HINT)
							  : historyFailed_ ? historyErrorText( historyError_, historyCode_, true)
											   : resource( IDS_HISTORY_LOADING) ; // 初期履歴の状態
			SetWindowTextW( chat_, hint.c_str()) ;
			return ;
		}
		const auto &card = cards_[selected_] ; // UIが所有する選択カード
		// 選択した行の完全状態をkeyboardと標準statusの読み取りにも反映します
		setStatus( selectedStatus()) ;
		// この同期境界で所有する値、または確定状態の検証値です
		std::wstring text = L"#" + std::to_wstring( card.id) + L"  " + card.path.filename().native() + L"  " +
							card.status.text() + L"\r\n" ; // 履歴と同じ実行IDを示す
		// 未完了fileには推測した本文や話者を表示しない
		if( card.result) {
			text += resource( asrDefinition( card.result->asrModel, card.result->asrProfile).resultResource) + L"  ·  " +
					resource( card.result->speechFilter ? IDS_SPEECH_FILTER_ON : IDS_SPEECH_FILTER_OFF) + L"\r\n" ;
			// この同期境界で所有する値、または確定状態の検証値です
			const unsigned int backendResource = card.result->cpuRetried ? IDS_CPU_RETRIED
												 : !card.result->usedGpu ? IDS_CPU_USED
												 : card.result->asrModel == AsrModel::PARAKEET
													 ? IDS_CUDA_USED
													 : IDS_GPU_USED ; // 実際の成功経路
			text += resource( card.result->apiAudioWindowTimes ? IDS_AUDIO_DEVICE : backendResource) + L"  ·  " +
					resource( IDS_ESTIMATE) + std::to_wstring( card.result->speakerCount) + L"\r\n" ;
			text += resource( !card.result->diarizationRequested						 ? IDS_NO_DIARIZATION
							 : card.result->speakerMethod == SpeakerMethod::SPECTRAL ? IDS_SPECTRAL_METHOD
																					 : IDS_LEGACY_METHOD) +
					L"\r\n" ;
			text += processingTimeSummary( *card.result) ;
			if( card.apiAudio) {
				text += L"\r\n" + apiAudioDetails( *card.apiAudio) + L"\r\n" ;
			}
		}
		// 整形の失敗/中断も元ASRの成功と区別して見出しへ残す
		if( card.reading.state != ReadingState::IDLE) {
			text += readingStatus( card.reading).text() + L"\r\n" ;
		}
		// 全文失敗も原文成功と独立した状態として残します
		// 保存失敗は有効全文と分けて明示します
		if( card.bounded.saveState == WholeTextBoundedSaveState::SAVE_FAILED) {
			text += resource( IDS_BOUNDED_UNSAVED) + L"\r\n" ;
		}
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		// 新ASRの保存前は架空進捗を示さず待ち理由だけを表示します
		if( card.automaticText.waiting) {
			text += resource( IDS_TEXT_CONTEXT_WAITING) + L"\r\n" ;
		}
		// 文章専用の未準備と保護拒否は通常ASR失敗から区別します
		if( card.bounded.latestRequest && card.bounded.latestRequest->profileId == K_TEXT_CONTEXT_PROFILE_ID &&
		   card.bounded.latestTerminal) {
			const auto &terminal = *card.bounded.latestTerminal ; // 本文なしの元診断
			// 生成済み拒否候補は保持し、標準表示へは昇格しません
			if( terminal.errorCode == static_cast<std::uint32_t>( WholeTextBoundedDiagnostic::PROTECTED_SPAN_CHANGED)) {
				text += resource( IDS_TEXT_CONTEXT_PROTECTED) + L"\r\n" ;
			} else if( terminal.errorKind == ErrorKind::MODEL_MISSING ||
					  terminal.errorKind == ErrorKind::MODEL_REJECTED) { // 未準備/破損の案内
				text += resource( IDS_TEXT_CONTEXT_MISSING) + L"\r\n" ;
			}
		}
		if( card.wholeText.state != ReadingState::IDLE) {
			text += wholeTextStatus( card.wholeText).text() + L"\r\n" ;
		}
		chatHeading_ = text ;
		// 完了結果は同じ話者をまとめた全幅背景の発話として表示する
		if( card.result) {
			requestChatDocument( false) ;
		} else {
			documentOperation_ = 0 ;
			documentRequest_.reset() ;
			document_.reset() ;
			SetWindowTextW( chat_, text.c_str()) ;
		}
		refreshDocumentActions() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定したfile内の話者を選び、保存済み表示名を編集欄へcopyします
	 */
	//------------------------------------------------------------------------------
	void updateName()
	{
		const int  speaker = static_cast<int>( SendDlgItemMessageW( renameWindow_, IDC_SPEAKER, CB_GETCURSEL, 0, 0)) + 1 ;
		const auto found = renameSource_->speakerNames.find( speaker) ; // 固定sourceだけ
		SetDlgItemTextW( renameWindow_, IDC_NAME,
						found == renameSource_->speakerNames.end() ? L"" : toWide( found->second).c_str()) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じfile/sourceだけへ名前変更を適用します
	 */
	//------------------------------------------------------------------------------
	void renameSpeaker()
	{
		const auto target = RowActionTarget{renameHistory_, renameSource_, {}, 0, false} ; // 固定行
		const auto index = resolveRow( target) ; // 別行/sourceへ読み替えません
		// 処理中のsourceと未知/削除行は保持します
		if( index >= cards_.size() || !renameSource_ || rowOperation( cards_[index])) {
			return ;
		}
		const int speaker = static_cast<int>( SendDlgItemMessageW( renameWindow_, IDC_SPEAKER, CB_GETCURSEL, 0, 0)) + 1 ;
		// 実在するfile内番号だけを変更します
		if( speaker < 1 || speaker > renameSource_->speakerCount) {
			return ;
		}
		std::array<wchar_t, 65> name{} ; // 最大64 UTF-16 code unit
		GetDlgItemTextW( renameWindow_, IDC_NAME, name.data(), static_cast<int>( name.size())) ;
		auto edited = std::make_shared<Transcript>( *renameSource_) ; // 元本文不変のsnapshot
		// 空欄だけは既定名へ戻します
		if( name.front() == L'\0') {
			edited->speakerNames.erase( speaker) ;
		} else {
			edited->speakerNames[speaker] = toUtf8( name.data()) ;
		}
		cards_[index].result = std::move( edited) ;
		cards_[index].wholeText.source = cards_[index].result ;
		ShowWindow( renameWindow_, SW_HIDE) ;
		renameSource_.reset() ;
		renameHistory_ = 0 ;
		// 別行の閲覧を勝手に切り替えず、同じ行だけを再描画します
		if( selected_ == index) {
			showCard() ;
		}
		saveHistory() ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 変更対象file/sourceを固定して小さいdialogを開きます
	 * @param[in] target 対象
	 */
	//------------------------------------------------------------------------------
	void showSpeakerRename( const RowActionTarget &target)
	{
		const auto index = resolveRow( target) ; // 同じ安定ID/sourceだけ
		// 結果/話者がない行や処理中ownerは編集しません
		if( index >= cards_.size() || !target.source || target.source->speakerCount < 1 || rowOperation( cards_[index])) {
			return ;
		}
		renameHistory_ = target.id ;
		renameSource_ = target.source ;
		// main所有の同じ非modal dialogを再利用します
		if( !IsWindow( renameWindow_)) {
			renameWindow_ = CreateDialogParamW( GetModuleHandleW( nullptr), MAKEINTRESOURCEW(IDD_RENAME), window_,
											   renameProc, reinterpret_cast<LPARAM>( this)) ;
			// 生成失敗は既存UI例外境界へ返します
			if( !renameWindow_) {
				throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
			}
		}
		SetWindowTextW( renameWindow_,
					   (resource( IDS_RENAME_MENU) + L" — " + cards_[index].path.filename().native()).c_str()) ;
		SetDlgItemTextW( renameWindow_, IDC_RENAME, resource( IDS_SETTINGS_APPLY).c_str()) ;
		SetDlgItemTextW( renameWindow_, IDCANCEL, resource( IDS_SETTINGS_CLOSE).c_str()) ;
		SendDlgItemMessageW( renameWindow_, IDC_NAME, EM_SETLIMITTEXT, 64, 0) ;
		SendDlgItemMessageW( renameWindow_, IDC_SPEAKER, CB_RESETCONTENT, 0, 0) ;
		// 原文に存在する同じfile内の話者番号だけを列挙します
		for( int speaker = 1; speaker <= renameSource_->speakerCount; ++speaker) {
			const auto label = resource( IDS_SPEAKER) + std::to_wstring( speaker) ; // 同期copy
			SendDlgItemMessageW( renameWindow_, IDC_SPEAKER, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>( label.c_str())) ;
		}
		SendDlgItemMessageW( renameWindow_, IDC_SPEAKER, CB_SETCURSEL, 0, 0) ;
		updateName() ;
		// 自所有SW_HIDE試験から実表示を開始しません
		if( IsWindowVisible( window_)) {
			ShowWindow( renameWindow_, SW_SHOWNA) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 話者名dialogの明示適用/選択とcloseを、同じ原文ownerへ束縛したUI操作へ渡します
	 * @param[in] window 自dialogの通知中有効なHWND。DWLP_USERはMainWindowの非所有pointerです
	 * @param[in] message WM_INITDIALOG、WM_COMMAND、WM_CLOSE等のWin32通知種別です
	 * @param[in] wparam WM_COMMANDではLOWORDが操作ID、HIWORDがcontrol通知codeです。他の通知では使用しません
	 * @param[in] lparam WM_INITDIALOGではdialog寿命中有効な非null MainWindow pointerを借用登録します。他の通知では使用しません
	 * @return 初期化/close/Esc/適用/有効な話者選択を処理したらTRUE、ownerなし・未処理・例外後はFALSEです。適用の保存完了を表しません
	 * @note closeは非表示と対象ownerの解除だけを行います。操作例外は固定codeの通知へ変換し、UI threadのcallback外へ出しません
	 */
	//------------------------------------------------------------------------------
	static INT_PTR CALLBACK renameProc( HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
	{
		auto *self = reinterpret_cast<MainWindow *>( GetWindowLongPtrW( window, DWLP_USER)) ; // 寿命内borrow
		// 同期初期化だけがthisを登録します
		if( message == WM_INITDIALOG) {
			SetWindowLongPtrW( window, DWLP_USER, lparam) ;
			return TRUE ;
		}
		// 初期化前の通知は標準処理へ渡します
		if( !self) {
			return FALSE ;
		}
		try {
			// 閉じるは適用せず未編集値を捨てます
			if( message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL)) {
				ShowWindow( window, SW_HIDE) ;
				self->renameHistory_ = 0 ;
				self->renameSource_.reset() ;
				return TRUE ;
			}
			// 同じdialogの適用buttonだけを確定します
			if( message == WM_COMMAND && LOWORD(wparam) == IDC_RENAME) {
				self->renameSpeaker() ;
				return TRUE ;
			}
			// 番号変更は固定sourceの表示名だけを読みます
			if( message == WM_COMMAND && LOWORD(wparam) == IDC_SPEAKER && HIWORD(wparam) == CBN_SELCHANGE &&
			   self->renameSource_) {
				self->updateName() ;
				return TRUE ;
			}
		} catch( const OperationError &error) {
			self->showCallbackError( error.kind(), error.code()) ;
		} catch( ...) {
			self->showCallbackError( ErrorKind::INTERNAL, 0, true) ;
		}
		return FALSE ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief dropされたpathを有界に取り出し、必ずHDROPを解放します
	 * @param[in] drop OSから所有権を受け取ったHDROP
	 */
	//------------------------------------------------------------------------------
	void acceptDrop( const HDROP drop)
	{
		// HDROPはcallbackを抜ける全経路で一度だけ解放する
		const auto release = []( HDROP value)
		{
			DragFinish( value) ;
		} ; // OS所有権の返却操作
		const std::unique_ptr<std::remove_pointer_t<HDROP>, decltype(release)> owned( drop, release) ;
		const UINT count = DragQueryFileW( drop, 0xFFFFFFFFU, nullptr, 0) ; // 投入file数
		// 過大なdrop列は一部を黙って取り込まず全体を拒否する
		if( count > K_MAXIMUM_OUTSTANDING_JOBS) {
			showMessage( IDS_FULL) ;
			return ;
		}
		// 各pathをOSから値として受け取り、shell commandへ変換しない
		for( UINT index = 0; index < count; ++index) {
			const UINT length = DragQueryFileW( drop, index, nullptr, 0) ; // 終端を除くUTF-16長
			// Win32のpath上限を超える値をallocationしない
			if( length == 0 || length > 32767) {
				showMessage( IDS_INVALID) ;
				continue ;
			}
			std::wstring path( static_cast<std::size_t>( length) + 1,
							  L'\0') ; // OSが書き込む所有buffer
			// 長さ問い合わせ後に得た値が一致する場合だけ投入する
			if( DragQueryFileW( drop, index, path.data(), length + 1) == length) {
				path.resize( length) ;
				addFile( path) ;
			}
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 標準dialogでlocalの対応音声を選択します
	 */
	//------------------------------------------------------------------------------
	void chooseInput()
	{
#if defined(KOEFUDE_WINDOW_TESTING)
        const auto selected = testHooks_.inputSelection() ; // OS dialogの代わりの所有戻り値
		// 取消しはカード/queue/保存を変更しない
		if( selected) {
			addFile( *selected) ;
		}
		return ;
#else
        ComPtr<IFileOpenDialog> dialog ; // UI STA内のmodal dialog
		HRESULT					status = CoCreateInstance( CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
														  IID_PPV_ARGS(&dialog)) ; // 最初の診断値
		// 作成失敗時には元HRESULTをUI境界へ伝える
		if( FAILED(status)) {
			throw OperationError( ErrorKind::INTERNAL, static_cast<std::uint32_t>( status)) ;
		}
		status = dialog->SetOptions( FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST) ;
		// local通常fileへ制限できないdialogは表示しない
		if( FAILED(status)) {
			throw OperationError( ErrorKind::INTERNAL, static_cast<std::uint32_t>( status)) ;
		}
		status = dialog->SetTitle( resource( IDS_OPEN_PICK).c_str()) ;
		// 操作目的を設定できない場合は元の診断値を保持する
		if( FAILED(status)) {
			throw OperationError( ErrorKind::INTERNAL, static_cast<std::uint32_t>( status)) ;
		}
		const auto				label = resource( IDS_AUDIO_FILTER) ; // dialog表示中のfilter文字列寿命
		const COMDLG_FILTERSPEC filter{label.c_str(), K_AUDIO_INPUT_PATTERN} ; // 対応形式だけを提示
		status = dialog->SetFileTypes( 1, &filter) ;
		// filter設定が失敗した状態で全形式対応のように表示しない
		if( FAILED(status)) {
			throw OperationError( ErrorKind::INTERNAL, static_cast<std::uint32_t>( status)) ;
		}
		status = dialog->Show( window_) ;
		// 利用者のdialog取消しは処理失敗として表示しない
		if( status == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
			return ;
		}
		ComPtr<IShellItem> item ; // 選択されたfilesystem項目
		// 選択失敗や仮想itemをpathとして扱わない
		if( FAILED(status) || FAILED(status = dialog->GetResult(&item))) {
			throw OperationError( ErrorKind::INVALID_INPUT, static_cast<std::uint32_t>( status)) ;
		}
		PWSTR rawPath = nullptr ; // COMが確保し、この関数がCoTaskMemFreeするpath
		status = item->GetDisplayName( SIGDN_FILESYSPATH, &rawPath) ;
		// filesystem pathを持たない入力を後続へ渡さない
		if( FAILED(status)) {
			throw OperationError( ErrorKind::INVALID_INPUT, static_cast<std::uint32_t>( status)) ;
		}
		const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned( rawPath, CoTaskMemFree) ;
		const std::filesystem::path								 path( rawPath) ; // dialog破棄後も有効な所有path
		addFile( path) ;
#endif
    }
	//------------------------------------------------------------------------------
	/**
	 * @brief 現在の行と準備済み表示版を固定し、明示選択した保存先の要求をworkerへ渡します
	 *
	 * @param[in] whole trueは専用の保存済み全文TXT操作です。API改善文の表示中は
	 * 通常APIの採用全文保存へ、それ以外は旧ローカル全文補正の
	 * Complete結果へ渡します。falseは通常の原文/整形版保存です
	 * 通常保存もAPI改善表示中のTXTでは採用API全文を保存します
	 * @param[in] speakerText trueは採用済み話者投影と現在の話者表示名を使う専用TXTです
	 * wholeより優先します。通常呼出しは両方false、旧全文操作は
	 * wholeだけtrue、話者付きTXT操作は本引数だけtrueにします
	 * @throws OperationError 保存dialogの作成、選択結果、filesystem pathの取得、
	 * または診断用初期folderの設定に失敗した場合はSaveFailedです
	 * @pre このWindowを所有するUI threadから呼び出します。controller_が有効であること
	 * @post 未準備/close開始、dialog取消し、追跡中の行/source/表示owner変更では保存しません
	 * 専用全文/話者TXTと通常整形版はTXTだけを提示し、通常原文はTXT/SRT/VTTを選べます
	 * 下位受付も所有結果・形式・有限queueを照合し、受付不可なら書込みを始めません
	 * @note modal dialog後に同じ行/source/表示ownerを再照合し、所有snapshotを渡します
	 * この関数の正常復帰は保存完了を意味せず、受理された要求の実保存はworkerと
	 * 後続完了通知が担います。再認識・文章改善の送信は開始しません
	 * 内部Window試験ではsave_selectionの戻り値を使い、OS dialogを開きません
	 */
	//------------------------------------------------------------------------------
	void chooseSave( const bool whole = false, const bool speakerText = false)
	{
		const auto target = rowTarget() ; // dialog中の選択/本文更新を別fileへ流しません
		// 準備済みの同じ表示版だけを保存します
		if( resolveRow( target, true) >= cards_.size()) {
			return ;
		}
#if defined(KOEFUDE_WINDOW_TESTING)
        const auto selected = testHooks_.saveSelection() ; // OS dialogの代わりの所有戻り値
		// 取消しでは保存要求を新しく作らない
		if( selected && resolveRow( target, true) < cards_.size()) {
			// 専用操作だけを独立TXT要求へ送ります
			if( speakerText) {
				// 専用書式はTXTだけとし、字幕を語時刻付きの投影へ読み替えません
				if( selected->format == ExportFormat::PLAIN_TEXT) {
					queueProjectionSave( selected->path) ;
				}
				// 全文整形保存は従来の専用経路を使う
			} else if( whole) {
				queueWholeTextSave( selected->path, selected->format) ;
			} else {
				queueSave( selected->path, selected->format) ;
			}
		}
#else
        ComPtr<IFileSaveDialog> dialog ; // 上書き確認を行う標準dialog
		HRESULT					status = CoCreateInstance( CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
														  IID_PPV_ARGS(&dialog)) ; // 保存dialog作成結果
		// 作成失敗をNULL dereferenceへ進めない
		if( FAILED(status)) {
			throw OperationError( ErrorKind::SAVE_FAILED, static_cast<std::uint32_t>( status)) ;
		}
		// 字幕も元の送信区間である制約と部分原文を同じ明示保存操作へ表示します
		if( cards_[selected_].apiAudio) {
			auto title = resource( IDS_AUDIO_TIME_BASIS) ; // 本文へ注記を混ぜません
			if( cards_[selected_].apiAudio->terminal != ApiAudioTerminal::COMPLETED) {
				title += L" / " + resource( IDS_AUDIO_PARTIAL) ;
			}
			static_cast<void>( dialog->SetTitle( title.c_str())) ;
		}
		const std::array<std::wstring, 3>	   labels{resource( IDS_FORMAT_TEXT), resource( IDS_FORMAT_SRT),
												  resource( IDS_FORMAT_VTT)} ; // filter表示の所有文字列
		// labelsの寿命内だけ表示名を借用する三形式の保存dialog filter
		const std::array<COMDLG_FILTERSPEC, 3> filters{
			{{labels[0].c_str(), L"*.txt"}, {labels[1].c_str(), L"*.srt"}, {labels[2].c_str(), L"*.vtt"}}} ;
		// この同期境界で所有する値、または確定状態の検証値です
		const bool formatted =
			whole || speakerText ||
			cardReadingDisplay( cards_[selected_]) == ReadingDisplay::FORMATTED ; // 字幕には元語時刻が必要
		dialog->SetFileTypes( formatted ? 1 : static_cast<UINT>( filters.size()), filters.data()) ;
		dialog->SetOptions( FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_OVERWRITEPROMPT | FOS_STRICTFILETYPES) ;
		// 明示試験の初期先だけを所有state/exportsへ向けます
		if( diagnosticLog_.currentMode()) {
			const auto		   folder = historyRoot_.parent_path() / L"exports" ; // 起動器作成済み
			ComPtr<IShellItem> initial ; // dialogへ渡す所有item
			const auto		   result = SHCreateItemFromParsingName( folder.c_str(), nullptr,
																	IID_PPV_ARGS(&initial)) ; // 値渡し
			// 不正初期先を通常exportへ黙って戻しません
			if( FAILED(result) || FAILED(dialog->SetFolder(initial.Get()))) {
				throw OperationError( ErrorKind::SAVE_FAILED, static_cast<std::uint32_t>( result)) ;
			}
		}
		dialog->SetDefaultExtension( L"txt") ;
		dialog->SetTitle( resource( IDS_SAVE_PICK).c_str()) ;
		dialog->SetFileName( cards_[selected_].path.stem().c_str()) ;
		status = dialog->Show( window_) ;
		// dialog取消しは現在の結果をそのまま保持する
		if( status == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
			return ;
		}
		ComPtr<IShellItem> item ; // 利用者が明示的に選んだ出力先
		UINT			   format = 0 ; // 1始まりの形式index
		// 未定義形式や仮想出力先を保存へ渡さない
		if( FAILED(status) || FAILED(status = dialog->GetResult(&item)) ||
		   FAILED(status = dialog->GetFileTypeIndex(&format)) || format < 1 || format > 3) {
			throw OperationError( ErrorKind::SAVE_FAILED, static_cast<std::uint32_t>( status)) ;
		}
		PWSTR rawPath = nullptr ; // COM所有権を受け取る出力path
		status = item->GetDisplayName( SIGDN_FILESYSPATH, &rawPath) ;
		// 通常filesystem以外へ暗黙保存しない
		if( FAILED(status)) {
			throw OperationError( ErrorKind::SAVE_FAILED, static_cast<std::uint32_t>( status)) ;
		}
		const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned( rawPath, CoTaskMemFree) ;
		// modal追跡中に選択/原文/表示版が変われば元結果を保持し保存しません
		if( resolveRow( target, true) >= cards_.size()) {
			return ;
		}
		// 選択後も専用全文と旧exportの型を分けます
		if( speakerText) {
			queueProjectionSave( rawPath) ;
			// 通常dialogでも全文整形の書式を保持する
		} else if( whole) {
			queueWholeTextSave( rawPath, static_cast<ExportFormat>( format - 1)) ;
		} else {
			queueSave( rawPath, static_cast<ExportFormat>( format - 1)) ;
		}
#endif
    }
	//------------------------------------------------------------------------------
	/**
	 * @brief 選択済み保存先と不変結果を、製品の有限workerへ送ります
	 * @param[in] path 明示選択されたlocal保存先。workerで改めて検証します
	 * @param[in] format Text/Srt/Vttのいずれか
	 * @throws OperationError 未定義の形式はSaveFailedです
	 */
	//------------------------------------------------------------------------------
	void queueSave( const std::filesystem::path &path, const ExportFormat format)
	{
		// 閉じ始めた画面や本文のないカードから保存要求を増やさない
		if( closing_ || selected_ >= cards_.size() || !cards_[selected_].result) {
			return ;
		}
		// dialog以外の内部入口でも、未定義の形式を保存先へ適用しない
		if( format != ExportFormat::PLAIN_TEXT && format != ExportFormat::SRT && format != ExportFormat::VTT) {
			throw OperationError( ErrorKind::SAVE_FAILED, ERROR_INVALID_PARAMETER) ;
		}
		// 採用API本文のTXTを同じworkerで保存し、字幕は元原文から出力します
		if( format == ExportFormat::PLAIN_TEXT && documentReady() && document_->request.showApiText) {
			ApiTextCorrectionJobRequest api ; // HTTPを呼ばない明示保存
			api.action = ApiTextAction::EXPORT ;
			api.source = cards_[selected_].result ;
			api.key = document_->view->key() ;
			api.record = cards_[selected_].apiText ;
			api.record.latest = api.record.adopted ;
			api.exportPath = path ;
			JobRequest job ;
			job.payload = std::move( api) ;
			const auto id = controller_->submit( std::move( job)) ;
			if( id) {
				saves_.emplace( id, cards_[selected_].id) ;
			}
			return ;
		}
		JobRequest		 request ; // 保存開始時点の不変snapshot
		ExportJobRequest save ; // 表示選択を受付時の所有値へ固定する
		save.output = path ;
		save.source = cards_[selected_].result ;
		save.formatted = cards_[selected_].reading.formatted ;
		save.display = cardReadingDisplay( cards_[selected_]) ;
		// 整形の補正語句へ字幕時刻を推測せず、原文選択を案内する
		if( save.display == ReadingDisplay::FORMATTED && format != ExportFormat::PLAIN_TEXT) {
			showMessage( IDS_READING_TEXT_ONLY) ;
			return ;
		}
		// hashの再計算はworkerに委ね、保存済みkeyだけを値として渡す
		if( save.formatted) {
			save.sourceKey = save.formatted->source ;
		}
		save.format = format ;
		request.payload = std::move( save) ;
		const auto id = controller_->submit( std::move( request)) ; // 同じ有限queueの受付結果
		// 上限超過時は成功表示せず、利用者へ再操作を案内する
		if( id == 0) {
			showMessage( IDS_FULL) ;
			return ;
		}
		saves_.emplace( id, cards_[selected_].id) ;
		diagnosticLog_.append( DiagnosticEvent::SAVE_ACCEPTED, id, static_cast<std::uint64_t>( format)) ;
		setStatus( {IDS_STAGES + static_cast<UINT>( Stage::SAVING)}) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 現在の行と準備済み表示版の全文を、明示操作でUnicode clipboardへコピーします
	 *
	 * @param[in] whole falseはPreparedChatDocument::full_textの全話者本文を選びます
	 * 話者filterには限定せず、通常原文/整形版ではその受付版、
	 * API改善表示/比較中では採用API全文、旧全文表示中では原文です
	 * trueはcorrected_full_textの保存済み完全本文を選び、空なら戻ります
	 * これは旧ローカル全文/bounded Completeの専用操作にも使われますが、
	 * API改善表示中には同じ欄へ準備された採用API全文が対象になります
	 * @throws OperationError clipboard転送領域の確保/lock、OpenClipboard、
	 * SetClipboardDataに失敗した場合はInternalです
	 * @pre このWindowを所有するUI threadから呼び出します
	 * @post 未準備/close開始/対象owner不一致、または専用本文が空ならclipboardを変更しません
	 * 成功時はNUL終端UTF-16をCF_UNICODETEXTとしてOSへ所有権移譲し、コピー完了を記録します
	 * 領域確保/lock/OpenClipboard失敗では既存clipboardを保持します
	 * EmptyClipboard後の転送失敗では既存内容の復元を保証せず、未移譲領域を解放します
	 * @note 保存・再認識・API送信を始めず、原文や採用本文を変更しません
	 * 内部Window試験では対象DTOだけを検証し、OS clipboardや完了logを操作しません
	 */
	//------------------------------------------------------------------------------
	void copyResult( const bool whole = false)
	{
		const auto target = rowTarget() ; // 同じsource/表示版の全文だけ
		// 未完了/別版の本文をclipboardへ出さない
		if( resolveRow( target, true) >= cards_.size()) {
			return ;
		}
		// 専用copyは保存済みCompleteだけを許可します
		if( whole && document_->correctedFullText.empty()) {
			return ;
		}
		// この同期境界で所有する値、または確定状態の検証値です
		const auto &text =
			whole ? document_->correctedFullText : document_->fullText ; // workerが準備した受付版の全話者本文
		// 内部試験は準備済みDTOを検証し、OS clipboardを操作しません
#if defined(KOEFUDE_WINDOW_TESTING)
        static_cast<void>( text) ;
		return ;
#else
        // clipboardへ所有権を移すためのNUL込みUTF-16領域。移譲前の失敗では解放する
		const HGLOBAL memory = GlobalAlloc( GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t)) ;
		// allocation失敗時に既存clipboardを消去しない
		if( !memory) {
			throw OperationError( ErrorKind::INTERNAL, ERROR_NOT_ENOUGH_MEMORY) ;
		}
		void *destination = GlobalLock( memory) ; // clipboard転送前の一時borrow
		// lock失敗では所有memoryを回収する
		if( !destination) {
			GlobalFree( memory) ;
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		memcpy( destination, text.c_str(), (text.size() + 1) * sizeof(wchar_t)) ;
		GlobalUnlock( memory) ;
		// 他processのclipboard利用中は内容を変更しない
		if( !OpenClipboard( window_)) {
			GlobalFree( memory) ;
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		EmptyClipboard() ;
		// この同期境界で所有する値、または確定状態の検証値です
		const HANDLE transferred = SetClipboardData( CF_UNICODETEXT, memory) ; // 成功でOSへ所有権移譲
		CloseClipboard() ;
		// 移譲失敗時だけ自分で解放する
		if( !transferred) {
			GlobalFree( memory) ;
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		diagnosticLog_.append( DiagnosticEvent::COPY_FINISHED, cards_[selected_].id) ;
#endif
    }
	//------------------------------------------------------------------------------
	/**
	 * @brief 既存timerで同じ原文/opの実経過だけを更新します
	 */
	//------------------------------------------------------------------------------
	void tickApiProgress()
	{
		// 終了開始後は新しい時計更新を要求しない
		if( !closing_) {
			// 自分の各行にある実進捗の経過だけを更新する
			for( auto &card : cards_) {
				// 進行中の投影だけに単調な観測時刻を与える
				if( card.projectionActive && card.status.projection) {
					card.status.projection->observedMs = progressMonotonicMs() ;
					// 閲覧中の同じ行だけstatus barを更新する
					if( selected_ < cards_.size() && cards_[selected_].id == card.id) {
						setStatus( card.status) ;
					}
				}
			}
		}
		// 既存100ms timerは稼働帯の位置だけを変え、工程割合を増やしません
		if( statusProgress_.activity) {
			statusProgress_.activity = !closing_ && apiClock_.active && selected_ < cards_.size() &&
									   apiClock_.matches( cards_[selected_].id, cards_[selected_].result) ;
			statusProgress_.activityFrame = (statusProgress_.activityFrame + 1) % 40 ;
			InvalidateRect( status_, nullptr, FALSE) ;
		}
		// 同じactive音声受付のstatusだけを実monotonic経過で更新し、終端/closeでは凍結します
		if( !closing_) {
			for( auto &card : cards_) {
				if( !card.busy || !card.operationId || !card.status.audio || card.status.audio->ended) {
					continue ;
				}
				card.status.audio->progress.observedMs = progressMonotonicMs() ;
				if( selected_ < cards_.size() && &card == &cards_[selected_]) {
					setStatus( card.status) ;
				}
			}
		}
		// API専用時計以外のmetadata/音声/旧結果へ経過を混ぜません
		if( !apiClock_.active || !apiStatus_.api || !apiClock_.tick( progressMonotonicMs())) {
			return ;
		}
		const auto found = std::find_if( cards_.begin(), cards_.end(),
			//------------------------------------------------------------------------------
			/**
			 * @brief 同じ行/ownerを照合します
			 * @param[in] card 有界候補
			 * @return 一致
			 */
			//------------------------------------------------------------------------------
			[this]( const FileCard &card)
			{
				return apiClock_.matches( card.id, card.result) ;
			}) ;
		// 原文差替え/削除後の時計を現在行へ再利用しません
		if( found == cards_.end() || apiStatusHistory_ != apiClock_.history || closing_) {
			apiClock_.active = false ;
			return ;
		}
		apiClock_.decorate( *apiStatus_.api) ;
		// 更新は同じ行の一列と選択中のstatusだけ。文書再構築やI/Oは行いません
		auto text = compactTableSymbol( compactImprovementState( found->apiText, found->id)) ;
		ListView_SetItemText( control( IDC_HISTORY), static_cast<int>( found - cards_.begin()), 3, text.data()) ;
		// 選択移動先のstatusへ他行の経過を出しません
		if( selected_ < cards_.size() && cards_[selected_].id == apiClock_.history) {
			setStatus( apiStatus_) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 非永続の案内対象を現在の同じ原文ownerへ照合します
	 * @param[in] target 所有値
	 * @return 同じ行/原文で未採用ならtrue
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool parakeetTargetValid( const ParakeetApiTarget &target) const
	{
		const auto found = std::find_if( cards_.begin(), cards_.end(),
			//------------------------------------------------------------------------------
			/**
			 * @brief 同じ永続IDだけを照合します
			 * @param[in] card 候補
			 * @return 一致
			 */
			//------------------------------------------------------------------------------
			[&]( const FileCard &card)
			{
				return card.id == target.history ;
			}) ; // 最大32件
		return found != cards_.end() && target.source && found->result == target.source && !found->audioApiRequested &&
			   !found->apiAudio && !found->apiText.adopted ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同意済み対象の現在の実行可否だけを照合します
	 * @param[in] target 同じsource
	 * @return 保存/行処理の未完了がなければtrue
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool parakeetTargetIdle( const ParakeetApiTarget &target) const
	{
		const auto found = std::find_if( cards_.begin(), cards_.end(),
			//------------------------------------------------------------------------------
			/**
			 * @brief 同じIDだけを照合します
			 * @param[in] card 候補
			 * @return 一致
			 */
			//------------------------------------------------------------------------------
			[&]( const FileCard &card)
			{
				return card.id == target.history ;
			}) ; // 同期borrowだけ
		return found != cards_.end() && !found->busy && !found->wholeText.active && !found->reading.active &&
			   !found->apiText.saveFailed ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 簡潔な全文送信説明と二つの明示buttonを表示します
	 * @param[in] target modal中も生存する所有値
	 * @return 改善するを選択
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool parakeetImprovePrompt( const ParakeetApiTarget &target)
	{
		FollowupModalScope modal( followupModalDepth_) ; // 再入pollは別案内を出しません
#if defined(KOEFUDE_WINDOW_TESTING)
        ++testHooks_.parakeetOffers ;
		return testHooks_.parakeetPrompt ? testHooks_.parakeetPrompt( target.history) : false ;
#else
        std::wstring filename ; // iteratorをmodalへ持ち越さない表示名の所有copy
		{
			const auto found = std::find_if( cards_.begin(), cards_.end(),
				//------------------------------------------------------------------------------
				/**
				 * @brief 表示する同じ行だけを探します
				 * @param[in] card 候補
				 * @return 一致
				 */
				//------------------------------------------------------------------------------
				[&]( const FileCard &card)
				{
					return card.id == target.history ;
				}) ;
			// 削除済み対象を現在選択の名前へ読み替えません
			if( found == cards_.end()) {
				return false ;
			}
			filename = found->path.filename().native() ;
		}
		const auto title = resource( IDS_TITLE) ; // resource由来の短い製品名
		const auto content = filename + L"\r\n\r\n" + resource( IDS_PARAKEET_API_OFFER) ;
		const auto improve = resource( IDS_PARAKEET_API_IMPROVE) ; // 明示送信button
		const auto later = resource( IDS_PARAKEET_API_LATER) ; // 辞退後も行メニューから実行可能
		const int  chosen = showImproveDialog( window_, content.c_str(), title.c_str(), improve.c_str(), later.c_str()) ;
		// 生成失敗を同意へ置き換えず、元診断値をcallback境界へ戻します
		if( !chosen) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		return chosen == IDYES ;
#endif
    }
	//------------------------------------------------------------------------------
	/**
	 * @brief 保存ACK後の案内と、一つの開いた設定内の明示続行を有限に進めます
	 * @note UI thread専用。modal前後に所有値だけを持ち、原文をcopy/hashしません
	 */
	//------------------------------------------------------------------------------
	void pumpParakeetFollowup()
	{
		// close/他modal/再入から新しい案内や送信を開始しません
		if( closing_ || followupModalDepth_ || !historyReady_ || !IsWindowEnabled( window_)) {
			return ;
		}
		const auto now = progressMonotonicMs() ; // 非永続の単調時計
		// 設定内の意図は最大一件だけで、queue中の次の案内へ進みません
		if( parakeetFollowup_.pending()) {
			const auto pending = *parakeetFollowup_.pending() ; // 外部呼出しを跨ぐ所有copy
			const bool sameSettings =
				!pending.session || (settingsWindow_.opened() && settingsWindow_.session() == pending.session) ;
			// close/reopen/原文置換/削除では別の原文へ送信意図を移しません
			if( !sameSettings || !parakeetTargetValid( pending.target)) {
				parakeetFollowup_.finish() ;
				return ;
			}
			// 長時間放置/時計異常は無言失効にせず、現在の設定内へ再実行方法を示します
			if( now < pending.startedMs || now - pending.startedMs > ParakeetApiFollowup::K_LIFETIME_MS) {
				parakeetFollowup_.finish() ;
				// 同じ開いた設定だけへ失効説明を表示します
				if( pending.session && sameSettings) {
					settingsWindow_.followupNotice( IDS_PARAKEET_API_EXPIRED) ;
				} else {
					setStatus( {IDS_PARAKEET_API_EXPIRED}) ;
				}
				return ;
			}
			// 保存失敗/未ACK/他のAPI要求中には本文を送りません
			if( historyDirty_ || historyFailed_ || apiTextWindow_.busy() || !parakeetTargetIdle( pending.target)) {
				return ;
			}
			// 同意直後、準備済みなら余分な設定画面を挟まず既存Improveへ渡します
			if( !pending.session && apiTextWindow_.ready() && !settingsWindow_.opened()) {
				parakeetFollowup_.finish() ; // enqueueより前に消費して二重ACKを拒否
				static_cast<void>( openApi( pending.target.history, false)) ;
				return ;
			}
			// 不足時だけ既存の文章改善tabを開き、このsessionを一度固定します
			if( !pending.session) {
				showSettings() ;
				// 起動復元/保存barrierや生成失敗を無期限の予約へ変えません
				if( !settingsWindow_.opened()) {
					parakeetFollowup_.finish() ;
					return ;
				}
				auto bound = pending ; // 開いたsessionへだけ束縛する所有値
				bound.session = settingsWindow_.session() ;
				parakeetFollowup_.wait( bound) ;
				settingsWindow_.followupPage( IDS_PARAKEET_API_SETUP) ;
				return ;
			}
			// local/API両方の正常保存ACK後だけ設定を閉じ、同じ原文へ一度続行します
			if( settingsWindow_.followupApplied()) {
				parakeetFollowup_.finish() ;
				settingsWindow_.close() ;
				static_cast<void>( openApi( pending.target.history, false)) ;
			}
			return ;
		}
		// 開いた設定や他のAPI要求へ次の完了案内を割り込ませません
		if( settingsWindow_.opened() || apiTextWindow_.busy() || historyDirty_ || historyFailed_) {
			return ;
		}
		parakeetFollowup_.discardStale(
			//------------------------------------------------------------------------------
			/**
			 * @brief 保存後に変わった対象を破棄します
			 * @param[in] target 候補
			 * @return 現在owner
			 */
			//------------------------------------------------------------------------------
			[this]( const ParakeetApiTarget &target)
			{
				return parakeetTargetValid( target) ;
			}) ;
		const auto target = parakeetFollowup_.take() ; // 一件をmodal前に取り出す所有値
		// 原文の保存ACKがまだない場合は案内を出しません
		if( !target) {
			return ;
		}
		bool improve = false ; // dialog失敗も同意へ変えません
		try {
			improve = parakeetImprovePrompt( *target) ; // vector参照なしのmodal
		} catch( ...) {
			static_cast<void>( parakeetFollowup_.answer( false)) ; // 当該案内だけを終端にします
			throw ;
		}
		auto accepted = parakeetFollowup_.answer( improve) ; // 辞退/再入消費はここで終端
		// 待機中の削除/別原文/先行Improveを現在選択へ読み替えません
		if( accepted && !closing_ && parakeetTargetValid( *accepted)) {
			parakeetFollowup_.wait( {std::move( *accepted), 0, progressMonotonicMs()}) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 最新eventをIDで照合し、終了済み結果を各カードへ反映します
	 */
	//------------------------------------------------------------------------------
	void collectEvents()
	{
		// WM_CREATEの途中ではworkerがまだ存在しない
		if( !controller_) {
			return ;
		}
		collectTranscriptionSettings() ;
		collectUpdate() ;
		collectHistoryEvent() ;
		collectModelEvent() ;
		wholeTextPreparationWindow_.poll() ;
		collectDocumentEvent() ;
		bool changed = false ; // 選択カードの本文を再描画する必要がある場合true
		bool apiHistoryChanged = false ; // 別行のACKでも既存表示選好の保存は維持します
		// 最大8件の最新状態を一度だけ受領する
		for( const auto &event : controller_->takeEvents()) {
			// API終端は対象sourceとopを再照合し選択移動でも別カードへ適用しません
			const bool direct = apiTextWindow_.directEvent( event.id) ; // 受領でactiveが解放される前
			if( apiTextWindow_.receive( event)) {
				// 通常行の進捗は確認Windowへ出さず、テーブル/status barへ表示します
				if( direct) {
					apiStatusHistory_ = apiTextWindow_.historyId() ;
					// direct要求ownerを設定metadata/現在選択へ読み替えません
					if( event.apiText) {
						apiStatusSource_ = event.apiText->request.source ;
					}
					apiStatus_ = event.terminal
									 ? UiStatus{IDS_API_FAILED, -1, event.error, event.code, apiTextEventStatus( event)}
									 : UiStatus{IDS_API_PREPARING} ;
					// 初回生成の同opだけへ区間数と時計を付け、HTTP0表示保存は独立に保ちます
					if( apiClock_.receive( event)) {
						// active段階にも終端と同じ固定表示型を使用します
						if( !apiStatus_.api) {
							apiStatus_.api = ApiTextStatusView{} ;
						}
						apiClock_.decorate( *apiStatus_.api) ;
					}
					// 同じ選択行/sourceだけへ説明と実工程の塗りを一緒に適用します
					if( selected_ < cards_.size() && cards_[selected_].id == apiStatusHistory_ &&
					   detail::sameTranscriptOwner( apiStatusSource_, cards_[selected_].result)) {
						setStatus( apiStatus_) ;
					}
					refreshHistory() ;
					if( event.terminal && !event.succeeded && event.error != ErrorKind::CANCELLED) {
						const auto target =
							std::find_if( cards_.begin(), cards_.end(),
										 [this]( const auto &card)
										 {
											 return card.id == apiStatusHistory_ &&
													detail::sameTranscriptOwner( card.result, apiStatusSource_) ;
										 }) ;
						if( target != cards_.end()) {
							queueStatusNotice( StatusNoticeKind::API, event.id, apiStatus_, &*target) ;
						}
					}
				}
				if( event.terminal && event.apiText && event.apiText->persistence.operationId) {
					const auto &ack = event.apiText->persistence ; // 保存workerからのtyped値
					const auto	found = std::find_if( cards_.begin(), cards_.end(),
						//------------------------------------------------------------------------------
						/**
						 * @brief 対象原文ownerを探します
						 * @param[in] c カード
						 * @return 全key一致
						 */
						//------------------------------------------------------------------------------
						[&ack]( const auto &c)
						{
							return c.id == ack.key.historyId &&
								   detail::sameTranscriptOwner( c.result, ack.source) ;
						}) ; // 最大32件
					// 保存ackがない採用は画面の既定本文へ反映しません
					if( found != cards_.end()) {
						found->pendingCompareSource.reset() ; // 後発recordへ旧予約を移植しません
						found->pendingCompareCandidate.reset() ;
						if( ack.committed) {
							found->apiText = ack.record ;
						} else {
							found->apiText.latest = ack.record.latest ;
							found->apiText.update = ack.record.update ;
							found->apiText.saveFailed = true ;
						}
						// 明示補正文選択は旧Original意図を変更する利用者操作です
						if( ack.committed && event.succeeded &&
						   (event.apiText->request.action == ApiTextAction::CORRECTED ||
							event.apiText->request.action == ApiTextAction::IMPROVE ||
							(event.apiText->request.action == ApiTextAction::RETRY_SAVE && ack.record.adopted &&
							 ack.record.latest == ack.record.adopted &&
							 ack.record.adopted->consent.intent == ApiTextIntent::ROW_MENU))) {
							found->resultPreference = ResultPreference::AUTOMATIC ;
						}
						// 原文切替も同じsidecar保存ack後だけ変更します
						if( ack.committed && event.succeeded &&
						   event.apiText->request.action == ApiTextAction::ORIGINAL) {
							found->resultPreference = ResultPreference::ORIGINAL ;
						}
						// 通常Improveの同op完全採用ACKだけをsession内の比較予約にします
						if( direct && !closing_ && event.succeeded && ack.committed &&
						   event.apiText->request.action == ApiTextAction::IMPROVE &&
						   event.apiText->request.operationId == event.id && ack.operationId == event.id &&
						   ack.key == event.apiText->request.key &&
						   detail::sameTranscriptOwner( ack.source, event.apiText->request.source) &&
						   ack.record.adopted && ack.record.latest && ack.record.adopted->operationId == event.id &&
						   ack.record.adopted->state == ApiTextState::CANDIDATE_READY &&
						   sameApiTextAttempt( ack.record.adopted, ack.record.latest) &&
						   ack.record.adopted->draft.source == ack.key &&
						   ack.record.adopted->consent.intent == ApiTextIntent::ROW_MENU &&
						   !ack.record.adopted->candidate.empty() && !ack.record.showOriginal &&
						   !ack.record.saveFailed) {
							found->pendingCompareSource = found->result ;
							found->pendingCompareCandidate = found->apiText.adopted ;
						}
						apiHistoryChanged = true ;
					}
				}
				// 他行の成功でも閲覧中の文書/左右mode/非0scrollを再構築しません
				changed = changed || (direct && event.terminal && selected_ < cards_.size() &&
									  cards_[selected_].id == apiStatusHistory_ &&
									  detail::sameTranscriptOwner( apiStatusSource_, cards_[selected_].result)) ;
				continue ;
			}
			// 整形は元ASRのstatus/resultへ触れず、専用状態へ反映する
			if( collectWholeTextEvent( event) || collectReadingEvent( event)) {
				changed = changed || event.terminal ;
				continue ;
			}
			const auto save = saves_.find( event.id) ; // 保存要求は推論履歴と別IDで照合
			// 保存結果を推論本文の置換として適用しない
			if( save != saves_.end()) {
				// 終端になるまで保存受付の対応を維持する
				if( event.terminal) {
					diagnosticLog_.append( DiagnosticEvent::SAVE_FINISHED, event.id, event.succeeded ? 1 : 0,
										  event.succeeded ? 0 : static_cast<std::uint64_t>( event.error), event.code) ;
					setStatus( event.succeeded ? UiStatus{IDS_SAVED} : UiStatus{0, -1, event.error, event.code}) ;
					if( !event.succeeded && event.error != ErrorKind::CANCELLED) {
						queueStatusNotice( StatusNoticeKind::EXPORT, event.id, {0, -1, event.error, event.code}) ;
					}
					saves_.erase( save) ;
				}
				continue ;
			}
			const auto found = std::find_if( cards_.begin(), cards_.end(),
				//------------------------------------------------------------------------------
				/**
				 * @brief 通知の操作IDと一致するカードを照合します
				 * @param[in] card 同期find_if中だけ借用する既存カード
				 * @return 現在の操作IDと同じならtrue
				 * @note 参照するeventは同期検索の完了まで有効で、callbackを保存しません
				 */
				//------------------------------------------------------------------------------
				[&event]( const FileCard &card)
				{
					return card.operationId == event.id ;
				}) ; // 古い・未知IDを別カードへ適用しない検索
			// clear済み・未受付IDは表示へ戻さない
			if( found == cards_.end()) {
				continue ;
			}
			// 投影終端ではraw/API record/文章改善をASR結果で上書きしない
			if( found->projectionActive || event.speakerText) {
				// 投影workerが通知した実段階だけを取り込む
				if( event.progress.speakerText.phase != ApiTextPhase::NONE) {
					found->status.projection = event.progress.speakerText ;
				}
				// 終端でbusy/取消し所有権を解除し、本文は保存ACKで判定する
				if( event.terminal) {
					found->busy = false ;
					found->projectionActive = false ;
					found->operationId = 0 ;
					found->cancelling = false ;
					// 元APIの小targetが同じ完了だけを採用する
					if( event.speakerText && found->apiAudio &&
					   speakerTextTarget( *found->apiAudio) == event.speakerText->persistence.target) {
						const auto &saved = event.speakerText->persistence ; // 今回の同source ACK
						// 未commitの失敗には空の前採用を流用せず、表示中の前採用を維持します
						if( saved.committed || saved.record.adopted) {
							found->speakerText = saved.record ;
							// 採用なしの失敗でも最新試行は原文と独立して残す
						} else if( saved.record.latest) {
							found->speakerText.latest = saved.record.latest ;
						}
					}
					// 進捗通知が省略された事前拒否にも独立終端を作る
					if( !found->status.projection) {
						found->status.projection = ApiTextProgress{} ;
					}
					// 元音声から分離した同opの投影終端
					auto &status = *found->status.projection ;
					status.phase = event.succeeded						 ? ApiTextPhase::COMPLETED
								   : event.error == ErrorKind::CANCELLED ? ApiTextPhase::CANCELLED
																		 : ApiTextPhase::FAILURE ;
					status.observedMs = progressMonotonicMs() ;
					// 保存ACK成功時だけ実窓完了を確定する
					if( event.succeeded) {
						status.completedUnits = status.totalUnits ;
					}
					if( !event.succeeded && event.error != ErrorKind::CANCELLED) {
						found->status.error = event.error ;
						found->status.code = event.code ;
						queueStatusNotice( StatusNoticeKind::PROJECTION, event.id, found->status, &*found) ;
					}
					// 完了した同じ行を閲覧中なら本文を再準備する
					if( selected_ < cards_.size() && cards_[selected_].id == found->id) {
						changed = true ;
					}
				}
				// 他行の閲覧/mode/scrollへ元行の終端を適用しない
				if( selected_ < cards_.size() && cards_[selected_].id == found->id) {
					setStatus( found->status) ;
				}
				refreshHistory() ;
				continue ;
			}
			// 終端結果だけが本文と最終診断を確定できる
			if( event.terminal) {
				// 診断schemaの終端分類。成功1、警告2、本文なし3、取消し4、失敗5
				const std::uint64_t finalState = event.succeeded ? (event.result && event.result->diarizationWarning ? 2
																	: event.result && event.result->lines.empty()	 ? 3
																												  : 1)
												 : event.error == ErrorKind::CANCELLED ? 4
																					   : 5 ;
				diagnosticLog_.append( DiagnosticEvent::JOB_FINISHED, event.id, finalState,
									  event.result ? event.result->lines.size() : 0, event.code) ;
				DiagnosticExecution execution ; // 本文やpathをログへ渡さない終端metadata
				// 対応IDの結果DTOがある場合だけ実行値を設定し、失敗時は未観測を保持する
				if( event.result) {
					execution.resultAvailable = true ;
					execution.model = event.result->asrModel ;
					execution.profile = event.result->asrProfile ;
					execution.usedGpu = event.result->usedGpu ;
					execution.cpuRetried = event.result->cpuRetried ;
				}
				diagnosticLog_.appendExecution( event.id, execution) ;
				found->busy = false ;
				found->operationId = 0 ;
				const bool preparation =
					found->audioResumeRequested && (!event.apiAudio || event.apiAudio->resumePreparationFailed) ;
				// typed/汎用の再開準備失敗は旧owner/recordを消しません
				if( !preparation) {
					found->result = event.result ;
					found->speakerText = {} ;
					found->status.projection.reset() ;
					if( event.apiAudio) {
						found->apiAudio = std::shared_ptr<const ApiAudioRecord>(
							event.apiAudio,
							&event.apiAudio->persistence.record) ; // 大recordのUI copyなし
					} else {
						found->apiAudio.reset() ;
					}
				}
				found->audioResumeRequested = false ;
				// 準備拒否では原文が変わらないため、前採用と比較予約も保持します
				if( !preparation) {
					found->apiText = {} ; // 新しいASR原文へ旧候補を移植しません
					found->pendingCompareSource.reset() ;
					found->pendingCompareCandidate.reset() ;
				}
				bool parakeetCompletionOffered = false ; // 警告でも完了確認を二重にしません
				// ADR1376: 新規local Parakeetだけを一度判定し、保存ACKまでは案内しません
				if( std::exchange( found->parakeetOfferRequested, false) && event.succeeded && !event.apiAudio &&
				   found->result &&
				   std::any_of( found->result->lines.begin(), found->result->lines.end(),
							   //------------------------------------------------------------------------------
							   /**
							    * @brief 内容のある原文だけを案内します
							    * @param[in] line 発言
							    * @return 本文byteあり
							    */
							   //------------------------------------------------------------------------------
							   []( const TranscriptLine &line)
							   {
								   return !line.text.empty() ;
							   })) {
					parakeetFollowup_.offer( {found->id, found->result, false}) ;
					parakeetCompletionOffered = true ;
				}
				// local/API補正の実行は「改善する」の明示選択だけから開始します
				// この同期境界で所有する値、または確定状態の検証値です
				const UINT completed = event.result && event.result->diarizationWarning ? IDS_WARNING
									   : event.result && event.result->lines.empty()
										   ? IDS_NO_TEXT
										   : IDS_COMPLETED ; // 成功時の結果分類
				found->status = event.succeeded ? UiStatus{completed} : UiStatus{0, -1, event.error, event.code} ;
				// 音声APIの部分終端/HTTP診断を通常GPU復旧案内から分離します
				if( preparation) {
					ApiAudioStatusView view ; // 今回の診断だけを表示し旧HTTPは保持recordに残す
					view.ended = true ;
					view.preparationFailed = true ;
					view.code = event.code ;
					view.terminal =
						event.error == ErrorKind::CANCELLED ? ApiAudioTerminal::CANCELLED : ApiAudioTerminal::FAILURE ;
					view.prefix = found->result && !found->result->lines.empty() ;
					view.progress = event.progress.apiAudio ;
					found->status.audio = view ;
				} else if( found->apiAudio) {
					found->status.audio = apiAudioStatus( *found->apiAudio) ;
					found->status.audio->progress = event.progress.apiAudio ;
					found->status.resourceId = event.succeeded						 ? completed
											   : event.error == ErrorKind::CANCELLED ? IDS_CANCELLED
																					 : 0 ;
				} else if( found->audioApiRequested && !event.succeeded) {
					ApiAudioStatusView view ; // plan成立前もlocal GPU復旧案内へ流しません
					view.ended = true ;
					view.code = event.code ;
					view.saveFailed = event.error == ErrorKind::SAVE_FAILED ;
					view.terminal =
						event.error == ErrorKind::CANCELLED ? ApiAudioTerminal::CANCELLED : ApiAudioTerminal::FAILURE ;
					found->status.audio = view ;
				}
				if( (!event.succeeded && event.error != ErrorKind::CANCELLED) ||
				   (event.succeeded &&
					(completed == IDS_NO_TEXT || (completed == IDS_WARNING && !parakeetCompletionOffered)))) {
					queueStatusNotice( StatusNoticeKind::ASR, event.id, found->status, &*found) ;
				}
				apiHistoryChanged = true ;
				// 新しい明示音声操作の完全raw ACKだけに、別opのPC内後続を一回受付ける
				if( found->projectionFollowup) {
					found->projectionFollowup = false ;
					// 原文最終ACK済みの明示Quality要求だけ一度後続する
					if( !closing_ && event.succeeded && event.apiAudio && event.apiAudio->persistence.committed &&
					   found->apiAudio && found->apiAudio->terminal == ApiAudioTerminal::COMPLETED) {
						// 最終原文ACK後に得た適格prefix token
						const auto token = speakerTextTarget( *found->apiAudio) ;
						// 保存prefixが適格な場合だけ別投影opを発行する
						if( token) {
							// 本文をcopyしない小target付き投影要求
							SpeakerTextJobRequest q ;
							q.target = *token ;
							q.modelRoot = found->projectionModelRoot ;
							submitProjection( std::move( q)) ;
						}
					}
				}
				// 他行完了で閲覧中の文書/進捗を上書きしません
				if( selected_ < cards_.size() && &cards_[selected_] == &*found) {
					changed = true ;
					setStatus( found->status) ;
				}
			} else {
				found->status = found->cancelling
									? UiStatus{IDS_CANCELLING}
									: UiStatus{event.progress.wholeTextPhase == WholeTextPhase::AUDIO_IDENTITY
												   ? IDS_WHOLE_PHASES
												   : IDS_STAGES + static_cast<UINT>( event.progress.stage),
											   event.progress.percent} ;
				// API音声だけは実段階/保存済み区間/経過を独立表示します
				if( event.progress.apiAudio.phase != ApiTextPhase::NONE) {
					ApiAudioStatusView view ;
					view.progress = event.progress.apiAudio ;
					found->status.audio = view ;
				}
				if( selected_ < cards_.size() && &cards_[selected_] == &*found) {
					setStatus( found->status) ;
				}
				// 選択中の同じ行だけ実通知を反映します
				if( event.progress.percent >= 0 && selected_ < cards_.size() && &cards_[selected_] == &*found) {
					statusProgress_.percent = event.progress.percent ;
				}
				// 未完了カードでも順番待ちの古い説明を残さず現在段階を示す
				if( selected_ < cards_.size() && &cards_[selected_] == &*found && !found->result) {
					// 選択カードの受付番号・file名・現在段階を示す所有文字列
					const auto text = L"#" + std::to_wstring( found->id) + L"  " + found->path.filename().native() +
									  L"\r\n" + found->status.text() ;
					SetWindowTextW( chat_, text.c_str()) ;
				}
			}
		}
		// 完了によって変わった本文だけを描画し、進捗でtext選択を解除しない
		if( changed || apiHistoryChanged) {
			refreshHistory() ;
			// APIの別行ACKは一覧/保存だけを更新し、現在の本文へ触れません
			if( changed) {
				showCard() ;
			}
			saveHistory() ;
		}
		pumpAutomaticTextContext() ; // 通常event一回につき有限カードを一巡するだけ
		const auto hadFollowup = parakeetFollowup_.size() != 0 || parakeetFollowup_.pending().has_value() ;
		pumpParakeetFollowup() ; // card iteratorを持たない境界で一件ずつ進めます
		if( !hadFollowup) {
			pumpStatusNotice() ; // 同じpollの既存完了確認へ詳細通知を重ねません
		}
		// 終了待ちの案内を通常進捗が上書きしない
		if( closing_) {
			setStatus( {IDS_STOPPING}) ;
			finishClose() ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 利用者のcontrol操作を処理します
	 * @param[in] id controlの識別子
	 * @param[in] notification 標準controlの通知code
	 */
	//------------------------------------------------------------------------------
	void command( const int id, const int notification)
	{
		// 停止後の新規操作はworkerへ送らない
		if( closing_) {
			return ;
		}
		// 復元前の明示準備は既定モデルで開始せず、同じ再操作を案内します
		if( !transcriptionSettingsReady_ &&
		   (id == IDC_MODEL_PREPARE || id == IDC_PREP_DOWNLOAD || id == IDC_PREP_RECHECK ||
			id == IDC_TEXT_CONTEXT_PREPARE || id == IDC_WHOLE_PREPARE || id == IDC_OPEN)) {
			showMessage( transcriptionSettingsLoadNotice_) ;
			return ;
		}
		// 通常menuの古い通知でも導入中に他の準備/生成を開始しません
		if( wholeTextPreparationWindow_.busy() &&
		   (id == IDC_MODEL_PREPARE || id == IDC_PREP_DOWNLOAD || id == IDC_PREP_RECHECK || id == IDC_WHOLE_CREATE ||
			id == IDC_WHOLE_CREATE_AVX2 || id == IDC_WHOLE_CREATE_MINISTRAL16GB || id == IDC_TEXT_CONTEXT_CREATE ||
			id == IDC_TEXT_CONTEXT_PREPARE || id == IDC_READING_CREATE || id == IDC_OPEN || id == IDC_DIARIZE ||
			id == IDC_ASR_MODEL || id == IDC_ASR_PROFILE || id == IDC_ASR_QUALITY || id == IDC_ASR_STANDARD ||
			id == IDC_SPEECH_FILTER)) {
			setStatus( {IDS_WTP_BUSY}) ;
			return ;
		}
		// 取得開始時の構成を、無効化済みcontrolの古い通知でも変更させない
		if( modelDownloadActive_ && (id == IDC_ASR_MODEL || id == IDC_ASR_PROFILE || id == IDC_ASR_QUALITY ||
									id == IDC_ASR_STANDARD || id == IDC_SPEECH_FILTER || id == IDC_DIARIZE)) {
			return ;
		}
		// button以外のcontrol通知も同じUI threadで扱う
		switch( id) {
			case IDC_UPDATE_CHECK: // 既存menuの手動確認
			case IDC_UPDATE_INSTALL: // 明示取得/終了更新
			case IDC_UPDATE_CANCEL: // 更新だけ取消し
				updateCommand( id) ;
				return ;
			case IDC_TEXT_CONTEXT_PREPARE: // 文章専用では補助Whisperを非対象にします
			case IDC_WHOLE_PREPARE: {
				WholeTextPreparationRequest request ; // 音声・本文と独立した所有要求
				// 明示文章専用操作だけが新用途を選び、通常appへ研究pathを埋め込みません
				if( id == IDC_TEXT_CONTEXT_PREPARE) {
					request.variant = WholeTextRuntimeVariant::MINISTRAL14_B_AVX2_NO_REPACK16_GB ;
					request.use = WholeTextPreparationUse::TEXT_ONLY_CONTEXT ;
				}
				request.bundleRoot = diagnosticModelReference() ;
				request.cacheRoot = diagnosticModelCache() ;
				request.runtimePath = modelRoot_.parent_path() / wholeTextRuntimeDescriptor( request.variant).dllName ;
#if defined(KOEFUDE_WINDOW_TESTING)
                request.cacheRoot =
					diagnosticLog_.currentMode() ? diagnosticModelCache() : modelRoot_ ; // 自process専用root
#endif
                WholeTextPreparationRunner preparationRunner ; // 通常は既定service
				// 本番の明示modeでも固定Whisperだけ同じ専用計画へ接続します
				if( diagnosticLog_.currentMode()) {
					preparationRunner =
						//------------------------------------------------------------------------------
						/**
						 * @brief 全文補正の準備検査へ、ローカルモデル照合と明示取得の依存を接続します
						 * @param[in] value 同期処理中だけ借用する補正方式・モデル参照先・cacheと取得意図です
						 * @param[in] stop 全文準備と内側のモデル処理へ伝える停止tokenです
						 * @param[in] notify 準備の実段階を通知するcallbackで、呼出し終了後は保持しません
						 * @return inspectWholeTextPreparationが検査した要素別の準備状態と計画の所有値です
						 */
						//------------------------------------------------------------------------------
						[]( const WholeTextPreparationRequest &value, std::stop_token stop,
						   const WholeTextPreparationCallback &notify)
						{
							auto services = wholeTextPreparationServices() ; // runtime/Gemma等は既存
							services.whisper =
							//------------------------------------------------------------------------------
							/**
							 * @brief 全文補正に必要な音声モデルを取得せずローカル検査します
							 * @param[in] options 検査するengineとprofileの設定borrowです
							 * @param[in] reference 読取り専用の同梱モデル参照directoryです
							 * @param[in] cache 検証済みモデルを再利用するcache directoryです
							 * @param[in] token 同期検査へ伝える協調停止tokenです
							 * @return 参照先/cacheの実在と検証状態に基づくModelPreparationPlanの所有値です
							 */
							//------------------------------------------------------------------------------
							[]( const EngineOptions &options, const std::filesystem::path &reference,
							   const std::filesystem::path &cache, std::stop_token token)
							{
								LocalModelInspector inspector ; /* worker同期 */
								return planCorrectionDiagnosticModels( options, reference, cache, inspector, token) ;
							} ;
							services.prepareWhisper =
							//------------------------------------------------------------------------------
							/**
							 * @brief 明示取得された補助音声モデルだけをcacheへ準備します
							 * @param[in] options 取得対象engineとprofileを決める設定borrowです
							 * @param[in] cache 検査と取得の両方に渡す書込み対象directoryです
							 * @param[in] token 取得・検証へ伝える協調停止tokenです
							 * @param[in] progress 取得の実段階を同期通知するcallbackのborrowです
							 * @return downloadModelsが返す取得・検証結果の計画です。失敗は同処理の例外として伝えます
							 * @note 名前のない第2引数の参照directoryは使いません。固定transportとcacheを取得元/取得先の引数へ渡します
							 */
							//------------------------------------------------------------------------------
							[]( const EngineOptions		   &options, const std::filesystem::path &,
							   const std::filesystem::path &cache, std::stop_token token,
							   const ModelPreparationCallback &progress)
							{
								WinHttpModelTransport transport ; /* isolatedのみ */
								return downloadModels( options, cache, cache, transport, token, progress) ;
							} ;
							return inspectWholeTextPreparation( value, stop, notify, services) ;
						} ;
				}
				wholeTextPreparationWindow_.show(
					window_, std::move( request),
					//------------------------------------------------------------------------------
					/**
					 * @brief 他workerのモデル使用/書込みと競合する受付を拒否します
					 * @return 準備・通常処理が空いていればtrue
					 */
					//------------------------------------------------------------------------------
					[this]
					{
						return !closing_ && modelOperation_ == 0 && !modelDownloadActive_ &&
							   std::none_of( cards_.begin(), cards_.end(),
								   //------------------------------------------------------------------------------
								   /**
								    * @brief engineを使用するカードを調べます
								    * @param[in] card 既存所有カード
								    * @return 処理中ならtrue
								    */
								   //------------------------------------------------------------------------------
								   []( const FileCard &card)
								   {
									   return card.busy || card.reading.active || card.wholeText.active ;
								   }) ;
					},
					std::move( preparationRunner)) ;
				break ; // 開くだけでは検査・取得・生成を開始しません
			}
			case IDC_MODEL_PREPARE:
				showModelPreparation( LocalDiagnosticReason::SETTINGS_PREPARATION) ;
				break ; // 通信なしで現在の条件と準備状態を表示する
			case IDC_PREP_DOWNLOAD:
				startModelDownload() ;
				break ; // 表示済み構成の明示取得
			case IDC_PREP_CANCEL:
				preparation_->cancel( modelOperation_) ;
				break ; // 現在の準備だけを取り消す
			case IDC_PREP_RECHECK:
				requestModelCheck( true, LocalDiagnosticReason::EXPLICIT_RECHECK) ;
				break ; // 失敗後も通信なしで既取得fileを検査する
			case IDC_DIARIZE:
				requestModelCheck() ;
				break ; // 有効にした共有話者依存を次jobの前に検査する
			case IDC_EXIT:
				PostMessageW( window_, WM_CLOSE, 0, 0) ;
				break ; // 標準menuからの安全な終了
			case IDC_RETRANSCRIBE:
				// menu受付時の安定IDへ既存の再実行を接続します
				if( selected_ < cards_.size()) {
					retranscribe( cards_[selected_].id) ;
				}
				break ;
			case IDC_HISTORY_REMOVE:
				// メニュー操作は選択した一つの履歴IDに限定する
				if( selected_ < cards_.size()) {
					removeHistory( cards_[selected_].id) ;
				}
				break ;
			case IDC_HISTORY_RETRY:
				retryHistory() ;
				break ; // 読込失敗と未保存の変更を区別して再試行する
			case IDC_UI_JAPANESE:
			case IDC_UI_ENGLISH:
				break ; // 旧menu commandでは言語切替/選好保存を開始しません
			case IDC_READING_VIEW: // 保存意図をitem dataで復元します
				// 表示選択は生成や音声読取りを開始しません
				if( notification == CBN_SELCHANGE && selected_ < cards_.size()) {
					// この同期境界で所有する値、または確定状態の検証値です
					const auto index = SendMessageW( control( IDC_READING_VIEW), CB_GETCURSEL, 0, 0) ; // 選択位置
					// この同期境界で所有する値、または確定状態の検証値です
					const auto value =
						SendMessageW( control( IDC_READING_VIEW), CB_GETITEMDATA, index, 0) ; // 安定した意図
					// 比較はUI ownerだけへ束縛し、新API要求/選好保存を発生させません
					if( index >= 0 && value == K_COMPARE_READING_CHOICE && comparisonAvailable()) {
						selectComparison() ;
						showCard() ;
						break ;
					}
					// 既存の原文/Automaticへ戻す時は従来のHTTP0保存導線を維持します
					if( index >= 0 && value >= 0 && value <= 4) {
						clearComparison() ;
					}
					// 未選択/未知意図を履歴へ保存しません
					if( index >= 0 && value >= 0 && value <= 4) {
						auto &card = cards_[selected_] ; // 同じUIカード
						// 新しいAPI表示意図は既存controlからHTTP0で先に保存します
						if( card.apiText.adopted && (value == static_cast<LRESULT>( ResultPreference::ORIGINAL) ||
													value == static_cast<LRESULT>( ResultPreference::AUTOMATIC))) {
							ApiTextCorrectionJobRequest selection ; // 同じ原文ownerだけ
							selection.source = card.result ;
							selection.key.historyId = card.id ;
							selection.record = card.apiText ;
							static_cast<void>( apiTextWindow_.direct(
								std::move( selection), value == static_cast<LRESULT>( ResultPreference::ORIGINAL)
														  ? ApiTextAction::ORIGINAL
														  : ApiTextAction::CORRECTED)) ;
							showCard() ; // 保存前は前表示を保持します
							break ;
						}
						card.resultPreference = static_cast<ResultPreference>( value) ;
						// Whole選択は旧Reading意図を破棄しません
						if( value <= 2) {
							static_cast<void>( selectReading( card.reading, static_cast<ReadingPreference>( value))) ;
						}
						showCard() ;
						saveHistory() ;
					}
				}
				break ;
			case IDC_FILTER:
				// 選択した完了fileの表示だけを更新する
				if( notification == CBN_SELCHANGE && selected_ < cards_.size() && cards_[selected_].result) {
					// この同期境界で所有する値、または確定状態の検証値です
					const auto item = SendMessageW( control( IDC_FILTER), CB_GETCURSEL, 0, 0) ; // 選択index
					cards_[selected_].filter = static_cast<int>(
						SendMessageW( control( IDC_FILTER), CB_GETITEMDATA, static_cast<WPARAM>( item), 0)) ;
					requestChatDocument( false) ;
					saveHistory() ;
				}
				break ; // 保存・全文copyは元snapshotの全話者を対象にする
			case IDC_OPEN:
				chooseInput() ;
				break ; // 対応するlocal音声の明示投入
			case IDC_SPECTRAL_METHOD:
			case IDC_LEGACY_METHOD:
				speakerMethod_ = id == IDC_SPECTRAL_METHOD ? SpeakerMethod::SPECTRAL : SpeakerMethod::LEGACY_THRESHOLD ;
				CheckMenuRadioItem( GetMenu( window_), IDC_SPECTRAL_METHOD, IDC_LEGACY_METHOD, static_cast<UINT>( id),
								   MF_BYCOMMAND) ;
				break ; // 次に投入されるjobだけへ方式変更を反映する
			case IDC_ASR_QUALITY:
			case IDC_ASR_STANDARD:
				// 無効化された構成への古いmessageでも不正な次ジョブを作らない
				if( !findAsrDefinition( asrModel_, id == IDC_ASR_QUALITY ? AsrProfile::QUALITY : AsrProfile::STANDARD)) {
					break ;
				}
				asrProfile_ = id == IDC_ASR_QUALITY ? AsrProfile::QUALITY : AsrProfile::STANDARD ;
				refreshAsrControls() ;
				break ; // 処理中・完了済みの結果のmodelを後から変更しない
			case IDC_SPEECH_FILTER:
				speechFilter_ = !speechFilter_ ;
				CheckMenuItem( GetMenu( window_), IDC_SPEECH_FILTER,
							  MF_BYCOMMAND | (speechFilter_ ? MF_CHECKED : MF_UNCHECKED)) ;
				requestModelCheck() ;
				break ; // 次のjobだけで発話区間検出を切り替える
			case IDC_COPY:
				copyResult() ;
				break ; // clipboardへの明示copy
			case IDC_SAVE:
				chooseSave() ;
				break ; // fileへの明示保存
			case IDC_RENAME:
				showSpeakerRename( rowTarget()) ;
				break ; // file内の表示名変更
			case IDC_SETTINGS_OPEN:
			case IDC_API_SETTINGS_OPEN:
				showSettings() ;
				break ; // 履歴なしの独立設定
			case IDC_API_TEXT_OPEN:
				openApi( selected_ < cards_.size() ? cards_[selected_].id : 0, false) ;
				break ; // 選択原文への明示操作
			case IDC_TEXT_CONTEXT_CREATE: // 保存済み原文への明示文章専用要求
				queueTextContext() ;
				break ;
			case IDC_WHOLE_CREATE_MINISTRAL16GB: // 固有profileを受付時に固定します
				queueWholeText( WholeTextRuntimeVariant::MINISTRAL14_B_AVX2_NO_REPACK16_GB) ;
				break ;
			case IDC_WHOLE_CREATE_AVX2: // 保存済み原文からの明示AVX2試用
				queueWholeText( WholeTextRuntimeVariant::AVX2_NO_REPACK) ;
				break ;
			case IDC_WHOLE_CREATE: // 保存済み原文からの明示CPU試用
				queueWholeText() ;
				break ;
			case IDC_BOUNDED_RETRY:
				// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
				if( selected_ < cards_.size() && cards_[selected_].bounded.latestRequest) {
					static_cast<void>( queueBoundedControl( cards_[selected_].id,
														  cards_[selected_].bounded.latestRequest->operationId, false)) ;
				}
				break ; // 同じJだけを保存します
			case IDC_BOUNDED_DISCARD:
				// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
				if( selected_ < cards_.size() && cards_[selected_].bounded.latestRequest) {
					static_cast<void>( queueBoundedControl( cards_[selected_].id,
														  cards_[selected_].bounded.latestRequest->operationId, true)) ;
				}
				break ; // 原文と有効全文を保ち未完だけ削除します
			case IDC_WHOLE_COPY: // 専用本文だけの明示copy
				copyResult( true) ;
				break ;
			case IDC_WHOLE_SAVE: // 専用本文だけの明示TXT保存
				chooseSave( true) ;
				break ;
			case IDC_READING_CREATE: // 採用済み方式だけで明示生成/再生成
				queueReading() ;
				break ;
			case IDC_CANCEL: // command受付時の同じ行/source/opだけを取消します
				cancelRow( rowTarget()) ;
				break ;
			case IDC_CLEAR:
				clearHistory() ;
				break ; // 永続履歴を明示確認後に全削除する
			default:
				break ; // 編集中の通常通知は標準controlへ任せる
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief Window messageを所有instanceへ振り分けます
	 * @param[in] window 通知先Window
	 * @param[in] message message識別子
	 * @param[in] wparam message固有値
	 * @param[in] lparam message固有値
	 * @return Win32で定義された処理結果
	 * @note 例外をOS callback境界の外へ漏らしません
	 */
	//------------------------------------------------------------------------------
	static LRESULT CALLBACK windowProc( HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
	{
		// GWLP_USERDATAはcreate呼出し元がmessage loop終了まで所有するinstanceのborrow
		auto *self = reinterpret_cast<MainWindow *>( GetWindowLongPtrW( window, GWLP_USERDATA)) ;
		// 最初の非client messageで所有せずinstanceを関連付ける
		if( message == WM_NCCREATE) {
			// この同期境界で所有する値、または確定状態の検証値です
			const auto *creation = reinterpret_cast<const CREATESTRUCTW *>( lparam) ; // OSが提供するborrow
			self = static_cast<MainWindow *>( creation->lpCreateParams) ;
			self->window_ = window ;
			SetWindowLongPtrW( window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( self)) ;
		}
		// instance関連付け前のmessageは標準処理へ戻す
		if( !self) {
			return DefWindowProcW( window, message, wparam, lparam) ;
		}
		try {
			// UIの標準lifetimeに沿って初期化、操作、終了を分離する
			switch( message) {
				case WM_CREATE:
					self->initialize() ;
					return 0 ; // controlとworkerの生成
				case WM_SIZE:
					self->paneMinimized_ = wparam == SIZE_MINIMIZED ;
					// 最小化はdragだけ確定し、0×0のcontrol配置/文書要求を作りません
					if( self->paneMinimized_) {
						self->finishSplitter( false, false) ;
						return 0 ;
					}
					self->layout() ;
					return 0 ; // 非blockingな再配置
				case WM_DPICHANGED: { // 新monitorの推奨矩形とfontへ切替え
					self->finishSplitter( false, false) ;
					self->scaleHistoryColumns( HIWORD(wparam)) ;
					const auto *suggested = reinterpret_cast<const RECT *>( lparam) ; // OS借用矩形
					SetWindowPos( window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
								 suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE) ;
					self->updateFonts() ;
					self->buttonIcons_.update( window) ;
					self->layout() ;
					return 0 ;
				}
				case WM_GETMINMAXINFO: { // controlが重ならない最小論理size
					auto	 *limits = reinterpret_cast<MINMAXINFO *>( lparam) ; // OSの出力構造体
					const int dpi = static_cast<int>( GetDpiForWindow( window)) ; // 現在のDPI
					limits->ptMinTrackSize = {MulDiv( 850, dpi, 96), MulDiv( 620, dpi, 96)} ;
					MONITORINFO monitor{sizeof(monitor)} ; // 現monitorの作業領域読取りだけ
					// 狭いmonitorで最小track値が初期clampを押し広げないよう制限します
					if( GetMonitorInfoW( MonitorFromWindow( window, MONITOR_DEFAULTTONEAREST), &monitor)) {
						const auto &work = monitor.rcWork ; // OS所有の作業矩形
						const auto	bounds =
							minimumPaneWindow( {work.left, work.top, work.right, work.bottom}, dpi) ; // 最小外寸
						limits->ptMinTrackSize = {bounds.width, bounds.height} ;
					}
					return 0 ;
				}
				case WM_COMMAND:
					self->command( LOWORD(wparam), HIWORD(wparam)) ;
					return 0 ; // 操作受付
				case WM_CANCELMODE: // modal等で親へ来た取消しも自境界へ集約
					self->finishSplitter( true) ;
					return 0 ;
				case WM_CONTEXTMENU:
					// 履歴以外の右クリックはcaption等の標準動作へ渡す
					if( reinterpret_cast<HWND>( wparam) == self->control( IDC_HISTORY)) {
						self->showHistoryMenu( {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)}) ;
						return 0 ;
					}
					break ;
				case WM_NOTIFY: {
					const auto *note = reinterpret_cast<const NMHDR *>( lparam) ; // 同期borrow
					// 自分のreport tableだけの通知を処理します
					if( !note || note->hwndFrom != self->control( IDC_HISTORY)) {
						break ;
					}
					// rebuild中の同期選択通知は元のID復元まで無視します
					if( self->refreshingHistory_) {
						return 0 ;
					}
					// 通常keyboard/マウス選択に同じ状態更新を使います
					if( note->code == LVN_ITEMCHANGED) {
						const auto *changed = reinterpret_cast<const NMLISTVIEW *>( lparam) ; // OS借用
						// 選択状態が変わった時だけ本文を更新します
						if( (changed->uChanged & LVIF_STATE) &&
						   ((changed->uOldState ^ changed->uNewState) & LVIS_SELECTED)) {
							const int selected = ListView_GetNextItem( note->hwndFrom, -1, LVNI_SELECTED) ; // 実選択
							self->selected_ = selected < 0 ? self->cards_.size() : static_cast<std::size_t>( selected) ;
							self->showCard() ;
						}
						return 0 ;
					}
					// 狭い列の省略部分は標準tooltipから同じ全metadataを読めます
					if( note->code == LVN_GETINFOTIPW) {
						auto *tip = reinterpret_cast<NMLVGETINFOTIPW *>( lparam) ; // OS出力borrow
						// 有効なrow/出力bufferだけを扱います
						if( tip->iItem >= 0 && static_cast<std::size_t>( tip->iItem) < self->cards_.size() &&
						   tip->pszText && tip->cchTextMax > 0) {
							const auto text = self->historyDetails( self->cards_[static_cast<std::size_t>( tip->iItem)]) ;
							const auto count = std::min( text.size(), static_cast<std::size_t>( tip->cchTextMax - 1)) ;
							std::copy_n( text.data(), count, tip->pszText) ;
							tip->pszText[count] = L'\0' ;
						}
						return 0 ;
					}
					// Deleteは選択行だけへ適用し、通常キーはcontrolへ任せます
					if( note->code == LVN_KEYDOWN) {
						const auto *key = reinterpret_cast<const NMLVKEYDOWN *>( lparam) ; // OS借用
						// 実選択を再確認してから履歴削除します
						if( key->wVKey == VK_DELETE) {
							const int selected = ListView_GetNextItem( note->hwndFrom, -1, LVNI_SELECTED) ; // 検証行
							// 未選択/古いindexは操作対象にしません
							if( selected >= 0 && static_cast<std::size_t>( selected) < self->cards_.size()) {
								self->removeHistory( self->cards_[static_cast<std::size_t>( selected)].id) ;
							}
							return 0 ;
						}
					}
					break ;
				}
				case WM_DROPFILES:
					self->acceptDrop( reinterpret_cast<HDROP>( wparam)) ;
					return 0 ; // OS所有dropの移譲
				case K_WAKE_MESSAGE:
				case WM_TIMER:
					self->collectEvents() ;
					self->tickApiProgress() ;
					self->lockModelControls() ;
					return 0 ; // 集約eventと同op実経過の回収
				case WM_CLOSE: // workerを強制終了せず受付停止と協調取消しを要求
					// modal確認中を含め、同じ終了処理を再入させない
					if( self->closing_) {
						return 0 ;
					}
					self->closing_ = true ;
					// 更新の明示終了だけは保存完了後のlaunchまでownerを保持します
					if( self->updater_ && !self->updateCloseIntent_) {
						self->updater_->requestStop() ;
					}
					self->parakeetFollowup_.clear() ; // 終了取消し後にも送信予約を復活しません
					self->transcriptionSettingsCloseWaiting_ = self->transcriptionSettingsOperation_ != 0 ;
					self->finishSplitter( false, false) ; // close中はcapture解放だけ、RTF要求なし
					DragAcceptFiles( window, FALSE) ;
					DragAcceptFiles( self->chat_, FALSE) ;
					// 未受領の成功とclose取消しが競合しても旧overlayを保持する
					for( auto &card : self->cards_) { // 最大32件の自分の要求だけ
						card.pendingCompareSource.reset() ; // sessionの表示予約は保存しません
						card.pendingCompareCandidate.reset() ;
						cancelReading( card.reading) ;
						card.automaticText.cancel() ; // close後の新自動送信を止めます
						cancelWholeTextCorrection( card.wholeText) ;
					}
					self->settingsWindow_.close() ;
					self->apiTextWindow_.stop() ;
					self->controller_->requestStop() ;
					self->preparation_->requestStop() ;
					self->wholeTextPreparationWindow_.requestStop() ;
					self->documents_->requestStop() ;
					self->showCard() ;
					self->collectEvents() ;
					return 0 ;
				case WM_CTLCOLORSTATIC: { // OSの標準色でlabelとreadonly editを表示
					const HDC dc = reinterpret_cast<HDC>( wparam) ; // OSが所有する描画context
					SetBkColor( dc, GetSysColor( COLOR_BTNFACE)) ;
					SetTextColor( dc, GetSysColor( COLOR_BTNTEXT)) ;
					return reinterpret_cast<LRESULT>( GetSysColorBrush( COLOR_BTNFACE)) ;
				}
				case WM_DESTROY: {
					KillTimer( window, K_POLL_TIMER) ;
					const HRESULT result = self->accessibility_.clear() ; // child破棄前に解除
					// 解除失敗も終了を妨げず、診断と非正常終了codeを残す
					if( FAILED(result)) {
						self->showCallbackError( ErrorKind::INTERNAL, static_cast<std::uint32_t>( result)) ;
						PostQuitMessage( 1) ;
						return 0 ;
					}
					PostQuitMessage( 0) ;
					return 0 ; // 終端
				}
				case WM_NCDESTROY:
					SetWindowLongPtrW( window, GWLP_USERDATA, 0) ;
					break ; // borrowの解除
				default:
					break ; // 標準Window動作を維持
			}
		} catch( const OperationError &error) {
			self->showCallbackError( error.kind(), error.code()) ;
			// 初期化失敗のWindowは完成扱いにしない
			if( message == WM_CREATE) {
				return -1 ;
			}
		} catch( ...) {
			self->showCallbackError( ErrorKind::INTERNAL, 0, true) ;
			// 初期化例外はOSのWindow作成を失敗させる
			if( message == WM_CREATE) {
				return -1 ;
			}
		}
		return DefWindowProcW( window, message, wparam, lparam) ;
	}

	/// OSが所有するmain Window
	HWND			   window_ = nullptr ;
	/// 所有Windowに属する見出し
	HWND			   title_ = nullptr ;
	/// 選択可能な発話表示control
	HWND			   chat_ = nullptr ;
	/// module所有のCtrl+O/Ctrl+S
	HACCEL			   accelerators_ = nullptr ;
	/// 進捗と安全な終了待ちの説明
	HWND			   status_ = nullptr ;
	/// セッション内の論理希望幅、OS captureは製品操作だけ
	MainPaneSplitState splitter_ ;
	/// 現在の5列pixel幅に対応するDPI
	int				   columnsDpi_ = 96 ;
	/// SIZE_MINIMIZED中は配置と文書要求を止める
	bool			   paneMinimized_ = false ;
	/// 配置/同期paint通知からの再帰layoutを抑止
	bool			   paneLayoutActive_ = false ;
#if defined(KOEFUDE_WINDOW_TESTING)
    /// -1直前/0失敗/1成功。実APIは置換しない
	std::function<void( HRGN, UINT, int)> paneRepaintObserver_ ;
#endif
    /// UI言語を切り替えても再描画できる状態
	UiStatus																	 currentStatus_ ;
	/// 最大64、modal前に一件ずつ消費
	std::vector<StatusNotice>													 statusNotices_ ;
	/// 行なし操作の非永続重複拒否
	std::array<std::uint64_t, static_cast<std::size_t>( StatusNoticeKind::COUNT)> noticeOperations_{} ;
	/// 起動時404と手動確認の通知を区別
	std::uint64_t																 manualUpdateOperation_ = 0 ;
	/// 全child HWNDの破棄後に解放する所有画像列
	ButtonIcons																	 buttonIcons_ ;
	/// STAが所有しchild HWND破棄前に解除する目的名
	ControlAccessibility														 accessibility_ ;
	/// 固定labelを再翻訳するcontrol IDとresource ID
	std::map<int, UINT>															 textControls_ ;
	/// HWND破棄まで保持する全幅進捗と説明
	StatusProgress																 statusProgress_ ;
	/// chat破棄まで保持する補間中の位置とtimer状態
	SmoothScroll																 smoothScroll_ ;
	/// 本instanceが解放する通常font
	HFONT																		 font_ = nullptr ;
	/// 本instanceが解放する見出しfont
	HFONT																		 titleFont_ = nullptr ;
	/// fontを更新する非所有control列
	std::vector<HWND>															 children_ ;
	/// exeと同じfolderにある固定の同梱model
	const std::filesystem::path													 modelRoot_ ;
	/// 空はLocalAppData、明示値は隔離したlocal履歴先
	const std::filesystem::path													 historyRoot_ ;
	/// 復元後に一度だけ開く明示起動入力
	std::filesystem::path														 initialInput_ ;
	/// 試験時だけ有効なログ。wWinMainがlifetimeを所有
	DiagnosticLog																&diagnosticLog_ ;
	/// 通常/明示隔離の別sink。旧mode判定に使わない
	LocalDiagnosticLog															*localDiagnostics_ ;
	/// 集約待機の入口
	LocalDiagnosticReason modelCheckReason_ = LocalDiagnosticReason::REFRESH ;
#if defined(KOEFUDE_WINDOW_TESTING)
    /// 専用試験の所有者がWindow/workerより長く保持する依存
	MainWindowTestHooks &testHooks_ ;
#endif
    /// 次jobに適用する研究/従来方式
	SpeakerMethod									 speakerMethod_ = SpeakerMethod::SPECTRAL ;
	/// 利用者指定の品質優先を初期選択する
	AsrProfile										 asrProfile_ = AsrProfile::QUALITY ;
	/// 独立した日本語評価を通った構成を初期選択
	AsrModel										 asrModel_ = AsrModel::PARAKEET ;
	/// CPU専用の選択をGPUモデルの設定へ持ち越さない
	bool											 cpuOnlyAsr_ = false ;
	/// GPU対応モデルで最後に選択したAuto/CPUの位置
	LRESULT											 preferredBackend_ = 0 ;
	/// 旧Auto選好の読取り保持。新規実行には使いません
	bool											 multilingualAutoLanguage_ = false ;
	/// Apply済みの次job設定
	bool											 diarizationEnabled_ = true ;
	/// Apply済みの任意人数、0は自動
	int												 expectedSpeakers_ = 0 ;
	/// 次jobで発話がある区間を抽出して認識する
	bool											 speechFilter_ = true ;
	/// resize時も保持する現在fileの説明
	std::wstring									 chatHeading_ ;
	/// 推論なしの最新表示準備worker
	std::unique_ptr<ChatDocumentController>			 documents_ ;
	/// 選択カードだけの所有cache
	std::shared_ptr<const PreparedChatDocument>		 document_ ;
	/// 最新IDに対応する描画条件
	std::optional<ChatDocumentRequest>				 documentRequest_ ;
	/// 0は表示待機なし
	std::uint64_t									 documentOperation_ = 0 ;
	/// 同じ版のresizeだけ選択/scroll保持
	bool											 preserveDocumentView_ = false ;
	/// 比較中の同じ行。0は標準表示
	std::uint64_t									 compareHistory_ = 0 ;
	/// 比較sourceの所有identity
	std::shared_ptr<const Transcript>				 compareSource_ ;
	/// 保存API候補の所有identity
	std::shared_ptr<const ApiTextAttempt>			 compareCandidate_ ;
	/// 前回layoutとの比較用、非永続
	bool											 comparisonShown_ = false ;
	/// 保存済み整形結果がある時だけ表示行を確保
	bool											 readingControlsShown_ = false ;
	/// Window破棄前に終了確認するworker
	std::unique_ptr<JobController>					 controller_ ;
	/// 本文I/Oと最後の保存を担う独立worker
	std::unique_ptr<HistoryController>				 history_ ;
	/// 終了確認するモデル専用worker
	std::unique_ptr<ModelPreparationController>		 preparation_ ;
	/// report再構築の同期通知guard
	bool											 refreshingHistory_ = false ;
	/// 通常行の最終状態が属する安定履歴ID
	std::uint64_t									 apiStatusHistory_ = 0 ;
	/// 同IDでも別原文へ進捗/終端を混ぜない
	std::shared_ptr<const Transcript>				 apiStatusSource_ ;
	/// 通常行API操作の段階・終端、本文/秘密を含みません
	UiStatus										 apiStatus_ ;
	/// 非永続の一件時計、同じ行/op/sourceだけ
	ApiTextProgressClock							 apiClock_ ;
	/// 同じjob workerに接続する独立設定/通常行操作
	ApiTextCorrectionWindow							 apiTextWindow_ ;
	/// 非永続、最大32案内/一続行
	ParakeetApiFollowup								 parakeetFollowup_ ;
	/// UI threadの自所有modalだけ
	mutable unsigned								 followupModalDepth_ = 0 ;
	/// 同じAPI設定を借用する四tab
	SettingsWindow									 settingsWindow_{apiTextWindow_} ;
	/// 単独network/hash/trust worker
	std::unique_ptr<AppUpdateController>			 updater_ ;
	/// Innoが通常の実行中appを保護する固定identity
	UniqueHandle									 appMutex_ ;
	/// URL/path/本文を含まない最新小DTO
	UpdateEvent										 updateEvent_ ;
	/// 当該段階のowner
	std::uint64_t									 updateOperation_ = 0 ;
	/// 起動復元後の一回だけ
	bool											 startupUpdateAttempted_ = false ;
	/// 保存ACK済みの起動選好
	bool											 checkUpdates_ = true ;
	/// Readyの明示操作に限る終了意図
	bool											 updateCloseIntent_ = false ;
	/// 小選好専用I/O worker
	std::unique_ptr<TranscriptionSettingsController> transcriptionSettings_ ;
	/// 起動復元成功後だけ新規/準備を受付
	bool											 transcriptionSettingsReady_ = false ;
	/// 原fileを保護したまま再起動復旧を案内
	bool											 transcriptionSettingsFailed_ = false ;
	/// Busy/IO/破損を分離
	UINT											 transcriptionSettingsLoadNotice_ = IDS_TRANSCRIPTION_LOADING ;
	/// 終了と競合した受理済み保存
	bool											 transcriptionSettingsCloseWaiting_ = false ;
	/// 明示選択前に失敗を破棄しない
	bool											 transcriptionSettingsCloseFailed_ = false ;
	/// close後失敗の次回明示open用
	bool											 transcriptionSettingsFailedDraft_ = false ;
	/// 終了取消し/retry用の小草稿
	TranscriptionPreferences						 transcriptionSettingsSubmitted_ ;
	/// ACK待ちsave ID
	std::uint64_t									 transcriptionSettingsOperation_ = 0 ;
	/// close後/別sessionのdirtyへACKを混ぜない
	std::uint64_t									 transcriptionSettingsSession_ = 0 ;
	/// 同じApplyの言語、別store
	UiLanguage										 transcriptionSettingsLanguage_ = UiLanguage::JAPANESE ;
	/// 選択file専用のmodeless名前編集
	HWND											 renameWindow_ = nullptr ;
	/// 表示開始時に固定した履歴ID
	std::uint64_t									 renameHistory_ = 0 ;
	/// 同ID別原文を拒否するowner
	std::shared_ptr<const Transcript>				 renameSource_ ;
	/// 独立した補正準備workerと画面
	WholeTextPreparationWindow						 wholeTextPreparationWindow_ ;
	/// UI threadだけが所有する準備画面
	ModelPreparationWindow							 preparationWindow_ ;
	/// 最新の受付操作が固定した依存構成
	EngineOptions									 modelOptions_ ;
	/// 現在選択だけに対応する所有通知と計画
	ModelPreparationEvent							 modelEvent_ ;
	/// 未受領終端を含む現在の準備ID
	std::uint64_t									 modelOperation_ = 0 ;
	/// 古い検査の終了後に最新選択を一度検査する状態
	bool											 modelCheckPending_ = false ;
	/// 明示取得中だけ依存設定を固定する状態
	bool											 modelDownloadActive_ = false ;
	/// 一つのrootで現在の全依存を検証済みの場合true
	bool											 modelReady_ = false ;
	/// 最大32件の、永続履歴に対応するUI所有値
	std::vector<FileCard>							 cards_ ;
	/// 明示保存operation IDと履歴IDの対応
	std::map<std::uint64_t, std::uint64_t>			 saves_ ;
	/// V7保存floor
	std::uint64_t									 nextOperationId_ = 1 ;
	/// rawは閲覧可能なまま派生保存だけを拒否
	bool											 speakerTextCorrupt_ = false ;
	/// 設定Applyとは別の明示補助準備
	std::optional<EngineOptions>					 projectionModelOptions_ ;
	/// 準備中も小tokenだけ
	std::optional<SpeakerTextJobRequest>			 pendingProjectionModel_ ;
	/// 再開前の補助モデル準備
	std::optional<RowActionTarget>					 pendingProjectionResume_ ;
	/// 削除・起動を跨いで払い出す永続ID
	std::uint64_t									 nextHistoryId_ = 1 ;
	/// 最新の保存要求。過去の完了通知と照合する
	std::uint64_t									 historyRevision_ = 0 ;
	/// 履歴だけの失敗分類
	HistoryErrorKind								 historyError_ = HistoryErrorKind::INTERNAL ;
	/// 元の履歴診断code
	std::uint32_t									 historyCode_ = 0 ;
	/// 検証済みの履歴を復元するまで新規受付を禁止
	bool											 historyReady_ = false ;
	/// 最新の表示内容に保存されていない変更がある場合true
	bool											 historyDirty_ = false ;
	/// 読込または最新snapshotの保存が失敗
	bool											 historyFailed_ = false ;
	/// 最後の保存確認後に履歴workerを停止した状態
	bool											 historyStopRequested_ = false ;
	/// 未保存分の破棄を利用者が明示した場合だけtrue
	bool											 allowUnsavedClose_ = false ;
	/// 終了確認dialogの再入を防止する状態
	bool											 closePromptOpen_ = false ;
	/// 選択カードindex、範囲外は未選択
	std::size_t										 selected_ = 0 ;
	/// trueなら新規操作を受け付けない
	bool											 closing_ = false ;
} ;

} // namespace
} // namespace koefude

// 通常製品の起動入口は、差替え依存を持つ試験binaryへ組み込まない
#if !defined(KOEFUDE_WINDOW_TESTING)
//------------------------------------------------------------------------------
/**
 * @brief native UIのSTAとWindow lifetimeを管理します
 * @param[in] instance exeの非所有module handle
 * @param[in] previous Windowsでは未使用
 * @param[in] commandLine OSの借用command line。CommandLineToArgvWで解析
 * @param[in] show 起動時の表示方法
 * @return 正常終了は0、初期化失敗は1
 */
//------------------------------------------------------------------------------
int WINAPI wWinMain( HINSTANCE instance, HINSTANCE previous, PWSTR commandLine, int show)
{
	static_cast<void>( previous) ;
	static_cast<void>( commandLine) ;
	// UI threadのSTA初期化結果。成功した場合だけ対応する終了処理を行う
	const HRESULT comStatus = CoInitializeEx( nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE) ;
	// 標準dialogを利用できない場合は初期化を中止する
	if( FAILED(comStatus)) {
		return 1 ;
	}
	int			  exitCode = 1 ; // 例外・初期化失敗時の終了code
	// System32から読み込むRich Edit module。Window終了後に解放する
	const HMODULE richEdit = LoadLibraryExW( L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) ;
	try {
		// Rich EditはOSのsystem directoryからだけ読み込む
		if( !richEdit) {
			throw koefude::OperationError( koefude::ErrorKind::INTERNAL, GetLastError()) ;
		}
		// このprocessで使用する標準controlと進捗controlの初期化指定
		INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS |
															ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES} ;
		// 標準control初期化が成功した場合だけWindowを作成する
		if( !InitCommonControlsEx( &controls)) {
			throw koefude::OperationError( koefude::ErrorKind::INTERNAL) ;
		}
		std::array<wchar_t, 32768> executable{} ; // OSのexe path上限を含むbuffer
		// この同期境界で所有する値、または確定状態の検証値です
		const DWORD				   pathLength =
			GetModuleFileNameW( nullptr, executable.data(), static_cast<DWORD>( executable.size())) ; // 実際のUTF-16長
		// 不明・切詰めpathをモデルの探索先にしない
		if( pathLength == 0 || pathLength >= executable.size()) {
			throw koefude::OperationError( koefude::ErrorKind::INVALID_INPUT, GetLastError()) ;
		}
		// 実行fileと同じ配布folderにある同梱モデルのroot
		const std::filesystem::path modelRoot = std::filesystem::path( executable.data()).parent_path() / L"models" ;
		std::filesystem::path		input ; // 明示起動optionで指定した最初の音声
		std::filesystem::path		historyRoot ; // 明示した隔離履歴先。通常起動は空のまま
		std::filesystem::path		acceptanceState ; // 明示試験の所有root
		std::wstring				diagnosticGeneration ; // 診断時だけ必須
		std::vector<std::wstring>	seenOptions ; // 最大5optionの二重拒否
		std::filesystem::path		diagnosticPath ; // 試験時だけの新規ローカルログfile
		int							count = 0 ; // OSが解析した引数数
		LPWSTR *arguments = CommandLineToArgvW( GetCommandLineW(), &count) ; // LocalFreeする所有配列
		const std::unique_ptr<void, decltype(&LocalFree)> owned( arguments, LocalFree) ;
		// 値を伴う既知optionだけを受け入れ、shellとして再評価しない
		for( int index = 1; index < count; ++index) {
			const std::wstring option( arguments[index]) ; // 今回解析するoption
			// 未知optionや値欠落は曖昧な起動を避けて失敗させる
			if( (option != L"--open" && option != L"--history-directory" && option != L"--diagnostic-log" &&
				option != L"--diagnostic-generation" && option != L"--acceptance-state-directory") ||
			   index + 1 >= count) {
				throw koefude::OperationError( koefude::ErrorKind::INVALID_INPUT) ;
			}
			// 二重optionの後勝ちによるroot混在を拒否します
			if( std::find( seenOptions.begin(), seenOptions.end(), option) != seenOptions.end()) {
				throw koefude::OperationError( koefude::ErrorKind::INVALID_INPUT) ;
			}
			seenOptions.push_back( option) ;
			// 履歴先はfile引数と分離し、shell展開せずstorageで検証する
			if( option == L"--history-directory") {
				historyRoot = arguments[++index] ;
			}
			// 明示された診断専用の保存先を音声入力と分けて受け付ける
			else if( option == L"--diagnostic-log") {
				diagnosticPath = arguments[++index] ; // 本文を含まない試験イベントの保存先
			} else if( option == L"--diagnostic-generation") {
				diagnosticGeneration = arguments[++index] ;
			} else if( option == L"--acceptance-state-directory") {
				acceptanceState = arguments[++index] ;
			} else {
				input = arguments[++index] ; // 明示した音声を履歴復元後に開く
			}
		}
		// 現行診断CLIは世代と一つの所有rootを必須にします
		if( !diagnosticPath.empty()) {
			const auto state = koefude::validateModelDirectory( acceptanceState) ; // local属性検証、書込み0
			const auto history = koefude::validateModelDirectory( historyRoot) ; // 同じrootの子だけ
			const auto log = std::filesystem::absolute( diagnosticPath).lexically_normal() ; // 新規file
			const auto normal = koefude::defaultModelCache().parent_path() ; // 書込みなしの既存root
			const auto overlaps =
				//------------------------------------------------------------------------------
				/**
				 * @brief 同じdirectoryまたは親子のrootを拒否します
				 * @param[in] left 正規local path
				 * @param[in] right 正規local path
				 * @return 大小文字を区別しない親子関係ならtrue
				 */
				//------------------------------------------------------------------------------
				[]( std::filesystem::path left, std::filesystem::path right)
				{
					const auto same =
					//------------------------------------------------------------------------------
					/**
					 * @brief 二つのdirectory表記をWindowsの大小文字を無視したordinal比較で照合します
					 * @param[in] a 同期比較中だけ借用する左のNUL終端UTF-16 pathです
					 * @param[in] b 同期比較中だけ借用する右のNUL終端UTF-16 pathです
					 * @return CompareStringOrdinalがCSTR_EQUALを返した場合だけtrueです。別表記やAPI失敗はfalseで、filesystemの実体同一性は調べません
					 */
					//------------------------------------------------------------------------------
					[]( const std::filesystem::path &a, const std::filesystem::path &b)
					{
						return CompareStringOrdinal( a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL ;
					} ; // Windowsのdirectory表記を照合
					auto parent = left ; // 祖先だけを有限探索
					// drive rootに達するまで左側の祖先を照合します
					while( !parent.empty()) {
						if( same( parent, right)) {
							return true ;
						}
						const auto next = parent.parent_path() ; /* 一段上 */
						if( next == parent) {
							break ;
						}
						parent = next ;
					}
					parent = right ;
					// 右側の祖先も確認し、一方向の重なりを残しません
					while( !parent.empty()) {
						if( same( parent, left)) {
							return true ;
						}
						const auto next = parent.parent_path() ; /* 一段上 */
						if( next == parent) {
							break ;
						}
						parent = next ;
					}
					return false ;
				} ;
			// 直接CLIでも通常履歴/model/exportを試験stateへ結び付けません
			if( overlaps( state, normal / L"history") || overlaps( state, normal / L"models") ||
			   overlaps( state, normal / L"exports") || overlaps( state, modelRoot.parent_path())) {
				throw koefude::OperationError( koefude::ErrorKind::INVALID_INPUT) ;
			}

			// 旧CLIと通常履歴への接続を起動前に拒否します
			if( diagnosticGeneration != L"correction-diagnostics-1287" || acceptanceState.empty() ||
			   history != state / L"history" || log.parent_path().parent_path() != state / L"reports" ||
			   log.filename() != L"app-events.jsonl") {
				throw koefude::OperationError( koefude::ErrorKind::INVALID_INPUT) ;
			}
		} else if( !acceptanceState.empty() || !diagnosticGeneration.empty()) {
			throw koefude::OperationError( koefude::ErrorKind::INVALID_INPUT) ;
		}
		koefude::DiagnosticLog		diagnosticLog( diagnosticPath) ; // 未指定時はI/O0
		koefude::LocalDiagnosticLog localDiagnostics(
			koefude::localDiagnosticDirectory( historyRoot)) ; // CLI検証後だけ、明示stateは隔離
		// 明示modeだけsessionとcompiled pinを書きます
		if( !diagnosticPath.empty()) {
			diagnosticLog.beginCurrent() ;
		}
		koefude::MainWindow window( modelRoot, std::move( historyRoot), std::move( input), diagnosticLog,
								   &localDiagnostics) ; // UI所有instance
		// Window作成失敗では不完全なUIを表示し続けない
		if( !window.create( instance, show)) {
			throw koefude::OperationError( koefude::ErrorKind::INTERNAL, GetLastError()) ;
		}
		diagnosticLog.append( koefude::DiagnosticEvent::APP_STARTED) ;
		MSG	 message{} ; // message loopで再利用するOS通知値
		BOOL received = 0 ; // GetMessageの成功・終了・失敗を区別
		// WM_QUITまで動かし、標準controlのTab移動とkeyboard操作を処理する
		while( (received = GetMessageW( &message, nullptr, 0, 0)) > 0) {
			// dialog navigationが消費しないmessageだけを通常dispatchする
			if( !window.dialogMessage( message)) {
				TranslateMessage( &message) ;
				DispatchMessageW( &message) ;
			}
		}
		exitCode = received == 0 ? 0 : 1 ;
		diagnosticLog.append( koefude::DiagnosticEvent::APP_EXITED, 0, static_cast<std::uint64_t>( exitCode),
							 diagnosticLog.healthy() ? 1 : 0) ;
		localDiagnostics.finish( static_cast<std::uint32_t>( exitCode)) ; // 全worker停止後のdrain/flush
	} catch( const koefude::OperationError &error) {
		MessageBoxW( nullptr, koefude::errorText( error.kind(), error.code()).c_str(), L"KoeFude", MB_OK | MB_ICONERROR) ;
	} catch( ...) {
		MessageBoxW( nullptr, koefude::resource( IDS_INTERNAL_ERROR).c_str(), L"KoeFude", MB_OK | MB_ICONERROR) ;
	}
	// 全Rich Edit Windowが破棄された後だけmoduleの参照を解放する
	if( richEdit) {
		FreeLibrary( richEdit) ;
	}
	CoUninitialize() ;
	return exitCode ;
}
#endif
