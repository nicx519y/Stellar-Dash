import type { ResourceSource } from './xora-resource-codec.cjs';
export class LightEngine {
 constructor(source: ResourceSource, now?: number, seed?: number);
 render(now: number, mask: number, points: {x:number;y:number}[], keyCount: number, options?: {sync?:boolean;oneShot?:boolean;speed?:number;colors?:number[]}): number[][];
}
