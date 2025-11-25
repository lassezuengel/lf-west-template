

cd HelloSocketsC
west build -b nrf52840dk_nrf52840 -p
cd ../HelloSocketsS
west build -b nrf52840dk_nrf52840 -p
cd ..