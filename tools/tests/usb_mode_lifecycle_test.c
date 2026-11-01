#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "usb_device.h"
#include "usb_auth.h"
#include "usb_mode_memory.h"
#include "usb_webhid_memory.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static bool active,auth_ready;
static unsigned starts,stops;
void usb_device_hw_shutdown(void){active=false;stops++;}
bool usb_device_hw_init(usb_board_profile_t p){CHECK(!active);CHECK(usb_mode_memory_is_web()==(p==USB_BOARD_PROFILE_WEB_CONFIG));if(p==USB_BOARD_PROFILE_PS5_COMPAT)CHECK(auth_ready);active=true;starts++;return true;}
void usb_auth_clear(void){CHECK(!active);auth_ready=false;}
bool usb_auth_begin(usb_auth_scheme_t s,uint8_t transaction){(void)s;(void)transaction;CHECK(!active&&!usb_mode_memory_is_web());auth_ready=true;return true;}
int main(void){
 CHECK(usb_device_init(USB_BOARD_PROFILE_WEB_CONFIG));CHECK(usb_mode_memory_is_web());
 memset(g_usb_mode_memory.webhid_out,0x5a,sizeof(g_usb_mode_memory.webhid_out));
 CHECK(usb_device_set_profile(USB_BOARD_PROFILE_WEB_CONFIG));CHECK(starts==1&&g_usb_mode_memory.webhid_out[0][0]==0x5a);
 CHECK(usb_device_set_profile(USB_BOARD_PROFILE_PS5_COMPAT));CHECK(!usb_mode_memory_is_web()&&active&&auth_ready);
 for(unsigned i=0;i<sizeof(g_usb_mode_memory);i++)CHECK(((uint8_t*)&g_usb_mode_memory)[i]==0);
 CHECK(usb_device_set_profile(USB_BOARD_PROFILE_WEB_CONFIG));CHECK(usb_mode_memory_is_web()&&!auth_ready);
 CHECK(starts==3&&stops>=3);usb_device_shutdown();CHECK(!active);
 puts("PASS production profile transition: stop DMA/IRQ, clear arena, prepare auth, start Device");return 0;
}

volatile uint32_t g_game_log_received,g_game_log_dropped,g_game_log_notices;
volatile uint8_t g_xora_diagnostics_enabled;
void usb_auth_process(void) {}
uint8_t usb_device_monitor_speed(void) {return 1;}
bool usb_monitor_process(uint8_t speed) {(void)speed;return false;}
