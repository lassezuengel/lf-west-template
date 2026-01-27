/*
 * Copyright (c) 2016-2019 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief App implementing 802.15.4 "serial-radio" protocol
 *
 * Application implementing 802.15.4 "serial-radio" protocol compatible
 * with popular Contiki-based native border routers.
 *
 * Modified to use UART instead of CDC ACM for J-Link VCOM support.
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wpan_serial, LOG_LEVEL_INF);

#include <nrf_802154.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <zephyr/net/buf.h>
#include <net_private.h>
#include <zephyr/net/ieee802154_radio.h>

#if defined(CONFIG_NET_TC_THREAD_COOPERATIVE)
#define THREAD_PRIORITY K_PRIO_COOP(CONFIG_NUM_COOP_PRIORITIES - 1)
#else
#define THREAD_PRIORITY K_PRIO_PREEMPT(8)
#endif

#define SLIP_END     0300
#define SLIP_ESC     0333
#define SLIP_ESC_END 0334
#define SLIP_ESC_ESC 0335

#define ENABLE_PROMISCUOUS_MODE 0

#define ANALYZE_UART_BOTTLENECK 0
#if ANALYZE_UART_BOTTLENECK
	static atomic_t packets_received = ATOMIC_INIT(0);
	static atomic_t packets_sent = ATOMIC_INIT(0);
#endif

enum slip_state {
	STATE_GARBAGE,
	STATE_OK,
	STATE_ESC,
};

/* RX queue */
static struct k_fifo rx_queue;
static K_THREAD_STACK_DEFINE(rx_stack, 1024);
static struct k_thread rx_thread_data;

/* TX queue */
static struct k_fifo tx_queue;
static K_THREAD_STACK_DEFINE(tx_stack, 1024);
static struct k_thread tx_thread_data;

/* Buffer for SLIP encoded data for the worst case */
static uint8_t slip_buf[1 + 2 * CONFIG_NET_BUF_DATA_SIZE];

/* ieee802.15.4 device */
static struct ieee802154_radio_api *radio_api;
static const struct device *const ieee802154_dev =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_ieee802154));
uint8_t mac_addr[8]; /* in little endian */

/* UART device - changed to use uart0 instead of CDC ACM */
static const struct device *const uart_dev =
	DEVICE_DT_GET(DT_NODELABEL(uart0));

void print_uart_pins(void)
{
    // Access the nRF UARTE peripheral registers directly
    NRF_UARTE_Type *uarte = (NRF_UARTE_Type *)DT_REG_ADDR(DT_NODELABEL(uart0));

    uint32_t txd_pin = uarte->PSEL.TXD;
    uint32_t rxd_pin = uarte->PSEL.RXD;

    LOG_INF("=== UART0 PIN CONFIGURATION ===");
    LOG_INF("TXD: P%d.%d (register: 0x%08x)",
            (txd_pin >> 5) & 0x1, txd_pin & 0x1F, txd_pin);
    LOG_INF("RXD: P%d.%d (register: 0x%08x)",
            (rxd_pin >> 5) & 0x1, rxd_pin & 0x1F, rxd_pin);
    LOG_INF("===============================");
}

/* SLIP state machine */
static uint8_t slip_state = STATE_OK;

static struct net_pkt *pkt_curr;

/* General helpers */

