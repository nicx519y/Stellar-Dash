const fs=require('node:fs');
const path=require('node:path');
const assert=require('node:assert/strict');
const sharp=require('../../application/www/node_modules/sharp');
require('../../application/www/node_modules/sucrase/register/ts-legacy-module-interop');
const {parseGIF,decompressFrames}=require('../../application/www/node_modules/gifuct-js');
const {gifFrameTimelineUs,selectGifFrameIndices}=require('../../application/www/lib/screen-control-image.ts');
const {buildJpegPayload,validateJpegPayload}=require('../../common/uimg-jpeg.cjs');
async function main(){
 const results=[];
 for(const file of fs.readdirSync(__dirname).filter(f=>f.endsWith('.gif'))){
  const gif=parseGIF(fs.readFileSync(path.join(__dirname,file)));
  assert.equal(gif.lsd.width,320);assert.equal(gif.lsd.height,172);
  const frames=decompressFrames(gif,true);
  assert.equal(frames.length,120);
  const {frameTimesUs,totalUs}=gifFrameTimelineUs(frames);
  assert.equal(totalUs,10000000);
  const selected=selectGifFrameIndices(frameTimesUs,totalUs,12,180);
  assert.deepEqual(selected,Array.from({length:120},(_,i)=>i));
  const jpeg=[];
  for(const frame of frames){
   // Encoder writes full-frame opaque images with disposal=2.
   assert.deepEqual(frame.dims,{top:0,left:0,width:320,height:172});
   assert.equal(frame.disposalType,2);
   jpeg.push(await sharp(Buffer.from(frame.patch),{raw:{width:320,height:172,channels:4}}).removeAlpha().jpeg({quality:82,chromaSubsampling:'4:2:0',progressive:false}).toBuffer());
  }
  const payload=buildJpegPayload(jpeg);validateJpegPayload(payload,120);
  assert(payload.length<=0x17f000);
  results.push({file,decoder:'WebConfig gifuct-js',frames:120,totalUs,sampling:'all 120 source frames preserved at 12 FPS',estimatedJpegPayloadBytes:payload.length,firmwareMaxPayloadBytes:0x17f000,note:'JPEG size is local sharp estimate; browser encoding may differ. No device access.'});
  console.log('PASS',file,'120 frames / 10s / 12 FPS / JPEG estimate',payload.length);
 }
 fs.writeFileSync(path.join(__dirname,'compatibility.json'),JSON.stringify(results,null,2));
}
main().catch(e=>{console.error(e);process.exitCode=1;});



