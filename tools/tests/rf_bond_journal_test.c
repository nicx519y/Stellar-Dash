#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rf_hop_bond_journal.h"

#define FLASH_SIZE 0x8000u

static uint8_t flash_bytes[FLASH_SIZE];
static int fail_at = -1;
static int operation_index = 0;

static uint8_t should_fail(void)
{
    const int current = operation_index++;
    return (fail_at >= 0 && current == fail_at) ? 1u : 0u;
}

static uint8_t flash_read(uint32_t address, void *data, uint32_t length)
{
    if(should_fail() != 0u || address + length > FLASH_SIZE)
    {
        return 1u;
    }
    memcpy(data, &flash_bytes[address], length);
    return 0u;
}

static uint8_t flash_write(uint32_t address, const void *data, uint32_t length)
{
    const uint8_t *source = (const uint8_t *)data;
    uint32_t i;

    if(should_fail() != 0u || address + length > FLASH_SIZE)
    {
        return 1u;
    }
    for(i = 0u; i < length; ++i)
    {
        if((uint8_t)(flash_bytes[address + i] | source[i]) !=
           flash_bytes[address + i])
        {
            return 1u;
        }
        flash_bytes[address + i] &= source[i];
    }
    return 0u;
}

static uint8_t flash_erase(uint32_t address, uint32_t length)
{
    if(should_fail() != 0u || address + length > FLASH_SIZE)
    {
        return 1u;
    }
    memset(&flash_bytes[address], 0xFF, length);
    return 0u;
}

static const rfh_bond_journal_backend_t backend = {
    flash_read,
    flash_write,
    flash_erase
};

static rfh_bond_record_t make_bond(uint32_t local, uint32_t peer,
                                   uint32_t access_address, uint32_t counter)
{
    rfh_bond_record_t bond;
    if(rfh_access_address_valid(access_address) == 0u)
    {
        access_address = rfh_access_address_from_seed(access_address ^ peer);
    }
    rfh_bond_record_init(&bond, access_address, 10u, 39u, RFH_RATE_1K,
                         local, peer, counter, access_address ^ peer);
    return bond;
}

static void reset_faults(void)
{
    fail_at = -1;
    operation_index = 0;
}

static void test_legacy_migration(void)
{
    const uint32_t local = 0x11112222u;
    rfh_bond_record_t legacy = make_bond(local, 1u, 0x6D35B8C9u, 1u);
    rfh_bond_record_t candidate = make_bond(local, 2u, 0x71764129u, 2u);
    rfh_bond_journal_state_t state;

    memset(flash_bytes, 0xFF, sizeof(flash_bytes));
    assert(rfh_bond_record_valid(&legacy) != 0u);
    assert(rfh_bond_record_valid(&candidate) != 0u);
    memcpy(&flash_bytes[RFH_BOND_JOURNAL_BANK_A_ADDR], &legacy, sizeof(legacy));
    reset_faults();
    assert(rfh_bond_journal_load(&backend, local, &state) != 0u);
    assert(state.has_active != 0u && state.active_is_legacy != 0u);
    assert(rfh_bond_journal_prepare(&backend, &state, local, &candidate) != 0u);
    assert(state.has_pending != 0u && state.pending_bank == 1u);
    assert(memcmp(&flash_bytes[RFH_BOND_JOURNAL_BANK_A_ADDR],
                  &legacy, sizeof(legacy)) == 0);
    assert(rfh_bond_journal_commit_pending(&backend, &state, local) != 0u);
    assert(state.has_active != 0u && state.active.peer_id_hash == 2u);
}

static void seed_committed(uint32_t local, const rfh_bond_record_t *bond,
                           rfh_bond_journal_state_t *state)
{
    memset(flash_bytes, 0xFF, sizeof(flash_bytes));
    reset_faults();
    assert(rfh_bond_journal_load(&backend, local, state) != 0u);
    assert(rfh_bond_journal_prepare(&backend, state, local, bond) != 0u);
    assert(rfh_bond_journal_commit_pending(&backend, state, local) != 0u);
}

