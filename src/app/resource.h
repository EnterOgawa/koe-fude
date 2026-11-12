//------------------------------------------------------------------------------

/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file resource.h
 * @brief UI表示resourceとcontrol IDを定義します
 * @version 1.0.0
 *
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 チャット画面のresourceを追加
 * 0.1.0 Shunji Ogawa 2026/09/09 文字起こし品質と発話区間検出の選択を追加
 * 0.1.0 Shunji Ogawa 2026/09/09 品質comboを常設し、model場所選択を削除
 * 0.1.0 Shunji Ogawa 2026/09/09 モデル系統と特化モデルの品質表示を追加
 * 0.1.0 Shunji Ogawa 2026/09/09 アイコン・話者filter・UI言語の識別子を追加
 * 0.1.0 Shunji Ogawa 2026/09/09 登録済み音声の再実行と受付制限の表示を追加
 * 0.1.0 Shunji Ogawa 2026/09/09 合計・工程別処理時間の説明を追加
 * 0.1.0 Shunji Ogawa 2026/09/10 永続履歴の復元・保存・削除と失敗の説明を追加
 * 0.1.0 Shunji Ogawa 2026/09/10 Parakeetの選択・CPU構成・結果を表すresource IDを追加
 * 0.1.0 Shunji Ogawa 2026/09/11 話者候補の理由と推定範囲の表示IDを追加
 * 0.1.0 Shunji Ogawa 2026/09/11 FP32品質とCUDA・CPU再試行の表示を追加
 * 0.1.0 Shunji Ogawa 2026/09/13 モデル準備の条件・容量・通信先・段階と操作IDを追加
 * 0.1.0 Shunji Ogawa 2026/09/27 設定欄のアクセシビリティ目的名を追加
 * 0.1.0 Shunji Ogawa 2026/10/02 WAVの画面接続・入力説明と検証を追加
 * 0.1.0 Shunji Ogawa 2026/10/02 MP3を画面の入力案内へ追加
 * 0.1.0 Shunji Ogawa 2026/10/04 保存済み整形の表示選択/準備/字幕案内を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 自由全文専用操作・実phase・原因別案内を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 固定AVX2方式と通常workerの検証接続を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 補正準備入口の状態・操作resourceを追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1280の明示モデル導入と同一枠の操作排他を接続
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1283の専用取得resourceを追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1343の文章専用補正を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1344の明示API補正と独立保存を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1345の動的モデル・凍結単価と旧履歴互換を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1346の通常API補正・保存モデル表示・限定ログを接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1347の音声テーブル・独立API設定と文章操作を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1349の一回適用と全単価表示へ更新
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1351のAPI専用HTTP失敗・保存状態を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1352の実区間進捗・採用保存を接続
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1353の実完了工程説明を追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1354の統合tabと行操作を追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1356の調整境界と目的名を追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1358の短いモデル状態resourceを追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1360の話者区切り検証と表示説明を追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1363の旧全文比較説明を追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1364の旧結果の表示位置/未対応説明を更新
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1365の改善有無のtooltip説明を追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1370の文脈分割と明示途中再開を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1375の適用済み選好の保存と復元を追加
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1378の明示更新境界を追加
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1380の短い状態と一回の詳細通知を追加
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1382の案内fontをMS ゴシックへ統一
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1400の準備commandを内部互換IDとして保持
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1402の初回取得失敗/キー未設定を区別
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */

//------------------------------------------------------------------------------

#pragma once

#define IDS_TITLE 100 // 製品名

#define IDS_SUBTITLE 101 // ローカル実行の説明

#define IDS_OPEN 102 // 音声選択操作

#define IDS_CLEAR 104 // メモリ履歴解放

#define IDS_COPY 105 // 明示copy

#define IDS_SAVE 106 // 明示保存

#define IDS_CANCEL 107 // 選択jobの取消し

#define IDS_DIARIZE 108 // 話者分離設定

#define IDS_AUTO_GPU 109 // 実測済みGPU優先

#define IDS_CPU 110 // CPU固定

#define IDS_JAPANESE 111 // 日本語固定

#define IDS_AUTO_LANGUAGE 112 // 言語自動判定

#define IDS_AUTO_SPEAKERS 113 // 話者数自動判定

#define IDS_SPEAKER_COUNT 114 // 話者数の単位

#define IDS_HINT 115 // 未投入時の案内

#define IDS_READY 116 // 待機状態

#define IDS_QUEUED 117 // キュー待機

#define IDS_COMPLETED 118 // 完了状態

#define IDS_WARNING 119 // 話者分離部分失敗

#define IDS_NO_TEXT 120 // 認識結果なし

#define IDS_CANCELLING 121 // 協調取消し待ち

#define IDS_CANCELLED 122 // 取消し完了

#define IDS_STOPPING 123 // Window close待ち

#define IDS_FULL 124 // 有限queue/historyの上限

#define IDS_ERROR 125 // 診断dialog

#define IDS_MODEL_HELP 126 // 初回準備導線

#define IDS_INVALID 127 // 入力拒否

#define IDS_PROCESS_ERROR 128 // 推論失敗からの復帰案内

#define IDS_SAVED 129 // 保存成功

#define IDS_SAVE_ERROR 130 // 保存失敗の案内

#define IDS_RENAME 131 // 話者名変更

#define IDS_NAME_HINT 132 // 話者名editの案内

#define IDS_SPEAKER 133 // 既定の話者名

#define IDS_GPU_USED 134 // 実行経路GPU

#define IDS_CPU_USED 135 // 実行経路CPU

#define IDS_ESTIMATE 136 // 話者数の推定表示

#define IDS_OPEN_PICK 138 // 音声dialog目的

#define IDS_SAVE_PICK 139 // 保存dialog目的

#define IDS_FORMAT_TEXT 140 // TXT形式

#define IDS_FORMAT_SRT 141 // SRT形式

#define IDS_FORMAT_VTT 142 // VTT形式

#define IDS_AUDIO_FILTER 143 // M4A/WAV/MP3の製品入力形式

#define IDS_FILES 144 // file履歴の標準label

#define IDS_CONVERSATION 145 // 発言表示領域のlabel

#define IDS_SETTINGS 146 // 推論設定group

#define IDS_MENU_FILE 147 // file menu

#define IDR_MAIN_MENU 201 // 標準Window menu

#define IDC_EXIT 1100 // アプリの終了操作

#define IDC_SPECTRAL_METHOD 1101 // 次jobへ研究方式を選択するmenu

#define IDC_LEGACY_METHOD 1102 // 次jobへ従来方式を選択するmenu

#define IDC_ASR_QUALITY 1103 // 次jobへ選択モデルの品質優先構成を適用するmenu

#define IDC_ASR_STANDARD 1104 // 次jobへ選択モデルの軽量構成を適用するmenu

#define IDC_SPEECH_FILTER 1105 // 発話区間検出の有効・無効を切り替えるmenu

#define IDS_SPECTRAL_METHOD 148 // 実行した研究方式の表示

