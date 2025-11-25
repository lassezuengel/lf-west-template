
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <string.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/ieee802154_mgmt.h>

#define SERVER_PORT 4242

// Use simplified addressing
#define SERVER_ADDR "2001:db8::1"

void main(void)
{
	printk("Starting 6LoWPAN TCP test (server)\n");
	
    #if 1
	k_sleep(K_SECONDS(2)); // Wait for network setup

    {
        struct net_if *iface = net_if_get_default();

        uint16_t pan_id = 0xabcd;
        net_mgmt(NET_REQUEST_IEEE802154_SET_PAN_ID, iface, &pan_id, sizeof(pan_id));

        struct ieee802154_req_params params = { .channel = 26 };
        net_mgmt(NET_REQUEST_IEEE802154_SET_CHANNEL, iface, &params, sizeof(params));

        // Bring interface up and wait for link-local
        net_if_up(iface);
        k_sleep(K_SECONDS(2));

        struct in6_addr addr;
        inet_pton(AF_INET6, "2001:db8::1", &addr);
        net_if_ipv6_addr_add(iface, &addr, NET_ADDR_MANUAL, 0);
        
        // Verify address
        char addr_str[NET_IPV6_ADDR_LEN];
        struct net_if_ipv6 *ipv6 = iface->config.ip.ipv6;
        if (ipv6 && ipv6->unicast[0].is_used) {
            net_addr_ntop(AF_INET6, &ipv6->unicast[0].address.in6_addr, addr_str, sizeof(addr_str));
            printk("IPv6 addr: %s\n", addr_str);
        } else {
            printk("NO IPv6 ADDRESS SET!\n");
        }
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