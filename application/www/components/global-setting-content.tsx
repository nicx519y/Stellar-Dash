"use client";

import { useEffect, useState, useCallback } from "react";
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
import { openConfirm } from "@/components/dialog-confirm";
import { useNavigationBlocker } from '@/hooks/use-navigation-blocker';
import React from "react";
import { 
    SettingContentLayout, 
    SideContent, 
    HitboxContent, 
    MainContent, 
    TopButtons 
} from "./setting-content-layout";
import { LuTimerReset } from "react-icons/lu";
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
        wsConnected,
        checkIsManualCalibrationCompleted,
        hitboxLayout,
        // updateGlobalConfig,
    } = useGamepadConfig();

    const [isInit, setIsInit] = useState<boolean>(false);
    const [needUpdate, setNeedUpdate] = useState<boolean>(false);
    // 添加本地 hotkeys 状态来存储用户修改
    const [currentHotkeys, setCurrentHotkeys] = useState<Hotkey[]>([]);
    const [calibrationActive, setCalibrationActive] = useState<boolean>(false);

    // 添加校准模式检查
    useNavigationBlocker(
        calibrationActive,
        t.CALIBRATION_MODE_WARNING_TITLE,
        t.CALIBRATION_MODE_WARNING_MESSAGE,
        async () => {
            onEndManualCalibration();
            return true;
        }
    );

    // 弹窗询问用户是否关闭校准模式
    const showCompletionDialog = async () => {
        const confirmed = await openConfirm({
            title: t.CALIBRATION_COMPLETION_DIALOG_TITLE,
            message: t.CALIBRATION_COMPLETION_DIALOG_MESSAGE
        });

        if (confirmed) {
            onEndManualCalibration();
        }
    };

    const deleteCalibrationDataClick = async () => {
        const confirmed = await openConfirm({
            title: t.CALIBRATION_CLEAR_DATA_DIALOG_TITLE,
            message: t.CALIBRATION_CLEAR_DATA_DIALOG_MESSAGE
        });

        if (confirmed) {
            await clearManualCalibrationData();
        }
    };

    const onStartManualCalibration = async () => {
        setCalibrationActive(true);
    }

    const onEndManualCalibration = async () => {
        setCalibrationActive(false);
    }

    // 检查手动校准是否完成，如果未完成，则弹出确认对话框
    const checkIsManualCalibrationCompletedHandle = useCallback(async () => {
        const isCompleted = await checkIsManualCalibrationCompleted();
        if(!isCompleted) {
            const confirmation = await openConfirm({
                title: t.CALIBRATION_CHECK_COMPLETED_DIALOG_TITLE,
                message: t.CALIBRATION_CHECK_COMPLETED_DIALOG_MESSAGE
            });
            if(confirmation && !calibrationActive) {
                onStartManualCalibration();
            }
        }
    }, [checkIsManualCalibrationCompleted, calibrationActive]);

    // 处理热键更新回调
    const handleHotkeyUpdate = useCallback((hotkeys: Hotkey[]) => {
        setCurrentHotkeys(hotkeys);
        setNeedUpdate(true);
    }, [currentHotkeys]);

    useEffect(() => {
        if(needUpdate) {
            updateHotkeysConfig(currentHotkeys);
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
    }, [sendPendingCommandImmediately]);

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
        if(calibrationActive) {
            setFinishConfigDisabled(true);
        } else if(!calibrationActive) {
            setFinishConfigDisabled(false);
        }
        return () => {
            setFinishConfigDisabled(false);
        }
    }, [calibrationActive]);

    useEffect(() => {
        // 如果webscoket断开，并且校准正在进行中，则停止校准
        if(!wsConnected && calibrationActive) {
            setCalibrationActive(false);
        }
    }, [wsConnected]);

    // 初始化 currentHotkeys
    useEffect(() => {
        if(isInit) {
            return;
        }
        
        if(!isInit && dataIsReady && hotkeysConfig.length > 0) {
            setCurrentHotkeys(Array.from({ length: DEFAULT_NUM_HOTKEYS_MAX }, (_, i) => {
                return hotkeysConfig?.[i] ?? { key: -1, action: HotkeyAction.None, isLocked: false, isHold: false };
            }));

            // 每次进入页面的时候会检查是否按键都校准了，如果未完成，则弹出确认对话框
            checkIsManualCalibrationCompletedHandle();

            setIsInit(true);
        }
    }, [dataIsReady, hotkeysConfig]);

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
                <HotkeySettingContent
                    disabled={calibrationActive}
                    onHotkeysUpdate={handleHotkeyUpdate}
                    hotkeys={currentHotkeys}
                />
            </MainContent>
            
            <TopButtons config={topButtonsConfig} />
        </SettingContentLayout>
    );
} 