cmake_minimum_required(VERSION 3.28.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gd32f303rc_bringup)

target_sources(app PRIVATE
  app.c
  precharge/precharge.c
  precharge/precharge_ctrl.c
  precharge/precharge_io.c
  host_link/host_link.c
  host_link/host_protocol.c
  gpio_shell.c
  precharge/mp10.c
  datetime.c
  params.c
)

add_subdirectory(../../bsp ${CMAKE_CURRENT_BINARY_DIR}/bsp)
target_include_directories(app PRIVATE inc)
