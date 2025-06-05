"use client";

import {
    Flex,
    Center,
    Text,
    Card,
    Box,
    HStack,
    Button,
    Switch,
    Popover,
    Portal,
} from "@chakra-ui/react";
import { useEffect, useState, useMemo } from "react";
import {
    HOTKEYS_SETTINGS_INTERACTIVE_IDS,
} from "@/types/gamepad-config";
import Hitbox from "@/components/hitbox";
import { HotkeySettingContent } from "./hotkey-setting-content";
import { ButtonMonitorManager, ButtonEvent } from "./button-monitor-manager";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import useUnsavedChangesWarning from "@/hooks/use-unsaved-changes-warning";
import { useLanguage } from "@/contexts/language-context";
import { InputModeSettingContent } from "./input-mode-content";
import { openConfirm } from "@/components/dialog-confirm";
import { GamePadColor } from "@/types/gamepad-color";
import { useNavigationBlocker } from '@/hooks/use-navigation-blocker';

export function GlobalSettingContent() {
    const { t } = useLanguage();
    const {
        clearManualCalibrationData,
        calibrationStatus,
        startManualCalibration,
        stopManualCalibration,
        fetchCalibrationStatus,
        globalConfig,
        updateGlobalConfig,
    } = useGamepadConfig();

    const [_isDirty, setIsDirty] = useUnsavedChangesWarning();

    // 管理活跃的热键索引
    const [activeHotkeyIndex, setActiveHotkeyIndex] = useState<number>(0);
    
    // 按键监控状态
    const [isButtonMonitoringEnabled, setIsButtonMonitoringEnabled] = useState<boolean>(false);

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

    // 手动校准状态监听和定时器
    useEffect(() => {
        let intervalId: NodeJS.Timeout | null = null;

        if (calibrationStatus.isActive) {
            // 手动校准激活时，每1秒获取一次校准状态
            intervalId = setInterval(() => {
                fetchCalibrationStatus();
            }, 1000);
        } else {
            // 校准停止时，重置完成对话框标志
            setHasShownCompletionDialog(false);
        }

        // 清理定时器
        return () => {
            if (intervalId) {
                clearInterval(intervalId);
            }
        };
    }, [calibrationStatus.isActive, fetchCalibrationStatus]);

    // 检测校准完成状态，显示确认对话框
    useEffect(() => {
        if (calibrationStatus.isActive &&
            calibrationStatus.allCalibrated &&
            !hasShownCompletionDialog) {

            setHasShownCompletionDialog(true);

            showCompletionDialog();
        }
    }, [calibrationStatus.isActive, calibrationStatus.allCalibrated, hasShownCompletionDialog]);

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

    const switchAutoCalibration = () => {
        updateGlobalConfig({ autoCalibrationEnabled: !globalConfig.autoCalibrationEnabled });
    }

    // 弹窗询问用户是否关闭校准模式
    const showCompletionDialog = async () => {
        const confirmed = await openConfirm({
            title: t.CALIBRATION_COMPLETION_DIALOG_TITLE,
            message: t.CALIBRATION_COMPLETION_DIALOG_MESSAGE
        });

        if (confirmed) {
            stopManualCalibration();
        }
    };

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

    return (
        <Flex direction="row" width={"100%"} height={"100%"} padding="18px">
            <Center>
                <InputModeSettingContent disabled={calibrationStatus.isActive} />
            </Center>
            <Center flex={1} >
                <Center padding="80px 30px" position="relative"   >
                    <Box position="absolute" top="0px" >
                        <Card.Root w="100%" h="100%" >
                            <Card.Body p="10px" >
                                <Flex direction="row" gap={2} w="500px" >
                                    <Center>
                                        <Switch.Root
                                            disabled={calibrationStatus.isActive}
                                            colorPalette={"green"}
                                            checked={globalConfig.autoCalibrationEnabled}
                                            onCheckedChange={switchAutoCalibration} >
                                            <Switch.HiddenInput />
                                            <Switch.Control>
                                                <Switch.Thumb />
                                            </Switch.Control>
                                            <Switch.Label >{globalConfig.autoCalibrationEnabled ? t.AUTO_CALIBRATION_TITLE : t.MANUAL_CALIBRATION_TITLE}</Switch.Label>
                                        </Switch.Root>
                                    </Center>
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
                                            colorPalette={"red"} size={"xs"} w="130px" variant="surface"
                                            onClick={deleteCalibrationDataClick} >
                                            {t.CALIBRATION_CLEAR_DATA_BUTTON}
                                        </Button>
                                    </HStack>
                                </Flex>
                            </Card.Body>
                        </Card.Root>
                    </Box>
                    {!calibrationStatus.isActive && (
                        <Hitbox
                            interactiveIds={HOTKEYS_SETTINGS_INTERACTIVE_IDS}
                            onClick={handleExternalClick}
                        />
                    )}
                    {calibrationStatus.isActive && (
                        <Hitbox
                            hasText={false}
                            buttonsColorList={calibrationButtonColors}
                        />
                    )}
                </Center>
            </Center>
            <Center>
                <ButtonMonitorManager
                    autoStart={false}
                    pollingInterval={500}
                    autoStopOnUnmount={true}
                    onError={(error) => {
                        console.error('按键监控错误:', error);
                    }}
                >
                    {(buttonMonitorProps) => (
                        <HotkeySettingContent
                            disabled={calibrationStatus.isActive}
                            activeHotkeyIndex={activeHotkeyIndex}
                            onActiveHotkeyIndexChange={setActiveHotkeyIndex}
                            isButtonMonitoringEnabled={isButtonMonitoringEnabled}
                            onButtonMonitoringToggle={setIsButtonMonitoringEnabled}
                            buttonMonitorProps={buttonMonitorProps}
                        />
                    )}
                </ButtonMonitorManager>
            </Center>
        </Flex>
    );
} 