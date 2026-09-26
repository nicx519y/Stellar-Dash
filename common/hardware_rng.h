#ifndef HBOX_HARDWARE_RNG_H
#define HBOX_HARDWARE_RNG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int HBoxHardwareRng_Init(void);
int HBoxHardwareRng_Fill(void *context,
                         unsigned char *output,
                         size_t length);
void HBoxHardwareRng_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* HBOX_HARDWARE_RNG_H */
