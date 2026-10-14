#!/usr/bin/env python3
"""
Cross-check of late_blight_ir.c against the authors' published R code.

Reference: IrishRulesModel() in Analysis.Rmd of
https://github.com/mladencucak/AnalysisPLBIreland (Cucak et al. 2019,
Agronomy 9(9):515). It is transcribed below line by line, keeping R's
1-based indexing and its 20-hour zero padding, so its quirks are preserved.

The C module is loaded through ctypes and both are run on random hourly
weather sequences with persistent humid spells. Every disagreement is
classified. Expected result:
  * C == reference with the "offset-1" fix applied, on every sequence;
  * C != unmodified reference only where a 1-hour break occurs inside an
    infection period (the R loop never checks the hour right after a
    1-hour break; see the C header).

Usage (from the component folder):
  gcc -std=c99 -O2 -shared -fPIC -Iinclude late_blight_ir.c -o /tmp/libir.so
  python3 host_test/crosscheck_reference.py /tmp/libir.so
"""

import ctypes
import random
import sys


# ---------------------------------------------------------------------------
# R reference, transcribed (Analysis.Rmd, IrishRulesModel, lines 94-222)
# ---------------------------------------------------------------------------
def r_irish_rules(temp, rh, rain, rh_thresh=90.0, temp_thresh=10.0, hours=12,
                  lw_mode="rain", lw_rhum_threshold=90.0, fix_offset1=False):
    """Returns the per-hour EBH vector exactly as the R function would.

    fix_offset1=True adds the missing check of criteria[n + 1] after a
    1-hour break, i.e. the written rule "breaks of <= 5 h are tolerated".
    """
    n_orig = len(temp)
    wet_before, wet_after = 3, 3
    # "Out of bounds" padding with 20 zeros
    rain = list(rain) + [0.0] * 20
    temp = list(temp) + [0.0] * 20
    rh = list(rh) + [0.0] * 20
    length = len(temp)

    def at(vec, k):  # R 1-based accessor
        return vec[k - 1]

    criteria = [1 if (temp[i] >= temp_thresh and rh[i] >= rh_thresh) else 0
                for i in range(length)]
    # ave(criteria, cumsum(criteria == 0), FUN = cumsum) == current run length
    criteria_sum, run = [], 0
    for c in criteria:
        run = run + 1 if c == 1 else 0
        criteria_sum.append(run)

    risk = [0] * length
    criteria_met12 = [1 if s >= hours else 0 for s in criteria_sum]
    idx = [i + 1 for i in range(length) if criteria_sum[i] == hours]

    for j in idx:
        window = range(j - wet_before, j + wet_after + 1)
        rain_sum = sum(at(rain, k) for k in window)
        if lw_mode == "rain":
            wet = rain_sum >= 0.1
        else:  # "rainrh"
            wet = any(at(rh, k) >= lw_rhum_threshold for k in window) or rain_sum >= 0.1
        n = j if wet else j + 4
        s = at(criteria_met12, n)
        m = n - 1
        while s == 1:
            risk[n - 1] = at(risk, m) + 1
            n = n + 1
            m = n - 1
            s = at(criteria, n)
            if fix_offset1 and s == 0 and at(criteria, n + 1) == 1:
                n = n + 1
                s = 1
            elif s == 0 and at(criteria, n + 2) == 1:
                n = n + 2
                s = 1
            elif s == 0 and at(criteria, n + 3) == 1:
                n = n + 3
                s = 1
            elif s == 0 and at(criteria, n + 4) == 1:
                n = n + 4
                s = 1
            elif s == 0 and at(criteria, n + 5) == 1:
                n = n + 5
                s = 1
    return risk[:n_orig]


# ---------------------------------------------------------------------------
# C module through ctypes
# ---------------------------------------------------------------------------
class IrParams(ctypes.Structure):
    _fields_ = [("temp_min_c", ctypes.c_float), ("rh_min_pct", ctypes.c_float),
                ("sporulation_hours", ctypes.c_uint8), ("wet_window_before_h", ctypes.c_uint8),
                ("wet_window_after_h", ctypes.c_uint8), ("dry_start_delay_h", ctypes.c_uint8),
                ("max_break_h", ctypes.c_uint8)]


class IrHour(ctypes.Structure):
    _fields_ = [("temp_c", ctypes.c_float), ("rh_pct", ctypes.c_float),
                ("met_valid", ctypes.c_uint8), ("wet", ctypes.c_uint8),
                ("wet_valid", ctypes.c_uint8)]


class IrResult(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint16) for name in (
        "hours_evaluated", "favourable_hours", "invalid_met_hours", "wet_hours",
        "unknown_wet_hours", "sporulation_events", "infection_starts_wet",
        "infection_starts_dry", "ebh_total", "ebh_max", "ebh_current")] + \
        [(name, ctypes.c_uint8) for name in (
            "infection_active", "gap_hours_current", "sporulation_pending", "start_truncated")]


