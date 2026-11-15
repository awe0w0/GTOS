# A3 external device drivers

| Directory | Upstream commit | License |
| --- | --- | --- |
| lsm6ds3tr-c | [STMicroelectronics driver 6735cd1](https://github.com/STMicroelectronics/lsm6ds3tr-c-pid/tree/6735cd1f6ea9208084f2e7dc69605250bc532ac6) | BSD-3-Clause |
| bq25601 | [BQ25601D driver f1ef7ee](https://github.com/CrashOverride85/BQ25601/tree/f1ef7ee1b5f63141c8f1dff4403938eea29b1b2e) | BSD-2-Clause |
| st7703 | [Espressif ESP LCD ST7703 1.0.4](https://components.espressif.com/components/espressif/esp_lcd_st7703/versions/1.0.4) | Apache-2.0 |

Each directory carries the upstream license. The ST7703 source uses the
ESP-IDF 6.1 structure member for RGB element order and keeps its context
until the underlying DPI panel confirms deletion.
The BQ25601 adapter uses charger control and status fields shared with the
BQ25601D, checks the A3 part number against the
[BQ25601 datasheet](https://www.ti.com/lit/ds/symlink/bq25601.pdf), and tracks
I²C errors before trusting the upstream cache. The vendored voltage conversion
uses the BQ25601 REG04 table, including the special code and maximum clamp.
The TUSB320LAI status adapter is
based on the [Texas Instruments datasheet](https://www.ti.com/lit/ds/symlink/tusb320lai.pdf)
and uses ESP-IDF's I²C driver.
