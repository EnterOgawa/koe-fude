# 製品ソースのビルド

KoeFude 1.0.0 / 2026/11/13 / Shunji Ogawa / MIT License

公開ソースはWindows用アプリのビルドに必要な製品ソース・固定依存の準備・利用案内・ライセンスに絞っています。
研究用ターゲット、評価音声、モデル本体、開発履歴、署名鍵を含みません。

## 必要な環境

- Windows 11 x64
- Visual Studio 2022のMSVC x64 C++ toolchainとWindows SDK
- CMake 3.25以上、Ninja
- 開発用Python 3.12以上
- GPU版をビルドする場合はVulkan SDKとglslc

MSVC 14.44.35207、Windows SDK 10.0.26100.0を基準にしています。
MSVCのx64 Native Tools環境で次を実行してください。利用する端末で `cmake`、`ninja`、`python` が利用可能であることを確認します。
アプリを使うPCにはPythonやビルドツールは不要です。

## CPU版

```powershell
python scripts/setup.py --cpu
cmake -S . -B build/cpu-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DKOEFUDE_CUDA=OFF -DKOEFUDE_VULKAN=OFF
cmake --build build/cpu-release --target koefude --parallel 4
```

`build/cpu-release/bin/KoeFude.exe` と同じフォルダーの3 DLLを一式で使います。
Debug版は別フォルダーを使い、`-DCMAKE_BUILD_TYPE=Debug` を指定します。
初回の依存準備は接続が必要です。モデルはアプリの準備画面から必要分だけ取得します。

## CUDA / Vulkan版

Vulkan SDKの環境を設定し、固定GPU runtimeを含む12依存を準備します。

```powershell
python scripts/setup.py
cmake -S . -B build/gpu-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DKOEFUDE_CUDA=ON -DKOEFUDE_VULKAN=ON
cmake --build build/gpu-release --target koefude --parallel 4
```

`build/gpu-release/bin` にアプリと固定runtime18 DLLを出力します。対応するNVIDIA driverが必要です。
Vulkan loaderはOSから任意読込みし、ビルドしたアプリの起動・CPU処理には必須としません。driverやloaderをこのソースから取得しません。
CUDAとVulkanは個別にOFFへできます。既定は両方ONです。

## 固定依存と再現条件

`dependencies.lock.json` のURL・revision・サイズ・SHA-256を確認し、取得物を検証してから展開します。未知の版へ自動更新しません。既存CMakeの厳密pinが参照する既知6ファイルだけ、原LFと変換後CRLFの両SHA-256を確認して改行を固定形へそろえます。
検証済みの同一ソース環境がある場合は `python scripts/setup.py --cpu --cache-root <source-root>` のように指定でき、その場合は通信せずキャッシュを照合します。
依存とモデルの準備後、通常のローカル文字起こしはオフラインで行えます。

製品版はCMakeの `project` と生成したversion resourceで一元管理しています。C++20 / Unicode / x64 / static CRTを使用し、自作targetの `/W4 /WX` とASLR・DEP・CFGを保持します。
自分でビルドしたEXEは未署名です。公式配布物の署名を引き継ぐことはありません。
製品用CMakeには研究・試験ターゲットを含めていません。ビルド成功は音声認識・話者精度や全GPU/実環境の受入を意味しません。

明示された補助runtimeの検証・遅延読込みslotは保持していますが、補助DLLやその研究用producerは同梱しません。
通常のParakeet/Whisper文字起こしと任意OpenAI APIは、これらの補助DLLを必要としません。

第三者の条件と対応sourceは [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) と [LICENSE_INVENTORY.md](../LICENSE_INVENTORY.md) を参照してください。