#define IDS_LEGACY_METHOD 149 // 実行した従来方式の表示

#define IDS_NO_DIARIZATION 150 // 話者分離を実行しない設定の表示

#define IDS_ASR_QUALITY 151 // 品質優先modelの結果表示

#define IDS_ASR_STANDARD 152 // 高速modelの結果表示

#define IDS_SPEECH_FILTER_ON 153 // 発話区間を抽出した設定の表示

#define IDS_SPEECH_FILTER_OFF 154 // 発話区間を抽出しない設定の表示

#define IDS_QUALITY_LABEL 155 // 常設の品質選択label

#define IDS_QUALITY_CHOICE 156 // 品質優先のcombo選択肢

#define IDS_STANDARD_CHOICE 157 // 標準・高速のcombo選択肢

#define IDS_STAGES 160 // 以後Stage enum順の処理説明

#define IDS_MODEL_LABEL 170 // 常設のモデル選択label

#define IDS_MODEL_WHISPER 171 // 多言語モデル系統

#define IDS_MODEL_KOTOBA 172 // 日本語特化モデル系統

#define IDS_KOTOBA_QUALITY_CHOICE 173 // 日本語モデルのFP16構成

#define IDS_KOTOBA_STANDARD_CHOICE 174 // 日本語モデルのQ5構成

#define IDS_ASR_KOTOBA_QUALITY 175 // 日本語FP16で認識した結果の説明

#define IDS_ASR_KOTOBA_STANDARD 176 // 日本語Q5で認識した結果の説明

#define IDC_OPEN 1001 // 音声投入button

#define IDC_CLEAR 1003 // 履歴clear button

#define IDC_COPY 1004 // copy button

#define IDC_SAVE 1005 // save button

#define IDC_CANCEL 1006 // cancel button

#define IDC_DIARIZE 1007 // 話者分離checkbox

#define IDC_BACKEND 1008 // CPU/GPU選択

#define IDC_LANGUAGE 1009 // 認識言語選択

#define IDC_COUNT 1010 // 既知の話者数

#define IDC_HISTORY 1011 // file別履歴list

#define IDC_CHAT 1012 // 選択・copy可能な発話表示

#define IDC_SPEAKER 1013 // 変更する話者番号

#define IDC_NAME 1014 // 話者表示名edit

#define IDC_RENAME 1015 // 表示名適用button

#define IDC_ASR_PROFILE 1016 // 常設の認識品質combo

#define IDC_ASR_LABEL 1017 // 認識品質comboのlabel

#define IDC_ASR_MODEL 1018 // モデル系統の選択combo

#define IDC_MODEL_LABEL 1019 // モデル系統comboのlabel

#define IDS_FILTER_LABEL 177 // 表示filterのlabel

#define IDS_FILTER_ALL 178 // 全話者表示

#define IDS_UNKNOWN 179 // 話者不明

#define IDS_MIXED 180 // 混在の選択肢

#define IDS_MIXED_SUFFIX 181 // 混在区間の話者label接尾辞

#define IDS_SPEAKER_SEPARATOR 182 // 混在話者名の区切り

#define IDS_FILTER_EMPTY 183 // 表示対象なしの説明

#define IDS_EXPORT_SCOPE 184 // 保存とcopyの対象範囲

#define IDS_LANGUAGE_SAVE_ERROR 185 // 言語設定保存の失敗

#define IDS_INTERNAL_ERROR 186 // 予期しない例外の説明

#define IDS_SINGLE_SPEAKER_COUNT 187 // 話者が一人の場合の人数単位

#define IDS_RETRANSCRIBE 188 // 現在の設定で登録済み音声を再実行する操作

#define IDS_RETRANSCRIBE_BUSY 189 // 同じ音声を処理中のため再実行できない説明

#define IDS_RETRANSCRIBE_LIMIT 190 // 受付・履歴上限で再実行できない説明

#define IDS_TIMING_TOTAL 191 // 待機を除く合計処理時間

#define IDS_TIMING_AUDIO 192 // 元音声の長さ

#define IDS_TIMING_DECODE 193 // 入力検証とdecodeの時間

#define IDS_TIMING_ASR 194 // モデル検証・読込を含む文字起こし時間

#define IDS_TIMING_DIARIZATION 195 // 話者分離と本文結合の時間

#define IDS_TIMING_SKIPPED 196 // 話者分離を省略した説明

#define IDS_TIMING_WARNING 197 // 話者分離の部分失敗

#define IDS_TIMING_NOTE 198 // 時刻形式とモデル準備時間の説明

#define IDS_TIMING_UNAVAILABLE 199 // 不正計測値の代替表示

#define IDS_HISTORY_LOADING 200 // 履歴を非同期に読み込む状態

#define IDS_HISTORY_SAVING 201 // 最新の履歴を保存中

#define IDS_HISTORY_SAVED 202 // 履歴の保存が完了した状態

#define IDS_HISTORY_LOAD_ERROR 203 // 既存履歴を保持した読込失敗

#define IDS_HISTORY_SAVE_ERROR 204 // 元fileを保持した保存失敗

#define IDS_HISTORY_IN_USE 205 // 同じ保存先を別の起動が使用中

#define IDS_HISTORY_INTERRUPTED 206 // 前回の処理が未完了のまま終了

#define IDS_HISTORY_REMOVE 207 // 一項目の履歴だけを削除

#define IDS_HISTORY_REMOVE_BUSY 208 // 処理中の項目の削除を禁止

#define IDS_HISTORY_CLEAR_CONFIRM 209 // 永続履歴の全削除確認

#define IDS_HISTORY_RETRY 210 // 履歴へのアクセスの再試行

#define IDS_HISTORY_EXIT_FAILED 211 // 未保存の変更がある終了の確認

#define IDS_HISTORY_CORRUPT 212 // 不正な保存内容を保護した案内

#define IDS_HISTORY_VERSION 213 // 未対応の履歴形式の案内

#define IDS_HISTORY_TOO_LARGE 214 // 履歴の件数・保存容量上限

#define IDS_HISTORY_REMOVED 215 // 履歴削除を受け付けた状態

#define IDS_TEMPORARY_STORAGE_ERROR 216 // 長時間処理用の専用一時領域の異常

#define IDS_MODEL_PARAKEET 217 // 独立評価を通った日本語CTCモデル

#define IDS_PARAKEET_CHOICE 218 // 既存INT8を使うCPUの軽量構成

#define IDS_ASR_PARAKEET 219 // 日本語CTCで認識した結果の説明

#define IDS_PARAKEET_FP32_CHOICE 220 // FP32を使う品質優先構成

#define IDS_ASR_PARAKEET_FP32 221 // 日本語CTC FP32で認識した結果の説明

#define IDS_CUDA_USED 222 // CUDAで成功した実行結果

#define IDS_CPU_RETRIED 223 // 同じモデルをCPUへ再試行して成功した結果

#define IDS_ATTR_UNAVAILABLE_SUFFIX 224 // 旧履歴等に理由を記録していない状態

#define IDS_ATTR_UNASSIGNED_SUFFIX 225 // 採用できる話者候補がない状態

