#!/usr/bin/env python3
"""
simple_server.py - IPv6 TCP echo server
Semantically equivalent to the Zephyr simple-server.c
"""

import socket
import logging
import sys

# Configure logging
logging.basicConfig(
    level=logging.DEBUG,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger('net_simple_server')

# Configuration
SERVER_IPV6_ADDR = '::'  # Listen on all IPv6 interfaces
SERVER_PORT = 4242
BUFFER_SIZE = 128


class UDPServer:
    """IPv6 UDP Server"""

    def __init__(self, addr, port):
        self.addr = addr
        self.port = port
        self.udp_sock = None
        self.proto = "IPv6"

    def start_udp_proto(self):
        """Creates and binds UDP socket"""
        try:
            # Create IPv6 UDP socket
            self.udp_sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM, socket.IPPROTO_UDP)

            # Set socket options
            # IPV6_V6ONLY = 1 (only accept IPv6 connections)
            self.udp_sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)

            # Allow address reuse
            self.udp_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

            # Bind to address and port
            self.udp_sock.bind((self.addr, self.port))

            logger.info(f"UDP server listening on [{self.addr}]:{self.port}")
            return 0

        except OSError as e:
            logger.error(f"Failed to start UDP server: {e}")
            return -1

    def start_udp(self):
        """Main server loop - accepts and handles connections"""
        if self.start_udp_proto() < 0:
            logger.error("Failed to start UDP server")
            return

        logger.info("What's up?")
        logger.info("Network connected")

        # Accept connections in a loop
        while True:
            try:
                logger.info("Waiting for stuff...")
                while True:
                    try:
                        data, client_addr = self.udp_sock.recvfrom(BUFFER_SIZE)

                        if not data:
                            logger.info("Client disconnected")
                            break

                        recv_len = len(data)
                        logger.info(f"Received {recv_len} bytes: {data.decode('utf-8', errors='replace')}")

                    except OSError as e:
                        logger.error(f"Receive error: {e}")
                        break

            except KeyboardInterrupt:
                logger.info("Server shutting down...")
                break
            except OSError as e:
                logger.error(f"Accept error: {e}")
                continue

    def stop_udp(self):
        """Stop the UDP server"""
        if self.udp_sock:
            self.udp_sock.close()
            logger.info("UDP server stopped")

def main():
    """Main entry point"""
    server = UDPServer(SERVER_IPV6_ADDR, SERVER_PORT)

    try:
        server.start_udp()
    finally:
        server.stop_udp()

    return 0


if __name__ == "__main__":
    sys.exit(main())