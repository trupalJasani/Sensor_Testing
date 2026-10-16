# Button test on IO2 (rev B pin), AgriNode / Beetle ESP32-C3

On the new board the button moves from IO4 to **IO2**. This test checks, before ordering, that
IO2 works for the button: clean presses, deep-sleep wake-up, and normal booting.

```
main/main.c                     test sequence
components/button/              pure-C debounced button driver (reused in the node firmware)
components/button/host_test/    PC test (15 checks)
components/bsp/                 ESP-IDF glue: GPIO, blue LED, deep sleep
```

## Wiring (breadboard, Beetle off the PCB)

```
Beetle pin "2" ──── button ──── Beetle GND
```
- No resistor: the Beetle has a 10 kΩ pull-up on IO2. The test does **not** switch on the ESP32's
  internal pull-up, so it checks exactly the rev B situation.
- 4-leg tactile button: use two **diagonal** legs (the two legs on the same side are often always connected).
- The green LED / IO8 circuit can stay on the breadboard; it is not used here.

## Run

```
cd button_io2_test
idf.py set-target esp32c3
idf.py build flash monitor
```

## Tests

| # | Do | Expected log |
|---|---|---|
| 1 | Start, don't touch the button | `button reads RELEASED at start (pin HIGH = pull-up OK)` |
| 2 | During the 20 s live test, press a few times (short and long) | one `PRESSED` + one `released after … ms` per press, blue LED on while pressed, **no double counts** |
| 3 | After "DEEP SLEEP", wait ~5 s, then press | board wakes, log shows `wake-up: BUTTON (GPIO2)` |
| 4 | Let it sleep without pressing | after 60 s: `wake-up: timer` |
| 5 | **Hold** the button and press the Beetle's RST, keep holding 2 s, then release | Write down what happens: normal start (log appears) or no start / download mode. IO2 is a boot (strapping) pin, so this tells us whether pressing the button during a reset is harmless. |

Pass = tests 1–4 as expected. Test 5 is information for the final firmware and the user note.

## Comparing with your current board

To test the button already soldered on your PCB (IO4), set in `main.c`:
```
#define BUTTON_GPIO        (4)
#define BUTTON_INT_PULLUP  (true)
```

## Host test (PC)

```
cd components/button
gcc -std=c99 -Wall -Wextra -Werror -Wconversion -O2 -Iinclude button.c host_test/test_button.c -o t && ./t
```
