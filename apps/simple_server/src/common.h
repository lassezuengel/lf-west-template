#ifndef SIMPLE_SERVER_COMMON_H
#define SIMPLE_SERVER_COMMON_H

#if defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS) || defined(CONFIG_NET_TCP) || \
	defined(CONFIG_COVERAGE_GCOV)
#define STACK_SIZE 4096
#else
#define STACK_SIZE 2048
#endif

#if defined(CONFIG_NET_TC_THREAD_PREEMPTIVE)
#define THREAD_PRIORITY K_PRIO_PREEMPT(8)
#else
#define THREAD_PRIORITY K_PRIO_COOP(CONFIG_NUM_COOP_PRIORITIES - 1)
#endif

// The server data, including app data and configuration data.
struct data {
  const char *proto;

  int tcp_sock;
};

void start_tcp(void);
void stop_tcp(void);

#endif /* SIMPLE_SERVER_COMMON_H */