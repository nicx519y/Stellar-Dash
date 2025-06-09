#include "configs/webconfig.hpp"
#include "qspi-w25q64.h"
#include "board_cfg.h"
#include "adc_btns/adc_calibration.hpp"
#include "configs/webconfig_btns_manager.hpp"
#include "leds/leds_manager.hpp"
#include "configs/webconfig_leds_manager.hpp"

extern "C" struct fsdata_file file__index_html[];

#define PATH_CGI_ACTION "/cgi/action"

#define LWIP_HTTPD_POST_MAX_PAYLOAD_LEN (1024 * 16)

#define LWIP_HTTPD_RESPONSE_MAX_PAYLOAD_LEN (1024 * 16)

// 定义全局静态映射表，将InputMode映射到对应的字符串
static const std::map<InputMode, const char*> INPUT_MODE_STRINGS = {
    {InputMode::INPUT_MODE_XINPUT, "XINPUT"},
    {InputMode::INPUT_MODE_PS4, "PS4"},
    {InputMode::INPUT_MODE_PS5, "PS5"},
    {InputMode::INPUT_MODE_SWITCH, "SWITCH"}
    // 可以轻松添加更多映射
};

// 从INPUT_MODE_STRINGS自动生成反向映射
static const std::map<std::string, InputMode> STRING_TO_INPUT_MODE = [](){
    std::map<std::string, InputMode> reverse_map;
    for(const auto& pair : INPUT_MODE_STRINGS) {
        reverse_map[pair.second] = pair.first;
    }
    return reverse_map;
}();

// 定义GamepadHotkey字符串到枚举的映射表
static const std::map<std::string, GamepadHotkey> STRING_TO_GAMEPAD_HOTKEY = {
    {"WebConfigMode", GamepadHotkey::HOTKEY_INPUT_MODE_WEBCONFIG},
    {"NSwitchMode", GamepadHotkey::HOTKEY_INPUT_MODE_SWITCH},
    {"XInputMode", GamepadHotkey::HOTKEY_INPUT_MODE_XINPUT},
    {"PS4Mode", GamepadHotkey::HOTKEY_INPUT_MODE_PS4},
    {"PS5Mode", GamepadHotkey::HOTKEY_INPUT_MODE_PS5},
    {"LedsEffectStyleNext", GamepadHotkey::HOTKEY_LEDS_EFFECTSTYLE_NEXT},
    {"LedsEffectStylePrev", GamepadHotkey::HOTKEY_LEDS_EFFECTSTYLE_PREV},
    {"LedsBrightnessUp", GamepadHotkey::HOTKEY_LEDS_BRIGHTNESS_UP},
    {"LedsBrightnessDown", GamepadHotkey::HOTKEY_LEDS_BRIGHTNESS_DOWN},
    {"LedsEnableSwitch", GamepadHotkey::HOTKEY_LEDS_ENABLE_SWITCH},
    {"CalibrationMode", GamepadHotkey::HOTKEY_INPUT_MODE_CALIBRATION},
    {"SystemReboot", GamepadHotkey::HOTKEY_SYSTEM_REBOOT}
};

// 从STRING_TO_GAMEPAD_HOTKEY自动生成反向映射
static const std::map<GamepadHotkey, const char*> GAMEPAD_HOTKEY_TO_STRING = [](){
    std::map<GamepadHotkey, const char*> reverse_map;
    for(const auto& pair : STRING_TO_GAMEPAD_HOTKEY) {
        reverse_map[pair.second] = pair.first.c_str();
    }
    return reverse_map;
}();

using namespace std;

// 处理SPA文件，这些url都指向index.html
const static char* spaPaths[] = {
    "/global",
    "/keys",
    "/leds",
    "/buttons-travel",
    "/switch-marking",
    "/firmware"
};
const static char* excludePaths[] = { "/css", "/images", "/js", "/static" };
static string http_post_uri;
static char http_post_payload[LWIP_HTTPD_POST_MAX_PAYLOAD_LEN];
static uint16_t http_post_payload_len = 0;
// static char http_response[LWIP_HTTPD_RESPONSE_MAX_PAYLOAD_LEN];

const static uint32_t rebootDelayMs = 1000;  // 1秒后重启
static uint32_t rebootTick = 0;  // 用于存储重启时间点
static bool needReboot = false;  // 是否需要重启的标志

void WebConfig::setup() {
    rndis_init();
}

void WebConfig::loop() {
    // rndis http server requires inline functions (non-class)
    rndis_task();

    // 检查是否需要重启
    if (needReboot && (HAL_GetTick() >= rebootTick)) {
        NVIC_SystemReset();
    }
}


enum class HttpStatusCode
{
    _200,
    _400,
    _500,
};

struct DataAndStatusCode
{
    DataAndStatusCode(string&& data, HttpStatusCode statusCode) :
        data(std::move(data)),
        statusCode(statusCode)
    {}

    string data;
    HttpStatusCode statusCode;
};

// **** WEB SERVER Overrides and Special Functionality ****
int set_file_data_with_status_code(fs_file* file, const DataAndStatusCode& dataAndStatusCode)
{
    static string returnData;

    const char* statusCodeStr = "";
    switch (dataAndStatusCode.statusCode)
    {
        case HttpStatusCode::_200: statusCodeStr = "200 OK"; break;
        case HttpStatusCode::_400: statusCodeStr = "400 Bad Request"; break;
        case HttpStatusCode::_500: statusCodeStr = "500 Internal Server Error"; break;
    }

    returnData.clear();
    returnData.append("HTTP/1.1 ");
    returnData.append(statusCodeStr);
    returnData.append("\r\n");
    returnData.append(
        "Server: Ionix-HitBox \r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: keep-alive\r\n"
        "Keep-Alive: timeout=5, max=100\r\n"
        "Content-Length: "
    );
    returnData.append(std::to_string(dataAndStatusCode.data.length()));
    returnData.append("\r\n\r\n");
    returnData.append(dataAndStatusCode.data);

    // printf("returnData: %s\n", returnData.c_str()); 

    file->data = returnData.c_str();
    file->len = returnData.size();
    file->index = file->len;
    file->http_header_included = 1;
    file->pextension = NULL;

    return 1;
}

int set_file_data(fs_file *file, string&& data)
{
    if (data.empty())
        return 0;
    return set_file_data_with_status_code(file, DataAndStatusCode(std::move(data), HttpStatusCode::_200));
}

cJSON* get_post_data()
{
    cJSON* postParams = cJSON_Parse(http_post_payload);   
    char* print_post_params = cJSON_PrintUnformatted(postParams);
    if(print_post_params) {
        // printf("postParams: %s\n", print_post_params);
        free(print_post_params);
    }
    return postParams;                                                                        
}

// LWIP callback on HTTP POST to validate the URI
err_t httpd_post_begin(void *connection, const char *uri, const char *http_request,
                       uint16_t http_request_len, int content_len, char *response_uri,
                       uint16_t response_uri_len, uint8_t *post_auto_wnd)
{
    LWIP_UNUSED_ARG(http_request);
    LWIP_UNUSED_ARG(http_request_len);
    LWIP_UNUSED_ARG(content_len);
    LWIP_UNUSED_ARG(response_uri);
    LWIP_UNUSED_ARG(response_uri_len);
    LWIP_UNUSED_ARG(post_auto_wnd);

    if (!uri || strncmp(uri, "/api", 4) != 0) {
        return ERR_ARG;
    }

    http_post_uri = uri;
    http_post_payload_len = 0;
    memset(http_post_payload, 0, LWIP_HTTPD_POST_MAX_PAYLOAD_LEN);
    return ERR_OK;
}

// LWIP callback on HTTP POST to for receiving payload
err_t httpd_post_receive_data(void *connection, struct pbuf *p)
{
    LWIP_UNUSED_ARG(connection);

    // Cache the received data to http_post_payload
    while (p != NULL)
    {
        if (http_post_payload_len + p->len <= LWIP_HTTPD_POST_MAX_PAYLOAD_LEN)
        {
            MEMCPY(http_post_payload + http_post_payload_len, p->payload, p->len);
            http_post_payload_len += p->len;
        }
        else // Buffer overflow
        {
            http_post_payload_len = 0xffff;
            break;
        }

        p = p->next;
    }

    // Need to release memory here or will leak
    pbuf_free(p);

    // If the buffer overflows, error out
    if (http_post_payload_len == 0xffff) {
        return ERR_BUF;
    }

    return ERR_OK;
}

// LWIP callback to set the HTTP POST response_uri, which can then be looked up via the fs_custom callbacks
void httpd_post_finished(void *connection, char *response_uri, uint16_t response_uri_len)
{
    LWIP_UNUSED_ARG(connection);

    if (http_post_payload_len != 0xffff) {
        strncpy(response_uri, http_post_uri.c_str(), response_uri_len);
        response_uri[response_uri_len - 1] = '\0';
    }
}


/* ======================================= apis begin ==================================================== */

std::string get_response_temp(STORAGE_ERROR_NO errNo, cJSON* data, std::string errorMessage = "")
{
    cJSON* json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "errNo", errNo);
    cJSON_AddItemToObject(json, "data", data!=NULL?data:cJSON_CreateObject());
    if (!errorMessage.empty()) {
        cJSON_AddStringToObject(json, "errorMessage", errorMessage.c_str());
    }

    char* temp = cJSON_PrintBuffered(json, LWIP_HTTPD_RESPONSE_MAX_PAYLOAD_LEN, 0);
    std::string response(temp);
    cJSON_Delete(json);
    free(temp);

    // printf("get_response_temp: response: %s\n", response.c_str());

    return response;
}

/**
 * @brief 构建按键映射的JSON
 * 
 * @param virtualMask 
 * @return cJSON* 
 */
cJSON* buildKeyMappingJSON(uint32_t virtualMask) {
    cJSON* keyMappingJSON = cJSON_CreateArray();
    
    for(uint8_t i = 0; i < NUM_ADC_BUTTONS + NUM_GPIO_BUTTONS; i++) {
        if(virtualMask & (1 << i)) {
            cJSON_AddItemToArray(keyMappingJSON, cJSON_CreateNumber(i));
        }
    }

    return keyMappingJSON;
}

/**
 * @brief 获取按键映射的虚拟掩码
 * 
 * @param keyMappingJSON 
 * @return uint32_t 
 */
uint32_t getKeyMappingVirtualMask(cJSON* keyMappingJSON) {
    if(!keyMappingJSON || !cJSON_IsArray(keyMappingJSON)) {
        return 0;
    }

    uint32_t virtualMask = 0;
    cJSON* item = NULL;
    cJSON_ArrayForEach(item, keyMappingJSON) {
        virtualMask |= (1 << (int)cJSON_GetNumberValue(item));
    }
    return virtualMask;
}