static int slip_process_byte(unsigned char c)
{
	struct net_buf *buf;
#ifdef VERBOSE_DEBUG
	LOG_INF("recv: state %u byte %x", slip_state, c);
#endif
	switch (slip_state) {
	case STATE_GARBAGE:
		if (c == SLIP_END) {
			slip_state = STATE_OK;
		}
		LOG_INF("garbage: discard byte %x", c);
		return 0;

	case STATE_ESC:
		if (c == SLIP_ESC_END) {
			c = SLIP_END;
		} else if (c == SLIP_ESC_ESC) {
			c = SLIP_ESC;
		} else {
			slip_state = STATE_GARBAGE;
			return 0;
		}
		slip_state = STATE_OK;
		break;

	case STATE_OK:
		if (c == SLIP_ESC) {
			slip_state = STATE_ESC;
			return 0;
		} else if (c == SLIP_END) {
			return 1;
		}
		break;
	}

#ifdef VERBOSE_DEBUG
	LOG_INF("processed: state %u byte %x", slip_state, c);
#endif

	if (!pkt_curr) {
		pkt_curr = net_pkt_rx_alloc_with_buffer(NULL, 256,
							AF_UNSPEC, 0,
							K_NO_WAIT);
		if (!pkt_curr) {
			LOG_ERR("No more buffers");
			return 0;
		}
	}

	buf = net_buf_frag_last(pkt_curr->buffer);
	if (!net_buf_tailroom(buf)) {
		LOG_ERR("No more buf space: buf %p len %u", buf, buf->len);

		net_pkt_unref(pkt_curr);
		pkt_curr = NULL;
		return 0;
	}

	net_buf_add_u8(buf, c);

	return 0;
}

static K_SEM_DEFINE(tx_sem, 0, 1);
static uint8_t *tx_data_ptr = NULL;
static uint16_t tx_data_len = 0;

static void interrupt_handler(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		unsigned char byte;

		/* Handle RX */
		if (uart_irq_rx_ready(dev)) {
			while (uart_fifo_read(dev, &byte, sizeof(byte))) {
				if (slip_process_byte(byte)) {
					if (!pkt_curr) {
						LOG_INF("Skip SLIP_END");
						continue;
					}

					LOG_INF("from SERIAL: Full packet %p, len %u", pkt_curr,
						net_pkt_get_len(pkt_curr));

					k_fifo_put(&rx_queue, pkt_curr);
					pkt_curr = NULL;
				}
			}
		}

		/* Handle TX */
		if (uart_irq_tx_ready(dev)) {
			if (tx_data_ptr && tx_data_len > 0) {
				int wrote = uart_fifo_fill(dev, tx_data_ptr, tx_data_len);
				if (wrote > 0) {
					tx_data_ptr += wrote;
					tx_data_len -= wrote;
				}

				if (tx_data_len == 0) {
					/* Transmission complete */
					uart_irq_tx_disable(dev);
					tx_data_ptr = NULL;
					k_sem_give(&tx_sem);
#if ANALYZE_UART_BOTTLENECK
					LOG_INF("TX complete for packet\n");
#endif
				}
			} else {
				uart_irq_tx_disable(dev);
			}
		}
	}
}

static int try_write(uint8_t *data, uint16_t len)
{
	int ret;

	/* Set up transmission */
	tx_data_ptr = data;
	tx_data_len = len;

	/* Enable TX interrupt */
	uart_irq_tx_enable(uart_dev);

	/* Wait for transmission to complete (with timeout) */
	ret = k_sem_take(&tx_sem, K_MSEC(1000));
	if (ret < 0) {
		LOG_ERR("TX timeout");
		uart_irq_tx_disable(uart_dev);
		tx_data_ptr = NULL;
		tx_data_len = 0;
		return -ETIMEDOUT;
	}

	return 0;
}

/* Allocate and send data to UART */
static void send_data(uint8_t *cfg, uint8_t *data, size_t len)
{
	struct net_pkt *pkt;

	pkt = net_pkt_alloc_with_buffer(NULL, len + 5,
					AF_UNSPEC, 0, K_NO_WAIT);
	if (!pkt) {
		LOG_INF("No pkt available");
		return;
	}

	LOG_INF("queue pkt %p len %u", pkt, len);

	/* Add configuration id */
	net_pkt_write(pkt, cfg, 2);
	net_pkt_write(pkt, data, len);

	/* simulate LQI */
	net_pkt_skip(pkt, 1);
	/* simulate FCS */
	net_pkt_skip(pkt, 2);

	net_pkt_set_overwrite(pkt, true);

	k_fifo_put(&tx_queue, pkt);
}

