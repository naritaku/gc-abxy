/*
 * CR2450 battery gauge: voltage divider on an nRF SAADC input (copy of ZMK's zmk,battery-voltage-divider driver,
 * MIT, with a CR2450 discharge table instead of the Li-ion curve). Readings below 1.8 V (main switch off, USB powered)
 * keep the last valid value instead of reporting a fake level (requirement SR-PWR-08).
 */
#define DT_DRV_COMPAT gc_battery_cr2450

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

#include <gc_abxy/battery.h>

LOG_MODULE_REGISTER(gc_batt, CONFIG_ZMK_LOG_LEVEL);

struct cr_config {
    uint8_t channel;
    uint32_t output_ohm;
    uint32_t full_ohm;
};

struct cr_data {
    const struct device *adc;
    struct adc_channel_cfg acc;
    struct adc_sequence as;
    uint16_t adc_raw;
    uint16_t millivolts;   /* filtered estimate, not the raw sample */
    uint8_t soc;           /* quantised to SOC_STEP, with hysteresis */
    bool valid;
    uint16_t sample_mv;    /* last sample, divider undone (debug view only) */
    uint32_t sample_ms;    /* uptime of the last sample (debug view only) */
};

/*
 * Reporting filter (measurement/battery-threshold-interim.md).
 *
 * What this does NOT do: improve accuracy. The +-4.1 % budget is dominated by the SAADC gain error and the
 * divider tolerance, and both are a FIXED bias for a given unit - no amount of filtering removes them.
 * Absolute state of charge is only good to about +-21 percentage points (the table runs at ~5 mV per %).
 *
 * What it does do: make the reported value behave. Two real defects without it:
 *   - a sample that lands during a BLE transmit reads low, so the level jumps around;
 *   - the voltage recovers once the load goes away, so the level goes UP, which for a primary cell looks broken.
 * The bias being fixed per unit is also why 4 % steps are still meaningful: the ABSOLUTE value is coarse, but
 * the change over time for one device is not, so the steps carry a usable trend.
 *
 * Asymmetric EWMA: falls with tau ~4 samples, rises 8x slower, so a dip is averaged away instead of being
 * followed, and a bad first sample still heals instead of sticking forever (which is what a strictly
 * monotonic filter would do). A jump of more than SWAP_MV upwards is a fresh cell and is taken immediately:
 * a load can only pull the voltage DOWN, so a large step up has no other explanation.
 *
 * Integer shifts give each direction a dead band: the estimate stops chasing once it is within 4 mV from
 * above or 32 mV from below. Both are far inside the +-107 mV measurement error, so neither matters.
 */
#define SOC_STEP     4     /* report 0, 4, 8 ... 100 */
#define FALL_SHIFT   2     /* est -= (est - raw) >> 2   : tau ~4 samples (~4 min at ZMK's 60 s period) */
#define RISE_SHIFT   5     /* est += (raw - est) >> 5   : 8x slower upwards */
#define SWAP_MV    150     /* a step up larger than this is a cell change, not load recovery */

/* Interim CR2450 table (mV -> %), to be replaced by the near-EOL measurement (SR-BAT-05).
 * The low-battery warning fires at CONFIG_GC_ABXY_BATTERY_LOW_PCT = 28 %, i.e. 2615 mV on this table.
 * (28 rather than the derived 25 because the report is quantised to SOC_STEP: 25 is not a step, so the warning
 * would land on 24 % = 2593 mV, just under the 2600 mV the error budget asks for.)
 * Li/MnO2 is very flat (the cell sits near 2.9 V for most of its life), so only the knee carries usable
 * information - see measurement/battery-threshold-interim.md for the derivation and the error budget. */
static const struct { uint16_t mv; uint8_t pct; } table[] = {
    {3000, 100}, {2950, 95}, {2900, 88}, {2850, 80}, {2800, 70}, {2750, 58}, {2700, 45},
    {2650, 35}, {2600, 25}, {2550, 18}, {2500, 12}, {2450, 8}, {2400, 5}, {2300, 2}, {2200, 0},
};

static uint8_t cr2450_mv_to_pct(uint16_t mv) {
    if (mv >= table[0].mv) {
        return 100;
    }
    for (int i = 1; i < ARRAY_SIZE(table); i++) {
        if (mv >= table[i].mv) {
            uint16_t span = table[i - 1].mv - table[i].mv;
            return table[i].pct + (table[i - 1].pct - table[i].pct) * (mv - table[i].mv) / span;
        }
    }
    return 0;
}

