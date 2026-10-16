# Battery voltage test with a resistor divider (AgriNode, Beetle ESP32-C3)

Stand-alone ESP-IDF project. No extra chip needed: two equal resistors.
Same method as the battery divider on the new rev B board (R4/R5 on IO4).

```
main/main.c                              test program (prints every 2 s)
components/battery_monitor/              pure-C driver (no ESP-IDF code)
components/battery_monitor/host_test/    PC test (165 checks)
components/bsp/                          ESP-IDF glue: ADC1 one-shot + eFuse calibration
battery_divider_wiring.png               wiring picture
```

## Wiring on the current board (rev A), see `battery_divider_wiring.png`

```
J8 pin (battery +) ---[ R_TOP 100k ]---+---[ R_BOTTOM 100k ]--- J7 pin 3 (GND)
                                       |
                                  J7 pin 1 (soil signal = GPIO0, has 100 nF on the board)
```

- **Unplug the soil sensor** from J7 during the test. J7 pin 2 (VCC) stays unconnected.
- Both resistors must have the **same value** (anything from 100 k to 1 M). Other values: change `R_TOP_OHM` / `R_BOTTOM_OHM` in `main.c`.
- Keep the J8 jumper cap on; with the switch ON both J8 pins are battery +.
- **Never** connect battery + directly to J7 pin 1 (4.2 V is above the 3.6 V the ESP32 pin allows).
- Female jumper wires fit on the J7 pins. Wire with the switch OFF.

## Build and flash (ESP-IDF terminal)

```
cd battery_adc_test
idf.py set-target esp32c3
idf.py build flash monitor
```

## Expected output

```
I (1530) BAT_TEST: === Battery voltage test (divider on GPIO0, 100000 / 100000 ohm) ===
I (1540) BAT_TEST: ADC calibration: eFuse curve fitting (good)
I (1540) BAT_TEST:     #   ADC pin [mV]   Battery [mV]   ~SOC (only at rest)
I (1590) BAT_TEST:     0       1952           3904         62 %
```

## Check and calibrate

1. Measure the battery with the DM850 (20 V range) at J8 to GND, at the same time.
2. The printed `Battery [mV]` should be within about ±50 mV.
3. For better accuracy set `CAL_FACTOR = multimeter / printed` (e.g. 3.950 V / 3.904 V = 1.012), flash again.
4. The % value is only a rough guess and only valid with the battery at rest (not charging, no load).

Hints in the output: `no voltage` = wiring or switch OFF; `above ADC range` = resistors not equal.

## On the new rev B board

No wiring needed (R4/R5 470k + C5 are on the board). In `main.c` set:

```
#define BAT_ADC_GPIO   (4)
#define R_TOP_OHM      (470000U)
#define R_BOTTOM_OHM   (470000U)
```

## Host test (PC, no hardware)

```
cd components/battery_monitor
gcc -std=c99 -Wall -Wextra -Werror -Wconversion -O2 -Iinclude battery_monitor.c host_test/test_battery_monitor.c -o t && ./t
```
