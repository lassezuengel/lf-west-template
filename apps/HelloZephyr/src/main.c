#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/net/buf.h>

LOG_MODULE_REGISTER(l2cap_example, LOG_LEVEL_INF);

#define MY_PSM 0x0080
#define L2CAP_MTU 64

/* Custom UUID to identify our ping-pong application */
#define PINGPONG_UUID 0x1234

/* Forward declaration */
static struct net_buf_pool l2cap_tx_pool;

/* Buffer pool for L2CAP transmit */
NET_BUF_POOL_DEFINE(l2cap_tx_pool, 4, BT_L2CAP_BUF_SIZE(L2CAP_MTU), 8, NULL);

/* Channel instance */
static struct bt_l2cap_le_chan le_chan;
static struct bt_conn *default_conn;
static bool connecting = false;
static bool is_advertising = false;
static bool is_scanning = false;

/* Advertising data with our custom UUID */
static struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, 
                 (PINGPONG_UUID & 0xff), ((PINGPONG_UUID >> 8) & 0xff)),
};

/* Forward declarations */
static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
                   struct net_buf_simple *buf);
static void start_scanning(void);
static void start_advertising(void);

static int chan_recv(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
    size_t len = buf->len;
    
    LOG_INF("Received %d bytes", len);
    LOG_HEXDUMP_INF(buf->data, len, "Data:");

    /* Check for PING message */
    if (len == 4 && memcmp(buf->data, "PING", 4) == 0) {
        LOG_INF("Got PING, sending PONG");
        
        struct net_buf *tx = net_buf_alloc(&l2cap_tx_pool, K_SECONDS(1));
        if (tx) {
            net_buf_reserve(tx, BT_L2CAP_SDU_CHAN_SEND_RESERVE);
            net_buf_add_mem(tx, "PONG", 4);
            int ret = bt_l2cap_chan_send(chan, tx);
            if (ret < 0) {
                LOG_ERR("Failed to send PONG: %d", ret);
                net_buf_unref(tx);
            }
        }
    }
    /* Check for PONG message */
    else if (len == 4 && memcmp(buf->data, "PONG", 4) == 0) {
        LOG_INF("Got PONG, sending PING");
        
        k_sleep(K_MSEC(1000)); /* Wait 1 second before next ping */
        
        struct net_buf *tx = net_buf_alloc(&l2cap_tx_pool, K_SECONDS(1));
        if (tx) {
            net_buf_reserve(tx, BT_L2CAP_SDU_CHAN_SEND_RESERVE);
            net_buf_add_mem(tx, "PING", 4);
            int ret = bt_l2cap_chan_send(chan, tx);
            if (ret < 0) {
                LOG_ERR("Failed to send PING: %d", ret);
                net_buf_unref(tx);
            }
        }
    }

    return 0;
}

static void chan_connected(struct bt_l2cap_chan *chan)
{
    struct bt_l2cap_le_chan *le_chan = BT_L2CAP_LE_CHAN(chan);
    struct bt_conn_info info;
    
    LOG_INF("L2CAP channel connected (MTU: %u)", le_chan->tx.mtu);

    /* If we are central, initiate ping */
    bt_conn_get_info(chan->conn, &info);
    if (info.role == BT_CONN_ROLE_CENTRAL) {
        LOG_INF("Central role - initiating PING");
        
        struct net_buf *tx = net_buf_alloc(&l2cap_tx_pool, K_SECONDS(1));
        if (tx) {
            net_buf_reserve(tx, BT_L2CAP_SDU_CHAN_SEND_RESERVE);
            net_buf_add_mem(tx, "PING", 4);
            int ret = bt_l2cap_chan_send(chan, tx);
            if (ret < 0) {
                LOG_ERR("Failed to send initial PING: %d", ret);
                net_buf_unref(tx);
            }
        }
    }
}

static void chan_disconnected(struct bt_l2cap_chan *chan)
{
    LOG_INF("L2CAP channel disconnected");
}

static struct net_buf *chan_alloc_buf(struct bt_l2cap_chan *chan)
{
    struct net_buf *buf = net_buf_alloc(&l2cap_tx_pool, K_NO_WAIT);
    if (!buf) {
        LOG_ERR("Unable to allocate buffer");
    }
    
    return buf;
}

static struct bt_l2cap_chan_ops chan_ops = {
    .connected = chan_connected,
    .disconnected = chan_disconnected,
    .recv = chan_recv,
    .alloc_buf = chan_alloc_buf,
};

static int accept_conn(struct bt_conn *conn, struct bt_l2cap_server *server,
                       struct bt_l2cap_chan **chan_ptr)
{
    LOG_INF("Accepting L2CAP connection");
    
    /* Initialize channel for this connection */
    memset(&le_chan, 0, sizeof(le_chan));
    le_chan.chan.ops = &chan_ops;
    le_chan.rx.mtu = L2CAP_MTU;
    
    *chan_ptr = &le_chan.chan;
    return 0;
}

