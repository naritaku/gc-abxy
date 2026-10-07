/* Read-only view of the CR2450 gauge for the debug GATT service (debug_svc.c). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

struct gc_battery_debug {
    uint16_t sample_mv;   /* last ADC sample converted to cell volts (divider undone); 0 before the first sample */
    uint16_t est_mv;      /* filtered estimate that drives the reported level */
    uint16_t adc_raw;     /* raw SAADC code of the last sample */
    uint8_t soc;          /* level reported to the host (0 = no valid reading yet) */
    bool valid;           /* a reading >= 1.8 V has been taken since boot */
    uint32_t age_ms;      /* time since the last sample */
};

void gc_battery_debug_get(struct gc_battery_debug *out);
