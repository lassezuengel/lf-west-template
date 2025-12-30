/* simple-server.c - Simplified IPv6 TCP echo server */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_simple_server_sample, LOG_LEVEL_DBG);

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

K_THREAD_DEFINE(tcp6_thread_id, STACK_SIZE,
                start_tcp, NULL, NULL, NULL,
                THREAD_PRIORITY, 0, -1);

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
  LOG_INF("What's up?");

  init_connection_manager();

  k_sem_take(&run_app, K_FOREVER);

  // k_thread_start(tcp6_thread_id);
  start_tcp();

  k_sem_take(&quit_lock, K_FOREVER);

  if (connected) {
    stop_tcp();
  }
  return 0;
}

/* TCP implementation */

// Creates and binds TCP listening socket.
static int start_tcp_proto(struct data *data,
                           struct sockaddr *bind_addr,
                           socklen_t bind_addrlen) {
  int optval;
  int ret;

  data->tcp_sock = socket(bind_addr->sa_family, SOCK_STREAM,
                          IPPROTO_TCP);
  if (data->tcp_sock < 0) {
    LOG_ERR("Failed to create TCP socket (%s): %d", data->proto,
            errno);
    return -errno;
  }

  if (bind_addr->sa_family == AF_INET6) {
    optval = IPV6_PREFER_SRC_PUBLIC;
    (void)setsockopt(data->tcp_sock, IPPROTO_IPV6,
                     IPV6_ADDR_PREFERENCES,
                     &optval, sizeof(optval));

    optval = 1;
    (void)setsockopt(data->tcp_sock, IPPROTO_IPV6, IPV6_V6ONLY,
                     &optval, sizeof(optval));
  }

  ret = bind(data->tcp_sock, bind_addr, bind_addrlen);
  if (ret < 0) {
    LOG_ERR("Failed to bind TCP socket (%s): %d", data->proto,
            errno);
    return -errno;
  }

  ret = listen(data->tcp_sock, 1);
  if (ret < 0) {
    LOG_ERR("Failed to listen on TCP socket (%s): %d",
            data->proto, errno);
    ret = -errno;
  }

  return ret;
}

struct data conf = {
  .proto = "IPv6",
  .tcp_sock = -1,
};

void start_tcp() {
  struct sockaddr_in6 addr6;

  (void)memset(&addr6, 0, sizeof(addr6));
  addr6.sin6_family = AF_INET6;
  addr6.sin6_port   = htons(4242);

  inet_pton(AF_INET6, CONFIG_NET_CONFIG_MY_IPV6_ADDR, &addr6.sin6_addr);

  if(start_tcp_proto(&conf, (struct sockaddr *)&addr6, sizeof(addr6)) < 0) {
    LOG_ERR("Failed to start TCP server");
    return;
  }

  // Accept connections in a LOOP
  while(true) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    LOG_INF("Waiting for connection...");
    int client_sock = accept(conf.tcp_sock, (struct sockaddr *)&client_addr,
                            &client_addr_len);

    if (client_sock < 0) {
      LOG_ERR("Accept error: %d", -errno);
      continue;  // Keep accepting
    }

    LOG_INF("Client connected!");

    while(true) {
      char buffer[128];
      ssize_t recv_len = recv(client_sock, buffer, sizeof(buffer), 0);

      if (recv_len < 0) {
        LOG_ERR("Receive error: %d", -errno);
        break;
      } else if (recv_len == 0) {
        LOG_INF("Client disconnected");
        break;
      }

      LOG_INF("Received %d bytes: %.*s", recv_len, (int)recv_len, buffer);
    }

    close(client_sock);
  }
}

void stop_tcp() {

}