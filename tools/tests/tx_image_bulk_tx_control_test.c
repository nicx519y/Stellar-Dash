#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "ch585_iap_protocol.h"
#include "tx_image_bulk.h"
static unsigned flash_reads;
/* Simulated read-only Application memory. Fail on any IAP/protection-region
 * access by the real producer; ordinary header copies remain normal memcpy. */
static void *flash_copy(void *out,const void *in,size_t size){
    uintptr_t address=(uintptr_t)in;
    if(address<0x100000u){
        assert(address>=CH585_IAP_APP_START && address<=CH585_IAP_FLASH_END && size<=CH585_IAP_FLASH_END-address);
        ++flash_reads;
        for(size_t i=0;i<size;++i)((uint8_t*)out)[i]=(uint8_t)(address+i);
        return out;
    }
    return memcpy(out,in,size);
}
#define memcpy flash_copy
#include "../../RF_PHY_Hop/TX/USB/usb_management_control.c"
#undef memcpy
static bool fast_ready=true;
bool usb_webhid_fast_ready(void){return fast_ready;}
uint8_t usb_webhid_fast_control(uint8_t op,const uint8_t *data,uint8_t size,uint8_t *response,uint8_t *length){
    (void)op;(void)data;(void)size;(void)response;*length=0;return USB_BOARD_STATUS_UNSUPPORTED;
}
bool usb_auth_get_snapshot(usb_auth_snapshot_t *snapshot){(void)snapshot;return false;}
static void caps(uint8_t status){
    uint8_t request[4]={USB_BOARD_CONTROL_TX_IMAGE_BULK_INFO,77,0,0},response[60],length;
    assert(usb_management_control_handle(request,sizeof(request),response,sizeof(response),&length));
    assert(response[1]==77 && response[2]==status);
    if(status==USB_BOARD_STATUS_OK){assert(length==16 && txb_u32(response+4)==XORA_TX_BULK_MAGIC &&
        txb_u32(response+8)==XORA_TX_BULK_VERSION && txb_u32(response+12)==XORA_TX_BULK_READ_BYTES);}
    else assert(length==4);
}
int main(void){
    assert(CH585_IAP_APP_START==0x1000u && CH585_IAP_APP_CAPACITY==XORA_TX_BACKUP_APP_BYTES);
    uint8_t bytes[XORA_TX_BULK_READ_BYTES];usb_management_control_init();
    assert(!usb_management_control_read_tx_image(0,bytes,sizeof(bytes)));caps(USB_BOARD_STATUS_BAD_ROLE);
    usb_management_control_set_role(USB_BOARD_ROLE_USB);
    assert(!usb_management_control_read_tx_image(0,bytes,sizeof(bytes)));assert(!flash_reads);
    usb_management_control_set_role(USB_BOARD_ROLE_MAINTENANCE);caps(USB_BOARD_STATUS_OK);
    fast_ready=false;caps(USB_BOARD_STATUS_NOT_READY);fast_ready=true;
    assert(!usb_management_control_read_tx_image(UINT32_MAX,bytes,1));
    assert(!usb_management_control_read_tx_image(XORA_TX_BACKUP_APP_BYTES-1,bytes,2));
    assert(!usb_management_control_read_tx_image(0,bytes,XORA_TX_BULK_READ_BYTES+1));
    assert(!usb_management_control_read_tx_image(0,bytes,0));assert(!flash_reads);
    assert(usb_management_control_read_tx_image(0,bytes,sizeof(bytes)));
    for(unsigned i=0;i<sizeof(bytes);++i)assert(bytes[i]==(uint8_t)(CH585_IAP_APP_START+i));
    assert(usb_management_control_read_tx_image(XORA_TX_BACKUP_APP_BYTES-7,bytes,7));
    for(unsigned i=0;i<7;++i)assert(bytes[i]==(uint8_t)(CH585_IAP_FLASH_END-7+i));
    assert(flash_reads==2);
}
