#include <cassert>
#include <cstdint>
#include <cstring>
#include "ch585_iap_protocol.h"
static uint8_t flash[CH585_IAP_FLASH_END], journal[256];
static bool s_update_active, s_reset_requested, s_dma_mode;
static uint32_t s_image_size, s_image_crc32, s_next_offset;
static unsigned erases,writes,verifies;
static bool eraseFail,writeFail,verifyFail,metadataFail;
static uint32_t R32_SPI0_DMA_NOW,R32_SPI0_DMA_END;
static uint8_t R8_SPI0_INT_FLAG,R8_SPI0_FIFO_COUNT;
#define RB_SPI_IF_CNT_END 1
#define RB_SPI_IF_FIFO_OV 2
#define EEPROM_PAGE_SIZE 256
unsigned EEPROM_READ(uint32_t address,void* out,uint32_t size) {
    assert(address==0);memcpy(out,journal,size);return 0;
}
unsigned EEPROM_ERASE(uint32_t address,uint32_t size) {
    assert(address==0 && size==sizeof(journal));memset(journal,255,size);return metadataFail;
}
unsigned EEPROM_WRITE(uint32_t address,const void* data,uint32_t size) {
    assert(address==0);memcpy(journal,data,size);return metadataFail;
}
unsigned FLASH_ROM_ERASE(uint32_t address,uint32_t size) {
    assert(address>=CH585_IAP_APP_START && size<=CH585_IAP_FLASH_END-address);
    ++erases;if(eraseFail) return 1;memset(flash+address,255,size);return 0;
}
unsigned FLASH_ROM_WRITE(uint32_t address,const uint32_t* data,uint32_t size) {
    assert(address>=CH585_IAP_APP_START && size<=CH585_IAP_FLASH_END-address);
    ++writes;if(writeFail) return 1;memcpy(flash+address,data,size);return 0;
}
unsigned FLASH_ROM_VERIFY(uint32_t address,const uint32_t* data,uint32_t size) {
    assert(address>=CH585_IAP_APP_START && size<=CH585_IAP_FLASH_END-address);
    ++verifies;return verifyFail || memcmp(flash+address,data,size)!=0;
}
/* PRODUCTION */
static void seal(ch585_iap_dma_packet_t& p) {
    unsigned size=s_dma_mode?1024:64;
    p.magic=CH585_IAP_PROTOCOL_MAGIC;p.version=s_dma_mode?2:1;
    uint32_t crc=crc32_update(~0u,reinterpret_cast<uint8_t*>(&p),size-4)^~0u;
    memcpy(reinterpret_cast<uint8_t*>(&p)+size-4,&crc,4);
}
int main() {
    memset(flash,0x77,sizeof(flash));memset(journal,255,sizeof(journal));
    assert(!update_was_interrupted());
    ch585_iap_dma_packet_t p={};p.command=CH585_IAP_CMD_PROBE;seal(p);
    assert(packet_valid(&p) && handle_packet(&p)==0); // Old host/new loader.
    uint8_t oldImage[40];memset(oldImage,0x35,sizeof(oldImage));
    p.command=CH585_IAP_CMD_BEGIN;p.offset=40;p.value=crc32_update(~0u,oldImage,40)^~0u;seal(p);
    assert(packet_valid(&p) && handle_packet(&p)==0);
    p.command=CH585_IAP_CMD_WRITE;p.offset=0;p.payload_length=40;memcpy(p.data,oldImage,40);seal(p);
    assert(packet_valid(&p) && handle_packet(&p)==0);
    p.command=CH585_IAP_CMD_END;p.payload_length=0;seal(p);
    assert(packet_valid(&p) && handle_packet(&p)==0 && s_reset_requested && !update_was_interrupted());
    // Reset returns to legacy framing before the next image/restore session.
    s_reset_requested=s_update_active=s_dma_mode=false;s_image_size=s_next_offset=0;
    erases=writes=verifies=0;p={};
    p.command=CH585_IAP_CMD_DMA;p.offset=1024;p.value=0;seal(p);
    assert(handle_packet(&p)==CH585_IAP_STATUS_BAD_STATE && !s_dma_mode && !erases);
    p.value=CH585_IAP_DMA_CONTRACT;seal(p);assert(handle_packet(&p)==0 && s_dma_mode);
    assert(!packet_valid(&p)); // Old framing cannot erase after negotiation.
    p={};p.command=CH585_IAP_CMD_WRITE;p.payload_length=1000;seal(p);assert(packet_valid(&p));
    p.data[999]^=1;assert(!packet_valid(&p));p.data[999]^=1;
    p.payload_length=1001;seal(p);assert(!packet_valid(&p));
    p.payload_length=1000;p.reserved=1;seal(p);assert(!packet_valid(&p));p.reserved=0;
    assert(handle_packet(&p)==CH585_IAP_STATUS_BAD_STATE && !writes);
    R32_SPI0_DMA_END=1024;R32_SPI0_DMA_NOW=1023;R8_SPI0_INT_FLAG=RB_SPI_IF_CNT_END;
    assert(!dma_packet_complete());R32_SPI0_DMA_NOW=1024;assert(dma_packet_complete());
    R8_SPI0_FIFO_COUNT=1;assert(!dma_packet_complete());R8_SPI0_FIFO_COUNT=0;
    R8_SPI0_INT_FLAG|=RB_SPI_IF_FIFO_OV;assert(!dma_packet_complete());R8_SPI0_INT_FLAG=0;
    assert(!dma_packet_complete());
    uint8_t image[2004];for(unsigned i=0;i<sizeof(image);++i) image[i]=i;
    p={};p.command=CH585_IAP_CMD_BEGIN;p.offset=CH585_IAP_APP_CAPACITY+4;
    assert(handle_packet(&p)==CH585_IAP_STATUS_BAD_ADDRESS && !erases);
    p.offset=2003;assert(handle_packet(&p)==CH585_IAP_STATUS_BAD_ADDRESS && !erases);
    p.offset=sizeof(image);p.value=crc32_update(~0u,image,sizeof(image))^~0u;
    metadataFail=true;assert(handle_packet(&p)==CH585_IAP_STATUS_METADATA_FAILED && !erases);
    metadataFail=false;eraseFail=true;assert(handle_packet(&p)==CH585_IAP_STATUS_ERASE_FAILED);
    assert(update_was_interrupted());eraseFail=false;assert(handle_packet(&p)==0);
    // A reset sees UPDATING until all data, full CRC and VALID commit succeed.
    assert(update_was_interrupted());p.command=CH585_IAP_CMD_WRITE;p.offset=~0u;p.payload_length=1000;
    assert(handle_packet(&p)==CH585_IAP_STATUS_BAD_ADDRESS && !writes);
    p.offset=0;memcpy(p.data,image,1000);writeFail=true;
    assert(handle_packet(&p)==CH585_IAP_STATUS_WRITE_FAILED && s_next_offset==0);
    writeFail=false;verifyFail=true;
    assert(handle_packet(&p)==CH585_IAP_STATUS_VERIFY_FAILED && s_next_offset==0);
    verifyFail=false;assert(handle_packet(&p)==0 && s_next_offset==1000);
    unsigned before=writes;assert(handle_packet(&p)==0 && writes==before && s_next_offset==1000);
    p.data[0]^=1;assert(handle_packet(&p)==CH585_IAP_STATUS_VERIFY_FAILED && writes==before);p.data[0]^=1;
    p.offset=1000;memcpy(p.data,image+1000,1000);assert(handle_packet(&p)==0);
    p.offset=2000;p.payload_length=4;memcpy(p.data,image+2000,4);assert(handle_packet(&p)==0);
    p.command=CH585_IAP_CMD_END;p.payload_length=0;flash[CH585_IAP_APP_START]^=1;
    assert(handle_packet(&p)==CH585_IAP_STATUS_VERIFY_FAILED && update_was_interrupted() && !s_reset_requested);
    flash[CH585_IAP_APP_START]^=1;metadataFail=true;
    assert(handle_packet(&p)==CH585_IAP_STATUS_METADATA_FAILED && !s_reset_requested);
    metadataFail=false;assert(handle_packet(&p)==0 && !update_was_interrupted() && s_reset_requested);
    for(unsigned i=0;i<CH585_IAP_APP_START;++i) assert(flash[i]==0x77);
    // Full-capacity restoration retains the same upper and lower boundaries.
    p={};p.command=CH585_IAP_CMD_BEGIN;p.offset=CH585_IAP_APP_CAPACITY;assert(handle_packet(&p)==0);
    p.command=CH585_IAP_CMD_WRITE;p.offset=CH585_IAP_APP_CAPACITY-4;p.payload_length=8;
    assert(handle_packet(&p)==CH585_IAP_STATUS_BAD_ADDRESS);
}
