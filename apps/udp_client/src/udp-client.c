/* echo-client.c - Simplified IPv6 TCP echo client */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_udp_client_sample, LOG_LEVEL_INF);

#include <errno.h>
#include <stdio.h>
#include <zephyr/kernel.h>

#include <zephyr/posix/sys/eventfd.h>
#include <zephyr/posix/poll.h>
#include <zephyr/sys/util.h>

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

static void log_client_tx_rate(uint32_t *packets_last_sec,
                               int64_t *last_log_ms) {
  int64_t now = k_uptime_get();

  while ((now - *last_log_ms) >= 1000) {
    LOG_INF("Client->server rate: sent %u packets in last second",
            *packets_last_sec);
    *packets_last_sec = 0;
    *last_log_ms += 1000;
  }
}

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
  uint32_t tx_packets_last_sec = 0;
  int64_t last_rate_log_ms = k_uptime_get();

  while(true) {
    if (++conf.counter % 1000 == 0U) {
      LOG_INF("%s UDP: Exchanged %u packets", conf.proto,
              conf.counter);
    }

    LOG_DBG("Sending stuff!");
    ret = sendall(lorem_ipsum, 12, addr, addrlen);
    if (ret < 0) {
      LOG_ERR("%s UDP: Failed to send data, errno %d", conf.proto,
              errno);
      log_client_tx_rate(&tx_packets_last_sec, &last_rate_log_ms);
      k_msleep(100);
      continue;
    }

    tx_packets_last_sec++;
    log_client_tx_rate(&tx_packets_last_sec, &last_rate_log_ms);

#ifndef CONFIG_UDP_CLIENT_EXPECT_REPLIES
      LOG_DBG("Reply wait disabled by CONFIG_UDP_CLIENT_EXPECT_REPLIES");
#else 
      char recv_buf[RECV_BUF_SIZE];
      size_t recv_buf_size = sizeof(recv_buf);
      int wait_remaining_ms = CONFIG_UDP_CLIENT_REPLY_TIMEOUT_MS;
      bool got_reply = false;

      struct pollfd fds[1];
      fds[0].fd = conf.udp_sock;
      fds[0].events = POLLIN;

      while (wait_remaining_ms > 0) {
        int64_t now = k_uptime_get();
        int64_t until_rate_log = 1000 - (now - last_rate_log_ms);
        int poll_timeout_ms;

        if (until_rate_log <= 0) {
          log_client_tx_rate(&tx_packets_last_sec, &last_rate_log_ms);
          continue;
        }

        poll_timeout_ms = MIN(wait_remaining_ms, (int)until_rate_log);
        ret = poll(fds, 1, poll_timeout_ms);
        if (ret < 0) {
          LOG_ERR("Poll failed: %d", -errno);
          return -errno;
        }

        wait_remaining_ms -= poll_timeout_ms;
        log_client_tx_rate(&tx_packets_last_sec, &last_rate_log_ms);

        if (ret == 0) {
          continue;
        }

        ret = recv(conf.udp_sock, recv_buf, recv_buf_size - 1, 0);
        if (ret < 0) {
          LOG_ERR("Receive failed: %d", -errno);
          break;
        }

        recv_buf[ret] = '\0';
        LOG_INF("Received %d bytes: %s", ret, recv_buf);
        got_reply = true;
        break;
      }

      if (!got_reply) {
        LOG_WRN("Timeout: no response received within %d ms",
                CONFIG_UDP_CLIENT_REPLY_TIMEOUT_MS);
      }
#endif

    int send_delay_ms = CONFIG_UDP_CLIENT_SEND_DELAY_MS;
    while (send_delay_ms > 0) {
      int64_t now = k_uptime_get();
      int64_t until_rate_log = 1000 - (now - last_rate_log_ms);
      int sleep_ms;

      if (until_rate_log <= 0) {
        log_client_tx_rate(&tx_packets_last_sec, &last_rate_log_ms);
        continue;
      }

      sleep_ms = MIN(send_delay_ms, (int)until_rate_log);
      k_msleep(sleep_ms);
      send_delay_ms -= sleep_ms;
      log_client_tx_rate(&tx_packets_last_sec, &last_rate_log_ms);
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

  // We connect to the server address so that we only see traffic from it
  // and TODO: can use send() and recv() instead of sendto() and recvfrom().
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