#define IDS_ATTR_CHANGE_SUFFIX 226 // 逐次交代の本文内割当てが未確定

#define IDS_ATTR_OVERLAP_SUFFIX 227 // 候補の時間的重なりを推定した状態

#define IDS_ATTR_RANGES 228 // 本文との文字対応と区別した推定範囲の注記

#define IDC_HISTORY_REMOVE 1109 // 選択履歴の明示削除

#define IDC_HISTORY_RETRY 1110 // 履歴保存・読込の再試行

#define IDC_RETRANSCRIBE 1108 // 履歴の再実行popup操作

#define IDC_UI_JAPANESE 1106 // 日本語UIの選択

#define IDC_UI_ENGLISH 1107 // 英語UIの選択

#define IDC_FILTER 1020 // 話者別の表示combo

#define IDC_FILTER_LABEL 1021 // 表示comboのlabel

#define IDC_EXPORT_SCOPE 1022 // 全話者保存の案内

#define IDI_APP 301 // 自作APPアイコン

#define IDI_ADD 302 // 自作ADDアイコン

#define IDI_SAVE 303 // 自作SAVEアイコン

#define IDI_COPY 304 // 自作COPYアイコン

#define IDI_CANCEL 305 // 自作CANCELアイコン

#define IDI_CLEAR 306 // 自作CLEARアイコン

#define IDI_RENAME 307 // 自作RENAMEアイコン

#define IDS_PREP_TITLE 229 // モデル準備の操作名とWindow名

#define IDS_PREP_INTRO 230 // 明示取得と通常offlineの説明

#define IDS_PREP_STORAGE 231 // 使用する保存先

#define IDS_PREP_CAPACITY 232 // 選択依存の最終容量

#define IDS_PREP_TRANSFER 233 // 不足分だけの転送容量

#define IDS_PREP_DISK 234 // 一時領域を含む追加空き容量

#define IDS_PREP_HOSTS 235 // redirectを含むHTTPS許可先

#define IDS_PREP_TERMS 236 // 配布条件の確認方法

#define IDS_PREP_SOURCE 237 // 配布条件と変換元の一次資料

#define IDS_PREP_ROLE_VAD 238 // 発話検出の用途

#define IDS_PREP_ROLE_ASR 239 // 文字起こしと補助fileの用途

#define IDS_PREP_DOWNLOAD 240 // 不足分の明示取得button

#define IDS_PREP_RECHECK 241 // 通信なしの再検査button

#define IDS_ACCESS_BACKEND 242 // 実行方式欄の目的名

#define IDS_ACCESS_LANGUAGE 243 // 認識言語欄の目的名

#define IDS_ACCESS_COUNT 244 // 話者数欄の目的名

#define IDS_ACCESS_SPEAKER 245 // 名前編集対象の目的名

#define IDS_ACCESS_NAME 246 // 話者表示名の編集欄の目的名

#define IDS_INPUT_CODEC 247 // 未対応の音声形式・属性

#define IDS_INPUT_DECODE 248 // 破損・形式不整合の読込失敗

#define IDS_INPUT_LIMIT 249 // 入力資源・形式規則・整数の上限

#define IDS_INPUT_TIMELINE 250 // 音声時刻・長さの不整合

#define IDS_PREP_STAGES 270 // ModelPreparationStage順の10段階

#define IDC_MODEL_PREPARE 1023 // 内部互換の準備画面command

#define IDC_PREP_DETAILS 1200 // 選択・copy可能な条件欄

#define IDC_PREP_STATUS 1201 // 準備段階と診断

#define IDC_PREP_PROGRESS 1202 // 実byteの進捗帯

#define IDC_PREP_DOWNLOAD 1203 // 選択構成の取得開始

#define IDC_PREP_CANCEL 1204 // 現在操作の協調取消し

#define IDC_PREP_RECHECK 1205 // local再検査の要求

#define IDC_READING_VIEW 1024 // 保存済み結果の表示意図

#define IDC_READING_LABEL 1025 // 保存済み結果の表示label

#define IDS_READING_LABEL 251 // 表示選択のlabelと目的名

#define IDS_READING_AUTO 252 // 有効整形を優先する表示意図

#define IDS_READING_ORIGINAL 253 // 原文の選択名

#define IDS_READING_FORMATTED 254 // 整形文の選択名

#define IDS_READING_TITLE 255 // 元時刻/話者を使う整形文の説明

#define IDS_READING_COUNTS 256 // 変更/原文保持件数の書式

#define IDS_READING_PREPARING 257 // 推論と区別した表示準備

#define IDS_READING_TEXT_ONLY 258 // 整形文字幕の拒否と対応

#define IDS_READING_RETAINED_COUNT 259 // 原文保持件数の接頭辞

#define IDS_READING_FAILED 260 // ASRと区別した表示準備失敗の説明

#define IDC_READING_CREATE 1026 // 採用済み方式がある場合だけの作成/再作成menu

#define IDS_READING_CREATE 261 // 初回生成の操作名

#define IDS_READING_RECREATE 262 // 明示再生成の操作名

#define IDS_READING_UNAVAILABLE 263 // 採用/準備済み方式なしの受付拒否

#define IDS_READING_QUEUED 264 // 整形workerの待機

#define IDS_READING_LOADING 265 // 整形方式の実読込段階

#define IDS_READING_FORMATTING 266 // 整形の実生成段階

#define IDS_READING_VALIDATING 267 // 整形全体の実照合段階

#define IDS_READING_CANCELLING 268 // 整形の協調取消し待ち

#define IDS_READING_DONE 269 // 整形成功と原文保持

#define IDS_READING_GENERATION_FAILED 280 // 整形失敗と前回結果保持

#define IDS_READING_CANCELLED 281 // 整形取消しと前回結果保持

#define IDS_READING_INTERRUPTED 282 // 前回未完了と再生成なし

// ADR1268: 自由全文専用操作と独立phase

#define IDS_WHOLE_TITLE 283 // 全文用の用途別説明

#define IDS_WHOLE_CREATE 284 // 全文用の用途別説明

#define IDS_WHOLE_COPY 285 // 全文用の用途別説明

#define IDS_WHOLE_SAVE 286 // 全文用の用途別説明

#define IDS_WHOLE_VIEW 287 // 全文用の用途別説明

#define IDS_WHOLE_ORIGINAL_COPY 288 // 全文用の用途別説明

#define IDS_WHOLE_ORIGINAL_SAVE 289 // 全文用の用途別説明

#define IDS_WHOLE_NO_PCM 290 // 全文用の用途別説明

#define IDS_WHOLE_PCM_MISMATCH 291 // 全文用の用途別説明

#define IDS_WHOLE_RESOURCES 292 // 全文用の用途別説明

#define IDS_WHOLE_MODELS 293 // 全文用の用途別説明

#define IDS_WHOLE_PHASES 300 // 全文用の用途別説明

#define IDS_WHOLE_PHASE_AUX 301 // 全文用の用途別説明