static void get_ieee_addr(void)
{
	uint8_t cfg[2] = { '!', 'M' };
	uint8_t mac[8];

	LOG_INF("");

	/* Send in BE */
	sys_memcpy_swap(mac, mac_addr, sizeof(mac));

	send_data(cfg, mac, sizeof(mac));
}

static void process_request(struct net_buf *buf)
{
	uint8_t cmd = net_buf_pull_u8(buf);


	switch (cmd) {
	case 'M':
		get_ieee_addr();
		break;
	default:
		LOG_ERR("Not handled request %c", cmd);
		break;
	}
}

static void send_pkt_report(uint8_t seq, uint8_t status, uint8_t num_tx)
{
	uint8_t cfg[2] = { '!', 'R' };
	uint8_t report[3];

	report[0] = seq;
	report[1] = status;
	report[2] = num_tx;

	send_data(cfg, report, sizeof(report));
}

static void process_data(struct net_pkt *pkt)
{
	struct net_buf *buf = net_buf_frag_last(pkt->buffer);
	uint8_t seq, num_attr;
	int ret, i;

	seq = net_buf_pull_u8(buf);
	num_attr = net_buf_pull_u8(buf);

	LOG_INF("from SERIAL to RADIO: seq %u num_attr %u", seq, num_attr);

	/**
	 * There are some attributes sent over this protocol
	 * discard them and return packet data report.
	 */

	for (i = 0; i < num_attr; i++) {
		/* attr */
		net_buf_pull_u8(buf);
		/* value */
		net_buf_pull_be16(buf);
	}

	/* Transmit data through radio */
	ret = radio_api->tx(ieee802154_dev, IEEE802154_TX_MODE_DIRECT,
			    pkt, buf);
	if (ret) {
		LOG_ERR("Error transmit data");
	}

	/* TODO: Return correct status codes */
	/* TODO: Implement re-transmissions if needed */

	/* Send packet data report */
	send_pkt_report(seq, ret, 1);
}

static void set_channel(uint8_t chan)
{
	LOG_INF("Set channel %u", chan);

	radio_api->set_channel(ieee802154_dev, chan);
}

static void process_config(struct net_pkt *pkt)
{
	struct net_buf *buf = net_buf_frag_last(pkt->buffer);
	uint8_t cmd = net_buf_pull_u8(buf);

	LOG_INF("Process config %c", cmd);

	switch (cmd) {
	case 'S':
		process_data(pkt);
		break;
	case 'C':
		set_channel(net_buf_pull_u8(buf));
		break;
	default:
		LOG_ERR("Unhandled cmd %u", cmd);
	}
}

static void rx_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("RX thread started");

	while (true) {
		struct net_pkt *pkt;
		struct net_buf *buf;
		uint8_t specifier;

		pkt = k_fifo_get(&rx_queue, K_FOREVER);
		buf = net_buf_frag_last(pkt->buffer);

		LOG_INF("from SERIAL: rx_queue pkt %p buf %p", pkt, buf);

		LOG_HEXDUMP_DBG(buf->data, buf->len, "SLIP >");

		/* TODO: process */
		specifier = net_buf_pull_u8(buf);
		switch (specifier) {
		case '?':
			process_request(buf);
			break;
		case '!':
			process_config(pkt);
			break;
		default:
			LOG_ERR("Unknown message specifier %c", specifier);
			break;
		}

		net_pkt_unref(pkt);
	}
}

static size_t slip_buffer(uint8_t *sbuf, struct net_buf *buf)
{
	size_t len = buf->len;
	uint8_t *sbuf_orig = sbuf;
	int i;

	/**
	 * This strange protocol does not require send START
	 * *sbuf++ = SLIP_END;
	 */

	for (i = 0; i < len; i++) {
		uint8_t byte = net_buf_pull_u8(buf);

		switch (byte) {
		case SLIP_END:
			*sbuf++ = SLIP_ESC;
			*sbuf++ = SLIP_ESC_END;
			break;
		case SLIP_ESC:
			*sbuf++ = SLIP_ESC;
			*sbuf++ = SLIP_ESC_ESC;
			break;
		default:
			*sbuf++ = byte;
		}
	}

	*sbuf++ = SLIP_END;

	return sbuf - sbuf_orig;
}

