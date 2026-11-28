# PebbleOS on the LilyGO T-Deck Pro

Use the `t_deck_pro` firmware board and `tdeckpro` native Xtensa app target. The 240 × 320 GDEQ031T10 panel uses the UC8253 controller. The driver uses the software reset, factory waveform and differential partial-update sequence validated by Newton-re. The Cyberdeck TCA8418 driver scans the T-Deck matrix. Press **U** for Up, **D** for Down, **Enter** for Select and **Backspace** for Back.

This bring-up covers USB, the monochrome panel, keyboard navigation, native applications and the BLE companion protocol. Touch, text entry, charger telemetry, GPS, LoRa, cellular and microSD need additional integration. The display sequence is for the original T-Deck Pro with the GDEQ031T10 panel.

## Build, back up and flash over USB

Set up the [PebbleOS tools and Espressif Xtensa compiler](../xteink/README.md#build-firmware-from-source), then build:

```sh
pbl configure --board t_deck_pro -b build-t-deck
pbl build -b build-t-deck
```

Set `PORT` to the T-Deck Pro’s USB-Serial/JTAG device. Before replacing its existing firmware, make a full flash backup and verify it is 16,777,216 bytes. If the old firmware occupies the PebbleOS filesystem region, erase that region once after making the backup. This clears the device’s existing data in that range.

```sh
esptool --chip esp32s3 --port "$PORT" read-flash 0 0x1000000 t-deck-pro-backup.bin
wc -c t-deck-pro-backup.bin
esptool --chip esp32s3 --port "$PORT" erase-region 0x780000 0x800000
pbl run -b build-t-deck --tty "$PORT" --resources
```

For subsequent PebbleOS updates, run only the last command. To return to the previous firmware and its data, use the complete backup from the same device with `esptool --chip esp32s3 --port "$PORT" write-flash 0 t-deck-pro-backup.bin`.

## Apps and the phone

Pair in **Devices → Add Watch** in the [public MobileApp fork](https://github.com/ericmigi/mobileapp/tree/codex/xteink-pro-companion). It identifies the device as **LilyGO T-Deck Pro** and selects the `tdeckpro` binary from compatible PBWs. Export the SDK with `pbl sdk 4.47-tdeckpro --board t_deck_pro --tarball -b build-t-deck-sdk`. Use `tdeckpro` in the app’s `targetPlatforms` and build with that SDK.

For USB app installation, run:

```sh
python tools/install_xteink_pbw.py --board t_deck_pro --port "$PORT" path/to/app.pbw
```

[Manufacturer pinout and schematics](https://wiki.lilygo.cc/products/t-deck-series/t-deck-pro/) · [Manufacturer reference repository and datasheets](https://github.com/Xinyuan-LilyGO/T-Deck-Pro) · [Newton-re hardware notes](https://github.com/dparnell/newton-re/blob/0e2fc147/docs/host-tdeck-pro.md)