#define IDS_WHOLE_PHASE_PREP 302 // 全文用の用途別説明

#define IDS_WHOLE_PHASE_GENERATE 303 // 全文用の用途別説明

#define IDS_WHOLE_PHASE_VALIDATE 304 // 全文用の用途別説明

#define IDS_WHOLE_TEXT_TITLE IDS_WHOLE_TITLE // 中立全文見出しの共有名

#define IDC_WHOLE_CREATE 1027 // 明示CPU試用

#define IDC_WHOLE_COPY 1028 // 全文専用copy

#define IDC_WHOLE_SAVE 1029 // 全文専用TXT

#define IDC_WHOLE_CREATE_AVX2 1030 // 明示AVX2試用

#define IDS_WHOLE_CREATE_AVX2 501 // AVX2用途説明

#define IDS_WHOLE_CPU_UNSUPPORTED 502 // CPU/OS契約不足

#define IDS_WHOLE_RUNTIME_REJECTED 503 // 完成構成の不一致

#define IDS_WHOLE_RUNTIME_MISSING 504 // 配布slot不足

#define IDC_WHOLE_PREPARE 1031 // 補正準備の通常menu

#define IDC_WTP_VARIANT 1210 // 明示CPU方式

#define IDC_WTP_DETAILS 1211 // 選択可能な検査snapshot

#define IDC_WTP_CHECK 1212 // 通信なしの明示検査

#define IDC_WTP_CANCEL 1213 // 準備検査の取消し

#define IDS_WTP_TITLE 510 // 補正の準備状況

#define IDS_WTP_INTRO 511 // 実行前の条件とsnapshot説明

#define IDS_WTP_PORTABLE 512 // portable選択

#define IDS_WTP_AVX2 513 // AVX2/no-repack選択

#define IDS_WTP_WEIGHT 514 // weight保存容量

#define IDS_WTP_CHECK 515 // 明示検査button

#define IDS_WTP_READY 516 // 三要素Ready

#define IDS_WTP_NOT_READY 517 // 準備不足

#define IDS_WTP_RUNTIME 518 // runtime用途

#define IDS_WTP_WHISPER 519 // 補助Whisper用途

#define IDS_WTP_GEMMA 520 // 補正Gemma用途

#define IDS_WTP_NEXT 521 // 正規導入の残件と次操作

#define IDS_WTP_STATES 530 // 要素状態6件の先頭

#define IDC_WTP_WHISPER 1214 // 固定補助モデルの明示取得

#define IDC_WTP_IMPORT 1215 // 取得済みlocalモデル導入

#define IDS_WTP_WHISPER_PREPARE 522 // 専用準備button

#define IDS_WTP_BUSY 524 // 他処理と競合する操作の理由

#define IDS_WTP_IMPORT 523 // 専用導入button

#define IDS_WTP_STAGES 540 // 実段階16件の先頭

#define IDC_WTP_DOWNLOAD 1216 // 固定補正モデルの明示取得

#define IDS_WTP_DOWNLOAD 525 // 専用Direct取得button

#define IDS_WTP_DOWNLOAD_FAILURE 526 // 元診断を保持した正規取得の案内

#define IDS_BOUNDED_PREFIX 560 // 未完prefixの明示表示

#define IDS_BOUNDED_FAILED 561 // 失敗prefix

#define IDS_BOUNDED_CANCELLED 562 // 取消しprefix

#define IDS_BOUNDED_INTERRUPTED 563 // 前回中断prefix

#define IDS_BOUNDED_RETRY 564 // 同一Jの明示保存再試行

#define IDS_BOUNDED_DISCARD 565 // 最新未完試行の明示削除

#define IDS_BOUNDED_UNSAVED 566 // 未保存の表示

#define IDC_BOUNDED_RETRY 1220 // 保存再試行command

#define IDC_BOUNDED_DISCARD 1221 // 未完削除command

#define IDS_BOUNDED_EXIT_FAILED 567 // 未保存Jの再保存/耐久detach/終了取消し

#define IDC_WHOLE_CREATE_MINISTRAL16GB 1032 // 明示Ministral16GB CPU評価

#define IDS_WHOLE_CREATE_MINISTRAL16GB 568 // 未受入の評価用途を示す操作名

#define IDS_WTP_MINISTRAL16GB 569 // 準備画面の固有モデル選択

#define IDC_TEXT_CONTEXT_CREATE 1033 // 保存済み原文の文章専用試作

#define IDC_TEXT_CONTEXT_PREPARE 1034 // 文章専用モデル準備

#define IDS_TEXT_CONTEXT_CREATE 570 // 明示文章専用操作

#define IDS_TEXT_CONTEXT_PREPARE 571 // 文章専用準備の説明

#define IDS_TEXT_CONTEXT_WAITING 572 // 通常原文保存barrier待ち

#define IDS_TEXT_CONTEXT_METADATA 573 // PCM由来metadata不足

#define IDS_TEXT_CONTEXT_PROTECTED 574 // 保護検査拒否

#define IDS_TEXT_CONTEXT_CANCELLED 575 // 未受付待ちの取消し

#define IDS_TEXT_CONTEXT_MISSING 576 // 文章専用モデル未準備・破損の案内

// ADR1344: API補正の明示確認画面

#define IDC_API_MODEL 1390

#define IDC_API_REFRESH 1391

#define IDC_API_METADATA 1392

#define IDS_API_CATALOG_WORKING 1393

#define IDS_API_CATALOG_SAVE_FAILED 1394

#define IDS_API_CATALOG_SOURCE 1395

#define IDS_API_PRICE_TIME 1396

#define IDS_API_MODEL_TIME 1397

#define IDS_API_PRICE_COLUMNS 1398

#define IDS_API_SEND_PREVIOUS 1399

#define IDS_API_SEND_CURRENT 1400

#define IDS_API_MODEL_AVAILABLE 1401

#define IDS_API_MODEL_INCOMPATIBLE 1402

#define IDS_API_MODEL_UNVERIFIED 1403

#define IDS_API_MODEL_PRICE_MISSING 1404

#define IDS_API_MODEL_UNAVAILABLE 1405

#define IDS_API_MODEL_SHUTDOWN 1406

#define IDS_API_SCOPE_UNKNOWN 1407

#define IDS_API_TIER_MISSING 1408

#define IDS_API_SHUTDOWN_DATE 1409

#define IDS_API_META_MISSING 1410

#define IDS_API_META_EXPIRED 1411

#define IDS_API_META_AUTH_DENIED 1412

#define IDS_API_META_PREVIOUS 1413

#define IDS_API_META_READY 1414

#define IDS_API_UNKNOWN_RESERVATION 1415

#define IDD_API_TEXT 1800 // API専用の固定識別子

#define IDC_API_TEXT_OPEN 1801 // API専用の固定識別子

#define IDC_API_PREVIEW 1802 // API専用の固定識別子

#define IDC_API_CANDIDATE 1803 // API専用の固定識別子

#define IDC_API_REPORT 1804 // API専用の固定識別子

