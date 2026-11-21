#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>
#include <memory>
#include "Dsp.h"

// THE BODY: five synthesized instruments (0 Piano, 1 Rhodes, 2 Nylon, 3 Strings, 4 Choir)
// plus the sampled VCSL library (5+): LIVE and HAUNTED instruments.
namespace gd
{
constexpr int kNumSynthBodies = 5;

struct Zone { juce::AudioBuffer<float> audio; double sr = 44100.0; float root = 60.0f; };
struct SampleSet { std::vector<Zone> zones; float release = 0.3f; bool oneshot = false; };
using SampleSetPtr = std::shared_ptr<const SampleSet>;

struct SampleSource
{
    virtual ~SampleSource() = default;
    virtual SampleSetPtr currentSet (int body) = 0;   // audio thread; may return nullptr while loading
};

struct GdSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

class GdVoice : public juce::SynthesiserVoice
{
public:
    GdVoice (std::atomic<float>* bodyP, std::atomic<float>* sinkP, SampleSource* src, int seed)
        : bodyParam (bodyP), sinkParam (sinkP), source (src)
    {
        noise.s = 12345u + (uint32_t) seed * 7919u;
        ks.assign (8192, 0.0f);
    }

    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<GdSound*> (s) != nullptr; }
    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void startNote (int note, float velocity, juce::SynthesiserSound*, int) override
    {
        sr     = getSampleRate();
        engine = juce::jmax (0, (int) std::lround (bodyParam->load()));
        const float sink = sinkParam->load();
        freq = (float) juce::MidiMessage::getMidiNoteInHertz (note) * std::pow (2.0f, -sink / 12.0f);
        vel = juce::jlimit (0.05f, 1.0f, velocity);
        t = 0; releasing = false; rel = 1.0f; attack = 0.0f; peakTrack = 0.0f; sampleEnded = false;
        relCoef = std::exp (-1.0f / (releaseTime() * (float) sr));

        switch (engine)
        {
            case 0:  startPiano();  break;
            case 1:  startRhodes(); break;
            case 2:  startNylon();  break;
            case 3:
            case 4:  startPad();    break;
            default: startSampler (note, sink); break;
        }
        if (engine >= kNumSynthBodies && zone == nullptr) clearCurrentNote();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff) { if (! (engine >= kNumSynthBodies && set != nullptr && set->oneshot)) releasing = true; }
        else              { clearCurrentNote(); set.reset(); zone = nullptr; }
    }

    void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
    {
        if (! isVoiceActive()) return;
        auto* L = out.getWritePointer (0);
        auto* R = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;
        const float g = (0.3f + 0.7f * vel) * engineGain();
        const int minLen = (int) (sr * 0.25);

        for (int i = 0; i < num; ++i)
        {
            float l = 0, r = 0;
            switch (engine)
            {
                case 0:  piano (l, r);   break;
                case 1:  rhodes (l, r);  break;
                case 2:  nylon (l, r);   break;
                case 3:  strings (l, r); break;
                case 4:  choir (l, r);   break;
                default: sampler (l, r); break;
            }
            if (releasing) rel *= relCoef;
            L[start + i] += l * g * rel;
            if (R != nullptr) R[start + i] += r * g * rel;
            ++t;

            if ((releasing && rel < 0.0005f) || (t > minLen && peakTrack < 0.00001f) || sampleEnded)
            {
                clearCurrentNote();
                set.reset(); zone = nullptr;
                break;
            }
        }
    }

