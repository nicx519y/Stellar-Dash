"""USB-only host interleavings of the production CH585 poller/retirement code.

No RF runtime, device sampling or Flash operations.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from .test_ch585_usb_handshake import function

ROOT = Path(__file__).resolve().parents[2]


class WebHidNssRetirementTest(unittest.TestCase):
    def test_live_nss_owns_dma_until_release(self):
        source = (ROOT / 'RF_PHY_Hop/TX/USB/usb_board_link_port_ch585.c').read_text()
        finish = function(source, 'static void tx_dma_finish(void)')
        process = function(source, 'USB_WEBHID_RAM void usb_board_link_port_process(void)')
        prefix = r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#define USB_WEBHID_RAM
enum { RB_SPI_IF_CNT_END=1, RB_SPI_IF_FST_BYTE=2, RB_SPI_DMA_ENABLE=4,
    RB_SPI_DMA_LOOP=8, SPI0_IT_CNT_END=1, SPI0_IT_DMA_END=16, DISABLE=0,
    USB_BOARD_LINK_MAX_FRAME_BYTES=64, USB_SPI_TX_SLOTS=4,
    USB_BOARD_STATUS_INTERNAL_ERROR=10, USB_SPI_IRQ_PIN=32, USB_SPI_RELEASE_GAP_US=1000 };
uint8_t R8_SPI0_INT_FLAG, R8_SPI0_CTRL_CFG=RB_SPI_DMA_ENABLE, R8_SPI0_FIFO_COUNT=4;
uint16_t R16_SPI0_TOTAL_CNT=100, s_tx_lengths[4]={100};
uint8_t s_tx_tail, s_tx_count=1, s_tx_armed=1, s_tx_nss_seen, s_tx_large_owned=1;
uint8_t s_ready=1,s_fast_webhid=1,s_fast_input,s_port_fault,s_release_gap_pending;
uint32_t s_release_gap_started_cycles,s_release_gap_cycles_per_us=60;
struct {uint32_t CNTL;} tick={100};
#define SysTick (&tick)
unsigned cleared,rx_started,released,detail_calls;uint32_t detail,expected,remaining;
bool selected,race;
uint8_t nss_is_high(void) {
    uint8_t old=!selected;
    if(race) {race=false;selected=true;R8_SPI0_INT_FLAG=RB_SPI_IF_FST_BYTE;R16_SPI0_TOTAL_CNT=96;}
    return old;
}
void SPI0_ITCfg(unsigned enable,unsigned bits) {(void)enable;(void)bits;}
void spi_fifo_clear(void) {++cleared;}
void rx_backend_start(uint8_t reset) {assert(reset);++rx_started;}
void GPIOA_SetBits(unsigned pin) {assert(pin==USB_SPI_IRQ_PIN);++released;}
void usb_webhid_fast_port_detail(uint32_t d,uint32_t p,uint32_t c) {
    ++detail_calls;detail=d;expected=p;remaining=c;
}
void port_lock(void) {}
void port_unlock(void) {}
void rx_dma_collect_locked(void) {}
void service_pending_nss_rise_locked(void) {}
bool tx_dma_arm_locked(void) {assert(false);return false;}
bool usb_webhid_fast_ready(void) {return true;}
'''
        main = r'''
int main(void) {
    /* First sample is high; the master asserts NSS and clocks the header
     * before the poller inspects FST_BYTE. This invokes production finish. */
    race=true;usb_board_link_port_process();
#ifdef PRE_FIX
    assert(s_port_fault==10 && !s_tx_armed && cleared==1 && released==1);
    puts("Reproduced fault=26: stale NSS sample retires an active read");
#else
    assert(s_tx_armed && s_tx_nss_seen && !s_port_fault && !cleared && !released && !detail_calls);
    /* CNT_END with NSS low must retain FIFO/buffer ownership too. */
    R8_SPI0_INT_FLAG=RB_SPI_IF_CNT_END;R16_SPI0_TOTAL_CNT=0;
    tx_dma_finish();assert(s_tx_armed && !cleared && s_tx_count==1);
    selected=false;usb_board_link_port_process();
    assert(!s_tx_armed && !s_tx_count && !s_port_fault && cleared==1 && released==1 && !s_tx_large_owned);
    /* A real partial read still fails; retain expected/remaining evidence. */
    s_tx_tail=0;s_tx_count=1;s_tx_armed=1;s_tx_lengths[0]=100;
    R8_SPI0_INT_FLAG=RB_SPI_IF_FST_BYTE;R16_SPI0_TOTAL_CNT=61;
    tx_dma_finish();assert(s_port_fault==10 && s_tx_count==1 && !s_tx_armed);
    assert(detail_calls==1 && (detail&255)==7 && expected==100 && remaining==61);
    /* A writer losing arbitration without clocks must leave TX armed. */
    s_port_fault=0;s_tx_armed=1;s_tx_nss_seen=1;R8_SPI0_INT_FLAG=0;R16_SPI0_TOTAL_CNT=100;
    unsigned before=cleared;tx_dma_finish();
    assert(s_tx_armed && !s_tx_nss_seen && !s_port_fault && cleared==before && detail_calls==1);
    /* The shared ordinary USB input/control port also retains active NSS. */
    s_fast_webhid=0;s_fast_input=1;selected=true;R8_SPI0_INT_FLAG=RB_SPI_IF_CNT_END;
    tx_dma_finish();assert(s_tx_armed && cleared==before && !s_port_fault);
    selected=false;tx_dma_finish();assert(!s_tx_armed && !s_tx_count && !s_port_fault);
    puts("Active read, completed read, genuine truncation and no-clock handoff passed");
#endif
}
'''
        guard = '''    if(nss_is_high() == 0u)
    {
        s_tx_nss_seen = 1u;
        return;
    }
'''
        self.assertIn(guard, finish)
        with tempfile.TemporaryDirectory(prefix='xora-webhid-nss-') as tmp:
            for previous in (True, False):
                body = finish.replace(guard, '', 1) if previous else finish
                body = body.replace('__asm volatile("fence iorw, iorw" ::: "memory");', '')
                cpp = Path(tmp) / ('before.c' if previous else 'after.c')
                exe = cpp.with_suffix('.exe')
                cpp.write_text(('#define PRE_FIX\n' if previous else '') + prefix + body + process + main)
                print('Compile NSS retirement ' + ('reproducer' if previous else 'regression'), flush=True)
                subprocess.run([shutil.which('gcc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                                str(cpp), '-o', str(exe)], check=True, timeout=60)
                subprocess.run([str(exe)], check=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
