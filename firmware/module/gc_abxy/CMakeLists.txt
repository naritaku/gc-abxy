# Sources are added to ZMK's `app` target (same pattern as community ZMK modules) so they see ZMK's private
# include path (zmk/*.h, drivers/behavior.h). The module's own headers live in include/.
target_include_directories(app PRIVATE include)
target_sources_ifdef(CONFIG_GC_ABXY_LED app PRIVATE src/led.c)
target_sources_ifdef(CONFIG_GC_ABXY_MGMT app PRIVATE src/mgmt.c)
target_sources_ifdef(CONFIG_GC_ABXY_BATTERY_CR2450 app PRIVATE src/battery_cr2450.c)
target_sources_ifdef(CONFIG_GC_ABXY_DEVICE_NAME app PRIVATE src/devname.c)
target_sources_ifdef(CONFIG_GC_ABXY_DIAG app PRIVATE src/diag.c)
