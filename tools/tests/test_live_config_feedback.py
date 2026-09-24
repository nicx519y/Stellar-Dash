import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
APP = ROOT / 'application'


def function(relative, signature):
    source = (APP / relative).read_text(encoding='utf-8')
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class LiveConfigFeedbackTests(unittest.TestCase):
    def native(self, source, files=None):
        compiler = shutil.which('g++') or shutil.which('clang++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            folder = pathlib.Path(temporary)
            for name, content in (files or {}).items():
                (folder / name).write_text(content, encoding='utf-8')
            cpp = folder / 'test.cpp'
            cpp.write_text(source, encoding='utf-8')
            executable = folder / 'test.exe'
            result = subprocess.run([compiler, '-std=c++17', '-I', str(folder),
                                     '-I', str(APP / 'Cpp_Core/Inc'), str(cpp), '-o', str(executable)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_qspi_wait_services_feedback_until_ready_but_never_acknowledges_busy_flash(self):
        driver = 'Drivers/QSPI-W25Q64/qspi-w25q64.c'
        self.native(r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
#include "qspi_wait_scope.hpp"
static QSPI_W25Qxx_WaitCallback wait_callback = nullptr;
static unsigned tick=0, busy=0, callbacks=0;
static bool readError=false;
#define QSPI_W25Qxx_OK 0
#define W25Qxx_ERROR_AUTOPOLLING -1
#define W25Qxx_Status_REG1_BUSY 1
#define W25Qxx_READY_POLL_MAX_ATTEMPTS 100
#define W25Qxx_READY_TIMEOUT_MS 20
uint32_t HAL_GetTick(){return tick;}
void QSPI_W25Qxx_ReadyPollPause(){++tick;}
int QSPI_W25Qxx_ReadStatusReg1(uint8_t* status){
    if(readError)return -1;
    *status=busy ? (--busy,1):0; return 0;
}
void feedback(){++callbacks;}
''' + function(driver, 'QSPI_W25Qxx_WaitCallback QSPI_W25Qxx_SetWaitCallback')
            + '\n' + function(driver, 'int8_t QSPI_W25Qxx_AutoPollingMemReady') + r'''
int main(){
    busy=12;
    { QspiWaitScope scope(feedback);
      assert(QSPI_W25Qxx_AutoPollingMemReady()==0);
      assert(callbacks==12 && busy==0);
      {QspiWaitScope exclusive(nullptr); assert(wait_callback==nullptr);}
      assert(wait_callback==feedback);
      busy=100;
      assert(QSPI_W25Qxx_AutoPollingMemReady()==-1);
      assert(busy>0); // Timeout is not a successful write.
      readError=true;
      unsigned before=callbacks;
      assert(QSPI_W25Qxx_AutoPollingMemReady()==-1 && callbacks==before);
    }
    assert(wait_callback==nullptr);
}
''', {'qspi-w25q64.h': '''#pragma once
typedef void (*QSPI_W25Qxx_WaitCallback)(void);
QSPI_W25Qxx_WaitCallback QSPI_W25Qxx_SetWaitCallback(QSPI_W25Qxx_WaitCallback);
'''})

    def test_save_feedback_pumps_input_led_and_output_without_dispatching_rpc(self):
        self.native(r'''
#include <cassert>
#include <cstdint>
unsigned tick=1, inputs=0, lights=0, links=0, outputs=0;
uint32_t HAL_GetTick(){return tick;}
struct Buttons{void update(){++inputs;} unsigned getCurrentMask(){return 42;}} buttons;
struct Leds{void update(unsigned mask){assert(mask==42);++lights;}} leds;
struct Link{void process(){++links;}} link;
#define WEBCONFIG_BTNS_MANAGER buttons
#define WEBCONFIG_LEDS_MANAGER leds
#define USB_BOARD_LINK link
struct WebHidService{
 bool initialized=true, sessionEstablished=true;
 void serviceConfigSaveFeedback();
 void pumpOutput(){++outputs;}
};
''' + function('Cpp_Core/Src/webhid_service.cpp', 'void WebHidService::serviceConfigSaveFeedback()') + r'''
int main(){
 WebHidService s; s.serviceConfigSaveFeedback();
 assert(inputs==1 && lights==1 && links==1 && outputs==1);
 s.serviceConfigSaveFeedback(); assert(inputs==1);
 ++tick; s.serviceConfigSaveFeedback(); assert(inputs==2);
 ++tick; s.sessionEstablished=false; s.serviceConfigSaveFeedback(); assert(inputs==2);
 s.sessionEstablished=true; s.initialized=false; s.serviceConfigSaveFeedback(); assert(inputs==2);
}
''')

    def test_brightness_and_color_updates_preserve_animation_and_held_keys(self):
        self.native(r'''
#include <cassert>
#include <cstdint>
#define APP_STAGE(...) ((void)0)
unsigned tick=100;
uint32_t HAL_GetTick(){return tick;}
enum WS2812B_StateTypeDef{WS2812B_RUNNING};
struct LEDProfile{bool ledEnabled=true, aroundLedEnabled=true;
 int ledEffect=1, aroundLedEffect=1; bool aroundLedSyncToMainLed=false, aroundLedTriggerByButton=false;
 int ledBrightness=50, ledColor1=123;};
struct LedStripController{
 mutable unsigned blanks=0, starts=0, powerChanges=0;
 static const LedStripController& keys(){static LedStripController s;return s;}
 static const LedStripController& ambient(){static LedStripController s;return s;}
 void setAllBrightness(unsigned)const{++blanks;} void submitFrame()const{}
 WS2812B_StateTypeDef start()const{++starts;return WS2812B_RUNNING;}
 void setPowerEnabled(bool)const{++powerChanges;}
};
struct LEDsManager{
 LEDProfile temporaryConfig{}; const LEDProfile* opts=&temporaryConfig;
 bool runtimeEnabled=true, usingTemporaryConfig=true;
 unsigned enabledKeysMask=0, animationStartTime=10, aroundLedAnimationStartTime=20;
 unsigned lastButtonState=42, rippleCount=3, aroundLedRippleCount=4;
 unsigned keyStartupRampStartTime=0, ambientStartupRampStartTime=0, setups=0;
 bool keyStartupRampActive=false, ambientStartupRampActive=false;
 void setup(){++setups;} void updateColorsFromConfig(){}
 void setTemporaryConfig(const LEDProfile&,uint32_t);
};
''' + function('Cpp_Core/Src/leds/leds_manager.cpp', 'void LEDsManager::setTemporaryConfig') + r'''
int main(){
 LEDsManager m; LEDProfile next=m.temporaryConfig;
 next.ledBrightness=80; next.ledColor1=999; m.setTemporaryConfig(next,42);
 assert(m.animationStartTime==10 && m.aroundLedAnimationStartTime==20);
 assert(m.lastButtonState==42 && m.rippleCount==3 && m.aroundLedRippleCount==4);
 assert(LedStripController::keys().blanks==0 && LedStripController::ambient().blanks==0);
 assert(m.setups==0);
 next.ledEffect=2; m.setTemporaryConfig(next,42);
 assert(m.animationStartTime==100 && m.rippleCount==0);
 assert(m.aroundLedAnimationStartTime==20 && m.aroundLedRippleCount==4);
 assert(m.lastButtonState==42 && LedStripController::keys().blanks==0);
 next.ledEnabled=false; m.setTemporaryConfig(next,42);
 assert(LedStripController::keys().powerChanges==1);
 assert(LedStripController::ambient().powerChanges==0);
}
''')

    def test_live_profile_refresh_preserves_pressed_state_without_restarting_sampling(self):
        self.native(r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <array>
#include <algorithm>
#define APP_DBG(...) ((void)0)
#define APP_ERR(...) ((void)0)
#define NUM_ADC_BUTTONS 1
#define MAX_ADC_VALUES_LENGTH 2
#define MIN_ADC_TOP_DEADZONE 0.1f
#define MIN_ADC_BOTTOM_DEADZONE 0.1f
#define MIN_ADC_RELEASE_ACCURACY 0.1f
using float_t=float;
enum class ADCBtnsError{SUCCESS,MAPPING_NOT_FOUND,GAMEPAD_PROFILE_NOT_FOUND,MAPPING_INVALID_RANGE};
enum class ButtonState{RELEASED,PRESSED}; enum class ButtonEvent{NONE,PRESS,RELEASE};
struct ADCButtonValueInfo{unsigned virtualPin=0;};
struct ADCValuesMapping{unsigned length=2; float step=1; uint16_t originalValues[2]={3000,1000};};
struct RapidTriggerProfile{float pressAccuracy=.3f,releaseAccuracy=.2f,topDeadzone=.1f,bottomDeadzone=.1f;};
struct GamepadProfile{struct{bool keysEnableTag[1]={true};} keysConfig;
 struct{int debounceAlgorithm=0; RapidTriggerProfile triggerConfigs[1];}triggerConfigs;};
GamepadProfile profile;
struct Storage{struct{bool autoCalibrationEnabled=false;char defaultProfileId[2]={'a',0};}config;
 GamepadProfile* getGamepadProfile(char*){return &profile;}} storage;
unsigned starts=0;
struct ADCManager{ADCValuesMapping mapping;
 std::array<ADCButtonValueInfo,1> values{};
 std::string getDefaultMapping(){return "mapping";}
 const auto& readADCValues(){return values;}
 const auto* getMapping(const char*){return &mapping;}
 ADCBtnsError getCalibrationValues(const char*,unsigned,bool,uint16_t& a,uint16_t& b){a=1000;b=3000;return ADCBtnsError::SUCCESS;}
 ADCBtnsError startADCSamping(bool){++starts;return ADCBtnsError::SUCCESS;}} adc;
#define ADC_MANAGER adc
#define STORAGE_MANAGER storage
struct ADCBtn{ButtonState state=ButtonState::PRESSED;ButtonEvent debounceCandidate=ButtonEvent::RELEASE;
 unsigned virtualPin=0,debounceSinceUs=456;bool initCompleted=true;
 float pressAccuracyMm=0,releaseAccuracyMm=0,highPrecisionReleaseAccuracyMm=0,topDeadzoneMm=0,bottomDeadzoneMm=0,halfwayDistanceMm=0;
 uint16_t pressStartValue=1234,releaseStartValue=2345,cachedPressThreshold=0,cachedReleaseThreshold=0;
 uint16_t valueMapping[2]{},calibratedMapping[2]{};};
struct ADCBtnsWorker{unsigned virtualPinMask=1,enabledKeysMask=1;bool buttonTriggerStatusChanged=false;
 const ADCValuesMapping* mapping=nullptr;int debounceAlgorithm=0;float maxTravelDistance=0;
 ADCBtn button;ADCBtn* buttonPtrs[1]={&button};
 ADCBtnsError setup(bool);
 void generateCalibratedMapping(ADCBtn* b,uint16_t,uint16_t){b->state=ButtonState::RELEASED;b->pressStartValue=65535;b->releaseStartValue=0;}
 uint16_t calculatePressThreshold(ADCBtn*,uint16_t value){return value+1;}
 uint16_t calculateReleaseThreshold(ADCBtn*,uint16_t value){return value-1;}
};
''' + function('Cpp_Core/Src/adc_btns/adc_btns_worker.cpp', 'ADCBtnsError ADCBtnsWorker::setup') + r'''
int main(){
 ADCBtnsWorker w;assert(w.setup(true)==ADCBtnsError::SUCCESS);
 assert(w.virtualPinMask==1 && w.button.state==ButtonState::PRESSED && starts==0);
 assert(w.button.pressStartValue==1234 && w.button.releaseStartValue==2345);
 assert(w.button.debounceCandidate==ButtonEvent::RELEASE && w.button.debounceSinceUs==456);
 assert(w.button.cachedPressThreshold==1235 && w.button.cachedReleaseThreshold==2344);
 assert(w.setup(false)==ADCBtnsError::SUCCESS);
 assert(w.virtualPinMask==0 && w.button.state==ButtonState::RELEASED && starts==1);
}
''')


if __name__ == '__main__':
    unittest.main()
