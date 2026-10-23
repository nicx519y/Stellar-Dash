"use client"

import { ChakraProvider } from "@chakra-ui/react"
import { Global } from '@emotion/react';
import { ColorModeProvider } from "@/components/ui/color-mode"
import { webSystem } from './theme'
import '@/public/fonts/sarasa-mono-sc/fonts.css'

const Fonts = () => (
  <Global
    styles={`

      /* Keep private-use button glyphs separate from the UI text font. */
      @font-face {
        font-family: 'icomoon';
        src: url('/fonts/custom_en.ttf') format('truetype');
        font-weight: normal;
        font-style: normal;
        font-display: swap;
      }

    `}
  />
);

export function Provider({ children }: { children: React.ReactNode }) {
  return (
    <ChakraProvider value={webSystem}>
      <Fonts />
      <ColorModeProvider>
        {children}
      </ColorModeProvider>
    </ChakraProvider>
  )
}
