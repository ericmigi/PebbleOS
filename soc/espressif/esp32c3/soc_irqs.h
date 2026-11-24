/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! ESP32-C3 interrupt sources, the inputs of the interrupt matrix.

#pragma once

#define PBL_SOC_IRQN_WIFI_MAC             0
#define PBL_SOC_IRQN_WIFI_MAC_NMI         1
#define PBL_SOC_IRQN_WIFI_PWR             2
#define PBL_SOC_IRQN_WIFI_BB              3
#define PBL_SOC_IRQN_BT_MAC               4
#define PBL_SOC_IRQN_BT_BB                5
#define PBL_SOC_IRQN_BT_BB_NMI            6
#define PBL_SOC_IRQN_RWBT                 7
#define PBL_SOC_IRQN_RWBLE                8
#define PBL_SOC_IRQN_RWBT_NMI             9
#define PBL_SOC_IRQN_RWBLE_NMI            10
#define PBL_SOC_IRQN_I2C_MASTER           11
#define PBL_SOC_IRQN_SLC0                 12
#define PBL_SOC_IRQN_SLC1                 13
#define PBL_SOC_IRQN_APB_CTRL             14
#define PBL_SOC_IRQN_UHCI0                15
#define PBL_SOC_IRQN_GPIO                 16
#define PBL_SOC_IRQN_GPIO_NMI             17
#define PBL_SOC_IRQN_SPI1                 18
#define PBL_SOC_IRQN_SPI2                 19
#define PBL_SOC_IRQN_I2S1                 20
#define PBL_SOC_IRQN_UART0                21
#define PBL_SOC_IRQN_UART1                22
#define PBL_SOC_IRQN_LEDC                 23
#define PBL_SOC_IRQN_EFUSE                24
#define PBL_SOC_IRQN_TWAI                 25
#define PBL_SOC_IRQN_USB_SERIAL_JTAG      26
#define PBL_SOC_IRQN_RTC_CORE             27
#define PBL_SOC_IRQN_RMT                  28
#define PBL_SOC_IRQN_I2C_EXT0             29
#define PBL_SOC_IRQN_TIMER1               30
#define PBL_SOC_IRQN_TIMER2               31
#define PBL_SOC_IRQN_TG0_T0               32
#define PBL_SOC_IRQN_TG0_WDT              33
#define PBL_SOC_IRQN_TG1_T0               34
#define PBL_SOC_IRQN_TG1_WDT              35
#define PBL_SOC_IRQN_CACHE_IA             36
#define PBL_SOC_IRQN_SYSTIMER_TARGET0     37
#define PBL_SOC_IRQN_SYSTIMER_TARGET1     38
#define PBL_SOC_IRQN_SYSTIMER_TARGET2     39
#define PBL_SOC_IRQN_SPI_MEM_REJECT_CACHE 40
#define PBL_SOC_IRQN_ICACHE_PRELOAD0      41
#define PBL_SOC_IRQN_ICACHE_SYNC0         42
#define PBL_SOC_IRQN_APB_ADC              43
#define PBL_SOC_IRQN_DMA_CH0              44
#define PBL_SOC_IRQN_DMA_CH1              45
#define PBL_SOC_IRQN_DMA_CH2              46
#define PBL_SOC_IRQN_RSA                  47
#define PBL_SOC_IRQN_AES                  48
#define PBL_SOC_IRQN_SHA                  49
#define PBL_SOC_IRQN_FROM_CPU0            50
#define PBL_SOC_IRQN_FROM_CPU1            51
#define PBL_SOC_IRQN_FROM_CPU2            52
#define PBL_SOC_IRQN_FROM_CPU3            53
#define PBL_SOC_IRQN_ASSIST_DEBUG         54
#define PBL_SOC_IRQN_DMA_APBPERI_PMS      55
#define PBL_SOC_IRQN_CORE0_IRAM0_PMS      56
#define PBL_SOC_IRQN_CORE0_DRAM0_PMS      57
#define PBL_SOC_IRQN_CORE0_PIF_PMS        58
#define PBL_SOC_IRQN_CORE0_PIF_PMS_SIZE   59
#define PBL_SOC_IRQN_BAK_PMS_VIOLATE      60
#define PBL_SOC_IRQN_CACHE_CORE0_ACS      61
