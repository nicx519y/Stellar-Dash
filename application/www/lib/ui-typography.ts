// Shared by Chakra UI, styled SVG labels and canvas charts.
export const WEB_FONT_NAME = 'Sarasa Mono SC';
// Keep the previous Latin monospace appearance; explicit families let CJK fall through to Sarasa.
// A generic monospace at the front could choose a system serif CJK fallback instead.
export const WEB_FONT_FAMILY = `SFMono-Regular, Consolas, "Liberation Mono", "${WEB_FONT_NAME}", "Microsoft YaHei UI", "Microsoft YaHei", "PingFang SC", "Noto Sans CJK SC", sans-serif`;

// Sarasa's Han glyph ink sits about 0.8px above center in a 12px action label.
// Compensate the label's visual center, independently of its line box and icon.
export const WEB_CJK_BUTTON_LABEL_OFFSET = '0.07em';

export const WEB_FONT_SIZES = {
  '2xs': '10px', xs: '11px', sm: '12px', md: '12px', lg: '13px', xl: '14px',
  '2xl': '16px', '3xl': '18px', '4xl': '20px', '5xl': '24px', '6xl': '28px', '7xl': '32px',
} as const;
