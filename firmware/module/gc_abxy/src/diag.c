/*
 * gc-abxy diagnostics over BLE: troubleshooting, pre-shipment check, integration-test aid.
 * The byte layouts below are the contract with the manual's diagnostics page and the bench page.
 *
 * Tier 0 (always readable, no pairing): Status, Battery. No keystroke information.
 * Tier 1 (only while diagnostics is ON): Event notifications, Counters.
 *
 * Diagnostics: a host writes START -> PENDING (30 s, magenta blink) -> PROFILE + A held together for 3 s on the
 * device -> ON (5 min, magenta flash every 5 s) -> OFF on timeout, STOP or reset. The log and counters are cleared
 * when ON starts and when it ends, so nothing typed outside a session the owner started on the device is kept.
 * While PENDING / ON the PROFILE button never switches, clears bonds or resets (mgmt.c asks gc_diag_active()).
 *
 * Service   d8bd0001-0b7f-4dc6-bc76-8c605103e9f1
 *   0002 Status    read (36 B, layout 2) / notify (first 20 B)
 *   0003 Control   write
 *   0004 Event     notify (12 B per event)
 *   0005 Counters  read (42 B), ON only
 *   0006 Battery   read (13 B)
 * All little endian; byte 0 of Status / Counters / Battery is the layout version (Status 2, the others 1).
 */
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/settings/settings.h>
#include <nrf.h>

#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/workqueue.h>
#include <zmk/event_manager.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/battery_state_changed.h>
#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/usb.h>
#endif

#include <gc_abxy/battery.h>
#include <gc_abxy/diag.h>
#include <gc_abxy/led.h>

LOG_MODULE_REGISTER(gc_diag, CONFIG_ZMK_LOG_LEVEL);

#define LAYOUT 1          /* Counters, Battery */
#define STATUS_LAYOUT 2   /* Status: 2 = boot counts by cause (was one total) */
#define STATUS_LEN 36
#define PENDING_MS (30 * 1000)
#define ON_MS (5 * 60 * 1000)
#define CONFIRM_MS 3000
#define STATUS_PERIOD_MS 10000
#define KEY_A 0            /* keymap position of A */
#define LOG_N 128          /* events kept for REPLAY */
#define LED_TEST_MAX_S 5

enum diag_state { DIAG_OFF, DIAG_PENDING, DIAG_ON };

enum op {
    OP_START = 0x01,
    OP_STOP = 0x02,
    OP_REPLAY = 0x03,
    OP_LED_TEST = 0x10,
    OP_BATT_NOW = 0x11,
    OP_COUNTERS_CLEAR = 0x12,
    OP_BOOTS_ZERO = 0x20,
};

enum ev_type {
    EV_KEY_DOWN = 1,
    EV_KEY_UP = 2,
    EV_MGMT = 3,
    EV_ENDPOINT = 4,
    EV_BLE = 5,
    EV_BATT = 6,
    EV_LED = 7,
    EV_OVERFLOW = 0xFF,
};

/* reset_cause bits in Status */
#define RC_POWER BIT(0)
#define RC_PIN BIT(1)
#define RC_SOFTWARE BIT(2)
#define RC_WATCHDOG BIT(3)
#define RC_BROWNOUT BIT(4) /* never on nRF52840: a brown-out resets like power-on (RC_POWER) */
#define RC_WAKE BIT(5)
#define RC_DEBUG BIT(6)
#define RC_OTHER BIT(7)

/* LED test colours, index -> rgb bits (bit0 R, bit1 G, bit2 B) */
static const uint8_t led_colours[] = {1, 2, 4, 7, 3, 5}; /* red, green, blue, white, yellow, magenta */

static atomic_t diag_state = ATOMIC_INIT(DIAG_OFF);
static int64_t deadline_ms;
static uint8_t reset_cause;
/* boot counts by cause, kept in settings; index = enum boot_kind */
enum boot_kind { BOOT_POWER, BOOT_PIN, BOOT_SOFTWARE, BOOT_FAULT, BOOT_KINDS };
static uint16_t boots[BOOT_KINDS];
static bool boots_loaded;

