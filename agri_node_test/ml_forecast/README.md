# ml_forecast: Edge-ML forecast of late-blight risk weather (next 24 h)

**Question the model answers on the node:** *"Will the next 24 hours meet the Smith-day criterion?"* The criterion is a minimum temperature ≥ 10 °C and at least 11 h with RH ≥ 90 %.

The Smith Period rule on the node only looks **back** at the last 48 h. This model looks **ahead**, so it can warn before a risk window starts.

The labels come from the rule applied to the following 24 h of real weather. **No disease observations were used**, so the output is a forecast of risk *weather*, not of infection.

## Data and protocol

| Item | Value |
|---|---|
| Data | DWD hourly air temperature and RH (TU product), station 04104, 1948-01-01 to 2024-12-31. 675k hours, 0 missing time stamps, 163 missing temperature and 171 missing RH values (gaps ≤ 3 h interpolated). |
| Samples | One per issue hour, May–September only: 282,131 rows. |
| Inputs | 18 features from the last 48 hourly values. Computed by **the node's own C code** (`components/blight_forecast/blight_forecast_features.c`), called from Python through ctypes, so training and node use identical features. |
| Split by year | Train 1948–2004 (209,304 rows), validation 2005–2014 (36,648), **test 2015–2024 (36,179)**. |
| Base rate (share of positive labels) | 15.4 % train, 15.3 % validation, 13.8 % test. |
| Selection | Model size, model choice and the warning threshold use **validation years only**. The test years were scored once. |
| Threshold rule | The threshold maximises the critical success index (CSI = hits / (hits + misses + false alarms)) on validation: **0.26**. |
| Uncertainty | 95 % confidence intervals from 1000 bootstrap resamples of whole test years. |

## Results on the held-out test years 2015–2024

| Model | ROC-AUC | Avg. precision | Brier skill | CSI | Hit rate | False-alarm ratio | Size in flash |
|---|---|---|---|---|---|---|---|
| **Random forest, 10 trees × depth 8 (deployed)** | **0.823** | **0.451** | **0.196** | **0.307** | **0.569** | 0.600 | **37 KB** |
| Logistic regression (18 weights) | 0.825 | 0.437 | 0.186 | 0.303 | 0.600 | 0.620 | < 0.1 KB |
| Single decision tree, depth 8 | 0.793 | 0.406 | 0.154 | 0.278 | 0.592 | 0.656 | 4 KB |
| Gradient boosting, 200 trees (reference, not exported) | 0.834 | 0.485 | 0.220 | 0.317 | 0.543 | 0.567 | — |
| Persistence ("last 24 h was a Smith day") | — | — | — | 0.222 | 0.364 | 0.637 | rule |
| No skill (base rate) | 0.5 | 0.138 | 0 | — | — | — | — |

- **Forest vs persistence:** CSI 0.307 [95 % CI 0.271–0.338] vs 0.222 [0.175–0.256]. The difference is +0.070 to +0.108 (95 % CI, excludes 0). The forest is better in **every one of the 10 test years** (`results/per_year_test.csv`).
- **Early warning:** in 171 onsets of a risk window, the forest was already warning for:
  - ≥ 1 h before the onset in 84 % of cases;
  - ≥ 6 h in 46 %;
  - ≥ 12 h in 15 %;
  - median 5 h.

  By definition, persistence never warns before an onset.
- **Reliability:** forecast probabilities match observed frequencies closely (`fig5`), so "35 %" means roughly 35 % of such situations became a Smith day in the DWD record.
- **Honest caveats:**
  - A logistic regression is almost as good. The forest's edge is small (AP +0.014, CSI +0.004).
  - Gradient boosting is better still, but larger and not exported.
  - The forest was kept because it has the best skill per byte among the exported models (`fig1`).
  - About 6 in 10 warnings are false alarms. That is the price of catching ~57 % of risk days at this threshold; for crop protection a miss costs more than a false alarm.

## Node implementation

- **Code:** `components/blight_forecast` (pure C99, static allocation).
- **Model tables:** 10 trees, 4242 nodes, 38,198 bytes of tables in **flash** (`.flash.rodata`), max depth 8.
- **RAM:** +764 B static RAM, including 384 B of feature working buffers that were moved off the 3.5 KB main-task stack. The node now logs the main task's unused stack before each sleep.
- **Threshold handling:** thresholds are float32 values rounded *down* from scikit-learn's doubles. So the C forest takes exactly the same branch as scikit-learn for every float32 input; the int16 truncation issue F4 cannot occur.
- **Verified:** on all 36,179 test rows, the C probability matches scikit-learn within 1.1e-7, with **0** warning decisions different.
- **Timing:** the node logs feature and inference time in µs each cycle (`Forecast next 24h … features X us, forest Y us`). Use these numbers for the thesis; they are not estimated here.
- **Payload:** byte 14 = probability × 100 (0xFF = not available); byte 15 = flags plus model version (`FC_FLAG_*`). The packet grows from 14 to 16 bytes, which adds about 41 ms airtime at SF10.

## Limits (state them in the thesis)

1. **Domain shift.** DWD measures at 2 m in a standard screen; the node sits in the canopy, where RH is usually higher. Before trusting the absolute numbers, compare a few weeks of node T/RH with the nearest DWD station.
2. **Inputs.** The model sees only local T/RH history. No weather-forecast data, no leaf wetness (DWD has none) and no time of day (the node has no clock). It uses the diurnal temperature shape as a proxy instead.
3. **Model type.** It forecasts the **rule** one day ahead. Agreement with blight outbreaks is not tested.
4. **Station.** One station only, trained on May–September; validity for other sites is not shown.

## Reproduce

```sh
cd agri_node_test/ml_forecast
python3 train_forecast.py --data ../main/produkt_tu_stunde_19480101_20241231_04104.txt   # ~4-5 min
python3 make_figures.py results
```

This writes the following files:
- `results/metrics.json`, `sweep.csv`, `per_year_test.csv`, `lead_times_test.csv`, `feature_importance.csv`, `predictions_test.csv` and `model.pkl`;
- `results/figures/fig1…fig5` as `.png` and `.pdf`;
- the C tables in `components/blight_forecast/blight_forecast_model.h`;
- the C test vectors in `components/blight_forecast/host_test/bf_golden_vectors.h`.

It needs Python 3 with numpy, pandas, scikit-learn and matplotlib, plus gcc (the node's C feature code is compiled on the fly). It was run with scikit-learn 1.9.1, numpy 2.5.3 and pandas 3.0.5; your venv has scikit-learn 1.9.0. `random_state = 42` everywhere.

Host tests:
```sh
cd components/blight_forecast
gcc -std=c99 -Wall -Wextra -Wpedantic -Wconversion -Werror -O2 -Iinclude -I. \
    blight_forecast_features.c blight_forecast.c host_test/test_blight_forecast.c -lm -o /tmp/t && /tmp/t
```
