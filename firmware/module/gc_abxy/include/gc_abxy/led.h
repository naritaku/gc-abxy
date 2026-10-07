#pragma once
#include <stdint.h>

enum gc_led_pattern {
    GC_LED_OFF = 0,
    GC_LED_BOOT_BLUE,      /* blink blue N times (N = repeat): selected profile is paired */
    GC_LED_BOOT_WHITE,     /* blink white N times: selected profile is free, ready to pair */
    GC_LED_BOOT_YELLOW,    /* blink yellow N times: battery low (overrides blue / white) */
    GC_LED_RAINBOW_ONCE,   /* red-yellow-green-cyan-blue-magenta at 84 ms, 2 revolutions (~1 s) */
    GC_LED_RED_BLINK,      /* continuous, management warning */
    GC_LED_RED_SOLID,      /* bond clear armed */
    GC_LED_RAINBOW_LOOP,   /* factory reset armed */
    GC_LED_DIAG_PENDING,   /* continuous magenta blink: diagnostics waiting for PROFILE + A held 3 s */
    GC_LED_DIAG_ON,        /* continuous: one short magenta flash every 5 s while diagnostics records */
};

/* Start a pattern. repeat is only used by the boot patterns. Continuous patterns run until gc_led_set(GC_LED_OFF). */
void gc_led_set(enum gc_led_pattern pattern, uint8_t repeat);

/* Blink the selected profile number: blue = paired, white = free for pairing, yellow = battery low.
 * Used at boot and after every profile switch (SR-LED-02 / SR-LED-03). */
void gc_led_show_profile(void);

/* Diagnostics LED test: light one colour (bit0 = R, bit1 = G, bit2 = B) for `seconds`, then off. */
void gc_led_test(uint8_t rgb, uint8_t seconds);
