#ifndef RF_BINDING_STORE_H
#define RF_BINDING_STORE_H
#include "rf_hop_bond_journal.h"
#include "../../../common/rf_binding_protocol.h"

static inline uint8_t rfb_is_web(const rfh_bond_record_t *b) {
    return b->version==RFH_BOND_WEB_VERSION && b->reserved==RFB_VERSION;
}
static inline void rfb_snapshot(uint8_t *out,const rfh_bond_journal_state_t *s,
                                uint32_t local,uint8_t pending) {
    const rfh_bond_record_t *b=pending?&s->pending:&s->active;
    uint8_t present=pending?s->has_pending:s->has_active;
    rfb_put(out+12,local);rfb_put(out+32,s->max_generation);
    out[9]=(s->has_pending?RFB_PENDING:0u);
    if(present) {
        out[9]|=RFB_PRESENT|(rfb_is_web(b)?RFB_WEB:0u);
        rfb_put(out+16,b->peer_id_hash);rfb_put(out+20,b->link_access_address);
        rfb_put(out+24,b->pair_counter);rfb_put(out+28,b->bond_confirm32);
    }
#if RFH_TEST_FIXED_BOND_ENABLE
    out[9]|=RFB_FIXED;
#endif
}

/* Backend calls occur only in the owning main loop, never in USB/RF ISR.
 * expectedRevision protects against another browser's stale transaction.
 * Repeating an already persisted operation is safe, including lost replies. */
static inline void rfb_store_request(const rfh_bond_journal_backend_t *io,
                                    uint32_t local,uint8_t receiver,
                                    const uint8_t *req,uint8_t *out) {
    rfh_bond_journal_state_t s;rfh_bond_record_t b;
    uint8_t op=req[5],status=RFB_OK,pending=(op==RFB_GET_PENDING||op==RFB_PREPARE);
    uint32_t txn=rfb_u32(req+8),expected=rfb_u32(req+12),peer=rfb_u32(req+16);
    uint32_t address=rfb_u32(req+20),generation=rfb_u32(req+24),journal_generation;
    memset(&s,0,sizeof(s));rfb_response_init(out,req,RFB_OK);
    if(!rfb_request_valid(req)||!local){status=RFB_INVALID;goto done;}
    if(!rfh_bond_journal_load(io,local,&s)){status=RFB_STORAGE;goto done;}
    if(op<=RFB_GET_PENDING)goto done;
#if RFH_TEST_FIXED_BOND_ENABLE
    status=RFB_UNSUPPORTED;goto done;
#endif
    if(!txn){status=RFB_INVALID;goto done;}
    if(op==RFB_PREPARE) {
        if(!peer){status=RFB_INVALID;goto done;}
        if(s.has_pending) {
            b=s.pending;
            status=(rfb_is_web(&b)&&b.bond_confirm32==txn&&b.peer_id_hash==peer&&
                (receiver||(b.link_access_address==address&&b.pair_counter==generation)))?RFB_OK:RFB_CONFLICT;
            goto done;
        }
        if(s.has_active&&rfb_is_web(&s.active)&&s.active.bond_confirm32==txn){status=RFB_CONFLICT;goto done;}
        if(expected!=s.max_generation){status=RFB_CONFLICT;goto done;}
        if(s.max_generation==UINT32_MAX){status=RFB_EXHAUSTED;goto done;}
        journal_generation=s.max_generation+1u;
        if(receiver) {
            /* A bijection of the durable journal generation. Skip invalid AAs
             * by advancing the SAME journal generation; never wrap/reuse it.
             * Only committed addresses can have been used on air. */
            uint32_t seed=rfh_fnv1a32_mix_u32(local,0x52464231UL);unsigned attempts=0;
            do {
                generation=journal_generation;
                address=seed+generation*0x9E3779B1UL;
                if(rfh_access_address_valid(address)&&address!=RFH_TEST_FIXED_ACCESS_ADDRESS&&
                   (!s.has_active||address!=s.active.link_access_address))break;
                if(journal_generation==UINT32_MAX||++attempts>=4096u){status=RFB_EXHAUSTED;goto done;}
                journal_generation++;
            } while(1);
        } else if(!generation||!rfh_access_address_valid(address)||address==RFH_TEST_FIXED_ACCESS_ADDRESS) {
            status=RFB_INVALID;goto done;
        }
        rfh_bond_record_init(&b,address,RFH_DISCOVERY_CHANNEL_A,RFH_DISCOVERY_CHANNEL_B,
                            RFH_RATE_1K,local,peer,generation,txn);
        b.version=RFH_BOND_WEB_VERSION;b.reserved=RFB_VERSION;b.checksum=rfh_bond_checksum(&b);
        if(!rfh_bond_journal_write_entry(io,rfh_bond_journal_choose_target(&s),
            RFH_BOND_JOURNAL_KIND_BOND,journal_generation,&b,RFH_BOND_MARKER_PREPARED))status=RFB_STORAGE;
    } else if(op==RFB_COMMIT) {
        if(s.has_active&&rfb_is_web(&s.active)&&s.active.bond_confirm32==txn)goto done;
        if(expected!=s.max_generation||!s.has_pending||!rfb_is_web(&s.pending)||s.pending.bond_confirm32!=txn) {
            status=RFB_CONFLICT;goto done;
        }
        if(!rfh_bond_journal_commit_pending(io,&s,local))status=RFB_STORAGE;
    } else {
        /* Never turn an already committed binding back into its predecessor. */
        if(s.has_active&&rfb_is_web(&s.active)&&s.active.bond_confirm32==txn){status=RFB_CONFLICT;goto done;}
        if(expected!=s.max_generation){status=RFB_CONFLICT;goto done;}
        if(!s.has_pending)goto done;
        if(!rfb_is_web(&s.pending)||s.pending.bond_confirm32!=txn){status=RFB_CONFLICT;goto done;}
        if(!rfh_bond_journal_abort_pending(io,&s,local))status=RFB_STORAGE;
    }
    if(!rfh_bond_journal_load(io,local,&s))status=RFB_STORAGE;
done:
    out[8]=status;rfb_snapshot(out,&s,local,pending);rfb_response_finish(out);
}
#endif
