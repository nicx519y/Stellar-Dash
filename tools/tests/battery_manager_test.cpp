#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#define private public
#include "power_manager.hpp"
#undef private

static uint32_t now;
uint32_t HAL_GetTick() { return now; }
void HAL_Delay(uint32_t ms) { now += ms; }
uint32_t __get_PRIMASK() { return 0; }
void __disable_irq() {}
void __enable_irq() {}
#define APP_STAGE(tag, ...) do { if (false) std::printf(__VA_ARGS__); } while (0)
#define APP_STAGE_ERROR(...) ((void)0)
#define APP_ERR(...) ((void)0)
// Keep log arguments referenced, as on a diagnostic target.
#define APP_DBG(...) do { if (false) std::printf(__VA_ARGS__); } while (0)
using GPIO_TypeDef = int;
static int fakeGpio;
#define GPIOA (&fakeGpio)
#define GPIOB (&fakeGpio)
#define GPIOC (&fakeGpio)
#define GPIOD (&fakeGpio)
#define GPIOE (&fakeGpio)
#define GPIOF (&fakeGpio)
#define GPIOG (&fakeGpio)
#define GPIOH (&fakeGpio)
#define GPIOI (&fakeGpio)
#define GPIOJ (&fakeGpio)
#define GPIOK (&fakeGpio)
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOC_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOD_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOE_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOF_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOG_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOH_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOI_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOJ_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOK_CLK_ENABLE() ((void)0)
#define __HAL_RCC_SYSCFG_CLK_ENABLE() ((void)0)
enum { GPIO_PIN_0=1, GPIO_PIN_2=4, GPIO_PIN_3=8, GPIO_PIN_8=256,
       GPIO_PIN_13=8192, GPIO_MODE_INPUT, GPIO_MODE_IT_FALLING,
       GPIO_NOPULL, GPIO_PULLUP, EXTI9_5_IRQn, EXTI15_10_IRQn,
       CH585_IRQ_EXTI_IRQn_PRIO, GPIO_PIN_RESET=0, GPIO_PIN_SET=1,
       I2C_MEMADD_SIZE_8BIT=1, HAL_OK=0 };
struct GPIO_InitTypeDef { unsigned Pin, Mode, Pull; };
void HAL_GPIO_Init(GPIO_TypeDef*, GPIO_InitTypeDef*) {}
void HAL_NVIC_SetPriority(int, unsigned, unsigned) {}
void HAL_NVIC_EnableIRQ(int) {}
static bool fastPg = true;
int HAL_GPIO_ReadPin(GPIO_TypeDef*, unsigned) { return fastPg ? GPIO_PIN_RESET : GPIO_PIN_SET; }
struct FakeBoard {
    bool charge=false, safe=false;
    unsigned transitions=0;
    bool isInitialized() const { return true; }
    void setup() {}
    bool isSafeLatched() const { return safe; }
    bool isChargeEnabled() const { return charge; }
    void setChargeEnabled(bool v) { if (v != charge) ++transitions; charge=v; }
} board;
#define BOARD_POWER board
enum class BoardMode { Usb, Rf };
struct { bool isStable() const { return true; } BoardMode current() const { return BoardMode::Usb; } } mode;
#define BOARD_MODE mode
enum class Ch585Role { Usb, Rf, Maintenance };
struct { bool isLocked() const { return true; } Ch585Role role() const { return Ch585Role::Usb; } } role;
#define CH585_ROLE_BOOTSTRAP role
enum class RfPowerReason { SystemSleep, SystemWake };
struct {
    bool ensureRfSleeping(RfPowerReason) { assert(false); return false; }
    bool wakeRfFromSleep(RfPowerReason) { assert(false); return false; }
    bool restoreRfRuntime(unsigned) { assert(false); return false; }
} connection;
#define CONNECTION_MANAGER connection
struct { unsigned getWirelessReportRate() const { return 1000; } } storage;
#define STORAGE_MANAGER storage
void SystemSleep_RequestStandby() {}

