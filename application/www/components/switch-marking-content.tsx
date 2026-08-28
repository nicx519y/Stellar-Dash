import { useLanguage } from "@/contexts/language-context";
import { Box, Flex, Center, Stack, IconButton, Button, VStack, Badge, HStack } from "@chakra-ui/react";
import { SegmentedControl } from "./ui/segmented-control";
import { Line } from 'react-chartjs-2';
import { Chart as ChartJS, CategoryScale, LinearScale, PointElement, LineElement, Title, Tooltip, Legend, ChartData, ChartOptions } from 'chart.js';
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { MenuContent, MenuItem, MenuRoot, MenuTrigger } from "./ui/menu";
import { LuTrash, LuPlus, LuMenu, LuStar, LuCheck, LuPencil } from "react-icons/lu";
import { openForm } from "./dialog-form";
import {
    SWITCH_MARKING_COUNT_MAX,
    SWITCH_MARKING_LENGTH_MAX,
    SWITCH_MARKING_LENGTH_MIN,
    SWITCH_MARKING_NAME_MAX_LENGTH,
    SWITCH_MARKING_STEP_MAX,
    SWITCH_MARKING_STEP_MIN,
} from "@/types/gamepad-config";
import { openConfirm } from "./dialog-confirm";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { useNavigationBlocker } from "@/hooks/use-navigation-blocker";
import { useColorMode } from "./ui/color-mode";

// 导入事件总线
import { eventBus, EVENTS } from "@/lib/event-manager";
import { StepInfo } from "@/types/adc";

// 注册Chart.js组件
ChartJS.register(CategoryScale, LinearScale, PointElement, LineElement, Title, Tooltip, Legend);

