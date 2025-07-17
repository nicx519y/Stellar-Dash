"use client";

import {
    Flex,
    Center,
    Text,
    Card,
    Box,
    HStack,
    Button,
    Popover,
    Portal,
} from "@chakra-ui/react";
import { useEffect, useState, useMemo, useCallback } from "react";
import {
    HOTKEYS_SETTINGS_INTERACTIVE_IDS,
    HotkeyAction,
    DEFAULT_NUM_HOTKEYS_MAX,
    Hotkey,
} from "@/types/gamepad-config";
import HitboxCalibration from "@/components/hitbox/hitbox-calibration";
import HitboxHotkey from "@/components/hitbox/hitbox-hotkey";
import { HotkeySettingContent } from "./hotkey-setting-content";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import useUnsavedChangesWarning from "@/hooks/use-unsaved-changes-warning";
import { useLanguage } from "@/contexts/language-context";
import { InputModeSettingContent } from "./input-mode-content";
import { openConfirm } from "@/components/dialog-confirm";
import { GamePadColor } from "@/types/gamepad-color";
import { useNavigationBlocker } from '@/hooks/use-navigation-blocker';
import React from "react";
import { ContentActionButtons } from "./content-action-buttons";
import { eventBus, EVENTS } from "@/lib/event-manager";
import { CalibrationStatus } from "@/types/types";