// static int try_write(uint8_t *data, uint16_t len)
// {
// 	int wrote;

// 	while (len) {
// 		wrote = uart_fifo_fill(uart_dev, data, len);
// 		if (wrote <= 0) {
// 			return -EIO;
// 		}

// 		len -= wrote;
// 		data += wrote;
// 	}

// 	return 0;
// }

/**
 * TX - transmit to SLIP interface
 */
static void tx_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("TX thread started");

	while (true) {
		struct net_pkt *pkt;
		struct net_buf *buf;
		size_t len;

		pkt = k_fifo_get(&tx_queue, K_FOREVER);
		buf = net_buf_frag_last(pkt->buffer);
		len = net_pkt_get_len(pkt);

		LOG_INF("from RADIO to SERIAL: Send pkt %p buf %p len %d", pkt, buf, len);

		LOG_HEXDUMP_DBG(buf->data, buf->len, "SLIP <");

		/* remove FCS 2 bytes */
		buf->len -= 2U;

		/* SLIP encode and send */
		len = slip_buffer(slip_buf, buf);

		if(try_write(slip_buf, len) < 0) {
			LOG_ERR("Error writing to UART");
		}

		net_pkt_unref(pkt);

		#if ANALYZE_UART_BOTTLENECK
			uint32_t sent_count = atomic_inc(&packets_sent);
			uint32_t recv_count = atomic_get(&packets_received);

			if (sent_count % 10 == 0) {  // Log every 10 packets
				LOG_INF("Stats: received=%u, sent=%u, lag=%d",
								recv_count, sent_count, (int)(recv_count - sent_count));
			}
		#endif
	}
}

static void init_rx_queue(void)
{
	k_fifo_init(&rx_queue);

	k_thread_create(&rx_thread_data, rx_stack,
			K_THREAD_STACK_SIZEOF(rx_stack),
			rx_thread,
			NULL, NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);
}

static void init_tx_queue(void)
{
	k_fifo_init(&tx_queue);

	k_thread_create(&tx_thread_data, tx_stack,
			K_THREAD_STACK_SIZEOF(tx_stack),
			tx_thread,
			NULL, NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);
}

/**
 * FIXME choose correct OUI, or add support in L2
 */
static uint8_t *get_mac(const struct device *dev)
{
	mac_addr[7] = 0x00;
	mac_addr[6] = 0x12;
	mac_addr[5] = 0x4b;
	mac_addr[4] = 0x00;

	sys_rand_get(mac_addr, 4U);

	mac_addr[0] = (mac_addr[0] & ~0x01) | 0x02;

	return mac_addr;
}