static enum boot_kind boot_kind(uint8_t rc) {
    if (rc & (RC_WATCHDOG | RC_OTHER)) {
        return BOOT_FAULT; /* watchdog, CPU lockup and anything unexpected: a firmware fault */
    }
    if (rc & (RC_SOFTWARE | RC_DEBUG)) {
        return BOOT_SOFTWARE; /* factory reset, reboot after flashing, debugger */
    }
    if (rc & RC_PIN) {
        return BOOT_PIN; /* reset button, including the double tap into the bootloader */
    }
    return BOOT_POWER; /* switch on, cell inserted, USB plugged in - and a brown-out, which looks the same */
}

/* ---- inputs, counters and the event ring (touched from several threads: one spinlock) ---- */
struct counters {
    uint16_t presses;
    uint16_t min_gap_ms; /* release -> next press, UINT16_MAX = none yet */
    uint16_t last_hold_ms;
    uint16_t max_hold_ms;
};
struct input {
    bool down;
    bool released_once; /* since counters were cleared */
    bool held_over;     /* already down when the counters were cleared: its release is not a record */
    uint32_t down_ms;
    uint32_t up_ms;
};
struct event {
    uint16_t seq;
    uint32_t t_ms;
    uint8_t type;
    uint8_t id;
    uint32_t value;
};

static struct k_spinlock lock;
static struct input inputs[GC_DIAG_INPUTS];
static struct counters counters[GC_DIAG_INPUTS];
static struct event ring[LOG_N];
static uint16_t next_seq;  /* seq of the next event */
static uint16_t sent_seq;  /* events before this were notified */

static uint16_t sat16(uint32_t v) { return v > UINT16_MAX ? UINT16_MAX : v; }

static void clear_records_locked(void) {
    memset(counters, 0, sizeof(counters));
    for (int i = 0; i < GC_DIAG_INPUTS; i++) {
        counters[i].min_gap_ms = UINT16_MAX;
        inputs[i].released_once = false;
        /* PROFILE and A are still held from the 3 s confirm; their release must not count as a 3 s hold */
        inputs[i].held_over = inputs[i].down;
    }
    memset(ring, 0, sizeof(ring));
    next_seq = 0;
    sent_seq = 0;
}

static struct k_work notify_work;
static struct k_work_delayable status_work;

static void push_event(uint8_t type, uint8_t id, uint32_t value) {
    if (atomic_get(&diag_state) != DIAG_ON) {
        return;
    }
    k_spinlock_key_t k = k_spin_lock(&lock);
    struct event *e = &ring[next_seq % LOG_N];
    *e = (struct event){.seq = next_seq, .t_ms = k_uptime_get_32(), .type = type, .id = id, .value = value};
    next_seq++;
    k_spin_unlock(&lock, k);
    k_work_submit(&notify_work);
}

/* ---- GATT values ---- */
static void status_bytes(uint8_t v[STATUS_LEN]) {
    int64_t now = k_uptime_get();
    int st = atomic_get(&diag_state);
    int64_t left = st == DIAG_OFF ? 0 : MAX(0, (deadline_ms - now + 999) / 1000);
    struct gc_battery_debug b;
    gc_battery_debug_get(&b);
    uint8_t bonded = 0;
    for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        if (!zmk_ble_profile_is_open(i)) {
            bonded |= BIT(i);
        }
    }
    uint8_t link = (zmk_ble_active_profile_is_connected() ? BIT(0) : 0) |
                   (zmk_endpoints_selected().transport == ZMK_TRANSPORT_USB ? BIT(1) : 0);
    bool low = b.valid && b.soc > 0 && b.soc <= CONFIG_GC_ABXY_BATTERY_LOW_PCT;

    memset(v, 0, STATUS_LEN);
    /* bytes 0..19 are also the notification (minimum ATT MTU): keep what changes in there */
    v[0] = STATUS_LAYOUT;
    v[1] = st;
    sys_put_le16(sat16(left), &v[2]);
    sys_put_le32((uint32_t)now, &v[4]);
    v[8] = reset_cause;
    v[9] = zmk_ble_active_profile_index();
    v[10] = bonded;
    v[11] = link;
    v[12] = gc_mgmt_state();
    v[13] = (b.valid ? BIT(0) : 0) | (low ? BIT(1) : 0);
    sys_put_le16(b.est_mv, &v[14]);
    v[16] = b.soc;
    /* 17..19 reserved; read-only part: */
    for (int i = 0; i < BOOT_KINDS; i++) {
        sys_put_le16(boots[i], &v[20 + 2 * i]);
    }
    sys_put_le32(NRF_FICR->DEVICEID[0], &v[28]);
    sys_put_le32(NRF_FICR->DEVICEID[1], &v[32]);
}

