#ifndef XORA_WEBHID_BENCHMARK_H
#define XORA_WEBHID_BENCHMARK_H
#include <stdint.h>
/* Development-only SRAM evidence. No key, nonce or application content. */
#define WEBHID_BENCHMARK_MAGIC 0x32424858u
#define WEBHID_BENCHMARK_REPORT 0x32u
#define WEBHID_BENCHMARK_DATA_BYTES 988u
typedef struct {
    uint32_t magic, version, size, consistency;
    uint32_t run_id, state, direction, seed; /* state: 1 running, 2 awaiting receiver, 3 verified, 4 stopped/error */
    uint32_t bytes, reports, crc32, errors;
    uint32_t started_ms, elapsed_ms, duration_ms, spi_hz;
    uint32_t report_bytes, window, rx_peak, decrypt_us;
    uint32_t process_us, dma_us, dma_blocks, dma_errors;
    uint32_t tx_usb_speed, tx_queue_peak, tx_backpressure, tx_spi_blocks;
    uint32_t tx_crc_errors, tx_protocol_errors, tx_duplicates, connection_epoch;
    char build_id[32];
} webhid_benchmark_snapshot_t;
typedef char webhid_benchmark_snapshot_size[(sizeof(webhid_benchmark_snapshot_t)==160u)?1:-1];
static inline uint32_t webhid_benchmark_next(uint32_t value) {
    return value * 1664525u + 1013904223u;
}
#endif
