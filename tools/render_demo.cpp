// Renders every preset on a synthesized power chord into one stereo WAV, and reports loudness.
//   g++ -O2 -std=c++17 -ISource -Itests tools/render_demo.cpp -o render_demo && ./render_demo demo.wav
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "Engine.h"
#include "Synth.h"

int main (int argc, char** argv)
{
    const std::string outPath = argc > 1 ? argv[1] : "demo_presets.wav";
    const double hiSr = 176400.0, sr = 44100.0;
    const double targetRmsDb = -20.0;

    auto chord = synth::powerChord (hiSr, 3.2);
    chord.resize (chord.size() + (size_t) (1.8 * hiSr), 0.0f); // room for echo and reverb tails

    std::vector<float> allL, allR;
    double t = 0.0;

    for (const auto& pr : psy::presets())
    {
        // Drive + amp at 4x, then down to the normal rate for the time-based pedals.
        psy::GainStage g;
        g.prepare (hiSr);
        g.configure (pr.p);
        std::vector<float> hi (chord.size());
        for (size_t n = 0; n < chord.size(); ++n)
            hi[n] = g.process (chord[n]);
        auto lo = synth::decimate4 (hi);

        psy::Board b;
        b.prepare (sr);
        b.setParams (pr.p);
        std::vector<float> L (lo.size()), R (lo.size());
        for (size_t n = 0; n < lo.size(); ++n)
        {
            float l = lo[n], r = lo[n];
            b.process (l, r);
            L[n] = l;
            R[n] = r;
        }

        double s = 0;
        float peak = 0;
        for (size_t n = 0; n < L.size(); ++n)
        {
            s += 0.5 * ((double) L[n] * L[n] + (double) R[n] * R[n]);
            peak = std::max ({ peak, std::fabs (L[n]), std::fabs (R[n]) });
        }
        const double rmsDb = 20.0 * std::log10 (std::sqrt (s / (double) L.size()) + 1e-12);
        std::printf ("%5.1fs  %-20s rms %6.1f dBFS  peak %.2f  (amp out %.1f dB: suggested %.1f)\n",
                     t, pr.name, rmsDb, peak, (double) psy::GainStage::kAmpOutDb,
                     (double) psy::GainStage::kAmpOutDb + (targetRmsDb - rmsDb));

        float gain = (float) std::pow (10.0, (targetRmsDb - rmsDb) / 20.0);
        if (peak * gain > 0.9f)
            gain = 0.9f / peak;
        for (size_t n = 0; n < L.size(); ++n) { L[n] *= gain; R[n] *= gain; }

        allL.insert (allL.end(), L.begin(), L.end());
        allR.insert (allR.end(), R.begin(), R.end());
        const size_t gap = (size_t) (0.5 * sr);
        allL.insert (allL.end(), gap, 0.0f);
        allR.insert (allR.end(), gap, 0.0f);
        t += (double) L.size() / sr + 0.5;
    }

    synth::writeWavStereo16 (outPath, allL, allR, 44100);
    std::printf ("Wrote %s (%.1f s)\n", outPath.c_str(), (double) allL.size() / sr);
}