#define IDC_API_KEY 1805 // API専用の固定識別子

#define IDC_API_PERSIST 1806 // API専用の固定識別子

#define IDC_API_SET_KEY 1807 // API専用の固定識別子

#define IDC_API_ERASE_KEY 1808 // API専用の固定識別子

#define IDC_API_KEY_STATE 1809 // API専用の固定識別子

#define IDC_API_BUDGET 1810 // API専用の固定識別子

#define IDC_API_PLAN 1811 // API専用の固定識別子

#define IDC_API_SEND 1812 // API専用の固定識別子

#define IDC_API_ADOPT 1813 // API専用の固定識別子

#define IDC_API_RETRY 1814 // API専用の固定識別子

#define IDC_API_ORIGINAL 1815 // API専用の固定識別子

#define IDC_API_CORRECTED 1816 // API専用の固定識別子

#define IDC_API_CANCEL 1817 // API専用の固定識別子

#define IDC_API_COPY 1818 // API専用の固定識別子

#define IDC_API_SAVE 1819 // API専用の固定識別子

#define IDS_API_OPEN 1850 // API利用者向けresource

#define IDS_API_READY 1851 // API利用者向けresource

#define IDS_API_ESTIMATE 1852 // API利用者向けresource

#define IDS_API_BUDGET 1853 // API利用者向けresource

#define IDS_API_WORKING 1854 // API利用者向けresource

#define IDS_API_BUSY 1855 // API利用者向けresource

#define IDS_API_FAILED 1856 // API利用者向けresource

#define IDS_API_KEY_READY 1857 // API利用者向けresource

#define IDS_API_KEY_MISSING 1858 // API利用者向けresource

#define IDS_API_SAVE_FAILED 1859 // API利用者向けresource

#define IDS_API_SAVED 1860 // API利用者向けresource

#define IDS_API_ADOPTED 1861 // API利用者向けresource

#define IDS_API_NOTICE 1862 // API利用者向けresource

#define IDS_API_STATE_PENDING 1900 // API状態の日本語resource

#define IDS_API_STATE_SENDING 1901 // API状態の日本語resource

#define IDS_API_STATE_READY 1902 // API状態の日本語resource

#define IDS_API_STATE_INCOMPLETE 1903 // API状態の日本語resource

#define IDS_API_STATE_FAILED 1904 // API状態の日本語resource

#define IDS_API_STATE_CANCELLED 1905 // API状態の日本語resource

#define IDS_API_STATE_INTERRUPTED 1906 // API状態の日本語resource

#define IDS_API_NOT_SENT 1907 // API状態の日本語resource

#define IDS_API_BILLING_UNKNOWN 1908 // API状態の日本語resource

#define IDS_API_USAGE_OBSERVED 1909 // API状態の日本語resource

#define IDS_API_USAGE_MISSING 1910 // API状態の日本語resource

#define IDS_API_INPUT_TOKENS 1911 // API状態の日本語resource

#define IDS_API_OUTPUT_TOKENS 1912 // API状態の日本語resource

#define IDS_API_CACHED_TOKENS 1913 // API状態の日本語resource

#define IDS_API_REASONING_TOKENS 1914 // API状態の日本語resource

#define IDS_API_CACHE_WRITES 1915 // API状態の日本語resource

#define IDS_API_COST_RANGE 1916 // API状態の日本語resource

#define IDS_API_ADOPTED_SAVED 1917 // API状態の日本語resource

#define IDC_API_DISCARD 1820 // 最新候補だけの明示破棄

#define IDS_API_PREPARING 1920 // API原文・予算の検証

#define IDS_API_AWAITING 1921 // 実送信と応答待ち

#define IDS_API_PERSISTING 1922 // 実保存段階

#define IDS_API_PROVENANCE_MODEL 1940 // 保存された補正出所の表示

#define IDS_API_REQUESTED_MODEL 1941 // 保存された補正出所の表示

#define IDS_API_ACTUAL_MODEL 1942 // 保存された補正出所の表示

#define IDS_API_MODEL_NOT_REPORTED 1943 // 保存された補正出所の表示

#define IDS_API_MODEL_NOT_VERIFIED 1944 // 保存された補正出所の表示

#define IDS_API_MODEL_MULTIPLE 1945 // 保存された補正出所の表示

#define IDS_API_PROVENANCE_ADOPTED 1946 // 保存された補正出所の表示

#define IDS_API_PROVENANCE_CANDIDATE 1947 // 保存された補正出所の表示

#define IDS_API_PROVENANCE_ORIGINAL 1948 // 保存された補正出所の表示

#define IDD_API_SETTINGS 1950 // 文書操作から独立した設定

#define IDC_API_SETTINGS_OPEN 1951 // 設定menuと文書内導線

#define IDC_API_TABS 1952 // 原文/改善案

#define IDC_API_SUMMARY 1953 // 対象と凍結見積

#define IDC_API_MORE 1954 // 追加操作menu

#define IDC_API_DETAILS 1955 // 診断/提供元条件

#define IDC_API_SETTINGS_APPLY 1956 // 一般設定をworker保存

#define IDS_API_SETTINGS_TITLE 1957 // 日本語標準設定

#define IDS_TABLE_NAME 1958 // 音声テーブル列

#define IDS_TABLE_DURATION 1959 // 音声テーブル列

#define IDS_TABLE_STATE 1960 // 音声テーブル列

#define IDS_TABLE_CORRECTION 1961 // 音声テーブル列

#define IDS_TABLE_MODEL 1962 // 音声テーブル列

#define IDS_API_SETUP_NEEDED 1963 // 未設定時の導線

#define IDS_API_COMPACT_NOTICE 1964 // 短い送信説明

#define IDS_API_ORIGINAL_TAB 1965 // 本文tab

#define IDS_API_CANDIDATE_TAB 1966 // 本文tab

#define IDS_API_IDLE 1967 // 未実行

#define IDS_API_SETTINGS_SAVED 1968 // 設定保存

#define IDS_API_PRICE_SIMPLE 1969 // 入力/出力の短い単価

#define IDS_API_MORE_COPY 1970 // 有限追加操作menu先頭

#define IDS_API_MORE_SAVE 1971 // 有限追加操作

#define IDS_API_MORE_RETRY 1972 // 有限追加操作

#define IDS_API_MORE_ORIGINAL 1973 // 有限追加操作

#define IDS_API_MORE_CORRECTED 1974 // 有限追加操作

#define IDS_API_MORE_DISCARD 1975 // 有限追加操作

#define IDS_API_MORE_DETAILS 1976 // 有限追加操作

#define IDS_API_NEXT_MODEL 1977 // 送信前だけの設定モデル

#define IDC_API_PRICES 1980 // 選択モデルの全単価table

#define IDS_API_RATE_RANGE 1981 // Standard区分

#define IDS_API_RATE_INPUT 1982 // 入力token

#define IDS_API_RATE_OUTPUT 1983 // 出力token

#define IDS_API_RATE_CACHE 1984 // cache入力

#define IDS_API_RATE_WRITE30 1985 // 30分cache書込

