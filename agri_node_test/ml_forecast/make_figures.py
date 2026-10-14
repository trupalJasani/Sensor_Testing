"""
Thesis figures for the 24-h forecast (reads results/ written by train_forecast.py).

Colours: validated categorical slots 1-3 (blue, orange, aqua) on a light
surface; aqua is below 3:1 contrast, so every aqua mark is direct-labelled.
"""

import json
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402
from sklearn.metrics import precision_recall_curve  # noqa: E402

RES = sys.argv[1] if len(sys.argv) > 1 else "results"
FIG = os.path.join(RES, "figures")
os.makedirs(FIG, exist_ok=True)

BLUE, ORANGE, AQUA = "#2a78d6", "#eb6834", "#1baf7a"
INK, INK2, MUTED, GRID, SURFACE = "#0b0b0b", "#52514e", "#8a8984", "#e6e5e1", "#fcfcfb"

plt.rcParams.update({
    "figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "savefig.facecolor": SURFACE,
    "font.family": "DejaVu Sans", "font.size": 10,
    "axes.edgecolor": GRID, "axes.labelcolor": INK2, "axes.titlecolor": INK,
    "axes.titlesize": 11, "axes.titleweight": "semibold", "axes.titlelocation": "left",
    "xtick.color": INK2, "ytick.color": INK2, "axes.grid": True, "grid.color": GRID,
    "grid.linewidth": 0.8, "axes.spines.top": False, "axes.axisbelow": True, "axes.spines.right": False,
    "legend.frameon": False, "legend.labelcolor": INK2, "lines.linewidth": 1.6,
})

metrics = json.load(open(os.path.join(RES, "metrics.json")))
sweep = pd.read_csv(os.path.join(RES, "sweep.csv"))
pred = pd.read_csv(os.path.join(RES, "predictions_test.csv"))
chosen = metrics["data"]["chosen"]
base_val = metrics["data"]["base_rate_val"]
base_test = metrics["data"]["base_rate_test"]


def save(fig, name):
    fig.tight_layout()
    fig.savefig(os.path.join(FIG, name + ".png"), dpi=200)
    fig.savefig(os.path.join(FIG, name + ".pdf"))
    plt.close(fig)


# 1) Model size vs skill (validation years) ---------------------------------
fig, ax = plt.subplots(figsize=(6.4, 4.0))
rf = sweep[sweep.model.str.startswith("rf_")]
tr = sweep[sweep.model.str.startswith("tree_")]
ax.scatter(rf.flash_bytes / 1024, rf.val_ap, s=36, color=BLUE, edgecolor=SURFACE, linewidth=1.5,
           label="Random forest (trees × depth)", zorder=3)
ax.scatter(tr.flash_bytes / 1024, tr.val_ap, s=36, color=ORANGE, edgecolor=SURFACE, linewidth=1.5,
           label="Single decision tree", zorder=3)
c = sweep.set_index("model").loc[chosen]
ax.scatter([c.flash_bytes / 1024], [c.val_ap], s=140, facecolor="none", edgecolor=INK, linewidth=1.2, zorder=4)
ax.annotate(f"deployed: {int(c.n_estimators)} trees × depth {int(c.max_depth)}\n"
            f"{c.flash_bytes/1024:.0f} KB, AP {c.val_ap:.3f}",
            (c.flash_bytes / 1024, c.val_ap), xytext=(12, -34), textcoords="offset points",
            color=INK, fontsize=9, arrowprops=dict(arrowstyle="-", color=MUTED, lw=0.8))
for _, r in rf.iterrows():
    if r.model in ("rf_n40_d12", "rf_n10_d6"):
        ax.annotate(f"{int(r.n_estimators)}×{int(r.max_depth)}", (r.flash_bytes / 1024, r.val_ap),
                    xytext=(6, 4), textcoords="offset points", color=INK2, fontsize=8)
lr_ap = sweep.set_index("model").loc["logreg", "val_ap"]
gb_ap = sweep[sweep.model.str.startswith("gb_")].val_ap.max()
for y, txt, ls in ((gb_ap, "gradient boosting, 200 trees (not exported)", ":"),
                   (lr_ap, "logistic regression", "--"),
                   (base_val, "no skill (base rate)", "-")):
    ax.axhline(y, color=MUTED, lw=1.0, ls=ls, zorder=1)
    ax.text(0.25, y + 0.004, txt, color=INK2, fontsize=8, va="bottom")
ax.axvline(64, color=MUTED, lw=1.0, ls="-.", zorder=1)
ax.text(70, 0.30, "64 KB budget", color=INK2, fontsize=8, rotation=90, va="bottom")
ax.set_xscale("log")
ax.set_xlabel("Model tables in flash (KB, log scale)")
ax.set_ylabel("Average precision, validation 2005–2014")
ax.set_title("Bigger forests add little skill")
ax.set_xlim(0.2, 2000)
ax.legend(loc="lower left", bbox_to_anchor=(0.0, 0.06), fontsize=8)
save(fig, "fig1_size_vs_skill")

