#!/usr/bin/env python3
"""
Complete bridge between wpan_serial (802.15.4) and Linux TUN interface.
Handles full 6LoWPAN compression/decompression.
"""

import serial
import struct
import fcntl
import os
import sys
import select
import ipaddress
from scapy.all import *
from scapy.layers.dot15d4 import *
from scapy.layers.sixlowpan import *

# Monkey-patch a bug in Scapy's dot15d4 layer. The util_srcpanid_present
# function incorrectly checks pkt.underlayer for fields that are present in pkt
# itself, causing an AttributeError.
import scapy.layers.dot15d4 as dot15d4_module
def _fixed_util_srcpanid_present(pkt):
    if (pkt.getfieldval("fcf_srcaddrmode") != 0) and \
       (pkt.getfieldval("fcf_panidcompress") == 0):
        return True
    return False
dot15d4_module.util_srcpanid_present = _fixed_util_srcpanid_present

# SLIP constants
SLIP_END = 0o300
SLIP_ESC = 0o333
SLIP_ESC_END = 0o334
SLIP_ESC_ESC = 0o335

class WPANBridge:
    def __init__(self, serial_port='/dev/ttyACM2', tun_prefix='48:1516:2342::', pan_id=0xabcd):
        self.serial_port = serial_port
        self.tun_prefix = tun_prefix
        self.pan_id = pan_id
        self.slip_buffer = b''
        self.seq = 0

        # Open serial
        self.ser = serial.Serial(serial_port, 115200, timeout=0)
        print(f"Opened {serial_port}")

        # Create TUN
        self.tun = self.create_tun()
        print(f"Created tun0 with prefix {tun_prefix}/64")

        # Radio MAC address (will be learned from ?M request)
        self.radio_mac = None
        self.radio_mac_be = None  # Big-endian version for addressing

        # Short address (derived from MAC)
        self.radio_short_addr = None

    def create_tun(self):
        """Create and configure TUN interface"""
        TUNSETIFF = 0x400454ca
        IFF_TUN = 0x0001
        IFF_NO_PI = 0x1000

        tun = open('/dev/net/tun', 'r+b', buffering=0)
        ifr = struct.pack('16sH', b'tun0', IFF_TUN | IFF_NO_PI)
        fcntl.ioctl(tun, TUNSETIFF, ifr)

        # Configure interface
        os.system('ip link set tun0 up')
        os.system(f'ip -6 addr add {self.tun_prefix}1/64 dev tun0')

        return tun

    def decode_slip(self, data):
        """Decode SLIP-encoded data"""
        result = []
        i = 0
        while i < len(data):
            if data[i] == SLIP_ESC:
                i += 1
                if i < len(data):
                    if data[i] == SLIP_ESC_END:
                        result.append(SLIP_END)
                    elif data[i] == SLIP_ESC_ESC:
                        result.append(SLIP_ESC)
            elif data[i] == SLIP_END:
                break
            else:
                result.append(data[i])
            i += 1
        return bytes(result)

    def encode_slip(self, data):
        """Encode data with SLIP"""
        result = []
        for byte in data:
            if byte == SLIP_END:
                result.extend([SLIP_ESC, SLIP_ESC_END])
            elif byte == SLIP_ESC:
                result.extend([SLIP_ESC, SLIP_ESC_ESC])
            else:
                result.append(byte)
        result.append(SLIP_END)
        return bytes(result)

    def handle_radio_packet(self, decoded):
        """Handle packet received from radio"""
        if len(decoded) < 2:
            return

        print(f"\nhandle_radio_packet: len={len(decoded)}")

        # Check for wpan_serial protocol messages
        if decoded[0] == ord('!'):
            cmd = chr(decoded[1])
            payload = decoded[2:]

            if cmd == 'M':
                # MAC address report (in big-endian)
                if len(payload) >= 8:
                    self.radio_mac_be = payload[:8]
                    # Convert to little-endian for internal use
                    self.radio_mac = bytes(reversed(self.radio_mac_be))

                    # Derive short address from first 2 bytes of MAC
                    self.radio_short_addr = struct.unpack('>H', self.radio_mac_be[:2])[0]

                    mac_str = ':'.join(f'{b:02x}' for b in self.radio_mac_be)
                    print(f"Radio MAC: {mac_str} (short: 0x{self.radio_short_addr:04x})")

            elif cmd == 'R':
                # Packet report (seq, status, num_tx)
                if len(payload) >= 3:
                    seq, status, num_tx = payload[0], payload[1], payload[2]
                    if status == 0:
                        print(f"  TX success: seq={seq}")
                    else:
                        print(f"  TX error: seq={seq} status={status}")

            return

        # Otherwise it's a data packet - should be 802.15.4 frame
        try:
            # Parse as 802.15.4 frame
            frame = Dot15d4FCS(decoded)

            # Check if it contains 6LoWPAN data
            if frame.haslayer(LoWPAN_IPHC):
                # Decompress 6LoWPAN to get full IPv6 packet
                ipv6_pkt = frame[IPv6]

                # Write to TUN
                ipv6_bytes = bytes(ipv6_pkt)
                self.tun.write(ipv6_bytes)

                src = ipv6_pkt.src
                dst = ipv6_pkt.dst
                proto = ipv6_pkt.nh if hasattr(ipv6_pkt, 'nh') else '?'
                print(f"<- Radio: {src} -> {dst} proto={proto} len={len(ipv6_bytes)}")

            elif frame.haslayer(IPv6):
                # Uncompressed IPv6 (rare, but possible)
                ipv6_pkt = frame[IPv6]
                ipv6_bytes = bytes(ipv6_pkt)
                self.tun.write(ipv6_bytes)
                print(f"<- Radio: {ipv6_pkt.src} -> {ipv6_pkt.dst} (uncompressed)")

            else:
                print("  Unknown frame type received from radio")

        except Exception as e:
            print(f"Parse error: {e}")
            # Print hex dump for debugging
            print(f"  Raw: {decoded[:40].hex()}")

    def mac_from_ipv6_bytes(self, dst_bytes):
        """Extract MAC address from IPv6 address bytes (16 bytes)"""

        # Extract last 8 bytes (interface identifier)
        iid = dst_bytes[8:]

        # Reverse EUI-64: flip bit 7 of first byte and remove ff:fe in middle
        mac = bytearray()
        mac.append(iid[0] ^ 0x02)  # Flip universal/local bit
        mac.append(iid[1])
        mac.append(iid[2])
        # Skip iid[3:5] (should be 0xff, 0xfe)
        mac.append(iid[5])
        mac.append(iid[6])
        mac.append(iid[7])

        return bytes(mac)

    def mac_from_ipv6(self, ipv6_addr):
        """Extract MAC address from IPv6 address string"""
        addr_obj = ipaddress.IPv6Address(ipv6_addr)
        return self.mac_from_ipv6_bytes(addr_obj.packed)

    def send_to_radio_raw(self, ipv6_bytes, dest_mac):
        """Send raw IPv6 bytes to radio via wpan_serial protocol"""

        print(f"Sending to radio: len={len(ipv6_bytes)}")

        # Build 802.15.4 frame manually as bytes to avoid Scapy issues
        frame_bytes = bytearray()

        # Frame Control Field (2 bytes)
        # FCF: Data(1), NoSec(0), NoPend(0), NoAck(0), PanCompress(1),
        # DestAddrExt(3), Ver(1=2006), SrcAddrExt(3)
        fcf = 0xDC41  # Data frame, PAN ID compress, extended addressing
        frame_bytes.extend(struct.pack('<H', fcf))

        # Sequence number (1 byte)
        frame_bytes.append(self.seq)

        # Destination PAN ID (2 bytes)
        frame_bytes.extend(struct.pack('<H', self.pan_id))

        # Destination address (8 bytes, little-endian)
        frame_bytes.extend(reversed(dest_mac))

        # Source address (8 bytes, little-endian)
        frame_bytes.extend(reversed(self.radio_mac_be))

        # 6LoWPAN dispatch: uncompressed IPv6 (0x41)
        frame_bytes.append(0x41)

        # Raw IPv6 packet
        frame_bytes.extend(ipv6_bytes)

        print(f" Built 802.15.4 frame; len={len(frame_bytes)}")

        # Build wpan_serial packet: !S + seq + num_attrs + [attrs] + frame
        packet = bytearray()
        packet.append(ord('!'))
        packet.append(ord('S'))
        packet.append(self.seq)
        packet.append(0)  # No attributes
        packet.extend(frame_bytes)

        # Encode and send via SLIP
        slip_data = self.encode_slip(bytes(packet))
        self.ser.write(slip_data)

        print(f"-> Radio: seq={self.seq} len={len(frame_bytes)} dest={dest_mac.hex(':')}")
        self.seq = (self.seq + 1) % 256

    def request_mac(self):
        """Request MAC address from radio"""
        packet = b'?M'
        slip_data = self.encode_slip(packet)
        self.ser.write(slip_data)
        print("-> Requesting MAC address...")

    def run(self):
        """Main loop"""
        print("\n=== WPAN Bridge Running! ===\n")

        # Request MAC address
        self.request_mac()

        # Wait a bit for MAC to be received
        import time
        time.sleep(0.5)

        if not self.radio_mac:
            print("Warning: Radio MAC not received yet, continuing anyway...")

        while True:
            # Use select to wait for data from either serial or TUN
            readable, _, _ = select.select([self.ser, self.tun], [], [], 0.1)

            # Handle serial data (from radio)
            if self.ser in readable:
                print("Reading from serial...")
                data = self.ser.read(4096)
                if data:
                    self.slip_buffer += data

                    # Process complete SLIP packets
                    while SLIP_END in self.slip_buffer:
                        end_idx = self.slip_buffer.index(SLIP_END)
                        slip_packet = self.slip_buffer[:end_idx+1]
                        self.slip_buffer = self.slip_buffer[end_idx+1:]

                        decoded = self.decode_slip(slip_packet)
                        if decoded:
                            self.handle_radio_packet(decoded)

            # Handle TUN data (from Linux)
            if self.tun in readable:
                print("Reading from TUN...")
                ipv6_data = self.tun.read(1500)
                if ipv6_data:
                    try:
                        ipv6_pkt = IPv6(ipv6_data)

                        # Determine destination MAC from IPv6 address
                        dest_mac = self.mac_from_ipv6(ipv6_pkt.dst)

                        print(f"-> TUN: {ipv6_pkt.src} -> {ipv6_pkt.dst} len={len(ipv6_pkt)}")

                        if self.radio_mac:
                            # self.send_to_radio(ipv6_pkt, dest_mac)
                            self.send_to_radio_raw(ipv6_data, dest_mac)
                        else:
                            print("  Skipping: Radio MAC not known yet")

                    except Exception as e:
                        print(f"TUN error: {e}")

def main():
    if os.geteuid() != 0:
        print("Error: Must run as root (for TUN interface)")
        sys.exit(1)

    try:
        bridge = WPANBridge(
            serial_port='/dev/ttyACM2',
            tun_prefix='48:1516:2342::',
            pan_id=0xabcd
        )
        bridge.run()
    except KeyboardInterrupt:
        print("\n\nShutting down...")
    except Exception as e:
        print(f"\nError: {e}")
        import traceback
        traceback.print_exc()

if __name__ == '__main__':
    main()