#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <string.h>

#define SERVER_PORT 4242
#define IS_SERVER 0  // Set to 1 for server, 0 for client

// Use simplified addressing
#define SERVER_ADDR "2001:db8::1"

void main(void)
{
	printk("Starting 6LoWPAN TCP test (client)\n");
	
	k_sleep(K_SECONDS(3)); // Wait for network setup

#if 1
	// Client mode
	k_sleep(K_SECONDS(3)); // Extra delay for server to start

	int sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		printk("Client socket failed: %d\n", errno);
		return;
	}

	struct sockaddr_in6 addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin6_family = AF_INET6;
	addr.sin6_port = htons(SERVER_PORT);
	
	if (inet_pton(AF_INET6, SERVER_ADDR, &addr.sin6_addr) != 1) {
		printk("Invalid address\n");
		close(sock);
		return;
	}

	printk("Connecting to server...\n");
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		printk("Connect failed: %d\n", errno);
		close(sock);
		return;
	}

	printk("Connected! Sending data...\n");
	const char *msg = "Hello from nRF52840!";
	send(sock, msg, strlen(msg), 0);

	char buf[64];
	int ret = recv(sock, buf, sizeof(buf) - 1, 0);
	if (ret > 0) {
		buf[ret] = '\0';
		printk("Server replied: %s\n", buf);
	}

	close(sock);
	printk("Done!\n");
#endif
}