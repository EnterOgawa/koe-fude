//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file spectral_clustering.cpp
 * @brief NME-SCの式に基づき人数と近傍graphを自動選択します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 論文の数式から有界なC++版を実装
 * 0.1.0 Shunji Ogawa 2026/09/09 発言量の偏りと声の変動による人数誤りを補正
 * 0.1.0 Shunji Ogawa 2026/09/10 短い別方向を等間隔の標本化で落とす問題を修正
 * 0.1.0 Shunji Ogawa 2026/09/23 人数投票を保持したgraph選択の診断候補を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 callbackの契約を明記
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実使用の標準headerを直接include
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 * @note 著者のsourceを複写せず、研究方式との差をADR 0009/0010に記録しています
 */
//------------------------------------------------------------------------------
#include "inference/spectral_clustering.h"

#include <span>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <set>
#include <utility>

#include <Eigen/Core>
#include <Eigen/Eigenvalues>

#include "core/types.h"

namespace koefude {
namespace {

using Matrix = Eigen::MatrixXd ; // 固有値計算の丸め誤差を抑える倍精度行列
using Index = Eigen::Index ; // Eigenの有界な行列index
constexpr std::size_t K_MAXIMUM_GRAPH_ROWS = 384 ; // O(N^3)固有値計算の入力上限
constexpr std::size_t K_MAXIMUM_EMBEDDING_ROWS = 4096 ; // 1fileで保持する声の特徴数の上限
constexpr std::size_t K_MAXIMUM_EMBEDDING_DIMENSIONS = 1024 ; // schema異常による過大確保を防ぐ
constexpr double	  K_EPSILON = 1e-10 ; // ゼロ固有値・丸め誤差の数値保護

/** @brief 一つの近傍graphで得た、声の特徴を含まない人数仮説です */
struct GraphHypothesis {
	/// 各行の近傍数
	int	   neighbors = 0 ;
	/// 最大eigengapに対応する人数
	int	   speakers = 0 ;
	/// 無向graphの連結成分数
	int	   components = 0 ;
	/// NME目的値。小さい候補を優先する
	double ratio = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 外部特徴を検証して単位長の所有行列へ変換します
 * @param[in] embeddings 同じ次元の有限な非ゼロvector列
 * @param[in] stop 行ごとの取消し状態
 * @return 各行が単位長の行列
 */
//------------------------------------------------------------------------------
[[nodiscard]] Matrix normalizedFeatures( const std::span<const std::vector<float>> embeddings,
										const std::stop_token					  stop)
{
	const std::size_t dimensions = embeddings.front().size() ; // 検証する共通次元
	// 次元不明や過大行列を確保しない
	if( dimensions == 0 || dimensions > K_MAXIMUM_EMBEDDING_DIMENSIONS) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// 検証後の単位長特徴を所有する行列
	Matrix matrix( static_cast<Index>( embeddings.size()), static_cast<Index>( dimensions)) ;
	// 各行のschema・有限性・ノルムを検証してからcosine計算に使う
	for( std::size_t row = 0; row < embeddings.size(); ++row) {
		checkCancelled( stop) ;
		// 異なるmodel次元を一つのgraphへ混ぜない
		if( embeddings[row].size() != dimensions) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
		// 型変換後も全座標を明示的に検証する
		for( std::size_t column = 0; column < dimensions; ++column) {
			matrix( static_cast<Index>( row), static_cast<Index>( column)) = embeddings[row][column] ;
		}
		const double norm = matrix.row( static_cast<Index>( row)).norm() ; // 単位長への換算分母
		// NaN/Infや無音由来のゼロvectorを別話者として扱わない
		if( !std::isfinite( norm) || norm <= K_EPSILON) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
		matrix.row( static_cast<Index>( row)) /= norm ;
	}
	return matrix ;
}

//------------------------------------------------------------------------------
/**
 * @brief graph上限内で音響方向を広く覆う観測を決定的に選びます
 * @param[in] features 検証済み単位長行列。1～4096行
 * @param[in] stop 代表選択と距離更新で確認する取消し状態
 * @return 最大384件の重複しない元行index。昇順で所有権を返します
 * @note 全行間の距離は保持せず、追加作業領域を行数に比例する量へ制限します
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<std::size_t> selectGraphRows( const Matrix &features, const std::stop_token stop)
{
	const std::size_t		 rows = static_cast<std::size_t>( features.rows()) ; // 検証済み全行数
	const std::size_t		 count = std::min( rows, K_MAXIMUM_GRAPH_ROWS) ; // 固有値計算へ渡す上限
	std::vector<std::size_t> selected ; // 最終的に元行順で返す所有index列
	selected.reserve( count) ;
	// graph上限以下は全観測を使い、既存の短い録音の標本を変えない
	if( rows <= K_MAXIMUM_GRAPH_ROWS) {
		selected.resize( rows) ;
		std::iota( selected.begin(), selected.end(), std::size_t{0}) ;
		return selected ;
	}
	const Eigen::RowVectorXd mean = features.colwise().mean() ; // 起点を選ぶ全体平均方向
	double					 closest = std::numeric_limits<double>::infinity() ; // 平均からの最小二乗距離
	Index					 current = 0 ; // 今回追加する未選択の元行
	// 時系列の先頭へ固定せず、全体平均に最も近い観測から被覆を始める
	for( Index row = 0; row < features.rows(); ++row) {
		checkCancelled( stop) ;
		const double distance = (features.row( row) - mean).squaredNorm() ; // 平均からの距離
		// 同距離なら元行indexが小さい候補を維持して再現性を保つ
		if( distance < closest) {
			closest = distance ;
			current = row ;
		}
	}
	// 各行から選択済み代表への最短距離だけを更新する
	Eigen::VectorXd	  distances = Eigen::VectorXd::Constant( features.rows(), std::numeric_limits<double>::infinity()) ;
	std::vector<bool> used( rows, false) ; // 同じ観測を二重に選ばないための状態
	// 未被覆の方向を順に追加し、graph上限で必ず終了する
	for( std::size_t iteration = 0; iteration < count; ++iteration) {
		checkCancelled( stop) ;
		selected.push_back( static_cast<std::size_t>( current)) ;
		used[static_cast<std::size_t>( current)] = true ;
		double farthest = -1.0 ; // 次の未選択行の最大最短距離
		Index  next = current ; // 最終反復では再使用しない次候補
		// 全行の最近傍距離を今回の代表で更新し、異なる少数方向も候補に残す
		for( Index row = 0; row < features.rows(); ++row) {
			checkCancelled( stop) ;
			distances( row) = std::min( distances( row), (features.row( row) - features.row( current)).squaredNorm()) ;
			// 重複方向でも未選択の行だけを選び、件数と同点時の順序を保つ
			if( !used[static_cast<std::size_t>( row)] && distances( row) > farthest) {
				farthest = distances( row) ;
				next = row ;
			}
		}
		current = next ;
	}
	std::sort( selected.begin(), selected.end()) ;
	return selected ;
}

//------------------------------------------------------------------------------
/**
 * @brief cosineの近傍順位と接続の強さから、次数正規化Laplacianを作ります
 * @param[in] ranking 行ごとの全近傍index。最大384行
 * @param[in] affinity 単位特徴のcosine類似度。rankingと同じ行数の正方行列
 * @param[in] neighbors 各行で採用する近傍数。自己loopを含みます
 * @return 自己loopを除き、D^-1/2 (D - A) D^-1/2で正規化した対称行列
 */
//------------------------------------------------------------------------------
[[nodiscard]] Matrix graphLaplacian( const std::vector<std::vector<Index>> &ranking, const Matrix &affinity,
									const int neighbors)
{
	const Index count = static_cast<Index>( ranking.size()) ; // 有界なgraphの行数
	Matrix		adjacency = Matrix::Zero( count, count) ; // 正のcosineを保持する有向近傍graph
	// 弱い接続を強い接続と同じ1へ置き換えず、上位近傍の類似度を保持する
	for( Index row = 0; row < count; ++row) {
		// 近傍数は呼出し側が行数以下へ制限する
		for( int neighbor = 0; neighbor < neighbors; ++neighbor) {
			const Index column =
				ranking[static_cast<std::size_t>( row)][static_cast<std::size_t>( neighbor)] ; // 接続先の行
			adjacency( row, column) = std::clamp( affinity( row, column), 0.0, 1.0) ;
		}
	}
	Matrix symmetric = (adjacency + adjacency.transpose()) * 0.5 ; // 平均による無向化
	symmetric.diagonal().setZero() ;
	const Eigen::VectorXd degree = symmetric.rowwise().sum() ; // 自己loopを除いた次数
	Matrix				  laplacian = -symmetric ; // 正規化前の対称Laplacian
	laplacian.diagonal() += degree ;
	// 発言量や観測の密度による次数差を、人数推定の根拠へ持ち込まない
	for( Index row = 0; row < count; ++row) {
		// 孤立点はゼロ行を維持し、架空の接続を作らない
		if( degree( row) <= 0) {
			continue ;
		}
		const double rowScale = std::sqrt( degree( row)) ; // 行の次数による正規化分母
		// 列の次数も除き、固有値計算に必要な対称性を保つ
		for( Index column = 0; column < count; ++column) {
			// 孤立点への成分は元からゼロであり、ゼロ除算しない
			if( degree( column) > 0) {
				laplacian( row, column) /= rowScale ;
				laplacian( row, column) /= std::sqrt( degree( column)) ;
			}
		}
	}
	return laplacian ;
}

//------------------------------------------------------------------------------
/**
 * @brief graphの分断数をedgeの有無から求めます
 * @param[in] laplacian 非対角の負要素がedgeを表す、最大384行の対称行列
 * @param[in] stop 成分の探索中に確認する取消し状態
 * @return 連結成分数。非空の入力では1以上
 * @note 小さい固有値を丸めて成分数へ置き換えず、実際の到達可能性を確認します
 */
//------------------------------------------------------------------------------
[[nodiscard]] int connectedComponents( const Matrix &laplacian, const std::stop_token stop)
{
	std::vector<bool>  visited( static_cast<std::size_t>( laplacian.rows()), false) ; // 探索済みの行
	std::vector<Index> pending ; // 現在の成分で未処理の行。各行は一度だけ追加する
	int				   components = 0 ; // 到達可能性で区別した成分数
	// 未探索の行を起点として、それぞれの成分を一度だけ調べる
	for( Index first = 0; first < laplacian.rows(); ++first) {
		// 別の起点から到達済みの行は同じ成分として扱う
		if( visited[static_cast<std::size_t>( first)]) {
			continue ;
		}
		++components ;
		visited[static_cast<std::size_t>( first)] = true ;
		pending.push_back( first) ;
		// 一つの成分内の到達先を、最大graph行数まで探索する
		while( !pending.empty()) {
			checkCancelled( stop) ;
			const Index row = pending.back() ; // 今回edgeを確認する行
			pending.pop_back() ;
			// 未探索かつ実edgeがある行だけを追加する
			for( Index column = 0; column < laplacian.cols(); ++column) {
				// 対角項や接続のない0値をedgeと誤認しない
				if( row != column && laplacian( row, column) < 0 && !visited[static_cast<std::size_t>( column)]) {
					visited[static_cast<std::size_t>( column)] = true ;
					pending.push_back( column) ;
				}
			}
		}
	}
	return components ;
}

//------------------------------------------------------------------------------
/**
 * @brief 固定した複数初期点と有限反復でspectral空間をcluster化します
 * @param[in] points 行がsample、列が小さい固有値に対応する座標
 * @param[in] count 非空clusterの目標数
 * @param[in] stop 各反復で確認する取消し状態
 * @return 行に対応する0始まりのlabel
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<int> deterministicKmeans( const Matrix &points, const int count, const std::stop_token stop)
{
	std::vector<int> best ; // 最小の二乗距離和を得た割当て
	double			 bestCost = std::numeric_limits<double>::infinity() ; // 初回候補を必ず受理する基準
	// 固定された3初期点で局所解への依存を減らす
	for( int attempt = 0; attempt < 3; ++attempt) {
		Matrix centers( count, points.cols()) ; // この試行のcluster中心
		centers.row( 0) = points.row( points.rows() * attempt / 3) ;
		Eigen::VectorXd nearest =
			Eigen::VectorXd::Constant( points.rows(), std::numeric_limits<double>::infinity()) ; // 既存中心への最短距離
		// 既存中心から最も離れた点を追加し、乱数を使用しない
		for( int cluster = 1; cluster < count; ++cluster) {
			// 前回追加した中心への距離で最短値を更新する
			for( Index row = 0; row < points.rows(); ++row) {
				nearest( row) = std::min( nearest( row), (points.row( row) - centers.row( cluster - 1)).squaredNorm()) ;
			}
			Index farthest = 0 ; // 現在の中心集合から最も離れた行
			nearest.maxCoeff( &farthest) ;
			centers.row( cluster) = points.row( farthest) ;
		}
		std::vector<int> labels( static_cast<std::size_t>( points.rows()), -1) ; // 直前の割当て
		// 最大100回で収束を判定し、異常な入力でも無期限に反復しない
		for( int iteration = 0; iteration < 100; ++iteration) {
			checkCancelled( stop) ;
			std::vector<int> updated( labels.size()) ; // 今回の最短中心への割当て
			std::vector<int> sizes( static_cast<std::size_t>( count), 0) ; // 空clusterの検出用件数
			Eigen::VectorXd	 distances( points.rows()) ; // 空cluster回復のための残差
			// 全sampleを最短の中心へ割り当てる
			for( Index row = 0; row < points.rows(); ++row) {
				double minimum = std::numeric_limits<double>::infinity() ; // 最短距離の初期値
				int	   selected = 0 ; // 同点は小さいindexを選ぶ
				// 全中心は20個以下に制限される
				for( int cluster = 0; cluster < count; ++cluster) {
					// 現在の点から比較中の中心までの二乗距離
					const double distance = (points.row( row) - centers.row( cluster)).squaredNorm() ;
					// より近い中心を見つけた場合だけ更新する
					if( distance < minimum) {
						minimum = distance ;
						selected = cluster ;
					}
				}
				updated[static_cast<std::size_t>( row)] = selected ;
				++sizes[static_cast<std::size_t>( selected)] ;
				distances( row) = minimum ;
			}
			// 空clusterには他の非単独clusterから最大残差の点を移す
			for( int cluster = 0; cluster < count; ++cluster) {
				// 非空clusterの代表を不要に動かさない
				if( sizes[static_cast<std::size_t>( cluster)] != 0) {
					continue ;
				}
				Index  selected = -1 ; // 移動可能な点がない場合のsentinel
				double largest = -1 ; // 非負距離の候補を受理する基準
				// 移動元を空にしない点だけを選ぶ
				for( Index row = 0; row < points.rows(); ++row) {
					// 同点は時系列で先の点を使用する
					if( sizes[static_cast<std::size_t>( updated[static_cast<std::size_t>( row)])] > 1 &&
					   distances( row) > largest) {
						selected = row ;
						largest = distances( row) ;
					}
				}
				// 目標数がsample数以下でも回復できない異常状態は拒否する
				if( selected < 0) {
					throw OperationError( ErrorKind::INFERENCE_FAILED) ;
				}
				auto &label = updated[static_cast<std::size_t>( selected)] ; // 移す点の割当て
				--sizes[static_cast<std::size_t>( label)] ;
				label = cluster ;
				++sizes[static_cast<std::size_t>( cluster)] ;
			}
			centers.setZero() ;
			// 新しい割当ての平均を求めるため座標を加算する
			for( Index row = 0; row < points.rows(); ++row) {
				centers.row( updated[static_cast<std::size_t>( row)]) += points.row( row) ;
			}
			// 空clusterを回復済みなのでゼロ除算は起こらない
			for( int cluster = 0; cluster < count; ++cluster) {
				centers.row( cluster) /= sizes[static_cast<std::size_t>( cluster)] ;
			}
			// 割当てが変わらなければこの試行を終了する
			if( updated == labels) {
				break ;
			}
			labels = std::move( updated) ;
		}
		double cost = 0 ; // 今回の二乗距離和
		// 全行の残差で複数初期点の結果を比較する
		for( Index row = 0; row < points.rows(); ++row) {
			cost += (points.row( row) - centers.row( labels[static_cast<std::size_t>( row)])).squaredNorm() ;
		}
		// 同点時は先に得た再現可能な解を保持する
		if( cost < bestCost) {
			bestCost = cost ;
			best = std::move( labels) ;
		}
	}
	return best ;
}

//------------------------------------------------------------------------------
/**
 * @brief 一つの話者の声の揺れに収まる、過分割されたclusterをまとめます
 * @param[in] features 単位長へ検証済みの全音声特徴
 * @param[in,out] result 全行の割当て。任意の人数指定がない場合だけ更新します
 * @param[in] stop 有限の統合反復で確認する取消し状態
 * @note 発言件数の少なさだけでは統合しません。特徴や中心を呼出し外へ保持しません
 */
//------------------------------------------------------------------------------
void refineOverlappingClusters( const Matrix &features, SpectralClusters &result, const std::stop_token stop)
{
	const int				 originalCount = result.count ; // 統合前の全体cluster数、最大20
	Matrix					 sums = Matrix::Zero( originalCount, features.cols()) ; // 単位特徴の方向和
	std::vector<std::size_t> counts( static_cast<std::size_t>( originalCount), 0) ; // 各群の観測数
	std::vector<int>		 parent( static_cast<std::size_t>( originalCount)) ; // 統合先への有限の対応
	std::iota( parent.begin(), parent.end(), 0) ;
	// graph標本だけでなく、割当て済みの全観測から声の揺れを推定する
	for( Index row = 0; row < features.rows(); ++row) {
		const int label = result.labels[static_cast<std::size_t>( row)] ; // この行の全体cluster
		sums.row( label) += features.row( row) ;
		++counts[static_cast<std::size_t>( label)] ;
	}
	// 一回で一つの群が減るため、最大でも元の人数-1回で終了する
	for( int iteration = 1; iteration < originalCount; ++iteration) {
		checkCancelled( stop) ;
		Matrix				directions = Matrix::Zero( originalCount, features.cols()) ; // 各群の平均方向
		std::vector<double> coherence( static_cast<std::size_t>( originalCount), 0) ; // 群内一致度
		// 方向和の長さと件数から、群内の平均cosine一致度を求める
		for( int cluster = 0; cluster < originalCount; ++cluster) {
			// 既に統合された群は比較候補へ戻さない
			if( counts[static_cast<std::size_t>( cluster)] == 0) {
				continue ;
			}
			const double norm = sums.row( cluster).norm() ; // 平均方向への換算分母
			// 打消しや数値異常により方向が定まらない群は推測統合しない
			if( !std::isfinite( norm) || norm <= K_EPSILON) {
				throw OperationError( ErrorKind::INFERENCE_FAILED) ;
			}
			directions.row( cluster) = sums.row( cluster) / norm ;
			coherence[static_cast<std::size_t>( cluster)] =
				norm / static_cast<double>( counts[static_cast<std::size_t>( cluster)]) ;
		}
		int	   source = -1 ; // 統合の根拠がない場合のsentinel
		int	   target = -1 ; // より多くの観測で支えられた統合先
		double strongest = K_EPSILON ; // 丸め誤差だけの同点を統合理由にしない
		// 群の中心が相手の群内変動より近い組だけを比較する
		for( int left = 0; left < originalCount; ++left) {
			// 空の群には声の特徴がない
			if( counts[static_cast<std::size_t>( left)] == 0) {
				continue ;
			}
			// 同じ組を逆順で再評価しない
			for( int right = left + 1; right < originalCount; ++right) {
				// 統合済みの群は平均方向を持たない
				if( counts[static_cast<std::size_t>( right)] == 0) {
					continue ;
				}
				const double similarity = directions.row( left).dot( directions.row( right)) ; // 中心同士のcosine
				// 群内の揺れに対して、中心同士がどれだけ近いかを表す差
				const double margin = similarity - std::min( coherence[static_cast<std::size_t>( left)],
															coherence[static_cast<std::size_t>( right)]) ;
				// 固定人数や固定距離ではなく、その音声の群内変動と照合する
				if( margin > strongest) {
					strongest = margin ;
					// 同点は先の群、件数差がある場合は多い観測で支えられた群を残す
					const bool keepLeft =
						counts[static_cast<std::size_t>( left)] >= counts[static_cast<std::size_t>( right)] ;
					target = keepLeft ? left : right ;
					source = keepLeft ? right : left ;
				}
			}
		}
		// 別話者の根拠を持つ群は、発言が短くてもそのまま残す
		if( source < 0) {
			break ;
		}
		sums.row( target) += sums.row( source) ;
		counts[static_cast<std::size_t>( target)] += counts[static_cast<std::size_t>( source)] ;
		counts[static_cast<std::size_t>( source)] = 0 ;
		parent[static_cast<std::size_t>( source)] = target ;
	}
	std::vector<int> compact( static_cast<std::size_t>( originalCount), -1) ; // 残った群の連続番号
	result.count = 0 ;
	// 空の群を人数へ数えず、最終labelの参照先を確定する
	for( int cluster = 0; cluster < originalCount; ++cluster) {
		// 生きている群だけに0始まりの番号を与える
		if( counts[static_cast<std::size_t>( cluster)] > 0) {
			compact[static_cast<std::size_t>( cluster)] = result.count++ ;
		}
	}
	// すべての音声特徴のlabelを、対応する統合先へ一度だけ移す
	for( auto &label : result.labels) {
		// parentは自分自身か過去に統合した別群だけを指し、最大20回で終わる
		while( parent[static_cast<std::size_t>( label)] != label) {
			label = parent[static_cast<std::size_t>( label)] ;
		}
		label = compact[static_cast<std::size_t>( label)] ;
	}
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief NME-SCと音響的な群内変動から話者clusterを自動推定します
 * @param[in] embeddings 4096行・1024次元以下の有限な非ゼロ特徴。保存しません
 * @param[in] expectedSpeakers 0は人数未知、1～20は任意の既知人数
 * @param[in] stop 固有値計算やk-meansの間で確認する取消し状態
 * @param[in] graphSelection Coupledは現行方式、Independentは診断専用のgraph比較
 * @return 入力行と同じ長さのlabelと推定人数
 * @throws OperationError 不正特徴、上限超過、数値異常、取消し
 * @note 音響方向を覆う最大384行のgraphと有限候補を使用します。ADR 0009/0010/0019参照
 * @see koefude::clusterSpeakers
 */
//------------------------------------------------------------------------------
SpectralClusters clusterSpeakers( const std::span<const std::vector<float>> embeddings, const int expectedSpeakers,
								 const std::stop_token stop, const SpectralGraphSelection graphSelection)
{
	checkCancelled( stop) ;
	// 人数上限と外部配列件数を確保・narrowingの前に検証する
	if( expectedSpeakers < 0 || expectedSpeakers > 20 || embeddings.size() > K_MAXIMUM_EMBEDDING_ROWS ||
	   (graphSelection != SpectralGraphSelection::COUPLED && graphSelection != SpectralGraphSelection::INDEPENDENT) ||
	   (graphSelection == SpectralGraphSelection::INDEPENDENT && expectedSpeakers != 0)) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// 音声根拠がない場合は架空の話者を作らない
	if( embeddings.empty()) {
		return {} ;
	}
	const Matrix	  full = normalizedFeatures( embeddings, stop) ; // 全特徴の単位長行列
	const std::size_t fitCount = std::min( embeddings.size(), K_MAXIMUM_GRAPH_ROWS) ; // graph上限
	// 観測数より多い既知人数を満たすlabelを捏造しない
	if( static_cast<std::size_t>( expectedSpeakers) > fitCount) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	SpectralClusters result ; // UI・外部resourceと独立した対応
	result.labels.resize( embeddings.size(), 0) ;
	result.count = 1 ;
	// 1人指定・単独sample・数値的に同一の特徴は分割の根拠がない
	if( expectedSpeakers == 1 || fitCount == 1 ||
	   (expectedSpeakers == 0 && (full.rowwise() - full.row( 0)).squaredNorm() < K_EPSILON)) {
		return result ;
	}
	Matrix	   fit( static_cast<Index>( fitCount), full.cols()) ; // graph用の有界標本
	const auto sampled = selectGraphRows( full, stop) ; // 音響方向を覆う全入力への逆対応
	// 元行順の代表だけをgraphへ渡し、標本外への割当てに同じ対応を使う
	for( std::size_t row = 0; row < fitCount; ++row) {
		const std::size_t source = sampled[row] ; // 全入力中の選択済み行
		fit.row( static_cast<Index>( row)) = full.row( static_cast<Index>( source)) ;
	}
	const Matrix					affinity = fit * fit.transpose() ; // 閾値を適用しないcosine類似度
	std::vector<std::vector<Index>> ranking( fitCount) ; // 各行で固定する近傍の順位
	// 異なる近傍数でも同じ順位を使うため一度だけsortする
	for( std::size_t row = 0; row < fitCount; ++row) {
		auto &order = ranking[row] ; // その行の所有順位列
		order.resize( fitCount) ;
		std::iota( order.begin(), order.end(), Index{0}) ;
		std::sort( order.begin(), order.end(),
				  //------------------------------------------------------------------------------
				  /**
				   * @brief 同じ行の類似度を降順、同率なら列番号順へ並べます
				   * @param[in] left 左候補の列番号
				   * @param[in] right 右候補の列番号
				   * @return 左の類似度が高いか、同率で列番号が小さければtrue
				   * @note rowとaffinityは同期sort完了まで参照し、callbackを保存しません
				   */
				  //------------------------------------------------------------------------------
				  [&]( const Index left, const Index right)
				  {
					  const double a = affinity( static_cast<Index>( row), left) ; // 左候補の類似度
					  const double b = affinity( static_cast<Index>( row), right) ; // 右候補の類似度
					  return a > b || (a == b && left < right) ;
				  }) ;
	}
	std::set<int> candidates ; // 重複を除いた近傍数の昇順探索
	const int	  maximumP = std::max( 2, static_cast<int>( (fitCount + 3) / 4)) ; // 有限探索範囲
	// 小規模は全点、大規模は等間隔24点へ計算量を制限する
	for( int point = 0; point < 24; ++point) {
		candidates.insert( 2 + point * (maximumP - 2) / 23) ;
	}
	std::vector<GraphHypothesis> hypotheses ; // 最大24候補の数量だけを保持する
	int							 minimumComponents = static_cast<int>( fitCount) ; // 探索範囲内で最も分断の少ない数
	// 有限の候補点ごとに人数と正規化eigengapを評価する
	for( const int neighbors : candidates) {
		checkCancelled( stop) ;
		const Matrix laplacian = graphLaplacian( ranking, affinity, neighbors) ; // 今回の対称行列
		// 候補比較では固有vectorを作らず固有値だけを計算する所有solver
		const Eigen::SelfAdjointEigenSolver<Matrix> solver( laplacian, Eigen::EigenvaluesOnly) ;
		// 数値失敗を有効な人数へ変換しない
		if( solver.info() != Eigen::Success || !solver.eigenvalues().allFinite()) {
			throw OperationError( ErrorKind::INFERENCE_FAILED) ;
		}
		const auto &values = solver.eigenvalues() ; // solverが所有する昇順固有値
		double		largestGap = 0 ; // 上限20人以内で最大の隣接差
		int			estimated = 1 ; // 最大差の位置に対応する人数
		// 零固有値を含む小さい側の差から人数を推定する
		for( Index index = 0; index < std::min<Index>( 20, values.size() - 1); ++index) {
			const double gap = values( index + 1) - values( index) ; // 人数index+1の分離度
			// 同点時は先の少人数仮説を維持する
			if( gap > largestGap) {
				largestGap = gap ;
				estimated = static_cast<int>( index) + 1 ;
			}
		}
		// graphのscaleを最大固有値で除いた分離度
		const double normalizedGap = largestGap / (values( values.size() - 1) + K_EPSILON) ;
		// 自己loopしかない退化graphのゼロ差では除算しない
		if( normalizedGap <= K_EPSILON) {
			continue ;
		}
		const double ratio = neighbors / normalizedGap ; // 論文のp/g_p
		const int	 components = connectedComponents( laplacian, stop) ; // 丸め誤差のない分断数
		minimumComponents = std::min( minimumComponents, components) ;
		hypotheses.push_back( {neighbors, estimated, components, ratio}) ;
	}
	// 数値的に選択できなかった場合は別アルゴリズムへ無言でfallbackしない
	if( hypotheses.empty()) {
		throw OperationError( ErrorKind::INFERENCE_FAILED) ;
	}
	std::array<int, 21> votes{} ; // 分断数が最小の候補が支持した1～20人の仮説
	// 類似度を保ったgraphで、人数推定の安定性を集約する
	for( const auto &hypothesis : hypotheses) {
		// 不要に分断された候補で支持数を増やさない
		if( hypothesis.components == minimumComponents) {
			++votes[static_cast<std::size_t>( hypothesis.speakers)] ;
		}
	}
	const int maximumVotes = *std::max_element( votes.begin(), votes.end()) ; // 最も多い支持数
	double	  bestRatio = std::numeric_limits<double>::infinity() ; // 同じ支持数ならNME値で比較
	// 最多の人数仮説の中でNME値が最小のgraphを使用する
	for( const auto &hypothesis : hypotheses) {
		// 既知人数を自動推定の入力へ使わず、指定時だけ最終人数へ反映する
		if( hypothesis.components == minimumComponents &&
		   (expectedSpeakers > 0 || votes[static_cast<std::size_t>( hypothesis.speakers)] == maximumVotes) &&
		   hypothesis.ratio < bestRatio) {
			bestRatio = hypothesis.ratio ;
			result.neighbors = hypothesis.neighbors ;
			result.count = expectedSpeakers > 0 ? expectedSpeakers : hypothesis.speakers ;
		}
	}
	// 人数の支持投票と同点規則は保持し、診断時だけ使用graphを選び直す
	if( graphSelection == SpectralGraphSelection::INDEPENDENT) {
		bestRatio = std::numeric_limits<double>::infinity() ;
		// 明示人数経路と同じ候補集合でNME最小のgraphだけを選ぶ
		for( const auto &hypothesis : hypotheses) {
			// 分断を増やすgraphは現行方式と同様に採用しない
			if( hypothesis.components == minimumComponents && hypothesis.ratio < bestRatio) {
				bestRatio = hypothesis.ratio ;
				result.neighbors = hypothesis.neighbors ;
			}
		}
	}
	checkCancelled( stop) ;
	// 選んだgraphだけを再構成し、候補数倍の行列を保持しない
	const Matrix								bestLaplacian = graphLaplacian( ranking, affinity, result.neighbors) ;
	const Eigen::SelfAdjointEigenSolver<Matrix> solver( bestLaplacian) ; // 最終graphの固有vector
	// 最終座標の生成失敗も呼出し元へ返す
	if( solver.info() != Eigen::Success || !solver.eigenvectors().allFinite()) {
		throw OperationError( ErrorKind::INFERENCE_FAILED) ;
	}
	Matrix coordinates = solver.eigenvectors().leftCols( result.count) ; // 小さい固有値側
	// 正規化Laplacianの座標の長さを揃え、次数差をk-meansへ戻さない
	for( Index row = 0; row < coordinates.rows(); ++row) {
		const double norm = coordinates.row( row).norm() ; // この行の座標長
		// 表現できない成分を任意の話者へ割り当てない
		if( !std::isfinite( norm) || norm <= K_EPSILON) {
			throw OperationError( ErrorKind::INFERENCE_FAILED) ;
		}
		coordinates.row( row) /= norm ;
	}
	const auto fitted = deterministicKmeans( coordinates, result.count, stop) ; // graph上の割当て
	Matrix	   centers = Matrix::Zero( result.count, full.cols()) ; // 元の音響空間の平均方向
	// graph標本のlabelはspectral解を維持し、未使用行への中心を求める
	for( std::size_t row = 0; row < fitCount; ++row) {
		result.labels[sampled[row]] = fitted[row] ;
		centers.row( fitted[row]) += fit.row( static_cast<Index>( row)) ;
	}
	// 単位長に揃えてcosine比較へ使う
	for( int cluster = 0; cluster < result.count; ++cluster) {
		const double norm = centers.row( cluster).norm() ; // 中心の方向の正規化分母
		// 打消しで方向を持たない中心は任意割当てせず拒否する
		if( norm <= K_EPSILON) {
			throw OperationError( ErrorKind::INFERENCE_FAILED) ;
		}
		centers.row( cluster) /= norm ;
	}
	std::size_t fitIndex = 0 ; // 標本に含まれた行の順次確認
	// graph上限を超えた行だけを音響中心へ割り当てる
	for( std::size_t row = 0; row < embeddings.size(); ++row) {
		checkCancelled( stop) ;
		// graph標本の割当てを平均中心で上書きしない
		if( fitIndex < fitCount && sampled[fitIndex] == row) {
			++fitIndex ;
			continue ;
		}
		Index cluster = 0 ; // 最大cosine類似度を持つ平均方向のindex
		(centers * full.row( static_cast<Index>( row)).transpose()).maxCoeff( &cluster) ;
		result.labels[row] = static_cast<int>( cluster) ;
	}
	checkCancelled( stop) ;
	// 任意の既知人数指定を自動統合で上書きせず、自動推定だけを精緻化する
	if( expectedSpeakers == 0 && result.count > 1) {
		refineOverlappingClusters( full, result, stop) ;
	}
	checkCancelled( stop) ;
	return result ;
}

} // namespace koefude
