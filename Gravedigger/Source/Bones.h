#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>
#include <atomic>
#include <algorithm>

// THE BONES: the captured sample, its chop points, and the sampler that plays them.
namespace gd
{
constexpr int kNumBones  = 16;
constexpr int kFirstNote = 60;   // MIDI 60 = C5 in FL Studio
constexpr int kPeakBins  = 22;
constexpr int kEnvBins   = 720;

struct Bank
{
    juce::AudioBuffer<float> audio;
    double sr = 44100.0;
    int len = 0;
    int bars = 0;
    bool synced = false;
    int numBones = 0;
    std::array<int, kNumBones + 1> starts {};
    std::vector<float> env;                                   // whole-sample envelope, kEnvBins
    std::array<std::array<float, kPeakBins>, kNumBones> peaks {};
};

// ---------------------------------------------------------------- chopping
inline void chopBank (Bank& b, int mode, int variant)
{
    b.numBones = 0;
    if (b.len < 64) return;
    const int n = b.len;
    const auto* L = b.audio.getReadPointer (0);
    const auto* R = b.audio.getNumChannels() > 1 ? b.audio.getReadPointer (1) : L;

    // skip leading silence
    int first = 0;
    while (first < n && std::abs (L[first]) < 0.002f && std::abs (R[first]) < 0.002f) ++first;
    if (first >= n) first = 0;
    first = juce::jmax (0, first - (int) (0.003 * b.sr));
    if (mode == 1 && b.synced) first = 0;

    std::vector<int> st;

    if (mode == 1) // EVEN
    {
        const double span = (double) (n - first) / kNumBones;
        const double shift = (variant % 2 == 1) ? span * 0.5 : 0.0;
        for (int i = 0; i < kNumBones; ++i)
            st.push_back (juce::jlimit (0, n - 1, first + (int) (shift + span * i)));
        if (shift > 0.0) st[0] = first;
    }
    else // HITS: onset detection
    {
        const int hop = 256, win = 1024;
        const int frames = juce::jmax (1, (n - win) / hop);
        std::vector<float> logE ((size_t) frames, 0.0f);
        for (int f = 0; f < frames; ++f)
        {
            double e = 0;
            const int o = f * hop;
            for (int k = 0; k < win; ++k) { const float m = 0.5f * (L[o + k] + R[o + k]); e += (double) m * m; }
            logE[(size_t) f] = (float) std::log (e + 1.0e-9);
        }
        std::vector<float> nov ((size_t) frames, 0.0f);
        for (int f = 1; f < frames; ++f) nov[(size_t) f] = juce::jmax (0.0f, logE[(size_t) f] - logE[(size_t) (f - 1)]);

        struct Cand { int pos; float v; };
        std::vector<Cand> cands;
        for (int f = 3; f < frames - 3; ++f)
        {
            const float v = nov[(size_t) f];
            if (v < 0.6f) continue;
            bool peak = true;
            for (int k = -3; k <= 3 && peak; ++k) if (k != 0 && nov[(size_t) (f + k)] > v) peak = false;
            if (peak) cands.push_back ({ f * hop + win / 2 - hop, v });
        }
        std::sort (cands.begin(), cands.end(), [] (const Cand& a, const Cand& c) { return a.v > c.v; });

        const double minDistSec[3] = { 0.09, 0.15, 0.06 };
        const int minDist = (int) (minDistSec[variant % 3] * b.sr);
        st.push_back (first);
        for (auto& c : cands)
        {
            if ((int) st.size() >= kNumBones) break;
            if (c.pos <= first) continue;
            bool ok = true;
            for (int s : st) if (std::abs (s - c.pos) < minDist) { ok = false; break; }
            if (ok) st.push_back (c.pos - (int) (0.004 * b.sr));
        }
        std::sort (st.begin(), st.end());
        // fill: split the longest bone until we have 16
        while ((int) st.size() < kNumBones)
        {
            int best = 0, bestLen = -1;
            for (size_t i = 0; i < st.size(); ++i)
            {
                const int e = (i + 1 < st.size()) ? st[i + 1] : n;
                if (e - st[i] > bestLen) { bestLen = e - st[i]; best = (int) i; }
            }
            if (bestLen < 2048) break;
            st.insert (st.begin() + best + 1, st[(size_t) best] + bestLen / 2);
        }
    }

    b.numBones = (int) st.size();
    for (int i = 0; i < b.numBones; ++i) b.starts[(size_t) i] = juce::jlimit (0, n - 1, st[(size_t) i]);
    b.starts[(size_t) b.numBones] = n;

    // visuals
    float gmax = 1.0e-6f;
    for (int i = 0; i < n; ++i) gmax = juce::jmax (gmax, std::abs (L[i]), std::abs (R[i]));
    b.env.assign (kEnvBins, 0.0f);
    for (int k = 0; k < kEnvBins; ++k)
    {
        const int a = (int) ((int64_t) n * k / kEnvBins), e = (int) ((int64_t) n * (k + 1) / kEnvBins);
        float m = 0;
        for (int i = a; i < e; ++i) m = juce::jmax (m, std::abs (L[i]), std::abs (R[i]));
        b.env[(size_t) k] = m / gmax;
    }
    for (int bn = 0; bn < kNumBones; ++bn)
    {
        auto& pk = b.peaks[(size_t) bn];
        pk.fill (0.0f);
        if (bn >= b.numBones) continue;
        const int s0 = b.starts[(size_t) bn], s1 = b.starts[(size_t) bn + 1];
        for (int k = 0; k < kPeakBins; ++k)
        {
            const int a = s0 + (int) ((int64_t) (s1 - s0) * k / kPeakBins);
            const int e = s0 + (int) ((int64_t) (s1 - s0) * (k + 1) / kPeakBins);
            float m = 0;
            for (int i = a; i < e; ++i) m = juce::jmax (m, std::abs (L[i]), std::abs (R[i]));
            pk[(size_t) k] = m / gmax;
        }
    }
}

// ---------------------------------------------------------------- sampler
class BonePlayer
{
public:
    void prepare (double newSr) { sr = newSr; allOff(); }
    void allOff() { for (auto& v : voices) v.on = false; }