#define IDS_API_RATE_WRITE12 1986 // 12時間cache書込

#define IDS_API_RATE_SHORT 1987 // Short価格

#define IDS_API_RATE_LONG 1988 // Long価格

#define IDS_API_RATE_MISSING 1989 // 未掲載

#define IDS_API_RATE_SOURCE 1990 // 公式価格出典

#define IDS_API_RATE_POLICY 1991 // 実送信の価格区分

#define IDS_API_PARTIAL_APPLY 1992 // キー成功・設定保存失敗

#define IDS_API_HTTP_429 2010 // API専用の固定終端説明

#define IDS_API_HTTP_401 2011 // API専用の固定終端説明

#define IDS_API_HTTP_403 2012 // API専用の固定終端説明

#define IDS_API_HTTP_400 2013 // API専用の固定終端説明

#define IDS_API_HTTP_503 2014 // API専用の固定終端説明

#define IDS_API_HTTP_FAILED 2015 // API専用の固定終端説明

#define IDS_API_TRANSPORT_FAILED 2016 // API専用の固定終端説明

#define IDS_API_PREFLIGHT_FAILED 2017 // API専用の固定終端説明

#define IDS_API_EMPTY_GENERATION 2018 // API専用の固定終端説明

#define IDS_API_PARTIAL_GENERATION 2019 // API専用の固定終端説明

#define IDS_API_CANCELLED_STATUS 2020 // API専用の固定終端説明

#define IDS_API_PRIOR_RETAINED 2021 // API専用の固定終端説明

#define IDS_API_HTTP_DETAIL_RATE 2022 // API専用の固定終端説明

#define IDS_API_HTTP_DETAIL_CREDIT 2023 // API専用の固定終端説明

#define IDS_API_HTTP_DETAIL_ORG_SPEND 2024 // API専用の固定終端説明

#define IDS_API_HTTP_DETAIL_PROJECT_SPEND 2025 // API専用の固定終端説明

#define IDS_API_HTTP_DETAIL_ORG_USAGE 2026 // API専用の固定終端説明

#define IDS_API_HTTP_DETAIL_QUOTA 2027 // API専用の固定終端説明

#define IDS_API_HTTP_DETAIL_OVERLOADED 2028 // API専用の固定終端説明

#define IDS_API_ORIGINAL_SAVED 2030 // 原文表示選好の保存成功

#define IDS_API_CORRECTED_SHOWN 2031 // 保存済み改善文の表示成功

#define IDS_API_DISCARDED 2032 // 未採用候補破棄の保存成功

#define IDS_API_PROGRESS_TITLE 2040 // ADR1352の実進捗/話者/比較表示

#define IDS_API_UNITS_COMPLETE 2041 // ADR1352の実進捗/話者/比較表示

#define IDS_API_UNITS_RECEIVED 2042 // ADR1352の実進捗/話者/比較表示

#define IDS_API_PHASE_PREPARING 2043 // ADR1352の実進捗/話者/比較表示

#define IDS_API_PHASE_AUTHORIZING 2044 // ADR1352の実進捗/話者/比較表示

#define IDS_API_PHASE_RESERVING 2045 // ADR1352の実進捗/話者/比較表示

#define IDS_API_PHASE_AWAITING 2046 // ADR1352の実進捗/話者/比較表示

#define IDS_API_PHASE_RESPONSE 2047 // ADR1352の実進捗/話者/比較表示

#define IDS_API_PHASE_ADOPTING 2048 // ADR1352の実進捗/話者/比較表示

#define IDS_API_PHASE_CANCELLING 2049 // ADR1352の実進捗/話者/比較表示

#define IDS_API_ELAPSED 2050 // ADR1352の実進捗/話者/比較表示

#define IDS_API_SPEAKER_NOTICE 2051 // ADR1352の実進捗/話者/比較表示

#define IDS_API_NEUTRAL 2052 // ADR1352の実進捗/話者/比較表示

#define IDS_API_COMPARE 2053 // ADR1352の実進捗/話者/比較表示

#define IDS_API_COMPARE_ORIGINAL 2054 // ADR1352の実進捗/話者/比較表示

#define IDS_API_COMPARE_CORRECTED 2055 // ADR1352の実進捗/話者/比較表示

#define IDS_API_COMPARE_SCOPE 2056 // ADR1352の実進捗/話者/比較表示

#define IDC_CHAT_COMPARE 2057 // 原文比較の右RichEdit

#define IDC_COMPARE_LEFT_LABEL 2058 // 比較の左見出し

#define IDC_COMPARE_RIGHT_LABEL 2059 // 比較の右見出し

#define IDS_API_STEPS_COMPLETE 2060 // 応答区間と最終採用保存の完了工程数

// ADR1354: 統合設定と選択行だけの操作

#define IDD_SETTINGS 2100 // 統合設定のmodeless dialog

#define IDC_SETTINGS_OPEN 2101 // 設定の唯一のmenu入口

#define IDC_SETTINGS_TABS 2102 // 一般/ASR/話者/文章改善

#define IDC_SETTINGS_APPLY 2103 // 次job draftの明示適用

#define IDC_SETTINGS_UI 2104 // 未適用UI言語

#define IDC_SETTINGS_UI_LABEL 2105 // 言語欄のlabel

#define IDC_SETTINGS_BACKEND_LABEL 2106 // backend欄のlabel

#define IDC_SETTINGS_LANGUAGE_LABEL 2107 // 認識言語欄のlabel

#define IDC_SETTINGS_COUNT_LABEL 2108 // 任意人数欄のlabel

#define IDC_SETTINGS_METHOD 2109 // 話者方式の未適用選択

#define IDC_SETTINGS_METHOD_LABEL 2110 // 話者方式label

#define IDC_SETTINGS_NOTE 2111 // Apply/Closeの契約

#define IDS_SETTINGS_TITLE 2112 // 統合設定タイトル

#define IDS_SETTINGS_GENERAL 2113 // 一般tab

#define IDS_SETTINGS_ASR 2114 // 文字起こしtab

#define IDS_SETTINGS_SPEAKERS 2115 // 話者tab

#define IDS_SETTINGS_IMPROVEMENT 2116 // 文章改善tab

#define IDS_SETTINGS_UI 2117 // UI言語label

#define IDS_SETTINGS_METHOD 2118 // 話者方式label

#define IDS_SETTINGS_VAD 2119 // VAD選択

#define IDS_SETTINGS_APPLY 2120 // 明示適用

#define IDS_SETTINGS_CLOSE 2121 // 未適用破棄close

#define IDS_SETTINGS_NOTE 2122 // 次回受付/明示適用の説明

#define IDS_RENAME_MENU 2123 // 話者名を変更するmenu

#define IDD_RENAME 2124 // 選択fileの話者名dialog

#define IDC_API_NOTICE_LABEL 2130 // API page送信先説明

#define IDC_API_KEY_LABEL 2131 // API pageキーlabel

#define IDC_API_MODEL_LABEL 2132 // API pageモデルlabel

