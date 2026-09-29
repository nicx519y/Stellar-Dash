#ifndef BOARD_LATEST_CH585_H
#define BOARD_LATEST_CH585_H

#include <stdbool.h>
#include <stdint.h>

#include "CH58x_common.h"

#define RFM_BOARD_SPI_NSS_PIN        GPIO_Pin_12
#define RFM_BOARD_SPI_SCK_PIN        GPIO_Pin_13
#define RFM_BOARD_SPI_MOSI_PIN       GPIO_Pin_14
#define RFM_BOARD_SPI_MISO_PIN       GPIO_Pin_15
#define RFM_BOARD_SPI_PIN_MASK       \
    (RFM_BOARD_SPI_NSS_PIN | RFM_BOARD_SPI_SCK_PIN | \
     RFM_BOARD_SPI_MOSI_PIN | RFM_BOARD_SPI_MISO_PIN)

#define RFM_BOARD_W_INT_PIN          GPIO_Pin_5
#define RFM_BOARD_W_INT_ACTIVE_LOW   1u
#define RFM_BOARD_BOOT_READY_PULSE_MS 100u
#define RFM_BOARD_HAS_DEBUG_UART0    0u

void rfm_board_latest_ch585_prepare_spi_pins(void);
void rfm_board_latest_ch585_prepare_sleep_pins(void);
void rfm_board_latest_ch585_stop_spi(void);
bool rfm_board_latest_ch585_nss_high(void);
bool rfm_board_latest_ch585_wake_high(void);
void rfm_board_latest_ch585_set_w_int(bool asserted);
void rfm_board_latest_ch585_pulse_boot_ready(void);
void rfm_board_latest_ch585_set_usb_spi_owner(bool enabled);
bool rfm_board_latest_ch585_usb_spi_owner(void);

#endif
