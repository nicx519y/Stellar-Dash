#include "configs/common_command_handler.hpp"
#include "fault_log_api.h"
#include "usb_board_link.hpp"
#include <cmath>
#include <cstdio>

namespace {
uint32_t number(cJSON *p, const char *key, uint32_t fallback) {
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(p,key);
    return cJSON_IsNumber(v) && std::isfinite(v->valuedouble) && v->valuedouble>=0 &&
        v->valuedouble<=UINT32_MAX && std::floor(v->valuedouble)==v->valuedouble
        ? static_cast<uint32_t>(v->valuedouble) : fallback;
}
bool txControl(const uint8_t *request,uint8_t length,void *response,uint8_t capacity,
               uint8_t &size,uint8_t &status) {
    return USB_BOARD_LINK.sendControl(USB_BOARD_CONTROL_FAULT_LOG,request,length,
        static_cast<uint8_t *>(response),capacity,&size,&status);
}
uint8_t readRecord(bool tx,uint32_t upper,uint32_t before,uint8_t category,
                   uint8_t severity,uint8_t pending,fl_record &record) {
    if(!tx) return XoraLog_Read(upper,before,category,severity,pending,&record);
    uint8_t request[12]={},reply[49],size=0,status=0;
    memcpy(request,&upper,4);memcpy(request+4,&before,4);
    request[8]=category;request[9]=severity;request[10]=pending;
    for(uint8_t offset=0;offset<sizeof(record);offset+=48) {
        request[11]=offset;
        if(!txControl(request,sizeof(request),reply,sizeof(reply),size,status))
            return FL_IO;
        if(size==1 && reply[0]==0xffu) return FL_FULL;
        if(status!=USB_BOARD_STATUS_OK || size!=49 || reply[0]!=offset) return FL_IO;
        memcpy(reinterpret_cast<uint8_t *>(&record)+offset,reply+1,48);
    }
    if(record.source!=FL_SOURCE_TX || record.words>14 ||
        record.crc!=fl_crc(&record,offsetof(fl_record,crc))) return FL_IO;
    if(!pending && !fl_valid(&record)) return FL_IO;
    return FL_OK;
}
void addRecord(cJSON *items,const fl_record &r) {
    cJSON *j=cJSON_CreateObject(),*data=cJSON_CreateArray();
    cJSON_AddNumberToObject(j,"sequence",r.sequence);
    cJSON_AddNumberToObject(j,"boot",r.boot);
    cJSON_AddNumberToObject(j,"uptimeMs",r.uptime_ms);
    cJSON_AddNumberToObject(j,"firmware",r.firmware);
    cJSON_AddNumberToObject(j,"event",r.event);
    cJSON_AddNumberToObject(j,"severity",r.severity);
    cJSON_AddNumberToObject(j,"category",r.category);
    cJSON_AddNumberToObject(j,"repeat",r.repeat);
    cJSON_AddBoolToObject(j,"pending",(r.flags&FL_PENDING)!=0);
    for(unsigned i=0;i<r.words && i<14;i++) cJSON_AddItemToArray(data,cJSON_CreateNumber(r.data[i]));
    cJSON_AddItemToObject(j,"data",data);
    cJSON_AddItemToArray(items,j);
}
}