export function SwitchMarkingContent() {
    const { t } = useLanguage();
    const { colorMode } = useColorMode();

    const [samplingNoise, setSamplingNoise] = useState<number>(0);
    const [samplingFrequency, setSamplingFrequency] = useState<number>(0);

    const gridColor = useMemo(() => {
        return colorMode === 'dark' ? 'rgba(255,255,255,0.1)' : 'rgba(0,0,0,0.1)';
    }, [colorMode]); 
    const options: ChartOptions<"line"> = {
        responsive: true,
        plugins: {
            legend: {
                position: 'top' as const,
                display: false,
            },
            title: {
                display: false,
                text: 'Chart.js Line Chart',
            },
        },
        scales: {
            x: {
                grid: {
                    color: gridColor,
                },
            },
            y: {
                grid: {
                    color: gridColor,
                },
            }
        },
        animation: {
            duration: 500,
            easing: 'easeInOutCubic',
        }
    };

    const [mappingData, setMappingData] = useState<ChartData<"line">>({
        labels: [],
        datasets: []
    });

    const { 
        deviceConnected, dataIsReady,
        mappingList, defaultMappingId, markingStatus, activeMapping,
        fetchMappingList, fetchMarkingStatus, startMarking, stopMarking, stepMarking,
        createMapping, deleteMapping, updateDefaultMapping, renameMapping, fetchActiveMapping,
        updateMarkingStatus
    } = useGamepadConfig();
    const [ isInit, setIsInit ] = useState<boolean>(false);
    const [ activeMappingId, setActiveMappingId ] = useState<string>("");
    const [ markingStatusToastMessage, setMarkingStatusToastMessage ] = useState<string>("");
    // 使用 useRef 保存最新的状态值，避免闭包问题
    const activeMappingIdRef = useRef<string>(activeMappingId);
    const markingStatusRef = useRef<StepInfo | undefined>(markingStatus);
    const stopMarkingRef = useRef(stopMarking);
    const deviceConnectedRef = useRef(deviceConnected);
    const fetchMappingListRef = useRef(fetchMappingList);
    const fetchMarkingStatusRef = useRef(fetchMarkingStatus);
    const fetchActiveMappingRef = useRef(fetchActiveMapping);
    const updateMarkingStatusRef = useRef(updateMarkingStatus);
    stopMarkingRef.current = stopMarking;
    deviceConnectedRef.current = deviceConnected;
    fetchMappingListRef.current = fetchMappingList;
    fetchMarkingStatusRef.current = fetchMarkingStatus;
    fetchActiveMappingRef.current = fetchActiveMapping;
    updateMarkingStatusRef.current = updateMarkingStatus;
    
    // 更新 ref 值
    useEffect(() => {
        activeMappingIdRef.current = activeMappingId;
    }, [activeMappingId]);
    
    useEffect(() => {
        markingStatusRef.current = markingStatus;
    }, [markingStatus, t]);

    const itemsConfig = useMemo(() => {
        return mappingList.filter(m => m.id !== "" && m.name !== "")
            .map(({ id, name }) => ({
                value: id,
                label: (
                    <HStack direction={"row"} alignItems={"center"} gap={2} >
                        { id === defaultMappingId && <LuCheck /> }
                        <span>{name}</span>
                    </HStack>
                )
            }));
    }, [mappingList, defaultMappingId]);

    useEffect(() => {
        if (!deviceConnected) {
            setIsInit(false);
            return;
        }
        if (!isInit && deviceConnected && dataIsReady) {
            void (async () => {
                await fetchMappingListRef.current();
                await fetchMarkingStatusRef.current();
                setIsInit(true);
            })().catch(() => undefined);
        }
    }, [deviceConnected, dataIsReady, isInit]);

    const stopMarkingForNavigation = useCallback(async (): Promise<boolean> => {
        if (!deviceConnectedRef.current || !markingStatusRef.current?.is_marking) {
            return true;
        }
        try {
            await stopMarkingRef.current();
            return true;
        } catch {
            return !deviceConnectedRef.current;
        }
    }, []);

    useNavigationBlocker(
        markingStatus?.is_marking === true,
        t.SETTINGS_SWITCH_MARKING_UNSAVED_CHANGES_WARNING_TITLE,
        t.SETTINGS_SWITCH_MARKING_UNSAVED_CHANGES_WARNING_MESSAGE,
        stopMarkingForNavigation,
    );

    useEffect(() => {
        return () => {
            if (deviceConnectedRef.current && markingStatusRef.current?.is_marking) {
                void stopMarkingRef.current().catch(() => undefined);
            }
        };
    }, []);

    useEffect(() => {
        if (markingStatus?.is_marking &&
            mappingList.some(m => m.id === markingStatus.id) &&
            activeMappingId !== markingStatus.id) {
            setActiveMappingId(markingStatus.id);
            return;
        }

        if(activeMappingId && activeMappingId !== "" && mappingList.find(m => m.id === activeMappingId)) {
            return;
        }

        if(defaultMappingId && mappingList.some(m => m.id === defaultMappingId)) {
            setActiveMappingId(defaultMappingId);
        } else if(mappingList.length > 0) {
            setActiveMappingId(mappingList[0].id);
        } else {
            setActiveMappingId("");
        }
    }, [activeMappingId, mappingList, defaultMappingId, markingStatus?.id, markingStatus?.is_marking]);

    useEffect(() => {
        const activeMappingIsMarking = (markingStatus?.id === activeMappingId);
        // 如果标记中，但标记的不是当前映射，则停止标记
        if(markingStatus?.is_marking && activeMappingId && !activeMappingIsMarking) {
            void stopMarkingRef.current().catch(() => undefined);
        }

        if(activeMappingIsMarking) {
            const myData = {
                labels: Array.from({ length: markingStatus?.length ?? 0 }, (_, i) => (i * (markingStatus?.step ?? 0)).toFixed(2)),
                datasets: [
                    {
                        label: markingStatus.mapping_name,
                        cubicInterpolationMode: 'monotone' as const,
                        tension: .4,
                        fill: true,
                        backgroundColor: 'rgba(75,192,192,0.2)',
                        borderColor: 'rgba(75,192,192,1)',
                        data: markingStatus.values,
                    },
                ],
            };  

            setMappingData(myData);
            setSamplingNoise(markingStatus.sampling_noise);
            setSamplingFrequency(markingStatus.sampling_frequency);

        } else {

            const myData = {
                labels: Array.from({ length: activeMapping?.length ?? 0 }, (_, i) => (i * (activeMapping?.step ?? 0)).toFixed(2)),
                datasets: [
                    {
                        label: activeMapping?.name ?? "",
                        cubicInterpolationMode: 'monotone' as const,
                        tension: .4,
                        fill: true,
                        backgroundColor: 'rgba(75,192,192,0.2)',
                        borderColor: 'rgba(75,192,192,1)',
                        data: activeMapping?.originalValues ?? [],
                    },
                ],
            };

            setMappingData(myData);
            setSamplingNoise(activeMapping?.samplingNoise ?? 0);
            setSamplingFrequency(activeMapping?.samplingFrequency ?? 0);

        }
    }, [activeMapping, activeMappingId, markingStatus]);

    useEffect(() => {
        if(activeMappingId && activeMappingId !== "" && mappingList.find(m => m.id === activeMappingId)) {
            void fetchActiveMappingRef.current(activeMappingId).catch(() => undefined);
        }
    }, [activeMappingId, mappingList]);

    // 更新标记状态提示信息
    useEffect(() => {
        if(!markingStatus) {
            setMarkingStatusToastMessage("");
            return;
        }

        // 如果标记未开始，则弹出提示
        if(!markingStatus.is_marking && !markingStatus.is_completed && !markingStatus.is_sampling) {
            setMarkingStatusToastMessage(t.SETTINGS_SWITCH_MARKING_START_DIALOG_MESSAGE);
        // 如果标记完成，则弹出提示
        } else if(!markingStatus.is_marking && markingStatus.is_completed) {
            setMarkingStatusToastMessage(t.SETTINGS_SWITCH_MARKING_COMPLETED_DIALOG_MESSAGE);
        // 如果标记开始，则弹出提示
        } else if(markingStatus.is_marking && !markingStatus.is_completed && !markingStatus.is_sampling) {
            // 如果步进即将完成，则弹出保存提示
            if(markingStatus.index >= markingStatus.length - 1) {
                setMarkingStatusToastMessage(t.SETTINGS_SWITCH_MARKING_SAVE_DIALOG_MESSAGE);
            // 如果步进未完成，则弹出步进提示
            } else {
                const step = markingStatus.index + 2;
                const distance = ((markingStatus.index + 1) * (markingStatus.step ?? 0)).toFixed(2);
                setMarkingStatusToastMessage(t.SETTINGS_SWITCH_MARKING_SAMPLING_START_DIALOG_MESSAGE.replace("<step>", step.toString()).replace("<distance>", distance));
            }
        // 如果采样中，则弹出提示
        } else if(markingStatus.is_sampling) {
            setMarkingStatusToastMessage(t.SETTINGS_SWITCH_MARKING_SAMPLING_DIALOG_MESSAGE.replace("<step>", (markingStatus.index + 2).toString()).replace("<distance>", ((markingStatus.index + 1) * (markingStatus.step ?? 0)).toFixed(2)));
        } else {
            setMarkingStatusToastMessage("");
        }
    }, [markingStatus]);

    // 订阅标记状态更新事件，在组件整个生命周期中保持订阅
    useEffect(() => {
        // 订阅标记状态更新事件
        const unsubscribe = eventBus.on(EVENTS.MARKING_STATUS_UPDATE, (data: unknown) => {
            if (data && typeof data === 'object' && 'status' in data) {
                const eventData = data as { status: StepInfo };
                const newStatus = eventData.status;
                console.log('通过事件总线收到标记状态更新:', newStatus);
                
                // 使用ref获取最新的状态值
                const currentActiveMappingId = activeMappingIdRef.current;
                const currentMarkingStatus = markingStatusRef.current;
                
                // 只有当前正在标记的映射或者状态发生变化时才更新
                if (newStatus.id === currentActiveMappingId || 
                    newStatus.is_marking !== currentMarkingStatus?.is_marking) {
                    updateMarkingStatusRef.current(newStatus);
                }
            }
        });

        console.log('订阅标记状态更新事件');

        // 返回清理函数，只在组件卸载时取消订阅
        return () => {
            console.log('取消订阅标记状态更新事件');
            unsubscribe();
        };
    // }, [updateMarkingStatus]); // 添加updateMarkingStatus依赖
    }, []);

    const createMappingClick = async () => {
        const result = await openForm({
            fields: [{
                name: "name",
                label: t.SETTINGS_SWITCH_MARKING_NAME_LABEL,
                placeholder: t.SETTINGS_SWITCH_MARKING_NAME_PLACEHOLDER,
                type: "text",
                defaultValue: "",
                validate: (value: string) => {
                    const [isValid, errorMessage] = validateSwitchMarkingName(value);
                    if (!isValid) {
                        return errorMessage;
                    }
                    return undefined;
                }
            }, {
                name: "length",
                label: t.SETTINGS_SWITCH_MARKING_LENGTH_LABEL,
                placeholder: t.SETTINGS_SWITCH_MARKING_LENGTH_PLACEHOLDER,
                type: "number",
                defaultValue: SWITCH_MARKING_LENGTH_MIN.toString(),
                min: SWITCH_MARKING_LENGTH_MIN,
                max: SWITCH_MARKING_LENGTH_MAX,
                step: 1,
                validate: (value: string) => {
                    const num = Number(value);
                    const [isValid, errorMessage] = validateSwitchMarkingLength(num);
                    if (!isValid) {
                        return errorMessage;
                    }
                    return undefined;
                }
            }, {
                name: "step",
                label: t.SETTINGS_SWITCH_MARKING_STEP_LABEL,
                placeholder: t.SETTINGS_SWITCH_MARKING_STEP_PLACEHOLDER,
                type: "number",
                defaultValue: "0.1",
                min: SWITCH_MARKING_STEP_MIN,
                max: SWITCH_MARKING_STEP_MAX,
                step: 0.1,
                validate: (value: string) => {
                    const num = parseFloat(value);
                    const [isValid, errorMessage] = validateSwitchMarkingStep(num);
                    if (!isValid) {
                        return errorMessage;
                    }
                    return undefined;
                }
            }]
        });

        if (result) {
            const createdMappingId = await createMapping(
                result.name,
                parseInt(result.length, 10),
                parseFloat(result.step),
            );
            setActiveMappingId(createdMappingId);
        }
    }

    const validateSwitchMarkingName = (name: string, excludedId?: string): [boolean, string] => {

        if (/[!@#$%^&*()_+\[\]{}|;:'",.<>?/\\]/.test(name)) {
            return [false, t.SETTINGS_SWITCH_MARKING_VALIDATION_SPECIAL_CHARS];
        }

        const nameBytes = new TextEncoder().encode(name).byteLength;
        if (nameBytes > SWITCH_MARKING_NAME_MAX_LENGTH || nameBytes < 1) {
            return [false, t.SETTINGS_SWITCH_MARKING_VALIDATION_LENGTH.replace("{0}", nameBytes.toString())];
        }

        if (mappingList.find(p => p.id !== excludedId && p.name === name)) {
            return [false, t.SETTINGS_SWITCH_MARKING_VALIDATION_SAME_NAME];
        }

        return [true, ""];
    }

    const validateSwitchMarkingLength = (length: number): [boolean, string] => {
        if (!Number.isInteger(length) || length < SWITCH_MARKING_LENGTH_MIN || length > SWITCH_MARKING_LENGTH_MAX) {
            return [false, t.SETTINGS_SWITCH_MARKING_VALIDATION_LENGTH_RANGE.replace("{0}", length.toString())];
        }
        return [true, ""];
    }

    const validateSwitchMarkingStep = (step: number): [boolean, string] => {
        if (!Number.isFinite(step) || step < SWITCH_MARKING_STEP_MIN || step > SWITCH_MARKING_STEP_MAX) {
            return [false, t.SETTINGS_SWITCH_MARKING_VALIDATION_STEP_RANGE.replace("{0}", step.toString())];
        }
        return [true, ""];
    }

    const deleteMappingClick = async () => {
        if (!activeMappingId || activeMapping?.id !== activeMappingId) {
            return;
        }
        const confirmed = await openConfirm({
            title: t.SETTINGS_SWITCH_MARKING_DELETE_DIALOG_TITLE,
            message: t.SETTINGS_SWITCH_MARKING_DELETE_CONFIRM_MESSAGE
        });


        if (confirmed) {
            await deleteMapping(activeMappingId);
        }
    }

    const setDefaultMappingClick = async () => {
        if(activeMapping?.id === activeMappingId) {
            await updateDefaultMapping(activeMappingId);
        }
    }

    const renameMappingClick = async () => {
        if(!activeMapping || activeMapping.id !== activeMappingId) {
            return;
        }
        const result = await openForm({
            fields: [{
                name: "name",
                label: t.SETTINGS_SWITCH_MARKING_NAME_LABEL,
                placeholder: t.SETTINGS_SWITCH_MARKING_NAME_PLACEHOLDER,
                type: "text",
                defaultValue: activeMapping?.name ?? "",
                validate: (value: string) => {
                    const [isValid, errorMessage] = validateSwitchMarkingName(value, activeMapping.id);
                    if (!isValid) {
                        return errorMessage;
                    }
                    return undefined;
                }
            }]
        });

        if (result) {
            await renameMapping(activeMappingId, result.name);
        }
    }

    const activeMappingChange = (id: string) => {
        if (markingStatus?.is_marking) {
            return;
        }
        setActiveMappingId(id);
    }

    const activeMappingReady = !!activeMapping && activeMapping.id === activeMappingId;
    const activeMappingIsMarked = activeMappingReady &&
        activeMapping.length >= SWITCH_MARKING_LENGTH_MIN &&
        activeMapping.originalValues.length >= activeMapping.length &&
        activeMapping.originalValues[0] > 0 &&
        activeMapping.originalValues[activeMapping.length - 1] > 0 &&
        activeMapping.originalValues[0] !== activeMapping.originalValues[activeMapping.length - 1];

    // 菜单项
    const menuItems = [
        {
            value: "create",
            label: "Add New",
            icon: <LuPlus />,
            onClick: createMappingClick,
            disabled: markingStatus?.is_marking === true || mappingList.length >= SWITCH_MARKING_COUNT_MAX,
        },
        {
            value: "delete",
            label: "Delete",
            icon: <LuTrash />,
            onClick: deleteMappingClick,
            disabled: markingStatus?.is_marking === true || !activeMappingReady || activeMappingId === defaultMappingId,
        },
        {
            value: "rename",
            label: "Rename",
            icon: <LuPencil />,
            onClick: renameMappingClick,
            disabled: markingStatus?.is_marking === true || !activeMappingReady,
        },
        {
            value: "set_default",
            label: "Set Default",
            icon: <LuStar />,
            onClick: setDefaultMappingClick,
            disabled: markingStatus?.is_marking === true || !activeMappingReady || activeMappingId === defaultMappingId || !activeMappingIsMarked,
        }
    ];

    return (
        <>
            <Flex direction={"column"} height={"100%"} width={"1700px"} padding={"30px"} >
                <VStack width={"100%"} >
                    <Center width={"100%"} >
                        <Stack direction="row" gap={2} alignItems="center">
                            <SegmentedControl
                                size="sm"
                                value={activeMappingId}
                                items={itemsConfig}
                                disabled={markingStatus?.is_marking === true}
                                onValueChange={(detail) => activeMappingChange(detail?.value ?? "")}
                            />
                            <MenuRoot size="md">
                                <MenuTrigger asChild>
                                    <IconButton
                                        aria-label="Menu"
                                        variant="ghost"
                                        size="sm"
                                    >
                                        <LuMenu />
                                    </IconButton>
                                </MenuTrigger>
                                <MenuContent>
                                    {menuItems.map((item) => (
                                        <MenuItem key={item.value} value={item.value} onClick={item.disabled? undefined : item.onClick} disabled={item.disabled}>
                                            {item.icon} {item.label}
                                        </MenuItem>
                                    ))}
                                </MenuContent>
                            </MenuRoot>
                            <Button 
                                display={ activeMappingId === "" ? "none" : "" }
                                colorPalette={ markingStatus?.is_marking ? "red" : "green" } 
                                size="xs" variant={ !markingStatus?.is_marking ? "solid" : "outline" } 
                                disabled={!markingStatus?.is_marking && !activeMappingReady}
                                onClick={() => {
                                if(markingStatus?.is_marking) {
                                    void stopMarking().catch(() => undefined);
                                } else {
                                    void startMarking(activeMappingId).catch(() => undefined);
                                }   
                            }}>
                                { markingStatus?.is_marking ? "Stop Marking" : "Start Marking" }
                            </Button>
                            <Button 
                                display={ activeMappingId === "" ? "none" : "" }
                                colorPalette={"green"} size="xs" 
                                variant={ markingStatus?.is_marking ? "solid" : "outline" } 
                                disabled={!markingStatus?.is_marking || markingStatus?.is_sampling} 
                                onClick={() => {
                                void stepMarking().catch(() => undefined);
                            }}>
                                { "Step" }
                            </Button>
                        </Stack>
                    </Center>
                    <Center width={"100%"} height={"2em"} paddingTop={"1em"} >
                        <Badge colorPalette={"green"} variant={"outline"} size="sm" >{ markingStatusToastMessage }</Badge>
                    </Center>
                    
                </VStack>
                <Box width={"100%"} flexGrow={1} padding={"18px 0"} position="relative" >
                    <HStack 
                        position="absolute" 
                        top="30px" 
                        right="30px" 
                        zIndex={1}
                        padding="2"
                        gap={2}
                    >
                        <Badge colorPalette={"blue"} variant={"outline"} size="sm" >
                            Sampling Frequency: { isNaN(samplingFrequency) ? 'N/A' : samplingFrequency?.toFixed(0) + ' Hz'}
                        </Badge>
                        <Badge colorPalette={"red"} variant={"outline"} size="sm">
                            Sampling Noise: { isNaN(samplingNoise) ? 'N/A' : samplingNoise?.toFixed(0) }
                        </Badge>
                    </HStack>
                    <Line data={mappingData} options={options} />
                </Box>
            </Flex>
        </>
    );
}
