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


class TCPClient:
    """IPv6 TCP Client"""

    def __init__(self, server_addr, server_port):
        self.server_addr = server_addr
        self.server_port = server_port
        self.tcp_sock = None
        self.proto = "IPv6"
        self.counter = 0
        self.connected = False

    def start_tcp_proto(self):
        """Creates TCP socket and connects to server"""
        try:
            # Create IPv6 TCP socket
            self.tcp_sock = socket.socket(socket.AF_INET6, socket.SOCK_STREAM, socket.IPPROTO_TCP)

            logger.info(f"Connecting to [{self.server_addr}]:{self.server_port}")

            # Connect to server
            self.tcp_sock.connect((self.server_addr, self.server_port))

            logger.info(f"Connected to TCP server")
            self.connected = True
            return 0

        except OSError as e:
            logger.error(f"Cannot connect to TCP remote ({self.proto}): {e}")
            return -1

    def sendall(self, data):
        """Send all data, handling partial sends"""
        total_sent = 0
        data_len = len(data)

        while total_sent < data_len:
            try:
                sent = self.tcp_sock.send(data[total_sent:])
                if sent == 0:
                    raise RuntimeError("Socket connection broken")
                total_sent += sent
            except OSError as e:
                logger.error(f"Send error: {e}")
                return -1

        return 0

    def send_tcp_data(self):
        """Send test data to server"""
        ret = self.sendall(LOREM_IPSUM)

        if ret < 0:
            logger.error(f"{self.proto} TCP: Failed to send data")

        return ret

    def process_tcp_proto(self):
        """Main loop - continuously send data"""
        try:
            while self.connected:
                self.counter += 1

                # Log progress every 1000 packets
                if self.counter % 1000 == 0:
                    logger.info(f"{self.proto} TCP: Exchanged {self.counter} packets")

                # Sleep between sends (100ms)
                time.sleep(SEND_INTERVAL_MS / 1000.0)

                # Send data
                ret = self.send_tcp_data()
                if ret < 0:
                    return ret

        except KeyboardInterrupt:
            logger.info("Interrupted by user")
            return 0
        except OSError as e:
            logger.error(f"Socket error: {e}")
            return -1

        return 0

    def start_tcp(self):
        """Start TCP connection"""
        logger.info("Starting...")
        return self.start_tcp_proto()

    def run_tcp(self):
        """Run the TCP client loop"""
        return self.process_tcp_proto()

    def stop_tcp(self):
        """Stop TCP connection"""
        logger.info("Stopping...")
        self.connected = False
        if self.tcp_sock:
            try:
                self.tcp_sock.close()
            except OSError:
                pass
            self.tcp_sock = None

    def run(self):
        """Main client loop"""
        logger.info("Run echo client")
        logger.info("Network connected")

        while True:
            ret = self.start_tcp()

            if ret == 0:
                ret = self.run_tcp()

            self.stop_tcp()

            # If interrupted, break
            if ret == 0:
                break

            # Otherwise retry after delay
            logger.info("Retrying connection in 5 seconds...")
            time.sleep(5)


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(description='IPv6 TCP Echo Client')
    parser.add_argument('server', help='IPv6 address of the server (e.g., fd00::1 or 2001:db8::1)')
    parser.add_argument('--port', type=int, default=PEER_PORT, help=f'Server port (default: {PEER_PORT})')

    args = parser.parse_args()

    client = TCPClient(args.server, args.port)

    try:
        client.run()
    except KeyboardInterrupt:
        logger.info("\nShutting down...")
    finally:
        client.stop_tcp()

    return 0


if __name__ == "__main__":
    sys.exit(main())