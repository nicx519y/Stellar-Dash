#ifndef HBOX_FIRMWARE_MANAGER_TEST_BOARD_CFG_H
#define HBOX_FIRMWARE_MANAGER_TEST_BOARD_CFG_H

/*
 * Host-only logging and lifecycle definitions for compiling the production
 * FirmwareManager implementation.  The reliability test never exercises any
 * MCU protection or hardware lifecycle operation.
 */
#define APP_DBG(...) ((void)0)
#define APP_ERR(...) ((void)0)
#define HBOX_SECURE_BOOT_REQUIRED 0

#endif
