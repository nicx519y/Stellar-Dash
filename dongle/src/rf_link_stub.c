#include "rf_link.h"

#include <string.h>

#include "dongle_config.h"
#include "platform_port.h"
#include "rf_protocol.h"

#include <stddef.h>

#ifdef CH585
#include "CH58x_common.h"
#endif

typedef enum {
    RF_MODE_IDLE = 0,
    RF_MODE_PAIRING,
    RF_MODE_CONNECTING,
    RF_MODE_CONNECTED
} rf_mode_t;

typedef struct {
    bool valid;
    uint32_t peer_uid;
    uint32_t nonce_local;
    uint32_t nonce_peer;
    uint8_t hop_seed;
} rf_bond_t;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t feature_flags;
    uint16_t length;
    uint32_t peer_uid;
    uint32_t nonce_local;
    uint32_t nonce_peer;
    uint8_t hop_seed;
    uint8_t reserved[3];
    uint32_t auth_tag;
    uint32_t checksum;
} rf_bond_store_t;

static rf_packet_cb_t s_packet_cb;
static bool s_connected;
static bool s_has_bond;
static rf_link_event_t s_event;
static rf_mode_t s_mode;
static rf_bond_t s_bond;
static uint32_t s_mode_deadline_us;
static uint32_t s_last_rx_us;
static uint32_t s_next_conn_req_us;
static uint8_t s_tx_seq;
static uint8_t s_last_rx_seq;
static uint32_t s_reject_count;
static uint8_t s_scan_cursor;
static uint8_t s_last_data_seq;
static bool s_has_last_data_seq;
static uint16_t s_lq_rx_ok;
static uint16_t s_lq_rx_fail;
static uint16_t s_lq_tx_fail;
static uint32_t s_lq_eval_deadline_us;
static uint32_t s_next_heartbeat_us;
static uint8_t s_tx_power_level;

static const uint8_t s_channel_plan[] = {
    2u, 5u, 8u, 11u, 14u, 17u, 20u, 23u,
    26u, 29u, 32u, 35u, 38u, 12u, 19u, 31u
};

#define BOND_STORE_MAGIC      (0x444E4244u) /* DNBD */
#define BOND_STORE_VERSION    (1u)
#define BOND_FEATURE_FLAGS    (0x01u)       /* bit0: strict device validation */
#define RF_HOP_AUTO           (0xFFu)
#define RF_CONNECT_RETRY_MAX  (2u)
#define RF_HEARTBEAT_RETRY    (1u)
#define RF_HEARTBEAT_INTERVAL_US (8000u)
#define RF_LQ_EVAL_PERIOD_US  (200000u)
#define RF_TX_PWR_MIN         (0u)
#define RF_TX_PWR_MAX         (3u)
#define RF_TX_PWR_DEFAULT     (2u)

#ifdef CH585
#define BOND_NVM_ADDR         (0u)
#define BOND_NVM_ERASE_SIZE   (EEPROM_MIN_ER_SIZE)
#endif

static uint32_t checksum32(const uint8_t *data, size_t len)
{
    uint32_t s = 0u;
    size_t i;
    for (i = 0u; i < len; ++i) {
        s = (s << 5) - s + data[i];
    }
    return s;
}

static uint32_t calc_auth_tag(uint32_t peer_uid, uint32_t nonce_local, uint32_t nonce_peer, uint8_t hop_seed)
{
    uint32_t x = peer_uid ^ nonce_local ^ ((nonce_peer << 7) | (nonce_peer >> 25));
    x ^= ((uint32_t)hop_seed << 24) | ((uint32_t)hop_seed << 8);
    x ^= 0x6D5A56A5u;
    return x;
}

static uint8_t channel_plan_size(void)
{
    return (uint8_t)(sizeof(s_channel_plan) / sizeof(s_channel_plan[0]));
}

static uint8_t normalize_hop_idx(uint8_t hop_idx)
{
    uint8_t n = channel_plan_size();
    if (n == 0u) {
        return 0u;
    }
    return (uint8_t)(hop_idx % n);
}

