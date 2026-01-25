#!/usr/bin/env python3
"""
flash-ocd.py - Flash firmware using OpenOCD
This script uses OpenOCD to flash firmware onto a target device.

This is only compatible with our testbed, though, and if you want to use it
with different hardware and hardware configurations, you will need to adapt
`ID_TO_PORT`, `OPENOCD_CFG_TEMPLATE` and possibly `OPENOCD_BIN`.
"""

import argparse
import subprocess
import time
import sys
import signal
from pathlib import Path

import warnings
warnings.filterwarnings("ignore", category=DeprecationWarning)
import telnetlib

OPENOCD_BIN = "/usr/bin/openocd"
OPENOCD_CFG_TEMPLATE = "/opt/testbed-utilities/openocd-configs/openocd_000760201{}.cfg"

ID_TO_PORT = {
    "664": 6091,
    "684": 6061,
    "513": 6081,
}

# ANSI color codes
RED = "\033[91m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
BLUE = "\033[94m"
CYAN = "\033[96m"
DIM = "\033[2m"
BOLD = "\033[1m"
RESET = "\033[0m"

class OpenOCDFlasher:
    def __init__(self, elf_path, board_id):
        self.elf_path = Path(elf_path).resolve()
        self.board_id = board_id
        self.port = ID_TO_PORT[board_id]
        self.cfg = OPENOCD_CFG_TEMPLATE.format(board_id)
        self.openocd_proc = None
        self.telnet = None

    def start_openocd(self):
        print(f"{BOLD}{BLUE}[INFO]{RESET} Starting OpenOCD (board {self.board_id})")
        self.openocd_proc = subprocess.Popen(
            [OPENOCD_BIN, "-f", self.cfg],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )

        # Print OpenOCD output asynchronously
        time.sleep(0.5)
        for _ in range(20):
            if self.openocd_proc.poll() is not None:
                raise RuntimeError("OpenOCD exited early")
            time.sleep(0.1)

    def connect_telnet(self):
        print(f"{BOLD}{BLUE}[INFO]{RESET} Connecting to OpenOCD telnet (port {self.port})")
        for attempt in range(10):
            try:
                self.telnet = telnetlib.Telnet("localhost", self.port, timeout=2)
                # Read initial prompt/banner
                self.telnet.read_until(b"> ", timeout=2)
                return
            except Exception:
                time.sleep(0.5)
        raise RuntimeError("Failed to connect to OpenOCD telnet")

    def print_telnet_exchange(self, cmd, response):
        """Pretty print telnet command and response"""
        print(f"{DIM}{CYAN}   -> {cmd}{RESET}")

        for line in response.split('\n'):
            line = line.strip()
            if not line or line == '>':
                continue

            # Highlight important lines
            if 'Error' in line or 'error' in line or 'fail' in line:
                print(f"{RED}      {line}{RESET}")
            elif 'Verified OK' in line or 'Programming Finished' in line:
                print(f"{GREEN}      {line}{RESET}")
            elif 'nRF52' in line or 'Flash' in line or 'RAM' in line:
                print(f"{DIM}      {line}{RESET}")
            elif line.startswith('**'):
                print(f"{DIM}      {line}{RESET}")
            else:
                print(f"{DIM}      {line}{RESET}")

    def send_cmd(self, cmd, wait_for=None, timeout=30):
        """
        Send a command and wait for either:
        - A specific string in the response (wait_for parameter)
        - The prompt ("> ") if wait_for is None
        """
        self.telnet.write(cmd.encode("ascii") + b"\n")

        if wait_for:
            # Wait for specific string, then continue reading until prompt
            output = self.telnet.read_until(wait_for.encode("ascii"), timeout=timeout)
            # Continue reading until we get the prompt
            output += self.telnet.read_until(b"> ", timeout=5)
        else:
            # Just wait for the prompt
            output = self.telnet.read_until(b"> ", timeout=timeout)

        decoded = output.decode(errors="ignore")
        self.print_telnet_exchange(cmd, decoded)
        return decoded

    def flash(self):
        # Reset and halt
        print(f"\n{BOLD}{BLUE}[STEP 1/3]{RESET} Halting target...")
        response = self.send_cmd("reset halt", wait_for="halted due to debug-request")

        # Program and verify
        print(f"\n{BOLD}{BLUE}[STEP 2/3]{RESET} Programming {self.elf_path.name}...")
        response = self.send_cmd(f"program {self.elf_path} verify", wait_for="Verified OK", timeout=60)

        # Check for programming errors
        if "Error" in response or "error" in response:
            print(f"\n{BOLD}{RED}[ERROR]{RESET} Programming failed!")
            return False

        if "Verified OK" not in response:
            print(f"\n{BOLD}{RED}[ERROR]{RESET} Verification failed!")
            return False

        # Reset and init
        print(f"\n{BOLD}{BLUE}[STEP 3/3]{RESET} Resetting target...")
        response = self.send_cmd("reset init", wait_for="halted due to debug-request")

        print(f"\n{BOLD}{GREEN}[SUCCESS]{RESET} Flashing complete!\n")
        return True

    def cleanup(self):
        try:
            if self.telnet:
                self.telnet.close()
        except Exception:
            pass

        try:
            if self.openocd_proc:
                self.openocd_proc.terminate()
                self.openocd_proc.wait(timeout=3)
        except Exception:
            if self.openocd_proc:
                self.openocd_proc.kill()

def main():
    parser = argparse.ArgumentParser(description="Flash nRF52 via OpenOCD + telnet")
    parser.add_argument("elf", help="Path to ELF file")
    parser.add_argument("id", choices=ID_TO_PORT.keys(), help="Board ID")
    args = parser.parse_args()

    flasher = OpenOCDFlasher(args.elf, args.id)

    def handle_exit(sig, frame):
        print(f"\n{BOLD}{YELLOW}[WARN]{RESET} Interrupted, exiting...")
        flasher.cleanup()
        sys.exit(1)

    signal.signal(signal.SIGINT, handle_exit)
    signal.signal(signal.SIGTERM, handle_exit)

    success = False
    try:
        flasher.start_openocd()
        flasher.connect_telnet()
        success = flasher.flash()
    except Exception as e:
        print(f"\n{BOLD}{RED}[ERROR]{RESET} {e}")
        success = False
    finally:
        flasher.cleanup()

    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()