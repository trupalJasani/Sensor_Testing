# agrinode_gateway_rx_test — gateway receive test (ESP32-S3 + Wio-E5)

Receive-only test for the gateway, to confirm that the packets from the node (`agrinode_revB_bringup`) arrive.

- **No Wi-Fi, no Blynk, no passwords.** The test values do not reach the dashboard.
- Same hardware and wiring as `agri_gateway`:
  - Wio-E5 on **UART1, 9600 baud**.
  - **GPIO17** (ESP TX) → Wio-E5 RX; **GPIO18** (ESP RX) ← Wio-E5 TX; plus GND and 3.3 V.
- Same radio settings as the node: **868.1 MHz, SF10, BW125**, `AT+TEST=RFCFG,868.1,SF10,125,12,15,14,ON,OFF,OFF`.

```
main/main.c                 Wio-E5 setup, receive loop, printing
components/gw_rx/           pure C: line assembly, RX parsing, payload decode, seq/RSSI statistics
                            (host_test/: 63 checks; sim_bsp_gw.c = PC simulation of the Wio-E5)
components/bsp_gw/          ESP-IDF glue (UART1)
```

## Run

Node and gateway can both be on the same PC (two terminals). Find the gateway port first:

```bash
ls /dev/ttyUSB* /dev/ttyACM*       # the node (Beetle) is usually /dev/ttyACM0
```

Then run (replace the port):

```bash
cd ~/esp_Project/Testting_components/agrinode_gateway_rx_test
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Then start the node (`agrinode_revB_bringup`) in the other terminal.

## What you should see

```
[PASS] Wio-E5 link: answers AT
       firmware +VER: 4.0.11
[PASS] RF settings: +TEST: RFCFG F:868100000, SF10, BW125K, ...
[PASS] receiver ON (868.1 MHz, SF10, BW125, continuous)
==== packet 1 ====
  radio   : RSSI -47 dBm (strong), SNR 10 dB, 16 bytes
  hex     : "0000850991170000000000000080FF80"
  SHT31   : 24.37 C, 60.33 %RH
  ...
  seq     : 1 in order, 64.3 s after the last one
  totals  : 2 received, 0 missing, 0 restart(s); RSSI min/avg/max -49/-48/-47 dBm
```

- **The `hex` line must be identical to the node's `payload :` line** for the same seq. That is the end-to-end proof.
- About **64 s between packets** (60 s period + ~4 s Wio-E5 start and TX on the node).
- **Seq:**
  - `MISSING` = packets lost on air.
  - `node restarted` = seq went back to 0 because the node was reset (normal after flashing the node).
- **RSSI on the bench:** values like −30 … −70 dBm are normal. SF10 still works down to about −130 dBm. SNR above 0 dB is comfortable.
- `soil 0.00 %`, `leaf index 0.00`, Irish Rules / forecast `n/a`: correct for bring-up packets (leaf not sent, models not run, soil sensor in air).

## If something fails

| Symptom | Check |
|---|---|
| `[FAIL] Wio-E5 link: no answer to AT` | TX/RX crossed? GPIO17 → module RX, GPIO18 ← module TX; GND; module powered |
| `RF settings` FAIL | Module firmware/region; it must report 868100000, SF10, BW125K |
| Setup PASS but no packet | Node log shows `TX DONE`? Antennas on both? Node at the same 868.1 MHz/SF10 (it is in the bring-up test). Try 1–2 m distance |
| `not a node packet` | Another LoRa device on 868.1 MHz; ignore it |

## Verified (2026-10-25, not yet on the real gateway)

- Builds with ESP-IDF v5.4.1 for esp32s3 with 0 warnings, including `-Wextra -Wconversion`.
- `gw_rx` host tests: **63 passed**, under ASan/UBSan. They use the real node payloads from the rev B runs 4 and 5, plus the 18-byte format with battery (2026-10-26).
- PC simulation of the whole program against a simulated Wio-E5:
  - A lost packet is reported as `1 packet(s) MISSING`.
  - A node restart is reported as `node restarted`.
  - A 4-byte foreign packet is rejected.
  - A dead module gives a clear FAIL and a retry every 5 s.
