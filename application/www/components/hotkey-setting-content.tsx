"use client";

import { useEffect, useState, useMemo, useCallback } from "react";
import {
    Stack,
    Fieldset,
    Text,
    Card,
    Box,
    HStack,
    Switch,
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
import { ButtonEvent } from "./button-monitor-manager";

// 按键监控管理器 Props 类型
interface ButtonMonitorProps {
    isMonitoring: boolean;
    isPolling: boolean;
    lastButtonStates?: any;
    startMonitoring: () => Promise<void>;
    stopMonitoring: () => Promise<void>;
    addEventListener: (listener: (event: ButtonEvent) => void) => void;
    removeEventListener: (listener: (event: ButtonEvent) => void) => void;
}

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
    /** 按键监控管理器属性 */
    buttonMonitorProps?: ButtonMonitorProps;
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
    buttonMonitorProps,
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

    // 使用外部提供的活跃索引，否则使用内部状态
    const activeHotkeyIndex = externalActiveIndex !== undefined ? externalActiveIndex : internalActiveIndex;
    const setActiveHotkeyIndex = onActiveHotkeyIndexChange || setInternalActiveIndex;

    // 从 gamepadConfig 加载 hotkeys 配置
    useEffect(() => {
        setHotkeys(Array.from({ length: DEFAULT_NUM_HOTKEYS_MAX }, (_, i) => {
            return hotkeysConfig?.[i] ?? { key: -1, action: HotkeyAction.None, isLocked: false };
        }));
        setIsDirty?.(false);
    }, [hotkeysConfig, setIsDirty]);

    // 更新单个热键
    const updateHotkey = useCallback((index: number, hotkey: Hotkey) => {
        if (index < 0 || index >= DEFAULT_NUM_HOTKEYS_MAX) return;

        const keys = hotkeys.map(h => h.key);
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

        const newHotkeys = hotkeys.slice();
        newHotkeys[index] = hotkey;
        setHotkeys(newHotkeys);
        setIsDirty?.(true);

        // 调用外部回调
        onHotkeyUpdate?.(index, hotkey);
    }, [hotkeys, t, setIsDirty, onHotkeyUpdate]);

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

    // 处理设备按键事件
    const handleDeviceButtonEvent = useCallback((buttonEvent: ButtonEvent) => {
        // 只处理按键按下事件，并且只在启用监控时
        if (buttonEvent.type === 'button-press' && isButtonMonitoringEnabled && !disabled && !calibrationStatus.isActive) {
            updateHotkey(activeHotkeyIndex, { 
                ...hotkeys[activeHotkeyIndex], 
                key: buttonEvent.buttonIndex 
            });
            
            // 显示成功提示
            toaster.create({
                title: "设备按键绑定成功",
                description: `按键 ${buttonEvent.buttonIndex} 已绑定到热键 ${activeHotkeyIndex + 1}`,
                type: "success",
                duration: 2000,
            });
        }
    }, [activeHotkeyIndex, hotkeys, updateHotkey, isButtonMonitoringEnabled, disabled, calibrationStatus.isActive]);

    // 监听设备按键事件（通过buttonMonitorProps）
    useEffect(() => {
        if (buttonMonitorProps && isButtonMonitoringEnabled) {
            buttonMonitorProps.addEventListener(handleDeviceButtonEvent);
            return () => buttonMonitorProps.removeEventListener(handleDeviceButtonEvent);
        }
    }, [buttonMonitorProps, isButtonMonitoringEnabled, handleDeviceButtonEvent]);

    // 处理监控开关变化
    const handleMonitoringToggle = useCallback(async (enabled: boolean) => {
        if (buttonMonitorProps) {
            try {
                if (enabled && !buttonMonitorProps.isMonitoring && !disabled && !calibrationStatus.isActive) {
                    await buttonMonitorProps.startMonitoring();
                } else if (!enabled && buttonMonitorProps.isMonitoring) {
                    await buttonMonitorProps.stopMonitoring();
                }
                onButtonMonitoringToggle?.(enabled);
            } catch (error) {
                console.error('监控状态切换失败:', error);
                // 重置开关状态
                onButtonMonitoringToggle?.(!enabled);
            }
        } else {
            onButtonMonitoringToggle?.(enabled);
        }
    }, [buttonMonitorProps, disabled, calibrationStatus.isActive, onButtonMonitoringToggle]);

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
                    {onButtonMonitoringToggle && (
                        <Text fontSize="sm" pt={2} color="blue.600">
                            💡 启用设备按键监控后，可以直接按下设备上的按键来绑定热键
                        </Text>
                    )}
                </Card.Description>
                
                {/* 设备按键监控控制 */}
                {onButtonMonitoringToggle && (
                    <Box pt={2}>
                        <HStack gap={2}>
                            <Switch.Root
                                disabled={disabled || calibrationStatus.isActive}
                                colorPalette="blue"
                                checked={isButtonMonitoringEnabled}
                                onCheckedChange={(details) => handleMonitoringToggle(details.checked)}
                            >
                                <Switch.HiddenInput />
                                <Switch.Control>
                                    <Switch.Thumb />
                                </Switch.Control>
                                <Switch.Label fontSize="sm">
                                    设备按键监控 {isButtonMonitoringEnabled ? '(已启用)' : '(已禁用)'}
                                </Switch.Label>
                            </Switch.Root>
                        </HStack>
                    </Box>
                )}
            </Card.Header>

            <Card.Body>
                <Fieldset.Root>
                    <Fieldset.Content>
                        <Stack gap={4}>
                            {Array.from({ length: DEFAULT_NUM_HOTKEYS_MAX }, (_, i) => (
                                <HotkeysField
                                    key={i}
                                    index={i}
                                    value={hotkeys[i] ?? { key: -1, action: HotkeyAction.None }}
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
            return hotkeysConfig?.[i] ?? { key: -1, action: HotkeyAction.None, isLocked: false };
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