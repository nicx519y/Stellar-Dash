import { 
  Box,
  Portal,
  Text,
  Button,
  VStack,
} from "@chakra-ui/react";
import * as React from "react";

interface ReconnectModalProps {
  isOpen: boolean;
  onReconnect: () => void;
  isLoading?: boolean;
}

export function ReconnectModal({ isOpen, onReconnect, isLoading = false }: ReconnectModalProps) {
  if (!isOpen) return null;

  return (
    <Portal>
      <Box
        position="fixed"
        top={0}
        left={0}
        right={0}
        bottom={0}
        zIndex={9999}
        display="flex"
        alignItems="center"
        justifyContent="center"
      >
        <Box
          position="absolute"
          top={0}
          left={0}
          right={0}
          bottom={0}
          bg="blackAlpha.100"
          backdropFilter="blur(4px)"
        />
        <Box
          position="relative"
          zIndex={1}
          bg="white"
          borderRadius="lg"
          p={8}
          boxShadow="xl"
          maxW="400px"
          w="90%"
        >
          <VStack gap={6}>
            <Text fontSize="lg" fontWeight="bold" textAlign="center">
              连接已断开
            </Text>
            <Text fontSize="md" textAlign="center" color="gray.600">
              与服务器的连接已断开，请点击下方按钮重新连接。
            </Text>
            <Button
              colorScheme="blue"
              size="lg"
              onClick={onReconnect}
              loading={isLoading}
              w="full"
            >
              {isLoading ? "连接中..." : "重新连接"}
            </Button>
          </VStack>
        </Box>
      </Box>
    </Portal>
  );
} 