//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file model_download_io.h
 * @brief モデルcacheの排他・有界一時file・原子的確定を宣言します
 * @version 1.0.0
 * @date 2026/09/13
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/13 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/13 既存モデルを保持する取得I/Oを追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1280の用途限定導入と実byte進捗を接続
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1283の専用Direct取得の用途を明記
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415のoperator括弧と文書lambdaの書式を補修
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <stop_token>
#include <string_view>

#include "inference/model_download.h"
#include "platform/win_helpers.h"

namespace koefude {

/** @brief cacheの準備排他と、専用一時領域の既存残骸回収を所有します */
class ModelCacheTransaction final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief local cacheを作り、同時取得を拒否して空き容量を照合します
	 * @param[in] root 信頼済みのlocal絶対directory
	 * @param[in] requiredBytes 余裕を含めた追加空き容量（byte）
	 * @throws OperationError 属性・容量・排他・保存先の失敗
	 * @note 回収走査は4096件まで。自作prefix/UUIDの通常fileだけを削除し、他は保持します
	 */
	//------------------------------------------------------------------------------
	ModelCacheTransaction( const std::filesystem::path &root, std::uint64_t requiredBytes) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief cache排他を解放します
	 */
	//------------------------------------------------------------------------------
	~ModelCacheTransaction() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 排他所有権の複製を禁止します
	 * @note 名前のない第1引数（const ModelCacheTransaction &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	ModelCacheTransaction( const ModelCacheTransaction &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 排他所有権の代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const ModelCacheTransaction &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	ModelCacheTransaction					  &operator=( const ModelCacheTransaction &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 一時領域を返します
	 * @return このtransaction中有効な借用path
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const std::filesystem::path &stagingRoot() const noexcept ;

	private:
	/// このcacheだけの一時領域
	std::filesystem::path stagingRoot_ ;
	/// 他起動による同時取得を拒否する所有handle
	UniqueHandle		  lock_ ;
} ;

/** @brief UUID一時fileを単独所有し、hash確認後だけrenameできる資源です */
class ModelStagingFile final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief cacheの専用領域へ未使用の一時fileを作ります
	 * @param[in] root 排他transactionの一時directory
	 * @param[in] limitBytes 追記可能な最大byte数、1～8GiB
	 * @throws OperationError 作成・上限の失敗
	 */
	//------------------------------------------------------------------------------
	ModelStagingFile( const std::filesystem::path &root, std::uint64_t limitBytes) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 用途13のregistry容量だけを許す導入・直接取得用fileを作ります
	 * @param[in] root transaction所有の一時directory
	 * @return 単独所有の一時file
	 * @throws OperationError registry・作成・属性の拒否
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] static ModelStagingFile forWholeTextImport( const std::filesystem::path &root) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 未確定fileをhandleで削除し、全資源を解放します
	 */
	//------------------------------------------------------------------------------
	~ModelStagingFile() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有fileの複製を禁止します
	 * @note 名前のない第1引数（const ModelStagingFile &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	ModelStagingFile( const ModelStagingFile &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有fileの代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const ModelStagingFile &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	ModelStagingFile	&operator=( const ModelStagingFile &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 上限を超えない全byteを追記します
	 * @param[in] bytes 呼出し中のborrow、最大64KiB
	 * @throws OperationError 上限・書込み失敗。失敗後は確定せず破棄します
	 */
	//------------------------------------------------------------------------------
	void				 append( std::span<const std::byte> bytes) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有handleから固定size/hashを確認します
	 * @param[in] bytes 正確な期待byte数
	 * @param[in] sha256 小文字64桁の期待hash
	 * @param[in] stop 読取り中の取消し
	 * @param[in] progress 空可の同期通知。検証済みの実読取りbyte数を渡します
	 * @throws OperationError 内容不一致、I/O、取消し
	 */
	//------------------------------------------------------------------------------
	void				 verify( std::uint64_t bytes, std::string_view sha256, std::stop_token stop,
								const std::function<void( std::uint64_t)> &progress = {}) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じvolumeの最終pathへhandleを保ったまま原子的にrenameします
	 * @param[in] path registryの検査済み保存先。検証済みの親directoryを作成します
	 * @throws OperationError 未検証、属性、rename、既存fileの競合
	 */
	//------------------------------------------------------------------------------
	void				 commit( const std::filesystem::path &path) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 読取り位置を先頭へ戻します
	 * @throws OperationError seek失敗
	 */
	//------------------------------------------------------------------------------
	void				 rewind() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 標準入力へ複製するhandleを返します
	 * @return object寿命中だけ有効なborrow
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] HANDLE handle() const noexcept ;

	private:
	/** @brief 専用factoryだけが使う容量検証済みtagです */
	struct WholeTextTag {} ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定registryまたは通常上限で確認済みの容量を使います
	 * @param[in] root 一時directory
	 * @param[in] limitBytes 固定上限byte
	 * @param[in] tag 外部から構築できない用途限定tag
	 */
	//------------------------------------------------------------------------------
	ModelStagingFile( const std::filesystem::path &root, std::uint64_t limitBytes, WholeTextTag tag) ;
	/// 確定先の逸脱を防ぐ所有cache root
	std::filesystem::path cacheRoot_ ;
	/// write/deleteを他processへ共有しない所有file
	UniqueHandle		  file_ ;
	/// この一時fileの最大byte数
	std::uint64_t		  limitBytes_ = 0 ;
	/// 成功した書込み総量
	std::uint64_t		  size_ = 0 ;
	/// 最後の追記後に期待値へ一致した状態
	bool				  verified_ = false ;
	/// 最終fileへrename済みなら削除しない
	bool				  committed_ = false ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 検証済みarchiveから固定memberだけを有界sinkへ取り出します
 * @param[in,out] archive 読取り専用に使う検証済み一時file。実行中の追記は禁止
 * @param[in] member registryの安全な相対member名
 * @param[in] sink 同じworkerで呼ぶ有界出力先。空は禁止
 * @param[in] stop 協調取消し。所有する子processを終了して回収します
 * @throws OperationError OSのtar不在、process/pipe/展開失敗、上限、取消し
 */
//------------------------------------------------------------------------------
void extractModelArchive( ModelStagingFile &archive, std::string_view member, const ModelDownloadSink &sink,
						 std::stop_token stop) ;

//------------------------------------------------------------------------------
/**
 * @brief 検査済みlocal directoryを一段ずつ作成します
 * @param[in] root local絶対directory
 * @throws OperationError 属性拒否、作成失敗
 */
//------------------------------------------------------------------------------
void createModelDirectory( const std::filesystem::path &root) ;

} // namespace koefude
