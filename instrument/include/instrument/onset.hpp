#ifndef INSTRUMENT_ONSET_H
#define INSTRUMENT_ONSET_H

#include "idsp/filter.hpp"
#include "idsp/functions.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace idsp
{
    struct OnsetResult
    {
        std::array<int, 64> indices{};  // sample indices, sorted ascending by time
        int count{0};                   // number of valid entries in `indices`
    };

    namespace detail
    {
        struct OnsetCandidate
        {
            int   index;
            float strength;
        };
    }

    // Detect onsets in a mono signal. Returns up to 64 most significant onset
    // sample indices, sorted by time. `sample_rate` is the source file's rate
    // (used to convert the cluster window and HPF cutoff to samples).
    //
    // Pipeline: HPF (~150 Hz) → abs → fast-attack / slow-release envelope
    // follower → positive first difference → adaptive threshold → 30 ms
    // cluster suppression → zero-crossing rewind → top-64 by strength.
    inline OnsetResult detect_onsets(const float* mono, size_t num_samples, float sample_rate)
    {
        OnsetResult out{};
        if (mono == nullptr || num_samples < 2 || sample_rate <= 0.f) return out;

        // --- Stage 1+2+3: HPF, rectify, envelope follower ---
        OnepoleFilter<OnePoleType::Highpass> hpf;
        hpf.set_cutoff(150.f / sample_rate);

        // One-pole envelope follower with asymmetric attack/release: track
        // attack via a fast coefficient, release via a slower one.
        const float attack_tc  = 0.003f;  // 3 ms
        const float release_tc = 0.050f;  // 50 ms
        const float a_attack   = std::exp(-1.f / (attack_tc  * sample_rate));
        const float a_release  = std::exp(-1.f / (release_tc * sample_rate));

        std::vector<float> envelope(num_samples, 0.f);
        float env = 0.f;
        for (size_t i = 0; i < num_samples; ++i)
        {
            const float x   = hpf.process(mono[i]);
            const float rect = std::fabs(x);
            const float a   = (rect > env) ? a_attack : a_release;
            env = rect + (env - rect) * a;
            envelope[i] = env;
        }

        // --- Stage 4: positive first difference ---
        std::vector<float> diff(num_samples, 0.f);
        for (size_t i = 1; i < num_samples; ++i)
        {
            const float d = envelope[i] - envelope[i - 1];
            diff[i] = (d > 0.f) ? d : 0.f;
        }

        // --- Stage 5: adaptive threshold (mean + 3*stddev of positive diff) ---
        double sum = 0.0, sq = 0.0;
        for (size_t i = 0; i < num_samples; ++i)
        {
            const double v = diff[i];
            sum += v;
            sq  += v * v;
        }
        const double mean = sum / static_cast<double>(num_samples);
        const double var  = std::max(sq / static_cast<double>(num_samples) - mean * mean, 0.0);
        const double stddev = std::sqrt(var);
        const float threshold = static_cast<float>(mean + 3.0 * stddev);

        // --- Stage 6: cluster suppression — within a 30 ms window, keep only
        //     the strongest candidate. Walk the diff series, accumulating a
        //     "current cluster" until the window expires, then emit.
        const int window_samples = std::max(1, static_cast<int>(0.030f * sample_rate));
        std::vector<detail::OnsetCandidate> candidates;
        candidates.reserve(num_samples / static_cast<size_t>(window_samples) + 1);

        int   best_idx       = -1;
        float best_strength  = 0.f;
        int   cluster_anchor = -window_samples;  // last emitted-cluster start

        for (size_t i = 0; i < num_samples; ++i)
        {
            if (diff[i] <= threshold) continue;

            if (best_idx < 0 || static_cast<int>(i) - cluster_anchor >= window_samples)
            {
                // Emit the previous cluster's best, start a new one.
                if (best_idx >= 0)
                {
                    candidates.push_back({best_idx, best_strength});
                }
                best_idx       = static_cast<int>(i);
                best_strength  = diff[i];
                cluster_anchor = static_cast<int>(i);
            }
            else if (diff[i] > best_strength)
            {
                best_idx      = static_cast<int>(i);
                best_strength = diff[i];
            }
        }
        if (best_idx >= 0) candidates.push_back({best_idx, best_strength});

        // --- Stage 7: zero-crossing rewind, up to ~10 ms back through the
        //     original signal. Find the nearest sign change and snap to it.
        const int rewind_samples = std::max(1, static_cast<int>(0.010f * sample_rate));
        for (auto& c : candidates)
        {
            const int begin = std::max(0, c.index - rewind_samples);
            for (int j = c.index; j > begin; --j)
            {
                const float a = mono[j];
                const float b = mono[j - 1];
                if ((a >= 0.f && b < 0.f) || (a < 0.f && b >= 0.f))
                {
                    c.index = j;
                    break;
                }
            }
        }

        // --- Stage 8: top-64 by strength, then sort by time ---
        if (candidates.size() > 64)
        {
            std::nth_element(candidates.begin(), candidates.begin() + 64, candidates.end(),
                             [](const detail::OnsetCandidate& a, const detail::OnsetCandidate& b)
                             { return a.strength > b.strength; });
            candidates.resize(64);
        }
        std::sort(candidates.begin(), candidates.end(),
                  [](const detail::OnsetCandidate& a, const detail::OnsetCandidate& b)
                  { return a.index < b.index; });

        out.count = static_cast<int>(candidates.size());
        for (int i = 0; i < out.count; ++i) out.indices[i] = candidates[i].index;
        return out;
    }
}

#endif
