#include "rfm_spi_port.h"

#include "CH58x_common.h"

#define SPI_PINS                      (GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15)
#define SPI_IRQ_PIN                   (GPIO_Pin_11)
#define SPI_TX_PENDING_RECOVER_US     (50000u)
#define US_TICK_STEP                  (10u)

static uint8_t s_spi_tx_buf[96];
static uint16_t s_spi_tx_len;
static uint16_t s_spi_tx_pos;
static uint8_t s_spi_tx_pending;
static uint32_t s_spi_tx_start_us;
static volatile uint32_t s_now_us;

static void spi_tx_fill_fifo(void)
{
    while ((s_spi_tx_pos < s_spi_tx_len) && (R8_SPI0_FIFO_COUNT < SPI_FIFO_SIZE)) {
        R8_SPI0_FIFO = s_spi_tx_buf[s_spi_tx_pos++];
    }
}

static uint32_t spi_now_us(void) { s_now_us += US_TICK_STEP; return s_now_us; }

void rfm_spi_port_init(void)
{
    GPIOPinRemap(ENABLE, RB_PIN_SPI0);
    GPIOADigitalCfg(ENABLE, (uint16_t)0xFFFFu);
    GPIOBDigitalCfg(ENABLE, SPI_PINS | SPI_IRQ_PIN);
    GPIOB_ModeCfg(SPI_IRQ_PIN, GPIO_ModeOut_PP_5mA);
    GPIOB_ResetBits(SPI_IRQ_PIN);

    GPIOB_ModeCfg(GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14, GPIO_ModeIN_PU);
    GPIOB_ModeCfg(GPIO_Pin_15, GPIO_ModeOut_PP_5mA);

    SPI0_SlaveInit();
    R8_SPI0_CTRL_MOD |= RB_SPI_FIFO_DIR;

    s_spi_tx_pending = 0u;
    s_spi_tx_start_us = 0u;
    s_spi_tx_len = 0u;
    s_spi_tx_pos = 0u;
    s_now_us = 0u;
}

void rfm_spi_port_set_irq(bool asserted)
{
    if (asserted) {
        GPIOB_SetBits(SPI_IRQ_PIN);
    } else {
        GPIOB_ResetBits(SPI_IRQ_PIN);
    }
}

bool rfm_spi_port_try_read(uint8_t *buf, size_t *inout_len)
{
    size_t max_len;
    size_t n = 0u;
    size_t expected;
    uint32_t wait_deadline_us;

    if ((buf == 0) || (inout_len == 0)) {
        return false;
    }

    max_len = *inout_len;
    if (max_len < 4u) {
        return false;
    }

    if (s_spi_tx_pending != 0u) {
        spi_tx_fill_fifo();
        if ((R8_SPI0_INT_FLAG & RB_SPI_IF_CNT_END) == 0u) {
            if (GPIOB_ReadPortPin(GPIO_Pin_12) &&
                ((int32_t)(spi_now_us() - (s_spi_tx_start_us + SPI_TX_PENDING_RECOVER_US)) >= 0)) {
                s_spi_tx_pending = 0u;
                rfm_spi_port_set_irq(false);
                while (R8_SPI0_FIFO_COUNT != 0u) {
                    (void)R8_SPI0_FIFO;
                }
                R8_SPI0_CTRL_MOD |= RB_SPI_FIFO_DIR;
            } else {
                return false;
            }
        } else {
            R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END;
            s_spi_tx_pending = 0u;
            /* Response transaction completed, deassert IRQ to prepare next edge. */
            rfm_spi_port_set_irq(false);
        }
    }

    R8_SPI0_CTRL_MOD |= RB_SPI_FIFO_DIR;
    if (R8_SPI0_FIFO_COUNT == 0u) {
        return false;
    }

    wait_deadline_us = spi_now_us() + 300u;
    while (n < 3u) {
        if (R8_SPI0_FIFO_COUNT != 0u) {
            buf[n++] = R8_SPI0_FIFO;
            wait_deadline_us = spi_now_us() + 300u;
            continue;
        }
        if ((int32_t)(spi_now_us() - wait_deadline_us) >= 0) {
            return false;
        }
    }

    expected = (size_t)(3u + buf[2] + 1u);
    if ((expected < 4u) || (expected > max_len)) {
        return false;
    }

    wait_deadline_us = spi_now_us() + 300u;
    while (n < expected) {
        if (R8_SPI0_FIFO_COUNT != 0u) {
            buf[n++] = R8_SPI0_FIFO;
            wait_deadline_us = spi_now_us() + 300u;
            continue;
        }
        if ((int32_t)(spi_now_us() - wait_deadline_us) >= 0) {
            return false;
        }
    }

    while (R8_SPI0_FIFO_COUNT != 0u) {
        (void)R8_SPI0_FIFO;
    }

    *inout_len = n;
    return true;
}

bool rfm_spi_port_try_write(const uint8_t *buf, size_t len)
{
    size_t i;

    if ((buf == 0) || (len == 0u) || (len > 4095u) || (len > sizeof(s_spi_tx_buf))) {
        return false;
    }

    R8_SPI0_CTRL_MOD &= (uint8_t)(~RB_SPI_FIFO_DIR);
    R16_SPI0_TOTAL_CNT = (uint16_t)len;
    R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END;

    while (R8_SPI0_FIFO_COUNT != 0u) {
        (void)R8_SPI0_FIFO;
    }

    for (i = 0u; i < len; ++i) {
        s_spi_tx_buf[i] = buf[i];
    }
    s_spi_tx_len = (uint16_t)len;
    s_spi_tx_pos = 0u;
    spi_tx_fill_fifo();
    s_spi_tx_pending = 1u;
    s_spi_tx_start_us = spi_now_us();
    return true;
}
