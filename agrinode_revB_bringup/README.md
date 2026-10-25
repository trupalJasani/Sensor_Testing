# agrinode_revB_bringup — first test of the rev B PCB (sensors + LoRa)

This test is for the freshly soldered rev B board. The **battery holder, slide switch and button do not need to be fitted**: the Beetle runs from its USB-C cable. There is no deep sleep, so the serial monitor stays connected the whole time.

```
main/main.c                 test sequence and settings (top of the file)
components/bsp_revb/        ESP-IDF glue for the rev B pin map
components/lora_power/      GPIO8 Wio-E5 power switch     (pure C, 21 host checks, from gpio8_test)
components/battery_monitor/ battery divider on GPIO4      (pure C, 165 host checks, from battery_adc_test)
components/wio_e5/          Wio-E5 AT driver               (pure C, 644 host checks, = agri_node_test)
components/sht31/, soil_moisture/                          (= agri_node_test)
```

## Why the normal node firmware is not used for this

On rev B:
- The Wio-E5 only gets power while **GPIO8 is LOW**. The current `agri_node_test` never drives GPIO8, so its radio would stay off.
- The node also waits for the button after a power-on, and the button is not fitted yet (and now sits on GPIO2).

## What it tests

**Start-up checks (once):**

| Check | Pass condition |
|---|---|
| GPIO2 button | idle HIGH (Beetle 10 k pull-up) |
| button press | **you press and release the button within 10 s** (log: `Button test: …`). Each press and release is printed with the hold time; bounces < 20 ms are ignored. PASS = ≥ 1 press seen and the pin HIGH again at the end |
| GPIO8 LoRa EN | released = HIGH, so the Wio-E5 is OFF by default (R8 + Beetle pull-up) |
| rail soft start | the sensor rail is switched on **without a brown-out reset** (see below). Also prints when vcc_sensor passed ~2.5 V, seen on the SDA pin through the SHT31 board's pull-up |
| sensor switch (U1) | Davis input ≈ 0 V with the rail OFF, > 0.5 V with it ON. The I²C pins are parked as inputs first: after boot, GPIO21 (SDA) idles HIGH as the ROM's UART0 TX and would feed the OFF rail through the SHT31 board's pull-ups (issue H4) |
| soil sensor | > 0.5 V with the rail ON |
| I2C scan / SHT31 | device 0x45 found, plausible T/RH |
| Davis divider (R10/R11) | not clipped (raw < 4095). Also prints the back-calculated sensor voltage |
| battery input (GPIO4) | info only: **no cell fitted, so the value is meaningless** |
| Wio-E5 link | answers `AT` within 3 s after GPIO8 goes LOW. `AT` is polled every 100 ms, so the printed time is the module's real boot time (± 0.1 s). Prints `AT+VER` and `AT+MODE` |
| Wio-E5 switch (U4) | your multimeter readings in the two 10-s windows (see below) |

**Then, every 60 s:**
1. Read all sensors.
2. Switch the Wio-E5 ON (GPIO8 LOW) and wait for `AT`.
3. Configure P2P (868.1 MHz, SF10, same as the node).
4. Send one **16-byte packet in the node's format**.
5. Switch the Wio-E5 OFF again. The UART pins are parked first, so the unpowered module is not fed through TX/RX.

The packet contains real temperature, humidity, soil and a sequence number. The leaf field is 0, because the Davis is not yet calibrated for the new divider. Smith/RF are 0, and Irish Rules and the forecast are flagged "not available". The blue LED on the Beetle is ON exactly while the Wio-E5 is powered.

## Sensor-rail soft start (why the board browned out)

U1 (AO3401, sensor rail) has its gate wired straight to GPIO7. Unlike U4 (Wio-E5: R7 22 k + C6 22 nF), it has **no gate soft start**. A plain `GPIO7 LOW` connects the empty C1 (10 µF) plus the sensor boards to the Beetle's 3.3 V in well under a microsecond. The 3.3 V supply collapses below 2.51 V (the brown-out level) before the LDO can react, and the chip resets. This then repeats on every boot.

