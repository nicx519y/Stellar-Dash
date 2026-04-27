"use client";

import { useEffect, useState, useRef, useMemo } from "react";
import { RAPID_TRIGGER_SETTINGS_INTERACTIVE_IDS } from "@/types/gamepad-config";
import HitboxPerformance from "@/components/hitbox/hitbox-performance";
import { ProfileSelect } from "./profile-select";
import { ButtonsPerformanceSettingContent } from "./buttons-performance-setting-content";
import { ButtonsPerformenceTestContent } from "./buttons-performence-test-content";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { useLanguage } from "@/contexts/language-context";
import { useButtonPerformanceMonitor, type ButtonPerformanceMonitoringData, type ButtonPerformanceData } from '@/hooks/use-button-performance-monitor';
import HitboxPerformanceTest from "./hitbox/hitbox-performance-test";
import { 
    SettingContentLayout, 
    SideContent, 
    HitboxContent, 
    MainContent, 
    TopButtons 
} from "./setting-content-layout";
import { BiSolidExit } from "react-icons/bi";
import { ImEqualizer2 } from "react-icons/im";
import { Platform } from "@/types/gamepad-config";

export function ButtonsPerformanceContent() {
    const [isTestingModeActivity, setIsTestingModeActivity] = useState<boolean>(false);
    const { t } = useLanguage();

    const { defaultProfile, sendPendingCommandImmediately, setFinishConfigDisabled, globalConfig } = useGamepadConfig();
    const { setError } = useGamepadConfig();
    const [selectedButtons, setSelectedButtons] = useState<number[]>([]);
    const initializedSelectionProfileIdRef = useRef<string | null>(null);

    const [performanceData, setPerformanceData] = useState<ButtonPerformanceMonitoringData | null>(null);
    const [allButtonsData, setAllButtonsData] = useState<ButtonPerformanceData[]>([]);
    const monitorIsActive = useRef(false);

    // 使用 context 中的 indexMapToGameControllerButtonOrCombination 方法
    const { indexMapToGameControllerButtonOrCombination } = useGamepadConfig();

    const mappedRapidTriggerButtons = useMemo(() => {
        const keyMapping = defaultProfile.keysConfig?.keyMapping ?? {};
        const mappedIds = Object.values(keyMapping)
            .flatMap((ids) => ids ?? [])
            .map((id) => Number(id))
            .filter((id) => Number.isFinite(id) && RAPID_TRIGGER_SETTINGS_INTERACTIVE_IDS.includes(id));
        return Array.from(new Set(mappedIds));
    }, [defaultProfile.keysConfig?.keyMapping]);

    useEffect(() => {
        if (initializedSelectionProfileIdRef.current === defaultProfile.id) return;
        initializedSelectionProfileIdRef.current = defaultProfile.id;
        setSelectedButtons(mappedRapidTriggerButtons.length > 0 ? mappedRapidTriggerButtons : RAPID_TRIGGER_SETTINGS_INTERACTIVE_IDS);
    }, [defaultProfile.id]);

    const highlightIds = useMemo(() => selectedButtons, [selectedButtons]);

    const handleButtonClick = (id: number) => {
        if (id < 0) return;
        if (!RAPID_TRIGGER_SETTINGS_INTERACTIVE_IDS.includes(id)) return;
        setSelectedButtons((prev) => prev.includes(id) ? prev.filter((x) => x !== id) : [...prev, id]);
    };

    /***************************************** 按键性能监控 ********************************************** */

    const { startMonitoring, stopMonitoring } = useButtonPerformanceMonitor({
        onError: (err) => {
            setError(err.message);
            console.error('Button performance monitoring error:', err.message);
        },
        onMonitoringStateChange: (active) => {
            console.log(`Monitoring state: ${active ? 'started' : 'stopped'}`);
        },
        onButtonPerformanceData: (data: ButtonPerformanceMonitoringData) => {
            setPerformanceData(data);

            // 实时更新所有按键数据
            const updatedAllButtonsData = [...allButtonsData];
            
            for(let i = 0; i < data.buttonCount; i++) {
                const buttonData: ButtonPerformanceData = data.buttonData[i];
                updatedAllButtonsData[buttonData.buttonIndex] = buttonData;
            }

            setAllButtonsData(updatedAllButtonsData);
        },
        useEventBus: true, // 使用 eventBus 监听
    });

    const handleStartMonitoring = async () => {

        if(monitorIsActive.current == true) return;
        monitorIsActive.current = true;

        setError(null);
        console.log('Starting button performance monitoring...');

        setAllButtonsData([]); // reset all buttons data

        try {
            await startMonitoring();
        } catch (err) {
            console.error('Failed to start monitoring:', err);
            monitorIsActive.current = false;
        }
    };

    const handleStopMonitoring = async () => {

        if(monitorIsActive.current == false) return;
        monitorIsActive.current = false;

        console.log('Stopping button performance monitoring...');
        try {
            await stopMonitoring();
            setAllButtonsData([]);
            
        } catch (err) {
            console.error('Failed to stop monitoring:', err);
            monitorIsActive.current = true;
        }
    };

    useEffect(() => {
        if(isTestingModeActivity) {
            sendPendingCommandImmediately('update_profile'); // 切换测试模式之前立即保存配置
            handleStartMonitoring();
        } else {
            handleStopMonitoring();
        }
    }, [isTestingModeActivity]);

    // 当测试模式状态改变时，更新完成配置按钮的禁用状态 
    useEffect(() => {
        setFinishConfigDisabled(isTestingModeActivity);
        return () => {
            setFinishConfigDisabled(false);
        }
    }, [isTestingModeActivity]);

    useEffect(() => {
        return () => {
            if(monitorIsActive.current == true) {
                handleStopMonitoring();
            }
            sendPendingCommandImmediately('update_profile');
        }
    }, []);

    // 渲染hitbox内容
    const renderHitboxContent = (containerWidth: number) => {
        // 使用 context 中的方法，传入本地状态
        const buttonLabelMap = indexMapToGameControllerButtonOrCombination(
            defaultProfile.keysConfig?.keyMapping ?? {},
            defaultProfile.keysConfig?.keyCombinations ?? [],
            globalConfig.inputMode ?? Platform.XINPUT
        );
        
        if (!isTestingModeActivity) {
            return (
                <HitboxPerformance
                    onClick={handleButtonClick}
                    interactiveIds={RAPID_TRIGGER_SETTINGS_INTERACTIVE_IDS}
                    highlightIds={highlightIds}
                    buttonLabelMap={buttonLabelMap}
                    containerWidth={containerWidth}
                />
            );
        } else {
            return (
                <HitboxPerformanceTest
                    buttonStates={allButtonsData}
                    buttonLabelMap={buttonLabelMap}
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
                text: isTestingModeActivity ? t.SETTINGS_RAPID_TRIGGER_EXIT_TEST_MODE_BUTTON : t.SETTINGS_RAPID_TRIGGER_ENTER_TEST_MODE_BUTTON,
                icon: (isTestingModeActivity ? BiSolidExit : ImEqualizer2),
                color: (isTestingModeActivity ? "blue" : "green") as "blue" | "green",
                size: "sm" as const,
                width: "240px",
                onClick: () => setIsTestingModeActivity(!isTestingModeActivity),
            }
        ],
        gap: 8,
        justifyContent: "flex-end" as const
    };

    return (
        <SettingContentLayout
            disabled={isTestingModeActivity}
        >
            <SideContent>
                <ProfileSelect disabled={isTestingModeActivity} />
            </SideContent>
            
            <HitboxContent>
                {renderHitboxContent}
            </HitboxContent>
            
            <MainContent>
                {!isTestingModeActivity && 
                    <ButtonsPerformanceSettingContent
                        selectedButtons={selectedButtons}
                        onSelectedButtonsChange={setSelectedButtons}
                    />
                }
                {isTestingModeActivity && 
                    <ButtonsPerformenceTestContent
                        allButtonsData={allButtonsData}
                        maxTravelDistance={performanceData?.maxTravelDistance || 0}
                    />
                }
            </MainContent>
            
            <TopButtons config={topButtonsConfig} />
        </SettingContentLayout>
    );
} 
