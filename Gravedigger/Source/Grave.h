#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include "Dsp.h"

// THE GRAVE: everything that makes your melody sound like a dug-up record.
namespace gd
{
struct TombProfile { float lp, hp, crackle, hiss, wowDepth, wowRate, sat, room, damp, width; };

// 0 SOUL '72   1 BASEMENT LO-FI   2 HAUNTED REEL '64   3 DETROIT DARK
static const TombProfile kTombs[4] =
{
    { 6500.0f,  90.0f, 1.0f, 1.0f, 1.0f, 0.55f, 1.0f, 0.55f, 0.55f, 0.70f },
    { 4200.0f, 140.0f, 1.6f, 1.3f, 1.4f, 0.80f, 1.3f, 0.35f, 0.70f, 0.50f },
    { 5200.0f, 260.0f, 0.7f, 1.9f, 1.8f, 0.35f, 0.9f, 0.88f, 0.30f, 0.60f },
    { 3300.0f,  60.0f, 0.8f, 0.8f, 0.9f, 0.50f, 1.8f, 0.62f, 0.60f, 0.80f },
};

struct GraveParams
{
    float years = 52, rot = 0.38f, drown = 0.22f, mold = 0.55f, ghost = 0.45f, bleed = 0.41f;
    int tomb = 0;
};

// Shared maths so the UI can show the same numbers the DSP uses
inline float graveScale (float years)            { return 0.15f + 1.7f * juce::jlimit (0.0f, 1.0f, years / 80.0f); }
inline float graveEff (float knob, float years)  { return juce::jlimit (0.0f, 1.6f, knob * graveScale (years)); }
inline float moldCutoff (float mold, float years, int tomb)
{
    const float m = graveEff (mold, years);
    return juce::jlimit (900.0f, 20000.0f, 20000.0f * std::pow (kTombs[juce::jlimit (0, 3, tomb)].lp / 20000.0f, m));
}

class Grave
{
public:
    void prepare (double newSr)
    {
        sr = newSr;
        dlSize = 1;
        while (dlSize < (int) (sr * 0.06)) dlSize <<= 1;
        for (auto& d : dl) d.assign ((size_t) dlSize, 0.0f);
        w = 0;
        reverb.setSampleRate (sr);
        reverb.reset();
        for (int c = 0; c < 2; ++c) { lp[c].reset(); lp2[c].reset(); hp[c].reset(); }
        gateEnv = 0; gateHold = 0; lpCur = 20000; hpCur = 20;
        wowPh = flPh = 0; driftMs = driftTarget = 0; driftCount = 0;
        crackAmp[0] = crackAmp[1] = 0;
    }

