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
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        for (int core = 0; core < 2; core++) {
            uint32_t count = self->idleCount[core];
            self->idleCount[core] = 0;
            // Auto-calibrate: the largest count ever seen is "100% idle".
            if (count > self->idleMax[core]) self->idleMax[core] = count;
            uint32_t idlePct = (count * 100) / self->idleMax[core];
            self->load[core] = idlePct > 100 ? 0 : 100 - idlePct;
        }
    }
}

void CpuMon::begin() {
    esp_register_freertos_idle_hook_for_cpu(cpumonIdleHook0, 0);
    esp_register_freertos_idle_hook_for_cpu(cpumonIdleHook1, 1);
    xTaskCreatePinnedToCore(statsTask, "cpumon", 2048, this, 1, NULL, 0);
}

} // namespace lilka