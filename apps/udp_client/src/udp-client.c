/* echo-client.c - Simplified IPv6 TCP echo client */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_udp_client_sample, LOG_LEVEL_DBG);

#include <errno.h>
#include <stdio.h>
#include <zephyr/kernel.h>

#include <zephyr/posix/sys/eventfd.h>

#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/socket.h>

#include "common.h"

#define APP_BANNER "Run UDP echo client"

#define INVALID_SOCK (-1)

#define EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)

const char lorem_ipsum[] = "LOREM_IPSUM";
const int ipsum_len = sizeof(lorem_ipsum) - 1;

APP_DMEM struct sample_data conf = {
  .proto = "IPv6",
  .counter = 0,
};

static APP_BMEM bool connected;

K_SEM_DEFINE(run_app, 0, 1);

static struct net_mgmt_event_callback mgmt_cb;

static int start_udp(void);
static void stop_udp(void);

// Sets up network event handler for connection management.
// On connection, it gives the semaphore to allow the client to run.
static void event_handler(struct net_mgmt_event_callback *cb,
                          uint32_t mgmt_event, struct net_if *iface) {
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

// Initializes network manager and connection event callback.
static void init_network_manager(void) {
  if (IS_ENABLED(CONFIG_NET_CONNECTION_MANAGER)) {
    net_mgmt_init_event_callback(&mgmt_cb,
                                 event_handler, EVENT_MASK);
    net_mgmt_add_event_callback(&mgmt_cb);

    conn_mgr_mon_resend_status();
  } else {
    connected = true;
    k_sem_give(&run_app);
  }
}

int main(void) {
  LOG_INF(APP_BANNER);
  printk("What's up? Client running!\n");

  init_network_manager();

  k_thread_priority_set(k_current_get(), THREAD_PRIORITY);

  int i = 0;
  int ret;

  while (true) {
    k_sem_take(&run_app, K_FOREVER);

    LOG_INF("Starting UDP sample run %d\n", ++i);
    ret = start_udp();
    if (ret < 0) {
      LOG_ERR("UDP sample failed: %d", -ret);
    }

    stop_udp();
  }

  return 0;
}

/* UDP implementation */
#define RECV_BUF_SIZE 128

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

static int process_udp_proto(struct sockaddr *addr, socklen_t addrlen) {
  int ret;

  while(true) {
    if (++conf.counter % 20 == 0U) {
      LOG_INF("%s TCP: Exchanged %u packets", conf.proto,
              conf.counter);
    }
    printk("Sending stuff!\n");

    k_msleep(3000);

    ret = sendall(lorem_ipsum, 12, addr, addrlen);
    if (ret < 0) {
      LOG_ERR("%s UDP: Failed to send data, errno %d", conf.proto,
              errno);
      break;
    }
  }

  return ret;
}

// Starts UDP connection to the given address and port.
static int start_udp_proto(sa_family_t family,
                           struct sockaddr *addr, socklen_t addrlen) {
  int ret;

  conf.udp_sock = socket(family, SOCK_DGRAM, IPPROTO_UDP);
  if (conf.udp_sock < 0) {
    LOG_ERR("Failed to create UDP socket (%s): %d", conf.proto,
            errno);
    return -errno;
  }

  ret = connect(conf.udp_sock, addr, addrlen);
  if (ret < 0) {
    LOG_ERR("Cannot connect to UDP remote (%s): %d", conf.proto,
            errno);
    return -errno;
  }

  ret = process_udp_proto(addr, addrlen);
  if (ret < 0) {
    LOG_ERR("UDP processing failed (%s): %d", conf.proto,
            -ret);
  }

  return ret;
}

// Starts UDP connection to the server.
static int start_udp(void) {
  int ret = 0;
  struct sockaddr_in6 addr6;

  addr6.sin6_family = AF_INET6;
  addr6.sin6_port = htons(PEER_PORT);
  inet_pton(AF_INET6, CONFIG_NET_CONFIG_PEER_IPV6_ADDR,
            &addr6.sin6_addr);

  return start_udp_proto(AF_INET6,
                         (struct sockaddr *)&addr6,
                         sizeof(addr6));
}

static void stop_udp(void) {
  LOG_INF("Stopping...");
  if (conf.udp_sock >= 0) {
    (void)close(conf.udp_sock);
  }
}