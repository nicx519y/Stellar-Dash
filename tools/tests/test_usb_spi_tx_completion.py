from pathlib import Path
import shutil
import tempfile
import unittest
from .application_paths import run_native

ROOT = Path(__file__).resolve().parents[2]


class UsbSpiTxCompletionTests(unittest.TestCase):
    def test_counter_end_does_not_release_fifo_while_master_selects_it(self):
        text = (ROOT / 'RF_PHY_Hop/TX/USB/usb_board_link_port_ch585.c').read_text()
        start = text.index('void usb_board_link_port_spi_irq_handler(void)')
        end = text.index('\nvoid usb_board_link_port_nss_rise_irq_handler', start)
        source = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"Failed: %s\n",#x);exit(1);}}while(0)
enum {RB_SPI_IF_CNT_END=1,RB_SPI_IF_DMA_END=2,RB_SPI_IF_FIFO_OV=4,RB_SPI_IF_FIFO_HF=8,
      SPI0_IT_CNT_END=1,DISABLE=0,USB_SPI_RX_DMA_BYTES=4092};
uint8_t R8_SPI0_INT_FLAG,s_ready=1,s_tx_armed=1,s_fast_input,s_fast_webhid;
uint32_t s_rx_dma_wrap_bytes,s_rx_dma_consumed;
static unsigned finished,disabled;static uint8_t selected=1;
uint8_t nss_is_high(void){return !selected;}
uint32_t rx_dma_position(void){return 0;}
void record_overflow(unsigned a,uint32_t b,uint32_t c){(void)a;(void)b;(void)c;}
void rx_drain_fifo_locked(void){}
void tx_dma_finish(void){CHECK(!selected);++finished;s_tx_armed=0;}
void SPI0_ITCfg(unsigned enable,unsigned mask){CHECK(enable==DISABLE);disabled|=mask;}
''' + text[start:end] + r'''
int main(void){
 R8_SPI0_INT_FLAG=RB_SPI_IF_CNT_END;usb_board_link_port_spi_irq_handler();
 CHECK(!finished&&s_tx_armed&&(disabled&SPI0_IT_CNT_END));
 CHECK(R8_SPI0_INT_FLAG&RB_SPI_IF_CNT_END); /* Poller still needs completion evidence. */
 selected=0;usb_board_link_port_spi_irq_handler();CHECK(finished==1&&!s_tx_armed);
 s_fast_webhid=1;R8_SPI0_INT_FLAG=RB_SPI_IF_DMA_END;
 usb_board_link_port_spi_irq_handler();CHECK(s_rx_dma_wrap_bytes==4092&&finished==1);
}
'''
        with tempfile.TemporaryDirectory(prefix='xora-spi-completion-') as folder:
            p = Path(folder)
            (p / 'test.c').write_text(source)
            exe = p / 'test.exe'
            result = run_native([shutil.which('gcc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                                 str(p / 'test.c'), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = run_native([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
