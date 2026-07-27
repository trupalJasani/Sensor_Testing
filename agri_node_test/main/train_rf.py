"""
Extends v3: adds a dew-point-depression (DPD) based leaf-wetness proxy to
the real DWD temperature+humidity data. This is what unlocks retraining
TomCast (which needs leaf wetness) on DWD data, not just Smith Period.

DPD = air_temperature - dew_point. A leaf is considered "wet" when DPD
drops below a threshold (default 3.0 degC), following the dew-point-
depression approach used in the leaf wetness duration (LWD) estimation
literature (Huber & Gillespie 1992; see also the constant-RH-threshold vs
DPD-model comparisons in Sentelhas et al.) as a documented alternative to
plain RH>=90% thresholds when no physical leaf wetness sensor is available.

Tune DPD_THRESHOLD_C below if you find it too sensitive/insensitive once
you can compare against your own Davis sensor's real readings later -
that comparison (DPD proxy vs real sensor at your site) is itself a good
validation result for your thesis.
"""

import glob
import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import accuracy_score, classification_report, confusion_matrix
import emlearn

DPD_THRESHOLD_C = 3.0   # "wet" when (air temp - dew point) <= this many degC

# ---------------------------------------------------------------------------
# 1. LOAD + CLEAN (same as v3)
# ---------------------------------------------------------------------------
INPUT_FILES = glob.glob("produkt_tu_stunde_*.txt")
if not INPUT_FILES:
    raise FileNotFoundError("No produkt_tu_stunde_*.txt found - see train_rf_v3 header for where to get one.")

frames = []
for f in INPUT_FILES:
    df = pd.read_csv(f, sep=";")
    df.columns = [c.strip() for c in df.columns]
    frames.append(df)
raw = pd.concat(frames, ignore_index=True)
raw["MESS_DATUM"] = pd.to_datetime(raw["MESS_DATUM"], format="%Y%m%d%H")
raw = raw[["MESS_DATUM", "TT_TU", "RF_TU"]].rename(columns={"MESS_DATUM": "ts", "TT_TU": "temp", "RF_TU": "hum"})
raw.loc[raw["temp"] <= -999, "temp"] = np.nan
raw.loc[raw["hum"] <= -999, "hum"] = np.nan
raw = raw.sort_values("ts").drop_duplicates("ts").set_index("ts")
full_index = pd.date_range(raw.index.min(), raw.index.max(), freq="h")
raw = raw.reindex(full_index)
raw[["temp", "hum"]] = raw[["temp", "hum"]].interpolate(limit=3)

# ---------------------------------------------------------------------------
# 2. DEW POINT + DPD-BASED WETNESS PROXY
# ---------------------------------------------------------------------------
def dew_point_celsius(temp_c, rh_pct):
    a, b = 17.625, 243.04
    rh_pct = np.clip(rh_pct, 1, 100)  # avoid log(0)
    gamma = np.log(rh_pct / 100.0) + (a * temp_c) / (b + temp_c)
    return (b * gamma) / (a - gamma)

raw["dew_point"] = dew_point_celsius(raw["temp"].to_numpy(), raw["hum"].to_numpy())
raw["dpd"] = raw["temp"] - raw["dew_point"]
raw["is_wet"] = raw["dpd"] <= DPD_THRESHOLD_C

print(f"DPD-proxy wet hours: {raw['is_wet'].sum()} / {len(raw)} "
      f"({100*raw['is_wet'].mean():.1f}% of hours flagged wet)")

# ---------------------------------------------------------------------------
# 3. LABEL GENERATORS
# ---------------------------------------------------------------------------
def smith_period_label(d1_min_t, d1_high_h, d2_min_t, d2_high_h):
    d1c = (d1_min_t >= 10.0) and (d1_high_h >= 11)   # hourly threshold, see v3 notes
    d2c = (d2_min_t >= 10.0) and (d2_high_h >= 11)
    if d1c and d2c: return 100
    if d1c or d2c: return 50
    return 0

