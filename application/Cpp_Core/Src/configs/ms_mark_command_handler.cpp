#include "configs/device_command_handler.hpp"
#include "storagemanager.hpp"
#include "adc_btns/adc_manager.hpp"
#include "adc_btns/adc_btns_marker.hpp"
#include "system_logger.h"
#include <map>
#include <cstring>
#include "config_transport_sink.hpp"

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
    size_t length = lengthJSON->valueint;
    float_t step = stepJSON->valuedouble;
    
    // 创建映射
    ADCBtnsError error = ADC_MANAGER.createADCMapping(mappingName, length, step);
    if(error != ADCBtnsError::SUCCESS) {
        LOG_ERROR("DeviceCommand", "ms_create_mapping: Failed to create mapping");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to create mapping");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
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

DeviceCommandResponse MSMarkCommandHandler::handleGetConfigBackup(const DeviceCommandRequest& request) {
    ADCValuesMappingStore store = {};
    ADCCommonConfig common = {};
    ADC_MANAGER.copyBackup(store, common);

    cJSON* backupJSON = cJSON_CreateObject();
    cJSON_AddNumberToObject(backupJSON, "version", 1);
    cJSON_AddStringToObject(backupJSON, "defaultMappingId", common.defaultMappingId);
    cJSON_AddStringToObject(backupJSON, "calibratedMappingId", common.calibratedMappingId);

    cJSON* mappingsJSON = cJSON_CreateArray();
    for (uint8_t i = 0; i < store.num; ++i) {
        const ADCValuesMapping& mapping = store.mapping[i];
        cJSON* mappingJSON = cJSON_CreateObject();
        cJSON_AddStringToObject(mappingJSON, "id", mapping.id);
        cJSON_AddStringToObject(mappingJSON, "name", mapping.name);
        cJSON_AddNumberToObject(mappingJSON, "length", mapping.length);
        cJSON_AddNumberToObject(mappingJSON, "step", mapping.step);
        cJSON_AddNumberToObject(mappingJSON, "samplingFrequency", mapping.samplingFrequency);
        cJSON_AddNumberToObject(mappingJSON, "samplingNoise", mapping.samplingNoise);
        cJSON* valuesJSON = cJSON_CreateArray();
        for (size_t j = 0; j < mapping.length; ++j) {
            cJSON_AddItemToArray(valuesJSON, cJSON_CreateNumber(mapping.originalValues[j]));
        }
        cJSON_AddItemToObject(mappingJSON, "originalValues", valuesJSON);
        cJSON_AddItemToArray(mappingsJSON, mappingJSON);
    }
    cJSON_AddItemToObject(backupJSON, "mappings", mappingsJSON);

    cJSON* manualJSON = cJSON_CreateArray();
    cJSON* autoJSON = cJSON_CreateArray();
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; ++i) {
        cJSON* manualPair = cJSON_CreateObject();
        cJSON_AddNumberToObject(manualPair, "topValue", common.manualCalibrationValues[i].topValue);
        cJSON_AddNumberToObject(manualPair, "bottomValue", common.manualCalibrationValues[i].bottomValue);
        cJSON_AddItemToArray(manualJSON, manualPair);

        cJSON* autoPair = cJSON_CreateObject();
        cJSON_AddNumberToObject(autoPair, "topValue", common.autoCalibrationValues[i].topValue);
        cJSON_AddNumberToObject(autoPair, "bottomValue", common.autoCalibrationValues[i].bottomValue);
        cJSON_AddItemToArray(autoJSON, autoPair);
    }
    cJSON_AddItemToObject(backupJSON, "manualCalibrationValues", manualJSON);
    cJSON_AddItemToObject(backupJSON, "autoCalibrationValues", autoJSON);

    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddItemToObject(dataJSON, "adcConfig", backupJSON);
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
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
    } else if (command == "ms_get_mapping") {
        return handleGetMapping(request);
    } else if (command == "get_adc_config_backup") {
        return handleGetConfigBackup(request);
    }
    
    return create_error_response(request.getCid(), command, -1, "Unknown command");
}
