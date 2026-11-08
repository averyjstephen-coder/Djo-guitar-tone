#pragma once
// Test/demo helpers (plain C++): a Karplus-Strong plucked-string power chord, a decimator and a WAV writer.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace synth
{
inline std::vector<float> pluck (double sr, double hz, double seconds, float decay, uint32_t seed)
{
    const int N = (int) std::lround (sr / hz);
    std::vector<float> buf ((size_t) N);
    uint32_t s = seed;
    for (auto& b : buf)
    {
        s = s * 1664525u + 1013904223u;
        b = (float) ((s >> 8) & 0xFFFF) / 32768.0f - 1.0f;
    }
    // Smooth the initial noise burst so the pluck is warm rather than hissy.
    for (int pass = 0; pass < 24; ++pass)
    {
        std::vector<float> t = buf;
        for (int i = 0; i < N; ++i)
            buf[(size_t) i] = 0.25f * t[(size_t) ((i + N - 1) % N)] + 0.5f * t[(size_t) i]
                            + 0.25f * t[(size_t) ((i + 1) % N)];
    }
    float peak = 1e-9f;
    for (float b : buf)
        peak = std::max (peak, std::fabs (b));
    for (auto& b : buf)
        b /= peak;

    std::vector<float> out ((size_t) (seconds * sr));
    int idx = 0;
    for (size_t n = 0; n < out.size(); ++n)
    {
        const int nxt = (idx + 1) % N;
        out[n] = buf[(size_t) idx];
        buf[(size_t) idx] = 0.5f * (buf[(size_t) idx] + buf[(size_t) nxt]) * decay;
        idx = nxt;
    }
    return out;
}

// E5 power chord (E2 + B2 + E3), lightly strummed, hit twice. Peak scaled to 0.5 like a hot guitar signal.
inline std::vector<float> powerChord (double sr, double seconds)
{
    std::vector<float> mix ((size_t) (seconds * sr), 0.0f);
    const double hits[2] = { 0.0, 1.6 };
    const double notes[3] = { 82.41, 123.47, 164.81 };
    uint32_t seed = 777;
    for (double hitT : hits)
        for (int k = 0; k < 3; ++k)
        {
            auto p = pluck (sr, notes[k], seconds, 0.996f, seed += 101);
            const size_t off = (size_t) ((hitT + 0.012 * k) * sr);
            for (size_t n = 0; n + off < mix.size() && n < p.size(); ++n)
                mix[n + off] += p[n];
        }
    float peak = 1e-9f;
    for (float v : mix)
        peak = std::max (peak, std::fabs (v));
    for (auto& v : mix)
        v *= 0.5f / peak;
    return mix;
}

// 4x decimation with a 255-tap Blackman-windowed sinc (cutoff ~19 kHz at a 44.1 kHz output).
inline std::vector<float> decimate4 (const std::vector<float>& in)
{
    const int taps = 255, M = taps / 2;
    const double fc = 19000.0 / 176400.0; // cycles per input sample
    std::vector<double> h ((size_t) taps);
    double sum = 0.0;
    for (int n = 0; n < taps; ++n)
    {
        const double t = n - M;
        const double sinc = t == 0.0 ? 2.0 * fc : std::sin (2.0 * 3.14159265358979323846 * fc * t) / (3.14159265358979323846 * t);
        const double w = 0.42 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * n / (taps - 1))
                       + 0.08 * std::cos (4.0 * 3.14159265358979323846 * n / (taps - 1));
        h[(size_t) n] = sinc * w;
        sum += h[(size_t) n];
    }
    for (auto& v : h)
        v /= sum;

    std::vector<float> out (in.size() / 4);
    for (size_t k = 0; k < out.size(); ++k)
    {
        double acc = 0.0;
        for (int j = 0; j < taps; ++j)
        {
            const long idx = (long) (4 * k) + j - M;
            if (idx >= 0 && idx < (long) in.size())
                acc += h[(size_t) j] * in[(size_t) idx];
        }
        out[k] = (float) acc;
    }
    return out;
}

inline void writeWav16 (const std::string& path, const std::vector<float>& x, int sampleRate)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f)
        return;
    auto w32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) std::fputc ((int) ((v >> (8 * i)) & 0xFF), f); };
    auto w16 = [&] (uint16_t v) { for (int i = 0; i < 2; ++i) std::fputc ((int) ((v >> (8 * i)) & 0xFF), f); };
    const uint32_t dataBytes = (uint32_t) (x.size() * 2);
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataBytes);
    std::fwrite ("WAVEfmt ", 1, 8, f); w32 (16); w16 (1); w16 (1);
    w32 ((uint32_t) sampleRate); w32 ((uint32_t) sampleRate * 2); w16 (2); w16 (16);
    std::fwrite ("data", 1, 4, f); w32 (dataBytes);
    for (float v : x)
    {
        const float c = std::max (-1.0f, std::min (1.0f, v));
        w16 ((uint16_t) (int16_t) std::lround (c * 32767.0f));
    }
    std::fclose (f);
}

inline void writeWavStereo16 (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int sampleRate)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f)
        return;
    auto w32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) std::fputc ((int) ((v >> (8 * i)) & 0xFF), f); };
    auto w16 = [&] (uint16_t v) { for (int i = 0; i < 2; ++i) std::fputc ((int) ((v >> (8 * i)) & 0xFF), f); };
    const size_t n = std::min (l.size(), r.size());
    const uint32_t dataBytes = (uint32_t) (n * 4);
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataBytes);
    std::fwrite ("WAVEfmt ", 1, 8, f); w32 (16); w16 (1); w16 (2);
    w32 ((uint32_t) sampleRate); w32 ((uint32_t) sampleRate * 4); w16 (4); w16 (16);
    std::fwrite ("data", 1, 4, f); w32 (dataBytes);
    for (size_t i = 0; i < n; ++i)
    {
        const float cl = std::max (-1.0f, std::min (1.0f, l[i]));
        const float cr = std::max (-1.0f, std::min (1.0f, r[i]));
        w16 ((uint16_t) (int16_t) std::lround (cl * 32767.0f));
        w16 ((uint16_t) (int16_t) std::lround (cr * 32767.0f));
    }
    std::fclose (f);
}
} // namespace synth
