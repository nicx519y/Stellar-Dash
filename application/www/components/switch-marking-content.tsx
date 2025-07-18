import { useLanguage } from "@/contexts/language-context";
import { Box, Flex, Center, Stack, IconButton, Button, VStack, Badge, HStack } from "@chakra-ui/react";
import { SegmentedControl } from "./ui/segmented-control";
import { Line } from 'react-chartjs-2';
import { Chart as ChartJS, CategoryScale, LinearScale, PointElement, LineElement, Title, Tooltip, Legend, ChartData, ChartOptions } from 'chart.js';
import { useEffect, useMemo, useRef, useState } from "react";
import { MenuContent, MenuItem, MenuRoot, MenuTrigger } from "./ui/menu";
import { LuTrash, LuPlus, LuMenu, LuStar, LuCheck, LuPencil } from "react-icons/lu";
import { openForm } from "./dialog-form";
import { PROFILE_NAME_MAX_LENGTH } from "@/types/gamepad-config";
import { openConfirm } from "./dialog-confirm";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import useUnsavedChangesWarning from "@/hooks/use-unsaved-changes-warning";
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

    const [_isDirty, setIsDirty] = useUnsavedChangesWarning(t.SETTINGS_SWITCH_MARKING_UNSAVED_CHANGES_WARNING_TITLE, t.SETTINGS_SWITCH_MARKING_UNSAVED_CHANGES_WARNING_MESSAGE);

    const [mappingData, setMappingData] = useState<ChartData<"line">>({
        labels: [],
        datasets: []
    });

    const { 
        mappingList, defaultMappingId, markingStatus, activeMapping,
        fetchMappingList, fetchMarkingStatus, startMarking, stopMarking, stepMarking, 
        createMapping, deleteMapping, updateDefaultMapping, renameMapping, fetchActiveMapping,
        updateMarkingStatus
    } = useGamepadConfig();
    const [ activeMappingId, setActiveMappingId ] = useState<string>("");
    const [ markingStatusToastMessage, setMarkingStatusToastMessage ] = useState<string>("");
    const nextActiveMappingIdRef = useRef<string>(activeMappingId);
    
    // 使用 useRef 保存最新的状态值，避免闭包问题
    const activeMappingIdRef = useRef<string>(activeMappingId);
    const markingStatusRef = useRef<StepInfo | undefined>(markingStatus);
    
    // 更新 ref 值
    useEffect(() => {
        activeMappingIdRef.current = activeMappingId;
    }, [activeMappingId]);
    
    useEffect(() => {
        markingStatusRef.current = markingStatus;
    }, [markingStatus]);

    const itemsConfig = useMemo(() => {
        return mappingList.map(({ id, name }) => ({
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
        fetchMappingList();
        // fetchDefaultMapping();
        fetchMarkingStatus(); // 仅用于初始化状态，后续通过WebSocket推送更新

        return () => {
            if(markingStatus?.is_marking) {
                stopMarking();
            }
        }
    }, []);

    useEffect(() => {

        if(nextActiveMappingIdRef.current && nextActiveMappingIdRef.current !== "" && nextActiveMappingIdRef.current !== activeMappingId) {
            setActiveMappingId(nextActiveMappingIdRef.current);
            nextActiveMappingIdRef.current = "";
            return;
        }

        if(activeMappingId && activeMappingId !== "" && mappingList.find(m => m.id === activeMappingId)) {
            return;
        }

        if(defaultMappingId && defaultMappingId !== "") {
            setActiveMappingId(defaultMappingId);
        } else if(mappingList.length > 0) {
            setActiveMappingId(mappingList[0].id);
        } else {
            setActiveMappingId("");
        }
    }, [mappingList, defaultMappingId]);

    useEffect(() => {
        // 如果标记中，则设置为脏状态，跳转页面时弹出确认框
        if(markingStatus?.is_marking) {
            setIsDirty(true);
        } else {
            setIsDirty(false);
        }

        const activeMappingIsMarking = (markingStatus?.id === activeMapping?.id);
        // 如果标记中，但标记的不是当前映射，则停止标记
        if(markingStatus?.is_marking && !activeMappingIsMarking) {
            stopMarking();
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
    }, [activeMapping, markingStatus]);

    useEffect(() => {
        if(activeMappingId && activeMappingId !== "" && mappingList.find(m => m.id === activeMappingId)) {
            fetchActiveMapping(activeMappingId);
        }
    }, [activeMappingId]);

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
            if(markingStatus.index >= markingStatus.length) {
                setMarkingStatusToastMessage(t.SETTINGS_SWITCH_MARKING_SAVE_DIALOG_MESSAGE);
            // 如果步进未完成，则弹出步进提示
            } else {
                const step = markingStatus.index + 1;   
                const distance = (step * (markingStatus.step ?? 0)).toFixed(2);
                setMarkingStatusToastMessage(t.SETTINGS_SWITCH_MARKING_SAMPLING_START_DIALOG_MESSAGE.replace("<step>", step.toString()).replace("<distance>", distance));
            }
        // 如果采样中，则弹出提示
        } else if(markingStatus.is_sampling) {
            setMarkingStatusToastMessage(t.SETTINGS_SWITCH_MARKING_SAMPLING_DIALOG_MESSAGE.replace("<step>", (markingStatus.index + 1).toString()).replace("<distance>", (markingStatus.index * (markingStatus.step ?? 0)).toFixed(2)));
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
                    updateMarkingStatus(newStatus);
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
                defaultValue: "1",
                min: 1,
                max: 50,
                step: 1,
                validate: (value: string) => {
                    const num = parseInt(value);
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
                min: 0.1,
                max: 10,
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
            await createMapping(result.name, parseInt(result.length), parseFloat(result.step));
            nextActiveMappingIdRef.current = result.id;
        }
    }

    const validateSwitchMarkingName = (name: string): [boolean, string] => {

        if (/[!@#$%^&*()_+\[\]{}|;:'",.<>?/\\]/.test(name)) {
            return [false, t.SETTINGS_SWITCH_MARKING_VALIDATION_SPECIAL_CHARS];
        }

        if (name.length > PROFILE_NAME_MAX_LENGTH || name.length < 1) {
            return [false, t.SETTINGS_SWITCH_MARKING_VALIDATION_LENGTH.replace("{0}", name.length.toString())];
        }

        if (mappingList.find(p => p.name === name)) {
            return [false, t.SETTINGS_SWITCH_MARKING_VALIDATION_SAME_NAME];
        }

        return [true, ""];
    }

    const validateSwitchMarkingLength = (length: number): [boolean, string] => {
        if (length < 1 || length > 50) {
            return [false, t.SETTINGS_SWITCH_MARKING_VALIDATION_LENGTH_RANGE.replace("{0}", length.toString())];
        }
        return [true, ""];
    }

    const validateSwitchMarkingStep = (step: number): [boolean, string] => {
        if (step < .1 || step > 10) {
            return [false, t.SETTINGS_SWITCH_MARKING_VALIDATION_STEP_RANGE.replace("{0}", step.toString())];
        }
        return [true, ""];
    }

    const deleteMappingClick = async () => {
        const confirmed = await openConfirm({
            title: t.SETTINGS_SWITCH_MARKING_DELETE_DIALOG_TITLE,
            message: t.SETTINGS_SWITCH_MARKING_DELETE_CONFIRM_MESSAGE
        });


        if (confirmed) {
            await deleteMapping(activeMapping?.id ?? '');
        }
    }

    const setDefaultMappingClick = async () => {
        if(activeMapping) {
            await updateDefaultMapping(activeMapping.id);
        }
    }

    const renameMappingClick = async () => {
        if(!activeMapping) {
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
                    const [isValid, errorMessage] = validateSwitchMarkingName(value);
                    if (!isValid) {
                        return errorMessage;
                    }
                    return undefined;
                }
            }]
        });

        if (result) {
            await renameMapping(activeMapping?.id ?? '', result.name);
        }
    }

    const activeMappingChange = (id: string) => {
        setActiveMappingId(id);
    }

    // 菜单项
    const menuItems = [
        {
            value: "create",
            label: "Add New",
            icon: <LuPlus />,
            onClick: createMappingClick
        },
        {
            value: "delete",
            label: "Delete",
            icon: <LuTrash />,
            onClick: deleteMappingClick,
            disabled: activeMappingId === "" || activeMappingId === null,
        },
        {
            value: "rename",
            label: "Rename",
            icon: <LuPencil />,
            onClick: renameMappingClick,
            disabled: activeMappingId === "" || activeMappingId === null,
        },
        {
            value: "set_default",
            label: "Set Default",
            icon: <LuStar />,
            onClick: setDefaultMappingClick,
            disabled: activeMappingId === defaultMappingId || activeMappingId === "" || activeMappingId === null,
        }
    ];

    return (
        <>
            <Flex direction={"column"} height={"100%"} width={"1700px"} padding={"30px"} >
                <VStack width={"100%"} >
                    <Center width={"100%"} >
                        <Stack direction="row" gap={2} alignItems="center">
                            <SegmentedControl size="sm" value={activeMappingId} items={itemsConfig} onValueChange={(detail) => activeMappingChange(detail?.value ?? "")} />
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
                                onClick={() => {
                                if(markingStatus?.is_marking) {
                                    stopMarking();
                                } else {
                                    startMarking(activeMappingId);
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
                                stepMarking();
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
