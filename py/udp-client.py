#!/usr/bin/env python3
"""
echo_client.py - IPv6 TCP echo client
Semantically equivalent to the Zephyr echo-client.c
"""

import socket
import logging
import sys
import time
import argparse

# Configure logging
logging.basicConfig(
    level=logging.DEBUG,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger('net_echo_client')

# Configuration
PEER_PORT = 4242
RECV_BUF_SIZE = 128
SEND_INTERVAL_MS = 100  # milliseconds between sends

# Test message (matching the Zephyr client)
LOREM_IPSUM = b"LOREM_IPSUM"


class UDPClient:
    """IPv6 UDP Client"""

    def __init__(self, server_addr, server_port):
        self.server_addr = server_addr
        self.server_port = server_port
        self.udp_sock = None
        self.proto = "IPv6"
        self.counter = 0

    def start_udp_proto(self):
        """Creates UDP socket and connects to server"""
        self.udp_sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        return 0

    def sendall(self, data):
        """Send all data, handling partial sends"""
        total_sent = 0
        data_len = len(data)

        while total_sent < data_len:
            try:
                sent = self.udp_sock.sendto(data[total_sent:], (self.server_addr, self.server_port))
                if sent == 0:
                    raise RuntimeError("Socket connection broken")
                total_sent += sent
            except OSError as e:
                logger.error(f"Send error: {e}")
                return -1

        return 0

    def send_udp_data(self):
        """Send test data to server"""
        ret = self.sendall(LOREM_IPSUM)

        if ret < 0:
            logger.error(f"{self.proto} UDP: Failed to send data")

        return ret

    def process_udp_proto(self):
        """Main loop - continuously send data"""
        try:
            while True:
                self.counter += 1

                # Log progress every 1000 packets
                if self.counter % 1000 == 0:
                    logger.info(f"{self.proto} UDP: Exchanged {self.counter} packets")

                # Sleep between sends (100ms)
                time.sleep(SEND_INTERVAL_MS / 1000.0)

                # Send data
                ret = self.send_udp_data()
                if ret < 0:
                    return ret

        except KeyboardInterrupt:
            logger.info("Interrupted by user")
            return 0
        except OSError as e:
            logger.error(f"Socket error: {e}")
            return -1

        return 0

    def start_udp(self):
        """Start UDP connection"""
        logger.info("Starting...")
        return self.start_udp_proto()

    def run_udp(self):
        """Run the UDP client loop"""
        return self.process_udp_proto()

    def stop_udp(self):
        """Stop UDP connection"""
        logger.info("Stopping...")
        self.connected = False
        if self.udp_sock:
            try:
                self.udp_sock.close()
            except OSError:
                pass
            self.udp_sock = None

    def run(self):
        """Main client loop"""
        logger.info("Run echo client")
        logger.info("Network connected")

        while True:
            ret = self.start_udp()

            if ret == 0:
                ret = self.run_udp()
            else:
                logger.error("Failed to start UDP client")

            self.stop_udp()

            # If interrupted, break
            if ret == 0:
                break

            # Otherwise retry after delay
            logger.info("Retrying connection in 5 seconds...")
            time.sleep(5)


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(description='IPv6 UDP Echo Client')
    parser.add_argument('server', help='IPv6 address of the server (e.g., fd00::1 or 2001:db8::1)')
    parser.add_argument('--port', type=int, default=PEER_PORT, help=f'Server port (default: {PEER_PORT})')

    args = parser.parse_args()

    client = UDPClient(args.server, args.port)

    try:
        client.run()
    except KeyboardInterrupt:
        logger.info("\nShutting down...")
    finally:
        client.stop_udp()

    return 0


if __name__ == "__main__":
    sys.exit(main())