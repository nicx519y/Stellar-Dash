#ifndef CH585_UPDATE_MODE_HPP
#define CH585_UPDATE_MODE_HPP

class Ch585UpdateMode {
public:
    Ch585UpdateMode(const Ch585UpdateMode&) = delete;
    Ch585UpdateMode& operator=(const Ch585UpdateMode&) = delete;

    static Ch585UpdateMode& getInstance()
    {
        static Ch585UpdateMode instance;
        return instance;
    }

    bool isManualIspActive() const;
    bool isManualIspPowered() const;
    bool isIapConfirmed() const;
    bool isManualEntryVisible() const;
    bool requestManualIsp();
    bool requestExitManualIsp();
    bool setIapConfirmed(bool confirmed);
    void setupManualIspRuntime();
    void shutdownManualIspRuntime();
    bool powerOnManualIsp();
    void powerOffManualIsp();

private:
    Ch585UpdateMode() = default;

    bool manualIspPowered = false;
};

#define CH585_UPDATE_MODE Ch585UpdateMode::getInstance()

#endif
