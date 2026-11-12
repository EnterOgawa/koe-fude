//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file speaker_text_json.h
 * @brief 投影sidecarの深さ、重複key、整数型を外部境界で検証します
 * @version 1.0.0
 * @date 2026/11/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372のbounded strict JSON境界を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/api_text_correction.h"
#include "core/types.h"
#include <cstddef>
#include <cstdint>
#include <json.hpp>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <vector>
namespace koefude::speaker_text_json {
//------------------------------------------------------------------------------
/**
 * @brief byte上限適用後の浅いJSONだけを読みます
 * @param[in] bytes 有界UTF-8
 * @return 重複なしJSON
 * @throws OperationError 不正/深い入力
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline nlohmann::json parse( const std::string_view bytes)
{
	std::vector<std::set<std::string>> keys ; // 最大32階層のobject key集合
	return nlohmann::json::parse( bytes,
		//------------------------------------------------------------------------------
		/**
		 * @brief 確保前に浅い構造と重複を検証します
		 * @param[in] depth 入力深さ
		 * @param[in] event parse段階
		 * @param[in,out] value 当event値
		 * @return 受理時true
		 */
		//------------------------------------------------------------------------------
		[&]( const int depth, const nlohmann::json::parse_event_t event, nlohmann::json &value)
		{
			// 深いarray/objectを再帰stackへ入れる前に拒否する
			if( depth > 32) {
				throw OperationError( ErrorKind::INVALID_INPUT) ;
			}
			// object開始時に当階層専用のkey集合を作る
			if( event == nlohmann::json::parse_event_t::object_start) {
				keys.emplace_back() ;
			}
			// 同じobjectの重複keyを後勝ちの上書きにしない
			if( event == nlohmann::json::parse_event_t::key &&
			   (!keys.back().insert( value.get<std::string>()).second)) {
				throw OperationError( ErrorKind::INVALID_INPUT) ;
			}
			// object終端で当階層の集合だけを解放する
			if( event == nlohmann::json::parse_event_t::object_end) {
				keys.pop_back() ;
			}
			// 保存契約にfloatはなく、整数同値の1.0も認めない
			if( event == nlohmann::json::parse_event_t::value && value.is_number_float()) {
				throw OperationError( ErrorKind::INVALID_INPUT) ;
			}
			return true ;
		}) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 型と符号と上限をget前に照合します
 * @param[in] value 外部JSON整数
 * @param[in] maximum 固定上限
 * @return 検証済みunsigned
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline std::uint64_t count( const nlohmann::json &value,
										 const std::uint64_t   maximum = std::numeric_limits<std::uint64_t>::max())
{
	// 符号変換や整数同値floatをget前に拒否する
	if( !value.is_number_unsigned() || value.get<std::uint64_t>() > maximum) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	return value.get<std::uint64_t>() ;
}
//------------------------------------------------------------------------------
/**
 * @brief SHAの各byteも符号/型/上限を変換前に検証します
 * @param[in] value 外部の32整数配列
 * @return 正確なSHA-256
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline ApiTextDigest digest( const nlohmann::json &value)
{
	// 不足/余剰byteを黙示初期化や切捨てで補わない
	if( !value.is_array() || value.size() != 32) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	ApiTextDigest out{} ; // 全byteを検証する小さい固定出力
	// 各byteを32件だけ検証し、負値や256以上の剰余変換を防ぐ
	for( std::size_t at = 0; at < out.size(); ++at) {
		out[at] = static_cast<std::uint8_t>( count( value.at( at), 255)) ;
	}
	return out ;
}
} // namespace koefude::speaker_text_json
