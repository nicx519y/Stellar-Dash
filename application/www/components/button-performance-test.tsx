'use client';

import { useState, useEffect } from 'react';
import { Box, Button, VStack, HStack, Text } from '@chakra-ui/react';
import HitboxPerformanceTest, { ButtonState } from './hitbox/hitbox-performance-test';

export default function ButtonPerformanceTest() {
    const [buttonStates, setButtonStates] = useState<ButtonState[]>([]);
    const [isRunning, setIsRunning] = useState(false);

    // 模拟按钮状态变化
    const simulateButtonPress = () => {
        const newStates: ButtonState[] = [];
        
        // 随机选择1-3个按钮按下
        const numPressed = Math.floor(Math.random() * 3) + 1;
        const pressedIndices = new Set<number>();
        
        while (pressedIndices.size < numPressed) {
            pressedIndices.add(Math.floor(Math.random() * 24)); // 24个按钮
        }
        
        // 生成所有按钮的状态
        for (let i = 0; i < 24; i++) {
            newStates.push({
                buttonIndex: i,
                isPressed: pressedIndices.has(i)
            });
        }
        
        setButtonStates(newStates);
    };

    // 清除所有按钮状态
    const clearButtonStates = () => {
        setButtonStates([]);
    };

    // 自动模拟按钮状态变化
    useEffect(() => {
        if (!isRunning) return;

        const interval = setInterval(() => {
            simulateButtonPress();
        }, 1000); // 每秒更新一次

        return () => clearInterval(interval);
    }, [isRunning]);

    return (
        <Box p={4}>
            <VStack gap={4} align="stretch">
                <Text fontSize="xl" fontWeight="bold">
                    按钮性能测试
                </Text>
                
                <HStack gap={4}>
                    <Button 
                        colorScheme={isRunning ? "red" : "green"}
                        onClick={() => setIsRunning(!isRunning)}
                    >
                        {isRunning ? "停止模拟" : "开始模拟"}
                    </Button>
                    
                    <Button 
                        colorScheme="blue"
                        onClick={simulateButtonPress}
                        disabled={isRunning}
                    >
                        手动触发
                    </Button>
                    
                    <Button 
                        colorScheme="gray"
                        onClick={clearButtonStates}
                    >
                        清除状态
                    </Button>
                </HStack>

                <Text fontSize="sm" color="gray.600">
                    当前按下按钮: {buttonStates.filter(s => s.isPressed).map(s => s.buttonIndex + 1).join(', ') || '无'}
                </Text>

                <Box 
                    border="1px solid" 
                    borderColor="gray.200" 
                    borderRadius="md" 
                    p={4}
                    width="100%"
                    maxWidth="900px"
                >
                    <HitboxPerformanceTest
                        buttonStates={buttonStates}
                        hasText={true}
                        interactiveIds={[]}
                        containerWidth={900}
                    />
                </Box>
            </VStack>
        </Box>
    );
} 