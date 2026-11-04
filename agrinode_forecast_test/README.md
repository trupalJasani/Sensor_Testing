# agrinode_forecast_test: on-device test of the Edge-ML forecast

This is a separate test firmware for the AgriNode (ESP32-C3, PCB rev B). It tests the
**24-h late-blight weather-risk forecast** on the real chip. The node firmware in
`../agri_node_test` is **not changed**: this project compiles the node's own
`main/edge_ai.c` and components straight from that folder. This folder must therefore
stay next to `agri_node_test` inside `Testting_components`.

| Test | Question it answers | Pass criterion |
|---|---|---|
| 1. Golden check | Does the forest on the ESP32-C3 give the same probability as scikit-learn? | 32/32 reference rows within 1e-6, same warning decision |
| 2. Replay | Does the node's **whole** forecast path (15-min ring buffer → hourly mean → features → forest → flags) reproduce the PC results on real summer weather, and does it warn before a real risk period? | 120/120 hours per episode within 1e-6 and the same warnings as `predictions_test.csv` |
| 3. Sensor dropout | What does the forecast do when the SHT31 fails for 2–4 h? | Reports the behaviour (no pass/fail): see "Known finding" |
| 4. Energy loop | Mean time per forecast and the current while forecasting | Gives µs; you read the current with the multimeter |

### The three replay episodes
These are real DWD 04104 data from the **test years** (never used for training or tuning):

| Episode | What happens | Lead before onset (PC) |
|---|---|---|
| `hit` | Smith-day risk window starting 2020-08-02 23:00 | the warning was on for 11 h before it |
| `miss` | risk window starting 2023-07-26 22:00 | no warning before it (0 h) |
| `false_alarm` | 19 h of warning from 2021-07-17 09:00, no Smith day within 2–3 days | n/a |

Each episode has 48 h of history and 120 issue hours. Every hour is written as **4
identical 15-min samples**, as the node does each cycle. The ring buffer starts at index
37, so the 48-h window wraps around the end of the buffer.

## Build and flash (ESP-IDF v5.4.1)
```sh
cd ~/esp_Project/Testting_components/agrinode_forecast_test
idf.py set-target esp32c3        # only the first time
idf.py build
ls -l /dev/serial/by-id/         # find the node's port
idf.py -p /dev/serial/by-id/<NODE-NAME> flash
```
Afterwards, flash the normal node firmware again from `../agri_node_test` (`idf.py -p … flash`).

## Run 1: with USB (tests 1–4, all printed results)
```sh
script -f -c "idf.py -p /dev/serial/by-id/<NODE-NAME> monitor" ~/Master_Thesis/ft_log_usb.txt
```
- The LED is on during tests 1–3, which take a few seconds.
- Then the energy test runs: 5 rounds, each with a 30-s **IDLE** phase (1 blink) and a
  30-s **FORECAST** phase (2 blinks). With USB connected you don't need to measure
  anything; this run gives the timing.
- After 5 blinks the board sleeps. Press the button to run everything again.
- Stop the monitor with **Ctrl+]**.

## Run 2: on battery (current for the energy figure)
USB powers the board, so the current at J8 only means something **without USB**.
1. Unplug USB. Remove the J8 jumper and connect the multimeter in the **200 mA** range
   across J8. Cell inserted.
2. Switch ON. The test starts by itself. The LED stays on during tests 1–3; on battery
   this can take a little longer, because console output has nowhere to go.
3. **1 blink → IDLE phase:** after about 10 s, write down the current.
   **2 blinks → FORECAST phase:** after about 10 s, write down the current.
   Do this for all 5 rounds.
4. After 5 blinks the node sleeps. Switch OFF, put the J8 jumper back, and measure the
   cell voltage with the multimeter.

The sensors and Wio-E5 are off in this test (node `BSP_Board_Init()`), so these
currents are lower than in a real node cycle. **The difference between FORECAST and
IDLE is the extra current of the CPU computing instead of waiting.**

