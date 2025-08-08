"use client";

import {
    Flex,
    Center,
    Text,
    Card,
    Box,
    HStack,
    Button,
    // Grid,
    // GridItem,
    Badge,
    Code,
    Table,
} from "@chakra-ui/react";
import { useEffect, useRef, useState } from 'react';
import { useButtonPerformanceMonitor, type ButtonPerformanceMonitoringData, type ButtonPerformanceData } from '@/hooks/use-button-performance-monitor';

interface ButtonPerformanceTableData {
    buttonIndex: number,
    virtualPin: number,
    currentValue: number,      // 当前ADC值
    currentDistance: number,   // 当前行程距离
    pressAdcValue: number,
    pressTriggerDistance: number,
    pressStartDistance: number,
    releaseAdcValue: number,
    releaseTriggerDistance: number,
    releaseStartDistance: number,
    isPressed: boolean,
}

export function ButtonPerformanceTest() {
    const [isMonitoring, setIsMonitoring] = useState(false);
    const [performanceData, setPerformanceData] = useState<ButtonPerformanceMonitoringData | null>(null);
    // const [lastUpdate, setLastUpdate] = useState<Date | null>(null);
    const [error, setError] = useState<string | null>(null);
    const [logs, setLogs] = useState<string[]>([]);
    const [eventHistory, setEventHistory] = useState<ButtonPerformanceTableData[]>([]);
    const [allButtonsData, setAllButtonsData] = useState<ButtonPerformanceTableData[]>([]);

    const prevButtonData = useRef<ButtonPerformanceTableData[]>([]);
    const monitorIsActive = useRef(false);

    const addLog = (message: string) => {
        const timestamp = new Date().toLocaleTimeString();
        setLogs(prev => [`[${timestamp}] ${message}`, ...prev.slice(0, 19)]); // 保留最新20条
    };

    const buttonCount = 18;

    useEffect(() => {
        for(let i = 0; i < buttonCount; i++) {
            prevButtonData.current.push({
                buttonIndex: i,
                virtualPin: i,
                currentValue: 0,
                currentDistance: 0,
                pressAdcValue: 0,
                pressTriggerDistance: 0,
                pressStartDistance: 0,
                releaseAdcValue: 0,
                releaseTriggerDistance: 0,
                releaseStartDistance: 0,
                isPressed: false,
            });
        }

        return () => {
            if(monitorIsActive.current) {
                stopMonitoring();
            }
        }
    }, []);

    const { startMonitoring, stopMonitoring } = useButtonPerformanceMonitor({
        onError: (err) => {
            setError(err.message);
            addLog(`❌ 错误: ${err.message}`);
        },
        onMonitoringStateChange: (active) => {
            setIsMonitoring(active);
            addLog(`📡 监控状态: ${active ? '已启动' : '已停止'}`);
        },
        onButtonPerformanceData: (data: ButtonPerformanceMonitoringData) => {

            console.log(data);

            setPerformanceData(data);
            // setLastUpdate(new Date());

            // 实时更新所有按键数据
            const updatedAllButtonsData = [...allButtonsData];
            
            for(let i = 0; i < data.buttonCount; i++) {
                const buttonData: ButtonPerformanceData = data.buttonData[i];
                
                // 查找或创建按键数据
                let buttonInfo = updatedAllButtonsData.find(item => item.buttonIndex === buttonData.buttonIndex);
                if (!buttonInfo) {
                    buttonInfo = {
                        buttonIndex: buttonData.buttonIndex,
                        virtualPin: buttonData.virtualPin,
                        currentValue: 0,
                        currentDistance: 0,
                        pressAdcValue: 0,
                        pressTriggerDistance: 0,
                        pressStartDistance: 0,
                        releaseAdcValue: 0,
                        releaseTriggerDistance: 0,
                        releaseStartDistance: 0,
                        isPressed: false,
                    };
                    updatedAllButtonsData.push(buttonInfo);
                }
                
                // 更新实时数据
                buttonInfo.virtualPin = buttonData.virtualPin;
                buttonInfo.currentValue = buttonData.currentValue;
                buttonInfo.currentDistance = buttonData.currentDistance;
                buttonInfo.isPressed = buttonData.isPressed;
                
                // 更新事件历史（只在状态变化时）
                const prevData = prevButtonData.current.find(item => item.buttonIndex === buttonData.buttonIndex);
                if(prevData) {
                    if(buttonData.isPressed && !prevData.isPressed) {
                        prevData.pressAdcValue = buttonData.currentValue;
                        prevData.pressTriggerDistance = buttonData.triggerDistance;
                        prevData.pressStartDistance = buttonData.pressStartValueDistance;
                        prevData.isPressed = true;
                    }else if(!buttonData.isPressed && prevData.isPressed) {
                        prevData.releaseAdcValue = buttonData.currentValue;
                        prevData.releaseTriggerDistance = buttonData.triggerDistance;
                        prevData.releaseStartDistance = buttonData.releaseStartValueDistance;
                        prevData.isPressed = false;
                    }
                }
            }

            setAllButtonsData(updatedAllButtonsData);
            setEventHistory(prevButtonData.current);
            
            // 记录日志
            data.buttonData.forEach((button: ButtonPerformanceData) => {
                const eventType = button.isPressed ? '按下' : '释放';
                addLog(`🎮 按键${button.buttonIndex} (VP:${button.virtualPin}) ${eventType}: ADC=${button.currentValue}, 行程=${button.triggerDistance.toFixed(2)}mm, 按下开始行程=${button.pressStartValueDistance.toFixed(2)}mm, 释放开始行程=${button.releaseStartValueDistance.toFixed(2)}mm`);
            });
        },
        useEventBus: true, // 使用 eventBus 监听
    });

    const handleStartMonitoring = async () => {
        setError(null);
        addLog('🚀 正在启动ADC按键性能监控...');
        try {
            await startMonitoring();
            monitorIsActive.current = true;
        } catch (err) {
            console.error('Failed to start monitoring:', err);
        }
    };

    const handleStopMonitoring = async () => {
        addLog('🛑 正在停止ADC按键性能监控...');
        try {
            await stopMonitoring();
            monitorIsActive.current = false;
        } catch (err) {
            console.error('Failed to stop monitoring:', err);
        }
    };

    const clearLogs = () => {
        setLogs([]);
    };

    const clearEventHistory = () => {
        setEventHistory([]);
    };

    // // 渲染按键状态可视化
    // const renderButtonVisual = () => {
    //     const buttons = [];
    //     const maxButtons = 18; // ADC按键数量
        
    //     for (let i = 0; i < maxButtons; i++) {
    //         // 查找当前按键的状态
    //         const buttonInfo = allButtonsData.find(button => button.buttonIndex === i);
    //         const isPressed = buttonInfo?.isPressed || false;
    //         const virtualPin = buttonInfo?.virtualPin || i;
            
    //         buttons.push(
    //             <GridItem key={i}>
    //                 <Center
    //                     w="60px"
    //                     h="60px"
    //                     rounded="md"
    //                     border="2px solid"
    //                     borderColor={isPressed ? "green.500" : "gray.300"}
    //                     bg={isPressed ? "green.500" : "gray.100"}
    //                     color={isPressed ? "white" : "gray.600"}
    //                     fontWeight="bold"
    //                     fontSize="sm"
    //                     transition="all 0.2s"
    //                     boxShadow={isPressed ? "lg" : "none"}
    //                     position="relative"
    //                 >
    //                     {i}
    //                     <Badge
    //                         position="absolute"
    //                         top="-8px"
    //                         right="-8px"
    //                         size="sm"
    //                         colorPalette="blue"
    //                         variant="solid"
    //                     >
    //                         {virtualPin}
    //                     </Badge>
    //                 </Center>
    //             </GridItem>
    //         );
    //     }

    //     return (
    //         <Grid templateColumns="repeat(9, 1fr)" gap={3} w="full">
    //             {buttons}
    //         </Grid>
    //     );
    // };

    return (
        <Flex direction="row" width="100%" height="100%" padding="18px">
            <Center flex={1} justifyContent="center" flexDirection="column">
                <Flex direction="column" gap={6} w="full" maxW="1400px">
                    {/* 页面标题 */}
                    <Text fontSize="2xl" fontWeight="bold" >
                        ADC按键性能测试
                    </Text>

                    {/* 控制面板 */}
                    <Card.Root w="full">
                        <Card.Header>
                            <Text fontSize="lg" fontWeight="semibold">
                                控制面板
                            </Text>
                        </Card.Header>
                        <Card.Body>
                            <Flex direction="column" gap={4}>
                                <HStack gap={4}>
                                    <Button
                                        onClick={handleStartMonitoring}
                                        disabled={isMonitoring}
                                        colorPalette="blue"
                                        variant="solid"
                                    >
                                        启动监控
                                    </Button>
                                    <Button
                                        onClick={handleStopMonitoring}
                                        disabled={!isMonitoring}
                                        colorPalette="red"
                                        variant="solid"
                                    >
                                        停止监控
                                    </Button>
                                    <Badge
                                        colorPalette={isMonitoring ? "green" : "gray"}
                                        variant="solid"
                                        px={3}
                                        py={1}
                                    >
                                        状态: {isMonitoring ? '监控中' : '未启动'}
                                    </Badge>
                                </HStack>
                                
                                {error && (
                                    <Box
                                        p={3}
                                        bg="red.50"
                                        rounded="md"
                                    >
                                        错误: {error}
                                    </Box>
                                )}
                            </Flex>
                        </Card.Body>
                    </Card.Root>

                    {/* 实时按键数据表格 */}
                    {allButtonsData.length > 0 && (
                        <Card.Root w="full">
                            <Card.Header>
                                <HStack justify="space-between" w="full">
                                    <Text fontSize="lg" fontWeight="semibold">
                                        实时按键数据
                                    </Text>
                                    <Badge colorPalette="blue" variant="solid">
                                        {allButtonsData.filter(btn => btn.isPressed).length} / {allButtonsData.length}
                                    </Badge>
                                </HStack>
                            </Card.Header>
                            <Card.Body>
                                <Box overflowX="auto">
                                    <Table.Root size="sm">
                                        <Table.Header>
                                            <Table.Row>
                                                <Table.ColumnHeader>按键</Table.ColumnHeader>
                                                <Table.ColumnHeader>虚拟引脚</Table.ColumnHeader>
                                                <Table.ColumnHeader>状态</Table.ColumnHeader>
                                                <Table.ColumnHeader>当前ADC值</Table.ColumnHeader>
                                            <Table.ColumnHeader>当前行程(mm)</Table.ColumnHeader>
                                            <Table.ColumnHeader>触发行程(mm)</Table.ColumnHeader>
                                                <Table.ColumnHeader>按下开始值</Table.ColumnHeader>
                                                <Table.ColumnHeader>释放开始值</Table.ColumnHeader>
                                                <Table.ColumnHeader>按下开始行程(mm)</Table.ColumnHeader>
                                                <Table.ColumnHeader>释放开始行程(mm)</Table.ColumnHeader>
                                            </Table.Row>
                                        </Table.Header>
                                        <Table.Body>
                                            {allButtonsData.map((button, index) => {
                                                const currentData = performanceData?.buttonData.find(
                                                    data => data.buttonIndex === button.buttonIndex
                                                );
                                                return (
                                                    <Table.Row key={index}>
                                                        <Table.Cell>
                                                            <Badge colorPalette="blue" variant="solid">
                                                                {button.buttonIndex}
                                                            </Badge>
                                                        </Table.Cell>
                                                        <Table.Cell>{button.virtualPin}</Table.Cell>
                                                        <Table.Cell>
                                                            <Badge 
                                                                colorPalette={button.isPressed ? "green" : "gray"} 
                                                                variant="solid"
                                                            >
                                                                {button.isPressed ? '按下' : '释放'}
                                                            </Badge>
                                                        </Table.Cell>
                                                        <Table.Cell>
                                                            <Code>{button.currentValue || '-'}</Code>
                                                        </Table.Cell>
                                                        <Table.Cell>
                                                            {button.currentDistance.toFixed(2) || '-'}
                                                        </Table.Cell>
                                                        <Table.Cell>
                                                            {currentData?.triggerDistance.toFixed(2) || '-'}
                                                        </Table.Cell>
                                                        <Table.Cell>
                                                            <Code>{currentData?.pressStartValue || '-'}</Code>
                                                        </Table.Cell>
                                                        <Table.Cell>
                                                            <Code>{currentData?.releaseStartValue || '-'}</Code>
                                                        </Table.Cell>
                                                        <Table.Cell>
                                                            {currentData?.pressStartValueDistance.toFixed(2) || '-'}
                                                        </Table.Cell>
                                                        <Table.Cell>
                                                            {currentData?.releaseStartValueDistance.toFixed(2) || '-'}
                                                        </Table.Cell>
                                                    </Table.Row>
                                                );
                                            })}
                                        </Table.Body>
                                    </Table.Root>
                                </Box>
                            </Card.Body>
                        </Card.Root>
                    )}

                    {/* 事件历史表格 */}
                    {eventHistory.length > 0 && (
                        <Card.Root w="full">
                            <Card.Header>
                                <HStack justify="space-between" w="full">
                                    <Text fontSize="lg" fontWeight="semibold">
                                        事件历史
                                    </Text>
                                    <Button
                                        onClick={clearEventHistory}
                                        size="sm"
                                        variant="outline"
                                        colorPalette="gray"
                                    >
                                        清空历史
                                    </Button>
                                </HStack>
                            </Card.Header>
                            <Card.Body>
                                <Box overflowX="auto">
                                    <Table.Root size="sm">
                                        <Table.Header>
                                            <Table.Row>
                                                <Table.ColumnHeader>按键</Table.ColumnHeader>
                                                <Table.ColumnHeader>虚拟引脚</Table.ColumnHeader>
                                                <Table.ColumnHeader>事件类型</Table.ColumnHeader>
                                                <Table.ColumnHeader>按下触发ADC值</Table.ColumnHeader>
                                                <Table.ColumnHeader>按下触发行程(mm)</Table.ColumnHeader>
                                                <Table.ColumnHeader>按下开始行程(mm)</Table.ColumnHeader>
                                                <Table.ColumnHeader>释放触发ADC值</Table.ColumnHeader>
                                                <Table.ColumnHeader>释放触发行程(mm)</Table.ColumnHeader>
                                                <Table.ColumnHeader>释放开始行程(mm)</Table.ColumnHeader>
                                            </Table.Row>
                                        </Table.Header>
                                        <Table.Body>
                                            {eventHistory.slice(0, buttonCount).map((event, index) => (
                                                <Table.Row key={index}>
                                                    <Table.Cell>
                                                        <Badge colorPalette="blue" variant="solid">
                                                            {event.buttonIndex}
                                                        </Badge>
                                                    </Table.Cell>
                                                    <Table.Cell>{event.virtualPin}</Table.Cell>
                                                    <Table.Cell>
                                                        <Badge 
                                                            colorPalette={event.isPressed ? "green" : "orange"} 
                                                            variant="solid"
                                                        >
                                                            {event.isPressed ? '按下' : '释放'}
                                                        </Badge>
                                                    </Table.Cell>
                                                    <Table.Cell>{event.pressAdcValue}</Table.Cell>
                                                    <Table.Cell>{event.pressTriggerDistance.toFixed(2)}</Table.Cell>
                                                    <Table.Cell>{event.pressStartDistance.toFixed(2)}</Table.Cell>
                                                    <Table.Cell>{event.releaseAdcValue}</Table.Cell>
                                                    <Table.Cell>{event.releaseTriggerDistance.toFixed(2)}</Table.Cell>
                                                    <Table.Cell>{event.releaseStartDistance.toFixed(2)}</Table.Cell>
                                                </Table.Row>
                                            ))}
                                        </Table.Body>
                                    </Table.Root>
                                </Box>
                            </Card.Body>
                        </Card.Root>
                    )}

                    {/* 日志面板 */}
                    <Card.Root w="full">
                        <Card.Header>
                            <HStack justify="space-between" w="full">
                                <Text fontSize="lg" fontWeight="semibold">
                                    实时日志
                                </Text>
                                <Button
                                    onClick={clearLogs}
                                    size="sm"
                                    variant="outline"
                                    colorPalette="gray"
                                >
                                    清空日志
                                </Button>
                            </HStack>
                        </Card.Header>
                        <Card.Body>
                            <Box
                                h="300px"
                                overflowY="auto"
                                p={3}
                                rounded="md"
                                border="1px solid"
                            >
                                {logs.length === 0 ? (
                                    <Center h="full">
                                        <Text >暂无日志</Text>
                                    </Center>
                                ) : (
                                    <Flex direction="column" gap={1}>
                                        {logs.map((log, index) => (
                                            <Text key={index} fontSize="sm" fontFamily="mono">
                                                {log}
                                            </Text>
                                        ))}
                                    </Flex>
                                )}
                            </Box>
                        </Card.Body>
                    </Card.Root>

                    {/* 使用说明 */}
                    <Card.Root w="full" >
                        <Card.Header>
                            <Text fontSize="lg" fontWeight="semibold" >
                                使用说明
                            </Text>
                        </Card.Header>
                        <Card.Body>
                            <Flex direction="column" gap={2}>
                                <Text fontSize="sm" >
                                    1. 点击&quot;启动监控&ldquo;开始监听ADC按键性能数据
                                </Text>
                                <Text fontSize="sm" >
                                    2. 实时显示所有18个ADC按键的当前状态和详细参数
                                </Text>
                                <Text fontSize="sm" >
                                    3. 绿色按键表示当前被按下，灰色表示未按下
                                </Text>
                                <Text fontSize="sm" >
                                    4. 实时按键数据表格显示所有按键的当前ADC值、行程距离等信息
                                </Text>
                                <Text fontSize="sm" >
                                    5. 事件历史表格记录按键状态变化时的详细参数
                                </Text>
                                <Text fontSize="sm" >
                                    6. 此模式专门用于ADC按键的性能测试和调试
                                </Text>
                                <Text fontSize="sm" >
                                    7. 支持实时WebSocket推送，每10ms发送一次所有按键数据
                                </Text>
                            </Flex>
                        </Card.Body>
                    </Card.Root>
                </Flex>
            </Center>
        </Flex>
    );
} 