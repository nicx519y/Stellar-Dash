#include <algorithm>
#include <cstring>

#include "contract_recording.hpp"
#include "storagemanager.hpp"
#include "board_mode.hpp"
#include "usb_board_link.hpp"
#include "usbdriver.hpp"
#include "adc_btns/adc_calibration.hpp"
#include "adc_btns/adc_manager.hpp"
#include "adc_btns/adc_btns_marker.hpp"
#include "adc_btns/adc_btns_worker.hpp"
#include "states/input_state.hpp"
#include "configs/webconfig_btns_manager.hpp"
#include "configs/webconfig_leds_manager.hpp"
#include "firmware/firmware_manager.hpp"
#include "ch585_firmware_update.hpp"
#include "config_transport_sink.hpp"
#include "qspi-w25q64.h"

DeviceCommandContractRecording g_deviceCommandContractRecording;
bool g_has_led_around = true;

extern "C" uint32_t HAL_GetTick(void) { static uint32_t tick = 1000u; return tick += 10u; }
extern "C" void HAL_Delay(uint32_t) {}

static void initProfile(GamepadProfile &profile, const char *id, const char *name)
{
    memset(&profile, 0, sizeof(profile));
    strncpy(profile.id, id, sizeof(profile.id) - 1u);
    strncpy(profile.name, name, sizeof(profile.name) - 1u);
    profile.enabled = true;
    profile.ledsConfigs.ledEnabled = true;
    profile.ledsConfigs.ledBrightness = 50u;
    profile.ledsConfigs.ledAnimationSpeed = 1u;
    profile.ledsConfigs.aroundLedAnimationSpeed = 1u;
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; ++i) {
        profile.keysConfig.keysEnableTag[i] = true;
        profile.triggerConfigs.triggerConfigs[i].virtualPin = i;
        profile.triggerConfigs.triggerConfigs[i].pressAccuracy = 0.2f;
        profile.triggerConfigs.triggerConfigs[i].releaseAccuracy = 0.2f;
        profile.triggerConfigs.triggerConfigs[i].topDeadzone = 0.2f;
        profile.triggerConfigs.triggerConfigs[i].bottomDeadzone = 0.2f;
    }
}

void Storage::initConfig()
{
    memset(&config, 0, sizeof(config));
    config.version = 1u;
    config.bootMode = BOOT_MODE_WEB_CONFIG;
    config.inputMode = INPUT_MODE_XINPUT;
    config.connectionMode = CONNECTION_MODE_USB;
    config.wirelessReportRate = RFM_RATE_1K;
    config.numProfilesMax = NUM_PROFILES;
    config.power.wakeHoldMs = 1000u;
    config.power.autoStandbyMs = 300000u;
    config.screenControl.brightness = 50u;
    config.screenControl.screenStyle = SCREEN_STYLE_DARK;
    config.screenControl.featuresMask = (1u << SCREEN_FEATURE_COUNT) - 1u;
    for (uint8_t i = 0u; i < SCREEN_FEATURE_COUNT; ++i) config.screenControl.featuresOrder[i] = i;
    initProfile(config.profiles[0], "profile-0", "Default");
    initProfile(config.profiles[1], "profile-1", "Alternate");
    strncpy(config.defaultProfileId, config.profiles[0].id, sizeof(config.defaultProfileId) - 1u);
}
bool Storage::saveConfig() { ++g_deviceCommandContractRecording.storageSaves; return true; }
bool Storage::resetConfig() { initConfig(); return true; }
void Storage::setInputMode(InputMode mode) { config.inputMode = mode; }
void Storage::setConnectionMode(ConnectionMode mode) { config.connectionMode = mode; }
void Storage::setWirelessReportRate(WirelessReportRate rate) { config.wirelessReportRate = rate; }
void Storage::setRfPowerStateHint(uint8_t hint) { config.reservedConnection0 = hint; }
GamepadProfile *Storage::getGamepadProfile(char *id)
{
    if (!id) return nullptr;
    for (auto &profile : config.profiles) if (strcmp(profile.id, id) == 0) return &profile;
    return nullptr;
}
bool Storage::setDefaultProfileId(const char *id)
{
    if (!id || !getGamepadProfile(const_cast<char *>(id))) return false;
    strncpy(config.defaultProfileId, id, sizeof(config.defaultProfileId) - 1u);
    return true;
}
void Storage::registerDefaultProfileChangedCallback(DefaultProfileChangedCallback) {}
void Storage::setBootMode(BootMode mode) { config.bootMode = mode; }

