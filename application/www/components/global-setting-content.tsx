"use client";

import { useEffect, useState, useCallback, useRef } from "react";
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
import { useLanguage } from "@/contexts/language-context";
import { InputModeSettingContent } from "./input-mode-content";
import { ConnectionAndPowerBasicSettingContent } from "./connection-mode-content";
import { ScreenControlSettingContent } from "./screen-control-setting-content";
import { cancelConfirm, openConfirm } from "@/components/dialog-confirm";
import { useNavigationBlocker } from '@/hooks/use-navigation-blocker';
import React from "react";
import { Tabs } from "@chakra-ui/react";
import { MainContentBody, SettingMainContentLayout } from "./setting-main-content-layout";
import { MdOutlineScreenshotMonitor } from "react-icons/md";
import { FaKeyboard } from "react-icons/fa";

import {
    SettingContentLayout,
    SideContent,
    HitboxContent,
    MainContent,
    TopButtons
} from "./setting-content-layout";
import { LuSettings2, LuTimerReset } from "react-icons/lu";
import { VscDashboard } from "react-icons/vsc";
import { FaRegStopCircle } from "react-icons/fa";

export function GlobalSettingContent() {
    const { t } = useLanguage();
    const {
        clearManualCalibrationData,
        updateHotkeysConfig,
        globalConfig,
        hotkeysConfig,
        dataIsReady,
        sendPendingCommandImmediately,
        setFinishConfigDisabled,
        deviceConnected,
        checkIsManualCalibrationCompleted,
        startManualCalibration,
        stopManualCalibration,
        hitboxLayout,
        // updateGlobalConfig,
    } = useGamepadConfig();

    const [isInit, setIsInit] = useState<boolean>(false);
    const [needUpdate, setNeedUpdate] = useState<boolean>(false);
    // 添加本地 hotkeys 状态来存储用户修改
    const [currentHotkeys, setCurrentHotkeys] = useState<Hotkey[]>([]);
    const [calibrationActive, setCalibrationActive] = useState<boolean>(false);
    const [mainTab, setMainTab] = useState<'basic' | 'hotkeys' | 'screen'>('basic');
    const calibrationActiveRef = useRef(false);
    const deviceConnectedRef = useRef(deviceConnected);
    const connectionEpochRef = useRef(0);
    const calibrationOperationRef = useRef(false);
    const calibrationCheckInFlightRef = useRef(false);

    const setCalibrationActiveForSession = useCallback((active: boolean) => {
        calibrationActiveRef.current = active;
        setCalibrationActive(active);
    }, []);

    const onStartManualCalibration = useCallback(async () => {
        if (
            !deviceConnectedRef.current ||
            calibrationOperationRef.current ||
            calibrationActiveRef.current ||
            globalConfig.autoCalibrationEnabled
        ) {
            return;
        }

        const epoch = connectionEpochRef.current;
        calibrationOperationRef.current = true;
        try {
            const status = await startManualCalibration();
            if (
                status.isActive &&
                deviceConnectedRef.current &&
                connectionEpochRef.current === epoch
            ) {
                setCalibrationActiveForSession(true);
            }
        } catch {
            // The context publishes the actionable transport error. Keep the
            // calibration UI inactive when the command was not acknowledged.
            setCalibrationActiveForSession(false);
        } finally {
            calibrationOperationRef.current = false;
        }
    }, [globalConfig.autoCalibrationEnabled, setCalibrationActiveForSession, startManualCalibration]);

    const onEndManualCalibration = useCallback(async (): Promise<boolean> => {
        if (!calibrationActiveRef.current) {
            return true;
        }

        if (!deviceConnectedRef.current) {
            setCalibrationActiveForSession(false);
            return true;
        }
        if (calibrationOperationRef.current) {
            return false;
        }

        const epoch = connectionEpochRef.current;
        calibrationOperationRef.current = true;
        try {
            const status = await stopManualCalibration();
            if (!deviceConnectedRef.current || connectionEpochRef.current !== epoch) {
                setCalibrationActiveForSession(false);
                return true;
            }
            if (status.isActive) {
                return false;
            }
            setCalibrationActiveForSession(false);
            return true;
        } catch {
            if (!deviceConnectedRef.current || connectionEpochRef.current !== epoch) {
                setCalibrationActiveForSession(false);
                return true;
            }
            // Keep the page blocked while the same live device still reports
            // a calibration session that we could not stop reliably.
            return false;
        } finally {
            if (connectionEpochRef.current === epoch) {
                calibrationOperationRef.current = false;
            }
        }
    }, [setCalibrationActiveForSession, stopManualCalibration]);

    // 添加校准模式检查
    useNavigationBlocker(
        calibrationActive,
        t.CALIBRATION_MODE_WARNING_TITLE,
        t.CALIBRATION_MODE_WARNING_MESSAGE,
        async () => {
            return onEndManualCalibration();
        }
    );

    // 弹窗询问用户是否关闭校准模式
    const showCompletionDialog = async () => {
        const confirmed = await openConfirm({
            title: t.CALIBRATION_COMPLETION_DIALOG_TITLE,
            message: t.CALIBRATION_COMPLETION_DIALOG_MESSAGE
        });

        if (confirmed) {
            await onEndManualCalibration();
        }
    };

    const deleteCalibrationDataClick = async () => {
        const confirmed = await openConfirm({
            title: t.CALIBRATION_CLEAR_DATA_DIALOG_TITLE,
            message: t.CALIBRATION_CLEAR_DATA_DIALOG_MESSAGE
        });

        if (confirmed) {
            await clearManualCalibrationData().catch(() => undefined);
        }
    };

    // 检查手动校准是否完成，如果未完成，则弹出确认对话框
    const checkIsManualCalibrationCompletedHandle = useCallback(async () => {
        if (
            !deviceConnectedRef.current ||
            calibrationCheckInFlightRef.current ||
            globalConfig.autoCalibrationEnabled
        ) {
            return;
        }
        calibrationCheckInFlightRef.current = true;
        const epoch = connectionEpochRef.current;
        try {
            const isCompleted = await checkIsManualCalibrationCompleted();
            if (!deviceConnectedRef.current || connectionEpochRef.current !== epoch) {
                return;
            }
            if (!isCompleted) {
                const confirmation = await openConfirm({
                    title: t.CALIBRATION_CHECK_COMPLETED_DIALOG_TITLE,
                    message: t.CALIBRATION_CHECK_COMPLETED_DIALOG_MESSAGE
                });
                if (
                    confirmation &&
                    deviceConnectedRef.current &&
                    connectionEpochRef.current === epoch &&
                    !calibrationActiveRef.current
                ) {
                    await onStartManualCalibration();
                }
            }
        } catch {
            // Disconnect/reset paths invalidate this check through the epoch.
            // The transport context owns any user-facing error reporting.
        } finally {
            calibrationCheckInFlightRef.current = false;
        }
    }, [checkIsManualCalibrationCompleted, globalConfig.autoCalibrationEnabled, onStartManualCalibration, t]);

    // 处理热键更新回调
    const handleHotkeyUpdate = useCallback((hotkeys: Hotkey[]) => {
        setCurrentHotkeys(hotkeys);
        setNeedUpdate(true);
    }, [currentHotkeys]);

    useEffect(() => {
        if (needUpdate) {
            void updateHotkeysConfig(currentHotkeys).catch(() => undefined);
            setNeedUpdate(false);
        }
    }, [needUpdate]);

    useEffect(() => {
        // 在关闭页面的时候 把队列中的 update_hotkeys_config 命令立即发送
        return () => {
            try {
                console.log("GlobalSettingContent unmount");
                sendPendingCommandImmediately('update_hotkeys_config');
            } catch (error) {
                console.warn('页面关闭前发送 update_hotkeys_config 命令失败:', error);
            }
        }
    }, []);

    // 处理外部点击（从Hitbox组件）
    const handleExternalClick = (keyId: number) => {
        const layoutLen = hitboxLayout?.length || 0;
        if (keyId >= 0 && keyId < layoutLen - 1) {
            // 触发自定义事件通知HotkeySettingContent组件
            const event = new CustomEvent('hitbox-click', {
                detail: { keyId }
            });
            window.dispatchEvent(event);
        }
    };

    // 当校准状态改变时，更新完成配置按钮的禁用状态
    useEffect(() => {
        if (calibrationActive) {
            setFinishConfigDisabled(true);
        } else if (!calibrationActive) {
            setFinishConfigDisabled(false);
        }
        return () => {
            setFinishConfigDisabled(false);
        }
    }, [calibrationActive]);

    useEffect(() => {
        deviceConnectedRef.current = deviceConnected;
        if (!deviceConnected) {
            // Invalidate every async prompt/operation from the old physical
            // device session. This is local cleanup only: no device command is
            // allowed once the transport has disconnected.
            connectionEpochRef.current += 1;
            calibrationOperationRef.current = false;
            calibrationCheckInFlightRef.current = false;
            cancelConfirm();
            setCalibrationActiveForSession(false);
            setIsInit(false);
        }
    }, [deviceConnected, setCalibrationActiveForSession]);

    // 初始化 currentHotkeys
    useEffect(() => {
        if (isInit) {
            return;
        }

        if (!isInit && dataIsReady) {
            setCurrentHotkeys(Array.from({ length: DEFAULT_NUM_HOTKEYS_MAX }, (_, i) => {
                return hotkeysConfig?.[i] ?? { key: -1, action: HotkeyAction.None, isLocked: false, isHold: false };
            }));

            // 每次进入页面的时候会检查是否按键都校准了，如果未完成，则弹出确认对话框
            checkIsManualCalibrationCompletedHandle();

            setIsInit(true);
        }
    }, [dataIsReady, hotkeysConfig, isInit, checkIsManualCalibrationCompletedHandle]);

    // 渲染hitbox内容
    const renderHitboxContent = (containerWidth: number) => {
        if (!calibrationActive) {
            return (
                <HitboxHotkey
                    interactiveIds={HOTKEYS_SETTINGS_INTERACTIVE_IDS}
                    onClick={handleExternalClick}
                    isButtonMonitoringEnabled={true} // 启用设备按键监控
                    containerWidth={containerWidth}
                />
            );
        } else {
            return (
                <HitboxCalibration
                    containerWidth={containerWidth}
                    calibrationAllCompletedCallback={showCompletionDialog}
                />
            );
        }
    };

    // 上方按键配置
    const topButtonsConfig = {
        show: true,
        buttons: [
            {
                text: calibrationActive ? t.CALIBRATION_STOP_BUTTON : t.CALIBRATION_START_BUTTON,
                icon: (calibrationActive ? FaRegStopCircle : VscDashboard),
                color: (calibrationActive ? "blue" : "green") as "blue" | "green",
                size: "sm" as const,
                width: "190px",
                disabled: globalConfig.autoCalibrationEnabled,
                onClick: calibrationActive ? onEndManualCalibration : onStartManualCalibration,
                hasTip: !calibrationActive,
                tipMessage: t.CALIBRATION_TIP_MESSAGE,
            },
            {
                text: t.CALIBRATION_CLEAR_DATA_BUTTON,
                icon: LuTimerReset,
                color: "red" as const,
                size: "sm" as const,
                width: "190px",
                disabled: globalConfig.autoCalibrationEnabled || calibrationActive,
                onClick: deleteCalibrationDataClick,
            }
        ],
        gap: 2,
        justifyContent: "flex-end" as const
    };

    return (
        <SettingContentLayout
            disabled={calibrationActive}
        >
            <SideContent>
                <InputModeSettingContent disabled={calibrationActive} />
            </SideContent>

            <HitboxContent>
                {renderHitboxContent}
            </HitboxContent>

            <MainContent>
                <SettingMainContentLayout width={778}>
                    
                    <MainContentBody>
                        <Tabs.Root
                            value={mainTab}
                            onValueChange={(details) => setMainTab(details.value as 'basic' | 'hotkeys' | 'screen')}
                            colorPalette="green"
                        >
                            <Tabs.List>
                                <Tabs.Trigger value="basic" fontSize="16px" fontWeight="extrabold">
                                    <LuSettings2 />
                                    {t.SETTINGS_BASIC_TITLE}
                                </Tabs.Trigger>
                                <Tabs.Trigger value="screen" fontSize="16px" fontWeight="extrabold" >
                                    <MdOutlineScreenshotMonitor />
                                    {t.SETTINGS_SCREEN_CONTROL_TITLE}
                                </Tabs.Trigger>
                                <Tabs.Trigger value="hotkeys" fontSize="16px" fontWeight="extrabold">
                                    <FaKeyboard />
                                    {t.SETTINGS_HOTKEYS_TITLE}
                                </Tabs.Trigger>
                            </Tabs.List>

                            <Tabs.Content value="basic" padding="24px 0">
                                <ConnectionAndPowerBasicSettingContent disabled={calibrationActive} />
                            </Tabs.Content>
                            <Tabs.Content value="hotkeys" padding="24px 0" >
                                <HotkeySettingContent
                                    disabled={calibrationActive}
                                    onHotkeysUpdate={handleHotkeyUpdate}
                                    hotkeys={currentHotkeys}
                                />
                            </Tabs.Content>
                            <Tabs.Content value="screen" padding="24px 0">
                                <ScreenControlSettingContent disabled={calibrationActive} />
                            </Tabs.Content>
                        </Tabs.Root>
                    </MainContentBody>
                </SettingMainContentLayout>
            </MainContent>

            <TopButtons config={topButtonsConfig} />
        </SettingContentLayout >
    );
} 
