/*
 * Copyright (c) 2017-2019 Intel Corporation.
 * Copyright (c) 2018 Nordic Semiconductor ASA.
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#define MY_PORT 4242
#if defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS) || defined(CONFIG_NET_TCP) || \
	defined(CONFIG_COVERAGE_GCOV)
#define STACK_SIZE 4096
#else
#define STACK_SIZE 2048
#endif

#if defined(CONFIG_NET_TC_THREAD_COOPERATIVE)
#define THREAD_PRIORITY K_PRIO_COOP(CONFIG_NUM_COOP_PRIORITIES - 1)
#else
#define THREAD_PRIORITY K_PRIO_PREEMPT(8)
#endif

#define RECV_BUFFER_SIZE 1280
#define STATS_TIMER 60 /* How often to print statistics (in seconds) */

#if defined(CONFIG_USERSPACE)
#include <zephyr/app_memory/app_memdomain.h>
extern struct k_mem_partition app_partition;
extern struct k_mem_domain app_domain;
#define APP_BMEM K_APP_BMEM(app_partition)
#define APP_DMEM K_APP_DMEM(app_partition)
#else
#define APP_BMEM
#define APP_DMEM
#endif

struct data {
	const char *proto;

	struct {
		int sock;
		atomic_t bytes_received;
		struct k_work_delayable stats_print;

		struct {
			int sock;
			char recv_buffer[RECV_BUFFER_SIZE];
			uint32_t counter;
		} accepted[CONFIG_NET_SAMPLE_NUM_HANDLERS];
	} tcp;
};

struct configs {
	struct data ipv6;
};

extern struct configs conf;

static void start_tcp(void);
static void stop_tcp(void);