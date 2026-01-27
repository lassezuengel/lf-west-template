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

# ...

udp_all: udp_clean udp_build udp_ssh

udp_clean:
	echo "Cleaning previous builds..."
	rm -r -f udp_build/

udp_build:
	echo "Building echo client and server..."

	cd udp_client && west build -p -b nrf52840dk/nrf52840 . -- -DOVERLAY_CONFIG=overlay-802154.conf
	cd udp_server && west build -p -b nrf52840dk/nrf52840 . -- -DOVERLAY_CONFIG=overlay-802154.conf

	mkdir udp_build
	cp udp_client/build/zephyr/zephyr.elf udp_build/echo_client.elf
	cp udp_server/build/zephyr/zephyr.elf udp_build/echo_server.elf

udp_ssh:
	echo "Transferring built files to remote server..."

	cd udp_build \
		&& scp echo_client.elf zungel@saclay.iot-lab.info:~ \
		&& scp echo_server.elf zungel@saclay.iot-lab.info:~

	scp run_experiment.py zungel@saclay.iot-lab.info:~

# ...
simplehailo_all: simplehailo_clean simplehailo_build simplehailo_ssh

simplehailo_clean:
	echo "Cleaning previous builds..."
	rm -r -f simplehailo_build/

simplehailo_build:
	echo "Building simplehailo client and server for nrf52833..."

	cd simple_client && west build -p -b nrf52833dk_nrf52833 . -- -DOVERLAY_CONFIG=overlay-802154.conf
	cd simple_server && west build -p -b nrf52833dk_nrf52833 . -- -DOVERLAY_CONFIG=overlay-802154.conf

	mkdir simplehailo_build
	cp simple_client/build/zephyr/zephyr.elf simplehailo_build/simple_client_nrf52833.elf
	cp simple_server/build/zephyr/zephyr.elf simplehailo_build/simple_server_nrf52833.elf
simplehailo_ssh:
	echo "Transferring built files to remote server..."

	cd simplehailo_build \
		&& scp simple_client_nrf52833.elf hailo@hailo-desktop:~/lf \
		&& scp simple_server_nrf52833.elf hailo@hailo-desktop:~/lf

# Compiles and transfers federate programs for NrfDistribute example
fed_all: fed_clean fed_build fed_ssh

fed_clean:
	echo "Cleaning previous builds..."
	rm -r -f fed_build/
	rm -r -f NrfBlinky/fed-gen/

fed_build:
	echo "Building federate programs..."

	cd NrfBlinky && lfc src/NrfDistribute.lf

	mkdir fed_build
	cp NrfBlinky/fed-gen/NrfDistribute/src-gen/federate__c/build/zephyr/zephyr.elf fed_build/federate__c.elf
	cp NrfBlinky/fed-gen/NrfDistribute/src-gen/federate__p/build/zephyr/zephyr.elf fed_build/federate__p.elf

fed_ssh:
	echo "Transferring built files to remote server..."

	cd fed_build \
		&& scp federate__c.elf hailo@hailo-desktop:~/lf \
		&& scp federate__p.elf hailo@hailo-desktop:~/lf