import { defineSemanticTokens, defineTokens } from "@chakra-ui/react"

/**
 * Raw palette values are intentionally kept separate from semantic colors.
 * Components should consume `app.*` and `hud.*` so a visual skin never
 * changes device-owned values such as configured LED colors.
 */
export const hboxTokens = defineTokens({
  fonts: {
    body: {
      value: "'custom_en', system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif",
    },
    heading: {
      value: "'custom_en', system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif",
    },
    mono: {
      value: "ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace",
    },
    mulish: {
      value: "'mulish', Arial, sans-serif",
    },
  },
  colors: {
    hbox: {
      cyber: {
        canvas: { value: "#020810" },
        panel: { value: "#06111A" },
        control: { value: "#08141D" },
        controlHover: { value: "#0B1C28" },
        cyan: { value: "#58EAF4" },
        purple: { value: "#9E8CFF" },
        success: { value: "#7CFF42" },
        danger: { value: "#FF4D5E" },
        warning: { value: "#FFD166" },
        text: { value: "#EAF7FB" },
        textMuted: { value: "#9CB3C0" },
        border: { value: "#183847" },
        interactiveBorder: { value: "#2A6A80" },
      },
    },
  },
})

export const hboxSemanticTokens = defineSemanticTokens({
  colors: {
    /*
     * Extend Chakra's core semantic colors with a Cyber-only condition. The
     * default light/dark values from `defaultConfig` remain intact, while the
     * Cyber skin is predictably dark even if Classic was last used in light
     * mode.
     */
    bg: {
      DEFAULT: {
        value: { _cyber: "{colors.hbox.cyber.canvas}" },
      },
      panel: {
        value: { _cyber: "{colors.hbox.cyber.panel}" },
      },
      subtle: {
        value: { _cyber: "{colors.hbox.cyber.panel}" },
      },
      muted: {
        value: { _cyber: "{colors.hbox.cyber.control}" },
      },
      emphasized: {
        value: { _cyber: "{colors.hbox.cyber.controlHover}" },
      },
    },
    fg: {
      DEFAULT: {
        value: { _cyber: "{colors.hbox.cyber.text}" },
      },
      muted: {
        value: { _cyber: "{colors.hbox.cyber.textMuted}" },
      },
      subtle: {
        value: { _cyber: "{colors.hbox.cyber.textMuted}" },
      },
    },
    border: {
      DEFAULT: {
        value: { _cyber: "{colors.hbox.cyber.border}" },
      },
      muted: {
        value: { _cyber: "{colors.hbox.cyber.border}" },
      },
      subtle: {
        value: { _cyber: "{colors.hbox.cyber.border}" },
      },
      emphasized: {
        value: { _cyber: "{colors.hbox.cyber.interactiveBorder}" },
      },
    },
    app: {
      canvas: {
        value: {
          base: "{colors.bg}",
          _cyber: "{colors.hbox.cyber.canvas}",
        },
      },
      panel: {
        value: {
          base: "{colors.bg.panel}",
          _cyber: "{colors.hbox.cyber.panel}",
        },
      },
      control: {
        value: {
          base: "{colors.bg.muted}",
          _cyber: "{colors.hbox.cyber.control}",
        },
      },
      controlHover: {
        value: {
          base: "{colors.bg.emphasized}",
          _cyber: "{colors.hbox.cyber.controlHover}",
        },
      },
      border: {
        value: {
          base: "{colors.border}",
          _cyber: "{colors.hbox.cyber.border}",
        },
      },
      interactiveBorder: {
        value: {
          base: "{colors.border.emphasized}",
          _cyber: "{colors.hbox.cyber.interactiveBorder}",
        },
      },
      text: {
        value: {
          base: "{colors.fg}",
          _cyber: "{colors.hbox.cyber.text}",
        },
      },
      textMuted: {
        value: {
          base: "{colors.fg.muted}",
          _cyber: "{colors.hbox.cyber.textMuted}",
        },
      },
      accent: {
        value: {
          base: "{colors.blue.500}",
          _cyber: "{colors.hbox.cyber.cyan}",
        },
      },
      accentMuted: {
        value: {
          base: "{colors.blue.subtle}",
          _cyber: "rgba(88, 234, 244, 0.12)",
        },
      },
      focusRing: {
        value: {
          base: "{colors.blue.focusRing}",
          _cyber: "{colors.hbox.cyber.cyan}",
        },
      },
      overlay: {
        value: {
          base: "{colors.blackAlpha.400}",
          _cyber: "rgba(0, 3, 8, 0.72)",
        },
      },
    },
    hud: {
      cyan: {
        value: {
          base: "{colors.cyan.500}",
          _cyber: "{colors.hbox.cyber.cyan}",
        },
      },
      purple: {
        value: {
          base: "{colors.purple.500}",
          _cyber: "{colors.hbox.cyber.purple}",
        },
      },
      success: {
        value: {
          base: "{colors.green.500}",
          _cyber: "{colors.hbox.cyber.success}",
        },
      },
      danger: {
        value: {
          base: "{colors.red.500}",
          _cyber: "{colors.hbox.cyber.danger}",
        },
      },
      warning: {
        value: {
          base: "{colors.orange.400}",
          _cyber: "{colors.hbox.cyber.warning}",
        },
      },
      line: {
        value: {
          base: "{colors.border}",
          _cyber: "rgba(88, 234, 244, 0.30)",
        },
      },
      lineStrong: {
        value: {
          base: "{colors.border.emphasized}",
          _cyber: "{colors.hbox.cyber.cyan}",
        },
      },
      grid: {
        value: {
          base: "{colors.blackAlpha.100}",
          _cyber: "rgba(88, 234, 244, 0.10)",
        },
      },
      glow: {
        value: {
          base: "{colors.blue.400}",
          _cyber: "rgba(88, 234, 244, 0.45)",
        },
      },
    },
  },
  shadows: {
    hudPanel: {
      value: {
        base: "{shadows.sm}",
        _cyber:
          "inset 0 0 0 1px rgba(88, 234, 244, 0.04), 0 14px 38px rgba(0, 0, 0, 0.34)",
      },
    },
    hudGlow: {
      value: {
        base: "{shadows.sm}",
        _cyber:
          "0 0 0 1px rgba(88, 234, 244, 0.16), 0 0 24px rgba(88, 234, 244, 0.13)",
      },
    },
  },
  radii: {
    hudPanel: {
      value: {
        base: "{radii.l2}",
        _cyber: "2px",
      },
    },
  },
})