namespace ConfigUtils {
const char *getInputModeString(InputMode mode) {
    switch (mode) { case INPUT_MODE_PS4: return "PS4"; case INPUT_MODE_PS5: return "PS5"; case INPUT_MODE_SWITCH: return "SWITCH"; case INPUT_MODE_XBOX: return "XBOX"; default: return "XINPUT"; }
}
InputMode getInputModeFromString(const char *s) {
    if (!s) return INPUT_MODE_XINPUT; if (!strcmp(s,"PS4")) return INPUT_MODE_PS4; if (!strcmp(s,"PS5")) return INPUT_MODE_PS5; if (!strcmp(s,"SWITCH")) return INPUT_MODE_SWITCH; if (!strcmp(s,"XBOX")) return INPUT_MODE_XBOX; return INPUT_MODE_XINPUT;
}
const char *getConnectionModeString(ConnectionMode m) { return m == CONNECTION_MODE_RF24G ? "RF24G" : "USB"; }
ConnectionMode getConnectionModeFromString(const char *s) { return s && !strcmp(s,"RF24G") ? CONNECTION_MODE_RF24G : CONNECTION_MODE_USB; }
const char *getWirelessReportRateString(WirelessReportRate r) { switch (r) { case RFM_RATE_2K:return "2K"; case RFM_RATE_4K:return "4K"; case RFM_RATE_8K:return "8K"; default:return "1K"; } }
WirelessReportRate getWirelessReportRateFromString(const char *s) { if(s&&!strcmp(s,"2K"))return RFM_RATE_2K;if(s&&!strcmp(s,"4K"))return RFM_RATE_4K;if(s&&!strcmp(s,"8K"))return RFM_RATE_8K;return RFM_RATE_1K; }
uint16_t getWirelessReportRateHz(WirelessReportRate r) { return static_cast<uint16_t>(r); }
const char *getScreenStyleString(uint8_t style) { return style == SCREEN_STYLE_LIGHT ? "LIGHT" : "DARK"; }
uint8_t getScreenStyleFromString(const char *s) { return s && !strcmp(s,"LIGHT") ? SCREEN_STYLE_LIGHT : SCREEN_STYLE_DARK; }
const char *getGamepadHotkeyString(GamepadHotkey action) { return action == HOTKEY_SYSTEM_REBOOT ? "SYSTEM_REBOOT" : "NONE"; }
GamepadHotkey getGamepadHotkeyFromString(const char *s) { return s && !strcmp(s,"SYSTEM_REBOOT") ? HOTKEY_SYSTEM_REBOOT : HOTKEY_NONE; }
void makeDefaultProfile(GamepadProfile &profile, const char *id, bool enabled) { const char *resolved=(id&&id[0])?id:"profile-new";initProfile(profile,resolved,resolved);profile.enabled=enabled; }
cJSON *buildHotkeysConfigJSON(Config &config) {
    cJSON *a=cJSON_CreateArray(); for(uint8_t i=0;i<NUM_GAMEPAD_HOTKEYS;++i){cJSON *o=cJSON_CreateObject();cJSON_AddNumberToObject(o,"key",config.hotkeys[i].virtualPin);cJSON_AddStringToObject(o,"action",getGamepadHotkeyString(config.hotkeys[i].action));cJSON_AddBoolToObject(o,"isHold",config.hotkeys[i].isHold);cJSON_AddBoolToObject(o,"isLocked",config.hotkeys[i].isLocked);cJSON_AddItemToArray(a,o);} return a;
}
cJSON *buildScreenControlConfigJSON(Config &config) { cJSON *o=cJSON_CreateObject();cJSON_AddNumberToObject(o,"brightness",config.screenControl.brightness);cJSON_AddStringToObject(o,"screenStyle",getScreenStyleString(config.screenControl.screenStyle));cJSON_AddNumberToObject(o,"featuresMask",config.screenControl.featuresMask);return o; }
cJSON *toJSON(Config &config) { cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"inputMode",getInputModeString(config.inputMode));cJSON_AddStringToObject(o,"connectionMode",getConnectionModeString(config.connectionMode));cJSON_AddItemToObject(o,"hotkeysConfig",buildHotkeysConfigJSON(config));cJSON_AddItemToObject(o,"screenControl",buildScreenControlConfigJSON(config));return o; }
bool fromJSON(Config &config, cJSON *json) { if(!cJSON_IsObject(json))return false;cJSON *m=cJSON_GetObjectItemCaseSensitive(json,"inputMode");if(cJSON_IsString(m))config.inputMode=getInputModeFromString(m->valuestring);return true; }
bool load(Config &config){(void)config;return true;} bool save(Config &config){(void)config;++g_deviceCommandContractRecording.storageSaves;return true;} bool reset(Config &config){(void)config;return true;} bool fromStorage(Config &config){(void)config;return true;}
}

