# 固定sherpaと同じ前処理のsourceだけを静的linkし、build中の取得とPython依存を避ける。
set(ctc_fbank_root "${CMAKE_CURRENT_SOURCE_DIR}/third_party/kaldi-native-fbank")
set(ctc_fft_root "${CMAKE_CURRENT_SOURCE_DIR}/third_party/kissfft")
if(NOT EXISTS "${ctc_fbank_root}/kaldi-native-fbank/csrc/online-feature.cc" OR
   NOT EXISTS "${ctc_fft_root}/kiss_fft.c")
    message(FATAL_ERROR "Run scripts/setup.py --dependencies to prepare fixed CTC feature sources")
endif()
add_library(koefude_ctc_fft STATIC
    "${ctc_fft_root}/kiss_fft.c"
    "${ctc_fft_root}/kiss_fftr.c")
target_include_directories(koefude_ctc_fft SYSTEM PUBLIC "${ctc_fft_root}")
target_compile_options(koefude_ctc_fft PRIVATE /guard:cf)

set(ctc_fbank_sources
    feature-fbank.cc feature-functions.cc feature-mfcc.cc feature-raw-audio-samples.cc
    feature-window.cc istft.cc kaldi-math.cc mel-computations.cc online-feature.cc
    rfft.cc stft.cc whisper-feature.cc)
list(TRANSFORM ctc_fbank_sources PREPEND "${ctc_fbank_root}/kaldi-native-fbank/csrc/")
add_library(koefude_ctc_fbank STATIC ${ctc_fbank_sources})
target_include_directories(koefude_ctc_fbank SYSTEM PUBLIC "${ctc_fbank_root}")
target_compile_options(koefude_ctc_fbank PRIVATE /guard:cf /utf-8)
target_link_libraries(koefude_ctc_fbank PRIVATE koefude_ctc_fft)
