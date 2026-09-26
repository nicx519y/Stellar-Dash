#include "board_power.hpp"

#include "board_cfg.h"
#include "stm32h7xx_hal.h"

namespace {

static void writePin(GPIO_TypeDef* port, uint16_t pin, bool high)
{
    HAL_GPIO_WritePin(port, pin, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void configureOutput(uint16_t pin)
{
    GPIO_InitTypeDef init = {};
    init.Pin = pin;
    init.Mode = GPIO_MODE_OUTPUT_PP;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOI, &init);
}

static void configureInput(GPIO_TypeDef* port, uint16_t pin, uint32_t pull)
{
    GPIO_InitTypeDef init = {};
    init.Pin = pin;
    init.Mode = GPIO_MODE_INPUT;
    init.Pull = pull;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(port, &init);
}

} // namespace

extern "C" void BoardPower_EarlyMainHold(void)
{
    __HAL_RCC_GPIOI_CLK_ENABLE();
    __DSB();

    /* Set ODR before switching MODER to output, so 3V3_Main never glitches. */
    GPIOI->BSRR = MAIN_POWER_EN_PIN;
    const uint32_t shift = 4u * 2u;
    GPIOI->MODER = (GPIOI->MODER & ~(3u << shift)) | (1u << shift);
    GPIOI->OTYPER &= ~static_cast<uint32_t>(MAIN_POWER_EN_PIN);
    GPIOI->PUPDR &= ~(3u << shift);
    __DSB();
}

extern "C" void BoardPower_Initialize(void)
{
    BOARD_POWER.setup();
}

extern "C" void BoardPower_SetChargeEnabled(bool enabled)
{
    BOARD_POWER.setChargeEnabled(enabled);
}

extern "C" void BoardPower_SetHallEnabled(bool enabled)
{
    BOARD_POWER.setHallEnabled(enabled);
}

extern "C" void BoardPower_EnableHallForAdc(void)
{
    BOARD_POWER.setHallEnabled(true);
    HAL_Delay(BOARD_HALL_STABILIZE_MS);
}

extern "C" void BoardPower_SetKeyLedEnabled(bool enabled)
{
    BOARD_POWER.setKeyLedEnabled(enabled);
}

extern "C" void BoardPower_SetAmbientLedEnabled(bool enabled)
{
    BOARD_POWER.setAmbientLedEnabled(enabled);
}

extern "C" void BoardPower_SetLcdEnabled(bool enabled)
{
    BOARD_POWER.setLcdEnabled(enabled);
}

void BoardPower::setup()
{
    __HAL_RCC_GPIOI_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    /*
     * Program the output latches first. PI4 and the local LCD rail stay high;
     * every external optional/high-current load starts disabled. The LCD is
     * the device's recovery UI and must not depend on CH585 role negotiation.
     */
    writePin(CHARGE_EN_N_PORT, CHARGE_EN_N_PIN, true);
    writePin(HALL_VCC_EN_PORT, HALL_VCC_EN_PIN, false);
    writePin(MAIN_POWER_EN_PORT, MAIN_POWER_EN_PIN, true);
    writePin(BOOST_5V_EN_PORT, BOOST_5V_EN_PIN, false);
    writePin(LED_EN_PORT, LED_EN_PIN, false);
    writePin(AMBIENT_EN_PORT, AMBIENT_EN_PIN, false);
    writePin(LCD_EN_PORT, LCD_EN_PIN, true);
    writePin(CH585_EN_PORT, CH585_EN_PIN, false);
    writePin(USB_HOST_EN_PORT, USB_HOST_EN_PIN, false);

    configureOutput(CHARGE_EN_N_PIN | HALL_VCC_EN_PIN | MAIN_POWER_EN_PIN |
                    BOOST_5V_EN_PIN | LED_EN_PIN | AMBIENT_EN_PIN |
                    LCD_EN_PIN | CH585_EN_PIN | USB_HOST_EN_PIN);

    configureInput(IS_FAST_CHARGE_PORT, IS_FAST_CHARGE_PIN, GPIO_PULLUP);
    configureInput(CHARGE_STAT_PORT, CHARGE_STAT_PIN, GPIO_PULLUP);
    configureInput(CHARGE_INT_PORT, CHARGE_INT_PIN, GPIO_PULLUP);
    configureInput(MODE_USB_N_PORT, MODE_USB_N_PIN, GPIO_PULLUP);
    configureInput(MODE_RF_N_PORT, MODE_RF_N_PIN, GPIO_PULLUP);
    configureInput(MAX17048_ALERT_PORT, MAX17048_ALERT_PIN, GPIO_PULLUP);
    configureInput(LCD_CTRL_AUX_PORT, LCD_CTRL_AUX_PIN, GPIO_NOPULL);

    chargeEnabled = false;
    hallEnabled = false;
    ledBoostEnabled = false;
    keyLedEnabled = false;
    ambientLedEnabled = false;
    lcdEnabled = true;
    ch585Enabled = false;
    usbHostEnabled = false;
    safeLatched = true;
    recoveryUiAllowed = false;
    initialized = true;
}

void BoardPower::assertMainPowerHold()
{
    BoardPower_EarlyMainHold();
}

void BoardPower::setChargeEnabled(bool enabled)
{
    /* CE is active-low. */
    writePin(CHARGE_EN_N_PORT, CHARGE_EN_N_PIN, !enabled);
    chargeEnabled = enabled;
}

void BoardPower::setHallEnabled(bool enabled)
{
    writePin(HALL_VCC_EN_PORT, HALL_VCC_EN_PIN, enabled);
    hallEnabled = enabled;
}

void BoardPower::setLedBoostEnabled(bool enabled)
{
    if (ledBoostEnabled == enabled) {
        return;
    }

    if (enabled) {
        writePin(BOOST_5V_EN_PORT, BOOST_5V_EN_PIN, true);
        HAL_Delay(BOARD_LED_5V_STABILIZE_MS);
    } else {
        writePin(BOOST_5V_EN_PORT, BOOST_5V_EN_PIN, false);
    }
    ledBoostEnabled = enabled;
}

void BoardPower::refreshLedBoost()
{
    setLedBoostEnabled(keyLedEnabled || ambientLedEnabled);
}

void BoardPower::setKeyLedEnabled(bool enabled)
{
    if (enabled) {
        setLedBoostEnabled(true);
    }
    writePin(LED_EN_PORT, LED_EN_PIN, enabled);
    keyLedEnabled = enabled;
    if (!enabled) {
        refreshLedBoost();
    }
}

void BoardPower::setAmbientLedEnabled(bool enabled)
{
    if (enabled) {
        setLedBoostEnabled(true);
    }
    writePin(AMBIENT_EN_PORT, AMBIENT_EN_PIN, enabled);
    ambientLedEnabled = enabled;
    if (!enabled) {
        refreshLedBoost();
    }
}

void BoardPower::setLcdEnabled(bool enabled)
{
    writePin(LCD_EN_PORT, LCD_EN_PIN, enabled);
    lcdEnabled = enabled;
}

void BoardPower::setCh585Enabled(bool enabled)
{
    if (!enabled) {
        (void)setUsbHostEnabled(false);
    }
    writePin(CH585_EN_PORT, CH585_EN_PIN, enabled);
    ch585Enabled = enabled;
}

bool BoardPower::setUsbHostEnabled(bool enabled)
{
    if (enabled && !ch585Enabled) {
        writePin(USB_HOST_EN_PORT, USB_HOST_EN_PIN, false);
        usbHostEnabled = false;
        return false;
    }
    writePin(USB_HOST_EN_PORT, USB_HOST_EN_PIN, enabled);
    usbHostEnabled = enabled;
    return true;
}

void BoardPower::enterSafeState()
{
    safeLatched = true;
    recoveryUiAllowed = true;
    (void)setUsbHostEnabled(false);
    setCh585Enabled(false);
    setKeyLedEnabled(false);
    setAmbientLedEnabled(false);
    setLedBoostEnabled(false);
    /* Keep the local UI available even when CH585 or the physical mode gate
     * fails. Only prepareForStandby() is allowed to remove LCD power. */
    setLcdEnabled(true);
    setHallEnabled(false);
    setChargeEnabled(false);
    assertMainPowerHold();
}

void BoardPower::enterRecoveryUiState()
{
    enterSafeState();
    /* Keep the global safe latch asserted while the local screen remains the
     * only enabled recovery UI. */
    recoveryUiAllowed = true;
}

void BoardPower::prepareForStandby()
{
    safeLatched = true;
    recoveryUiAllowed = false;
    (void)setUsbHostEnabled(false);
    setCh585Enabled(false);
    setKeyLedEnabled(false);
    setAmbientLedEnabled(false);
    setLedBoostEnabled(false);
    setLcdEnabled(false);
    setHallEnabled(false);
    assertMainPowerHold();
}

void BoardPower::releaseSafeState()
{
    /*
     * Callers may release the latch only after a physical mode has debounced
     * and the selected CH585 role (when required) has acknowledged.  Optional
     * rails remain off until their owning subsystem explicitly enables them.
     */
    safeLatched = false;
    recoveryUiAllowed = false;
    assertMainPowerHold();
}
