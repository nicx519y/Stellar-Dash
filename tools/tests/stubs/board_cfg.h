#ifndef HBOX_TEST_BOARD_CFG_H
#define HBOX_TEST_BOARD_CFG_H

/* Host-side UsbBoardLink tests only require the role handshake timeout. */
#define CH585_ROLE_RESPONSE_TIMEOUT_MS 20u

static inline void hbox_test_app_stage_error(const char *stage,
                                             const char *format,
                                             ...)
{
    (void)stage;
    (void)format;
}

#define APP_STAGE_ERROR(...) hbox_test_app_stage_error(__VA_ARGS__)

#endif
