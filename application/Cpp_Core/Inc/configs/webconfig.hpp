#ifndef _WEBCONFIG_H_
#define _WEBCONFIG_H_

#include "gpconfig.hpp"
#include "configmanager.hpp"
#include <string>
#include "rndis.h"
#include "fs.h"
#include "fscustom.h"
#include "fsdata.h"
#include "lwip/apps/httpd.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "config.hpp"
#include "storagemanager.hpp"
#include "cJSON.h"
#include "cJSON_Utils.h"
#include "adc_btns/adc_manager.hpp"
#include "adc_btns/adc_btns_marker.hpp"
#include "main.h"  // 用于 HAL_GetTick
#include "board_cfg.h"

class WebConfig : public GPConfig
{
public:
    virtual void setup();
    virtual void loop();
private:
    
};


#endif