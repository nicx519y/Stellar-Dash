# RF24G Self-Managed Hopping Protocol

Date: 2026-05-17

This document defines the preferred RF24G direction for `RF_PHY_Hop`: a self-managed protocol built on the lower-level RFIP Basic path. The design keeps timing, packet format, hopping, recovery, and diagnostics under project control.

## Goals

- Keep 8 kHz input latency as the normal target.
- Avoid blocking retransmission for input packets.
- Recover from interference without user-visible disconnects.
- Make every link decision measurable through counters.
- Keep the first implementation small enough to debug on CH585.

## Roles

- RX dongle is the coordinator.
- TX module follows the coordinator's channel plan.
- TX input packets are latest-state packets. A lost old packet is not retransmitted if a newer state is available.

The protocol has three phases:

1. Pair/Search: find the peer and exchange session parameters.
2. Data: send input packets using a deterministic hop schedule.
3. Recovery: re-acquire after missed packets or channel drift.

## RF Packet Format

All multi-byte fields are little-endian.

```c
typedef struct __attribute__((packed)) {
    uint8_t magic;       /* 0xA7 */
    uint8_t type;        /* DATA, BEACON, SYNC, CTRL, ACK */
    uint8_t session_id;  /* Assigned during pair/search */
    uint8_t seq;         /* Increments per DATA packet */
    uint8_t hop_epoch;   /* Increments when hop table changes */
    uint8_t flags;
    uint8_t payload[4];  /* Initial Hitbox bitmask payload */
    uint16_t crc16;      /* CRC over all previous bytes */
} rf24g_packet_t;
```

Initial DATA payload is 4 bytes because the current verified RF path runs 4B at 8 kHz. If full analog input is needed later, define a second DATA type for 15B payload and re-measure the 8 kHz margin.

## Channel Table

Use a small project-owned table first:

```text
{ 4, 8, 12, 16, 20, 24, 28, 32, 36 }
```

Avoid channels known to overlap the noisiest local Wi-Fi regions after measurement. The exact table should become configurable later.

## Hop Schedule

Start with slow deterministic hopping:

```text
channel_index = (session_seed + (seq / HOP_DWELL_PACKETS)) % hop_count
```

Recommended first value:

```text
HOP_DWELL_PACKETS = 16
```

At 8 kHz this changes channel every 2 ms. That is fast enough to escape narrow interference, but slow enough to keep RX re-arm and debug manageable.

Per-packet hopping is deliberately not the first target. It makes timing and re-acquisition harder before the basic protocol is proven.

## Pair/Search Phase

Use a fixed small search channel set:

```text
{ 16, 24, 32 }
```

TX behavior:

- Periodically sends BEACON on each search channel.
- BEACON contains device id, protocol version, supported rates, and a random nonce.
- Uses a low beacon rate, for example 100-250 Hz per active search pass.

RX behavior:

- Scans search channels.
- On valid BEACON, replies with SYNC.
- SYNC contains `session_id`, `session_seed`, initial `seq_base`, hop table id, and requested report rate.

After TX receives SYNC, both sides enter Data phase.

## Data Phase

TX sends DATA packets at the selected rate:

```text
8K: every 125 us
4K: every 250 us
2K: every 500 us
1K: every 1 ms
```

RX computes the expected channel from the received `seq`, `session_seed`, and `hop_epoch`. On each good packet:

- Validate magic, session, type, CRC.
- Detect missing packets from `seq` delta.
- Update latest input state.
- Re-arm RX on the next expected channel.

Input DATA packets are not individually acknowledged.

## Recovery Phase

RX keeps a short miss counter:

- `miss <= 2`: stay on expected schedule.
- `miss 3..8`: scan nearby schedule positions on current/next channel.
- `miss > 8`: enter fast reacquire.

Fast reacquire scans:

```text
for offset in 0..hop_count-1:
    try channel for expected seq window
```

If no packet is found within 20-50 ms, RX returns to Pair/Search. TX also returns to Pair/Search if it has not received any SYNC/CTRL heartbeat within the configured link timeout.

## Link Quality Counters

Both sides should expose counters every 1 or 5 seconds:

- `tx_attempt`
- `tx_ok`
- `tx_busy`
- `rx_ok`
- `rx_crc`
- `rx_bad_session`
- `rx_seq_gap`
- `rx_dup`
- `rx_miss_max`
- `hop_epoch`
- `current_channel`
- `reacquire_count`
- `rate`

RX can also keep per-channel buckets:

```c
typedef struct {
    uint32_t rx_ok;
    uint32_t rx_crc;
    uint32_t miss;
    int8_t last_rssi;
} rf_channel_stat_t;
```

## Adaptive Channel Mask

After the fixed hop table works, add a channel mask.

RX marks a channel bad when:

- CRC failures exceed a threshold in a rolling window, or
- consecutive misses happen while adjacent channels remain healthy.

RX sends CTRL packets at a low rate with:

- new `hop_epoch`
- channel mask
- requested rate

TX applies the update only when CRC is valid and `hop_epoch` is newer.

## Adaptive Rate

Recommended policy:

- Drop rate quickly: if loss is bad for 50 ms, go 8K -> 4K -> 2K.
- Raise rate slowly: require 500 ms to 2 s of clean link before stepping up.
- Never oscillate directly between 8K and 1K.

The user-facing mode can still show "8K target"; the RF link may temporarily downshift to preserve stability.

## Implementation Order

1. Add `seq` and CRC to the current fixed-channel 4B packet.
2. Decode RX payload and count sequence gaps.
3. Add a fixed hop table with `HOP_DWELL_PACKETS = 16`.
4. Add RX fast reacquire.
5. Add per-channel quality counters.
6. Add channel mask updates.
7. Add adaptive rate control.

Each step should be tested before adding the next one.