/**
 * @brief 构建profile列表的JSON
 * 
 * @param config 
 * @return cJSON* 
 */
cJSON* buildProfileListJSON(Config& config) {
    // 创建返回数据结构
    cJSON* profileListJSON = cJSON_CreateObject();
    cJSON* itemsJSON = cJSON_CreateArray();

    // 添加默认配置ID和最大配置数
    cJSON_AddStringToObject(profileListJSON, "defaultId", config.defaultProfileId);
    cJSON_AddNumberToObject(profileListJSON, "maxNumProfiles", config.numProfilesMax);

    // 添加所有配置文件信息
    for(uint8_t i = 0; i < NUM_PROFILES; i++) {
        if(config.profiles[i].enabled) {  // 只添加已启用的配置文件
            cJSON* profileJSON = cJSON_CreateObject();
            
            // 基本信息
            cJSON_AddStringToObject(profileJSON, "id", config.profiles[i].id);
            cJSON_AddStringToObject(profileJSON, "name", config.profiles[i].name);
            cJSON_AddBoolToObject(profileJSON, "enabled", config.profiles[i].enabled);

            // 添加到数组
            cJSON_AddItemToArray(itemsJSON, profileJSON);
        }
    }

    // 构建返回结构
    cJSON_AddItemToObject(profileListJSON, "items", itemsJSON);

    return profileListJSON;
}

// 辅助函数：构建配置文件的JSON结构
cJSON* buildProfileJSON(GamepadProfile* profile) {
    if (!profile) {
        return nullptr;
    }

    cJSON* profileDetailsJSON = cJSON_CreateObject();

    // 基本信息
    cJSON_AddStringToObject(profileDetailsJSON, "id", profile->id);
    cJSON_AddStringToObject(profileDetailsJSON, "name", profile->name);

    // 按键配置
    cJSON* keysConfigJSON = cJSON_CreateObject();
    cJSON_AddBoolToObject(keysConfigJSON, "invertXAxis", profile->keysConfig.invertXAxis);
    cJSON_AddBoolToObject(keysConfigJSON, "invertYAxis", profile->keysConfig.invertYAxis);
    cJSON_AddBoolToObject(keysConfigJSON, "fourWayMode", profile->keysConfig.fourWayMode);

    cJSON_AddNumberToObject(keysConfigJSON, "socdMode", profile->keysConfig.socdMode);

    // 按键映射
    cJSON* keyMappingJSON = cJSON_CreateObject();

    cJSON_AddItemToObject(keyMappingJSON, "DPAD_UP", buildKeyMappingJSON(profile->keysConfig.keyDpadUp));
    cJSON_AddItemToObject(keyMappingJSON, "DPAD_DOWN", buildKeyMappingJSON(profile->keysConfig.keyDpadDown));
    cJSON_AddItemToObject(keyMappingJSON, "DPAD_LEFT", buildKeyMappingJSON(profile->keysConfig.keyDpadLeft));
    cJSON_AddItemToObject(keyMappingJSON, "DPAD_RIGHT", buildKeyMappingJSON(profile->keysConfig.keyDpadRight));
    cJSON_AddItemToObject(keyMappingJSON, "B1", buildKeyMappingJSON(profile->keysConfig.keyButtonB1));
    cJSON_AddItemToObject(keyMappingJSON, "B2", buildKeyMappingJSON(profile->keysConfig.keyButtonB2));
    cJSON_AddItemToObject(keyMappingJSON, "B3", buildKeyMappingJSON(profile->keysConfig.keyButtonB3));
    cJSON_AddItemToObject(keyMappingJSON, "B4", buildKeyMappingJSON(profile->keysConfig.keyButtonB4));
    cJSON_AddItemToObject(keyMappingJSON, "L1", buildKeyMappingJSON(profile->keysConfig.keyButtonL1));
    cJSON_AddItemToObject(keyMappingJSON, "L2", buildKeyMappingJSON(profile->keysConfig.keyButtonL2));
    cJSON_AddItemToObject(keyMappingJSON, "R1", buildKeyMappingJSON(profile->keysConfig.keyButtonR1));
    cJSON_AddItemToObject(keyMappingJSON, "R2", buildKeyMappingJSON(profile->keysConfig.keyButtonR2));
    cJSON_AddItemToObject(keyMappingJSON, "S1", buildKeyMappingJSON(profile->keysConfig.keyButtonS1));
    cJSON_AddItemToObject(keyMappingJSON, "S2", buildKeyMappingJSON(profile->keysConfig.keyButtonS2));
    cJSON_AddItemToObject(keyMappingJSON, "L3", buildKeyMappingJSON(profile->keysConfig.keyButtonL3));
    cJSON_AddItemToObject(keyMappingJSON, "R3", buildKeyMappingJSON(profile->keysConfig.keyButtonR3));
    cJSON_AddItemToObject(keyMappingJSON, "A1", buildKeyMappingJSON(profile->keysConfig.keyButtonA1));
    cJSON_AddItemToObject(keyMappingJSON, "A2", buildKeyMappingJSON(profile->keysConfig.keyButtonA2));
    cJSON_AddItemToObject(keyMappingJSON, "Fn", buildKeyMappingJSON(profile->keysConfig.keyButtonFn));
    cJSON_AddItemToObject(keysConfigJSON, "keyMapping", keyMappingJSON);

    // LED配置
    cJSON* ledsConfigJSON = cJSON_CreateObject();
    cJSON_AddBoolToObject(ledsConfigJSON, "ledEnabled", profile->ledsConfigs.ledEnabled);
    cJSON_AddNumberToObject(ledsConfigJSON, "ledsEffectStyle", static_cast<int>(profile->ledsConfigs.ledEffect));
    
    // LED颜色数组
    cJSON* ledColorsJSON = cJSON_CreateArray();
    char colorStr[8];
    sprintf(colorStr, "#%06X", profile->ledsConfigs.ledColor1);
    cJSON_AddItemToArray(ledColorsJSON, cJSON_CreateString(colorStr));
    sprintf(colorStr, "#%06X", profile->ledsConfigs.ledColor2);
    cJSON_AddItemToArray(ledColorsJSON, cJSON_CreateString(colorStr));
    sprintf(colorStr, "#%06X", profile->ledsConfigs.ledColor3);
    cJSON_AddItemToArray(ledColorsJSON, cJSON_CreateString(colorStr));
    
    cJSON_AddItemToObject(ledsConfigJSON, "ledColors", ledColorsJSON);
    cJSON_AddNumberToObject(ledsConfigJSON, "ledBrightness", profile->ledsConfigs.ledBrightness);
    cJSON_AddNumberToObject(ledsConfigJSON, "ledAnimationSpeed", profile->ledsConfigs.ledAnimationSpeed);


    // 触发器配置
    cJSON* triggerConfigsJSON = cJSON_CreateObject();   
    cJSON* triggerConfigsArrayJSON = cJSON_CreateArray();
    
    for(uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        cJSON* triggerJSON = cJSON_CreateObject();
        RapidTriggerProfile* trigger = &profile->triggerConfigs.triggerConfigs[i];
        char buffer[32];
        // 使用snprintf限制小数点后4位
        snprintf(buffer, sizeof(buffer), "%.4f", trigger->topDeadzone);
        cJSON_AddRawToObject(triggerJSON, "topDeadzone", buffer);
        snprintf(buffer, sizeof(buffer), "%.4f", trigger->bottomDeadzone);
        cJSON_AddRawToObject(triggerJSON, "bottomDeadzone", buffer);
        snprintf(buffer, sizeof(buffer), "%.4f", trigger->pressAccuracy);
        cJSON_AddRawToObject(triggerJSON, "pressAccuracy", buffer);
        snprintf(buffer, sizeof(buffer), "%.4f", trigger->releaseAccuracy);
        cJSON_AddRawToObject(triggerJSON, "releaseAccuracy", buffer);
        cJSON_AddItemToArray(triggerConfigsArrayJSON, triggerJSON);

    }

    cJSON_AddBoolToObject(triggerConfigsJSON, "isAllBtnsConfiguring", profile->triggerConfigs.isAllBtnsConfiguring);
    cJSON_AddItemToObject(triggerConfigsJSON, "triggerConfigs", triggerConfigsArrayJSON);

    // // 组装最终结构
    cJSON_AddItemToObject(profileDetailsJSON, "keysConfig", keysConfigJSON);
    cJSON_AddItemToObject(profileDetailsJSON, "ledsConfigs", ledsConfigJSON);
    cJSON_AddItemToObject(profileDetailsJSON, "triggerConfigs", triggerConfigsJSON);

    return profileDetailsJSON;
}

// 辅助函数：构建快捷键配置的JSON结构
cJSON* buildHotkeysConfigJSON(Config& config) {
    cJSON* hotkeysConfigJSON = cJSON_CreateArray();

    // 添加所有快捷键配置
    for(uint8_t i = 0; i < NUM_GAMEPAD_HOTKEYS; i++) {
        cJSON* hotkeyJSON = cJSON_CreateObject();
        
        // 添加快捷键动作(转换为字符串)
        auto it = GAMEPAD_HOTKEY_TO_STRING.find(config.hotkeys[i].action);
        if (it != GAMEPAD_HOTKEY_TO_STRING.end()) {
            cJSON_AddStringToObject(hotkeyJSON, "action", it->second);
        } else {
            cJSON_AddStringToObject(hotkeyJSON, "action", "None");
        }

        // 添加快捷键序号
        cJSON_AddNumberToObject(hotkeyJSON, "key", config.hotkeys[i].virtualPin);

        // 添加是否长按
        cJSON_AddBoolToObject(hotkeyJSON, "isHold", config.hotkeys[i].isHold);

        // 添加锁定状态
        cJSON_AddBoolToObject(hotkeyJSON, "isLocked", config.hotkeys[i].isLocked);
        
        // 添加到组
        cJSON_AddItemToArray(hotkeysConfigJSON, hotkeyJSON);
    }

    return hotkeysConfigJSON;
}

/**
 * @brief 获取profile列表
 * 
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": { "profileList": {
 *          "defaultId": "profile-0",
 *          "maxNumProfiles": 10,
 *          "items": [
 *              {
 *                  "id": 0,
 *                  "name": "Default",
 *                  ...
 *              },
 *              ...
 *          ]
 *      } }
 * }
 */
std::string apiGetProfileList() {
    Config& config = Storage::getInstance().config;
    
    // 创建返回数据结构
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* profileListJSON = buildProfileListJSON(config);
    
    if (!profileListJSON) {
        cJSON_Delete(dataJSON);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to build profile list JSON");
    }

    // 构建返回结构
    cJSON_AddItemToObject(dataJSON, "profileList", profileListJSON);

    // 生成返回字符串
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);

    return response;
}



