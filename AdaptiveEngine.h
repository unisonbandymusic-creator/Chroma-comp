#pragma once
// ============================================================================
//  AdaptiveEngine.h  -  "Adaptive analyze": listens to >= 5 s of the incoming
//  audio, measures it, and decides compressor settings from the chosen
//  Feel / Intensity / Transient mode.  (No JUCE dependency; unit-testable.)
//
//  Direct port of the Chroma-Comp web prototype's analysis + decision logic.
//  Listener  : runs on the audio thread, allocation-free (pre-reserved).
//  analyse() / decide() : run on the message thread once listening is done.
//
//  TransientDetector / DynamicTimes : real-time (per-sample, allocation-free)
//  transient tracking that the compressor uses to scale attack and release on
//  the fly. decide() also picks a sensible strength for it (Decision::adapt).
// ============================================================================
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace chroma
{
enum Feel      { Tight = 0, Subtle, Balance, Glue };
enum Intensity { Low = 0, Mid, High };

inline const char* feelName (int f)      { static const char* n[] = { "Tight", "Subtle", "Balance", "Glue" }; return n[f & 3]; }
inline const char* intensityName (int i) { static const char* n[] = { "Low", "Mid", "High" }; return n[i < 0 ? 0 : (i > 2 ? 2 : i)]; }

class Listener
{
public:
    static constexpr int N = 1024, FN = 64;

    void prepare (double sampleRate, double maxSeconds = 15.0)
    {
        sr = sampleRate;
        capE = (int) std::ceil (maxSeconds * sr / N) + 2;
        capF = (int) std::ceil (maxSeconds * sr / FN) + 2;
        E.reserve ((size_t) capE);  Lo.reserve ((size_t) capE);  F.reserve ((size_t) capF);
        a = 1.0 - std::exp (-2.0 * 3.14159265358979323846 * 150.0 / sr);
        begin();
    }

    void begin()
    {
        E.clear(); Lo.clear(); F.clear();
        s = lo = fq = y = pk = 0.0;
        sc = fc = 0; frames = 0; aud = 0; done = false;
    }

    // Feed (pre-compression) audio; returns true once enough has been heard.
    bool process (const float* l, const float* r, int n) noexcept
    {
        if (done) return true;
        for (int i = 0; i < n; ++i)
        {
            const double v = 0.5 * ((double) l[i] + (double) r[i]), vv = v * v;
            s += vv;  fq += vv;
            if (++fc == FN) { if ((int) F.size() < capF) F.push_back ((float) (fq / FN)); fq = 0.0; fc = 0; }
            y += a * (v - y);  lo += y * y;
            pk = std::max (pk, std::fabs (v));
            if (++sc == N)
            {
                const double e = s / N;
                if ((int) E.size() < capE) { E.push_back ((float) e); Lo.push_back ((float) (lo / N)); }
                if (e > 1.0e-7) ++aud;
                s = lo = 0.0;  sc = 0;  ++frames;
                const double sec = (double) frames * N / sr;
                if (sec >= 5.0 && (aud >= 64 || sec >= 15.0)) { done = true; return true; }
            }
        }
        return false;
    }

    double seconds() const noexcept { return (double) frames * N / sr; }
    double sampleRate() const noexcept { return sr; }

    std::vector<float> E, Lo, F;
    double pk = 0.0;

private:
    double sr = 44100.0, a = 0.0, s = 0.0, lo = 0.0, fq = 0.0, y = 0.0;
    int capE = 0, capF = 0, sc = 0, fc = 0, frames = 0, aud = 0;
    bool done = false;
};

// ----------------------------------------------------------------------------
//  Real-time transient detector (audio thread, per sample, no allocation).
//
//  Compares a fast envelope follower with a slow one. On steady material the
//  two agree (ratio ~1); on an onset the fast one jumps first and the ratio
//  rises. The ratio is mapped to 0..1 and exposed at three time scales:
//    onset()    - quick: high while an attack is rising (drives attack scaling)
//    activity() - medium: stays up ~60 ms after a hit (drives release scaling)
//    density()  - slow (~2 s): how much of the time the material is "hitting"
// ----------------------------------------------------------------------------
class TransientDetector
{
public:
    static constexpr float onsetRatio = 1.5f;    // fast/slow ratio where detection starts (~3.5 dB)
    static constexpr float fullRatio  = 4.0f;    // ratio treated as a full-strength transient (~12 dB)
    static constexpr float floorLin   = 1.0e-4f; // ignore onsets below about -80 dBFS

    void prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        fastAtk = alpha (0.25);  fastRel = alpha (15.0);     // fast follower
        slowAtk = alpha (20.0);  slowRel = alpha (80.0);     // slow follower
        onUp  = alpha (0.2);     onDn  = alpha (8.0);        // onset()
        actUp = alpha (1.0);     actDn = alpha (60.0);       // activity()
        densA = alpha (2000.0);                              // density()
        reset();
    }

    void reset() noexcept { fast = slow = on = act = dens = 0.0f; }

    // x: any sample value or the stereo-linked peak; sign is ignored.
    void process (float x) noexcept
    {
        x = std::fabs (x);
        fast += (x > fast ? fastAtk : fastRel) * (x - fast);
        slow += (x > slow ? slowAtk : slowRel) * (x - slow);
        fast = tiny (fast);  slow = tiny (slow);

        const float ratio = fast / (slow + floorLin);
        const float t = std::min (1.0f, std::max (0.0f, (ratio - onsetRatio) * (1.0f / (fullRatio - onsetRatio))));

        on   += (t > on  ? onUp  : onDn)  * (t - on);
        act  += (t > act ? actUp : actDn) * (t - act);
        dens += densA * (act - dens);
        on = tiny (on);  act = tiny (act);  dens = tiny (dens);
    }

    float onset()    const noexcept { return on; }
    float activity() const noexcept { return act; }
    float density()  const noexcept { return dens; }

private:
    double alpha (double ms) const noexcept { return 1.0 - std::exp (-1.0 / (ms * 0.001 * sr)); }
    static float tiny (float v) noexcept { return v < 1.0e-20f ? 0.0f : v; }   // values here are >= 0

    double sr = 44100.0;
    float fastAtk = 0, fastRel = 0, slowAtk = 0, slowRel = 0, onUp = 0, onDn = 0, actUp = 0, actDn = 0, densA = 0;
    float fast = 0, slow = 0, on = 0, act = 0, dens = 0;
};

