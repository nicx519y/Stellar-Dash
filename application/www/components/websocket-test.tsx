import React, { useState, useRef, useEffect } from 'react';
import {
    Box,
    Button,
    Card,
    HStack,
    VStack,
    Text,
    Spinner,
    Separator,
    Textarea,
} from '@chakra-ui/react';
import { Tag } from '@/components/ui/tag';
import { showToast } from '@/components/ui/toaster';

interface WebSocketMessage {
    type: 'sent' | 'received';
    content: string;
    timestamp: Date;
}

const WebSocketTest: React.FC = () => {
    const [connectionStatus, setConnectionStatus] = useState<'disconnected' | 'connecting' | 'connected'>('disconnected');
    const [messages, setMessages] = useState<WebSocketMessage[]>([]);
    const [inputMessage, setInputMessage] = useState('');
    const [localError, setLocalError] = useState<string | null>(null);
    
    const wsRef = useRef<WebSocket | null>(null);
    const reconnectTimeoutRef = useRef<NodeJS.Timeout | null>(null);

    // 获取WebSocket服务器地址
    const getWebSocketUrl = () => {
        // 获取当前页面的host，并使用8081端口
        const host = window.location.hostname;
        return `ws://${host}:8081`;
    };

    // 连接WebSocket
    const connectWebSocket = () => {
        if (connectionStatus === 'connecting' || connectionStatus === 'connected') {
            return;
        }

        setConnectionStatus('connecting');
        setLocalError(null);
        
        const wsUrl = getWebSocketUrl();
        console.log('连接到WebSocket服务器:', wsUrl);

        try {
            const ws = new WebSocket(wsUrl);
            wsRef.current = ws;

            ws.onopen = () => {
                console.log('WebSocket连接已建立');
                setConnectionStatus('connected');
                setLocalError(null);
                showToast({
                    title: '连接成功',
                    description: 'WebSocket连接已建立',
                    type: 'success',
                });

                // 发送连接测试消息
                const testMessage = JSON.stringify({
                    type: 'connection_test',
                    timestamp: new Date().toISOString(),
                    message: 'Hello from web client!'
                });
                ws.send(testMessage);
                
                addMessage('sent', testMessage);
            };

            ws.onmessage = (event) => {
                console.log('收到WebSocket消息:', event.data);
                addMessage('received', event.data);
            };

            ws.onclose = (event) => {
                console.log('WebSocket连接已关闭:', event.code, event.reason);
                setConnectionStatus('disconnected');
                wsRef.current = null;
                
                if (event.code !== 1000 && event.code !== 1001) {
                    setLocalError(`连接关闭: ${event.reason || '未知原因'} (代码: ${event.code})`);
                    showToast({
                        title: '连接断开',
                        description: `WebSocket连接已断开 (${event.code})`,
                        type: 'error',
                    });
                    
                    // 自动重连
                    scheduleReconnect();
                }
            };

            ws.onerror = (error) => {
                console.error('WebSocket错误:', error);
                setConnectionStatus('disconnected');
                setLocalError('WebSocket连接错误');
                showToast({
                    title: '连接错误',
                    description: 'WebSocket连接发生错误',
                    type: 'error',
                });
            };

        } catch (error) {
            console.error('创建WebSocket连接失败:', error);
            setConnectionStatus('disconnected');
            setLocalError(`连接失败: ${error instanceof Error ? error.message : '未知错误'}`);
        }
    };

    // 断开WebSocket连接
    const disconnectWebSocket = () => {
        if (reconnectTimeoutRef.current) {
            clearTimeout(reconnectTimeoutRef.current);
            reconnectTimeoutRef.current = null;
        }

        if (wsRef.current) {
            wsRef.current.close(1000, '用户主动断开');
            wsRef.current = null;
        }
        setConnectionStatus('disconnected');
    };

    // 计划重连
    const scheduleReconnect = () => {
        if (reconnectTimeoutRef.current) {
            clearTimeout(reconnectTimeoutRef.current);
        }
        
        reconnectTimeoutRef.current = setTimeout(() => {
            console.log('尝试自动重连...');
            connectWebSocket();
        }, 3000);
    };

    // 发送消息
    const sendMessage = () => {
        if (!wsRef.current || connectionStatus !== 'connected' || !inputMessage.trim()) {
            return;
        }

        const message = inputMessage.trim();
        try {
            wsRef.current.send(message);
            addMessage('sent', message);
            setInputMessage('');
            showToast({
                title: '消息已发送',
                type: 'success',
            });
        } catch (error) {
            console.error('发送消息失败:', error);
            showToast({
                title: '发送失败',
                description: '消息发送失败',
                type: 'error',
            });
        }
    };

    // 添加消息到列表
    const addMessage = (type: 'sent' | 'received', content: string) => {
        const newMessage: WebSocketMessage = {
            type,
            content,
            timestamp: new Date()
        };
        setMessages(prev => [...prev, newMessage]);
    };

    // 清空消息
    const clearMessages = () => {
        setMessages([]);
    };

    // 清理效果
    useEffect(() => {
        return () => {
            disconnectWebSocket();
        };
    }, []);

    // 处理按键事件
    const handleKeyPress = (e: React.KeyboardEvent) => {
        if (e.key === 'Enter' && !e.shiftKey) {
            e.preventDefault();
            sendMessage();
        }
    };

    // 格式化时间戳
    const formatTimestamp = (timestamp: Date) => {
        return timestamp.toLocaleTimeString();
    };

    // 获取状态标签
    const getStatusTag = () => {
        switch (connectionStatus) {
            case 'connected':
                return <Tag colorPalette="green">已连接</Tag>;
            case 'connecting':
                return <Tag colorPalette="blue">连接中</Tag>;
            case 'disconnected':
                return <Tag colorPalette="red">未连接</Tag>;
            default:
                return <Tag colorPalette="gray">未知</Tag>;
        }
    };

    return (
        <Box p={6} maxW="1200px" mx="auto">
            <VStack gap={6} align="stretch">
                {/* 标题 */}
                <Box>
                    <Text fontSize="2xl" fontWeight="bold">WebSocket 连接测试</Text>
                    <Text color="gray.600" mt={2}>
                        测试与STM32设备的WebSocket实时通信连接 (端口: 8081)
                    </Text>
                </Box>

                {/* 连接状态和控制 */}
                <Card.Root>
                    <Card.Body p={4}>
                        <VStack gap={4} align="stretch">
                            <HStack justify="space-between">
                                <HStack gap={4}>
                                    <Text fontWeight="medium">连接状态:</Text>
                                    {getStatusTag()}
                                    {connectionStatus === 'connecting' && <Spinner size="sm" />}
                                </HStack>
                                <HStack gap={2}>
                                    <Button
                                        onClick={connectWebSocket}
                                        disabled={connectionStatus === 'connecting' || connectionStatus === 'connected'}
                                        colorPalette="blue"
                                        size="sm"
                                    >
                                        连接
                                    </Button>
                                    <Button
                                        onClick={disconnectWebSocket}
                                        disabled={connectionStatus === 'disconnected'}
                                        colorPalette="red"
                                        size="sm"
                                    >
                                        断开
                                    </Button>
                                </HStack>
                            </HStack>

                            {localError && (
                                <Box p={3} bg="red.50" borderRadius="md" borderLeft="4px solid" borderColor="red.500">
                                    <Text color="red.700" fontSize="sm">{localError}</Text>
                                </Box>
                            )}

                            <Text fontSize="sm" color="gray.600">
                                WebSocket URL: {getWebSocketUrl()}
                            </Text>
                        </VStack>
                    </Card.Body>
                </Card.Root>

                {/* 消息发送 */}
                <Card.Root>
                    <Card.Body p={4}>
                        <VStack gap={4} align="stretch">
                            <Text fontWeight="medium">发送消息</Text>
                            <HStack gap={2}>
                                <Textarea
                                    value={inputMessage}
                                    onChange={(e) => setInputMessage(e.target.value)}
                                    onKeyDown={handleKeyPress}
                                    placeholder="输入要发送的消息..."
                                    disabled={connectionStatus !== 'connected'}
                                    resize="none"
                                    rows={2}
                                    flex={1}
                                />
                                <Button
                                    onClick={sendMessage}
                                    disabled={connectionStatus !== 'connected' || !inputMessage.trim()}
                                    colorPalette="blue"
                                >
                                    发送
                                </Button>
                            </HStack>
                            <Text fontSize="xs" color="gray.500">
                                按 Enter 发送，Shift+Enter 换行
                            </Text>
                        </VStack>
                    </Card.Body>
                </Card.Root>

                {/* 消息历史 */}
                <Card.Root>
                    <Card.Body p={4}>
                        <VStack gap={4} align="stretch">
                            <HStack justify="space-between">
                                <Text fontWeight="medium">消息历史 ({messages.length})</Text>
                                <Button
                                    onClick={clearMessages}
                                    size="sm"
                                    variant="outline"
                                    disabled={messages.length === 0}
                                >
                                    清空
                                </Button>
                            </HStack>

                            <Box
                                maxH="400px"
                                overflowY="auto"
                                border="1px solid"
                                borderColor="gray.200"
                                borderRadius="md"
                                p={3}
                            >
                                {messages.length === 0 ? (
                                    <Text color="gray.500" fontSize="sm" textAlign="center">
                                        暂无消息
                                    </Text>
                                ) : (
                                    <VStack gap={2} align="stretch">
                                        {messages.map((message, index) => (
                                            <Box key={index}>
                                                <HStack gap={2} mb={1}>
                                                    <Tag
                                                        size="sm"
                                                        colorPalette={message.type === 'sent' ? 'blue' : 'green'}
                                                    >
                                                        {message.type === 'sent' ? '发送' : '接收'}
                                                    </Tag>
                                                    <Text fontSize="xs" color="gray.500">
                                                        {formatTimestamp(message.timestamp)}
                                                    </Text>
                                                </HStack>
                                                <Box
                                                    p={2}
                                                    bg={message.type === 'sent' ? 'blue.50' : 'green.50'}
                                                    borderRadius="md"
                                                    fontSize="sm"
                                                >
                                                    <Text fontFamily="mono">{message.content}</Text>
                                                </Box>
                                                {index < messages.length - 1 && <Separator mt={2} />}
                                            </Box>
                                        ))}
                                    </VStack>
                                )}
                            </Box>
                        </VStack>
                    </Card.Body>
                </Card.Root>

                {/* 使用说明 */}
                <Card.Root>
                    <Card.Body p={4}>
                        <VStack gap={3} align="start">
                            <Text fontWeight="medium">使用说明</Text>
                            <VStack gap={1} align="start" fontSize="sm" color="gray.600">
                                <Text>• 点击连接按钮建立WebSocket连接到STM32设备</Text>
                                <Text>• 连接成功后可以发送和接收实时消息</Text>
                                <Text>• 支持自动重连，连接断开后3秒自动尝试重连</Text>
                                <Text>• 可以发送JSON格式的结构化数据或纯文本消息</Text>
                                <Text>• 所有消息都会显示在消息历史中，包含时间戳</Text>
                            </VStack>
                        </VStack>
                    </Card.Body>
                </Card.Root>
            </VStack>
        </Box>
    );
};

export default WebSocketTest; 