/**
 * @brief 获取默认profile
 * 
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": { "profileDetails": {
 *          "id": 0,
 *          "name": "Default",
 *          "keysConfig": {
 *              "invertXAxis": false,
 *              "invertYAxis": false,
 *              "fourWayMode": false,
 *              "socdMode": "SOCD_MODE_NEUTRAL",
 *              "keyMapping": {}    
 *          },
 *          "ledsConfigs": {
 *              "ledEnabled": true,
 *              "ledsEffectStyle": "STATIC",
 *              "ledColors": [
 *                  "#000000",
 *                  "#E67070",
 *                  "#000000"
 *              ],
 *              "ledBrightness": 84
 *          },
 *          "triggerConfigs": {
 *              "isAllBtnsConfiguring": false,
 *              "triggerConfigs": [
 *                  {
 *                      "topDeadzone": 0.6,
 *                      "bottomDeadzone": 0.3,
 *                      "pressAccuracy": 0.3,
 *                      "releaseAccuracy": 0
 *                  },
 *                  ...
 *              ]
 *          }
 *      } }
 * }
 */
std::string apiGetDefaultProfile() {
    // printf("apiGetDefaultProfile start.\n");

    Config& config = Storage::getInstance().config;
    
    // 查找默认配置文件
    GamepadProfile* defaultProfile = nullptr;
    for(uint8_t i = 0; i < NUM_PROFILES; i++) {
        if(strcmp(config.defaultProfileId, config.profiles[i].id) == 0) {
            defaultProfile = &config.profiles[i];
            break;
        }
    }
    
    // printf("apiGetDefaultProfile: defaultProfile: %s\n", defaultProfile->name);

    if(!defaultProfile) {
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Default profile not found");
    }

    // 创建返回数据结构
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* profileDetailsJSON = buildProfileJSON(defaultProfile);

    if (!profileDetailsJSON) {
        cJSON_Delete(dataJSON);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to build profile JSON");
    }
    
    cJSON_AddItemToObject(dataJSON, "profileDetails", profileDetailsJSON);

    // 生成返回字符串
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);

    printf("apiGetDefaultProfile response: %s\n", response.c_str());
    return response;
}

std::string apiGetProfile(const char* profileId) {
    if(!profileId) {
        return get_response_temp(STORAGE_ERROR_NO::PARAMETERS_ERROR, NULL, "Profile ID not provided");
    }

    Config& config = Storage::getInstance().config;
    GamepadProfile* targetProfile = nullptr;
    
    // 查找目标配置文件
    for(uint8_t i = 0; i < NUM_PROFILES; i++) {
        if(strcmp(profileId, config.profiles[i].id) == 0) {
            targetProfile = &config.profiles[i];
            break;
        }
    }
    
    if(!targetProfile) {
        return get_response_temp(STORAGE_ERROR_NO::PARAMETERS_ERROR, NULL, "Profile not found");
    }

    // 创建返回数据结构
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* profileDetailsJSON = buildProfileJSON(targetProfile);
    if (!profileDetailsJSON) {
        cJSON_Delete(dataJSON);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to build profile JSON");
    }
    
    cJSON_AddItemToObject(dataJSON, "profileDetails", profileDetailsJSON);
    
    // 生成返回字符串
    std::string result = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    return result;
}

/**
 * @brief 获取当前profile的hotkeys配置
 * 
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "hotkeysConfig": [
 *              {
 *                  "key": 0,
 *                  "action": "WebConfigMode",
 *                  "isLocked": true
 *              },
 *              ...
 *          ]
 *      }
 * }
 */
std::string apiGetHotkeysConfig() {
    // printf("apiGetHotkeysConfig start.\n");
    Config& config = Storage::getInstance().config;
    
    // 创建返回数据结构
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* hotkeysConfigJSON = buildHotkeysConfigJSON(config);
    
    if (!hotkeysConfigJSON) {
        cJSON_Delete(dataJSON);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to build hotkeys config JSON");
    }

    // 构建返回结构
    cJSON_AddItemToObject(dataJSON, "hotkeysConfig", hotkeysConfigJSON);
    
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    // printf("apiGetHotkeysConfig response: %s\n", response.c_str());
    return response;
}

/**
 * @brief 更新profile   
 * @param std::string profileDetails
 * {
 *      "id": 0,
 *      "name": "Default",
 *      "keysConfig": {
 *          ...   
 *      },
 *      "ledsConfigs": {
 *          ...
 *      },
 *      "triggerConfigs": {
 *          ...
 *      }
 * }
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "profileDetails": {
 *              "id": 0,
 *              "name": "Default",
 *              "keysConfig": {
 *                  "invertXAxis": false,
 *                  "invertYAxis": false,
 *                  "fourWayMode": false,
 *                  "socdMode": "SOCD_MODE_NEUTRAL",
 *                  "keyMapping": {}    
 *              },
 *              "ledsConfigs": {
 *                  "ledEnabled": true,
 *                  "ledsEffectStyle": "STATIC",
 *                  "ledColors": [
 *                      "#000000",
 *                      "#E67070",
 *                      "#000000"
 *                  ],
 *                  "ledBrightness": 84
 *              },
 *              "triggerConfigs": {
 *                  "isAllBtnsConfiguring": false,
 *                  "triggerConfigs": [
 *                      {
 *                          "topDeadzone": 0.6,
 *                          "bottomDeadzone": 0.3,
 *                          "pressAccuracy": 0.3,
 *                          "releaseAccuracy": 0
 *                      },
 *                      ...
 *                  ]
 *              }
 *          }
 *      }
 * }
 */
std::string apiUpdateProfile() {
    // printf("apiUpdateProfile start.\n");
    Config& config = Storage::getInstance().config;
    cJSON* params = get_post_data();
    
    if(!params) {
        return get_response_temp(STORAGE_ERROR_NO::PARAMETERS_ERROR, NULL, "Invalid parameters");
    }

    cJSON* details = cJSON_GetObjectItem(params, "profileDetails");
    
    if(!details) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::PARAMETERS_ERROR, NULL, "Invalid parameters");
    }

    // 获取profile ID并查找对应的配置文件
    cJSON* idItem = cJSON_GetObjectItem(details, "id");
    if(!idItem) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::PARAMETERS_ERROR, NULL, "Profile ID not provided");
    }

    GamepadProfile* targetProfile = nullptr;
    for(uint8_t i = 0; i < NUM_PROFILES; i++) {
        if(strcmp(idItem->valuestring, config.profiles[i].id) == 0) {
            targetProfile = &config.profiles[i];
            break;
        }
    }

    if(!targetProfile) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::PARAMETERS_ERROR, NULL, "Profile not found");
    }

    // 更新基本信息
    cJSON* nameItem = cJSON_GetObjectItem(details, "name");
    if(nameItem) {
        strcpy(targetProfile->name, nameItem->valuestring);
    }

    // 更新按键配置
    cJSON* keysConfig = cJSON_GetObjectItem(details, "keysConfig");
    if(keysConfig) {
        cJSON* item;
        
        if((item = cJSON_GetObjectItem(keysConfig, "invertXAxis"))) {
            targetProfile->keysConfig.invertXAxis = item->type == cJSON_True;
        }
        if((item = cJSON_GetObjectItem(keysConfig, "invertYAxis"))) {
            targetProfile->keysConfig.invertYAxis = item->type == cJSON_True;
        }
        if((item = cJSON_GetObjectItem(keysConfig, "fourWayMode"))) {
            targetProfile->keysConfig.fourWayMode = item->type == cJSON_True;
        }
        if((item = cJSON_GetObjectItem(keysConfig, "socdMode")) 
            && item->type == cJSON_Number 
            && item->valueint >= 0 
            && item->valueint < SOCDMode::NUM_SOCD_MODES) {
            targetProfile->keysConfig.socdMode = static_cast<SOCDMode>(item->valueint);
        }
      
        // 更新按键映射
        cJSON* keyMapping = cJSON_GetObjectItem(keysConfig, "keyMapping");                                          
        if(keyMapping) {
            if((item = cJSON_GetObjectItem(keyMapping, "DPAD_UP"))) 
                targetProfile->keysConfig.keyDpadUp = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "DPAD_DOWN"))) 
                targetProfile->keysConfig.keyDpadDown = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "DPAD_LEFT"))) 
                targetProfile->keysConfig.keyDpadLeft = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "DPAD_RIGHT"))) 
                targetProfile->keysConfig.keyDpadRight = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "B1"))) 
                targetProfile->keysConfig.keyButtonB1 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "B2"))) 
                targetProfile->keysConfig.keyButtonB2 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "B3"))) 
                targetProfile->keysConfig.keyButtonB3 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "B4"))) 
                targetProfile->keysConfig.keyButtonB4 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "L1"))) 
                targetProfile->keysConfig.keyButtonL1 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "L2"))) 
                targetProfile->keysConfig.keyButtonL2 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "R1"))) 
                targetProfile->keysConfig.keyButtonR1 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "R2"))) 
                targetProfile->keysConfig.keyButtonR2 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "S1"))) 
                targetProfile->keysConfig.keyButtonS1 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "S2"))) 
                targetProfile->keysConfig.keyButtonS2 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "L3"))) 
                targetProfile->keysConfig.keyButtonL3 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "R3"))) 
                targetProfile->keysConfig.keyButtonR3 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "A1"))) 
                targetProfile->keysConfig.keyButtonA1 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "A2"))) 
                targetProfile->keysConfig.keyButtonA2 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "Fn"))) 
                targetProfile->keysConfig.keyButtonFn = getKeyMappingVirtualMask(item);
        }
    }

    // 更新LED配置
    cJSON* ledsConfig = cJSON_GetObjectItem(details, "ledsConfigs");
    if(ledsConfig) {
        cJSON* item;
        
        if((item = cJSON_GetObjectItem(ledsConfig, "ledEnabled"))) {
            targetProfile->ledsConfigs.ledEnabled = item->type == cJSON_True;
        }
        
        // 解析LED效
        if((item = cJSON_GetObjectItem(ledsConfig, "ledsEffectStyle")) 
            && item->type == cJSON_Number 
            && item->valueint >= 0 
            && item->valueint < LEDEffect::NUM_EFFECTS) {
            targetProfile->ledsConfigs.ledEffect = static_cast<LEDEffect>(item->valueint);
        }

        // 解析LED颜色
        cJSON* ledColors = cJSON_GetObjectItem(ledsConfig, "ledColors");
        if(ledColors && cJSON_GetArraySize(ledColors) >= 3) {
            sscanf(cJSON_GetArrayItem(ledColors, 0)->valuestring, "#%x", &targetProfile->ledsConfigs.ledColor1);
            sscanf(cJSON_GetArrayItem(ledColors, 1)->valuestring, "#%x", &targetProfile->ledsConfigs.ledColor2);
            sscanf(cJSON_GetArrayItem(ledColors, 2)->valuestring, "#%x", &targetProfile->ledsConfigs.ledColor3);

            APP_DBG("ledColors: %06x, %06x, %06x\n", targetProfile->ledsConfigs.ledColor1, targetProfile->ledsConfigs.ledColor2, targetProfile->ledsConfigs.ledColor3);
        }
        
        if((item = cJSON_GetObjectItem(ledsConfig, "ledBrightness"))) {
            targetProfile->ledsConfigs.ledBrightness = item->valueint;
        }

        if((item = cJSON_GetObjectItem(ledsConfig, "ledAnimationSpeed"))) { 
            targetProfile->ledsConfigs.ledAnimationSpeed = item->valueint;
        }
    }

    // 更新按键行程配置
    cJSON* triggerConfigs = cJSON_GetObjectItem(details, "triggerConfigs");
    if(triggerConfigs) {
        cJSON* isAllBtnsConfiguring = cJSON_GetObjectItem(triggerConfigs, "isAllBtnsConfiguring");
        if(isAllBtnsConfiguring) {
            targetProfile->triggerConfigs.isAllBtnsConfiguring = isAllBtnsConfiguring->type == cJSON_True;
        }

        cJSON* configs = cJSON_GetObjectItem(triggerConfigs, "triggerConfigs");
        if(configs) {
            for(uint8_t i = 0; i < NUM_ADC_BUTTONS && i < cJSON_GetArraySize(configs); i++) {
                cJSON* trigger = cJSON_GetArrayItem(configs, i);
                RapidTriggerProfile* triggerProfile = &targetProfile->triggerConfigs.triggerConfigs[i];
                if(trigger) {
                    cJSON* item;
                    if((item = cJSON_GetObjectItem(trigger, "topDeadzone")))
                        triggerProfile->topDeadzone = item->valuedouble;
                    if((item = cJSON_GetObjectItem(trigger, "bottomDeadzone")))
                        triggerProfile->bottomDeadzone = item->valuedouble;
                    if((item = cJSON_GetObjectItem(trigger, "pressAccuracy")))
                        triggerProfile->pressAccuracy = item->valuedouble;
                    if((item = cJSON_GetObjectItem(trigger, "releaseAccuracy")))
                        triggerProfile->releaseAccuracy = item->valuedouble;
                }
            }
        }
    }

    // 保存配置
    if(!STORAGE_MANAGER.saveConfig()) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to save configuration");
    }

    // 构建返回数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* profileDetailsJSON = buildProfileJSON(targetProfile);
    if (!profileDetailsJSON) {
        cJSON_Delete(dataJSON);
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to build profile JSON");
    }
    
    cJSON_AddItemToObject(dataJSON, "profileDetails", profileDetailsJSON);
    
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    
    cJSON_Delete(params);
    
    // printf("apiUpdateProfile response: %s\n", response.c_str());
    return response;
}

