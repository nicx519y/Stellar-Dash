"""Actual IAP/client functions with flash/SPI faults. No devices or RF runtime."""
from pathlib import Path
import shutil
import tempfile
import unittest
from .application_paths import run_native
from .test_ch585_usb_handshake import function

ROOT = Path(__file__).resolve().parents[2]


class IapDmaTests(unittest.TestCase):
    def run_cpp(self, source):
        with tempfile.TemporaryDirectory(prefix='xora-iap-dma-') as tmp:
            cpp = Path(tmp) / 'test.cpp'
            exe = cpp.with_suffix('.exe')
            cpp.write_text(source, encoding='utf-8')
            compiler = shutil.which('g++')
            self.assertIsNotNone(compiler)
            result = run_native([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                 '-Icommon', '-Iapplication/Inc/firmware', str(cpp), '-o', str(exe)],
                                cwd=ROOT, capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = run_native([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_loader_frames_flash_bounds_and_commit_faults(self):
        production = (ROOT / 'RF_PHY_Hop/TX/IAP/iap_main.c').read_text()
        prefix = (ROOT / 'tools/tests/iap_dma_loader_test.cpp').read_text()
        bodies = function(production, 'static uint32_t crc32_update(').replace(
            'crc32_update(', 'real_crc32_update(', 1)
        bodies += '''
static uint32_t crc32_update(uint32_t crc,const uint8_t* data,uint32_t size) {
    if(reinterpret_cast<uintptr_t>(data)==CH585_IAP_APP_START) data=flash+CH585_IAP_APP_START;
    return real_crc32_update(crc,data,size);
}
'''
        for signature in ['static bool metadata_read(', 'static bool metadata_write(',
                          'static bool update_was_interrupted(', 'static bool packet_valid(',
                          'static bool dma_packet_complete(', 'static uint8_t handle_packet(']:
            bodies += function(production, signature) + '\n'
        self.run_cpp(prefix.replace('/* PRODUCTION */', bodies))

    def test_controller_negotiation_packet_retries_and_legacy_fallback(self):
        production = (ROOT / 'application/Src/firmware/ch585_iap_client.cpp').read_text()
        prefix = (ROOT / 'tools/tests/iap_dma_client_test.cpp').read_text()
        bodies = production[production.index('static constexpr uint32_t kCombinedIapBytes'):production.index('} // namespace')]
        for signature in ['bool Ch585IapClient::transact(', 'bool Ch585IapClient::programCombinedImage(',
                          'bool Ch585IapClient::programApplicationImage(']:
            bodies += function(production, signature) + '\n'
        self.run_cpp(prefix.replace('/* PRODUCTION */', bodies))

    def test_rx_is_armed_before_ready_and_dma_objects_are_isolated(self):
        source = (ROOT / 'RF_PHY_Hop/TX/IAP/iap_main.c').read_text()
        ack = function(source, 'static bool send_response(')
        self.assertLess(ack.index('spi_receive_start();'), ack.index('set_w_int(false)'))
        arm = function(source, 'static void spi_receive_start(')
        self.assertIn('RB_SPI_DMA_ENABLE', arm)
        self.assertIn('R32_SPI0_DMA_NOW = R32_SPI0_DMA_BEG', arm)
        self.assertNotIn('prepare_spi_pins', arm)  # That helper publishes high too early.
        main = function(source, 'int main(void)')
        self.assertIn('if(!s_dma_mode && (R8_SPI0_INT_FLAG & RB_SPI_IF_FIFO_OV)', main)
        makefile = (ROOT / 'RF_PHY_Hop/TX/Makefile').read_text()
        self.assertIn('IAP/SDK/CH58x_sys.o', makefile)
        self.assertIn('$(IAP_MAIN_OBJ) $(IAP_BOARD_OBJ) $(IAP_DRIVER_OBJS): CFLAGS += -flto', makefile)
        self.assertIn('--pad-to 0x1000', makefile)

    def test_raw_port_uses_dma_only_after_negotiation(self):
        port = (ROOT / 'application/Src/transport/usb/usb_board_link_port.cpp').read_text()
        # Native fault simulation of the production raw-port body (HAL registers mocked).
        prefix = (ROOT / 'tools/tests/iap_dma_port_test.cpp').read_text()
        self.run_cpp(prefix.replace('/* PRODUCTION */', function(port, 'bool USBBoardLinkPort_RawTransact(')))
