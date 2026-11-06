cmake_minimum_required(VERSION 3.28.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gd32f303rc_bringup)

target_sources(app PRIVATE
  src/main.c
  src/led.c
  src/datetime.c
  src/params.c
  src/board_io.c
  src/serial_rx.c
)