/**
 * @brief 创建profile
 * @param std::string
 * {
 *      "profileName": "Default",
 * }
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "profileList": {
 *              "items": [
 *                  {
 *                      "id": 0,
 *                      "name": "Default",
 *                      ...
 *                  },
 *                  ...
 *              ]
 *          }
 *      }
 * }
 */
std::string apiCreateProfile() {
    // printf("apiCreateProfile start.\n");
    Config& config = Storage::getInstance().config;
    cJSON* params = get_post_data();
    
    if(!params) {
        return get_response_temp(STORAGE_ERROR_NO::PARAMETERS_ERROR, NULL, "Invalid parameters");
    }

    // 检查是否达到最大配置文件数
    uint8_t enabledCount = 0;
    for(uint8_t i = 0; i < NUM_PROFILES; i++) {
        if(config.profiles[i].enabled) {
            enabledCount++;
        }
    }

    if(enabledCount >= config.numProfilesMax) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Maximum number of profiles reached");
    }

    // 查找第一个未启用的配置文件
    GamepadProfile* targetProfile = nullptr;
    for(uint8_t i = 0; i < NUM_PROFILES; i++) {
        if(!config.profiles[i].enabled) {
            targetProfile = &config.profiles[i];
            break;
        }
    }

    if(!targetProfile) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "No available profile slot");
    }

    // 获取新配置文件名称
    cJSON* nameItem = cJSON_GetObjectItem(params, "profileName");
    if(nameItem && nameItem->valuestring) {
        ConfigUtils::makeDefaultProfile(*targetProfile, targetProfile->id, true); // 启用配置文件 并且 初始化配置文件
        strncpy(targetProfile->name, nameItem->valuestring, sizeof(targetProfile->name) - 1); // 设置配置文件名称
        targetProfile->name[sizeof(targetProfile->name) - 1] = '\0';  // 确保字符串结束
        strcpy(config.defaultProfileId, targetProfile->id); // 设置默认配置文件ID 为新创建的配置文件ID
    } else {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Profile name not provided");
    }

    // 保存配置
    if(!STORAGE_MANAGER.saveConfig()) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to save configuration");
    }

    // 构建返回数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* profileListJSON = buildProfileListJSON(config);
    
    if (!profileListJSON) {
        cJSON_Delete(dataJSON);
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to build profile list JSON");
    }

    cJSON_AddItemToObject(dataJSON, "profileList", profileListJSON);
    
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    
    cJSON_Delete(params);
    
    // printf("apiCreateProfile response: %s\n", response.c_str());

    return response;
}

/**
 * @brief 删除profile
 * @param std::string
 * {
 *      "profileId": "id",
 * }
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "profileList": {
 *              "items": [
 *                  {
 *                      "id": 0,
 *                      "name": "Default",
 *                      ...
 *                  },
 *                  ...
 *              ]
 *          }
 *      }
 * }
 */
std::string apiDeleteProfile() {
    // printf("apiDeleteProfile start.\n");    
    Config& config = Storage::getInstance().config;
    cJSON* params = get_post_data();
    
    if(!params) {
        return get_response_temp(STORAGE_ERROR_NO::PARAMETERS_ERROR, NULL, "Invalid parameters");
    }

    // 获取要删除的配置文件ID
    cJSON* profileIdItem = cJSON_GetObjectItem(params, "profileId");
    if(!profileIdItem || !profileIdItem->valuestring) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::PARAMETERS_ERROR, NULL, "Profile ID not provided");
    }

    // 查找目标配置文件
    GamepadProfile* targetProfile = nullptr;
    uint8_t numEnabledProfiles = 0;
    uint8_t targetIndex = 0;

    for(uint8_t i = 0; i < NUM_PROFILES; i++) {
        if(config.profiles[i].enabled) {
            numEnabledProfiles++;
            if(strcmp(profileIdItem->valuestring, config.profiles[i].id) == 0) {
                targetProfile = &config.profiles[i];
                targetIndex = i;
            }
        }
    }

    // 如果目标配置文件不存在，则返回错误
    if(!targetProfile) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::PARAMETERS_ERROR, NULL, "Profile not found");
    }

    // 不允许关闭最后一个启用的配置文件
    if(numEnabledProfiles <= 1) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Cannot delete the last active profile");
    }

    // 禁用配置文件（相当于删除）
    config.profiles[targetIndex].enabled = false;

    // 为了保持内存连续性，将目标配置文件之后的配置文件向前移动一位
    // 保存目标配置文件的副本
    GamepadProfile* tempProfile = (GamepadProfile*)malloc(sizeof(GamepadProfile));
    if(!tempProfile) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to allocate memory");
    }
    // 保存目标配置文件
    memcpy(tempProfile, &config.profiles[targetIndex], sizeof(GamepadProfile));
    // 将目标配置文件之后的配置文件向前移动一位
    memmove(&config.profiles[targetIndex], 
            &config.profiles[targetIndex + 1], 
            (NUM_PROFILES - targetIndex - 1) * sizeof(GamepadProfile));
    // 将目标配置文件放到最后一个
    memcpy(&config.profiles[NUM_PROFILES - 1], tempProfile, sizeof(GamepadProfile));
    free(tempProfile);

    // 设置下一个启用的配置文件为默认配置文件
    for(uint8_t i = targetIndex; i >= 0; i --) {
        if(config.profiles[i].enabled) {
            strcpy(config.defaultProfileId, config.profiles[i].id);
            break;
        }
    }

    // 保存配置
    if(!STORAGE_MANAGER.saveConfig()) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to save configuration");
    }

    // 构建返回数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* profileListJSON = buildProfileListJSON(config);
    
    if (!profileListJSON) {
        cJSON_Delete(dataJSON);
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to build profile list JSON");
    }

    cJSON_AddItemToObject(dataJSON, "profileList", profileListJSON);
    
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    
    cJSON_Delete(params);
    
    // printf("apiDeleteProfile response: %s\n", response.c_str());

    return response;
}

/**
 * @brief 切换默认profile
 * @param std::string
 * {
 *      "profileId": "id",
 * }
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "profileList": {
 *              "items": [
 *                  {
 *                      "id": 0,
 *                      "name": "Default",
 *                      ...
 *                  },
 *                  ...
 *              ]
 *          }
 *      }
 * }
 */
std::string apiSwitchDefaultProfile() {
    // printf("apiSwitchDefaultProfile start.\n");
    Config& config = Storage::getInstance().config;
    cJSON* params = get_post_data();
    
    if(!params) {
        return get_response_temp(STORAGE_ERROR_NO::PARAMETERS_ERROR, NULL, "Invalid parameters");
    }

    // 获取要设置为默认的配置文件ID
    cJSON* profileIdItem = cJSON_GetObjectItem(params, "profileId");
    if(!profileIdItem || !profileIdItem->valuestring) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::PARAMETERS_ERROR, NULL, "Profile ID not provided");
    }

    // 查找目标配置文件
    GamepadProfile* targetProfile = nullptr;
    for(uint8_t i = 0; i < NUM_PROFILES; i++) {
        if(strcmp(profileIdItem->valuestring, config.profiles[i].id) == 0) {
            targetProfile = &config.profiles[i];
            break;
        }
    }

    if(!targetProfile) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::PARAMETERS_ERROR, NULL, "Profile not found");
    }

    // 检查目标配置文件是否已启用
    if(!targetProfile->enabled) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Cannot set disabled profile as default");
    }

    strcpy(config.defaultProfileId, targetProfile->id);

    // 保存配置
    if(!STORAGE_MANAGER.saveConfig()) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to save configuration");
    }

    // 构建返回数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* profileListJSON = buildProfileListJSON(config);
    
    if (!profileListJSON) {
        cJSON_Delete(dataJSON);
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to build profile list JSON");
    }

    cJSON_AddItemToObject(dataJSON, "profileList", profileListJSON);
    
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    
    cJSON_Delete(params);
    
    // printf("apiSwitchDefaultProfile response: %s\n", response.c_str());

    return response;
}

