"use client";

import {
    Flex,
    Center,
    Text,
    Card,
    Box,
    HStack,
    Button,
    Grid,
    GridItem,
    Badge,
    Code,
} from "@chakra-ui/react";
import { useState } from 'react';
import { useButtonMonitor } from '@/hooks/use-button-monitor';
import { type ButtonStateBinaryData, triggerMaskToBinaryString, triggerMaskToButtonArray } from '@/lib/button-binary-parser';

export function ButtonMonitorTest() {
    const [isMonitoring, setIsMonitoring] = useState(false);
    const [buttonStates, setButtonStates] = useState<ButtonStateBinaryData | null>(null);
    const [lastUpdate, setLastUpdate] = useState<Date | null>(null);
    const [error, setError] = useState<string | null>(null);
    const [logs, setLogs] = useState<string[]>([]);

    const addLog = (message: string) => {
        const timestamp = new Date().toLocaleTimeString();
        setLogs(prev => [`[${timestamp}] ${message}`, ...prev.slice(0, 19)]); // 保留最新20条
    };

    const { startMonitoring, stopMonitoring } = useButtonMonitor({
        onError: (err) => {
            setError(err.message);
            addLog(`❌ 错误: ${err.message}`);
        },
        onMonitoringStateChange: (active) => {
            setIsMonitoring(active);
            addLog(`📡 监控状态: ${active ? '已启动' : '已停止'}`);
        },
        onButtonStatesChange: (states) => {
            setButtonStates(states);
            setLastUpdate(new Date());
            if (states?.triggerMask > 0) {
                const triggeredButtons = triggerMaskToButtonArray(states.triggerMask, states.totalButtons);
                const binaryString = triggerMaskToBinaryString(states.triggerMask);
                addLog(`🎮 按键触发: 掩码=0x${states.triggerMask.toString(16).toUpperCase()}, 按键=${triggeredButtons.join(',')}, 二进制=${binaryString}`);
            }
        },
        useEventBus: true, // 使用 eventBus 监听
    });

    const handleStartMonitoring = async () => {
        setError(null);
        addLog('🚀 正在启动按键监控...');
        try {
            await startMonitoring();
        } catch (err) {
            console.error('Failed to start monitoring:', err);
        }
    };

    const handleStopMonitoring = async () => {
        addLog('🛑 正在停止按键监控...');
        try {
            await stopMonitoring();
        } catch (err) {
            console.error('Failed to stop monitoring:', err);
        }
    };

    const clearLogs = () => {
        setLogs([]);
    };

    // 渲染按键状态可视化
    const renderButtonVisual = () => {
        if (!buttonStates) return null;

        const buttons = [];
        for (let i = 0; i < buttonStates.totalButtons; i++) {
            const isPressed = (buttonStates.triggerMask & (1 << i)) !== 0;
            buttons.push(
                <GridItem key={i}>
                    <Center
                        w="60px"
                        h="60px"
                        rounded="md"
                        border="2px solid"
                        borderColor={isPressed ? "green.500" : "gray.300"}
                        bg={isPressed ? "green.500" : "gray.100"}
                        color={isPressed ? "white" : "gray.600"}
                        fontWeight="bold"
                        fontSize="sm"
                        transition="all 0.2s"
                        boxShadow={isPressed ? "lg" : "none"}
                    >
                        {i}
                    </Center>
                </GridItem>
            );
        }

        return (
            <Grid templateColumns="repeat(8, 1fr)" gap={3} w="full">
                {buttons}
            </Grid>
        );
    };

    return (
        <Flex direction="row" width="100%" height="100%" padding="18px">
            <Center flex={1} justifyContent="center" flexDirection="column">
                <Flex direction="column" gap={6} w="full" maxW="1200px">
                    {/* 页面标题 */}
                    <Text fontSize="2xl" fontWeight="bold" >
                        按键监控测试
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

                    {/* 按键状态可视化 */}
                    {buttonStates && (
                        <Card.Root w="full">
                            <Card.Header>
                                <HStack justify="space-between" w="full">
                                    <Text fontSize="lg" fontWeight="semibold">
                                        按键状态可视化
                                    </Text>
                                    {lastUpdate && (
                                        <Text fontSize="sm" color="gray.500">
                                            最后更新: {lastUpdate.toLocaleTimeString()}
                                        </Text>
                                    )}
                                </HStack>
                            </Card.Header>
                            <Card.Body>
                                <Flex direction="column" gap={4}>
                                    <Box p={4} bg="gray.50" rounded="lg">
                                        {renderButtonVisual()}
                                    </Box>
                                    
                                    <Grid templateColumns="repeat(2, 1fr)" gap={4}>
                                        <Box>
                                            <Text fontWeight="medium" fontSize="sm">
                                                触发掩码: <Code>{buttonStates.triggerMask}</Code>
                                            </Text>
                                        </Box>
                                        <Box>
                                            <Text fontWeight="medium" fontSize="sm">
                                                二进制: <Code>{buttonStates ? triggerMaskToBinaryString(buttonStates.triggerMask) : 'N/A'}</Code>
                                            </Text>
                                        </Box>
                                        <Box>
                                            <Text fontWeight="medium" fontSize="sm">
                                                总按键数: <Code>{buttonStates.totalButtons}</Code>
                                            </Text>
                                        </Box>
                                        <Box>
                                            <Text fontWeight="medium" fontSize="sm">
                                                活跃状态: <Code>{buttonStates.isActive ? '是' : '否'}</Code>
                                            </Text>
                                        </Box>
                                    </Grid>
                                </Flex>
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
                                    1. 点击&quot;启动监控&ldquo;开始监听按键状态变化
                                </Text>
                                <Text fontSize="sm" >
                                    2. 按下设备上的任意按键，应该能看到实时的状态更新
                                </Text>
                                <Text fontSize="sm" >
                                    3. 绿色的按键表示当前被按下，灰色表示未按下
                                </Text>
                                <Text fontSize="sm" >
                                    4. 使用WebHID事件推送模式，无需轮询即可实时获取状态
                                </Text>
                                <Text fontSize="sm" >
                                    5. 日志面板显示详细的操作记录和状态变化
                                </Text>
                            </Flex>
                        </Card.Body>
                    </Card.Root>
                </Flex>
            </Center>
        </Flex>
    );
} 