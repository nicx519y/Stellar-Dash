"""Behavior tests compile the actual bounded state components, without hardware."""
import unittest
from . import test_live_config_feedback as feedback
from .test_live_config_feedback import ROOT, function

PREAMBLE = r'''
#include <cassert>
#include <cstring>
#include <vector>
#include "webhid_logical_assembler.hpp"
#include "webhid_telemetry_state.hpp"
#include "device_security_crypto.h"
extern "C" void HBoxCrypto_Zeroize(void *data, size_t length) { std::memset(data, 0, length); }
'''

class WebHidStateComponentsTests(unittest.TestCase):
    def native(self, body):
        production = ''.join((ROOT / ('application/Src/webconfig/' + name)).read_text()
            for name in ['webhid_logical_assembler.cpp', 'webhid_telemetry_state.cpp'])
        feedback.LiveConfigFeedbackTests.native(self, PREAMBLE + production + body,
            {n: (ROOT / "common" / n).read_text() for n in ["webhid_protocol.h", "device_security_crypto.h"]})

    def test_fragment_boundary_oversize_discard_and_secure_reset(self):
        self.native(r'''
int main() {
    WebHidLogicalAssembler a;
    using S = WebHidLogicalAssembler::Status;
    constexpr auto F = WEBHID_REPORT_FLAG_FRAGMENTED;
    constexpr auto L = WEBHID_REPORT_FLAG_LAST;
    const uint8_t json[] = "{\"transactionId\":42}";
    assert(a.accept(1,false,0,json,1).status == S::Invalid);
    assert(a.accept(1,false,F,json,5).status == S::Pending);
    assert(a.accept(2,false,F|L,json+5,sizeof(json)-6).status == S::Invalid);
    assert(a.accept(1,true,F|L,json+5,sizeof(json)-6).status == S::Invalid);
    assert(a.accept(1,false,F|L,json+5,sizeof(json)-6).status == S::Complete);
    assert(a.size() == sizeof(json)-1 && std::memcmp(a.data(),json,a.size())==0);
    a.reset(); assert(a.size()==0 && a.data()[0]==0);
    std::vector<uint8_t> payload(a.Capacity,' ');
    std::memcpy(payload.data(),json,sizeof(json)-1);
    assert(a.accept(1,true,F,payload.data(),payload.size()).status == S::Pending);
    assert(a.accept(1,true,F|L,json,0).status == S::Complete); // Exact capacity.
    a.reset();
    assert(a.accept(1,true,F,payload.data(),payload.size()).status == S::Pending);
    assert(a.accept(1,true,F,json,1).status == S::Pending); // Discard until LAST.
    const auto result=a.accept(1,true,F|L,json,1);
    assert(result.status==S::Oversize && result.secure && result.transactionId==42);
    assert(a.size()==0);for(size_t i=0;i<=a.Capacity;++i) assert(a.data()[i]==0);
    assert(a.accept(1,false,L,json,sizeof(json)-1).status==S::Complete);
    a.reset();assert(a.data()[0]==0);
}
''')

    def test_queues_preserve_pinned_head_and_report_overflow(self):
        self.native(r'''
int main() {
    WebHidTelemetryState t;
    for(unsigned i=0;i<32;++i)t.enqueueButton(i,22,true,false);
    const auto first=*t.frontButton();
    t.enqueueButton(99,22,true,true);
    assert(t.frontButton()->event_sequence_le==first.event_sequence_le);
    for(unsigned i=0;i<31;++i)t.commitButton();
    assert(t.frontButton()->trigger_mask_le==99 && t.frontButton()->total_dropped_snapshots_le==1);
    t.commitButton();assert(!t.hasButtons());
    for(unsigned i=0;i<33;++i)t.enqueueButton(i,22,true,false);
    assert(t.frontButton()->trigger_mask_le==1); // Unpinned drops oldest.
    t.start(10);
    for(unsigned i=0;i<64;++i){assert(t.admitEdge(false));t.enqueueAdmittedEdge({});}
    const auto seq=t.frontEdge()->edgeSequence;
    assert(!t.admitEdge(true));assert(t.frontEdge()->edgeSequence==seq && t.sampleDrops()==1);
    assert(t.admitEdge(false));t.enqueueAdmittedEdge({});assert(t.frontEdge()->edgeSequence==seq+1 && t.sampleDrops()==2);
    t.reset();assert(!t.enabled() && !t.hasEdges() && !t.hasButtons() && !t.hasSample());
}
''')

    def test_sample_pinning_checkpoint_pages_and_session_lifecycle(self):
        self.native(r'''
int main() {
    WebHidTelemetryState t;t.start(10);
    assert(t.schedule(10,100,10,false));assert(t.hasSample() && t.sampleTimestamp()==100);
    t.schedule(50,500,10,true);assert(t.sampleTimestamp()==100 && t.sampleDrops()==0);
    t.schedule(50,500,10,false);assert(t.sampleTimestamp()==500 && t.sampleDrops()==4);
    t.beginCheckpoint(500,1234);
    for(unsigned i=0;i<WEBHID_PERF_KEY_COUNT;++i){webhid_perf_checkpoint_key_v1_t k={};k.raw_adc_le=i;t.setCheckpointKey(i,k);}
    t.finishCheckpoint();webhid_perf_checkpoint_v1_t p={},retry={};
    unsigned pages=0;
    while(t.checkpointReport(p)) {
        assert(t.checkpointReport(retry) && std::memcmp(&p,&retry,sizeof(p))==0);
        assert(p.chunk_index==pages && p.device_timestamp_us_le==500 && p.max_travel_distance_um_le==1234);
        assert(p.total_dropped_samples_le==4 && p.keys[0].raw_adc_le==p.first_button);
        t.commitCheckpoint();++pages;
    }
    assert(pages==WEBHID_PERF_KEY_COUNT/WEBHID_PERF_CHECKPOINT_KEYS);
    t.commitSample();assert(!t.hasSample() && t.sampleDrops()==0);
    t.stop();assert(!t.enabled() && !t.hasCheckpoint());
    t.reset();t.start(100);assert(t.admitEdge(false));t.enqueueAdmittedEdge({});assert(t.frontEdge()->edgeSequence==1);
}
''')

    def test_rejected_edge_does_not_construct_adc_snapshot(self):
        self.native(r'''
struct ADCBtn {
    uint16_t currentValue=100, pressTriggerSnapshot=200, pressStartSnapshot=300;
    uint16_t releaseTriggerSnapshot=400, releaseStartSnapshot=500;
};
static unsigned clockReads=0, distanceReads=0;
uint16_t distanceMicrometres(ADCBtn *, uint16_t value) { ++distanceReads; return value; }
struct Worker { ADCBtn button; ADCBtn *getButtonState(uint8_t) { return &button; } } ADC_BTNS_WORKER;
enum class OutboundFrameSource { None, Edge };
struct WebHidService {
    bool initialized=true, sessionEstablished=true;
    WebHidTelemetryState telemetry;
    OutboundFrameSource pendingFrameSource=OutboundFrameSource::None;
    uint32_t monotonicMicros() { ++clockReads; return 123; }
    void onAdcButtonTransition(uint8_t, bool);
};
''' + function('Src/webconfig/webhid_service.cpp', 'void WebHidService::onAdcButtonTransition(') + r'''
int main() {
    WebHidService service;service.telemetry.start(10);
    for(unsigned i=0;i<64;++i)service.onAdcButtonTransition(0,true);
    const auto first=*service.telemetry.frontEdge();
    service.pendingFrameSource=OutboundFrameSource::Edge;
    clockReads=distanceReads=0;
    service.onAdcButtonTransition(0,false);
    assert(clockReads==0 && distanceReads==0);
    assert(service.telemetry.frontEdge()->edgeSequence==first.edgeSequence);
    assert(service.telemetry.sampleDrops()==1);
    service.pendingFrameSource=OutboundFrameSource::None;
    service.onAdcButtonTransition(0,false);
    assert(clockReads==1 && distanceReads==5);
    assert(service.telemetry.frontEdge()->edgeSequence==first.edgeSequence+1);
    assert(service.telemetry.sampleDrops()==2);
}
''')
