import type { FastRequest } from "../../shared/fast-recovery";
const channels = [10,16,22,24,28,34,39,255];
export function fastCrc(data: Uint8Array): number {
  let crc=0xffff;
  for(const v of data){crc^=v<<8;for(let b=0;b<8;b++)crc=crc&0x8000?((crc<<1)^0x1021)&65535:(crc<<1)&65535;}
  return crc;
}
export function buildFastControl(input: FastRequest): Buffer {
  const n=(v: unknown,max: number,def=0)=>{
    const x=v===undefined?def:v;if(typeof x!=="number" || !Number.isInteger(x) || x<0 || x>max)throw new Error("Invalid experimental control field");return x;
  };
  const op=n(input.operation,5),id=n(input.testId,65535),target=n(input.target,2);
  if(op<1 || id===0)throw new Error("Operation and test ID are required");
  const channel=n(input.channel,255,255);if(!channels.includes(channel))throw new Error("Invalid RF channel");
  const kind=n(input.kind,8),count=n(input.count,255),ms=n(input.durationMs,5000),delay=n(input.delayUs,1000000);
  if(op===2 && (!kind || !ms))throw new Error("Injection needs a bounded duration");
  if(op===2 && ((kind===4 || kind===8) && ms>4000 || kind===8 && channel===255))throw new Error("Channel mask needs a real allowed channel and at most 4000ms");
  if(op===2 && kind===2 && target!==1)throw new Error("DATA 丢失在 RX 接收入口注入，请选择 RX");
  if(input.enabled!==undefined && typeof input.enabled!=="boolean")throw new Error("Invalid enabled field");
  if(n(input.seed,0xffffffff)!==0 || (op!==2 && (kind || count || ms || delay || channel!==255)) ||
     (op!==1 && input.enabled) || ((op===1 || op===4 || op===5) && target))throw new Error("Unexpected fields for experimental operation");
  const b=Buffer.alloc(32);b.writeUInt32LE(0x344c5446,0);b[4]=1;b[5]=op;b[6]=target;b[7]=kind;
  b.writeUInt16LE(id,8);b.writeUInt16LE(count,10);b.writeUInt16LE(ms,12);b[14]=channel;b[15]=input.enabled?1:0;
  b.writeUInt32LE(delay,16);b.writeUInt32LE(n(input.seed,0xffffffff),20);b.writeUInt16LE(fastCrc(b.subarray(0,30)),30);return b;
}
