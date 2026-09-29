#ifndef HBOX_FACTORY_IDENTITY_SERVICE_H
#define HBOX_FACTORY_IDENTITY_SERVICE_H

#include "factory_identity_enrollment.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Supplied by the reviewed factory-only transport source selected with
 * HBOX_FACTORY_IDENTITY_SERVICE_SOURCE.  There is deliberately no in-tree
 * fallback.  The service must authenticate an anti-replay factory session
 * before its separate gate source returns authorized.
 *
 * Returning is allowed: main() then follows the normal fail-closed secure
 * boot path.  An unprovisioned or partially provisioned device will not jump
 * to the application.
 */
void HBoxFactoryIdentityService_Run(
    const hbox_factory_enrollment_api_v1_t *api);

#ifdef __cplusplus
}
#endif

#endif /* HBOX_FACTORY_IDENTITY_SERVICE_H */
