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
    body: 'icomoon, Arial, Helvetica, sans-serif',
    heading: 'Arial, Helvetica, sans-serif',
    mono: 'monospace',
    mulish: 'mulish, Arial, sans-serif'
  }
};

const Fonts = () => (
  <Global
    styles={`

      @font-face {
        font-family: 'custom_en';
        src: url('/fonts/custom_en.ttf') format('truetype');
        font-weight: normal;
        font-style: normal;
        font-display: swap;
      }

      /* 预加载字体 */
      .font-preload {
        font-family: 'custom_en', sans-serif;
        position: absolute;
        left: -9999px;
        top: -9999px;
        visibility: hidden;
        opacity: 0;
      }

      body {
        font-family: 'custom_en', system-ui, sans-serif;
        letter-spacing: 0.05em;
        line-height: 1.8em;
      }


    `}
  />
);

export function Provider({ children }: { children: React.ReactNode }) {
  return (
    <ChakraProvider value={theme}>
      <Fonts />
      {/* 添加字体预加载元素 */}
      <div className="font-preload">字体预加载</div>
      <ColorModeProvider>
        {children}
      </ColorModeProvider>
    </ChakraProvider>
  )
}
