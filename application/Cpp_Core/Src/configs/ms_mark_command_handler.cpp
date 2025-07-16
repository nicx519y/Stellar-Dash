#include "configs/websocket_command_handler.hpp"
#include "storagemanager.hpp"
#include "adc_btns/adc_manager.hpp"
#include "adc_btns/adc_btns_marker.hpp"
#include "system_logger.h"
#include <map>

// ============================================================================
// MSMarkCommandHandler 实现
// ============================================================================

MSMarkCommandHandler& MSMarkCommandHandler::getInstance() {
    static MSMarkCommandHandler instance;
    return instance;
}

cJSON* MSMarkCommandHandler::buildMappingListJSON() {
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

WebSocketDownstreamMessage MSMarkCommandHandler::handleGetList(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling ms_get_list command, cid: %d", request.getCid());

    cJSON* dataJSON = cJSON_CreateObject();
    // 添加映射列表到响应数据
    cJSON_AddItemToObject(dataJSON, "mappingList", buildMappingListJSON());
    cJSON_AddItemToObject(dataJSON, "defaultMappingId", cJSON_CreateString(ADC_MANAGER.getDefaultMapping().c_str()));
    
    LOG_INFO("WebSocket", "ms_get_list command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

WebSocketDownstreamMessage MSMarkCommandHandler::handleGetMarkStatus(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling ms_get_mark_status command, cid: %d", request.getCid());
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* markStatusJSON = ADC_BTNS_MARKER.getStepInfoJSON();
    
    // 添加标记状态到响应数据
    cJSON_AddItemToObject(dataJSON, "status", markStatusJSON);
    
    LOG_INFO("WebSocket", "ms_get_mark_status command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

WebSocketDownstreamMessage MSMarkCommandHandler::handleSetDefault(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling ms_set_default command, cid: %d", request.getCid());
    
    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("WebSocket", "ms_set_default: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }
    
    // 获取映射ID
    cJSON* idJSON = cJSON_GetObjectItem(params, "id");
    if (!idJSON || !cJSON_IsString(idJSON)) {
        LOG_ERROR("WebSocket", "ms_set_default: Missing or invalid mapping id");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing or invalid mapping id");
    }
    
    const char* mappingId = idJSON->valuestring;

    // 设置默认映射
    ADCBtnsError error = ADC_MANAGER.setDefaultMapping(mappingId);
    if(error != ADCBtnsError::SUCCESS) {
        LOG_ERROR("WebSocket", "ms_set_default: Failed to set default mapping");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to set default mapping");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddStringToObject(dataJSON, "id", mappingId);

    LOG_INFO("WebSocket", "ms_set_default command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

WebSocketDownstreamMessage MSMarkCommandHandler::handleGetDefault(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling ms_get_default command, cid: %d", request.getCid());
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    
    // 获取默认映射名称
    std::string defaultId = ADC_MANAGER.getDefaultMapping();
    if(defaultId.empty()) {
        cJSON_AddStringToObject(dataJSON, "id", "");
    } else {
        cJSON_AddStringToObject(dataJSON, "id", defaultId.c_str());
    }
    
    LOG_INFO("WebSocket", "ms_get_default command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

WebSocketDownstreamMessage MSMarkCommandHandler::handleCreateMapping(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling ms_create_mapping command, cid: %d", request.getCid());
    
    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("WebSocket", "ms_create_mapping: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }
    
    // 获取映射名称
    cJSON* nameJSON = cJSON_GetObjectItem(params, "name");
    if (!nameJSON || !cJSON_IsString(nameJSON)) {
        LOG_ERROR("WebSocket", "ms_create_mapping: Missing or invalid mapping name");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing or invalid mapping name");
    }
    
    // 获取长度
    cJSON* lengthJSON = cJSON_GetObjectItem(params, "length");
    if (!lengthJSON || !cJSON_IsNumber(lengthJSON)) {
        LOG_ERROR("WebSocket", "ms_create_mapping: Missing or invalid length");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing or invalid length");
    }
    
    // 获取步长
    cJSON* stepJSON = cJSON_GetObjectItem(params, "step");
    if (!stepJSON || !cJSON_IsNumber(stepJSON)) {
        LOG_ERROR("WebSocket", "ms_create_mapping: Missing or invalid step");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing or invalid step");
    }
    
    const char* mappingName = nameJSON->valuestring;
    size_t length = lengthJSON->valueint;
    float_t step = stepJSON->valuedouble;
    
    // 创建映射
    ADCBtnsError error = ADC_MANAGER.createADCMapping(mappingName, length, step);
    if(error != ADCBtnsError::SUCCESS) {
        LOG_ERROR("WebSocket", "ms_create_mapping: Failed to create mapping");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to create mapping");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddItemToObject(dataJSON, "defaultMappingId", cJSON_CreateString(ADC_MANAGER.getDefaultMapping().c_str()));
    cJSON_AddItemToObject(dataJSON, "mappingList", buildMappingListJSON());
    
    LOG_INFO("WebSocket", "ms_create_mapping command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

WebSocketDownstreamMessage MSMarkCommandHandler::handleDeleteMapping(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling ms_delete_mapping command, cid: %d", request.getCid());
    
    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("WebSocket", "ms_delete_mapping: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }
    
    // 获取映射ID
    cJSON* idJSON = cJSON_GetObjectItem(params, "id");
    if (!idJSON || !cJSON_IsString(idJSON)) {
        LOG_ERROR("WebSocket", "ms_delete_mapping: Missing or invalid mapping id");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing or invalid mapping id");
    }
    
    const char* mappingId = idJSON->valuestring;
    
    // 删除映射
    ADCBtnsError error = ADC_MANAGER.removeADCMapping(mappingId);
    if(error != ADCBtnsError::SUCCESS) {
        LOG_ERROR("WebSocket", "ms_delete_mapping: Failed to delete mapping");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to delete mapping");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddItemToObject(dataJSON, "defaultMappingId", cJSON_CreateString(ADC_MANAGER.getDefaultMapping().c_str()));
    cJSON_AddItemToObject(dataJSON, "mappingList", buildMappingListJSON());
    
    LOG_INFO("WebSocket", "ms_delete_mapping command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

WebSocketDownstreamMessage MSMarkCommandHandler::handleRenameMapping(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling ms_rename_mapping command, cid: %d", request.getCid());
    
    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("WebSocket", "ms_rename_mapping: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    // 获取映射ID
    cJSON* idJSON = cJSON_GetObjectItem(params, "id");
    if (!idJSON || !cJSON_IsString(idJSON)) {
        LOG_ERROR("WebSocket", "ms_rename_mapping: Missing or invalid mapping id");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing or invalid mapping id");
    }

    // 获取映射名称
    cJSON* nameJSON = cJSON_GetObjectItem(params, "name");
    if (!nameJSON || !cJSON_IsString(nameJSON)) {
        LOG_ERROR("WebSocket", "ms_rename_mapping: Missing or invalid mapping name");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing or invalid mapping name");
    }

    const char* mappingId = idJSON->valuestring;
    const char* mappingName = nameJSON->valuestring;

    // 重命名映射
    ADCBtnsError error = ADC_MANAGER.renameADCMapping(mappingId, mappingName);
    if(error != ADCBtnsError::SUCCESS) {   
        LOG_ERROR("WebSocket", "ms_rename_mapping: Failed to rename mapping");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to rename mapping");
    }

    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddItemToObject(dataJSON, "defaultMappingId", cJSON_CreateString(ADC_MANAGER.getDefaultMapping().c_str()));
    cJSON_AddItemToObject(dataJSON, "mappingList", buildMappingListJSON());

    LOG_INFO("WebSocket", "ms_rename_mapping command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

WebSocketDownstreamMessage MSMarkCommandHandler::handleMarkMappingStart(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling ms_mark_mapping_start command, cid: %d", request.getCid());
    
    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("WebSocket", "ms_mark_mapping_start: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }
    
    // 获取映射ID
    cJSON* idJSON = cJSON_GetObjectItem(params, "id");
    if (!idJSON || !cJSON_IsString(idJSON)) {
        LOG_ERROR("WebSocket", "ms_mark_mapping_start: Missing or invalid mapping id");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing or invalid mapping id");
    }
    
    const char* mappingId = idJSON->valuestring;
    
    // 开始标记
    ADCBtnsError error = ADC_BTNS_MARKER.setup(mappingId);
    if(error != ADCBtnsError::SUCCESS) {
        LOG_ERROR("WebSocket", "ms_mark_mapping_start: Failed to start marking");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to start marking");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* statusJSON = ADC_BTNS_MARKER.getStepInfoJSON();
    cJSON_AddItemToObject(dataJSON, "status", statusJSON);
    
    LOG_INFO("WebSocket", "ms_mark_mapping_start command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

WebSocketDownstreamMessage MSMarkCommandHandler::handleMarkMappingStop(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling ms_mark_mapping_stop command, cid: %d", request.getCid());
    
    // 停止标记
    ADC_BTNS_MARKER.reset();
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* statusJSON = ADC_BTNS_MARKER.getStepInfoJSON();
    cJSON_AddItemToObject(dataJSON, "status", statusJSON);
    
    LOG_INFO("WebSocket", "ms_mark_mapping_stop command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

WebSocketDownstreamMessage MSMarkCommandHandler::handleMarkMappingStep(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling ms_mark_mapping_step command, cid: %d", request.getCid());
    
    // 执行标记步进
    ADCBtnsError error = ADC_BTNS_MARKER.step();
    if(error != ADCBtnsError::SUCCESS) {
        LOG_ERROR("WebSocket", "ms_mark_mapping_step: Failed to perform marking step");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to perform marking step");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* statusJSON = ADC_BTNS_MARKER.getStepInfoJSON();
    cJSON_AddItemToObject(dataJSON, "status", statusJSON);
    
    LOG_INFO("WebSocket", "ms_mark_mapping_step command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

WebSocketDownstreamMessage MSMarkCommandHandler::handleGetMapping(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling ms_get_mapping command, cid: %d", request.getCid());
    
    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("WebSocket", "ms_get_mapping: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    cJSON* idJSON = cJSON_GetObjectItem(params, "id");
    if (!idJSON || !cJSON_IsString(idJSON)) {
        LOG_ERROR("WebSocket", "ms_get_mapping: Missing or invalid mapping id");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing or invalid mapping id");
    }

    const ADCValuesMapping* resultMapping = ADC_MANAGER.getMapping(idJSON->valuestring);
    if (!resultMapping) {
        LOG_ERROR("WebSocket", "ms_get_mapping: Failed to get mapping");
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

    LOG_INFO("WebSocket", "ms_get_mapping command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

WebSocketDownstreamMessage MSMarkCommandHandler::handle(const WebSocketUpstreamMessage& request) {
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
    }
    
    return create_error_response(request.getCid(), command, -1, "Unknown command");
} 