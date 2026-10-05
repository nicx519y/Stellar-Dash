"use client";
import { useEffect, useMemo, useRef, useState } from 'react';
import { Box } from '@chakra-ui/react';
import { useColorMode } from '../ui/color-mode';
import { HitboxLightingPreview, ambientBorderColor, type PreviewOptions, type RGB } from '@/lib/hitbox-lighting-preview';
import type { ResourceSource } from '@/lib/resources';
import topology from '../../../../common/xora-light-topology.json';
import { HITBOX_WIDTH, HITBOX_HEIGHT, HITBOX_PADDING, HITBOX_LAYOUT_SCALE } from './hitbox-constants';
import styles from './hitbox-leds.module.css';

export type LightingKeyLayout = {x:number;y:number;r:number};
export const defaultLightingLayout: LightingKeyLayout[] = topology.slice(0,22).map(point => ({
  x:point.x * HITBOX_LAYOUT_SCALE, y:point.y * HITBOX_LAYOUT_SCALE, r:point.r * 2,
}));
const width = HITBOX_WIDTH + HITBOX_PADDING * 2 + 2;
const height = HITBOX_HEIGHT + HITBOX_PADDING * 2 + 2;

/** A single continuous, interpolated strip on the existing enclosure outline. */
function drawAmbient(canvas: HTMLCanvasElement, colors: RGB[], enabled: boolean) {
  const ctx = canvas.getContext('2d');
  if (!ctx) return;
  const scale = Math.max(0.1, canvas.getBoundingClientRect().width / width);
  const density = Math.min(window.devicePixelRatio || 1, 2) * scale;
  const pixelsW = Math.round(width * density), pixelsH = Math.round(height * density);
  if (canvas.width !== pixelsW || canvas.height !== pixelsH) {
    canvas.width = pixelsW; canvas.height = pixelsH;
  }
  ctx.setTransform(density, 0, 0, density, 0, 0);
  ctx.clearRect(0, 0, width, height);
  if (!enabled) return;
  const x = HITBOX_PADDING + 0.36, y = HITBOX_PADDING + 0.36;
  const gradient = ctx.createConicGradient(0, x + HITBOX_WIDTH / 2, y + HITBOX_HEIGHT / 2);
  const stops = Array.from({ length: 160 }, (_, i) => {
    const p = i / 40;
    const nx = p < 1 ? 0 : p < 2 ? p - 1 : p < 3 ? 1 : 4 - p;
    const ny = p < 1 ? p : p < 2 ? 1 : p < 3 ? 3 - p : 0;
    const angle = (Math.atan2((ny - 0.5) * HITBOX_HEIGHT, (nx - 0.5) * HITBOX_WIDTH) / (2 * Math.PI) + 1) % 1;
    return { angle, color: `rgb(${ambientBorderColor(colors, p).join(',')})` };
  }).sort((a,b) => a.angle - b.angle);
  for (const stop of stops) gradient.addColorStop(stop.angle, stop.color);
  gradient.addColorStop(1, stops[0].color);
  ctx.beginPath();
  ctx.roundRect(x, y, HITBOX_WIDTH, HITBOX_HEIGHT, 10);
  ctx.strokeStyle = gradient;
  ctx.lineWidth = 10;
  ctx.stroke();
}

