//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file model_archive.cpp
 * @brief OSのtarを使い、固定memberを標準出力だけへ取り出します
 * @version 1.0.0
 * @date 2026/09/13
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/13 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/13 非表示の有界子processとhandle継承制限を実装
 * 0.1.0 Shunji Ogawa 2026/10/19 宣言の役割を明記
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
#include "platform/model_download_io.h"

#include <span>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.h"
#include "platform/win_helpers.h"

namespace koefude {
namespace {

/** @brief 子process attribute listの内部資源を解放します */
struct AttributeDeleter {
	//------------------------------------------------------------------------------
	/**
	 * @brief listを解放します
	 * @param[in] value 呼出し中有効な初期化済みlist
	 */
	//------------------------------------------------------------------------------
	void operator()( void *value) const noexcept
	{
		DeleteProcThreadAttributeList( static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>( value)) ;
	}
} ;

/** @brief 所有する子processと、その子孫だけを異常時に回収します */
struct ArchiveProcess {
	/// KILL_ON_JOB_CLOSEの所有job object
	UniqueHandle job ;
	/// 起動したtarだけのhandle
	UniqueHandle process ;
	/// 作成時のsuspended thread
	UniqueHandle thread ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 例外時も子process終了後にpipeの所有権を返します
	 */
	//------------------------------------------------------------------------------
	~ArchiveProcess()
	{
		// 未回収の自作子processだけを終了し、thread強制終了を行わない
		if( process && WaitForSingleObject( process.get(), 0) != WAIT_OBJECT_0) {
			TerminateProcess( process.get(), ERROR_CANCELLED) ;
			WaitForSingleObject( process.get(), INFINITE) ;
		}
	}
} ;

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 検証済みarchiveから固定memberだけを有界sinkへ取り出します
 * @param[in,out] archive 読取り専用に使う検証済み一時file。実行中の追記は禁止
 * @param[in] member registryの安全な相対member名
 * @param[in] sink 同じworkerで呼ぶ有界出力先。空は禁止
 * @param[in] stop 協調取消し。所有する子processを終了して回収します
 * @throws OperationError OSのtar不在、process/pipe/展開失敗、上限、取消し
 * @see koefude::extractModelArchive
 */
//------------------------------------------------------------------------------
void extractModelArchive( ModelStagingFile &archive, const std::string_view member, const ModelDownloadSink &sink,
						 const std::stop_token stop)
{
	checkCancelled( stop) ;
	// bsdtarのpatternやoptionになる文字をregistry境界でも再確認する
	if( !sink || member.empty() || member.size() > 1024 || member.front() == '-' || member.front() == '/' ||
	   member.back() == '/' || member.find( "..") != std::string_view::npos ||
	   std::any_of( member.begin(), member.end(),
				   //------------------------------------------------------------------------------
				   /**
				    * @brief 固定memberの文字を確認します
				    * @param[in] unit ASCII文字
				    * @return 許可外の文字ならtrue
				    */
				   //------------------------------------------------------------------------------
				   []( const char unit)
				   {
					   return !((unit >= 'a' && unit <= 'z') || (unit >= 'A' && unit <= 'Z') ||
								(unit >= '0' && unit <= '9') || unit == '/' || unit == '_' || unit == '-' ||
								unit == '.') ;
				   })) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_INVALID_NAME) ;
	}
	std::wstring system( 32768, L'\0') ; // System32の取得先。PATH検索をしない
	// OSから返るSystem32 pathの文字数。終端とbuffer内の範囲を検証する
	const UINT	 length = GetSystemDirectoryW( system.data(), static_cast<UINT>( system.size())) ;
	// OSが返す通常のsystem directoryだけを使う
	if( length == 0 || length >= system.size()) {
		throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
	}
	system.resize( length) ;
	const auto			executable = validateLocalFile( std::filesystem::path( system) / L"tar.exe") ; // OSのtar
	// 検証済みOS tarへ固定optionと単一member名を渡す所有command line
	std::wstring		command = L"\"" + executable.native() + L"\" -xOf - -- \"" + toWide( member) + L"\"" ;
	SECURITY_ATTRIBUTES inherited{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE} ; // 許可handleだけ継承する属性
	HANDLE				rawRead = nullptr ; // stdout pipeの受取先
	HANDLE				rawWrite = nullptr ; // stdout pipeの送信先
	// archiveの内容はdisk上のpathへ展開せずpipeで受ける
	if( !CreatePipe( &rawRead, &rawWrite, &inherited, 1024 * 1024)) {
		throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
	}
	UniqueHandle output( rawRead) ; // 親だけが読むstdout
	UniqueHandle outputWriter( rawWrite) ; // 子へ渡したら親では閉じるstdout
	// 親の読取り側は子processへ継承させない
	if( !SetHandleInformation( output.get(), HANDLE_FLAG_INHERIT, 0)) {
		throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
	}
	archive.rewind() ;
	HANDLE rawInput = nullptr ; // 読取りだけを許すstdinの複製
	// 書込み・削除権限を子へ渡さず、検証済みfileの同じobjectを読む
	if( !DuplicateHandle( GetCurrentProcess(), archive.handle(), GetCurrentProcess(), &rawInput, GENERIC_READ, TRUE, 0)) {
		throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
	}
	UniqueHandle input( rawInput) ; // 子へ継承するstdin
	UniqueHandle errors( CreateFileW( L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherited,
									OPEN_EXISTING, 0,
									nullptr)) ; // 固定OS sinkへstderrを捨てる
	// stderrを親のconsoleや永続logへ流さない
	if( errors.get() == INVALID_HANDLE_VALUE) {
		throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
	}
	SIZE_T	   attributeBytes = 0 ; // 継承handle listに必要な領域
	// 継承handle属性listの必要容量照会結果。errorと必要byte数を併せて検証する
	const BOOL queried = InitializeProcThreadAttributeList( nullptr, 1, 0, &attributeBytes) ;
	// 想定外のsizeを大きいallocationへ変換しない
	if( queried || GetLastError() != ERROR_INSUFFICIENT_BUFFER || attributeBytes > 65536 || attributeBytes == 0) {
		throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_INVALID_DATA) ;
	}
	std::vector<std::byte> attributeData( attributeBytes) ; // process作成まで保持する整列済み領域
	// Win32が管理する可変長構造体へのAPI境界。attribute_dataを移動しない
	auto				  *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>( attributeData.data()) ;
	// 初期化に成功したlistだけをdeleterへ渡す
	if( !InitializeProcThreadAttributeList( attributes, 1, 0, &attributeBytes)) {
		throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
	}
	const std::unique_ptr<void, AttributeDeleter> ownedAttributes( attributes) ; // list内部の資源を所有
	const std::array<HANDLE, 3> handles{input.get(), outputWriter.get(), errors.get()} ; // 継承allowlist
	// 他の音声・履歴・token・model handleを子へ漏らさない
	if( !UpdateProcThreadAttribute( attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
								  const_cast<HANDLE *>( handles.data()), sizeof(handles), nullptr, nullptr)) {
		throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
	}
	ArchiveProcess child ; // 全失敗経路で回収するprocess資源
	child.job.reset( CreateJobObjectW( nullptr, nullptr)) ;
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{} ; // memory・子孫数・crash時回収の上限
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS |
											  JOB_OBJECT_LIMIT_PROCESS_MEMORY |
											  JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION ;
	limits.BasicLimitInformation.ActiveProcessLimit = 1 ;
	limits.ProcessMemoryLimit = 256ULL * 1024 * 1024 ;
	// 上限を設定できない場合に無制限のhelperを起動しない
	if( !child.job ||
	   !SetInformationJobObject( child.job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
		throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
	}
	STARTUPINFOEXW startup{} ; // 制限した標準入出力だけを子へ設定する
	startup.StartupInfo.cb = sizeof(startup) ;
	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW ;
	startup.StartupInfo.wShowWindow = SW_HIDE ;
	startup.StartupInfo.hStdInput = input.get() ;
	startup.StartupInfo.hStdOutput = outputWriter.get() ;
	startup.StartupInfo.hStdError = errors.get() ;
	startup.lpAttributeList = attributes ;
	PROCESS_INFORMATION process{} ; // 作成したprocess/threadの受取先
	// shellを介さず、job制限を設定するまで実行を停止して作成する
	if( !CreateProcessW( executable.c_str(), command.data(), nullptr, nullptr, TRUE,
					   CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr, system.c_str(),
					   &startup.StartupInfo, &process)) {
		throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
	}
	child.process.reset( process.hProcess) ;
	child.thread.reset( process.hThread) ;
	// 必須上限の設定後だけarchiveを読み始める
	if( !AssignProcessToJobObject( child.job.get(), child.process.get()) ||
	   ResumeThread( child.thread.get()) == static_cast<DWORD>( -1)) {
		throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
	}
	outputWriter.reset() ;
	input.reset() ;
	errors.reset() ;
	std::array<std::byte, 65536> buffer{} ; // pipeから回収する固定上限
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes( 10) ; // 展開時間の上限
	// EOF/終了/取消し/timeoutまで出力を逐次回収し、pipe飽和で子を待たせ続けない
	for(;;) {
		checkCancelled( stop) ;
		DWORD available = 0 ; // 今すぐ読めるstdout byte数
		// pipe切断以外の失敗を正常EOFとして扱わない
		if( !PeekNamedPipe( output.get(), nullptr, 0, nullptr, &available, nullptr)) {
			const DWORD code = GetLastError() ; // pipeの元code
			// 子がstdoutを閉じた場合は終了codeを確認する
			if( code == ERROR_BROKEN_PIPE) {
				break ;
			}
			throw OperationError( ErrorKind::MODEL_REJECTED, code) ;
		}
		// 読める分だけ同期読取りし、取消し確認を定期的に挟む
		if( available != 0) {
			DWORD		read = 0 ; // 今回の実読取り量
			const DWORD wanted = std::min( available, static_cast<DWORD>( buffer.size())) ; // buffer上限
			// pipe失敗や不正EOFを部分fileの確定へ進めない
			if( !ReadFile( output.get(), buffer.data(), wanted, &read, nullptr) || read == 0) {
				throw OperationError( ErrorKind::MODEL_REJECTED, GetLastError()) ;
			}
			sink( std::span<const std::byte>( buffer).first( read)) ;
		}
		// 終了済みで全出力を消費した場合は検証へ進む
		else if( WaitForSingleObject( child.process.get(), 1) == WAIT_OBJECT_0) {
			DWORD		remaining = 0 ; // 最初のpeekからprocess終了までに届いた出力
			// 子process終了直後のpipe残量照会が成功したか
			const BOOL	peeked = PeekNamedPipe( output.get(), nullptr, 0, nullptr, &remaining, nullptr) ;
			const DWORD code = peeked ? ERROR_SUCCESS : GetLastError() ; // 最終pipe照会の診断
			// 終了直前の書込みを未読のまま捨てない
			if( peeked && remaining != 0) {
				continue ;
			}
			// 読取り完了または通常のpipe切断だけをEOFとする
			if( peeked || code == ERROR_BROKEN_PIPE) {
				break ;
			}
			throw OperationError( ErrorKind::MODEL_REJECTED, code) ;
		}
		// 圧縮展開に無制限のCPU時間を与えない
		if( std::chrono::steady_clock::now() >= deadline) {
			throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_TIMEOUT) ;
		}
	}
	// stdout終了後も子processの終了を取消し可能に確認する
	while( WaitForSingleObject( child.process.get(), 50) == WAIT_TIMEOUT) {
		checkCancelled( stop) ;
		// stdoutを閉じて残留するhelperも時間上限を適用する
		if( std::chrono::steady_clock::now() >= deadline) {
			throw OperationError( ErrorKind::MODEL_REJECTED, ERROR_TIMEOUT) ;
		}
	}
	DWORD exitCode = 0 ; // stderrを記録せず数値の終了codeだけを保持する
	// archiveエラーを最終fileの通常成功へ変換しない
	if( !GetExitCodeProcess( child.process.get(), &exitCode) || exitCode != 0) {
		throw OperationError( ErrorKind::MODEL_REJECTED, exitCode == 0 ? GetLastError() : exitCode) ;
	}
	checkCancelled( stop) ;
}

} // namespace koefude
