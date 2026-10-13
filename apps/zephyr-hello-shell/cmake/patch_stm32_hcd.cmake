# Keep the shared west workspace untouched; compile a patched private HAL copy.
if(NOT ZEPHYR_HAL_STM32_MODULE_DIR)
  message(FATAL_ERROR "STM32 HAL module path unavailable; cannot patch HCD IRQ handler")
endif()

set(hcd_relative stm32cube/stm32h7xx/drivers/src/stm32h7xx_hal_hcd.c)
set(hcd_vendor "${ZEPHYR_HAL_STM32_MODULE_DIR}/${hcd_relative}")
set(hcd_copy "${PROJECT_BINARY_DIR}/${hcd_relative}")
set(hcd_patch "${CMAKE_CURRENT_LIST_DIR}/../../../patches/stm32h7-hcd-rx-before-hcint.patch")
if(NOT EXISTS "${hcd_vendor}" OR NOT EXISTS "${hcd_patch}")
  message(FATAL_ERROR "STM32 HAL source or HCD patch is missing")
endif()

configure_file("${hcd_vendor}" "${hcd_copy}" COPYONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${hcd_patch}")
find_program(GIT_EXECUTABLE git REQUIRED)
find_program(PATCH_EXECUTABLE patch REQUIRED)
execute_process(
  COMMAND "${GIT_EXECUTABLE}" apply --unidiff-zero --reverse --check "${hcd_patch}"
  WORKING_DIRECTORY "${ZEPHYR_HAL_STM32_MODULE_DIR}"
  RESULT_VARIABLE hcd_already_patched
  OUTPUT_QUIET ERROR_QUIET
)
if(NOT hcd_already_patched EQUAL 0)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --unidiff-zero --check "${hcd_patch}"
    WORKING_DIRECTORY "${ZEPHYR_HAL_STM32_MODULE_DIR}"
    RESULT_VARIABLE hcd_patch_check
    ERROR_VARIABLE hcd_patch_error
  )
  if(NOT hcd_patch_check EQUAL 0)
    message(FATAL_ERROR "HCD RX-before-HCINT patch does not apply to active HAL: ${hcd_patch_error}")
  endif()
  execute_process(
    COMMAND "${PATCH_EXECUTABLE}" --batch --forward --fuzz=0 -p1 -i "${hcd_patch}"
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
    RESULT_VARIABLE hcd_patch_result
    ERROR_VARIABLE hcd_patch_error
  )
  if(NOT hcd_patch_result EQUAL 0)
    message(FATAL_ERROR "Failed to apply HCD patch to build copy: ${hcd_patch_error}")
  endif()
endif()

file(READ "${hcd_copy}" hcd_patched_source)
string(FIND "${hcd_patched_source}" "void HAL_HCD_IRQHandler(" hcd_irq_pos)
string(FIND "${hcd_patched_source}" "while (__HAL_HCD_GET_FLAG(hhcd, USB_OTG_GINTSTS_RXFLVL))" hcd_rx_pos)
string(FIND "${hcd_patched_source}" "/* Handle Host channel Interrupt */" hcd_channel_pos)
if(hcd_irq_pos EQUAL -1 OR hcd_rx_pos LESS hcd_irq_pos OR
   hcd_channel_pos LESS hcd_rx_pos OR hcd_channel_pos EQUAL -1)
  message(FATAL_ERROR "HCD build copy does not drain RXFLVL before HCINT")
endif()

# Zephyr creates the library in stm32cube/CMakeLists.txt; its h7 child adds
# the HCD source. Check membership before replacing, so a HAL layout change
# fails configuration instead of silently compiling the unpatched source.
get_property(hal_targets DIRECTORY "${ZEPHYR_HAL_STM32_MODULE_DIR}/stm32cube"
             PROPERTY BUILDSYSTEM_TARGETS)
set(hcd_target "")
foreach(hal_target IN LISTS hal_targets)
  get_target_property(hal_sources "${hal_target}" SOURCES)
  if("${hcd_vendor}" IN_LIST hal_sources)
    set(hcd_target "${hal_target}")
    break()
  endif()
endforeach()
if(NOT hcd_target)
  message(FATAL_ERROR "Cannot locate original HCD source in STM32 HAL target")
endif()
set_source_files_properties("${hcd_vendor}" TARGET_DIRECTORY "${hcd_target}"
                            PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources("${hcd_target}" PRIVATE "${hcd_copy}")
message(STATUS "STM32 HCD: compiling patched build copy ${hcd_copy}")
