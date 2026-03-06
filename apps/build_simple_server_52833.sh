#!/bin/bash

cd simple_server && west build -p -b nrf52833dk/nrf52833 . -- -DOVERLAY_CONFIG=overlay-802154.conf
