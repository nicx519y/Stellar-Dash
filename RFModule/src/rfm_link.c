#include "rfm_link.h"

#include <stddef.h>
#include <string.h>

#include "platform_port.h"
#include "rfm_protocol.h"

#if defined(CH585) || defined(CH584)
#include "CH58x_common.h"
#endif

#define RFM_DEVICE_UID            (0x584D0001u)
#define RFM_BOND_MAGIC            (0x4D424652u) /* RFBM */
#define RFM_BOND_VERSION          (1u)
#define RFM_BOND_FEATURE_FLAGS    (0x01u)
#define RFM_HOP_AUTO              (0xFFu)

#define RFM_CONN_ACK_RETRY        (2u)
#define RFM_INPUT_RETRY           (1u)

#define RFM_TX_PWR_MIN            (0u)
#define RFM_TX_PWR_MAX            (3u)
#define RFM_TX_PWR_DEFAULT        (2u)

#if defined(CH585) || defined(CH584)
#define RFM_BOND_NVM_ADDR         (0u)
#define RFM_BOND_NVM_ERASE_SIZE   (EEPROM_MIN_ER_SIZE)
#endif

typedef struct {
    bool valid;
    uint32_t peer_uid;
    uint32_t nonce_local;
    uint32_t nonce_peer;
    uint8_t hop_seed;
} rfm_bond_t;

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
} rfm_bond_store_t;

static const uint8_t s_channel_plan[RFM_CHANNEL_TABLE_SIZE] = {
    2u, 5u, 8u, 11u, 14u, 17u, 20u, 23u,
    26u, 29u, 32u, 35u, 38u, 12u, 19u, 31u
};

static rfm_state_t s_state;
static rfm_event_t s_event;
static bool s_connected;
static bool s_has_bond;
static rfm_bond_t s_bond;

static uint8_t s_tx_seq;
static uint8_t s_last_rx_seq;
static uint8_t s_scan_cursor;
static uint8_t s_tx_power_level;
static uint32_t s_reject_count;

static uint8_t s_latest_input[RFM_RF_INPUT_PAYLOAD_LEN];
static bool s_input_valid;
static uint32_t s_last_rx_us;
static uint32_t s_next_adv_us;
static uint32_t s_mode_deadline_us;
static uint32_t s_pair_ok_deadline_us;
static uint32_t s_next_scan_us;
static uint32_t s_next_report_us;

static rfm_report_rate_t s_rate_hz;
static uint16_t s_lq_rx_ok;
static uint16_t s_lq_rx_fail;
static uint16_t s_lq_tx_fail;
static uint32_t s_lq_eval_deadline_us;

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
bool rf_hw_bond_load(void *buf, size_t len)
{
#if defined(CH585) || defined(CH584)
    if ((buf == 0) || (len == 0u)) {
        return false;
    }
    return (EEPROM_READ(RFM_BOND_NVM_ADDR, buf, (uint32_t)len) == 0u);
#else
    (void)buf;
    (void)len;
    return false;
#endif
}

__attribute__((weak))
bool rf_hw_bond_store(const void *buf, size_t len)
{
#if defined(CH585) || defined(CH584)
    uint32_t page_words[RFM_BOND_NVM_ERASE_SIZE / 4u];
    uint8_t *page = (uint8_t *)page_words;
    size_t i;

    if ((buf == 0) || (len == 0u) || (len > RFM_BOND_NVM_ERASE_SIZE)) {
        return false;
    }
    for (i = 0u; i < (RFM_BOND_NVM_ERASE_SIZE / 4u); ++i) {
        page_words[i] = 0xFFFFFFFFu;
    }
    memcpy(page, buf, len);
    if (EEPROM_ERASE(RFM_BOND_NVM_ADDR, RFM_BOND_NVM_ERASE_SIZE) != 0u) {
        return false;
    }
    return (EEPROM_WRITE(RFM_BOND_NVM_ADDR, page, RFM_BOND_NVM_ERASE_SIZE) == 0u);
#else
    (void)buf;
    (void)len;
    return false;
#endif
}

