cmake_minimum_required(VERSION 3.20.0)

set(APP_BASE "${CMAKE_CURRENT_SOURCE_DIR}/../../apps/zephyr-hello-shell")
set(KCONFIG_ROOT "${APP_BASE}/Kconfig")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(minimal_zephyr_mouse)

target_sources(app PRIVATE
  src/main.c
  ${APP_BASE}/src/boot_optimizations.c
  ${APP_BASE}/src/board_io.c
  ${APP_BASE}/src/usb_host/stm32_hcd_adapter.c
  ${APP_BASE}/src/usb_host/usb_gpio_capture.c
  ${APP_BASE}/src/usb_host/usb_host_shell.c
  ${APP_BASE}/src/usb_host/usb_boot_mouse_report.c
  ${APP_BASE}/src/usb_host/usb_boot_mouse_class.c
  ${APP_BASE}/third_party/stm32-mw-usb-host/Core/Src/usbh_core.c
  ${APP_BASE}/third_party/stm32-mw-usb-host/Core/Src/usbh_ctlreq.c
  ${APP_BASE}/third_party/stm32-mw-usb-host/Core/Src/usbh_ioreq.c
  ${APP_BASE}/third_party/stm32-mw-usb-host/Core/Src/usbh_pipes.c
)

target_include_directories(app PRIVATE
  ${APP_BASE}/src/usb_host
  ${APP_BASE}/third_party/stm32-mw-usb-host/Core/Inc
)
