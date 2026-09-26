#ifndef HBOX_BOARD_SECURITY_CONFIRMATION_H
#define HBOX_BOARD_SECURITY_CONFIRMATION_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Poll the raw, board-owned confirmation inputs from the WebConfig main loop.
 * The implementation requires the physical USB position, the CH585
 * maintenance role, a released-to-armed transition, and a two-second
 * GPIO1+FN hold before producing one short-lived, one-shot authorization.
 */
void HBoxBoardSecurityConfirmation_Poll(void);
void HBoxBoardSecurityConfirmation_Reset(void);

/* Consume the pending physical authorization, if one exists. */
bool HBoxBoard_DangerousActionConfirmed(void);

#ifdef __cplusplus
}
#endif

#endif /* HBOX_BOARD_SECURITY_CONFIRMATION_H */
