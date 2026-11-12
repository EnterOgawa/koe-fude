---
language: ja
tags:
  - audio
  - automatic-speech-recognition
license: apache-2.0
---

# Kotoba-Whisper: kotoba-whisper-v2.0 for Whisper cpp
This repository contains the model weights for [kotoba-tech/kotoba-whisper-v2.0](https://huggingface.co/kotoba-tech/kotoba-whisper-v2.0)
converted to [GGML](https://github.com/ggerganov/ggml) format. GGML is the weight format expected by C/C++ packages 
such as [Whisper.cpp](https://github.com/ggerganov/whisper.cpp), for which we provide an example below.

## Usage
Kotoba-Whisper can be run with the [Whisper.cpp](https://github.com/ggerganov/whisper.cpp) package with the original 
sequential long-form transcription algorithm.

Steps for getting started:

1. Clone the Whisper.cpp repository:
```
git clone https://github.com/ggerganov/whisper.cpp.git
cd whisper.cpp
```
2. Download the GGML weights for `kotoba-tech/kotoba-whisper-v2.0`:

```bash
wget https://huggingface.co/kotoba-tech/kotoba-whisper-v2.0-ggml/resolve/main/ggml-kotoba-whisper-v2.0.bin -P ./models
```

3. Run inference using the provided sample audio:

```bash
wget https://huggingface.co/kotoba-tech/kotoba-whisper-v1.0-ggml/resolve/main/sample_ja_speech.wav
make -j && ./main -m models/ggml-kotoba-whisper-v2.0.bin -l ja -f sample_ja_speech.wav --output-file transcription --output-json
```

Note that it runs only with 16-bit WAV files, so make sure to convert your input before running the tool. For example, you can use ffmpeg like this:
```
ffmpeg -i input.mp3 -ar 16000 -ac 1 -c:a pcm_s16le output.wav
```

### Benchmark
We measure the inference speed of different kotoba-whisper-v2.0 implementations with four different Japanese speech audio on MacBook Pro with the following spec:
- Apple M2 Pro
- 32GB
- 14-inch, 2023
- OS Sonoma Version 14.4.1 (23E224)

| audio file | audio duration (min)| [whisper.cpp](https://huggingface.co/kotoba-tech/kotoba-whisper-v2.0-ggml) (sec) | [faster-whisper](https://huggingface.co/kotoba-tech/kotoba-whisper-v2.0-faster) (sec)| [hf pipeline](https://huggingface.co/kotoba-tech/kotoba-whisper-v2.0) (sec)
|--------|------|-----|------|-----|
|audio 1 | 50.3 | 581 | 2601 | 807 |
|audio 2 | 5.6  | 41  | 73   | 61  |
|audio 3 | 4.9  | 30  | 141  | 54  |
|audio 4 | 5.6  | 35  | 126  | 69  |

Scripts to re-run the experiment can be found bellow:
* [whisper.cpp](https://huggingface.co/kotoba-tech/kotoba-whisper-v1.0-ggml/blob/main/benchmark.sh)
* [faster-whisper](https://huggingface.co/kotoba-tech/kotoba-whisper-v1.0-faster/blob/main/benchmark.sh)
* [hf pipeline](https://huggingface.co/kotoba-tech/kotoba-whisper-v1.0/blob/main/benchmark.sh)
* 
Also, currently whisper.cpp and faster-whisper support the [sequential long-form decoding](https://huggingface.co/distil-whisper/distil-large-v3#sequential-long-form),
and only Huggingface pipeline supports the [chunked long-form decoding](https://huggingface.co/distil-whisper/distil-large-v3#chunked-long-form), which we empirically
 found better than the sequnential long-form decoding.

### Quantized Model
To use the quantized model, download the quantized GGML weights:

```bash
wget https://huggingface.co/kotoba-tech/kotoba-whisper-v2.0-ggml/resolve/main/ggml-kotoba-whisper-v2.0-q5_0.bin -P ./models
```

Run inference on the sample audio:
```bash
make -j && ./main -m models/ggml-kotoba-whisper-v2.0-q5_0.bin -l ja -f sample_ja_speech.wav --output-file transcription.quantized --output-json
```

Note that the benchmark results are almost identical to the raw non-quantized model weight.

### Conversion details
The original model was converted with the following command:

```
# clone OpenAI whisper and whisper.cpp
git clone https://github.com/openai/whisper
git clone https://github.com/ggerganov/whisper.cpp

# get the models
cd whisper.cpp/models
git clone https://huggingface.co/kotoba-tech/kotoba-whisper-v2.0

# convert to ggml
python3 ./convert-h5-to-ggml.py ./kotoba-whisper-v2.0/ ../../whisper .
mv ggml-model.bin ggml-kotoba-whisper-v2.0

# quantize ggml model
cd ../
make quantize
./quantize models/ggml-kotoba-whisper-v2.0.bin models/ggml-kotoba-whisper-v2.0-q5_0.bin q5_0
```

## Model Details

For more information about the kotoba-whisper-v2.0, refer to the original [model card](https://huggingface.co/kotoba-tech/kotoba-whisper-v2.0).
