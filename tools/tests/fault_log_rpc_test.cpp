#include "configs/common_command_handler.hpp"
#include "usb_board_link.hpp"
#include "fault_log_runtime.h"
#include <cassert>
#include <cstdio>
static uint8_t flash[8192];
static int txMode, readFailure;
static int readFlash(uint32_t a,void *p,uint32_t n){assert(a+n<=sizeof(flash));if(readFailure)return -1;memcpy(p,flash+a,n);return 0;}
static int writeFlash(uint32_t a,const void *p,uint32_t n){assert(a+n<=sizeof(flash));memcpy(flash+a,p,n);return 0;}
static int eraseFlash(uint32_t a){assert(a+4096<=sizeof(flash));memset(flash+a,255,4096);return 0;}
bool UsbBoardLink::sendControl(usb_board_control_opcode_t op,const uint8_t *request,uint8_t length,
    uint8_t *response,uint8_t capacity,uint8_t *size,uint8_t *status) {
    assert(op==USB_BOARD_CONTROL_FAULT_LOG);
    *status=USB_BOARD_STATUS_OK;
    if(txMode==1){*status=USB_BOARD_STATUS_UNSUPPORTED;return false;}
    if(txMode==2){*status=USB_BOARD_STATUS_NOT_READY;return false;}
    int result=XoraLog_Control(request,length,response,size);
    if(txMode==3 && *size) --*size;
    if(txMode==4 && length && *size==49) response[3]^=1; // CRC rejection
    if(txMode==5 && length && *size==49) response[0]^=1; // offset rejection
    assert(*size<=capacity);
    return result==FL_OK;
}
static DeviceCommandResponse query(const char *json) {
    CommonCommandHandler handler;
    DeviceCommandRequest r;r.setCommand("get_device_logs_page");r.setParams(cJSON_Parse(json));
    return handler.handleGetDeviceLogsPage(r);
}
static unsigned value(cJSON *p,const char *key){return static_cast<unsigned>(cJSON_GetObjectItem(p,key)->valuedouble);}
static bool state(cJSON *p,const char *s){return !strcmp(cJSON_GetObjectItem(p,"status")->valuestring,s);}
int main() {
    memset(flash,255,sizeof(flash));memset(&log_retained,0,sizeof(log_retained));
    fl_backend backend={readFlash,writeFlash,eraseFlash,sizeof(flash)};
    log_start(&backend,1,123,1);
    for(unsigned i=0;i<20;i++){
        fl_event(&log_runtime,i,4,2,3,&i,1);fl_service(&log_runtime,&log_retained,1);
    }
    {auto r=query("{\"source\":\"stm32\"}");assert(r.getErrNo()==0);
     assert(value(r.getData(),"upper")==20 && value(r.getData(),"nextBefore")==13);
     assert(cJSON_GetArraySize(cJSON_GetObjectItem(r.getData(),"items"))==8);}
    {auto r=query("{\"source\":\"stm32\",\"upper\":20,\"oldest\":1,\"before\":13}");
     assert(value(r.getData(),"nextBefore")==5);}
    fl_event(&log_runtime,21,5,2,3,nullptr,0);
    fl_service(&log_runtime,&log_retained,1);
    {auto r=query("{\"source\":\"stm32\",\"upper\":20,\"oldest\":1,\"before\":13}");
     assert(value(r.getData(),"upper")==20 && value(r.getData(),"nextBefore")==5);}
    {auto r=query("{\"source\":\"stm32\",\"category\":3}");
     assert(cJSON_GetArraySize(cJSON_GetObjectItem(r.getData(),"items"))==0);}
    {auto r=query("{\"source\":\"invalid\"}");assert(r.getErrNo()==400);}
    {auto r=query("{\"source\":\"stm32\",\"limit\":9}");assert(r.getErrNo()==400);}
    txMode=1;{auto r=query("{\"source\":\"tx\"}");assert(state(r.getData(),"unsupported"));}
    txMode=2;{auto r=query("{\"source\":\"tx\"}");assert(state(r.getData(),"unavailable"));}
    txMode=3;{auto r=query("{\"source\":\"tx\"}");assert(state(r.getData(),"unsupported"));}
    txMode=0;
    memset(flash,255,sizeof(flash));log_start(&backend,2,456,1);
    fl_event(&log_runtime,20,5,2,3,nullptr,0);
    {auto r=query("{\"source\":\"tx\"}");assert(state(r.getData(),"ok"));
     assert(value(r.getData(),"pending")==1);assert(cJSON_GetArraySize(cJSON_GetObjectItem(r.getData(),"items"))==1);}
    fl_service(&log_runtime,&log_retained,1);
    {auto r=query("{\"source\":\"tx\"}");assert(state(r.getData(),"ok"));
     assert(value(r.getData(),"pending")==0);assert(cJSON_GetArraySize(cJSON_GetObjectItem(r.getData(),"items"))==1);}
    txMode=4;{auto r=query("{\"source\":\"tx\"}");assert(state(r.getData(),"unavailable"));}
    txMode=5;{auto r=query("{\"source\":\"tx\"}");assert(state(r.getData(),"unavailable"));}
    txMode=0;
    readFailure=1;
    fl_event(&log_runtime,22,4,2,3,nullptr,0);
    {auto r=query("{\"source\":\"tx\"}");assert(state(r.getData(),"storage_error"));
     assert(cJSON_GetArraySize(cJSON_GetObjectItem(r.getData(),"items"))==1);}
    readFailure=0;
    for(unsigned i=0;i<70;i++){
        fl_event(&log_runtime,i+30,5,2,3,&i,1);fl_service(&log_runtime,&log_retained,1);
    }
    {auto r=query("{\"source\":\"tx\",\"upper\":1,\"oldest\":1}");
     assert(cJSON_IsTrue(cJSON_GetObjectItem(r.getData(),"overwritten")));}
    log_retained.streak=3;fl_retained_seal(&log_retained);assert(XoraLog_Restricted());
    XoraLog_Tick(31000);assert(XoraLog_Restricted());
    log_retained.streak=1;fl_retained_seal(&log_retained);XoraLog_Tick(31001);assert(!XoraLog_Restricted());
    puts("fault RPC: paging, filters, TX framing, pending records, partial failure and boot guard passed");
}
