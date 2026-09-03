#ifndef RF_COMMAND_TRANSACTION_HPP
#define RF_COMMAND_TRANSACTION_HPP

#include <stdint.h>

struct RFCommandTransactionResult {
    uint8_t txn = 0u;
    uint8_t attempts = 0u;
    uint8_t ackFrame[32] = {0};
    uint16_t ackLen = 0u;
};

class RFCommandTransaction {
public:
    static bool send(uint8_t cmd,
                     const uint8_t* args,
                     uint8_t argsLen,
                     RFCommandTransactionResult& result);
    static bool sendScheduled(uint8_t cmd,
                              const uint8_t* args,
                              uint8_t argsLen,
                              RFCommandTransactionResult& result);

private:
    static uint8_t nextTransactionId();
};

#endif
