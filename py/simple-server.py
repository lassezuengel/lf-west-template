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


class TCPServer:
    """IPv6 TCP Server"""

    def __init__(self, addr, port):
        self.addr = addr
        self.port = port
        self.tcp_sock = None
        self.proto = "IPv6"

    def start_tcp_proto(self):
        """Creates and binds TCP listening socket"""
        try:
            # Create IPv6 TCP socket
            self.tcp_sock = socket.socket(socket.AF_INET6, socket.SOCK_STREAM, socket.IPPROTO_TCP)

            # Set socket options
            # IPV6_V6ONLY = 1 (only accept IPv6 connections)
            self.tcp_sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)

            # Allow address reuse
            self.tcp_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

            # Bind to address and port
            self.tcp_sock.bind((self.addr, self.port))

            # Listen with backlog of 1
            self.tcp_sock.listen(1)

            logger.info(f"TCP server listening on [{self.addr}]:{self.port}")
            return 0

        except OSError as e:
            logger.error(f"Failed to start TCP server: {e}")
            return -1

    def start_tcp(self):
        """Main server loop - accepts and handles connections"""
        if self.start_tcp_proto() < 0:
            logger.error("Failed to start TCP server")
            return

        logger.info("What's up?")
        logger.info("Network connected")

        # Accept connections in a loop
        while True:
            try:
                logger.info("Waiting for connection...")
                client_sock, client_addr = self.tcp_sock.accept()

                logger.info(f"Client connected from {client_addr}")

                # Handle client communication
                while True:
                    try:
                        data = client_sock.recv(BUFFER_SIZE)

                        if not data:
                            logger.info("Client disconnected")
                            break

                        recv_len = len(data)
                        logger.info(f"Received {recv_len} bytes: {data.decode('utf-8', errors='replace')}")

                        # Echo back (optional - uncomment if you want echo functionality)
                        # client_sock.sendall(data)

                    except OSError as e:
                        logger.error(f"Receive error: {e}")
                        break

                client_sock.close()

            except KeyboardInterrupt:
                logger.info("Server shutting down...")
                break
            except OSError as e:
                logger.error(f"Accept error: {e}")
                continue

    def stop_tcp(self):
        """Stop the TCP server"""
        if self.tcp_sock:
            self.tcp_sock.close()
            logger.info("TCP server stopped")


def main():
    """Main entry point"""
    server = TCPServer(SERVER_IPV6_ADDR, SERVER_PORT)

    try:
        server.start_tcp()
    finally:
        server.stop_tcp()

    return 0


if __name__ == "__main__":
    sys.exit(main())