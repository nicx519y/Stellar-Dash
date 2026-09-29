import unittest
from tools.rf_link_report import distribution, summarize


class RfLinkReportTests(unittest.TestCase):
    def test_empty_data_is_not_a_pass(self):
        self.assertIsNone(distribution([])["p95_ms"])
        self.assertEqual(distribution(range(1, 101))["p95_ms"], 95)

    def test_repeated_samples_reboots_and_missing_samples(self):
        def sample(count, seq, latency):
            return dict(kind="packet", channel="RF", messageType="RFH_RHD1_0",
                        timestampMs=seq * 100, seq=seq, rfConnectCount=count, rfConnectMs=latency)
        result = summarize([sample(1, 1, 80), sample(1, 2, 80), sample(3, 3, 90), sample(1, 1, 70)])
        self.assertEqual(result["initial_connect_rf_ms"]["samples"], 2)
        self.assertEqual(result["reconnect_rf_ms"]["samples"], 1)
        self.assertEqual(result["missing_connect_samples"], 1)

    def test_live_hop_is_not_a_disconnect(self):
        result = summarize(dict(kind="packet", channel="RF", messageType=f"RFH_RHM1_{state}",
                                timestampMs=index * 100, rfStateCode=state)
                           for index, state in enumerate(["C", "HR", "C", "RP", "C"]))
        self.assertEqual(result["link_losses"], 1)
        self.assertEqual(result["host_observed_recovery_ms"]["samples"], 1)
        self.assertEqual(result["host_observed_recovery_ms"]["max_ms"], 100)


if __name__ == "__main__":
    unittest.main()