private:
    std::atomic<float>* bodyParam;
    std::atomic<float>* sinkParam;
    SampleSource* source;
    Noise noise;
    double sr = 44100.0;
    int engine = 0;
    float freq = 440, vel = 1, rel = 1, relCoef = 0.999f, attack = 0, peakTrack = 0;
    int t = 0;
    bool releasing = false;

    float releaseTime() const
    {
        switch (engine) { case 0: return 0.12f; case 1: return 0.25f; case 2: return 0.18f; case 3: return 0.45f; case 4: return 0.6f; default: return 0.3f; }
    }
    float engineGain() const
    {
        switch (engine) { case 0: return 0.3f; case 1: return 0.3f; case 2: return 0.6f; case 3: return 0.15f; case 4: return 1.5f; default: return 0.55f; }
    }
    inline void track (float s) { peakTrack = juce::jmax (std::abs (s), peakTrack * 0.9997f); }
    inline float atk (float sec) { attack = juce::jmin (1.0f, attack + 1.0f / (sec * (float) sr)); return attack; }

    // ---------------- PIANO: inharmonic partials, two strings per partial, hammer noise
    static constexpr int NP = 24;
    float pc[NP] {}, ps[NP] {}, pcw[NP] {}, psw[NP] {}, pa[NP] {}, pd[NP] {};
    int np = 0;
    Svf hammer; float hammerEnv = 0, hammerDecay = 0;

    void startPiano()
    {
        np = 0;
        const float B  = 0.00025f;
        const float T0 = 4.0f * std::pow (261.6f / freq, 0.5f);
        for (int n = 1; n <= NP / 2; ++n)
        {
            const float fn = (float) n * freq * std::sqrt (1.0f + B * (float) (n * n));
            if (fn > (float) sr * 0.42f) break;
            const float amp = std::pow ((float) n, -1.15f) * std::exp (-(float) (n - 1) * (0.42f - 0.3f * vel));
            const float T60 = T0 / (1.0f + 0.45f * (float) (n - 1));
            for (int s = 0; s < 2; ++s)
            {
                const float f = fn * (s == 0 ? 1.0f : 1.0007f);
                const float w = kTwoPi * f / (float) sr;
                pc[np] = 1.0f; ps[np] = 0.0f;
                pcw[np] = std::cos (w); psw[np] = std::sin (w);
                pa[np] = amp * 0.5f;
                pd[np] = std::pow (0.001f, 1.0f / (T60 * (float) sr));
                ++np;
            }
        }
        hammer.set (sr, juce::jlimit (800.0f, 5000.0f, freq * 3.0f), 1.2f);
        hammer.reset();
        hammerEnv = 1.0f;
        hammerDecay = std::exp (-1.0f / (0.006f * (float) sr));
    }

    void piano (float& l, float& r)
    {
        float s = 0;
        for (int k = 0; k < np; ++k)
        {
            const float ns = ps[k] * pcw[k] + pc[k] * psw[k];
            const float nc = pc[k] * pcw[k] - ps[k] * psw[k];
            ps[k] = ns; pc[k] = nc;
            s += ns * pa[k];
            pa[k] *= pd[k];
        }
        if ((t & 1023) == 0)
            for (int k = 0; k < np; ++k)
            {
                const float m = 1.0f / std::sqrt (pc[k] * pc[k] + ps[k] * ps[k] + 1.0e-12f);
                pc[k] *= m; ps[k] *= m;
            }
        if (hammerEnv > 0.0001f)
        {
            s += hammer.bpn (noise.tick()) * hammerEnv * 0.5f * vel;
            hammerEnv *= hammerDecay;
        }
        s *= atk (0.002f);
        track (s);
        l = r = s;
    }

    // ---------------- RHODES: 2-op FM + tine ping + gentle autopan
    float cph = 0, mph = 0, tph = 0, idx = 0, idxDecay = 0, amp = 0, ampD = 0, tineAmp = 0, tineD = 0, trem = 0;

    void startRhodes()
    {
        cph = mph = tph = 0;
        idx = 1.2f + 2.2f * vel;
        idxDecay = std::exp (-1.0f / (0.35f * (float) sr));
        amp = 1.0f;
        ampD = std::pow (0.001f, 1.0f / ((5.0f * std::pow (261.6f / freq, 0.4f)) * (float) sr));
        tineAmp = 0.25f * vel;
        tineD = std::exp (-1.0f / (0.05f * (float) sr));
    }

    void rhodes (float& l, float& r)
    {
        const float w = freq / (float) sr;
        mph += w;          mph -= std::floor (mph);
        cph += w;          cph -= std::floor (cph);
        tph += w * 14.0f;  tph -= std::floor (tph);
        const float m = std::sin (kTwoPi * mph);
        float s = std::sin (kTwoPi * cph + (idx * 0.85f + 0.15f) * m);
        s += std::sin (kTwoPi * tph) * tineAmp;
        idx *= idxDecay; tineAmp *= tineD; amp *= ampD;
        s *= amp * atk (0.002f);
        trem += 4.6f / (float) sr; trem -= std::floor (trem);
        const float pan = 0.18f * std::sin (kTwoPi * trem);
        track (s);
        l = s * (1.0f - pan);
        r = s * (1.0f + pan);
    }

    // ---------------- NYLON: Karplus-Strong with pluck-position comb
    std::vector<float> ks;
    int ksw = 0;
    float ksDelay = 100, ksLoss = 0.99f, ksPrev = 0;
    Svf body;

    void startNylon()
    {
        std::fill (ks.begin(), ks.end(), 0.0f);
        ksPrev = 0;
        ksDelay = juce::jlimit (2.0f, 8000.0f, (float) sr / freq - 0.5f);
        const float T60 = 3.0f * std::pow (261.6f / freq, 0.6f);
        ksLoss = std::pow (0.001f, 1.0f / (T60 * freq));
        const int L = juce::jlimit (2, 8000, (int) ksDelay);
        float lp = 0;
        const float a = 0.25f + 0.6f * vel;
        for (int i = 0; i < L; ++i) { lp += a * (noise.tick() - lp); ks[(size_t) i] = lp; }
        const int P = juce::jmax (1, L / 5);
        for (int i = L - 1; i >= P; --i) ks[(size_t) i] -= ks[(size_t) (i - P)];
        ksw = L;
        body.set (sr, 3500.0f + 3000.0f * vel, 0.7f);
        body.reset();
    }

    void nylon (float& l, float& r)
    {
        const int N = (int) ks.size();
        float rp = (float) ksw - ksDelay;
        while (rp < 0) rp += (float) N;
        const int i0 = (int) rp % N;
        const float fr = rp - std::floor (rp);
        const int i1 = (i0 + 1) % N;
        const float y = ks[(size_t) i0] + fr * (ks[(size_t) i1] - ks[(size_t) i0]);
        const float o = ksLoss * 0.5f * (y + ksPrev);
        ksPrev = y;
        ks[(size_t) ksw] = o;
        ksw = (ksw + 1) % N;
        const float s = body.lp (y) * atk (0.001f);
        track (s);
        l = r = s;
    }

    // ---------------- STRINGS / CHOIR: detuned saws + filters
    Saw saws[4];
    float detune[4] { 1, 1, 1, 1 };
    Svf f1, f2, f3, hpf, sideLp;
    float vibPh = 0;
    OnePole env;

    void startPad()
    {
        const float detS[4] = { -7.0f, 0.0f, 7.0f, 0.0f };
        const float detC[4] = { -12.0f, -4.0f, 5.0f, 11.0f };
        for (int k = 0; k < 4; ++k)
        {
            const float c = engine == 3 ? detS[k] : detC[k];
            detune[k] = std::pow (2.0f, c / 1200.0f);
            saws[k].ph = noise.uni();
        }
        if (engine == 3)
        {
            f1.set (sr, juce::jmin (9000.0f, freq * 3.0f + 1600.0f), 0.7f);
            sideLp.set (sr, juce::jmin (9000.0f, freq * 3.0f + 1600.0f), 0.7f);
            hpf.set (sr, 120.0f, 0.7f);
        }
        else
        {
            f1.set (sr, 750.0f, 6.0f);
            f2.set (sr, 1150.0f, 8.0f);
            f3.set (sr, 2800.0f, 10.0f);
            hpf.set (sr, 150.0f, 0.7f);
        }
        f1.reset(); f2.reset(); f3.reset(); hpf.reset(); sideLp.reset();
        env.setTau (sr, engine == 3 ? 0.12f : 0.16f);
        env.z = 0;
        vibPh = 0;
    }

    float vib()
    {
        vibPh += (engine == 3 ? 5.2f : 5.6f) / (float) sr;
        vibPh -= std::floor (vibPh);
        const float depth = juce::jmin (1.0f, (float) t / (float) (0.5 * sr)) * (engine == 3 ? 0.0025f : 0.0035f);
        return 1.0f + depth * std::sin (kTwoPi * vibPh);
    }

    void strings (float& l, float& r)
    {
        const float v = vib();
        float a[3];
        for (int k = 0; k < 3; ++k) { saws[k].dt = freq * detune[k] * v / (float) sr; a[k] = saws[k].tick(); }
        const float e = env.tick (1.0f);
        const float m = hpf.hp (f1.lp (a[0] + a[1] + a[2])) * e;
        const float s = sideLp.lp (a[0] - a[2]) * 0.35f * e;
        track (m);
        l = m + s;
        r = m - s;
    }

    void choir (float& l, float& r)
    {
        const float v = vib();
        float x = 0;
        for (int k = 0; k < 4; ++k) { saws[k].dt = freq * detune[k] * v / (float) sr; x += saws[k].tick(); }
        x *= 0.25f;
        const float br = noise.tick() * 0.04f;
        float y = f1.bpn (x + br) + 0.55f * f2.bpn (x + br) + 0.25f * f3.bpn (x + br);
        y = hpf.hp (y) * env.tick (1.0f);
        track (y);
        l = r = y;
    }

    // ---------------- SAMPLER: real recorded instruments (VCSL, CC0)
    SampleSetPtr set;
    const Zone* zone = nullptr;
    double spos = 0, sinc = 1;
    bool sampleEnded = false;

    void startSampler (int note, float sink)
    {
        sampleEnded = false;
        zone = nullptr;
        set = source != nullptr ? source->currentSet (engine) : nullptr;
        if (set == nullptr || set->zones.empty()) return;
        const float target = (float) note - sink;
        size_t best = 0;
        float bestD = 1.0e9f;
        for (size_t i = 0; i < set->zones.size(); ++i)
        {
            const float d = std::abs (set->zones[i].root - target);
            if (d < bestD) { bestD = d; best = i; }
        }
        zone = &set->zones[best];
        spos = 0.0;
        sinc = std::pow (2.0, (double) (target - zone->root) / 12.0) * zone->sr / sr;
        relCoef = std::exp (-1.0f / (juce::jmax (0.02f, set->release) * (float) sr));
    }

    void sampler (float& l, float& r)
    {
        l = r = 0.0f;
        if (zone == nullptr) { sampleEnded = true; return; }
        const int n = zone->audio.getNumSamples();
        const int i0 = (int) spos;
        if (i0 + 1 >= n) { sampleEnded = true; return; }
        const float fr = (float) (spos - (double) i0);
        const float* a = zone->audio.getReadPointer (0);
        const float* b = zone->audio.getReadPointer (zone->audio.getNumChannels() > 1 ? 1 : 0);
        const float e = atk (0.002f);
        l = (a[i0] + fr * (a[i0 + 1] - a[i0])) * e;
        r = (b[i0] + fr * (b[i0 + 1] - b[i0])) * e;
        spos += sinc;
        track (0.5f * (std::abs (l) + std::abs (r)));
    }
};
} // namespace gd