static bool init_ieee802154(void)
{
	LOG_INF("Initialize ieee802.15.4");

	if (!device_is_ready(ieee802154_dev)) {
		LOG_ERR("IEEE 802.15.4 device not ready");
		return false;
	}

	radio_api = (struct ieee802154_radio_api *)ieee802154_dev->api;

	/**
	 * Do actual initialization of the chip
	 */
	get_mac(ieee802154_dev);

	if (IEEE802154_HW_FILTER &
	    radio_api->get_capabilities(ieee802154_dev)) {
		struct ieee802154_filter filter;
		uint16_t short_addr;

		// TODO: Do we need this?
		/* Set short address */
		// short_addr = (mac_addr[0] << 8) + mac_addr[1];
		// filter.short_addr = short_addr;

		// radio_api->filter(ieee802154_dev, true,
		// 		  IEEE802154_FILTER_TYPE_SHORT_ADDR,
		// 		  &filter);

		// /* Set ieee address */
		// filter.ieee_addr = mac_addr;
		// radio_api->filter(ieee802154_dev, true,
		// 		  IEEE802154_FILTER_TYPE_IEEE_ADDR,
		// 		  &filter);

#ifdef CONFIG_NET_CONFIG_SETTINGS
		LOG_INF("Set panid %x", CONFIG_NET_CONFIG_IEEE802154_PAN_ID);

		filter.pan_id = CONFIG_NET_CONFIG_IEEE802154_PAN_ID;

		radio_api->filter(ieee802154_dev, true,
				  IEEE802154_FILTER_TYPE_PAN_ID,
				  &filter);
#endif /* CONFIG_NET_CONFIG_SETTINGS */
	}

#ifdef CONFIG_NET_CONFIG_SETTINGS
	LOG_INF("Set channel %u", CONFIG_NET_CONFIG_IEEE802154_CHANNEL);
	radio_api->set_channel(ieee802154_dev,
			       CONFIG_NET_CONFIG_IEEE802154_CHANNEL);
#endif /* CONFIG_NET_CONFIG_SETTINGS */

	/* Start ieee802154 */
	radio_api->start(ieee802154_dev);

#if ENABLE_PROMISCUOUS_MODE
	// **ENABLE PROMISCUOUS MODE AT HAL LEVEL**
	LOG_INF("Enabling promiscuous mode at HAL level");
	nrf_802154_promiscuous_set(true);

	bool promisc = nrf_802154_promiscuous_get();
	LOG_INF("Promiscuous mode is: %s", promisc ? "ENABLED" : "DISABLED");

	// Force the nRF radio to continuous RX
	LOG_INF("Setting radio to continuous RX mode");

	struct ieee802154_config config;
	config.rx_on_when_idle = true;

	int ret = radio_api->configure(ieee802154_dev,
	                               IEEE802154_CONFIG_RX_ON_WHEN_IDLE,
	                               &config);
	if (ret == 0) {
		LOG_INF("Successfully enabled RX-on-when-idle");
	} else {
		LOG_ERR("Failed to enable RX-on-when-idle: %d", ret);
	}
#endif

	return true;
}

#if ANALYZE_UART_BOTTLENECK
int net_recv_data(struct net_if *iface, struct net_pkt *pkt)
{
	uint32_t recv_count = atomic_inc(&packets_received);

	LOG_INF("from RADIO: Received pkt %p, len %d [#%u]",
	        pkt, net_pkt_get_len(pkt), recv_count);

	k_fifo_put(&tx_queue, pkt);
	return 0;
}
#else
int net_recv_data(struct net_if *iface, struct net_pkt *pkt)
{
	LOG_INF("from RADIO: Received pkt %p, len %d", pkt, net_pkt_get_len(pkt));

	k_fifo_put(&tx_queue, pkt);

	return 0;
}
#endif

enum net_verdict ieee802154_handle_ack(struct net_if *iface, struct net_pkt *pkt)
{
	return NET_CONTINUE;
}

int main(void)
{
	int ret;

	LOG_INF("Starting wpan_serial application (UART mode)...");

	LOG_INF("Checking readiness of UART device %s...",
		uart_dev->name);
	if (!device_is_ready(uart_dev)) {
		LOG_ERR("UART device not ready");
		return 0;
	}

	LOG_INF("UART serial initialized");

	print_uart_pins();

	/* Initialize net_pkt */
	net_pkt_init();

	/* Initialize RX queue */
	init_rx_queue();

	/* Initialize TX queue */
	init_tx_queue();

	/* Initialize ieee802154 device */
	if (!init_ieee802154()) {
		LOG_ERR("Unable to initialize ieee802154");
		return 0;
	}

	uart_irq_callback_set(uart_dev, interrupt_handler);

	/* Enable rx interrupts */
	uart_irq_rx_enable(uart_dev);

	LOG_INF("WPAN serial ready - using UART0 via J-Link");

	return 0;
}