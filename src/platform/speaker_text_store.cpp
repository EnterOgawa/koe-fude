//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file speaker_text_store.cpp
 * @brief 破損投影を原文から隔離し、既知操作番号の再利用を防ぎます
 * @version 1.0.0
 * @date 2026/11/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372専用sidecarのstrict保存/復元を追加
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
#include "platform/speaker_text_store.h"
#include "core/api_text_correction.h"
#include "core/speaker_text_json.h"
#include "core/speaker_text_record.h"
#include <optional>
#include <span>
#include <algorithm>
#include <exception>
#include <json.hpp>
#include <limits>
#include <set>
#include <string>
#include <utility>
namespace koefude {
namespace {
using Json = nlohmann::json ; // 固定版JSON
//------------------------------------------------------------------------------
/**
 * @brief 保存形式の不正を固定分類へ変換します
 * @param[in] valid 成立条件
 */
//------------------------------------------------------------------------------
void require( const bool valid)
{
	// 不正派生値を原文全体の破損にせず専用分類で拒否する
	if( !valid) {
		throw HistoryError( HistoryErrorKind::CORRUPT) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 最新試行の小metadataを符号化します
 * @param[in] a optional試行
 * @return nullまたは配列
 */
//------------------------------------------------------------------------------
Json attemptJson( const std::optional<SpeakerTextAttempt> &a)
{
	// 最新試行がなければ架空のPendingを保存しない
	if( !a) {
		return nullptr ;
	}
	const auto &t = a->target ; // 本文を含まない小CAS
	return {a->operationId,
			{t.historyId, t.generationOperation, t.update, t.sourceSha, t.planSha, t.completePrefix},
			a->state,
			a->error,
			a->code} ;
}
//------------------------------------------------------------------------------
/**
 * @brief 保存最新試行を復元します
 * @param[in] j 固定配列
 * @return optional試行
 */
//------------------------------------------------------------------------------
std::optional<SpeakerTextAttempt> readAttempt( const Json &j)
{
	// 未記録の試行をUnknownのまま復元する
	if( j.is_null()) {
		return {} ;
	}
	require( j.is_array() && j.size() == 5 && j.at( 1).is_array() && j.at( 1).size() == 6) ;
	const auto		  &t = j.at( 1) ; // 固定target配列
	SpeakerTextAttempt a ; // 原文や名前を保存しない
	a.operationId = speaker_text_json::count( j.at( 0), UINT64_MAX - 1) ;
	a.target = {speaker_text_json::count( t.at( 0)),
				speaker_text_json::count( t.at( 1)),
				speaker_text_json::count( t.at( 2)),
				speaker_text_json::digest( t.at( 3)),
				speaker_text_json::digest( t.at( 4)),
				static_cast<std::uint32_t>( speaker_text_json::count( t.at( 5), K_MAXIMUM_SEGMENTS))} ;
	a.state = static_cast<SpeakerTextState>( speaker_text_json::count( j.at( 2), 8)) ;
	a.error =
		static_cast<ErrorKind>( speaker_text_json::count( j.at( 3), static_cast<unsigned>( ErrorKind::TEMPORARY_STORAGE))) ;
	a.code = static_cast<std::uint32_t>( speaker_text_json::count( j.at( 4), UINT32_MAX)) ;
	require( a.operationId && a.operationId != UINT64_MAX && a.target.historyId && static_cast<unsigned>( a.state) <= 8 &&
			static_cast<unsigned>( a.error) <= static_cast<unsigned>( ErrorKind::TEMPORARY_STORAGE)) ;
	// crash等で残った実行中状態を起動時に再開しない
	if( a.state <= SpeakerTextState::SAVING) {
		a.state = SpeakerTextState::INTERRUPTED ;
	}
	return a ;
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 最大32行とglobal floorをSHA付きで符号化します
 * @param[in] snapshot 同writerの正本
 * @return 最大64MiB
 * @throws HistoryError 不正/上限
 * @see koefude::encodeSpeakerTextStore
 */
//------------------------------------------------------------------------------
std::vector<std::uint8_t> encodeSpeakerTextStore( const HistorySnapshot &snapshot)
{
	try {
		require( !snapshot.speakerTextCorrupt && snapshot.nextOperationId) ;
		Json records = Json::array() ; // source本文を複製しない派生recordだけ
		// 通常履歴数に関わらず実投影recordだけを選ぶ
		for( const auto &entry : snapshot.entries) {
			// 派生値のないlocal履歴はsidecar件数へ数えない
			if( !entry.speakerText.latest && !entry.speakerText.adopted) {
				continue ;
			}
			require( records.size() < 32) ; // 通常local履歴数ではなく派生record数だけを制限する
			Json adopted = nullptr ; // 未採用なら最新試行だけ
			// 採用がある場合だけ全bindingを検証して符号化する
			if( entry.speakerText.adopted) {
				require( entry.result && entry.apiAudio) ;
				validateSpeakerTextRecord( *entry.speakerText.adopted, *entry.result, *entry.apiAudio, false) ;
				adopted = Json::parse( encodeSpeakerTextRecord( *entry.speakerText.adopted)) ;
			}
			records.push_back( {entry.id, attemptJson( entry.speakerText.latest), std::move( adopted)}) ;
		}
		const auto text = Json( {{"domain", "koefude.history.speaker_text"},
								{"version", 1},
								{"floor", snapshot.nextOperationId},
								{"records", records}})
							  .dump() ; // 固定root
		require( text.size() + 32 <= K_SPEAKER_TEXT_STORE_BYTES) ;
		std::vector<std::uint8_t> bytes( text.begin(), text.end()) ; // SHAを末尾へ追加
		// 保存した全byteを束縛するSHA-256
		const auto				  sha = apiTextDigest( text) ;
		bytes.insert( bytes.end(), sha.begin(), sha.end()) ;
		return bytes ;
	} catch( const HistoryError &) {
		throw ;
	} catch( const std::exception &) {
		throw HistoryError( HistoryErrorKind::CORRUPT) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief rawを変えず、同target採用と最大opだけを復元します
 * @param[in] bytes SHA付き有界sidecar
 * @param[in,out] snapshot V8検証済み正本
 * @throws HistoryError 不正/未知版/上限。原文は保持し、部分採用を行いません
 * @see koefude::decodeSpeakerTextStore
 */
//------------------------------------------------------------------------------
void decodeSpeakerTextStore( const std::span<const std::uint8_t> bytes, HistorySnapshot &snapshot)
{
	try {
		require( bytes.size() >= 34 && bytes.size() <= K_SPEAKER_TEXT_STORE_BYTES) ;
		const std::string_view text( reinterpret_cast<const char *>( bytes.data()),
									bytes.size() - 32) ; // 所有byte寿命内のUTF-8借用
		// 保存した全byteを束縛するSHA-256
		const auto			   sha = apiTextDigest( text) ;
		require( std::equal( sha.begin(), sha.end(), bytes.end() - 32)) ;
		const auto j = speaker_text_json::parse( text) ; // byte/深さ/重複key/数値型を先行検証
		require( j.is_object() && j.size() == 4 && j.at( "domain") == "koefude.history.speaker_text" &&
				j.at( "version") == 1 && j.at( "records").is_array() && j.at( "records").size() <= 32) ;
		HistorySnapshot candidate = snapshot ; // 部分採用を公開しない
		// 符号/型を検証済みの次操作番号
		const auto		floor = speaker_text_json::count( j.at( "floor")) ;
		require( floor > 0) ;
		candidate.nextOperationId = std::max( candidate.nextOperationId, floor) ;
		std::set<std::uint64_t> ids ; // 二重recordを拒否
		// 最大32の派生recordを重複なく一度ずつ復元する
		for( const auto &item : j.at( "records")) {
			require( item.is_array() && item.size() == 3) ;
			// 同じownerへ束縛する安定識別値
			const auto id = speaker_text_json::count( item.at( 0)) ;
			require( id && ids.insert( id).second) ;
			const auto latest = readAttempt( item.at( 1)) ; // 先行削除されたrecordでもfloorを失わない
			// 削除済み行でも最新試行opのfloorを失わない
			if( latest) {
				candidate.nextOperationId = std::max( candidate.nextOperationId, latest->operationId + 1) ;
			}
			// 採用opもglobal floorへ数え、番号再利用を防ぐ
			if( !item.at( 2).is_null()) {
				// 受付済みexportの操作番号
				const auto op = speaker_text_json::count( item.at( 2).at( "op"), UINT64_MAX - 1) ;
				require( op) ;
				candidate.nextOperationId = std::max( candidate.nextOperationId, op + 1) ;
			}
			auto entry = std::find_if( candidate.entries.begin(), candidate.entries.end(),
				//------------------------------------------------------------------------------
				/**
				 * @brief 保存安定IDで対象を見付けます
				 * @param[in] e 一履歴
				 * @return 同じID
				 */
				//------------------------------------------------------------------------------
				[id]( const auto &e)
				{
					return e.id == id ;
				}) ; // 元履歴数の契約は変更しない
			// 削除済み/原文のない行へ投影を復活させない
			if( entry == candidate.entries.end() || !entry->result || !entry->apiAudio) {
				continue ;
			}
			const auto target = speakerTextTarget( *entry->apiAudio) ; // stale派生値は使わない
			// source/plan/prefix更新後の旧最新試行を採用しない
			if( !target || (latest && latest->target != *target)) {
				continue ;
			}
			// 前採用と最新試行を分離した派生状態
			SpeakerTextRecord record ;
			record.latest = latest ; // 現sourceだけの派生状態
			// 採用がある場合だけ固定recordを検証する
			if( !item.at( 2).is_null()) {
				const auto adoptedTarget = item.at( 2).at( "target") ; // source更新後は旧投影を復活させない
				// 旧採用のsource/plan/prefixを現在行へ読み替えない
				if( adoptedTarget.at( 3) != Json( target->sourceSha) || adoptedTarget.at( 4) != Json( target->planSha) ||
				   adoptedTarget.at( 5) != Json( target->completePrefix)) {
					continue ;
				}
				record.adopted = decodeSpeakerTextRecord( item.at( 2).dump(), *entry->result, *entry->apiAudio) ;
			}
			entry->speakerText = std::move( record) ;
		}
		snapshot = std::move( candidate) ;
	} catch( const HistoryError &) {
		throw ;
	} catch( const std::exception &) {
		throw HistoryError( HistoryErrorKind::CORRUPT) ;
	}
}
} // namespace koefude
