/* echo-server.c - Simplified IPv6 TCP echo server */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_echo_server_sample, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>
#include <zephyr/linker/sections.h>
#include <errno.h>
#include <zephyr/shell/shell.h>

#include <zephyr/net/net_core.h>

#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/conn_mgr_monitor.h>

#include "common.h"

#define APP_BANNER "Run echo server"

static struct k_sem quit_lock;
static struct net_mgmt_event_callback mgmt_cb;
static bool connected;
K_SEM_DEFINE(run_app, 0, 1);
static bool want_to_quit;

#define EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)

APP_DMEM struct configs conf = {
	.ipv6 = {
		.proto = "IPv6",
	},
};

void quit(void)
{
	k_sem_give(&quit_lock);
}

static void start_udp_and_tcp(void)
{
	LOG_INF("Starting...");
	start_tcp();
}

static void stop_udp_and_tcp(void)
{
	LOG_INF("Stopping...");
	stop_tcp();
}

static void event_handler(struct net_mgmt_event_callback *cb,
			  uint32_t mgmt_event, struct net_if *iface)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(cb);

	if ((mgmt_event & EVENT_MASK) != mgmt_event) {
		return;
	}

	if (want_to_quit) {
		k_sem_give(&run_app);
		want_to_quit = false;
	}

	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		LOG_INF("Network connected");

		connected = true;
		k_sem_give(&run_app);

		return;
	}

	if (mgmt_event == NET_EVENT_L4_DISCONNECTED) {
		if (connected == false) {
			LOG_INF("Waiting network to be connected");
		} else {
			LOG_INF("Network disconnected");
			connected = false;
		}

		k_sem_reset(&run_app);

		return;
	}
}

static void init_app(void)
{
	k_sem_init(&quit_lock, 0, K_SEM_MAX_LIMIT);

	LOG_INF(APP_BANNER);

	if (IS_ENABLED(CONFIG_NET_CONNECTION_MANAGER)) {
		net_mgmt_init_event_callback(&mgmt_cb,
					     event_handler, EVENT_MASK);
		net_mgmt_add_event_callback(&mgmt_cb);

		conn_mgr_mon_resend_status();
	}
}

static int cmd_sample_quit(const struct shell *sh,
			  size_t argc, char *argv[])
{
	want_to_quit = true;

	conn_mgr_mon_resend_status();

	quit();

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sample_commands,
	SHELL_CMD(quit, NULL,
		  "Quit the sample application\n",
		  cmd_sample_quit),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(sample, &sample_commands,
		   "Sample application commands", NULL);

int main(void)
{
	init_app();

	if (!IS_ENABLED(CONFIG_NET_CONNECTION_MANAGER)) {
		k_sem_give(&run_app);
	}

	k_sem_take(&run_app, K_FOREVER);

	start_udp_and_tcp();

	k_sem_take(&quit_lock, K_FOREVER);

	if (connected) {
		stop_udp_and_tcp();
	}
	return 0;
}

/* TCP implementation */

#include <zephyr/net/socket.h>

#define MAX_CLIENT_QUEUE CONFIG_NET_SAMPLE_NUM_HANDLERS

K_THREAD_STACK_ARRAY_DEFINE(tcp6_handler_stack, CONFIG_NET_SAMPLE_NUM_HANDLERS,
			    STACK_SIZE);
static struct k_thread tcp6_handler_thread[CONFIG_NET_SAMPLE_NUM_HANDLERS];
static APP_BMEM bool tcp6_handler_in_use[CONFIG_NET_SAMPLE_NUM_HANDLERS];

static void process_tcp6(void);

K_THREAD_DEFINE(tcp6_thread_id, STACK_SIZE,
		process_tcp6, NULL, NULL, NULL,
		THREAD_PRIORITY, 0, -1);

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

static int start_tcp_proto(struct data *data,
			   struct sockaddr *bind_addr,
			   socklen_t bind_addrlen)
{
	int optval;
	int ret;

	data->tcp.sock = socket(bind_addr->sa_family, SOCK_STREAM,
				IPPROTO_TCP);
	if (data->tcp.sock < 0) {
		LOG_ERR("Failed to create TCP socket (%s): %d", data->proto,
			errno);
		return -errno;
	}

	if (bind_addr->sa_family == AF_INET6) {
		optval = IPV6_PREFER_SRC_PUBLIC;
		(void)setsockopt(data->tcp.sock, IPPROTO_IPV6,
				 IPV6_ADDR_PREFERENCES,
				 &optval, sizeof(optval));

		optval = 1;
		(void)setsockopt(data->tcp.sock, IPPROTO_IPV6, IPV6_V6ONLY,
				 &optval, sizeof(optval));
	}

	ret = bind(data->tcp.sock, bind_addr, bind_addrlen);
	if (ret < 0) {
		LOG_ERR("Failed to bind TCP socket (%s): %d", data->proto,
			errno);
		return -errno;
	}

	ret = listen(data->tcp.sock, MAX_CLIENT_QUEUE);
	if (ret < 0) {
		LOG_ERR("Failed to listen on TCP socket (%s): %d",
			data->proto, errno);
		ret = -errno;
	}

	return ret;
}

static void handle_data(void *ptr1, void *ptr2, void *ptr3)
{
	int slot = POINTER_TO_INT(ptr1);
	struct data *data = ptr2;
	bool *in_use = ptr3;
	int offset = 0;
	int received;
	int client;
	int ret;

	client = data->tcp.accepted[slot].sock;

	do {
		received = recv(client,
			data->tcp.accepted[slot].recv_buffer + offset,
			sizeof(data->tcp.accepted[slot].recv_buffer) - offset,
			0);

		if (received == 0) {
			LOG_INF("TCP (%s): Connection closed", data->proto);
			break;
		} else if (received < 0) {
			LOG_ERR("TCP (%s): Connection error %d", data->proto,
				errno);
			break;
		} else {
			atomic_add(&data->tcp.bytes_received, received);
		}

		offset += received;

		ret = sendall(client,
			      data->tcp.accepted[slot].recv_buffer,
			      offset);
		if (ret < 0) {
			LOG_ERR("TCP (%s): Failed to send, "
				"closing socket", data->proto);
			break;
		}

		LOG_DBG("TCP (%s): Received and replied with %d bytes",
			data->proto, offset);

		if (++data->tcp.accepted[slot].counter % 1000 == 0U) {
			LOG_INF("%s TCP: Sent %u packets", data->proto,
				data->tcp.accepted[slot].counter);
		}

		offset = 0;
	} while (true);

	*in_use = false;

	(void)close(client);

	data->tcp.accepted[slot].sock = -1;
}

static int get_free_slot(struct data *data)
{
	int i;

	for (i = 0; i < CONFIG_NET_SAMPLE_NUM_HANDLERS; i++) {
		if (data->tcp.accepted[i].sock < 0) {
			return i;
		}
	}

	return -1;
}

static int process_tcp(struct data *data)
{
	int client;
	int slot;
	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);

	LOG_INF("Waiting for TCP connection on port %d (%s)...",
		MY_PORT, data->proto);

	client = accept(data->tcp.sock, (struct sockaddr *)&client_addr,
			&client_addr_len);
	if (client < 0) {
		LOG_ERR("%s accept error (%d)", data->proto, -errno);
		return -errno;
	}

	slot = get_free_slot(data);
	if (slot < 0) {
		LOG_ERR("Cannot accept more connections");
		close(client);
		return 0;
	}

	data->tcp.accepted[slot].sock = client;

	LOG_INF("TCP (%s): Accepted connection", data->proto);

	if (client_addr.sin_family == AF_INET6) {
		tcp6_handler_in_use[slot] = true;

		k_thread_create(
			&tcp6_handler_thread[slot],
			tcp6_handler_stack[slot],
			K_THREAD_STACK_SIZEOF(tcp6_handler_stack[slot]),
			handle_data,
			INT_TO_POINTER(slot), data, &tcp6_handler_in_use[slot],
			THREAD_PRIORITY, 0,
			K_NO_WAIT);

		if (IS_ENABLED(CONFIG_THREAD_NAME)) {
			char name[16];
			snprintk(name, sizeof(name), "tcp6[%3d]", (uint8_t)slot);
			k_thread_name_set(&tcp6_handler_thread[slot], name);
		}
	}

	return 0;
}

