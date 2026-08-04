/* simple-server.c - Simplified IPv6 TCP echo server */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_simple_server_sample, LOG_LEVEL_DBG);

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <assert.h>
#include <zephyr/linker/sections.h>
#include <zephyr/shell/shell.h>

#include <zephyr/net/net_core.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_if.h>

#include "common.h"

#define LED0_NODE DT_PATH(leds, led_0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

static struct k_sem quit_lock;
static struct net_mgmt_event_callback mgmt_cb;
static struct k_work_delayable connection_work;
static bool connected;
K_SEM_DEFINE(run_app, 0, 1);
static bool want_to_quit = false;

K_THREAD_DEFINE(tcp6_thread_id, STACK_SIZE,
                start_tcp, NULL, NULL, NULL,
                THREAD_PRIORITY, 0, -1);

#define EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)

static void connection_work_handler(struct k_work* work) { k_sem_give(&run_app); }

// Network management event callback handler.
static void event_handler(struct net_mgmt_event_callback *cb,
                          uint32_t mgmt_event, struct net_if *iface) {
  ARG_UNUSED(iface);
  ARG_UNUSED(cb);

  switch (mgmt_event) {
  case NET_EVENT_L4_CONNECTED:
    k_work_schedule(&connection_work, K_NO_WAIT);
    break;

  case NET_EVENT_L4_DISCONNECTED:
    k_sem_reset(&run_app);
    break;

  default:
    break;
  }
}

// Initializes the connection manager.
static void init_connection_manager(void) {
  k_work_init_delayable(&connection_work, connection_work_handler);

  if (IS_ENABLED(CONFIG_NET_CONNECTION_MANAGER)) {
    net_mgmt_init_event_callback(&mgmt_cb, event_handler, EVENT_MASK);
    net_mgmt_add_event_callback(&mgmt_cb);

    // We would usually call `conn_mgr_mon_resend_status()` now in order
    // to trigger an immediate status update, but this causes a crash in
    // Zephyr 4.1.0 (but not 3.7.0, interestingly).
    //
    // Instead, we will check the current connection state and signal
    // the semaphore if we are already connected.

    // Instead, check if already connected
    struct net_if* iface = net_if_get_default();
    if (iface && net_if_is_up(iface)) {
      if (net_if_ipv6_get_global_addr(NET_ADDR_PREFERRED, &iface)) {
        k_sem_give(&run_app);
      }
    } // else: just keep waiting for the event callback to trigger when the interface comes up
  } else {
    // Network manager is not enabled. This is usually not intended behavior,
    // but we will just signal the semaphore immediately in this case to avoid blocking forever.
    k_sem_give(&run_app);
  }
}

int main(void) {
  LOG_INF("What's up? Server running!\n");

  init_connection_manager();
  k_sem_take(&run_app, K_FOREVER);

  assert(device_is_ready(led.port));
  gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);

  // for(int i = 0; i < 30; i++) {
  //   gpio_pin_toggle_dt(&led);
  //   k_msleep(100);
  // }

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

  int flag = 1;
  int result = setsockopt(data->tcp_sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));
  if (result < 0) {
    LOG_ERR("Failed to set TCP_NODELAY on TCP socket (%s): %d", data->proto,
            errno);
    return -errno;
  }

#if 0
  if (bind_addr->sa_family == AF_INET6) {
    optval = IPV6_PREFER_SRC_PUBLIC;
    (void)setsockopt(data->tcp_sock, IPPROTO_IPV6,
                     IPV6_ADDR_PREFERENCES,
                     &optval, sizeof(optval));

    optval = 1;
    (void)setsockopt(data->tcp_sock, IPPROTO_IPV6, IPV6_V6ONLY,
                     &optval, sizeof(optval));
    LOG_INF("PREFERENCES and V6ONLY set");
  }
#endif

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
    int client_sock = accept(conf.tcp_sock, (struct sockaddr *)&client_addr, &client_addr_len);

    if (client_sock < 0) {
      LOG_ERR("Accept error: %d", -errno);
      continue;  // Keep accepting
    }

    LOG_INF("Client connected!");

    int64_t current_time = k_uptime_get();
    while(true) {
      char buffer[128];
      ssize_t recv_len = recv(client_sock, buffer, sizeof(buffer), 0);
      LOG_INF("Time since last receive: %lld ms", k_uptime_get() - current_time);
      current_time = k_uptime_get();

      if (recv_len < 0) {
        LOG_ERR("Receive error: %d", -errno);
        break;
      } else if (recv_len == 0) {
        LOG_INF("Client disconnected");
        break;
      }

      LOG_INF("Received %d bytes: %.*s", recv_len, (int)recv_len, buffer);

      gpio_pin_toggle_dt(&led);
    }

    close(client_sock);
  }
}

void stop_tcp() {

}