/**
 * @brief 更新hotkeys配置
 * @param std::string
 * {
 *      "hotkeysConfig": [
 *          {
 *              "key": 0,
 *              "action": "WebConfigMode",
 *              "isLocked": true
 *          },
 *          ...
 *      ]
 * }
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "hotkeysConfig": [
 *              {
 *                  "key": 0,
 *                  "action": "WebConfigMode",
 *                  "isLocked": true
 *              },
 *              ...
 *          ]
 *      }
 * }
 */
std::string apiUpdateHotkeysConfig() {
    // printf("apiUpdateHotkeysConfig start.\n");
    Config& config = Storage::getInstance().config;
    cJSON* params = get_post_data();
    
    if(!params) {
        return get_response_temp(STORAGE_ERROR_NO::PARAMETERS_ERROR, NULL, "Invalid parameters");
    }
    
    // 获取快捷键配置组
    cJSON* hotkeysConfigArray = cJSON_GetObjectItem(params, "hotkeysConfig");
    if(!hotkeysConfigArray || !cJSON_IsArray(hotkeysConfigArray)) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::PARAMETERS_ERROR, NULL, "Invalid hotkeys configuration");
    }

    // 遍历并更新每个快捷键配置
    int numHotkeys = cJSON_GetArraySize(hotkeysConfigArray);
    for(int i = 0; i < numHotkeys && i < NUM_GAMEPAD_HOTKEYS; i++) {
        cJSON* hotkeyItem = cJSON_GetArrayItem(hotkeysConfigArray, i);
        if(!hotkeyItem) continue;

        // 获取快捷键序号
        cJSON* keyItem = cJSON_GetObjectItem(hotkeyItem, "key");
        if(!keyItem || !cJSON_IsNumber(keyItem)) continue;
        int keyIndex = keyItem->valueint;
        if(keyIndex < -1 || keyIndex >= (NUM_ADC_BUTTONS + NUM_GPIO_BUTTONS)) continue;
        config.hotkeys[i].virtualPin = keyIndex;

        // 获取动作
        cJSON* actionItem = cJSON_GetObjectItem(hotkeyItem, "action");
        if(!actionItem || !cJSON_IsString(actionItem)) continue;

        // 获取锁定状态
        cJSON* isLockedItem = cJSON_GetObjectItem(hotkeyItem, "isLocked");
        if(isLockedItem) {
            config.hotkeys[i].isLocked = cJSON_IsTrue(isLockedItem);
        }

        // 获取是否长按
        cJSON* isHoldItem = cJSON_GetObjectItem(hotkeyItem, "isHold");
        if(isHoldItem) {
            config.hotkeys[i].isHold = cJSON_IsTrue(isHoldItem);
        }

        // 根据字符串设置动作
        const char* actionStr = actionItem->valuestring;
        auto it = STRING_TO_GAMEPAD_HOTKEY.find(actionStr);
        if (it != STRING_TO_GAMEPAD_HOTKEY.end()) {
            config.hotkeys[i].action = it->second;
        } else {
            config.hotkeys[i].action = GamepadHotkey::HOTKEY_NONE;
        }
    }

    // 保存配置
    if(!STORAGE_MANAGER.saveConfig()) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to save configuration");
    }

    // 构建返回数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* hotkeysConfigJSON = buildHotkeysConfigJSON(config);
    
    if (!hotkeysConfigJSON) {
        cJSON_Delete(dataJSON);
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to build hotkeys config JSON");
    }

    cJSON_AddItemToObject(dataJSON, "hotkeysConfig", hotkeysConfigJSON);
    
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    
    cJSON_Delete(params);
    
    // printf("apiUpdateHotkeysConfig response: %s\n", response.c_str());

    return response;
}

/**
 * @brief 重启
 * 
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "message": "System is rebooting"
 *      }
 * }
 */
std::string apiReboot() {
    // printf("apiReboot start.\n");
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddStringToObject(dataJSON, "message", "System is rebooting");
    
    STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
    STORAGE_MANAGER.saveConfig();

    // 设置延迟重启时间
    rebootTick = HAL_GetTick() + rebootDelayMs;
    needReboot = true;
    
    // 获取标准格式的响应
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    // printf("apiReboot response: %s\n", response.c_str());
    return response;
}


/**
 * @brief 构建轴体映射名称列表JSON
 * @return cJSON*
 */
cJSON* buildMappingListJSON() {

    // 获取轴体映射名称列表
    std::vector<ADCValuesMapping*> mappingList = ADC_MANAGER.getMappingList();

    APP_DBG("buildMappingListJSON: mappingList size: %d", mappingList.size());

    cJSON* listJSON = cJSON_CreateArray();
    for(ADCValuesMapping* mapping : mappingList) {
        cJSON* itemJSON = cJSON_CreateObject();
        cJSON_AddStringToObject(itemJSON, "id", mapping->id);
        cJSON_AddStringToObject(itemJSON, "name", mapping->name);
        cJSON_AddItemToArray(listJSON, itemJSON);
    }

    APP_DBG("buildMappingListJSON: listJSON size: %d", cJSON_GetArraySize(listJSON));

    return listJSON;
}

/**
 * @brief 获取轴体映射名称列表
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "mappingList": [
 *              {
 *                  "id": "mappingId1",
 *                  "name": "mappingName1",
 *              },
 *              ...
 *          ]
 *      }
 * }
 */
std::string apiMSGetList() {
    APP_DBG("apiMSGetList: start");

    cJSON* dataJSON = cJSON_CreateObject();
    // 添加映射列表到响应数据
    cJSON_AddItemToObject(dataJSON, "mappingList", buildMappingListJSON());

    cJSON_AddItemToObject(dataJSON, "defaultMappingId", cJSON_CreateString(ADC_MANAGER.getDefaultMapping().c_str()));
    
    // 获取标准格式的响应
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    return response;
}

/**
 * @brief 获取标记状态
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "markStatus": {
 *              "isMarking": true,
 *              "markIndex": 0,
 *              "isCompleted": false,
 *              "mapping": {
 *                  "name": "mappingName",
 *                  "length": 10,
 *                  "step": 0.1,
 *                  "originalValues": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9],
 *                  "calibratedValues": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9],
 *                  "markingValues": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]
 *              }
 *          }
 *      }
 * }
 */
std::string apiMSGetMarkStatus() {
    // printf("apiMSGetMarkStatus start.\n");
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();

    cJSON* markStatusJSON = ADC_BTNS_MARKER.getStepInfoJSON();
    
    // 添加标记状态到响应数据
    cJSON_AddItemToObject(dataJSON, "status", markStatusJSON);
    
    // 获取标准格式的响应
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    
    // printf("apiMSGetMarkStatus response: %s\n", response.c_str());
    return response;
}

/**
 * @brief 设置默认轴体
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "defaultMapping": {
 *              "name": "mappingName",
 *              "length": 10,
 *              "step": 0.1,
 *              "originalValues": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9],
 *              "calibratedValues": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]
 *          }
 *      }
 * }
 */
std::string apiMSSetDefault() {
    // printf("apiMSSetDefault start.\n");
    
    // 解析请求参数
    cJSON* params = cJSON_Parse(http_post_payload);
    if (!params) {
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Invalid request parameters");
    }
    
    // 获取映射名称
    cJSON* idJSON = cJSON_GetObjectItem(params, "id");
    if (!idJSON || !cJSON_IsString(idJSON)) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Missing or invalid mapping id");
    }
    
    const char* mappingId = idJSON->valuestring;

    // 设置默认映射
    ADCBtnsError error = ADC_MANAGER.setDefaultMapping(mappingId);
    if(error != ADCBtnsError::SUCCESS) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to set default mapping");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddStringToObject(dataJSON, "id", mappingId);

    // 获取标准格式的响应
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    
    cJSON_Delete(params);


    // printf("apiMSSetDefault response: %s\n", response.c_str());
    return response;
}

/**
 * @brief 获取默认轴体
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "defaultMapping": {
 *              "name": "mappingName",
 *              "length": 10,
 *              "step": 0.1,
 *              "originalValues": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9],
 *              "calibratedValues": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]
 *          }
 *      }
 * }
 */
std::string apiMSGetDefault() {
    // printf("apiMSGetDefault start.\n");
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    
    // 获取默认映射名称
    std::string defaultId = ADC_MANAGER.getDefaultMapping();
    if(defaultId.empty()) {
        cJSON_AddStringToObject(dataJSON, "id", "");
    } else {
        cJSON_AddStringToObject(dataJSON, "id", defaultId.c_str());
    }
    
    // 获取标准格式的响应
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    
    // printf("apiMSGetDefault response: %s\n", response.c_str());
    return response;
}

/**
 * @brief 创建轴体映射
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "mapping": {
 *              "name": "mappingName",
 *              "length": 10,
 *              "step": 0.1,
 *              "originalValues": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9],
 *              "calibratedValues": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]
 *          }
 *      }
 * }
 */
std::string apiMSCreateMapping() {
    // printf("apiMSCreateMapping start.\n");
    
    // 解析请求参数
    cJSON* params = cJSON_Parse(http_post_payload);
    if (!params) {
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Invalid request parameters");
    }
    
    // 获取映射名称
    cJSON* nameJSON = cJSON_GetObjectItem(params, "name");
    if (!nameJSON || !cJSON_IsString(nameJSON)) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Missing or invalid mapping name");
    }
    
    // 获取映射长度
    cJSON* lengthJSON = cJSON_GetObjectItem(params, "length");
    if (!lengthJSON || !cJSON_IsNumber(lengthJSON)) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Missing or invalid mapping length");
    }
    
    // 获取步长
    cJSON* stepJSON = cJSON_GetObjectItem(params, "step");
    if (!stepJSON || !cJSON_IsNumber(stepJSON)) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Missing or invalid mapping step");
    }
    
    const char* mappingName = nameJSON->valuestring;
    size_t length = (size_t)lengthJSON->valueint;
    float_t step = (float_t)stepJSON->valuedouble;
    
    // 创建映射
    ADCBtnsError error = ADC_MANAGER.createADCMapping(mappingName, length, step);

    // printf("apiMSCreateMapping error: %d\n", error);

    if(error != ADCBtnsError::SUCCESS) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to create mapping");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddItemToObject(dataJSON, "defaultMappingId", cJSON_CreateString(ADC_MANAGER.getDefaultMapping().c_str()));
    cJSON_AddItemToObject(dataJSON, "mappingList", buildMappingListJSON());
    
    // 获取标准格式的响应
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    
    cJSON_Delete(params);
    
    // printf("apiMSCreateMapping response: %s\n", response.c_str());
    return response;
}

/**
 * @brief 删除轴体映射
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "mapping": {
 *              "name": "mappingName",
 *              "length": 10,
 *              "step": 0.1,
 *              "originalValues": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9],
 *              "calibratedValues": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]
 *          }
 *      }
 * }
 */
