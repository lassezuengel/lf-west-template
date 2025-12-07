/* echo-client.c - Simplified IPv6 TCP echo client */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_echo_client_sample, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>
#include <errno.h>
#include <stdio.h>

#include <zephyr/posix/sys/eventfd.h>

#include <zephyr/net/socket.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/conn_mgr_monitor.h>

#include "common.h"

#define APP_BANNER "Run echo client"

#define INVALID_SOCK (-1)

#define EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)

const char lorem_ipsum[] = "LOREM_IPSUM";
const int ipsum_len = sizeof(lorem_ipsum) - 1;

APP_DMEM struct configs conf = {
	.ipv6 = {
		.proto = "IPv6",
		.tcp.sock = INVALID_SOCK,
	},
};

static APP_BMEM struct pollfd fds[2];
static APP_BMEM int nfds;

static APP_BMEM bool connected;

K_SEM_DEFINE(run_app, 0, 1);

static struct net_mgmt_event_callback mgmt_cb;

static void prepare_fds(void)
{
	nfds = 0;

	fds[nfds].fd = eventfd(0, 0);
	fds[nfds].events = POLLIN;
	nfds++;

	if (conf.ipv6.tcp.sock >= 0) {
		fds[nfds].fd = conf.ipv6.tcp.sock;
		fds[nfds].events = POLLIN;
		nfds++;
	}
}

static void wait(void)
{
	int ret;

	ret = poll(fds, nfds, -1);
	if (ret < 0) {
		LOG_ERR("Error in poll:%d", errno);
		return;
	}

	if (ret > 0 && fds[0].revents) {
		eventfd_t value;
		eventfd_read(fds[0].fd, &value);
		LOG_DBG("Received restart event.");
		return;
	}
}

static int start_tcp(void);
static int process_tcp(void);
static void stop_tcp(void);

static int start_udp_and_tcp(void)
{
	int ret;

	LOG_INF("Starting...");

	ret = start_tcp();
	if (ret < 0) {
		return ret;
	}

	prepare_fds();

	return 0;
}

static int run_udp_and_tcp(void)
{
	int ret;

	wait();

	ret = process_tcp();
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static void stop_udp_and_tcp(void)
{
	LOG_INF("Stopping...");
	stop_tcp();
}

static void event_handler(struct net_mgmt_event_callback *cb,
			  uint32_t mgmt_event, struct net_if *iface)
{
	if ((mgmt_event & EVENT_MASK) != mgmt_event) {
		return;
	}

	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		LOG_INF("Network connected");
		connected = true;
		k_sem_give(&run_app);
		return;
	}

	if (mgmt_event == NET_EVENT_L4_DISCONNECTED) {
		LOG_INF("Network disconnected");
		connected = false;
		k_sem_reset(&run_app);
		return;
	}
}

static void init_app(void)
{
	LOG_INF(APP_BANNER);

	if (IS_ENABLED(CONFIG_NET_CONNECTION_MANAGER)) {
		net_mgmt_init_event_callback(&mgmt_cb,
					     event_handler, EVENT_MASK);
		net_mgmt_add_event_callback(&mgmt_cb);

		conn_mgr_mon_resend_status();
	}
}

static void start_client(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int iterations = CONFIG_NET_SAMPLE_SEND_ITERATIONS;
	int i = 0;
	int ret;

	while (iterations == 0 || i < iterations) {
		k_sem_take(&run_app, K_FOREVER);

		ret = start_udp_and_tcp();

		while (connected && (ret == 0)) {
			ret = run_udp_and_tcp();

			if (iterations > 0) {
				i++;
				if (i >= iterations) {
					break;
				}
			}
		}

		stop_udp_and_tcp();
	}
}

int main(void)
{
	init_app();

	if (!IS_ENABLED(CONFIG_NET_CONNECTION_MANAGER)) {
		connected = true;
		k_sem_give(&run_app);
	}

	k_thread_priority_set(k_current_get(), THREAD_PRIORITY);

	start_client(NULL, NULL, NULL);
	return 0;
}

/* TCP implementation */

#include <zephyr/random/random.h>

