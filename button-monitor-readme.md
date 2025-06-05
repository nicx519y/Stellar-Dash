# 按键监控管理器 (ButtonMonitorManager)

## 功能概述

`ButtonMonitorManager` 是一个用于管理按键监控的React组件，提供以下功能：

1. **自动轮询管理**: 开启监控后自动进行半秒一次的状态轮询
2. **事件发布系统**: 监听按键变化并发布 `button-press`、`button-release`、`button-change` 事件
3. **状态同步**: 与 gamepad-config-context 中的按键监控接口同步
4. **生命周期管理**: 组件挂载/卸载时的自动清理

## 基本用法

### 1. 作为渲染函数组件使用

```tsx
import { ButtonMonitorManager } from '@/components/button-monitor-manager';

function MyComponent() {
    return (
        <ButtonMonitorManager
            autoStart={false}
            pollingInterval={500}
            autoStopOnUnmount={true}
            onMonitoringStateChange={(isActive) => {
                console.log('监控状态:', isActive);
            }}
            onButtonStatesChange={(states) => {
                console.log('按键状态:', states);
            }}
            onError={(error) => {
                console.error('错误:', error);
            }}
        >
            {({ 
                isMonitoring, 
                isPolling, 
                lastButtonStates, 
                startMonitoring, 
                stopMonitoring,
                addEventListener,
                removeEventListener
            }) => (
                <div>
                    <button onClick={startMonitoring} disabled={isMonitoring}>
                        开始监控
                    </button>
                    <button onClick={stopMonitoring} disabled={!isMonitoring}>
                        停止监控
                    </button>
                    <p>监控状态: {isMonitoring ? '活跃' : '停止'}</p>
                    <p>轮询状态: {isPolling ? '运行中' : '停止'}</p>
                </div>
            )}
        </ButtonMonitorManager>
    );
}
```

### 2. 使用 useButtonMonitor Hook

```tsx
import { useButtonMonitor } from '@/components/button-monitor-manager';

function MyComponent() {
    const { manager, ButtonMonitorComponent } = useButtonMonitor({
        autoStart: false,
        pollingInterval: 500,
        autoStopOnUnmount: true
    });

    return (
        <div>
            <ButtonMonitorComponent />
            
            {manager && (
                <div>
                    <button onClick={manager.startMonitoring}>开始监控</button>
                    <button onClick={manager.stopMonitoring}>停止监控</button>
                    <p>状态: {manager.isMonitoring ? '活跃' : '停止'}</p>
                </div>
            )}
        </div>
    );
}
```

### 3. 作为无UI服务组件使用

```tsx
function MyComponent() {
    return (
        <div>
            {/* 无UI的后台监控服务 */}
            <ButtonMonitorManager
                autoStart={true}
                onButtonStatesChange={(states) => {
                    // 处理按键状态变化
                }}
            />
            
            {/* 其他组件内容 */}
            <div>我的应用内容</div>
        </div>
    );
}
```

## 事件监听

### 添加按键事件监听器

```tsx
useEffect(() => {
    const handleButtonEvent = (event) => {
        switch (event.type) {
            case 'button-press':
                console.log(`按键 ${event.buttonIndex} 被按下`);
                break;
            case 'button-release':
                console.log(`按键 ${event.buttonIndex} 被释放`);
                break;
            case 'button-change':
                console.log('按键状态发生变化:', event.triggerBinary);
                break;
        }
    };

    // 通过渲染函数参数添加监听器
    addEventListener(handleButtonEvent);
    
    return () => removeEventListener(handleButtonEvent);
}, [addEventListener, removeEventListener]);
```

### 按键事件对象结构

```typescript
interface ButtonEvent {
    type: 'button-press' | 'button-release' | 'button-change';
    buttonIndex: number;        // 按键索引（-1 表示整体变化事件）
    isPressed: boolean;         // 是否被按下
    triggerMask: number;        // 触发掩码
    triggerBinary: string;      // 二进制表示
    totalButtons: number;       // 总按键数
    timestamp: number;          // 时间戳
}
```

## 配置选项

| 属性 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `autoStart` | boolean | false | 组件挂载时是否自动开始监控 |
| `pollingInterval` | number | 500 | 轮询间隔(毫秒) |
| `autoStopOnUnmount` | boolean | true | 组件卸载时是否自动停止监控 |
| `onMonitoringStateChange` | function | - | 监控状态变化回调 |
| `onButtonStatesChange` | function | - | 按键状态变化回调 |
| `onError` | function | - | 错误处理回调 |
| `children` | function | - | 渲染函数 |

## 渲染函数参数

| 参数 | 类型 | 说明 |
|------|------|------|
| `isMonitoring` | boolean | 是否正在监控 |
| `isPolling` | boolean | 是否正在轮询 |
| `lastButtonStates` | ButtonStates | 最新的按键状态 |
| `startMonitoring` | function | 开始监控函数 |
| `stopMonitoring` | function | 停止监控函数 |
| `addEventListener` | function | 添加事件监听器 |
| `removeEventListener` | function | 移除事件监听器 |

## 完整示例

查看 `button-monitor-demo.tsx` 文件获取完整的使用示例，包括：
- 监控状态显示
- 按键状态可视化
- 事件记录和历史
- 完整的错误处理

## 注意事项

1. **性能考虑**: 默认轮询间隔为500ms，可根据需求调整
2. **内存管理**: 组件会自动清理定时器和事件监听器
3. **错误处理**: 建议总是提供 `onError` 回调来处理异常
4. **状态同步**: 组件状态与 gamepad-config-context 保持同步 