    void process (float* L, float* R, int n, const GraveParams& p)
    {
        const auto& tp = kTombs[juce::jlimit (0, 3, p.tomb)];
        const float age = juce::jlimit (0.0f, 1.0f, p.years / 80.0f);

        // BLEED: saturation
        const float bl = graveEff (p.bleed, p.years) * tp.sat;
        const float drive = 1.0f + 5.0f * bl;
        const float mk = 1.0f / std::pow (drive, 0.65f);
        const float bias = 0.08f * bl;
        const float biasT = std::tanh (bias);

        // MOLD: band limiting
        const float m = graveEff (p.mold, p.years);
        const float lpT = moldCutoff (p.mold, p.years, p.tomb);
        const float hpT = 15.0f + tp.hp * m;
        lpCur += (lpT - lpCur) * 0.25f;
        hpCur += (hpT - hpCur) * 0.25f;
        for (int c = 0; c < 2; ++c)
        {
            lp[c].set (sr, lpCur, 0.6f);
            lp2[c].set (sr, juce::jmin (20000.0f, lpCur * 1.1f), 0.55f);
            hp[c].set (sr, hpCur, 0.6f);
        }

        // DROWN: wow & flutter
        const float d = graveEff (p.drown, p.years);
        const float wowMs = 3.5f * d * tp.wowDepth;
        const float flMs  = 0.22f * d;

        // ROT: crackle & hiss
        const float r = graveEff (p.rot, p.years);
        const float crackP = (0.00003f + 0.0008f * r * tp.crackle) * (44100.0f / (float) sr);
        const float hissA = 0.006f * r * tp.hiss;

        // GHOST: room
        const float gh = juce::jlimit (0.0f, 1.2f, graveEff (p.ghost, p.years));
        juce::Reverb::Parameters rp;
        rp.roomSize = juce::jlimit (0.0f, 0.98f, tp.room * (0.55f + 0.45f * juce::jmin (1.0f, gh)));
        rp.damping  = tp.damp;
        rp.wetLevel = 0.42f * gh;
        rp.dryLevel = 1.0f - 0.3f * juce::jmin (1.0f, gh);
        rp.width    = 0.9f;
        rp.freezeMode = 0.0f;
        reverb.setParameters (rp);

        const float width = 1.0f - (1.0f - tp.width) * juce::jmin (1.0f, age * 1.25f);
        const int holdLen = (int) (sr * 1.5);
        const float msToS = (float) sr / 1000.0f;
        const int mask = dlSize - 1;

        for (int i = 0; i < n; ++i)
        {
            float l = L[i], rr = R[i];

            // keep the record "spinning" only while something plays
            const float a = juce::jmax (std::abs (l), std::abs (rr));
            if (a > 0.0005f) gateHold = holdLen;
            const float gt = gateHold > 0 ? 1.0f : 0.0f;
            if (gateHold > 0) --gateHold;
            gateEnv += (gt - gateEnv) * 0.0005f;

            if (bl > 0.001f)
            {
                l  = (std::tanh (drive * l  + bias) - biasT) * mk;
                rr = (std::tanh (drive * rr + bias) - biasT) * mk;
            }

            if (gateEnv > 0.001f && r > 0.0005f)
            {
                l  += noise.tick() * hissA * gateEnv;
                rr += noise.tick() * hissA * gateEnv;
                if (noise.uni() < crackP)
                {
                    const float u = noise.uni();
                    const float amp = (0.15f + 0.85f * u * u * u) * 0.35f * juce::jmin (1.0f, r * 1.5f);
                    const float pan = noise.uni();
                    const float sgn = noise.tick() > 0 ? 1.0f : -1.0f;
                    crackAmp[0] += amp * sgn * (1.0f - pan);
                    crackAmp[1] += amp * sgn * pan;
                }
                l  += crackAmp[0] * gateEnv;
                rr += crackAmp[1] * gateEnv;
            }
            crackAmp[0] *= -0.55f;
            crackAmp[1] *= -0.55f;

            l  = hp[0].hp (lp2[0].lp (lp[0].lp (l)));
            rr = hp[1].hp (lp2[1].lp (lp[1].lp (rr)));

            // tape transport
            wowPh += tp.wowRate / (float) sr;  wowPh -= std::floor (wowPh);
            flPh  += 7.3f / (float) sr;        flPh  -= std::floor (flPh);
            if (--driftCount <= 0)
            {
                driftCount = (int) (sr * 0.5);
                driftTarget = noise.uni() * 0.8f * d * tp.wowDepth;
            }
            driftMs += (driftTarget - driftMs) * 0.00005f;
            const float modMs = 1.0f + wowMs * (0.5f + 0.5f * std::sin (kTwoPi * wowPh))
                                     + flMs * (0.5f + 0.5f * std::sin (kTwoPi * flPh)) + driftMs;

            dl[0][(size_t) w] = l;
            dl[1][(size_t) w] = rr;
            float rp2 = (float) w - modMs * msToS;
            while (rp2 < 0) rp2 += (float) dlSize;
            const int i0 = ((int) rp2) & mask;
            const int i1 = (i0 + 1) & mask;
            const float fr = rp2 - std::floor (rp2);
            L[i] = dl[0][(size_t) i0] + fr * (dl[0][(size_t) i1] - dl[0][(size_t) i0]);
            R[i] = dl[1][(size_t) i0] + fr * (dl[1][(size_t) i1] - dl[1][(size_t) i0]);
            w = (w + 1) & mask;
        }

        if (gh > 0.01f) reverb.processStereo (L, R, n);

        for (int i = 0; i < n; ++i)
        {
            const float mid  = 0.5f * (L[i] + R[i]);
            const float side = 0.5f * (L[i] - R[i]) * width;
            L[i] = std::tanh (mid + side);
            R[i] = std::tanh (mid - side);
        }
    }

private:
    double sr = 44100.0;
    std::vector<float> dl[2];
    int dlSize = 4096, w = 0;
    Svf lp[2], lp2[2], hp[2];
    float lpCur = 20000, hpCur = 20;
    juce::Reverb reverb;
    Noise noise;
    float gateEnv = 0; int gateHold = 0;
    float wowPh = 0, flPh = 0, driftMs = 0, driftTarget = 0; int driftCount = 0;
    float crackAmp[2] { 0, 0 };
};
} // namespace gd
