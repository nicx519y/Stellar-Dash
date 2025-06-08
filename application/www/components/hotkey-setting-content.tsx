"use client";

import { useEffect, useState, useCallback, useRef } from "react";
import {
    Stack,
    Fieldset,
    Text,
    Card,
} from "@chakra-ui/react";
import {
    HotkeyAction,
    DEFAULT_NUM_HOTKEYS_MAX,
    Hotkey,
} from "@/types/gamepad-config";
import HotkeysField from "./hotkeys-field";
import { toaster } from "@/components/ui/toaster";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import useUnsavedChangesWarning from "@/hooks/use-unsaved-changes-warning";
import { useLanguage } from "@/contexts/language-context";
import { ContentActionButtons } from "@/components/content-action-buttons";
import { ButtonEvent } from "@/components/button-monitor-manager";
import { useButtonMonitor } from "@/hooks/use-button-monitor";

interface HotkeySettingContentProps {
    /** 是否禁用组件（例如在校准模式下） */
    disabled?: boolean;
    /** 活跃的热键索引 */
    activeHotkeyIndex?: number;
    /** 活跃热键索引变化回调 */
    onActiveHotkeyIndexChange?: (index: number) => void;
    /** 热键更新回调 */
    onHotkeyUpdate?: (index: number, hotkey: Hotkey) => void;
    /** 自定义宽度 */
    width?: string;
    /** 自定义高度 */
    height?: string;
    /** 是否启用设备按键监控 */
    isButtonMonitoringEnabled?: boolean;
    /** 按键监控开关回调 */
    onButtonMonitoringToggle?: (enabled: boolean) => void;
}

