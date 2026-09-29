import {
  Box,
  chakra,
  type HTMLChakraProps,
} from "@chakra-ui/react"
import * as React from "react"

import {
  deviceStageRecipe,
  hudPanelRecipe,
  hudSectionTitleRecipe,
  statusChipRecipe,
} from "@/theme"

const HudPanelRoot = chakra("section", hudPanelRecipe)
const HudSectionTitleRoot = chakra("h2", hudSectionTitleRecipe)
const StatusChipRoot = chakra("span", statusChipRecipe)
const DeviceStageRoot = chakra("section", deviceStageRecipe)

export type HudPanelProps = React.ComponentProps<typeof HudPanelRoot>

export const HudPanel = React.forwardRef<HTMLElement, HudPanelProps>(
  function HudPanel(props, ref) {
    return <HudPanelRoot ref={ref} data-hud-panel="" {...props} />
  },
)

export type HudSectionTitleProps = React.ComponentProps<
  typeof HudSectionTitleRoot
>

export const HudSectionTitle = React.forwardRef<
  HTMLHeadingElement,
  HudSectionTitleProps
>(function HudSectionTitle(props, ref) {
  return <HudSectionTitleRoot ref={ref} {...props} />
})

export type StatusChipTone =
  | "neutral"
  | "info"
  | "success"
  | "warning"
  | "danger"

export interface StatusChipProps
  extends Omit<React.ComponentProps<typeof StatusChipRoot>, "tone"> {
  tone?: StatusChipTone
  showDot?: boolean
}

export const StatusChip = React.forwardRef<HTMLSpanElement, StatusChipProps>(
  function StatusChip(
    { children, showDot = true, tone = "neutral", ...props },
    ref,
  ) {
    return (
      <StatusChipRoot ref={ref} tone={tone} data-tone={tone} {...props}>
        {showDot && (
          <Box
            as="span"
            aria-hidden="true"
            boxSize="1.5"
            flexShrink="0"
            borderRadius="full"
            bg="currentColor"
            boxShadow="0 0 8px currentColor"
          />
        )}
        {children}
      </StatusChipRoot>
    )
  },
)

export interface DeviceStageProps
  extends Omit<HTMLChakraProps<"section">, "backgroundImage"> {
  /**
   * Optional product-owned scene asset. When omitted (or if the image fails),
   * the recipe's CSS-only gradient remains a usable fallback.
   */
  sceneImage?: string
}

export const DeviceStage = React.forwardRef<HTMLElement, DeviceStageProps>(
  function DeviceStage({ sceneImage, style, ...props }, ref) {
    const sceneBackground = sceneImage
      ? `radial-gradient(circle at 50% 44%, rgba(88, 234, 244, 0.08), transparent 44%), linear-gradient(rgba(2, 8, 16, 0.12), rgba(2, 8, 16, 0.34)), url(${JSON.stringify(sceneImage)})`
      : undefined

    return (
      <DeviceStageRoot
        ref={ref}
        data-device-stage=""
        backgroundImage={sceneBackground}
        style={style}
        {...props}
      />
    )
  },
)
