/*
 * gc-abxy rear PROFILE button (requirements SR-CTL-01..12, SR-LED-02, SR-LED-05).
 *
 * The button is read here from its own GPIO (devicetree gc,profile-button), not through kscan, so it is not a keymap
 * position: ZMK Studio shows only A/B/X/Y and nobody can remap the button that switches profiles and resets the
 * device (SR-CTL-12).
 *
 *  press ................ start timer; release every sent report and swallow A/B/X/Y from now on (SR-CTL-02)
 *  release < 5 s ........ next BLE profile (1 -> 2 -> 3 -> 1), then blink its number (SR-CTL-01, SR-LED-02)
 *
 *  PROFILE alone
 *   5 s ................. warning: red blink (SR-LED-05)
 *   release 5-8 s ....... cancel, nothing destructive (SR-CTL-03)
 *   8 s ................. bond clear armed: red solid (SR-CTL-04)
 *   release >= 8 s ...... clear the current profile's bond (SR-CTL-05)
 *
 *  PROFILE with X + Y (in any order, at any time during the hold) = factory reset course (SR-CTL-06..08)
 *   5 s ................. red blink
 *   10 s ................ factory reset armed: rainbow
 *   release PROFILE ..... factory reset if armed; before that, cancel
 *   release X or Y ...... cancel; the rest of the hold does nothing
 *   A failed factory reset therefore never turns into a bond clear.
 *
 *  A/B/X/Y pressed while PROFILE is held stay swallowed until they are physically released (SR-CTL-10).
 *
 *  USB HID active: the whole button is inhibited (SR-CTL-11). While wired, the output does not follow the BLE
 *  profile, so a switch or a bond clear there would change a setting the user cannot see, and they would lose
 *  track of which profile is selected. A hold that sees USB become active is cancelled. Bench-test PROFILE on
 *  charger or battery power, not on a PC: ZMK selects the USB endpoint whenever USB is enumerated.
 *
 * Swallowing A/B/X/Y relies on this module's zmk_position_state_changed listener running before ZMK's keymap
 * listener. ZMK calls listeners in link order and module sources are linked before ZMK's own, so this holds; the
 * firmware workflow checks the symbol order in zmk.elf and fails the build if it ever changes.
 */
#define DT_DRV_COMPAT gc_profile_button

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log.h>

#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/endpoint_changed.h>

#include <gc_abxy/diag.h>
#include <gc_abxy/led.h>

LOG_MODULE_REGISTER(gc_mgmt, CONFIG_ZMK_LOG_LEVEL);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1, "exactly one gc,profile-button node is required");

#define T_MGMT_MS 5000
#define T_BOND_MS 8000
#define T_FACTORY_MS 10000

enum state {
    IDLE,
    HOLD,          /* PROFILE held < 5 s */
    MGMT,          /* PROFILE alone, 5-8 s */
    BOND_ARMED,    /* PROFILE alone, >= 8 s */
    FACTORY,       /* PROFILE + X + Y, < 10 s */
    FACTORY_ARMED, /* PROFILE + X + Y, >= 10 s */
    CANCELLED,     /* hold goes on but does nothing (X/Y let go on the factory course, or USB became active) */
};

static const struct gpio_dt_spec button = GPIO_DT_SPEC_INST_GET(0, gpios);
static struct gpio_callback button_cb;
static struct k_work_delayable debounce_work;
static bool button_down;

static enum state st = IDLE;
static bool x_held, y_held;
static uint32_t swallowed; /* positions whose press was swallowed; their release is swallowed too */
static int64_t t_press;
static struct k_work_delayable tick;

#if IS_ENABLED(CONFIG_GC_ABXY_LED)
#define LED(p, n) gc_led_set(p, n)
#define LED_PROFILE() gc_led_show_profile()
#else
#define LED(p, n)
#define LED_PROFILE()
#endif

static bool usb_hid_active(void) {
    return zmk_endpoints_selected().transport == ZMK_TRANSPORT_USB;
}

static int64_t held_ms(void) { return k_uptime_get() - t_press; }

static void set_st(enum state s) {
    if (s != st) {
        gc_diag_mgmt_state(s, st);
    }
    st = s;
}

uint8_t gc_mgmt_state(void) { return st; }