# 2) Precision-recall on the test years -------------------------------------
fig, ax = plt.subplots(figsize=(6.0, 4.2))
y = pred.y.to_numpy()
for col, colr, lab in (("p_rf", BLUE, "Random forest (deployed)"), ("p_logreg", ORANGE, "Logistic regression")):
    p, r, _ = precision_recall_curve(y, pred[col])
    ax.plot(r, p, color=colr, label=lab)
m_rf, m_pe = metrics["rf"], metrics["persistence"]
ax.scatter([m_rf["pod"]], [m_rf["precision"]], s=64, color=BLUE, edgecolor=SURFACE, linewidth=2, zorder=4)
ax.annotate(f"warning threshold {m_rf['threshold']:.2f}", (m_rf["pod"], m_rf["precision"]),
            xytext=(8, 8), textcoords="offset points", color=INK, fontsize=8)
ax.scatter([m_pe["pod"]], [m_pe["precision"]], s=64, color=AQUA, edgecolor=SURFACE, linewidth=2, zorder=4,
           label="Persistence (yesterday's rule result)")
ax.annotate("persistence", (m_pe["pod"], m_pe["precision"]), xytext=(8, -12), textcoords="offset points",
            color=INK, fontsize=8)
ax.axhline(base_test, color=MUTED, lw=1.0)
ax.text(0.98, base_test + 0.01, f"no skill = base rate {base_test:.3f}", ha="right", color=INK2, fontsize=8)
ax.set_xlim(0, 1); ax.set_ylim(0, 1)
ax.set_xlabel("Recall (share of risk days warned)")
ax.set_ylabel("Precision (share of warnings that were right)")
ax.set_title("Next-24-h Smith-day forecast, test years 2015–2024")
ax.legend(loc="upper right", fontsize=8)
save(fig, "fig2_precision_recall_test")

# 3) Lead time before the onset of a risk window ----------------------------
lead = pd.read_csv(os.path.join(RES, "lead_times_test.csv")).lead_rf.to_numpy()
hours = np.arange(1, 25)
share = [(lead >= h).mean() * 100 for h in hours]
fig, ax = plt.subplots(figsize=(6.0, 3.6))
ax.plot(hours, share, color=BLUE, marker="o", markersize=4)
for h in (1, 6, 12):
    ax.annotate(f"{share[h-1]:.0f} %", (h, share[h - 1]), xytext=(4, 6), textcoords="offset points",
                color=INK, fontsize=8)
ax.set_xlabel("Warning on at least this many hours before the risk window starts")
ax.set_ylabel("Share of onsets (%)")
ax.set_ylim(0, 100); ax.set_xlim(0.5, 24.5)
ax.set_title(f"Warning lead time, {len(lead)} onsets in the test years")
ax.text(23.5, 90, "persistence rule: 0 % warned in advance", ha="right", color=INK2, fontsize=8)
save(fig, "fig3_lead_time_test")

# 4) Permutation importance ---------------------------------------------------
imp = pd.read_csv(os.path.join(RES, "feature_importance.csv")).sort_values("perm_importance")
fig, ax = plt.subplots(figsize=(6.0, 5.0))
ax.barh(imp.feature, imp.perm_importance, xerr=imp.perm_std, color=BLUE, height=0.6,
        error_kw=dict(ecolor=MUTED, lw=0.8))
ax.set_xlabel("Drop in average precision when the feature is shuffled (validation)")
ax.set_title("Which inputs the forest relies on")
ax.grid(axis="y", visible=False)
ax.set_axisbelow(True)
save(fig, "fig4_feature_importance")

# 5) Reliability of the probability ------------------------------------------
bins = np.linspace(0, 1, 11)
idx = np.clip(np.digitize(pred.p_rf, bins) - 1, 0, 9)
rel = pred.assign(b=idx).groupby("b").agg(p=("p_rf", "mean"), o=("y", "mean"), n=("y", "size"))
rel = rel[rel.n >= 50]
fig, ax = plt.subplots(figsize=(4.6, 4.2))
ax.plot([0, 1], [0, 1], color=MUTED, lw=1.0, ls="--")
ax.text(0.62, 0.55, "perfectly reliable", color=INK2, fontsize=8, rotation=42)
ax.plot(rel.p, rel.o, color=BLUE, marker="o", markersize=5, label="Random forest")
ax.set_xlim(0, 1); ax.set_ylim(0, 1)
ax.set_xlabel("Forecast probability")
ax.set_ylabel("Observed frequency (test years)")
ax.set_title("Reliability of the forecast")
save(fig, "fig5_reliability_test")
print("figures written to", FIG)
