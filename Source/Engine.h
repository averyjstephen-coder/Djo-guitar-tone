#pragma once
// Psych Pedalboard: plain C++ DSP engine (no JUCE), so it can be unit-tested and rendered offline.
//
// Chain:  [Drive] -> [Top-boost amp + cab]  (nonlinear, run oversampled)   -> GainStage
//         [Chorus] -> [Phaser] -> [Delay] -> [Reverb]                      -> Board (normal sample rate)
//
// These are generic implementations voiced after the kinds of pedals and amps involved, not circuit models.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace psy
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

inline float dbToLin (float db) { return std::pow (10.0f, db / 20.0f); }

// ------------------------------------------------------------------ parameters and presets

struct Params
{
    bool driveOn = false;
    float drive = 0.4f, driveTone = 0.5f, driveLevelDb = 0.0f;

    bool ampOn = true;
    float ampGain = 0.35f, ampTreble = 0.5f;
    bool cab = true;

    bool chOn = true;
    float chRate = 0.8f, chDepth = 0.5f, chLevel = 0.5f, chEq = 0.0f;

    bool phOn = false;
    float phRate = 0.3f, phDepth = 0.7f, phFb = 0.4f, phMix = 0.5f;

    bool dlOn = false;
    float dlTimeMs = 360.0f, dlFb = 0.3f, dlTone = 0.5f, dlWobble = 0.2f, dlMix = 0.25f;

    bool rvOn = true;
    float rvSize = 0.5f, rvDamp = 0.5f, rvMix = 0.2f;

    float outDb = 0.0f;
};

struct Preset
{
    const char* name;
    const char* note;
    Params p;
};

inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list = []
    {
        std::vector<Preset> v;
        auto add = [&] (const char* name, const char* note, auto tweak)
        {
            Params p;
            p.chOn = false;
            p.rvOn = false;
            tweak (p);
            v.push_back ({ name, note, p });
        };

        add ("Clean Chorus Pop", "Top-boost amp near the edge of breakup, chorus, a little room.", [] (Params& p)
        {
            p.ampGain = 0.30f; p.ampTreble = 0.55f;
            p.chOn = true; p.chRate = 0.7f; p.chDepth = 0.5f; p.chLevel = 0.5f;
            p.rvOn = true; p.rvSize = 0.4f; p.rvMix = 0.18f;
        });
        add ("Palm-Mute Chorus", "A bit more amp gain, slow deep chorus: good for muted verse parts.", [] (Params& p)
        {
            p.ampGain = 0.45f; p.ampTreble = 0.5f;
            p.chOn = true; p.chRate = 0.4f; p.chDepth = 0.65f; p.chLevel = 0.6f;
            p.rvOn = true; p.rvSize = 0.4f; p.rvMix = 0.12f;
        });
        add ("Phase Dream", "Slow phaser into a dark echo and a big room.", [] (Params& p)
        {
            p.ampGain = 0.30f;
            p.phOn = true; p.phRate = 0.2f; p.phDepth = 0.8f; p.phFb = 0.55f; p.phMix = 0.6f;
            p.dlOn = true; p.dlTimeMs = 420.0f; p.dlFb = 0.35f; p.dlTone = 0.4f; p.dlWobble = 0.3f; p.dlMix = 0.22f;
            p.rvOn = true; p.rvSize = 0.7f; p.rvMix = 0.3f;
        });
        add ("Fuzzy Psych Lead", "Overdrive into the amp, light chorus and echo for lead lines.", [] (Params& p)
        {
            p.driveOn = true; p.drive = 0.75f; p.driveTone = 0.45f; p.driveLevelDb = -5.0f;
            p.ampGain = 0.55f;
            p.chOn = true; p.chRate = 0.5f; p.chDepth = 0.4f; p.chLevel = 0.3f;
            p.dlOn = true; p.dlTimeMs = 330.0f; p.dlFb = 0.3f; p.dlMix = 0.2f;
            p.rvOn = true; p.rvMix = 0.22f;
        });
        add ("Tape Slapback", "Short wobbly echo on a clean amp.", [] (Params& p)
        {
            p.ampGain = 0.30f;
            p.dlOn = true; p.dlTimeMs = 120.0f; p.dlFb = 0.2f; p.dlTone = 0.35f; p.dlWobble = 0.35f; p.dlMix = 0.3f;
            p.rvOn = true; p.rvMix = 0.12f;
        });
        add ("Shimmer Wash", "Everything on and slow: chorus, phaser, echo, long reverb.", [] (Params& p)
        {
            p.ampGain = 0.28f;
            p.chOn = true; p.chRate = 0.3f; p.chDepth = 0.7f; p.chLevel = 0.6f;
            p.phOn = true; p.phRate = 0.12f; p.phDepth = 0.7f; p.phFb = 0.5f; p.phMix = 0.5f;
            p.dlOn = true; p.dlTimeMs = 520.0f; p.dlFb = 0.45f; p.dlTone = 0.35f; p.dlWobble = 0.4f; p.dlMix = 0.3f;
            p.rvOn = true; p.rvSize = 0.9f; p.rvDamp = 0.6f; p.rvMix = 0.4f;
        });
        return v;
    }();
    return list;
}