static ssize_t read_status(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len,
                           uint16_t offset) {
    uint8_t v[STATUS_LEN];
    status_bytes(v);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, v, sizeof(v));
}

static ssize_t read_battery(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len,
                            uint16_t offset) {
    struct gc_battery_debug b;
    uint8_t v[13];
    gc_battery_debug_get(&b);
    v[0] = LAYOUT;
    sys_put_le16(b.sample_mv, &v[1]);
    sys_put_le16(b.est_mv, &v[3]);
    sys_put_le16(b.adc_raw, &v[5]);
    v[7] = b.soc;
    v[8] = b.valid ? BIT(0) : 0;
    sys_put_le32(b.age_ms, &v[9]);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, v, sizeof(v));
}

static ssize_t read_counters(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len,
                             uint16_t offset) {
    if (atomic_get(&diag_state) != DIAG_ON) {
        return BT_GATT_ERR(BT_ATT_ERR_READ_NOT_PERMITTED);
    }
    uint8_t v[2 + 8 * GC_DIAG_INPUTS] = {LAYOUT, 0};
    k_spinlock_key_t k = k_spin_lock(&lock);
    for (int i = 0; i < GC_DIAG_INPUTS; i++) {
        uint8_t *r = &v[2 + 8 * i];
        sys_put_le16(counters[i].presses, &r[0]);
        sys_put_le16(counters[i].min_gap_ms, &r[2]);
        sys_put_le16(counters[i].last_hold_ms, &r[4]);
        sys_put_le16(counters[i].max_hold_ms, &r[6]);
    }
    k_spin_unlock(&lock, k);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, v, sizeof(v));
}

/* ---- Control: validated here (so the host gets an error at once), carried out on the system work queue ---- */
struct cmd {
    uint8_t op;
    uint8_t arg[4];
};
K_MSGQ_DEFINE(cmd_q, sizeof(struct cmd), 4, 1);
static struct k_work cmd_work;

static bool usb_powered(void) {
#if IS_ENABLED(CONFIG_ZMK_USB)
    return zmk_usb_is_powered();
#else
    return false;
#endif
}