def tomcast_hours_to_dsv(mean_temp_c, wet_hours):
    if mean_temp_c < 13.0: return 0
    if mean_temp_c < 18.0:
        if wet_hours <= 6: return 0
        if wet_hours <= 15: return 1
        if wet_hours <= 20: return 2
        return 3
    elif mean_temp_c < 21.0:
        if wet_hours <= 3: return 0
        if wet_hours <= 8: return 1
        if wet_hours <= 15: return 2
        if wet_hours <= 22: return 3
        return 4
    elif mean_temp_c < 26.0:
        if wet_hours <= 2: return 0
        if wet_hours <= 5: return 1
        if wet_hours <= 12: return 2
        if wet_hours <= 20: return 3
        return 4
    else:
        if wet_hours <= 3: return 0
        if wet_hours <= 8: return 1
        if wet_hours <= 15: return 2
        if wet_hours <= 22: return 3
        return 4

# ---------------------------------------------------------------------------
# 4. SLIDE 48H (SMITH) AND 24H (TOMCAST) WINDOWS
# ---------------------------------------------------------------------------
STRIDE_HOURS = 6
temp = raw["temp"].to_numpy()
hum = raw["hum"].to_numpy()
wet = raw["is_wet"].to_numpy()
ts = raw.index

X_smith, y_smith = [], []
X_tomcast, y_tomcast = [], []

for start in range(0, len(temp) - 48, STRIDE_HOURS):
    d1_t, d2_t = temp[start:start+24], temp[start+24:start+48]
    d1_h, d2_h = hum[start:start+24], hum[start+24:start+48]
    d1_w, d2_w = wet[start:start+24], wet[start+24:start+48]

    if np.isnan(d1_t).sum() > 2 or np.isnan(d2_t).sum() > 2:
        continue

    d1_min_t, d2_min_t = np.nanmin(d1_t), np.nanmin(d2_t)
    d1_high_h = np.nansum(d1_h >= 90.0)
    d2_high_h = np.nansum(d2_h >= 90.0)
    X_smith.append([d1_min_t, d1_high_h, d2_min_t, d2_high_h])
    y_smith.append(smith_period_label(d1_min_t, d1_high_h, d2_min_t, d2_high_h))

    # TomCast: rolling single most-recent day (day2), DPD-based wet hours
    wet_mask = d2_w
    wet_count = int(np.nansum(wet_mask))
    if wet_count == 0:
        X_tomcast.append([0.0, 0])
        y_tomcast.append(0)
    else:
        mean_wet_temp = np.nanmean(d2_t[wet_mask])
        dsv = tomcast_hours_to_dsv(mean_wet_temp, wet_count)
        X_tomcast.append([mean_wet_temp, wet_count])
        y_tomcast.append(dsv * 25)

X_smith, y_smith = np.array(X_smith), np.array(y_smith)
X_tomcast, y_tomcast = np.array(X_tomcast), np.array(y_tomcast)

print(f"\nSmith windows: {len(X_smith)}  classes: {dict(zip(*np.unique(y_smith, return_counts=True)))}")
print(f"TomCast windows: {len(X_tomcast)}  classes: {dict(zip(*np.unique(y_tomcast, return_counts=True)))}")

# ---------------------------------------------------------------------------
# 5. TRAIN BOTH (chronological split), EXPORT
# ---------------------------------------------------------------------------
def train_eval_export(X, y, name, feature_names):
    cutoff = int(len(X) * 0.8)
    X_tr, X_te = X[:cutoff], X[cutoff:]
    y_tr, y_te = y[:cutoff], y[cutoff:]
    clf = RandomForestClassifier(n_estimators=15, max_depth=6, random_state=42, class_weight="balanced")
    clf.fit(X_tr, y_tr)
    y_pred = clf.predict(X_te)
    acc = accuracy_score(y_te, y_pred)
    print(f"\n[{name}] held-out accuracy: {acc*100:.2f}%")
    print(classification_report(y_te, y_pred, digits=3, zero_division=0))
    cmodel = emlearn.convert(clf, method="inline")
    cmodel.save(file=f"rf_model_{name}_dpd.h", name=f"rf_{name}_dpd")
    print(f"Exported rf_model_{name}_dpd.h (features: {feature_names})")
    return acc

train_eval_export(X_smith, y_smith, "smith", ["day1_min_temp", "day1_high_hum_h", "day2_min_temp", "day2_high_hum_h"])
train_eval_export(X_tomcast, y_tomcast, "tomcast", ["mean_wet_temp", "wet_hours_dpd"])