// ------------------------------------------------------------------ building blocks

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0.0f; }

    void lowpass (double sr, double f, double q)
    {
        const double w = kTwoPi * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highpass (double sr, double f, double q)
    {
        const double w = kTwoPi * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void peak (double sr, double f, double q, double db)
    {
        const double A = std::pow (10.0, db / 40.0);
        const double w = kTwoPi * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void highshelf (double sr, double f, double q, double db)
    {
        const double A = std::pow (10.0, db / 40.0);
        const double w = kTwoPi * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        const double beta = 2.0 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + beta), -2 * A * ((A - 1) + (A + 1) * c),
             A * ((A + 1) + (A - 1) * c - beta), (A + 1) - (A - 1) * c + beta,
             2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - beta);
    }

private:
    void set (double nb0, double nb1, double nb2, double a0, double na1, double na2)
    {
        b0 = (float) (nb0 / a0);
        b1 = (float) (nb1 / a0);
        b2 = (float) (nb2 / a0);
        a1 = (float) (na1 / a0);
        a2 = (float) (na2 / a0);
    }
};

struct Coupler
{
    float x1 = 0.0f, y1 = 0.0f;
    float process (float x, float R)
    {
        const float y = x - x1 + R * y1;
        x1 = x;
        y1 = y;
        return y;
    }
    void reset() { x1 = y1 = 0.0f; }
};

inline float tube (float x, float bias) { return std::tanh (x + bias) - std::tanh (bias); }

// Fractional delay read from a circular buffer; `w` is the index of the most recently written sample.
inline float readFrac (const std::vector<float>& buf, int w, float delay)
{
    const int size = (int) buf.size();
    const int whole = (int) delay;
    const float frac = delay - (float) whole;
    int i0 = w - whole;
    int i1 = i0 - 1;
    while (i0 < 0) i0 += size;
    while (i1 < 0) i1 += size;
    return buf[(size_t) i0] * (1.0f - frac) + buf[(size_t) i1] * frac;
}

// ------------------------------------------------------------------ drive + amp (run oversampled)

