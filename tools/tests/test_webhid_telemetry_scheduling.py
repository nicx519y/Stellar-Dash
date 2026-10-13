import unittest

from . import test_live_config_feedback as feedback
from .test_live_config_feedback import function, ROOT


class WebHidTelemetrySchedulingTests(unittest.TestCase):
    def test_live_sample_interleaves_checkpoint_and_preserves_control_and_pinned_ownership(self):
        source = r'''
#include <cassert>
#include <cstdint>
#include <vector>
#include <cstring>
#include "webhid_telemetry_state.hpp"
#include "device_security_crypto.h"
extern "C" void HBoxCrypto_Zeroize(void *data, size_t length) { std::memset(data, 0, length); }
enum class OutboundFrameSource { None, Logical, Edge, ButtonState, Sample, Checkpoint, Benchmark };
struct WebHidService {
 bool initialized=true, sessionEstablished=true, blocked=false;
 WebHidTelemetryState telemetry;
 OutboundFrameSource pendingFrameSource=OutboundFrameSource::None;
 std::vector<int> outboundQueue, calls;
 void pumpOutput();
 bool pumpLogicalOutput(){calls.push_back(1);return true;}
 bool sendOneEdge(){calls.push_back(2);return true;}
 bool sendOneButtonState(){calls.push_back(3);return true;}
 bool sendLatestSample(uint32_t){calls.push_back(4);if(blocked)pendingFrameSource=OutboundFrameSource::Sample;return !blocked;}
 bool sendCheckpointChunk(){calls.push_back(5);return true;}
 void pumpBenchmark(){}
 bool queueOneEvent(){return false;}
};
''' + (ROOT / 'application/Src/webconfig/webhid_telemetry_state.cpp').read_text() + function('Src/webconfig/webhid_service.cpp', 'void WebHidService::pumpOutput()') + r'''
int main(){
 WebHidService s;s.telemetry.start(10);s.telemetry.schedule(10,100,10,false);s.telemetry.beginCheckpoint(100,200);s.telemetry.finishCheckpoint();
 s.pumpOutput();assert((s.calls==std::vector<int>{4,5}));assert(!s.telemetry.hasSample());
 s.calls.clear();s.telemetry.schedule(20,200,10,false);s.blocked=true;s.pumpOutput();
 assert((s.calls==std::vector<int>{4}));assert(s.telemetry.hasSample());
 s.calls.clear();s.outboundQueue.push_back(1);s.blocked=false;s.pumpOutput();
 assert((s.calls==std::vector<int>{4})); // Already sequenced sample owns its retry.
 s.pendingFrameSource=OutboundFrameSource::None;s.calls.clear();s.pumpOutput();
 assert((s.calls==std::vector<int>{1}));
 s.outboundQueue.clear();assert(s.telemetry.admitEdge(false));s.telemetry.enqueueAdmittedEdge({});s.calls.clear();s.pumpOutput();
 assert((s.calls==std::vector<int>{2}));
 s.telemetry.commitEdge();s.telemetry.commitSample();s.calls.clear();s.pumpOutput();
 assert((s.calls==std::vector<int>{5}));
}
'''
        feedback.LiveConfigFeedbackTests.native(self, source,
            {n: (ROOT / "common" / n).read_text() for n in ["webhid_protocol.h", "device_security_crypto.h"]})
