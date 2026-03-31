cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr_hello_shell)

target_sources(app PRIVATE
  src/main.c
  src/board_io.c
  src/http_server.c
  src/web_assets.c
)

zephyr_linker_sources(SECTIONS sections-rom.ld)
zephyr_linker_section(NAME http_resource_desc_board_http_service
                      KVMA RAM_REGION GROUP RODATA_REGION)
