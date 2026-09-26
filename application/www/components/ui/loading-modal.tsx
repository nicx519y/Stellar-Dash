import {
  Box,
  Button,
  Center,
  HStack,
  Portal,
  Spinner,
  Stack,
  Text,
} from "@chakra-ui/react";
import { LuCheck, LuSlidersHorizontal, LuUsb } from "react-icons/lu";
import { useLanguage } from "@/contexts/language-context";
import { DeviceConnectionPhase } from "@/lib/device-transport/device-command-types";
import { CONNECTION_TEXT, connectionPresentation } from "@/lib/connection-presentation";
import * as React from "react";
import type { ConfigSyncProgress } from '@/lib/device-transport/config-sync';

type LoadingVariant = "connection" | "operation";
type ConnectionState = "waiting" | "connecting";

interface LoadingModalProps {
  isOpen: boolean;
  variant?: LoadingVariant;
  connectionState?: ConnectionState;
  headerAction?: React.ReactNode;
  connectionPhase?: DeviceConnectionPhase;
  configReadProgress?: ConfigSyncProgress;
  noDeviceAction?: {
    label: string;
    onClick: () => void;
    loading?: boolean;
  };
  noDeviceTitle?: string;
  noDeviceSteps?: string[];
  noDeviceMessage?: string;
}