BoardModeManager &BoardModeManager::getInstance(){static BoardModeManager i;return i;}
UsbBoardLink &UsbBoardLink::getInstance(){static UsbBoardLink i;return i;}
void UsbBoardLink::setForContractTest(usb_board_role_t role,bool compatible){role_=role;compatible_=compatible;memset(&caps_,0,sizeof(caps_));}
uint16_t USBDriver::effectiveReportRateHz(InputMode, uint16_t requestedRateHz) const{return requestedRateHz;}
UsbReportRateLimit USBDriver::reportRateLimit(InputMode, uint16_t) const{return UsbReportRateLimit::None;}

ADCCalibrationManager &ADCCalibrationManager::getInstance(){static ADCCalibrationManager i;return i;}
ADCBtnsError ADCCalibrationManager::startManualCalibration(){active_=true;++g_deviceCommandContractRecording.calibrationStarts;if(callback_)callback_();return ADCBtnsError::SUCCESS;}
ADCBtnsError ADCCalibrationManager::stopCalibration(){active_=false;++g_deviceCommandContractRecording.calibrationStops;if(callback_)callback_();return ADCBtnsError::SUCCESS;}
ADCBtnsError ADCCalibrationManager::resetAllCalibration(){active_=false;++g_deviceCommandContractRecording.calibrationResets;return ADCBtnsError::SUCCESS;}
void ADCCalibrationManager::setCalibrationStatusChangedCallback(CalibrationStatusChangedCallback cb){callback_=std::move(cb);}
bool ADCCalibrationManager::isCalibrationActive()const{return active_;} bool ADCCalibrationManager::isAllButtonsCalibrated(bool){return true;} uint8_t ADCCalibrationManager::getUncalibratedButtonCount()const{return 0;} uint8_t ADCCalibrationManager::getActiveCalibrationButtonCount()const{return active_?1:0;} CalibrationPhase ADCCalibrationManager::getButtonPhase(uint8_t)const{return active_?CalibrationPhase::TOP_SAMPLING:CalibrationPhase::COMPLETED;} CalibrationLEDColor ADCCalibrationManager::getButtonLEDColor(uint8_t)const{return CalibrationLEDColor::GREEN;} bool ADCCalibrationManager::isButtonCalibrated(uint8_t)const{return true;} ADCBtnsError ADCCalibrationManager::getCalibrationValues(uint8_t,uint16_t&t,uint16_t&b)const{t=100;b=4000;return ADCBtnsError::SUCCESS;}

