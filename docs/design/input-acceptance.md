# 入力形式と制限

KoeFude 1.0.0はローカルのM4A・WAV・MP3・FLACファイルを受け付けます。
M4A内のAAC-LCを優先しています。拡張子が同じでも、すべてのコーデックや破損ファイルの処理を保証するものではありません。

| 入力 | 対応範囲 | 主な範囲外・制限 |
|---|---|---|
| M4A / AAC-LC | 非DRM、mono/stereo。44.1/48kHzを中心に確認 | DRM、外部参照、複雑な構成、圧縮内部の完全な破損検知は保証しません |
| WAV / PCM16 | RIFF/WAVE、16bit mono/stereo、8～192kHz | RF64、圧縮WAV、float/24bit/extensibleは範囲外 |
| MP3 / Layer III | MPEG-1/2、mono/stereo、16/22.05/24/32/44.1/48kHz、CBR/VBR | MPEG-2.5、Layer I/II、free-format、APEv2等は範囲外 |
| native FLAC | 16/24bit mono/stereo、8～192kHz、既知の正の総sample数 | Ogg FLAC、未知総数は範囲外。巨大metadataやframe内部破損の検知に制限があります |
| M4A / ALAC | 単一自己包含音声track、16/24bit mono/stereo、8～48kHz、既知総数 | edit/fragmented/外部参照/複数track等は範囲外 |
| M4A / HE-AAC | 明示v1 mono/stereo・v2 stereo、復号後44.1/48kHz、単一自己包含track、editなし | implicit SBR/PSやその他のrate/channelは対応を約束しません |

音声はMedia Foundationで16kHz monoへ変換します。原ファイルは変更しません。
固定の録音時間上限は設けず、区間ごとの処理と一時領域を使います。空き容量・利用可能メモリ・整数の表現限界で停止する場合があります。
ALAC/FLACの8,001Hzには形式間のPCM差が残り、すべてのrateやWindows環境を検証済みとは扱いません。

URL、UNC、device path、フォルダー、playlistは受け付けません。通常のローカルファイルを使ってください。
クラウド同期専用のプレースホルダー等、読み取りで同期通信が生じるファイルは先にローカルへ保存してください。
不正・未対応入力の全検知を保証せず、デコード失敗と無音・認識結果なしを別の状態として表示します。
