/* BLE device name "GC-ABXY-XXXX", XXXX = low 16 bits of the nRF52840 DEVICEID[0] (requirement SR-ID-01). */
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <nrf.h>
#include <zmk/ble.h>

LOG_MODULE_REGISTER(gc_devname, CONFIG_ZMK_LOG_LEVEL);

static char name[16];

static int devname_init(void) {
    uint32_t id = NRF_FICR->DEVICEID[0];
    snprintf(name, sizeof(name), "GC-ABXY-%04X", (unsigned)(id & 0xFFFF));
    int rc = zmk_ble_set_device_name(name);
    LOG_INF("device name %s (%d)", name, rc);
    return 0;
}
/* after ZMK BLE init (CONFIG_ZMK_BLE_INIT_PRIORITY = 50, APPLICATION level) */
SYS_INIT(devname_init, APPLICATION, 95);