static void schedule_next(int64_t at_ms) {
    int64_t left = at_ms - held_ms();
    k_work_reschedule(&tick, K_MSEC(left > 0 ? left : 0));
}

void gc_mgmt_cancel_hold(void) {
    if (st != IDLE && st != CANCELLED) {
        LOG_INF("cancel: diagnostics started");
        k_work_cancel_delayable(&tick);
        set_st(CANCELLED);
    }
}

static void cancel(const char *why) {
    LOG_INF("cancel: %s", why);
    k_work_cancel_delayable(&tick);
    set_st(CANCELLED);
    LED(GC_LED_OFF, 0);
}

static void enter_factory_course(void) {
    int64_t held = held_ms();
    LOG_INF("X+Y held: factory reset course (%lld ms)", held);
    if (held >= T_FACTORY_MS) {
        set_st(FACTORY_ARMED);
        LED(GC_LED_RAINBOW_LOOP, 0);
        return;
    }
    set_st(FACTORY);
    if (held >= T_MGMT_MS) {
        LED(GC_LED_RED_BLINK, 0); /* from red solid (bond armed) back to the warning: not armed yet */
        schedule_next(T_FACTORY_MS);
    } else {
        schedule_next(T_MGMT_MS);
    }
}

static void factory_reset_cb(struct k_work *work) {
    LOG_WRN("FACTORY RESET");
    zmk_ble_clear_all_bonds();       /* all 3 profile bonds (SR-CTL-09) */
    zmk_ble_prof_select(0);          /* selected profile -> 1 */
    zmk_keymap_reset_settings();     /* Studio keymap changes */
    zmk_endpoints_select_transport(ZMK_TRANSPORT_USB); /* endpoint preference back to default */
    k_sleep(K_MSEC(500));            /* let settings writes complete */
    sys_reboot(SYS_REBOOT_WARM);
}
static K_WORK_DEFINE(factory_reset_work, factory_reset_cb);

static void tick_cb(struct k_work *work) {
    if (st == IDLE || st == CANCELLED) {
        return;
    }
    if (usb_hid_active()) {
        cancel("USB HID active during hold (SR-CTL-11)");
        return;
    }
    int64_t held = held_ms();
    switch (st) {
    case HOLD:
        if (held >= T_MGMT_MS) {
            LOG_INF("warning (5 s)");
            set_st(MGMT);
            LED(GC_LED_RED_BLINK, 0);
            schedule_next(T_BOND_MS);
        }
        break;
    case MGMT:
        if (held >= T_BOND_MS) {
            LOG_INF("bond clear armed (8 s)");
            set_st(BOND_ARMED);
            LED(GC_LED_RED_SOLID, 0);
        }
        break;
    case FACTORY:
        if (held >= T_FACTORY_MS) {
            LOG_INF("factory reset armed (10 s)");
            set_st(FACTORY_ARMED);
            LED(GC_LED_RAINBOW_LOOP, 0);
        } else if (held >= T_MGMT_MS) {
            LED(GC_LED_RED_BLINK, 0);
            schedule_next(T_FACTORY_MS);
        }
        break;
    default:
        break;
    }
}

static void on_pressed(void) {
    if (gc_diag_active()) {
        /* diagnostics: PROFILE only confirms / is recorded; A/B/X/Y are still swallowed while it is held so a
         * PROFILE + A confirmation does not type repeated 'a' on a USB host */
        t_press = k_uptime_get();
        set_st(CANCELLED);
        zmk_endpoints_clear_current();
        return;
    }
    if (usb_hid_active()) {
        LOG_INF("PROFILE ignored: USB HID active (SR-CTL-11)");
        return; /* st stays IDLE: A/B/X/Y keep working */
    }
    t_press = k_uptime_get();
    set_st(HOLD);
    zmk_endpoints_clear_current(); /* A/B/X/Y already down stop repeating on the host (SR-CTL-02) */
    if (x_held && y_held) {
        enter_factory_course();
    } else {
        schedule_next(T_MGMT_MS);
    }
}

