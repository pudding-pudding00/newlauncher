#!/usr/bin/env python3
"""Min-entropy assessment of the Wii timebase-jitter source.

Reads the "JD <hex>" lines of the hardware runner's `jitterdump` suite:

    python tools/hwtest.py --wii <ip> --dol build-wii/tests/hw/wtls_hwtest.dol \\
        --suite jitterdump --timeout 300 > jd.log
    python tools/jitter_assess.py jd.log

and prints per-sample min-entropy estimates in the spirit of NIST SP 800-90B
section 6.3 (non-IID track). The predictor estimates use the accuracy of the
best predictor *in hindsight* (best lag, best order-k context table), which is
an upper bound on what the online predictors of 90B achieve, so the entropy
they report is lower (more conservative) than 90B's.

The final line is the minimum over all estimators: the number the library's
entropy credit (src/wtls_jitter.h) must stay well below.
"""
import argparse
import math
import sys
from collections import Counter, defaultdict

import numpy as np

Z = 2.576  # 99 % one-sided bound, as in SP 800-90B


def upper(p, n):
    return min(1.0, p + Z * math.sqrt(p * (1.0 - p) / (n - 1)))


def h(p):
    return -math.log2(p) if p > 0 else float("inf")


def load(path):
    """{variant: samples}. Lines are "JD <variant> <hex>" (or "JD <hex>": variant "?")."""
    chunks = defaultdict(list)
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if not line.startswith("JD "):
                continue
            parts = line[3:].split()
            if len(parts) == 2:
                chunks[parts[0]].append(bytes.fromhex(parts[1]))
            elif len(parts) == 1:
                chunks["?"].append(bytes.fromhex(parts[0]))
    return {v: np.frombuffer(b"".join(c), dtype=np.uint8) for v, c in chunks.items()}


def mcv(x):
    return h(upper(np.bincount(x, minlength=256).max() / len(x), len(x)))


def best_lag(x, max_lag=256):
    """Predict x[i] = x[i-d] with the best single lag d (hindsight)."""
    n = len(x)
    best, best_d = 0.0, 0
    for d in range(1, max_lag + 1):
        acc = float(np.mean(x[d:] == x[:-d]))
        if acc > best:
            best, best_d = acc, d
    return h(upper(best, n - best_d)), best_d, best


def best_context(x, k):
    """Order-k predictor: most frequent successor of each k-sample context (hindsight)."""
    n = len(x)
    key = np.zeros(n - k, dtype=np.int64)
    for j in range(k):
        key = key * 256 + x[j:n - k + j].astype(np.int64)
    pair = key * 256 + x[k:].astype(np.int64)
    uniq, counts = np.unique(pair, return_counts=True)
    ctx = uniq // 256
    best = defaultdict(int)
    for c, cnt in zip(ctx.tolist(), counts.tolist()):
        if cnt > best[c]:
            best[c] = cnt
    hits = sum(best.values())
    contexts = len(best)
    return h(upper(hits / (n - k), n - k)), contexts


def t_tuple(x, t_max=8, cutoff=35):
    """SP 800-90B 6.3.5 t-tuple estimate (most common t-tuple, per-sample)."""
    n = len(x)
    p_max = 0.0
    for t in range(1, t_max + 1):
        key = np.zeros(n - t + 1, dtype=np.int64)
        for j in range(t):
            key = key * 256 + x[j:n - t + 1 + j].astype(np.int64)
        _, counts = np.unique(key, return_counts=True)
        q = counts.max()
        if q < cutoff:
            break
        p_max = max(p_max, (q / (n - t + 1)) ** (1.0 / t))
    return h(upper(p_max, n))


def collision_rate(x):
    """Rényi-2 (collision) entropy per sample: an upper bound sanity check."""
    p = np.bincount(x, minlength=256) / len(x)
    return -math.log2(float(np.sum(p * p)))


def assess(name, x):
    print(f"variant {name}: {len(x)} samples")
    hist = Counter(x.tolist())
    print("  top values:", ", ".join(f"{v}:{c / len(x):.3f}" for v, c in hist.most_common(6)),
          f"(distinct {len(hist)})")
    est = {"MCV": mcv(x)}
    hl, d, acc = best_lag(x)
    est[f"lag (best d={d}, acc {acc:.3f})"] = hl
    for k in (1, 2, 3, 4, 5, 6):
        hk, nctx = best_context(x, k)
        if nctx * 20 > len(x):  # too few samples per context: hindsight overfits
            break
        est[f"order-{k} context ({nctx} ctx)"] = hk
    est["t-tuple"] = t_tuple(x)
    for n, v in est.items():
        print(f"    {n:32s} H = {v:.3f} bits/sample")
    print(f"    {'(Renyi-2, not a bound)':32s} H2 = {collision_rate(x):.3f}")
    print(f"  MIN H = {min(est.values()):.3f} bits/sample")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    args = ap.parse_args()
    data = load(args.log)
    if not data:
        sys.exit(f"no JD lines in {args.log}")
    for name, x in sorted(data.items()):
        if len(x) < 10000:
            print(f"variant {name}: only {len(x)} samples, skipped")
            continue
        assess(name, x)


if __name__ == "__main__":
    main()
