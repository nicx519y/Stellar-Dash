#include "rf_transport.hpp"

#include <string.h>

extern "C" bool __attribute__((weak)) RF_Bridge_Transfer(const uint8_t* tx, uint16_t txLen, uint8_t* rx, uint16_t* rxLen) {
    (void)tx;
    (void)txLen;
    if (rxLen) *rxLen = 0;
    (void)rx;
    return false;
}

namespace {
static constexpr uint8_t RF_SYNC = 0xA5u;
static constexpr uint8_t CMD_GET_STATUS = 0x01u;
static constexpr uint8_t CMD_SET_RATE = 0x05u;
static constexpr uint8_t CMD_INPUT_DATA = 0x06u;
static constexpr uint8_t INPUT_PAYLOAD_LEN = 15u;
}

bool RFTransport::transferCommand(uint8_t cmd, const uint8_t* payload, uint8_t len) {
    if (len > 24u) {
        state = RFTransportState::Error;
        return false;
    }

    uint8_t frame[4u + 24u + 1u] = {0};
    frame[0] = RF_SYNC;
    frame[1] = cmd;
    frame[2] = len;
    if (len > 0u && payload != nullptr) {
        memcpy(&frame[3], payload, len);
    }

    uint8_t checksum = 0u;
    const uint8_t totalNoChecksum = (uint8_t)(3u + len);
    for (uint8_t i = 0; i < totalNoChecksum; i++) {
        checksum = (uint8_t)(checksum + frame[i]);
    }
    frame[totalNoChecksum] = checksum;

    uint8_t rxBuf[32] = {0};
    uint16_t rxLen = sizeof(rxBuf);
    bool ok = RF_Bridge_Transfer(frame, (uint16_t)(totalNoChecksum + 1u), rxBuf, &rxLen);
    state = ok ? RFTransportState::Connected : RFTransportState::Error;
    return ok;
}

uint8_t RFTransport::encodeDpad(uint8_t dpad) {
    const bool up = (dpad & GAMEPAD_MASK_UP) != 0u;
    const bool down = (dpad & GAMEPAD_MASK_DOWN) != 0u;
    const bool left = (dpad & GAMEPAD_MASK_LEFT) != 0u;
    const bool right = (dpad & GAMEPAD_MASK_RIGHT) != 0u;

    if (up && right) return 2u;
    if (right && down) return 4u;
    if (down && left) return 6u;
    if (left && up) return 8u;
    if (up) return 1u;
    if (right) return 3u;
    if (down) return 5u;
    if (left) return 7u;
    return 0u;
}

bool RFTransport::begin() {
    return transferCommand(CMD_GET_STATUS, nullptr, 0u);
}

bool RFTransport::setRate(uint16_t rateHz) {
    uint8_t payload[2] = {
        (uint8_t)(rateHz & 0xFFu),
        (uint8_t)((rateHz >> 8) & 0xFFu),
    };
    return transferCommand(CMD_SET_RATE, payload, sizeof(payload));
}

bool RFTransport::sendInput(const GamepadState& gamepad) {
    uint8_t payload[INPUT_PAYLOAD_LEN] = {0};
    payload[0] = seq++;
    payload[1] = 0u;

    uint16_t buttons = (uint16_t)(gamepad.buttons & 0xFFFFu);
    payload[2] = (uint8_t)(buttons & 0xFFu);
    payload[3] = (uint8_t)((buttons >> 8) & 0xFFu);
    payload[4] = encodeDpad(gamepad.dpad);
    payload[5] = gamepad.lt;
    payload[6] = gamepad.rt;

    int16_t lx = (int16_t)((int32_t)gamepad.lx - 32768);
    int16_t ly = (int16_t)((int32_t)gamepad.ly - 32768);
    int16_t rx = (int16_t)((int32_t)gamepad.rx - 32768);
    int16_t ry = (int16_t)((int32_t)gamepad.ry - 32768);

    payload[7] = (uint8_t)(lx & 0xFF);
    payload[8] = (uint8_t)((lx >> 8) & 0xFF);
    payload[9] = (uint8_t)(ly & 0xFF);
    payload[10] = (uint8_t)((ly >> 8) & 0xFF);
    payload[11] = (uint8_t)(rx & 0xFF);
    payload[12] = (uint8_t)((rx >> 8) & 0xFF);
    payload[13] = (uint8_t)(ry & 0xFF);
    payload[14] = (uint8_t)((ry >> 8) & 0xFF);

    return transferCommand(CMD_INPUT_DATA, payload, sizeof(payload));
}
