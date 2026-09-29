import { createSystem, defaultConfig, defineConfig } from "@chakra-ui/react"

import { hboxRecipes } from "./recipes"
import { hboxSemanticTokens, hboxTokens } from "./tokens"

export const hboxThemeConfig = defineConfig({
  conditions: {
    cyber: 'html[data-skin="cyber"] &',
  },
  globalCss: {
    'html[data-skin="cyber"]': {
      colorScheme: "dark",
      bg: "app.canvas",
      color: "app.text",
      _motionReduce: {
        scrollBehavior: "auto",
      },
    },
    'html[data-skin="cyber"] body': {
      bg: "app.canvas",
      color: "app.text",
    },
    'html[data-skin="cyber"] *::selection': {
      bg: "app.accentMuted",
      color: "app.text",
    },
    'html[data-skin="cyber"] :focus-visible': {
      outlineColor: "app.focusRing",
    },
  },
  theme: {
    tokens: hboxTokens,
    semanticTokens: hboxSemanticTokens,
    recipes: hboxRecipes,
  },
})

/**
 * A single Chakra system is shared by both visual skins. Switching skins only
 * changes semantic token conditions and never remounts ChakraProvider.
 */
export const system = createSystem(defaultConfig, hboxThemeConfig)
