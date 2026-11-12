//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_catalog_http.cpp
 * @brief 秘密を分離した固定metadata GETをasync WinHTTPへ接続します
 * @version 1.0.0
 * @date 2026/11/08
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/08 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/08 handle closingまでcallback所有を維持
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "core/api_model_catalog.h"
#include "core/api_text_correction.h"
#include "platform/win_helpers.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <windows.h>
#include <winhttp.h>

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
class ApiCatalogHttp final : public IApiCatalogTransport {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定Modelsへ一回GETします
	 * @param[in] key 秘密borrow
	 * @param[in] stop 取消し
	 * @param[in] deadline operation期限
	 * @return 有界応答
	 */
	//------------------------------------------------------------------------------
	ApiCatalogHttpResponse listModels( const ApiKeySecret &key, std::stop_token stop,
									  std::chrono::steady_clock::time_point deadline) override
	{
		return get( false, &key, stop, deadline) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief Authorizationなしの固定価格GETです
	 * @param[in] stop 取消し
	 * @param[in] deadline operation期限
	 * @return 有界応答
	 */
	//------------------------------------------------------------------------------
	ApiCatalogHttpResponse getPrices( std::stop_token stop, std::chrono::steady_clock::time_point deadline) override
	{
		return get( true, nullptr, stop, deadline) ;
	}

	private:
	//------------------------------------------------------------------------------
	/**
	 * @brief 外部URLを受け取らず固定二先へ一回GETします
	 * @param[in] prices 公開先選択
	 * @param[in] key Modelsだけ必須、公開先はnull
	 * @param[in] stop 取消し
	 * @param[in] operationDeadline 全体期限
	 * @return 有界応答
	 */
	//------------------------------------------------------------------------------
	ApiCatalogHttpResponse get( bool prices, const ApiKeySecret *key, std::stop_token stop,
							   std::chrono::steady_clock::time_point operationDeadline)
	{
		ApiCatalogHttpResponse response ; // 生bodyはparser以外へ渡しません
		try {
			// 取得前取消しとModelsのキー不足はHTTP0です
			if( stop.stop_requested() || std::chrono::steady_clock::now() >= operationDeadline || (!prices && !key)) {
				response.error = stop.stop_requested()									 ? ERROR_CANCELLED
								 : std::chrono::steady_clock::now() >= operationDeadline ? ERROR_TIMEOUT
																						 : ERROR_INVALID_DATA ;
				return response ;
			}
			const HttpHandle session( WinHttpOpen( L"KoeFude/0.1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
												 WINHTTP_NO_PROXY_BYPASS,
												 WINHTTP_FLAG_ASYNC)) ; // proxy/auth探索なし
			// session作成失敗を本文なしの診断で返します
			if( !session) {
				response.error = GetLastError() ;
				return response ;
			}
			static_cast<void>( WinHttpSetTimeouts( session.get(), 15000, 15000, 30000, 30000)) ;
			const HttpHandle connection( WinHttpConnect(
				session.get(), prices ? L"developers.openai.com" : L"api.openai.com", 443, 0)) ; // allowlist固定
			// host/pathを外部入力から組み立てません
			if( !connection) {
				response.error = GetLastError() ;
				return response ;
			}
			AsyncRequest state ; // request RAIIより先に生成し最後に解放
			HttpHandle	 request( WinHttpOpenRequest( connection.get(), L"GET",
													prices ? L"/api/docs/pricing.md" : L"/v1/models", nullptr,
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
			} guard{request, state} ; // header allocation失敗にも適用
			const auto deadline =
				std::min( operationDeadline,
						 std::chrono::steady_clock::now() + std::chrono::seconds( 30)) ; // active requestにも全op期限
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
			headers.text = prices ? L"Accept: text/markdown\r\nAccept-Encoding: identity\r\n"
								  : L"Accept: application/json\r\nAccept-Encoding: "
									L"identity\r\nAuthorization: Bearer " ;
			// 公開価格は秘密を引数として受け取らずAuthorizationを構築しません
			if( !prices) {
				headers.text.append( key->borrow().begin(), key->borrow().end()) ;
			}
			const bool headersSet =
				WinHttpAddRequestHeaders( request.get(), headers.text.c_str(), static_cast<DWORD>( headers.text.size()),
										 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) != FALSE ;
			SecureZeroMemory( headers.text.data(), headers.text.size() * sizeof(wchar_t)) ;
			// 全処理で一回のSendRequestだけを許可します
			if( headersSet && !stop.stop_requested()) {
				const bool sent =
					WinHttpSendRequest( request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
									   context) != FALSE ; // GETにはrequest bodyを渡しません
				const DWORD sendError = sent ? ERROR_SUCCESS : GetLastError() ; // 呼出し直後の固定値
				// metadataの自動retryを行いません
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
							std::array<wchar_t, 512> header{} ; // 小さい固定header上限
							DWORD headerSize = static_cast<DWORD>( sizeof(header)) ; // WinHTTP byte単位
							// MIMEは応答をparseする前に必須取得します
							if( !WinHttpQueryHeaders( request.get(), WINHTTP_QUERY_CONTENT_TYPE,
													WINHTTP_HEADER_NAME_BY_INDEX, header.data(), &headerSize,
													WINHTTP_NO_HEADER_INDEX)) {
								response.error = GetLastError() ;
								return response ;
							}
							response.contentType = toUtf8( header.data()) ;
							header.fill( 0) ;
							headerSize = static_cast<DWORD>( sizeof(header)) ;
							// Content-Encoding未通知だけはidentityと同等、未知圧縮はparserで拒否します
							if( WinHttpQueryHeaders( request.get(), WINHTTP_QUERY_CONTENT_ENCODING,
												   WINHTTP_HEADER_NAME_BY_INDEX, header.data(), &headerSize,
												   WINHTTP_NO_HEADER_INDEX)) {
								response.encoding = toUtf8( header.data()) ;
							} else if( GetLastError() != ERROR_WINHTTP_HEADER_NOT_FOUND) {
								response.error = GetLastError() ;
								return response ;
							}
							const std::size_t limit = prices ? 524288 : 1048576 ; // 固定GET別上限
							// 上限まで読み、EOFまたは異常で終了します
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
									break ;
								}
								// 各固定GETの応答上限をallocation前に確認します
								if( count > limit - response.body.size()) {
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
			// guardがrequest closeとHANDLE_CLOSING待機を行ってからstateを解放します
			return response ;
		} catch( ...) {
			response.error = ERROR_NOT_ENOUGH_MEMORY ; // 生exception/キーを返しません
			response.body.clear() ;
			return response ; // guardは既にclosingを待ち終えています
		}
	}
} ;
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 固定WinHTTP GET境界を作ります
 * @return worker専用owner
 * @see koefude::makeApiCatalogHttp
 */
//------------------------------------------------------------------------------
std::unique_ptr<IApiCatalogTransport> makeApiCatalogHttp()
{
	return std::make_unique<ApiCatalogHttp>() ;
}
} // namespace koefude