static void on_released(void) {
    enum state s = st;
    k_work_cancel_delayable(&tick);
    set_st(IDLE);
    if (s == IDLE) {
        return;
    }
    if (usb_hid_active()) {
        LOG_INF("release ignored: USB HID active (SR-CTL-11)");
        if (!gc_diag_active()) {
            LED(GC_LED_OFF, 0);
        }
        return;
    }
    switch (s) {
    case HOLD:
        LOG_INF("short press (%lld ms): next profile", held_ms());
        zmk_ble_prof_next();   /* SR-CTL-01 */
        LED_PROFILE();         /* its number: blue = paired, white = free (SR-LED-02) */
        break;
    case BOND_ARMED:
        LOG_WRN("bond clear, profile %d (SR-CTL-05)", zmk_ble_active_profile_index() + 1);
        zmk_ble_clear_bonds();
        LED_PROFILE();         /* the same number, now white = ready to pair again */
        break;
    case FACTORY_ARMED:
        LED(GC_LED_RAINBOW_LOOP, 0);
        k_work_submit(&factory_reset_work); /* SR-CTL-07 / SR-CTL-09 */
        break;
    default: /* MGMT, FACTORY, CANCELLED: nothing destructive (SR-CTL-03, SR-CTL-08) */
        LOG_INF("released: cancel");
        if (!gc_diag_active()) { /* the diagnostics pattern owns the LED */
            LED(GC_LED_OFF, 0);
        }
        break;
    }
}

/* ---- button input: both edges -> debounce -> level ---- */
static void debounce_cb(struct k_work *work) {
    bool down = gpio_pin_get_dt(&button) > 0;
    if (down == button_down) {
        return;
    }
    button_down = down;
    gc_diag_input(GC_DIAG_PROFILE, down);
    if (down) {
        on_pressed();
    } else {
        on_released();
    }
}

static void button_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    k_work_reschedule(&debounce_work, K_MSEC(CONFIG_GC_ABXY_MGMT_DEBOUNCE_MS));
}

/* ---- A/B/X/Y: track X / Y, swallow everything while PROFILE is held ---- */
static int mgmt_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev && ev->position < GC_DIAG_PROFILE) {
        gc_diag_input(ev->position, ev->state); /* raw contact, before anything is swallowed */
    }
    if (ev) {
        if (ev->position == CONFIG_GC_ABXY_MGMT_POS_X) {
            x_held = ev->state;
        } else if (ev->position == CONFIG_GC_ABXY_MGMT_POS_Y) {
            y_held = ev->state;
        }

        if (ev->position == CONFIG_GC_ABXY_MGMT_POS_X || ev->position == CONFIG_GC_ABXY_MGMT_POS_Y) {
            bool both = x_held && y_held;
            if (both && (st == HOLD || st == MGMT || st == BOND_ARMED)) {
                enter_factory_course();
            } else if (!both && (st == FACTORY || st == FACTORY_ARMED)) {
                cancel("X or Y released on the factory reset course (SR-CTL-08)");
            }
        }

        uint32_t bit = BIT(ev->position);
        if (ev->state && st != IDLE) {
            swallowed |= bit;
            return ZMK_EV_EVENT_HANDLED;
        }
        if (!ev->state && (swallowed & bit)) {
            swallowed &= ~bit;
            return ZMK_EV_EVENT_HANDLED;
        }
        return ZMK_EV_EVENT_BUBBLE;
    }
    const struct zmk_endpoint_changed *ep = as_zmk_endpoint_changed(eh);
    if (ep && ep->endpoint.transport == ZMK_TRANSPORT_USB && st != IDLE && st != CANCELLED) {
        cancel("USB became active during hold (SR-CTL-11)");
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(gc_mgmt, mgmt_listener);
ZMK_SUBSCRIPTION(gc_mgmt, zmk_position_state_changed);
ZMK_SUBSCRIPTION(gc_mgmt, zmk_endpoint_changed);

static int mgmt_init(void) {
    k_work_init_delayable(&tick, tick_cb);
    k_work_init_delayable(&debounce_work, debounce_cb);
    if (!gpio_is_ready_dt(&button)) {
        LOG_ERR("PROFILE button GPIO not ready");
        return -ENODEV;
    }
    int err = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (!err) {
        err = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
    }
    if (err) {
        LOG_ERR("PROFILE button setup failed: %d", err);
        return err;
    }
    gpio_init_callback(&button_cb, button_isr, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb);
    button_down = gpio_pin_get_dt(&button) > 0; /* held at power-on: wait for a release first */
    return 0;
}
SYS_INIT(mgmt_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
