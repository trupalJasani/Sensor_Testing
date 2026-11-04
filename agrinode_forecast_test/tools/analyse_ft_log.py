#!/usr/bin/env python3
"""
analyse_ft_log.py - evaluates a forecast-test log (the "FT," lines).

Usage (from the agrinode_forecast_test folder):
  python3 tools/analyse_ft_log.py ft_log.txt
  python3 tools/analyse_ft_log.py ft_log.txt --i-idle-ma 18.4 --i-forecast-ma 22.1 --vbat 3.9

The two currents are the multimeter readings at J8 (node on BATTERY, USB
unplugged) in the IDLE and FORECAST phases; --vbat is the cell voltage.

Writes <log name>_results/:
  summary.txt, golden.csv, replay.csv, faults.csv, energy.csv,
  fig_replay_<episode>.png/.pdf, fig_fault.png/.pdf
Needs Python 3 with pandas and matplotlib.
"""
import argparse
import os
import re
import sys

import pandas as pd

ANSI = re.compile(r"\x1b\[[0-9;]*m")

# Reference palette (dataviz skill): categorical slots 1-4, text and grid inks.
C_BLUE, C_ORANGE, C_AQUA, C_YELLOW = "#2a78d6", "#eb6834", "#1baf7a", "#eda100"
C_TEXT, C_MUTED, C_GRID, C_BAND = "#0b0b0b", "#52514e", "#e4e3df", "#f6d9c9"

REP_COLS = ["episode", "j", "temp", "rh", "p_dev", "p_ref", "warn_dev", "warn_ref", "y",
            "flags", "feature_us", "model_us", "call_us"]
FAULT_COLS = ["episode", "kind", "n_hours", "j", "y", "p_clean", "p_fault", "warn_clean",
              "warn_fault", "flags"]


def read_ft_lines(path):
    """Returns the FT records of the log as lists of fields (ANSI codes removed)."""
    recs = []
    with open(path, "r", errors="replace") as fh:
        for line in fh:
            line = ANSI.sub("", line).strip().replace("\r", "")
            i = line.find("FT,")
            if i >= 0:
                recs.append(line[i:].split(","))
    return recs


def last_run(recs):
    """Keeps only the records of the last complete test run (after the last FT,START)."""
    starts = [i for i, r in enumerate(recs) if r[1] == "START"]
    return recs[starts[-1]:] if starts else recs


def build_tables(recs):
    """Converts the records into DataFrames."""
    gold = [r[2:9] for r in recs if r[1] == "GOLD"]
    rep = [r[2:15] for r in recs if r[1] == "REP"]
    fault = [r[2:12] for r in recs if r[1] == "FAULT"]
    energy = [r[2:7] for r in recs if r[1] == "ENERGY"]
    t = {}
    t["golden"] = pd.DataFrame(gold, columns=["i", "p_dev", "p_ref", "diff", "warn_dev", "warn_ref", "result"])
    t["replay_all"] = pd.DataFrame(rep, columns=REP_COLS)
    t["fault"] = pd.DataFrame(fault, columns=FAULT_COLS)
    t["energy"] = pd.DataFrame(energy, columns=["round", "iterations", "loop_us", "mean_us", "status"])
    for k in ("golden", "replay_all", "fault", "energy"):
        df = t[k]
        for c in df.columns:
            if c not in ("episode", "kind", "result", "status", "flags"):
                df[c] = pd.to_numeric(df[c])
    t["episodes"] = {r[3]: (r[5], int(r[7])) for r in recs if r[1] == "EPISODE"}
    t["header"] = next((r for r in recs if r[1] == "HDR"), None)
    t["start"] = next((r for r in recs if r[1] == "START"), None)
    t["summary"] = next((r for r in recs if r[1] == "SUMMARY"), None)
    t["rep_sum"] = [r for r in recs if r[1] == "REP_SUM"]
    t["fault_sum"] = [r for r in recs if r[1] == "FAULT_SUM"]
    t["battery"] = next((r[2] for r in recs if r[1] == "BATTERY"), "n/a")
    return t


def dedup_replays(rep):
    """The 'hit' episode is replayed more than once; checks every copy is
    identical (determinism) and returns one copy per episode."""
    out, notes = [], []
    for ep, g in rep.groupby("episode", sort=False):
        n_runs = len(g) // g["j"].nunique()
        first = g.iloc[: g["j"].nunique()].reset_index(drop=True)
        same = True
        for k in range(1, n_runs):
            other = g.iloc[k * len(first):(k + 1) * len(first)].reset_index(drop=True)
            same &= bool((other[["p_dev", "warn_dev", "flags"]] == first[["p_dev", "warn_dev", "flags"]]).all().all())
        notes.append(f"{ep}: {n_runs} run(s), identical: {same}")
        out.append(first)
    return pd.concat(out, ignore_index=True), notes


def issue_times(episodes, rep):
    """Adds the issue time stamp (newest hour of the 48-h window)."""
    ts = []
    for _, r in rep.iterrows():
        start = pd.Timestamp(episodes[r["episode"]][0])
        ts.append(start + pd.Timedelta(hours=int(r["j"]) + 47))
    rep = rep.copy()
    rep.insert(1, "issue_time", ts)
    return rep


