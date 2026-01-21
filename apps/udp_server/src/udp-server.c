/* udp-server.c - Simplified IPv6 UDP echo server */
/* This is actually a receiver of data, but persistent,
   so we call it the server. */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_udp_server_sample, LOG_LEVEL_DBG);

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/sections.h>
#include <zephyr/shell/shell.h>

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

  while(true) {
    char buffer[128];
    ssize_t recv_len = recv(conf.udp_sock, buffer, sizeof(buffer) - 1, 0);

    if (recv_len < 0) {
      LOG_ERR("Receive error: %d", -errno);
      break;
    }

    LOG_INF("Received %d bytes: %.*s", recv_len, (int)recv_len, buffer);
  }
}

void stop_udp() {
  LOG_INF("Stopping UDP server");
}