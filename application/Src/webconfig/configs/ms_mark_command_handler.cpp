#include "configs/device_command_handler.hpp"
#include "storagemanager.hpp"
#include "adc_btns/adc_manager.hpp"
#include "adc_btns/adc_btns_marker.hpp"
#include "system_logger.h"
#include <map>
#include <cstring>
#include <cmath>
#include "config_transport_sink.hpp"
#include "adc_btns/adc_btns_worker.hpp"
#include "states/input_state.hpp"

// ============================================================================
// MSMarkCommandHandler 实现
// ============================================================================

MSMarkCommandHandler& MSMarkCommandHandler::getInstance() {
    static MSMarkCommandHandler instance;
    return instance;
}

// 提供全局访问MSMarkCommandHandler实例的函数
MSMarkCommandHandler& getMarkingCommandHandler() {
    return MSMarkCommandHandler::getInstance();
}

// 发送标记状态变化通知
void MSMarkCommandHandler::sendMarkingStatusNotification() {
    // 创建通知消息（不带CID的消息）
    cJSON* json = cJSON_CreateObject();
    if (!json) {
        LOG_ERROR("DeviceCommand", "Failed to create notification JSON");
        return;
    }
    
    // 设置通知消息格式：{"command": "marking_status_update", "errNo": 0, "data": {...}}
    cJSON_AddStringToObject(json, "command", "marking_status_update");
    cJSON_AddNumberToObject(json, "errNo", 0);
    
    // 添加状态数据
    cJSON* statusJSON = ADC_BTNS_MARKER.getStepInfoJSON();
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddItemToObject(dataJSON, "status", statusJSON);
    cJSON_AddItemToObject(json, "data", dataJSON);
    
    // 序列化JSON并广播
    char* json_string = cJSON_PrintUnformatted(json);
    if (json_string) {
        ConfigTransport_PublishJson(json_string, strlen(json_string));
        // APP_DBG("sendMarkingStatusNotification: Marking status notification sent - %s", json_string);
        free(json_string);
    } else {
        LOG_ERROR("DeviceCommand", "Failed to serialize notification JSON");
    }
    
    cJSON_Delete(json);
}

cJSON* MSMarkCommandHandler::buildMappingListJSON() {
    // 获取轴体映射名称列表
    std::vector<ADCValuesMapping*> mappingList = ADC_MANAGER.getMappingList();

    // APP_DBG("buildMappingListJSON: mappingList size: %d", mappingList.size());

    cJSON* listJSON = cJSON_CreateArray();
    for(ADCValuesMapping* mapping : mappingList) {
        cJSON* itemJSON = cJSON_CreateObject();
        cJSON_AddStringToObject(itemJSON, "id", mapping->id);
        cJSON_AddStringToObject(itemJSON, "name", mapping->name);
        cJSON_AddItemToArray(listJSON, itemJSON);
    }

    // APP_DBG("buildMappingListJSON: listJSON size: %d", cJSON_GetArraySize(listJSON));

    return listJSON;
}