static ssize_t write_control(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len,
                             uint16_t offset, uint8_t flags) {
    const uint8_t *b = buf;
    int st = atomic_get(&diag_state);
    bool ok = false;

    if (offset != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    if (len < 1 || len > 5) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    switch (b[0]) {
    case OP_START:
        ok = len == 1 && st == DIAG_OFF;
        break;
    case OP_STOP:
        ok = len == 1 && st != DIAG_OFF;
        break;
    case OP_REPLAY:
        ok = len == 3 && st == DIAG_ON;
        break;
    case OP_LED_TEST:
        ok = len == 3 && st == DIAG_ON && b[1] < ARRAY_SIZE(led_colours) && b[2] >= 1 && b[2] <= LED_TEST_MAX_S;
        break;
    case OP_BATT_NOW:
    case OP_COUNTERS_CLEAR:
        ok = len == 1 && st == DIAG_ON;
        break;
    case OP_BOOTS_ZERO:
        ok = len == 5 && st == DIAG_ON && usb_powered() && memcmp(&b[1], "ZERO", 4) == 0;
        break;
    default:
        break;
    }
    if (!ok) {
        return BT_GATT_ERR(BT_ATT_ERR_WRITE_REQ_REJECTED);
    }
    struct cmd c = {.op = b[0]};
    memcpy(c.arg, &b[1], len - 1);
    if (k_msgq_put(&cmd_q, &c, K_NO_WAIT) != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }
    k_work_submit(&cmd_work);
    return len;
}

static bool status_notify_on, event_notify_on;

static void status_ccc(const struct bt_gatt_attr *attr, uint16_t value) {
    status_notify_on = value == BT_GATT_CCC_NOTIFY;
    if (status_notify_on) {
        k_work_reschedule(&status_work, K_NO_WAIT);
    }
}

static void event_ccc(const struct bt_gatt_attr *attr, uint16_t value) {
    event_notify_on = value == BT_GATT_CCC_NOTIFY;
    if (event_notify_on) {
        k_work_submit(&notify_work);
    }
}

#define DIAG_UUID(n) BT_UUID_128_ENCODE(0xd8bd0000 + (n), 0x0b7f, 0x4dc6, 0xbc76, 0x8c605103e9f1)
static const struct bt_uuid_128 svc_uuid = BT_UUID_INIT_128(DIAG_UUID(1));
static const struct bt_uuid_128 status_uuid = BT_UUID_INIT_128(DIAG_UUID(2));
static const struct bt_uuid_128 control_uuid = BT_UUID_INIT_128(DIAG_UUID(3));
static const struct bt_uuid_128 event_uuid = BT_UUID_INIT_128(DIAG_UUID(4));
static const struct bt_uuid_128 counters_uuid = BT_UUID_INIT_128(DIAG_UUID(5));
static const struct bt_uuid_128 battery_uuid = BT_UUID_INIT_128(DIAG_UUID(6));

BT_GATT_SERVICE_DEFINE(gc_diag_svc, BT_GATT_PRIMARY_SERVICE(&svc_uuid),
    /* [1] decl, [2] value, [3] CCC */
    BT_GATT_CHARACTERISTIC(&status_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_READ,
                           read_status, NULL, NULL),
    BT_GATT_CCC(status_ccc, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    /* [4] decl, [5] value */
    BT_GATT_CHARACTERISTIC(&control_uuid.uuid, BT_GATT_CHRC_WRITE, BT_GATT_PERM_WRITE, NULL, write_control, NULL),
    /* [6] decl, [7] value, [8] CCC */
    BT_GATT_CHARACTERISTIC(&event_uuid.uuid, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE, NULL, NULL, NULL),
    BT_GATT_CCC(event_ccc, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    /* [9] decl, [10] value */
    BT_GATT_CHARACTERISTIC(&counters_uuid.uuid, BT_GATT_CHRC_READ, BT_GATT_PERM_READ, read_counters, NULL, NULL),
    /* [11] decl, [12] value */
    BT_GATT_CHARACTERISTIC(&battery_uuid.uuid, BT_GATT_CHRC_READ, BT_GATT_PERM_READ, read_battery, NULL, NULL));

#define ATTR_STATUS (&gc_diag_svc.attrs[2])
#define ATTR_EVENT (&gc_diag_svc.attrs[7])

/* ---- notifications ---- */
static void status_work_cb(struct k_work *work) {
    if (!status_notify_on) {
        return;
    }
    uint8_t v[STATUS_LEN];
    status_bytes(v);
    bt_gatt_notify(NULL, ATTR_STATUS, v, 20); /* first 20 B: fits the minimum ATT MTU; device_id is read-only */
    k_work_reschedule(&status_work, K_MSEC(STATUS_PERIOD_MS));
}

static void status_changed(void) { k_work_reschedule(&status_work, K_NO_WAIT); }

static void notify_one(const struct event *e) {
    uint8_t v[12];
    sys_put_le16(e->seq, &v[0]);
    sys_put_le32(e->t_ms, &v[2]);
    v[6] = e->type;
    v[7] = e->id;
    sys_put_le32(e->value, &v[8]);
    int err = bt_gatt_notify(NULL, ATTR_EVENT, v, sizeof(v));
    if (err == -ENOMEM) {
        k_sleep(K_MSEC(5)); /* buffers full: give the stack a moment, the host can REPLAY anything lost */
    }
}

static void notify_work_cb(struct k_work *work) {
    for (;;) {
        struct event e;
        k_spinlock_key_t k = k_spin_lock(&lock);
        if (!event_notify_on || sent_seq == next_seq) {
            k_spin_unlock(&lock, k);
            return;
        }
        if ((uint16_t)(next_seq - sent_seq) > LOG_N) {
            /* the ring moved on: say how many were lost, then continue from the oldest kept */
            uint16_t lost = next_seq - sent_seq - LOG_N;
            sent_seq = next_seq - LOG_N;
            e = (struct event){.seq = sent_seq, .t_ms = k_uptime_get_32(), .type = EV_OVERFLOW, .value = lost};
            k_spin_unlock(&lock, k);
            notify_one(&e);
            continue;
        }
        e = ring[sent_seq % LOG_N];
        sent_seq++;
        k_spin_unlock(&lock, k);
        notify_one(&e);
    }
}

/* ---- state ---- */
static struct k_work_delayable deadline_work, confirm_work, led_restore_work;

static void set_state(enum diag_state s) {
    atomic_set(&diag_state, s);
    LOG_INF("diagnostics %s", s == DIAG_ON ? "ON" : s == DIAG_PENDING ? "PENDING" : "OFF");
    status_changed();
}

static void diag_off(void) {
    k_work_cancel_delayable(&confirm_work);
    k_work_cancel_delayable(&deadline_work);
    k_work_cancel_delayable(&led_restore_work);
    set_state(DIAG_OFF);
    k_spinlock_key_t k = k_spin_lock(&lock);
    clear_records_locked();
    k_spin_unlock(&lock, k);
    gc_led_set(GC_LED_OFF, 0);
}

static void deadline_cb(struct k_work *work) { diag_off(); }

static void confirm_cb(struct k_work *work) {
    k_spinlock_key_t k = k_spin_lock(&lock);
    bool both = inputs[GC_DIAG_PROFILE].down && inputs[KEY_A].down;
    if (both && atomic_get(&diag_state) == DIAG_PENDING) {
        clear_records_locked();
    }
    k_spin_unlock(&lock, k);
    if (!both || atomic_get(&diag_state) != DIAG_PENDING) {
        return;
    }
    deadline_ms = k_uptime_get() + ON_MS;
    k_work_reschedule(&deadline_work, K_MSEC(ON_MS));
    set_state(DIAG_ON);
    gc_led_set(GC_LED_DIAG_ON, 0);
}

static void led_restore_cb(struct k_work *work) {
    if (atomic_get(&diag_state) == DIAG_ON) {
        gc_led_set(GC_LED_DIAG_ON, 0);
    }
}

static void boots_save_cb(struct k_work *work) {
    int err = settings_save_one("gc_diag/boots", boots, sizeof(boots));
    LOG_INF("boots: power %u, pin %u, software %u, fault %u (save %d)", boots[BOOT_POWER], boots[BOOT_PIN],
            boots[BOOT_SOFTWARE], boots[BOOT_FAULT], err);
}
static K_WORK_DEFINE(boots_save_work, boots_save_cb);

static void batt_now_cb(struct k_work *work) {
    const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zmk_battery));
    int err = sensor_sample_fetch(dev);
    struct gc_battery_debug b;
    gc_battery_debug_get(&b);
    LOG_INF("battery sample on request: %d, %u mV", err, b.sample_mv);
    push_event(EV_BATT, 1, b.sample_mv | ((uint32_t)b.soc << 16));
    status_changed();
}
static K_WORK_DEFINE(batt_now_work, batt_now_cb);

static void cmd_work_cb(struct k_work *work) {
    struct cmd c;
    while (k_msgq_get(&cmd_q, &c, K_NO_WAIT) == 0) {
        int st = atomic_get(&diag_state);
        switch (c.op) {
        case OP_START:
            if (st != DIAG_OFF) {
                break;
            }
            gc_mgmt_cancel_hold(); /* a PROFILE hold already in progress must not act on release */
            deadline_ms = k_uptime_get() + PENDING_MS;
            k_work_reschedule(&deadline_work, K_MSEC(PENDING_MS));
            set_state(DIAG_PENDING);
            gc_led_set(GC_LED_DIAG_PENDING, 0);
            if (inputs[GC_DIAG_PROFILE].down && inputs[KEY_A].down) {
                k_work_reschedule(&confirm_work, K_MSEC(CONFIRM_MS));
            }
            break;
        case OP_STOP:
            diag_off();
            break;
        case OP_REPLAY: {
            uint16_t from = sys_get_le16(c.arg);
            k_spinlock_key_t k = k_spin_lock(&lock);
            if (from <= next_seq) { /* a session cannot wrap the 16-bit seq in 5 min */
                sent_seq = from;  /* the notify loop reports an overflow if `from` is older than the ring */
            }
            k_spin_unlock(&lock, k);
            k_work_submit(&notify_work);
            break;
        }
        case OP_LED_TEST:
            gc_led_test(led_colours[c.arg[0]], c.arg[1]);
            push_event(EV_LED, c.arg[0], c.arg[1]);
            k_work_reschedule(&led_restore_work, K_MSEC(c.arg[1] * 1000 + 100));
            break;
        case OP_BATT_NOW:
            k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &batt_now_work); /* same queue as ZMK's reads */
            break;
        case OP_COUNTERS_CLEAR: {
            k_spinlock_key_t k = k_spin_lock(&lock);
            memset(counters, 0, sizeof(counters));
            for (int i = 0; i < GC_DIAG_INPUTS; i++) {
                counters[i].min_gap_ms = UINT16_MAX;
                inputs[i].released_once = false;
                inputs[i].held_over = inputs[i].down;
            }
            k_spin_unlock(&lock, k);
            break;
        }
        case OP_BOOTS_ZERO:
            memset(boots, 0, sizeof(boots));
            k_work_submit(&boots_save_work);
            status_changed();
            break;
        default:
            break;
        }
    }
}