DeviceCommandResponse CommonCommandHandler::handleGetDeviceLogsPage(const DeviceCommandRequest &request) {
    cJSON *params=request.getParams();
    const cJSON *source=cJSON_GetObjectItemCaseSensitive(params,"source");
    if(!cJSON_IsString(source) || (strcmp(source->valuestring,"stm32") && strcmp(source->valuestring,"tx")))
        return create_error_response(request.getCid(),request.getCommand(),400,"source must be stm32 or tx");
    for(const char *key: {"category","severity","before","upper","oldest","limit"}) {
        const cJSON *v=cJSON_GetObjectItemCaseSensitive(params,key);
        if(v && (!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble) ||
            v->valuedouble<0 || v->valuedouble>UINT32_MAX || std::floor(v->valuedouble)!=v->valuedouble))
            return create_error_response(request.getCid(),request.getCommand(),400,"log parameters must be unsigned integers");
    }
    const bool tx=!strcmp(source->valuestring,"tx");
    const uint32_t category=number(params,"category",0),severity=number(params,"severity",0);
    uint32_t before=number(params,"before",0);
    uint32_t limit=number(params,"limit",8);
    if(category>3 || (severity && (severity<2 || severity>4)) || !limit || limit>8)
        return create_error_response(request.getCid(),request.getCommand(),400,"invalid log filter or limit");
    fl_info info={};
    const char *state="ok";
    if(tx) {
        uint8_t size=0,status=0xff;
        if(!txControl(nullptr,0,&info,sizeof(info),size,status))
            state=status==USB_BOARD_STATUS_UNSUPPORTED ? "unsupported" : "unavailable";
        else if(size!=sizeof(info) || info.version!=1 || info.pending>3 || info.status>FL_UNSUPPORTED) state="unsupported";
        if(strcmp(state,"ok")) memset(&info,0,sizeof(info));
    } else XoraLog_Info(&info);
    if(!strcmp(state,"ok") && info.status) state="storage_error";
    const bool initial=cJSON_GetObjectItemCaseSensitive(params,"upper")==nullptr;
    const uint32_t upper=number(params,"upper",info.newest);
    const uint32_t oldest=number(params,"oldest",info.oldest);
    cJSON *result=cJSON_CreateObject(),*items=cJSON_CreateArray();
    fl_record record;
    bool more=false;
    if(!strcmp(state,"ok") || !strcmp(state,"storage_error")) {
        if(initial) {
            for(uint8_t i=1;i<=info.pending && i<=3;i++) {
                uint8_t status=readRecord(tx,upper,0,0,0,i,record);
                if(status!=FL_OK) {if(strcmp(state,"storage_error")) state="unavailable";break;}
                if((!category || record.category==category) && (!severity || record.severity==severity))
                    addRecord(items,record);
            }
        }
        for(uint32_t i=0;i<limit;i++) {
            uint8_t status=readRecord(tx,upper,before,category,severity,0,record);
            if(status==FL_FULL) break;
            if(status!=FL_OK) {if(strcmp(state,"storage_error")) state="unavailable";break;}
            addRecord(items,record);before=record.sequence;
            if(i+1==limit) {
                status=readRecord(tx,upper,before,category,severity,0,record);
                more=status==FL_OK;
                if(status!=FL_OK && status!=FL_FULL && strcmp(state,"storage_error")) state="unavailable";
            }
        }
    }
    cJSON_AddNumberToObject(result,"version",1);
    cJSON_AddStringToObject(result,"source",source->valuestring);
    cJSON_AddStringToObject(result,"status",state);
    cJSON_AddNumberToObject(result,"upper",upper);
    cJSON_AddNumberToObject(result,"oldest",oldest);
    cJSON_AddNumberToObject(result,"nextBefore",more ? before : 0);
    cJSON_AddBoolToObject(result,"more",more);
    cJSON_AddBoolToObject(result,"overwritten",oldest && info.oldest>oldest);
    cJSON_AddNumberToObject(result,"pending",info.pending);
    cJSON_AddNumberToObject(result,"dropped",info.dropped);
    cJSON_AddNumberToObject(result,"firmware",info.firmware);
    cJSON_AddBoolToObject(result,"restricted",info.restricted!=0);
    cJSON_AddItemToObject(result,"items",items);
    return create_success_response(request.getCid(),request.getCommand(),result);
}

DeviceCommandResponse CommonCommandHandler::handleGetDeviceLogsList(const DeviceCommandRequest &request) {
    cJSON *result=cJSON_CreateObject(),*items=cJSON_CreateArray();
    fl_info info; XoraLog_Info(&info);
    fl_record r;uint32_t before=0,count=0;
    while(count<50 && XoraLog_Read(info.newest,before,0,0,0,&r)==FL_OK) {
        char text[160];
        std::snprintf(text,sizeof(text),"[%s] STM32 boot=%lu +%lums event=%u repeat=%lu detail=0x%08lX",
            r.severity==FL_FATAL?"FATAL":r.severity==FL_ERROR?"ERROR":"WARN",
            (unsigned long)r.boot,(unsigned long)r.uptime_ms,r.event,
            (unsigned long)r.repeat,(unsigned long)r.data[0]);
        cJSON_AddItemToArray(items,cJSON_CreateString(text));before=r.sequence;++count;
    }
    cJSON_AddItemToObject(result,"items",items);cJSON_AddNumberToObject(result,"count",count);
    return create_success_response(request.getCid(),request.getCommand(),result);
}
