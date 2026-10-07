#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>

namespace chroma
{
// ITU-R BS.1770 momentary loudness (K-weighted, 400 ms window updated every 100 ms) for the loudness meter.
// Real-time safe: no allocation, no locks. process() runs on the audio thread, `momentary` is read by the GUI.
class LoudnessMeter
{
public:
    static constexpr float kFloorLufs = -120.0f;
    std::atomic<float> momentary { kFloorLufs };     // LUFS (momentary, ungated)

    void prepare (double sampleRate)
    {
        const double sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        const double pi = 3.14159265358979323846;
        blockLen = std::max (1, (int) std::lround (sr * 0.1));          // 100 ms hop, 4 hops = 400 ms

        {   // stage 1: high-shelf "head" filter
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (pi * f0 / sr), Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                      2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        {   // stage 2: RLB high-pass
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (pi * f0 / sr);
            const double a0 = 1.0 + K / Q + K * K;
            rlb = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        reset();
    }

    void reset() noexcept
    {
        for (auto& c : chan) c = {};
        for (auto& r : ring) r = 0.0;
        ringPos = 0;  acc = 0.0;  count = 0;
        momentary.store (kFloorLufs, std::memory_order_relaxed);
    }

    // data: one pointer per channel (1 or 2 are used; both weighted 1.0 as in BS.1770 for L/R)
    void process (const float* const* data, int numCh, int n) noexcept
    {
        numCh = std::min (numCh, 2);
        if (numCh <= 0 || data == nullptr) return;
        for (int i = 0; i < n; ++i)
        {
            double e = 0.0;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const double v = chan[ch].tick (data[ch][i], shelf, rlb);
                e += v * v;
            }
            acc += e;
            if (++count >= blockLen)
            {
                ring[ringPos] = acc / (double) blockLen;
                ringPos = (ringPos + 1) & 3;
                acc = 0.0;  count = 0;

                const double mean = 0.25 * (ring[0] + ring[1] + ring[2] + ring[3]);
                const float lufs = (float) (-0.691 + 10.0 * std::log10 (std::max (mean, 1.0e-12)));
                momentary.store (std::max (kFloorLufs, lufs), std::memory_order_relaxed);
            }
        }
    }

private:
    struct Coeffs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct Chan
    {
        double s1z1 = 0, s1z2 = 0, s2z1 = 0, s2z2 = 0;      // transposed direct form II states
        double tick (float x, const Coeffs& a, const Coeffs& b) noexcept
        {
            double y = a.b0 * (double) x + s1z1;
            s1z1 = a.b1 * (double) x - a.a1 * y + s1z2;
            s1z2 = a.b2 * (double) x - a.a2 * y;
            const double w = y;
            y = b.b0 * w + s2z1;
            s2z1 = b.b1 * w - b.a1 * y + s2z2;
            s2z2 = b.b2 * w - b.a2 * y;
            return y;
        }
    };

    Coeffs shelf, rlb;
    Chan chan[2];
    double ring[4] {};
    double acc = 0.0;
    int ringPos = 0, count = 0, blockLen = 4800;
};
} // namespace chroma
