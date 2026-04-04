#ifndef SPI_SCREEN_MANAGER_HPP
#define SPI_SCREEN_MANAGER_HPP

#include <stdint.h>

class SPIScreenManager {
public:
    SPIScreenManager(SPIScreenManager const&) = delete;
    void operator=(SPIScreenManager const&) = delete;
    static SPIScreenManager& getInstance() {
        static SPIScreenManager instance;
        return instance;
    }

    void setup();
    void loop();

    bool menuPrev();
    bool menuNext();

private:
    SPIScreenManager() = default;

    void rebuildMenu();
    void handleInput(uint32_t nowMs);
    void renderFrame();
    void renderBars();
    void beginAnimation(int dir);

    uint8_t menuCount = 0;
    uint8_t menuIndex = 0;
    uint8_t menuIds[32] = {0};

    bool animActive = false;
    uint32_t animStartMs = 0;
    int animDir = 0;
};

#endif
