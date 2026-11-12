//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file app_update.cpp
 * @brief stable feedの厳密検証と明示更新workerを実装します
 * @version 1.0.0
 * @date 2026/11/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 1.0.0 Shunji Ogawa 2026/11/10 新規作成
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1378のbounded feedとstale/取消し境界を追加
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
#include "core/app_update.h"
#include <optional>
#include <algorithm>
#include <charconv>
#include <json.hpp>
#include <limits>
#include <set>
#include <utility>
#include <vector>
namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief canonical stable版だけを受理します
 * @param[in] text vは任意
 * @return 不正/範囲超過はnullopt
 * @see koefude::parseUpdateVersion
 */
//------------------------------------------------------------------------------
std::optional<UpdateVersion> parseUpdateVersion( std::string_view text) noexcept
{
	// tagにだけ許す単一vを除き、空/非canonicalな入力は後続で拒否します
	if( text.starts_with( 'v')) {
		text.remove_prefix( 1) ;
	}
	UpdateVersion out ; // 完全三成分だけ返します
	// 三成分を一度ずつ読み、余剰/先頭0/符号/範囲超過を拒否します
	for( std::size_t at = 0; at < out.parts.size(); ++at) {
		const auto dot = text.find( '.') ; // 次の区切り
		const auto part = text.substr( 0, dot) ; // 所有元の短いborrow
		unsigned   value = 0 ; // 符号なし整数境界
		const auto parsed = std::from_chars( part.data(), part.data() + part.size(), value) ;
		// 三成分の位置とcanonical表記が一致しない入力は候補にしません
		if( part.empty() || part.size() > 5 || (part.size() > 1 && part[0] == '0') || parsed.ec != std::errc{} ||
		   parsed.ptr != part.data() + part.size() || value > 65535 || ((at < 2) != (dot != std::string_view::npos))) {
			return std::nullopt ;
		}
		out.parts[at] = static_cast<std::uint16_t>( value) ;
		text = dot == std::string_view::npos ? std::string_view{} : text.substr( dot + 1) ;
	}
	return out ;
}
//------------------------------------------------------------------------------
/**
 * @brief canonical三成分を作ります
 * @param[in] version 検証済み版
 * @return ASCII版
 * @see koefude::updateVersionText
 */
