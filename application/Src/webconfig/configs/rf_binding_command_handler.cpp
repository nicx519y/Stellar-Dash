#include "configs/rf_binding_command_handler.hpp"
#include "configs/user_image_command_handler.hpp"
#include "usb_board_link.hpp"
#include "board_mode.hpp"
#include "rf_binding_protocol.h"
#include "adc_btns/adc_calibration.hpp"
#include "firmware/firmware_manager.hpp"
#include "ch585_firmware_update.hpp"
#include <cmath>

namespace {
bool word(cJSON *params,const char *key,uint32_t &value) {
    const cJSON *item=cJSON_GetObjectItemCaseSensitive(params,key);
    if(!cJSON_IsNumber(item)||!std::isfinite(item->valuedouble)||item->valuedouble<0||
       item->valuedouble>4294967295.0||std::floor(item->valuedouble)!=item->valuedouble)return false;
    value=static_cast<uint32_t>(item->valuedouble);return true;
}
}
DeviceCommandResponse RfBindingCommandHandler::handle(const DeviceCommandRequest& request)
{
    const auto &command=request.getCommand();const auto cid=request.getCid();
    auto &link=UsbBoardLink::getInstance();
    if(!BOARD_MODE.isStable()||BOARD_MODE.current()!=BoardMode::Usb||
       link.role()!=USB_BOARD_ROLE_MAINTENANCE||link.profile()!=USB_BOARD_PROFILE_WEB_CONFIG)
        return create_error_response(cid,command,409,"Binding requires USB WebConfig maintenance mode");
    const auto update=CH585_FIRMWARE_UPDATE.status();
    if(ADC_CALIBRATION_MANAGER.isCalibrationActive()||UserImageCommandHandler::isUploadActive()||
       FirmwareManager::GetInstance()->IsUpgradeActive()||
       (update!=Ch585FirmwareUpdateStatus::Idle&&update!=Ch585FirmwareUpdateStatus::Completed&&
        update!=Ch585FirmwareUpdateStatus::Failed))
        return create_error_response(cid,command,409,"Device is busy; finish calibration or firmware update first");
    uint8_t req[RFB_REQUEST_SIZE]={},reply[RFB_RESPONSE_SIZE]={},length=0;
    uint8_t op=RFB_GET_ACTIVE;
    if(command=="prepare_rf_binding")op=RFB_PREPARE;
    else if(command=="commit_rf_binding")op=RFB_COMMIT;
    else if(command=="abort_rf_binding")op=RFB_ABORT;
    else if(command!="get_rf_binding")return create_error_response(cid,command,400,"Unknown binding command");
    cJSON *params=request.getParams();
    if(op==RFB_GET_ACTIVE&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(params,"pending")))op=RFB_GET_PENDING;
    rfb_put(req,RFB_REQUEST_MAGIC);req[4]=RFB_VERSION;req[5]=op;req[6]=cid;req[7]=cid>>8;
    if(op>=RFB_PREPARE) {
        const char *keys[]={"transaction","expectedRevision","peerId","address","generation"};
        unsigned count=op==RFB_PREPARE?5u:2u;
        for(unsigned i=0;i<count;i++) {
            uint32_t v;
            if(!word(params,keys[i],v))return create_error_response(cid,command,400,"Invalid binding parameter");
            rfb_put(req+8+4*i,v);
        }
    }
    rfb_put(req+28,rfb_checksum(req,28));
    uint8_t remoteStatus=USB_BOARD_STATUS_NOT_READY;
    if(!link.sendControl(USB_BOARD_CONTROL_RF_BINDING,req,sizeof(req),reply,sizeof(reply),&length,&remoteStatus)) {
        if(remoteStatus==USB_BOARD_STATUS_UNSUPPORTED)
            return create_error_response(cid,command,501,"BINDING_UNSUPPORTED: update TX firmware");
        return create_error_response(cid,command,503,"TX binding request failed; query state before retrying");
    }
    if(length!=sizeof(reply)||rfb_u32(reply)!=RFB_RESPONSE_MAGIC||reply[4]!=RFB_VERSION||
       reply[5]!=op||reply[6]!=req[6]||reply[7]!=req[7]||rfb_u32(reply+44)!=rfb_checksum(reply,44))
        return create_error_response(cid,command,502,"Invalid TX binding response");
    cJSON *data=cJSON_CreateObject();
    cJSON_AddNumberToObject(data,"version",reply[4]);cJSON_AddNumberToObject(data,"status",reply[8]);
    cJSON_AddNumberToObject(data,"flags",reply[9]);
    const char *names[]={"localId","peerId","address","generation","transaction","revision","capabilities"};
    for(unsigned i=0;i<7;i++)cJSON_AddNumberToObject(data,names[i],rfb_u32(reply+12+4*i));
    return create_success_response(cid,command,data);
}