def c_irish_rules(lib, temp, rh, wet, preset=0):
    n = len(temp)
    params = IrParams()
    assert lib.ir_params_init(ctypes.byref(params), preset) == 0
    hours = (IrHour * n)()
    for i in range(n):
        hours[i].temp_c = temp[i]
        hours[i].rh_pct = rh[i]
        hours[i].met_valid = 1
        hours[i].wet = 1 if wet[i] else 0
        hours[i].wet_valid = 1
    ebh = (ctypes.c_uint16 * n)()
    res = IrResult()
    status = lib.ir_evaluate(ctypes.byref(params), hours, ctypes.c_uint16(n), ebh, ctypes.byref(res))
    assert status == 0, status
    return list(ebh), res


# ---------------------------------------------------------------------------
# Random weather with persistent humid / dry spells
# ---------------------------------------------------------------------------
def random_weather(rng, n_hours):
    temp, rh, rain = [], [], []
    humid = False
    t = rng.uniform(8.0, 20.0)
    for _ in range(n_hours):
        if rng.random() < (0.08 if humid else 0.06):
            humid = not humid
        t = min(28.0, max(4.0, t + rng.gauss(0.0, 0.8)))
        if humid:
            h = rng.choice([rng.uniform(90.0, 100.0)] * 9 + [rng.uniform(80.0, 90.0)])
        else:
            h = rng.uniform(55.0, 92.0)
        r = rng.choice([0.0] * 12 + [rng.uniform(0.1, 2.0)]) if humid else 0.0
        temp.append(round(t, 1))
        rh.append(round(h))  # R code rounds RH to whole percent after infill
        rain.append(round(r, 1))
    return temp, rh, rain


def has_one_hour_break_in_period(ebh_ref_fixed, crit):
    """True if a period (per fixed reference) contains a 1-hour break."""
    for i in range(1, len(crit) - 1):
        if crit[i] == 0 and ebh_ref_fixed[i - 1] > 0 and crit[i + 1] == 1:
            return True
    return False


def main():
    lib = ctypes.CDLL(sys.argv[1])
    rng = random.Random(20261013)
    n_seq, n_hours = 3000, 240
    stats = {"seq": 0, "periods": 0, "c_eq_fixed": 0, "c_eq_orig": 0,
             "diff_explained": 0, "diff_unexplained": 0,
             "rainrh_dry_starts": 0, "rain_dry_starts": 0, "rain_wet_starts": 0}

    for _ in range(n_seq):
        temp, rh, rain = random_weather(rng, n_hours)
        stats["seq"] += 1
        crit = [1 if (t >= 10.0 and h >= 90.0) else 0 for t, h in zip(temp, rh)]

        # Original model: leaf "wet" when rain >= 0.1 mm in that hour.
        wet_rain = [r >= 0.1 for r in rain]
        ref_orig = r_irish_rules(temp, rh, rain, lw_mode="rain")
        ref_fixed = r_irish_rules(temp, rh, rain, lw_mode="rain", fix_offset1=True)
        c_ebh, res = c_irish_rules(lib, temp, rh, wet_rain)
        stats["periods"] += res.infection_starts_wet + res.infection_starts_dry
        stats["rain_dry_starts"] += res.infection_starts_dry
        stats["rain_wet_starts"] += res.infection_starts_wet

        if c_ebh == ref_fixed:
            stats["c_eq_fixed"] += 1
        else:
            print("MISMATCH vs fixed reference")
            print(" crit", "".join(map(str, crit)))
            print(" C   ", c_ebh)
            print(" Rfix", ref_fixed)
            sys.exit(1)
        if c_ebh == ref_orig:
            stats["c_eq_orig"] += 1
        elif has_one_hour_break_in_period(ref_fixed, crit):
            stats["diff_explained"] += 1
        else:
            stats["diff_unexplained"] += 1

        # Cucak "rainrh" variant: wet if rain or RH >= 90 in the hour.
        wet_rainrh = [(r >= 0.1) or (h >= 90.0) for r, h in zip(rain, rh)]
        ref_rr = r_irish_rules(temp, rh, rain, lw_mode="rainrh", fix_offset1=True)
        c_rr, res_rr = c_irish_rules(lib, temp, rh, wet_rainrh)
        if c_rr != ref_rr:
            print("MISMATCH (rainrh) vs fixed reference")
            sys.exit(1)
        stats["rainrh_dry_starts"] += res_rr.infection_starts_dry

    print(f"sequences                         : {stats['seq']} x {n_hours} h")
    print(f"infection periods (rain proxy)    : {stats['periods']} "
          f"({stats['rain_wet_starts']} wet starts, {stats['rain_dry_starts']} dry starts)")
    print(f"C == R (offset-1 fixed)           : {stats['c_eq_fixed']} / {stats['seq']}")
    print(f"C == R (unmodified)               : {stats['c_eq_orig']} / {stats['seq']}")
    print(f"  differences with 1-h break      : {stats['diff_explained']}")
    print(f"  differences NOT explained       : {stats['diff_unexplained']}")
    print(f"rain+RH proxy: C == R fixed on all; dry starts = {stats['rainrh_dry_starts']}")
    ok = (stats["c_eq_fixed"] == stats["seq"]) and (stats["diff_unexplained"] == 0)
    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
