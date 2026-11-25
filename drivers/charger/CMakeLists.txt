# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

pbl_library()
pbl_library_sources(charger.c)
pbl_library_sources_ifdef(CONFIG_CHARGER_ADC adc.c)
pbl_library_sources_ifdef(CONFIG_CHARGER_NPM1300 npm1300.c)
pbl_library_sources_ifdef(CONFIG_CHARGER_SHELL shell.c)
pbl_library_sources_ifdef(CONFIG_CHARGER_SIM sim.c)
pbl_library_sources_ifdef(CONFIG_CHARGER_X4_PRO cw2017.c)
