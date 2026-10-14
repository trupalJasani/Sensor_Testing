"""
Data preparation for the 24-h late-blight weather-risk forecast.

- Loads DWD hourly air temperature / relative humidity (TU product).
- Builds 48-h input windows and computes the features with the SAME C code
  that runs on the node (components/blight_forecast, through ctypes).
- Builds the label: "do the NEXT 24 hours meet the Smith-day criterion?"
  (min T >= 10 C and >= 11 h with RH >= 90 %).

Only issue times in the crop season (May-September) are used.
"""

import ctypes
import os
import subprocess
import tempfile

import numpy as np
import pandas as pd

HERE = os.path.dirname(os.path.abspath(__file__))
COMPONENT = os.path.normpath(os.path.join(HERE, "..", "components", "blight_forecast"))

HISTORY_H = 48
HORIZON_H = 24
SEASON_MONTHS = (5, 6, 7, 8, 9)
SMITH_TEMP_MIN_C = 10.0
SMITH_RH_PCT = 90.0
SMITH_RH_HOURS = 11

# Must match bf_feature_t in blight_forecast.h
FEATURES = [
    "t_now", "rh_now", "dpd_now",
    "t_min_24", "t_mean_24", "t_max_24", "rh_mean_24", "rh90_h_24",
    "rh90_h_prev24", "t_min_prev24",
    "rh_mean_6", "dt_6", "drh_6", "rh90_run",
    "h_since_tmax", "h_since_tmin", "dpd_mean_6", "smith_day_now",
]
N_FEATURES = len(FEATURES)


def load_dwd(path):
    """Hourly series on a gap-free index; gaps <= 3 h interpolated (as train_rf.py)."""
    df = pd.read_csv(path, sep=";")
    df.columns = [c.strip() for c in df.columns]
    df["ts"] = pd.to_datetime(df["MESS_DATUM"].astype(str), format="%Y%m%d%H")
    df = df.rename(columns={"TT_TU": "temp", "RF_TU": "rh"})[["ts", "temp", "rh"]]
    df.loc[df["temp"] <= -999, "temp"] = np.nan
    df.loc[df["rh"] <= -999, "rh"] = np.nan
    df = df.drop_duplicates("ts").set_index("ts").sort_index()
    df = df.reindex(pd.date_range(df.index.min(), df.index.max(), freq="h"))
    df[["temp", "rh"]] = df[["temp", "rh"]].interpolate(limit=3, limit_area="inside")
    return df


def build_c_library(model_header_dir=None):
    """Compiles the node's C feature code (+ optional model) into a shared library."""
    # unique name per build: dlopen() returns the already-loaded library for a
    # path it has seen, so reusing one path would silently keep the old code
    tag = "model" if model_header_dir is not None else "features"
    fd_, out = tempfile.mkstemp(prefix=f"libbf_{tag}_", suffix=".so")
    os.close(fd_)
    srcs = [os.path.join(COMPONENT, "blight_forecast_features.c"),
            os.path.join(COMPONENT, "host_test", "bf_batch.c")]
    incs = ["-I", os.path.join(COMPONENT, "include")]
    if model_header_dir is not None:
        srcs.append(os.path.join(COMPONENT, "blight_forecast.c"))
        incs += ["-I", model_header_dir]
    else:
        srcs.append(os.path.join(COMPONENT, "host_test", "bf_no_model.c"))
    cmd = ["gcc", "-std=c99", "-O2", "-shared", "-fPIC", *incs, *srcs, "-lm", "-o", out]
    subprocess.run(cmd, check=True)
    return ctypes.CDLL(out)