// Turns the detector outputs into attack / release scaling, in octaves
// (multiplier = 2^octaves) so the caller can apply them with its own fast exp2.
struct DynamicTimes
{
    static constexpr float attackSpanOct  = 1.5f;   // up to x2.8 slower (preserve) or faster (unpreserved)
    static constexpr float releaseSpanOct = 1.5f;   // up to x0.35 (faster) right after a hit

    struct Octaves { float attack, release; };

    // amount 0..1. preserve = true : attack gets slower on onsets so the hit is let through.
    //              preserve = false: attack gets faster on onsets so the hit is caught.
    // Release gets faster after a hit (so one peak doesn't duck the following material), tempered on
    // dense, constantly-hitting material where fast release would pump or distort.
    static Octaves octaves (float onset, float activity, float density, float amount, bool preserve) noexcept
    {
        const float a = std::min (1.0f, std::max (0.0f, amount));
        const float d = std::min (1.0f, std::max (0.0f, density));
        Octaves o;
        o.attack  = (preserve ? attackSpanOct : -attackSpanOct) * a * onset;
        o.release = -releaseSpanOct * a * activity * (1.0f - 0.5f * d);
        return o;
    }
};

struct Analysis
{
    bool ok = false;
    float rise = 8, p50 = 0, p90 = 0, pkDb = 0, crest = 0, bass = 0, sharp = 0, dens = 0;
    int bpm = 0;
    double secs = 0;
    std::vector<float> lv;   // sorted loudness (dB) of the audible frames
};

struct Decision
{
    float threshold = -18, ratio = 3, knee = 8, attack = 10, release = 120, makeup = 0;
    float adapt = 0.0f;       // 0..1 -> Compressor::Params::dynamicsAmount (0 = fixed attack/release)
    bool  preserve = true;    // -> Compressor::Params::preserveTransients
    std::string report;
};