/** Shared drawing and pointer preview; it has no device connection dependency. */
export function HitboxLightingCanvas({keys,ambient,options,layout=defaultLightingLayout,hardwareMask=0,
  interactiveIds=[],highlightIds=[],onPress,hasText=false,className,scale=1,compact=false,visible=true,resetToken}: {
  keys:ResourceSource; ambient:ResourceSource; options:PreviewOptions; layout?:readonly LightingKeyLayout[];
  hardwareMask?:number; interactiveIds?:readonly number[]; highlightIds?:readonly number[];
  onPress?:(id:number)=>void; hasText?:boolean; className?:string; scale?:number; compact?:boolean; visible?:boolean;
  resetToken?:unknown;
}) {
  const {colorMode} = useColorMode();
  const [pointerMask,setPointerMask] = useState(0);
  const engine = useMemo(() => new HitboxLightingPreview(keys,ambient,performance.now()), [keys,ambient]);
  const circleRefs = useRef<(SVGCircleElement|null)[]>([]), canvasRef = useRef<HTMLCanvasElement|null>(null);
  const latest = useRef({options,pointerMask,hardwareMask,colorMode});
  latest.current = {options,pointerMask,hardwareMask,colorMode};
  useEffect(() => {setPointerMask(0);}, [keys,ambient,resetToken]);
  useEffect(() => {
    let frame = 0, last = -Infinity;
    const animate = (now: number) => {
      if (now - last >= 1000 / 30) {
        last = now;
        const current = latest.current;
        const colors = engine.render(now, current.pointerMask, current.hardwareMask, current.options);
        circleRefs.current.forEach((circle, i) => {
          const color = colors.keys[i] ?? [0,0,0];
          circle?.setAttribute('fill', !current.options.keyEnabled || current.options.disabledKeys.includes(i)
            ? current.colorMode === 'light' ? '#fff' : '#080808' : `rgb(${color.join(',')})`);
        });
        if (canvasRef.current) drawAmbient(canvasRef.current, colors.ambient, current.options.ambientEnabled);
      }
      frame = requestAnimationFrame(animate);
    };
    frame = requestAnimationFrame(animate);
    return () => cancelAnimationFrame(frame);
  }, [engine]);
  const interactive = (i: number) => (interactiveIds ?? []).includes(i) && !(options.disabledKeys ?? []).includes(i);
  const setPressed = (i: number, down: boolean) => {
    if (!interactive(i)) return;
    setPointerMask(mask => down ? mask | (1 << i) : mask & ~(1 << i));
    onPress?.(down ? i : -1);
  };
  return <Box position="relative" display={visible ? 'block' : 'none'} className={className} width={compact ? '100%' : undefined} aspectRatio={compact ? `${width} / ${height}` : undefined} data-testid="hitbox-lighting-preview">
    <canvas ref={canvasRef} role="img" aria-label="XORA ambient lighting preview" style={{ position:'absolute', top:0, left:0, width:compact ? '100%' : width, height:compact ? '100%' : height, transform:compact ? undefined : `scale(${scale})`, transformOrigin:'center', pointerEvents:'none' }}/>
    <svg width={width} height={height} viewBox={`0 0 ${width} ${height}`}
      style={{ width:compact ? '100%' : width, height:compact ? '100%' : height, display:compact ? 'block' : undefined, position:'relative', transform:compact ? undefined : `scale(${scale})`, transformOrigin:'center' }} role="img" aria-label="XORA lighting preview"
      onPointerLeave={() => setPointerMask(0)}>
      <g transform={`translate(${HITBOX_PADDING} ${HITBOX_PADDING})`}>
      <rect x="0.36" y="0.36" width={HITBOX_WIDTH} height={HITBOX_HEIGHT} rx="10" fill="none" stroke={options.ambientEnabled ? 'none' : 'gray'} strokeWidth="1" />
      {layout.map((item,i) => <g key={i}>
        <circle cx={item.x} cy={item.y} r={item.r + 3} fill="none" stroke="gray" strokeWidth="1" pointerEvents="none" />
        <circle className={styles.key} ref={element => { circleRefs.current[i] = element; }} id={`btn-${i}`} cx={item.x} cy={item.y} r={item.r}
          fill={colorMode === 'light' ? 'white' : 'black'}
          stroke={((pointerMask | hardwareMask) & (1 << i)) || highlightIds?.includes(i) ? 'yellowgreen' : 'gray'}
          strokeWidth={((pointerMask | hardwareMask) & (1 << i)) ? 2 : 1}
          style={{ cursor:interactive(i) ? 'pointer' : 'default', touchAction:'none' }}
          role={interactive(i) ? 'button' : undefined} aria-label={interactive(i) ? `Key ${i+1}` : undefined} tabIndex={interactive(i) ? 0 : undefined}
          onPointerDown={event => { if (interactive(i)) { event.currentTarget.setPointerCapture(event.pointerId); setPressed(i,true); } }}
          onPointerUp={() => setPressed(i,false)} onPointerCancel={() => setPressed(i,false)} onLostPointerCapture={() => setPressed(i,false)}
          onKeyDown={event => { if(event.key === ' ' || event.key === 'Enter') {event.preventDefault(); setPressed(i,true);} }}
          onKeyUp={() => setPressed(i,false)} onBlur={() => setPressed(i,false)} />
        {(hasText ?? true) && <text x={item.x} y={i < layout.length - 4 ? item.y : item.y+30} textAnchor="middle" dominantBaseline="middle"
          fontSize="14" fill={colorMode === 'light' ? 'black' : 'white'} pointerEvents="none">{i === layout.length-1 ? 'Fn' : i+1}</text>}
      </g>)}
      </g>
    </svg>
  </Box>;
}