WebConfigBtnsManager &WebConfigBtnsManager::getInstance(){static WebConfigBtnsManager i;return i;} void WebConfigBtnsManager::setButtonStateChangedCallback(ButtonStateChangedCallback cb){stateCallback_=std::move(cb);} void WebConfigBtnsManager::setButtonPerformanceMonitoringCallback(ButtonPerformanceMonitoringCallback){} void WebConfigBtnsManager::setADCBtnTestCallback(ADCBtnTestCallback){} bool WebConfigBtnsManager::startButtonWorkers(){active_=true;++g_deviceCommandContractRecording.monitorStarts;return true;} void WebConfigBtnsManager::stopButtonWorkers(){active_=false;++g_deviceCommandContractRecording.monitorStops;} bool WebConfigBtnsManager::isActive()const{return active_;} uint8_t WebConfigBtnsManager::getTotalButtonCount()const{return NUM_ADC_BUTTONS+NUM_GPIO_BUTTONS;} void WebConfigBtnsManager::enableTestMode(bool e){testMode_=e;} bool WebConfigBtnsManager::isTestModeEnabled()const{return testMode_;} uint32_t WebConfigBtnsManager::getCurrentMask()const{return 0x5u;} std::vector<uint8_t> WebConfigBtnsManager::buildButtonPerformanceMonitoringBinaryData(){return {};}
WebConfigLedsManager &WebConfigLedsManager::getInstance(){static WebConfigLedsManager i;return i;} void WebConfigLedsManager::applyPreviewConfig(const LEDProfile&){preview_=true;++g_deviceCommandContractRecording.ledPreviews;} void WebConfigLedsManager::clearPreviewConfig(){preview_=false;++g_deviceCommandContractRecording.ledClears;} bool WebConfigLedsManager::isInPreviewMode()const{return preview_;}

