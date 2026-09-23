#ifndef RF_BINDING_PLATFORM_H
#define RF_BINDING_PLATFORM_H
#include "CH58x_common.h"
#include "rf_binding_store.h"
/* Keep the existing RF identity algorithm, including the role discriminator. */
static inline uint32_t rfb_platform_identity(uint8_t receiver) {
    const char *tag=receiver?"HBOX-RF-HOP:RX":"HBOX-RF-HOP:TX";
    uint8_t mac[8] __attribute__((aligned(4)))={0};uint32_t h=2166136261UL;unsigned i;
    if(GetMACAddress(mac)!=0u)return 0u;
    for(i=0;tag[i];i++){h^=(uint8_t)tag[i];h*=16777619UL;}
    for(i=0;i<6;i++){h^=mac[i];h*=16777619UL;}
    /* USB maintenance does not call SetSysClock, so its chip_info BSS may
     * still be zero. Read only the same immutable chip identifier. */
    h=rfh_fnv1a32_mix_u32(h,*(const uint32_t *)ROM_CFG_CHIP_ID);
    return h?h:(receiver?0x52584A31UL:0x54584A31UL);
}
static uint8_t rfb_flash_read(uint32_t a,void *p,uint32_t n){return EEPROM_READ(a,p,n);}
static uint8_t rfb_flash_write(uint32_t a,const void *p,uint32_t n){return EEPROM_WRITE(a,(void*)p,n);}
static uint8_t rfb_flash_erase(uint32_t a,uint32_t n){return EEPROM_ERASE(a,n);}
static const rfh_bond_journal_backend_t rfb_flash={rfb_flash_read,rfb_flash_write,rfb_flash_erase};
#endif
