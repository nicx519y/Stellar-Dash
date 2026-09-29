#include "screen_control/jpeg_player.hpp"
#include "stm32h7xx_hal_jpeg.h"
#include "stm32h7xx_hal_dma2d.h"
#include <cassert>
#include <vector>
#include <cstdio>
uint32_t tick=0, sampling=0, outputSize=0, emitted=0;
bool paused=false, infoSent=false, badDimensions=false, corruptRow=false, stall=false;
std::vector<uint32_t> heights;
void resetJpeg(){ paused=false;infoSent=false;emitted=0; }
void resetDma(){registers={};}
uint32_t HAL_GetTick(){return tick;}
int HAL_JPEG_Init(JPEG_HandleTypeDef*){return HAL_OK;}
int HAL_DMA2D_Init(DMA2D_HandleTypeDef* h){h->locked=false;return HAL_OK;}
int HAL_DMA2D_ConfigLayer(DMA2D_HandleTypeDef* h,uint32_t){assert(h->LayerCfg[1].ChromaSubSampling==sampling);return HAL_OK;}
int HAL_JPEG_Decode_IT(JPEG_HandleTypeDef*,uint8_t*,uint32_t,uint8_t*,uint32_t){return HAL_OK;}
void HAL_JPEG_ConfigOutputBuffer(JPEG_HandleTypeDef*,uint8_t*,uint32_t size){outputSize=size;}
int HAL_JPEG_Resume(JPEG_HandleTypeDef*,uint32_t){paused=false;return HAL_OK;}
int HAL_JPEG_Pause(JPEG_HandleTypeDef*,uint32_t){paused=true;return HAL_OK;}
void HAL_JPEG_IRQHandler(JPEG_HandleTypeDef* h){
 if(stall)return;
 if(!infoSent){ JPEG_ConfTypeDef info{badDimensions?321u:320u,172,1,sampling};HAL_JPEG_InfoReadyCallback(h,&info);infoSent=true;return; }
 if(paused)return;
 emitted++;
 HAL_JPEG_DataReadyCallback(h,nullptr,corruptRow?4:outputSize);
 if(emitted==(sampling==0?11u:22u))HAL_JPEG_DecodeCpltCallback(h);
}
int HAL_DMA2D_Start(DMA2D_HandleTypeDef* h,uint32_t,uint32_t,uint32_t width,uint32_t lines){
 if(h->locked)return HAL_ERROR; // Faithful to HAL's lock lifetime.
 h->locked=true;assert(width==320);heights.push_back(lines);registers.CR=1;return HAL_OK;
}
int HAL_DMA2D_PollForTransfer(DMA2D_HandleTypeDef* h,uint32_t timeout){assert(timeout==0);assert(registers.CR==0);h->locked=false;return HAL_OK;}
int main(){
 alignas(32) uint8_t input[4]{};alignas(32) uint16_t pixels[320*172]{};
 ST7789_Handle lcd{true,pixels,false,0,0,0,0};
 for(sampling=0;sampling<3;sampling++){
  heights.clear();lcd.dirty_valid=false;
  assert(ScreenJpeg_Begin(input,4,&lcd));int state=0;
  for(int n=0;n<200 && state==0;n++){
   state=ScreenJpeg_Poll();
   if(state==0)assert(!lcd.dirty_valid);
   if(registers.CR){assert(ScreenJpeg_Poll()==0);registers.CR=0;}
  }
  assert(state==1);assert(!ScreenJpeg_Active());assert(lcd.dirty_valid);
  assert(heights.size()==(sampling==0?11u:22u));assert(heights.back()==(sampling==0?12u:4u));
  uint32_t total=0;for(auto height:heights)total+=height;assert(total==172);
 }
 sampling=0;lcd.dirty_valid=false;stall=true;
 assert(ScreenJpeg_Begin(input,4,&lcd));tick+=150;assert(ScreenJpeg_Poll()==-1);assert(!lcd.dirty_valid);stall=false;
 badDimensions=true;assert(ScreenJpeg_Begin(input,4,&lcd));assert(ScreenJpeg_Poll()==-1);badDimensions=false;
 corruptRow=true;assert(ScreenJpeg_Begin(input,4,&lcd));assert(ScreenJpeg_Poll()==-1);corruptRow=false;
 assert(ScreenJpeg_Begin(input,4,&lcd));assert(ScreenJpeg_Poll()==0);registers.ISR=DMA2D_ISR_TEIF;
 assert(ScreenJpeg_Poll()==-1);assert(!lcd.dirty_valid);
 assert(ScreenJpeg_Begin(input,4,&lcd));ScreenJpeg_Cancel();assert(!ScreenJpeg_Active());assert(!lcd.dirty_valid);
 assert(!ScreenJpeg_Begin(input+1,3,&lcd));
 puts("JPEG playback state tests passed");
}
