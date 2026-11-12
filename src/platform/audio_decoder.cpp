//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file audio_decoder.cpp
 * @brief Media Foundationで用途別に音声を検証し、所有PCMへ変換します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 M4A/AACのnative decodeを実装
 * 0.1.0 Shunji Ogawa 2026/09/10 PCMを専用一時fileへ順次保存し、固定時間制限を撤廃
 * 0.1.0 Shunji Ogawa 2026/09/13 製品M4Aと開発用の有界MP3/WAVEの入口を分離
 * 0.1.0 Shunji Ogawa 2026/10/02 同じstreamのPCM16検証付き製品WAVを接続
 * 0.1.0 Shunji Ogawa 2026/10/02 MP3全frame検査とnative属性/実出力数照合を接続
 * 0.1.0 Shunji Ogawa 2026/10/02 FLAC metadataと既知総数の実出力照合を接続
 * 0.1.0 Shunji Ogawa 2026/10/02 M4A内ALACのcookie/packet検証と実PCM数照合を接続
 * 0.1.0 Shunji Ogawa 2026/10/02 明示HEの事前検査・native core・実数/時刻照合を接続
 * 0.1.0 Shunji Ogawa 2026/10/03 一般AACの全packet範囲をreader作成前に検査
 * 0.1.0 Shunji Ogawa 2026/10/19 分岐意図・宣言の役割を明記
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実契約・所有寿命・戻り値の意味を具体化
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 * @note COMとMedia Foundationの所有は本呼出しに閉じ、UI threadでは実行しません
 */
//------------------------------------------------------------------------------
#include "platform/audio_decoder.h"
#include "platform/aac_packet_bounds.h"
#include "platform/alac_decode_checks.h"
#include "platform/alac_format.h"
#include "platform/evaluation_audio_decoder.h"
#include "platform/flac_decode_checks.h"
#include "platform/flac_format.h"
#include "platform/heaac_decode_checks.h"
#include "platform/heaac_format.h"
#include "platform/m4a_reference_gate.h"
#include "platform/mp3_decode_checks.h"
#include "platform/mp3_format.h"
#include "platform/temporary_store.h"
#include "platform/wave_decode_checks.h"
#include "platform/wave_format.h"
#include "platform/win_helpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

// Media FoundationとWRLの宣言はWindows基本型の後にincludeする
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <windows.h>
#include <wrl/client.h>