#define IDC_API_PRICES_LABEL 2133 // API page単価単位

#define IDR_MAIN_ACCEL 2140 // 標準file選択/保存のkeyboard導線

#define IDC_MAIN_SPLITTER 2150 // 左一覧/右本文の調整境界

#define IDS_MAIN_SPLITTER 2151 // 境界の標準accessibility目的名

#define IDS_TABLE_MODEL_UNVERIFIED 2160 // 実応答の既知IDを未確認

#define IDS_TABLE_MODEL_MULTIPLE 2161 // 実応答が区間間で相違

#define IDS_TABLE_IMPROVEMENT_PRESENT 2162 // 現在原文の保存済み改善あり

#define IDS_TABLE_IMPROVEMENT_ABSENT 2163 // 現在原文の保存済み改善なし

#define IDS_API_SPEAKER_STRUCTURE_FAILED 2170 // 話者保持応答の構造不整合

#define IDS_API_TURN_NOTICE 2171 // 発言IDだけからの表示対応

#define IDS_API_COMPARE_LEGACY 2172 // 元行IDのない旧採用の表示位置/未対応説明

// ADR1368: 音声APIの固定表示と同じ設定内のmetadata操作

#define IDS_AUDIO_MODEL 3400 // 音声API専用resource

#define IDS_AUDIO_PROFILE 3401 // 音声API専用resource

#define IDS_AUDIO_DEVICE 3402 // 音声API専用resource

#define IDS_AUDIO_NOTICE 3403 // 音声API専用resource

#define IDS_AUDIO_KEY_SHARED 3404 // 音声API専用resource

#define IDS_AUDIO_REFRESH 3405 // 音声API専用resource

#define IDS_AUDIO_PRICE_MISSING 3406 // 音声API専用resource

#define IDS_AUDIO_PRICE_STALE 3407 // 音声API専用resource

#define IDS_AUDIO_PREPARING 3408 // 音声API専用resource

#define IDS_AUDIO_INTENT 3409 // 音声API専用resource

#define IDS_AUDIO_AWAITING 3410 // 音声API専用resource

#define IDS_AUDIO_PREFIX_SAVE 3411 // 音声API専用resource

#define IDS_AUDIO_FINAL_SAVE 3412 // 音声API専用resource

#define IDS_AUDIO_DONE 3413 // 音声API専用resource

#define IDS_AUDIO_NO_SPEECH 3414 // 音声API専用resource

#define IDS_AUDIO_EMPTY 3415 // 音声API専用resource

#define IDS_AUDIO_CANCELLED 3416 // 音声API専用resource

#define IDS_AUDIO_INTERRUPTED 3417 // 音声API専用resource

#define IDS_AUDIO_AUTH 3418 // 音声API専用resource

#define IDS_AUDIO_FORBIDDEN 3419 // 音声API専用resource

#define IDS_AUDIO_MODEL_MISSING 3420 // 音声API専用resource

#define IDS_AUDIO_SIZE 3421 // 音声API専用resource

#define IDS_AUDIO_LIMIT 3422 // 音声API専用resource

#define IDS_AUDIO_SERVICE 3423 // 音声API専用resource

#define IDS_AUDIO_FAILED 3424 // 音声API専用resource

#define IDS_AUDIO_SAVE_FAILED 3425 // 音声API専用resource

#define IDS_AUDIO_PARTIAL 3426 // 音声API専用resource

#define IDS_AUDIO_ORIGIN 3427 // 音声API専用resource

#define IDS_AUDIO_TIME_BASIS 3428 // 音声API専用resource

#define IDS_AUDIO_SEND_DURATION 3429 // 音声API専用resource

#define IDS_AUDIO_ESTIMATE 3430 // 音声API専用resource

#define IDS_AUDIO_USAGE_UNKNOWN 3431 // 音声API専用resource

#define IDS_AUDIO_NOT_READY 3432 // 音声API専用resource

#define IDC_AUDIO_INFO 2160 // 送信先/料金/時刻basis

#define IDC_AUDIO_REFRESH 2161 // 明示metadata更新

#define IDC_AUDIO_KEYS 2162 // 既存共有キー設定へ移動

#define IDS_AUDIO_CANCELLING 3433 // 取消し待ち

#define IDS_AUDIO_SECONDS 3434 // 秒単位

#define IDS_AUDIO_PRICE_UNIT 3435 // 分単価

#define IDS_AUDIO_HUNDRED_MIN 3436 // 100分概算

#define IDS_AUDIO_ACTUAL_VERIFIED 3437 // 厳密実model

#define IDS_AUDIO_ACTUAL_UNVERIFIED 3438 // 未検証実model

#define IDS_AUDIO_USAGE_OBSERVED 3439 // 観測duration

#define IDS_AUDIO_STANDARD 3443 // 従来の話者境界優先

#define IDC_AUDIO_RESUME 2163 // 同じ行の保存済み完全prefixの続き

#define IDS_AUDIO_RESUME 3444 // 明示的な音声API再開

#define IDS_AUDIO_RESUME_UNCERTAIN 3445 // 当該未完窓だけの課金不明確認

#define IDS_AUDIO_CONTEXT_NOTICE 3446 // 長い文脈窓の本文対応制約

#define IDS_AUDIO_STANDARD_NOTICE 3447 // 従来話者境界の説明

#define IDS_AUDIO_RESUME_PREP_FAILED 3448 // 今回準備失敗、旧HTTPを流用しない

#define IDS_AUDIO_ATTEMPT 3449 // 操作単位の固定料金出所

#define IDS_AUDIO_REMAINING 3450 // 今回残量概算

#define IDC_SPEAKER_TEXT 2200 // 同じ行の明示PC内話者対応

#define IDC_SPEAKER_TEXT_SAVE 2201 // 明示話者付きTXT

#define IDS_SPEAKER_TEXT_MENU 3460 // 行操作

#define IDS_SPEAKER_TEXT_SAVE 3461 // 専用保存

#define IDS_SPEAKER_TEXT_NOTICE 3462 // 補助モデル/時刻制約

#define IDS_SPEAKER_TEXT_ACTIVE 3463 // 実窓処理中

#define IDS_SPEAKER_TEXT_DONE 3464 // 耐久採用済み

#define IDS_SPEAKER_TEXT_FAILED 3465 // 原文/前採用を保持

#define IDS_SPEAKER_TEXT_CANCELLED 3466 // PC内処理だけの取消し

#define IDS_SPEAKER_TEXT_PREPARING 3467 // 必要モデルの準備

#define IDS_TRANSCRIPTION_LOADING 3470 // 起動復元の受付barrier
#define IDS_TRANSCRIPTION_LOAD_FAILED 3471 // 原file保護と明示復旧
#define IDS_TRANSCRIPTION_SAVING 3472 // local保存ACK待ち
#define IDS_TRANSCRIPTION_SAVED 3473 // localの耐久保存成功
#define IDS_TRANSCRIPTION_SAVE_FAILED 3474 // 旧値/dirty保持と明示retry

