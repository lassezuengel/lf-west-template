echo_all: echo_clean echo_build echo_ssh

echo_clean:
	echo "Cleaning previous builds..."
	rm -r -f echo_build/

echo_build:
	echo "Building echo client and server..."

	cd echo_client && west build -p -b nrf52840dk/nrf52840 . -- -DOVERLAY_CONFIG=overlay-802154.conf
	cd echo_server && west build -p -b nrf52840dk/nrf52840 . -- -DOVERLAY_CONFIG=overlay-802154.conf

	mkdir echo_build
	cp echo_client/build/zephyr/zephyr.elf echo_build/echo_client.elf
	cp echo_server/build/zephyr/zephyr.elf echo_build/echo_server.elf

echo_ssh:
	echo "Transferring built files to remote server..."

	cd echo_build \
		&& scp echo_client.elf zungel@saclay.iot-lab.info:~ \
		&& scp echo_server.elf zungel@saclay.iot-lab.info:~

	scp run_experiment.py zungel@saclay.iot-lab.info:~


# ...

simple_all: simple_clean simple_build simple_ssh

simple_clean:
	echo "Cleaning previous builds..."
	rm -r -f simple_build/

simple_build:
	echo "Building echo client and server..."

	cd simple_client && west build -p -b nrf52840dk/nrf52840 . -- -DOVERLAY_CONFIG=overlay-802154.conf
	cd simple_server && west build -p -b nrf52840dk/nrf52840 . -- -DOVERLAY_CONFIG=overlay-802154.conf

	mkdir simple_build
	cp simple_client/build/zephyr/zephyr.elf simple_build/echo_client.elf
	cp simple_server/build/zephyr/zephyr.elf simple_build/echo_server.elf

simple_ssh:
	echo "Transferring built files to remote server..."

	cd simple_build \
		&& scp echo_client.elf zungel@saclay.iot-lab.info:~ \
		&& scp echo_server.elf zungel@saclay.iot-lab.info:~

	scp run_experiment.py zungel@saclay.iot-lab.info:~