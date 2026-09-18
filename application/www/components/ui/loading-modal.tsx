import {
  Box,
  Button,
  Card,
  Center,
  HStack,
  Portal,
  Spinner,
  Stack,
  Text,
} from "@chakra-ui/react";
import { Alert } from "@/components/ui/alert";
import { keyframes } from "@emotion/react";
import * as React from "react";

type LoadingVariant = "connection" | "no-device" | "operation";

interface LoadingModalProps {
  isOpen: boolean;
  variant?: LoadingVariant;
  headerAction?: React.ReactNode;
  noDeviceAction?: {
    label: string;
    onClick: () => void;
    loading?: boolean;
  };
  noDeviceTitle?: string;
  noDeviceSteps?: string[];
  noDeviceMessage?: string;
}

const CONNECTOR_DOT_COUNT = 14;

const connectorDotAnimations = Array.from({ length: CONNECTOR_DOT_COUNT }, (_, index) => {
  const waitingUntil = 10 + index * 5;
  const activeFrom = waitingUntil + 1;

  return keyframes`
    0%, ${waitingUntil}% {
      opacity: 0.18;
      box-shadow: 0 0 0 rgba(45, 212, 191, 0);
    }
    ${activeFrom}%, 84% {
      opacity: 1;
      box-shadow: 0 0 12px rgba(45, 212, 191, 0.9);
    }
    86%, 100% {
      opacity: 0.18;
      box-shadow: 0 0 0 rgba(45, 212, 191, 0);
    }
  `;
});

function HitboxDeviceIcon() {
  return (
    <Box
      width={{ base: "66px", sm: "82px" }}
    >
      <svg viewBox="0 0 104 72" width="100%" style={{ display: "block", height: "auto" }} role="img" aria-label="HBox device">
        <rect x="5" y="10" width="94" height="52" rx="12" fill="#0b1a21" stroke="currentColor" strokeWidth="2.5" />
        <path d="M17 62h14m42 0h14" stroke="currentColor" strokeWidth="3" strokeLinecap="round" />
        <circle cx="28" cy="35" r="5.5" fill="#132a33" stroke="currentColor" strokeWidth="2" />
        <circle cx="40" cy="24" r="5.5" fill="#132a33" stroke="currentColor" strokeWidth="2" />
        <circle cx="42" cy="45" r="5.5" fill="#132a33" stroke="currentColor" strokeWidth="2" />
        <circle cx="61" cy="28" r="5" fill="#132a33" stroke="currentColor" strokeWidth="2" />
        <circle cx="73" cy="24" r="5" fill="#132a33" stroke="currentColor" strokeWidth="2" />
        <circle cx="85" cy="29" r="5" fill="#132a33" stroke="currentColor" strokeWidth="2" />
        <circle cx="64" cy="42" r="5" fill="#132a33" stroke="currentColor" strokeWidth="2" />
        <circle cx="76" cy="39" r="5" fill="#132a33" stroke="currentColor" strokeWidth="2" />
        <circle cx="87" cy="43" r="5" fill="#132a33" stroke="currentColor" strokeWidth="2" />
      </svg>
    </Box>
  );
}

function WebPageIcon() {
  return (
    <Box
      width={{ base: "66px", sm: "82px" }}
    >
      <svg viewBox="0 0 104 72" width="100%" style={{ display: "block", height: "auto" }} role="img" aria-label="WebConfig page">
        <rect x="7" y="8" width="90" height="56" rx="9" fill="#0b1a21" stroke="currentColor" strokeWidth="2.5" />
        <path d="M8 24h88" stroke="currentColor" strokeWidth="2.5" />
        <circle cx="18" cy="16" r="2.5" fill="currentColor" />
        <circle cx="27" cy="16" r="2.5" fill="currentColor" opacity="0.7" />
        <circle cx="36" cy="16" r="2.5" fill="currentColor" opacity="0.45" />
        <rect x="18" y="34" width="27" height="20" rx="4" fill="#132a33" stroke="currentColor" strokeWidth="2" />
        <path d="M55 36h28M55 44h22M55 52h16" stroke="currentColor" strokeWidth="3" strokeLinecap="round" opacity="0.85" />
      </svg>
    </Box>
  );
}

function ConnectionEndpoint({
  color,
  children,
}: {
  color: string;
  children: React.ReactNode;
}) {
  return (
    <Box
      flex="0 0 auto"
      color={color}
      textAlign="center"
      minWidth={{ base: "72px", sm: "94px" }}
    >
      <Center minHeight={{ base: "52px", sm: "64px" }}>{children}</Center>
    </Box>
  );
}

function DeviceStatusFrame({
  variant,
  label,
  children,
}: {
  variant: "connection" | "no-device";
  label: string;
  children: React.ReactNode;
}) {
  return (
    <Box
      data-testid="device-status-card"
      data-loading-variant={variant}
      role="status"
      aria-live="polite"
      aria-label={label}
      width={{ base: "calc(100vw - 32px)", sm: "min(560px, calc(100vw - 48px))" }}
      px={{ base: 4, sm: 7 }}
      py={{ base: 5, sm: 7 }}
      border="1px solid"
      borderColor="rgba(94, 234, 212, 0.22)"
      borderRadius={{ base: "18px", sm: "22px" }}
      bg="rgba(5, 14, 20, 0.82)"
      boxShadow="0 24px 80px rgba(0, 0, 0, 0.48), inset 0 1px 0 rgba(255, 255, 255, 0.04)"
    >
      {children}
    </Box>
  );
}