namespace koefude {
namespace {

using Microsoft::WRL::ComPtr ; // COM参照を所有するsmart pointer

// SDKの負値sentinelをAPIが要求するDWORDへ明示変換する
constexpr DWORD K_AUDIO_STREAM = static_cast<DWORD>( MF_SOURCE_READER_FIRST_AUDIO_STREAM) ;
constexpr DWORD K_ALL_STREAMS = static_cast<DWORD>( MF_SOURCE_READER_ALL_STREAMS) ; // 全stream選択用
constexpr DWORD K_MEDIA_SOURCE = static_cast<DWORD>( MF_SOURCE_READER_MEDIASOURCE) ; // source属性用
// MFMPEG4Format_Baseとbig-endianのalacを組み合わせた実測full GUIDだけを許可する
constexpr GUID	K_M4A_ALAC_SUBTYPE{0x616C6163, 0x767A, 0x494D, {0xB4, 0x78, 0xF2, 0x9D, 0x25, 0xDC, 0x90, 0x37}} ;

/** @brief 製品の受付形式と開発用clipの上限を混同しない内部用途です */
enum class InputPurpose {
	/// 製品のM4A/AACと検証済みALAC。固定時間制限を設けない
	PRODUCT_M4A,
	/// 製品のRIFF/PCM16。同じ有界な長時間処理を使う
	PRODUCT_WAVE,
	/// 製品のMPEG-1/2 Layer III。frame検査付きで固定時間上限なし
	PRODUCT_MP3,
	/// 製品のnative FLAC。metadataと既知総数を照合し固定時間上限なし
	PRODUCT_FLAC,
	/// 開発用MP3。実decode60秒・原file64MiBまで
	EVALUATION_MP3,
	/// 合成検証用のPCM WAVE。同じclip上限を適用
	EVALUATION_WAVE,
} ;

//------------------------------------------------------------------------------
/**
 * @brief 固定clip制限を適用しない製品用途を判定します
 * @param[in] purpose 検証済みの内部用途
 * @return 製品M4A/WAV/MP3/FLACの場合true
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool isProduct( const InputPurpose purpose)
{
	return purpose == InputPurpose::PRODUCT_M4A || purpose == InputPurpose::PRODUCT_WAVE ||
		   purpose == InputPurpose::PRODUCT_MP3 || purpose == InputPurpose::PRODUCT_FLAC ;
}

//------------------------------------------------------------------------------
/**
 * @brief 拡張子をWindowsの大文字小文字差をなくして検証します
 * @param[in] path 呼出し中に参照するpath。fileは開きません
 * @return 小文字へ変換した拡張子
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::wstring lowerExtension( const std::filesystem::path &path)
{
	auto extension = path.extension().native() ; // 比較する拡張子の所有文字列
	// 各文字を小文字化し、.MP3等も同じcodec条件で確認する
	for( auto &character : extension) { // 今回変換する拡張子の文字
		character = static_cast<wchar_t>( std::towlower( character)) ;
	}
	return extension ;
}

//------------------------------------------------------------------------------
/**
 * @brief HRESULTを元の値を維持して共通例外へ変換します
 * @param[in] result 判定するHRESULT
 * @param[in] kind 呼出し元が区分する失敗種別
 * @throws OperationError resultが失敗の場合
 */
//------------------------------------------------------------------------------
void checkHr( const HRESULT result, const ErrorKind kind = ErrorKind::DECODE_FAILED)
{
	// 失敗したAPIの診断値を後続処理で失わないよう直ちに保存する
	if( FAILED(result)) {
		throw OperationError( kind, static_cast<std::uint32_t>( result)) ;
	}
}

/** @brief 本呼出しのCOM初期化とMedia Foundation使用を対応させます */
class MediaRuntime final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief MTAとMedia Foundationを開始します
	 * @throws OperationError 開始失敗時
	 */
	//------------------------------------------------------------------------------
	MediaRuntime()
	{
		checkHr( CoInitializeEx( nullptr, COINIT_MULTITHREADED)) ;
		const HRESULT result = MFStartup( MF_VERSION, MFSTARTUP_FULL) ; // platform開始の診断値
		// constructor完了前の失敗でもCOM参照数を元へ戻す
		if( FAILED(result)) {
			CoUninitialize() ;
			checkHr( result) ;
		}
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief COM objectが破棄された後にruntimeの使用を終了します
	 */
	//------------------------------------------------------------------------------
	~MediaRuntime() noexcept
	{
		// runtime終了時の失敗はこれ以上利用しないresourceの状態を変えない
		MFShutdown() ;
		CoUninitialize() ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 同じthreadのCOM初期化参照とMedia Foundation開始/終了を一回ずつ対応させるため、copy構築を禁止します
	 * @note 名前のない第1引数（const MediaRuntime &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	MediaRuntime( const MediaRuntime &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じthreadのCOM初期化参照とMedia Foundation開始/終了を一回ずつ対応させるため、copy代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const MediaRuntime &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	MediaRuntime &operator=( const MediaRuntime &) = delete ;
} ;

/** @brief 読込み中の原file変更を防ぐ非共有write handleを所有します */
class InputLock final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief local入力を開き、通常fileと容量を検証します
	 * @param[in] path 検証済みの絶対path。呼出し中だけ参照
	 * @throws OperationError 開けない場合、reparse、非通常file、上限超過
	 */
	//------------------------------------------------------------------------------
	explicit InputLock( const std::filesystem::path &path)
	{
		handle_ = CreateFileW( path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
							  FILE_ATTRIBUTE_NORMAL, nullptr) ;
		// 書込み・置換と競合する入力を検証済みとして扱わない
		if( handle_ == INVALID_HANDLE_VALUE) {
			throw OperationError( ErrorKind::INVALID_INPUT, GetLastError()) ;
		}
		BY_HANDLE_FILE_INFORMATION information{} ; // 開いたfileの属性と実size
		// constructor失敗時もhandleを閉じ、次の入力を受け付けられる状態へ戻す
		if( !GetFileInformationByHandle( handle_, &information)) {
			const DWORD error = GetLastError() ; // CloseHandleで上書きされる前の診断値
			CloseHandle( handle_) ;
			handle_ = INVALID_HANDLE_VALUE ;
			throw OperationError( ErrorKind::INVALID_INPUT, error) ;
		}
		bytes_ = (static_cast<std::uint64_t>( information.nFileSizeHigh) << 32U) | information.nFileSizeLow ;
		constexpr DWORD K_REJECTED_ATTRIBUTES = FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT |
												FILE_ATTRIBUTE_OFFLINE ; // 通常file以外の拒否属性
		// 自動同期やdirectoryと空fileを音声として扱わない。内容は順次読み込む
		if( (information.dwFileAttributes & K_REJECTED_ATTRIBUTES) != 0 || bytes_ == 0) {
			CloseHandle( handle_) ;
			handle_ = INVALID_HANDLE_VALUE ;
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief すべての終了経路で入力の読込みlockを解除します
	 */
	//------------------------------------------------------------------------------
	~InputLock() noexcept
	{
		CloseHandle( handle_) ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 開いたfileのbyte数を返します
	 * @return 0より大きく上限以下のsize
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t bytes() const noexcept
	{
		return bytes_ ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 入力fileの非共有write handleとCloseHandle責任を単独で保持するため、copy構築を禁止します
	 * @note 名前のない第1引数（const InputLock &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	InputLock( const InputLock &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 入力fileの非共有write handleとCloseHandle責任を単独で保持するため、copy代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const InputLock &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	InputLock &operator=( const InputLock &) = delete ;

	private:
	/// 本objectがCloseHandleする入力handle
	HANDLE		  handle_ = INVALID_HANDLE_VALUE ;
	/// 開いたfileのsize（byte）
	std::uint64_t bytes_ = 0 ;
} ;

/** @brief IMFMediaBuffer::Lock/Unlockを例外経路でも対応させます */
class BufferLock final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief decode bufferを呼出し中だけ固定します
	 * @param[in] buffer このobjectより長く生存するCOM buffer
	 * @throws OperationError Lock失敗時
	 */
	//------------------------------------------------------------------------------
	explicit BufferLock( IMFMediaBuffer &buffer) : buffer_( buffer)
	{
		checkHr( buffer_.Lock( &data_, nullptr, &bytes_)) ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief buffer参照より先に固定を解除します
	 */
	//------------------------------------------------------------------------------
	~BufferLock() noexcept
	{
		buffer_.Unlock() ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 固定中の読取りpointerを返します
	 * @return 本objectの寿命中だけ有効
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const BYTE *data() const noexcept
	{
		return data_ ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 現在の有効byte数を返します
	 * @return Lockで取得したbyte数
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] DWORD bytes() const noexcept
	{
		return bytes_ ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 借用IMFMediaBufferの一回のLockとUnlock、および固定pointerの寿命を対応させるため、copy構築を禁止します
	 * @note 名前のない第1引数（const BufferLock &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	BufferLock( const BufferLock &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 借用IMFMediaBufferの一回のLockとUnlock、および固定pointerの寿命を対応させるため、copy代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const BufferLock &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	BufferLock &operator=( const BufferLock &) = delete ;

	private:
	/// 呼出し側が所有する借用buffer
	IMFMediaBuffer &buffer_ ;
	/// Lock中だけ有効な借用pointer
	BYTE		   *data_ = nullptr ;
	/// data_から読取り可能なbyte数
	DWORD			bytes_ = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 用途別のlocal音声pathを正規化し、network/device pathを除外します
 * @param[in] path 利用者が指定したpath
 * @param[in] purpose 許可する形式と上限を区別する内部用途
 * @return 検証済みの絶対path
 * @throws OperationError 用途別のlocal入力条件を満たさない場合
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::filesystem::path validatePath( const std::filesystem::path &path, const InputPurpose purpose)
{
	const std::wstring input = path.native() ; // path構文を検証するUnicode値
	// UNC、Win32 device namespace、空の指定をAPIへ渡さない
	if( input.empty() || input.starts_with( L"\\\\") || input.find( L"://") != std::wstring::npos) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	const auto		   absolute = validateLocalFile( path) ; // 通常local fileと祖先属性を検証
	const auto		   extension = lowerExtension( absolute) ; // 大文字小文字を統一した拡張子
	// 製品入力または評価clipの用途に対応する許可拡張子
	const auto		   expected = purpose == InputPurpose::PRODUCT_M4A ? L".m4a"
								  : purpose == InputPurpose::EVALUATION_MP3 || purpose == InputPurpose::PRODUCT_MP3 ? L".mp3"
								  : purpose == InputPurpose::PRODUCT_FLAC ? L".flac"
																		  : L".wav" ;
	// 専用M4Aと上限付き開発clipを保持し、製品WAV/MP3を別用途で選ぶ
	const std::wstring root = absolute.root_path().native() ; // drive種別確認用root
	const UINT		   driveType = GetDriveTypeW( root.c_str()) ; // network driveを除外するOS値
	// 用途に対応するlocal disk上の拡張子だけを許可し、代替data streamも拒否する
	if( extension != expected || driveType == DRIVE_REMOTE || driveType == DRIVE_UNKNOWN ||
	   driveType == DRIVE_NO_ROOT_DIR || absolute.native().find( L':', 2) != std::wstring::npos) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	return absolute ;
}

//------------------------------------------------------------------------------
/**
 * @brief 出力media typeを明示して16kHz float monoを要求します
 * @param[in,out] reader workerだけが使用するsource reader
 * @throws OperationError 変換を構成できない場合
 */
//------------------------------------------------------------------------------
void configureOutput( IMFSourceReader &reader)
{
	ComPtr<IMFMediaType> output ; // 要求するPCM属性を所有
	checkHr( MFCreateMediaType( &output)) ;
	checkHr( output->SetGUID( MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ;
	checkHr( output->SetGUID( MF_MT_SUBTYPE, MFAudioFormat_Float)) ;
	checkHr( output->SetUINT32( MF_MT_AUDIO_NUM_CHANNELS, 1)) ;
	checkHr( output->SetUINT32( MF_MT_AUDIO_SAMPLES_PER_SECOND, K_SAMPLE_RATE)) ;
	checkHr( output->SetUINT32( MF_MT_AUDIO_BITS_PER_SAMPLE, 32)) ;
	checkHr( output->SetUINT32( MF_MT_AUDIO_BLOCK_ALIGNMENT, 4)) ;
	checkHr( output->SetUINT32( MF_MT_AUDIO_AVG_BYTES_PER_SECOND, K_SAMPLE_RATE * 4)) ;
	checkHr( reader.SetCurrentMediaType( K_AUDIO_STREAM, nullptr, output.Get())) ;

	ComPtr<IMFMediaType> actual ; // source readerが実際に受理した型
	checkHr( reader.GetCurrentMediaType( K_AUDIO_STREAM, &actual)) ;
	GUID subtype{} ; // float PCMであることを確認するsubtype
	checkHr( actual->GetGUID( MF_MT_SUBTYPE, &subtype)) ;
	// 要求が無視された場合に圧縮byte列をfloatとして解釈しない
	if( subtype != MFAudioFormat_Float || MFGetAttributeUINT32( actual.Get(), MF_MT_AUDIO_NUM_CHANNELS, 0) != 1 ||
	   MFGetAttributeUINT32( actual.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 0) != K_SAMPLE_RATE ||
	   MFGetAttributeUINT32( actual.Get(), MF_MT_AUDIO_BITS_PER_SAMPLE, 0) != 32) {
		throw OperationError( ErrorKind::UNSUPPORTED_CODEC) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 同じMF streamの指定位置から形式別の小さいmetadataだけを読みます
 * @param[in,out] stream 本workerが所有する同期stream
 * @param[in] offset 元stream先頭基準のbyte位置
 * @param[out] destination 形式別の最大量以内の借用span。失敗時は内容未確定
 * @param[in] maximumRead WAV/MP3は従来の18byte、ALACは32byte、FLACは34byte
 * @param[in] stop 各同期API前後で確認する取消しtoken
 * @return 実読取りbyte数。部分readの拒否はpure parserが行います
 * @throws OperationError 元のseek/read HRESULT、取消し、内部範囲不正
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::size_t readAudioHeader( IMFByteStream &stream, const std::uint64_t offset,
										  const std::span<std::byte> destination, const std::size_t maximumRead,
										  const std::stop_token stop)
{
	checkCancelled( stop) ;
	// parserの最大read量をAPI境界でも照合してULONGへのnarrowingを限定する
	if( destination.empty() || destination.size() > maximumRead || maximumRead > 34) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	checkHr( stream.SetCurrentPosition( offset)) ;
	checkCancelled( stop) ;
	ULONG read = 0 ; // EOFでも成功し得るAPIの実充足byte数
	// BYTE*は借用spanのobject表現を同じ同期呼出し中だけ書き込み、保持しない
	checkHr( stream.Read( reinterpret_cast<BYTE *>( destination.data()), static_cast<ULONG>( destination.size()), &read)) ;
	checkCancelled( stop) ;
	return read ;
}

//------------------------------------------------------------------------------
/**
 * @brief 用途別のcodecと容量を検証し、共有するMedia Foundation経路で変換します
 * @param[in] path 呼出し中だけ参照するlocal入力
 * @param[in] stop 協調的な取消しtoken
 * @param[in] progress worker上の段階通知。空を許可します
 * @param[in] purpose 製品M4A/WAV/MP3/FLACまたは上限付き開発clipの用途
 * @return 原入力属性と所有PCM。途中失敗では一時領域を解放します
 * @throws OperationError path/codec/容量/timelineが不正、API失敗、取消し
 */
//------------------------------------------------------------------------------
[[nodiscard]] AudioData decodeAudio( const std::filesystem::path &path, const std::stop_token stop,
									const ProgressCallback &progress, const InputPurpose purpose)
{
	checkCancelled( stop) ;
	reportProgress( progress, Stage::VALIDATING, -1) ;
	checkCancelled( stop) ;
	const auto		absolute = validatePath( path, purpose) ; // file解決後の所有Unicode path
	const InputLock input( absolute) ; // 処理中のwrite/deleteを拒否する原file lock
	// 開発clipだけに容量上限を適用し、製品の長時間処理を分離する
	if( !isProduct( purpose) && input.bytes() > K_EVALUATION_MAXIMUM_BYTES) {
		throw OperationError( ErrorKind::INPUT_TOO_LARGE) ;
	}
	const MediaRuntime	  runtime ; // 以下のCOM objectより先に開始し、後に解放
	ComPtr<IMFByteStream> stream ; // local fileに限定したbyte stream
	checkHr(
		MFCreateFile( MF_ACCESSMODE_READ, MF_OPENMODE_FAIL_IF_NOT_EXIST, MF_FILEFLAGS_NONE, absolute.c_str(), &stream)) ;
	std::optional<WaveInputInfo>  wave ; // 製品WAVだけで確定する元PCMの構造
	std::optional<Mp3InputInfo>	  mp3 ; // 製品MP3だけで確定するframe列の構造
	std::optional<FlacInputInfo>  flac ; // 製品FLACだけで確定するSTREAMINFOと範囲
	std::optional<AlacInputInfo>  alac ; // 製品M4AでALACの全検査が成功した所有属性
	std::optional<HeAacInputInfo> heaac ; // 明示HEだけで確定する全packet/時間の所有属性
	// 形式破損をMFの部分成功へ渡さず、同じstreamを検証して先頭へ戻す
	if( purpose == InputPurpose::PRODUCT_WAVE || purpose == InputPurpose::PRODUCT_MP3 ||
	   purpose == InputPurpose::PRODUCT_FLAC || purpose == InputPurpose::PRODUCT_M4A) {
		QWORD streamBytes = 0 ; // MFが開いたstreamの実byte数
		checkHr( stream->GetLength( &streamBytes)) ;
		// 原file lock時の実体と異なるstreamを検証済みとして扱わない
		if( streamBytes != input.bytes()) {
			throw OperationError( ErrorKind::DECODE_FAILED) ;
		}
		const auto read =
			//------------------------------------------------------------------------------
			/**
			 * @brief 同期MF read/seekをpure parserへ仲介します
			 * @param[in] offset 元stream基準の位置
			 * @param[out] destination 呼出し中だけ有効な形式別最大量の出力
			 * @return 実read byte数
			 */
			//------------------------------------------------------------------------------
			[&]( const std::uint64_t offset, const std::span<std::byte> destination)
			{
				const std::size_t maximumRead = purpose == InputPurpose::PRODUCT_FLAC  ? 34
												: purpose == InputPurpose::PRODUCT_M4A ? 32
																					   : 18 ; // 旧形式の上限を保持
				return readAudioHeader( *stream.Get(), offset, destination, maximumRead, stop) ;
			} ; // stream/stopを借用し、本decode呼出し内だけで使用する
		// WAVはPCM chunk、MP3は全frameの宣言範囲を同じstreamで検査する
		if( purpose == InputPurpose::PRODUCT_WAVE) {
			wave = inspectPcm16Wave( streamBytes, read, stop) ;
		}
		// MP3だけ全frameを検査する
		else if( purpose == InputPurpose::PRODUCT_MP3) {
			mp3 = inspectLayer3Mp3( streamBytes, read, stop) ;
		}
		// FLACはmetadataを検査する
		else if( purpose == InputPurpose::PRODUCT_FLAC) {
			flac = inspectNativeFlac( streamBytes, read, stop) ;
		} else { // 外部参照をMFへ渡す前に拒否し、ALACの構造を検査する
			// ALAC entryを識別した場合だけ、AACと異なる全packet条件を適用する
			if( inspectM4aLocalReferences( streamBytes, read, stop)) {
				alac = inspectM4aAlac( streamBytes, read, stop) ;
			}
			// 明示HEの専用時間契約
			else if( hasExplicitM4aHeaac( streamBytes, read, stop)) {
				heaac = inspectM4aHeaac( streamBytes, read, stop) ;
			} else { // 一般AACの配置を照合し、edit/時間/ASCは既存native検査へ委ねる
				static_cast<void>( inspectM4aAacPacketBounds( streamBytes, read, stop)) ;
			}
		}
		checkHr( stream->SetCurrentPosition( 0)) ;
		checkCancelled( stop) ;
	}
	ComPtr<IMFAttributes> attributes ; // 自動network入力を持たないreaderの設定
	checkHr( MFCreateAttributes( &attributes, 2)) ;
	checkHr( attributes->SetUINT32( MF_SOURCE_READER_DISABLE_DXVA, TRUE)) ;
	checkHr( attributes->SetUINT32( MF_READWRITE_DISABLE_CONVERTERS, FALSE)) ;
	ComPtr<IMFSourceReader> reader ; // worker内だけで使用するreader
	checkHr( MFCreateSourceReaderFromByteStream( stream.Get(), attributes.Get(), &reader)) ;
	checkHr( reader->SetStreamSelection( K_ALL_STREAMS, FALSE)) ;
	checkHr( reader->SetStreamSelection( K_AUDIO_STREAM, TRUE)) ;

	ComPtr<IMFMediaType> native ; // 入力codecとchannel数を確認する圧縮側の型
	checkHr( reader->GetNativeMediaType( K_AUDIO_STREAM, 0, &native)) ;
	GUID subtype{} ; // 入力streamのcodec識別子
	checkHr( native->GetGUID( MF_MT_SUBTYPE, &subtype)) ;
	const bool nativeAlac = purpose == InputPurpose::PRODUCT_M4A &&
							subtype == K_M4A_ALAC_SUBTYPE ; // full GUIDと製品用途の両方を必要とする
	// containerがALACなのにnativeが別型と解釈した場合は変換を開始しない
	if( alac && !nativeAlac) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	// native ALACを識別しても元entryの全検査なしでは変換しない
	if( nativeAlac && !alac) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	// 明示HEと異なるnative full GUIDを変換前に拒否する
	if( heaac && subtype != MFAudioFormat_AAC) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	AudioData audio ; // 本関数が所有し成功時に返すPCM
	audio.sourceBytes = input.bytes() ;
	audio.sourceRate = MFGetAttributeUINT32( native.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 0) ;
	audio.sourceChannels = MFGetAttributeUINT32( native.Get(), MF_MT_AUDIO_NUM_CHANNELS, 0) ;
	audio.aacProfile = MFGetAttributeUINT32( native.Get(), MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0) ;
	// 拡張子に対応するcodecだけを許可する用途別判定
	const bool codecAllowed =
		purpose == InputPurpose::PRODUCT_M4A ? subtype == MFAudioFormat_AAC || nativeAlac
		: purpose == InputPurpose::EVALUATION_MP3 || purpose == InputPurpose::PRODUCT_MP3 ? subtype == MFAudioFormat_MP3
		: purpose == InputPurpose::PRODUCT_WAVE											  ? subtype == MFAudioFormat_PCM
		: purpose == InputPurpose::PRODUCT_FLAC ? subtype == MFAudioFormat_FLAC
												: subtype == MFAudioFormat_PCM || subtype == MFAudioFormat_Float ;
	// 未検証codecや多channelを既定supportへ暗黙に追加しない
	if( !codecAllowed || audio.sourceChannels < 1 || audio.sourceChannels > 2 || audio.sourceRate < 8'000 ||
	   audio.sourceRate > 192'000) {
		throw OperationError( ErrorKind::UNSUPPORTED_CODEC) ;
	}
	// 宣言PCMとMFが解釈したnative属性の不一致を音声変換前に拒否する
	if( wave) {
		validateWaveNativeFormat( *wave, audio.sourceRate, audio.sourceChannels,
								 MFGetAttributeUINT32( native.Get(), MF_MT_AUDIO_BITS_PER_SAMPLE, 0),
								 MFGetAttributeUINT32( native.Get(), MF_MT_AUDIO_BLOCK_ALIGNMENT, 0),
								 MFGetAttributeUINT32( native.Get(), MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 0)) ;
	}
	// 圧縮frameと別のnative解釈や属性欠落を変換前に拒否する
	if( mp3) {
		GUID major{} ; // 圧縮streamの主media分類
		checkHr( native->GetGUID( MF_MT_MAJOR_TYPE, &major)) ;
		// 実圧縮音声以外のmediaを音声として渡さない
		if( major != MFMediaType_Audio) {
			throw OperationError( ErrorKind::DECODE_FAILED) ;
		}
		validateMp3NativeFormat( *mp3, audio.sourceRate, audio.sourceChannels,
								MFGetAttributeUINT32( native.Get(), MF_MT_AUDIO_BLOCK_ALIGNMENT, 0)) ;
	}
	// FLACの必須native属性は宣言と完全一致させ、欠落も拒否する
	if( flac) {
		GUID major{} ; // 元FLAC streamの主media分類
		checkHr( native->GetGUID( MF_MT_MAJOR_TYPE, &major)) ;
		// 非音声mediaをFLACとして変換しない
		if( major != MFMediaType_Audio) {
			throw OperationError( ErrorKind::DECODE_FAILED) ;
		}
		validateFlacNativeFormat( *flac, audio.sourceRate, audio.sourceChannels,
								 MFGetAttributeUINT32( native.Get(), MF_MT_AUDIO_BITS_PER_SAMPLE, 0),
								 MFGetAttributeUINT32( native.Get(), MF_MT_AUDIO_BLOCK_ALIGNMENT, 0)) ;
	}
	// ALACはPCM alignmentを要求せず、設定cookieの全fieldを必須にする
	if( nativeAlac) {
		GUID major{} ; // 圧縮ALACの主media分類
		checkHr( native->GetGUID( MF_MT_MAJOR_TYPE, &major)) ;
		UINT32 cookieBytes = 0 ; // 未検証blobを確保する前の宣言byte数
		checkHr( native->GetBlobSize( MF_MT_USER_DATA, &cookieBytes)) ;
		// parser成功・Audio major・正確なcookie長を必須にする
		if( !alac || major != MFMediaType_Audio || cookieBytes != 24) {
			throw OperationError( ErrorKind::DECODE_FAILED) ;
		}
		std::array<std::byte, 24> cookie{} ; // 本呼出しでだけ所有する固定cookie
		UINT32					  actualBytes = 0 ; // 同期APIが満たしたbyte数
		// BYTE*は固定配列のobject表現を同期API中だけ借用し、保存しない
		checkHr( native->GetBlob( MF_MT_USER_DATA, reinterpret_cast<BYTE *>( cookie.data()),
								static_cast<UINT32>( cookie.size()), &actualBytes)) ;
		// 宣言と実readの間に属性が変わった場合を拒否する
		if( actualBytes != cookie.size()) {
			throw OperationError( ErrorKind::DECODE_FAILED) ;
		}
		validateAlacNativeFormat( *alac, audio.sourceRate, audio.sourceChannels,
								 MFGetAttributeUINT32( native.Get(), MF_MT_AUDIO_BITS_PER_SAMPLE, 0), cookie) ;
	}
	// AACのUserDataは有界に読み、native側の明示HEも事前検査の有無へ照合する
	if( purpose == InputPurpose::PRODUCT_M4A && subtype == MFAudioFormat_AAC) {
		UINT32		  userBytes = 0 ; // 任意blob確保前の宣言長
		const HRESULT blobResult = native->GetBlobSize( MF_MT_USER_DATA, &userBytes) ; // 欠落値
		// HEでは設定欠落を拒否し、既存LCの欠落条件は変更しない
		if( heaac) {
			checkHr( blobResult) ;
		}
		// 未検証blobの容量を確保せず固定配列以下だけを読む
		if( SUCCEEDED(blobResult)) {
			// UserData上限はASC64byteとprefix12byteの和
			if( userBytes > 76) {
				throw OperationError( ErrorKind::INPUT_TOO_LARGE) ;
			}
			std::array<std::byte, 76> userData{} ; // 同期呼出しだけで所有する有界設定
			UINT32					  actualBytes = 0 ; // GetBlobが満たした長さ
			// BYTE*は固定配列のobject表現を同期API内だけで借用する
			checkHr( native->GetBlob( MF_MT_USER_DATA, reinterpret_cast<BYTE *>( userData.data()),
									static_cast<UINT32>( userData.size()), &actualBytes)) ;
			// 長さがAPI間で変わった設定を検証済みとして扱わない
			if( actualBytes != userBytes) {
				throw OperationError( ErrorKind::DECODE_FAILED) ;
			}
			const auto aot = userBytes > 12 ? std::to_integer<unsigned>( userData[12]) >> 3 : 0 ; // ASC先頭の明示AOT
			// 未検査のHEをLC経路へ迂回させない
			if( !heaac && (aot == 5 || aot == 29)) {
				throw OperationError( ErrorKind::UNSUPPORTED_CODEC) ;
			}
			// HEはAudio majorと全設定を照合し、coreと復号後属性を区別する
			if( heaac) {
				GUID major{} ; // native圧縮音声の主media分類
				checkHr( native->GetGUID( MF_MT_MAJOR_TYPE, &major)) ;
				// 非音声mediaの同じsubtypeを許可しない
				if( major != MFMediaType_Audio) {
					throw OperationError( ErrorKind::DECODE_FAILED) ;
				}
				verifyHeaacNative( heaac->config, audio.sourceRate, audio.sourceChannels,
								  std::span( userData).first( userBytes)) ;
				audio.sourceRate = heaac->config.outputRate ;
				audio.sourceChannels = heaac->config.outputChannels ;
			}
		}
	}
	configureOutput( *reader.Get()) ;
	const auto	  pcm = std::make_shared<PcmStorage>() ; // 失敗・取消し・終了時に回収する一時PCM
	HeAacTimeline heaacTimeline ; // このdecode呼出しだけの検証済みHE native時刻
	audio.source = pcm ;

	PROPVARIANT	  duration{} ; // 進捗表示専用の宣言時間。上限検証の正本にはしない
	// Media Foundationの宣言時間を取得したHRESULT。実出力量の上限とは別に扱う
	const HRESULT durationResult = reader->GetPresentationAttribute( K_MEDIA_SOURCE, MF_PD_DURATION, &duration) ;
	const double  declaredMs = heaac ? static_cast<double>( heaac->duration100ns) / 10'000.0
							   : SUCCEEDED(durationResult) && duration.vt == VT_UI8
								   ? static_cast<double>( duration.uhVal.QuadPart) / 10'000.0
								   : 0.0 ; // 未知は0で保持するmetadata時間（ms）
	PropVariantClear( &duration) ;
	reportProgress( progress, Stage::DECODING, 0) ;
	std::size_t emptyReads = 0 ; // 不正streamでsampleのない反復が無制限にならないための回数

	// end-of-streamまたは取消しまで、固定bufferのPCMを専用一時領域へ順次追記する
	for(;;) {
		checkCancelled( stop) ;
		DWORD			  flags = 0 ; // 今回のsampleに伴うreader状態
		LONGLONG		  timestamp = 0 ; // 元音声基準の100ns時刻
		ComPtr<IMFSample> sample ; // 今回のdecode結果を所有
		checkHr( reader->ReadSample( K_AUDIO_STREAM, 0, nullptr, &flags, &timestamp, &sample)) ;
		checkCancelled( stop) ;
		// 型変更後のbyteを旧PCM型のまま解釈しない
		if( (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0 ||
		   ((wave || mp3 || flac || alac || heaac) && (flags & MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED) != 0)) {
			throw OperationError( ErrorKind::UNSUPPORTED_CODEC) ;
		}
		// end-of-streamにsampleが同梱された場合も、最後のPCMを取り込む
		if( sample) {
			emptyReads = 0 ;
			ComPtr<IMFMediaBuffer> buffer ; // 複数bufferを連続したPCMへ結合した結果
			checkHr( sample->ConvertToContiguousBuffer( &buffer)) ;
			const BufferLock locked( *buffer.Get()) ; // 解放まで有効なPCM view
			// floatの途中で終わるbufferを境界外アクセス前に拒否する
			if( locked.bytes() % sizeof(float) != 0 || locked.bytes() > 16 * 1024 * 1024) {
				throw OperationError( ErrorKind::DECODE_FAILED) ;
			}
			const std::size_t count = locked.bytes() / sizeof(float) ; // 今回のfloat要素数
			// 製品は整数表現、開発clipは実sample数による上限
			const auto		  maximum = isProduct( purpose) ? K_MAXIMUM_TIMELINE_SAMPLES : K_EVALUATION_MAXIMUM_SAMPLES ;
			// 宣言時間が誤っていても、割当て・保存前に実際の出力量を制限する
			if( count > maximum - pcm->size()) {
				throw OperationError( ErrorKind::INPUT_TOO_LARGE) ;
			}
			// 実数が過大なMP3は一時領域への追記前に停止する
			if( mp3) {
				validateMp3OutputUpperBound( *mp3, pcm->size() + count) ;
			}
			// 既知総数を超えるFLACは所有PCMへ追記する前に停止する
			if( flac) {
				validateFlacOutputUpperBound( *flac, pcm->size() + count) ;
			}
			// ALACの宣言総数を超えるPCMも追記前に拒否する
			if( alac) {
				validateAlacOutputUpperBound( *alac, pcm->size() + count) ;
			}
			// HEの宣言ceilと実開始/期間を保存前に照合する
			if( heaac) {
				validateHeaacOutputUpperBound( *heaac, pcm->size() + count) ;
				LONGLONG sampleDuration = 0 ; // native sampleの100ns期間
				checkHr( sample->GetSampleDuration( &sampleDuration)) ;
				validateHeaacSampleTimeline( heaacTimeline, pcm->size(), count, timestamp, sampleDuration) ;
			}
			// 16kHzの1sampleは625単位の100ns。乗算は検証済みの表現範囲内
			const std::int64_t expected = static_cast<std::int64_t>( pcm->size() * 625) ;
			// HEは検証済み局所時刻を使い、他形式の既存100ms条件を保持する
			if( !heaac && (timestamp < expected - 1'000'000 || timestamp > expected + 1'000'000)) {
				throw OperationError( ErrorKind::INVALID_TIMELINE) ;
			}
			std::vector<float> decoded( count) ; // 最大16MiBの今回分だけを所有する正規化buffer
			// 0byte bufferではpointerの有効性を仮定してmemcpyを呼ばない
			if( count > 0) {
				std::memcpy( decoded.data(), locked.data(), locked.bytes()) ;
			}
			// engineへ渡す前に非有限PCMを除外し、decoderの微小なovershootだけを正規化する
			for( auto &value : decoded) {
				// NaN/Infを模型へ伝播させず入力失敗とする
				if( !std::isfinite( value)) {
					throw OperationError( ErrorKind::DECODE_FAILED) ;
				}
				value = std::clamp( value, -1.0F, 1.0F) ;
			}
			pcm->append( decoded) ;
			const double actualMs = static_cast<double>( pcm->size()) / 16 ; // 実際のdecode終端ms
			// 宣言時間が使えない場合は段階表示だけを更新する
			const int	 percent = declaredMs > 0 ? static_cast<int>( std::min( 99.0, actualMs * 100 / declaredMs))
												  : -1 ; // 実sample数から得る段階進捗
			reportProgress( progress, Stage::DECODING, percent) ;
		} else {
			++emptyReads ;
			// 壊れたstreamの無進行反復に上限を設ける
			if( emptyReads > 1024) {
				throw OperationError( ErrorKind::DECODE_FAILED) ;
			}
		}
		// readerが終端を確定してからdecode完了を通知する
		if( (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
			break ;
		}
	}
	// 空streamを認識成功・無音検出と混同しない
	if( audio.sampleCount() == 0) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	// 終端が早すぎる/長すぎるWAVを部分成功としてengineへ渡さない
	if( wave) {
		validateWaveOutputSamples( *wave, audio.sampleCount()) ;
	}
	// 圧縮時のnominalと実PCMは同一とせず、固定した終端条件だけへ照合する
	if( mp3) {
		validateMp3OutputSamples( *mp3, audio.sampleCount()) ;
	}
	// MFの早い正常終了を成功へまとめず、既知総数との不足も照合する
	if( flac) {
		validateFlacOutputSamples( *flac, audio.sampleCount()) ;
	}
	// ALACの早い正常終了を宣言総数へ照合し、不足を部分成功にしない
	if( alac) {
		validateAlacOutputSamples( *alac, audio.sampleCount()) ;
	}
	// HEの早い正常終了もfloor未満なら部分成功として返さない
	if( heaac) {
		validateHeaacOutputSamples( *heaac, audio.sampleCount()) ;
	}
	checkCancelled( stop) ;
	reportProgress( progress, Stage::DECODING, 100) ;
	return audio ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief local M4A/AAC・ALAC、PCM16 WAV、Layer III MP3、native FLACを16kHz monoへ変換します
 * @param[in] path 通常local file。WAVはmono/stereo・8～192kHz。呼出し中だけ参照します
 * @param[in] stop 協調的な取消しtoken
 * @param[in] progress worker上で受け取る段階通知。空を許可します
 * @return 元入力属性と部分読取り可能な所有PCM。原fileを変更しません
 * @throws OperationError 不正入力、上限/資源不足、未対応codec、decode/timeline異常、取消し
 * @note COM MTAを使用できるworker専用のblocking処理です。固定の録音時間制限は設けません
 * 途中失敗/取消しではPCMを返さず一時領域を解放します。UI threadで呼ばないこと
 * MP3はMPEG-1/2・mono/stereo・規定6rateとID3外枠/frame範囲を検証します
 * FLACは16/24bit・mono/stereo・8～192kHz・既知の正の総数を検証します
 * FLAC出力は16kHzで総数一致、他rateで理想数から上下2sampleまでです
 * ALACは単一track/version0・16/24bit・mono/stereo・8～48kHzと全packet範囲を検証します
 * ALAC出力は16kHzで総数一致、他rateで理想ceilから上下2sampleまでです
 * 圧縮payload/CRCの完全性や許容差内のPCM欠落を保証しません
 * @see koefude::decodeInputAudio
 */
//------------------------------------------------------------------------------
AudioData decodeInputAudio( const std::filesystem::path &path, const std::stop_token stop,
						   const ProgressCallback &progress)
{
	checkCancelled( stop) ;
	const auto extension = lowerExtension( path) ; // fileを開かず用途だけを選ぶ所有文字列
	// 従来の製品M4Aは同じ実装・codec/資源条件へ渡す
	if( extension == L".m4a") {
		return decodeAudio( path, stop, progress, InputPurpose::PRODUCT_M4A) ;
	}
	// WAVだけはRIFF/PCM16の事前検証付き製品用途へ渡す
	if( extension == L".wav") {
		return decodeAudio( path, stop, progress, InputPurpose::PRODUCT_WAVE) ;
	}
	// MP3はMPEG-1/2 Layer IIIの全frame検査付き製品用途へ渡す
	if( extension == L".mp3") {
		return decodeAudio( path, stop, progress, InputPurpose::PRODUCT_MP3) ;
	}
	// native FLACはmetadataと実出力数の照合付き製品用途へ渡す
	if( extension == L".flac") {
		return decodeAudio( path, stop, progress, InputPurpose::PRODUCT_FLAC) ;
	}
	throw OperationError( ErrorKind::INVALID_INPUT) ;
}

//------------------------------------------------------------------------------
/**
 * @brief local M4Aを16kHz mono float PCMへ変換します
 * @param[in] path 読取り対象。local通常fileのみで、呼出し中は参照可能なこと
 * @param[in] stop 協調的な取消しtoken
 * @param[in] progress worker上で受け取る段階通知。空を許可
 * @return 所有PCMと入力属性。原fileは変更しません
 * @throws OperationError 不正入力、上限超過、未対応codec、decode失敗、取消し
 * @note blocking処理です。COM MTAを使用できるworkerから呼び出します
 * AACの従来条件と、上記ALACの検証/出力数条件を使います
 * @see koefude::decodeM4a
 */
//------------------------------------------------------------------------------
AudioData decodeM4a( const std::filesystem::path &path, const std::stop_token stop, const ProgressCallback &progress)
{
	return decodeAudio( path, stop, progress, InputPurpose::PRODUCT_M4A) ;
}

//------------------------------------------------------------------------------
/**
 * @brief localのMP3またはPCM WAVEを評価用の16kHz mono float PCMへ変換します
 * @param[in] path 呼出し中に参照可能な通常local file。拡張子と実codecを検証します
 * @param[in] stop workerからの協調的な取消しtoken
 * @return 原file属性と所有PCM。原fileは変更しません
 * @throws OperationError 不正入力、60秒/64MiB超過、未対応codec、decode失敗、取消し
 * @note COM MTAを使用できるworker向けのblocking APIです。mono/stereoのみを許可し、
 * 失敗時は途中PCMを返さず専用一時領域を解放します。製品入力から使用しません
 * @see koefude::decodeEvaluationClip
 */
//------------------------------------------------------------------------------
AudioData decodeEvaluationClip( const std::filesystem::path &path, const std::stop_token stop)
{
	// 明示した評価clipをMP3経路またはWAVE経路へ送る用途指定
	const auto purpose = lowerExtension( path) == L".mp3" ? InputPurpose::EVALUATION_MP3 : InputPurpose::EVALUATION_WAVE ;
	// MP3以外はWAVE条件で検証し、第三の拡張子を暗黙に許可しない
	return decodeAudio( path, stop, {}, purpose) ;
}

} // namespace koefude
