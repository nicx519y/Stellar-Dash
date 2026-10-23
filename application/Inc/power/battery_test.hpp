#pragma once
#include "battery_test_log.h"
#include "fault_log_api.h"
extern "C" {
void BatteryTest_GetInfo(fl_info *info);
uint8_t BatteryTest_Read(uint32_t upper, uint32_t before, uint8_t category,
                        uint8_t severity, uint8_t pending, fl_record *record);
uint32_t XoraLog_CurrentBoot(void);
}