static I2C_HandleTypeDef bus;
static uint8_t regs[256];
static uint8_t faultHistory, faultCurrent;
static unsigned faultReads;
static bool gaugeOk=true, ioOk=true;
static MAX17048_State gaugeState{3800, 500, 0, false, true};
int HAL_I2C_IsDeviceReady(I2C_HandleTypeDef*, unsigned, unsigned, unsigned) { return ioOk ? HAL_OK : 1; }
int HAL_I2C_Mem_Read(I2C_HandleTypeDef*, unsigned, uint8_t reg, unsigned, uint8_t* data, unsigned, unsigned) {
    if (!ioOk) return 1;
    *data = reg == 0x0c ? (faultReads++ % 2 == 0 ? faultHistory : faultCurrent) : regs[reg];
    return HAL_OK;
}
int HAL_I2C_Mem_Write(I2C_HandleTypeDef*, unsigned, uint8_t reg, unsigned, uint8_t* data, unsigned, unsigned) {
    regs[reg]=*data; return ioOk ? HAL_OK : 1;
}
extern "C" {
bool PowerI2C_Init() { return ioOk; }
void PowerI2C_DeInit() {}
I2C_HandleTypeDef* PowerI2C_GetHandle() { return &bus; }
bool PowerI2C_AsyncBusy() { return false; }
PowerI2C_Result PowerI2C_StepRegister(const PowerI2C_Register* op, uint16_t* value) {
    if (!ioOk) return POWER_I2C_FAILED;
    uint8_t got=0;
    HAL_I2C_Mem_Read(&bus, 0, op->reg, 0, &got, 1, 0);
    if (op->kind == POWER_I2C_UPDATE) regs[op->reg] = got = (got & ~op->mask) | (op->value & op->mask);
    *value=got;
    return op->kind == POWER_I2C_VERIFY && (got & op->mask) != (op->value & op->mask)
        ? POWER_I2C_FAILED : POWER_I2C_DONE;
}
bool MAX17048_Init(MAX17048_Handle* h, I2C_HandleTypeDef* b) { h->i2c=b; return h->online=gaugeOk; }
bool MAX17048_ConfigureAlert(MAX17048_Handle*, uint8_t) { return gaugeOk; }
bool MAX17048_ReadState(MAX17048_Handle*, MAX17048_State* s) { *s=gaugeState; return gaugeOk; }
bool MAX17048_ClearAlert(MAX17048_Handle*) { return gaugeOk; }
PowerI2C_Result MAX17048_Step(MAX17048_Handle* h, MAX17048_Job*, MAX17048_State* s) {
    h->online=gaugeOk; *s=gaugeState; return gaugeOk ? POWER_I2C_DONE : POWER_I2C_FAILED;
}
}
#define _Static_assert static_assert
#include "../../application/Src/power/drivers/bq25895/bq25895.c"
#include "../../application/Src/power/power_manager.cpp"

static void cycle(PowerManager& p, bool irq=false, bool catchup=false) {
    now += 1000;
    if (irq) p.notifyChargerIrqFromISR();
    if (catchup) p.requestRefreshFromISR();
    p.loop();
    for (unsigned i=0; p.isPolling() && i<150; ++i) { ++now; p.loop(); }
    assert(!p.isPolling());
}

int main() {
    regs[0x14]=0x39;
    regs[0x11]=0x80 | 64; // 9V
    regs[0x0b]=0x04 | (2<<3);
    PowerManager p;
    p.setup();
    assert(board.charge && board.transitions==1);
    assert(p.getSnapshot().fast_charge);
    regs[0x0b]=0x04 | (3<<3); // charge complete -> IRQ
    for (unsigned i=0; i<15; ++i) {
        cycle(p, i==0, i%2==0);
        assert(board.charge && board.transitions==1);
        assert(p.getChargeState()==PowerChargeState::Full);
        assert(!p.getSnapshot().fast_charge);
    }
    // An event racing a clean publish must defer a new enable, not toggle CE.
    p.notifyChargerIrqFromISR();
    p.setChargingEnabled(true);
    assert(board.transitions==1);
    cycle(p);
    // Different encoded transient faults must not be OR'ed into timer expiry.
    faultHistory=0x10; faultCurrent=0x20;
    cycle(p, true);
    assert(!board.charge && p.charge_recovery_.latchedFault()==0);
    faultHistory=faultCurrent=0;
    for (unsigned i=0; i<10; ++i) { cycle(p); assert(!board.charge); }
    cycle(p); assert(board.charge);
    // Timer fault remains latched across clean reads, then clears on unplug.
    faultHistory=0x30;
    cycle(p,true); assert(!board.charge);
    faultHistory=0;
    for (unsigned i=0;i<12;++i) { cycle(p); assert(!board.charge); }
    regs[0x0b]=regs[0x11]=0;
    cycle(p); assert(p.charge_recovery_.latchedFault()==0x30);
    cycle(p); assert(p.charge_recovery_.latchedFault()==0);
    regs[0x11]=0x80|24; regs[0x0b]=0x04|(2<<3); // 5V, keep 1.5A
    for(unsigned i=0;i<12;++i) cycle(p);
    assert(board.charge && !p.getSnapshot().fast_charge);
    assert(regs[0]==0x5c && regs[4]==0x19 && regs[6]==0x5a);
    assert((regs[2]&0x40) && (regs[3]&0x10));
    // Gauge outage retains display value, never measurement validity.
    gaugeOk=false; cycle(p);
    auto s=p.getSnapshot();
    assert(!s.valid && s.data_quality==PowerDataQuality::Stale && s.soc_permille==500);
    assert(!board.charge);
    now+=10001;
    assert(p.getSnapshot().data_quality==PowerDataQuality::Unknown);
    // Recovery windows tolerate tick wrap, not missing samples.
    ChargeRecoveryPolicy policy;
    policy.record(0x08,0x08);
    policy.sample(UINT32_MAX-500, true, true, false, 0, 0);
    policy.sample(499, true, true, false, 0, 0);
    assert(policy.latchedFault()==0);
    policy = ChargeRecoveryPolicy{};
    policy.record(0x08,0x08);
    policy.sample(1000,true,true,false,0,0);
    policy.sample(5000,true,true,false,0,0);
    assert(policy.latchedFault()==0x08);
    puts("battery manager: full/IRQ/catchup, fault latch, recovery, profiles and stale data passed");
}
