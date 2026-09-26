"use client";

import { useEffect, useMemo, useState } from "react";
import { 
  Box, Card, Heading, HStack, VStack,
  Button, Text, Badge, Spinner
} from "@chakra-ui/react";
import { showToast } from "@/components/ui/toaster";
import { DeviceTransportState } from "@/lib/device-transport";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";

export function ViewLogsContent() {
  const [logs, setLogs] = useState<string[]>([]);
  const [loading, setLoading] = useState<boolean>(false);

  const { deviceState, fetchDeviceLogsList } = useGamepadConfig();

  const stateLabel = useMemo(() => {
    switch (deviceState) {
      case DeviceTransportState.CONNECTED: return "已连接";
      case DeviceTransportState.CONNECTING: return "连接中";
      case DeviceTransportState.ERROR: return "错误";
      case DeviceTransportState.DISCONNECTED: return "未连接";
      default: return "未知";
    }
  }, [deviceState]);

  const fetchLogs = async () => {
    setLoading(true);
    try {
      const items = await fetchDeviceLogsList();
      setLogs(items);
    } catch (error: unknown) {
      const message = error instanceof Error ? error.message : String(error);
      showToast({ title: "获取日志失败", description: message, type: "error", duration: 3000 });
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    // Fetch once the authenticated device session is ready.
    const fetchWhenConnected = async () => {
      if (deviceState === DeviceTransportState.CONNECTED) {
        await fetchLogs();
      }
    };
    fetchWhenConnected();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [deviceState]);


  return (
    <Box p="18px">
      <Card.Root>
        <Card.Header>
          <HStack justify="space-between">
            <Heading size="md">设备日志</Heading>
            <Badge colorPalette={deviceState === DeviceTransportState.CONNECTED ? "green" : deviceState === DeviceTransportState.CONNECTING ? "yellow" : deviceState === DeviceTransportState.ERROR ? "red" : "gray"}>
              {stateLabel}
            </Badge>
          </HStack>
        </Card.Header>
        <Card.Body>
          <VStack alignItems="stretch" gap={4}>
            <HStack>
              <Button colorPalette="blue" onClick={fetchLogs} disabled={loading}>
                刷新
              </Button>
            </HStack>

            <Box
              borderWidth="1px"
              borderRadius="md"
              p={3}
              bg="black"
              color="green.300"
              fontFamily="mono"
              width="820px"
              height="520px"
              overflowY="auto"
            >
              {loading && (
                <HStack>
                  <Spinner size="sm" color="green.400" />
                  <Text>加载中...</Text>
                </HStack>
              )}
              {!loading && logs.length === 0 && (
                <Text color="gray.400">暂无日志</Text>
              )}
              {!loading && logs.length > 0 && (
                <VStack alignItems="stretch" gap={1}>
                  {logs.map((line, idx) => (
                    <Text key={idx} whiteSpace="pre-wrap">
                      {line}
                    </Text>
                  ))}
                </VStack>
              )}
            </Box>
          </VStack>
        </Card.Body>
      </Card.Root>
    </Box>
  );
}

export default ViewLogsContent;
