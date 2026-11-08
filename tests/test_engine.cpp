#include <cmath>
#include <cstdio>
#include <vector>

#include "Engine.h"
#include "Synth.h"

static int failures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (! (cond)) { std::printf ("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } \
    } while (0)

static const double kSr = 44100.0;
static const double kHiSr = 176400.0;
static const double kTwoPi = 6.283185307179586;

static std::vector<float> sine (double sr, double f, double amp, double seconds)
{
    std::vector<float> x ((size_t) (seconds * sr));
    for (size_t n = 0; n < x.size(); ++n)
        x[n] = (float) (amp * std::sin (kTwoPi * f * (double) n / sr));
    return x;
}

static double rms (const std::vector<float>& y, size_t from = 0)
{
    double s = 0;
    for (size_t n = from; n < y.size(); ++n)
        s += (double) y[n] * y[n];
    return std::sqrt (s / (double) (y.size() - from));
}

static double harmonicMag (const std::vector<float>& y, double sr, double f, int h)
{
    double re = 0, im = 0;
    for (size_t n = 0; n < y.size(); ++n)
    {
        const double ph = kTwoPi * f * h * (double) n / sr;
        re += y[n] * std::cos (ph);
        im += y[n] * std::sin (ph);
    }
    return 2.0 * std::hypot (re, im) / (double) y.size();
}

// Index and value of the largest |sample| inside [lo, hi].
static std::pair<int, float> peakIn (const std::vector<float>& y, int lo, int hi)
{
    int bi = lo;
    float bv = 0;
    for (int i = std::max (0, lo); i <= hi && i < (int) y.size(); ++i)
        if (std::fabs (y[(size_t) i]) > bv) { bv = std::fabs (y[(size_t) i]); bi = i; }
    return { bi, bv };
}

int main()
{
    // 1. All-pass stage keeps the level (it only moves phase)
    {
        psy::AllpassStage a;
        const float coef = 0.5f;
        auto x = sine (kSr, 1000.0, 0.5, 1.0);
        std::vector<float> y (x.size());
        for (size_t n = 0; n < x.size(); ++n)
            y[n] = a.process (x[n], coef);
        CHECK (std::fabs (rms (y, y.size() / 2) / rms (x, x.size() / 2) - 1.0) < 0.01);
    }

    // 2. Chorus: level 0 is transparent; level > 0 with no modulation gives a delayed copy at ~8 ms
    {
        psy::Chorus c;
        c.prepare (kSr);
        c.set (1.0f, 0.0f, 0.0f, 0.0f);
        float l = 0.3f, r = -0.2f;
        c.process (l, r);
        CHECK (std::fabs (l - 0.3f) < 1e-6f && std::fabs (r + 0.2f) < 1e-6f);

        psy::Chorus d;
        d.prepare (kSr);
        d.set (1.0f, 0.0f, 1.0f, 0.0f);
        std::vector<float> out (2000);
        for (size_t n = 0; n < out.size(); ++n)
        {
            float a = n == 0 ? 1.0f : 0.0f, b = a;
            d.process (a, b);
            out[n] = a;
        }
        const auto pk = peakIn (out, 100, 1000);
        CHECK (std::abs (pk.first - (int) std::lround (0.008 * kSr)) <= 1);
        CHECK (pk.second > 0.4f && pk.second < 0.8f); // level 1 -> 1/(1+0.5) = 0.667
    }

    // 3. Phaser: mix 0 is transparent; stays finite with feedback
    {
        psy::Phaser p;
        p.prepare (kSr);
        p.set (0.5f, 0.8f, 0.85f, 0.0f);
        auto x = sine (kSr, 440.0, 0.5, 0.5);
        double maxDiff = 0;
        for (float s : x)
        {
            float l = s, r = s;
            p.process (l, r);
            maxDiff = std::max (maxDiff, (double) std::fabs (l - s));
        }
        CHECK (maxDiff < 1e-6);

        psy::Phaser q;
        q.prepare (kSr);
        q.set (3.0f, 1.0f, 0.85f, 1.0f);
        bool finite = true;
        float peak = 0;
        for (int n = 0; n < (int) (kSr * 3); ++n)
        {
            float s = 0.5f * (float) std::sin (kTwoPi * 220.0 * n / kSr);
            float l = s, r = s;
            q.process (l, r);
            finite = finite && std::isfinite (l) && std::isfinite (r);
            peak = std::max (peak, std::fabs (l));
        }
        CHECK (finite && peak < 20.0f);
    }

    // 4. Delay: echoes land at the set time and fade by roughly the feedback amount
    {
        psy::Delay d;
        d.prepare (kSr);
        d.set (200.0f, 0.5f, 1.0f, 0.0f, 1.0f);
        d.reset();
        const int t = (int) std::lround (0.200 * kSr);
        std::vector<float> out ((size_t) (3 * t + 400));
        for (size_t n = 0; n < out.size(); ++n)
        {
            float a = n == 0 ? 1.0f : 0.0f, b = a;
            d.process (a, b);
            out[n] = a;
        }
        const auto e1 = peakIn (out, t - 40, t + 40);
        const auto e2 = peakIn (out, 2 * t - 60, 2 * t + 60);
        CHECK (std::abs (e1.first - t) <= 5);
        CHECK (e1.second > 0.5f);
        const float ratio = e2.second / e1.second;
        CHECK (ratio > 0.25f && ratio < 0.8f);
    }

    // 5. Reverb: mix 0 transparent; tail decays and stays finite at maximum size
    {
        psy::Reverb rv;
        rv.prepare (kSr);
        rv.set (0.5f, 0.5f, 0.0f);
        float l = 0.4f, r = 0.4f;
        rv.process (l, r);
        CHECK (std::fabs (l - 0.4f) < 1e-6f);

        psy::Reverb big;
        big.prepare (kSr);
        big.set (1.0f, 0.0f, 1.0f);
        std::vector<float> tail ((size_t) (kSr * 12));
        bool finite = true;
        for (size_t n = 0; n < tail.size(); ++n)
        {
            float a = n == 0 ? 1.0f : 0.0f, b = a;
            big.process (a, b);
            finite = finite && std::isfinite (a);
            tail[n] = a;
        }
        std::vector<float> early (tail.begin() + (long) (0.1 * kSr), tail.begin() + (long) (1.1 * kSr));
        std::vector<float> late (tail.end() - (long) kSr, tail.end());
        CHECK (finite);
        CHECK (rms (late) < 0.5 * rms (early));
        CHECK (rms (late) < 0.01);
    }

    // 6. Gain stage: silence stays silent, output bounded, more amp gain means more distortion
    {
        psy::GainStage g;
        g.prepare (kHiSr);
        psy::Params p;
        p.driveOn = true;
        p.drive = 1.0f;
        p.ampGain = 1.0f;
        g.configure (p);
        g.reset();
        float quiet = 0;
        for (int n = 0; n < 20000; ++n)
            quiet = std::max (quiet, std::fabs (g.process (0.0f)));
        CHECK (quiet < 1e-6f);

        const auto chord = synth::powerChord (kHiSr, 1.0);
        g.reset();
        float peak = 0;
        bool finite = true;
        for (float s : chord)
        {
            const float y = g.process (s);
            finite = finite && std::isfinite (y);
            peak = std::max (peak, std::fabs (y));
        }
        CHECK (finite && peak < 4.0f);

        auto thd = [&] (float gain)
        {
            psy::GainStage s;
            s.prepare (kHiSr);
            psy::Params q;
            q.ampGain = gain;
            s.configure (q);
            auto x = sine (kHiSr, 220.0, 0.3, 1.0);
            std::vector<float> y (x.size());
            for (size_t n = 0; n < x.size(); ++n)
                y[n] = s.process (x[n]);
            y.erase (y.begin(), y.begin() + (long) (y.size() / 2));
            double h = 0;
            for (int k = 2; k <= 8; ++k)
                h += std::pow (harmonicMag (y, kHiSr, 220.0, k), 2.0);
            return std::sqrt (h) / harmonicMag (y, kHiSr, 220.0, 1);
        };
        CHECK (thd (1.0f) > thd (0.1f));
    }

    // 7. Whole board: every preset finite and bounded; everything off is bit-transparent
    {
        const auto chord = synth::powerChord (kSr, 2.0);
        for (const auto& pr : psy::presets())
        {
            psy::Board b;
            b.prepare (kSr);
            b.setParams (pr.p);
            b.reset();
            bool finite = true;
            float peak = 0;
            for (float s : chord)
            {
                float l = s, r = s;
                b.process (l, r);
                finite = finite && std::isfinite (l) && std::isfinite (r);
                peak = std::max ({ peak, std::fabs (l), std::fabs (r) });
            }
            if (! finite || peak > 4.0f)
                std::printf ("  preset %s: finite %d peak %.2f\n", pr.name, (int) finite, peak);
            CHECK (finite);
            CHECK (peak < 4.0f);
        }

        psy::Params off;
        off.chOn = off.phOn = off.dlOn = off.rvOn = false;
        psy::Board b;
        b.prepare (kSr);
        b.setParams (off);
        float maxDiff = 0;
        for (float s : chord)
        {
            float l = s, r = s;
            b.process (l, r);
            maxDiff = std::max (maxDiff, std::fabs (l - s));
        }
        CHECK (maxDiff == 0.0f);
    }

    if (failures == 0)
        std::printf ("All engine tests passed.\n");
    return failures == 0 ? 0 : 1;
}
