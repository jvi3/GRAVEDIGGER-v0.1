#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <cstdint>

namespace gd
{
constexpr float kPi    = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

// Topology-preserving state variable filter (Cytomic / Zavalishin)
struct Svf
{
    float ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1;

    void set (double sr, float fc, float q)
    {
        fc = juce::jlimit (10.0f, (float) (sr * 0.45), fc);
        const float g = std::tan (kPi * fc / (float) sr);
        k  = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0; }

    inline void tick (float v0, float& lp, float& bp, float& hp)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = v0 - k * v1 - v2;
    }
    inline float lp (float x) { float l, b, h; tick (x, l, b, h); return l; }
    inline float hp (float x) { float l, b, h; tick (x, l, b, h); return h; }
    // unity-peak band pass
    inline float bpn (float x) { float l, b, h; tick (x, l, b, h); return b * k; }
};

inline float polyBlep (float t, float dt)
{
    if (t < dt)        { t /= dt;               return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt;   return t * t + t + t + 1.0f; }
    return 0.0f;
}

struct Saw
{
    float ph = 0, dt = 0;
    inline float tick()
    {
        const float v = 2.0f * ph - 1.0f - polyBlep (ph, dt);
        ph += dt;
        while (ph >= 1.0f) ph -= 1.0f;
        return v;
    }
};

struct OnePole
{
    float z = 0, a = 0;
    void setTau (double sr, float sec) { a = std::exp (-1.0f / (float) (sec * sr)); }
    inline float tick (float x) { z = x + a * (z - x); return z; }
};

struct Noise
{
    uint32_t s = 22222u;
    inline float tick()  { s = s * 1664525u + 1013904223u; return (float) (int32_t) s * (1.0f / 2147483648.0f); }
    inline float uni()   { return 0.5f * (tick() + 1.0f); }
};
} // namespace gd