class GainStage
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        atk = 1.0f - std::exp ((float) (-1.0 / (0.002 * sr)));
        rel = 1.0f - std::exp ((float) (-1.0 / (0.120 * sr)));
        gainSmooth = 1.0f - std::exp ((float) (-1.0 / (0.010 * sr)));
        dcR = (float) std::exp (-kTwoPi * 20.0 / sr);
        reset();
        configure (Params {});
        gCur = gTarget;
    }

    void reset()
    {
        dHp.reset(); dLp.reset(); aHp.reset(); aMid.reset(); aTreble.reset(); cab1.reset(); cab2.reset();
        dc.reset();
        env = 0.0f;
    }

    void configure (const Params& p)
    {
        driveOn = p.driveOn;
        ampOn = p.ampOn;
        cabOn = p.cab;

        dHp.highpass (sr, 100.0, 0.707);
        dGain = dbToLin (4.0f + p.drive * 28.0f);
        dLp.lowpass (sr, 2000.0 + (double) p.driveTone * 6000.0, 0.707);
        dLevel = dbToLin (-10.0f + p.driveLevelDb);

        aHp.highpass (sr, 90.0, 0.707);
        gTarget = dbToLin (6.0f + p.ampGain * 30.0f);
        aMid.peak (sr, 1500.0, 1.0, 2.0);
        aTreble.highshelf (sr, 3000.0, 0.707, -4.0 + 12.0 * (double) p.ampTreble);
        cab1.lowpass (sr, 6500.0, 0.6);
        cab2.lowpass (sr, 6500.0, 0.8);
        aOut = dbToLin (kAmpOutDb);
    }

    float process (float x)
    {
        if (driveOn)
        {
            float v = dHp.process (x);
            v = tube (v * dGain, 0.05f);
            x = dLp.process (v) * dLevel;
        }
        if (ampOn)
        {
            x = aHp.process (x);
            const float a = std::fabs (x);
            env += (a > env ? atk : rel) * (a - env);
            gCur += gainSmooth * (gTarget - gCur);
            float v = tube (x * gCur / (1.0f + 0.45f * env), 0.10f);
            v = dc.process (v, dcR);
            v = aMid.process (v);
            v = aTreble.process (v);
            if (cabOn)
            {
                v = cab1.process (v);
                v = cab2.process (v);
            }
            x = v * aOut;
        }
        return x;
    }

    static constexpr float kAmpOutDb = -10.0f;

private:
    double sr = 44100.0;
    Biquad dHp, dLp, aHp, aMid, aTreble, cab1, cab2;
    Coupler dc;
    bool driveOn = false, ampOn = true, cabOn = true;
    float dGain = 1, dLevel = 1, gCur = 1, gTarget = 1, aOut = 1;
    float env = 0, atk = 0.01f, rel = 0.0001f, gainSmooth = 0.001f, dcR = 0.999f;
};

// ------------------------------------------------------------------ chorus (Boss CH-1-style: short modulated delay, stereo)

class Chorus
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        size = (int) (0.05 * sr) + 8;
        for (auto& b : buf) b.assign ((size_t) size, 0.0f);
        reset();
    }
    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& s : shelf) s.reset();
        w = 0;
        phase = 0.0;
    }
    void set (float rateHz, float depth, float levelAmt, float eq)
    {
        rate = rateHz;
        depthAmt = depth;
        level = levelAmt;
        for (auto& s : shelf) s.highshelf (sr, 2500.0, 0.707, eq * 6.0);
    }
    void process (float& l, float& r)
    {
        buf[0][(size_t) w] = l;
        buf[1][(size_t) w] = r;

        const float base = 0.008f * (float) sr;   // 8 ms centre delay
        const float mod = depthAmt * 0.004f * (float) sr; // up to +/- 4 ms
        const float lfo = (float) std::sin (kTwoPi * phase);

        float wetL = readFrac (buf[0], w, base + mod * lfo);
        float wetR = readFrac (buf[1], w, base - mod * lfo); // opposite phase for stereo width
        wetL = shelf[0].process (wetL);
        wetR = shelf[1].process (wetR);

        const float g = 1.0f / (1.0f + 0.5f * level);
        l = (l + level * wetL) * g;
        r = (r + level * wetR) * g;

        w = (w + 1) % size;
        phase += (double) rate / sr;
        if (phase >= 1.0) phase -= 1.0;
    }

private:
    double sr = 44100.0, phase = 0.0;
    int size = 2048, w = 0;
    std::array<std::vector<float>, 2> buf;
    std::array<Biquad, 2> shelf;
    float rate = 0.8f, depthAmt = 0.5f, level = 0.5f;
};