static uint8_t derive_hop_idx(uint8_t seq)
{
    uint8_t n = channel_plan_size();
    if ((n == 0u) || !s_has_bond || !s_bond.valid) {
        return 0u;
    }
    return (uint8_t)((s_bond.hop_seed + seq) % n);
}

__attribute__((weak))
void rf_hw_set_channel(uint8_t channel)
{
    (void)channel;
}

__attribute__((weak))
void rf_hw_set_tx_power(uint8_t level)
{
    (void)level;
}

__attribute__((weak))
void rf_hw_enable_link_guard(uint8_t enable_crc, uint8_t enable_ack, uint8_t enable_agc)
{
    (void)enable_crc;
    (void)enable_ack;
    (void)enable_agc;
}

__attribute__((weak))
bool rf_hw_init(void)
{
    return true;
}

static void apply_channel(uint8_t hop_idx)
{
    uint8_t idx = normalize_hop_idx(hop_idx);
    rf_hw_set_channel(s_channel_plan[idx]);
}

static void apply_tx_power(uint8_t level)
{
    if (level > RF_TX_PWR_MAX) {
        level = RF_TX_PWR_MAX;
    }
    s_tx_power_level = level;
    rf_hw_set_tx_power(level);
}

static void bump_power(void)
{
    if (s_tx_power_level < RF_TX_PWR_MAX) {
        apply_tx_power((uint8_t)(s_tx_power_level + 1u));
    }
}

static void drop_power(void)
{
    if (s_tx_power_level > RF_TX_PWR_MIN) {
        apply_tx_power((uint8_t)(s_tx_power_level - 1u));
    }
}

static uint8_t scan_next_hop_idx(void)
{
    uint8_t idx = normalize_hop_idx(s_scan_cursor);
    s_scan_cursor = (uint8_t)(idx + 1u);
    if (s_scan_cursor >= channel_plan_size()) {
        s_scan_cursor = 0u;
    }
    apply_channel(idx);
    return idx;
}

static bool is_packet_from_bonded_peer(const rf_proto_frame_t *pkt)
{
    uint8_t expect;
    uint8_t expect_prev;

    if (!s_has_bond || !s_bond.valid) {
        return false;
    }
    if (pkt->hdr.hop_idx >= channel_plan_size()) {
        return false;
    }
    if (s_mode != RF_MODE_CONNECTED) {
        return true;
    }

    expect = derive_hop_idx(pkt->hdr.seq);
    expect_prev = (expect == 0u) ? (uint8_t)(channel_plan_size() - 1u) : (uint8_t)(expect - 1u);
    return ((pkt->hdr.hop_idx == expect) || (pkt->hdr.hop_idx == expect_prev));
}

static void eval_link_quality(uint32_t now_us)
{
    uint16_t fail_score;

    if ((int32_t)(now_us - s_lq_eval_deadline_us) < 0) {
        return;
    }
    s_lq_eval_deadline_us = now_us + RF_LQ_EVAL_PERIOD_US;

    fail_score = (uint16_t)(s_lq_rx_fail + (uint16_t)(s_lq_tx_fail * 2u));

    if (fail_score > (uint16_t)(s_lq_rx_ok + 8u)) {
        bump_power();
    } else if ((fail_score == 0u) && (s_lq_rx_ok > 20u)) {
        drop_power();
    }

    s_lq_rx_ok = 0u;
    s_lq_rx_fail = 0u;
    s_lq_tx_fail = 0u;
}

/* Hardware hook functions (replace in real RF driver). */
__attribute__((weak))
bool rf_hw_read_frame(uint8_t *buf, size_t *inout_len)
{
    (void)buf;
    (void)inout_len;
    return false;
}

__attribute__((weak))
bool rf_hw_send_frame(const uint8_t *buf, size_t len)
{
    (void)buf;
    (void)len;
    return true;
}

/* Bond persistence hooks (replace if custom storage backend is needed). */
__attribute__((weak))
bool rf_hw_bond_load(void *buf, size_t len)
{
#ifdef CH585
    if ((buf == 0) || (len == 0u)) {
        return false;
    }
    return (EEPROM_READ(BOND_NVM_ADDR, buf, (uint32_t)len) == 0u);
#else
    (void)buf;
    (void)len;
    return false;
#endif
}

