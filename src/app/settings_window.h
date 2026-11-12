//------------------------------------------------------------------------------

/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file settings_window.h
 * @brief 既存推論設定の未適用値とAPI設定childを一つのtab画面へ配置します
 * @version 1.0.0
 * @date 2026/11/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1354の統合tabと次jobへの明示適用を追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1355のpage内ラベル背景を標準tabへ統一
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1370の文脈分割と明示途中再開を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1374の未適用変更と保存ACKへApplyを同期
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1375の適用済み選好の保存と復元を追加
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1376の文章改善tabと同session続行境界を追加
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1378の明示更新と設定ACKを接続
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1385の日本語専用入口と旧選好互換を反映
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1400の文字起こし設定の準備ボタンを削除
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */

//------------------------------------------------------------------------------

#pragma once

#include "app/api_catalog_time.h"

#include "app/api_text_correction_window.h"

#include "app/localization.h"

#include "app/resource.h"

#include "app/tab_page_background.h"

#include "core/engines.h"

#include "inference/model_store.h"

#include "platform/win_helpers.h"

#include <algorithm>

#include <array>

#include <commctrl.h>

#include <functional>

#include <utility>

#include <windows.h>

namespace koefude {

/** @brief UI threadでsession内draftを所有し、Applyだけを既存受付へ渡します */

class SettingsWindow final {

	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 既存API設定controllerを借用します
	 * @param[in] api 本objectより長寿命の所有者
	 */
	//------------------------------------------------------------------------------

