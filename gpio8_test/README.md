# GPIO8 test (LoRa power-switch pin), AgriNode / Beetle ESP32-C3

Checks that IO8 can safely drive the Wio-E5 power MOSFET of the rev B board:
it must be **HIGH (MOSFET off)** at reset, at boot and in deep sleep, and
**LOW (MOSFET on)** only when the firmware asks for it.

```
main/main.c                         test sequence
components/lora_power/              pure-C driver, reused later in the node firmware
components/lora_power/host_test/    PC test (21 checks)
components/bsp/                     ESP-IDF glue: GPIO8, blue LED (GPIO10), deep sleep
```

On the current board (rev A) **nothing is connected to IO8**, so the test is safe.
No extra parts needed. The Beetle's **blue LED** shows the state: LED ON = IO8 LOW = "LoRa ON".

## Run

```
cd gpio8_test
idf.py set-target esp32c3
idf.py build flash monitor
```

## Measure

Multimeter on DC 20 V: black on GND, red on **IO8** = J1 pin 6 = the Beetle pin marked **8**
(easiest on the bottom of the PCB at J1 pin 6, counted from the BAT pin).

| Step (shown in the log) | Blue LED | IO8 should read | Meaning on rev B |
|---|---|---|---|
| 1) RELEASED | off | 3.3 V | LoRa OFF (pull-up) |
| 2) ON = IO8 LOW | on | 0 V | LoRa ON |
| OFF = IO8 HIGH | off | 3.3 V | LoRa OFF |
| 3) DEEP SLEEP (20 s) | off | 3.3 V | LoRa OFF while sleeping |
| hold the Beetle's RST button | off | 3.3 V | LoRa OFF during reset |

The log also prints what the pad actually reads, so a wrong value shows up even without the multimeter.

Pass = all five rows correct. Then IO8 is fine for the MOSFET.

## Optional: current check

With the DM850 in series at J8 (jumper removed, 20 mA range), the current rises by about
**0.33 mA** while IO8 is LOW (the Beetle's 10 kΩ pull-up). For a clean reading set
`MIRROR_ON_BLUE_LED` to 0 in `main.c`, because the blue LED itself adds about 0.5 mA.

## Host test (PC)

```
cd components/lora_power
gcc -std=c99 -Wall -Wextra -Werror -Wconversion -O2 -Iinclude lora_power.c host_test/test_lora_power.c -o t && ./t
```
