#!/usr/bin/env python3
"""Linear stability of the SLAAPS Manual rate+attitude loop versus canard authority.

MODEL ONLY: no measured aerodynamics, no flight acceptance. The loop is
  m = (Kp + Ki/s) * (ATT_P*(theta_ref - theta) - omega),  omega = b*k*e^{-sT}/((tau*s+1)) * m / s ... with b = M_SCALE / I
where k scales the real canard authority against the modeled one ((V/V_design)^2). Body aerodynamic damping is not modeled.
Characteristic polynomial: s^3 (tau s + 1) D(s) + k b (Kp s + Ki)(s + ATT_P) N(s) = 0, e^{-sT} ~ N/D (Pade).
"""
import sys
import numpy as np

def pade(T, n=4):
    # coefficients of the (n,n) Pade approximant of exp(-sT), highest power first
    from math import factorial
    c = [factorial(2 * n - j) * factorial(n) / (factorial(2 * n) * factorial(j) * factorial(n - j)) for j in range(n + 1)]
    num = [(-T) ** j * c[j] for j in range(n, -1, -1)]
    den = [(T) ** j * c[j] for j in range(n, -1, -1)]
    return np.array(num), np.array(den)

def poles(k, b, Kp, Ki, att, tau, T):
    N, D = pade(T)
    lead = np.polymul([tau, 1.0], np.polymul([1, 0, 0, 0], D))
    num = np.polymul(np.polymul([Kp, Ki], [1.0, att]), N) * (k * b)
    return np.roots(np.polyadd(lead, num))

def metrics(k, b, Kp, Ki, att, tau, T):
    r = poles(k, b, Kp, Ki, att, tau, T)
    if np.max(r.real) >= -1e-9: return False, np.max(r.real), 0.0
    # damping of the complex poles inside the control band (|s| < 60 rad/s); real poles count as zeta = 1
    z = 1.0
    for p in r:
        if abs(p) < 60.0 and abs(p.imag) > 1e-6: z = min(z, -p.real / abs(p))
    slow = max(p.real for p in r if abs(p) < 60.0)
    return True, slow, z

AXES = {  # name: (M_SCALE, inertia, Kp, Ki, ATT_P) at the revision-3 design, 40 m/s
    "roll":  (0.176, 0.0027886, 0.0792, 0.0792, 1.43),
    "pitch": (0.5165, 0.0337643, 0.261, 0.209, 1.14),
    "yaw":   (0.5165, 0.0337507, 0.261, 0.209, 1.14),
}
TAU = 0.05  # SLA_Cn_LAG, measured loaded servo lag

def sweep(T, zmin=0.5):
    ks = np.geomspace(0.02, 30, 700)
    out = {}
    for name, (ms, I, Kp, Ki, att) in AXES.items():
        b = ms / I
        ok = [metrics(k, b, Kp, Ki, att, TAU, T) for k in ks]
        stable = [o[0] for o in ok]
        good = [o[0] and o[2] >= zmin for o in ok]
        def span(flags):
            idx = [i for i, f in enumerate(flags) if f]
            return (ks[idx[0]], ks[idx[-1]]) if idx else (None, None)
        out[name] = {"stable": span(stable), "good": span(good), "ks": ks, "ok": ok}
    return out

if __name__ == "__main__":
    for T in (0.018, 0.028, 0.048):
        res = sweep(T)
        print("total delay %.0f ms (servo 8 ms + %.0f ms assumed controller/state/output latency)" % (T * 1e3, T * 1e3 - 8))
        for n, r in res.items():
            s, g = r["stable"], r["good"]
            f = lambda x: "none" if x[0] is None else "%.2f..%.1f" % x
            print("  %-5s stable authority multiple k: %-12s  zeta>=0.5: %s" % (n, f(s), f(g)))
    print("\nzeta / slowest pole (rad/s) at base delay 28 ms")
    res = sweep(0.028)
    print("   k   " + "  ".join("%-18s" % n for n in res))
    for kk in (0.05, 0.1, 0.2, 0.45, 0.8, 1.0, 1.5, 2, 3, 5, 8):
        row = []
        for n, (ms, I, Kp, Ki, att) in AXES.items():
            st, slow, z = metrics(kk, ms / I, Kp, Ki, att, TAU, 0.028)
            row.append("%-18s" % ("UNSTABLE" if not st else "z=%.2f s=%.2f" % (z, -slow)))
        print("  %4.2f  " % kk + "  ".join(row))


def coverage(v_lo, v_hi, T=0.028):
    """Best single design speed for [v_lo, v_hi] indicated m/s: maximise the worst damping ratio over the range."""
    best = None
    for vd in np.arange(10.0, 60.0, 0.5):
        worst = 1.0
        for name, (ms, I, Kp, Ki, att) in AXES.items():
            for v in np.linspace(v_lo, v_hi, 40):
                st, slow, z = metrics((v / vd) ** 2, ms / I, Kp, Ki, att, TAU, T)
                worst = min(worst, z if st else -1.0)
        if best is None or worst > best[1]: best = (vd, worst)
    return best


if __name__ == "__main__" and len(sys.argv) > 1 and sys.argv[1] == "coverage":
    for lo, hi in ((18.0, 40.0), (17.8, 35.7)):
        vd, z = coverage(lo, hi)
        print("range %.1f..%.1f m/s indicated: best single design speed %.1f m/s, worst-case damping ratio %.2f" % (lo, hi, vd, z))