    // midi: notes C5..D#6 trigger bones (gate). preview: one-shot of a bone, -1 = none.
    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, int n, const Bank& b,
                 float rate, bool reverse, int preview,
                 std::atomic<uint32_t>* hits, std::atomic<int>& lastBone)
    {
        if (b.numBones <= 0 || b.len <= 0) { allOff(); return; }

        if (preview >= 0 && preview < b.numBones) startVoice (-1, preview, 0.9f, true, b, rate, reverse, hits, lastBone);

        int pos = 0;
        for (const auto meta : midi)
        {
            const int sp = juce::jlimit (0, n, meta.samplePosition);
            renderRange (out, pos, sp, b);
            pos = sp;
            const auto m = meta.getMessage();
            if (m.isNoteOn())
            {
                const int idx = m.getNoteNumber() - kFirstNote;
                if (idx >= 0 && idx < b.numBones)
                    startVoice (m.getNoteNumber(), idx, m.getFloatVelocity(), false, b, rate, reverse, hits, lastBone);
            }
            else if (m.isNoteOff())
            {
                for (auto& v : voices) if (v.on && ! v.oneShot && v.note == m.getNoteNumber()) v.releasing = true;
            }
            else if (m.isAllNotesOff() || m.isAllSoundOff())
            {
                for (auto& v : voices) if (v.on) v.releasing = true;
            }
        }
        renderRange (out, pos, n, b);
    }

private:
    struct Voice
    {
        bool on = false, releasing = false, oneShot = false, reverse = false;
        int note = -1, start = 0, end = 0;
        double pos = 0, inc = 1;
        float gain = 1, rel = 1;
        uint32_t age = 0;
    };
    std::array<Voice, 16> voices;
    double sr = 44100.0;
    uint32_t clock = 0;

    void startVoice (int note, int idx, float vel, bool oneShot, const Bank& b, float rate, bool reverse,
                     std::atomic<uint32_t>* hits, std::atomic<int>& lastBone)
    {
        Voice* v = nullptr;
        for (auto& c : voices) if (! c.on) { v = &c; break; }
        if (v == nullptr)
        {
            v = &voices[0];
            for (auto& c : voices) if (c.age < v->age) v = &c;
        }
        v->on = true; v->releasing = false; v->oneShot = oneShot; v->reverse = reverse;
        v->note = note;
        v->start = b.starts[(size_t) idx];
        v->end = b.starts[(size_t) idx + 1];
        const double step = (double) rate * b.sr / sr;
        v->inc = reverse ? -step : step;
        v->pos = reverse ? (double) (v->end - 2) : (double) v->start;
        v->gain = 0.25f + 0.75f * vel;
        v->rel = 1.0f;
        v->age = ++clock;
        hits[idx].fetch_add (1);
        lastBone.store (idx);
    }

    void renderRange (juce::AudioBuffer<float>& out, int from, int to, const Bank& b)
    {
        if (to <= from) return;
        auto* L = out.getWritePointer (0);
        auto* R = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;
        const auto* sL = b.audio.getReadPointer (0);
        const auto* sR = b.audio.getNumChannels() > 1 ? b.audio.getReadPointer (1) : sL;
        const float fadeIn = (float) (0.0015 * b.sr), fadeOut = (float) (0.004 * b.sr);
        const float relCoef = std::exp (-1.0f / (0.03f * (float) sr));

        for (auto& v : voices)
        {
            if (! v.on) continue;
            for (int i = from; i < to; ++i)
            {
                const int i0 = (int) v.pos;
                if (i0 < v.start || i0 + 1 >= v.end) { v.on = false; break; }
                const float fr = (float) (v.pos - (double) i0);
                const float l = sL[i0] + fr * (sL[i0 + 1] - sL[i0]);
                const float r = sR[i0] + fr * (sR[i0 + 1] - sR[i0]);
                const float dS = v.reverse ? (float) ((double) v.end - v.pos) : (float) (v.pos - (double) v.start);
                const float dE = v.reverse ? (float) (v.pos - (double) v.start) : (float) ((double) v.end - v.pos);
                const float e = juce::jmin (1.0f, dS / fadeIn) * juce::jmin (1.0f, dE / fadeOut);
                if (v.releasing) { v.rel *= relCoef; if (v.rel < 0.0005f) { v.on = false; break; } }
                const float g = v.gain * v.rel * e;
                L[i] += l * g;
                if (R != nullptr) R[i] += r * g;
                v.pos += v.inc;
            }
        }
    }
};
} // namespace gd