ADCManager &ADCManager::getInstance(){static ADCManager i;if(i.mapping_.id[0]=='\0')i.resetForContractTest();return i;} void ADCManager::resetForContractTest(){mapping_={};strcpy(mapping_.id,"mapping-default");strcpy(mapping_.name,"Default");mapping_.length=3;mapping_.step=.1f;mapping_.samplingFrequency=1000;mapping_.originalValues[0]=100;mapping_.originalValues[1]=200;mapping_.originalValues[2]=300;default_="mapping-default";shared_=false;} std::vector<ADCValuesMapping*> ADCManager::getMappingList(){return {&mapping_};} const ADCValuesMapping *ADCManager::getMapping(const char *id)const{return id&&strcmp(id,mapping_.id)==0?&mapping_:nullptr;} ADCBtnsError ADCManager::createADCMapping(const char *name,size_t length,float_t step,std::string *createdId){(void)name;(void)length;(void)step;(void)createdId;return ADCBtnsError::MAPPING_STORAGE_FULL;} ADCBtnsError ADCManager::removeADCMapping(const char *id){return id?ADCBtnsError::SUCCESS:ADCBtnsError::INVALID_PARAMS;} ADCBtnsError ADCManager::renameADCMapping(const char *id,const char *name){if(!id||!name)return ADCBtnsError::INVALID_PARAMS;strncpy(mapping_.name,name,sizeof(mapping_.name)-1);return ADCBtnsError::SUCCESS;} ADCBtnsError ADCManager::setDefaultMapping(const char *id){if(!id)return ADCBtnsError::INVALID_PARAMS;default_=id;return ADCBtnsError::SUCCESS;} std::string ADCManager::getDefaultMapping()const{return default_;} ADCBtnsError ADCManager::installSharedMapping(const ADCValuesMapping&mapping,const char*sha){if(!sha||strlen(sha)!=64u)return ADCBtnsError::INVALID_PARAMS;mapping_=mapping;default_=mapping.id;shared_=true;return ADCBtnsError::SUCCESS;} ADCBtnsError ADCManager::clearSharedMapping(const char*id){if(!shared_||!id||strcmp(id,mapping_.id)!=0)return ADCBtnsError::MAPPING_NOT_FOUND;resetForContractTest();return ADCBtnsError::SUCCESS;}
ADCBtnsMarker &ADCBtnsMarker::getInstance(){static ADCBtnsMarker i;return i;} ADCBtnsError ADCBtnsMarker::setup(const char *id){if(!id)return ADCBtnsError::INVALID_PARAMS;draft_=false;strncpy(step_.id,id,sizeof(step_.id)-1);step_.is_marking=true;return ADCBtnsError::SUCCESS;} ADCBtnsError ADCBtnsMarker::setupDraft(const char*name,size_t length,float_t step){if(!name||length<2u||length>MAX_ADC_VALUES_LENGTH)return ADCBtnsError::INVALID_PARAMS;step_={};draft_=true;strcpy(step_.id,"draft");strncpy(step_.mapping_name,name,sizeof(step_.mapping_name)-1u);step_.length=static_cast<uint8_t>(length);step_.step=step;step_.values.resize(length);step_.noise_values.assign(length,2u);step_.frequency_values.assign(length,1000u);for(size_t i=0;i<length;++i)step_.values[i]=100u+static_cast<uint32_t>(i)*100u;step_.index=static_cast<int16_t>(length-1u);step_.is_completed=true;return ADCBtnsError::SUCCESS;} ADCBtnsError ADCBtnsMarker::step(){if(!step_.is_marking)return ADCBtnsError::NOT_MARKING;++step_.index;return ADCBtnsError::SUCCESS;} ADCBtnsError ADCBtnsMarker::persistProgress(){return step_.is_marking?ADCBtnsError::SUCCESS:ADCBtnsError::NOT_MARKING;} void ADCBtnsMarker::reset(){step_.is_marking=false;} cJSON *ADCBtnsMarker::getStepInfoJSON()const{cJSON*o=cJSON_CreateObject();cJSON_AddStringToObject(o,"id",step_.id);cJSON_AddBoolToObject(o,"isMarking",step_.is_marking);cJSON_AddBoolToObject(o,"isCompleted",step_.is_completed);cJSON_AddNumberToObject(o,"index",step_.index);return o;} ADCBtnsError ADCBtnsMarker::getDraftMapping(ADCValuesMapping&mapping)const{if(!draft_||!step_.is_completed)return ADCBtnsError::NOT_MARKING;mapping={};strncpy(mapping.name,step_.mapping_name,sizeof(mapping.name)-1u);mapping.length=step_.length;mapping.step=step_.step;mapping.samplingNoise=2u;mapping.samplingFrequency=1000u;for(size_t i=0;i<mapping.length;++i)mapping.originalValues[i]=step_.values[i];return ADCBtnsError::SUCCESS;}
ADCBtnsWorker &ADCBtnsWorker::getInstance(){static ADCBtnsWorker i;return i;} ADCBtnsError ADCBtnsWorker::setup(){return ADCBtnsError::SUCCESS;}
InputState &InputState::getInstance(){static InputState i;return i;} bool InputState::suspendInputPipelineForStorage(){const bool was=running_;running_=false;return was;} bool InputState::resumeInputPipelineAfterStorage(bool wasRunning){running_=wasRunning;return running_;}

