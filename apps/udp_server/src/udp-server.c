/* udp-server.c - Simplified IPv6 UDP echo server */
/* This is actually a receiver of data, but persistent,
   so we call it the server. */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_udp_server_sample, LOG_LEVEL_INF);

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/sections.h>
#include <zephyr/posix/poll.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include <zephyr/net/net_core.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_mgmt.h>

#include "common.h"

static struct k_sem quit_lock;
static struct net_mgmt_event_callback mgmt_cb;
static bool connected;
K_SEM_DEFINE(run_app, 0, 1);
static bool want_to_quit = false;

#define EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)

// Network management event callback handler.
static void event_handler(struct net_mgmt_event_callback *cb,
                          uint32_t mgmt_event, struct net_if *iface) {
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

// Initializes the connection manager.
static void init_connection_manager(void) {
  k_sem_init(&quit_lock, 0, K_SEM_MAX_LIMIT);

  if (IS_ENABLED(CONFIG_NET_CONNECTION_MANAGER)) {
    net_mgmt_init_event_callback(&mgmt_cb,
                                 event_handler, EVENT_MASK);
    net_mgmt_add_event_callback(&mgmt_cb);

    conn_mgr_mon_resend_status();
  } else {
    k_sem_give(&run_app);
  }
}

int main(void) {
  LOG_INF("What's up? UDP server started!");

  init_connection_manager();

  k_sem_take(&run_app, K_FOREVER);

  start_udp();

  k_sem_take(&quit_lock, K_FOREVER);

  if (connected) {
    stop_udp();
  }
  return 0;
}

/* UDP implementation */

// Creates and binds UDP listening socket.
static int start_udp_proto(struct data *data,
                           struct sockaddr *bind_addr,
                           socklen_t bind_addrlen) {
  int optval;
  int ret;

  data->udp_sock = socket(bind_addr->sa_family, SOCK_DGRAM,
                          IPPROTO_UDP);
  if (data->udp_sock < 0) {
    LOG_ERR("Failed to create UDP socket (%s): %d", data->proto,
            errno);
    return -errno;
  }

  if (bind_addr->sa_family == AF_INET6) {
    optval = IPV6_PREFER_SRC_PUBLIC;
    (void)setsockopt(data->udp_sock, IPPROTO_IPV6,
                     IPV6_ADDR_PREFERENCES,
                     &optval, sizeof(optval));

    optval = 1;
    (void)setsockopt(data->udp_sock, IPPROTO_IPV6, IPV6_V6ONLY,
                     &optval, sizeof(optval));
  }

  ret = bind(data->udp_sock, bind_addr, bind_addrlen);
  if (ret < 0) {
    LOG_ERR("Failed to bind UDP socket (%s): %d", data->proto,
            errno);
    return -errno;
  }

  return ret;
}

struct data conf = {
  .proto = "IPv6",
  .udp_sock = -1,
};

static ssize_t sendall(const void *buf, size_t len, struct sockaddr *addr, socklen_t addrlen) {
  while (len) {
    ssize_t out_len = sendto(conf.udp_sock, buf, len, 0, addr, addrlen);

    if (out_len < 0) {
      return out_len;
    }
    buf = (const char *)buf + out_len;
    len -= out_len;
  }

  return 0;
}

static void log_server_rx_rate(uint32_t *packets_last_sec,
                               int64_t *last_log_ms) {
  int64_t now = k_uptime_get();

  while ((now - *last_log_ms) >= 1000) {
    LOG_INF("Client->server rate: received %u packets in last second",
            *packets_last_sec);
    *packets_last_sec = 0;
    *last_log_ms += 1000;
  }
}

void start_udp() {
  struct sockaddr_in6 addr6;

  (void)memset(&addr6, 0, sizeof(addr6));
  addr6.sin6_family = AF_INET6;
  addr6.sin6_port   = htons(4242);

  inet_pton(AF_INET6, CONFIG_NET_CONFIG_MY_IPV6_ADDR, &addr6.sin6_addr);

  if(start_udp_proto(&conf, (struct sockaddr *)&addr6, sizeof(addr6)) < 0) {
    LOG_ERR("Failed to start UDP server");
    return;
  }

  LOG_INF("UDP server started on port %d", ntohs(addr6.sin6_port));

  struct sockaddr_in6 client_addr6;
  uint32_t rx_packets_last_sec = 0;
  int64_t last_rate_log_ms = k_uptime_get();

  while(true) {
    int ret;
    struct pollfd fds[1];
    int64_t now = k_uptime_get();
    int64_t until_rate_log = 1000 - (now - last_rate_log_ms);
    int poll_timeout_ms = (until_rate_log <= 0) ? 0 : (int)until_rate_log;
    socklen_t client_addr_len = sizeof(client_addr6);

    fds[0].fd = conf.udp_sock;
    fds[0].events = POLLIN;

    ret = poll(fds, 1, poll_timeout_ms);
    if (ret < 0) {
      LOG_ERR("Poll error: %d", -errno);
      continue;
    }

    log_server_rx_rate(&rx_packets_last_sec, &last_rate_log_ms);

    if (ret == 0) {
      continue;
    }

    char buffer[128];
    ssize_t recv_len = recvfrom(conf.udp_sock, buffer, sizeof(buffer) - 1, 0,
                                (struct sockaddr *)&client_addr6, &client_addr_len);

    if (recv_len < 0) {
      LOG_ERR("Receive error: %d", -errno);
      continue;
    }

    rx_packets_last_sec++;

    LOG_DBG("Received %d bytes: %.*s", recv_len, (int)recv_len, buffer);

    if (!IS_ENABLED(CONFIG_UDP_SERVER_SEND_REPLIES)) {
      LOG_DBG("Reply disabled by CONFIG_UDP_SERVER_SEND_REPLIES");
      continue;
    }

    char message[] = "Okay";
    ssize_t sent_len = sendall(message, sizeof(message), (struct sockaddr *)&client_addr6, sizeof(client_addr6));

    if (sent_len < 0) {
      LOG_ERR("Send error: %d", -errno);
      continue;
    }

    char addr_str_client[NET_IPV6_ADDR_LEN];
    LOG_INF("Sent %d bytes response to %s",
          (int)sizeof(message),
          net_addr_ntop(AF_INET6, &client_addr6.sin6_addr, addr_str_client, sizeof(addr_str_client)));
  }
}

void stop_udp() {
  LOG_INF("Stopping UDP server");
  if (conf.udp_sock >= 0) {
    close(conf.udp_sock);
    conf.udp_sock = -1;
  }
}