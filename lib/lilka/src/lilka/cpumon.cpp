#include "cpumon.h"

#include <Arduino.h>
#include <esp_freertos_hooks.h>

namespace lilka {

CpuMon cpumon;

bool cpumonIdleHook0() {
    cpumon.idleCount[0]++;
    return true; // let the idle task continue (feeds the task WDT)
}

bool cpumonIdleHook1() {
    cpumon.idleCount[1]++;
    return true;
}

void CpuMon::statsTask(void* arg) {
    CpuMon* self = static_cast<CpuMon*>(arg);
    while (true) {
        vTaskDelay(self->periodMs / portTICK_PERIOD_MS);
        for (int core = 0; core < 2; core++) {
            uint32_t count = self->idleCount[core];
            self->idleCount[core] = 0;

            // Sliding sum of idle counts over the last WINDOW_MS.
            self->windowSum[core] -= self->samples[core][self->head];
            self->samples[core][self->head] = count;
            self->windowSum[core] += count;

            if (self->filled + 1 < self->sampleCount) continue; // window not full yet

            // Auto-calibrate: the largest 1-second sum ever seen is "100% idle".
            if (self->windowSum[core] > self->idleMax[core]) {
                self->idleMax[core] = self->windowSum[core];
            }
            uint32_t idlePct = (uint32_t)((self->windowSum[core] * 100) / self->idleMax[core]);
            self->load[core] = (uint8_t)(100 - idlePct);
        }
        self->head = (self->head + 1) % self->sampleCount;
        if (self->filled < self->sampleCount) self->filled++;
    }
}

void CpuMon::begin(uint32_t periodMs_) {
    periodMs = periodMs_;
    sampleCount = WINDOW_MS / periodMs;
    if (sampleCount < 1) sampleCount = 1;
    if (sampleCount > MAX_SAMPLES) sampleCount = MAX_SAMPLES;

    esp_register_freertos_idle_hook_for_cpu(cpumonIdleHook0, 0);
    esp_register_freertos_idle_hook_for_cpu(cpumonIdleHook1, 1);
    xTaskCreatePinnedToCore(statsTask, "cpumon", 2048, this, 1, NULL, 0);
}

} // namespace lilka