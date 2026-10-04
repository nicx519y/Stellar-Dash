"""USB-only production hsDma with controlled HAL completion interleavings."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from .test_ch585_usb_handshake import function

ROOT = Path(__file__).resolve().parents[2]


class WebHidDmaRetirementTest(unittest.TestCase):
    def test_dma_handles_retire_before_the_next_block(self):
        source = (ROOT / 'application/Src/transport/usb/usb_board_link_port.cpp').read_text()
        body = function(source, 'static bool hsDma(')
        condition = '''while(HAL_SPI_GetState(&s_hspi)!=HAL_SPI_STATE_READY ||
          HAL_DMA_GetState(&s_hsTxDma)!=HAL_DMA_STATE_READY ||
          HAL_DMA_GetState(&s_hsRxDma)!=HAL_DMA_STATE_READY)'''
        self.assertIn(condition, body)
        prefix = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#define HBOX_SECURE_BOOT_REQUIRED 1
enum {HAL_OK=0,HAL_ERROR=1,HAL_SPI_STATE_READY=1,HAL_DMA_STATE_READY=1,BUSY=2,
      HAL_SPI_ERROR_NONE=0,HAL_SPI_ERROR_DMA=16};
struct Dma {int state=HAL_DMA_STATE_READY;bool locked=false,complete=false;};
struct Spi {int state=HAL_SPI_STATE_READY;unsigned error=0;};
Dma s_hsTxDma,s_hsRxDma;Spi s_hspi;
struct {uint32_t CYCCNT=0;} dwt;
#define DWT (&dwt)
bool s_fastWebHid=true,s_fastIap=false,lateTx=true,stuck=false,stuckTx=false,dmaError=false;
unsigned starts=0,aborts=0;uint32_t tick=0;
uint8_t s_hsTx[4096],s_hsRx[4096];
void SCB_CleanDCache_by_Addr(uint32_t*,int32_t) {}
void SCB_InvalidateDCache_by_Addr(uint32_t*,int32_t) {}
void __DSB() {}
uint32_t HAL_GetTick() {return tick;}
int HAL_SPI_GetState(Spi* p) {return p->state;}
int HAL_DMA_GetState(Dma* p) {return p->state;}
unsigned HAL_SPI_GetError(Spi* p) {return p->error;}
int HAL_SPI_TransmitReceive_DMA(Spi* p,uint8_t*,uint8_t* rx,uint16_t size) {
    ++starts;
    // HAL_DMA_Start_IT refuses a BUSY/locked stream; no new body clocks.
    if(s_hsTxDma.state!=HAL_DMA_STATE_READY || s_hsTxDma.locked ||
       s_hsRxDma.state!=HAL_DMA_STATE_READY || s_hsRxDma.locked) {
        p->error=HAL_SPI_ERROR_DMA;return HAL_ERROR;
    }
    p->state=BUSY;p->error=0;
    s_hsTxDma={BUSY,true,!lateTx};s_hsRxDma={BUSY,true,true};
    memset(rx,0xab,size);return HAL_OK;
}
void HAL_DMA_IRQHandler(Dma* p) {
    if(stuck || (stuckTx && p==&s_hsTxDma)) return;
    if(p->complete) {p->state=HAL_DMA_STATE_READY;p->locked=false;p->complete=false;}
    if(p==&s_hsRxDma) {
        // Hardware TX completes just AFTER the TX poll. RX then enables EOT
        // in the same loop pass; TX TC bookkeeping needs one more poll.
        if(s_hsTxDma.state==BUSY) s_hsTxDma.complete=true;
        if(dmaError) s_hspi.error=HAL_SPI_ERROR_DMA;
    }
}
void HAL_SPI_IRQHandler(Spi* p) {
    ++tick;
    if(s_hsRxDma.state==HAL_DMA_STATE_READY) p->state=HAL_SPI_STATE_READY;
}
int HAL_SPI_Abort(Spi* p) {++aborts;p->state=HAL_SPI_STATE_READY;s_hsTxDma={};s_hsRxDma={};return HAL_OK;}
'''
        main = r'''
int main() {
    uint8_t rx[3100];
    assert(hsDma(nullptr,rx,28));
#ifdef PRE_FIX
    assert(s_hspi.state==HAL_SPI_STATE_READY && s_hsTxDma.state==BUSY && s_hsTxDma.locked);
    assert(!hsDma(nullptr,rx,28) && s_hspi.error==HAL_SPI_ERROR_DMA);
    puts("Reproduced: SPI READY, TX DMA BUSY/locked; next frame body cannot start");
#else
    assert(s_hsTxDma.state==HAL_DMA_STATE_READY && !s_hsTxDma.locked);
    for(unsigned i=0;i<1000;++i) {
        lateTx=(i%2)==0;
        uint16_t size=(i%3)==0?28:(i%3)==1?32:3100;
        memset(rx,0,sizeof(rx));assert(hsDma(nullptr,rx,size));
        assert(rx[0]==0xab && rx[size-1]==0xab);
        assert(!s_hsTxDma.locked && !s_hsRxDma.locked);
    }
    // Faults remain errors, not successful completions or silent retries.
    s_fastWebHid=false;s_fastIap=true;
    assert(hsDma(nullptr,rx,1024)); // The offline IAP uses the same safe DMA retirement.
    s_fastIap=false;assert(!hsDma(nullptr,rx,1024));s_fastWebHid=true;
    dmaError=true;assert(!hsDma(nullptr,rx,28));dmaError=false;
    stuck=true;unsigned before=starts;uint32_t began=tick;
    assert(!hsDma(nullptr,rx,28) && tick-began==10 && aborts==1 && starts==before+1);
    stuck=false;stuckTx=true;lateTx=true;before=starts;began=tick;
    assert(!hsDma(nullptr,rx,28) && tick-began==10 && aborts==2 && starts==before+1);
    puts("1000 short/long exchanges, both completion orders, DMA error and bounded timeout passed");
#endif
}
'''
        with tempfile.TemporaryDirectory(prefix='xora-webhid-dma-') as tmp:
            for previous in (True, False):
                current = body.replace(condition, 'while(HAL_SPI_GetState(&s_hspi)!=HAL_SPI_STATE_READY)') if previous else body
                cpp = Path(tmp) / ('before.cpp' if previous else 'after.cpp')
                exe = cpp.with_suffix('.exe')
                cpp.write_text(('#define PRE_FIX\n' if previous else '') + prefix + current + main)
                print('Compile DMA retirement ' + ('reproducer' if previous else 'regression'), flush=True)
                subprocess.run([shutil.which('g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                str(cpp), '-o', str(exe)], check=True, timeout=60)
                subprocess.run([str(exe)], check=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
