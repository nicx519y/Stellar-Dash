#ifndef RF_HOP_BOND_H
#define RF_HOP_BOND_H

#include <stdint.h>
#include <string.h>

#include "rf_hop_protocol.h"

#define RFH_BOND_MAGIC                  0x48424652UL
#define RFH_BOND_VERSION                2u
#define RFH_BOND_EEPROM_ADDR_DEFAULT    0x6000u
#define RFH_BOND_EEPROM_ERASE_SIZE      4096u

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t link_access_address;
    uint8_t channel_a;
    uint8_t channel_b;
    uint8_t rate_code;
    uint8_t reserved;
    uint32_t local_id_hash;
    uint32_t peer_id_hash;
    uint32_t pair_counter;
    uint32_t bond_confirm32;
    uint32_t checksum;
} rfh_bond_record_t;

static inline uint32_t rfh_bond_checksum(const rfh_bond_record_t *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    uint32_t hash = 2166136261UL;
    uint32_t i;

    for(i = 0u; i < (uint32_t)(sizeof(rfh_bond_record_t) - sizeof(uint32_t)); ++i)
    {
        hash ^= bytes[i];
        hash *= 16777619UL;
    }
    return hash;
}

static inline uint8_t rfh_bond_record_valid(const rfh_bond_record_t *record)
{
    if(record == 0)
    {
        return 0u;
    }
    if((record->magic != RFH_BOND_MAGIC) ||
       (record->version != RFH_BOND_VERSION) ||
       (record->length != sizeof(rfh_bond_record_t)) ||
       (rfh_access_address_valid(record->link_access_address) == 0u) ||
       (rfh_channel_valid(record->channel_a) == 0u) ||
       (rfh_channel_valid(record->channel_b) == 0u) ||
       (record->channel_a == record->channel_b) ||
       (record->rate_code > RFH_RATE_8K))
    {
        return 0u;
    }
    return (record->checksum == rfh_bond_checksum(record)) ? 1u : 0u;
}

static inline void rfh_bond_record_init(rfh_bond_record_t *record,
                                        uint32_t link_access_address,
                                        uint8_t channel_a,
                                        uint8_t channel_b,
                                        uint8_t rate_code,
                                        uint32_t local_id_hash,
                                        uint32_t peer_id_hash,
                                        uint32_t pair_counter,
                                        uint32_t bond_confirm32)
{
    if(record == 0)
    {
        return;
    }

    memset(record, 0, sizeof(*record));
    record->magic = RFH_BOND_MAGIC;
    record->version = RFH_BOND_VERSION;
    record->length = sizeof(rfh_bond_record_t);
    record->link_access_address = link_access_address;
    record->channel_a = channel_a;
    record->channel_b = channel_b;
    record->rate_code = rate_code;
    record->local_id_hash = local_id_hash;
    record->peer_id_hash = peer_id_hash;
    record->pair_counter = pair_counter;
    record->bond_confirm32 = bond_confirm32;
    record->checksum = rfh_bond_checksum(record);
}

#endif
