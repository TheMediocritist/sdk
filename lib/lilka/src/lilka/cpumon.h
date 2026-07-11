#ifndef LILKA_CPUMON_H
#define LILKA_CPUMON_H

#include <stdint.h>

namespace lilka {

/// Per-core CPU load monitor based on idle-task hooks. Load = how much of
/// each second the core did NOT spend idle, relative to a calibrated
/// idle-only baseline.
class CpuMon {
public:
    /// Register the idle hooks. Call once, early (lilka::begin is a good
    /// place), ideally before heavy tasks start so calibration is honest.
    void begin();

    /// Load per core, 0..100. Updated once per second.
    uint8_t getLoad(int core) const {
        return core >= 0 && core < 2 ? load[core] : 0;
    }

private:
    friend bool cpumonIdleHook0();
    friend bool cpumonIdleHook1();
    static void statsTask(void* arg);

    volatile uint32_t idleCount[2] = {0, 0};
    uint32_t idleMax[2] = {1, 1}; // calibrated ceiling (auto-adapts)
    volatile uint8_t load[2] = {0, 0};
};

extern CpuMon cpumon;

} // namespace lilka

#endif // LILKA_CPUMON_H