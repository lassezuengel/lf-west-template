#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(tcp_server, LOG_LEVEL_DBG);

#define SERVER_PORT 4242
#define RECV_BUF_SIZE 1280

static K_SEM_DEFINE(run_app, 0, 1);
static bool connected = false;

static void event_handler(struct net_mgmt_event_callback *cb,
			  uint32_t mgmt_event, struct net_if *iface)
{
	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		printk("Network connected");
		connected = true;
		k_sem_give(&run_app);
	} else if (mgmt_event == NET_EVENT_L4_DISCONNECTED) {
		printk("Network disconnected");
		connected = false;
		k_sem_reset(&run_app);
	}
}

int main(void)
{
	static struct net_mgmt_event_callback mgmt_cb;
	struct sockaddr_in6 bind_addr, client_addr;
	socklen_t client_addr_len;
	int listen_sock, client_sock;
	char recv_buf[RECV_BUF_SIZE];
	int ret, optval;

	printk("Starting TCP echo server");

	/* Register network event callback */
	net_mgmt_init_event_callback(&mgmt_cb, event_handler,
				     NET_EVENT_L4_CONNECTED |
				     NET_EVENT_L4_DISCONNECTED);
	net_mgmt_add_event_callback(&mgmt_cb);
	conn_mgr_mon_resend_status();

	/* Wait for network */
	printk("Waiting for network...");
	k_sem_take(&run_app, K_FOREVER);

	/* Create socket */
	listen_sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
	if (listen_sock < 0) {
		printk("socket() failed: %d", errno);
		return -1;
	}

	/* IPv6-only (don't map to IPv4) */
	optval = 1;
	setsockopt(listen_sock, IPPROTO_IPV6, IPV6_V6ONLY,
		   &optval, sizeof(optval));

	/* Bind */
	memset(&bind_addr, 0, sizeof(bind_addr));
	bind_addr.sin6_family = AF_INET6;
	bind_addr.sin6_port = htons(SERVER_PORT);
	bind_addr.sin6_addr = in6addr_any;

	ret = bind(listen_sock, (struct sockaddr *)&bind_addr,
		   sizeof(bind_addr));
	if (ret < 0) {
		printk("bind() failed: %d", errno);
		close(listen_sock);
		return -1;
	}

	/* Listen */
	ret = listen(listen_sock, 1);
	if (ret < 0) {
		printk("listen() failed: %d", errno);
		close(listen_sock);
		return -1;
	}

	printk("Listening on port %d", SERVER_PORT);

	/* Accept loop */
	while (connected) {
		client_addr_len = sizeof(client_addr);
		
		printk("Waiting for client...");
		client_sock = accept(listen_sock,
				     (struct sockaddr *)&client_addr,
				     &client_addr_len);
		if (client_sock < 0) {
			printk("accept() failed: %d", errno);
			continue;
		}

		printk("Client connected");

		/* Echo loop */
		while (1) {
			ret = recv(client_sock, recv_buf, sizeof(recv_buf), 0);
			if (ret <= 0) {
				if (ret == 0) {
					printk("Client disconnected");
				} else {
					printk("recv() failed: %d", errno);
				}
				break;
			}

			printk("Received %d bytes", ret);

			/* Send all data back */
			int sent = 0;
			while (sent < ret) {
				int n = send(client_sock, recv_buf + sent,
					     ret - sent, 0);
				if (n < 0) {
					printk("send() failed: %d", errno);
					goto close_client;
				}
				sent += n;
			}

			LOG_DBG("Echoed %d bytes", ret);
		}

close_client:
		close(client_sock);
	}

	close(listen_sock);
	return 0;
}