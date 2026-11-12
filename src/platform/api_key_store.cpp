//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_key_store.cpp
 * @brief 固定targetの現在ユーザー資格情報とsession秘密を管理します
 * @version 1.0.0
 * @date 2026/11/08
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/08 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/08 opt-in資格情報保管を追加
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1349の一回適用と全単価表示へ更新
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
#include "core/api_text_correction.h"
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <windows.h>

// Credential Manager宣言はWindows基本型の後にincludeします
#include <wincred.h>

namespace koefude {
namespace {
constexpr wchar_t K_TARGET[] = L"koefude/openai/api-key/v1" ; // 列挙や他app認証を使わない固定target
/** @brief 同じjob workerだけで使用する資格情報保管です */
class ApiKeyStore final : public IApiKeyStore {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief workerのsessionキーを設定/解除し、明示した保存方式を固定資格情報targetへ反映します
	 *
	 * @param[in] key 検証済み秘密を共有所有するlease。非nullなら成功後もstoreが共有所有し、
	 * acquireで同じ秘密を使います。nullptrはsessionと固定targetの削除要求です
	 * 借用文字列を保存せず、他ownerが保持する秘密まで消去する契約ではありません
	 * @param[in] persist 非nullキーでtrueなら、現在ユーザーの固定targetへ資格情報を書込みます
	 * falseはsessionだけに保持し、既存の同target資格情報も削除します
	 * keyがnullptrなら本値によらず削除します。他targetは操作しません
	 * @throws OperationError 固定targetへの書込み/削除に失敗した場合はSaveFailedと
	 * 元Win32 codeを返します。削除時のERROR_NOT_FOUNDは成功扱いです
	 * @pre 同じjob workerに呼出しを直列化し、acquire/persistedを含む同一storeへの並行操作をしません
	 * @post 永続操作の成功後だけsession/読取済み状態/保存有無を更新します
	 * 例外時は従前のsessionとstore内状態を保持します。正常復帰時は指定した保存/削除を
	 * Windowsが受理済みです。OS資格情報とsessionを跨ぐtransactionや、他processによる
	 * 後続変更・OS障害に対する永続先の独自rollback/完全耐久性までは保証しません
	 * @note 同じOpenAIキーを使う文章改善/音声APIの保管境界です。キー登録だけでAPIを送信せず、
	 * 秘密を履歴・平文config・logへ渡しません
	 */
	//------------------------------------------------------------------------------
	void set( std::shared_ptr<const ApiKeySecret> key, bool persist) override
	{
		// 永続化を選んだ一回の操作だけで書き込みます
		if( key && persist) {
			CREDENTIALW credential{} ; // Windowsが同期借用する固定資格情報
			credential.Type = CRED_TYPE_GENERIC ;
			credential.TargetName = const_cast<wchar_t *>( K_TARGET) ;
			credential.CredentialBlobSize = static_cast<DWORD>( key->borrow().size()) ;
			credential.CredentialBlob = reinterpret_cast<LPBYTE>( const_cast<char *>( key->borrow().data())) ;
			credential.Persist = CRED_PERSIST_LOCAL_MACHINE ;
			// 失敗時はsession値と旧資格情報を変更しません
			if( !CredWriteW( &credential, 0)) {
				throw OperationError( ErrorKind::SAVE_FAILED, GetLastError()) ;
			}
		} else {
			// sessionのみへ変更/明示削除はこのtargetだけを削除します
			if( !CredDeleteW( K_TARGET, CRED_TYPE_GENERIC, 0) && GetLastError() != ERROR_NOT_FOUND) {
				throw OperationError( ErrorKind::SAVE_FAILED, GetLastError()) ;
			}
		}
		session_ = std::move( key) ;
		checked_ = true ;
		persisted_ = session_ && persist ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief storeが最後に確認したキーの永続保存状態を照会します
	 * @return Windows実装は成功したset/acquireで固定した保存状態を返します。初回acquire前はfalse
	 *         このinterfaceの既定実装もfalseで、永続保存を報告しません
	 * @pre 同じworkerでset/acquireを含む操作を直列化し、破棄と並行しないこと
	 * @note 呼出すたびに資格情報を再照会しません。他processによる後続変更の実在確認ではありません
	 */
	//------------------------------------------------------------------------------
	bool persisted() const noexcept override
	{
		return persisted_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定targetから初回だけ読み、以後はsessionにcacheした秘密の共有ownerを取得します
	 * @return 設定済みなら不変秘密の共有owner、未設定ならnullptr。後のsetでstoreが切り替わっても、
	 *         取得済みownerはその最後の共有参照を解放するまで生存します
	 * @throws OperationError 読取失敗はSaveFailedと元Win32 code、不正な保管size/秘密はInvalidInput
	 * @throws std::bad_alloc 秘密stringまたは共有ownerの確保に失敗した場合
	 * @pre 同じworkerでset/persistedを含む操作を直列化し、破棄と並行しないこと
	 * @post Windows実装では読取成功または未設定だけをcacheし、読取例外を未設定として固定しません
	 * @note 取得はAPI通信を行わず、未設定と読取異常を区別します
	 */
	//------------------------------------------------------------------------------
	std::shared_ptr<const ApiKeySecret> acquire() override
	{
		// 同じ起動内の二度目は資格情報I/Oを繰り返しません
		if( checked_) {
			return session_ ;
		}
		PCREDENTIALW credential = nullptr ; // Windows所有値はCredFreeまでだけ有効
		// 既知target一つだけを読み、キー未設定を正常に区別します
		if( !CredReadW( K_TARGET, CRED_TYPE_GENERIC, 0, &credential)) {
			// 読取異常を未設定へ黙って縮小しません
			if( GetLastError() != ERROR_NOT_FOUND) {
				throw OperationError( ErrorKind::SAVE_FAILED, GetLastError()) ;
			}
			checked_ = true ;
			return {} ;
		}
		try {
			// 外部保管のサイズも秘密所有型へ渡す前に制限します
			if( !credential || credential->CredentialBlobSize == 0 || credential->CredentialBlobSize > 2048) {
				throw OperationError( ErrorKind::INVALID_INPUT) ;
			}
			session_ = std::make_shared<ApiKeySecret>( std::string(
				reinterpret_cast<const char *>( credential->CredentialBlob), credential->CredentialBlobSize)) ;
		} catch( ...) {
			// 失敗時もWindowsから取得した秘密bufferを消します
			if( credential) {
				SecureZeroMemory( credential->CredentialBlob, credential->CredentialBlobSize) ;
				CredFree( credential) ;
			}
			throw ;
		}
		SecureZeroMemory( credential->CredentialBlob, credential->CredentialBlobSize) ;
		CredFree( credential) ;
		checked_ = true ;
		persisted_ = true ;
		return session_ ;
	}

	private:
	/// workerだけが所有する秘密lease
	std::shared_ptr<const ApiKeySecret> session_ ;
	/// 固定targetへの保存状態だけ、UIに秘密は返しません
	bool								persisted_ = false ;
	/// 初回の固定target読取済み
	bool								checked_ = false ;
} ;
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 固定Windows保管を作ります
 * @return worker専用owner
 * @see koefude::makeApiKeyStore
 */
//------------------------------------------------------------------------------
std::unique_ptr<IApiKeyStore> makeApiKeyStore()
{
	return std::make_unique<ApiKeyStore>() ;
}
} // namespace koefude