namespace detail
{
inline double rnd (double x) { return std::floor (x + 0.5); }
inline double cl (double v, double a, double b) { return v < a ? a : (v > b ? b : v); }

// median attack time (ms) of the hits (64-sample resolution)
inline float riseMs (const std::vector<float>& Fe, double sr, double p50)
{
    const int n = (int) Fe.size();
    std::vector<double> F ((size_t) n), r;
    for (int i = 0; i < n; ++i) F[(size_t) i] = 10.0 * std::log10 (Fe[(size_t) i] + 1e-10);
    int last = -99;
    for (int i = 3; i < n - 20; ++i)
    {
        if (F[i] - F[i - 2] < 6 || F[i] < p50 - 6 || F[i] <= F[i - 1] || i - last < 10) continue;
        const double fl = std::min ({ F[i - 3], F[i - 2], F[i - 1] });
        double pk = -200; int pi = i;
        for (int j = i; j < i + 20; ++j) if (F[j] > pk) { pk = F[j]; pi = j; }
        if (pk - fl < 8) continue;
        int a = pi, b = pi;
        for (int j = pi; j >= i - 3 && F[j] > fl + 3; --j) a = j;
        for (int j = a; j <= pi; ++j) { b = j; if (F[j] >= pk - 2) break; }
        r.push_back (std::max (0.5, (b - a) * 64.0 / sr * 1000.0));
        last = i;
    }
    if (r.size() < 3) return 8.0f;
    std::sort (r.begin(), r.end());
    return (float) r[r.size() >> 1];
}
} // namespace detail

inline Analysis analyse (const Listener& L)
{
    using namespace detail;
    Analysis A;
    const int n = (int) L.E.size();
    const double sr = L.sampleRate();
    constexpr int N = Listener::N;
    if (n < 32) return A;

    std::vector<double> dB ((size_t) n);
    for (int i = 0; i < n; ++i) dB[(size_t) i] = 10.0 * std::log10 (L.E[(size_t) i] + 1e-10);
    for (double v : dB) if (v > -70.0) A.lv.push_back ((float) v);
    if (A.lv.size() < 32) return A;
    std::sort (A.lv.begin(), A.lv.end());

    auto q = [&] (double p) { return (double) A.lv[(size_t) std::floor (p * (double) (A.lv.size() - 1))]; };
    const double p50 = q(0.5), p90 = q(0.9), pkDb = 20.0 * std::log10 (L.pk + 1e-9);

    double sE = 0, sL = 0;
    for (int i = 0; i < n; ++i) { sE += L.E[(size_t) i]; sL += L.Lo[(size_t) i]; }

    std::vector<double> fl ((size_t) n, 0.0);
    int hits = 0, last = -9;  double jump = 0;
    for (int i = 1; i < n; ++i)
    {
        const double j = dB[(size_t) i] - dB[(size_t) i - 1];
        fl[(size_t) i] = j > 0 ? j : 0;
        if (j > 6 && dB[(size_t) i] > p50 - 6 && i - last > 4) { ++hits; jump += j; last = i; }
    }
    const int lmin = (int) rnd (60.0 / 180.0 * sr / N), lmax = (int) rnd (60.0 / 70.0 * sr / N);
    int bl = 0;  double br = 0;
    for (int l = lmin; l <= lmax; ++l)
    {
        double c = 0;
        for (int i = 0; i + l < n; ++i) c += fl[(size_t) i] * fl[(size_t) (i + l)];
        if (c > br) { br = c; bl = l; }
    }

    A.secs  = (double) n * N / sr;
    A.rise  = riseMs (L.F, sr, p50);
    A.p50 = (float) p50;  A.p90 = (float) p90;  A.pkDb = (float) pkDb;
    A.crest = (float) (pkDb - p50);
    A.bass  = (float) (sL / (sE + 1e-12));
    A.sharp = hits ? (float) (jump / hits) : 0.0f;
    A.dens  = (float) (hits / A.secs);
    A.bpm   = bl ? (int) rnd (60.0 * sr / N / bl) : 0;
    A.ok = true;
    return A;
}

