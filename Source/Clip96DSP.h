// BoomTown Beats Clip 96 — DSP engine.
// Straight C++ port of the browser prototype (prototype/dsp.js). No JUCE dependency, so it can be tested on its own.
// Per-sample double precision; all buffers are allocated in prepare(), nothing in process().
//
// Push -> [optional band split: the lows get their own pre-clip] -> oversampled clipper (hardness, asym)
//      -> lookahead limiter (1.5 ms, release, stereo link) -> safety clamp at the ceiling -> Delta / Match -> output.
// Modes: Clip (clipper only), Clip + Limit (clipper 1.5 dB above the ceiling, limiter catches the rest), Limit.
// The limiter's lookahead delay is always in the path, so the latency only depends on Quality, not on the mode.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace cl96
{
constexpr double kTau = 6.283185307179586476925286766559;
constexpr double kPi  = 3.1415926535897932384626433832795;

inline double dbl (double d) noexcept { return std::pow (10.0, d / 20.0); }
inline double clampd (double v, double a, double b) noexcept { return v < a ? a : (v > b ? b : v); }

//==============================================================================
namespace P
{
    enum Id { mode, quality, push, shape, asym, split, lowDrive, release, link, ceiling, out, delta, match, bypass, total };
}

struct ParamInfo { double def; bool logSmooth; bool noSmooth; };
inline const std::array<ParamInfo, P::total>& paramInfo()
{
    static const std::array<ParamInfo, P::total> t {{
        { 1, false, true },       // mode (Clip, Clip + Limit, Limit)
        { 2, false, true },       // quality = oversampling factor 1 / 2 / 4
        { 6, false, false },      // push dB
        { 50, false, false },     // hardness %
        { 0, false, false },      // asym %
        { 40, true, false },      // split Hz (40 = off)
        { 0, false, false },      // low drive dB
        { 60, true, false },      // release ms
        { 100, false, false },    // link %
        { -0.3, false, false },   // ceiling dB
        { 0, false, false },      // output dB
        { 0, false, true },       // delta
        { 0, false, true },       // match
        { 0, false, false },      // bypass
    }};
    return t;
}

constexpr const char* kModeNames[3] = { "Clip", "Clip + Limit", "Limit" };
constexpr double kSplitOff = 40.5;     // split knob at its minimum = Off
constexpr int kMaxD = 1024;            // delay buffers (power of two, > latency at 192 kHz)

//==============================================================================
using Coef  = std::array<double, 5>;
using State = std::array<double, 2>;
inline double bq (const Coef& c, State& s, double x) noexcept
{
    const double y = c[0] * x + s[0];
    s[0] = c[1] * x - c[3] * y + s[1];
    s[1] = c[2] * x - c[4] * y;
    return y;
}
inline void setHP2 (Coef& o, double fs, double f, double q) noexcept
{
    f = clampd (f, 5.0, fs * 0.45);
    const double w = kTau * f / fs, sn = std::sin (w), cs = std::cos (w), al = sn / (2.0 * q), a0 = 1.0 + al;
    o = { (1.0 + cs) / 2.0 / a0, -(1.0 + cs) / a0, (1.0 + cs) / 2.0 / a0, -2.0 * cs / a0, (1.0 - al) / a0 };
}
inline void setLP2 (Coef& o, double fs, double f, double q) noexcept
{
    f = clampd (f, 20.0, fs * 0.45);
    const double w = kTau * f / fs, sn = std::sin (w), cs = std::cos (w), al = sn / (2.0 * q), a0 = 1.0 + al;
    o = { (1.0 - cs) / 2.0 / a0, (1.0 - cs) / a0, (1.0 - cs) / 2.0 / a0, -2.0 * cs / a0, (1.0 - al) / a0 };
}

//==============================================================================
// Kaiser-windowed FIR oversampling (polyphase up, decimating down). 129 / 257 taps: up + down is exactly 64 base-rate
// samples at 2x and 4x. down() is aligned to the first sub-sample of each group, which is what makes it exact.
inline double besselI0 (double x) noexcept
{
    double s = 1.0, t = 1.0;
    for (int k = 1; k < 32; ++k) { const double r = x / (2.0 * k); t *= r * r; s += t; }
    return s;
}

class Oversampler
{
public:
    void prepare (int factor)
    {
        f = factor;
        if (f == 1) return;
        nReal = (f == 2) ? 129 : 257;
        N = ((nReal + f - 1) / f) * f;
        h.assign ((size_t) N, 0.0);
        const double fc = 0.5 / f * 0.9, beta = 7.0, m = (nReal - 1) / 2.0, i0b = besselI0 (beta);
        double sum = 0.0;
        for (int n = 0; n < nReal; ++n)
        {
            const double t = n - m, x = 2.0 * fc * t;
            const double sinc = (t == 0.0) ? 1.0 : std::sin (kPi * x) / (kPi * x);
            const double r = 2.0 * n / (nReal - 1) - 1.0;
            h[(size_t) n] = 2.0 * fc * sinc * besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
            sum += h[(size_t) n];
        }
        for (auto& v : h) v /= sum;
        L = N / f;
        ph.assign ((size_t) f, std::vector<double> ((size_t) L, 0.0));
        for (int p = 0; p < f; ++p)
            for (int j = 0; j < L; ++j)
                ph[(size_t) p][(size_t) j] = h[(size_t) (p + j * f)] * f;
        ui.assign ((size_t) L * 2, 0.0);
        di.assign ((size_t) N * 2, 0.0);
        reset();
    }

    void reset() noexcept
    {
        std::fill (ui.begin(), ui.end(), 0.0);
        std::fill (di.begin(), di.end(), 0.0);
        up = 0; dp = 0;
    }

    void upsample (double x, double* out) noexcept
    {
        up = (up + 1) % L;
        ui[(size_t) up] = x; ui[(size_t) (up + L)] = x;
        const double* u = ui.data() + up + L;
        for (int p = 0; p < f; ++p)
        {
            const double* a = ph[(size_t) p].data();
            double s = 0.0;
            for (int j = 0; j < L; ++j) s += a[j] * u[-j];
            out[p] = s;
        }
    }

    void push (double x) noexcept
    {
        dp = (dp + 1) % N;
        di[(size_t) dp] = x; di[(size_t) (dp + N)] = x;
    }

    double down() const noexcept
    {
        const double* d = di.data() + dp + N - (f - 1);
        double s = 0.0;
        for (int k = 0; k < nReal; ++k) s += h[(size_t) k] * d[-k];
        return s;
    }

private:
    int f = 1, nReal = 0, N = 0, L = 0, up = 0, dp = 0;
    std::vector<double> h, ui, di;
    std::vector<std::vector<double>> ph;
};

//==============================================================================
class Engine
{
public:
    static int lookaheadFor (double fs) noexcept { return std::max (1, (int) std::floor (0.0015 * fs + 0.5)); }
    static int latencyFor (int os, double fs) noexcept { return (os == 1 ? 0 : 64) + lookaheadFor (fs) - 1; }

    Engine()
    {
        const auto& info = paramInfo();
        for (int i = 0; i < P::total; ++i) tgt[(size_t) i] = cur[(size_t) i] = info[(size_t) i].def;
    }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        sa = 1.0 - std::exp (-kBlock / (fs * 0.025));
        La = lookaheadFor (fs);
        for (auto& r : req) r.assign ((size_t) La, 1.0);
        box.assign ((size_t) La, 1.0);
        for (int k = 0; k < 3; ++k)
            for (int s = 0; s < 2; ++s) { U[k][s].prepare (1 << k); D[k][s].prepare (1 << k); }
        setQuality ((int) tgt[P::quality]);
        reset();
    }

    void reset()
    {
        for (int i = 0; i < P::total; ++i) cur[(size_t) i] = tgt[(size_t) i];
        for (int k = 0; k < 3; ++k) for (int s = 0; s < 2; ++s) { U[k][s].reset(); D[k][s].reset(); }
        for (auto& side : sLP) { side[0] = {}; side[1] = {}; }
        for (auto& r : req) std::fill (r.begin(), r.end(), 1.0);
        std::fill (box.begin(), box.end(), 1.0);
        boxSum = La; rp = bp = 0; env = 1.0;
        for (auto& b : lim) b.fill (0.0);
        for (auto& b : raw) b.fill (0.0);
        lw = ww = 0;
        dcx[0] = dcx[1] = dcy[0] = dcy[1] = 0.0;
        design(); derive();
    }

    void setParam (int index, double value) noexcept
    {
        tgt[(size_t) index] = value;
        if (index == P::quality && (int) value != os) setQuality ((int) value);
    }

    int getOversampling() const noexcept { return os; }
    int getLatency() const noexcept { return latencyFor (os, fs); }

    // meters, read and cleared by the caller after process(): input peak, peak after Push, peak after the ceiling
    // (before Delta / Output), output peak and the lowest limiter gain
    double inPeak = 0, pushPeak = 0, redPeak = 0, outPeak = 0, limMin = 1;

    void process (const float* iL, const float* iR, float* oL, float* oR, int n) noexcept
    {
        double ub[4];
        for (int b0 = 0; b0 < n; b0 += kBlock)
        {
            const int e = std::min (n, b0 + kBlock);
            if (smooth()) design();
            derive();
            const int lat = getLatency(), limDelay = La - 1;
            const int k = os == 1 ? 0 : (os == 2 ? 1 : 2);
            for (int s = b0; s < e; ++s)
            {
                const double inL = iL[s], inR = iR[s];
                inPeak = std::max (inPeak, std::max (std::abs (inL), std::abs (inR)));
                // ---- clip stage (per channel) ----
                double cy[2];
                for (int ch = 0; ch < 2; ++ch)
                {
                    double u = (ch ? inR : inL) * pushG;
                    pushPeak = std::max (pushPeak, std::abs (u));
                    if (splitAmt > 0)
                    {
                        const double lo = bq (cLP, sLP[ch][1], bq (cLP, sLP[ch][0], u)), hi = u - lo;
                        const double c = clipC, g = lowG;
                        const double lo2 = c * h (lo * g / c) / g;      // lows pre-clipped on their own
                        u = hi + lo + (lo2 - lo) * splitAmt;
                    }
                    double y;
                    if (! useClip)
                    {
                        y = u;                                           // keep the same latency as the oversampled path
                        if (os != 1) { U[k][ch].upsample (u, ub); for (int p = 0; p < os; ++p) D[k][ch].push (ub[p]); y = D[k][ch].down(); }
                    }
                    else if (os == 1) y = clipOS (u, ch);
                    else { U[k][ch].upsample (u, ub); for (int p = 0; p < os; ++p) D[k][ch].push (clipOS (ub[p], ch)); y = D[k][ch].down(); }
                    cy[ch] = y;
                }
                // ---- lookahead limiter, linked ----
                lim[0][(size_t) lw] = cy[0]; lim[1][(size_t) lw] = cy[1];
                if (useLim)
                {
                    const double c = ceil;
                    const double rl = std::min (1.0, c / (std::abs (cy[0]) + 1e-12)), rr = std::min (1.0, c / (std::abs (cy[1]) + 1e-12));
                    const double mn = std::min (rl, rr);
                    req[0][(size_t) rp] = mn + (rl - mn) * (1.0 - link);
                    req[1][(size_t) rp] = mn + (rr - mn) * (1.0 - link);
                }
                else { req[0][(size_t) rp] = 1.0; req[1][(size_t) rp] = 1.0; }
                rp = (rp + 1) % La;
                // hold-min over the lookahead window, then a box filter of the same length, then release
                double m0 = 1.0, m1 = 1.0;
                const double* q0 = req[0].data(); const double* q1 = req[1].data();
                for (int j = 0; j < La; ++j) { if (q0[j] < m0) m0 = q0[j]; if (q1[j] < m1) m1 = q1[j]; }
                const double mm = std::min (m0, m1);
                boxSum += mm - box[(size_t) bp]; box[(size_t) bp] = mm; bp = (bp + 1) % La;
                const double avg = std::min (1.0, boxSum / La);
                env = avg < env ? avg : env + (avg - env) * rel;
                const double g = env;
                const int ri = (lw - limDelay + kMaxD) & (kMaxD - 1);
                lw = (lw + 1) & (kMaxD - 1);
                const double gL = mm < 1.0 ? g * std::min (1.0, m0 / mm) : g, gR = mm < 1.0 ? g * std::min (1.0, m1 / mm) : g;
                limMin = std::min (limMin, g);
                double yl = lim[0][(size_t) ri] * gL, yr = lim[1][(size_t) ri] * gR;
                // safety clamp: nothing passes the ceiling
                yl = clampd (yl, -ceil, ceil); yr = clampd (yr, -ceil, ceil);
                // ---- dry path aligned to the full latency (for delta and bypass) ----
                raw[0][(size_t) ww] = inL; raw[1][(size_t) ww] = inR;
                const int di = (ww - lat + kMaxD) & (kMaxD - 1);
                const double dl = raw[0][(size_t) di], dr = raw[1][(size_t) di];
                ww = (ww + 1) & (kMaxD - 1);
                redPeak = std::max (redPeak, std::max (std::abs (yl), std::abs (yr)));
                if (deltaOn) { yl = dl * pushG - yl; yr = dr * pushG - yr; }
                yl *= outG; yr *= outG;
                yl += (dl - yl) * byp; yr += (dr - yr) * byp;
                oL[s] = (float) yl; oR[s] = (float) yr;
                outPeak = std::max (outPeak, std::max (std::abs (yl), std::abs (yr)));
            }
        }
    }

private:
    static constexpr int kBlock = 32;

    double fs = 48000.0, sa = 0.0;
    int os = 2, La = 72;
    std::array<double, P::total> tgt {}, cur {};
    Oversampler U[3][2], D[3][2];
    Coef cLP {};
    State sLP[2][2] {};
    std::vector<double> req[2], box;
    double boxSum = 72, env = 1;
    int rp = 0, bp = 0, lw = 0, ww = 0;
    std::array<std::array<double, kMaxD>, 2> lim {}, raw {};
    double dcx[2] {}, dcy[2] {}, dcR = 0.0;
    double pushG = 1, ceil = 1, clipC = 1, p = 2, ip = 0.5, asA = 0, asH = 0, asN = 1, splitAmt = 0, lowG = 1, rel = 0, link = 1, outG = 1, byp = 0;
    bool useClip = true, useLim = true, deltaOn = false;

    void setQuality (int q)
    {
        os = (q == 1 || q == 2 || q == 4) ? q : 2;
        cur[P::quality] = os;
        const int k = os == 1 ? 0 : (os == 2 ? 1 : 2);
        for (int s = 0; s < 2; ++s) { U[k][s].reset(); D[k][s].reset(); }
        dcx[0] = dcx[1] = dcy[0] = dcy[1] = 0.0;
        dcR = 1.0 - kTau * 5.0 / (fs * os);
    }

    bool smooth() noexcept
    {
        const auto& info = paramInfo();
        bool moved = false;
        for (int k = 0; k < P::total; ++k)
        {
            if (k == P::quality) continue;
            const auto& pi = info[(size_t) k];
            const double target = tgt[(size_t) k], v = cur[(size_t) k];
            if (v == target) continue;
            double nv;
            if (pi.noSmooth) nv = target;
            else if (pi.logSmooth) { nv = v * std::pow (target / v, sa); if (std::abs (nv / target - 1.0) < 1e-4) nv = target; }
            else { nv = v + (target - v) * sa; if (std::abs (nv - target) < 1e-5) nv = target; }
            cur[(size_t) k] = nv;
            moved = true;
        }
        return moved;
    }

    void design() noexcept { setLP2 (cLP, fs, std::max (cur[P::split], 40.0), 0.7071); }

    void derive() noexcept
    {
        pushG = dbl (cur[P::push]);
        ceil = dbl (cur[P::ceiling]);
        const int m = (int) cur[P::mode];
        useClip = m != 2; useLim = m != 0;
        clipC = m == 1 ? ceil * dbl (1.5) : ceil;      // the clipper sits a little above the limiter
        p = 2.0 + 18.0 * cur[P::shape] / 100.0;
        ip = 1.0 / p;
        asA = 0.6 * cur[P::asym] / 100.0;
        asH = h (asA); asN = 1.0 / (1.0 + asH);
        splitAmt = clampd ((cur[P::split] - kSplitOff) / 10.0, 0.0, 1.0);
        lowG = dbl (cur[P::lowDrive]);
        rel = 1.0 - std::exp (-1.0 / (fs * cur[P::release] / 1000.0));
        link = cur[P::link] / 100.0;
        outG = dbl (cur[P::out]) * (cur[P::match] > 0.5 ? 1.0 / pushG : 1.0);
        deltaOn = cur[P::delta] > 0.5; byp = cur[P::bypass];
    }

    double h (double v) const noexcept { return v / std::pow (1.0 + std::pow (std::abs (v), p), ip); }
    // clip curve normalised to 1; asym shifts the operating point (even harmonics) and keeps |y| < 1
    double curve (double v) const noexcept { return asA == 0.0 ? h (v) : (h (v + asA) - asH) * asN; }
    double clipOS (double u, int ch) noexcept
    {
        double y = clipC * curve (u / clipC);
        if (asA != 0.0) { const double d = y - dcx[ch] + dcR * dcy[ch]; dcx[ch] = y; dcy[ch] = d; y = d; }
        return y;
    }
};
} // namespace cl96
