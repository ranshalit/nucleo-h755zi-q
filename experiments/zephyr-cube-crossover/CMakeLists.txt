cmake_minimum_required(VERSION 3.20.0)

set(APP_BASE "${CMAKE_CURRENT_SOURCE_DIR}/../../apps/zephyr-hello-shell")
set(CUBE_BASE "${CMAKE_CURRENT_SOURCE_DIR}/../cube-hid-mouse")
set(KCONFIG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Kconfig")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr_cube_crossover)

option(CROSSOVER_USE_ZEPHYR_BRIDGE "Use Zephyr's HCD bridge with Cube middleware" OFF)
option(CROSSOVER_USE_BOOT_MOUSE_CLASS "Use the application's boot-mouse class instead of Cube HID" OFF)
option(CROSSOVER_SLOW_CLASS_POLL "Sleep 1 ms between class-processing iterations" OFF)
option(CROSSOVER_BURST_CLASS_POLL "Use bounded 50 us class-processing bursts" OFF)

if(CROSSOVER_SLOW_CLASS_POLL AND CROSSOVER_BURST_CLASS_POLL)
  message(FATAL_ERROR "Choose only one crossover class polling cadence")
endif()

if(CROSSOVER_SLOW_CLASS_POLL)
  target_compile_definitions(app PRIVATE CROSSOVER_SLOW_CLASS_POLL)
endif()
if(CROSSOVER_BURST_CLASS_POLL)
  target_compile_definitions(app PRIVATE CROSSOVER_BURST_CLASS_POLL)
endif()

target_sources(app PRIVATE
  src/main.c
  ${APP_BASE}/src/boot_optimizations.c
  ${CUBE_BASE}/middleware/Core/Src/usbh_core.c
  ${CUBE_BASE}/middleware/Core/Src/usbh_ctlreq.c
  ${CUBE_BASE}/middleware/Core/Src/usbh_ioreq.c
  ${CUBE_BASE}/middleware/Core/Src/usbh_pipes.c
)

if(CROSSOVER_USE_BOOT_MOUSE_CLASS)
  target_sources(app PRIVATE
    ${APP_BASE}/src/usb_host/usb_boot_mouse_class.c
    ${APP_BASE}/src/usb_host/usb_boot_mouse_report.c
  )
  target_include_directories(app PRIVATE ${APP_BASE}/src/usb_host)
  target_compile_definitions(app PRIVATE CROSSOVER_BOOT_MOUSE_CLASS)
else()
  target_sources(app PRIVATE
    ${CUBE_BASE}/middleware/Class/HID/Src/usbh_hid.c
    ${CUBE_BASE}/middleware/Class/HID/Src/usbh_hid_mouse.c
    ${CUBE_BASE}/middleware/Class/HID/Src/usbh_hid_keybd.c
    ${CUBE_BASE}/middleware/Class/HID/Src/usbh_hid_parser.c
  )
endif()

if(CROSSOVER_USE_ZEPHYR_BRIDGE)
  target_sources(app PRIVATE
    ${APP_BASE}/src/usb_host/stm32_hcd_adapter.c
    ${APP_BASE}/src/usb_host/usb_gpio_capture.c
  )
  target_include_directories(app PRIVATE ${APP_BASE}/src/usb_host)
  target_compile_definitions(app PRIVATE CROSSOVER_ZEPHYR_BRIDGE)
else()
  target_sources(app PRIVATE ${CUBE_BASE}/src/usbh_platform.c)
  target_compile_definitions(app PRIVATE CUBE_ZEPHYR_CROSSOVER)
endif()
target_include_directories(app BEFORE PRIVATE
  include
  ${CUBE_BASE}/include
  ${CUBE_BASE}/middleware/Core/Inc
  ${CUBE_BASE}/middleware/Class/HID/Inc
)