// ------------------------------------------------------------------ phaser (4 all-pass stages, Phase 90-style)

struct AllpassStage
{
    float z = 0.0f;
    float process (float x, float a)
    {
        const float y = a * x + z;
        z = x - a * y;
        return y;
    }
};

class Phaser
{
public:
    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    void reset()
    {
        for (auto& ch : st) for (auto& s : ch) s.z = 0.0f;
        last = { 0.0f, 0.0f };
        phase = 0.0;
    }
    void set (float rateHz, float depth, float feedback, float mixAmt)
    {
        rate = rateHz; depthAmt = depth; fb = feedback; mix = mixAmt;
    }
    void process (float& l, float& r)
    {
        float* io[2] = { &l, &r };
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const double ph = phase + (ch == 1 ? 0.25 : 0.0); // right side 90 degrees apart
            const double lfo = std::sin (kTwoPi * ph);
            const double fc = 949.0 * std::pow (3.16, (double) depthAmt * lfo); // sweeps ~300 Hz .. 3 kHz
            const double t = std::tan (kPi * std::min (fc, sr * 0.45) / sr);
            const float a = (float) ((t - 1.0) / (t + 1.0));

            const float in = *io[ch];
            float v = in + fb * last[ch];
            for (auto& s : st[ch])
                v = s.process (v, a);
            last[ch] = v;
            *io[ch] = in * (1.0f - 0.5f * mix) + v * (0.5f * mix);
        }
        phase += (double) rate / sr;
        if (phase >= 1.0) phase -= 1.0;
    }

private:
    double sr = 44100.0, phase = 0.0;
    std::array<std::array<AllpassStage, 4>, 2> st;
    std::array<float, 2> last { 0.0f, 0.0f };
    float rate = 0.3f, depthAmt = 0.7f, fb = 0.4f, mix = 0.5f;
};

// ------------------------------------------------------------------ delay (tape-style: dark repeats, wobble)

class Delay
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        size = (int) (1.2 * sr) + 8;
        for (auto& b : buf) b.assign ((size_t) size, 0.0f);
        reset();
    }
    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        lp = { 0.0f, 0.0f };
        w = 0;
        phase = 0.0;
        timeCur = timeTarget;
    }
    void set (float timeMs, float feedback, float tone, float wobbleAmt, float mixAmt)
    {
        timeTarget = timeMs;
        fb = feedback;
        wobble = wobbleAmt;
        mix = mixAmt;
        const double fc = 1500.0 + (double) tone * 9000.0;
        lpCoef = 1.0f - (float) std::exp (-kTwoPi * fc / sr);
    }
    void process (float& l, float& r)
    {
        timeCur += 0.0005f * (timeTarget - timeCur); // slew so time changes glide
        float* io[2] = { &l, &r };
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const double ph = phase + (ch == 1 ? 0.33 : 0.0);
            const float wob = wobble * 0.0025f * (float) sr * (float) std::sin (kTwoPi * ph);
            float d = timeCur * 0.001f * (float) sr * (ch == 1 ? 1.07f : 1.0f) + wob;
            d = std::clamp (d, 2.0f, (float) (size - 4));

            const float del = readFrac (buf[ch], w, d);
            lp[ch] += lpCoef * (del - lp[ch]);

            const float in = *io[ch];
            buf[ch][(size_t) ((w + 1) % size)] = in + fb * std::tanh (lp[ch]);
            *io[ch] = in + mix * 0.8f * del;
        }
        w = (w + 1) % size;
        phase += 0.7 / sr;
        if (phase >= 1.0) phase -= 1.0;
    }

private:
    double sr = 44100.0, phase = 0.0;
    int size = 4096, w = 0;
    std::array<std::vector<float>, 2> buf;
    std::array<float, 2> lp { 0.0f, 0.0f };
    float timeCur = 360.0f, timeTarget = 360.0f, fb = 0.3f, wobble = 0.2f, mix = 0.25f, lpCoef = 0.5f;
};

// ------------------------------------------------------------------ reverb (Freeverb-style)