export function GlobalSettingContent() {
    const { t } = useLanguage();
    const {
        clearManualCalibrationData,
        calibrationStatus,
        startManualCalibration,
        stopManualCalibration,
        fetchCalibrationStatus,
        updateCalibrationStatus,
        fetchHotkeysConfig,
        updateHotkeysConfig,
        globalConfig,
        hotkeysConfig,
        // updateGlobalConfig,
    } = useGamepadConfig();

    const [_isDirty, setIsDirty] = useUnsavedChangesWarning();

    // 管理活跃的热键索引
    const [activeHotkeyIndex, setActiveHotkeyIndex] = useState<number>(0);

    // 按键监控状态
    const [isButtonMonitoringEnabled, setIsButtonMonitoringEnabled] = useState<boolean>(false);

    // 添加本地 hotkeys 状态来存储用户修改
    const [currentHotkeys, setCurrentHotkeys] = useState<Hotkey[]>([]);

    const [calibrationTipOpen, setCalibrationTipOpen] = useState<boolean>(false);
    const [hasShownCompletionDialog, setHasShownCompletionDialog] = useState<boolean>(false);

    // 添加校准模式检查
    useNavigationBlocker(
        calibrationStatus.isActive,
        t.CALIBRATION_MODE_WARNING_TITLE,
        t.CALIBRATION_MODE_WARNING_MESSAGE,
        async () => {
            try {
                await stopManualCalibration();
                return true;
            } catch {
                await fetchCalibrationStatus();
                return !calibrationStatus.isActive;
            }
        }
    );

    // 根据校准状态生成按钮颜色列表
    const calibrationButtonColors = useMemo(() => {
        if (!calibrationStatus.isActive || !calibrationStatus.buttons) {
            return undefined;
        }

        const colorMap = {
            'OFF': GamePadColor.fromString('#000000'),      // 黑色
            'RED': GamePadColor.fromString('#FF0000'),      // 红色 - 未校准
            'CYAN': GamePadColor.fromString('#00FFFF'),     // 天蓝色 - 顶部值采样中
            'DARK_BLUE': GamePadColor.fromString('#0000AA'), // 深蓝色 - 底部值采样中
            'GREEN': GamePadColor.fromString('#00FF00'),    // 绿色 - 校准完成
            'YELLOW': GamePadColor.fromString('#FFFF00'),   // 黄色 - 校准出错
        };

        const colors = calibrationStatus.buttons.map(button =>
            colorMap[button.ledColor] || GamePadColor.fromString('#808080') // 默认灰色
        );

        return colors;
    }, [calibrationStatus]);

    // 手动校准状态监听 - 移除定时器，改为事件监听
    useEffect(() => {

        // 添加校准状态更新事件监听
        const handleCalibrationUpdate = (data: unknown) => {
            if(!calibrationStatus.isActive) return;
            console.log('Global setting received calibration update:', data);
            // 处理校准状态更新
            if (data && typeof data === 'object' && 'calibrationStatus' in data) {
                const eventData = data as { calibrationStatus?: CalibrationStatus };
                if (eventData.calibrationStatus) {
                    console.log('更新校准状态:', eventData.calibrationStatus);
                    // 确保数据格式正确，添加默认值
                    const statusData = eventData.calibrationStatus;
                    const newCalibrationStatus: CalibrationStatus = {
                        isActive: statusData?.isActive || false,
                        uncalibratedCount: statusData?.uncalibratedCount || 0,
                        activeCalibrationCount: statusData?.activeCalibrationCount || 0,
                        allCalibrated: statusData?.allCalibrated || false,
                        buttons: statusData?.buttons || []
                    };
                    // 直接更新校准状态
                    updateCalibrationStatus(newCalibrationStatus);
                }
            }
        };

        // 订阅校准更新事件
        const unsubscribe = eventBus.on(EVENTS.CALIBRATION_UPDATE, handleCalibrationUpdate);

        // 清理函数
        return () => {
            unsubscribe();
        };
    }, []);

    // 弹窗询问用户是否关闭校准模式
    const showCompletionDialog = useCallback(async () => {
        const confirmed = await openConfirm({
            title: t.CALIBRATION_COMPLETION_DIALOG_TITLE,
            message: t.CALIBRATION_COMPLETION_DIALOG_MESSAGE
        });

        if (confirmed) {
            stopManualCalibration();
        }
    }, [t.CALIBRATION_COMPLETION_DIALOG_TITLE, t.CALIBRATION_COMPLETION_DIALOG_MESSAGE, stopManualCalibration]);

    // 检测校准完成状态，显示确认对话框
    useEffect(() => {
        if (calibrationStatus.isActive &&
            calibrationStatus.allCalibrated &&
            !hasShownCompletionDialog) {

            setHasShownCompletionDialog(true);
            showCompletionDialog();
        }
    }, [calibrationStatus.isActive, calibrationStatus.allCalibrated, hasShownCompletionDialog, showCompletionDialog]);

    // 当校准停止时，重置完成对话框标志
    useEffect(() => {
        if (!calibrationStatus.isActive) {
            setHasShownCompletionDialog(false);
        }
    }, [calibrationStatus.isActive]);

    const deleteCalibrationDataClick = async () => {
        const confirmed = await openConfirm({
            title: t.CALIBRATION_CLEAR_DATA_DIALOG_TITLE,
            message: t.CALIBRATION_CLEAR_DATA_DIALOG_MESSAGE
        });

        if (confirmed) {
            await onDeleteCalibrationConfirm();
        }
    };

    const onDeleteCalibrationConfirm = () => {
        return clearManualCalibrationData();
    }

    const onStartManualCalibration = () => {
        startManualCalibration();
    }

    const onEndManualCalibration = () => {
        stopManualCalibration();
    }

    // const switchAutoCalibration = () => {
    //     updateGlobalConfig({ autoCalibrationEnabled: !globalConfig.autoCalibrationEnabled });
    // }

    // 处理外部点击（从Hitbox组件）
    const handleExternalClick = (keyId: number) => {
        if (keyId >= 0 && keyId < 20) {
            // 触发自定义事件通知HotkeySettingContent组件
            const event = new CustomEvent('hitbox-click', {
                detail: { keyId, activeHotkeyIndex }
            });
            window.dispatchEvent(event);
        }
    };

    // 处理热键更新回调
    const handleHotkeyUpdate = (index: number, hotkey: Hotkey) => {
        const newHotkeys = [...currentHotkeys];
        newHotkeys[index] = hotkey;
        setCurrentHotkeys(newHotkeys);
        setIsDirty(true);
    };

    // 初始化 currentHotkeys
    useEffect(() => {
        setCurrentHotkeys(Array.from({ length: DEFAULT_NUM_HOTKEYS_MAX }, (_, i) => {
            return hotkeysConfig?.[i] ?? { key: -1, action: HotkeyAction.None, isLocked: false, isHold: false };
        }));
    }, [hotkeysConfig]);

    // 保存热键配置
    const saveHotkeysConfigHandler = async () => {
        if (!currentHotkeys || currentHotkeys.length === 0) return;
        await updateHotkeysConfig(currentHotkeys);
    };

    // 重置热键配置
    const resetHotkeysConfigHandler = async () => {
        await fetchHotkeysConfig();
    };

    return (
        <Flex direction="row" width={"100%"} height={"100%"} padding="18px">
            <Center>
                <InputModeSettingContent disabled={calibrationStatus.isActive} />
            </Center>
            <Center flex={1} justifyContent={"center"} flexDirection={"column"}  >

                <Center padding="80px 30px" position="relative" flex={1}  >
                    <Box position="absolute" top="50%" left="50%" transform="translateX(-50%) translateY(-350px)" zIndex={10} >
                        <Card.Root w="100%" h="100%" >
                            <Card.Body p="10px" >
                                <Flex direction="row" gap={2} w="268px" >
                                    {/* 隐藏自动校准入口 */}
                                    <HStack flex={1} justifyContent="flex-end" >
                                        <Popover.Root open={calibrationTipOpen} >
                                            <Popover.Trigger asChild>
                                                <Button
                                                    disabled={globalConfig.autoCalibrationEnabled}
                                                    colorPalette={calibrationStatus.isActive ? "blue" : "green"}
                                                    size="xs"
                                                    w="130px"
                                                    onMouseEnter={!calibrationStatus.isActive ? () => setCalibrationTipOpen(true) : undefined}
                                                    onMouseLeave={!calibrationStatus.isActive ? () => setCalibrationTipOpen(false) : undefined}
                                                    onClick={calibrationStatus.isActive ? onEndManualCalibration : onStartManualCalibration}
                                                >
                                                    {calibrationStatus.isActive ? t.CALIBRATION_STOP_BUTTON : t.CALIBRATION_START_BUTTON}
                                                </Button>
                                            </Popover.Trigger>
                                            <Portal>
                                                <Popover.Positioner>
                                                    <Popover.Content>
                                                        <Popover.Arrow />
                                                        <Popover.Body>
                                                            <Text fontSize={"xs"} whiteSpace="pre-wrap" >{t.CALIBRATION_TIP_MESSAGE}</Text>
                                                        </Popover.Body>
                                                    </Popover.Content>
                                                </Popover.Positioner>
                                            </Portal>
                                        </Popover.Root>

                                        <Button
                                            disabled={globalConfig.autoCalibrationEnabled || calibrationStatus.isActive}
                                            colorPalette={"red"} size={"xs"} w="130px" variant="solid"
                                            onClick={deleteCalibrationDataClick} >
                                            {t.CALIBRATION_CLEAR_DATA_BUTTON}
                                        </Button>
                                    </HStack>
                                </Flex>
                            </Card.Body>
                        </Card.Root>
                    </Box>
                    {!calibrationStatus.isActive && (
                        <HitboxHotkey
                            interactiveIds={HOTKEYS_SETTINGS_INTERACTIVE_IDS}
                            onClick={handleExternalClick}
                        />
                    )}
                    {calibrationStatus.isActive && (
                        <HitboxCalibration
                            hasText={false}
                            buttonsColorList={calibrationButtonColors}
                        />
                    )}
                </Center>
                <Center flex={0}  >
                    <ContentActionButtons
                        isDirty={_isDirty}
                        disabled={calibrationStatus.isActive}
                        resetHandler={resetHotkeysConfigHandler}
                        saveHandler={saveHotkeysConfigHandler}
                    />
                </Center>
            </Center>
            <Flex>
                <HotkeySettingContent
                    disabled={calibrationStatus.isActive}
                    activeHotkeyIndex={activeHotkeyIndex}
                    onActiveHotkeyIndexChange={setActiveHotkeyIndex}
                    isButtonMonitoringEnabled={isButtonMonitoringEnabled}
                    onButtonMonitoringToggle={setIsButtonMonitoringEnabled}
                    onHotkeyUpdate={handleHotkeyUpdate}
                />
            </Flex>
        </Flex>
    );
} 