# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr_msc_demo)

string(TIMESTAMP APP_BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ" UTC)
target_compile_definitions(app PRIVATE
	APP_BUILD_TIMESTAMP=\"${APP_BUILD_TIMESTAMP}\"
)

target_sources(app PRIVATE
	src/main.c
	src/board_io.c
	src/http_server.c
	src/web_assets.c
	src/usb_msc.c
	src/sample_usbd_init.c
)
target_include_directories(app PRIVATE src)

zephyr_linker_sources(SECTIONS sections-rom.ld)
zephyr_linker_section(NAME http_resource_desc_board_http_service
                      KVMA RAM_REGION GROUP RODATA_REGION)