#define IDS_TRANSCRIPTION_LOAD_BUSY 3475 // writer lease
#define IDS_TRANSCRIPTION_LOAD_IO 3476 // access/location
#define IDS_TRANSCRIPTION_EXIT_FAILED 3477 // close/save failure

#define IDS_PARAKEET_API_OFFER 3480 // 新しい原文の明示全文送信案内
#define IDS_PARAKEET_API_IMPROVE 3481 // 同意button
#define IDS_PARAKEET_API_LATER 3482 // 今回だけ辞退
#define IDS_PARAKEET_API_SETUP 3483 // 開いた同sessionの続行説明
#define IDS_PARAKEET_API_EXPIRED 3484 // 有限意図失効後の明示再実行

// ADR1378: 既存設定menu/一般tabの明示更新
#define IDC_UPDATE_CHECK 2250 // 手動確認
#define IDC_UPDATE_INSTALL 2251 // 明示取得/終了更新
#define IDC_UPDATE_CANCEL 2252 // 更新だけを取消し
#define IDC_SETTINGS_UPDATE 2253 // 起動確認の未適用選好
#define IDC_SETTINGS_UPDATE_NOTE 2254 // 固定通信説明
#define IDS_UPDATE_STARTUP 3490 // 起動一回ON/OFF
#define IDS_UPDATE_PRIVACY 3491 // 確認/取得/CA照会説明
#define IDS_UPDATE_CHECKING 3492 // 確認中
#define IDS_UPDATE_AVAILABLE 3493 // 新版あり
#define IDS_UPDATE_CURRENT 3494 // 有効なfeed確認済み
#define IDS_UPDATE_UNAVAILABLE 3495 // 未公開/offline等
#define IDS_UPDATE_DOWNLOADING 3496 // 実取得/検証中
#define IDS_UPDATE_READY 3497 // 準備済み
#define IDS_UPDATE_FAILED 3498 // 更新だけの失敗
#define IDS_UPDATE_CANCELLED 3499 // 更新だけの取消し
#define IDS_UPDATE_GET_PROMPT 3500 // 明示取得同意
#define IDS_UPDATE_CLOSE_PROMPT 3501 // 明示終了同意
#define IDS_UPDATE_BUSY 3502 // 実行/保存を保持
#define IDS_UPDATE_GET 3503 // menu取得表示
#define IDS_UPDATE_INSTALL 3504 // menu終了更新表示
#define IDS_UPDATE_PORTABLE 3505 // outを上書きしない通常導入

// ADR1380: 常設statusの短い固定説明
#define IDS_SHORT_READY 3520 // 詳細とは独立した短い状態
#define IDS_SHORT_QUEUED 3521 // 詳細とは独立した短い状態
#define IDS_SHORT_DONE 3522 // 詳細とは独立した短い状態
#define IDS_SHORT_WARNING 3523 // 詳細とは独立した短い状態
#define IDS_SHORT_NO_TEXT 3524 // 詳細とは独立した短い状態
#define IDS_SHORT_CANCELLING 3525 // 詳細とは独立した短い状態
#define IDS_SHORT_CANCELLED 3526 // 詳細とは独立した短い状態
#define IDS_SHORT_INTERRUPTED 3527 // 詳細とは独立した短い状態
#define IDS_SHORT_STOPPING 3528 // 詳細とは独立した短い状態
#define IDS_SHORT_VALIDATING 3529 // 詳細とは独立した短い状態
#define IDS_SHORT_DECODING 3530 // 詳細とは独立した短い状態
#define IDS_SHORT_LOADING 3531 // 詳細とは独立した短い状態
#define IDS_SHORT_TRANSCRIBING 3532 // 詳細とは独立した短い状態
#define IDS_SHORT_DIARIZING 3533 // 詳細とは独立した短い状態
#define IDS_SHORT_COMBINING 3534 // 詳細とは独立した短い状態
#define IDS_SHORT_SAVING 3535 // 詳細とは独立した短い状態
#define IDS_SHORT_SAVED 3536 // 詳細とは独立した短い状態
#define IDS_SHORT_PREPARING 3537 // 詳細とは独立した短い状態
#define IDS_SHORT_GENERATING 3538 // 詳細とは独立した短い状態
#define IDS_SHORT_FAILED 3539 // 詳細とは独立した短い状態
#define IDS_SHORT_ATTENTION 3540 // 詳細とは独立した短い状態
#define IDS_SHORT_AWAITING 3541 // 詳細とは独立した短い状態
#define IDS_SHORT_HISTORY_LOADING 3542 // 詳細とは独立した短い状態
#define IDS_SHORT_HISTORY_SAVING 3543 // 詳細とは独立した短い状態
#define IDS_SHORT_HISTORY_SAVED 3544 // 詳細とは独立した短い状態
#define IDS_SHORT_HISTORY_UNSAVED 3545 // 詳細とは独立した短い状態
#define IDS_SHORT_IMPROVED 3546 // 詳細とは独立した短い状態
#define IDS_SHORT_NOT_IMPROVED 3547 // 詳細とは独立した短い状態
#define IDS_SHORT_UPDATE_CHECKING 3548 // 詳細とは独立した短い状態
#define IDS_SHORT_UPDATE_AVAILABLE 3549 // 詳細とは独立した短い状態
#define IDS_SHORT_UPDATE_CURRENT 3550 // 詳細とは独立した短い状態
#define IDS_SHORT_UPDATE_UNCONFIRMED 3551 // 詳細とは独立した短い状態
#define IDS_SHORT_UPDATE_DOWNLOADING 3552 // 詳細とは独立した短い状態
#define IDS_SHORT_UPDATE_READY 3553 // 詳細とは独立した短い状態
#define IDS_SHORT_UPDATE_FAILED 3554 // 詳細とは独立した短い状態
#define IDS_SHORT_API 3560 // API文章改善の用途
#define IDS_SHORT_AUDIO 3561 // 音声APIの用途
#define IDS_SHORT_REMOVED 3562 // 履歴削除成功

#define IDS_MESSAGE_OK 3580 // 共通案内の標準OK
#define IDS_MESSAGE_YES 3581 // 共通確認の同意
#define IDS_MESSAGE_NO 3582 // 共通確認の辞退
#define IDS_MESSAGE_CANCEL 3583 // 共通確認の取消し

// ADR1400: API設定のmetadata取得状態だけを表示します
#define IDC_API_CATALOG_PROGRESS 3590 // 取得中だけの標準marquee
#define IDS_API_CATALOG_READY 3591 // 取得終端の案内
#define IDS_API_CATALOG_FAILED 3592 // 取得失敗の案内
#define IDD_API_CATALOG_PROGRESS 3593 // metadata取得中のowned dialog
#define IDC_API_CATALOG_MESSAGE 3594 // 取得中の固定説明
#define IDS_API_CATALOG_CANCELLED 3595 // metadata取得取消しの案内
#define IDS_API_META_FAILED 3596 // 前回成功値のないmetadata取得失敗
#define IDS_API_META_KEY_MISSING 3597 // 認証GET未実行と候補選択の区別