/* ---- hooks from mgmt.c ---- */
bool gc_diag_active(void) { return atomic_get(&diag_state) != DIAG_OFF; }

void gc_diag_input(uint8_t input, bool down) {
    if (input >= GC_DIAG_INPUTS) {
        return;
    }
    uint32_t now = k_uptime_get_32();
    bool record = atomic_get(&diag_state) == DIAG_ON;
    uint32_t hold = 0;
    bool both;

    k_spinlock_key_t k = k_spin_lock(&lock);
    struct input *in = &inputs[input];
    struct counters *c = &counters[input];
    if (down && !in->down) {
        if (record) {
            c->presses++;
            if (in->released_once) {
                c->min_gap_ms = MIN(c->min_gap_ms, sat16(now - in->up_ms));
            }
        }
        in->down_ms = now;
        in->held_over = false;
    } else if (!down && in->down) {
        hold = now - in->down_ms;
        if (record && !in->held_over) {
            c->last_hold_ms = sat16(hold);
            c->max_hold_ms = MAX(c->max_hold_ms, sat16(hold));
        }
        in->held_over = false;
        in->up_ms = now;
        in->released_once = true;
    } else {
        k_spin_unlock(&lock, k);
        return; /* no edge */
    }
    in->down = down;
    both = inputs[GC_DIAG_PROFILE].down && inputs[KEY_A].down;
    k_spin_unlock(&lock, k);

    push_event(down ? EV_KEY_DOWN : EV_KEY_UP, input, down ? 0 : hold);

    if (atomic_get(&diag_state) == DIAG_PENDING && (input == GC_DIAG_PROFILE || input == KEY_A)) {
        if (both) {
            k_work_reschedule(&confirm_work, K_MSEC(CONFIRM_MS));
        } else {
            k_work_cancel_delayable(&confirm_work);
        }
    }
}

