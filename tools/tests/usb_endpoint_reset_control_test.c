#include <stdint.h>
#include <stdio.h>

#include "usb_endpoint_reset_control.h"

#define TEST_DONE        0x80u
#define TEST_TOGGLE_MATCH 0x10u
#define TEST_DATA1       0x04u
#define TEST_RESPONSE_ACK 0x02u
#define TEST_RESPONSE_NAK 0x00u

static int expect_control(
    const char *name,
    uint8_t actual,
    uint8_t expected)
{
    if(actual == expected)
    {
        return 0;
    }

    fprintf(
        stderr,
        "%s: got 0x%02x, expected 0x%02x\n",
        name,
        actual,
        expected);
    return 1;
}

int main(void)
{
    int failures = 0;

    failures += expect_control(
        "pending IN DATA0 advances to DATA1",
        usb_endpoint_reset_control(
            TEST_DONE, TEST_DONE, 0u, TEST_DATA1, TEST_RESPONSE_NAK),
        TEST_DATA1 | TEST_RESPONSE_NAK);
    failures += expect_control(
        "pending IN DATA1 advances to DATA0",
        usb_endpoint_reset_control(
            TEST_DONE | TEST_DATA1,
            TEST_DONE,
            0u,
            TEST_DATA1,
            TEST_RESPONSE_NAK),
        TEST_RESPONSE_NAK);
    failures += expect_control(
        "idle IN preserves DATA1",
        usb_endpoint_reset_control(
            TEST_DATA1,
            TEST_DONE,
            0u,
            TEST_DATA1,
            TEST_RESPONSE_NAK),
        TEST_DATA1 | TEST_RESPONSE_NAK);
    failures += expect_control(
        "pending OUT advances and reopens ACK",
        usb_endpoint_reset_control(
            TEST_DONE | TEST_TOGGLE_MATCH,
            TEST_DONE,
            TEST_TOGGLE_MATCH,
            TEST_DATA1,
            TEST_RESPONSE_ACK),
        TEST_DATA1 | TEST_RESPONSE_ACK);
    failures += expect_control(
        "duplicate OUT does not advance",
        usb_endpoint_reset_control(
            TEST_DONE,
            TEST_DONE,
            TEST_TOGGLE_MATCH,
            TEST_DATA1,
            TEST_RESPONSE_ACK),
        TEST_RESPONSE_ACK);
    failures += expect_control(
        "reset strips stale status bits",
        usb_endpoint_reset_control(
            0x7Fu,
            TEST_DONE,
            TEST_TOGGLE_MATCH,
            TEST_DATA1,
            TEST_RESPONSE_ACK),
        TEST_DATA1 | TEST_RESPONSE_ACK);

    if(failures != 0)
    {
        return 1;
    }

    puts("usb_endpoint_reset_control_test: PASS");
    return 0;
}