def style_axes(ax):
    """Recessive grid and axes."""
    ax.grid(axis="y", color=C_GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    for s in ("left", "bottom"):
        ax.spines[s].set_color(C_MUTED)
    ax.tick_params(colors=C_MUTED, labelsize=9)


def plot_replay(rep, episodes, thr, outdir):
    """One figure per episode: probability, threshold, true risk hours, warnings."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.dates as mdates
    import matplotlib.pyplot as plt

    for ep, g in rep.groupby("episode", sort=False):
        fig, ax = plt.subplots(figsize=(8, 3.2), dpi=150)
        x = g["issue_time"]
        # Hours whose next 24 h really were a Smith day.
        in_band, band_start = False, None
        for xi, yi in zip(x, g["y"]):
            if yi and not in_band:
                band_start, in_band = xi, True
            if not yi and in_band:
                ax.axvspan(band_start, xi, color=C_BAND, lw=0)
                in_band = False
        if in_band:
            ax.axvspan(band_start, x.iloc[-1], color=C_BAND, lw=0)
        ax.plot(x, g["p_ref"], color=C_MUTED, lw=1.2, ls=(0, (1, 2)), label="PC (scikit-learn)")
        ax.plot(x, g["p_dev"], color=C_BLUE, lw=2, label="ESP32-C3")
        ax.axhline(thr, color=C_TEXT, lw=1, ls="--")
        ax.text(x.iloc[0], thr + 0.02, f"warning threshold {thr:.2f}", color=C_MUTED, fontsize=8)
        w = g[g["warn_dev"] == 1]
        ax.plot(w["issue_time"], [-0.04] * len(w), "|", color=C_BLUE, ms=8, mew=1.5, label="warning on")
        anchor = episodes[ep][1]
        if anchor < len(g):
            ax.axvline(x.iloc[anchor], color=C_ORANGE, lw=1.5)
            ax.text(x.iloc[anchor], 0.97, " anchor", color=C_MUTED, fontsize=8, va="top")
        ax.set_ylim(-0.08, 1.0)
        ax.set_ylabel("P(next 24 h = Smith day)", color=C_MUTED, fontsize=9)
        ax.set_title(f"Replay '{ep}' - shaded: next 24 h really were a Smith day (DWD 04104)",
                     color=C_TEXT, fontsize=10, loc="left")
        ax.xaxis.set_major_formatter(mdates.DateFormatter("%d %b\n%H:%M"))
        style_axes(ax)
        ax.legend(frameon=False, fontsize=8, loc="upper right", ncol=3)
        fig.tight_layout()
        for ext in ("png", "pdf"):
            fig.savefig(os.path.join(outdir, f"fig_replay_{ep}.{ext}"))
        plt.close(fig)


def plot_fault(fault, rep, episodes, thr, outdir):
    """Clean vs sensor-dropout probabilities for the affected hours."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    if fault.empty:
        return
    ep = fault["episode"].iloc[0]
    clean = rep[rep["episode"] == ep]
    fig, ax = plt.subplots(figsize=(8, 3.8), dpi=150)
    ax.plot(clean["j"], clean["p_dev"], color=C_BLUE, lw=2, label="clean")
    styles = {("zero", 2): (C_ORANGE, "2 h dropout stored as 0 C / 0 % (node now)"),
              ("nan", 2): (C_AQUA, "2 h dropout stored as NAN"),
              ("nan", 4): (C_YELLOW, "4 h dropout stored as NAN")}
    for (kind, n), g in fault.groupby(["kind", "n_hours"], sort=False):
        col, lab = styles.get((kind, int(n)), (C_MUTED, f"{kind} {n} h"))
        # Hours without a forecast (p = -1) become gaps in the line.
        p = g["p_fault"].where(g["p_fault"] >= 0)
        ax.plot(g["j"], p, color=col, lw=2, label=lab, marker="o", ms=3)
    ax.axhline(thr, color=C_TEXT, lw=1, ls="--")
    ax.set_xlabel("issue hour of the episode", color=C_MUTED, fontsize=9)
    ax.set_ylabel("forecast probability", color=C_MUTED, fontsize=9)
    ax.set_title(f"Sensor dropout in episode '{ep}' (hours without a dot: no forecast)",
                 color=C_TEXT, fontsize=10, loc="left")
    style_axes(ax)
    ax.legend(frameon=False, fontsize=8, loc="upper center", bbox_to_anchor=(0.5, -0.2), ncol=2)
    fig.tight_layout()
    for ext in ("png", "pdf"):
        fig.savefig(os.path.join(outdir, f"fig_fault.{ext}"))
    plt.close(fig)


def main():
    """Parses the log, writes tables, figures and the summary."""
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--i-idle-ma", type=float, help="current in the IDLE phase (mA, J8, battery)")
    ap.add_argument("--i-forecast-ma", type=float, help="current in the FORECAST phase (mA)")
    ap.add_argument("--vbat", type=float, default=None, help="cell voltage during the test (V)")
    ap.add_argument("--no-figures", action="store_true")
    a = ap.parse_args()

    recs = last_run(read_ft_lines(a.log))
    if not recs:
        sys.exit("no FT lines found")
    t = build_tables(recs)
    outdir = os.path.splitext(a.log)[0] + "_results"
    os.makedirs(outdir, exist_ok=True)
    thr = float(t["header"][5]) if t["header"] else 0.26

    S = []
    if t["start"]:
        S.append("Build: " + ", ".join(t["start"][2:]))
    S.append(f"Battery reading at start: {t['battery']} mV")
    if t["header"]:
        S.append(f"Model: {t['header'][3]}, threshold {thr}")

    g = t["golden"]
    if not g.empty:
        S.append(f"\n1) Golden check: {int((g['result'] == 'PASS').sum())}/{len(g)} PASS, "
                 f"max |p_dev - p_ref| = {g['diff'].max():.2e}, warning mismatches = "
                 f"{int((g['warn_dev'] != g['warn_ref']).sum())}")
        g.to_csv(os.path.join(outdir, "golden.csv"), index=False)

    rep = pd.DataFrame()
    if not t["replay_all"].empty:
        rep, notes = dedup_replays(t["replay_all"])
        rep = issue_times(t["episodes"], rep)
        rep.to_csv(os.path.join(outdir, "replay.csv"), index=False)
        S.append("\n2) Replay of DWD episodes (device vs scikit-learn, and vs the truth y):")
        S += ["   " + n for n in notes]
        seen = set()
        for r in t["rep_sum"]:
            if r[2] in seen:
                continue
            seen.add(r[2])
            (ep, n, ok, wmis, ptol, maxd, nw, ny, hit, fa, miss, lead,
             mcall, xcall, mfeat, mmod, res) = r[2:19]
            S.append(f"   {ep:12s} {res}: {ok}/{n} forecasts, warning mismatches {wmis}, "
                     f"max |dp| {float(maxd):.2e}; warnings {nw} h, true risk {ny} h, hits {hit}, "
                     f"false alarms {fa}, misses {miss}; lead before onset "
                     f"{'n/a' if lead == '-1' else lead + ' h'}; call mean {mcall} us (max {xcall}), "
                     f"features {mfeat} us, forest {mmod} us")
        ok = rep["call_us"]
        S.append(f"   All replay calls of run_blight_forecast(): mean {ok.mean():.1f} us, "
                 f"SD {ok.std():.1f}, min {ok.min()}, max {ok.max()} (n = {len(ok)}); "
                 f"features {rep['feature_us'].mean():.1f} us, forest {rep['model_us'].mean():.1f} us")

    f = t["fault"]
    if t["fault_sum"]:
        f.to_csv(os.path.join(outdir, "faults.csv"), index=False)
        S.append("\n3) Sensor dropout (episode, kind, first raw hour, hours, compared, affected, "
                 "warning flips, gap-flagged, no forecast, max |dp|):")
        for r in t["fault_sum"]:
            S.append("   " + ", ".join(r[2:]))

    e = t["energy"]
    if not e.empty:
        e.to_csv(os.path.join(outdir, "energy.csv"), index=False)
        mean_us = e["loop_us"].sum() / e["iterations"].sum()
        S.append(f"\n4) Energy loop: {int(e['iterations'].sum())} forecasts in {e['loop_us'].sum() / 1e6:.1f} s, "
                 f"mean {mean_us:.2f} us per run_blight_forecast() call "
                 f"(rounds: {', '.join(f'{v:.1f}' for v in e['mean_us'])} us)")
        if a.i_idle_ma is not None and a.i_forecast_ma is not None:
            di = a.i_forecast_ma - a.i_idle_ma
            v = a.vbat if a.vbat else 3.7
            t_s = mean_us * 1e-6
            S.append(f"   Currents: IDLE {a.i_idle_ma} mA, FORECAST {a.i_forecast_ma} mA, "
                     f"difference {di:.2f} mA; cell {v} V{'' if a.vbat else ' (assumed)'}")
            S.append(f"   Extra energy per forecast (CPU busy instead of waiting): "
                     f"{v * di * 1e-3 * t_s * 1e6:.1f} uJ from the cell "
                     f"({3.3 * di * 1e-3 * t_s * 1e6:.1f} uJ at 3.3 V)")
            S.append(f"   Whole-board energy during one forecast: {v * a.i_forecast_ma * 1e-3 * t_s * 1e6:.1f} uJ "
                     f"from the cell; per day (96 cycles): {96 * v * di * 1e-3 * t_s * 1e3:.3f} mJ extra")
        else:
            S.append("   (give --i-idle-ma and --i-forecast-ma to compute the energy)")

    text = "\n".join(S)
    print(text)
    with open(os.path.join(outdir, "summary.txt"), "w") as fh:
        fh.write(text + "\n")

    if not a.no_figures and not rep.empty:
        plot_replay(rep, t["episodes"], thr, outdir)
        plot_fault(f, rep, t["episodes"], thr, outdir)
    print("results in", outdir)


if __name__ == "__main__":
    main()
