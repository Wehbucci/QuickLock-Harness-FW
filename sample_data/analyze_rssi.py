#!/usr/bin/env python3
"""
analyze_rssi.py - Calibrate the QuickLock harness proximity model from bench data.

Reads rssi_calibration_all.csv (one row per RSSI sample) and produces the two
things config.h needs, which are NOT the same job:

  1. RSSI_C_DBM / RSSI_N  -- the log-distance model. Only feeds
     proximity_distance_m(), i.e. the "(~X m)" in the log. Telemetry.
  2. OUT_THRESHOLD_DBM / IN_THRESHOLD_DBM -- thresholds on the FILTERED RSSI.
     This is what actually drives the auto-arm decision (F14). Safety-critical.

Model:  RSSI(d) = C - 10*N*log10(d)          [d0 = 1 m]
Fit:    y = a + b*x  with x = log10(d)  ->  C = a,  N = -b/10

Usage:  python3 analyze_rssi.py [csv_path]
"""

import csv
import math
import sys
from collections import defaultdict

import numpy as np

# --- Link parameters, for the sanity check against theory -------------------
# Fob TX power (quicklock-fob-fw/lib/config/config.h). The harness never calls
# a set-TX-power API, so it sits at the ESP32 default (~0 dBm).
TX_POWER_DBM = -4.0
FREQ_MHZ = 2440.0

# EMA weight the harness applies before thresholding (config.h RSSI_ALPHA).
# The decision sees FILTERED RSSI, so filtered spread -- not raw -- is what
# determines whether two distances are separable.
RSSI_ALPHA = 0.15

# What config.h ships with today, for the "would this even work?" verdict.
CURRENT_OUT_THRESHOLD = -74.0
CURRENT_IN_THRESHOLD = -70.0


def fspl_db(d_m, f_mhz=FREQ_MHZ):
    """Free-space path loss in dB. Undefined at d=0."""
    return 20 * math.log10(d_m) + 20 * math.log10(f_mhz) - 27.55


def load(path):
    """-> {distance_m: [raw_rssi, ...]}"""
    by_dist = defaultdict(list)
    with open(path, newline="") as fh:
        for row in csv.DictReader(fh):
            by_dist[float(row["distance_m"])].append(int(row["rssi_raw_dbm"]))
    return dict(sorted(by_dist.items()))


def fit(distances, means):
    """Least-squares fit of RSSI = C - 10*N*log10(d). Returns (C, N, r2, rmse)."""
    x = np.log10(np.asarray(distances, dtype=float))
    y = np.asarray(means, dtype=float)
    b, a = np.polyfit(x, y, 1)
    pred = a + b * x
    ss_res = float(np.sum((y - pred) ** 2))
    ss_tot = float(np.sum((y - np.mean(y)) ** 2))
    r2 = 1.0 - ss_res / ss_tot if ss_tot > 0 else float("nan")
    rmse = math.sqrt(ss_res / len(y))
    return a, -b / 10.0, r2, rmse


def filtered_sigma(raw_sigma, alpha=RSSI_ALPHA):
    """
    Steady-state std-dev of an EMA fed white noise:
        var_out/var_in = alpha / (2 - alpha)
    Optimistic if samples are autocorrelated (they often are), so treat this as
    a best case.
    """
    return raw_sigma * math.sqrt(alpha / (2.0 - alpha))


