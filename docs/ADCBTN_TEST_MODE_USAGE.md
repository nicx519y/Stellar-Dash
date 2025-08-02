# ADC按键技术测试模式使用指南

## 概述

ADC按键技术测试模式（ADCBtnTest）是一个专门用于技术测试和调试的功能，它允许在不推送virtualPin Mask的情况下，获取ADC按键触发时的详细信息。

## 功能特性

在技术测试模式下，系统会：

1. **不推送virtualPin Mask**：ADC按键的触发不会影响正常的按键掩码输出
2. **在update()中统一判断**：在WebConfigBtnsManager的update()方法中统一检查按键状态变化
3. **推送详细的触发信息**：当ADC按键触发（按下或释放）时，会推送包含以下信息的回调：
   - 按键索引和虚拟引脚
   - 触发时的ADC值
   - 触发时的物理行程（mm）
   - limitValue对应的物理行程（mm）
   - 触发事件类型（按下/释放）
   - 时间戳

## 实现原理

技术测试模式采用以下实现方式：

1. **状态变化跟踪**：在ADC按键工作器中维护一个状态变化跟踪数组
2. **统一判断**：在WebConfigBtnsManager的update()方法中统一检查所有按键的状态变化
3. **批量处理**：一次性处理所有有状态变化的按键，然后清除状态变化标志
4. **高效映射**：使用virtualPin到buttonIndex的映射表，避免循环查找，提高性能

## 使用方法

### 1. 设置测试回调

```cpp
// 设置ADC按键技术测试模式回调
WEBCONFIG_BTNS_MANAGER.setADCBtnTestCallback([](const ADCBtnTestEvent& event) {
    printf("Button %d (VP:%d) %s: ADC=%d, Travel=%.2fmm, Limit=%.2fmm\n",
           event.buttonIndex, event.virtualPin, 
           event.isPressEvent ? "PRESS" : "RELEASE",
           event.adcValue, event.triggerDistance, event.limitValueDistance);
});
```

### 2. 启用技术测试模式

```cpp
// 启用技术测试模式
WEBCONFIG_BTNS_MANAGER.enableTestMode(true);
```

### 3. 启动按键工作器

```cpp
// 启动按键工作器
WEBCONFIG_BTNS_MANAGER.startButtonWorkers();
```

### 4. 在主循环中调用update

```cpp
// 在主循环中调用update()方法
// 在技术测试模式下，update()会自动检查按键状态变化并调用测试回调
WEBCONFIG_BTNS_MANAGER.update();
```

### 5. 禁用技术测试模式

```cpp
// 禁用技术测试模式，恢复正常模式
WEBCONFIG_BTNS_MANAGER.enableTestMode(false);
```

## 数据结构

### ADCBtnTestEvent结构体

```cpp
struct ADCBtnTestEvent {
    uint8_t buttonIndex;           // 按键索引
    uint8_t virtualPin;            // 虚拟引脚
    uint16_t adcValue;             // 触发时的ADC值
    float triggerDistance;         // 触发时的物理行程（mm）
    float limitValueDistance;      // limitValue对应的物理行程（mm）
    uint16_t limitValue;           // limitValue值
    bool isPressEvent;             // 是否为按下事件（true=按下，false=释放）
    uint32_t timestamp;            // 时间戳
};
```

## 应用场景

1. **按键性能测试**：测试按键的响应时间和精度
2. **行程校准验证**：验证物理行程与ADC值的对应关系
3. **触发阈值调试**：调试按下和释放的触发阈值
4. **按键质量评估**：评估按键的稳定性和一致性

## 注意事项

1. 技术测试模式会禁用ADC按键的正常掩码输出，只保留GPIO按键的正常功能
2. 测试完成后应及时禁用技术测试模式，恢复正常功能
3. 回调函数中应避免执行耗时操作，以免影响系统性能
4. 技术测试模式与正常模式可以随时切换，无需重启系统

## 示例输出

```
Button 0 (VP:0) PRESS: ADC=2048, Travel=2.50mm, Limit=1.80mm
Button 0 (VP:0) RELEASE: ADC=1024, Travel=4.20mm, Limit=5.10mm
Button 1 (VP:1) PRESS: ADC=3072, Travel=1.80mm, Limit=1.20mm
```

这些信息可以帮助开发者了解按键的触发行为、物理行程和ADC值的对应关系，以及limitValue的计算逻辑。

## WebSocket接口集成示例

如果需要通过WebSocket接口提供技术测试模式，可以这样实现：

```cpp
// WebSocket命令处理示例
void handleADCBtnTestCommand(const cJSON* command) {
    cJSON* action = cJSON_GetObjectItem(command, "action");
    if (!action || !cJSON_IsString(action)) {
        return;
    }
    
    if (strcmp(action->valuestring, "enable_test_mode") == 0) {
        // 启用技术测试模式
        WEBCONFIG_BTNS_MANAGER.setADCBtnTestCallback([](const ADCBtnTestEvent& event) {
            // 构造WebSocket消息
            cJSON* testEvent = cJSON_CreateObject();
            cJSON_AddStringToObject(testEvent, "type", "adc_btn_test_event");
            cJSON_AddNumberToObject(testEvent, "buttonIndex", event.buttonIndex);
            cJSON_AddNumberToObject(testEvent, "virtualPin", event.virtualPin);
            cJSON_AddNumberToObject(testEvent, "adcValue", event.adcValue);
            cJSON_AddNumberToObject(testEvent, "triggerDistance", event.triggerDistance);
            cJSON_AddNumberToObject(testEvent, "limitValueDistance", event.limitValueDistance);
            cJSON_AddNumberToObject(testEvent, "limitValue", event.limitValue);
            cJSON_AddBoolToObject(testEvent, "isPressEvent", event.isPressEvent);
            cJSON_AddNumberToObject(testEvent, "timestamp", event.timestamp);
            
            // 发送WebSocket消息
            sendWebSocketMessage(testEvent);
            cJSON_Delete(testEvent);
        });
        
        WEBCONFIG_BTNS_MANAGER.enableTestMode(true);
        
        // 返回成功响应
        cJSON* response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "status", "success");
        cJSON_AddStringToObject(response, "message", "ADC button test mode enabled");
        sendWebSocketResponse(response);
        cJSON_Delete(response);
        
    } else if (strcmp(action->valuestring, "disable_test_mode") == 0) {
        // 禁用技术测试模式
        WEBCONFIG_BTNS_MANAGER.enableTestMode(false);
        
        // 返回成功响应
        cJSON* response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "status", "success");
        cJSON_AddStringToObject(response, "message", "ADC button test mode disabled");
        sendWebSocketResponse(response);
        cJSON_Delete(response);
    }
}
```

