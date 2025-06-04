"use client";

import {
    Flex,
    Center,
    Stack,
    Fieldset,
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
    HotkeyAction,
    DEFAULT_NUM_HOTKEYS_MAX,
    Hotkey,
    HOTKEYS_SETTINGS_INTERACTIVE_IDS,
} from "@/types/gamepad-config";
import Hitbox from "@/components/hitbox";
import HotkeysField from "./hotkeys-field";
import { toaster } from "@/components/ui/toaster";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import useUnsavedChangesWarning from "@/hooks/use-unsaved-changes-warning";
import { useLanguage } from "@/contexts/language-context";
import { ContentActionButtons } from "@/components/content-action-buttons";
import { InputModeSettingContent } from "./input-mode-content";
import { openConfirm } from "@/components/dialog-confirm";
import { GamePadColor } from "@/types/gamepad-color";
import { useNavigationBlocker } from '@/hooks/use-navigation-blocker';

export function GlobalSettingContent() {
    const { t } = useLanguage();
    const {
        hotkeysConfig,
        updateHotkeysConfig,
        fetchHotkeysConfig,
        clearManualCalibrationData,
        calibrationStatus,
        startManualCalibration,
        stopManualCalibration,
        fetchCalibrationStatus,
        globalConfig,
        updateGlobalConfig,
    } = useGamepadConfig();

    const [_isDirty, setIsDirty] = useUnsavedChangesWarning();

    const [hotkeys, setHotkeys] = useState<Hotkey[]>([]);
    const [activeHotkeyIndex, setActiveHotkeyIndex] = useState<number>(0);
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

    useEffect(() => {
        // 从 gamepadConfig 加载 hotkeys 配置
        setHotkeys(Array.from({ length: DEFAULT_NUM_HOTKEYS_MAX }, (_, i) => {
            return hotkeysConfig?.[i] ?? { key: -1, action: HotkeyAction.None, isLocked: false };
        }));
        setIsDirty?.(false);
    }, [hotkeysConfig]);

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
    }, [calibrationStatus.isActive, calibrationStatus.allCalibrated, hasShownCompletionDialog, stopManualCalibration]);

    useMemo(() => {
        if (activeHotkeyIndex >= 0 && hotkeys[activeHotkeyIndex]?.isLocked === true) {
            const index = hotkeys.findIndex(h => h.isLocked === undefined || h.isLocked === false);
            if (index >= 0) {
                setActiveHotkeyIndex(index);
            }
        }
    }, [hotkeys]);

    const saveHotkeysConfigHandler = async () => {
        if (!hotkeysConfig) return;
        await updateHotkeysConfig(hotkeys);
    };

    const updateHotkey = (index: number, hotkey: Hotkey) => {
        if (index < 0 || index >= DEFAULT_NUM_HOTKEYS_MAX) return;

        const keys = hotkeys.map(h => h.key);
        const keyIndex = keys.indexOf(hotkey.key);
        // 如果 hotkey 的 key 已经在 hotkeys 中，并且不是当前正在编辑的 hotkey，则不更新
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
        setIsDirty?.(true); // 更新 dirty 状态
    };

    const handleHitboxClick = (id: number) => {
        if (id >= 0 && id < 20) {
            updateHotkey(activeHotkeyIndex, { ...hotkeys[activeHotkeyIndex], key: id });
        }
    };

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
                            onClick={handleHitboxClick}
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
                <Card.Root w="778px" h="100%" >
                    <Card.Header>
                        <Card.Title fontSize={"md"}  >
                            <Text fontSize={"32px"} fontWeight={"normal"} color={"green.600"} >{t.SETTINGS_HOTKEYS_TITLE}</Text>
                        </Card.Title>
                        <Card.Description fontSize={"sm"} pt={4} pb={4} whiteSpace="pre-wrap"  >
                            {t.SETTINGS_HOTKEYS_HELPER_TEXT}
                        </Card.Description>
                    </Card.Header>
                    <Card.Body>
                        <Fieldset.Root  >
                            <Fieldset.Content >
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
                                            onFieldClick={(index) => setActiveHotkeyIndex(index)}
                                            disabled={(hotkeys[i]?.isLocked || calibrationStatus.isActive) ?? false}
                                        />
                                    ))}


                                </Stack>
                            </Fieldset.Content>
                        </Fieldset.Root>
                    </Card.Body>
                    <Card.Footer justifyContent={"flex-start"} >
                        <ContentActionButtons
                            isDirty={_isDirty}
                            disabled={calibrationStatus.isActive}
                            resetLabel={t.BUTTON_RESET}
                            saveLabel={t.BUTTON_SAVE}
                            resetHandler={fetchHotkeysConfig}
                            saveHandler={saveHotkeysConfigHandler}
                        />
                    </Card.Footer>
                </Card.Root>
            </Center>
        </Flex>
    );
} 