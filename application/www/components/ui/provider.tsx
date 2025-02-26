"use client"

import { ChakraProvider, defaultSystem } from "@chakra-ui/react"
import { Global } from '@emotion/react';
import { ColorModeProvider } from "@/components/ui/color-mode"

// 定义主题配置
const theme = {
  ...defaultSystem,
  styles: {
    global: {
      body: {
        fontFamily: 'Arial, Helvetica, sans-serif',
      }
    }
  },
  fonts: {
    body: 'Arial, Helvetica, sans-serif',
    heading: 'Arial, Helvetica, sans-serif',
    mono: 'monospace',
    icomoon: 'icomoon, Arial, sans-serif'
  }
};

const Fonts = () => (
  <Global
    styles={`
      @font-face {
        font-family: 'icomoon';
        src: url('/fonts/icomoon.ttf') format('truetype');
        font-weight: normal;
        font-style: normal;
        font-display: block;
      }
    `}
  />
);

export function Provider({ children }: { children: React.ReactNode }) {
  return (
    <ChakraProvider value={theme}>
      <Fonts />
      <ColorModeProvider>
        {children}
      </ColorModeProvider>
    </ChakraProvider>
  )
}
