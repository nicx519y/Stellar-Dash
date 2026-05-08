#ifndef RF_BRIDGE_PORT_HPP
#define RF_BRIDGE_PORT_HPP

#include <stdint.h>

// Stable SPI bridge port API (STM32 side).
// Upper-layer RF transport should only use this API.
bool RFBridgePort_Transfer(const uint8_t* tx, uint16_t txLen, uint8_t* rx, uint16_t* rxLen);

#endif