std::string apiMSDeleteMapping() {
    // printf("apiMSDeleteMapping start.\n");
    
    // 解析请求参数
    cJSON* params = cJSON_Parse(http_post_payload);
    if (!params) {
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Invalid request parameters");
    }
    
    // 获取映射名称
    cJSON* idJSON = cJSON_GetObjectItem(params, "id");
    if (!idJSON || !cJSON_IsString(idJSON)) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Missing or invalid mapping id");
    }
    
    const char* mappingId = idJSON->valuestring;
    
    // 删除映射
    ADCBtnsError error = ADC_MANAGER.removeADCMapping(mappingId);
    if(error != ADCBtnsError::SUCCESS) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to delete mapping");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddItemToObject(dataJSON, "defaultMappingId", cJSON_CreateString(ADC_MANAGER.getDefaultMapping().c_str()));
    cJSON_AddItemToObject(dataJSON, "mappingList", buildMappingListJSON());
    
    // 获取标准格式的响应
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    
    cJSON_Delete(params);
    
    // printf("apiMSDeleteMapping response: %s\n", response.c_str());
    return response;
}

/** 
 * @brief 重命名轴体映射
 * @return std::string 
 */
std::string apiMSRenameMapping() {
    // 解析请求参数
    cJSON* params = cJSON_Parse(http_post_payload);
    if (!params) {
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Invalid request parameters");
    }

    // 获取映射名称
    cJSON* idJSON = cJSON_GetObjectItem(params, "id");
    if (!idJSON || !cJSON_IsString(idJSON)) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Missing or invalid mapping id");
    }

    // 获取映射名称
    cJSON* nameJSON = cJSON_GetObjectItem(params, "name");
    if (!nameJSON || !cJSON_IsString(nameJSON)) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Missing or invalid mapping name");
    }

    const char* mappingId = idJSON->valuestring;
    const char* mappingName = nameJSON->valuestring;

    // 重命名映射
    ADCBtnsError error = ADC_MANAGER.renameADCMapping(mappingId, mappingName);
    if(error != ADCBtnsError::SUCCESS) {   
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to rename mapping");
    }

    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddItemToObject(dataJSON, "defaultMappingId", cJSON_CreateString(ADC_MANAGER.getDefaultMapping().c_str()));
    cJSON_AddItemToObject(dataJSON, "mappingList", buildMappingListJSON());

    // 获取标准格式的响应
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    
    cJSON_Delete(params);
    
    // printf("apiMSRenameMapping response: %s\n", response.c_str());
    return response;
}


/**
 * @brief 开始标记
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "message": "Marking started"
 *      }
 * }
 */
std::string apiMSMarkMappingStart() {
    // printf("apiMSMarkMappingStart start.\n");
    
    // 解析请求参数
    cJSON* params = cJSON_Parse(http_post_payload);
    if (!params) {
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Invalid request parameters");
    }
    
    // 获取映射名称
    cJSON* idJSON = cJSON_GetObjectItem(params, "id");
    if (!idJSON || !cJSON_IsString(idJSON)) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Missing or invalid mapping id");
    }
    
    const char* mappingId = idJSON->valuestring;
    
    // 开始标记
    ADCBtnsError error = ADC_BTNS_MARKER.setup(mappingId);
    if(error != ADCBtnsError::SUCCESS) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to start marking");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* statusJSON = ADC_BTNS_MARKER.getStepInfoJSON();
    cJSON_AddItemToObject(dataJSON, "status", statusJSON);
    
    // 获取标准格式的响应
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    
    cJSON_Delete(params);
    
    // printf("apiMSMarkMappingStart response: %s\n", response.c_str());
    return response;
}

/**
 * @brief 停止标记
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "message": "Marking stopped"
 *      }
 * }
 */
std::string apiMSMarkMappingStop() {
    // printf("apiMSMarkMappingStop start.\n");
    
    // 停止标记
    ADC_BTNS_MARKER.reset();
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* statusJSON = ADC_BTNS_MARKER.getStepInfoJSON();
    cJSON_AddItemToObject(dataJSON, "status", statusJSON);
    
    // 获取标准格式的响应
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    
    // printf("apiMSMarkMappingStop response: %s\n", response.c_str());
    return response;
}

/**
 * @brief 标记步进
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "message": "Marking step"
 *      }
 * }
 */
std::string apiMSMarkMappingStep() {
    
    // 执行标记步进
    ADCBtnsError error = ADC_BTNS_MARKER.step();
    if(error != ADCBtnsError::SUCCESS) {
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to perform marking step");
    }
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* statusJSON = ADC_BTNS_MARKER.getStepInfoJSON();
    cJSON_AddItemToObject(dataJSON, "status", statusJSON);
    // 获取标准格式的响应
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    return response;
}

/**
 * @brief 获取轴体映射
 * @return std::string 
 */
std::string apiMSGetMapping() {
    cJSON* params = cJSON_Parse(http_post_payload);
    if (!params) {
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Invalid request parameters");
    }

    cJSON* idJSON = cJSON_GetObjectItem(params, "id");
    if (!idJSON || !cJSON_IsString(idJSON)) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Missing or invalid mapping id");
    }

    const ADCValuesMapping* resultMapping = ADC_MANAGER.getMapping(idJSON->valuestring);
    if (!resultMapping) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to get mapping");
    }

    cJSON_Delete(params);

    cJSON* mappingJSON = cJSON_CreateObject();
    cJSON_AddItemToObject(mappingJSON, "id", cJSON_CreateString(resultMapping->id));
    cJSON_AddItemToObject(mappingJSON, "name", cJSON_CreateString(resultMapping->name));
    cJSON_AddItemToObject(mappingJSON, "length", cJSON_CreateNumber(resultMapping->length));
    cJSON_AddItemToObject(mappingJSON, "step", cJSON_CreateNumber(resultMapping->step));
    cJSON_AddItemToObject(mappingJSON, "samplingFrequency", cJSON_CreateNumber(resultMapping->samplingFrequency));
    cJSON_AddItemToObject(mappingJSON, "samplingNoise", cJSON_CreateNumber(resultMapping->samplingNoise));

    cJSON* originalValuesJSON = cJSON_CreateArray();
    for(size_t i = 0; i < resultMapping->length; i++) {
        cJSON_AddItemToArray(originalValuesJSON, cJSON_CreateNumber(resultMapping->originalValues[i]));
    }
    cJSON_AddItemToObject(mappingJSON, "originalValues", originalValuesJSON);
    
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddItemToObject(dataJSON, "mapping", mappingJSON);

    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);

    // printf("apiMSGetMapping response: %s\n", response.c_str());

    return response;
}

/**
 * @brief 获取全局配置
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "globalConfig": {
 *              "inputMode": "XINPUT",
 *              "autoCalibrationEnabled": true,
 *              "manualCalibrationActive": false
 *          }
 *      }
 * }
 */
std::string apiGetGlobalConfig() {
    Config& config = Storage::getInstance().config;
    
    // 创建返回数据结构
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* globalConfigJSON = cJSON_CreateObject();
    
    // 使用全局映射表获取输入模式字符串
    const char* modeStr = "XINPUT"; // 默认值
    auto it = INPUT_MODE_STRINGS.find(config.inputMode);
    if (it != INPUT_MODE_STRINGS.end()) {
        modeStr = it->second;
    }
    cJSON_AddStringToObject(globalConfigJSON, "inputMode", modeStr);
    
    // 添加自动校准模式状态
    cJSON_AddBoolToObject(globalConfigJSON, "autoCalibrationEnabled", config.autoCalibrationEnabled);
    
    // 添加手动校准状态
    cJSON_AddBoolToObject(globalConfigJSON, "manualCalibrationActive", ADC_CALIBRATION_MANAGER.isCalibrationActive());
    
    // 构建返回结构
    cJSON_AddItemToObject(dataJSON, "globalConfig", globalConfigJSON);
    
    // 生成返回字符串
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    
    return response;
}

/**
 * @brief 更新全局配置
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "globalConfig": {
 *              "inputMode": "XINPUT",
 *              "autoCalibrationEnabled": true,
 *              "manualCalibrationActive": false
 *          }
 *      }
 * }
 */
std::string apiUpdateGlobalConfig() {
    Config& config = Storage::getInstance().config;
    cJSON* params = get_post_data();
    
    if(!params) {
        return get_response_temp(STORAGE_ERROR_NO::PARAMETERS_ERROR, NULL, "Invalid parameters");
    }

    // 更新全局配置
    cJSON* globalConfig = cJSON_GetObjectItem(params, "globalConfig");
    if(globalConfig) {
        // 更新输入模式
        cJSON* inputModeItem = cJSON_GetObjectItem(globalConfig, "inputMode");
        if(inputModeItem && cJSON_IsString(inputModeItem)) {
            // 使用反向映射表查找对应的InputMode枚举值
            std::string modeStr = inputModeItem->valuestring;
            auto it = STRING_TO_INPUT_MODE.find(modeStr);
            if (it != STRING_TO_INPUT_MODE.end()) {
                config.inputMode = it->second;
            } else {
                config.inputMode = InputMode::INPUT_MODE_XINPUT; // 默认值
            }
        }
        
        // 更新自动校准模式
        cJSON* autoCalibrationItem = cJSON_GetObjectItem(globalConfig, "autoCalibrationEnabled");
        if(autoCalibrationItem) {
            config.autoCalibrationEnabled = cJSON_IsTrue(autoCalibrationItem);
        }
    }

    // 保存配置
    if(!STORAGE_MANAGER.saveConfig()) {
        cJSON_Delete(params);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to save configuration");
    }

    // 创建返回数据结构
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* globalConfigJSON = cJSON_CreateObject();
    
    // 使用全局映射表获取输入模式字符串
    const char* modeStr = "XINPUT"; // 默认值
    auto it = INPUT_MODE_STRINGS.find(config.inputMode);
    if (it != INPUT_MODE_STRINGS.end()) {
        modeStr = it->second;
    }
    cJSON_AddStringToObject(globalConfigJSON, "inputMode", modeStr);
    
    // 添加自动校准模式状态
    cJSON_AddBoolToObject(globalConfigJSON, "autoCalibrationEnabled", config.autoCalibrationEnabled);
    
    // 添加手动校准状态
    cJSON_AddBoolToObject(globalConfigJSON, "manualCalibrationActive", ADC_CALIBRATION_MANAGER.isCalibrationActive());
    
    // 构建返回结构
    cJSON_AddItemToObject(dataJSON, "globalConfig", globalConfigJSON);
    
    // 生成返回字符串
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    
    cJSON_Delete(params);
    
    return response;
}