static void test_prepare_power_cuts(void)
{
    const uint32_t local = 0xAABBCCDDu;
    const rfh_bond_record_t old_bond = make_bond(local, 10u, 0x6D35B8C9u, 1u);
    const rfh_bond_record_t new_bond = make_bond(local, 20u, 0x71764129u, 2u);
    rfh_bond_journal_state_t state;
    uint8_t baseline[FLASH_SIZE];
    int cut;

    seed_committed(local, &old_bond, &state);
    memcpy(baseline, flash_bytes, sizeof(baseline));

    for(cut = 0; cut < 8; ++cut)
    {
        memcpy(flash_bytes, baseline, sizeof(flash_bytes));
        operation_index = 0;
        fail_at = cut;
        (void)rfh_bond_journal_prepare(&backend, &state, local, &new_bond);
        reset_faults();
        assert(rfh_bond_journal_load(&backend, local, &state) != 0u);
        assert(state.has_active != 0u);
        assert(state.active.peer_id_hash == old_bond.peer_id_hash);
        if(state.has_pending != 0u)
        {
            assert(state.pending.peer_id_hash == new_bond.peer_id_hash);
        }
    }
}

static void test_commit_and_abort_recovery(void)
{
    const uint32_t local = 0x13572468u;
    const rfh_bond_record_t old_bond = make_bond(local, 1u, 0x6D35B8C9u, 1u);
    const rfh_bond_record_t new_bond = make_bond(local, 2u, 0x71764129u, 2u);
    rfh_bond_journal_state_t state;
    uint8_t prepared[FLASH_SIZE];
    int cut;

    seed_committed(local, &old_bond, &state);
    assert(rfh_bond_journal_prepare(&backend, &state, local, &new_bond) != 0u);
    memcpy(prepared, flash_bytes, sizeof(prepared));

    for(cut = 0; cut < 3; ++cut)
    {
        memcpy(flash_bytes, prepared, sizeof(flash_bytes));
        operation_index = 0;
        fail_at = cut;
        (void)rfh_bond_journal_commit_pending(&backend, &state, local);
        reset_faults();
        assert(rfh_bond_journal_load(&backend, local, &state) != 0u);
        assert(state.has_active != 0u);
        assert(state.active.peer_id_hash == 1u || state.active.peer_id_hash == 2u);
        assert(!(state.active.peer_id_hash == 2u && state.has_pending != 0u));
    }

    memcpy(flash_bytes, prepared, sizeof(flash_bytes));
    reset_faults();
    assert(rfh_bond_journal_load(&backend, local, &state) != 0u);
    assert(rfh_bond_journal_abort_pending(&backend, &state, local) != 0u);
    assert(state.has_active != 0u && state.active.peer_id_hash == 1u);
    assert(state.has_pending == 0u);
}

static void test_atomic_unbind(void)
{
    const uint32_t local = 0x24681357u;
    const rfh_bond_record_t bond = make_bond(local, 7u, 0x71764129u, 1u);
    rfh_bond_journal_state_t state;
    uint8_t baseline[FLASH_SIZE];
    int cut;

    seed_committed(local, &bond, &state);
    memcpy(baseline, flash_bytes, sizeof(baseline));
    for(cut = 0; cut < 8; ++cut)
    {
        memcpy(flash_bytes, baseline, sizeof(flash_bytes));
        reset_faults();
        assert(rfh_bond_journal_load(&backend, local, &state) != 0u);
        operation_index = 0;
        fail_at = cut;
        (void)rfh_bond_journal_write_tombstone(&backend, &state, local);
        reset_faults();
        assert(rfh_bond_journal_load(&backend, local, &state) != 0u);
        assert((state.has_active != 0u) ||
               (state.active_is_tombstone != 0u));
        if(state.has_active != 0u)
        {
            assert(state.active.peer_id_hash == bond.peer_id_hash);
        }
        assert(state.has_pending == 0u);
    }

    memcpy(flash_bytes, baseline, sizeof(flash_bytes));
    reset_faults();
    assert(rfh_bond_journal_load(&backend, local, &state) != 0u);
    assert(rfh_bond_journal_write_tombstone(&backend, &state, local) != 0u);
    assert(state.has_active == 0u);
    assert(state.active_is_tombstone != 0u);

    reset_faults();
    assert(rfh_bond_journal_load(&backend, local, &state) != 0u);
    assert(state.has_active == 0u);
    assert(state.active_is_tombstone != 0u);
}

int main(void)
{
    test_legacy_migration();
    test_prepare_power_cuts();
    test_commit_and_abort_recovery();
    test_atomic_unbind();
    puts("rf bond journal tests passed");
    return 0;
}
