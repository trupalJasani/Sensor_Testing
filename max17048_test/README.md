# MAX17048 fuel-gauge test (AgriNode, Beetle ESP32-C3)

Stand-alone ESP-IDF project. It only tests the battery gauge: no LoRa, no sleep.

```
main/main.c                       test program (prints every 5 s)
components/max17048/              pure-C driver (no ESP-IDF code)
components/max17048/host_test/    PC test with a simulated chip (48 checks)
components/bsp/                   ESP-IDF glue: I2C on GPIO21/20, GPIO7 sensor power
```

## Wiring (current board, no PCB change)

| MAX17048 breakout | AgriNode | Note |
|---|---|---|
| CELL / VDD / BAT+ (battery side) | battery + : J8 pin on the Beetle side (`/bat`) | the chip measures **its own supply**, so it must sit on the battery, never on 3.3 V |
| GND | GND (e.g. J3 pin 2) | |
| SDA | IO21 (J3 pin 4) | shared with the SHT31 |
| SCL | IO20 (J3 pin 3) | shared with the SHT31 |
| VIN / VCC for the I2C pull-ups (only if the board has one) | 3.3 V: J3 pin 1 for this test, Beetle 3V3 (J2-2) for a permanent install | |
| ALRT, QST | not connected | |

**Check before connecting SDA/SCL:** power the breakout from the battery only and measure SDA and SCL to GND.
About 3.3 V or 0 V is fine. If you see the battery voltage (3.7–4.2 V), the pull-ups go to the battery,
which is too high for the ESP32-C3 pins (max 3.6 V); do not connect it like that.

If the breakout has its own battery JST connectors, do not route the cell current through it on this board;
just tap the voltage as in the table.

The test switches the sensors ON (GPIO7 LOW) on purpose: the SHT31 board's pull-ups hang on
`vcc_sensor`, and with the sensors OFF they pull the I2C bus low.

## Build and flash (ESP-IDF terminal)

```
cd max17048_test
idf.py set-target esp32c3
idf.py build flash monitor
```

## Expected output

```
I (1530) FG_TEST: === MAX17048 fuel gauge test ===
I (1650) FG_TEST: I2C scan (SDA=GPIO21, SCL=GPIO20):
I (1660) BSP:   found 0x36  (MAX17048 fuel gauge)
I (1670) BSP:   found 0x45  (SHT31)
I (1680) FG_TEST: MAX17048 found
I (1680) FG_TEST: VERSION=0x0012  CONFIG=0x971C (RCOMP=0x97, empty alert 4 %)  HIBRT=0x8030  ID=0x..
W (1690) FG_TEST: reset flag set: the gauge was just powered. ...
I (2200) FG_TEST:    #   VCELL[V]   SOC[%]   CRATE[%/h]   hibernate  flags
I (2210) FG_TEST:    0    3.987     78.40      -0.21        no       0x00
```

## What to check

1. **Voltage:** compare VCELL with the DM850 (20 V range) at the same point (J8 `/bat` to GND). It should agree within about ±0.02 V.
2. **SOC:** right after the gauge is powered, SOC is an estimate from the voltage. It becomes reliable after some hours of use or one charge cycle.
3. **CRATE:** positive while charging over USB, negative on battery. Small values (< 1 %/h) with the node idle are normal.
4. **hibernate = yes** after a while with a steady load: the chip then updates only every ~45 s and draws ~3 µA. That is the normal low-power state.
5. **Errors:** "no answer from 0x36" means wiring or supply; "not a MAX17048" means the chip is a MAX17043/44 (different scaling).

`DO_QUICK_START` in `main.c` restarts the SOC estimate at boot. Leave it at 0 unless the first SOC is clearly wrong right after connecting the battery.

## Host test (PC, no hardware)

```
cd components/max17048
gcc -std=c99 -Wall -Wextra -Werror -Wconversion -O2 -Iinclude max17048.c host_test/test_max17048.c -lm -o t && ./t
```
