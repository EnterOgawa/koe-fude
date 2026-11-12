# 配布file・モデルとライセンスの対応

KoeFude 1.0.0 / 2026/11/10 / Shunji Ogawa。原条件確認: 2026/09/27、既存固定runtimeの条件を継承。
自作部分だけがMIT。モデルweightはZIPに含みません。
配布バイナリの各fileのsize/hashとcomponentはPACKAGE.json、個別条件は下記の原文を参照してください。

| Component | 条件 | 原文 |
|---|---|---|
| koefude | MIT | [LICENSE](LICENSE) |
| whisper.cpp | MIT | [whisper-cpp-LICENSE.txt](licenses/whisper-cpp-LICENSE.txt) |
| kaldi-native-fbank | Apache-2.0 | [LICENSE](licenses/kaldi-native-fbank/LICENSE) |
| kissfft | BSD-3-Clause | [kissfft-COPYING.txt](licenses/kissfft-COPYING.txt), [kissfft-BSD-3-Clause.txt](licenses/kissfft-BSD-3-Clause.txt) |
| eigen | MPL-2.0 and bundled conditions | [COPYING.README](licenses/eigen-5.0.1/COPYING.README), [COPYING.MPL2](licenses/eigen-5.0.1/COPYING.MPL2), [COPYING.MINPACK](licenses/eigen-5.0.1/COPYING.MINPACK), [COPYING.BSD](licenses/eigen-5.0.1/COPYING.BSD), [COPYING.APACHE](licenses/eigen-5.0.1/COPYING.APACHE) |
| sherpa-onnx | Apache-2.0 | [sherpa-LICENSE.txt](licenses/sherpa-LICENSE.txt) |
| sherpa-bundled | Apache-2.0 / MIT / BSD | [LICENSE](licenses/kaldi-decoder/LICENSE), [LICENSE](licenses/kaldifst/LICENSE), [COPYING](licenses/openfst/COPYING), [LICENSE](licenses/simple-sentencepiece/LICENSE), [LICENSE.MIT](licenses/nlohmann-json/LICENSE.MIT), [LICENSE](licenses/hclust-cpp/LICENSE) |
| onnxruntime | MIT and bundled conditions | [onnxruntime-LICENSE.txt](licenses/onnxruntime-LICENSE.txt), [onnxruntime-ThirdPartyNotices.txt](licenses/onnxruntime-ThirdPartyNotices.txt) |
| nvidia-cuda_cudart | NVIDIA CUDA EULA | [nvidia-cuda_cudart-12.8.90.txt](licenses/nvidia-cuda_cudart-12.8.90.txt) |
| nvidia-libcublas | NVIDIA CUDA EULA | [nvidia-libcublas-12.8.4.1.txt](licenses/nvidia-libcublas-12.8.4.1.txt) |
| nvidia-libcufft | NVIDIA CUDA EULA | [nvidia-libcufft-11.3.3.83.txt](licenses/nvidia-libcufft-11.3.3.83.txt) |
| nvidia-cuda_nvrtc | NVIDIA CUDA EULA | [nvidia-cuda_nvrtc-12.8.93.txt](licenses/nvidia-cuda_nvrtc-12.8.93.txt) |
| nvidia-cudnn | NVIDIA cuDNN EULA and bundled conditions | [nvidia-cudnn-9.10.2.21.txt](licenses/nvidia-cudnn-9.10.2.21.txt) |
| whisper-model | MIT | [whisper-model-LICENSE.txt](licenses/whisper-model-LICENSE.txt) |
| pyannote-model | MIT | [pyannote-segmentation-LICENSE.txt](licenses/pyannote-segmentation-LICENSE.txt) |
| campplus-model | Apache-2.0 | [LICENSE](licenses/3d-speaker/LICENSE) |
| silero-model | MIT | [silero-vad-LICENSE.txt](licenses/silero-vad-LICENSE.txt) |
| kotoba-model | Apache-2.0 | [kotoba-Apache-2.0.txt](licenses/kotoba-Apache-2.0.txt), [kotoba-ggml-model-card.md](licenses/kotoba-ggml-model-card.md) |
| parakeet-model | CC-BY-4.0 | [CC-BY-4.0.txt](licenses/CC-BY-4.0.txt) |
| msvc-runtime | Microsoft Software License Terms | [vc-runtime.docx](licenses/msvc/vc-runtime.docx), [visual-studio-community.docx](licenses/msvc/visual-studio-community.docx), [Redist.txt](licenses/msvc/Redist.txt) |

## モデルの帰属と取得元