def c_features(lib, temp, rh, ends):
    """Features for windows ending at `ends`, computed by the node's C code."""
    temp32 = np.ascontiguousarray(np.nan_to_num(temp, nan=0.0), dtype=np.float32)
    rh32 = np.ascontiguousarray(np.nan_to_num(rh, nan=0.0), dtype=np.float32)
    valid = np.ascontiguousarray(np.isfinite(temp) & np.isfinite(rh), dtype=np.uint8)
    ends = np.ascontiguousarray(ends, dtype=np.int64)
    out = np.zeros((len(ends), N_FEATURES), dtype=np.float32)
    status = np.zeros(len(ends), dtype=np.int32)
    fp = ctypes.POINTER(ctypes.c_float)
    lib.bf_batch_features(temp32.ctypes.data_as(fp), rh32.ctypes.data_as(fp),
                          valid.ctypes.data_as(ctypes.POINTER(ctypes.c_ubyte)),
                          ends.ctypes.data_as(ctypes.POINTER(ctypes.c_long)),
                          ctypes.c_long(len(ends)),
                          out.ctypes.data_as(fp),
                          status.ctypes.data_as(ctypes.POINTER(ctypes.c_int)))
    return out, status


def c_predict(lib, X):
    """Probabilities from the C model (requires a library built with a model)."""
    X = np.ascontiguousarray(X, dtype=np.float32)
    p = np.zeros(len(X), dtype=np.float32)
    fp = ctypes.POINTER(ctypes.c_float)
    lib.bf_batch_predict(X.ctypes.data_as(fp), ctypes.c_long(len(X)), p.ctypes.data_as(fp))
    return p


def smith_window_flags(temp32, rh32):
    """flag[e] = 1 if the 24 h window ENDING at index e meets the Smith-day criterion."""
    n = len(temp32)
    t = pd.Series(temp32)
    hi = pd.Series((rh32 >= SMITH_RH_PCT).astype(np.float32))
    nan = pd.Series((~np.isfinite(temp32) | ~np.isfinite(rh32)).astype(np.float32))
    tmin = t.rolling(24).min().to_numpy()
    c90 = hi.rolling(24).sum().to_numpy()
    has_nan = nan.rolling(24).sum().to_numpy() > 0
    flag = (tmin >= SMITH_TEMP_MIN_C) & (c90 >= SMITH_RH_HOURS)
    flag = flag.astype(np.float32)
    flag[has_nan | ~np.isfinite(tmin)] = np.nan
    assert len(flag) == n
    return flag


def build_dataset(path, lib):
    """Returns a DataFrame with issue time, year, label, features (one row per issue hour)."""
    df = load_dwd(path)
    temp = df["temp"].to_numpy(dtype=np.float64)
    rh = df["rh"].to_numpy(dtype=np.float64)
    temp32 = temp.astype(np.float32)
    rh32 = rh.astype(np.float32)
    n = len(df)
    idx = df.index

    flag = smith_window_flags(temp32, rh32)          # window ending at e
    ends = np.arange(HISTORY_H - 1, n - HORIZON_H)
    in_season = np.isin(idx.month[ends], SEASON_MONTHS)
    ends = ends[in_season]

    # history window must be complete (no NaN) for training rows
    bad = pd.Series((~np.isfinite(temp) | ~np.isfinite(rh)).astype(np.int8))
    hist_bad = bad.rolling(HISTORY_H).sum().to_numpy()[ends] > 0
    label = flag[ends + HORIZON_H]                    # window t+1 .. t+24
    keep = (~hist_bad) & np.isfinite(label)
    ends = ends[keep]
    label = label[keep].astype(np.int8)

    X, status = c_features(lib, temp, rh, ends)
    ok = status == 0
    out = pd.DataFrame(X[ok], columns=FEATURES)
    out.insert(0, "issue_time", idx[ends[ok]])
    out.insert(1, "year", idx[ends[ok]].year)
    out.insert(2, "end_index", ends[ok])
    out["y"] = label[ok]
    out.attrs["n_dropped_status"] = int((~ok).sum())
    out.attrs["flag_series"] = flag
    out.attrs["index"] = idx
    return out
