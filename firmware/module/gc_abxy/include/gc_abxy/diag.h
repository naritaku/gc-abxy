/* Diagnostics: hooks from mgmt.c into diag.c.
 * All are no-ops unless CONFIG_GC_ABXY_DIAG. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define GC_DIAG_INPUTS 5 /* 0..3 = A, B, X, Y (keymap positions), 4 = PROFILE */
#define GC_DIAG_PROFILE 4

#if IS_ENABLED(CONFIG_GC_ABXY_DIAG)
/* Raw input edge: A/B/X/Y before PROFILE swallows them, PROFILE after debounce. */
void gc_diag_input(uint8_t input, bool down);
/* Management state machine moved (values of enum state in mgmt.c). */
void gc_diag_mgmt_state(uint8_t now, uint8_t prev);
/* Diagnostics pending or on: PROFILE must not switch, clear bonds or reset. */
bool gc_diag_active(void);
#else
static inline void gc_diag_input(uint8_t input, bool down) {}
static inline void gc_diag_mgmt_state(uint8_t now, uint8_t prev) {}
static inline bool gc_diag_active(void) { return false; }
#endif

/* Provided by mgmt.c */
uint8_t gc_mgmt_state(void);
/* Diagnostics started: drop a PROFILE hold in progress so its release does nothing. */
void gc_mgmt_cancel_hold(void);
