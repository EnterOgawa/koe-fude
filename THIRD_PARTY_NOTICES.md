# 製品の第三者通知

KoeFude 1.0.0 / 2026/11/10 / Shunji Ogawa。原条件と既存固定runtimeの条件を継承します。
自作部分はMIT License、第三者部分は各原文の条件に従います。
配布バイナリのfileごとのhash・componentは`PACKAGE.json`、帰属と原文は
[LICENSE_INVENTORY.md](LICENSE_INVENTORY.md)と`licenses/`で確認できます。
配布バイナリにはモデルweight、研究worker/DLL、開発用sourceを同梱しません。このリポジトリには製品のビルドに必要なソースと固定依存の準備を含めます。取得可能な通常モデル11 artifactの帰属を維持します。

## アプリに組み込むcodeとruntime

whisper.cpp/ggmlはMIT、kaldi-native-fbankはApache-2.0、KISS FFTはBSD-3-Clauseです。
Eigen 5.0.1はMPL-2.0を主条件とし、同梱の追加条件も維持します。
配布バイナリには未改変の対応sourceを`source/eigen-5.0.1.tar.gz`へ添えています。
ソースビルド時は`dependencies.lock.json`の固定URL・size・SHA-256により同じEigen 5.0.1 sourceを取得します。
sherpa-onnxはApache-2.0、ONNX RuntimeはMITです。
sherpaに由来するkaldi-decoder、kaldifst、OpenFst、simple-sentencepiece、nlohmann/json、
hclust-cpp/fastclusterの原文、ONNX Runtimeの推移依存noticeも添えています。

CUDA Runtime 12.8.90、cuBLAS 12.8.4.1、cuFFT 11.3.3.83、cuDNN 9.10.2.21、NVRTC 12.8.93の
Windows runtimeをGPU配布バイナリへ固定hashで同梱します。これらはNVIDIAの個別条件に従い、MITへ変更しません。
このアプリから利用するruntimeであり、SDKや開発toolを単独で再配布するものではありません。
NVIDIAの著作権・原EULA・cuDNN内の第三者条件を維持します。
NVRTCは本体/builtinsの同じ公式archive一組だけで、原CUDA EULAをlicenses/へ添えます。
SDK header・static library・代替NVRTC DLLは含めません。

Visual Studio 2022の正規VC Redistから、MSVCP140/MSVCP140_1/VCRUNTIME140/VCRUNTIME140_1を
未改変でapp-local同梱します。Microsoftのruntime条件、Visual Studioの配布条件と再配布一覧を
licenses/msvc/へ添えています。これらもKoeFudeのMITとは別の条件です。

WindowsのMedia Foundation、WinHTTP、tar.exe、oleacc.dll、任意のVulkan loader、GPU driver、
MSVC/Windows SDKはOS又は開発環境のcomponentで、配布バイナリやソースリポジトリへ同梱しません。
研究モデル、Python、会話音声・注釈、比較probeは同梱しません。

## アプリから取得するモデル

モデルは用途・条件・通信先を表示した明示操作で必要分だけ取得します。
取得後は固定size/SHA-256を検証します。製品のモデル定義はアプリに組み込み、帰属と固定revisionを同梱一覧へ添えます。

Whisper/OpenAI由来はMIT、Kotoba-WhisperはApache-2.0、Silero VADはMIT、
pyannoteの採用ONNX変換artifactは同梱原文のMIT、CAM++はApache-2.0です。
日本語Parakeetの採用artifactはNVIDIA NeMo teamに由来するCC BY 4.0です。
INT8/tokenはsherpa配布、FP32 graph/外部weightはOpenVoiceOS変換に由来します。
FP32 graphにはKoeFudeがsherpa用metadata 338byteを追加し、weightは変更していません。
原著作者・固定revision・変換元へのリンクは対応一覧とPACKAGE.jsonのモデル帰属に保持しています。
配布元・著作者がKoeFudeを推奨しているという意味ではありません。

モデルweightを含まないZIPにも、取得可能な製品モデルの原license・model cardを添えます。
第三者のアクセス条件やgated modelの同意を回避しません。

ソースビルドで使う固定依存12件のrevision・取得元・size・SHA-256は`dependencies.lock.json`を参照してください。
