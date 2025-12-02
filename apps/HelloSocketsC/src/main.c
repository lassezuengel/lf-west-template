#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(tcp_client, LOG_LEVEL_INF);

#define SERVER_PORT 4242
#define RECV_BUF_SIZE 128

#define SERVER_ADDR "2001:db8::2"

static K_SEM_DEFINE(network_connected, 0, 1);
static bool is_connected = false;

static void net_event_handler(struct net_mgmt_event_callback *cb,
                              uint32_t mgmt_event, struct net_if *iface)
{
	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		printk("Network connected");
		is_connected = true;
		k_sem_give(&network_connected);
	} else if (mgmt_event == NET_EVENT_L4_DISCONNECTED) {
		printk("Network disconnected");
		is_connected = false;
	}
}

int main(void)
{
	static struct net_mgmt_event_callback mgmt_cb;
	struct sockaddr_in6 server_addr;
	int sock;
	char buf[RECV_BUF_SIZE];
	int ret;
	int msg_count = 0;

	printk("Starting TCP client");

	/* Setup network event callback */
	net_mgmt_init_event_callback(&mgmt_cb, net_event_handler,
	                             NET_EVENT_L4_CONNECTED | 
	                             NET_EVENT_L4_DISCONNECTED);
	net_mgmt_add_event_callback(&mgmt_cb);
	
	/* Trigger connection status check */
	conn_mgr_mon_resend_status();

	/* Wait for network to be ready */
	printk("Waiting for network connection...");
	k_sem_take(&network_connected, K_FOREVER);
	printk("Network ready");

	/* Prepare server address */
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin6_family = AF_INET6;
	server_addr.sin6_port = htons(SERVER_PORT);
	
	ret = inet_pton(AF_INET6, SERVER_ADDR, &server_addr.sin6_addr);
	if (ret != 1) {
		printk("Invalid server address");
		return -1;
	}

	/* Connection loop */
	while (1) {
		if (!is_connected) {
			printk("Network not connected, waiting...");
			k_sleep(K_SECONDS(5));
			continue;
		}

		/* Create socket */
		sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
		if (sock < 0) {
			printk("Failed to create socket: %d", errno);
			k_sleep(K_SECONDS(5));
			continue;
		}

		/* Connect to server */
		printk("Connecting to server...");
		ret = connect(sock, (struct sockaddr *)&server_addr, 
		              sizeof(server_addr));
		if (ret < 0) {
			printk("Connect failed: %d", errno);
			close(sock);
			k_sleep(K_SECONDS(5));
			continue;
		}

		printk("Connected to server");

		/* Send and receive messages */
		for (int i = 0; i < 10; i++) {
			char msg[32];
			snprintf(msg, sizeof(msg), "Message %d", msg_count++);
			
			printk("Sending: %s", msg);
			ret = send(sock, msg, strlen(msg), 0);
			if (ret < 0) {
				printk("Send failed: %d", errno);
				break;
			}

			/* Receive echo */
			ret = recv(sock, buf, sizeof(buf) - 1, 0);
			if (ret <= 0) {
				if (ret == 0) {
					printk("Server closed connection");
				} else {
					printk("Receive failed: %d", errno);
				}
				break;
			}

			buf[ret] = '\0';
			printk("Received: %s", buf);

			k_sleep(K_SECONDS(2));
		}

		printk("Closing connection");
		close(sock);
		
		/* Wait before reconnecting */
		k_sleep(K_SECONDS(10));
	}

	return 0;
}