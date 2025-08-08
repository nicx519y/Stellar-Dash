"use client";

import {
    Flex,
    Center,
    Card,
    Box,
    Button,
} from "@chakra-ui/react";
import { useEffect, useState, useRef, useMemo } from "react";
import { RAPID_TRIGGER_SETTINGS_INTERACTIVE_IDS } from "@/types/gamepad-config";
import HitboxPerformance from "@/components/hitbox/hitbox-performance";
import { ContentActionButtons } from "@/components/content-action-buttons";
import { ProfileSelect } from "./profile-select";
import { ButtonsPerformanceSettingContent } from "./buttons-performance-setting-content";
import { ButtonsPerformenceTestContent } from "./buttons-performence-test-content";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { useLanguage } from "@/contexts/language-context";
import { useButtonPerformanceMonitor, type ButtonPerformanceMonitoringData, type ButtonPerformanceData } from '@/hooks/use-button-performance-monitor';
import HitboxPerformanceTest from "./hitbox/hitbox-performance-test";




export function ButtonsPerformanceContent() {

    const [isTestingModeActivity, setIsTestingModeActivity] = useState<boolean>(false);
    const { t } = useLanguage();

    // 添加容器引用和宽度状态
    const { defaultProfile, sendPendingCommandImmediately } = useGamepadConfig();
    const containerRef = useRef<HTMLDivElement>(null);
    const { setError } = useGamepadConfig();
    const [containerWidth, setContainerWidth] = useState<number>(0);
    const [selectedButton, setSelectedButton] = useState<number>(0); // 当前选中的按钮
    const [canSelectAllButton, setCanSelectAllButton] = useState<boolean>(false);
    const [isInit, setIsInit] = useState<boolean>(false);
    const interactive = useMemo(() => canSelectAllButton ? RAPID_TRIGGER_SETTINGS_INTERACTIVE_IDS : [], [canSelectAllButton]);

    const [performanceData, setPerformanceData] = useState<ButtonPerformanceMonitoringData | null>(null);
    const [allButtonsData, setAllButtonsData] = useState<ButtonPerformanceData[]>([]);
    const monitorIsActive = useRef(false);

    // 监听容器宽度变化
    useEffect(() => {
        const updateWidth = () => {
            if (containerRef.current) {
                const width = containerRef.current.offsetWidth;
                console.log("buttons-performance containerWidth: ", width);
                setContainerWidth(width);
            }
        };

        // 初始获取宽度
        updateWidth();

        // 监听窗口大小变化
        const resizeObserver = new ResizeObserver(updateWidth);
        if (containerRef.current) {
            resizeObserver.observe(containerRef.current);
        }

        // 清理
        return () => {
            resizeObserver.disconnect();
        };
    }, []);

    useEffect(() => {
        if(defaultProfile.triggerConfigs && !isInit) {
            setCanSelectAllButton(!(defaultProfile.triggerConfigs.isAllBtnsConfiguring));
            setSelectedButton(0);
            setIsInit(true);
        }
    }, [defaultProfile]);


    /**
     * 点击按钮
     */
    const handleButtonClick = (id: number) => {
        if (selectedButton !== id && id >= 0) {
            setSelectedButton(id);
        }
    };

    // 计算缩放比例
    const calculateScale = (): number => {
        if (!containerWidth) return 1;
        
        const hitboxWidth = 829; // StyledSvg的原始宽度
        const margin = 80; // 左右边距
        const availableWidth = containerWidth - (margin * 2);
        
        if (availableWidth <= 0) return 0.1; // 最小缩放比例
        
        const scale = availableWidth / hitboxWidth;
        return Math.min(scale, 1.3); // 最大不超过1.3，避免过度放大
    };

    const scale = calculateScale();

    // 计算动态位置偏移
    const calculateDynamicOffset = (): number => {
        const baseOffset = 280; // 原始偏移量
        const margin = 80; // 保持80px边距
        return (baseOffset * scale) + margin;
    };

    const dynamicOffset = calculateDynamicOffset();
    

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

    useEffect(() => {
        return () => {
            if(monitorIsActive.current == true) {
                handleStopMonitoring();
            }
            sendPendingCommandImmediately('update_profile');
        }
    }, []);

    return (
        <>
            <Flex direction="row" width={"100%"} height={"100%"} padding={"18px"} >
                <Flex flex={0} justifyContent={"flex-start"} height="fit-content" >
                    <ProfileSelect disabled={isTestingModeActivity} />
                </Flex>
                <Center ref={containerRef} flex={1} justifyContent={"center"} flexDirection={"column"}>
                    <Center padding="80px 30px" position="relative" flex={1}  >

                    <Box position="absolute" top="50%" left="50%" transform={`translateX(-50%) translateY(-${dynamicOffset}px)`} zIndex={10} >
                        <Card.Root w="100%" h="100%" >
                            <Card.Body p="10px" >
                                <Button w="240px" size="xs" variant="solid" colorPalette={isTestingModeActivity ? "blue" : "green"} onClick={() => setIsTestingModeActivity(!isTestingModeActivity)} >
                                    {isTestingModeActivity ? t.SETTINGS_RAPID_TRIGGER_EXIT_TEST_MODE_BUTTON : t.SETTINGS_RAPID_TRIGGER_ENTER_TEST_MODE_BUTTON}
                                </Button>
                            </Card.Body>
                        </Card.Root>
                    </Box>
                    {!isTestingModeActivity && 
                        <HitboxPerformance
                            onClick={(id) => handleButtonClick(id)}
                            highlightIds={[selectedButton ?? -1]}
                            interactiveIds={interactive}
                            containerWidth={containerWidth}
                        />
                    }
                    {isTestingModeActivity && 
                        <HitboxPerformanceTest
                            buttonStates={allButtonsData}
                            containerWidth={containerWidth}
                        />
                    }
                    </Center>
                    <Center flex={0}  >
                        <ContentActionButtons disabled={isTestingModeActivity} />
                    </Center>
                </Center>
                <Center>
                    {!isTestingModeActivity && 
                    <ButtonsPerformanceSettingContent
                        selectedButton={selectedButton}
                        selectAllButtonHandler={(result: boolean) => setCanSelectAllButton(!result)}
                    />
                    }
                    {isTestingModeActivity && 
                    <ButtonsPerformenceTestContent
                        allButtonsData={allButtonsData}
                        maxTravelDistance={performanceData?.maxTravelDistance || 0}
                    />
                    }
                </Center>
            </Flex>
        </>

    );
} 