void gc_diag_mgmt_state(uint8_t now, uint8_t prev) {
    push_event(EV_MGMT, now, prev);
    status_changed(); /* Status notifications carry mgmt + uptime: INTEG-05 transition times outside diagnostics */
}

/* ---- ZMK events ---- */
static int diag_listener(const zmk_event_t *eh) {
    const struct zmk_endpoint_changed *ep = as_zmk_endpoint_changed(eh);
    if (ep) {
        push_event(EV_ENDPOINT, ep->endpoint.transport == ZMK_TRANSPORT_USB ? 1 : 0, 0);
        status_changed();
        return ZMK_EV_EVENT_BUBBLE;
    }
    const struct zmk_ble_active_profile_changed *pc = as_zmk_ble_active_profile_changed(eh);
    if (pc) {
        push_event(EV_BLE, pc->index, zmk_ble_active_profile_is_connected() ? 1 : 0);
        status_changed();
        return ZMK_EV_EVENT_BUBBLE;
    }
    const struct zmk_battery_state_changed *bs = as_zmk_battery_state_changed(eh);
    if (bs) {
        struct gc_battery_debug b;
        gc_battery_debug_get(&b);
        push_event(EV_BATT, 0, b.sample_mv | ((uint32_t)bs->state_of_charge << 16));
        status_changed();
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(gc_diag, diag_listener);
ZMK_SUBSCRIPTION(gc_diag, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(gc_diag, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(gc_diag, zmk_battery_state_changed);

/* ---- boot count (settings / NVS: one small append per boot, see the design note on flash wear) ---- */
static int diag_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    if (settings_name_steq(name, "boots", NULL) && len == sizeof(boots)) { /* an older single total is ignored */
        int rc = read_cb(cb_arg, boots, sizeof(boots));
        return rc < 0 ? rc : 0;
    }
    return -ENOENT;
}

static int diag_settings_commit(void) {
    if (!boots_loaded) { /* once per boot, after the stored value (if any) was loaded */
        boots_loaded = true;
        enum boot_kind k = boot_kind(reset_cause); /* reset_cause is read in diag_init, before settings load */
        if (boots[k] < UINT16_MAX) {
            boots[k]++;
        }
        k_work_submit(&boots_save_work);
    }
    return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(gc_diag, "gc_diag", NULL, diag_settings_set, diag_settings_commit, NULL);

static int diag_init(void) {
    uint32_t cause = 0;
    if (hwinfo_get_reset_cause(&cause) == 0) {
        reset_cause = ((cause & RESET_POR) ? RC_POWER : 0) |
                      ((cause & RESET_PIN) ? RC_PIN : 0) |
                      ((cause & RESET_SOFTWARE) ? RC_SOFTWARE : 0) |
                      ((cause & RESET_WATCHDOG) ? RC_WATCHDOG : 0) |
                      ((cause & RESET_BROWNOUT) ? RC_BROWNOUT : 0) |
                      ((cause & RESET_LOW_POWER_WAKE) ? RC_WAKE : 0) |
                      ((cause & RESET_DEBUG) ? RC_DEBUG : 0);
        if (!cause) {
            reset_cause = RC_POWER; /* nRF52840 RESETREAS is all zero after power-on AND after a brown-out */
        } else if (!reset_cause) {
            reset_cause = RC_OTHER;
        }
        hwinfo_clear_reset_cause();
    }
    k_spinlock_key_t k = k_spin_lock(&lock);
    clear_records_locked();
    k_spin_unlock(&lock, k);
    k_work_init(&notify_work, notify_work_cb);
    k_work_init(&cmd_work, cmd_work_cb);
    k_work_init_delayable(&status_work, status_work_cb);
    k_work_init_delayable(&deadline_work, deadline_cb);
    k_work_init_delayable(&confirm_work, confirm_cb);
    k_work_init_delayable(&led_restore_work, led_restore_cb);
    LOG_INF("reset cause 0x%08x -> 0x%02x", cause, reset_cause);
    return 0;
}
SYS_INIT(diag_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
