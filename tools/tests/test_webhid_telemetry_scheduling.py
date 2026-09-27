import unittest

from . import test_live_config_feedback as feedback
from .test_live_config_feedback import function


class WebHidTelemetrySchedulingTests(unittest.TestCase):
    def test_live_sample_interleaves_checkpoint_and_preserves_control_and_pinned_ownership(self):
        source = r'''
#include <cassert>
#include <cstdint>
#include <vector>
enum class OutboundFrameSource { None, Logical, Edge, ButtonState, Sample, Checkpoint, Benchmark };
struct WebHidService {
 bool initialized=true, sessionEstablished=true, samplePending=false, checkpointActive=false, blocked=false;
 unsigned edgeCount=0, buttonStateCount=0, sampleTimestampUs=0;
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
''' + function('Src/webconfig/webhid_service.cpp', 'void WebHidService::pumpOutput()') + r'''
int main(){
 WebHidService s;s.samplePending=true;s.checkpointActive=true;
 s.pumpOutput();assert((s.calls==std::vector<int>{4,5}));assert(!s.samplePending);
 s.calls.clear();s.samplePending=true;s.blocked=true;s.pumpOutput();
 assert((s.calls==std::vector<int>{4}));assert(s.samplePending);
 s.calls.clear();s.outboundQueue.push_back(1);s.blocked=false;s.pumpOutput();
 assert((s.calls==std::vector<int>{4})); // Already sequenced sample owns its retry.
 s.pendingFrameSource=OutboundFrameSource::None;s.calls.clear();s.pumpOutput();
 assert((s.calls==std::vector<int>{1}));
 s.outboundQueue.clear();s.edgeCount=1;s.calls.clear();s.pumpOutput();
 assert((s.calls==std::vector<int>{2}));
 s.edgeCount=0;s.samplePending=false;s.calls.clear();s.pumpOutput();
 assert((s.calls==std::vector<int>{5}));
}
'''
        feedback.LiveConfigFeedbackTests.native(self, source)