class Reverb
{
public:
    void prepare (double sampleRate)
    {
        const double scale = sampleRate / 44100.0;
        static const int combT[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static const int apT[4] = { 556, 441, 341, 225 };
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const int spread = ch == 0 ? 0 : 23;
            for (size_t i = 0; i < 8; ++i)
                comb[ch][i].init ((int) std::lround ((combT[i] + spread) * scale));
            for (size_t i = 0; i < 4; ++i)
                ap[ch][i].init ((int) std::lround ((apT[i] + spread) * scale));
        }
    }
    void reset()
    {
        for (auto& ch : comb) for (auto& c : ch) c.clear();
        for (auto& ch : ap) for (auto& a : ch) a.clear();
    }
    void set (float size, float damp, float mixAmt)
    {
        feedback = 0.7f + 0.28f * std::clamp (size, 0.0f, 1.0f);
        damping = 0.4f * std::clamp (damp, 0.0f, 1.0f);
        mix = mixAmt;
    }
    void process (float& l, float& r)
    {
        const float in = (l + r) * 0.015f;
        float outL = 0.0f, outR = 0.0f;
        for (auto& c : comb[0]) outL += c.process (in, feedback, damping);
        for (auto& c : comb[1]) outR += c.process (in, feedback, damping);
        for (auto& a : ap[0]) outL = a.process (outL);
        for (auto& a : ap[1]) outR = a.process (outR);
        l += outL * 3.0f * mix;
        r += outR * 3.0f * mix;
    }

private:
    struct Comb
    {
        std::vector<float> buf;
        int idx = 0;
        float store = 0.0f;
        void init (int n) { buf.assign ((size_t) std::max (1, n), 0.0f); idx = 0; store = 0.0f; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); idx = 0; store = 0.0f; }
        float process (float x, float fbk, float damp)
        {
            const float out = buf[(size_t) idx];
            store = out * (1.0f - damp) + store * damp;
            buf[(size_t) idx] = x + store * fbk;
            if (++idx >= (int) buf.size()) idx = 0;
            return out;
        }
    };
    struct Allpass
    {
        std::vector<float> buf;
        int idx = 0;
        void init (int n) { buf.assign ((size_t) std::max (1, n), 0.0f); idx = 0; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); idx = 0; }
        float process (float x)
        {
            const float b = buf[(size_t) idx];
            const float out = -x + b;
            buf[(size_t) idx] = x + b * 0.5f;
            if (++idx >= (int) buf.size()) idx = 0;
            return out;
        }
    };

    std::array<std::array<Comb, 8>, 2> comb;
    std::array<std::array<Allpass, 4>, 2> ap;
    float feedback = 0.84f, damping = 0.2f, mix = 0.2f;
};

// ------------------------------------------------------------------ the time-based half of the board

class Board
{
public:
    void prepare (double sampleRate)
    {
        chorus.prepare (sampleRate);
        phaser.prepare (sampleRate);
        delay.prepare (sampleRate);
        reverb.prepare (sampleRate);
        setParams (Params {});
        delay.reset();
    }
    void reset() { chorus.reset(); phaser.reset(); delay.reset(); reverb.reset(); }

    void setParams (const Params& np)
    {
        p = np;
        chorus.set (p.chRate, p.chDepth, p.chLevel, p.chEq);
        phaser.set (p.phRate, p.phDepth, p.phFb, p.phMix);
        delay.set (p.dlTimeMs, p.dlFb, p.dlTone, p.dlWobble, p.dlMix);
        reverb.set (p.rvSize, p.rvDamp, p.rvMix);
    }

    void process (float& l, float& r)
    {
        if (p.chOn) chorus.process (l, r);
        if (p.phOn) phaser.process (l, r);
        if (p.dlOn) delay.process (l, r);
        if (p.rvOn) reverb.process (l, r);
    }

private:
    Params p;
    Chorus chorus;
    Phaser phaser;
    Delay delay;
    Reverb reverb;
};
} // namespace psy