export function HotkeySettingContent({
    disabled = false,
    activeHotkeyIndex: externalActiveIndex,
    onActiveHotkeyIndexChange,
    onHotkeyUpdate,
    width = "778px",
    height = "100%",
    isButtonMonitoringEnabled = false,
    onButtonMonitoringToggle,
}: HotkeySettingContentProps) {
    const { t } = useLanguage();
    const {
        hotkeysConfig,
        updateHotkeysConfig,
        fetchHotkeysConfig,
        calibrationStatus,
    } = useGamepadConfig();

    const [_isDirty, setIsDirty] = useUnsavedChangesWarning();
    const [hotkeys, setHotkeys] = useState<Hotkey[]>([]);
    const [internalActiveIndex, setInternalActiveIndex] = useState<number>(0);

    // 使用 ref 存储最新的状态，避免事件监听器重复创建
    const hotkeysRef = useRef<Hotkey[]>([]);
    const activeHotkeyIndexRef = useRef<number>(0);

    // 使用新的按键监控 hook
    const buttonMonitor = useButtonMonitor({
        pollingInterval: 500,
        onError: (error) => {
            console.error('按键监控错误:', error);
        },
    });

    // 使用外部提供的活跃索引，否则使用内部状态
    const activeHotkeyIndex = externalActiveIndex !== undefined ? externalActiveIndex : internalActiveIndex;
    const setActiveHotkeyIndex = onActiveHotkeyIndexChange || setInternalActiveIndex;

    // 更新 ref 中的状态
    useEffect(() => {
        hotkeysRef.current = hotkeys;
    }, [hotkeys]);

    useEffect(() => {
        activeHotkeyIndexRef.current = activeHotkeyIndex;
    }, [activeHotkeyIndex]);

    // 从 gamepadConfig 加载 hotkeys 配置
    useEffect(() => {
        setHotkeys(Array.from({ length: DEFAULT_NUM_HOTKEYS_MAX }, (_, i) => {
            return hotkeysConfig?.[i] ?? { key: -1, action: HotkeyAction.None, isLocked: false, isHold: false };
        }));
        setIsDirty?.(false);
    }, [hotkeysConfig, setIsDirty]);


    // 更新单个热键
    const updateHotkey = useCallback((index: number, hotkey: Hotkey) => {
        if (index < 0 || index >= DEFAULT_NUM_HOTKEYS_MAX) return;

        const keys = hotkeysRef.current.map(h => h.key);
        const keyIndex = keys.indexOf(hotkey.key);
        
        // 如果热键已经被绑定到其他位置，显示错误提示
        if (keyIndex >= 0 && keyIndex !== index && hotkey.key >= 0) {
            toaster.create({
                title: t.ERROR_KEY_ALREADY_BINDED_TITLE,
                description: t.ERROR_KEY_ALREADY_BINDED_DESC,
                type: "error",
            });
            return;
        }

        const newHotkeys = hotkeysRef.current.slice();
        newHotkeys[index] = hotkey;
        setHotkeys(newHotkeys);
        setIsDirty?.(true);

        // 调用外部回调
        onHotkeyUpdate?.(index, hotkey);
    }, [t, setIsDirty, onHotkeyUpdate]);

    // 监听设备按键事件 - 移除会频繁变化的依赖项
    useEffect(() => {
        const handleDeviceButtonEvent = (event: CustomEvent<ButtonEvent>) => {
            const buttonEvent = event.detail;
            
            console.log('Hotkey handleDeviceButtonEvent called:', {
                type: buttonEvent.type,
                buttonIndex: buttonEvent.buttonIndex,
                timestamp: Date.now()
            });
            
            // 只处理按键按下事件，并且只在启用监控时
            if (buttonEvent.type === 'button-press' && 
                isButtonMonitoringEnabled && 
                !disabled && 
                !calibrationStatus.isActive) {
                
                const currentActiveIndex = activeHotkeyIndexRef.current;
                const currentHotkeys = hotkeysRef.current;
                
                console.log('Processing button press for hotkey binding:', {
                    activeIndex: currentActiveIndex,
                    buttonIndex: buttonEvent.buttonIndex
                });
                
                // 获取当前热键设置并更新
                const currentHotkey = currentHotkeys[currentActiveIndex] || { key: -1, action: HotkeyAction.None, isHold: false, isLocked: false };
                updateHotkey(currentActiveIndex, { 
                    ...currentHotkey,
                    key: buttonEvent.buttonIndex
                });
            }
        };

        // 添加事件监听器
        window.addEventListener('device-button-event', handleDeviceButtonEvent as EventListener);

        // 清理函数
        return () => {
            window.removeEventListener('device-button-event', handleDeviceButtonEvent as EventListener);
        };
    }, [isButtonMonitoringEnabled, disabled, calibrationStatus.isActive, updateHotkey]);

    // 自动选择下一个可用的热键索引（如果当前的被锁定）
    useEffect(() => {
        if (activeHotkeyIndex >= 0 && hotkeys[activeHotkeyIndex]?.isLocked === true) {
            const index = hotkeys.findIndex(h => h.isLocked === undefined || h.isLocked === false);
            if (index >= 0) {
                setActiveHotkeyIndex(index);
            }
        }
    }, [hotkeys, activeHotkeyIndex, setActiveHotkeyIndex]);

    

    // 监听外部点击事件（从Hitbox组件）
    useEffect(() => {
        const handleHitboxClick = (event: CustomEvent) => {
            const { keyId, activeHotkeyIndex: clickActiveIndex } = event.detail;
            // 只有当前组件的活跃索引与点击时的活跃索引匹配时才处理
            if (clickActiveIndex === activeHotkeyIndex && keyId >= 0 && keyId < 20) {
                updateHotkey(activeHotkeyIndex, { 
                    ...hotkeys[activeHotkeyIndex], 
                    key: keyId 
                });
            }
        };

        window.addEventListener('hitbox-click', handleHitboxClick as EventListener);
        
        return () => {
            window.removeEventListener('hitbox-click', handleHitboxClick as EventListener);
        };
    }, [activeHotkeyIndex, hotkeys, updateHotkey]);

    // 保存热键配置
    const saveHotkeysConfigHandler = async () => {
        if (!hotkeysConfig) return;
        await updateHotkeysConfig(hotkeys);
    };

    // 处理热键字段点击
    const handleHotkeyFieldClick = (index: number) => {
        setActiveHotkeyIndex(index);
    };

    // 重置热键配置
    const resetHotkeysConfigHandler = async () => {
        await fetchHotkeysConfig();
    };

    const handleButtonMonitoringToggle = async (checked: boolean) => {
        if (checked) {
            await buttonMonitor.startMonitoring();
            console.log('Button monitoring started');
            onButtonMonitoringToggle?.(true);
        } else {
            await buttonMonitor.stopMonitoring();
            console.log('Button monitoring stopped');
            onButtonMonitoringToggle?.(false);
        }
    };

    /**
     * 组件挂载时，如果组件未禁用，则开启按键监控,
     * 组件卸载时，关闭按键监控
     * 
     */
    useEffect(() => {
        if (!disabled) {
            handleButtonMonitoringToggle(true);
        } else {
            handleButtonMonitoringToggle(false);
        }
        return () => {
            handleButtonMonitoringToggle(false);
        };
    }, [disabled]);

    return (
        <Card.Root w={width} h={height}>
            <Card.Header>
                <Card.Title fontSize="md">
                    <Text fontSize="32px" fontWeight="normal" color="green.600">
                        {t.SETTINGS_HOTKEYS_TITLE}
                    </Text>
                </Card.Title>
                <Card.Description fontSize="sm" pt={4} pb={4} whiteSpace="pre-wrap">
                    {t.SETTINGS_HOTKEYS_HELPER_TEXT}
                </Card.Description>
                
            </Card.Header>

            <Card.Body>
                <Fieldset.Root>
                    <Fieldset.Content>
                        <Stack gap={4}>
                            {Array.from({ length: DEFAULT_NUM_HOTKEYS_MAX }, (_, i) => (
                                <HotkeysField
                                    key={i}
                                    index={i}
                                    value={hotkeys[i] ?? { key: -1, action: HotkeyAction.None, isHold: false, isLocked: false }}
                                    onValueChange={(changeDetail) => {
                                        updateHotkey(i, changeDetail);
                                    }}
                                    isActive={i === activeHotkeyIndex}
                                    onFieldClick={handleHotkeyFieldClick}
                                    disabled={(hotkeys[i]?.isLocked || disabled || calibrationStatus.isActive) ?? false}
                                />
                            ))}
                        </Stack>
                    </Fieldset.Content>
                </Fieldset.Root>
            </Card.Body>

            <Card.Footer justifyContent="flex-start">
                <ContentActionButtons
                    isDirty={_isDirty}
                    disabled={disabled || calibrationStatus.isActive}
                    resetLabel={t.BUTTON_RESET}
                    saveLabel={t.BUTTON_SAVE}
                    resetHandler={resetHotkeysConfigHandler}
                    saveHandler={saveHotkeysConfigHandler}
                />
            </Card.Footer>
        </Card.Root>
    );
}