__attribute__((weak))
bool rf_hw_bond_store(const void *buf, size_t len)
{
#ifdef CH585
    uint32_t page_words[BOND_NVM_ERASE_SIZE / 4u];
    uint8_t *page = (uint8_t *)page_words;
    size_t i;

    if ((buf == 0) || (len == 0u) || (len > BOND_NVM_ERASE_SIZE)) {
        return false;
    }

    for (i = 0u; i < (BOND_NVM_ERASE_SIZE / 4u); ++i) {
        page_words[i] = 0xFFFFFFFFu;
    }
    memcpy(page, buf, len);

    if (EEPROM_ERASE(BOND_NVM_ADDR, BOND_NVM_ERASE_SIZE) != 0u) {
        return false;
    }
    return (EEPROM_WRITE(BOND_NVM_ADDR, page, BOND_NVM_ERASE_SIZE) == 0u);
#else
    (void)buf;
    (void)len;
    return false;
#endif
}

__attribute__((weak))
bool rf_hw_bond_clear(void)
{
#ifdef CH585
    return (EEPROM_ERASE(BOND_NVM_ADDR, BOND_NVM_ERASE_SIZE) == 0u);
#else
    return false;
#endif
}

static bool bond_store_load(void)
{
    rf_bond_store_t rec;
    uint32_t chk;

    memset(&rec, 0, sizeof(rec));
    if (!rf_hw_bond_load(&rec, sizeof(rec))) {
        return false;
    }

    if ((rec.magic != BOND_STORE_MAGIC) ||
        (rec.version != BOND_STORE_VERSION) ||
        (rec.length != (uint16_t)sizeof(rec))) {
        return false;
    }

    chk = checksum32((const uint8_t *)&rec, offsetof(rf_bond_store_t, checksum));
    if (chk != rec.checksum) {
        return false;
    }

    if (rec.auth_tag != calc_auth_tag(rec.peer_uid, rec.nonce_local, rec.nonce_peer, rec.hop_seed)) {
        return false;
    }

    s_bond.peer_uid = rec.peer_uid;
    s_bond.nonce_local = rec.nonce_local;
    s_bond.nonce_peer = rec.nonce_peer;
    s_bond.hop_seed = rec.hop_seed;
    s_bond.valid = true;
    s_has_bond = true;
    return true;
}

static bool bond_store_save(void)
{
    rf_bond_store_t rec;

    memset(&rec, 0, sizeof(rec));
    rec.magic = BOND_STORE_MAGIC;
    rec.version = BOND_STORE_VERSION;
    rec.feature_flags = BOND_FEATURE_FLAGS;
    rec.length = (uint16_t)sizeof(rec);
    rec.peer_uid = s_bond.peer_uid;
    rec.nonce_local = s_bond.nonce_local;
    rec.nonce_peer = s_bond.nonce_peer;
    rec.hop_seed = s_bond.hop_seed;
    rec.auth_tag = calc_auth_tag(rec.peer_uid, rec.nonce_local, rec.nonce_peer, rec.hop_seed);
    rec.checksum = checksum32((const uint8_t *)&rec, offsetof(rf_bond_store_t, checksum));

    return rf_hw_bond_store(&rec, sizeof(rec));
}

static void bond_store_clear(void)
{
    (void)rf_hw_bond_clear();
}

static void set_event(rf_link_event_t ev)
{
    if (s_event == RF_LINK_EVENT_NONE) {
        s_event = ev;
    }
}