#define RECV_BUF_SIZE 128

static ssize_t sendall(int sock, const void *buf, size_t len)
{
	while (len) {
		ssize_t out_len = send(sock, buf, len, 0);

		if (out_len < 0) {
			return out_len;
		}
		buf = (const char *)buf + out_len;
		len -= out_len;
	}

	return 0;
}

static int send_tcp_data(struct sample_data *data)
{
	int ret;

	do {
		data->tcp.expecting = sys_rand32_get() % ipsum_len;
	} while (data->tcp.expecting == 0U);

	data->tcp.received = 0U;

	ret = sendall(data->tcp.sock, lorem_ipsum, data->tcp.expecting);

	if (ret < 0) {
		LOG_ERR("%s TCP: Failed to send data, errno %d", data->proto,
			errno);
	} else {
		if (PRINT_PROGRESS) {
			LOG_DBG("%s TCP: Sent %d bytes", data->proto,
				data->tcp.expecting);
		}
	}

	return ret;
}

static int compare_tcp_data(struct sample_data *data, const char *buf, uint32_t received)
{
	if (data->tcp.received + received > data->tcp.expecting) {
		LOG_ERR("Too much data received: TCP %s", data->proto);
		return -EIO;
	}

	if (memcmp(buf, lorem_ipsum + data->tcp.received, received) != 0) {
		LOG_ERR("Invalid data received: TCP %s", data->proto);
		return -EIO;
	}

	return 0;
}

static int start_tcp_proto(struct sample_data *data, sa_family_t family,
			   struct sockaddr *addr, socklen_t addrlen)
{
	int ret;

	data->tcp.sock = socket(family, SOCK_STREAM, IPPROTO_TCP);
	if (data->tcp.sock < 0) {
		LOG_ERR("Failed to create TCP socket (%s): %d", data->proto,
			errno);
		return -errno;
	}

	ret = connect(data->tcp.sock, addr, addrlen);
	if (ret < 0) {
		LOG_ERR("Cannot connect to TCP remote (%s): %d", data->proto,
			errno);
		ret = -errno;
	}

	return ret;
}

static int process_tcp_proto(struct sample_data *data)
{
	int ret, received;
	char buf[RECV_BUF_SIZE];

	do {
		received = recv(data->tcp.sock, buf, sizeof(buf), MSG_DONTWAIT);

		if (received == 0) {
			ret = -EIO;
			continue;
		} else if (received < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				ret = 0;
			} else {
				ret = -errno;
			}
			continue;
		}

		ret = compare_tcp_data(data, buf, received);
		if (ret != 0) {
			break;
		}

		data->tcp.received += received;
		if (data->tcp.received < data->tcp.expecting) {
			continue;
		}

		if (PRINT_PROGRESS) {
			LOG_DBG("%s TCP: Received and compared %d bytes, all ok",
				data->proto, data->tcp.received);
		}

		if (++data->tcp.counter % 1000 == 0U) {
			LOG_INF("%s TCP: Exchanged %u packets", data->proto,
				data->tcp.counter);
		}

		ret = send_tcp_data(data);
		break;
	} while (received > 0);

	return ret;
}

static int start_tcp(void)
{
	int ret = 0;
	struct sockaddr_in6 addr6;

	addr6.sin6_family = AF_INET6;
	addr6.sin6_port = htons(PEER_PORT);
	inet_pton(AF_INET6, CONFIG_NET_CONFIG_PEER_IPV6_ADDR,
		  &addr6.sin6_addr);

	ret = start_tcp_proto(&conf.ipv6, AF_INET6,
			      (struct sockaddr *)&addr6,
			      sizeof(addr6));
	if (ret < 0) {
		return ret;
	}

	ret = send_tcp_data(&conf.ipv6);

	return ret;
}

static int process_tcp(void)
{
	int ret = 0;

	ret = process_tcp_proto(&conf.ipv6);
	if (ret < 0) {
		return ret;
	}

	return ret;
}

static void stop_tcp(void)
{
	if (conf.ipv6.tcp.sock >= 0) {
		(void)close(conf.ipv6.tcp.sock);
	}
}