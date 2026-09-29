"use client"

import { Box, Heading, Icon, Text, VStack } from "@chakra-ui/react"
import { LuMonitorUp } from "react-icons/lu"

export function ViewportGuard() {
  return (
    <Box
      className="cyber-viewport-guard"
      position="fixed"
      inset="0"
      zIndex="max"
      alignItems="center"
      justifyContent="center"
      bg="#020810"
      color="#eaf7fb"
      p="8"
      role="alert"
      aria-live="polite"
    >
      <VStack
        maxWidth="520px"
        borderWidth="1px"
        borderColor="#2a6a80"
        bg="rgba(3, 16, 25, 0.96)"
        boxShadow="0 0 32px rgba(88, 234, 244, 0.18)"
        px="10"
        py="9"
        textAlign="center"
        gap="4"
      >
        <Icon as={LuMonitorUp} boxSize="10" color="#58eaf4" />
        <Heading size="lg" color="#eaf7fb">
          Increase the workspace size
        </Heading>
        <Text color="#9cb3c0">
          Cyber workspace requires at least 1440 × 900 CSS pixels at 100%
          browser zoom.
        </Text>
      </VStack>
    </Box>
  )
}