DeviceCommandResponse MSMarkCommandHandler::handleGetList(const DeviceCommandRequest& request) {
    // LOG_INFO("DeviceCommand", "Handling ms_get_list command, cid: %d", request.getCid());

    cJSON* dataJSON = cJSON_CreateObject();
    // 添加映射列表到响应数据
    cJSON_AddItemToObject(dataJSON, "mappingList", buildMappingListJSON());
    cJSON_AddItemToObject(dataJSON, "defaultMappingId", cJSON_CreateString(ADC_MANAGER.getDefaultMapping().c_str()));
    cJSON_AddStringToObject(dataJSON, "storageMode", "shared-singleton");
    cJSON_AddNumberToObject(dataJSON, "installSchemaVersion", 1);
    cJSON_AddStringToObject(dataJSON, "source",
        ADC_MANAGER.isSharedMappingInstalled()
            ? "server-installed" : "factory-fallback");
    
    // LOG_INFO("DeviceCommand", "ms_get_list command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

DeviceCommandResponse MSMarkCommandHandler::handleGetMarkStatus(const DeviceCommandRequest& request) {
    // LOG_INFO("DeviceCommand", "Handling ms_get_mark_status command, cid: %d", request.`getCid());
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* markStatusJSON = ADC_BTNS_MARKER.getStepInfoJSON();
    
    // 添加标记状态到响应数据
    cJSON_AddItemToObject(dataJSON, "status", markStatusJSON);
    
    // LOG_INFO("DeviceCommand", "ms_get_mark_status command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

DeviceCommandResponse MSMarkCommandHandler::handleSetDefault(const DeviceCommandRequest& request) {
    // LOG_INFO("DeviceCommand", "Handling ms_set_default command, cid: %d", request.getCid());
    
    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("DeviceCommand", "ms_set_default: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }
    
    // 获取映射ID
    cJSON* idJSON = cJSON_GetObjectItem(params, "id");
    if (!idJSON || !cJSON_IsString(idJSON)) {
        LOG_ERROR("DeviceCommand", "ms_set_default: Missing or invalid mapping id");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing or invalid mapping id");
    }
    
    const char* mappingId = idJSON->valuestring;

    // 设置默认映射
    ADCBtnsError error = ADC_MANAGER.setDefaultMapping(mappingId);
    if(error != ADCBtnsError::SUCCESS) {
        LOG_ERROR("DeviceCommand", "ms_set_default: Failed to set default mapping");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to set default mapping");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddStringToObject(dataJSON, "id", mappingId);

    // LOG_INFO("DeviceCommand", "ms_set_default command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

DeviceCommandResponse MSMarkCommandHandler::handleGetDefault(const DeviceCommandRequest& request) {
    // LOG_INFO("DeviceCommand", "Handling ms_get_default command, cid: %d", request.getCid());
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    
    // 获取默认映射名称
    std::string defaultId = ADC_MANAGER.getDefaultMapping();
    if(defaultId.empty()) {
        cJSON_AddStringToObject(dataJSON, "id", "");
    } else {
        cJSON_AddStringToObject(dataJSON, "id", defaultId.c_str());
    }
    
    // LOG_INFO("DeviceCommand", "ms_get_default command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

DeviceCommandResponse MSMarkCommandHandler::handleCreateMapping(const DeviceCommandRequest& request) {
    // LOG_INFO("DeviceCommand", "Handling ms_create_mapping command, cid: %d", request.getCid());
    
    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("DeviceCommand", "ms_create_mapping: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }
    
    // 获取映射名称
    cJSON* nameJSON = cJSON_GetObjectItem(params, "name");
    if (!nameJSON || !cJSON_IsString(nameJSON)) {
        LOG_ERROR("DeviceCommand", "ms_create_mapping: Missing or invalid mapping name");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing or invalid mapping name");
    }
    
    // 获取长度
    cJSON* lengthJSON = cJSON_GetObjectItem(params, "length");
    if (!lengthJSON || !cJSON_IsNumber(lengthJSON)) {
        LOG_ERROR("DeviceCommand", "ms_create_mapping: Missing or invalid length");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing or invalid length");
    }
    
    // 获取步长
    cJSON* stepJSON = cJSON_GetObjectItem(params, "step");
    if (!stepJSON || !cJSON_IsNumber(stepJSON)) {
        LOG_ERROR("DeviceCommand", "ms_create_mapping: Missing or invalid step");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing or invalid step");
    }
    
    const char* mappingName = nameJSON->valuestring;
    const double lengthValue = lengthJSON->valuedouble;
    const double stepValue = stepJSON->valuedouble;
    if (mappingName[0] == '\0' ||
        strlen(mappingName) >= sizeof(ADCValuesMapping::name) ||
        !std::isfinite(lengthValue) || std::floor(lengthValue) != lengthValue ||
        lengthValue < 2.0 || lengthValue > static_cast<double>(MAX_ADC_VALUES_LENGTH) ||
        !std::isfinite(stepValue) || stepValue < 0.1 || stepValue > 10.0) {
        return create_error_response(request.getCid(), request.getCommand(), 1,
                                     "Mapping name, length or step is outside the supported range");
    }
    size_t length = static_cast<size_t>(lengthValue);
    float_t step = static_cast<float_t>(stepValue);
    
    // 创建映射
    std::string createdMappingId;
    ADCBtnsError error = ADC_MANAGER.createADCMapping(
        mappingName, length, step, &createdMappingId);
    if(error != ADCBtnsError::SUCCESS) {
        LOG_ERROR("DeviceCommand", "ms_create_mapping: Failed to create mapping");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to create mapping");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddStringToObject(dataJSON, "createdMappingId", createdMappingId.c_str());
    cJSON_AddItemToObject(dataJSON, "defaultMappingId", cJSON_CreateString(ADC_MANAGER.getDefaultMapping().c_str()));
    cJSON_AddItemToObject(dataJSON, "mappingList", buildMappingListJSON());
    
    // LOG_INFO("DeviceCommand", "ms_create_mapping command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

DeviceCommandResponse MSMarkCommandHandler::handleDeleteMapping(const DeviceCommandRequest& request) {
    // LOG_INFO("DeviceCommand", "Handling ms_delete_mapping command, cid: %d", request.getCid());
    
    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("DeviceCommand", "ms_delete_mapping: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }
    
    // 获取映射ID
    cJSON* idJSON = cJSON_GetObjectItem(params, "id");
    if (!idJSON || !cJSON_IsString(idJSON)) {
        LOG_ERROR("DeviceCommand", "ms_delete_mapping: Missing or invalid mapping id");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing or invalid mapping id");
    }
    
    const char* mappingId = idJSON->valuestring;

    if (ADC_MANAGER.getDefaultMapping() == mappingId) {
        return create_error_response(request.getCid(), request.getCommand(), 1,
                                     "Select another default mapping before deleting this mapping");
    }
    
    // 删除映射
    ADCBtnsError error = ADC_MANAGER.removeADCMapping(mappingId);
    if(error != ADCBtnsError::SUCCESS) {
        LOG_ERROR("DeviceCommand", "ms_delete_mapping: Failed to delete mapping");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to delete mapping");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddItemToObject(dataJSON, "defaultMappingId", cJSON_CreateString(ADC_MANAGER.getDefaultMapping().c_str()));
    cJSON_AddItemToObject(dataJSON, "mappingList", buildMappingListJSON());
    
    // LOG_INFO("DeviceCommand", "ms_delete_mapping command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

DeviceCommandResponse MSMarkCommandHandler::handleRenameMapping(const DeviceCommandRequest& request) {
    // LOG_INFO("DeviceCommand", "Handling ms_rename_mapping command, cid: %d", request.getCid());
    
    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("DeviceCommand", "ms_rename_mapping: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    // 获取映射ID
    cJSON* idJSON = cJSON_GetObjectItem(params, "id");
    if (!idJSON || !cJSON_IsString(idJSON)) {
        LOG_ERROR("DeviceCommand", "ms_rename_mapping: Missing or invalid mapping id");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing or invalid mapping id");
    }

    // 获取映射名称
    cJSON* nameJSON = cJSON_GetObjectItem(params, "name");
    if (!nameJSON || !cJSON_IsString(nameJSON)) {
        LOG_ERROR("DeviceCommand", "ms_rename_mapping: Missing or invalid mapping name");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing or invalid mapping name");
    }

    const char* mappingId = idJSON->valuestring;
    const char* mappingName = nameJSON->valuestring;
    if (mappingName[0] == '\0' ||
        strlen(mappingName) >= sizeof(ADCValuesMapping::name)) {
        return create_error_response(request.getCid(), request.getCommand(), 1,
                                     "Mapping name exceeds the 15-byte UTF-8 limit");
    }

    // 重命名映射
    ADCBtnsError error = ADC_MANAGER.renameADCMapping(mappingId, mappingName);
    if(error != ADCBtnsError::SUCCESS) {   
        LOG_ERROR("DeviceCommand", "ms_rename_mapping: Failed to rename mapping");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to rename mapping");
    }

    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddItemToObject(dataJSON, "defaultMappingId", cJSON_CreateString(ADC_MANAGER.getDefaultMapping().c_str()));
    cJSON_AddItemToObject(dataJSON, "mappingList", buildMappingListJSON());

    // LOG_INFO("DeviceCommand", "ms_rename_mapping command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

DeviceCommandResponse MSMarkCommandHandler::handleMarkMappingStart(const DeviceCommandRequest& request) {
    // LOG_INFO("DeviceCommand", "Handling ms_mark_mapping_start command, cid: %d", request.getCid());
    
    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("DeviceCommand", "ms_mark_mapping_start: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }
    
    // 获取映射ID
    cJSON* idJSON = cJSON_GetObjectItem(params, "id");
    if (!idJSON || !cJSON_IsString(idJSON)) {
        LOG_ERROR("DeviceCommand", "ms_mark_mapping_start: Missing or invalid mapping id");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing or invalid mapping id");
    }
    
    const char* mappingId = idJSON->valuestring;
    
    // 开始标记
    ADCBtnsError error = ADC_BTNS_MARKER.setup(mappingId);
    if(error != ADCBtnsError::SUCCESS) {
        LOG_ERROR("DeviceCommand", "ms_mark_mapping_start: Failed to start marking");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to start marking");
    }
    
    // 发送状态变化通知
    sendMarkingStatusNotification();
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* statusJSON = ADC_BTNS_MARKER.getStepInfoJSON();
    cJSON_AddItemToObject(dataJSON, "status", statusJSON);
    
    // LOG_INFO("DeviceCommand", "ms_mark_mapping_start command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

DeviceCommandResponse MSMarkCommandHandler::handleMarkMappingStop(const DeviceCommandRequest& request) {
    // LOG_INFO("DeviceCommand", "Handling ms_mark_mapping_stop command, cid: %d", request.getCid());

    // Persist the last completed sample before clearing the RAM recording
    // state. NOT_MARKING also covers stopping before the first completed point
    // or while the current sampling window is still active.
    const ADCBtnsError persistResult = ADC_BTNS_MARKER.persistProgress();
    if (persistResult != ADCBtnsError::SUCCESS &&
        persistResult != ADCBtnsError::NOT_MARKING) {
        return create_error_response(request.getCid(), request.getCommand(), 1,
                                     "Failed to persist mapping before stop");
    }

    // 停止标记
    ADC_BTNS_MARKER.reset();
    
    // 发送状态变化通知
    sendMarkingStatusNotification();
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* statusJSON = ADC_BTNS_MARKER.getStepInfoJSON();
    cJSON_AddItemToObject(dataJSON, "status", statusJSON);
    
    // LOG_INFO("DeviceCommand", "ms_mark_mapping_stop command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

DeviceCommandResponse MSMarkCommandHandler::handleMarkMappingStep(const DeviceCommandRequest& request) {
    // LOG_INFO("DeviceCommand", "Handling ms_mark_mapping_step command, cid: %d", request.getCid());
    
    // 执行标记步进
    ADCBtnsError error = ADC_BTNS_MARKER.step();
    if(error != ADCBtnsError::SUCCESS) {
        LOG_ERROR("DeviceCommand", "ms_mark_mapping_step: Failed to perform marking step");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to perform marking step");
    }
    
    // 发送状态变化通知
    sendMarkingStatusNotification();
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* statusJSON = ADC_BTNS_MARKER.getStepInfoJSON();
    cJSON_AddItemToObject(dataJSON, "status", statusJSON);
    
    // LOG_INFO("DeviceCommand", "ms_mark_mapping_step command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

DeviceCommandResponse MSMarkCommandHandler::handleGetMapping(const DeviceCommandRequest& request) {
    // LOG_INFO("DeviceCommand", "Handling ms_get_mapping command, cid: %d", request.getCid());
    
    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("DeviceCommand", "ms_get_mapping: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    cJSON* idJSON = cJSON_GetObjectItem(params, "id");
    if (!idJSON || !cJSON_IsString(idJSON)) {
        LOG_ERROR("DeviceCommand", "ms_get_mapping: Missing or invalid mapping id");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing or invalid mapping id");
    }

    const ADCValuesMapping* resultMapping = ADC_MANAGER.getMapping(idJSON->valuestring);
    if (!resultMapping) {
        LOG_ERROR("DeviceCommand", "ms_get_mapping: Failed to get mapping");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to get mapping");
    }

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

    // LOG_INFO("DeviceCommand", "ms_get_mapping command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

DeviceCommandResponse MSMarkCommandHandler::handleMarkMappingSync(
    const DeviceCommandRequest& request) {
    const ADCBtnsError result = ADC_BTNS_MARKER.persistProgress();
    if (result != ADCBtnsError::SUCCESS) {
        return create_error_response(request.getCid(), request.getCommand(), 1,
                                     "Failed to persist mapping recording progress");
    }
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddItemToObject(dataJSON, "status", ADC_BTNS_MARKER.getStepInfoJSON());
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

DeviceCommandResponse MSMarkCommandHandler::handleGetConfigBackup(const DeviceCommandRequest& request) {
    return create_error_response(request.getCid(), request.getCommand(), 410,
                                 "ADC backup has moved to the server mapping catalog");
}

namespace {
cJSON* mappingToJSON(const ADCValuesMapping& mapping, bool includeId) {
    cJSON* json = cJSON_CreateObject();
    if (includeId) cJSON_AddStringToObject(json, "id", mapping.id);
    cJSON_AddStringToObject(json, "name", mapping.name);
    cJSON_AddNumberToObject(json, "length", mapping.length);
    cJSON_AddNumberToObject(json, "step", mapping.step);
    cJSON_AddNumberToObject(json, "samplingNoise", mapping.samplingNoise);
    cJSON_AddNumberToObject(json, "samplingFrequency", mapping.samplingFrequency);
    cJSON* values = cJSON_CreateArray();
    for (size_t index = 0u; index < mapping.length; ++index) {
        cJSON_AddItemToArray(values,
                             cJSON_CreateNumber(mapping.originalValues[index]));
    }
    cJSON_AddItemToObject(json, "originalValues", values);
    return json;
}

bool parseInstallMapping(cJSON* source, ADCValuesMapping& mapping) {
    if (!source || !cJSON_IsObject(source)) return false;
    cJSON* id = cJSON_GetObjectItem(source, "id");
    cJSON* name = cJSON_GetObjectItem(source, "name");
    cJSON* length = cJSON_GetObjectItem(source, "length");
    cJSON* step = cJSON_GetObjectItem(source, "step");
    cJSON* noise = cJSON_GetObjectItem(source, "samplingNoise");
    cJSON* frequency = cJSON_GetObjectItem(source, "samplingFrequency");
    cJSON* values = cJSON_GetObjectItem(source, "originalValues");
    if (!cJSON_IsString(id) || !cJSON_IsString(name) ||
        !id->valuestring || !name->valuestring || id->valuestring[0] == '\0' ||
        name->valuestring[0] == '\0' || strlen(id->valuestring) >= sizeof(mapping.id) ||
        strlen(name->valuestring) >= sizeof(mapping.name) ||
        !cJSON_IsNumber(length) || !std::isfinite(length->valuedouble) ||
        std::floor(length->valuedouble) != length->valuedouble ||
        length->valuedouble < 2.0 || length->valuedouble > MAX_ADC_VALUES_LENGTH ||
        !cJSON_IsNumber(step) || !std::isfinite(step->valuedouble) ||
        !cJSON_IsNumber(noise) || !cJSON_IsNumber(frequency) ||
        !cJSON_IsArray(values) ||
        cJSON_GetArraySize(values) != static_cast<int>(length->valuedouble)) {
        return false;
    }
    const double noiseValue = noise->valuedouble;
    const double frequencyValue = frequency->valuedouble;
    if (noiseValue < 0.0 || noiseValue > UINT16_MAX ||
        std::floor(noiseValue) != noiseValue || frequencyValue < 1.0 ||
        frequencyValue > UINT16_MAX || std::floor(frequencyValue) != frequencyValue) {
        return false;
    }
    memset(&mapping, 0, sizeof(mapping));
    strncpy(mapping.id, id->valuestring, sizeof(mapping.id) - 1u);
    strncpy(mapping.name, name->valuestring, sizeof(mapping.name) - 1u);
    mapping.length = static_cast<size_t>(length->valuedouble);
    mapping.step = static_cast<float_t>(step->valuedouble);
    mapping.samplingNoise = static_cast<uint16_t>(noiseValue);
    mapping.samplingFrequency = static_cast<uint16_t>(frequencyValue);
    for (size_t index = 0u; index < mapping.length; ++index) {
        cJSON* sample = cJSON_GetArrayItem(values, static_cast<int>(index));
        if (!cJSON_IsNumber(sample) || !std::isfinite(sample->valuedouble) ||
            std::floor(sample->valuedouble) != sample->valuedouble ||
            sample->valuedouble < 0.0 || sample->valuedouble > UINT16_MAX) {
            return false;
        }
        mapping.originalValues[index] =
            static_cast<uint32_t>(sample->valuedouble);
    }
    return true;
}
}

DeviceCommandResponse MSMarkCommandHandler::handleInstallMapping(
    const DeviceCommandRequest& request) {
    cJSON* params = request.getParams();
    cJSON* mappingJSON = params ? cJSON_GetObjectItem(params, "mapping") : nullptr;
    cJSON* shaJSON = params ? cJSON_GetObjectItem(params, "sha256") : nullptr;
    ADCValuesMapping mapping = {};
    if (!parseInstallMapping(mappingJSON, mapping) || !cJSON_IsString(shaJSON) ||
        !shaJSON->valuestring || strlen(shaJSON->valuestring) != 64u) {
        return create_error_response(request.getCid(), request.getCommand(), 1,
                                     "Invalid server mapping payload");
    }

    const bool pipelineWasRunning = INPUT_STATE.suspendInputPipelineForStorage();
    const ADCBtnsError result = ADC_MANAGER.installSharedMapping(
        mapping, shaJSON->valuestring);
    bool runtimeReloaded = false;
    if (result == ADCBtnsError::SUCCESS) {
        runtimeReloaded = pipelineWasRunning
            ? INPUT_STATE.resumeInputPipelineAfterStorage(true)
            : ADC_BTNS_WORKER.setup() == ADCBtnsError::SUCCESS;
    } else {
        (void)INPUT_STATE.resumeInputPipelineAfterStorage(pipelineWasRunning);
        return create_error_response(request.getCid(), request.getCommand(), 1,
                                     "Mapping validation or shared storage write failed");
    }

    cJSON* data = cJSON_CreateObject();
    cJSON_AddItemToObject(data, "mapping", mappingToJSON(mapping, true));
    cJSON_AddBoolToObject(data, "calibrationCleared", true);
    cJSON_AddBoolToObject(data, "requiresCalibration", true);
    cJSON_AddBoolToObject(data, "runtimeReloaded", runtimeReloaded);
    return create_success_response(request.getCid(), request.getCommand(), data);
}

DeviceCommandResponse MSMarkCommandHandler::handleClearInstalledMapping(
    const DeviceCommandRequest& request) {
    cJSON* params = request.getParams();
    cJSON* idJSON = params ? cJSON_GetObjectItem(params, "id") : nullptr;
    if (!cJSON_IsString(idJSON) || !idJSON->valuestring ||
        idJSON->valuestring[0] == '\0') {
        return create_error_response(request.getCid(), request.getCommand(), 1,
                                     "Missing or invalid installed mapping id");
    }

    const bool pipelineWasRunning = INPUT_STATE.suspendInputPipelineForStorage();
    const ADCBtnsError result = ADC_MANAGER.clearSharedMapping(idJSON->valuestring);
    bool runtimeReloaded = false;
    if (result == ADCBtnsError::SUCCESS) {
        runtimeReloaded = pipelineWasRunning
            ? INPUT_STATE.resumeInputPipelineAfterStorage(true)
            : ADC_BTNS_WORKER.setup() == ADCBtnsError::SUCCESS;
    } else {
        (void)INPUT_STATE.resumeInputPipelineAfterStorage(pipelineWasRunning);
        return create_error_response(request.getCid(), request.getCommand(), 1,
                                     "Unable to clear installed mapping or load factory fallback");
    }

    const std::string fallbackId = ADC_MANAGER.getDefaultMapping();
    const ADCValuesMapping* fallback = ADC_MANAGER.getMapping(fallbackId.c_str());
    if (fallback == nullptr) {
        return create_error_response(request.getCid(), request.getCommand(), 1,
                                     "Factory fallback mapping is unavailable");
    }

    cJSON* data = cJSON_CreateObject();
    cJSON_AddItemToObject(data, "mapping", mappingToJSON(*fallback, true));
    cJSON_AddItemToObject(data, "mappingList", buildMappingListJSON());
    cJSON_AddStringToObject(data, "defaultMappingId", fallbackId.c_str());
    cJSON_AddStringToObject(data, "storageMode", "shared-singleton");
    cJSON_AddNumberToObject(data, "installSchemaVersion", 1);
    cJSON_AddStringToObject(data, "source", "factory-fallback");
    cJSON_AddBoolToObject(data, "calibrationCleared", true);
    cJSON_AddBoolToObject(data, "runtimeReloaded", runtimeReloaded);
    return create_success_response(request.getCid(), request.getCommand(), data);
}

DeviceCommandResponse MSMarkCommandHandler::handleDraftBegin(
    const DeviceCommandRequest& request) {
    cJSON* params = request.getParams();
    cJSON* name = params ? cJSON_GetObjectItem(params, "name") : nullptr;
    cJSON* length = params ? cJSON_GetObjectItem(params, "length") : nullptr;
    cJSON* step = params ? cJSON_GetObjectItem(params, "step") : nullptr;
    if (!cJSON_IsString(name) || !name->valuestring ||
        !cJSON_IsNumber(length) || !cJSON_IsNumber(step) ||
        !std::isfinite(length->valuedouble) ||
        std::floor(length->valuedouble) != length->valuedouble) {
        return create_error_response(request.getCid(), request.getCommand(), 1,
                                     "Invalid mapping draft parameters");
    }
    const ADCBtnsError result = ADC_BTNS_MARKER.setupDraft(
        name->valuestring, static_cast<size_t>(length->valuedouble),
        static_cast<float_t>(step->valuedouble));
    if (result != ADCBtnsError::SUCCESS) {
        return create_error_response(request.getCid(), request.getCommand(), 1,
                                     "Unable to start mapping draft");
    }
    cJSON* data = cJSON_CreateObject();
    cJSON_AddItemToObject(data, "status", ADC_BTNS_MARKER.getStepInfoJSON());
    return create_success_response(request.getCid(), request.getCommand(), data);
}

DeviceCommandResponse MSMarkCommandHandler::handleDraftGet(
    const DeviceCommandRequest& request) {
    ADCValuesMapping mapping = {};
    if (ADC_BTNS_MARKER.getDraftMapping(mapping) != ADCBtnsError::SUCCESS) {
        return create_error_response(request.getCid(), request.getCommand(), 409,
                                     "Mapping draft is not complete");
    }
    cJSON* data = cJSON_CreateObject();
    cJSON_AddItemToObject(data, "mapping", mappingToJSON(mapping, false));
    return create_success_response(request.getCid(), request.getCommand(), data);
}

DeviceCommandResponse MSMarkCommandHandler::handle(const DeviceCommandRequest& request) {
    const std::string& command = request.getCommand();
    
    if (command == "ms_get_list") {
        return handleGetList(request);
    } else if (command == "ms_get_mark_status") {
        return handleGetMarkStatus(request);
    } else if (command == "ms_set_default") {
        return handleSetDefault(request);
    } else if (command == "ms_get_default") {
        return handleGetDefault(request);
    } else if (command == "ms_create_mapping") {
        return handleCreateMapping(request);
    } else if (command == "ms_delete_mapping") {
        return handleDeleteMapping(request);
    } else if (command == "ms_rename_mapping") {
        return handleRenameMapping(request);
    } else if (command == "ms_mark_mapping_start") {
        return handleMarkMappingStart(request);
    } else if (command == "ms_mark_mapping_stop") {
        return handleMarkMappingStop(request);
    } else if (command == "ms_mark_mapping_step") {
        return handleMarkMappingStep(request);
    } else if (command == "ms_mark_mapping_sync") {
        return handleMarkMappingSync(request);
    } else if (command == "ms_get_mapping") {
        return handleGetMapping(request);
    } else if (command == "get_adc_config_backup") {
        return handleGetConfigBackup(request);
    } else if (command == "ms_install_mapping") {
        return handleInstallMapping(request);
    } else if (command == "ms_clear_installed_mapping") {
        return handleClearInstalledMapping(request);
    } else if (command == "ms_mapping_draft_begin") {
        return handleDraftBegin(request);
    } else if (command == "ms_mapping_draft_get") {
        return handleDraftGet(request);
    }
    
    return create_error_response(request.getCid(), command, -1, "Unknown command");
}