// Your selections steer the result; the audio sets the numbers; intensity scales how hard it works.
inline Decision decide (const Analysis& A, int feel, int intensity, bool keepTransients)
{
    using namespace detail;
    if (! A.ok || A.lv.empty()) return Decision{};   // incomplete analysis: avoid 0/0 in av() below
    struct PBt { double r, a, rl, k, t; };
    static const PBt PBs[4] = { { 1.3, .4, 1.4, .6, -2 }, { .6, 1.2, 1.0, 1.4, 2 }, { .75, 1.6, 1.6, 1.2, -3 }, { .5, .5, 1.5, 1.8, 2 } };
    struct ITt { double t, r, k, a, rl; };
    static const ITt ITs[3] = { { 4, .55, 1.3, 1.3, 1.15 }, { 0, 1, 1, 1, 1 }, { -5, 1.5, .8, .75, .85 } };

    const PBt PB = PBs[feel & 3];
    const ITt I  = ITs[intensity < 0 ? 0 : (intensity > 2 ? 2 : intensity)];
    const bool fastT = ! keepTransients;
    const double TB = keepTransients ? (A.sharp > 10 ? .9 : 1.0) : .9;   // unpreserved: no extra ratio, so fast attack never turns into squashing

    double thr = cl (rnd (((A.p50 + A.p90) / 2 + PB.t + I.t) * 2) / 2, -45, -6);
    const double ratio = cl (rnd ((1 + (cl (A.crest / 2, 2, 10) * PB.r * TB - 1) * I.r) * 2) / 2, 1.5, 16);
    const double knee  = cl (rnd ((18 - A.sharp * .8) * PB.k * I.k), 0, 30);
    const double attack = fastT ? cl (rnd (cl (A.rise * .6, .5, 8) * I.a * 2) / 2, .5, 10)
                                : cl (rnd ((34 - A.sharp * 1.8) * PB.a * I.a), 1, 80);
    const double release = cl (rnd (60000.0 / (A.bpm ? A.bpm : 120) * .35 * PB.rl * I.rl * (fastT ? 1.15 : 1.0) / 5) * 5, 30, 800);

    // makeup: average gain reduction on the louder half, capped so peaks stay under -1 dBFS
    const double s = 1.0 / ratio - 1.0, eK = knee;
    auto gr = [&] (double L)
    {
        const double o = L - thr;
        if (2 * o < -eK) return 0.0;
        if (eK > 0 && 2 * o <= eK) return s * (o + eK / 2) * (o + eK / 2) / (2 * eK);
        return s * o;
    };
    const size_t from = (size_t) std::floor ((double) A.lv.size() * .5);
    auto av = [&] { double c = 0; for (size_t i = from; i < A.lv.size(); ++i) c += gr (A.lv[i]); return c / (double) (A.lv.size() - from); };
    double g = av();
    if (fastT)           for (int i = 0; i < 80 && g < -6 && thr < -6; ++i) { thr += .5; g = av(); }  // average GR held to 6 dB
    if (feel == Glue)  { for (int i = 0; i < 80 && g < -3 && thr < -6; ++i) { thr += .5; g = av(); }
                         for (int i = 0; i < 80 && g > -1 && thr > -45; ++i) { thr -= .5; g = av(); } } // Glue: average GR held at 1-3 dB
    const double room = std::floor ((-1 - (A.pkDb + gr (A.pkDb))) * 2) / 2;
    const double makeup = std::max (0.0, std::min (cl (rnd (-g * .8 * 2) / 2, 0, 24), room));

    Decision d;
    d.threshold = (float) thr; d.ratio = (float) ratio; d.knee = (float) knee;
    d.attack = (float) attack; d.release = (float) release; d.makeup = (float) makeup;

    // strength of the real-time transient-adaptive timing: more for higher intensity and sharper material,
    // less for Glue (which wants a steady, smooth release)
    static const double adaptByIntensity[3] = { .35, .6, .85 };
    static const double adaptByFeel[4]      = { 1.0, .8, .9, .6 };   // Tight, Subtle, Balance, Glue
    const double sharpK = .6 + .4 * cl (A.sharp / 12.0, 0, 1);
    d.adapt    = (float) cl (adaptByIntensity[intensity < 0 ? 0 : (intensity > 2 ? 2 : intensity)] * adaptByFeel[feel & 3] * sharpK, 0, 1);
    d.preserve = keepTransients;

    const std::string bpmStr = A.bpm ? std::to_string (A.bpm) + " BPM, " : std::string();   // keeps the c_str() pointer valid through snprintf

    char buf[768];
    std::snprintf (buf, sizeof (buf),
        "Listened for %.1f s. Tuned for %s, %s transients at %s intensity, attack %.4g ms from a measured %.1f ms transient rise. "
        "Heard %s%.0f dB crest, %.1f hits/s, %s. Threshold %.4g dB, ratio %.4g:1, knee %.4g dB, attack %.4g ms, release %.4g ms, makeup +%.4g dB. "
        "Transient-adaptive timing %.0f%%.",
        A.secs, feelName (feel), keepTransients ? "preserved" : "unpreserved (fast-attack)", intensityName (intensity),
        attack, A.rise,
        bpmStr.c_str(), A.crest, A.dens,
        A.bass > .6 ? "bass-heavy" : (A.bass < .25 ? "light low end" : "balanced low end"),
        thr, ratio, knee, attack, release, makeup, d.adapt * 100.0);
    d.report = buf;
    return d;
}
} // namespace chroma
