/* echo-client.c - Simplified IPv6 TCP echo client */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_echo_client_sample, LOG_LEVEL_DBG);

#include <errno.h>
#include <stdio.h>
#include <zephyr/kernel.h>

#include <zephyr/posix/sys/eventfd.h>

#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/socket.h>

#include "common.h"

#define APP_BANNER "Run echo client"

#define INVALID_SOCK (-1)

#define EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)

const char lorem_ipsum[] = "LOREM_IPSUM";
const int ipsum_len = sizeof(lorem_ipsum) - 1;

APP_DMEM struct sample_data conf = {
  .proto = "IPv6",
  .tcp_sock = INVALID_SOCK,
  .counter = 0,
};

static APP_BMEM struct pollfd fds[2];
static APP_BMEM int nfds;

static APP_BMEM bool connected;

K_SEM_DEFINE(run_app, 0, 1);

static struct net_mgmt_event_callback mgmt_cb;

// Prepares the fds for polling.
static void prepare_fds(void) {
  nfds = 0;

  fds[nfds].fd = eventfd(0, 0);
  fds[nfds].events = POLLIN;
  nfds++;

  if (conf.tcp_sock >= 0) {
    fds[nfds].fd = conf.tcp_sock;
    fds[nfds].events = POLLIN;
    nfds++;
  }
}

// Waits for events on the prepared fds.
static void wait(void) {
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
static int process_tcp_proto(void);
static void stop_tcp(void);

static int start_tcp_and_fds(void) {
  LOG_INF("Starting...");

  int ret = start_tcp();
  if (ret < 0) {
    return ret;
  }

  prepare_fds();

  return 0;
}

static int run_tcp(void) {
  int ret = process_tcp_proto();
  if (ret < 0) {
    return ret;
  }

  return 0;
}

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

static void start_client() {
  int i = 0;
  int ret;

  while (true) {
    k_sem_take(&run_app, K_FOREVER);

    ret = start_tcp_and_fds();

    while (connected && (ret == 0)) {
      ret = run_tcp();
    }

    stop_tcp();
  }
}

int main(void) {
  LOG_INF(APP_BANNER);
  printk("What's up? Client running!\n");

  init_network_manager();

  k_thread_priority_set(k_current_get(), THREAD_PRIORITY);

  start_client();
  return 0;
}

/* TCP implementation */

#define RECV_BUF_SIZE 128

static ssize_t sendall(int sock, const void *buf, size_t len) {
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

static int send_tcp_data() {
  int ret;

  ret = sendall(conf.tcp_sock, lorem_ipsum, 12);

  if (ret < 0) {
    LOG_ERR("%s TCP: Failed to send data, errno %d", conf.proto,
            errno);
  }

  return ret;
}

// Starts TCP connection to the given address and port.
static int start_tcp_proto(sa_family_t family,
                           struct sockaddr *addr, socklen_t addrlen) {
  int ret;

  conf.tcp_sock = socket(family, SOCK_STREAM, IPPROTO_TCP);
  if (conf.tcp_sock < 0) {
    LOG_ERR("Failed to create TCP socket (%s): %d", conf.proto,
            errno);
    return -errno;
  }

  ret = connect(conf.tcp_sock, addr, addrlen);
  if (ret < 0) {
    LOG_ERR("Cannot connect to TCP remote (%s): %d", conf.proto,
            errno);
    ret = -errno;
  }

  return ret;
}

static int process_tcp_proto() {
  int ret, received;
  char buf[RECV_BUF_SIZE];

  do {
    if (++conf.counter % 1000 == 0U) {
      LOG_INF("%s TCP: Exchanged %u packets", conf.proto,
              conf.counter);
    }
    printk("Sending stuff!\n");

    k_msleep(100);

    ret = send_tcp_data();
    if (ret < 0) {
      break;
    }
  } while (true);

  return ret;
}

// Starts TCP connection to the server.
static int start_tcp(void) {
  int ret = 0;
  struct sockaddr_in6 addr6;

  addr6.sin6_family = AF_INET6;
  addr6.sin6_port = htons(PEER_PORT);
  inet_pton(AF_INET6, CONFIG_NET_CONFIG_PEER_IPV6_ADDR,
            &addr6.sin6_addr);

  return start_tcp_proto(AF_INET6,
                         (struct sockaddr *)&addr6,
                         sizeof(addr6));
}

static void stop_tcp(void) {
  LOG_INF("Stopping...");
  if (conf.tcp_sock >= 0) {
    (void)close(conf.tcp_sock);
  }
}