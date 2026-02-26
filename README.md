
<div align="center">

<img src="assets/logo.png" alt="logo" width="350"/>

---

![License: GPL-2.0](https://img.shields.io/badge/GPL--2.0-red?style=for-the-badge)
![Nintendo Switch](https://img.shields.io/badge/Nintendo_Switch-E60012?style=for-the-badge\&logo=nintendo-switch\&logoColor=white)
[![Discord](https://img.shields.io/badge/Discord-5865F2?style=for-the-badge\&logo=discord\&logoColor=white)](https://dsc.gg/horizonoc)
![VSCode](https://img.shields.io/badge/VSCode-0078D4?style=for-the-badge\&logo=visual%20studio%20code\&logoColor=white)
![Made with Notepad++](assets/np++.png?raw=true)
![C++](https://img.shields.io/badge/C%2B%2B-00599C?style=for-the-badge\&logo=c%2B%2B\&logoColor=white)
![Downloads](https://img.shields.io/github/downloads/souldbminersmwc/Horizon-OC/total.svg?style=for-the-badge)

---

</div>

## ⚠️ Disclaimer

> **THIS TOOL CAN BE DANGEROUS IF MISUSED. PROCEED WITH CAUTION.**
> Due to the design of Horizon OS, **overclocking RAM can cause NAND OR SD CORRUPTION.**
> Ensure you have a **full NAND, PROINFO, EMUMMC and SD backup** before proceeding.

---

## About

**Horizon OC** is an open-source overclocking tool for Nintendo Switch consoles running **Atmosphere custom firmware**.
It enables advanced CPU, GPU, and RAM tuning with user-friendly configuration tools.

---

## Features

* **CPU:** Up to 1963MHz (Mariko) / 1785MHz (Erista)
* **GPU:** Up to 1075MHz (Mariko) / 921MHz (Erista)
* **RAM:** Up to 1866/2133MHz (Mariko) / 1600MHz (Erista)
* Over/undervolting support
* Built-in configurator
* Compatible with most homebrew

> It is reccomended to read the [guide](https://rentry.co/howtoget60fps) before proceeding, as this can help you get a *significant* performance boost over the default settings, often times with less power draw and heat output

---

## Installation

1. Ensure you have the latest versions of

   * [Atmosphere](https://github.com/Atmosphere-NX/Atmosphere)
   * [Ultrahand Overlay](https://github.com/ppkantorski/Ultrahand-Overlay)
2. Download and extract the **Horizon OC Package** to the root of your SD card.
3. If using **Hekate**, edit `hekate_ipl.ini` to include:

   ```
   kip1=atmosphere/kips/hoc.kip
   ```

   *(No changes needed if using fusee.)*

---

## Configuration

1. Open the Horizon OC Overlay
2. Open the settings menu
3. Adjust your overclocking settings as desired. A helpful guide can be found [here.](https://rentry.co/mariko#oc-settings-for-horizon-oc)
4. Click **Save KIP Settings** to apply your configuration.

---

## Building from Source

Refer to COMPILATION.md

---
## Clock table

### MEM clocks
* 3200 → max on mariko, JEDEC.
* 2933 → JEDEC.
* 2666 → JEDEC.
* 2400 → max on erista, JEDEC.
* 2133 → mariko safe max (4266 Modules), JEDEC.
* 1996 → JEDEC.
* 1866 → mariko safe max (3733 Modules), JEDEC.
* 1600 → official docked, boost mode, erista safe max, JEDEC.
* 1331 → official handheld, JEDEC.
* 1065
* 800
* 665

### CPU clocks
* 2601 → mariko absolute max, very dangerous
* 2499
* 2397 → mariko safe max with UV (low speedo)
* 2295
* 2193
* 2091
* 1963 → mariko no UV max clock
* 1887
* 1785 → erista no UV max clock, boost mode
* 1683
* 1581
* 1428
* 1326
* 1224 → sdev oc
* 1122
* 1020 → official docked & handheld
* 918
* 816
* 714
* 612 → sleep mode
  
**Notes:**
1. On Erista, CPU in handheld is capped to 1581MHz

### GPU clocks
* 1536 → absolute max clock on mariko. very dangerous
* 1459
* 1382
* 1305
* 1267 → NVIDIA T214 rating
* 1228 → mariko HiOPT safe clock
* 1152 → mariko SLT max clock
* 1075 → mariko no UV max clock. absolute max clock on erista. very dangerous
* 998 → NVIDIA T210 rating
* 960 (erista only) → erista slt/hiopt safe max clock
* 921 → erista no UV max clock
* 844
* 768 → official docked
* 691
* 614
* 537
* 460 → max handheld
* 384 → official handheld
* 307 → official handheld
* 230
* 153
* 76 → boost mode

**Notes:**
1. GPU overclock is capped at 460MHz on erista in handheld
2. On Mariko, cap with No uv is 614MHz, with SLT it is 691MHz and with HiOPT it's 768MHz
3. Clocks higher than 768MHz on erista need the official charger is plugged in.
4. On Mariko, cap with No uv is 844MHz, with SLT it is 921MHz and with HiOPT it's 998MHz

---

## Credits
* **Lightos's Cat** - Cat

* **Souldbminer** – hoc-clk and loader development
* **Lightos** – loader patches development
* **SciresM** - Atmosphere CFW
* **CTCaer** - L4T, Hekate, perfect ram timings
* **KazushiMe** – Switch OC Suite
* **hanai3bi (meha)** – Switch OC Suite, EOS, sys-clk-eos
* **NaGaa95** – L4T-OC-kernel
* **B3711 (halop)** – EOS
* **sys-clk team (m4xw, p-sam, natinusala)** – sys-clk
* **b0rd2death** – Ultrahand sys-clk & Status Monitor fork
* **MasaGratoR and ZachyCatGames** - General help
* **MasaGratoR** - Status Monitor & Display Refresh Rate Driver
* **Dom, Samybigio, Arcdelta, Miki, Happy, Flopsider, Winnerboi77, Blaise, Alvise, TDRR, agjeococh, frost, letum00 and Xenshen** - Testing
* **Samybigio2011** - Italian translations
