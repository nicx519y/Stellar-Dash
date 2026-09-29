#pragma once

#include "qspi-w25q64.h"

// Install only around an ordinary WebHID configuration handler. All exits,
// including rejected saves, restore the previous policy before other work.
class QspiWaitScope {
public:
    explicit QspiWaitScope(QSPI_W25Qxx_WaitCallback callback)
        : previous_(QSPI_W25Qxx_SetWaitCallback(callback)) {}
    ~QspiWaitScope() { QSPI_W25Qxx_SetWaitCallback(previous_); }
    QspiWaitScope(const QspiWaitScope&) = delete;
    QspiWaitScope& operator=(const QspiWaitScope&) = delete;
private:
    QSPI_W25Qxx_WaitCallback previous_;
};
