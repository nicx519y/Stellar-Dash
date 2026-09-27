import pathlib, struct, unittest
from tools.webhid_benchmark_capture import SIZE, capture_script, decode

class CaptureTests(unittest.TestCase):
    def test_only_two_non_halting_sram_reads(self):
        script=capture_script('0123456789abcdef01234567',0x38000100,pathlib.Path('capture.bin'))
        self.assertEqual(script.count('dump_image '),2)
        for forbidden in ('halt','reset','mww','stm32h7x','flash bank','unlock','option','rdp'):
            self.assertNotIn(forbidden,script.lower())
        with self.assertRaises(ValueError): capture_script('0123456789abcdef01234567',0x3800F800,'x')
    def test_incomplete_torn_or_uninitialized_snapshots_are_not_results(self):
        words=[0]*32;words[:6]=[0x32424858,2,SIZE,2,47,3]
        raw=struct.pack('<32I',*words)+bytes(32)
        self.assertEqual(decode(raw)['run_id'],47)
        for index,value in ((0,0),(2,64),(3,3),(5,1),(5,2)):
            bad=words[:];bad[index]=value
            with self.assertRaises(ValueError):decode(struct.pack('<32I',*bad)+bytes(32))
