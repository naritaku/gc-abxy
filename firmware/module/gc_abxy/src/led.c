/*
 * gc-abxy status LED manager: drives the XIAO nRF52840 RGB LED (gpio-leds led0=red, led1=green, led2=blue, active low)
 * with the patterns required by requirements SR-LED-01..06. Normally off (SR-LED-01, SR-LED-06).
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/ble.h>
#include <zmk/battery.h>

#include <gc_abxy/led.h>

LOG_MODULE_REGISTER(gc_led, CONFIG_ZMK_LOG_LEVEL);

static const struct gpio_dt_spec led_r = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_g = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led_b = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

struct step {
    uint8_t rgb; /* bit0 = R, bit1 = G, bit2 = B */
    uint16_t ms;
};

/* colour bits; not R/G/B, which Zephyr/nRF headers already define */
#define LED_R 1
#define LED_G 2
#define LED_B 4

static const struct step blink_blue[] = {{LED_B, 200}, {0, 250}};
static const struct step blink_white[] = {{LED_R | LED_G | LED_B, 200}, {0, 250}};
static const struct step blink_yellow[] = {{LED_R | LED_G, 200}, {0, 250}};
/*
 * Hue wheel. The XIAO's LED is three plain GPIOs (no PWM), so the reachable colours are the 6 corners of the
 * RGB cube with at least one channel on: red -> yellow -> green -> cyan -> blue -> magenta. Going through the
 * three mixed colours instead of R/G/B only reads as a rainbow rather than three separate blinks.
 * 6 x 84 ms = 504 ms per revolution; GC_LED_RAINBOW_ONCE runs 2 of them (~1.0 s) to keep SR-LED-04.
 */
#define T_HUE_MS 84
static const struct step rainbow[] = {
    {LED_R,     T_HUE_MS},  /* red */
    {LED_R | LED_G, T_HUE_MS},  /* yellow */
    {LED_G,     T_HUE_MS},  /* green */
    {LED_G | LED_B, T_HUE_MS},  /* cyan */
    {LED_B,     T_HUE_MS},  /* blue */
    {LED_R | LED_B, T_HUE_MS},  /* magenta */
};
#define RAINBOW_ONCE_CYCLES 2
static const struct step red_blink[] = {{LED_R, 150}, {0, 350}};
static const struct step red_solid[] = {{LED_R, 1000}};
static const struct step diag_pending[] = {{LED_R | LED_B, 150}, {0, 350}};
static const struct step diag_on[] = {{LED_R | LED_B, 60}, {0, 4940}};
static struct step test_step;

static struct {
    const struct step *steps;
    uint8_t n_steps;
    uint8_t idx;
    int16_t cycles_left; /* -1 = forever */
    enum gc_led_pattern pattern;
} run;

static struct k_work_delayable led_work;
static bool rainbow_pending; /* connected while a number blink was running: show the rainbow right after it */

/* a continuous (management) pattern owns the LED until the state machine turns it off */
static bool continuous(void) { return run.steps != NULL && run.cycles_left == -1; }

static void set_rgb(uint8_t rgb) {
    gpio_pin_set_dt(&led_r, (rgb & LED_R) ? 1 : 0);
    gpio_pin_set_dt(&led_g, (rgb & LED_G) ? 1 : 0);
    gpio_pin_set_dt(&led_b, (rgb & LED_B) ? 1 : 0);
}

static void led_work_cb(struct k_work *work) {
    if (run.steps == NULL) {
        set_rgb(0);
        return;
    }
    const struct step *s = &run.steps[run.idx];
    set_rgb(s->rgb);
    k_work_reschedule(&led_work, K_MSEC(s->ms));
    run.idx++;
    if (run.idx >= run.n_steps) {
        run.idx = 0;
        if (run.cycles_left > 0) {
            run.cycles_left--;
        }
        if (run.cycles_left == 0) {
            run.steps = NULL; /* next callback turns the LED off */
            if (rainbow_pending) {
                rainbow_pending = false;
                run.steps = rainbow; run.n_steps = ARRAY_SIZE(rainbow); run.cycles_left = RAINBOW_ONCE_CYCLES;
                run.pattern = GC_LED_RAINBOW_ONCE;
            }
        }
    }
}

void gc_led_test(uint8_t rgb, uint8_t seconds) {
    k_work_cancel_delayable(&led_work);
    rainbow_pending = false;
    test_step = (struct step){.rgb = rgb & (LED_R | LED_G | LED_B), .ms = (uint16_t)(seconds ? seconds : 1) * 1000};
    run.steps = &test_step; run.n_steps = 1; run.idx = 0; run.cycles_left = 1; run.pattern = GC_LED_OFF;
    k_work_reschedule(&led_work, K_NO_WAIT);
}

