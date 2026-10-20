#ifndef XORA_FAULT_LOG_API_H
#define XORA_FAULT_LOG_API_H
#include "fault_log.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t version, newest, oldest, count, pending, dropped, firmware, status, restricted;
} fl_info;
void XoraLog_Init(void);
void XoraLog_Service(int safe);
void XoraLog_Tick(uint32_t now);
void XoraLog_Event(uint16_t event, uint8_t category, uint8_t severity, uint32_t detail);
void XoraLog_RequestFlush(void);
int XoraLog_Restricted(void);
void XoraLog_RequireMaintenance(void);
void XoraLog_BootFlags(uint32_t flags);
void XoraLog_Info(fl_info *info);
uint8_t XoraLog_Read(uint32_t upper, uint32_t before, uint8_t category,
                     uint8_t severity, uint8_t pending, fl_record *record);
void XoraLog_Fatal(uint32_t reason);
/* TX management wire: INFO has no request and returns fl_info; READ uses
 * upper:u32, before:u32, category:u8, severity:u8, pending:u8 (1-based),
 * offset:u8. Response echoes offset and contains up to 48 record bytes.
 * Status is carried by the existing transaction header. */
int XoraLog_Control(const uint8_t *request, uint8_t length, uint8_t *response, uint8_t *size);
#ifdef __cplusplus
}
#endif
#endif
