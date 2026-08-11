#include "board_role_selector.h"

#include <stdint.h>

#include "CH58x_common.h"
#include "board_latest_ch585.h"
#include "usb_board_link_codec.h"

#define ROLE_SELECT_TIMEOUT_MS       1000u
#define ROLE_RESPONSE_TIMEOUT_MS      500u
#define ROLE_RELEASE_GAP_US          1000u
/* The STM32 bootstrap request is only five bytes; poll fast enough to drain
 * the FIFO while that short NSS transaction is still active. */
#define ROLE_POLL_STEP_US               1u

static void selector_receive_start(void)
{
    rfm_board_latest_ch585_prepare_spi_pins();
    SPI0_SlaveInit();
    R8_SPI0_CTRL_MOD =
        (uint8_t)((R8_SPI0_CTRL_MOD | RB_SPI_FIFO_DIR) &
                  (uint8_t)~RB_SPI_SLV_CMD_MOD);
    R8_SPI0_CTRL_CFG &=
        (uint8_t)~(RB_SPI_DMA_ENABLE | RB_SPI_DMA_LOOP);
    R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END | RB_SPI_IF_DMA_END |
                       RB_SPI_IF_FIFO_OV | RB_SPI_IF_FIFO_HF |
                       RB_SPI_IF_BYTE_END | RB_SPI_IF_FST_BYTE;
    while(R8_SPI0_FIFO_COUNT != 0u)
    {
        (void)R8_SPI0_FIFO;
    }
}

static bool selector_wait_nss_idle(uint32_t timeout_ms)
{
    uint32_t elapsed_us = 0u;
    const uint32_t timeout_us = timeout_ms * 1000u;

    while(!rfm_board_latest_ch585_nss_high())
    {
        if(elapsed_us >= timeout_us)
        {
            return false;
        }
        DelayUs(ROLE_POLL_STEP_US);
        elapsed_us += ROLE_POLL_STEP_US;
    }
    return true;
}

static bool selector_wait_response_transaction(uint32_t timeout_ms)
{
    bool saw_nss_low = false;
    uint32_t elapsed_us = 0u;
    const uint32_t timeout_us = timeout_ms * 1000u;

    while(elapsed_us < timeout_us)
    {
        if(!rfm_board_latest_ch585_nss_high())
        {
            saw_nss_low = true;
        }
        else if(saw_nss_low)
        {
            return true;
        }
        DelayUs(ROLE_POLL_STEP_US);
        elapsed_us += ROLE_POLL_STEP_US;
    }
    return false;
}

static bool selector_send_role_result(usb_board_role_t role,
                                      usb_board_status_t status)
{
    usb_board_role_selected_v1_t result;
    uint8_t response[USB_BOARD_LINK_MAX_FRAME_BYTES];
    uint8_t response_length = 0u;
    uint8_t index;

    result.role = (uint8_t)role;
    result.status = (uint8_t)status;
    if(!usb_board_link_encode(USB_BOARD_EVT_ROLE_SELECTED,
                              &result,
                              (uint8_t)sizeof(result),
                              response,
                              (uint8_t)sizeof(response),
                              &response_length))
    {
        return false;
    }
    if(!selector_wait_nss_idle(ROLE_RESPONSE_TIMEOUT_MS))
    {
        return false;
    }

    R8_SPI0_CTRL_CFG &=
        (uint8_t)~(RB_SPI_DMA_ENABLE | RB_SPI_DMA_LOOP);
    R8_SPI0_CTRL_MOD &= (uint8_t)~RB_SPI_FIFO_DIR;
    R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END | RB_SPI_IF_DMA_END |
                       RB_SPI_IF_FIFO_OV | RB_SPI_IF_FIFO_HF |
                       RB_SPI_IF_BYTE_END | RB_SPI_IF_FST_BYTE;
    while(R8_SPI0_FIFO_COUNT != 0u)
    {
        (void)R8_SPI0_FIFO;
    }

    R16_SPI0_TOTAL_CNT = response_length;
    for(index = 0u; index < response_length; ++index)
    {
        R8_SPI0_FIFO = response[index];
    }
    rfm_board_latest_ch585_set_w_int(true);

    if(!selector_wait_response_transaction(ROLE_RESPONSE_TIMEOUT_MS))
    {
        rfm_board_latest_ch585_set_w_int(false);
        selector_receive_start();
        return false;
    }

    R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END;
    rfm_board_latest_ch585_set_w_int(false);
    /* Let STM32 observe the released event line before the USB subsystem can
     * queue its initial USB_STATE event and assert W_INT again. */
    DelayUs(ROLE_RELEASE_GAP_US);
    return true;
}

static bool selector_role_valid(uint8_t role)
{
    return (role == USB_BOARD_ROLE_RF) ||
           (role == USB_BOARD_ROLE_USB) ||
           (role == USB_BOARD_ROLE_MAINTENANCE);
}

bool rfm_board_role_selector_wait(usb_board_role_t *selected_role)
{
    usb_board_link_parser_t parser;
    usb_board_link_frame_t frame;
    uint32_t elapsed_us = 0u;

    if(selected_role == (usb_board_role_t *)0)
    {
        return false;
    }
    *selected_role = USB_BOARD_ROLE_NONE;
    usb_board_link_parser_init(&parser);
    selector_receive_start();

    while(elapsed_us < (ROLE_SELECT_TIMEOUT_MS * 1000u))
    {
        while(R8_SPI0_FIFO_COUNT != 0u)
        {
            uint8_t byte = R8_SPI0_FIFO;

            if(usb_board_link_parser_feed(&parser, byte, &frame))
            {
                usb_board_status_t status = USB_BOARD_STATUS_OK;
                usb_board_role_t role = USB_BOARD_ROLE_NONE;

                if(frame.command != USB_BOARD_CMD_SELECT_ROLE)
                {
                    continue;
                }
                if(frame.length != sizeof(usb_board_role_select_v1_t))
                {
                    status = USB_BOARD_STATUS_BAD_LENGTH;
                }
                else
                {
                    role = (usb_board_role_t)frame.payload[0];
                    if(!selector_role_valid((uint8_t)role))
                    {
                        status = USB_BOARD_STATUS_BAD_ROLE;
                        role = USB_BOARD_ROLE_NONE;
                    }
                }

                if(selector_send_role_result(role, status) &&
                   (status == USB_BOARD_STATUS_OK))
                {
                    *selected_role = role;
                    rfm_board_latest_ch585_stop_spi();
                    return true;
                }

                usb_board_link_parser_init(&parser);
                selector_receive_start();
            }
        }

        if((R8_SPI0_INT_FLAG & RB_SPI_IF_FIFO_OV) != 0u)
        {
            R8_SPI0_INT_FLAG = RB_SPI_IF_FIFO_OV;
            usb_board_link_parser_init(&parser);
            selector_receive_start();
        }
        DelayUs(ROLE_POLL_STEP_US);
        elapsed_us += ROLE_POLL_STEP_US;
    }

    rfm_board_latest_ch585_stop_spi();
    return false;
}