static int cr_sample_fetch(const struct device *dev, enum sensor_channel chan) {
    struct cr_data *d = dev->data;
    const struct cr_config *cfg = dev->config;

    if (chan != SENSOR_CHAN_GAUGE_VOLTAGE && chan != SENSOR_CHAN_GAUGE_STATE_OF_CHARGE && chan != SENSOR_CHAN_ALL) {
        return -ENOTSUP;
    }
    int rc = adc_read(d->adc, &d->as);
    d->as.calibrate = false;
    if (rc != 0) {
        LOG_DBG("adc_read failed: %d", rc);
        return rc;
    }
    int32_t val = d->adc_raw;
    adc_raw_to_millivolts(adc_ref_internal(d->adc), d->acc.gain, d->as.resolution, &val);
    uint16_t mv = val * (uint64_t)cfg->full_ohm / cfg->output_ohm;
    d->sample_mv = mv;
    d->sample_ms = k_uptime_get_32();
    if (mv < 1800) {
        /* main switch off / not battery powered: keep the last valid reading */
        LOG_DBG("battery %d mV < 1.8 V: keeping last value", mv);
        if (!d->valid) {
            d->millivolts = 0;
            d->soc = 0;
        }
        return 0;
    }
    if (!d->valid) {
        d->millivolts = mv;                       /* first sample seeds the estimate */
    } else if (mv > d->millivolts + SWAP_MV) {
        LOG_INF("battery: %d -> %d mV, fresh cell", d->millivolts, mv);
        d->millivolts = mv;
    } else if (mv < d->millivolts) {
        d->millivolts -= (d->millivolts - mv) >> FALL_SHIFT;
    } else {
        d->millivolts += (mv - d->millivolts) >> RISE_SHIFT;
    }

    uint8_t pct = cr2450_mv_to_pct(d->millivolts);
    if (!d->valid || pct + SOC_STEP <= d->soc || pct >= d->soc + SOC_STEP) {
        d->soc = (pct / SOC_STEP) * SOC_STEP;     /* a full step of movement is needed to change the report */
    }
    if (d->soc == 0) {
        /* 0 % is reserved for "no valid reading yet" (see the < 1.8 V branch). A nearly empty cell reports 1 %, so
         * the LED logic, which treats 0 as unknown, keeps showing the low-battery yellow down to the last reading. */
        d->soc = 1;
    }
    d->valid = true;
    LOG_DBG("raw %d -> %d mV -> est %d mV -> %d%% -> report %d%%", d->adc_raw, mv, d->millivolts, pct, d->soc);
    return 0;
}

static int cr_channel_get(const struct device *dev, enum sensor_channel chan, struct sensor_value *out) {
    struct cr_data *d = dev->data;
    switch (chan) {
    case SENSOR_CHAN_GAUGE_VOLTAGE:
        out->val1 = d->millivolts / 1000;
        out->val2 = (d->millivolts % 1000) * 1000U;
        return 0;
    case SENSOR_CHAN_GAUGE_STATE_OF_CHARGE:
        out->val1 = d->soc;
        out->val2 = 0;
        return 0;
    default:
        return -ENOTSUP;
    }
}

static const struct sensor_driver_api cr_api = {
    .sample_fetch = cr_sample_fetch,
    .channel_get = cr_channel_get,
};

static int cr_init(const struct device *dev) {
    struct cr_data *d = dev->data;
    const struct cr_config *cfg = dev->config;
    if (!device_is_ready(d->adc)) {
        LOG_ERR("ADC not ready");
        return -ENODEV;
    }
    d->as = (struct adc_sequence){
        .channels = BIT(0),
        .buffer = &d->adc_raw,
        .buffer_size = sizeof(d->adc_raw),
        .oversampling = 4,
        .calibrate = true,
    };
#ifdef CONFIG_ADC_NRFX_SAADC
    d->acc = (struct adc_channel_cfg){
        .gain = ADC_GAIN_1_6,
        .reference = ADC_REF_INTERNAL,
        .acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 40),
        .input_positive = SAADC_CH_PSELP_PSELP_AnalogInput0 + cfg->channel,
    };
    d->as.resolution = 12;
#else
#error Unsupported ADC
#endif
    int rc = adc_channel_setup(d->adc, &d->acc);
    LOG_DBG("AIN%u setup: %d", cfg->channel, rc);
    return rc;
}

static struct cr_data cr_data = {.adc = DEVICE_DT_GET(DT_IO_CHANNELS_CTLR(DT_DRV_INST(0)))};
static const struct cr_config cr_cfg = {
    .channel = DT_IO_CHANNELS_INPUT(DT_DRV_INST(0)),
    .output_ohm = DT_INST_PROP(0, output_ohms),
    .full_ohm = DT_INST_PROP(0, full_ohms),
};

DEVICE_DT_INST_DEFINE(0, &cr_init, NULL, &cr_data, &cr_cfg, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY, &cr_api);

void gc_battery_debug_get(struct gc_battery_debug *out) {
    /* Plain copies of values written by the sensor fetch; a torn read only affects a debug readout. */
    const struct cr_data *d = &cr_data;
    *out = (struct gc_battery_debug){
        .sample_mv = d->sample_mv,
        .est_mv = d->millivolts,
        .adc_raw = d->adc_raw,
        .soc = d->soc,
        .valid = d->valid,
        .age_ms = d->sample_ms ? k_uptime_get_32() - d->sample_ms : UINT32_MAX,
    };
}
