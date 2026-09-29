import { defineRecipe } from "@chakra-ui/react"

export const hudPanelRecipe = defineRecipe({
  className: "hbox-hud-panel",
  base: {
    position: "relative",
    minWidth: "0",
    bg: "app.panel",
    color: "app.text",
    borderWidth: "1px",
    borderColor: "app.border",
    borderRadius: "hudPanel",
    boxShadow: "hudPanel",
    overflow: "hidden",
    transitionProperty: "border-color, box-shadow, background-color",
    transitionDuration: "moderate",
    _focusWithin: {
      borderColor: "app.interactiveBorder",
    },
    "html[data-skin=cyber] &": {
      backgroundImage:
        "linear-gradient(135deg, rgba(88, 234, 244, 0.035), transparent 42%), linear-gradient(315deg, rgba(158, 140, 255, 0.025), transparent 38%)",
      clipPath:
        "polygon(0 10px, 10px 0, calc(100% - 10px) 0, 100% 10px, 100% calc(100% - 10px), calc(100% - 10px) 100%, 10px 100%, 0 calc(100% - 10px))",
    },
    _motionReduce: {
      transition: "none",
    },
  },
  variants: {
    density: {
      compact: {
        p: "3",
      },
      normal: {
        p: "4",
      },
      spacious: {
        p: "6",
      },
    },
    emphasis: {
      default: {},
      strong: {
        borderColor: "app.interactiveBorder",
        boxShadow: "hudGlow",
      },
      quiet: {
        bg: "transparent",
        boxShadow: "none",
      },
    },
  },
  defaultVariants: {
    density: "normal",
    emphasis: "default",
  },
})

export const hudSectionTitleRecipe = defineRecipe({
  className: "hbox-hud-section-title",
  base: {
    display: "flex",
    alignItems: "center",
    gap: "2",
    minHeight: "7",
    pb: "2",
    color: "app.text",
    fontSize: "sm",
    fontWeight: "semibold",
    letterSpacing: "wide",
    lineHeight: "short",
    borderBottomWidth: "1px",
    borderColor: "app.border",
    _before: {
      content: '""',
      display: "block",
      width: "0.2rem",
      height: "1rem",
      flexShrink: "0",
      borderRadius: "full",
      bg: "app.accent",
    },
    "html[data-skin=cyber] &": {
      color: "hud.cyan",
      textTransform: "uppercase",
      letterSpacing: "0.09em",
      textShadow: "0 0 12px var(--chakra-colors-hud-glow)",
    },
  },
})

export const statusChipRecipe = defineRecipe({
  className: "hbox-status-chip",
  base: {
    display: "inline-flex",
    alignItems: "center",
    gap: "1.5",
    width: "fit-content",
    minHeight: "6",
    px: "2.5",
    py: "1",
    borderWidth: "1px",
    borderRadius: "full",
    fontSize: "xs",
    fontWeight: "medium",
    letterSpacing: "wide",
    lineHeight: "1",
    whiteSpace: "nowrap",
  },
  variants: {
    tone: {
      neutral: {
        color: "app.textMuted",
        bg: "app.control",
        borderColor: "app.border",
      },
      info: {
        color: "hud.cyan",
        bg: "app.accentMuted",
        borderColor: "app.interactiveBorder",
      },
      success: {
        color: "hud.success",
        bg: "color-mix(in srgb, var(--chakra-colors-hud-success) 10%, transparent)",
        borderColor:
          "color-mix(in srgb, var(--chakra-colors-hud-success) 42%, transparent)",
      },
      warning: {
        color: "hud.warning",
        bg: "color-mix(in srgb, var(--chakra-colors-hud-warning) 10%, transparent)",
        borderColor:
          "color-mix(in srgb, var(--chakra-colors-hud-warning) 42%, transparent)",
      },
      danger: {
        color: "hud.danger",
        bg: "color-mix(in srgb, var(--chakra-colors-hud-danger) 10%, transparent)",
        borderColor:
          "color-mix(in srgb, var(--chakra-colors-hud-danger) 42%, transparent)",
      },
    },
  },
  defaultVariants: {
    tone: "neutral",
  },
})

export const deviceStageRecipe = defineRecipe({
  className: "hbox-device-stage",
  base: {
    position: "relative",
    isolation: "isolate",
    display: "grid",
    placeItems: "center",
    minWidth: "0",
    minHeight: "0",
    overflow: "hidden",
    color: "app.text",
    bg: "app.canvas",
    backgroundImage:
      "radial-gradient(circle at 50% 44%, color-mix(in srgb, var(--chakra-colors-app-accent) 10%, transparent), transparent 42%), linear-gradient(180deg, var(--chakra-colors-app-panel), var(--chakra-colors-app-canvas))",
    backgroundPosition: "center",
    backgroundRepeat: "no-repeat",
    backgroundSize: "cover",
    borderWidth: "1px",
    borderColor: "app.border",
    borderRadius: "hudPanel",
    boxShadow: "hudPanel",
    _before: {
      content: '""',
      position: "absolute",
      zIndex: "-1",
      inset: "0",
      pointerEvents: "none",
      backgroundImage:
        "linear-gradient(var(--chakra-colors-hud-grid) 1px, transparent 1px), linear-gradient(90deg, var(--chakra-colors-hud-grid) 1px, transparent 1px)",
      backgroundSize: "24px 24px",
      maskImage: "linear-gradient(to bottom, transparent, black 16%, black 84%, transparent)",
    },
    _after: {
      content: '""',
      position: "absolute",
      zIndex: "2",
      inset: "10px",
      pointerEvents: "none",
      borderWidth: "1px",
      borderColor: "hud.line",
      opacity: "0.72",
    },
    "html[data-skin=cyber] &": {
      boxShadow: "hudGlow",
    },
  },
})

export const hboxRecipes = {
  hudPanel: hudPanelRecipe,
  hudSectionTitle: hudSectionTitleRecipe,
  statusChip: statusChipRecipe,
  deviceStage: deviceStageRecipe,
}
