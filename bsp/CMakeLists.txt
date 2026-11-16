target_sources(app PRIVATE
  main.c
  board_startup.c
  board_watchdog.c
  board_gpio.c
  board_adc.c
  serial_rx.c
  rs485_tx.c
)

target_include_directories(app PRIVATE inc)
