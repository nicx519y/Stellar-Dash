"use client"

import { Box, Grid, HStack, Icon, Text, VStack } from "@chakra-ui/react"
import { LuCpu, LuGamepad2, LuRadioTower, LuUserRound } from "react-icons/lu"

import { useGamepadConfig } from "@/contexts/gamepad-config-context"
import { configuredTransportMode } from "@/lib/device-transport"
import { PlatformLabelMap, platformForDisplay } from "@/types/gamepad-config"
import { HudPanel, StatusChip } from "@/components/ui/hud"

interface StatusCellProps {
  icon: React.ElementType
  label: string
  primary: string
  secondary: string
  tone?: "neutral" | "info" | "success" | "warning" | "danger"
}

function StatusCell({
  icon,
  label,
  primary,
  secondary,
  tone = "neutral",
}: StatusCellProps) {
  return (
    <HudPanel
      as="section"
      height="100%"
      minWidth={0}
      px="3.5"
      py="2.5"
      aria-label={label}
    >
      <HStack height="100%" align="center" gap="3">
        <Box
          display="grid"
          placeItems="center"
          boxSize="9"
          flexShrink={0}
          color="app.accent"
          borderWidth="1px"
          borderColor="app.border"
          bg="app.control"
          boxShadow="0 0 16px rgba(88, 234, 244, 0.12)"
        >
          <Icon as={icon} boxSize="4.5" />
        </Box>
        <VStack minWidth={0} align="stretch" gap="0.5">
          <Text
            color="app.textMuted"
            fontSize="10px"
            letterSpacing="0.12em"
            textTransform="uppercase"
            lineHeight="1"
          >
            {label}
          </Text>
          <StatusChip tone={tone} width="fit-content" maxWidth="100%">
            <Text truncate fontSize="xs" fontWeight="700">
              {primary}
            </Text>
          </StatusChip>
          <Text color="app.textMuted" truncate fontSize="10px" lineHeight="1.25">
            {secondary}
          </Text>
        </VStack>
      </HStack>
    </HudPanel>
  )
}

export function StatusDock() {
  const {
    deviceConnected,
    dataIsReady,
    profileList,
    defaultProfile,
    globalConfig,
    firmwareInfo,
  } = useGamepadConfig()

  const selectedProfile =
    defaultProfile?.name ||
    profileList.items.find((profile) => profile.id === profileList.defaultId)
      ?.name ||
    "—"
  const inputMode = globalConfig.inputMode
    ? PlatformLabelMap.get(platformForDisplay(globalConfig.inputMode))?.label ??
      String(globalConfig.inputMode)
    : "—"
  const connectionMode =
    globalConfig.physicalConnectionMode ?? globalConfig.connectionMode ?? "—"
  const reportRate = globalConfig.wirelessReportRate ?? "—"
  const firmwareVersion = firmwareInfo?.firmware?.version ?? "—"
  const hardwareVersion = globalConfig.hardware?.hardwareVersion ?? "—"
  const radioVersion = globalConfig.ch585?.firmwareVersion ?? "—"

  return (
    <Grid
      className="cyber-status-dock"
      gridTemplateColumns="repeat(4, minmax(0, 1fr))"
      gap="2"
      height="100%"
      aria-label="Device status"
    >
      <StatusCell
        icon={LuRadioTower}
        label="Device"
        primary={deviceConnected ? "ONLINE" : "DISCONNECTED"}
        secondary={`${configuredTransportMode().toUpperCase()} · ${
          dataIsReady ? "READY" : "SYNCING"
        }`}
        tone={deviceConnected ? "success" : "danger"}
      />
      <StatusCell
        icon={LuUserRound}
        label="Profile"
        primary={selectedProfile}
        secondary={
          defaultProfile?.isCompetitionProfile
            ? "COMPETITION LOCK"
            : `ID ${profileList.defaultId || "—"}`
        }
        tone={defaultProfile?.isCompetitionProfile ? "warning" : "info"}
      />
      <StatusCell
        icon={LuGamepad2}
        label="Runtime Config"
        primary={inputMode}
        secondary={`${String(connectionMode)} · ${String(reportRate)}`}
        tone="info"
      />
      <StatusCell
        icon={LuCpu}
        label="Versions"
        primary={`FW ${firmwareVersion}`}
        secondary={`HW ${hardwareVersion} · RF ${radioVersion}`}
      />
    </Grid>
  )
}