	explicit SettingsWindow( ApiTextCorrectionWindow &api) : api_( api)
	{
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief workerを所有せず自分のWindowだけを解放します
	 */
	//------------------------------------------------------------------------------

	~SettingsWindow()

	{

		// main破棄後の非所有HWNDへDestroyを重ねません

		if( IsWindow( window_)) {

			DestroyWindow( window_) ;
		}
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 未適用値を現在設定から開始します
	 * @param[in] owner 自processのmain HWND
	 * @param[in] options 次job用の適用済み値。pathを読まずcopyします
	 * @param[in] language 旧互換引数。表示と明示Applyは日本語に固定します
	 *
	 * @param[in] apply 同期確定、またはasyncApply時の保存受付callback
	 *
	 * @param[in] action 旧互換引数。設定画面から準備commandを発火しません
	 * @param[in] asyncApply trueはcompleteLocalの保存ACKまでcleanにしません
	 * @param[in] checkUpdates 保存済みの起動確認選好
	 */
	//------------------------------------------------------------------------------

	void show( HWND owner, const EngineOptions &options, UiLanguage language,

			  std::function<void( const EngineOptions &, UiLanguage)> apply,

			  std::function<void( int)> action, bool asyncApply = false, bool checkUpdates = true)

	{

		// 開いている間の再選択は入力途中のdraftを捨てません

		if( opened_) {

			return ;
		}

		updateCheck_ = appliedUpdateCheck_ = checkUpdates ;
		draft_ = options ;
		draft_.autoLanguage = false ; // 表示用だけを日本語に揃え、旧耐久値は書き換えません

		language_ = UiLanguage::JAPANESE ;
		static_cast<void>( language) ; // 旧English入力も日本語専用版の表示へ読み替えます

		applied_ = draft_ ; // 同じ表示基準によりopenだけでdirtyを作りません

		appliedLanguage_ = language_ ;

		apply_ = std::move( apply) ;

		static_cast<void>( action) ; // 旧callerの引数互換を保持します

		asyncApply_ = asyncApply ;
		localBusy_ = false ;
		++session_ ;

		apiVisited_ = false ;

		// 同じcontrolsを再利用し、tabを開くだけではAPI pageを作りません

		if( !IsWindow( window_)) {

			window_ = CreateDialogParamW( GetModuleHandleW( nullptr), MAKEINTRESOURCEW(IDD_SETTINGS),

										 owner, dialogProc,

										 reinterpret_cast<LPARAM>( this)) ; // 同期初期化borrow

			// 生成失敗を設定表示成功として扱いません

			if( !window_) {

				throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
			}
		}

		populate() ;

		TabCtrl_SetCurSel( item( IDC_SETTINGS_TABS), 0) ;

		selectTab() ;

		layout() ;

		opened_ = true ;

		updateApply() ;

		// 所有SW_HIDE試験では実表示を開始しません

		if( IsWindowVisible( owner)) {

			ShowWindow( window_, SW_SHOWNA) ;
		}
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 設定sessionを終了し、未適用secret/draftを破棄します
	 */
	//------------------------------------------------------------------------------

	void close()

	{

		// API pageを見ていない一般設定closeでAPI操作へ触れません

		if( apiVisited_) {

			api_.closeSettingsPage() ;
		}

		api_.closeAudioMetadata() ;

		audioVisited_ = false ;

		opened_ = false ;

		++session_ ; // close前のACKを次のdraftへ適用しません
		localBusy_ = false ;

		apiVisited_ = false ;

		draft_ = {} ;

		ShowWindow( window_, SW_HIDE) ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief model取得/導入中だけ次job設定変更を禁止します
	 * @param[in] locked 操作固定
	 */
	//------------------------------------------------------------------------------

	void lock( bool locked)

	{

		locked_ = locked ;

		// 同じ有限controlsだけを固定し、結果閲覧は止めません

		for( const int id : {IDC_ASR_MODEL, IDC_ASR_PROFILE, IDC_BACKEND, IDC_DIARIZE,

							IDC_COUNT, IDC_SETTINGS_METHOD, IDC_SPEECH_FILTER}) {

			EnableWindow( item( id), !locked && !localBusy_) ;
		}
		EnableWindow( item( IDC_SETTINGS_UI), FALSE) ; // 日本語専用版の固定表示
		EnableWindow( item( IDC_LANGUAGE), FALSE) ; // 初回open前のlockでも言語を有効にしません
		EnableWindow( item( IDC_SETTINGS_UPDATE), !localBusy_) ;

		// CPU/日本語専用制約も共通のregistryへ戻します

		if( opened_) {

			constrain() ;
		}

		updateApply() ;

		if( opened_) {

			audioMetadata() ;
		}
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief navigationで使う借用HWNDです
	 * @return 未作成NULL
	 */
	//------------------------------------------------------------------------------

	HWND handle() const noexcept

	{

		return window_ ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 保存受付を束縛する設定sessionです
	 * @return close/openごとの単調番号
	 */
	//------------------------------------------------------------------------------
	std::uint64_t session() const noexcept
	{
		return session_ ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 終了取消し時だけ失敗した草稿を戻します
	 * @param[in] options 保存に失敗した選好
	 * @param[in] language 同じApplyの言語
	 * @param[in] checkUpdates 同じApplyの起動確認選好
	 */
	//------------------------------------------------------------------------------
	void restoreLocalDraft( const EngineOptions &options, UiLanguage language, bool checkUpdates = true)
	{
		draft_ = options ;
		updateCheck_ = checkUpdates ;
		language_ = language ;
		populate() ;
		selectTab() ;
		updateApply() ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 同じsessionのlocal保存ACKだけを適用します
	 * @param[in] session 保存を受け付けたsession
	 * @param[in] success 耐久保存成功
	 * @param[in] code 失敗診断値
	 */
	//------------------------------------------------------------------------------
	void completeLocal( std::uint64_t session, bool success, std::uint32_t code = 0)
	{
		// close後や新sessionの草稿へ遅いACKを流用しません
		if( !opened_ || session != session_ || !localBusy_) {
			return ;
		}
		localBusy_ = false ;
		// 正常保存ACKだけを新しいclean基準にします
		if( success) {
			applied_ = submitted_ ;
			appliedUpdateCheck_ = submittedUpdateCheck_ ;
			appliedLanguage_ = submittedLanguage_ ;
		}
		const auto text = resource( success ? IDS_TRANSCRIPTION_SAVED : IDS_TRANSCRIPTION_SAVE_FAILED) +
						  (success ? L"" : L" [" + std::to_wstring( code) + L"]") ; // 固定説明
		SetDlgItemTextW( window_, IDC_SETTINGS_NOTE, text.c_str()) ;
		lock( locked_) ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 現設定sessionが開いているかを返します
	 * @return 未closeならtrue
	 */
	//------------------------------------------------------------------------------
	bool opened() const noexcept
	{
		return opened_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 明示改善に必要な既存tabと短い続行説明を表示します
	 * @param[in] notice resource ID
	 */
	//------------------------------------------------------------------------------
	void followupPage( UINT notice)
	{
		TabCtrl_SetCurSel( item( IDC_SETTINGS_TABS), 3) ;
		selectTab() ;
		followupNotice( notice) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 続行/失効を既存footerへ示します
	 * @param[in] notice resource ID
	 */
	//------------------------------------------------------------------------------
	void followupNotice( UINT notice)
	{
		SetDlgItemTextW( window_, IDC_SETTINGS_NOTE, resource( notice).c_str()) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief local/APIを別々に照合し、途中/失敗/未適用でcloseしません
	 * @return 開いた同sessionの全適用が完了し、現在資格も有効
	 */
	//------------------------------------------------------------------------------
	bool followupApplied() const
	{
		return opened_ && !localBusy_ && !localDirty() && !api_.busy() && api_.settingsFollowupReady() ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 同じApplyの未適用更新選好を返します
	 * @return checkboxのsnapshot
	 */
	//------------------------------------------------------------------------------
	bool checkUpdates() const noexcept
	{
		return updateCheck_ ;
	}

	private:
	friend struct Settings1354TestAccess ; // 所有SW_HIDE専用event試験だけ

	//------------------------------------------------------------------------------
	/**
	 * @brief 所有controlを参照します
	 * @param[in] id resource ID
	 * @return 非所有HWND
	 */
	//------------------------------------------------------------------------------

	HWND item( int id) const noexcept

	{

		return GetDlgItem( window_, id) ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief comboへ表示名と安定IDを一緒に追加します
	 * @param[in] id control ID
	 * @param[in] label 現言語文字列
	 * @param[in] data enum値
	 * @param[in] selected 選択するか
	 */
	//------------------------------------------------------------------------------

	void choice( int id, const std::wstring &label, LPARAM data, bool selected)

	{

		const auto index = SendMessageW( item( id), CB_ADDSTRING, 0,

										reinterpret_cast<LPARAM>( label.c_str())) ; // 同期copy

		SendMessageW( item( id), CB_SETITEMDATA, index, data) ;

		// 表示順でenum値を推定しません

		if( selected) {

			SendMessageW( item( id), CB_SETCURSEL, index, 0) ;
		}
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 現選択の安定dataを読みます
	 * @param[in] id combo ID
	 * @return data/未選択-1
	 */
	//------------------------------------------------------------------------------

	LRESULT selected( int id) const

	{

		const auto index = SendMessageW( item( id), CB_GETCURSEL, 0, 0) ; // 有限選択

		return index < 0 ? -1 : SendMessageW( item( id), CB_GETITEMDATA, index, 0) ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 同一registryの実在構成と言語/CPU制約をdraftへ適用します
	 */
	//------------------------------------------------------------------------------

	void constrain()

	{

		const auto &definition = asrDefinition( draft_.asrModel, draft_.asrProfile) ; // 正本

		const bool	cpu = definition.engine == AsrEngine::PARAKEET_CTC &&

						 (!KOEFUDE_HAS_CUDA || !definition.gpuValidated) ; // 実装済み経路

		EnableWindow( item( IDC_LANGUAGE), FALSE) ; // engine本来の多言語能力と製品入口を分けます

		const bool audio =

			definition.engine == AsrEngine::OPEN_AI_TRANSCRIPTIONS ; // APIではGPU選択なし

		EnableWindow( item( IDC_BACKEND), !locked_ && !localBusy_ && !cpu && !audio) ;

		EnableWindow( item( IDC_ASR_PROFILE),

					 !locked_ && !localBusy_ && SendMessageW( item( IDC_ASR_PROFILE), CB_GETCOUNT, 0, 0) > 1) ;

		SendMessageW( item( IDC_LANGUAGE), CB_SETCURSEL, 0, 0) ;

		SendMessageW( item( IDC_BACKEND), CB_SETCURSEL, cpu ? 1 : preferredBackend_, 0) ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 選択系統の実在品質だけを列挙します
	 */
	//------------------------------------------------------------------------------

	void profiles()

	{

		// 系統変更時に存在しない品質だけを品質優先へ戻します

		if( !findAsrDefinition( draft_.asrModel, draft_.asrProfile)) {

			draft_.asrProfile = AsrProfile::QUALITY ;
		}

		SendMessageW( item( IDC_ASR_PROFILE), CB_RESETCONTENT, 0, 0) ;

		// registryの最大二構成だけを表示し、架空の軽量候補を作りません

		for( const auto profile : {AsrProfile::QUALITY, AsrProfile::STANDARD}) {

			const auto *definition = findAsrDefinition( draft_.asrModel, profile) ; // 任意候補

			// 実在するweightの構成だけを追加します

			if( definition) {

				choice( IDC_ASR_PROFILE, resource( definition->choiceResource),

					   static_cast<LPARAM>( profile), profile == draft_.asrProfile) ;
			}
		}

		constrain() ;

		audioMetadata() ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief resourceと適用済み値をcontrolsへ一回copyします
	 */
	//------------------------------------------------------------------------------

	void populate()

	{

		SetWindowTextW( window_, resource( IDS_SETTINGS_TITLE).c_str()) ;
		SetDlgItemTextW( window_, IDC_SETTINGS_UPDATE, resource( IDS_UPDATE_STARTUP).c_str()) ;
		SetDlgItemTextW( window_, IDC_SETTINGS_UPDATE_NOTE, resource( IDS_UPDATE_PRIVACY).c_str()) ;
		CheckDlgButton( window_, IDC_SETTINGS_UPDATE, updateCheck_ ? BST_CHECKED : BST_UNCHECKED) ;

		TabCtrl_DeleteAllItems( item( IDC_SETTINGS_TABS)) ;

		// 四項目のtabだけを現在のUI言語で作ります

		for( int index = 0; index < 4; ++index) {

			auto	label = resource( IDS_SETTINGS_GENERAL + index) ; // 同期copy文字列

			TCITEMW tab{} ; // 標準tab item

			tab.mask = TCIF_TEXT ;

			tab.pszText = label.data() ;

			TabCtrl_InsertItem( item( IDC_SETTINGS_TABS), index, &tab) ;
		}

		const std::array labels{std::pair{IDC_SETTINGS_UI_LABEL, IDS_SETTINGS_UI},

								std::pair{IDC_MODEL_LABEL, IDS_MODEL_LABEL},

								std::pair{IDC_ASR_LABEL, IDS_QUALITY_LABEL},

								std::pair{IDC_SETTINGS_BACKEND_LABEL, IDS_ACCESS_BACKEND},

								std::pair{IDC_SETTINGS_LANGUAGE_LABEL, IDS_ACCESS_LANGUAGE},

								std::pair{IDC_SETTINGS_COUNT_LABEL, IDS_ACCESS_COUNT},

								std::pair{IDC_SETTINGS_METHOD_LABEL, IDS_SETTINGS_METHOD},

								std::pair{IDC_DIARIZE, IDS_DIARIZE},

								std::pair{IDC_SPEECH_FILTER, IDS_SETTINGS_VAD},

								std::pair{IDC_SETTINGS_APPLY, IDS_SETTINGS_APPLY},

								std::pair{IDC_SETTINGS_NOTE, IDS_SETTINGS_NOTE},

								std::pair{IDCANCEL, IDS_SETTINGS_CLOSE}} ; // 既存resourceも共有

		// 本文/secretを触らず固定labelだけを更新します

		for( const auto &[id, text] : labels) {

			SetDlgItemTextW( window_, id, resource( text).c_str()) ;
		}

		// 各comboはsessionの適用済み値から再生成します

		for( const int id : {IDC_SETTINGS_UI, IDC_ASR_MODEL, IDC_BACKEND, IDC_LANGUAGE, IDC_COUNT,

							IDC_SETTINGS_METHOD}) {

			SendMessageW( item( id), CB_RESETCONTENT, 0, 0) ;
		}

		choice( IDC_SETTINGS_UI, resource( IDS_JAPANESE), 0, true) ;
		EnableWindow( item( IDC_SETTINGS_UI), FALSE) ;

		preferredBackend_ = draft_.backend == Backend::CPU ? 1 : 0 ;

		choice( IDC_BACKEND, resource( IDS_AUTO_GPU), 0, preferredBackend_ == 0) ;

		choice( IDC_BACKEND, resource( IDS_CPU), 1, preferredBackend_ == 1) ;

		choice( IDC_LANGUAGE, resource( IDS_JAPANESE), 0, true) ;

		choice( IDC_COUNT, resource( IDS_AUTO_SPEAKERS), 0, draft_.expectedSpeakers == 0) ;

		// 自動推定を既定とし、任意の人数だけを補助値へ追加します

		for( int count = 1; count <= 20; ++count) {

			choice( IDC_COUNT, std::to_wstring( count) + resource( IDS_SPEAKER_COUNT), count,

				   count == draft_.expectedSpeakers) ;
		}

		choice( IDC_SETTINGS_METHOD, resource( IDS_SPECTRAL_METHOD), 0,

			   draft_.speakerMethod == SpeakerMethod::SPECTRAL) ;

		choice( IDC_SETTINGS_METHOD, resource( IDS_LEGACY_METHOD), 1,

			   draft_.speakerMethod == SpeakerMethod::LEGACY_THRESHOLD) ;

		// 系統の重複は表示名でなくenum値で除きます

		for( const auto &definition : asrDefinitions()) {

			bool	   present = false ; // この系統の登録有無

			const auto count = SendMessageW( item( IDC_ASR_MODEL), CB_GETCOUNT, 0, 0) ; // 有限件数

			// 同じregistryの既登録系統だけを走査します

			for( LRESULT index = 0; index < count; ++index) {

				present |= SendMessageW( item( IDC_ASR_MODEL), CB_GETITEMDATA, index, 0) ==

						   static_cast<LPARAM>( definition.family) ;
			}

			// 各系統を一回だけ追加します

			if( !present) {

				choice( IDC_ASR_MODEL, resource( definition.familyResource),

					   static_cast<LPARAM>( definition.family),

					   definition.family == draft_.asrModel) ;
			}
		}

		CheckDlgButton( window_, IDC_DIARIZE, draft_.diarization ? BST_CHECKED : BST_UNCHECKED) ;

		CheckDlgButton( window_, IDC_SPEECH_FILTER,

					   draft_.speechFilter ? BST_CHECKED : BST_UNCHECKED) ;

		profiles() ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 有限controlsから現在の未適用値だけを読みます
	 */
	//------------------------------------------------------------------------------
	void readDraft()
	{
		draft_.backend = preferredBackend_ == 1 ? Backend::CPU : Backend::AUTO ;
		draft_.autoLanguage = false ; // 明示Applyのsnapshotは日本語専用
		draft_.expectedSpeakers = static_cast<int>( selected( IDC_COUNT)) ;
		draft_.diarization = IsDlgButtonChecked( window_, IDC_DIARIZE) == BST_CHECKED ;
		draft_.speechFilter = IsDlgButtonChecked( window_, IDC_SPEECH_FILTER) == BST_CHECKED ;
		draft_.speakerMethod =
			selected( IDC_SETTINGS_METHOD) == 1 ? SpeakerMethod::LEGACY_THRESHOLD : SpeakerMethod::SPECTRAL ;
		language_ = UiLanguage::JAPANESE ; // 旧combo通知でもEnglishへ戻しません
		updateCheck_ = IsDlgButtonChecked( window_, IDC_SETTINGS_UPDATE) == BST_CHECKED ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief sessionの適用済み値と編集欄を比較します
	 * @return localの未適用変更あり
	 */
	//------------------------------------------------------------------------------
	bool localDirty() const noexcept
	{
		return draft_.asrModel != applied_.asrModel || draft_.asrProfile != applied_.asrProfile ||
			   draft_.backend != applied_.backend || draft_.autoLanguage != applied_.autoLanguage ||
			   draft_.expectedSpeakers != applied_.expectedSpeakers || draft_.diarization != applied_.diarization ||
			   draft_.speechFilter != applied_.speechFilter || draft_.speakerMethod != applied_.speakerMethod ||
			   language_ != appliedLanguage_ || updateCheck_ != appliedUpdateCheck_ ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 未適用値/lock/API ACKだけからApplyを更新し、通信や保存を開始しません
	 */
	//------------------------------------------------------------------------------
	void updateApply()
	{
		// populate途中/close後のcontrol値を新しい基準に混ぜません
		if( !opened_) {
			EnableWindow( item( IDC_SETTINGS_APPLY), FALSE) ;
			return ;
		}
		readDraft() ;
		EnableWindow( item( IDC_SETTINGS_APPLY), !locked_ && !localBusy_ && !api_.busy() &&
												   (localDirty() || (apiVisited_ && api_.settingsDirty()))) ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief draftを次jobへ明示確定し、API変更は保存成功ACKまで未適用として保持します
	 */
	//------------------------------------------------------------------------------
	void apply()
	{
		updateApply() ;
		// disabledへの古いcommandも無変更/処理中の二重適用へ変えません
		if( !IsWindowEnabled( item( IDC_SETTINGS_APPLY))) {
			return ;
		}
		// 非同期保存は受付だけでcleanにせず、以前の耐久値を保持します
		if( localDirty()) {
			submittedUpdateCheck_ = updateCheck_ ;
			submitted_ = draft_ ;
			submittedLanguage_ = language_ ;
			localBusy_ = asyncApply_ ;
			try {
				apply_( submitted_, submittedLanguage_) ;
			} catch( ...) {
				localBusy_ = false ; // 受付前失敗はdirtyのまま明示再適用できます
				throw ;
			}
			// 純粋fake等の同期契約も従前どおり維持します
			if( !asyncApply_) {
				applied_ = submitted_ ;
				appliedUpdateCheck_ = submittedUpdateCheck_ ;
				appliedLanguage_ = submittedLanguage_ ;
			} else {
				SetDlgItemTextW( window_, IDC_SETTINGS_NOTE, resource( IDS_TRANSCRIPTION_SAVING).c_str()) ;
			}
		}
		// 未訪問/未編集APIを一般tabのApplyから保存・更新しません
		if( apiVisited_) {
			api_.applySettingsPage() ;
		}
		lock( locked_) ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 現tabのcontrolsだけを表示し、明示API初回表示を既存契約へ渡します
	 */
	//------------------------------------------------------------------------------

	void selectTab()

	{

		const int		 tab = TabCtrl_GetCurSel( item( IDC_SETTINGS_TABS)) ; // 現選択

		const std::array groups{std::pair{IDC_SETTINGS_UI_LABEL, 0},

								std::pair{IDC_SETTINGS_UI, 0},
								std::pair{IDC_SETTINGS_UPDATE, 0},
								std::pair{IDC_SETTINGS_UPDATE_NOTE, 0},

								std::pair{IDC_MODEL_LABEL, 1},

								std::pair{IDC_ASR_MODEL, 1},

								std::pair{IDC_ASR_LABEL, 1},

								std::pair{IDC_ASR_PROFILE, 1},

								std::pair{IDC_SETTINGS_BACKEND_LABEL, 1},

								std::pair{IDC_BACKEND, 1},

								std::pair{IDC_SETTINGS_LANGUAGE_LABEL, 1},

								std::pair{IDC_LANGUAGE, 1},

								std::pair{IDC_SPEECH_FILTER, 1},

								std::pair{IDC_DIARIZE, 2},

								std::pair{IDC_SETTINGS_COUNT_LABEL, 2},

								std::pair{IDC_COUNT, 2},

								std::pair{IDC_SETTINGS_METHOD_LABEL, 2},

								std::pair{IDC_SETTINGS_METHOD, 2}} ; // 有限の既存設定欄

		// 非選択tabはTab移動と描画の対象外にします

		for( const auto &[id, page] : groups) {

			ShowWindow( item( id), page == tab ? SW_SHOWNA : SW_HIDE) ;
		}

		// 一般tabを開くだけでmetadata GETを追加しません

		if( tab == 3 && !apiVisited_) {

			api_.showSettingsPage( window_) ;

			apiVisited_ = true ;
		}

		// 同じsessionのtab往復はpageの表示だけで再GETしません

		if( apiVisited_) {

			api_.setSettingsPageVisible( tab == 3) ;
		}

		audioMetadata() ;

		layout() ;

		lock( locked_) ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief API選択の同じtab内へ送信先/単価/共有キーを表示します
	 */
	//------------------------------------------------------------------------------

	void audioMetadata()

	{

		const bool visible = TabCtrl_GetCurSel( item( IDC_SETTINGS_TABS)) == 1 &&

							 draft_.asrModel == AsrModel::OPEN_AI_AUDIO ;

		// 固定三controlだけ表示し、local tabや一般tabでは通信しません

		for( const auto id : {IDC_AUDIO_INFO, IDC_AUDIO_REFRESH, IDC_AUDIO_KEYS}) {

			ShowWindow( item( id), visible ? SW_SHOWNA : SW_HIDE) ;
		}

		// API構成を明示表示した一sessionだけmetadata取得を許可します

		if( visible && !audioVisited_) {

			audioVisited_ = true ;

			api_.showAudioMetadata() ;
		}

		auto text = resource( IDS_AUDIO_NOTICE) ; // 送信先/音声送信/時刻basis

		text += L"\r\n" + resource( draft_.asrProfile == AsrProfile::STANDARD

									   ? IDS_AUDIO_STANDARD_NOTICE

									   : IDS_AUDIO_CONTEXT_NOTICE) ;

		const auto &catalog = api_.audioCatalog() ; // 秘密なしUI値

		// 補助用途/追加処理を適用前の同じ設定内に明示する。取得容量/条件は既存準備画面

		text += L"\r\n" + resource( IDS_SPEAKER_TEXT_NOTICE) ;

		// 未取得と有効単価を分けて表示します

		if( !catalog.audioPrice.microPerMinute) {

			text += L"\r\n" + resource( IDS_AUDIO_PRICE_MISSING) ;

		} else {

			text += L"\r\n$" +

					std::to_wstring( static_cast<double>( *catalog.audioPrice.microPerMinute) /

									1'000'000) +

					resource( IDS_AUDIO_PRICE_UNIT) + resource( IDS_AUDIO_HUNDRED_MIN) +

					std::to_wstring( static_cast<double>( *catalog.audioPrice.microPerMinute) /

									10'000) +

					L"; UTC " + formatApiCatalogTime( catalog.audioPriceMeta.fetched) ;

			if( apiCatalogNow() < catalog.audioPriceMeta.fetched ||

			   apiCatalogNow() - catalog.audioPriceMeta.fetched > 86400) {

				text += L"\r\n" + resource( IDS_AUDIO_PRICE_STALE) ;
			}
		}

		text += L"\r\nhttps://developers.openai.com/api/docs/pricing.md" ;

		SetDlgItemTextW( window_, IDC_AUDIO_INFO, text.c_str()) ;

		SetDlgItemTextW( window_, IDC_AUDIO_KEYS, resource( IDS_AUDIO_KEY_SHARED).c_str()) ;

		SetDlgItemTextW( window_, IDC_AUDIO_REFRESH, resource( IDS_AUDIO_REFRESH).c_str()) ;

		EnableWindow( item( IDC_AUDIO_REFRESH), !locked_ && !api_.busy()) ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief tab内容と下部Apply/Closeをclient内へ配置します
	 */
	//------------------------------------------------------------------------------

	void layout()

	{

		RECT client{} ; // 自所有clientのpixel座標

		GetClientRect( window_, &client) ;

		const int  dpi = static_cast<int>( GetDpiForWindow( window_)) ; // 現monitor DPI

		const int  width = MulDiv( client.right, 96, dpi) ; // 96DPI論理幅

		const int  height = MulDiv( client.bottom, 96, dpi) ; // 96DPI論理高

		const auto place =
			//------------------------------------------------------------------------------
			/**
			 * @brief 同じDPIで自controlsを配置します
			 * @param[in] id 配置する自dialog内のchild control ID
			 * @param[in] x 親client左端からの論理pixel位置（96 DPI基準）
			 * @param[in] y 親client上端からの論理pixel位置（96 DPI基準）
			 * @param[in] w 論理pixel幅。1未満は1へ丸めて現DPIへ換算します
			 * @param[in] h 論理pixel高さ。1未満は1へ丸めて現DPIへ換算します
			 * @note 捕捉したDPIで座標を換算し、MoveWindowで再描画を要求します。controlの所有権は移しません
			 */
			//------------------------------------------------------------------------------
			[&]( int id, int x, int y, int w, int h)
			{
				MoveWindow( item( id), MulDiv( x, dpi, 96), MulDiv( y, dpi, 96),

						   MulDiv( std::max( 1, w), dpi, 96), MulDiv( std::max( 1, h), dpi, 96), TRUE) ;
			} ;

		place( IDC_SETTINGS_TABS, 8, 8, width - 16, height - 60) ;

		place( IDC_SETTINGS_NOTE, 12, height - 46, width - 204, 34) ;

		place( IDC_SETTINGS_APPLY, width - 180, height - 40, 76, 28) ;

		place( IDCANCEL, width - 96, height - 40, 84, 28) ;

		const int valueX = 184 ; // 同じ幅のラベル列

		const int valueWidth = width - valueX - 32 ; // 右余白を維持

		// 四行の対応するlabel/comboを同じ位置へ置きます

		for( const auto &[label, combo, row] :

			{std::array{IDC_SETTINGS_UI_LABEL, IDC_SETTINGS_UI, 0},

			 std::array{IDC_MODEL_LABEL, IDC_ASR_MODEL, 0},

			 std::array{IDC_ASR_LABEL, IDC_ASR_PROFILE, 1},

			 std::array{IDC_SETTINGS_BACKEND_LABEL, IDC_BACKEND, 2},

			 std::array{IDC_SETTINGS_LANGUAGE_LABEL, IDC_LANGUAGE, 3},

			 std::array{IDC_SETTINGS_COUNT_LABEL, IDC_COUNT, 1},

			 std::array{IDC_SETTINGS_METHOD_LABEL, IDC_SETTINGS_METHOD, 2}}) {

			place( label, 24, 54 + row * 44, 152, 26) ;

			place( combo, valueX, 50 + row * 44, valueWidth, 240) ;
		}

		place( IDC_SETTINGS_UPDATE, 24, 100, width - 56, 28) ;
		place( IDC_SETTINGS_UPDATE_NOTE, 24, 138, width - 56, 112) ;
		place( IDC_DIARIZE, 24, 50, width - 56, 28) ;

		place( IDC_SPEECH_FILTER, 24, 230, width - 56, 28) ;

		place( IDC_AUDIO_INFO, 24, 320, width - 56, 170) ;

		place( IDC_AUDIO_REFRESH, 24, 502, 240, 28) ;

		place( IDC_AUDIO_KEYS, 280, 502, width - 312, 28) ;

		// 実API childをtabのclient内へ配置し、適用buttonを二重に置きません

		if( apiVisited_) {

			MoveWindow( api_.settingsHandle(), MulDiv( 20, dpi, 96), MulDiv( 40, dpi, 96),

					   MulDiv( width - 40, dpi, 96), MulDiv( height - 142, dpi, 96), TRUE) ;

			api_.layoutSettingsPage() ;
		}

		TabPageBackground::invalidate( window_) ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 設定dialogの編集と明示Apply、tab描画、配置、closeを未適用draftへ振り分けます
	 * @param[in] window UI threadが所有するdialogの通知中有効なHWND。DWLP_USERへ登録したSettingsWindowを寿命中だけ借用します
	 * @param[in] message 初期化、command/notify、描画/サイズ、close等のWin32 message ID
	 * @param[in] wparam commandでは操作ID/通知code、WM_ERASEBKGND/PRINTCLIENT/CTLCOLORSTATICでは同期借用HDCです。他はOS定義値です
	 * @param[in,out] lparam WM_INITDIALOGは非null this、WM_NOTIFYは同期借用NMHDR、WM_GETMINMAXINFOは書込MINMAXINFOです
	 *        WM_CTLCOLORSTATICと設定変更通知では自child HWNDです。他は種類に応じた通知値です
	 * @return 初期化と処理済み通知はTRUE、未登録/未処理/例外後はFALSEです。背景描画はDWLP_MSGRESULTへ成否を設定してTRUE、対象staticの色通知は借用brush
	 *        handleを直接返します
	 * @note brushはTabPageBackground::labelのstock NULL_BRUSHまたはNULLで、呼出側は解放しません。closeは暗黙Applyしません
	 *        例外は固定診断表示へ変換してcallback外へ出しません
	 */
	//------------------------------------------------------------------------------

	static INT_PTR CALLBACK dialogProc( HWND window, UINT message, WPARAM wparam,

									   LPARAM lparam) noexcept

	{

		auto *self =

			reinterpret_cast<SettingsWindow *>( GetWindowLongPtrW( window, DWLP_USER)) ; // 借用

		// 同期作成時だけ所有者と結び付けます

		if( message == WM_INITDIALOG) {

			self = reinterpret_cast<SettingsWindow *>( lparam) ; // CreateDialogParam同期境界

			self->window_ = window ;

			SetWindowLongPtrW( window, DWLP_USER, lparam) ;

			return TRUE ;
		}

		// 初期化前の通知は標準処理へ返します

		if( !self) {

			return FALSE ;
		}

		try {

			// tab背面を描き、checkbox等が親背景を要求してもfooterとpageを区別します

			if( message == WM_ERASEBKGND || message == WM_PRINTCLIENT) {

				const bool painted = TabPageBackground::paint( self->item( IDC_SETTINGS_TABS), window,

															  reinterpret_cast<HDC>( wparam)) ;

				SetWindowLongPtrW( window, DWLP_MSGRESULT, painted ? TRUE : FALSE) ;

				return TRUE ;
			}

			// page内の有限staticだけをtab背景へ合わせ、footerは標準dialogへ返します

			if( message == WM_CTLCOLORSTATIC) {

				const auto label = reinterpret_cast<HWND>( lparam) ; // 同期通知の自control

				const int  id = GetDlgCtrlID( label) ; // 本文を読まない固定ID

				// 一般/ASR/話者のpage labelだけを共通描画に含めます

				if( id == IDC_SETTINGS_UI_LABEL || id == IDC_SETTINGS_UPDATE_NOTE || id == IDC_MODEL_LABEL ||
				   id == IDC_ASR_LABEL ||

				   id == IDC_SETTINGS_BACKEND_LABEL || id == IDC_SETTINGS_LANGUAGE_LABEL ||

				   id == IDC_SETTINGS_COUNT_LABEL || id == IDC_SETTINGS_METHOD_LABEL) {

					return reinterpret_cast<INT_PTR>( TabPageBackground::label(

						self->item( IDC_SETTINGS_TABS), label, reinterpret_cast<HDC>( wparam))) ;
				}
			}

			// theme handle/brushを保持せず、次描画で現在の標準色を取得します

			if( message == WM_THEMECHANGED || message == WM_SYSCOLORCHANGE) {

				TabPageBackground::invalidate( window) ;
			}

			// childの編集/保存ACKは所有HWNDを照合し、値やsecretをmessageへ載せません
			if( message == ApiTextCorrectionWindow::K_SETTINGS_CHANGED_MESSAGE &&
			   reinterpret_cast<HWND>( lparam) == self->api_.settingsHandle()) {
				self->updateApply() ;
				return TRUE ;
			}

			// tab切替はsource/キー/本文操作から独立させます

			if( message == WM_NOTIFY) {

				const auto *note = reinterpret_cast<const NMHDR *>( lparam) ; // 同期borrow

				// 自分のtabだけを処理します

				if( note && note->idFrom == IDC_SETTINGS_TABS && note->code == TCN_SELCHANGE) {

					self->selectTab() ;

					return TRUE ;
				}
			}

			// resize後もApply/Closeがtab内容へ重なりません

			if( message == WM_SIZE) {

				self->layout() ;

				return TRUE ;
			}

			// 読めるAPI価格tableと四tabを維持する最小sizeです

			if( message == WM_GETMINMAXINFO) {

				auto	 *limits = reinterpret_cast<MINMAXINFO *>( lparam) ; // OS出力borrow

				const int dpi = static_cast<int>( GetDpiForWindow( window)) ; // 現DPI

				limits->ptMinTrackSize = {MulDiv( 860, dpi, 96), MulDiv( 690, dpi, 96)} ;

				return TRUE ;
			}

			// closeはApplyを暗黙実行しません

			if( message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL)) {

				self->close() ;

				return TRUE ;
			}

			// 自分のcontrolsだけを既存契約へ渡します

			if( message == WM_COMMAND) {

				const int id = LOWORD(wparam) ; // 固定ID

				// 無効controlの遅い通知も保存中のsnapshotを変更しません
				if( self->localBusy_ &&
				   (id == IDC_ASR_MODEL || id == IDC_ASR_PROFILE || id == IDC_BACKEND || id == IDC_LANGUAGE ||
					id == IDC_COUNT || id == IDC_SETTINGS_METHOD || id == IDC_DIARIZE || id == IDC_SPEECH_FILTER ||
					id == IDC_SETTINGS_UI || id == IDC_SETTINGS_UPDATE)) {
					return TRUE ;
				}

				// 設定系統をdraft内で変更し、実行中jobへ触れません

				if( id == IDC_ASR_MODEL && HIWORD(wparam) == CBN_SELCHANGE) {

					const auto previous = self->draft_.asrModel ; // 系統切替前の値

					self->draft_.asrModel = static_cast<AsrModel>( self->selected( id)) ;

					// 初めて音声APIへ切り替える時は文脈優先を既定にします

					if( self->draft_.asrModel == AsrModel::OPEN_AI_AUDIO &&

					   previous != AsrModel::OPEN_AI_AUDIO) {

						self->draft_.asrProfile = AsrProfile::QUALITY ;
					}

					self->profiles() ;

					self->audioMetadata() ;

				} else if( id == IDC_ASR_PROFILE && HIWORD(wparam) == CBN_SELCHANGE) {

					// 実registryの品質だけをdraftへ保持します

					self->draft_.asrProfile = static_cast<AsrProfile>( self->selected( id)) ;

					self->audioMetadata() ;

					self->constrain() ;

				} else if( id == IDC_BACKEND && HIWORD(wparam) == CBN_SELCHANGE) {

					// CPU専用構成から戻るときの明示選択を保持します

					self->preferredBackend_ = self->selected( id) ;

				} else if( id == IDC_LANGUAGE && HIWORD(wparam) == CBN_SELCHANGE) {

					// 旧Auto通知でも新規認識は日本語へ固定します
					self->draft_.autoLanguage = false ;

				} else if( id == IDC_AUDIO_REFRESH) {

					self->api_.refreshAudioMetadata() ;

					self->audioMetadata() ;

				} else if( id == IDC_AUDIO_KEYS) {

					TabCtrl_SetCurSel( self->item( IDC_SETTINGS_TABS), 3) ;

					self->selectTab() ;

				} else if( id == IDC_SETTINGS_APPLY) {

					// 一回の明示Applyだけが次jobの値を確定します

					self->apply() ;
				}

				self->updateApply() ;

				return TRUE ;
			}

		} catch( const OperationError &error) {

			// 例外はdialog境界で固定診断へ変換し秘密/free textを表示しません

			SetDlgItemTextW(

				window, IDC_SETTINGS_NOTE,

				(resource( IDS_INTERNAL_ERROR) + L" [" + std::to_wstring( error.code()) + L"]")

					.c_str()) ;

		} catch( ...) {

			// 未知例外もcallback外へ漏らしません

			SetDlgItemTextW( window, IDC_SETTINGS_NOTE, resource( IDS_INTERNAL_ERROR).c_str()) ;
		}

		return FALSE ;
	}

	/// MainWindowが先に作成し後に解放するcontroller
	ApiTextCorrectionWindow								  &api_ ;

	/// mainが所有するmodeless dialog
	HWND												   window_ = nullptr ;

	/// session内の値だけ、新規保存schemaを持ちません
	EngineOptions										   draft_ ;

	/// 正常local Apply後だけ更新するsession基準
	EngineOptions										   applied_ ;

	/// 適用済みUI言語
	UiLanguage											   appliedLanguage_ = UiLanguage::JAPANESE ;

	/// 未適用UI言語
	UiLanguage											   language_ = UiLanguage::JAPANESE ;

	/// draft内の明示GPU/CPU選択
	LRESULT												   preferredBackend_ = 0 ;

	/// 同じsessionの再openで入力を破棄しません
	bool												   opened_ = false ;

	/// 明示API表示をsession一回へ制限
	bool												   audioVisited_ = false ;

	/// 明示API tab初回表示だけmetadata更新を許可
	bool												   apiVisited_ = false ;

	/// model取得/導入中の適用拒否
	bool												   locked_ = false ;

	/// 同期値確定
	std::function<void( const EngineOptions &, UiLanguage)> apply_ ;
	/// productionは保存ACK契約
	bool												   asyncApply_ = false ;
	/// local保存の二重Apply/編集を禁止
	bool												   localBusy_ = false ;
	/// close/openを跨ぐACK拒否
	std::uint64_t										   session_ = 0 ;
	/// ACK待ちの固定草稿
	EngineOptions										   submitted_ ;
	/// 同じApplyの表示言語
	UiLanguage											   submittedLanguage_ = UiLanguage::JAPANESE ;
	/// 一般tabの未適用選好
	bool												   updateCheck_ = true ;
	/// 保存ACK後のclean基準
	bool												   appliedUpdateCheck_ = true ;
	/// 保存中の固定snapshot
	bool												   submittedUpdateCheck_ = true ;
} ;

} // namespace koefude