static bool send_packet(rf_packet_type_t type, const uint8_t *payload, uint8_t payload_len, uint8_t hop_idx)
{
    rf_proto_frame_t frame;
    uint8_t raw[RF_PROTO_MAX_FRAME];
    size_t raw_len;
    uint8_t i;

    if (payload_len > RF_PROTO_MAX_PAYLOAD) {
        return false;
    }

    memset(&frame, 0, sizeof(frame));
    frame.hdr.version = RF_PROTO_VERSION;
    frame.hdr.type = (uint8_t)type;
    frame.hdr.seq = s_tx_seq++;
    frame.hdr.ack_seq = s_last_rx_seq;
    frame.hdr.ack_bits = 0u;
    if (hop_idx == RF_HOP_AUTO) {
        frame.hdr.hop_idx = derive_hop_idx(frame.hdr.seq);
    } else {
        frame.hdr.hop_idx = normalize_hop_idx(hop_idx);
    }
    frame.hdr.epoch_lsb = (uint8_t)(platform_now_us() & 0xFFu);
    frame.payload_len = payload_len;
    apply_channel(frame.hdr.hop_idx);

    for (i = 0u; i < payload_len; ++i) {
        frame.payload[i] = payload[i];
    }

    raw_len = rf_protocol_encode(&frame, raw, sizeof(raw));
    if (raw_len == 0u) {
        return false;
    }

    return rf_hw_send_frame(raw, raw_len);
}

static bool send_packet_retry(rf_packet_type_t type, const uint8_t *payload, uint8_t payload_len, uint8_t hop_idx, uint8_t max_retry)
{
    uint8_t i;
    for (i = 0u; i <= max_retry; ++i) {
        uint8_t use_hop = hop_idx;
        if ((hop_idx != RF_HOP_AUTO) && (i != 0u)) {
            use_hop = scan_next_hop_idx();
        }
        if (send_packet(type, payload, payload_len, use_hop)) {
            return true;
        }
        s_lq_tx_fail++;
    }
    return false;
}

static uint32_t read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void write_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void on_packet_pairing(const rf_proto_frame_t *pkt, uint32_t now_us)
{
    uint8_t rsp[9];

    if ((pkt->hdr.type == RF_PKT_ADV_REQ) && (pkt->payload_len >= 4u)) {
        s_bond.peer_uid = read_u32_le(pkt->payload);
        s_bond.nonce_local = now_us ^ 0xA55A33CCu;
        s_bond.hop_seed = (uint8_t)(s_bond.nonce_local & 0x1Fu);

        write_u32_le(&rsp[0], s_bond.nonce_local);
        rsp[4] = s_bond.hop_seed;
        rsp[5] = 0x01u; /* feature bit0: 8K capable */
        rsp[6] = 0x20u; /* channel count */
        rsp[7] = 0x02u; /* PHY code: 2Mbps */
        rsp[8] = 0x00u; /* reserved */
        (void)send_packet(RF_PKT_ADV_RSP, rsp, sizeof(rsp), 0u);
        return;
    }

    if ((pkt->hdr.type == RF_PKT_PAIR_CONFIRM) && (pkt->payload_len >= 8u)) {
        s_bond.nonce_peer = read_u32_le(pkt->payload);
        if (read_u32_le(&pkt->payload[4]) == s_bond.nonce_local) {
            s_bond.valid = true;
            s_has_bond = true;
            if (!bond_store_save()) {
                s_reject_count++;
                s_lq_tx_fail++;
                s_bond.valid = false;
                s_has_bond = false;
                return;
            }
            s_mode = RF_MODE_IDLE;
            set_event(RF_LINK_EVENT_PAIRING_DONE);
        } else {
            s_reject_count++;
        }
    }
}

static void on_packet_connecting(const rf_proto_frame_t *pkt, uint32_t now_us)
{
    uint32_t ack_uid;
    uint32_t ack_tag;
    uint32_t expect_tag;
    (void)now_us;

    if (pkt->hdr.type != RF_PKT_CONN_ACK) {
        return;
    }

    if (pkt->payload_len < 8u) {
        s_reject_count++;
        s_lq_rx_fail++;
        return;
    }

    ack_uid = read_u32_le(pkt->payload);
    ack_tag = read_u32_le(&pkt->payload[4]);
    expect_tag = calc_auth_tag(s_bond.peer_uid, s_bond.nonce_peer, s_bond.nonce_local, s_bond.hop_seed);

    if ((ack_uid != s_bond.peer_uid) || (ack_tag != expect_tag) || !is_packet_from_bonded_peer(pkt)) {
        s_reject_count++;
        s_lq_rx_fail++;
        return;
    }

    s_lq_rx_ok++;
    s_connected = true;
    s_mode = RF_MODE_CONNECTED;
    s_last_rx_us = platform_now_us();
    s_next_heartbeat_us = s_last_rx_us + RF_HEARTBEAT_INTERVAL_US;
    apply_channel(derive_hop_idx((uint8_t)(s_tx_seq + 1u)));
    set_event(RF_LINK_EVENT_CONNECT_DONE);
}

