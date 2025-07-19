"use client";

import { useEffect, useState, useCallback, useRef, useMemo } from "react";
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
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import useUnsavedChangesWarning from "@/hooks/use-unsaved-changes-warning";
import { useLanguage } from "@/contexts/language-context";
import { btnPosList } from "@/components/hitbox/hitbox-base";
import { showToast } from "./ui/toaster";
// import { useButtonMonitor } from "@/hooks/use-button-monitor";

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
    /** 是否启用校准 */
    calibrationActive?: boolean;
}

export function HotkeySettingContent({
    disabled = false,
    onHotkeyUpdate,
    width = "778px",
    height = "100%",
    calibrationActive = false,
}: HotkeySettingContentProps) {
    const { t } = useLanguage();
    const {
        hotkeysConfig,
    } = useGamepadConfig();

    const [_isDirty, setIsDirty] = useUnsavedChangesWarning();
    const [hotkeys, setHotkeys] = useState<Hotkey[]>([]);

    // 使用 ref 存储最新的状态，避免事件监听器重复创建
    const hotkeysRef = useRef<Hotkey[]>([]);

    const [activeHotkeyIndex, setActiveHotkeyIndex] = useState<number>(0);

    hotkeysRef.current = useMemo(() => {
        return hotkeys;
    }, [hotkeys]);

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

        // 如果热键已经被锁定，则不能更改
        if(hotkeysRef.current[index].isLocked && hotkey.key == hotkeysRef.current[index].key) {
            showToast({
                title: t.ERROR_KEY_ALREADY_BINDED_TITLE,
                description: t.ERROR_KEY_ALREADY_BINDED_DESC,
                type: "error",
            });
            return;
        }

        const keys = hotkeysRef.current.map(h => h.key);
        let isChange = false;
        
        for(let i = 0; i < keys.length; i++) {

            // 如果热键已经被绑定到其他位置，则释放位置
            if(keys[i] === hotkey.key && i !== index) {
                hotkeysRef.current[i].key = -1;
                isChange = true;
            }
            // 如果热键未绑定到预计位置，则绑定
            if(keys[i] !== hotkey.key && i == index) {
                hotkeysRef.current[i].key = hotkey.key;
                isChange = true;
            }
        }

        if(isChange) {
            setHotkeys(hotkeysRef.current);
            setIsDirty?.(true);
            // 调用外部回调
            onHotkeyUpdate?.(index, hotkey);
        }

    }, [t, setIsDirty, onHotkeyUpdate]);

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
            const { keyId } = event.detail;
            // 只有当前组件的活跃索引与点击时的活跃索引匹配时才处理
            if (keyId >= 0 && keyId < btnPosList.length - 1) {
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

    

    // 处理热键字段点击
    const handleHotkeyFieldClick = (index: number) => {
        setActiveHotkeyIndex(index);
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
                                    disabled={(hotkeys[i]?.isLocked || disabled || calibrationActive) ?? false}
                                />
                            ))}
                        </Stack>
                    </Fieldset.Content>
                </Fieldset.Root>
            </Card.Body>

            <Card.Footer justifyContent="flex-start">
                
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