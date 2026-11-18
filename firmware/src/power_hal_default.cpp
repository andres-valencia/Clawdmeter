#include "hal/power_hal.h"

// Boards whose PMU can't cut its own power: no software power off, so PWR
// keeps acting on the first press (pwr_taps.h). The AXP2101 boards override
// these (boards/common/axp2101_power.inc).
__attribute__((weak)) bool power_hal_can_shutdown(void) { return false; }
__attribute__((weak)) void power_hal_shutdown(void) {}
