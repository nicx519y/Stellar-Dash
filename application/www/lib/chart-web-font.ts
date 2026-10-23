import { Chart, type Plugin } from 'chart.js';
import { WEB_FONT_NAME } from './ui-typography';

let ready: Promise<FontFace[]> | undefined;

// Canvas does not redraw automatically when a CSS webfont replaces its fallback.
export const chartWebFont: Plugin = {
  id: 'web-font',
  afterInit(chart) {
    if (typeof document === 'undefined' || !document.fonts) return;
    ready ??= document.fonts.load(`12px "${WEB_FONT_NAME}"`, '中');
    void ready.then(() => {
      if (Chart.getChart(chart.canvas) === chart) chart.update('none');
    }).catch(() => { /* Keep the chart usable with the configured fallback fonts. */ });
  },
};
