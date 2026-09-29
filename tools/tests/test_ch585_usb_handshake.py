"""USB-only host simulation of production handshake, port gates and shared ISR.

No RF runtime regression or hardware access. Register stubs model EXTI's
latched rising edge even when the physical high pulse was missed by the CPU.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class UsbHandshakeTest(unittest.TestCase):
    def test_iap_ack_and_release_fault_are_independent(self):
        implementation = (ROOT / 'application/Src/firmware/ch585_iap_client.cpp').read_text(encoding='utf-8')
        source = r'''
#include <cassert>
#include <cstring>
#include <initializer_list>
#define private public
#include "ch585_iap_client.hpp"
#undef private
#include "ch585_iap_protocol.h"
#define APP_STAGE_ERROR(...) ((void)0)
bool fault=false;
int reply=0;
bool USBBoardLinkPort_HasReleaseFault() { return fault; }
bool USBBoardLinkPort_RawTransact(const uint8_t* data, uint16_t, uint8_t* out, uint16_t, uint32_t) {
    if (reply==0) return false;
    const auto& request=*reinterpret_cast<const ch585_iap_packet_t*>(data);
    ch585_iap_response_t response={};
    response.magic=CH585_IAP_RESPONSE_MAGIC;
    response.version=CH585_IAP_PROTOCOL_VERSION;
    response.command=request.command; response.sequence=request.sequence;
    response.status=reply==2 ? CH585_IAP_STATUS_BAD_STATE : CH585_IAP_STATUS_OK;
    auto bytes=reinterpret_cast<const uint8_t*>(&response);
    for(unsigned i=0;i<sizeof(response)-1;++i) response.crc8+=bytes[i];
    if(reply==3) ++response.crc8;
    memcpy(out,&response,sizeof(response)); return true;
}
'''
        for signature in ['static uint32_t crc32Update(', 'static uint8_t crc8Sum(', 'bool Ch585IapClient::transact(']:
            source += function(implementation, signature) + '\n'
        source += r'''
int main() {
    auto& client=Ch585IapClient::getInstance();
    for(uint8_t command : {CH585_IAP_CMD_BEGIN, CH585_IAP_CMD_WRITE, CH585_IAP_CMD_END}) {
        for(bool broken : {false,true}) {
            fault=broken;
            reply=0;
            assert(!client.transact(command,4096,0,nullptr,0,20));
            assert(client.lastTransactionTimedOut==!broken);
            reply=1;
            assert(client.transact(command,4096,0,nullptr,0,20));
            assert(!client.lastTransactionTimedOut); // Valid ACK survives broken release.
            reply=2;
            assert(!client.transact(command,4096,0,nullptr,0,20));
            assert(client.status()==Ch585IapClientStatus::DeviceError && !client.lastTransactionTimedOut);
            reply=3;
            assert(!client.transact(command,4096,0,nullptr,0,20));
            assert(client.status()==Ch585IapClientStatus::ProtocolError && !client.lastTransactionTimedOut);
        }
    }
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / 'test.cpp').write_text(source, encoding='utf-8')
            print('Compile/run IAP ACK classification (mock port; no flash)', flush=True)
            subprocess.run([shutil.which('g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            f'-I{ROOT / "application/Inc/firmware"}', f'-I{ROOT / "common"}',
                            str(path / 'test.cpp'), '-o', str(path / 'test.exe')], check=True, timeout=60)
            subprocess.run([str(path / 'test.exe')], check=True, timeout=10)

    def test_production_observer_port_and_shared_irq(self):
        compiler = shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            (folder / 'stm32h7xx_hal.h').write_text(r'''
#pragma once
#include <cstdint>
struct Registers { uint32_t IMR1=0, RTSR1=0, FTSR1=0, PR1=0; };
extern Registers regs;
extern uint32_t ticks, masked;
extern bool wireHigh;
#define EXTI (&regs)
#define EXTI_D1 (&regs)
#define CLEAR_BIT(r,b) ((r) &= ~(b))
#define SET_BIT(r,b) ((r) |= (b))
#define __HAL_GPIO_EXTI_CLEAR_IT(b) (regs.PR1 &= ~(b))
#define __HAL_GPIO_EXTI_GET_IT(b) (regs.PR1 & (b))
#define RESET 0
#define GPIO_PIN_SET 1
#define GPIO_PIN_RESET 0
#define GPIO_MODE_IT_RISING 1
#define GPIO_PULLUP 1
#define GPIO_SPEED_FREQ_LOW 0
struct GPIO_InitTypeDef { uint32_t Pin, Mode, Pull, Speed; };
inline uint32_t __get_PRIMASK() { return masked; }
inline void __disable_irq() { masked=1; }
inline void __enable_irq() { masked=0; }
inline void __DSB() {}
inline void HAL_GPIO_Init(int, GPIO_InitTypeDef*) {}
inline void HAL_NVIC_SetPriority(int,int,int) {}
inline void HAL_NVIC_EnableIRQ(int) {}
inline void SCB_CleanDCache_by_Addr(uint32_t*,unsigned) {}
inline int HAL_GPIO_ReadPin(int,int) { return wireHigh; }
inline uint32_t HAL_GetTick() { return ticks; }
''')
            (folder / 'board_cfg.h').write_text('''#pragma once
#define CH585_IRQ_GPIO_PORT 0
#define CH585_IRQ_PIN (1u<<10)
#define RF_BRIDGE_IRQ_PIN CH585_IRQ_PIN
#define RF_BRIDGE_IRQ_EXTI_IRQn 40
#define RF_BRIDGE_IRQ_EXTI_IRQn_PRIO 4
#define MAX17048_ALERT_PIN (1u<<13)
''')
            (folder / 'rf_bridge_port_internal.h').write_text(
                '#pragma once\nvoid RFBridgePort_IRQ_IRQHandler();\n')
            irq = (ROOT / 'application/Core/Src/stm32h7xx_it.c').read_text(encoding='utf-8')
            port = (ROOT / 'application/Src/transport/usb/usb_board_link_port.cpp').read_text(encoding='utf-8')
            source = r'''
#include <cassert>
#include <cstdio>
#include "stm32h7xx_hal.h"
#include "board_cfg.h"
#include "ch585_handshake.h"
Registers regs;
uint32_t ticks=0, masked=0;
bool wireHigh=false, s_ready=true, s_fastWebHid=true;
unsigned gaugeAlerts=0, transfers=0, faults=0;
constexpr unsigned WHF_HEADER_BYTES=32, WHF_BLOCK_BYTES=3104;
void RFBridgePort_IRQ_IRQHandler() { assert(false); } // USB must never dispatch RF.
void PowerManager_NotifyGaugeAlertFromISR() { ++gaugeAlerts; }
void chipSelect(bool) {}
void ownershipGuardDelay() {}
bool hsDma(const uint8_t*, void*, uint16_t) { ++transfers; return true; }
void USBBoardLink_HsTransportFault() { ++faults; }
'''
            source += function(irq, 'void EXTI15_10_IRQHandler(void)') + '\n'
            for signature in ['static bool eventLineIsHigh()', 'static bool refreshEventRelease()',
                              'bool USBBoardLinkPort_HasEvent()', 'bool USBBoardLinkPort_HasReleaseFault()',
                              'bool USBBoardLinkPort_SendWebHidBlock(']:
                source += function(port, signature) + '\n'
            source += r'''
void pulse(bool serviceIrq) {
    assert(regs.RTSR1 & CH585_IRQ_PIN);
    regs.PR1 |= CH585_IRQ_PIN; // Physical low -> high -> low before CPU polls.
    wireHigh=false;
    if(serviceIrq) EXTI15_10_IRQHandler();
}
int main() {
    uint8_t block[WHF_HEADER_BYTES]={};
    for (unsigned mode=0; mode<3; ++mode) {
        Ch585Handshake_Acquire(CH585_LINE_USB,20);
        wireHigh=false;
        const auto ticket=Ch585Handshake_BeginRead(CH585_LINE_USB);
        assert(ticket && !Ch585Handshake_BeginRead(CH585_LINE_USB));
        if (mode<2) pulse(mode==0); else wireHigh=true;
        Ch585Handshake_EndRead(CH585_LINE_USB,ticket);
        assert(Ch585Handshake_Ready(CH585_LINE_USB));
        assert(USBBoardLinkPort_HasEvent()==!wireHigh);
        assert(USBBoardLinkPort_SendWebHidBlock(block,sizeof(block))==wireHigh);
        wireHigh=true;
        assert(USBBoardLinkPort_SendWebHidBlock(block,sizeof(block)));
    }
    // Finish first, then capture release; unrelated PC13 must survive PE10 changes.
    Ch585Handshake_Acquire(CH585_LINE_USB,20); wireHigh=false;
    auto ticket=Ch585Handshake_BeginRead(CH585_LINE_USB);
    Ch585Handshake_EndRead(CH585_LINE_USB,ticket);
    assert(!Ch585Handshake_Ready(CH585_LINE_USB));
    regs.PR1 |= MAX17048_ALERT_PIN;
    pulse(true);
    assert(gaugeAlerts==1 && Ch585Handshake_Ready(CH585_LINE_USB));
    regs.PR1 |= MAX17048_ALERT_PIN;
    regs.IMR1 |= MAX17048_ALERT_PIN;
    Ch585Handshake_Release(CH585_LINE_USB);
    assert((regs.PR1 & regs.IMR1 & MAX17048_ALERT_PIN)!=0);
    EXTI15_10_IRQHandler(); assert(gaugeAlerts==2);
    // Old pending edge and old same-owner ticket cannot release the new read.
    regs.PR1 |= CH585_IRQ_PIN;
    Ch585Handshake_Acquire(CH585_LINE_USB,20);
    const auto newer=Ch585Handshake_BeginRead(CH585_LINE_USB);
    Ch585Handshake_EndRead(CH585_LINE_USB,ticket);
    assert(!Ch585Handshake_Ready(CH585_LINE_USB));
    masked=1;
    Ch585Handshake_EndRead(CH585_LINE_USB,newer);
    assert(masked==1); masked=0;
    // Time wrap, never-release, sticky fault even after a late high.
    Ch585Handshake_Release(CH585_LINE_USB);
    Ch585Handshake_Acquire(CH585_LINE_USB,20);
    ticks=0xfffffff8u;
    ticket=Ch585Handshake_BeginRead(CH585_LINE_USB);
    Ch585Handshake_EndRead(CH585_LINE_USB,ticket);
    ticks+=19;
    assert(!USBBoardLinkPort_HasReleaseFault() && !USBBoardLinkPort_HasEvent());
    ticks++;
    assert(USBBoardLinkPort_HasReleaseFault());
    const auto count=g_ch585_release_fault[1];
    wireHigh=true;
    for(unsigned i=0;i<10000;++i) {
        assert(!USBBoardLinkPort_HasEvent());
        assert(!USBBoardLinkPort_SendWebHidBlock(block,sizeof(block)));
    }
    assert(g_ch585_release_fault[1]==count && g_ch585_release_fault[0]==0x57524c31);
    Ch585Handshake_Release(CH585_LINE_USB);
    Ch585Handshake_Acquire(CH585_LINE_USB,20);
    assert(Ch585Handshake_Ready(CH585_LINE_USB));
    puts("USB release pulses, sticky timeout, generation and shared IRQ passed");
}
'''
            (folder / 'test.cpp').write_text(source)
            exe = folder / 'test.exe'
            print('Compile production USB handshake/ISR fixture', flush=True)
            subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            f'-I{folder}', f'-I{ROOT / "application/Inc/transport"}',
                            str(ROOT / 'application/Src/transport/ch585_handshake.cpp'),
                            str(folder / 'test.cpp'), '-o', str(exe)], check=True, timeout=60)
            print('Run USB-only handshake scenarios', flush=True)
            subprocess.run([str(exe)], check=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