FirmwareManager *FirmwareManager::GetInstance(){static FirmwareManager i;if(i.metadata_.magic!=FIRMWARE_MAGIC){memset(&i.metadata_,0,sizeof(i.metadata_));i.metadata_.magic=FIRMWARE_MAGIC;i.metadata_.metadata_size=METADATA_STRUCT_SIZE;i.metadata_.target_slot=FIRMWARE_SLOT_A;strcpy(i.metadata_.firmware_version,"contract-test");strcpy(i.metadata_.build_date,"2026-08-14");strcpy(i.metadata_.device_model,DEVICE_MODEL_STRING);}return &i;} const FirmwareMetadata *FirmwareManager::GetCurrentMetadata(){return &metadata_;} bool FirmwareManager::CreateUpgradeSession(const char*,const FirmwareMetadata*){active_=true;++g_deviceCommandContractRecording.firmwareCreates;return true;} bool FirmwareManager::ProcessFirmwareChunk(const char*,const char*,const ChunkData* chunk){++g_deviceCommandContractRecording.firmwareChunks;g_deviceCommandContractRecording.firmwareChunkData=chunk?chunk->data:nullptr;g_deviceCommandContractRecording.firmwareChunkSize=chunk?chunk->chunk_size:0u;return active_;} bool FirmwareManager::CompleteUpgradeSession(const char*){++g_deviceCommandContractRecording.firmwareCompletes;active_=false;return true;} bool FirmwareManager::AbortUpgradeSession(const char*){++g_deviceCommandContractRecording.firmwareAborts;active_=false;return true;} uint32_t FirmwareManager::GetUpgradeProgress(const char*){return active_?50u:100u;} bool FirmwareManager::GetUpgradeStatus(const char*,UpgradeStatus *status,uint32_t *progress) const{if(!active_)return false;if(status)*status=UPGRADE_STATUS_ACTIVE;if(progress)*progress=50u;return true;}
Ch585FirmwareUpdate &Ch585FirmwareUpdate::getInstance(){static Ch585FirmwareUpdate i;return i;} bool Ch585FirmwareUpdate::begin(uint32_t total,const uint8_t[32]){if(!total)return false;total_=total;received_=0;status_=Ch585FirmwareUpdateStatus::Receiving;++g_deviceCommandContractRecording.ch585Begins;return true;} bool Ch585FirmwareUpdate::write(uint32_t offset,const uint8_t*,uint32_t length){if(status_!=Ch585FirmwareUpdateStatus::Receiving||offset!=received_)return false;received_+=length;++g_deviceCommandContractRecording.ch585Writes;return true;} bool Ch585FirmwareUpdate::finalizeAndSchedule(){if(!total_||received_!=total_)return false;status_=Ch585FirmwareUpdateStatus::Scheduled;++g_deviceCommandContractRecording.ch585Completes;return true;} bool Ch585FirmwareUpdate::isPending()const{return status_==Ch585FirmwareUpdateStatus::Scheduled;} bool Ch585FirmwareUpdate::hasFailed()const{return status_==Ch585FirmwareUpdateStatus::Failed;} Ch585FirmwareUpdateStatus Ch585FirmwareUpdate::status()const{return status_;} uint8_t Ch585FirmwareUpdate::progress()const{return total_?static_cast<uint8_t>((received_*100u)/total_):0;} uint32_t Ch585FirmwareUpdate::totalSize()const{return total_;} uint32_t Ch585FirmwareUpdate::receivedSize()const{return received_;}

void ConfigTransport_SetJsonSink(config_json_sink_t){} void ConfigTransport_SetBinarySink(config_binary_sink_t){} void ConfigTransport_PublishJson(const char*,size_t){} void ConfigTransport_PublishBinary(const uint8_t*,size_t){} void ConfigTransport_ReplyBinary(const uint8_t*,size_t){}
extern "C" int8_t QSPI_W25Qxx_ReadBuffer_WithXIPOrNot(uint8_t *buffer,uint32_t,uint32_t length){memset(buffer,0,length);return QSPI_W25Qxx_OK;}

void sanitize_string_control_chars(char *str, size_t maxLength)
{
    if (str == nullptr) return;
    for (size_t index = 0u; index < maxLength && str[index] != '\0'; ++index) {
        if (str[index] < 32 && str[index] != ' ' && str[index] != '\n' &&
            str[index] != '\r' && str[index] != '\t') {
            str[index] = '_';
        }
    }
}
