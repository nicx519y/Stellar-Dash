#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "CONFIG.h"
#include "CH58x_common.h"
#include "ch585_iap_protocol.h"
#include "board_latest_ch585.h"

#define IAP_IDLE_BOOT_TIMEOUT_US 500000u
#define IAP_RESPONSE_TIMEOUT_US  500000u
#define IAP_POLL_STEP_US               1u

static bool s_update_active;
static bool s_reset_requested;
static uint32_t s_image_size;
static uint32_t s_image_crc32;
static uint32_t s_next_offset;

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t length)
{
    uint32_t i;
    while(length-- != 0u)
    {
        crc ^= *data++;
        for(i = 0u; i < 8u; ++i)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

static uint8_t crc8_sum(const uint8_t *data, uint8_t length)
{
    uint8_t sum = 0u;
    while(length-- != 0u) sum = (uint8_t)(sum + *data++);
    return sum;
}

static bool metadata_read(ch585_iap_metadata_t *metadata)
{
    memset(metadata, 0, sizeof(*metadata));
    return EEPROM_READ(CH585_IAP_METADATA_EEPROM_ADDR,
                       metadata,
                       sizeof(*metadata)) == 0u;
}

static bool metadata_write(uint8_t state, uint32_t size, uint32_t crc)
{
    ch585_iap_metadata_t metadata __attribute__((aligned(4)));
    memset(&metadata, 0, sizeof(metadata));
    metadata.magic = CH585_IAP_METADATA_MAGIC;
    metadata.state = state;
    metadata.protocol_version = CH585_IAP_PROTOCOL_VERSION;
    metadata.image_size = size;
    metadata.image_crc32 = crc;
    if(EEPROM_ERASE(CH585_IAP_METADATA_EEPROM_ADDR, EEPROM_PAGE_SIZE) != 0u)
    {
        return false;
    }
    return EEPROM_WRITE(CH585_IAP_METADATA_EEPROM_ADDR,
                        &metadata,
                        sizeof(metadata)) == 0u;
}

static bool update_was_interrupted(void)
{
    ch585_iap_metadata_t metadata __attribute__((aligned(4)));
    return metadata_read(&metadata) &&
           metadata.magic == CH585_IAP_METADATA_MAGIC &&
           metadata.state == CH585_IAP_IMAGE_STATE_UPDATING;
}

static void spi_receive_start(void)
{
    rfm_board_latest_ch585_prepare_spi_pins();
    SPI0_SlaveInit();
    R8_SPI0_CTRL_MOD = (uint8_t)((R8_SPI0_CTRL_MOD | RB_SPI_FIFO_DIR) &
                                 (uint8_t)~RB_SPI_SLV_CMD_MOD);
    R8_SPI0_CTRL_CFG &= (uint8_t)~(RB_SPI_DMA_ENABLE | RB_SPI_DMA_LOOP);
    R8_SPI0_INT_FLAG = 0xFFu;
    while(R8_SPI0_FIFO_COUNT != 0u) (void)R8_SPI0_FIFO;
}

static bool wait_nss_high(uint32_t timeout_us)
{
    uint32_t elapsed = 0u;
    while(!rfm_board_latest_ch585_nss_high())
    {
        if(elapsed++ >= timeout_us) return false;
        DelayUs(IAP_POLL_STEP_US);
    }
    return true;
}

static bool send_response(const ch585_iap_packet_t *packet, uint8_t status)
{
    ch585_iap_response_t response;
    const uint8_t *bytes = (const uint8_t *)&response;
    uint8_t i;
    bool saw_low = false;
    uint32_t elapsed = 0u;

    memset(&response, 0, sizeof(response));
    response.magic = CH585_IAP_RESPONSE_MAGIC;
    response.version = CH585_IAP_PROTOCOL_VERSION;
    response.command = packet->command;
    response.sequence = packet->sequence;
    response.status = status;
    response.crc8 = crc8_sum(bytes, (uint8_t)(sizeof(response) - 1u));

    if(!wait_nss_high(IAP_RESPONSE_TIMEOUT_US)) return false;
    R8_SPI0_CTRL_CFG &= (uint8_t)~(RB_SPI_DMA_ENABLE | RB_SPI_DMA_LOOP);
    R8_SPI0_CTRL_MOD &= (uint8_t)~RB_SPI_FIFO_DIR;
    R8_SPI0_INT_FLAG = 0xFFu;
    while(R8_SPI0_FIFO_COUNT != 0u) (void)R8_SPI0_FIFO;
    R16_SPI0_TOTAL_CNT = (uint16_t)sizeof(response);
    for(i = 0u; i < sizeof(response); ++i) R8_SPI0_FIFO = bytes[i];
    rfm_board_latest_ch585_set_w_int(true);

    while(elapsed++ < IAP_RESPONSE_TIMEOUT_US)
    {
        if(!rfm_board_latest_ch585_nss_high()) saw_low = true;
        else if(saw_low) break;
        DelayUs(IAP_POLL_STEP_US);
    }
    rfm_board_latest_ch585_set_w_int(false);
    spi_receive_start();
    return saw_low;
}

static bool packet_valid(const ch585_iap_packet_t *packet)
{
    uint32_t crc;
    if(packet->magic != CH585_IAP_PROTOCOL_MAGIC ||
       packet->version != CH585_IAP_PROTOCOL_VERSION ||
       packet->payload_length > CH585_IAP_DATA_SIZE)
    {
        return false;
    }
    crc = crc32_update(0xFFFFFFFFu,
                       (const uint8_t *)packet,
                       CH585_IAP_PACKET_SIZE - sizeof(packet->packet_crc32));
    return (crc ^ 0xFFFFFFFFu) == packet->packet_crc32;
}

static uint8_t handle_packet(const ch585_iap_packet_t *packet)
{
    uint32_t address;
    uint32_t crc;

    switch(packet->command)
    {
    case CH585_IAP_CMD_PROBE:
        return CH585_IAP_STATUS_OK;

    case CH585_IAP_CMD_BEGIN:
        if(packet->offset == 0u || packet->offset > CH585_IAP_APP_CAPACITY)
            return CH585_IAP_STATUS_BAD_ADDRESS;
        if(!metadata_write(CH585_IAP_IMAGE_STATE_UPDATING,
                           packet->offset,
                           packet->value))
            return CH585_IAP_STATUS_METADATA_FAILED;
        s_update_active = true;
        s_image_size = packet->offset;
        s_image_crc32 = packet->value;
        s_next_offset = 0u;
        if(FLASH_ROM_ERASE(CH585_IAP_APP_START, CH585_IAP_APP_CAPACITY) != 0u)
            return CH585_IAP_STATUS_ERASE_FAILED;
        return CH585_IAP_STATUS_OK;

    case CH585_IAP_CMD_WRITE:
        if(!s_update_active) return CH585_IAP_STATUS_BAD_STATE;
        if(packet->offset != s_next_offset ||
           packet->payload_length == 0u ||
           (packet->payload_length & 3u) != 0u ||
           packet->offset + packet->payload_length > s_image_size)
            return CH585_IAP_STATUS_BAD_ADDRESS;
        address = CH585_IAP_APP_START + packet->offset;
        if(FLASH_ROM_WRITE(address,
                           (uint32_t *)(uintptr_t)packet->data,
                           packet->payload_length) != 0u)
            return CH585_IAP_STATUS_WRITE_FAILED;
        if(FLASH_ROM_VERIFY(address,
                            (uint32_t *)(uintptr_t)packet->data,
                            packet->payload_length) != 0u)
            return CH585_IAP_STATUS_VERIFY_FAILED;
        s_next_offset += packet->payload_length;
        return CH585_IAP_STATUS_OK;

    case CH585_IAP_CMD_END:
        if(!s_update_active || s_next_offset != s_image_size)
            return CH585_IAP_STATUS_BAD_STATE;
        crc = crc32_update(0xFFFFFFFFu,
                           (const uint8_t *)(uintptr_t)CH585_IAP_APP_START,
                           s_image_size) ^ 0xFFFFFFFFu;
        if(crc != s_image_crc32) return CH585_IAP_STATUS_VERIFY_FAILED;
        if(!metadata_write(CH585_IAP_IMAGE_STATE_VALID,
                           s_image_size,
                           s_image_crc32))
            return CH585_IAP_STATUS_METADATA_FAILED;
        s_update_active = false;
        s_reset_requested = true;
        return CH585_IAP_STATUS_OK;

    default:
        return CH585_IAP_STATUS_BAD_COMMAND;
    }
}

static void jump_to_app(void)
{
    uint32_t irq_status;
    rfm_board_latest_ch585_stop_spi();
    SYS_DisableAllIrq(&irq_status);
    ((void (*)(void))(uintptr_t)CH585_IAP_APP_START)();
    for(;;) {}
}

int main(void)
{
    ch585_iap_packet_t packet __attribute__((aligned(4)));
    uint8_t *raw = (uint8_t *)&packet;
    uint16_t received = 0u;
    uint32_t idle_us = 0u;
    const bool interrupted = update_was_interrupted();

    SetSysClock(SYSCLK_FREQ);
    s_update_active = interrupted;
    spi_receive_start();

    for(;;)
    {
        while(R8_SPI0_FIFO_COUNT != 0u)
        {
            uint8_t byte = R8_SPI0_FIFO;
            idle_us = 0u;
            if(received == 0u && byte != (uint8_t)(CH585_IAP_PROTOCOL_MAGIC & 0xFFu))
            {
                if(!interrupted) jump_to_app();
                continue;
            }
            if(received < sizeof(packet)) raw[received++] = byte;
            if(received == sizeof(packet))
            {
                uint8_t status = packet_valid(&packet)
                    ? handle_packet(&packet)
                    : CH585_IAP_STATUS_BAD_PACKET;
                (void)send_response(&packet, status);
                received = 0u;
                if(s_reset_requested)
                {
                    DelayMs(2);
                    SYS_ResetExecute();
                }
            }
        }

        if((R8_SPI0_INT_FLAG & RB_SPI_IF_FIFO_OV) != 0u)
        {
            R8_SPI0_INT_FLAG = RB_SPI_IF_FIFO_OV;
            received = 0u;
            spi_receive_start();
        }

        if(!interrupted && !s_update_active && ++idle_us >= IAP_IDLE_BOOT_TIMEOUT_US)
        {
            jump_to_app();
        }
        DelayUs(IAP_POLL_STEP_US);
    }
}