| 製品artifact | Component | 固定revision | 著作者・変換元 |
|---|---|---|---|
| whisper-large-v3-turbo-q8 | whisper-model | 5359861c739e955e79d9a303bcbc70fb988958b1 | [出典](https://huggingface.co/openai/whisper-large-v3-turbo) / [配布](https://huggingface.co/ggerganov/whisper.cpp/resolve/5359861c739e955e79d9a303bcbc70fb988958b1/ggml-large-v3-turbo-q8_0.bin) |
| pyannote-segmentation-3-fp32 | pyannote-model | archive-sha256-24615ee884c897d9d2ba09bb4d30da6bb1b15e685065962db5b02e76e4996488 | [出典](https://huggingface.co/pyannote/segmentation-3.0) / [配布](https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-segmentation-models/sherpa-onnx-pyannote-segmentation-3-0.tar.bz2) |
| campplus-zh-en-common-advanced | campplus-model | github-release-asset-198893102 | [出典](https://modelscope.cn/models/iic/speech_campplus_sv_zh_en_16k-common_advanced) / [配布](https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-recongition-models/3dspeaker_speech_campplus_sv_zh_en_16k-common_advanced.onnx) |
| whisper-large-v3-fp16 | whisper-model | 5359861c739e955e79d9a303bcbc70fb988958b1 | [出典](https://huggingface.co/openai/whisper-large-v3) / [配布](https://huggingface.co/ggerganov/whisper.cpp/resolve/5359861c739e955e79d9a303bcbc70fb988958b1/ggml-large-v3.bin) |
| silero-vad-6.2.0 | silero-model | 9ffd54a1e1ee413ddf265af9913beaf518d1639b | [出典](https://github.com/snakers4/silero-vad) / [配布](https://huggingface.co/ggml-org/whisper-vad/resolve/9ffd54a1e1ee413ddf265af9913beaf518d1639b/ggml-silero-v6.2.0.bin) |
| kotoba-whisper-v2-fp16 | kotoba-model | e3a0cf6a62b95911703cfb97d819292e058f12c3 | [出典](https://huggingface.co/kotoba-tech/kotoba-whisper-v2.0/tree/7eb575277d18909a4af8a24e3ae8cce2e99794ae) / [配布](https://huggingface.co/kotoba-tech/kotoba-whisper-v2.0-ggml/resolve/e3a0cf6a62b95911703cfb97d819292e058f12c3/ggml-kotoba-whisper-v2.0.bin) |
| kotoba-whisper-v2-q5 | kotoba-model | e3a0cf6a62b95911703cfb97d819292e058f12c3 | [出典](https://huggingface.co/kotoba-tech/kotoba-whisper-v2.0/tree/7eb575277d18909a4af8a24e3ae8cce2e99794ae) / [配布](https://huggingface.co/kotoba-tech/kotoba-whisper-v2.0-ggml/resolve/e3a0cf6a62b95911703cfb97d819292e058f12c3/ggml-kotoba-whisper-v2.0-q5_0.bin) |
| parakeet-ja-ctc-int8 | parakeet-model | github-release-asset-271243108 | [出典](https://huggingface.co/nvidia/parakeet-tdt_ctc-0.6b-ja/tree/44edb27eea9317daf89333e75eb830db4b1cc298) / [配布](https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-nemo-parakeet-tdt_ctc-0.6b-ja-35000-int8.tar.bz2) |
| parakeet-ja-tokens | parakeet-model | github-release-asset-271243108 | [出典](https://huggingface.co/nvidia/parakeet-tdt_ctc-0.6b-ja/tree/44edb27eea9317daf89333e75eb830db4b1cc298) / [配布](https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-nemo-parakeet-tdt_ctc-0.6b-ja-35000-int8.tar.bz2) |
| parakeet-ja-ctc-fp32 | parakeet-model | 4353e7b9e2e9ebdd35e85b8140c7351d03c2219c | [出典](https://huggingface.co/OpenVoiceOS/nvidia-parakeet-tdt_ctc-0.6b-ja-onnx/tree/4353e7b9e2e9ebdd35e85b8140c7351d03c2219c) / [配布](https://huggingface.co/OpenVoiceOS/nvidia-parakeet-tdt_ctc-0.6b-ja-onnx/resolve/4353e7b9e2e9ebdd35e85b8140c7351d03c2219c/model.onnx) |
| parakeet-ja-ctc-fp32-data | parakeet-model | 4353e7b9e2e9ebdd35e85b8140c7351d03c2219c | [出典](https://huggingface.co/OpenVoiceOS/nvidia-parakeet-tdt_ctc-0.6b-ja-onnx/tree/4353e7b9e2e9ebdd35e85b8140c7351d03c2219c) / [配布](https://huggingface.co/OpenVoiceOS/nvidia-parakeet-tdt_ctc-0.6b-ja-onnx/resolve/4353e7b9e2e9ebdd35e85b8140c7351d03c2219c/model.onnx_data) |

ParakeetはNVIDIA NeMo team、FP32 ONNX変換はOpenVoiceOS、INT8/token配布はsherpa。
FP32 graphへsherpa用metadata 338byteを追加し、weightは未変更です。
配布バイナリにはEigenの未改変対応sourceをsource/eigen-5.0.1.tar.gzに添付します。ソースビルド用の同一sourceはdependencies.lock.jsonの固定URL・size・SHA-256を用いてscripts/setup.pyが取得します。
DLLを開発SDKとして単独再配布せず、原EULAと著作権を維持してください。
