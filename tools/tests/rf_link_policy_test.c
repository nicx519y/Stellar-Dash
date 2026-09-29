#include <assert.h>
#include <stdio.h>
#include "rf_link_policy.h"

int main(void)
{
    unsigned cycle;
    for(cycle = 0; cycle < 100; ++cycle) {
        rfh_health_t h;
        uint32_t start = cycle & 1 ? 0xfffffff0u : cycle * 1000u;
        rfh_health_start(&h, start);
        /* Repeated hardware failures in one logical window are not 3 misses. */
        rfh_health_failed(&h, start + 1, 10);
        rfh_health_failed(&h, start + 2, 10);
        assert(!rfh_health_retry(&h, start + 10));
        assert(rfh_health_retry(&h, start + 11));
        rfh_health_failed(&h, start + 12, 10);
        assert(rfh_health_retry(&h, start + 22));
        rfh_health_failed(&h, start + 23, 10);
        assert(!rfh_health_retry(&h, start + 33));
        assert(!rfh_health_poll(&h, start + 99, 100, 500));
        assert(h.misses == 0);
        assert(!rfh_health_poll(&h, start + 100, 100, 500));
        assert(h.misses == 1);
        assert(!rfh_health_poll(&h, start + 200, 100, 500));
        assert(rfh_health_poll(&h, start + 300, 100, 500));
        /* No callback whatsoever still has a finite escape. */
        rfh_health_start(&h, start);
        assert(rfh_health_poll(&h, start + 500, 100, 500));
        rfh_health_start(&h, start);
        rfh_health_ack(&h, start + 80);
        assert(!rfh_health_poll(&h, start + 100, 100, 500));
        assert(h.misses == 0);
        rfh_health_failed(&h, start + 105, 10);
        rfh_health_ack(&h, start + 110);
        assert(!rfh_health_retry(&h, start + 115));
    }
    {
        rfh_sequence_t s = {0};
        assert(rfh_sequence_accept(&s, 254, 0, 16) == 1);
        assert(rfh_sequence_accept(&s, 255, 1, 16) == 1);
        assert(rfh_sequence_accept(&s, 1, 2, 16) == 2); /* one missing, even if CRC fires */
        assert(rfh_sequence_accept(&s, 1, 3, 16) == 0);
        assert(rfh_sequence_accept(&s, 255, 4, 16) == 0);
        assert(rfh_sequence_accept(&s, 140, 20, 16) == 1); /* ambiguous wrap rebases */
    }
    {
        rfh_hop_transaction_t h = {0};
        assert(rfh_hop_prepare(&h, 255, 16, 22, 100, 200) == 1);
        assert(rfh_hop_prepare(&h, 255, 16, 22, 199, 200) == 2);
        assert(h.deadline == 300); /* repeats cannot extend the deadline */
        assert(rfh_hop_prepare(&h, 255, 39, 22, 200, 200) == 0);
        assert(!rfh_hop_confirm(&h, 254, 22, 22));
        assert(!rfh_hop_confirm(&h, 255, 22, 16));
        assert(rfh_hop_confirm(&h, 255, 22, 22));
        assert(rfh_hop_prepare(&h, 1, 22, 16, 400, 200) == 1);
        assert(!rfh_hop_prepare(&h, 255, 16, 22, 450, 200));
    }
    assert(rfh_ack_delay(100, 200, 1000, 280) == 180);
    assert(rfh_ack_delay(100, 380, 1000, 280) == 0);
    assert(rfh_ack_delay(990, 10, 1000, 30) == 10);
    assert(rfh_ack_delay(990, 21, 1000, 30) == 0);
    puts("rf link policy: 100 recovery cycles, wrap, retry, hop and ACK deadlines passed");
    return 0;
}
