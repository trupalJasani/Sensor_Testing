"""
Train and evaluate the 24-h late-blight weather-risk forecast.

Target  : will the NEXT 24 h meet the Smith-day criterion
          (min T >= 10 C and >= 11 h with RH >= 90 %)?
Inputs  : 18 features from the last 48 hourly T/RH values, computed by the
          node's own C code (components/blight_forecast).
Data    : DWD hourly TU, station 04104, issue hours May-September.
Split   : train 1948-2004 | validation 2005-2014 | test 2015-2024 (by year,
          so no window crosses a split; seasons never cross a year).
Selection and the warning threshold use ONLY the validation years. The test
years are scored once at the end.

Usage:
  python3 train_forecast.py --data ../main/produkt_tu_stunde_19480101_20241231_04104.txt
Outputs (in results/): sweep.csv, metrics.json, predictions_test.csv,
  model.pkl, and components/blight_forecast/blight_forecast_model.h
"""

import argparse
import json
import os
import pickle
import time

import numpy as np
import pandas as pd
from sklearn.ensemble import GradientBoostingClassifier, RandomForestClassifier
from sklearn.inspection import permutation_importance
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import average_precision_score, brier_score_loss, roc_auc_score
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler
from sklearn.tree import DecisionTreeClassifier

import export_c
import forecast_data as fd

TRAIN_END, VAL_END = 2004, 2014
SEED = 42
FLASH_BUDGET_BYTES = 64 * 1024     # model tables must fit comfortably in flash
AP_TOLERANCE = 0.005               # prefer a smaller forest within this AP of the best


# --------------------------------------------------------------------------
# Metrics
# --------------------------------------------------------------------------
def binary_scores(y, yhat):
    """Forecast-verification scores for a yes/no warning."""
    y = np.asarray(y).astype(bool)
    yhat = np.asarray(yhat).astype(bool)
    tp = int(np.sum(y & yhat)); fp = int(np.sum(~y & yhat))
    fn = int(np.sum(y & ~yhat)); tn = int(np.sum(~y & ~yhat))
    pod = tp / (tp + fn) if tp + fn else float("nan")          # recall / hit rate
    far = fp / (tp + fp) if tp + fp else float("nan")          # false alarm ratio
    pofd = fp / (fp + tn) if fp + tn else float("nan")         # false alarm rate
    csi = tp / (tp + fp + fn) if tp + fp + fn else float("nan")
    prec = tp / (tp + fp) if tp + fp else float("nan")
    f1 = 2 * tp / (2 * tp + fp + fn) if tp + fp + fn else float("nan")
    pss = pod - pofd                                             # Peirce skill score
    acc = (tp + tn) / len(y)
    return dict(tp=tp, fp=fp, fn=fn, tn=tn, pod=pod, far=far, pofd=pofd, csi=csi,
                precision=prec, f1=f1, pss=pss, accuracy=acc)


def prob_scores(y, p, clim):
    """Scores for a probability forecast; BSS is relative to climatology."""
    bs = brier_score_loss(y, p)
    bs_ref = brier_score_loss(y, np.full_like(p, clim, dtype=float))
    return dict(roc_auc=roc_auc_score(y, p), avg_precision=average_precision_score(y, p),
                brier=bs, bss=1.0 - bs / bs_ref)


def best_threshold(y, p):
    """Threshold that maximises CSI (equivalently F1) on the given data."""
    grid = np.round(np.arange(0.05, 0.951, 0.01), 2)
    best = max(grid, key=lambda t: binary_scores(y, p >= t)["csi"])
    return float(best)


def year_bootstrap(df, fn, n_boot=1000, seed=SEED):
    """95 % CI of fn(sub_df) by resampling whole years (keeps autocorrelation
    within a season). Every statistic uses the same resampled year sets."""
    rng = np.random.default_rng(seed)
    years = np.sort(df["year"].unique())
    groups = {y: g.reset_index(drop=True) for y, g in df.groupby("year")}
    vals = []
    for _ in range(n_boot):
        pick = rng.choice(years, size=len(years), replace=True)
        sub = pd.concat([groups[y] for y in pick], ignore_index=True)
        vals.append(fn(sub))
    lo, hi = np.percentile(vals, [2.5, 97.5])
    return float(lo), float(hi)


