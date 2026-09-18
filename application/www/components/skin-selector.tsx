"use client"

import {
  Box,
  Button,
  type HTMLChakraProps,
  Text,
} from "@chakra-ui/react"

import {
  type VisualSkin,
  useSkin,
} from "@/contexts/skin-context"

export interface SkinSelectorProps
  extends Omit<HTMLChakraProps<"div">, "children"> {
  ariaLabel?: string
  classicLabel?: string
  cyberLabel?: string
}

const skinOptions: ReadonlyArray<{
  value: VisualSkin
  shortLabel: string
}> = [
  { value: "classic", shortLabel: "C" },
  { value: "cyber", shortLabel: "H" },
]

export function SkinSelector({
  ariaLabel = "Visual skin",
  classicLabel = "Classic",
  cyberLabel = "Cyber HUD",
  ...rootProps
}: SkinSelectorProps) {
  const { skin, setSkin } = useSkin()
  const labels: Record<VisualSkin, string> = {
    classic: classicLabel,
    cyber: cyberLabel,
  }

  return (
    <Box
      role="group"
      aria-label={ariaLabel}
      display="inline-flex"
      alignItems="center"
      gap="1"
      p="1"
      bg="app.control"
      borderWidth="1px"
      borderColor="app.border"
      borderRadius="md"
      {...rootProps}
    >
      {skinOptions.map((option) => {
        const active = option.value === skin
        const label = labels[option.value]

        return (
          <Button
            key={option.value}
            type="button"
            size="xs"
            minW="8"
            px="2.5"
            variant="ghost"
            aria-label={`${label} skin`}
            aria-pressed={active}
            title={label}
            color={active ? "app.accent" : "app.textMuted"}
            bg={active ? "app.accentMuted" : "transparent"}
            borderWidth="1px"
            borderColor={active ? "app.interactiveBorder" : "transparent"}
            _hover={{
              color: "app.text",
              bg: "app.controlHover",
              borderColor: "app.interactiveBorder",
            }}
            _focusVisible={{
              outline: "2px solid",
              outlineColor: "app.focusRing",
              outlineOffset: "2px",
            }}
            onClick={() => setSkin(option.value)}
          >
            <Text as="span" hideBelow="md" lineHeight="1">
              {label}
            </Text>
            <Text
              as="span"
              display={{ base: "inline", md: "none" }}
              aria-hidden="true"
              lineHeight="1"
            >
              {option.shortLabel}
            </Text>
          </Button>
        )
      })}
    </Box>
  )
}
