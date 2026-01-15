#!/bin/bash

cd wpan_serial_uart && west build -p -b nrf52833dk/nrf52833 . -- -DDTC_OVERLAY_FILE=dwm3001c.overlay