# --------------------------------------------------------------------------
# Lead time of warnings before the onset of a risk window
# --------------------------------------------------------------------------
def onset_lead_times(test, flag, warn_col, max_lead=24):
    """For each onset (24-h window ending at e meets the criterion, the one
    ending at e-1 does not), find how many hours before e the warning was
    continuously on. A forecast issued at t targets the window ending t+24.
    Returns a list of lead hours (0 = no warning before the onset)."""
    warn = pd.Series(test[warn_col].to_numpy(), index=test["end_index"].to_numpy())
    leads = []
    for e in np.where((flag[1:] == 1) & (flag[:-1] == 0))[0] + 1:
        issue_for_e = e - fd.HORIZON_H
        if issue_for_e not in warn.index:
            continue
        lead = 0
        for t in range(e - 1, e - 1 - max_lead, -1):     # issue times before the onset
            if t in warn.index and bool(warn[t]):
                lead += 1
            else:
                break
        leads.append(lead)
    return leads


# --------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--out", default="results")
    ap.add_argument("--header", default=os.path.join(fd.COMPONENT, "blight_forecast_model.h"))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    t0 = time.time()
    lib = fd.build_c_library()
    ds = fd.build_dataset(args.data, lib)
    flag = ds.attrs["flag_series"]
    ds.attrs = {}   # arrays in attrs break pd.concat
    F = fd.FEATURES
    tr = ds[ds.year <= TRAIN_END]
    va = ds[(ds.year > TRAIN_END) & (ds.year <= VAL_END)]
    te = ds[ds.year > VAL_END].copy()
    Xtr, ytr = tr[F].to_numpy(np.float32), tr["y"].to_numpy()
    Xva, yva = va[F].to_numpy(np.float32), va["y"].to_numpy()
    Xte, yte = te[F].to_numpy(np.float32), te["y"].to_numpy()
    clim = float(ytr.mean())
    pv = binary_scores(yva, va["smith_day_now"].to_numpy() >= 0.5)
    print(f"persistence on validation: CSI={pv['csi']:.3f} POD={pv['pod']:.3f} FAR={pv['far']:.3f}")
    print(f"rows train/val/test = {len(tr)}/{len(va)}/{len(te)}; base rate train {clim:.4f}"
          f" val {yva.mean():.4f} test {yte.mean():.4f}  ({time.time()-t0:.0f}s)")

    # ---------------- candidate models (fit on train, score on validation) -----
    candidates = []
    candidates.append(("logreg", {}, make_pipeline(StandardScaler(), LogisticRegression(max_iter=2000))))
    for d in (4, 6, 8, 10):
        candidates.append((f"tree_d{d}", dict(max_depth=d),
                           DecisionTreeClassifier(max_depth=d, min_samples_leaf=50, random_state=SEED)))
    for n in (10, 20, 40):
        for d in (6, 8, 10, 12):
            candidates.append((f"rf_n{n}_d{d}", dict(n_estimators=n, max_depth=d),
                               RandomForestClassifier(n_estimators=n, max_depth=d, min_samples_leaf=20,
                                                      max_features="sqrt", n_jobs=2, random_state=SEED)))
    for n in (100, 200):
        candidates.append((f"gb_n{n}_d3", dict(n_estimators=n, max_depth=3),
                           GradientBoostingClassifier(n_estimators=n, max_depth=3, learning_rate=0.1,
                                                      subsample=0.8, random_state=SEED)))

    rows, fitted = [], {}
    for name, hp, model in candidates:
        t1 = time.time()
        model.fit(Xtr, ytr)
        fit_s = time.time() - t1
        pva = model.predict_proba(Xva)[:, 1]
        s = prob_scores(yva, pva, clim)
        size = None
        if isinstance(model, RandomForestClassifier):
            size = export_c.table_bytes(export_c.forest_tables(model))
        elif isinstance(model, DecisionTreeClassifier):
            size = model.tree_.node_count * 9
        thr = best_threshold(yva, pva)
        b = binary_scores(yva, pva >= thr)
        rows.append(dict(model=name, **hp, fit_s=round(fit_s, 1), flash_bytes=size,
                         val_ap=s["avg_precision"], val_auc=s["roc_auc"], val_bss=s["bss"],
                         val_thr=thr, val_csi=b["csi"], val_pod=b["pod"], val_far=b["far"]))
        fitted[name] = model
        print(f"  {name:14s} AP={s['avg_precision']:.4f} AUC={s['roc_auc']:.4f} BSS={s['bss']:.3f} "
              f"CSI={b['csi']:.3f}@{thr:.2f} size={size} fit={fit_s:.0f}s")
    sweep = pd.DataFrame(rows)
    sweep.to_csv(os.path.join(args.out, "sweep.csv"), index=False)

    # ---------------- select the deployable RF -------------------------------
    rf_rows = sweep[sweep.model.str.startswith("rf_") & (sweep.flash_bytes <= FLASH_BUDGET_BYTES)]
    best_ap = rf_rows.val_ap.max()
    ok = rf_rows[rf_rows.val_ap >= best_ap - AP_TOLERANCE].sort_values("flash_bytes")
    chosen = ok.iloc[0]["model"]
    rf = fitted[chosen]
    thr = float(sweep.set_index("model").loc[chosen, "val_thr"])
    print(f"chosen: {chosen} (val AP {sweep.set_index('model').loc[chosen,'val_ap']:.4f}, "
          f"best RF AP within budget {best_ap:.4f}), threshold {thr:.2f}")
    best_tree = sweep[sweep.model.str.startswith("tree_")].sort_values("val_ap").iloc[-1]["model"]
    best_gb = sweep[sweep.model.str.startswith("gb_")].sort_values("val_ap").iloc[-1]["model"]

    # ---------------- test evaluation (once) ---------------------------------
    results = {}
    te["p_rf"] = rf.predict_proba(Xte)[:, 1]
    te["warn_rf"] = te["p_rf"] >= thr
    te["warn_persist"] = te["smith_day_now"] >= 0.5
    te["p_clim"] = clim
    for key, name in (("logreg", "logreg"), ("tree", best_tree), ("gb", best_gb)):
        m = fitted[name]
        te[f"p_{key}"] = m.predict_proba(Xte)[:, 1]
        thr_k = float(sweep.set_index("model").loc[name, "val_thr"])
        te[f"warn_{key}"] = te[f"p_{key}"] >= thr_k
        results[key] = dict(model=name, threshold=thr_k, **prob_scores(yte, te[f"p_{key}"], clim),
                            **binary_scores(yte, te[f"warn_{key}"]))
    results["rf"] = dict(model=chosen, threshold=thr, **prob_scores(yte, te["p_rf"], clim),
                         **binary_scores(yte, te["warn_rf"]))
    results["persistence"] = dict(model="persistence (last 24 h was a Smith day)",
                                  **binary_scores(yte, te["warn_persist"]))
    results["climatology"] = dict(model=f"constant p={clim:.4f}",
                                  brier=brier_score_loss(yte, np.full(len(yte), clim)))

    # bootstrap CIs (by year) for the key comparison
    def csi_rf(d): return binary_scores(d.y, d.warn_rf)["csi"]
    def csi_pe(d): return binary_scores(d.y, d.warn_persist)["csi"]
    def csi_diff(d): return csi_rf(d) - csi_pe(d)
    def ap_rf(d): return average_precision_score(d.y, d.p_rf)
    def pod_rf(d): return binary_scores(d.y, d.warn_rf)["pod"]
    def pod_pe(d): return binary_scores(d.y, d.warn_persist)["pod"]
    te_b = te[["year", "y", "p_rf", "warn_rf", "warn_persist"]].copy()
    results["bootstrap_95ci"] = dict(rf_csi=year_bootstrap(te_b, csi_rf), persistence_csi=year_bootstrap(te_b, csi_pe),
                                     csi_rf_minus_persistence=year_bootstrap(te_b, csi_diff),
                                     rf_avg_precision=year_bootstrap(te_b, ap_rf),
                                     rf_pod=year_bootstrap(te_b, pod_rf), persistence_pod=year_bootstrap(te_b, pod_pe))

    # per-year CSI (shows year-to-year spread)
    per_year = te.groupby("year").apply(lambda d: pd.Series(dict(
        events=int(d.y.sum()), csi_rf=binary_scores(d.y, d.warn_rf)["csi"],
        csi_persistence=binary_scores(d.y, d.warn_persist)["csi"])), include_groups=False)
    per_year.to_csv(os.path.join(args.out, "per_year_test.csv"))

    # lead time before onsets
    lead_rf = onset_lead_times(te, flag, "warn_rf")
    lead_pe = onset_lead_times(te, flag, "warn_persist")
    def lead_summary(l):
        a = np.asarray(l)
        return dict(onsets=int(len(a)), warned_ge1h=float(np.mean(a >= 1)), warned_ge6h=float(np.mean(a >= 6)),
                    warned_ge12h=float(np.mean(a >= 12)), median_lead_h=float(np.median(a)))
    results["onset_lead"] = dict(rf=lead_summary(lead_rf), persistence=lead_summary(lead_pe))
    pd.DataFrame(dict(lead_rf=lead_rf)).to_csv(os.path.join(args.out, "lead_times_test.csv"), index=False)

    # permutation importance on validation (AP)
    pi = permutation_importance(rf, Xva, yva, scoring="average_precision", n_repeats=5,
                                random_state=SEED, n_jobs=2)
    imp = pd.DataFrame(dict(feature=F, perm_importance=pi.importances_mean, perm_std=pi.importances_std,
                            impurity_importance=rf.feature_importances_)).sort_values("perm_importance", ascending=False)
    imp.to_csv(os.path.join(args.out, "feature_importance.csv"), index=False)

    # ---------------- export + C-vs-Python check -----------------------------
    model_id = (f"RF{rf.n_estimators}x{rf.max_depth} smith24h DWD04104 "
                f"train1948-{TRAIN_END} thr{thr:.2f}")
    tab = export_c.write_header(rf, args.header, thr, F, model_id, notes=[
        "Target: next 24 h meet the Smith-day criterion (Tmin>=10C, >=11 h RH>=90%).",
        f"Train 1948-{TRAIN_END}, validation {TRAIN_END+1}-{VAL_END}, test {VAL_END+1}-2024, May-Sep.",
        "Probability = mean of leaf class-1 fractions; warning if >= BF_MODEL_THRESHOLD."])
    # golden vectors for the C host test: positives, negatives and rows near the threshold
    rng = np.random.default_rng(SEED)
    p_all = te["p_rf"].to_numpy()
    near_idx = np.argsort(np.abs(p_all - thr))[:8]
    pos_idx = rng.choice(np.where(yte == 1)[0], 12, replace=False)
    neg_idx = rng.choice(np.where(yte == 0)[0], 12, replace=False)
    gidx = np.concatenate([near_idx, pos_idx, neg_idx])
    export_c.write_golden(os.path.join(fd.COMPONENT, "host_test", "bf_golden_vectors.h"),
                          Xte[gidx], p_all[gidx], thr, model_id)
    clib = fd.build_c_library(model_header_dir=os.path.dirname(args.header))
    p_c = fd.c_predict(clib, Xte)
    diff = np.abs(p_c - te["p_rf"].to_numpy())
    dec_mismatch = int(np.sum((p_c >= np.float32(thr)) != te["warn_rf"].to_numpy()))
    near = int(np.sum(np.abs(te["p_rf"].to_numpy() - thr) < 1e-5))
    results["c_export"] = dict(n_trees=tab["n_trees"], n_nodes=tab["n_nodes"], max_depth=tab["max_depth"],
                               table_bytes=export_c.table_bytes(tab), max_abs_prob_diff=float(diff.max()),
                               decision_mismatches=dec_mismatch, rows_within_1e5_of_threshold=near,
                               rows_checked=int(len(Xte)))
    print("C export:", results["c_export"])

    results["data"] = dict(rows_train=len(tr), rows_val=len(va), rows_test=len(te),
                           base_rate_train=clim, base_rate_val=float(yva.mean()), base_rate_test=float(yte.mean()),
                           features=F, chosen=chosen, threshold=thr, sklearn_seed=SEED)
    with open(os.path.join(args.out, "metrics.json"), "w") as fh:
        json.dump(results, fh, indent=2, default=float)
    te[["issue_time", "year", "end_index", "y", "smith_day_now", "p_rf", "warn_rf", "warn_persist",
        "p_logreg", "p_tree", "p_gb"]].to_csv(os.path.join(args.out, "predictions_test.csv"), index=False)
    with open(os.path.join(args.out, "model.pkl"), "wb") as fh:
        pickle.dump(dict(rf=rf, threshold=thr, features=F), fh)
    print(json.dumps({k: v for k, v in results.items() if k in ("rf", "persistence", "bootstrap_95ci", "onset_lead")},
                     indent=1, default=float))
    print(f"done in {time.time()-t0:.0f}s")


if __name__ == "__main__":
    main()