static struct bt_l2cap_server server = {
    .psm = MY_PSM,
    .sec_level = BT_SECURITY_L1,
    .accept = accept_conn,
};

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    struct bt_conn_info info;
    
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    
    connecting = false; /* Clear connecting flag */
    
    if (err) {
        LOG_ERR("Connection failed to %s (err %u)", addr, err);
        
        /* Stop any ongoing advertising/scanning */
        if (is_advertising) {
            bt_le_adv_stop();
            is_advertising = false;
        }
        if (is_scanning) {
            bt_le_scan_stop();
            is_scanning = false;
        }
        
        /* Restart after a delay */
        k_sleep(K_MSEC(1000));
        start_advertising();
        k_sleep(K_MSEC(500));
        start_scanning();
        return;
    }

    LOG_INF("Connected to %s", addr);

    if (!default_conn) {
        default_conn = bt_conn_ref(conn);
    }

    /* Stop advertising and scanning when connected */
    if (is_advertising) {
        bt_le_adv_stop();
        is_advertising = false;
    }
    if (is_scanning) {
        bt_le_scan_stop();
        is_scanning = false;
    }

    /* If we are central, initiate L2CAP connection */
    bt_conn_get_info(conn, &info);
    if (info.role == BT_CONN_ROLE_CENTRAL) {
        LOG_INF("Central role - initiating L2CAP connection");
        
        /* Initialize channel */
        memset(&le_chan, 0, sizeof(le_chan));
        le_chan.chan.ops = &chan_ops;
        le_chan.rx.mtu = L2CAP_MTU;
        
        k_sleep(K_MSEC(100)); /* Small delay to ensure connection is stable */
        
        int ret = bt_l2cap_chan_connect(conn, &le_chan.chan, MY_PSM);
        if (ret) {
            LOG_ERR("L2CAP connect error: %d", ret);
        }
    }
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];
    
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Disconnected from %s (reason 0x%02x)", addr, reason);
    
    if (default_conn == conn) {
        bt_conn_unref(default_conn);
        default_conn = NULL;
        connecting = false;
        
        /* Restart advertising and scanning after disconnect */
        LOG_INF("Restarting advertising and scanning");
        
        k_sleep(K_MSEC(1000)); // Wait a bit before restarting
        
        start_advertising();
        k_sleep(K_MSEC(500));
        start_scanning();
    }
}

static struct bt_conn_cb conn_callbacks = {
    .connected = connected_cb,
    .disconnected = disconnected_cb,
};

static bool check_pingpong_uuid(struct bt_data *data, void *user_data)
{
    bool *found = user_data;
    uint16_t uuid;
    
    if (data->type == BT_DATA_UUID16_ALL && data->data_len >= 2) {
        uuid = sys_get_le16(data->data);
        if (uuid == PINGPONG_UUID) {
            *found = true;
            return false; /* Stop parsing */
        }
    }
    
    return true; /* Continue parsing */
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
                   struct net_buf_simple *buf)
{
    char addr_str[BT_ADDR_LE_STR_LEN];
    bool found_uuid = false;
    
    /* Already connected or connecting */
    if (default_conn || connecting) {
        return;
    }
    
    /* Parse advertising data to check for our UUID */
    bt_data_parse(buf, check_pingpong_uuid, &found_uuid);
    
    if (!found_uuid) {
        return; /* Not our ping-pong device */
    }
    
    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
    
    /* Stop scanning and advertising before connecting */
    if (is_scanning) {
        bt_le_scan_stop();
        is_scanning = false;
    }
    if (is_advertising) {
        bt_le_adv_stop();
        is_advertising = false;
    }
    
    LOG_INF("Found ping-pong device at %s, connecting...", addr_str);
    
    connecting = true;
    
    struct bt_conn *conn;
    int err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
                                BT_LE_CONN_PARAM_DEFAULT, &conn);
    if (err) {
        LOG_ERR("Failed to create connection: %d", err);
        connecting = false;
        
        /* Restart after a delay on error */
        k_sleep(K_MSEC(2000));
        start_advertising();
        k_sleep(K_MSEC(500));
        start_scanning();
    } else {
        bt_conn_unref(conn);
    }
}

static void start_advertising(void)
{
    if (is_advertising) return;
    
    struct bt_le_adv_param adv_param = {
        .options = BT_LE_ADV_OPT_CONNECTABLE,
        .interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
        .interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
    };
    
    int err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        LOG_ERR("Advertising failed to start: %d", err);
    } else {
        LOG_INF("Advertising started with UUID 0x%04x", PINGPONG_UUID);
        is_advertising = true;
    }
}

static void start_scanning(void)
{
    if (is_scanning) return;
    
    struct bt_le_scan_param scan_param = {
        .type = BT_HCI_LE_SCAN_ACTIVE,
        .options = BT_LE_SCAN_OPT_NONE,
        .interval = BT_GAP_SCAN_FAST_INTERVAL,
        .window = BT_GAP_SCAN_FAST_WINDOW,
    };
    
    int err = bt_le_scan_start(&scan_param, scan_cb);
    if (err) {
        LOG_ERR("Scanning failed to start: %d", err);
    } else {
        LOG_INF("Scanning started (looking for UUID 0x%04x)", PINGPONG_UUID);
        is_scanning = true;
    }
}

int main(void)
{
    int err;
    
    LOG_INF("Starting L2CAP Ping-Pong example");
    
    /* Enable Bluetooth */
    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("bt_enable failed: %d", err);
        return 0;
    }

    LOG_INF("Bluetooth initialized");
    
    /* Register connection callbacks */
    bt_conn_cb_register(&conn_callbacks);

    /* Register L2CAP server */
    err = bt_l2cap_server_register(&server);
    if (err) {
        LOG_ERR("Failed to register L2CAP server: %d", err);
        return 0;
    }
    LOG_INF("L2CAP server registered (PSM: 0x%04x)", MY_PSM);

    /* Start operations with proper sequencing */
    k_sleep(K_MSEC(1000));
    start_advertising();
    k_sleep(K_MSEC(1000));
    start_scanning();

    /* Main loop */
    while (1) {
        k_sleep(K_SECONDS(1));
    }

    return 0;
}