void gc_led_set(enum gc_led_pattern pattern, uint8_t repeat) {
    k_work_cancel_delayable(&led_work);
    run.idx = 0;
    if (pattern != GC_LED_RAINBOW_ONCE) {
        rainbow_pending = false;
    }
    run.pattern = pattern;
    switch (pattern) {
    case GC_LED_BOOT_BLUE:
        run.steps = blink_blue; run.n_steps = ARRAY_SIZE(blink_blue); run.cycles_left = repeat ? repeat : 1; break;
    case GC_LED_BOOT_WHITE:
        run.steps = blink_white; run.n_steps = ARRAY_SIZE(blink_white); run.cycles_left = repeat ? repeat : 1; break;
    case GC_LED_BOOT_YELLOW:
        run.steps = blink_yellow; run.n_steps = ARRAY_SIZE(blink_yellow); run.cycles_left = repeat ? repeat : 1; break;
    case GC_LED_RAINBOW_ONCE:
        run.steps = rainbow; run.n_steps = ARRAY_SIZE(rainbow); run.cycles_left = RAINBOW_ONCE_CYCLES; break;
    case GC_LED_RED_BLINK:
        run.steps = red_blink; run.n_steps = ARRAY_SIZE(red_blink); run.cycles_left = -1; break;
    case GC_LED_RED_SOLID:
        run.steps = red_solid; run.n_steps = ARRAY_SIZE(red_solid); run.cycles_left = -1; break;
    case GC_LED_RAINBOW_LOOP:
        run.steps = rainbow; run.n_steps = ARRAY_SIZE(rainbow); run.cycles_left = -1; break;
    case GC_LED_DIAG_PENDING:
        run.steps = diag_pending; run.n_steps = ARRAY_SIZE(diag_pending); run.cycles_left = -1; break;
    case GC_LED_DIAG_ON:
        run.steps = diag_on; run.n_steps = ARRAY_SIZE(diag_on); run.cycles_left = -1; break;
    case GC_LED_OFF:
    default:
        /* cycles_left must not stay -1 here, or continuous() would keep blocking the connection rainbow and the
         * low-battery reminder for the rest of the power cycle after any long press */
        run.steps = NULL; run.cycles_left = 0; set_rgb(0); return;
    }
    k_work_reschedule(&led_work, K_NO_WAIT);
}

/* ---- profile number (SR-LED-02 / SR-LED-03): at boot and after each profile switch ---- */
static bool boot_shown;
static bool was_connected;

void gc_led_show_profile(void) {
    uint8_t n = zmk_ble_active_profile_index() + 1;
    uint8_t soc = zmk_battery_state_of_charge();
    bool low = (soc > 0 && soc <= CONFIG_GC_ABXY_BATTERY_LOW_PCT);
    bool open = zmk_ble_active_profile_is_open();
    LOG_INF("profile %d (%s), battery %d%% (%s)", n, open ? "free" : "paired", soc, low ? "LOW" : "ok");
    gc_led_set(low ? GC_LED_BOOT_YELLOW : open ? GC_LED_BOOT_WHITE : GC_LED_BOOT_BLUE, n);
}

static void boot_blink_cb(struct k_work *work) {
    if (boot_shown) {
        return;
    }
    boot_shown = true;
    gc_led_show_profile();
}
static K_WORK_DELAYABLE_DEFINE(boot_blink_work, boot_blink_cb);

/* ---- low-battery notification during operation (SR-BAT-04): yellow blinks when the level first drops to the
 * threshold, then once an hour while it stays low. Boot already shows yellow if low. ---- */
static bool batt_low;
static void low_batt_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(low_batt_work, low_batt_cb);
static void low_batt_cb(struct k_work *work) {
    if (!batt_low) {
        return;
    }
    if (!continuous()) { /* do not interrupt a management-mode pattern */
        gc_led_set(GC_LED_BOOT_YELLOW, zmk_ble_active_profile_index() + 1);
    }
    k_work_schedule(&low_batt_work, K_MINUTES(60));
}

static int led_listener(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *bev = as_zmk_battery_state_changed(eh);
    if (bev) {
        bool low = (bev->state_of_charge > 0 && bev->state_of_charge <= CONFIG_GC_ABXY_BATTERY_LOW_PCT);
        if (low && !batt_low) {
            LOG_WRN("battery low: %d%%", bev->state_of_charge);
            batt_low = true;
            if (boot_shown) {
                k_work_schedule(&low_batt_work, K_NO_WAIT);
            }
        } else if (!low && batt_low && bev->state_of_charge > CONFIG_GC_ABXY_BATTERY_LOW_PCT + 5) {
            batt_low = false; /* hysteresis: fresh cell inserted */
            k_work_cancel_delayable(&low_batt_work);
        }
        return ZMK_EV_EVENT_BUBBLE;
    }
    const struct zmk_ble_active_profile_changed *pev = as_zmk_ble_active_profile_changed(eh);
    if (pev) {
        bool connected = zmk_ble_active_profile_is_connected();
        if (connected && !was_connected && boot_shown && !continuous()) {
            /* SR-LED-04: ~1 s rainbow on connection. A profile number that is still blinking finishes first. */
            if (run.steps != NULL) {
                rainbow_pending = true;
            } else {
                gc_led_set(GC_LED_RAINBOW_ONCE, 1);
            }
        }
        was_connected = connected;
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(gc_led, led_listener);
ZMK_SUBSCRIPTION(gc_led, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(gc_led, zmk_battery_state_changed);

static int led_init(void) {
    gpio_pin_configure_dt(&led_r, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_g, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_b, GPIO_OUTPUT_INACTIVE);
    k_work_init_delayable(&led_work, led_work_cb);
    /* wait for the first battery sample (ZMK samples at init + every 60 s) and BLE init before blinking */
    k_work_schedule(&boot_blink_work, K_MSEC(1500));
    return 0;
}
SYS_INIT(led_init, APPLICATION, 90);
