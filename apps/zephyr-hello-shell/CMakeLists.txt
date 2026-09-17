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

if(DEFINED ENV{SOURCE_DATE_EPOCH} AND
   NOT "$ENV{SOURCE_DATE_EPOCH}" MATCHES "^[0-9]+$")
  message(FATAL_ERROR "SOURCE_DATE_EPOCH must be a nonnegative integer")
endif()

string(TIMESTAMP APP_BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ" UTC)
target_compile_definitions(app PRIVATE
  APP_BUILD_TIMESTAMP=\"${APP_BUILD_TIMESTAMP}\"
)

target_sources(app PRIVATE
  src/boot_optimizations.c
  src/main.c
  src/board_io.c
  src/http_server.c
  src/web_assets.c
  src/mjpeg_multipart.c
  src/camera_frames.c
  src/camera_stream.c
  src/usb_host/stm32_hcd_adapter.c
  src/usb_host/usb_gpio_capture.c
  src/usb_host/usb_host_shell.c
  third_party/stm32-mw-usb-host/Core/Src/usbh_core.c
  third_party/stm32-mw-usb-host/Core/Src/usbh_ctlreq.c
  third_party/stm32-mw-usb-host/Core/Src/usbh_ioreq.c
  third_party/stm32-mw-usb-host/Core/Src/usbh_pipes.c
)

target_include_directories(app PRIVATE
  src/usb_host
  third_party/stm32-mw-usb-host/Core/Inc
)

zephyr_linker_sources(SECTIONS sections-rom.ld)
zephyr_linker_section(NAME http_resource_desc_board_http_service
                      KVMA RAM_REGION GROUP RODATA_REGION)
