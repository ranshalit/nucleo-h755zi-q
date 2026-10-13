if(NOT EXISTS "${SPEECH_TFLM_DIR}/zephyr/module.yml")
  message(FATAL_ERROR "Speech model dependency missing; run git submodule update --init --recursive")
endif()
get_filename_component(SPEECH_APP_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
target_sources(app PRIVATE "${SPEECH_APP_ROOT}/src/speech/speech_model.cc")
set(NO_THREADSAFE_STATICS $<TARGET_PROPERTY:compiler-cpp,no_threadsafe_statics>)
zephyr_compile_options($<$<COMPILE_LANGUAGE:CXX>:${NO_THREADSAFE_STATICS}>)
foreach(model micro_speech_quantized audio_preprocessor_int8)
  generate_inc_file_for_target(app
    "${SPEECH_TFLM_DIR}/tensorflow/lite/micro/examples/micro_speech/models/${model}.tflite"
    "${ZEPHYR_BINARY_DIR}/include/generated/${model}.inc")
endforeach()

# Zephyr's module already supplies window/RFFT kernels and implementations.
set(speech_signal_kernels
  energy energy_flexbuffers_generated_data
  fft_auto_scale_common fft_auto_scale_kernel
  filter_bank filter_bank_flexbuffers_generated_data
  filter_bank_log filter_bank_log_flexbuffers_generated_data
  filter_bank_spectral_subtraction filter_bank_spectral_subtraction_flexbuffers_generated_data
  filter_bank_square_root filter_bank_square_root_common
  pcan pcan_flexbuffers_generated_data)
foreach(source IN LISTS speech_signal_kernels)
  target_sources(app PRIVATE "${SPEECH_TFLM_DIR}/signal/micro/kernels/${source}.cc")
endforeach()
set(speech_signal_sources
  energy fft_auto_scale filter_bank filter_bank_log
  filter_bank_spectral_subtraction filter_bank_square_root
  log max_abs msb_32 msb_64 pcan_argc_fixed square_root_32 square_root_64)
foreach(source IN LISTS speech_signal_sources)
  target_sources(app PRIVATE "${SPEECH_TFLM_DIR}/signal/src/${source}.cc")
endforeach()