static void on_packet_connected(const rf_proto_frame_t *pkt, uint32_t now_us)
{
    if (!is_packet_from_bonded_peer(pkt)) {
        s_reject_count++;
        s_lq_rx_fail++;
        return;
    }

    apply_channel(pkt->hdr.hop_idx);
    s_last_rx_us = now_us;
    s_lq_rx_ok++;

    if ((pkt->hdr.type == RF_PKT_INPUT_DATA) && (pkt->payload_len > 0u) && (s_packet_cb != 0)) {
        if (s_has_last_data_seq && (pkt->hdr.seq == s_last_data_seq)) {
            return;
        }
        s_last_data_seq = pkt->hdr.seq;
        s_has_last_data_seq = true;
        s_packet_cb(pkt->payload, pkt->payload_len);
        return;
    }

    if (pkt->hdr.type == RF_PKT_UNBIND) {
        rf_link_clear_bond();
    }
}

void rf_link_init(rf_packet_cb_t cb)
{
    uint32_t now_us;

    s_packet_cb = cb;
    s_connected = false;
    s_has_bond = false;
    s_event = RF_LINK_EVENT_NONE;
    s_mode = RF_MODE_IDLE;
    memset(&s_bond, 0, sizeof(s_bond));
    s_mode_deadline_us = 0u;
    s_last_rx_us = 0u;
    s_next_conn_req_us = 0u;
    s_tx_seq = 0u;
    s_last_rx_seq = 0u;
    s_reject_count = 0u;
    s_scan_cursor = 0u;
    s_last_data_seq = 0u;
    s_has_last_data_seq = false;
    s_lq_rx_ok = 0u;
    s_lq_rx_fail = 0u;
    s_lq_tx_fail = 0u;
    now_us = platform_now_us();
    s_lq_eval_deadline_us = now_us + RF_LQ_EVAL_PERIOD_US;
    s_next_heartbeat_us = now_us + RF_HEARTBEAT_INTERVAL_US;

#if (DONGLE_DIAG_RF_INIT_PHASE >= 1u)
    (void)rf_hw_init();
#endif
#if (DONGLE_DIAG_RF_INIT_PHASE >= 2u)
    rf_hw_enable_link_guard(1u, 1u, 1u);
#endif
#if (DONGLE_DIAG_RF_INIT_PHASE >= 3u)
    apply_tx_power(RF_TX_PWR_DEFAULT);
#endif
#if (DONGLE_DIAG_RF_INIT_PHASE >= 4u)
    apply_channel(0u);
#endif
    (void)bond_store_load();
}

