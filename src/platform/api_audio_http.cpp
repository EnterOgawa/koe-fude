//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_audio_http.cpp
 * @brief 固定HTTPSへの単発送信をasync WinHTTPへ接続します
 * @version 1.0.0
 * @date 2026/11/08
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/08 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/08 handle closingまでcallback所有を維持
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1370の文脈分割と明示途中再開を接続
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
#include "core/api_audio_transcription.h"
#include "platform/win_helpers.h"
#include <span>
#include <algorithm>
#include <array>
#include <atomic>
#include <bcrypt.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <windows.h>
#include <winhttp.h>

#if defined(KOEFUDE_AUDIO_HTTP_TESTING)
// 専用試験targetだけがWinHTTP API依存を差し替え、通常EXEへ入口を出しません
#include "api_audio_http_link_seam.h"
#endif

namespace koefude {
namespace {
/** @brief WinHTTP handleの所有解放です */
struct HttpDeleter {
	//------------------------------------------------------------------------------
	/**
	 * @brief handleを閉じます
	 * @param[in] handle null可
	 */
	//------------------------------------------------------------------------------
	void operator()( void *handle) const noexcept
	{
		if( handle) {
			WinHttpCloseHandle( handle) ;
		}
	}
} ;
using HttpHandle = std::unique_ptr<void, HttpDeleter> ; // session/connectionのRAII
/** @brief request close完了までstack上で生存するcallback contextです */
struct AsyncRequest {
	/// 完了イベント
	UniqueHandle			changed{CreateEventW( nullptr, FALSE, FALSE, nullptr)} ;
	/// HANDLE_CLOSING専用
	UniqueHandle			closed{CreateEventW( nullptr, TRUE, FALSE, nullptr)} ;
	/// 最後の完了種類
	std::atomic<DWORD>		status{0} ;
	/// 元WinHTTP code
	std::atomic<DWORD>		error{0} ;
	/// READ_COMPLETEの実byte数
	std::atomic<DWORD>		count{0} ;
	/// callback終了まで有効な読取先
	std::array<char, 65536> buffer{} ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 例外を出さず完了事実だけを通知します
 * @param[in] handle WinHTTP request
 * @param[in] context requestの借用context
 * @param[in] status 通知種類
 * @param[in] information 同期借用値
 * @param[in] length 借用byte数
 */
//------------------------------------------------------------------------------
void CALLBACK statusCallback( HINTERNET handle, DWORD_PTR context, DWORD status, LPVOID information,
							 DWORD length) noexcept
{
	static_cast<void>( handle) ;
	auto *state = reinterpret_cast<AsyncRequest *>( context) ; // WinHTTP context境界、closing後まで有効
	// context未登録の通知には所有値がありません
	if( !state) {
		return ;
	}
	// 最後のcallbackでだけ破棄可能を通知します
	if( status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
		SetEvent( state->closed.get()) ;
		return ;
	}
	// 非完了の診断通知を処理段階と誤認しません
	if( status != WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE && status != WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE &&
	   status != WINHTTP_CALLBACK_STATUS_READ_COMPLETE && status != WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
		return ;
	}
	// エラー情報はcodeだけを取得します
	if( status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR && information && length >= sizeof(WINHTTP_ASYNC_RESULT)) {
		state->error.store( static_cast<WINHTTP_ASYNC_RESULT *>( information)->dwError) ;
	}
	state->count.store( length) ;
	state->status.store( status, std::memory_order_release) ;
	SetEvent( state->changed.get()) ;
}
/** @brief 固定先へ一回だけ送る同期worker境界です */
class ApiAudioHttp final : public IApiAudioHttpTransport {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 一窓のPCMを内部WAV/multipartへ変換し、固定audio/transcriptionsへ一回だけPOSTします
	 * @param[in] samples 同期中だけ借用する16kHz mono float列です。1〜9,600,000sample（600秒）、各値は有限で[-1,1]とします
	 * @param[in] autoLanguage trueなら言語hintを省略し、falseならlanguages[]=jaを送ります。認識言語の返答を保証する指定ではありません
	 * @param[in] key callerが復帰まで保持するAPI秘密へのborrowです。Authorizationの一時copyを作り、秘密ownerを移譲・永続化しません
	 * @param[in] stop 送信前と完了待機中の取消しtokenです。送信後の取消しはサーバー側の処理/課金停止を保証しません
	 * @param[in] operationDeadline steady_clock上の当窓の絶対期限です。完了待機は内部設定時のnow+600秒との早い方を照合します
	 * @return attempted（SendRequestを呼んだ事実）、status（取得済みHTTP、未取得0）、error（通信/取消し/期限等のcode）と
	 * 最大1MiBの受信bodyを所有するDTOです。error=0はこの層のエラー未記録で、HTTP成功や生成/保存成功そのものではありません
	 * @note 通常はbodyのEOFまで読みますが、error時は部分bodyが残り得ます。完全取得bitやJSON検証/保存ACKは含まず、後段で判定します
	 * requestを閉じcallback終了まで待ってからborrowを終えます。この終了待ちや準備を含む関数復帰時刻の上限をdeadlineは保証しません
	 * @see koefude::IApiAudioHttpTransport::post
	 */
	//------------------------------------------------------------------------------
	ApiAudioHttpResponse post( std::span<const float> samples, bool autoLanguage, const ApiKeySecret &key,
							  std::stop_token stop, std::chrono::steady_clock::time_point operationDeadline) override
	{
		ApiAudioHttpResponse response ; // 失敗時もattemptedを保持
		bool				 eof = false ; // 正常な本文EOFの事実
		bool				 reusable = false ; // 正常完読の場合だけ次窓へ親handleを残す
		struct ParentGuard {
			ApiAudioHttp &owner ; // 一操作のtransport owner
			bool		 &reusable ; // request終了後の再利用判定
			//------------------------------------------------------------------------------
			/**
			 * @brief request closing後に失敗した親handleを解放します
			 */
			//------------------------------------------------------------------------------
			~ParentGuard()
			{
				// 取消し/HTTP失敗/設定失敗は次の窓へ接続を残しません
				if( !reusable) {
					owner.connection_.reset() ;
					owner.session_.reset() ;
				}
			}
		} parentGuard{*this, reusable} ;
		try {
			// 送信前取消しと過大bodyはHTTP0です
			if( stop.stop_requested() || samples.empty() || samples.size() > K_API_AUDIO_WINDOW_SAMPLES) {
				response.error = stop.stop_requested() ? ERROR_CANCELLED : ERROR_FILE_TOO_LARGE ;
				return response ;
			}
			std::array<unsigned char, 16> random{} ; // 元file情報を含まない境界乱数
			// OS乱数が使えない時に弱い固定境界へ切替えません
			if( BCryptGenRandom( nullptr, random.data(), static_cast<ULONG>( random.size()),
							   BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
				response.error = ERROR_GEN_FAILURE ;
				return response ;
			}
			std::string	   boundary = "KoeFudeAudio" ; // 有界ASCIIのmultipart境界
			constexpr char K_HEX[] = "0123456789abcdef" ; // 元pathと無関係な表現
			// 16byteを衝突検証可能なASCIIへ変換します
			for( const auto byte : random) {
				boundary.push_back( K_HEX[byte >> 4]) ;
				boundary.push_back( K_HEX[byte & 15]) ;
			}
			struct AudioBody {
				std::string wav ; // この窓だけの最小WAV
				std::string body ; // closing完了まで生存するmultipart
				//------------------------------------------------------------------------------
				/**
				 * @brief callback終了後に音声copyを消します
				 */
				//------------------------------------------------------------------------------
				~AudioBody()
				{
					SecureZeroMemory( wav.data(), wav.size()) ;
					SecureZeroMemory( body.data(), body.size()) ;
				}
			} audioBody ;
			audioBody.wav = makeApiAudioWav( samples) ;
			audioBody.body = makeApiAudioMultipart( audioBody.wav, autoLanguage, boundary) ;
			const std::string_view body = audioBody.body ; // request closingより長寿命のborrow
			// 一job内でだけsession/connectionをlazyに再利用します
			if( !session_) {
				session_.reset( WinHttpOpen( L"KoeFude/0.1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
										   WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC)) ;
				// 親設定が成立しなければPOSTを始めません
				if( !session_ || !WinHttpSetTimeouts( session_.get(), 15000, 15000, 30000, 300000)) {
					response.error = GetLastError() ;
					return response ;
				}
			}
			// 同じ固定hostだけへ接続します
			if( !connection_) {
				connection_.reset( WinHttpConnect( session_.get(), L"api.openai.com", 443, 0)) ;
				if( !connection_) {
					response.error = GetLastError() ;
					return response ;
				}
			}
			AsyncRequest state ; // request RAIIより先に生成し最後に解放
			HttpHandle	 request( WinHttpOpenRequest( connection_.get(), L"POST", L"/v1/audio/transcriptions", nullptr,
													WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
													WINHTTP_FLAG_SECURE)) ; // 正規TLS
			// requestを作れない時はSendRequestを呼びません
			if( !request) {
				response.error = GetLastError() ;
				return response ;
			}

			// event作成失敗をasync開始前に拒否します
			if( !state.changed || !state.closed) {
				response.error = ERROR_NOT_ENOUGH_MEMORY ;
				return response ;
			}
			DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES |
							 WINHTTP_DISABLE_AUTHENTICATION ; // 自動転送/認証/再送を禁止
			DWORD	  headerLimit = 65536 ; // HTTP header上限
			DWORD	  autoLogon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH ; // Windows資格情報送信なし
			DWORD_PTR context = reinterpret_cast<DWORD_PTR>( &state) ; // callback借用、closingまで生存
			// hardening設定のどれかが失敗した場合に緩いfallbackを使いません
			if( !WinHttpSetOption( request.get(), WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) ||
			   !WinHttpSetOption( request.get(), WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE, &headerLimit,
								 sizeof(headerLimit)) ||
			   !WinHttpSetOption( request.get(), WINHTTP_OPTION_AUTOLOGON_POLICY, &autoLogon, sizeof(autoLogon)) ||
			   !WinHttpSetOption( request.get(), WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)) ||
			   WinHttpSetStatusCallback( request.get(), statusCallback,
										WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES,
										0) == WINHTTP_INVALID_STATUS_CALLBACK) {
				response.error = GetLastError() ;
				return response ;
			}
			/** @brief すべての例外経路でcallback終了まで待つrequest所有guardです */
			struct ClosingGuard {
				HttpHandle	 &request ; // このscopeのrequest owner
				AsyncRequest &state ; // requestより長寿命のcontext
				//------------------------------------------------------------------------------
				/**
				 * @brief close後にHANDLE_CLOSINGを待って借用を終えます
				 */
				//------------------------------------------------------------------------------
				~ClosingGuard()
				{
					request.reset() ;
					static_cast<void>( WaitForSingleObject( state.closed.get(), INFINITE)) ;
				}
			} guard{request, state} ; // headers/body allocation失敗にも適用
			const auto deadline =
				std::min( operationDeadline,
						 std::chrono::steady_clock::now() +
							 std::chrono::seconds( 600)) ; // 一requestだけの期限。全録音には流用しません
			const auto wait =
				//------------------------------------------------------------------------------
				/**
				 * @brief 完了通知を有界に待ちます
				 * @param[in] expected 期待status
				 * @return 正常通知
				 */
				//------------------------------------------------------------------------------
				[&state, &response, &stop, deadline]( DWORD expected)
				{
					// 実イベント待機中も取消し/期限を確認し、偽の率を作りません
					for(;;) {
						// 取消し後の課金停止は保証しません
						if( stop.stop_requested() || std::chrono::steady_clock::now() >= deadline) {
							response.error = stop.stop_requested() ? ERROR_CANCELLED : ERROR_TIMEOUT ;
							return false ;
						}
						const DWORD event = WaitForSingleObject( state.changed.get(), 50) ; // workerだけが待機
						// 完了イベントのrelease通知後だけcallback値を読みます
						if( event == WAIT_OBJECT_0) {
							const auto status = state.status.load( std::memory_order_acquire) ; // 最新完了
							// WinHTTP内部再送要求も再試行しません
							if( status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
								response.error = state.error.load() ;
								return false ;
							}
							// 想定外通知をEOF成功へ縮小しません
							if( status != expected) {
								response.error = ERROR_INVALID_DATA ;
								return false ;
							}
							return true ;
						}
						// OS待機異常を無限待機へしません
						if( event == WAIT_FAILED) {
							response.error = GetLastError() ;
							return false ;
						}
					}
				} ;
			/** @brief header内の一時秘密を全例外経路で消すownerです */
			struct HeaderGuard {
				std::wstring text ; // WinHTTPが同期借用する秘密header
				//------------------------------------------------------------------------------
				/**
				 * @brief コピー済みの秘密も含む全headerをゼロ化します
				 */
				//------------------------------------------------------------------------------
				~HeaderGuard()
				{
					SecureZeroMemory( text.data(), text.size() * sizeof(wchar_t)) ;
				}
			} headers ; // request guardより先に秘密を消します
			headers.text = L"Content-Type: multipart/form-data; boundary=" + toWide( boundary) +
						   L"\r\nAccept: application/json\r\nAccept-Encoding: "
						   L"identity\r\nAuthorization: Bearer " ;
			headers.text.append( key.borrow().begin(), key.borrow().end()) ;
			const bool headersSet =
				WinHttpAddRequestHeaders( request.get(), headers.text.c_str(), static_cast<DWORD>( headers.text.size()),
										 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) != FALSE ;
			SecureZeroMemory( headers.text.data(), headers.text.size() * sizeof(wchar_t)) ;
			// 全処理で一回のSendRequestだけを許可します
			if( headersSet && !stop.stop_requested()) {
				response.attempted = true ;
				const bool sent =
					WinHttpSendRequest( request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, const_cast<char *>( body.data()),
									   static_cast<DWORD>( body.size()), static_cast<DWORD>( body.size()),
									   context) != FALSE ; // bodyはclosingまで呼出元所有
				const DWORD sendError = sent ? ERROR_SUCCESS : GetLastError() ; // 呼出し直後の固定値
				// 自動有料retryを行いません
				if( (sent || sendError == ERROR_IO_PENDING) && wait( WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE)) {
					const bool	receiving = WinHttpReceiveResponse( request.get(), nullptr) != FALSE ; // async受信開始
					const DWORD receiveError = receiving ? ERROR_SUCCESS : GetLastError() ; // 同期開始失敗を保持
					// response header通知までだけ待機します
					if( (receiving || receiveError == ERROR_IO_PENDING) &&
					   wait( WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE)) {
						DWORD size = sizeof(response.status) ; // 数値statusの返却長
						// 応答headerを数値だけで読みます
						if( !WinHttpQueryHeaders( request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
												WINHTTP_HEADER_NAME_BY_INDEX, &response.status, &size,
												WINHTTP_NO_HEADER_INDEX)) {
							response.error = GetLastError() ;
						} else {
							// 最大1MiBまで読み、EOFまたは異常で終了します
							for(;;) {
								const bool	reading = WinHttpReadData( request.get(), state.buffer.data(),
																	  static_cast<DWORD>( state.buffer.size()),
																	  nullptr) != FALSE ; // async buffer
								const DWORD readError = reading ? ERROR_SUCCESS : GetLastError() ; // 直後の元code
								// 同期読取開始失敗も成功EOFと区別します
								if( !reading && readError != ERROR_IO_PENDING) {
									response.error = readError ;
									break ;
								}
								// READ_COMPLETE前にbufferを再利用しません
								if( !(reading || readError == ERROR_IO_PENDING) ||
								   !wait( WINHTTP_CALLBACK_STATUS_READ_COMPLETE)) {
									break ;
								}
								const auto count = state.count.load() ; // 実受信byte数
								// EOFは正常な本文終端です
								if( count == 0) {
									eof = true ;
									break ;
								}
								// 過大JSONをallocation前に拒否します
								if( count > K_API_AUDIO_RESPONSE_LIMIT - response.body.size()) {
									response.error = ERROR_FILE_TOO_LARGE ;
									break ;
								}
								response.body.append( state.buffer.data(), count) ;
							}
						}
					}
					// ReceiveResponseの即時失敗も元codeを保持します
					if( !receiving && receiveError != ERROR_IO_PENDING) {
						response.error = receiveError ;
					}
				}
				// ERROR_IO_PENDINGを完了後の古いLastErrorで失敗へ変換しません
				if( !sent && sendError != ERROR_IO_PENDING) {
					response.error = sendError ;
				}
			} else {
				response.error = stop.stop_requested() ? ERROR_CANCELLED : GetLastError() ;
			}
			reusable = response.attempted && !response.error && response.status >= 200 && response.status < 300 &&
					   eof && !stop.stop_requested() ;
			// requestはclosing後に解放し、親handleは正常完読時だけ次窓へ残します
			return response ;
		} catch( ...) {
			response.error = ERROR_NOT_ENOUGH_MEMORY ; // 生exception/キーを返しません
			response.body.clear() ;
			return response ; // guardは既にclosingを待ち終えています
		}
	}

	private:
	/// 一操作のworkerだけが使用、最後に解放
	HttpHandle session_ ;
	/// sessionより先に解放
	HttpHandle connection_ ;
} ;
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 固定WinHTTP音声transportを作ります
 * @return worker専用owner
 * @see koefude::makeApiAudioHttp
 */
//------------------------------------------------------------------------------
std::unique_ptr<IApiAudioHttpTransport> makeApiAudioHttp()
{
	return std::make_unique<ApiAudioHttp>() ;
}
} // namespace koefude
