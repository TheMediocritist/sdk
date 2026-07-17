#ifndef LILKA_CPUMON_H
#define LILKA_CPUMON_H

#include <stddef.h>
#include <stdint.h>

namespace lilka {

/// Per-core CPU load monitor based on idle-task hooks. Load = fraction of
/// the last second the core did NOT spend idle, relative to a calibrated
/// idle-only baseline.
class CpuMon {
public:
    /// Register the idle hooks and start sampling every periodMs milliseconds.
    /// Call once, early (lilka::begin is a good place), ideally before heavy
    /// tasks start so calibration is honest.
    void begin(uint32_t periodMs = 100);

    /// Load per core, 0..100, over a rolling WINDOW_MS window, updated every
    /// sampling period. Reads 0 until the first full window has elapsed.
    uint8_t getLoad(int core) const {
        return core >= 0 && core < 2 ? load[core] : 0;
    }

private:
    friend bool cpumonIdleHook0();
    friend bool cpumonIdleHook1();
    static void statsTask(void* arg);

    static constexpr uint32_t WINDOW_MS = 1000;
    static constexpr size_t MAX_SAMPLES = 100; // supports periods down to 10 ms

    uint32_t periodMs = 100;
    size_t sampleCount = 10;

    volatile uint32_t idleCount[2] = {0, 0};
    uint64_t windowSum[2] = {0, 0};
    uint64_t idleMax[2] = {1, 1}; // calibrated ceiling (auto-adapts)
    volatile uint8_t load[2] = {0, 0};

    uint32_t samples[2][MAX_SAMPLES] = {};
    size_t head = 0;
    size_t filled = 0;
};

extern CpuMon cpumon;

} // namespace lilka

#endif // LILKA_CPUMON_H