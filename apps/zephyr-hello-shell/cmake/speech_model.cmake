if(NOT EXISTS "${SPEECH_TFLM_DIR}/zephyr/module.yml")
  message(FATAL_ERROR "Speech model dependency missing; run git submodule update --init --recursive")
endif()
get_filename_component(SPEECH_APP_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
target_sources(app PRIVATE "${SPEECH_APP_ROOT}/src/speech/speech_model.cc")
set(NO_THREADSAFE_STATICS $<TARGET_PROPERTY:compiler-cpp,no_threadsafe_statics>)
zephyr_compile_options($<$<COMPILE_LANGUAGE:CXX>:${NO_THREADSAFE_STATICS}>)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # Preserve CMSIS-NN speed optimizations; shrink the interpreter/frontend glue.
  get_target_property(tflm_sources modules__tflite-micro SOURCES)
  foreach(source IN LISTS tflm_sources)
    set_property(SOURCE "${source}" TARGET_DIRECTORY modules__tflite-micro
      APPEND PROPERTY COMPILE_OPTIONS -Os)
  endforeach()
  set_source_files_properties("${SPEECH_APP_ROOT}/src/speech/speech_model.cc"
    PROPERTIES COMPILE_OPTIONS -Os)
endif()
generate_inc_file_for_target(app
  "${SPEECH_APP_ROOT}/models/speech_keywords.tflite"
  "${ZEPHYR_BINARY_DIR}/include/generated/speech_keywords.inc")
generate_inc_file_for_target(app
  "${SPEECH_TFLM_DIR}/tensorflow/lite/micro/examples/micro_speech/models/audio_preprocessor_int8.tflite"
  "${ZEPHYR_BINARY_DIR}/include/generated/audio_preprocessor_int8.inc")
dt_nodelabel(speech_model_flash_path NODELABEL speech_model_flash)
if(speech_model_flash_path)
  zephyr_linker_sources(SECTIONS "${SPEECH_APP_ROOT}/cmake/speech_bank2.ld")
endif()

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
