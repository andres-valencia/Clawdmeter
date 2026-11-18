#pragma once

// Power / battery / power-button abstraction.
//
// Some boards (AMOLED-2.16) wire PWR through the PMU's PKEY IRQ; others
// (AMOLED-1.8) route it through an IO expander. The HAL hides which
// source produced the press — shared code just polls
// power_hal_pwr_pressed() once per loop.

void power_hal_init(void);
void power_hal_tick(void);

int  power_hal_battery_pct(void);  // 0..100, or -1 if no battery (see BoardCaps.has_battery)
bool power_hal_is_charging(void);
bool power_hal_is_vbus_in(void);   // USB cable present (true even without a battery)

// Edge-triggered: returns true once per PWR short-press, then clears.
bool power_hal_pwr_pressed(void);

// Edge-triggered: true once when a PWR hold crosses the long-press threshold
// (~1.5s), then clears. Starts the hold-to-pair gesture.
bool power_hal_pwr_long_pressed(void);

// Edge-triggered: true once on the PWR release edge, then clears. Completes
// or cancels the hold-to-pair gesture.
bool power_hal_pwr_released(void);

// This fork: the PMU can cut the board's power on request (a PWR double
// press, pwr_taps.h). Optional: weak defaults in power_hal_default.cpp say
// no; the AXP2101 boards say yes.
bool power_hal_can_shutdown(void);
// Power the board off now (the PWR button turns it back on). Pairing,
// tokens and settings in NVS are kept.
void power_hal_shutdown(void);
