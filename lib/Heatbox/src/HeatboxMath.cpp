#include "HeatboxMath.h"

namespace heatbox {
namespace detail {

int32_t median(const int32_t *src, uint8_t n) {
    int32_t t[MEDIAN_N];
    memcpy(t, src, n * sizeof(int32_t));
    for (uint8_t i = 1; i < n; i++) {
        int32_t v = t[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 && t[j] > v) {
            t[j + 1] = t[j];
            j--;
        }
        t[j + 1] = v;
    }
    return t[n >> 1];
}

float slopeCountsPerSec(const int32_t *y, int n, float dt) {
    if (n < 3) return 0.0f;
    int64_t acc = 0;
    const int m = n - 1;
    for (int i = 0; i < n; i++) {
        acc += (int64_t)(2 * i - m) * (int64_t)y[i];
    }

    float fn = (float)n;
    float sxx = fn * (fn * fn - 1.0f) * (1.0f / 3.0f);
    return (2.0f * (float)acc / sxx) / dt;
}

bool solve3x3(const float M[3][3], const float r[3], float x[3]) {
    float c00 = M[1][1] * M[2][2] - M[1][2] * M[2][1];
    float c01 = M[1][0] * M[2][2] - M[1][2] * M[2][0];
    float c02 = M[1][0] * M[2][1] - M[1][1] * M[2][0];
    float det = M[0][0] * c00 - M[0][1] * c01 + M[0][2] * c02;
    if (fabsf(det) < 1e-9f) return false;
    float inv = 1.0f / det;

    x[0] = inv * ( r[0] * c00
                 - M[0][1] * (r[1] * M[2][2] - M[1][2] * r[2])
                 + M[0][2] * (r[1] * M[2][1] - M[1][1] * r[2]) );
    x[1] = inv * ( M[0][0] * (r[1] * M[2][2] - M[1][2] * r[2])
                 - r[0] * c01
                 + M[0][2] * (M[1][0] * r[2] - r[1] * M[2][0]) );
    x[2] = inv * ( M[0][0] * (M[1][1] * r[2] - r[1] * M[2][1])
                 - M[0][1] * (M[1][0] * r[2] - r[1] * M[2][0])
                 + r[0] * c02 );
    return true;
}

#if USE_CURVE_FIT
float sseForP(const int32_t *y, int n, float p, float abc[3]) {
    float Se = 0, See = 0, Ss = 0, Sse = 0, Sss = 0;
    float Sy = 0, Sey = 0, Ssy = 0, Syy = 0;
    const float invp = 1.0f / p;
    const float invn1 = 1.0f / (float)(n - 1);
    const int32_t y0 = y[0];

    for (int i = 0; i < n; i++) {
        float s = (float)i * invn1;
        float e = expf(-s * invp);
        float yy = (float)(y[i] - y0);
        Se += e; See += e * e;
        Ss += s; Sse += s * e; Sss += s * s;
        Sy += yy; Sey += e * yy; Ssy += s * yy;
        Syy += yy * yy;
    }

    const float M[3][3] = {
        { (float)n, Se, Ss },
        { Se, See, Sse },
        { Ss, Sse, Sss }
    };
    const float r[3] = { Sy, Sey, Ssy };
    if (!solve3x3(M, r, abc)) return 1e30f;

    float sse = Syy - (abc[0] * Sy + abc[1] * Sey + abc[2] * Ssy);
    return (sse < 0.0f) ? 0.0f : sse;
}

HbFit fitCurve(const int32_t *y, int n, float dt) {
    HbFit F; memset(&F, 0, sizeof(F));
    if (n < 3) return F;

    const float dur = (float)(n - 1) * dt;
    float lo = logf(TAU_MIN_S / dur);
    float hi = logf(TAU_MAX_S / dur);
    if (hi <= lo) return F;

    const float GR = 0.6180339887f;
    float a = lo, b = hi;
    float c = b - GR * (b - a);
    float d = a + GR * (b - a);
    float abc[3];
    float fc = sseForP(y, n, expf(c), abc);
    float fd = sseForP(y, n, expf(d), abc);

    for (int it = 0; it < GOLDEN_ITER; it++) {
        if (fc < fd) {
            b = d;
            d = c;
            fd = fc;
            c = b - GR * (b - a);
            fc = sseForP(y, n, expf(c), abc);
        } else {
            a = c;
            c = d;
            fc = fd;
            d = a + GR * (b - a);
            fd = sseForP(y, n, expf(d), abc);
        }
    }

    float p = expf(0.5f * (a + b));
    float sse = sseForP(y, n, p, abc);

    F.valid = true;
    F.tau_s = p * dur;
    F.W_inf = (float)y[0] + abc[0];
    F.A = abc[1];
    F.k_cps = abc[2] / dur;
    F.rmse_mg = sqrtf(sse / (float)n) / COUNTS_PER_MG;

    F.resid_mg = fabsf(F.A * expf(-1.0f / p)) / COUNTS_PER_MG;

    float Amg = fabsf(F.A) / COUNTS_PER_MG;
    F.t_stop_s = (Amg > RESID_STOP_MG)
               ? F.tau_s * logf(Amg / RESID_STOP_MG)
               : 0.0f;
    return F;
}
#endif

}  // namespace detail
}  // namespace heatbox