void rf_link_poll(void)
{
    uint32_t now_us = platform_now_us();
    uint8_t raw[RF_PROTO_MAX_FRAME];
    rf_proto_frame_t pkt;
    size_t raw_len;
    uint8_t frames_budget = 4u;

    while (frames_budget > 0u) {
        raw_len = sizeof(raw);
        if (!rf_hw_read_frame(raw, &raw_len)) {
            break;
        }
        frames_budget--;
        if (!rf_protocol_decode(raw, raw_len, &pkt)) {
            continue;
        }

        s_last_rx_seq = pkt.hdr.seq;
        if (s_mode == RF_MODE_PAIRING) {
            on_packet_pairing(&pkt, now_us);
        } else if (s_mode == RF_MODE_CONNECTING) {
            on_packet_connecting(&pkt, now_us);
        } else if (s_mode == RF_MODE_CONNECTED) {
            on_packet_connected(&pkt, now_us);
        }
    }

    if ((s_mode == RF_MODE_PAIRING) && ((int32_t)(now_us - s_mode_deadline_us) >= 0)) {
        s_mode = RF_MODE_IDLE;
        set_event(RF_LINK_EVENT_PAIRING_TIMEOUT);
    }

    if (s_mode == RF_MODE_CONNECTING) {
        if ((int32_t)(now_us - s_next_conn_req_us) >= 0) {
            uint8_t req[9];
            uint32_t req_tag;
            uint8_t scan_hop = scan_next_hop_idx();
            write_u32_le(&req[0], s_bond.peer_uid);
            req[4] = s_bond.hop_seed;
            req_tag = calc_auth_tag(s_bond.peer_uid, s_bond.nonce_local, s_bond.nonce_peer, s_bond.hop_seed);
            write_u32_le(&req[5], req_tag);
            (void)send_packet_retry(RF_PKT_CONN_REQ, req, sizeof(req), scan_hop, RF_CONNECT_RETRY_MAX);
            s_next_conn_req_us = now_us + 2000u;
        }
        if ((int32_t)(now_us - s_mode_deadline_us) >= 0) {
            s_mode = RF_MODE_IDLE;
            set_event(RF_LINK_EVENT_CONNECT_TIMEOUT);
        }
    }

    if (s_mode == RF_MODE_CONNECTED) {
        if ((int32_t)(now_us - s_last_rx_us) > 30000) {
            s_connected = false;
            s_mode = RF_MODE_CONNECTING;
            s_mode_deadline_us = now_us + 3000000u;
            s_next_conn_req_us = now_us;
            s_scan_cursor = normalize_hop_idx(s_bond.hop_seed);
            s_next_heartbeat_us = now_us + RF_HEARTBEAT_INTERVAL_US;
            set_event(RF_LINK_EVENT_LINK_LOST);
        } else if ((int32_t)(now_us - s_next_heartbeat_us) >= 0) {
            (void)send_packet_retry(RF_PKT_HEARTBEAT, 0, 0u, RF_HOP_AUTO, RF_HEARTBEAT_RETRY);
            s_next_heartbeat_us += RF_HEARTBEAT_INTERVAL_US;
        }
    }

    eval_link_quality(now_us);
}

bool rf_link_is_connected(void)
{
    return s_connected;
}

bool rf_link_has_bond(void)
{
    return s_has_bond;
}

void rf_link_clear_bond(void)
{
    s_connected = false;
    s_has_bond = false;
    s_event = RF_LINK_EVENT_NONE;
    s_mode = RF_MODE_IDLE;
    memset(&s_bond, 0, sizeof(s_bond));
    s_has_last_data_seq = false;
    bond_store_clear();
}

void rf_link_start_pairing(void)
{
    s_connected = false;
    s_mode = RF_MODE_PAIRING;
    s_mode_deadline_us = platform_now_us() + 10000000u;
    s_bond.valid = false;
}

void rf_link_stop_pairing(void)
{
    if (s_mode == RF_MODE_PAIRING) {
        s_mode = RF_MODE_IDLE;
        s_mode_deadline_us = 0u;
    }
}

void rf_link_start_connect(void)
{
    if (!s_has_bond) {
        return;
    }
    s_connected = false;
    s_mode = RF_MODE_CONNECTING;
    s_mode_deadline_us = platform_now_us() + 3000000u;
    s_next_conn_req_us = platform_now_us();
    s_scan_cursor = normalize_hop_idx(s_bond.hop_seed);
    apply_channel(s_scan_cursor);
}

void rf_link_stop_connect(void)
{
    if (s_mode == RF_MODE_CONNECTING) {
        s_mode = RF_MODE_IDLE;
        s_mode_deadline_us = 0u;
        s_next_conn_req_us = 0u;
    }
}

rf_link_event_t rf_link_take_event(void)
{
    rf_link_event_t ev = s_event;
    s_event = RF_LINK_EVENT_NONE;
    return ev;
}