## WebSocket推送

在技术测试模式下，系统会自动通过WebSocket推送ADC按键测试事件。系统会在每次`update()`调用时收集所有有状态变化的按键事件，然后一次性批量推送到所有连接的客户端。

### 二进制数据格式

```cpp
// 推送头部
struct ADCBtnTestEventBinaryData {
    uint8_t command;        // 命令号：2 表示ADC按键测试事件
    uint8_t eventCount;     // 本次推送的事件数量
    uint32_t timestamp;     // 时间戳
    uint8_t reserved[2];    // 保留字节
};

// 单个事件项
struct ADCBtnTestEventItem {
    uint8_t buttonIndex;    // 按键索引
    uint8_t virtualPin;     // 虚拟引脚
    uint16_t adcValue;      // 触发时的ADC值
    float triggerDistance;  // 触发时的物理行程（mm）
    float limitValueDistance; // limitValue对应的物理行程（mm）
    uint16_t limitValue;    // limitValue值
    uint8_t isPressEvent;   // 是否为按下事件（1=按下，0=释放）
};
```

### 数据布局

推送的二进制数据按以下顺序排列：
1. `ADCBtnTestEventBinaryData` 头部（8字节）
2. `ADCBtnTestEventItem` 数组（每个事件项20字节）
3. 总大小 = 8 + eventCount × 20 字节

### 前端接收示例

```javascript
// WebSocket连接
const ws = new WebSocket('ws://192.168.1.100:8081');

ws.onmessage = function(event) {
    if (event.data instanceof ArrayBuffer) {
        // 处理二进制数据
        const data = new DataView(event.data);
        const command = data.getUint8(0);
        
        if (command === 2) { // ADC按键测试事件
            const eventCount = data.getUint8(1);
            const timestamp = data.getUint32(2, true);
            
            console.log(`Received ${eventCount} ADC button test events at timestamp ${timestamp}`);
            
            // 解析每个事件项
            let offset = 8; // 跳过头部
            for (let i = 0; i < eventCount; i++) {
                const buttonIndex = data.getUint8(offset);
                const virtualPin = data.getUint8(offset + 1);
                const adcValue = data.getUint16(offset + 2, true);
                const triggerDistance = data.getFloat32(offset + 4, true);
                const limitValueDistance = data.getFloat32(offset + 8, true);
                const limitValue = data.getUint16(offset + 12, true);
                const isPressEvent = data.getUint8(offset + 14);
                
                console.log(`Event ${i}: Button ${buttonIndex} (VP:${virtualPin}) ${isPressEvent ? 'PRESS' : 'RELEASE'}: ADC=${adcValue}, Travel=${triggerDistance.toFixed(2)}mm, Limit=${limitValueDistance.toFixed(2)}mm`);
                
                offset += 20; // 移动到下一个事件项
            }
        }
    }
};
```

### 推送时机

- 在每次`update()`调用时，系统会检查所有ADC按键的状态变化
- 收集所有有状态变化的按键事件到事件收集器
- 在`update()`结束时，如果收集器中有事件，则调用`sendADCBtnTestEventNotification()`
- `sendADCBtnTestEventNotification()`调用`buildADCBtnTestEventBinaryData()`构建完整的二进制数据
- 数据组装在`buildADCBtnTestEventBinaryData()`方法中完成，包括头部和所有事件项的组装
- 推送后清空事件收集器，为下次`update()`做准备
- 自动推送到所有连接的WebSocket客户端
- 无需手动调用，系统自动处理

## 技术测试模式的优势

1. **精确的数据获取**：提供详细的触发信息，包括ADC值、物理行程和limitValue
2. **不影响正常功能**：在测试模式下，ADC按键不会影响正常的按键掩码输出
3. **统一处理**：在update()方法中统一判断和处理所有按键状态变化，避免频繁回调
4. **实时监控**：可以实时监控按键的触发行为和性能
5. **易于集成**：通过回调机制，可以轻松集成到WebSocket、串口或其他通信接口中
6. **灵活切换**：可以随时启用或禁用测试模式，无需重启系统
7. **性能优化**：批量处理状态变化，减少系统开销
8. **高效映射**：使用virtualPin到buttonIndex的映射表，O(1)时间复杂度获取buttonIndex
9. **自动WebSocket推送**：测试事件自动通过WebSocket推送到前端，无需额外配置
10. **二进制数据格式**：使用紧凑的二进制格式，减少网络传输开销
11. **批量推送机制**：在每次update()时收集所有事件并一次性推送，减少网络包数量
12. **事件同步性**：同一update()周期内的所有事件共享相同的时间戳，便于分析 