In the first run this did not happen. The I²C back-feed (H4) had already charged the rail to roughly 1 V, so the jump was smaller. Parking the I²C pins removed that "accidental soft start".

**Fix (firmware only, no PCB change):** `BSP_SensorPowerOnSoft()` drives GPIO7 from the LEDC peripheral:
1. Short LOW pulses at 20 kHz: 24 ns at first, growing slowly to ~1 µs over `SENSOR_SOFTSTART_MS` (200 ms).
2. A fast phase of about 30 ms up to 50 % duty.
3. GPIO7 is handed back to the normal GPIO, held LOW.

Each short pulse moves only a small charge into the rail, and the LDO refills its own capacitors in the 50 µs pause. The pad also uses the weakest driver, so the gate edges are slower.

- If the last reset was a brown-out, the next boot automatically uses a 4× longer ramp (800 ms) and says so in the log.
- `SENSOR_SOFTSTART_MS = 0` gives the old hard switch-on, for an A/B check of the cause.
- For a rev C board: give U1 the same gate network as U4 (series resistor + gate–drain capacitor). Then no firmware ramp is needed.

## Run

The gateway (ESP32-S3 with the current `agri_gateway`) must be running. Note that the gateway pushes these test values to Blynk as normal.

```bash
cd ~/esp_Project/Testting_components/agrinode_revB_bringup
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## Multimeter windows

When the log says **"Wio-E5 power test"**:
1. Set the meter to DC 20 V. Black probe on GND; red probe on **J5 pin 2** (the Wio-E5's 3V3 pin) or on **U4 pin 3** (drain).
2. First window, `Wio-E5 OFF`: expect **≈ 0 V**. A few hundred mV means the module is being fed through the UART lines.
3. Second window, `Wio-E5 ON`: expect **≈ 3.3 V**, and the blue LED is on.

## Reading the result

At the end of the start-up checks there is a **START-UP SUMMARY** with PASS/FAIL/INFO per line. In the cycles:
- On the node: `LoRa : ready after … ms, power cut confirmed (module restarted), TX DONE` and `totals : N sent, N TX DONE, 0 no answer`.
  - **"power cut confirmed"** means the module came back in its default mode (`LWABP`) although the previous cycle had set `TEST`, so U4 really removed its power.
  - **"STILL IN TEST MODE: module kept power!"** means it was never switched off: check U4, or a back-feed through the UART lines.
  - This check is only meaningful from the second cycle on.
- On the gateway: the same `Seq` numbers, 0, 1, 2 …, with temperature/humidity/soil equal to the node log. Compare the hex payload printed by the node with the one the gateway receives.

## If something fails

| Symptom | Check first |
|---|---|
| `rail soft start  rail already >2.5V before switch-on: inrush not tested` | Normal when the soil sensor and the Davis are unplugged: nothing drains C1, so the rail keeps its charge from the last run. The soft start is only really tested with both sensors plugged in. |
| `Brownout detector was triggered` right after `Sensor rail ON` | Run again: the next boot ramps 4× slower automatically. If it still resets, set `SENSOR_SOFTSTART_MS` to 1000, try a different USB port/cable (no hub), and measure vcc_sensor (J7-2) with the rail OFF. |
| button press FAIL "no press seen" | Pressed in time? Solder joints of the button; continuity GPIO2 (J1-8) ↔ button ↔ GND when pressed |
| button press FAIL "still LOW" | Button held, or a short between GPIO2 and GND (solder bridge) |
| GPIO8 reads LOW when released | R8 (220 k) missing or a short on the `lora_en` net |
| Wio-E5 no answer, J5-2 ≈ 0 V in the ON window | U4 (AO3401) orientation and solder joints, R7 (22 k) |
| Wio-E5 no answer, J5-2 ≈ 3.3 V | UART: R9/R12 (1 k) soldered? Module fully seated in J4/J5? |
| TX DONE on the node but nothing on the gateway | gateway running and on 868.1 MHz/SF10? Antenna on both? |
| sensor switch FAIL, OFF value high | Measure vcc_sensor (J7 pin 2) during the OFF phase: ≈ 0 V means U1 is fine and something feeds the ADC line; 0.5–2 V means the rail is back-fed or U1 leaks (R1, U1 orientation) |
| sensor switch FAIL, ON value low | Davis plugged into J6? |
| Davis CLIPPED | R10 = 47 kΩ and R11 = 100 kΩ fitted (not swapped)? |
| I2C: no 0x45 | SHT31 board on J3, its solder joints, sensor rail ON |
| soil FAIL | soil sensor plugged into J7? |

## Settings (top of `main/main.c`)

| Setting | Default | Meaning |
|---|---|---|
| `TX_PERIOD_S` | 60 | seconds between packets (keep ≥ 33 s: 1 % duty cycle at SF10) |
| `LORA_POWER_CYCLE` | 1 | 1 = Wio-E5 OFF between packets (as the node will do); 0 = stays ON |
| `SENSOR_SETTLE_MS` | 5000 | wait after the sensor rail is switched ON |
| `SENSOR_SOFTSTART_MS` | 200 | slow phase of the sensor-rail soft start; 0 = hard switch-on (old behaviour) |
| `METER_WINDOW_S` | 10 | length of each multimeter window |
| `BUTTON_WINDOW_S` | 10 | time to press the push button |

## First run on the board (2026-10-22, before the fixes below)

- **PASS:** GPIO2, GPIO8, soil, I²C (0x45), SHT31, Davis divider (1853 mV = ~2.72 V at the sensor, not clipped), Wio-E5 link (`+VER: 4.0.11`), TX DONE.
- **FAIL: sensor switch.** Davis 727 mV and soil 455 mV with the rail OFF. This was caused by the I²C pins still being in their boot state; fixed by parking them.
- The Wio-E5 answered 1554 ms after power-on. With the old 0.6 s retry steps that time is coarse; the 100 ms poll now measures it.
- Harmless warning `uart: GPIO 6 is not usable`: IDF 5.4.1 keeps the TX pin reserved after `uart_driver_delete()`. Now released with `esp_gpio_revoke()`.

## Second run (2026-10-22, with the I²C pins parked)

- Boot checks for GPIO2/GPIO8 passed. GPIO20/21 now start as inputs, so the I²C park works.
- Then a **brown-out reset loop**: `Sensor rail ON, settling 5000 ms ...` → `Brownout detector was triggered` → reset reason `brown-out` → again.
- **Cause (strong hypothesis):** inrush when U1 switches a now really empty rail with no gate soft start (see above). Fixed in firmware with the PWM soft start.

## Third run (2026-10-22, soft-start version, soil + Davis unplugged)

- **No brown-out.** But this run did not test the soft start: `SDA probe n/a` = SDA already read HIGH with the rail OFF, so vcc_sensor was still charged (> ~2.5 V) before the switch-on.
  - Cause: with no sensor plugged in, nothing drains C1. In addition, the I²C pins were parked only after the 1.5 s start delay. During that delay GPIO21 (U0TXD, idle HIGH) charged the rail through the SHT31 board's pull-ups.
  - Changed: the pins are now parked first thing in `app_main()`, and this case is reported as `INFO … inrush not tested`.
- Sensor switch and soil FAIL: expected with both sensors unplugged (Davis 4 mV = only R11 to GND; the soil input floats).
- SHT31 27.56 °C / 36.19 %RH; I²C only 0x45.
- **Wio-E5 boot time: 1446 ms** after GPIO8 LOW (13 polls of 100 ms), `+VER: 4.0.11`, `+MODE: LWABP`.

## Fourth run (2026-10-22 ~16:46, soft-start firmware, both sensors plugged in, Davis dry)

- **START-UP SUMMARY: 0 failures.**
- **Soft start works.** The log shows `rail soft start  no brown-out; rail >2.5V at 21 ms, 122 ns pulses`, and the SDA probe was valid (rail low before the switch-on). The run-2 brown-out is gone.
- **Sensor switch:** Davis OFF 4 mV → ON 2221 mV. The I²C back-feed (run 1: 727 mV) is gone. Soil OFF 582 mV is probably the soil board's own output capacitor still discharging from the previous run: the Davis shows the rail is OFF.
- **Davis dry through R10/R11:** raw 2987 (2221 mV at GPIO1, ~3.27 V at the sensor) after the 5 s settle, and raw 2990 at 27 s.
  - Dry ≈ 99 % of the 3.3 V supply.
  - Even the supply itself (3.3 V → 2245 mV at GPIO1) stays below the C3's 2500 mV limit, so the dry end can no longer clip.
- SHT31 23.18 °C / 44.77 %RH; soil 1448 mV (placeholder %).
- **Wio-E5:** 1450 ms boot time; cycle 0 `TX DONE`, payload `0000060956120000DA1600000080FF80`.
  - Cycle 0's "power cut confirmed" was not yet meaningful, because TEST mode is first set in cycle 0. The message now says so; the check is meaningful from cycle 1 on.
- This run used the build from 15:32 (ELF `dd3883ff…`), i.e. **without** the early pin parking. Rebuild to get it.

## Fifth run (2026-10-25, current build, soil in air, Davis dry, indoors)

- **0 failures.** The button gave 15 clean presses (70–110 ms). The soft start passed again.
- **Soil OFF 5 mV** (run 4: 582 mV). The early pin parking removed it, so it was boot back-feed, not the soil board's capacitor.
- **Soil in air:** raw ≈ 3255, 2410 mV, which reads 0 % with the placeholder calibration (air value 2800 is wrong for this board).
- **Davis dry:** ≈ 2855 mV at the sensor (run 4: 3265 mV, run 1: 2724 mV). "Dry" is not a stable value yet; see project doc 06.
- **Cycle 1: "power cut confirmed"**, so U4 really switches the Wio-E5 off.
- Still to confirm: gateway reception.

## Verified before flashing (2026-10-22, soft-start version)

- Builds with ESP-IDF v5.4.1 for esp32c3 with 0 warnings, including `-Wextra -Wconversion` on `main` and `bsp_revb`.
- PC simulation of the whole test: all checks PASS. After a simulated brown-out reset, the ramp is 800 ms and the log says why. Dead module and stuck switch give the same clear results as before.
- Rough circuit model (`tools/railsim.c`, build with `gcc -O2 railsim.c -lm`; many values guessed: Beetle 3.3 V capacitance 4.7–22 µF, wiring inductance, gate speed):
  - Hard switch-on: the 3.3 V node falls to **1.0–2.2 V** in every case (below 2.51 V), which matches the brown-out.
  - With the soft start: it never falls below **3.19 V**.
  - This is a model, not a measurement. The real test is the board run.

## Verified before flashing (2026-10-22, first version)

- Builds with ESP-IDF v5.4.1 for esp32c3 with 0 warnings, including `-Wextra -Wconversion` on `main` and `bsp_revb`.
- Driver host tests pass: lora_power 21, battery_monitor 165, wio_e5 644.
- The whole sequence was run on a PC against a simulated board and Wio-E5:
  - All checks PASS.
  - Packets are built correctly, e.g. `00002E0995150000171100000080FF80` = 23.50 °C, 55.25 %RH, soil 43.75 %, seq 0.
  - A dead module gives a clear FAIL, and the loop keeps running.
- This version was then run on the board: see "First run" above.