__attribute__((weak))
bool rf_hw_bond_clear(void)
{
#if defined(CH585) || defined(CH584)
    return (EEPROM_ERASE(RFM_BOND_NVM_ADDR, RFM_BOND_NVM_ERASE_SIZE) == 0u);
#else
    return false;
#endif
}

static uint32_t checksum32(const uint8_t *data, size_t len)
{
    uint32_t s = 0u;
    size_t i;
    for (i = 0u; i < len; ++i) {
        s = (s << 5) - s + data[i];
    }
    return s;
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

static uint32_t calc_auth_tag(uint32_t peer_uid, uint32_t nonce_local, uint32_t nonce_peer, uint8_t hop_seed)
{
    uint32_t x = peer_uid ^ nonce_local ^ ((nonce_peer << 7) | (nonce_peer >> 25));
    x ^= ((uint32_t)hop_seed << 24) | ((uint32_t)hop_seed << 8);
    x ^= 0x6D5A56A5u;
    return x;
}

static uint8_t derive_hop_idx(uint8_t seq)
{
    if (!s_has_bond || !s_bond.valid) {
        return 0u;
    }
    return (uint8_t)((s_bond.hop_seed + seq) % RFM_CHANNEL_TABLE_SIZE);
}

static void apply_channel(uint8_t hop_idx)
{
    uint8_t idx = (uint8_t)(hop_idx % RFM_CHANNEL_TABLE_SIZE);
    rf_hw_set_channel(s_channel_plan[idx]);
}

static void apply_tx_power(uint8_t level)
{
    if (level > RFM_TX_PWR_MAX) {
        level = RFM_TX_PWR_MAX;
    }
    s_tx_power_level = level;
    rf_hw_set_tx_power(level);
}

static uint32_t report_period_us(void)
{
    switch (s_rate_hz) {
    case RFM_RATE_8K:
        return 125u;
    case RFM_RATE_4K:
        return 250u;
    case RFM_RATE_2K:
        return 500u;
    case RFM_RATE_1K:
    default:
        return 1000u;
    }
}

static void set_event(rfm_event_t ev)
{
    if (s_event == RFM_EVENT_NONE) {
        s_event = ev;
    }
}

static void change_state(rfm_state_t st)
{
    s_state = st;
    switch (st) {
    case RFM_STATE_PAIRING:
        set_event(RFM_EVENT_PAIRING);
        break;
    case RFM_STATE_PAIR_OK:
        set_event(RFM_EVENT_PAIRED);
        break;
    case RFM_STATE_CONNECTING:
    case RFM_STATE_RECONNECTING:
        set_event(RFM_EVENT_CONNECTING);
        break;
    case RFM_STATE_CONNECTED:
        set_event(RFM_EVENT_CONNECTED);
        break;
    case RFM_STATE_IDLE:
    default:
        break;
    }
}

static bool bond_store_load(void)
{
    rfm_bond_store_t rec;
    uint32_t chk;

    memset(&rec, 0, sizeof(rec));
    if (!rf_hw_bond_load(&rec, sizeof(rec))) {
        return false;
    }
    if ((rec.magic != RFM_BOND_MAGIC) ||
        (rec.version != RFM_BOND_VERSION) ||
        (rec.length != (uint16_t)sizeof(rec))) {
        return false;
    }

    chk = checksum32((const uint8_t *)&rec, offsetof(rfm_bond_store_t, checksum));
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
    rfm_bond_store_t rec;

    memset(&rec, 0, sizeof(rec));
    rec.magic = RFM_BOND_MAGIC;
    rec.version = RFM_BOND_VERSION;
    rec.feature_flags = RFM_BOND_FEATURE_FLAGS;
    rec.length = (uint16_t)sizeof(rec);
    rec.peer_uid = s_bond.peer_uid;
    rec.nonce_local = s_bond.nonce_local;
    rec.nonce_peer = s_bond.nonce_peer;
    rec.hop_seed = s_bond.hop_seed;
    rec.auth_tag = calc_auth_tag(rec.peer_uid, rec.nonce_local, rec.nonce_peer, rec.hop_seed);
    rec.checksum = checksum32((const uint8_t *)&rec, offsetof(rfm_bond_store_t, checksum));

    return rf_hw_bond_store(&rec, sizeof(rec));
}

static void bond_store_clear(void)
{
    (void)rf_hw_bond_clear();
}

static bool is_packet_from_bonded_peer(const rfm_proto_frame_t *pkt)
{
    uint8_t expect;
    uint8_t expect_prev;

    if (!s_has_bond || !s_bond.valid) {
        return false;
    }
    if (pkt->hdr.hop_idx >= RFM_CHANNEL_TABLE_SIZE) {
        return false;
    }
    if (s_state != RFM_STATE_CONNECTED) {
        return true;
    }

    expect = derive_hop_idx(pkt->hdr.seq);
    expect_prev = (expect == 0u) ? (uint8_t)(RFM_CHANNEL_TABLE_SIZE - 1u) : (uint8_t)(expect - 1u);
    return ((pkt->hdr.hop_idx == expect) || (pkt->hdr.hop_idx == expect_prev));
}

static bool send_packet(rfm_packet_type_t type, const uint8_t *payload, uint8_t payload_len, uint8_t hop_idx)
{
    rfm_proto_frame_t frame;
    uint8_t raw[RFM_PROTO_MAX_FRAME];
    size_t raw_len;
    uint8_t i;

    if (payload_len > RFM_PROTO_MAX_PAYLOAD) {
        return false;
    }

    memset(&frame, 0, sizeof(frame));
    frame.hdr.version = RFM_PROTO_VERSION;
    frame.hdr.type = (uint8_t)type;
    frame.hdr.seq = s_tx_seq++;
    frame.hdr.ack_seq = s_last_rx_seq;
    frame.hdr.ack_bits = 0u;
    frame.hdr.hop_idx = (hop_idx == RFM_HOP_AUTO) ? derive_hop_idx(frame.hdr.seq) : (uint8_t)(hop_idx % RFM_CHANNEL_TABLE_SIZE);
    frame.hdr.epoch_lsb = (uint8_t)(platform_now_us() & 0xFFu);
    frame.payload_len = payload_len;

    for (i = 0u; i < payload_len; ++i) {
        frame.payload[i] = payload[i];
    }

    apply_channel(frame.hdr.hop_idx);
    raw_len = rfm_protocol_encode(&frame, raw, sizeof(raw));
    if (raw_len == 0u) {
        return false;
    }
    return rf_hw_send_frame(raw, raw_len);
}

static bool send_packet_retry(rfm_packet_type_t type, const uint8_t *payload, uint8_t payload_len, uint8_t hop_idx, uint8_t retry)
{
    uint8_t i;
    uint8_t use_hop = hop_idx;

    for (i = 0u; i <= retry; ++i) {
        if (send_packet(type, payload, payload_len, use_hop)) {
            return true;
        }
        s_lq_tx_fail++;
        use_hop = (uint8_t)((use_hop + 1u) % RFM_CHANNEL_TABLE_SIZE);
    }
    return false;
}

static void on_adv_rsp(const rfm_proto_frame_t *pkt, uint32_t now_us)
{
    uint8_t confirm[8];

    if ((pkt->hdr.type != RFM_PKT_ADV_RSP) || (pkt->payload_len < 9u)) {
        return;
    }

    s_bond.peer_uid = RFM_DEVICE_UID;
    s_bond.nonce_local = read_u32_le(&pkt->payload[0]);
    s_bond.hop_seed = (uint8_t)(pkt->payload[4] % RFM_CHANNEL_TABLE_SIZE);
    s_bond.nonce_peer = now_us ^ 0xB14D26E7u;
    s_bond.valid = true;

    write_u32_le(&confirm[0], s_bond.nonce_peer);
    write_u32_le(&confirm[4], s_bond.nonce_local);
    if (!send_packet_retry(RFM_PKT_PAIR_CONFIRM, confirm, sizeof(confirm), 0u, 1u)) {
        s_reject_count++;
        set_event(RFM_EVENT_ERROR);
        return;
    }

    s_has_bond = true;
    if (!bond_store_save()) {
        s_bond.valid = false;
        s_has_bond = false;
        s_reject_count++;
        set_event(RFM_EVENT_ERROR);
        return;
    }

    change_state(RFM_STATE_PAIR_OK);
    s_pair_ok_deadline_us = now_us + 200000u;
}

static void on_conn_req(const rfm_proto_frame_t *pkt, uint32_t now_us)
{
    uint32_t uid;
    uint8_t hop_seed;
    uint32_t req_tag;
    uint32_t expect_req_tag;
    uint8_t ack[8];
    uint32_t ack_tag;

    if (pkt->hdr.type != RFM_PKT_CONN_REQ) {
        return;
    }
    if (pkt->payload_len < 9u) {
        s_reject_count++;
        s_lq_rx_fail++;
        return;
    }
    if (!is_packet_from_bonded_peer(pkt)) {
        s_reject_count++;
        s_lq_rx_fail++;
        return;
    }

    uid = read_u32_le(&pkt->payload[0]);
    hop_seed = pkt->payload[4];
    req_tag = read_u32_le(&pkt->payload[5]);
    expect_req_tag = calc_auth_tag(s_bond.peer_uid, s_bond.nonce_local, s_bond.nonce_peer, s_bond.hop_seed);

    if ((uid != s_bond.peer_uid) || (hop_seed != s_bond.hop_seed) || (req_tag != expect_req_tag)) {
        s_reject_count++;
        s_lq_rx_fail++;
        return;
    }

    ack_tag = calc_auth_tag(s_bond.peer_uid, s_bond.nonce_peer, s_bond.nonce_local, s_bond.hop_seed);
    write_u32_le(&ack[0], s_bond.peer_uid);
    write_u32_le(&ack[4], ack_tag);

    if (!send_packet_retry(RFM_PKT_CONN_ACK, ack, sizeof(ack), RFM_HOP_AUTO, RFM_CONN_ACK_RETRY)) {
        s_lq_tx_fail++;
        return;
    }

    s_connected = true;
    s_last_rx_us = now_us;
    s_next_report_us = now_us;
    s_scan_cursor = s_bond.hop_seed;
    s_lq_rx_ok++;
    change_state(RFM_STATE_CONNECTED);
}

static void on_connected_downlink(const rfm_proto_frame_t *pkt, uint32_t now_us)
{
    if (!is_packet_from_bonded_peer(pkt)) {
        s_reject_count++;
        s_lq_rx_fail++;
        return;
    }

    s_last_rx_us = now_us;
    s_lq_rx_ok++;

    if (pkt->hdr.type == RFM_PKT_UNBIND) {
        rfm_link_unbind();
        return;
    }

    if (pkt->hdr.type == RFM_PKT_CONN_REQ) {
        on_conn_req(pkt, now_us);
    }
}

static void eval_link_quality(uint32_t now_us)
{
    uint16_t fail_score;

    if ((int32_t)(now_us - s_lq_eval_deadline_us) < 0) {
        return;
    }
    s_lq_eval_deadline_us = now_us + RFM_LQ_EVAL_PERIOD_US;
    fail_score = (uint16_t)(s_lq_rx_fail + (uint16_t)(s_lq_tx_fail * 2u));

    if (fail_score > (uint16_t)(s_lq_rx_ok + 8u)) {
        if (s_tx_power_level < RFM_TX_PWR_MAX) {
            apply_tx_power((uint8_t)(s_tx_power_level + 1u));
        }
        set_event(RFM_EVENT_LINK_QUALITY_WARN);
    } else if ((fail_score == 0u) && (s_lq_rx_ok > 20u) && (s_tx_power_level > RFM_TX_PWR_MIN)) {
        apply_tx_power((uint8_t)(s_tx_power_level - 1u));
    }

    s_lq_rx_ok = 0u;
    s_lq_rx_fail = 0u;
    s_lq_tx_fail = 0u;
}

void rfm_link_init(void)
{
    memset(&s_bond, 0, sizeof(s_bond));
    memset(s_latest_input, 0, sizeof(s_latest_input));

    s_state = RFM_STATE_IDLE;
    s_event = RFM_EVENT_NONE;
    s_connected = false;
    s_has_bond = false;
    s_tx_seq = 0u;
    s_last_rx_seq = 0u;
    s_scan_cursor = 0u;
    s_tx_power_level = RFM_TX_PWR_DEFAULT;
    s_reject_count = 0u;
    s_input_valid = false;
    s_rate_hz = RFM_RATE_1K;

    s_last_rx_us = 0u;
    s_next_adv_us = 0u;
    s_mode_deadline_us = 0u;
    s_pair_ok_deadline_us = 0u;
    s_next_scan_us = 0u;
    s_next_report_us = 0u;

    s_lq_rx_ok = 0u;
    s_lq_rx_fail = 0u;
    s_lq_tx_fail = 0u;
    s_lq_eval_deadline_us = platform_now_us() + RFM_LQ_EVAL_PERIOD_US;

    rf_hw_enable_link_guard(1u, 1u, 1u);
    apply_tx_power(RFM_TX_PWR_DEFAULT);
    apply_channel(0u);

    if (bond_store_load()) {
        change_state(RFM_STATE_CONNECTING);
        s_mode_deadline_us = platform_now_us() + RFM_CONNECT_TIMEOUT_US;
        s_next_scan_us = platform_now_us();
    } else {
        /* Avoid first-pair deadlock when host has not issued START_PAIR yet. */
        s_mode_deadline_us = platform_now_us() + RFM_PAIR_TIMEOUT_US;
        s_next_adv_us = platform_now_us();
        change_state(RFM_STATE_PAIRING);
    }
}

void rfm_link_start_pairing(void)
{
    s_connected = false;
    s_mode_deadline_us = platform_now_us() + RFM_PAIR_TIMEOUT_US;
    s_next_adv_us = platform_now_us();
    change_state(RFM_STATE_PAIRING);
}

void rfm_link_stop_pairing(void)
{
    if (s_state == RFM_STATE_PAIRING) {
        change_state(s_has_bond ? RFM_STATE_CONNECTING : RFM_STATE_IDLE);
    }
}

void rfm_link_unbind(void)
{
    s_connected = false;
    s_has_bond = false;
    memset(&s_bond, 0, sizeof(s_bond));
    bond_store_clear();
    change_state(RFM_STATE_IDLE);
    set_event(RFM_EVENT_UNBOUND);
}

bool rfm_link_set_report_rate(rfm_report_rate_t rate)
{
    if ((rate != RFM_RATE_1K) &&
        (rate != RFM_RATE_2K) &&
        (rate != RFM_RATE_4K) &&
        (rate != RFM_RATE_8K)) {
        return false;
    }
    s_rate_hz = rate;
    set_event(RFM_EVENT_RATE_APPLIED);
    return true;
}

bool rfm_link_push_input(const uint8_t *payload, size_t len)
{
    if ((payload == 0) || (len != RFM_RF_INPUT_PAYLOAD_LEN)) {
        return false;
    }
    if (!s_connected) {
        return false;
    }

    memcpy(s_latest_input, payload, RFM_RF_INPUT_PAYLOAD_LEN);
    s_input_valid = true;
    return true;
}

rfm_status_t rfm_link_get_status(void)
{
    rfm_status_t st;
    st.state = s_state;
    st.rate_hz = s_rate_hz;
    st.tx_power_level = s_tx_power_level;
    st.rx_ok = s_lq_rx_ok;
    st.rx_fail = s_lq_rx_fail;
    st.tx_fail = s_lq_tx_fail;
    st.reject_count = s_reject_count;
    st.has_bond = s_has_bond;
    st.connected = s_connected;
    return st;
}

rfm_event_t rfm_link_take_event(void)
{
    rfm_event_t ev = s_event;
    s_event = RFM_EVENT_NONE;
    return ev;
}

void rfm_link_poll(void)
{
    uint32_t now_us = platform_now_us();
    uint8_t raw[RFM_PROTO_MAX_FRAME];
    rfm_proto_frame_t pkt;
    size_t raw_len;

    while (1) {
        raw_len = sizeof(raw);
        if (!rf_hw_read_frame(raw, &raw_len)) {
            break;
        }
        if (!rfm_protocol_decode(raw, raw_len, &pkt)) {
            s_lq_rx_fail++;
            continue;
        }

        s_last_rx_seq = pkt.hdr.seq;
        if (s_state == RFM_STATE_PAIRING) {
            on_adv_rsp(&pkt, now_us);
        } else if ((s_state == RFM_STATE_CONNECTING) || (s_state == RFM_STATE_RECONNECTING)) {
            on_conn_req(&pkt, now_us);
        } else if (s_state == RFM_STATE_CONNECTED) {
            on_connected_downlink(&pkt, now_us);
        }
    }

    if (s_state == RFM_STATE_PAIRING) {
        if ((int32_t)(now_us - s_mode_deadline_us) >= 0) {
            change_state(RFM_STATE_IDLE);
            set_event(RFM_EVENT_ERROR);
        } else if ((int32_t)(now_us - s_next_adv_us) >= 0) {
            uint8_t req[4];
            write_u32_le(req, RFM_DEVICE_UID);
            (void)send_packet_retry(RFM_PKT_ADV_REQ, req, sizeof(req), 0u, 1u);
            s_next_adv_us = now_us + RFM_ADV_INTERVAL_US;
        }
    }

    if (s_state == RFM_STATE_PAIR_OK) {
        if ((int32_t)(now_us - s_pair_ok_deadline_us) >= 0) {
            change_state(RFM_STATE_CONNECTING);
            s_mode_deadline_us = now_us + RFM_CONNECT_TIMEOUT_US;
            s_next_scan_us = now_us;
            s_scan_cursor = s_bond.hop_seed;
        }
    }

    if ((s_state == RFM_STATE_CONNECTING) || (s_state == RFM_STATE_RECONNECTING)) {
        if ((int32_t)(now_us - s_next_scan_us) >= 0) {
            apply_channel(s_scan_cursor);
            s_scan_cursor = (uint8_t)((s_scan_cursor + 1u) % RFM_CHANNEL_TABLE_SIZE);
            s_next_scan_us = now_us + RFM_SCAN_STEP_US;
        }
        if ((s_state == RFM_STATE_CONNECTING) && ((int32_t)(now_us - s_mode_deadline_us) >= 0)) {
            change_state(RFM_STATE_RECONNECTING);
        }
    }

    if (s_state == RFM_STATE_CONNECTED) {
        if ((int32_t)(now_us - s_last_rx_us) > (int32_t)RFM_LINK_LOST_TIMEOUT_US) {
            s_connected = false;
            change_state(RFM_STATE_RECONNECTING);
            set_event(RFM_EVENT_LINK_LOST);
            s_next_scan_us = now_us;
            s_scan_cursor = s_bond.hop_seed;
        } else if (s_input_valid && ((int32_t)(now_us - s_next_report_us) >= 0)) {
            if (send_packet_retry(RFM_PKT_INPUT_DATA, s_latest_input, RFM_RF_INPUT_PAYLOAD_LEN, RFM_HOP_AUTO, RFM_INPUT_RETRY)) {
                s_next_report_us = now_us + report_period_us();
            } else {
                s_lq_tx_fail++;
                s_next_report_us = now_us + report_period_us();
            }
        }
    }

    eval_link_quality(now_us);
}
