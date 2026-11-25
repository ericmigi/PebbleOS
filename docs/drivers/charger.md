# Charger

A charger reports whether external power is present, the charge status and
the battery voltage, current and temperature, and turns charging on and off.

| | |
| --- | --- |
| Directory | `drivers/charger` |
| Header | `pbl/drivers/charger.h` |
| Kconfig | `CONFIG_CHARGER`, enabled by the application |
| Role | `CHARGER` |
| Sample | {doc}`../samples/drivers/charger` |
| API | {apidoc}`API reference <drivers_charger>` |

## Drivers

| Driver | Compatible | Kconfig |
| --- | --- | --- |
| Nordic nPM1300 | `nordic,npm1300` | `CONFIG_CHARGER_NPM1300` |
| Presence pin and ADC | `coredevices,adc-charger` | `CONFIG_CHARGER_ADC` |
| Simulator | — | `CONFIG_CHARGER_SIM` |
