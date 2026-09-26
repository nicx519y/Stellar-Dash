export const rxCounters = ["received","accepted","rejected","sameMerged","changedOverwritten",
  "ready","submitted","completed","congestionDropped","cancelled","submitFailed","auxDropped","crc","control"] as const;
export const rxTimings = ["rearm","parse","ready","rf","usb","background","critical","serviceGap",
  "queueWait","inFlight","rfCpu","usbCpu","backgroundCpu","timer"] as const;
export const rxBounds = [8,16,32,64,125,250,500,1000,Infinity];
export interface RxTiming { count:number; maxUs:number; over125:number; bins:number[] }
export interface RxProfile {
  version:1; snapshot:number; cycles:number; spanUs:number; speed:number; depth:number; highwater:number;
  pipeline:boolean; generation:number; lastDrop:number; dropGeneration:number; readCycles:number; cyclesPerUs:number;
  spikeCount:number; counters:Record<typeof rxCounters[number],number>;
  timings:Record<typeof rxTimings[number],RxTiming>;
  spikes:Array<{id:number;generation:number;us:number;cycles:number}>;
}
export function rxQuantile(t:RxTiming,p:number):number|null {
  if(!t.count)return null;let n=0;
  for(let i=0;i<t.bins.length;i++){n+=t.bins[i];if(n>=Math.ceil(t.count*p))return rxBounds[i];}
  return null;
}
export function rxProfileDelta(a:RxProfile,b:RxProfile){
  const dt=((b.cycles-a.cycles)>>>0)/b.cyclesPerUs;
  if(a.pipeline!==b.pipeline || a.cyclesPerUs!==b.cyclesPerUs || !Number.isFinite(dt) || dt<100000 || dt>6000000)return null;
  const delta=Object.fromEntries(rxCounters.map(k=>[k,(b.counters[k]-a.counters[k])>>>0])) as RxProfile["counters"];
  if(rxCounters.some(k=>delta[k]>dt*0.1+100))return null;
  return {seconds:dt/1e6,delta};
}
