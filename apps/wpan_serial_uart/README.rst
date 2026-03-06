Overview
********

The wpan_serial sample shows how to use hardware with 802.15.4 radio and USB
controller as a "serial-radio" device for Contiki-based border routers.

Requirements
************

The sample assumes that 802.15.4 radio and UART are supported on
a board. You can pick, for example, a Qorvo 3001CDK, nRF52833DK or nRF52840DK board.

Building and Running
********************

#. Before building and running this sample, be sure your Linux system's
   ModemManager is disabled, otherwise, it can interfere with serial
   port communication:

   .. code-block:: console

     $ sudo systemctl disable ModemManager.service

#. Build the sample Zephyr application to a board with a 802.15.4 radio
   and UART controller. There are configuration files for various setups
   in the ``wpan_serial_uart`` directory:

   - :file:`prj.conf`
     This is the standard default config. This can be used by itself for
     hardware which has native 802.15.4 support.

   To build the wpan_serial sample:

   .. zephyr-app-commands::
     :zephyr-app: wpan_serial_uart
     :board: <board name>
     :conf: "prj.conf [overlay-<RADIO>.conf]"
     :goals: build
     :compact:

   Here's how to build and flash the sample for the Atmel SAM R21
   Xplained Pro Development Kit.

   .. zephyr-app-commands::
     :zephyr-app: wpan_serial_uart
     :board: samr21_xpro
     :goals: build flash
     :compact:

#. Connect board to Linux PC, /dev/ttyACM[number] should appear.
#. Run Contiki-based native border router (6lbr, native-router, etc)
   Example for Contiki:

   .. code-block:: console

     $ cd examples/ipv6/native-border-router
     $ make
     $ sudo ./border-router.native -v5 -s ttyACM0 fd01::1/64

Now you have a Contiki native board router.  You can access its web-based
interface with your browser using the server address printed in the
border-router output.

.. code-block:: console

  ...
  Server IPv6 addresses:
   0x62c5c0: =>fd01::212:4b00:531f:113a
  ...

Use your browser to access ``http://[fd01::212:4b00:531f:113a]/`` and you'll
see available neighbors and routes.