/**
 * @brief 开始手动校准
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "message": "Manual calibration started",
 *          "calibrationStatus": {
 *              "isActive": true,
 *              "uncalibratedCount": 8,
 *              "activeCalibrationCount": 8,
 *              "allCalibrated": false
 *          }
 *      }
 * }
 */
std::string apiStartManualCalibration() {
    // 开始手动校准
    // 初始化ADC管理器
    ADCBtnsError error = ADC_CALIBRATION_MANAGER.startManualCalibration();
    if(error != ADCBtnsError::SUCCESS) {
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to start manual calibration");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddStringToObject(dataJSON, "message", "Manual calibration started");
    
    // 添加校准状态信息
    cJSON* statusJSON = cJSON_CreateObject();
    cJSON_AddBoolToObject(statusJSON, "isActive", ADC_CALIBRATION_MANAGER.isCalibrationActive());
    cJSON_AddNumberToObject(statusJSON, "uncalibratedCount", ADC_CALIBRATION_MANAGER.getUncalibratedButtonCount());
    cJSON_AddNumberToObject(statusJSON, "activeCalibrationCount", ADC_CALIBRATION_MANAGER.getActiveCalibrationButtonCount());
    cJSON_AddBoolToObject(statusJSON, "allCalibrated", ADC_CALIBRATION_MANAGER.isAllButtonsCalibrated());
    
    cJSON_AddItemToObject(dataJSON, "calibrationStatus", statusJSON);
    
    // 获取标准格式的响应
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    
    return response;
}

/**
 * @brief 结束手动校准
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "message": "Manual calibration stopped",
 *          "calibrationStatus": {
 *              "isActive": false,
 *              "uncalibratedCount": 3,
 *              "activeCalibrationCount": 0,
 *              "allCalibrated": false
 *          }
 *      }
 * }
 */
std::string apiStopManualCalibration() {
    // 停止手动校准
    ADCBtnsError error = ADC_CALIBRATION_MANAGER.stopCalibration();
    
    if(error != ADCBtnsError::SUCCESS) {
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to stop manual calibration");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddStringToObject(dataJSON, "message", "Manual calibration stopped");
    
    // 添加校准状态信息
    cJSON* statusJSON = cJSON_CreateObject();
    cJSON_AddBoolToObject(statusJSON, "isActive", ADC_CALIBRATION_MANAGER.isCalibrationActive());
    cJSON_AddNumberToObject(statusJSON, "uncalibratedCount", ADC_CALIBRATION_MANAGER.getUncalibratedButtonCount());
    cJSON_AddNumberToObject(statusJSON, "activeCalibrationCount", ADC_CALIBRATION_MANAGER.getActiveCalibrationButtonCount());
    cJSON_AddBoolToObject(statusJSON, "allCalibrated", ADC_CALIBRATION_MANAGER.isAllButtonsCalibrated());
    
    cJSON_AddItemToObject(dataJSON, "calibrationStatus", statusJSON);
    
    // 获取标准格式的响应
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    
    return response;
}

/**
 * @brief 获取校准状态
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "calibrationStatus": {
 *              "isActive": true,
 *              "uncalibratedCount": 3,
 *              "activeCalibrationCount": 8,
 *              "allCalibrated": false,
 *              "buttons": [
 *                  {
 *                      "index": 0,
 *                      "phase": "TOP_SAMPLING",
 *                      "sampleCount": 5,
 *                      "isCalibrated": false,
 *                      "topValue": 0,
 *                      "bottomValue": 0,
 *                      "expectedTopValue": 3900,
 *                      "expectedBottomValue": 100,
 *                      "ledColor": "CYAN"
 *                  }
 *              ]
 *          }
 *      }
 * }
 */
std::string apiGetCalibrationStatus() {
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* statusJSON = cJSON_CreateObject();
    
    // 添加总体校准状态
    cJSON_AddBoolToObject(statusJSON, "isActive", ADC_CALIBRATION_MANAGER.isCalibrationActive());
    cJSON_AddNumberToObject(statusJSON, "uncalibratedCount", ADC_CALIBRATION_MANAGER.getUncalibratedButtonCount());
    cJSON_AddNumberToObject(statusJSON, "activeCalibrationCount", ADC_CALIBRATION_MANAGER.getActiveCalibrationButtonCount());
    cJSON_AddBoolToObject(statusJSON, "allCalibrated", ADC_CALIBRATION_MANAGER.isAllButtonsCalibrated());
    
    // 注意：即使所有按钮都校准完成，也不自动关闭校准模式
    // 只有用户手动调用停止校准接口才会关闭校准模式
    // 这样设计是为了让用户有机会确认校准结果
    
    // 添加每个按钮的详细状态
    cJSON* buttonsArray = cJSON_CreateArray();
    
    for(uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        cJSON* buttonJSON = cJSON_CreateObject();
        
        cJSON_AddNumberToObject(buttonJSON, "index", i);
        
        // 获取校准阶段
        CalibrationPhase phase = ADC_CALIBRATION_MANAGER.getButtonPhase(i);
        const char* phaseStr = "IDLE";
        switch(phase) {
            case CalibrationPhase::IDLE: phaseStr = "IDLE"; break;
            case CalibrationPhase::TOP_SAMPLING: phaseStr = "TOP_SAMPLING"; break;
            case CalibrationPhase::BOTTOM_SAMPLING: phaseStr = "BOTTOM_SAMPLING"; break;
            case CalibrationPhase::COMPLETED: phaseStr = "COMPLETED"; break;
            case CalibrationPhase::ERROR: phaseStr = "ERROR"; break;
        }
        cJSON_AddStringToObject(buttonJSON, "phase", phaseStr);
        
        // 获取校准状态
        cJSON_AddBoolToObject(buttonJSON, "isCalibrated", ADC_CALIBRATION_MANAGER.isButtonCalibrated(i));
        
        // 获取校准值
        uint16_t topValue = 0, bottomValue = 0;
        ADC_CALIBRATION_MANAGER.getCalibrationValues(i, topValue, bottomValue);
        cJSON_AddNumberToObject(buttonJSON, "topValue", topValue);
        cJSON_AddNumberToObject(buttonJSON, "bottomValue", bottomValue);
        
        // 获取LED颜色
        CalibrationLEDColor ledColor = ADC_CALIBRATION_MANAGER.getButtonLEDColor(i);
        const char* colorStr = "OFF";
        switch(ledColor) {
            case CalibrationLEDColor::OFF: colorStr = "OFF"; break;
            case CalibrationLEDColor::RED: colorStr = "RED"; break;
            case CalibrationLEDColor::CYAN: colorStr = "CYAN"; break;
            case CalibrationLEDColor::DARK_BLUE: colorStr = "DARK_BLUE"; break;
            case CalibrationLEDColor::GREEN: colorStr = "GREEN"; break;
            case CalibrationLEDColor::YELLOW: colorStr = "YELLOW"; break;
        }
        cJSON_AddStringToObject(buttonJSON, "ledColor", colorStr);
        
        cJSON_AddItemToArray(buttonsArray, buttonJSON);
    }
    
    cJSON_AddItemToObject(statusJSON, "buttons", buttonsArray);
    cJSON_AddItemToObject(dataJSON, "calibrationStatus", statusJSON);
    
    // 获取标准格式的响应
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    
    return response;
}

/**
 * @brief 清除手动校准数据
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "message": "Manual calibration data cleared successfully",
 *          "calibrationStatus": {
 *              "isActive": false,
 *              "uncalibratedCount": 8,
 *              "activeCalibrationCount": 0,
 *              "allCalibrated": false
 *          }
 *      }
 * }
 */
std::string apiClearManualCalibrationData() {
    // 清除所有手动校准数据
    ADCBtnsError error = ADC_CALIBRATION_MANAGER.resetAllCalibration();
    if(error != ADCBtnsError::SUCCESS) {
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to clear manual calibration data");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddStringToObject(dataJSON, "message", "Manual calibration data cleared successfully");
    
    // 添加清除后的校准状态信息
    cJSON* statusJSON = cJSON_CreateObject();
    cJSON_AddBoolToObject(statusJSON, "isActive", ADC_CALIBRATION_MANAGER.isCalibrationActive());
    cJSON_AddNumberToObject(statusJSON, "uncalibratedCount", ADC_CALIBRATION_MANAGER.getUncalibratedButtonCount());
    cJSON_AddNumberToObject(statusJSON, "activeCalibrationCount", ADC_CALIBRATION_MANAGER.getActiveCalibrationButtonCount());
    cJSON_AddBoolToObject(statusJSON, "allCalibrated", ADC_CALIBRATION_MANAGER.isAllButtonsCalibrated());
    
    cJSON_AddItemToObject(dataJSON, "calibrationStatus", statusJSON);
    
    // 获取标准格式的响应
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    
    return response;
}


/**
 * @brief 开启按键功能
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "message": "Button monitoring started successfully",
 *          "status": "active"
 *      }
 * }
 */
std::string apiStartButtonMonitoring() {
    // 获取按键管理器实例并开始监控
    WebConfigBtnsManager& btnsManager = WebConfigBtnsManager::getInstance();
    
    // 启动按键工作器
    btnsManager.startButtonWorkers();
    
    // 验证启动状态
    if (!btnsManager.isActive()) {
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to start button monitoring");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    if (!dataJSON) {
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to create JSON object");
    }
    
    cJSON_AddStringToObject(dataJSON, "message", "Button monitoring started successfully");
    cJSON_AddStringToObject(dataJSON, "status", "active");
    cJSON_AddBoolToObject(dataJSON, "isActive", btnsManager.isActive());
    
    // 获取标准格式的响应
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    return response;
}

/**
 * @brief 关闭按键功能
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "message": "Button monitoring stopped successfully",
 *          "status": "inactive"
 *      }
 * }
 */
std::string apiStopButtonMonitoring() {
    // 获取按键管理器实例
    WebConfigBtnsManager& btnsManager = WebConfigBtnsManager::getInstance();
    
    // 停止按键工作器（真正停止ADC和GPIO按键采样）
    btnsManager.stopButtonWorkers();
    
    // 验证停止状态
    if (btnsManager.isActive()) {
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to stop button monitoring");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    if (!dataJSON) {
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to create JSON object");
    }
    
    cJSON_AddStringToObject(dataJSON, "message", "Button monitoring stopped successfully");
    cJSON_AddStringToObject(dataJSON, "status", "inactive");
    cJSON_AddBoolToObject(dataJSON, "isActive", btnsManager.isActive());
    
    // 获取标准格式的响应
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    return response;
}

/**
 * @brief 轮询获取按键状态
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "triggerMask": 5,  // 二进制码：按键0和按键2在本周期内有触发
 *          "triggerBinary": "00000101",  // 二进制字符串表示
 *          "totalButtons": 8,
 *          "timestamp": 1234567890
 *      }
 * }
 */
std::string apiGetButtonStates() {
    // 获取按键管理器实例
    WebConfigBtnsManager& btnsManager = WebConfigBtnsManager::getInstance();
    
    // 检查按键工作器是否活跃
    if (!btnsManager.isActive()) {
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Button monitoring is not active");
    }
    
    // 获取并清空触发掩码
    uint32_t triggerMask = btnsManager.getAndClearTriggerMask();
    uint8_t totalButtons = btnsManager.getTotalButtonCount();
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    if (!dataJSON) {
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to create JSON object");
    }
    
    // 添加触发掩码（十进制）
    cJSON_AddNumberToObject(dataJSON, "triggerMask", triggerMask);
    
    // 创建二进制字符串表示
    std::string binaryStr = "";
    for (int i = totalButtons - 1; i >= 0; i--) {
        binaryStr += (triggerMask & (1U << i)) ? "1" : "0";
    }
    cJSON_AddStringToObject(dataJSON, "triggerBinary", binaryStr.c_str());
    
    // 添加按键总数
    cJSON_AddNumberToObject(dataJSON, "totalButtons", totalButtons);
    
    // 添加时间戳
    cJSON_AddNumberToObject(dataJSON, "timestamp", HAL_GetTick());
    
    // 获取标准格式的响应
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    return response;
}




/**
 * @brief 推送前端 LED 配置到后端硬件
 * POST 请求体格式：
 * {
 *     "ledEnabled": true,
 *     "ledsEffectStyle": 2,
 *     "ledColors": ["#ff0000", "#00ff00", "#0000ff"],
 *     "ledBrightness": 75,
 *     "ledAnimationSpeed": 3
 * }
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "message": "LED configuration applied successfully"
 *      }
 * }
 */
std::string apiPushLedsConfig() {
    cJSON* postParams = get_post_data();
    if (!postParams) {
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Invalid JSON data");
    }

    // 获取当前默认配置文件作为基础
    const GamepadProfile* currentProfile = STORAGE_MANAGER.getDefaultGamepadProfile();
    if (!currentProfile) {
        cJSON_Delete(postParams);
        return get_response_temp(STORAGE_ERROR_NO::ACTION_FAILURE, NULL, "Failed to get current profile");
    }

    // 创建临时的LED配置，基于当前配置
    LEDProfile tempLedsConfig = currentProfile->ledsConfigs;
    
    // 解析前端传来的配置参数
    cJSON* item;
    
    // LED 启用状态
    if ((item = cJSON_GetObjectItem(postParams, "ledEnabled"))) {
        tempLedsConfig.ledEnabled = cJSON_IsTrue(item);
    }
    
    // LED 特效类型
    if ((item = cJSON_GetObjectItem(postParams, "ledsEffectStyle")) 
        && cJSON_IsNumber(item) 
        && item->valueint >= 0 
        && item->valueint < LEDEffect::NUM_EFFECTS) {
        tempLedsConfig.ledEffect = static_cast<LEDEffect>(item->valueint);
    }
    
    // LED 亮度
    if ((item = cJSON_GetObjectItem(postParams, "ledBrightness")) 
        && cJSON_IsNumber(item)
        && item->valueint >= 0 
        && item->valueint <= 100) {
        tempLedsConfig.ledBrightness = item->valueint;
    }
    
    // LED 动画速度
    if ((item = cJSON_GetObjectItem(postParams, "ledAnimationSpeed")) 
        && cJSON_IsNumber(item)
        && item->valueint >= 1 
        && item->valueint <= 5) {
        tempLedsConfig.ledAnimationSpeed = item->valueint;
    }
    
    // LED 颜色数组
    cJSON* ledColors = cJSON_GetObjectItem(postParams, "ledColors");
    if (ledColors && cJSON_IsArray(ledColors) && cJSON_GetArraySize(ledColors) >= 3) {
        cJSON* color1 = cJSON_GetArrayItem(ledColors, 0);
        cJSON* color2 = cJSON_GetArrayItem(ledColors, 1);
        cJSON* color3 = cJSON_GetArrayItem(ledColors, 2);
        
        if (color1 && cJSON_IsString(color1)) {
            sscanf(color1->valuestring, "#%lx", &tempLedsConfig.ledColor1);
        }
        if (color2 && cJSON_IsString(color2)) {
            sscanf(color2->valuestring, "#%lx", &tempLedsConfig.ledColor2);
        }
        if (color3 && cJSON_IsString(color3)) {
            sscanf(color3->valuestring, "#%lx", &tempLedsConfig.ledColor3);
        }
    }
    
    APP_DBG("tempLedsConfig: %d, %d, %ld, %ld, %ld, %d, %d", 
            tempLedsConfig.ledEnabled, 
            tempLedsConfig.ledEffect, 
            tempLedsConfig.ledColor1, 
            tempLedsConfig.ledColor2, 
            tempLedsConfig.ledColor3,
            tempLedsConfig.ledBrightness,
            tempLedsConfig.ledAnimationSpeed);

    // 通过WebConfigLedsManager应用预览配置
    WEBCONFIG_LEDS_MANAGER.applyPreviewConfig(tempLedsConfig);
    // 通过WebConfigBtnsManager启动按键工作器
    WEBCONFIG_BTNS_MANAGER.startButtonWorkers();
    
    cJSON_Delete(postParams);
    
    // 返回成功响应
    cJSON* dataJSON = cJSON_CreateObject();
    if (dataJSON) {
        cJSON_AddStringToObject(dataJSON, "message", "LED configuration applied successfully for preview");
    }
    
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    return response;
}

/**
 * @brief 清除LED预览模式，恢复默认配置
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "message": "LED preview mode cleared successfully"
 *      }
 * }
 */
std::string apiClearLedsPreview() {
    WEBCONFIG_LEDS_MANAGER.clearPreviewConfig();
    WEBCONFIG_BTNS_MANAGER.stopButtonWorkers();
    
    // 返回成功响应
    cJSON* dataJSON = cJSON_CreateObject();
    if (dataJSON) {
        cJSON_AddStringToObject(dataJSON, "message", "LED preview mode cleared successfully");
    }
    
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    return response;
}

/*============================================ apis end ====================================================*/


typedef std::string (*HandlerFuncPtr)();
static const std::pair<const char*, HandlerFuncPtr> handlerFuncs[] =
{
    { "/api/global-config", apiGetGlobalConfig },
    { "/api/update-global-config", apiUpdateGlobalConfig },
    { "/api/profile-list",  apiGetProfileList },
    { "/api/default-profile",  apiGetDefaultProfile },
    { "/api/hotkeys-config", apiGetHotkeysConfig },    
    { "/api/update-profile", apiUpdateProfile },
    { "/api/create-profile", apiCreateProfile },
    { "/api/delete-profile", apiDeleteProfile },
    { "/api/switch-default-profile", apiSwitchDefaultProfile },
    { "/api/update-hotkeys-config", apiUpdateHotkeysConfig },
    { "/api/reboot", apiReboot },
    { "/api/ms-get-list", apiMSGetList },          //获取轴体映射列表
    { "/api/ms-get-mark-status", apiMSGetMarkStatus },      // 获取标记状态
    { "/api/ms-set-default", apiMSSetDefault },            // 设置默认轴体
    { "/api/ms-get-default", apiMSGetDefault },            // 获取默认轴体
    { "/api/ms-create-mapping", apiMSCreateMapping },      // 创建轴体映射
    { "/api/ms-delete-mapping", apiMSDeleteMapping },      // 删除轴体映射
    { "/api/ms-rename-mapping", apiMSRenameMapping },      // 重命名轴体映射
    { "/api/ms-mark-mapping-start", apiMSMarkMappingStart },// 开始标记
    { "/api/ms-mark-mapping-stop", apiMSMarkMappingStop },    // 停止标记
    { "/api/ms-mark-mapping-step", apiMSMarkMappingStep },    // 标记步进
    { "/api/ms-get-mapping", apiMSGetMapping },              // 获取轴体映射
    { "/api/start-manual-calibration", apiStartManualCalibration }, // 开始手动校准
    { "/api/stop-manual-calibration", apiStopManualCalibration },   // 结束手动校准
    { "/api/get-calibration-status", apiGetCalibrationStatus },     // 获取校准状态
    { "/api/clear-manual-calibration-data", apiClearManualCalibrationData }, // 清除手动校准数据
    { "/api/start-button-monitoring", apiStartButtonMonitoring },   // 开启按键功能
    { "/api/stop-button-monitoring", apiStopButtonMonitoring },     // 关闭按键功能
    { "/api/get-button-states", apiGetButtonStates },               // 轮询获取按键状态
    { "/api/push-leds-config", apiPushLedsConfig },                 // 推送 LED 配置
    { "/api/clear-leds-preview", apiClearLedsPreview },             // 清除 LED 预览模式
#if !defined(NDEBUG)
    // { "/api/echo", echo },
#endif
};

typedef DataAndStatusCode (*HandlerFuncStatusCodePtr)();
static const std::pair<const char*, HandlerFuncStatusCodePtr> handlerFuncsWithStatusCode[] =
{
    // { "/api/setConfig", setConfig },
};


int fs_open_custom(struct fs_file *file, const char *name)
{
    // APP_DBG("fs_open_custom: %s", name);

    // 处理API请求
    for (const auto& handlerFunc : handlerFuncs)
    {
        if (strcmp(handlerFunc.first, name) == 0)
        {
            // APP_DBG("handlerFunc.first: %s  name: %s result: %d", handlerFunc.first, name, strcmp(handlerFunc.first, name));
            return set_file_data(file, handlerFunc.second());
        }
    }

    // 处理API请求
    // for (const auto& handlerFunc : handlerFuncsWithStatusCode)
    // {
    //     if (strcmp(handlerFunc.first, name) == 0)
    //     {
    //         return set_file_data(file, handlerFunc.second());
    //     }
    // }

    // 处理静态文件
    for (const char* excludePath : excludePaths)
        if (strcmp(excludePath, name) == 0)
            return 0;

    // 处理SPA文件
    for (const char* spaPath : spaPaths)
    {
        if (strcmp(spaPath, name) == 0)
        {
            // 查找 index.html 文件
            const struct fsdata_file* f = getFSRoot();
            while (f != NULL) {
                if (!strcmp("/index.html", (char *)f->name)) {
                    file->data = (const char *)f->data;
                    file->len = f->len;
                    file->index = f->len;
                    file->http_header_included = f->http_header_included;
                    file->pextension = NULL;
                    file->is_custom_file = 0;
                    return 1;
                }
                f = f->next;
            }
            return 0;
        }
    }

    return 0;
}

void fs_close_custom(struct fs_file *file)
{
    if (file && file->is_custom_file && file->pextension)
    {
        mem_free(file->pextension);
        file->pextension = NULL;
    }
}
