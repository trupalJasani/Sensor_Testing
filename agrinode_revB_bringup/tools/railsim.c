/* Rough model of the rev B sensor-rail switch-on (U1 AO3401, C1 10 uF).
 * Many parameters are GUESSES (Beetle 3V3 capacitance, LDO response, wiring
 * inductance, gate drive). Purpose: does the soft-start schedule keep the
 * 3V3 node above 2.51 V where a hard switch-on does not? Not a proof. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

typedef struct { double C3, Cr, L, R, tau_g, K, kl, Vth, Ilim, tau_ldo; } P;

static double V3, Vs, I, Vg, Ildo;
static double minV3;

static double rail_load(double v) /* soil ~5 mA above 2.5 V, Davis, divider */
{
    double soil = (v > 2.5) ? 0.005 : v / 500.0;
    return soil + (v > 0.5 ? 100e-6 : 0) + v / 147e3;
}

/* advance one step dt with pin level pin (0 = LOW = ON) */
static void step(const P *p, double dt, int pin)
{
    double target = pin ? 3.3 : 0.0;
    Vg += (target - Vg) * (1.0 - exp(-dt / p->tau_g));
    double vov = (3.3 - Vg) - p->Vth;     /* |Vgs| - Vth, source at ~3V3 */
    if (vov <= 0) { I = 0; }
    else {
        double rch = 1.0 / (p->kl * vov);
        double isat = p->K * vov * vov;
        double tau = p->L / (p->R + rch);
        double iinf = (V3 - Vs) / (p->R + rch);
        I = iinf + (I - iinf) * exp(-dt / tau);
        if (I > isat) I = isat;
        if (I < 0) I = 0;
    }
    /* LDO: first-order lag towards the current needed, limited */
    double want = (3.3 - V3) * 50.0 + 0.025; /* stiff regulator + ESP load */
    if (want < 0) want = 0;
    if (want > p->Ilim) want = p->Ilim;
    Ildo += (want - Ildo) * (1.0 - exp(-dt / p->tau_ldo));
    V3 += (Ildo - 0.025 - I) * dt / p->C3;
    Vs += (I - rail_load(Vs)) * dt / p->Cr;
    if (Vs < 0) Vs = 0;
    if (V3 < minV3) minV3 = V3;
}

static void reset(void) { V3 = 3.3; Vs = 0; I = 0; Vg = 3.3; Ildo = 0.025; minV3 = 3.3; }

static void hard(const P *p)
{
    reset();
    for (double t = 0; t < 2e-3; t += 1e-9) step(p, 1e-9, 0);
}

/* one PWM period with ON time ton, period 50 us */
static void period(const P *p, double ton)
{
    double t = 0, T = 50e-6;
    for (; t < ton; t += 1e-9) step(p, 1e-9, 0);
    for (double u = 0; u < 3e-6 && t < T; u += 1e-9, t += 1e-9) step(p, 1e-9, 1);
    for (; t < T; t += 20e-9) step(p, 20e-9, 1);
}

static double soft(const P *p, unsigned ramp_ms, double *vs_end_slow)
{
    reset();
    const double lsb = 1e9 / (20000.0 * 2048.0) * 1e-9;
    unsigned duty = 1;
    for (unsigned k = 0; k < ramp_ms; k++) {
        duty = 1 + (40u * k) / ramp_ms;
        for (int n = 0; n < 20; n++) period(p, duty * lsb);
    }
    *vs_end_slow = Vs;
    while (duty < 1024) {
        duty += duty / 8 + 1; if (duty > 1024) duty = 1024;
        for (int n = 0; n < 20; n++) period(p, duty * lsb);
    }
    for (double t = 0; t < 1e-3; t += 1e-9) step(p, 1e-9, 0); /* final GPIO LOW */
    return minV3;
}

int main(void)
{
    double C3s[] = {4.7e-6, 10e-6, 22e-6};
    double Ls[] = {10e-9, 30e-9};
    double taus[] = {30e-9, 120e-9};
    double Ks[] = {5, 30};
    printf("C3uF  LnH taug_ns  K | hard minV3 | soft200 minV3  Vs@slow_end | soft800 minV3\n");
    for (int a = 0; a < 3; a++) for (int b = 0; b < 2; b++) for (int c = 0; c < 2; c++) for (int d = 0; d < 2; d++) {
        P p = {C3s[a], 12e-6, Ls[b], 0.15, taus[c], Ks[d], 20.0, 0.9, 0.9, 5e-6};
        hard(&p); double h = minV3;
        double vs1, vs2;
        double s1 = soft(&p, 200, &vs1);
        double s2 = soft(&p, 800, &vs2);
        printf("%4.1f %4.0f %6.0f %4.0f | %8.2f   | %8.2f %10.2f   | %8.2f\n",
               C3s[a] * 1e6, Ls[b] * 1e9, taus[c] * 1e9, Ks[d], h, s1, vs1, s2);
    }
    return 0;
}
