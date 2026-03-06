/*
 * Copyright (c) 2017 Intel Corporation.
 * Copyright (c) 2018 Nordic Semiconductor ASA.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

/* Value of 0 will cause the IP stack to select next free port */
#define MY_PORT 0

#define PEER_PORT 4242

/* Turn off the progress printing so that shell can be used.
 * Set to true if you want to see progress output.
 */
#define PRINT_PROGRESS false

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

#if defined(CONFIG_NET_TC_THREAD_PREEMPTIVE)
#define THREAD_PRIORITY K_PRIO_PREEMPT(8)
#else
#define THREAD_PRIORITY K_PRIO_COOP(CONFIG_NUM_COOP_PRIORITIES - 1)
#endif

struct sample_data {
	const char *proto;
	int udp_sock;
	int counter;
};

#if !defined(CONFIG_NET_CONFIG_PEER_IPV6_ADDR)
#define CONFIG_NET_CONFIG_PEER_IPV6_ADDR ""
#endif

extern const char lorem_ipsum[];
extern const int ipsum_len;