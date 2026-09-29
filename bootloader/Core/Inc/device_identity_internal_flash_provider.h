#ifndef HBOX_DEVICE_IDENTITY_INTERNAL_FLASH_PROVIDER_H
#define HBOX_DEVICE_IDENTITY_INTERNAL_FLASH_PROVIDER_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Factory integration hook.
 *
 * The repository weak implementation always denies.  A reviewed factory
 * service may override it only after authenticating the fixture/session.
 * The provider additionally checks the MCU's RDP1 and Secure-mode status
 * before every write. Returning true is authority to consume previously
 * erased flashwords; it is never authority to erase the internal sector.
 */
int HBoxIdentityFactoryGate_IsAuthorized(void);

#ifdef __cplusplus
}
#endif

#endif /* HBOX_DEVICE_IDENTITY_INTERNAL_FLASH_PROVIDER_H */
