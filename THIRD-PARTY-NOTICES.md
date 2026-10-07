# Third-party notices

The firmware image `gc-abxy.uf2` (GitHub Releases, https://gc-abxy.pages.dev/fw) is built from this repository
together with the following open-source software, pinned by `firmware/config/west.yml` (ZMK v0.3.0).
Each component remains under its own license; the full texts are in the linked repositories at the pinned revisions.

| Component | Used for | License | Source |
|---|---|---|---|
| ZMK Firmware v0.3.0 | keyboard firmware, ZMK Studio | MIT | https://github.com/zmkfirmware/zmk |
| Zephyr RTOS v3.5.0+zmk-fixes | RTOS, Bluetooth LE host and controller, USB | Apache-2.0 | https://github.com/zmkfirmware/zephyr |
| nrfx (Zephyr module hal_nordic) | nRF52840 drivers | BSD-3-Clause | https://github.com/zephyrproject-rtos/hal_nordic |
| CMSIS (Zephyr module cmsis) | Arm Cortex-M core support | Apache-2.0 | https://github.com/zephyrproject-rtos/cmsis |
| TinyCrypt (Zephyr module) | Bluetooth LE pairing cryptography (AES, CMAC, ECC-DH) | BSD-3-Clause | https://github.com/zephyrproject-rtos/tinycrypt |
| nanopb | ZMK Studio messages | Zlib | https://github.com/zmkfirmware/nanopb |
| zmk-studio-messages | ZMK Studio protocol definitions | see repository | https://github.com/zmkfirmware/zmk-studio-messages |

The Apache-2.0 components require a copy of the license and any NOTICE files; see the Zephyr and CMSIS repositories
at the revisions above. The code in `firmware/` of this repository is MIT (see `LICENSE`).