static void process_tcp6(void)
{
	int ret;
	struct sockaddr_in6 addr6;

	(void)memset(&addr6, 0, sizeof(addr6));
	addr6.sin6_family = AF_INET6;
	addr6.sin6_port = htons(MY_PORT);

	ret = start_tcp_proto(&conf.ipv6, (struct sockaddr *)&addr6,
			      sizeof(addr6));
	if (ret < 0) {
		quit();
		return;
	}

	while (ret == 0) {
		ret = process_tcp(&conf.ipv6);
		if (ret != 0) {
			break;
		}
	}

	quit();
}

static void print_stats(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct data *data = CONTAINER_OF(dwork, struct data, tcp.stats_print);
	int total_received = atomic_get(&data->tcp.bytes_received);

	if (total_received) {
		if ((total_received / STATS_TIMER) < 1024) {
			LOG_INF("%s TCP: Received %d B/sec", data->proto,
				total_received / STATS_TIMER);
		} else {
			LOG_INF("%s TCP: Received %d KiB/sec", data->proto,
				total_received / 1024 / STATS_TIMER);
		}

		atomic_set(&data->tcp.bytes_received, 0);
	}

	k_work_reschedule(&data->tcp.stats_print, K_SECONDS(STATS_TIMER));
}

void start_tcp(void)
{
	int i;

	for (i = 0; i < CONFIG_NET_SAMPLE_NUM_HANDLERS; i++) {
		conf.ipv6.tcp.accepted[i].sock = -1;
		tcp6_handler_in_use[i] = false;
	}

	k_work_init_delayable(&conf.ipv6.tcp.stats_print, print_stats);
	k_thread_start(tcp6_thread_id);
	k_work_reschedule(&conf.ipv6.tcp.stats_print, K_SECONDS(STATS_TIMER));
}

void stop_tcp(void)
{
	int i;

	k_thread_abort(tcp6_thread_id);
	if (conf.ipv6.tcp.sock >= 0) {
		(void)close(conf.ipv6.tcp.sock);
	}

	for (i = 0; i < CONFIG_NET_SAMPLE_NUM_HANDLERS; i++) {
		if (tcp6_handler_in_use[i] == true) {
			k_thread_abort(&tcp6_handler_thread[i]);
		}

		if (conf.ipv6.tcp.accepted[i].sock >= 0) {
			(void)close(conf.ipv6.tcp.accepted[i].sock);
		}
	}
}