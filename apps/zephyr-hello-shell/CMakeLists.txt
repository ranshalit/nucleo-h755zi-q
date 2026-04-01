cmake_minimum_required(VERSION 3.20.0)

set(extra_conf_file "")
set(extra_dtc_overlay_file "")

if(BOARD STREQUAL "nucleo_h755zi_q/stm32h755xx/m7")
  set(extra_conf_file "${CMAKE_CURRENT_SOURCE_DIR}/boards/nucleo_h755zi_q_stm32h755xx_m7.conf")
  set(extra_dtc_overlay_file "${CMAKE_CURRENT_SOURCE_DIR}/boards/nucleo_h755zi_q_stm32h755xx_m7.overlay")
endif()

if(extra_conf_file)
  set(CONF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/prj.conf;${extra_conf_file}" CACHE STRING "" FORCE)
endif()

if(extra_dtc_overlay_file)
  set(DTC_OVERLAY_FILE "${extra_dtc_overlay_file}" CACHE STRING "" FORCE)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr_hello_shell)

target_sources(app PRIVATE
  src/boot_optimizations.c
  src/main.c
  src/board_io.c
  src/http_server.c
  src/web_assets.c
)

zephyr_linker_sources(SECTIONS sections-rom.ld)
zephyr_linker_section(NAME http_resource_desc_board_http_service
                      KVMA RAM_REGION GROUP RODATA_REGION)