## Evaluate
```sh
cd ~/esp_Project/Testting_components/agrinode_forecast_test
python3 tools/analyse_ft_log.py ~/Master_Thesis/ft_log_usb.txt \
        --i-idle-ma <mean IDLE mA> --i-forecast-ma <mean FORECAST mA> --vbat <cell V>
```
This writes `~/Master_Thesis/ft_log_usb_results/`:
- `summary.txt` with all numbers;
- the CSVs `golden.csv`, `replay.csv`, `faults.csv` and `energy.csv`;
- the figures `fig_replay_hit|miss|false_alarm` and `fig_fault`, each as `.png` and `.pdf`.

Energy per forecast = V × (I_FORECAST − I_IDLE) × t_forecast, where t_forecast is the
mean time from the energy loop. The script also reports the whole-board energy
V × I_FORECAST × t. The Beetle's 3.3 V regulator is linear, so the current at the cell
≈ the current at 3.3 V.

## Optional: -O2 timing comparison
The node is built with `-Og` (Debug), and so is this test (the `opt` field in
`FT,START`). For a comparison, run `idf.py menuconfig` → Compiler options → Optimization
Level → **Optimize for performance (-O2)**, then build, flash and log again. Change
nothing in the node.

## PC check (already run, 33/33 checks)
`sh host_test/run_host_test.sh` runs the same test core on the PC with the node's
sources. Results from 2026-11-04:
- Golden check 32/32, max |Δp| 3.7e-8.
- Replay: all three episodes 120/120, 0 warning mismatches, max |Δp| ≤ 8.7e-8.
- Lead before onset: `hit` 11 h and `miss` 0 h, as in `predictions_test.csv`.
- Within the replay windows:
  - `hit`: 56 hits, 23 false alarms, 13 misses.
  - `miss`: 33 hits, 47 false alarms, 36 misses.
  - `false_alarm`: 34 false-alarm hours.

The same numbers are expected on the ESP32-C3. Floating-point maths differs only in the
last bits (e.g. `logf`); the tolerance is 1e-6.

## Known finding (sensor dropout, test 3)
In `vc_application.c`, a **failed SHT31 read stores 0 °C / 0 %RH**. These values pass
the plausibility check in the hourly aggregation, so the forecast treats them as real
weather. PC result for a 2-h dropout 13 h before the `hit` onset:

| Dropout stored as | Hours affected | Warning flips | Flagged | No forecast | max \|Δp\| |
|---|---|---|---|---|---|
| 0 °C / 0 % (node now) | 49 | 4 | **0** (silent) | 0 | 0.27 |
| NAN, 2 h | 49 | 0 | 49 (`GAPS_FILLED`) | 0 | 0.10 |
| NAN, 4 h | 51 | 1 | 6 | 45 (> 3 h gap) | 0.45 |

Storing **NAN** on a failed read would turn a silent error into a flagged one. This is a
proposal; **the node code is not changed**.

## Files
| File | Content |
|---|---|
| `main/main.c` | Test sequence, LED signals, energy phases |
| `main/ft_core.c/.h` | Pure C test logic (also runs on the PC); output lines start with `FT,` |
| `main/ft_bsp.c/.h` | ESP-IDF glue: node BSP (pins, LED, battery), console, clock, deep sleep |
| `main/ft_episodes.h` | **Generated** episode data (`tools/make_episodes.py`) |
| `tools/make_episodes.py` | Regenerates `ft_episodes.h` and `tools/episodes.csv` from the DWD file and `predictions_test.csv` |
| `tools/analyse_ft_log.py` | Log → summary, CSV, figures, energy |
| `host_test/` | PC run of the test core |
| `sdkconfig.defaults` | Same CPU/RTOS/compiler settings as the node. **Task watchdog off** (the energy loop does not yield for 30 s); main stack 6144 B |

The golden rows come from `../agri_node_test/components/blight_forecast/host_test/bf_golden_vectors.h`.
