# late_blight_ir: Irish Rules with measured leaf wetness

Pure C99 component (no ESP-IDF headers, no `malloc`). It computes **effective blight hours (EBH)** for potato late blight (*Phytophthora infestans*) from hourly temperature, relative humidity and leaf wetness.

## Model

The model is Bourke (1953), as described and re-evaluated in Cucak et al. (2019), *Agronomy* 9(9):515, doi:10.3390/agronomy9090515.

1. An hour is **favourable** if T ≥ T<sub>t</sub> and RH ≥ RH<sub>t</sub>.
2. **Sporulation** is SD<sub>t</sub> consecutive favourable hours.
3. The model then checks **leaf wetness** from 3 h before to 3 h after the last sporulation hour.
   - If the leaf was wet, EBH counting starts at that last sporulation hour.
   - If the leaf was dry, the run must last 4 more consecutive hours, and counting starts then.
4. During the **infection period**, every favourable hour adds 1 EBH. The period ends after more than 5 consecutive unfavourable hours.
5. The **warning** threshold is applied by the consumer, here the gateway. Met Éireann uses 12 EBH. Cucak et al. propose 4–11 EBH, set during the season.

| Preset | RH<sub>t</sub> | T<sub>t</sub> | SD<sub>t</sub> |
|---|---|---|---|
| `IR_PRESET_BOURKE_ORIGINAL` (node default) | 90 % | 10 °C | 12 h |
| `IR_PRESET_CUCAK2019_LOW_RISK` | 88 % | 10 °C | 10 h |
| `IR_PRESET_CUCAK2019_OPTIMISED` | 88 % | 12 °C | 10 h |

EBH counts hours favourable for infection. **It is not a disease probability**, and it has not been validated against outbreaks at this site.

## Differences from the published model

- **Leaf wetness is measured.** The original model has no wetness sensor; it calls the leaf wet if ≥ 0.1 mm of rain fell in the ±3 h window. Here `ir_hour_t.wet` comes from the Davis 6420 on the node. Offline runs can still feed a rain or RH proxy into the same core.
- **Missing T/RH hours count as unfavourable.** The authors' R code interpolates gaps of up to 7 h instead.
- **One quirk in the authors' R code is not reproduced.** In `IrishRulesModel` (github.com/mladencucak/AnalysisPLBIreland), the hour right after a 1-hour break is never checked. That can drop one EBH or end a period early. This module follows the written rule instead: breaks of ≤ 5 h are tolerated.

### Finding from the authors' code (relevant to the thesis)

Cucak et al. report that a combined "rain + RH ≥ 90 %" wetness estimator scores better than rain alone. In their R code that estimator is an OR across the ±3 h window. The window always contains the last sporulation hour, and that hour already has RH ≥ RH<sub>t</sub>. So with RH<sub>t</sub> = 90 % the estimator **always says wet**, and the 4-hour dry delay never applies.

The cross-check below confirms this: with that proxy there were 0 dry starts in 3000 random sequences. With measured leaf wetness, the dry-start rule is actually tested.

## Node integration

- `main/edge_ai.c` → `run_irish_rules_risk()` turns the 192 × 15-min ring buffer into 48 hourly values:
  - mean T and mean RH from ≥ 3 of 4 valid samples;
  - an hour is wet if ≥ 2 of its 4 samples have a Davis index ≥ `EDGE_AI_LEAF_WET_INDEX_THRESHOLD`.
- That threshold is currently **7.5, a placeholder** (midpoint of 0–15). Set it from field calibration.
- LoRa payload:
  - byte 12 = maximum EBH in the last 48 h (saturates at 255);
  - byte 13 = `IR_FLAG_*` status bits (see `edge_ai.h`).
- If an infection period started before the 48-h window, the EBH is underestimated. The `IR_FLAG_START_TRUNCATED` bit marks this case.

## Tests (host PC, no hardware)

Run from this folder:

```sh
gcc -std=c99 -Wall -Wextra -Wpedantic -Wconversion -Werror -O2 -Iinclude \
    late_blight_ir.c host_test/test_late_blight_ir.c -lm -o /tmp/test_ir && /tmp/test_ir

gcc -std=c99 -O2 -shared -fPIC -Iinclude late_blight_ir.c -o /tmp/libir.so
python3 host_test/crosscheck_reference.py /tmp/libir.so
```

Results on 2026-10-13:

- **Unit tests:** 296 checks passed, also with ASan/UBSan. They cover:
  - the 12 h and 16 h starts;
  - the edges of the wetness window;
  - a 5 h break (period continues) vs a 6 h break (period ends);
  - inclusive thresholds;
  - invalid and unknown data;
  - a retroactive start when the window is still incomplete;
  - all three presets;
  - hourly aggregation;
  - the 1500-vs-index unit check.
- **Cross-check:** the R code was transcribed line by line, including its 20-hour padding and 1-based indexing, and run on 3000 random sequences of 240 h.
  - C matched the R code with the 1-hour-break check added on **3000 / 3000** sequences.
  - C matched the unmodified R code on 1871. Every one of the 1129 differences contains a 1-hour break inside an infection period.
  - That rate depends on the random generator. It is not a real-weather rate.
