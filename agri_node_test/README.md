# agri_node_test — Agrinode field node (PCB rev B)

Beetle ESP32-C3 on the Agrinode PCB **rev B**, powered by one Li-ion cell (battery holder, slide switch, TP4057 charger, DW01A protection). Each cycle it measures the battery, reads the SHT31, the soil sensor and the Davis 6420 leaf-wetness sensor, and runs the late-blight models (Smith, heuristic, RF, Irish Rules, 24 h forecast). Every 15 min it sends an 18-byte LoRa P2P packet through the Wio-E5, then goes back to deep sleep.

The rev A version is kept in `../agri_node_test_revA_backup_2026-10-26/`.

## What changed for rev B (2026-10-26)

| Part | Rev A firmware | Rev B firmware |
|---|---|---|
| Sensor rail GPIO7 | plain LOW | **PWM soft start** (~230 ms; 4× slower after a brown-out). A plain LOW browns out rev B. |
| Pins at boot | — | `BSP_Board_Init()` first: GPIO20/21 (I2C) and GPIO5/6 (UART) parked, GPIO8 released (no back-feed, H4) |
| Wio-E5 | `AT+LOWPOWER` sleep, UART pins held in deep sleep | **switched off by GPIO8** between cycles. Each cycle: power on → `WaitReady` (AT every 100 ms, max 3 s) → `ConfigP2P` (answers checked) → send (waits for `TX DONE`) → UART pins parked → power off |
| Button | GPIO4 | **GPIO2** (start gate, maintenance mode, deep-sleep wake) |
| Status LED GPIO10 | active LOW (so it was lit the whole awake time) | **active HIGH**, still lit the whole awake time and off in deep sleep (`LED_ON_WHILE_AWAKE 1` in `vc_application.c`; 0 = blink only) |
| Console | UART0 (GPIO20/21 = I2C pins!) + USB | **USB-Serial-JTAG only** |
| Leaf ADC | 1 sample | **64-sample mean** (as in the calibration tool) |
| Leaf calibration | DRY 4095 / WET 3416 | **DRY 2995.0 / WET 2477.7** (2026-10-25, rev B divider) |
| Wet threshold | index 7.5 (placeholder) | **index 3.0** (≈ 2891 counts, provisional) |
| Unplugged leaf sensor | read as fully wet (15) | raw < 1000 → error log, index 0 (dry) |
| Battery | not measured | GPIO4 divider (470k/470k), read at the start of each cycle before anything is switched on; 16 samples, min/max dropped |
| Low battery | — | < 3.50 V: warning. **< 3.30 V: protection mode** (no sensors, no radio, wake every 1 h) until > 3.45 V |
| Payload | 16 bytes | **18 bytes**: bytes 16–17 = battery mV (0 = not measured) |

**The gateway must be updated too** (`agri_gateway`, 2026-10-26). It accepts 18-, 16- and 14-byte packets and prints `Battery: 3.85 V`. The old gateway firmware rejects 18-byte packets ("Invalid payload size: 36").

**Still open:**
- The wet threshold rests on one valid drying run and one dew-free night.
- The soil sensor settle time has not been measured; the node keeps the old 2 s (`SENSOR_SETTLE_MS`). The Davis itself needs ≤ 0.2 s.
- Soil calibration is not done yet (placeholder 2800/1200; in air the sensor reads ~3255).
- The battery reading is not yet compared with a multimeter. If it is off, set a correction with `BATMON_SetCalFactor()`.
- In protection mode the 48-h model history gets a gap (no samples are written).

## Build and flash

```bash
. ~/esp/v5.4.1/esp-idf/export.sh
cd ~/Master_Thesis/agri_node_test
idf.py set-target esp32c3        # only needed once
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Because the console moved to USB-Serial-JTAG, `sdkconfig` changed. If the build complains, run `idf.py fullclean` once.

## What a good first boot looks like

1. Switch on with the slide switch. The blue LED lights up. The node logs `Battery: ... mV` and waits for the button: the blue LED blinks. Press the button on **GPIO2**: 4 quick blinks, then the LED stays on until the node goes to deep sleep. On every 15-min wake-up the LED is on for the ~6–8 s the node is awake.
2. `Sensor power ON (soft start ~230 ms, rail up after ~20 ms)`, with **no** "Brownout detector was triggered".
3. `Wio-E5 power ON (booting)`, then after the 2 s settle time: SHT31, soil and leaf readings.
   - Leaf line example: `raw 2995 (... mV at GPIO1, ~3270 mV at sensor), index 0.00 -> dry (threshold 3.0)`.
4. `Wio-E5 ready ~1300–1500 ms after power-on`, then `Radio Response: +TEST: TXLRPKT ... +TEST: TX DONE`, then `Wio-E5 power OFF`.
5. `Entering Deep Sleep Now (900 s)...`. The updated gateway prints the packet including `Battery: x.xx V`.

## Host tests (PC, no board)

```bash
cd components/wio_e5 && gcc -std=c99 -Wall -Wextra -Werror -Wconversion -O2 -Iinclude wio_e5.c host_test/test_wio_e5.c -o /tmp/twio && /tmp/twio
cd ../leaf_wetness_sensor && gcc -std=c99 -Wall -Wextra -Werror -Wconversion -O2 -Iinclude leaf_wetness.c host_test/test_leaf_wetness.c -lm -o /tmp/tleaf && /tmp/tleaf
cd ../battery_monitor && gcc -std=c99 -Wall -Wextra -Werror -Wconversion -O2 -Iinclude battery_monitor.c host_test/test_battery_monitor.c -o /tmp/tbat && /tmp/tbat
cd ../lora_power && gcc -std=c99 -Wall -Wextra -Werror -Wconversion -O2 -Iinclude lora_power.c host_test/test_lora_power.c -o /tmp/tlp && /tmp/tlp
cd ../.. && gcc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -O2 -Imain/host_test/stubs -Imain \
  -Icomponents/late_blight_ir/include -Icomponents/blight_forecast/include main/edge_ai.c \
  components/late_blight_ir/late_blight_ir.c components/blight_forecast/blight_forecast_features.c \
  components/blight_forecast/blight_forecast.c main/host_test/test_edge_ai.c -lm -o /tmp/tea && /tmp/tea
```

Results on 2026-10-26: wio_e5 721, leaf_wetness 723, battery_monitor 179, lora_power 21, edge_ai 45, late_blight_ir 296, blight_forecast 120 checks; all pass, also under ASan/UBSan.

## Layout

```
main/vc_application.c          state machine (INIT, WAIT_FOR_START, READ, PROCESS, TRANSMIT, IDLE, ERROR)
main/edge_ai.[ch]              models; EDGE_AI_LEAF_WET_INDEX_THRESHOLD
components/bsp                 all ESP-IDF hardware code (rev B pin map in bsp.h)
components/leaf_wetness_sensor pure C: Davis calibration + plausibility check
components/wio_e5              pure C: AT driver (+ WaitReady)
components/lora_power          pure C: Wio-E5 power switch (from the bring-up test)
components/battery_monitor     pure C: battery voltage, SoC estimate, protection thresholds
components/sht31, soil_moisture, late_blight_ir, blight_forecast
```
