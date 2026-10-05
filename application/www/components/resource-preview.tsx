"use client";
import { Box } from '@chakra-ui/react';
import { HitboxLightingCanvas } from './hitbox/hitbox-lighting-canvas';
import { factoryLightSource, type PreviewOptions } from '@/lib/hitbox-lighting-preview';
import type { ResourceSource } from '@/lib/resources';
const interactiveKeys = Array.from({length:22},(_,i)=>i);
export function ResourcePreview({source,colors,speed=3,brightness=100,sync=false,oneShot=false,mask=0,reserveLightingLayout=false}: {
  source:ResourceSource|null; colors?:number[]; speed?:number; brightness?:number; sync?:boolean;
  oneShot?:boolean; mask?:number; reserveLightingLayout?:boolean;
}) {
  if (source?.type === "switch-mapping") {
    const values: number[] = source.payload.originalValues;
    const min = Math.min(...values),
      max = Math.max(...values);
    return (
      <svg viewBox="0 0 310 100" width="100%" role="img" aria-label="ADC curve">
        <polyline
          fill="none"
          stroke="#81c995"
          strokeWidth="2"
          points={values
            .map(
              (v, i) =>
                `${5 + (i / (values.length - 1)) * 300},${95 - ((v - min) / Math.max(1, max - min)) * 90}`,
            )
            .join(" ")}
        />
      </svg>
    );
  }

  if (!source && !reserveLightingLayout) return null;
  const keys = source?.type === 'key-lighting' ? source : factoryLightSource(false);
  const ambient = source?.type === 'ambient-lighting' ? source : factoryLightSource(true);
  const options: PreviewOptions = {
    keyEnabled:source?.type === 'key-lighting',
    ambientEnabled:source?.type === 'ambient-lighting' || (!!source && sync),
    sync:source?.type === 'key-lighting' && sync, oneShot,
    keyColors:colors ?? keys.payload.colors, ambientColors:colors ?? ambient.payload.colors,
    keySpeed:speed, ambientSpeed:speed, keyBrightness:brightness, ambientBrightness:brightness,
    disabledKeys:[],
  };
  return <Box width="100%" maxW="540px">
    <HitboxLightingCanvas keys={keys} ambient={ambient} options={options} compact hardwareMask={mask}
      interactiveIds={source ? interactiveKeys : []}/>
  </Box>;
}
