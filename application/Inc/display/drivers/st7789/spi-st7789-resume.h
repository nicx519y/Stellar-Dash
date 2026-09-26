#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void SPIST7789_BeginResume(uint32_t now);
int SPIST7789_PollResume(uint32_t now);
void SPIST7789_CancelResume(void);
bool SPIST7789_ResumeActive(void);
// Driver-private electrical hooks. Host tests substitute only these operations.
bool SPIST7789_ResumePrepare(void);
bool SPIST7789_ResumeWrite(uint8_t command, const uint8_t* data, uint16_t size);
void SPIST7789_ResumePowerOn(void);
void SPIST7789_ResumeReady(void);
bool SPIST7789_IsReady(void);
void SPIST7789_DeInit(void);
#ifdef __cplusplus
}
#endif
