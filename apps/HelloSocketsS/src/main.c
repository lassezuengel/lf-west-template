
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <string.h>

#define SERVER_PORT 4242

// Use simplified addressing
#define SERVER_ADDR "2001:db8::1"

void main(void)
{
	printk("Starting 6LoWPAN TCP test (server)\n");
	
    #if 1
	k_sleep(K_SECONDS(3)); // Wait for network setup

    {
        struct net_if *iface = net_if_get_default();
        struct in6_addr addr;
        inet_pton(AF_INET6, "2001:db8::1", &addr);
        net_if_ipv6_addr_add(iface, &addr, NET_ADDR_MANUAL, 0);
    }

	// Server mode
	int srv_sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
	if (srv_sock < 0) {
		printk("Server socket failed: %d\n", errno);
		return;
	}

	struct sockaddr_in6 addr = {
		.sin6_family = AF_INET6,
		.sin6_port = htons(SERVER_PORT),
		.sin6_addr = IN6ADDR_ANY_INIT
	};

	if (bind(srv_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		printk("Bind failed: %d\n", errno);
		close(srv_sock);
		return;
	}

	if (listen(srv_sock, 1) < 0) {
		printk("Listen failed: %d\n", errno);
		close(srv_sock);
		return;
	}

	printk("Server listening on port %d...\n", SERVER_PORT);

	while (1) {
		struct sockaddr_in6 client_addr;
		socklen_t client_len = sizeof(client_addr);
		
		int client_sock = accept(srv_sock, (struct sockaddr *)&client_addr, &client_len);
		if (client_sock < 0) {
			printk("Accept failed: %d\n", errno);
			continue;
		}

		printk("Client connected!\n");

		char buf[64];
		int ret = recv(client_sock, buf, sizeof(buf) - 1, 0);
		if (ret > 0) {
			buf[ret] = '\0';
			printk("Received: %s\n", buf);
			send(client_sock, "ACK", 3, 0);
		}

		close(client_sock);
		printk("Connection closed\n");
	}
#endif
}