def main(path):
    by_dist = load(path)

    # ---------------------------------------------------------------- stats
    print("=" * 78)
    print("1. PER-DISTANCE STATISTICS (raw samples)")
    print("=" * 78)
    print(f"{'dist_m':>7} {'n':>4} {'mean':>8} {'median':>8} {'sd':>6} "
          f"{'min':>5} {'max':>5} {'sd_filt':>8}")
    stats = {}
    for d, vals in by_dist.items():
        a = np.asarray(vals, dtype=float)
        sd = float(a.std(ddof=1))
        stats[d] = {"mean": float(a.mean()), "median": float(np.median(a)),
                    "sd": sd, "n": len(a), "sd_filt": filtered_sigma(sd)}
        print(f"{d:7.1f} {len(a):4d} {a.mean():8.2f} {np.median(a):8.1f} "
              f"{sd:6.2f} {a.min():5.0f} {a.max():5.0f} {filtered_sigma(sd):8.2f}")

    # ------------------------------------------------- link budget vs theory
    print()
    print("=" * 78)
    print("2. LINK BUDGET vs THEORY  (is the RF itself healthy?)")
    print("=" * 78)
    print(f"Assuming fob TX = {TX_POWER_DBM:+.0f} dBm, ideal 0 dBi antennas, free space.")
    print(f"{'dist_m':>7} {'measured':>9} {'theory':>8} {'deficit':>8}")
    for d in [x for x in by_dist if x > 0]:
        theory = TX_POWER_DBM - fspl_db(d)
        meas = stats[d]["mean"]
        print(f"{d:7.1f} {meas:9.2f} {theory:8.2f} {meas - theory:8.1f}")

    # -------------------------------------------------------- monotonicity
    print()
    print("=" * 78)
    print("3. MONOTONICITY (RSSI must fall as distance grows)")
    print("=" * 78)
    ds = [x for x in by_dist if x > 0]
    violations = []
    for i in range(len(ds) - 1):
        d0, d1 = ds[i], ds[i + 1]
        delta = stats[d1]["mean"] - stats[d0]["mean"]
        flag = ""
        if delta > 0:
            flag = "  <-- INVERTED (farther reads STRONGER)"
            violations.append((d0, d1, delta))
        print(f"  {d0:5.1f} -> {d1:5.1f} m : {delta:+6.2f} dB{flag}")

    # ---------------------------------------------------------------- fits
    print()
    print("=" * 78)
    print("4. LOG-DISTANCE FITS  (C = RSSI at 1 m, N = path-loss exponent)")
    print("=" * 78)
    print("d=0 is excluded everywhere: log10(0) is undefined and the model does")
    print("not apply in the near field.")
    print()

    subsets = {
        "all d>0": [d for d in by_dist if d > 0],
        "excl 1.5 (flagged outlier)": [d for d in by_dist if d > 0 and d != 1.5],
        "excl 1.5, near-only (<=6 m)": [d for d in by_dist if 0 < d <= 6 and d != 1.5],
        "excl 1.5, far-only (>=4.5 m)": [d for d in by_dist if d >= 4.5],
    }
    results = {}
    for name, ds_sub in subsets.items():
        if len(ds_sub) < 3:
            continue
        means = [stats[d]["mean"] for d in ds_sub]
        C, N, r2, rmse = fit(ds_sub, means)
        results[name] = (C, N, r2, rmse)
        note = ""
        if N < 1.6:
            note = "  <-- N below free space: implausible"
        print(f"  {name:32s} C={C:7.2f} dBm  N={N:5.2f}  R2={r2:5.2f}  "
              f"RMSE={rmse:4.2f} dB{note}")

    # ------------------------------------------------------ discriminability
    print()
    print("=" * 78)
    print("5. DISCRIMINABILITY  (can the filter tell these apart?)")
    print("=" * 78)
    print("Separation between each distance and 12 m, in units of filtered sigma.")
    print("Below ~2 sigma the two are not reliably distinguishable.")
    print()
    ref = max(ds)
    print(f"{'dist_m':>7} {'gap_dB':>8} {'sigmas':>8}   verdict")
    for d in ds:
        if d == ref:
            continue
        gap = stats[d]["mean"] - stats[ref]["mean"]
        pooled = math.sqrt(stats[d]["sd_filt"] ** 2 + stats[ref]["sd_filt"] ** 2)
        sig = gap / pooled if pooled else float("nan")
        verdict = ("separable" if sig >= 3 else
                   "marginal" if sig >= 2 else "NOT SEPARABLE")
        print(f"{d:7.1f} {gap:8.2f} {sig:8.2f}   {verdict}")

    # ---------------------------------------------------- current thresholds
    print()
    print("=" * 78)
    print("6. VERDICT ON THE THRESHOLDS CURRENTLY IN config.h")
    print("=" * 78)
    print(f"OUT_THRESHOLD_DBM = {CURRENT_OUT_THRESHOLD}  "
          f"IN_THRESHOLD_DBM = {CURRENT_IN_THRESHOLD}")
    print()
    bad = []
    for d in ds:
        m = stats[d]["mean"]
        state = "OUT OF RANGE" if m < CURRENT_OUT_THRESHOLD else "in range"
        if m < CURRENT_OUT_THRESHOLD:
            bad.append(d)
        print(f"  at {d:5.1f} m: mean filtered ~{m:7.2f} dBm -> {state}")
    if bad:
        print()
        print(f"  *** At EVERY distance from {min(bad)} m outward the harness would")
        print("  *** declare the fob out of range. Auto-arm would fire constantly.")

    # ------------------------------------------------- threshold candidates
    print()
    print("=" * 78)
    print("7. THRESHOLD CANDIDATES  (error rates from the empirical spread)")
    print("=" * 78)
    print("For each candidate OUT threshold: P(declared out) at each distance,")
    print("using a normal model on the FILTERED value.")
    print()
    candidates = [-82.0, -84.0, -86.0, -88.0]
    header = "  " + "".join(f"{d:>8.1f}" for d in ds)
    print(f"{'threshold':>10}" + header)
    for T in candidates:
        cells = ""
        for d in ds:
            mu, sd = stats[d]["mean"], max(stats[d]["sd_filt"], 1e-6)
            p_out = 0.5 * (1 + math.erf((T - mu) / (sd * math.sqrt(2))))
            cells += f"{p_out * 100:7.0f}%"
        print(f"{T:10.1f}" + cells)
    print()
    print("Read down a column: it should go from ~0% at short range to ~100% at")
    print("long range. A column that never reaches 100%, or a row that is high")
    print("everywhere, means that threshold cannot separate near from far.")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "rssi_calibration_all.csv")
