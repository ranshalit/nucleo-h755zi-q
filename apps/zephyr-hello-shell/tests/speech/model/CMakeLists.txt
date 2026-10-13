cmake_minimum_required(VERSION 3.20.0)
get_filename_component(APP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(SPEECH_TFLM_DIR "${APP_ROOT}/third_party/tflite-micro")
list(APPEND ZEPHYR_EXTRA_MODULES "${SPEECH_TFLM_DIR}")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(speech_model_fixture)
include("${APP_ROOT}/cmake/speech_model.cmake")
target_include_directories(app PRIVATE "${APP_ROOT}/src/speech" "${APP_ROOT}/src/usb_host")
target_sources(app PRIVATE src/main.c
  "${APP_ROOT}/src/speech/speech_service.c"
  "${APP_ROOT}/src/speech/speech_audio.c"
  "${APP_ROOT}/src/speech/speech_decision.c")
foreach(fixture yes_1000ms no_1000ms silence_1000ms yes_30ms no_30ms)
  generate_inc_file_for_target(app
    "${SPEECH_TFLM_DIR}/tensorflow/lite/micro/examples/micro_speech/testdata/${fixture}.wav"
    "${ZEPHYR_BINARY_DIR}/include/generated/${fixture}.inc")
endforeach()
