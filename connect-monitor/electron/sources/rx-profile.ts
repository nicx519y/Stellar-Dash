import {rxCounters,rxTimings,type RxProfile} from "../../shared/rx-profile";
const WORDS=210,PAGES=35;
/** Ordered USB stream; a page-zero barrier prevents mixing firmware resets. */
export class RxProfileDecoder {
  private snapshot=-1;private next=0;private first=0;private words:number[]=[];
  reset(){this.snapshot=-1;this.next=0;this.words=[];}
  parse(v:DataView,now:number):RxProfile|undefined {
    if(v.byteLength!==32 || v.getUint32(0,true)!==0x31505852 || v.getUint8(7)!==1)return;
    const page=v.getUint8(6),seq=v.getUint16(4,true);
    if(page>=PAGES)return;
    if(page===0){this.reset();this.snapshot=seq;this.first=now;}
    if(seq!==this.snapshot || now-this.first>3000 || page!==this.next){this.reset();return;}
    for(let i=0;i<6;i++)this.words[page*6+i]=v.getUint32(8+4*i,true);
    if(++this.next!==PAGES)return;
    const w=this.words;this.reset();
    if(w.length!==WORDS || w[2]>3 || w[3]>4 || w[4]>4 || w[5]>1 || w[10]===0 || w[10]>500)return;
    let offset=26;
    const timings=Object.fromEntries(rxTimings.map(k=>{
      const t={count:w[offset],maxUs:w[offset+1],over125:w[offset+2],bins:w.slice(offset+3,offset+12)};
      offset+=12;return [k,t];
    })) as RxProfile["timings"];
    if(rxTimings.some(k=>timings[k].bins.reduce((a,b)=>a+b,0)!==timings[k].count))return;
    return {version:1,snapshot:seq,cycles:w[0],spanUs:w[1],speed:w[2],depth:w[3],highwater:w[4],pipeline:!!w[5],
      generation:w[6],lastDrop:w[7],dropGeneration:w[8],readCycles:w[9],cyclesPerUs:w[10],spikeCount:w[11],
      counters:Object.fromEntries(rxCounters.map((k,i)=>[k,w[12+i]])) as RxProfile["counters"],timings,
      spikes:Array.from({length:4},(_,i)=>({id:w[194+i*4],generation:w[195+i*4],us:w[196+i*4],cycles:w[197+i*4]}))};
  }
}
export const rxProfileDecoder=new RxProfileDecoder();