function ConnectionLoading() {
  return (
    <DeviceStatusFrame
      variant="connection"
      label="Connecting HBox device to WebConfig"
    >
      <Box display="flex" alignItems="center" justifyContent="center" width="100%">
        <ConnectionEndpoint color="teal.300">
          <HitboxDeviceIcon />
        </ConnectionEndpoint>

        <Box
          aria-hidden="true"
          flex="1 1 auto"
          minWidth={{ base: "70px", sm: "150px" }}
          mx={{ base: 1, sm: 4 }}
          display="flex"
          alignItems="center"
          justifyContent="space-between"
        >
          {Array.from({ length: CONNECTOR_DOT_COUNT }, (_, index) => (
            <Box
              key={index}
              data-connector-dot="true"
              width="5px"
              height="5px"
              borderRadius="full"
              bg="teal.300"
              opacity={0.18}
              animation={`${connectorDotAnimations[index]} 1.85s ease-in-out infinite`}
              css={{
                "@media (prefers-reduced-motion: reduce)": {
                  animationDuration: "3.7s",
                },
              }}
            />
          ))}
        </Box>

        <ConnectionEndpoint color="cyan.300">
          <WebPageIcon />
        </ConnectionEndpoint>
      </Box>
    </DeviceStatusFrame>
  );
}

function NoDeviceStatus({
  action,
  title,
  steps,
  message,
}: {
  action?: LoadingModalProps["noDeviceAction"];
  title?: string;
  steps?: string[];
  message?: string;
}) {
  return (
    <Card.Root
      data-testid="device-status-card"
      data-loading-variant="no-device"
      role="dialog"
      aria-modal="true"
      aria-label={title ?? "Device not connected"}
      width={{ base: "calc(100vw - 32px)", sm: "min(600px, calc(100vw - 48px))" }}
      borderRadius={{ base: "18px", sm: "22px" }}
      border="1px solid"
      borderColor="whiteAlpha.200"
      bg="rgba(13, 18, 24, 0.96)"
      boxShadow="0 28px 90px rgba(0, 0, 0, 0.55)"
      overflow="hidden"
    >
      <Card.Header pb={3}>
        <Card.Title fontSize={{ base: "xl", sm: "2xl" }}>
          {title ?? "Device Not Connected"}
        </Card.Title>
      </Card.Header>

      <Card.Body pt={0}>
        <Stack gap={3}>
          {(steps ?? []).map((step, index) => (
            <HStack key={step} alignItems="flex-start" gap={3}>
              <Center
                flex="0 0 auto"
                width="28px"
                height="28px"
                borderRadius="full"
                bg="green.500"
                color="white"
                fontSize="sm"
                fontWeight="700"
              >
                {index + 1}
              </Center>
              <Text pt="3px" color="whiteAlpha.900" lineHeight="1.5">
                {step}
              </Text>
            </HStack>
          ))}

          {message && (
            <Alert colorPalette="yellow" mt={2}>
              {message}
            </Alert>
          )}
        </Stack>
      </Card.Body>

      {action && (
        <Card.Footer
          justifyContent="flex-end"
          borderTop="1px solid"
          borderColor="whiteAlpha.100"
          pt={4}
        >
          <Button
            colorPalette="green"
            onClick={action.onClick}
            loading={action.loading}
          >
            {action.label}
          </Button>
        </Card.Footer>
      )}
    </Card.Root>
  );
}

export function LoadingModal({
  isOpen,
  variant = "operation",
  headerAction,
  noDeviceAction,
  noDeviceTitle,
  noDeviceSteps,
  noDeviceMessage,
}: LoadingModalProps) {
  if (!isOpen) return null;

  const isConnection = variant === "connection";
  const isDeviceStatus = isConnection || variant === "no-device";

  return (
    <Portal>
      <Box
        position="fixed"
        inset={0}
        zIndex={9999}
        display="flex"
        alignItems={isDeviceStatus ? "flex-start" : "center"}
        justifyContent="center"
        pt={isDeviceStatus ? 16 : 0}
        pointerEvents="auto"
        isolation="isolate"
      >
        <Box
          data-testid="global-blur-backdrop"
          position="absolute"
          inset={0}
          zIndex={0}
          bg={isDeviceStatus ? "rgba(2, 8, 12, 0.42)" : "blackAlpha.100"}
          backdropFilter={isDeviceStatus ? "blur(10px) saturate(0.72)" : "blur(4px)"}
        />
        <Box position="relative" zIndex={1}>
          {isConnection ? (
            <ConnectionLoading />
          ) : variant === "no-device" ? (
            <NoDeviceStatus
              action={noDeviceAction}
              title={noDeviceTitle}
              steps={noDeviceSteps}
              message={noDeviceMessage}
            />
          ) : (
            <Center p={8} data-loading-variant="operation">
              <Spinner color="green.500" size="xl" />
            </Center>
          )}
        </Box>
        {isDeviceStatus && headerAction && (
          <Box position="absolute" top={2} right={4} zIndex={2}>
            {headerAction}
          </Box>
        )}
      </Box>
    </Portal>
  );
}
