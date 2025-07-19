"use client";

import { useEffect, useCallback, useState } from "react";
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
import { useLanguage } from "@/contexts/language-context";
import { btnPosList } from "@/components/hitbox/hitbox-base";
import { showToast } from "./ui/toaster";

interface HotkeySettingContentProps {
    /** 热键配置数组 */
    hotkeys: Hotkey[];
    /** 热键批量更新回调 */
    onHotkeysUpdate: (hotkeys: Hotkey[]) => void;
    /** 是否禁用组件（例如在校准模式下） */
    disabled?: boolean;
    /** 自定义宽度 */
    width?: string;
    /** 自定义高度 */
    height?: string;
    /** 是否启用校准 */
    calibrationActive?: boolean;
}

export function HotkeySettingContent({
    hotkeys,
    onHotkeysUpdate,  // 改为批量更新
    disabled = false,
    width = "778px",
    height = "100%",
    calibrationActive = false,
}: HotkeySettingContentProps) {
    const { t } = useLanguage();

    const [activeHotkeyIndex, setActiveHotkeyIndex] = useState<number>(0);

    // 更新单个热键
    const updateHotkey = useCallback((index: number, hotkey: Hotkey) => {
        if (index < 0 || index >= DEFAULT_NUM_HOTKEYS_MAX) return;

        for (let i = 0; i < hotkeys.length; i++) {
            console.log("hotkeys[i]: ", hotkeys[i], " key: ", hotkey.key);
            if (hotkeys[i].isLocked && hotkey.key == hotkeys[i].key) {
                showToast({
                    title: t.ERROR_KEY_ALREADY_BINDED_TITLE,
                    description: t.ERROR_KEY_ALREADY_BINDED_DESC,
                    type: "error",
                });
                return;
            }
        }

        // 创建新的热键数组
        const newHotkeys = [...hotkeys];
        let hasChanges = false;

        // 1. 如果新的热键不是 -1，检查是否已经被其他位置使用
        if (hotkey.key !== -1) {
            for (let i = 0; i < newHotkeys.length; i++) {
                if (i !== index && newHotkeys[i].key === hotkey.key) {
                    // 释放已被占用的位置
                    newHotkeys[i] = { ...newHotkeys[i], key: -1 };
                    hasChanges = true;
                }
            }
        }

        // 2. 更新当前位置的热键
        if (newHotkeys[index].key !== hotkey.key) {
            newHotkeys[index] = { ...newHotkeys[index], key: hotkey.key };
            hasChanges = true;
        }

        if(newHotkeys[index].action !== hotkey.action) {
            newHotkeys[index] = { ...newHotkeys[index], action: hotkey.action };
            hasChanges = true;
        }

        if(newHotkeys[index].isHold !== hotkey.isHold) {
            newHotkeys[index] = { ...newHotkeys[index], isHold: hotkey.isHold };
            hasChanges = true;
        }

        // 3. 如果有变化，批量通知父组件
        if (hasChanges) {
            onHotkeysUpdate(newHotkeys);
        }

    }, [onHotkeysUpdate, hotkeys]);

    // 自动选择下一个可用的热键索引（如果当前的被锁定）
    useEffect(() => {
        if (activeHotkeyIndex >= 0 && hotkeys[activeHotkeyIndex]?.isLocked === true) {
            const index = hotkeys.findIndex(h => h.isLocked === undefined || h.isLocked === false);
            if (index >= 0) {
                setActiveHotkeyIndex(index);
            }
        }
    }, [hotkeys, activeHotkeyIndex]);

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