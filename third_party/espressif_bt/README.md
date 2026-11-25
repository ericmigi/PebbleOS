# Espressif ESP32-C3 Bluetooth LE controller

The ESP32-C3's Bluetooth LE controller and PHY, as Espressif ships them for
ESP-IDF, built into PebbleOS without ESP-IDF or FreeRTOS. NimBLE stays the
host; it talks to the controller over its virtual HCI (VHCI), see
`third_party/nimble/transport/hci_esp32c3.c`.

## Contents

| Path | What | License |
| --- | --- | --- |
| `esp32c3-bt-lib/` | Submodule, [espressif/esp32c3-bt-lib](https://github.com/espressif/esp32c3-bt-lib) at the commit ESP-IDF 5.5.5 pins: `esp32c3/libbtdm_app.a`, the controller (`libbtdm_app_flash.a` with `CONFIG_ESP32C3_BT_CONTROLLER_IN_FLASH`) | Apache-2.0 |
| `esp-phy-lib/` | Submodule, [espressif/esp-phy-lib](https://github.com/espressif/esp-phy-lib) at the commit ESP-IDF 5.5.5 pins: `esp32c3/libphy.a`, `libbtbb.a` | Apache-2.0 |
| `esp32c3/bt.c`, `phy.c`, `esp_bt.h`, `phy.h`, `phy_init_data.h`, `esp32c3_regs.h` | Derived from ESP-IDF 5.5.5 `components/bt/controller/esp32c3/bt.c`, `components/esp_phy/src/*.c` and their headers, cut down to BLE alone, the FreeRTOS calls mapped to the PebbleOS kernel | Apache-2.0, Espressif and Core Devices |
| `esp32c3/phy_init_data.c` | ESP-IDF 5.5.5 `components/esp_phy/esp32c3/phy_init_data.c`, unchanged | Apache-2.0, Espressif |
| `esp32c3/rom.ld`, `rom_patch.ld` | The ROM symbols the libraries use, picked from ESP-IDF's ROM scripts by `gen_rom_ld.py`; `rom_patch.ld` adds those of the controller patched in ROM | Apache-2.0, Espressif |
| `esp32c3/sdkconfig.h` | The ESP-IDF configuration the derived code reads | Apache-2.0 |
| `esp32c3/ctl.c`, `include/` | PebbleOS's own: the guarded bring-up and the `btc` shell command | Apache-2.0 |

The libraries are byte for byte those of PlatformIO's `framework-espidf`
5.5.5 (SHA-256 checked).

## Choices

- **The controller patched in ROM** (`libbtdm_app.a`), ESP-IDF's default,
  with about 23 KiB of RAM code (`.iram1`, placed in `.ramfunc`). The one
  linked whole in flash (`CONFIG_ESP32C3_BT_CONTROLLER_IN_FLASH`,
  `libbtdm_app_flash.a`, which the Arduino core's sdkconfig selects) needs
  5 KiB, but its interrupt handlers run from flash through the cache: on
  the Xteink X4 they took over 3 ms and no advertising event went on air.
- **BLE only**: no Wi-Fi coexistence (`libcoexist.a` is not needed), no
  modem sleep, no power management, no PLL tracking (the controller turns
  it off for BLE), the low power clock on the main crystal.
- **A full RF calibration** at every bring-up; nothing is stored.
- **Everything starts on request**, guarded by the RTC watchdog and a
  retained register (`esp32c3/ctl.c`): the PHY init may turn the USB PHY
  off, and USB-Serial-JTAG is the only way into the Xteink X4.
  `phy_bbpll_en_usb(true)` runs before `register_chipv7_phy()`, as ESP-IDF
  does with `ESP_PHY_ENABLE_USB`.

## Updating

Take the libraries and the derived code from the same ESP-IDF release: the
OS adaptation table (`osi_funcs_t`, `OSI_VERSION`) and the controller
configuration (`esp_bt_controller_config_t`, `ESP_BT_CTRL_CONFIG_VERSION`)
must match the library, and `bt.c` checks their sizes. Move both submodules
to the commits the release's `components/bt/controller/lib_esp32c3_family`
and `components/esp_phy/lib` point to, diff the release's `bt.c`,
`phy_init.c` and `esp_bt.h` against the previous one and carry the changes
over, then regenerate `rom.ld` from the names a link of the flash-only
controller without it leaves undefined, and `rom_patch.ld` likewise for the
ROM-patched one, from `esp32c3.rom.ld`, `rom.bt_funcs.ld`, `rom.eco3.ld`
and `rom.eco3_bt_funcs.ld`:

```shell
grep -o "undefined reference to .[A-Za-z0-9_]*" build.log | cut -c25- | sort -u > symbols.txt
python3 esp32c3/gen_rom_ld.py symbols.txt $IDF/components/esp_rom/esp32c3/ld/esp32c3.rom.ld \
  $IDF/components/esp_rom/esp32c3/ld/esp32c3.rom.eco3.ld > esp32c3/rom.ld
```