//------------------------------------------------------------------------------
std::string updateVersionText( UpdateVersion version)
{
	return std::to_string( version.parts[0]) + "." + std::to_string( version.parts[1]) + "." +
		   std::to_string( version.parts[2]) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 手動追跡可能なasset転送先だけを受理します
 * @param[in] url Location値
 * @return release-assets固定host/pathの正規HTTPSならtrue
 * @see koefude::allowedUpdateRedirect
 */
//------------------------------------------------------------------------------
bool allowedUpdateRedirect( std::string_view url) noexcept
{
	constexpr std::string_view PREFIX =
		"https://release-assets.githubusercontent.com/github-production-release-asset/" ; // 固定先
	return url.size() > PREFIX.size() && url.size() <= 8192 && url.starts_with( PREFIX) &&
		   std::all_of( url.begin(), url.end(),
					   //------------------------------------------------------------------------------
					   /**
					    * @brief URL内の曖昧区切りを拒否します
					    * @param[in] c ASCII byte
					    * @return 正規URL文字
					    */
					   //------------------------------------------------------------------------------
					   []( unsigned char c)
					   {
						   return c > 32 && c < 127 && c != '\\' && c != '#' ;
					   }) ;
}
//------------------------------------------------------------------------------
/**
 * @brief bounded JSONとassetを照合します
 * @param[in] bytes 有界UTF-8
 * @return 正規asset
 * @throws UpdateFailure 不正feed
 * @see koefude::parseUpdateFeed
 */
//------------------------------------------------------------------------------
UpdateAsset parseUpdateFeed( std::string_view bytes)
{
	try {
		// byte上限をparser allocationより先に適用します
		if( bytes.empty() || bytes.size() > K_UPDATE_METADATA_LIMIT) {
			throw UpdateFailure{} ;
		}
		std::vector<std::set<std::string>> keys ; // 深さ16以下の重複key検出
		const auto						   json = nlohmann::json::parse(
			bytes,
			//------------------------------------------------------------------------------
			/**
			 * @brief 深さ/文字列/重複を読込み時に拒否します
			 * @param[in] depth 入力深さ
			 * @param[in] event parser段階
			 * @param[in,out] value 同段階の値
			 * @return 受理時true
			 */
			//------------------------------------------------------------------------------
			[&]( int depth, nlohmann::json::parse_event_t event, nlohmann::json &value)
			{
				// 余分な深さや巨大文字列は下位構造を採用しません
				if( depth > 16 || (value.is_string() && value.get_ref<const std::string &>().size() > 65536)) {
					throw UpdateFailure{} ;
				}
				// object毎に独立したkey集合を持ちます
				if( event == nlohmann::json::parse_event_t::object_start) {
					keys.emplace_back() ;
				}
				// 後勝ちで信頼fieldを上書きしません
				if( event == nlohmann::json::parse_event_t::key &&
				   !keys.back().insert( value.get<std::string>()).second) {
					throw UpdateFailure{} ;
				}
				// 閉じたobjectの検証状態を解放します
				if( event == nlohmann::json::parse_event_t::object_end) {
					keys.pop_back() ;
				}
				return true ;
			}) ; // invalid UTF-8/comment/trailing inputもstrict拒否
		const auto tag = json.at( "tag_name").get<std::string>() ; // 外部tag、最大長を次で検証
		const auto version = parseUpdateVersion( tag) ; // stableだけ
		// latest endpointでもdraft/prereleaseを自分で検証します
		if( !version || tag.size() > 18 || !json.at( "draft").is_boolean() || !json.at( "prerelease").is_boolean() ||
		   json.at( "draft").get<bool>() || json.at( "prerelease").get<bool>() || !json.at( "assets").is_array() ||
		   json.at( "assets").size() > 64) {
			throw UpdateFailure{} ;
		}
		const auto name =
			"KoeFude-" + updateVersionText( *version) + "-windows-x64-cuda-vulkan-setup.exe" ; // registry外backend不可
		const auto url = "https://github.com/EnterOgawa/koe-fude/releases/download/" + tag + "/" +
						 name ; // 外部URLでroutingしません
		std::optional<UpdateAsset> out ; // 完全一致は一件だけ
		// 最大64件を走査し、曖昧な同名assetを拒否します
		for( const auto &asset : json.at( "assets")) {
			// 別形式ZIP/notes/checksumは実行候補にしません
			if( asset.at( "name").get<std::string>() != name) {
				continue ;
			}
			// 型/出所/準備状態/sizeを数値変換より先に検証します
			if( out || asset.at( "browser_download_url").get<std::string>() != url ||
			   asset.at( "state").get<std::string>() != "uploaded" || !asset.at( "size").is_number_unsigned()) {
				throw UpdateFailure{} ;
			}
			UpdateAsset value ; // 下記の全field検証後にだけ公開
			value.version = *version ;
			value.url = url ;
			value.size = asset.at( "size").get<std::uint64_t>() ;
			const auto digest = asset.at( "digest").get<std::string>() ; // 固定sha256形式
			// 未掲載digest/空asset/巨大assetを未検証のまま取得しません
			if( value.size == 0 || value.size > K_UPDATE_INSTALLER_LIMIT || digest.size() != 71 ||
			   !digest.starts_with( "sha256:")) {
				throw UpdateFailure{} ;
			}
			// 正確に32byteへ変換し、未知表記を寛容に読みません
			for( std::size_t at = 0; at < value.sha256.size(); ++at) {
				unsigned	byte = 0 ; // SHAの当byte
				const auto *first = digest.data() + 7 + at * 2 ; // 検証済み範囲
				const auto	parsed = std::from_chars( first, first + 2, byte, 16) ;
				// 非hexの一部だけを採用しません
				if( parsed.ec != std::errc{} || parsed.ptr != first + 2) {
					throw UpdateFailure{} ;
				}
				value.sha256[at] = static_cast<std::uint8_t>( byte) ;
			}
			out = std::move( value) ;
		}
		// 正規backendのinstallerがないreleaseを利用可能と表示しません
		if( !out) {
			throw UpdateFailure{} ;
		}
		return *out ;
	} catch( const UpdateFailure &) {
		throw ;
	} catch( ...) {
		throw UpdateFailure{} ; // parser自由文/応答本文をUI/logへ出しません
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 所有workerを開始します
 * @param[in] current 現appの版
 * @param[in] platform 非NULL同期境界
 * @param[in] notify HWNDへの値なし通知、空可
 * @see koefude::AppUpdateController::AppUpdateController(koefude::UpdateVersion, std::unique_ptr< koefude::IUpdatePlatform >, std::function< void()>)
 */
//------------------------------------------------------------------------------
AppUpdateController::AppUpdateController( UpdateVersion current, std::unique_ptr<IUpdatePlatform> platform,
										 std::function<void()> notify)
	: current_( current), platform_( std::move( platform)), notify_( std::move( notify))
{
	// NULL依存でworkerを成立させません
	if( !platform_) {
		throw UpdateFailure{UpdateError::IO} ;
	}
	worker_ = std::jthread(
		//------------------------------------------------------------------------------
		/**
		 * @brief 所有workerの例外境界を呼びます
		 */
		//------------------------------------------------------------------------------
		[this]
		{
			run() ;
		}) ;
}
//------------------------------------------------------------------------------
/**
 * @brief stopped後にjoinします。初期化失敗時は取消しを要求します
 * @see koefude::AppUpdateController::~AppUpdateController
 */
//------------------------------------------------------------------------------
AppUpdateController::~AppUpdateController()
{
	requestStop() ;
	// UIは正常終了でstoppedを確認済み、初期化失敗時だけも所有threadを回収します
	if( worker_.joinable()) {
		worker_.join() ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 起動一回または手動確認を受理します
 * @return op、重複/停止は0
 * @see koefude::AppUpdateController::check
 */
//------------------------------------------------------------------------------
std::uint64_t AppUpdateController::check()
{
	return submit( 0, 0) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 明示取得を受理します
 * @param[in] checkedOp 確認済み候補のop
 * @return 新op、stale/重複は0
 * @see koefude::AppUpdateController::download
 */
//------------------------------------------------------------------------------
std::uint64_t AppUpdateController::download( std::uint64_t checkedOp)
{
	return submit( 1, checkedOp) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 保存済みidleの明示終了更新です
 * @param[in] readyOp 準備済みop
 * @return 新op、stale/重複は0
 * @see koefude::AppUpdateController::launch
 */
//------------------------------------------------------------------------------
std::uint64_t AppUpdateController::launch( std::uint64_t readyOp)
{
	return submit( 2, readyOp) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 受付を一slotへ固定します
 * @param[in] kind 0確認/1取得/2起動
 * @param[in] owner 前段op
 * @return 新opまたは0
 * @see koefude::AppUpdateController::submit
 */
//------------------------------------------------------------------------------
std::uint64_t AppUpdateController::submit( unsigned kind, std::uint64_t owner)
{
	// 同期基盤失敗後は壊れたmutexへ再入しません
	if( fatal_) {
		return 0 ;
	}
	std::scoped_lock lock( mutex_) ; // 通信/I/Oを含まない小slot
	// 前段のowner/状態と終端ACK受領を必須にします
	if( busy_ || event_ || stopping_ || stopped_ || next_ == UINT64_MAX ||
	   (kind == 1 && (state_.state != UpdateState::AVAILABLE || state_.operation != owner)) ||
	   (kind == 2 && (state_.state != UpdateState::READY || state_.operation != owner))) {
		return 0 ;
	}
	cancellation_ = std::stop_source{} ;
	state_.operation = next_++ ;
	state_.state = kind == 0 ? UpdateState::CHECKING : kind == 1 ? UpdateState::DOWNLOADING : UpdateState::LAUNCHING ;
	state_.error = UpdateError::NONE ;
	state_.code = 0 ;
	busy_ = true ;
	pending_ = kind ;
	condition_.notify_one() ;
	return state_.operation ;
}
//------------------------------------------------------------------------------
/**
 * @brief 当該処理だけを取消します
 * @see koefude::AppUpdateController::cancel
 */
//------------------------------------------------------------------------------
void AppUpdateController::cancel()
{
	// fatal終端後の同期経路へ再入しません
	if( fatal_) {
		return ;
	}
	std::scoped_lock lock( mutex_) ; // stop sourceと受付更新の競合だけを保護
	cancellation_.request_stop() ;
}
//------------------------------------------------------------------------------
/**
 * @brief 終端を一度回収します
 * @return 新通知なしはnullopt
 * @see koefude::AppUpdateController::takeEvent
 */
//------------------------------------------------------------------------------
std::optional<UpdateEvent> AppUpdateController::takeEvent()
{
	unsigned unread = 1 ; // 固定fatalを一度だけ公開
	// 同期基盤が失敗した場合もChecking等を残さず終端にします
	if( fatal_) {
		// UIはoperation=0の固定fatalだけを例外的に現在処理へ結びます
		if( fatal_.compare_exchange_strong( unread, 2)) {
			UpdateEvent event ;
			event.state = UpdateState::FAILURE ;
			event.error = UpdateError::IO ;
			return event ;
		}
		return std::nullopt ;
	}
	std::scoped_lock lock( mutex_) ; // 小ACKだけを転送
	return std::exchange( event_, std::nullopt) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 受付停止と協調取消しを要求します
 * @see koefude::AppUpdateController::requestStop
 */
//------------------------------------------------------------------------------
void AppUpdateController::requestStop()
{
	// fatal後は既に資源解放へ進んでいます
	if( fatal_ || stopped_) {
		return ;
	}
	std::scoped_lock lock( mutex_) ; // workerを待たず終了意図だけを記録
	stopping_ = true ;
	cancellation_.request_stop() ;
	condition_.notify_one() ;
}
//------------------------------------------------------------------------------
/**
 * @brief 通知とplatform解放完了を返します
 * @return 完了時true
 * @see koefude::AppUpdateController::stopped
 */
//------------------------------------------------------------------------------
bool AppUpdateController::stopped() const noexcept
{
	return stopped_.load() ;
}
//------------------------------------------------------------------------------
/**
 * @brief platform同期処理を単一workerで逐次実行します
 * @see koefude::AppUpdateController::run
 */
//------------------------------------------------------------------------------
void AppUpdateController::run() noexcept
{
	try {
		// 一件ずつ終端化し、stopでは所有platformを解放して終了します
		for(;;) {
			unsigned		kind = 0 ; // 今回段階
			UpdateEvent		result ; // 小終端DTO
			std::stop_token stop ; // 同じopへ束縛
			{
				std::unique_lock lock( mutex_) ; // I/Oはこのscope外
				condition_.wait( lock,
								//------------------------------------------------------------------------------
								/**
								 * @brief 停止/要求だけを待ちます
								 * @return 起床条件
								 */
								//------------------------------------------------------------------------------
								[this]
								{
									return stopping_ || pending_.has_value() ;
								}) ;
				// 終了後に新しいGET/取得/起動を始めません
				if( stopping_) {
					break ;
				}
				kind = *pending_ ;
				pending_.reset() ;
				result = state_ ;
				stop = cancellation_.get_token() ;
			}
			try {
				// 受付直後の停止もnetwork/最終起動前に反映します
				if( stop.stop_requested()) {
					throw UpdateFailure{UpdateError::CANCELLED} ;
				}
				// checkは旧stagingを再実行せず破棄して新しい一回だけを取得します
				if( kind == 0) {
					platform_->reset() ;
					asset_.reset() ;
					const auto feed = platform_->check( stop) ; // 固定metadataだけ
					// 非200を最新版なしという成功へ変換しません
					if( feed.status != 200) {
						throw UpdateFailure{UpdateError::HTTP, feed.status} ;
					}
					asset_ = parseUpdateFeed( feed.body) ;
					result.version = asset_->version ;
					result.installed = platform_->installed() ;
					result.state = asset_->version > current_ ? UpdateState::AVAILABLE : UpdateState::CURRENT ;
				} else if( kind == 1) {
					// 明示取得だけが一時file作成と音声外のdownloadを許可します
					platform_->prepare( *asset_, stop) ;
					result.state = UpdateState::READY ;
				} else {
					// 明示終了更新と保存完了後にだけ呼ばれる最終境界です
					platform_->launch( *asset_, stop) ;
					result.state = UpdateState::LAUNCHED ;
				}
				// 準備完了後の取消しも実行可能なReadyを残しません
				if( stop.stop_requested() && kind != 2) {
					throw UpdateFailure{UpdateError::CANCELLED} ;
				}
			} catch( const UpdateFailure &error) {
				result.error = error.error ;
				result.code = error.code ;
				result.state = error.error == UpdateError::CANCELLED ? UpdateState::CANCELLED
							   : kind == 0							 ? UpdateState::UNAVAILABLE
																	 : UpdateState::FAILURE ;
				platform_->reset() ;
				asset_.reset() ;
			} catch( ...) {
				result.error = UpdateError::IO ; // 自由文を持たないworker例外境界
				result.state = kind == 0 ? UpdateState::UNAVAILABLE : UpdateState::FAILURE ;
				platform_->reset() ;
				asset_.reset() ;
			}
			{
				std::scoped_lock lock( mutex_) ; // 終端だけを公開、stale混用なし
				state_ = result ;
				event_ = result ;
				busy_ = false ;
			}
			try {
				// 通知失敗でも小ACKはpollで回収できます
				if( notify_) {
					notify_() ;
				}
			} catch( ...) {
				// 保存済みACKを維持し、UIの通常timerへ委ねます
			}
		}
	} catch( ...) {
		// 同期基盤失敗は固定診断をmutex外へ残し、UIのpollで終端化します
		fatal_ = 1 ;
	}
	platform_->reset() ;
	platform_.reset() ;
	stopped_ = true ;
}
} // namespace koefude
