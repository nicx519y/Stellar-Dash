import { createSystem, defaultConfig, defineConfig } from '@chakra-ui/react';
import { WEB_FONT_FAMILY, WEB_FONT_SIZES } from '@/lib/ui-typography';

export const webSystem = createSystem(defaultConfig, defineConfig({
  globalCss: {
    html: { fontFamily: 'body' },
    body: { fontFamily: 'body', fontSize: 'md', lineHeight: '1.5', letterSpacing: 'normal', fontVariantNumeric: 'tabular-nums' },
    'button, input, select, textarea': { fontFamily: 'inherit' },
    'code, pre, kbd, samp': { fontFamily: 'mono' },
  },
  theme: {
    tokens: {
      fonts: {
        body: { value: WEB_FONT_FAMILY },
        heading: { value: WEB_FONT_FAMILY },
        mono: { value: WEB_FONT_FAMILY },
      },
      fontSizes: Object.fromEntries(Object.entries(WEB_FONT_SIZES).map(([name, value]) => [name, { value }])),
    },
    textStyles: {
      ...Object.fromEntries(Object.keys(WEB_FONT_SIZES).map(name => [name, {
        value: { fontSize: name, lineHeight: '1.5', letterSpacing: 'normal' },
      }])),
      label: { value: { fontSize: 'sm', lineHeight: '1.5', fontWeight: 'medium' } },
    },
  },
}));