function ConnectionLoading({
  phase,
  progress,
}: {
  phase: DeviceConnectionPhase;
  progress: ConfigSyncProgress;
}) {
  const { currentLanguage } = useLanguage();
  const text = CONNECTION_TEXT[currentLanguage];
  const { total, completed, percent, stage, detail } = connectionPresentation(phase, progress);
  const steps = [text.connect, text.sync, text.ready];

  return (
    <>
      <Box flex={1} minHeight={0} overflowY="auto" px={{ base: 5, sm: 8 }} pt={8} pb={7}>
        <HStack justify="space-between" mb={6}>
          <Center boxSize="52px" borderRadius="16px" bg="rgba(145, 201, 116, 0.1)"
            border="1px solid rgba(145, 201, 116, 0.22)" color="#b7e59b" aria-hidden="true">
            <LuSlidersHorizontal size={24} />
          </Center>
          <Text fontSize="xs" letterSpacing="0.16em" color="whiteAlpha.600" fontWeight="600">XORA / WEBCONFIG</Text>
        </HStack>
        <Text as="h2" fontSize={{ base: "xl", sm: "2xl" }} fontWeight="600" letterSpacing="-0.025em">
          {text.title}
        </Text>
        <Text mt={2} fontSize="sm" color="whiteAlpha.600" lineHeight="1.7">{text.description}</Text>

        <Box mt={8}>
          <HStack justify="space-between" align="end" gap={4} mb={3}>
            <Box>
              <Text fontSize="xs" color="whiteAlpha.600" mb={1}>{text.progress}</Text>
              <Text fontSize="sm" fontWeight="500">{text[detail]}</Text>
            </Box>
            <Text data-testid="connection-sync-percent" fontSize="3xl" fontWeight="500"
              color="#c4eeac" lineHeight="1" fontVariantNumeric="tabular-nums">
              {percent}<Box as="span" ml={0.5} fontSize="sm" color="whiteAlpha.600">%</Box>
            </Text>
          </HStack>
          <Box role="progressbar" aria-label={text.progress} aria-valuemin={0} aria-valuemax={100}
            aria-valuenow={percent} height="8px" borderRadius="full" bg="whiteAlpha.100" overflow="hidden">
            <Box height="100%" width={`${percent}%`} borderRadius="full"
              bg="linear-gradient(90deg, #649d4a, #a7d98a 70%, #d4f4be)"
              boxShadow="0 0 16px rgba(167, 217, 138, 0.3)"
              transition="width 300ms ease" _motionReduce={{ transition: "none" }} />
          </Box>
          <Text mt={3} fontSize="xs" color="whiteAlpha.600" fontVariantNumeric="tabular-nums">
            {total > 0 ? text.readCount.replace('{completed}', String(completed)).replace('{total}', String(total)) : text.waiting}
          </Text>
        </Box>

        <HStack mt={7} align="flex-start" gap={2}>
          {steps.map((label, index) => (
            <Box key={label} flex={1} minWidth={0} borderTop="1px solid"
              borderColor={index <= stage ? 'rgba(167, 217, 138, 0.55)' : 'whiteAlpha.200'} pt={3}>
              <HStack gap={1.5} align="flex-start" color={index <= stage ? '#b7e59b' : 'whiteAlpha.500'}>
                <Center boxSize="16px" flexShrink={0} fontSize="10px" aria-hidden="true">
                  {index < stage ? <LuCheck size={13} /> : `0${index + 1}`}
                </Center>
                <Text fontSize="xs" lineHeight="16px" aria-current={index === stage ? 'step' : undefined}>{label}</Text>
              </HStack>
            </Box>
          ))}
        </HStack>
      </Box>
      <HStack flexShrink={0} px={{ base: 5, sm: 8 }} py={4} gap={2} borderTop="1px solid" borderColor="whiteAlpha.100"
        bg="rgba(0, 0, 0, 0.12)" color="whiteAlpha.600">
        <LuUsb size={14} aria-hidden="true" />
        <Text fontSize="xs">{text.keepConnected}</Text>
      </HStack>
    </>
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
  const { t } = useLanguage();
  return (
    <>
      <Box flex={1} minHeight={0} overflowY="auto" px={{ base: 5, sm: 8 }} pt={8} pb={7}>
        <HStack justify="space-between" mb={6}>
          <Center boxSize="52px" borderRadius="16px" bg="rgba(145, 201, 116, 0.1)"
            border="1px solid rgba(145, 201, 116, 0.22)" color="#b7e59b" aria-hidden="true">
            <LuUsb size={24} />
          </Center>
          <Text fontSize="xs" letterSpacing="0.16em" color="whiteAlpha.600" fontWeight="600">XORA / WEBCONFIG</Text>
        </HStack>
        <Text as="h2" fontSize={{ base: "xl", sm: "2xl" }} fontWeight="600" letterSpacing="-0.025em">
          {title ?? t.RECONNECT_MODAL_TITLE}
        </Text>
        <Stack gap={3} mt={6}>
          {(steps ?? []).map((step, index) => (
            <HStack key={step} alignItems="flex-start" gap={3}>
              <Center flexShrink={0} boxSize="22px" borderRadius="full"
                bg="rgba(145, 201, 116, 0.1)" color="#b7e59b" fontSize="xs" fontWeight="600">
                {index + 1}
              </Center>
              <Text fontSize="sm" color="whiteAlpha.700" lineHeight="1.7">{step}</Text>
            </HStack>
          ))}
          {message && (
            <Text mt={2} fontSize="xs" lineHeight="1.7" color="#d6cba9"
              borderLeft="2px solid" borderColor="rgba(214, 203, 169, 0.4)" pl={3}>
              {message}
            </Text>
          )}
        </Stack>
      </Box>
      {action && (
        <HStack flexShrink={0} justify="flex-end" px={{ base: 5, sm: 8 }} py={4}
          borderTop="1px solid" borderColor="whiteAlpha.100" bg="rgba(0, 0, 0, 0.12)">
          <Button size="sm" fontSize="sm" colorPalette="green" onClick={action.onClick} loading={action.loading}>
            {action.label}
          </Button>
        </HStack>
      )}
    </>
  );
}

/** Both states share a fixed shell; only the content fades between them. */
function DeviceConnectionCard({ state, children, title }: {
  state: ConnectionState;
  children: React.ReactNode;
  title: string;
}) {
  const shell = React.useRef<HTMLDivElement>(null);
  const body = React.useRef<HTMLDivElement>(null);
  const [displayed, setDisplayed] = React.useState(state);
  const committed = React.useRef<(() => void) | null>(null);
  const snapshot = React.useRef({ children, title });
  if (displayed === state) snapshot.current = { children, title };

  React.useLayoutEffect(() => { committed.current?.(); committed.current = null; }, [displayed]);
  React.useLayoutEffect(() => {
    const container = shell.current;
    const content = body.current;
    if (!container || !content) return;
    let cancelled = false;
    const animations: Animation[] = [];
    const animate = async (element: HTMLElement, frames: Keyframe[], duration: number) => {
      const animation = element.animate(frames, { duration, easing: 'cubic-bezier(0.22, 1, 0.36, 1)', fill: 'forwards' });
      animations.push(animation);
      await animation.finished.catch(() => {});
    };
    const changeContent = async () => {
      if (displayed !== state) {
        await new Promise<void>(resolve => { committed.current = resolve; setDisplayed(state); });
      }
    };
    const transition = async () => {
      if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) {
        await changeContent();
      } else if (displayed !== state || (content.style.opacity !== '' && content.style.opacity !== '1')) {
        container.dataset.transition = 'fade-out';
        await animate(content, [{ opacity: getComputedStyle(content).opacity }, { opacity: 0 }], 160);
        if (cancelled) return;
        await changeContent();
        if (cancelled) return;
        container.dataset.transition = 'fade-in';
        await animate(content, [{ opacity: 0 }, { opacity: 1 }], 180);
      }
      if (cancelled) return;
      animations.forEach(animation => animation.cancel());
      content.style.opacity = '';
      container.dataset.transition = 'idle';
    };
    void transition();
    return () => {
      cancelled = true;
      // Preserve opacity if a new state interrupts the fade.
      content.style.opacity = getComputedStyle(content).opacity;
      animations.forEach(animation => animation.cancel());
      committed.current?.();
      committed.current = null;
    };
    // Progress/text changes update in place; only the connection state starts a transition.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [state]);

  return (
    <Box ref={shell} data-testid="device-status-card" data-loading-variant="connection"
      data-connection-state={displayed} role="dialog" aria-modal="true" aria-label={snapshot.current.title}
      width="min(520px, calc(100vw - 32px))" height={{ base: "520px", sm: "456px" }} boxSizing="border-box"
      border="1px solid" borderColor="rgba(159, 211, 133, 0.2)" borderRadius="24px"
      bg="linear-gradient(145deg, #17221d 0%, #101619 45%, #0d1218 100%)"
      boxShadow="0 32px 100px rgba(0, 0, 0, 0.5), inset 0 1px 0 rgba(255, 255, 255, 0.05)"
      overflow="hidden" color="whiteAlpha.900">
      <Box ref={body} height="100%" display="flex" flexDirection="column"
        aria-live="polite" inert={displayed !== state ? true : undefined}>
        {snapshot.current.children}
      </Box>
    </Box>
  );
}

export function LoadingModal({
  isOpen,
  variant = "operation",
  connectionState = "connecting",
  headerAction,
  connectionPhase = DeviceConnectionPhase.OPENING,
  configReadProgress = { completed: 0, total: 0 },
  noDeviceAction,
  noDeviceTitle,
  noDeviceSteps,
  noDeviceMessage,
}: LoadingModalProps) {
  const { t, currentLanguage } = useLanguage();
  if (!isOpen) return null;

  const isDeviceStatus = variant === "connection";

  return (
    <Portal>
      <Box
        position="fixed"
        inset={0}
        zIndex={9999}
        display="flex"
        flexDirection="column"
        alignItems="center"
        justifyContent="flex-start"
        pt={isDeviceStatus ? 20 : 0}
        pb={isDeviceStatus ? "max(80px, 24dvh)" : 0}
        overflowY="auto"
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
        <Box position="relative" zIndex={1} my="auto">
          {isDeviceStatus ? (
            <DeviceConnectionCard state={connectionState} title={connectionState === 'waiting'
              ? noDeviceTitle ?? t.RECONNECT_MODAL_TITLE : CONNECTION_TEXT[currentLanguage].title}>
              {connectionState === 'connecting' ? (
                <ConnectionLoading phase={connectionPhase} progress={configReadProgress} />
              ) : <NoDeviceStatus
              action={noDeviceAction}
              title={noDeviceTitle}
              steps={noDeviceSteps}
              message={noDeviceMessage}
              />}
            </DeviceConnectionCard>
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
