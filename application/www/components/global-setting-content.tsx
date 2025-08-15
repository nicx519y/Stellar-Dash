"use client";

import { useEffect, useState, useMemo, useCallback, useRef } from "react";
import {
    HOTKEYS_SETTINGS_INTERACTIVE_IDS,
    HotkeyAction,
    DEFAULT_NUM_HOTKEYS_MAX,
    Hotkey,
} from "@/types/gamepad-config";
import HitboxCalibration from "@/components/hitbox/hitbox-calibration";
import HitboxHotkey from "@/components/hitbox/hitbox-hotkey";
import { btnPosList } from "@/components/hitbox/hitbox-base";
import { HotkeySettingContent } from "./hotkey-setting-content";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { useLanguage } from "@/contexts/language-context";
import { InputModeSettingContent } from "./input-mode-content";
import { openConfirm } from "@/components/dialog-confirm";
import { GamePadColor } from "@/types/gamepad-color";
import { useNavigationBlocker } from '@/hooks/use-navigation-blocker';
import React from "react";
import { eventBus, EVENTS } from "@/lib/event-manager";
import { CalibrationStatus } from "@/types/types";
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
        startManualCalibration,
        stopManualCalibration,
        updateHotkeysConfig,
        globalConfig,
        hotkeysConfig,
        dataIsReady,
        sendPendingCommandImmediately,
        setFinishConfigDisabled,
        wsConnected,
        // updateGlobalConfig,
    } = useGamepadConfig();

    const [isInit, setIsInit] = useState<boolean>(false);
    const [needUpdate, setNeedUpdate] = useState<boolean>(false);

    // 添加本地 hotkeys 状态来存储用户修改
    const [currentHotkeys, setCurrentHotkeys] = useState<Hotkey[]>([]);

    const [hasShownCompletionDialog, setHasShownCompletionDialog] = useState<boolean>(false);

    const [calibrationStatus, setCalibrationStatus] = useState<CalibrationStatus>({
        isActive: false,
        uncalibratedCount: 0,
        activeCalibrationCount: 0,
        allCalibrated: false,
        buttons: []
    });

    // 使用 useRef 来保存 calibrationStatus 的最新引用
    const calibrationStatusRef = useRef(calibrationStatus);
    // 每次渲染时更新 ref
    calibrationStatusRef.current = calibrationStatus;

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
                return true;
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

    // 手动校准状态监听 - 只订阅一次，通过 ref 获取最新状态
    useEffect(() => {
        // 添加校准状态更新事件监听
        const handleCalibrationUpdate = (data: unknown) => {
            // 通过 ref 获取最新的 calibrationStatus 状态
            if (!calibrationStatusRef.current.isActive) return;
            
            // 处理校准状态更新
            if (data && typeof data === 'object' && 'calibrationStatus' in data) {
                const eventData = data as { calibrationStatus?: CalibrationStatus };
                if (eventData.calibrationStatus) {
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
                    setCalibrationStatus(newCalibrationStatus);
                }
            }
        };

        // 订阅校准更新事件（只订阅一次）
        const unsubscribe = eventBus.on(EVENTS.CALIBRATION_UPDATE, handleCalibrationUpdate);
        
        // 清理函数
        return () => {
            unsubscribe();
        };
    }, []); // 依赖数组为空，只执行一次

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

    const onStartManualCalibration = async () => {
        try {
            const status = await startManualCalibration();
            setCalibrationStatus(status);
        } catch {
            throw new Error("Failed to start manual calibration");
        }
    }

    const onEndManualCalibration = async () => {
        try {
            const status = await stopManualCalibration();
            setCalibrationStatus(status);
        } catch {
            throw new Error("Failed to stop manual calibration");
        }
    }

    // 处理外部点击（从Hitbox组件）
    const handleExternalClick = (keyId: number) => {

        if (keyId >= 0 && keyId < btnPosList.length - 1) {
            // 触发自定义事件通知HotkeySettingContent组件
            const event = new CustomEvent('hitbox-click', {
                detail: { keyId }
            });
            window.dispatchEvent(event);
        }
    };

    // 处理热键更新回调
    const handleHotkeyUpdate = useCallback((hotkeys: Hotkey[]) => {
        setCurrentHotkeys(hotkeys);
        setNeedUpdate(true);
    }, [currentHotkeys]);

    // 初始化 currentHotkeys
    useEffect(() => {
        if(isInit) {
            return;
        }
        
        if(!isInit && dataIsReady && hotkeysConfig.length > 0) {
            setCurrentHotkeys(Array.from({ length: DEFAULT_NUM_HOTKEYS_MAX }, (_, i) => {
                return hotkeysConfig?.[i] ?? { key: -1, action: HotkeyAction.None, isLocked: false, isHold: false };
            }));
            setIsInit(true);
        }
    }, [dataIsReady, hotkeysConfig]);

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

    // 当校准状态改变时，更新完成配置按钮的禁用状态
    useEffect(() => {
        if(calibrationStatus.isActive) {
            setFinishConfigDisabled(true);
        } else if(!calibrationStatus.isActive) {
            setFinishConfigDisabled(false);
        }
        return () => {
            setFinishConfigDisabled(false);
        }
    }, [calibrationStatus.isActive]);

    useEffect(() => {
        // 如果webscoket断开，并且校准正在进行中，则停止校准
        if(!wsConnected && calibrationStatus.isActive) {
            setCalibrationStatus({
                isActive: false,
                uncalibratedCount: 0,
                activeCalibrationCount: 0,
                allCalibrated: false,
                buttons: []
            });
        }
    }, [wsConnected]);

    // 渲染hitbox内容
    const renderHitboxContent = (containerWidth: number) => {
        if (!calibrationStatus.isActive) {
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
                    hasText={false}
                    buttonsColorList={calibrationButtonColors}
                    containerWidth={containerWidth}
                />
            );
        }
    };

    // 上方按键配置
    const topButtonsConfig = {
        show: true,
        buttons: [
            {
                text: calibrationStatus.isActive ? t.CALIBRATION_STOP_BUTTON : t.CALIBRATION_START_BUTTON,
                icon: (calibrationStatus.isActive ? FaRegStopCircle : VscDashboard),
                color: (calibrationStatus.isActive ? "blue" : "green") as "blue" | "green",
                size: "sm" as const,
                width: "190px",
                disabled: globalConfig.autoCalibrationEnabled,
                onClick: calibrationStatus.isActive ? onEndManualCalibration : onStartManualCalibration,
                hasTip: !calibrationStatus.isActive,
                tipMessage: t.CALIBRATION_TIP_MESSAGE,
            },
            {
                text: t.CALIBRATION_CLEAR_DATA_BUTTON,
                icon: LuTimerReset,
                color: "red" as const,
                size: "sm" as const,
                width: "190px",
                disabled: globalConfig.autoCalibrationEnabled || calibrationStatus.isActive,
                onClick: deleteCalibrationDataClick,
            }
        ],
        gap: 2,
        justifyContent: "flex-end" as const
    };

    return (
        <SettingContentLayout
            disabled={calibrationStatus.isActive}
        >
            <SideContent>
                <InputModeSettingContent disabled={calibrationStatus.isActive} />
            </SideContent>
            
            <HitboxContent>
                {renderHitboxContent}
            </HitboxContent>
            
            <MainContent>
                <HotkeySettingContent
                    disabled={calibrationStatus.isActive}
                    onHotkeysUpdate={handleHotkeyUpdate}
                    hotkeys={currentHotkeys}
                />
            </MainContent>
            
            <TopButtons config={topButtonsConfig} />
        </SettingContentLayout>
    );
} 