// 导出辅助hook，用于外部组件管理热键状态
export function useHotkeyManager() {
    const { hotkeysConfig } = useGamepadConfig();
    const [hotkeys, setHotkeys] = useState<Hotkey[]>([]);
    const [activeHotkeyIndex, setActiveHotkeyIndex] = useState<number>(0);

    // 初始化热键状态
    useEffect(() => {
        setHotkeys(Array.from({ length: DEFAULT_NUM_HOTKEYS_MAX }, (_, i) => {
            return hotkeysConfig?.[i] ?? { key: -1, action: HotkeyAction.None, isLocked: false, isHold: false };
        }));
    }, [hotkeysConfig]);

    // 提供热键操作方法
    const updateHotkey = (index: number, hotkey: Hotkey) => {
        if (index < 0 || index >= DEFAULT_NUM_HOTKEYS_MAX) return;
        
        const newHotkeys = hotkeys.slice();
        newHotkeys[index] = hotkey;
        setHotkeys(newHotkeys);
    };

    // 处理外部点击（例如从Hitbox组件）
    const handleExternalClick = (keyId: number) => {
        if (keyId >= 0 && keyId < 20) {
            updateHotkey(activeHotkeyIndex, { 
                ...hotkeys[activeHotkeyIndex], 
                key: keyId 
            });
        }
    };

    return {
        hotkeys,
        activeHotkeyIndex,
        setActiveHotkeyIndex,
        updateHotkey,
        handleExternalClick,
    };
} 