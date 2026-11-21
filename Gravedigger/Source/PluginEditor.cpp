#include "PluginEditor.h"
#include "BinaryData.h"

namespace gd
{
using namespace juce;

// ================================================================== fonts & glyph helpers
namespace
{
    Typeface::Ptr loadFace (const char* data, int size)
    {
        if (data == nullptr || size < 1000) return nullptr;
        return Typeface::createSystemTypefaceFor (data, (size_t) size);
    }
    String U (juce_wchar c) { return String::charToString (c); }
    String DOT()   { return U ((juce_wchar) 0x00B7); }
    String DASH()  { return U ((juce_wchar) 0x2014); }
    String MINUS() { return U ((juce_wchar) 0x2212); }

    Point<float> pol (float cx, float cy, float r, float a) { return { cx + r * std::sin (a), cy - r * std::cos (a) }; }

    float textW (const Font& f, const String& s) { return GlyphArrangement::getStringWidth (f, s); }

    String flNoteName (int midi)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return String (names[midi % 12]) + String (midi / 12);   // FL Studio: MIDI 60 = C5
    }
}

Font fraktur (float h)
{
    static Typeface::Ptr tf = loadFace (BinaryData::Fraktur_ttf, BinaryData::Fraktur_ttfSize);
    if (tf != nullptr) return Font (FontOptions (tf).withHeight (h));
    return Font (FontOptions ("Times New Roman", h, Font::bold));
}
Font oswald (float h)
{
    static Typeface::Ptr tf = loadFace (BinaryData::Oswald_ttf, BinaryData::Oswald_ttfSize);
    if (tf != nullptr) return Font (FontOptions (tf).withHeight (h));
    return Font (FontOptions (h, Font::bold));
}
Font mono (float h)
{
    static Typeface::Ptr tf = loadFace (BinaryData::PlexMono_ttf, BinaryData::PlexMono_ttfSize);
    if (tf != nullptr) return Font (FontOptions (tf).withHeight (h));
    return Font (FontOptions (Font::getDefaultMonospacedFontName(), h, Font::plain));
}

// ================================================================== look and feel
void GdLook::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, Slider& s)
{
    const bool big = (bool) s.getProperties()["big"];
    auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float cx = b.getCentreX(), cy = b.getCentreY();
    const float r = jmin (b.getWidth(), b.getHeight()) * 0.5f - 3.0f;
    const float th = big ? 4.0f : 3.0f;
    const float av = a0 + pos * (a1 - a0);

    Path track; track.addCentredArc (cx, cy, r, r, 0.0f, a0, a1, true);
    g.setColour (col::line);
    g.strokePath (track, PathStrokeType (th, PathStrokeType::curved, PathStrokeType::rounded));
    Path val; val.addCentredArc (cx, cy, r, r, 0.0f, a0, av, true);
    g.setColour (col::red);
    g.strokePath (val, PathStrokeType (th, PathStrokeType::curved, PathStrokeType::rounded));

    if (big)
        for (int k = 0; k <= 40; ++k)
        {
            const float a = a0 + (a1 - a0) * (float) k / 40.0f;
            const float len = (k % 10 == 0) ? 7.0f : 3.0f;
            g.setColour (k % 10 == 0 ? col::muted : Colour (0xff5a524a));
            g.drawLine (Line<float> (pol (cx, cy, r - 9.0f, a), pol (cx, cy, r - 9.0f - len, a)), 1.0f);
        }

    const float ri = r - (big ? 22.0f : 8.0f);
    g.setColour (Colour (0xff1B1816));
    g.fillEllipse (cx - ri, cy - ri, ri * 2, ri * 2);
    g.setColour (Colour (0xff332e29));
    g.drawEllipse (cx - ri, cy - ri, ri * 2, ri * 2, 1.0f);
    g.setColour (col::bone);
    g.drawLine (Line<float> (pol (cx, cy, ri * 0.35f, av), pol (cx, cy, ri - 3.0f, av)), big ? 2.4f : 2.0f);
}

// ================================================================== knob
Knob::Knob (AudioProcessorValueTreeState& s, const String& id, String nm, String pl,
            std::function<String (float)> fmt, bool isBig)
    : raw (s.getRawParameterValue (id)), name (nm), plain (pl), format (fmt), big (isBig)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.getProperties().set ("big", big);
    slider.setMouseDragSensitivity (big ? 300 : 220);
    addAndMakeVisible (slider);
    att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
    if (auto* p = s.getParameter (id))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

void Knob::resized()
{
    if (big) slider.setBounds (0, 0, getHeight(), getHeight());
    else     slider.setBounds ((getWidth() - 50) / 2, 0, 50, 50);
}

void Knob::paint (Graphics& g)
{
    const String v = format ? format (raw->load()) : String (raw->load(), 2);
    if (big)
    {
        const float x = (float) getHeight() + 14.0f;
        g.setColour (col::muted);
        g.setFont (oswald (13.0f).withExtraKerningFactor (0.15f));
        g.drawText (name, Rectangle<float> (x, 6, 160, 18), Justification::centredLeft);
        const auto bigF = oswald (40.0f);
        g.setFont (bigF);
        g.setColour (col::bone);
        g.drawText (v, Rectangle<float> (x, 24, 120, 44), Justification::centredLeft);
        g.setColour (col::redTxt);
        g.setFont (oswald (16.0f));
        g.drawText ("yrs", Rectangle<float> (x + textW (bigF, v) + 6.0f, 40, 40, 22), Justification::centredLeft);
        g.setColour (col::muted);
        g.setFont (mono (10.0f));
        g.drawText (plain, Rectangle<float> (x, 70, 170, 14), Justification::centredLeft);
        g.drawText ("fine-tune below.", Rectangle<float> (x, 84, 170, 14), Justification::centredLeft);
        return;
    }
    const float w = (float) getWidth();
    g.setColour (col::bone);
    g.setFont (oswald (14.0f).withExtraKerningFactor (0.1f));
    g.drawText (name, Rectangle<float> (0, 52, w, 18), Justification::centred);
    g.setColour (col::muted);
    g.setFont (mono (10.0f));
    g.drawText (plain, Rectangle<float> (0, 69, w, 13), Justification::centred);
    g.setColour (col::redTxt);
    g.drawText (v, Rectangle<float> (0, 81, w, 13), Justification::centred);
}

// ================================================================== option strip
OptionStrip::OptionStrip (GdProcessor& p, const String& id, StringArray l, Style st, StringArray d)
    : proc (p), paramId (id), raw (p.apvts.getRawParameterValue (id)), labels (l), descs (d), style (st) {}

Rectangle<float> OptionStrip::cell (int i) const
{
    const float W = (float) getWidth(), H = (float) getHeight();
    const int n = jmax (1, labels.size());
    switch (style)
    {
        case Segmented: return { W / (float) n * (float) i, 0.0f, W / (float) n, H };
        case Grid2:
        {
            const float cw = (W - 6.0f) * 0.5f;
            return { (float) (i % 2) * (cw + 6.0f), (float) (i / 2) * 36.0f, cw, 30.0f };
        }
        default: return { 0.0f, (float) i * 50.0f, W, 44.0f };
    }
}

void OptionStrip::paint (Graphics& g)
{
    const int cur = current();
    if (style == Segmented)
    {
        g.setColour (col::line);
        g.drawRect (getLocalBounds().toFloat(), 1.0f);
        for (int i = 0; i < labels.size(); ++i)
        {
            auto c = cell (i);
            if (i == cur) { g.setColour (col::sel); g.fillRect (c.reduced (1.0f)); }
            g.setColour (i == cur ? col::bone : col::muted);
            g.setFont (mono (11.0f));
            g.drawText (labels[i], c, Justification::centred);
        }
        return;
    }
    for (int i = 0; i < labels.size(); ++i)
    {
        auto c = cell (i);
        const bool on = i == cur;
        g.setColour (on ? col::deep : col::card);
        g.fillRect (c);
        g.setColour (on ? col::red : col::line);
        g.drawRect (c, 1.0f);
        if (style == Grid2)
        {
            g.setColour (on ? col::bone : col::muted);
            g.setFont (mono (10.5f));
            g.drawText (labels[i], c.reduced (8.0f, 0.0f), Justification::centredLeft);
            continue;
        }
        // body list
        auto nb = Rectangle<float> (c.getX() + 10.0f, c.getCentreY() - 8.0f, 22.0f, 16.0f);
        g.setFont (mono (10.0f));
        if (on) { g.setColour (col::red); g.fillRect (nb); g.setColour (col::bg); }
        else    { g.setColour (Colour (0xff3a342f)); g.drawRect (nb, 1.0f); g.setColour (col::muted); }
        g.drawText (String (i + 1).paddedLeft ('0', 2), nb, Justification::centred);
        g.setColour (on ? col::bone : Colour (0xffCFC6B2));
        g.setFont (oswald (15.0f).withExtraKerningFactor (0.12f));
        g.drawText (labels[i], Rectangle<float> (c.getX() + 42.0f, c.getY() + 5.0f, c.getWidth() - 50.0f, 19.0f), Justification::centredLeft);
        g.setColour (on ? col::redTxt : col::muted);
        g.setFont (mono (10.0f));
        g.drawText (descs[i], Rectangle<float> (c.getX() + 42.0f, c.getY() + 24.0f, c.getWidth() - 50.0f, 14.0f), Justification::centredLeft);
    }
}

void OptionStrip::mouseDown (const MouseEvent& e)
{
    for (int i = 0; i < labels.size(); ++i)
        if (cell (i).contains (e.position))
        {
            proc.setChoice (paramId, i);
            repaint();
            return;
        }
}

// ================================================================== record + eye
RecordView::RecordView (GdProcessor& p) : proc (p)
{
    Random vr (13);
    for (int k = 0; k < 24; ++k)
    {
        float rr = RL - 1.0f, aa = vr.nextFloat() * kTwoPi;
        Path v;
        v.startNewSubPath (pol (C, C, rr, aa));
        const int steps = 4 + vr.nextInt (4);
        Point<float> mid;
        for (int s = 0; s < steps; ++s)
        {
            rr -= 3.0f + vr.nextFloat() * 3.0f;
            aa += (vr.nextFloat() - 0.5f) * 0.3f;
            if (rr < 33.0f) break;
            v.lineTo (pol (C, C, rr, aa));
            if (s == 1) mid = pol (C, C, rr, aa);
        }
        veins.push_back ({ v, 0.4f + vr.nextFloat() * 0.9f });
        if (vr.nextFloat() < 0.5f && mid.x != 0.0f)
        {
            Path br;
            br.startNewSubPath (mid);
            br.lineTo (pol (C, C, RL - 10.0f - vr.nextFloat() * 6.0f, aa + (vr.nextFloat() - 0.5f) * 0.8f));
            veins.push_back ({ br, 0.4f });
        }
    }
    for (int k = 0; k < 48; ++k)
    {
        const float a = (float) k * kTwoPi / 48.0f;
        streaks.startNewSubPath (pol (0, 0, 13.0f, a));
        streaks.lineTo (pol (0, 0, 28.0f, a + (vr.nextFloat() - 0.5f) * 0.1f));
    }
    buildImage();
}

void RecordView::setBank (const BankView& v)
{
    numBones = v.numBones;
    fracs = v.fracs;
    env = v.env;
    buildImage();
    repaint();
}

Path RecordView::sectorPath (int i, float r0, float r1) const
{
    const float gap = 0.042f;
    float a0 = (float) fracs[(size_t) i] * kTwoPi + gap * 0.5f;
    float a1 = (float) fracs[(size_t) i + 1] * kTwoPi - gap * 0.5f;
    if (a1 < a0 + 0.01f) a1 = a0 + 0.01f;
    Path p;
    p.addCentredArc (C, C, r1, r1, 0.0f, a0, a1, true);
    p.addCentredArc (C, C, r0, r0, 0.0f, a1, a0, false);
    p.closeSubPath();
    return p;
}

void RecordView::buildImage()
{
    img = Image (Image::ARGB, 720, 720, true);
    Graphics g (img);
    g.addTransform (AffineTransform::scale (2.0f));
    const bool cut = numBones > 0 && (int) fracs.size() > numBones;

    g.setColour (cut ? Colour (0xff3a0a07) : Colour (0xff0d0b0a));
    g.fillEllipse (C - R - 2, C - R - 2, (R + 2) * 2, (R + 2) * 2);

    Random gr (13);
    Path all;
    const int count = cut ? numBones : 1;
    for (int i = 0; i < count; ++i)
    {
        float a0 = 0.0f, a1 = kTwoPi;
        Path sec;
        if (cut)
        {
            sec = sectorPath (i, RL + 2.0f, R);
            a0 = (float) fracs[(size_t) i] * kTwoPi + 0.021f;
            a1 = (float) fracs[(size_t) i + 1] * kTwoPi - 0.021f;
        }
        else
        {
            sec.addEllipse (C - R, C - R, R * 2, R * 2);
            sec.addEllipse (C - RL - 2, C - RL - 2, (RL + 2) * 2, (RL + 2) * 2);
            sec.setUsingNonZeroWinding (false);
        }
        all.addPath (sec);
        g.setColour (Colour (0xff100E0D));
        g.fillPath (sec);
        if (a1 <= a0) continue;
        for (float r = 70.0f; r < R - 3.0f; r += 3.4f + gr.nextFloat() * 1.2f)
        {
            Path arc;
            arc.addCentredArc (C, C, r, r, 0.0f, a0 + 0.006f, a1 - 0.006f, true);
            g.setColour (((int) r) % 3 != 0 ? Colour (0xff2f2a26) : Colour (0xff3a342f));
            g.strokePath (arc, PathStrokeType (0.7f));
        }
    }

    if (cut && (int) env.size() == kEnvBins)
    {
        g.saveState();
        g.reduceClipRegion (all);
        for (int k = 0; k < kEnvBins; ++k)
        {
            const float a = (float) k / (float) kEnvBins * kTwoPi;
            g.setColour (col::red.withAlpha (0.03f + 0.32f * env[(size_t) k]));
            g.drawLine (Line<float> (pol (C, C, RL + 12.0f, a), pol (C, C, R - 6.0f, a)), 0.8f);
        }
        g.restoreState();

        g.setFont (mono (9.0f));
        for (int i = 0; i < numBones; ++i)
        {
            const float a0 = (float) fracs[(size_t) i] * kTwoPi, a1 = (float) fracs[(size_t) i + 1] * kTwoPi;
            if (a1 - a0 < 0.12f) continue;
            const auto p = pol (C, C, R - 16.0f, (a0 + a1) * 0.5f);
            g.setColour (col::muted);
            g.drawText (String (i + 1).paddedLeft ('0', 2), Rectangle<float> (p.x - 10, p.y - 6, 20, 12), Justification::centred);
        }
    }

    // label ring with text
    g.setColour (col::bg);
    g.fillEllipse (C - RL - 8, C - RL - 8, (RL + 8) * 2, (RL + 8) * 2);
    const String ring = "GRAVEDIGGER RECORDS " + DOT() + " PRESSED FROM YOUR OWN HANDS " + DOT() + " NO CLEARANCE "
                        + DOT() + " SIDE B " + DOT() + " DO NOT PLAY AFTER MIDNIGHT " + DOT() + " ";
    const auto f = mono (6.4f);
    g.setFont (f);
    g.setColour (col::muted);
    float a = 0.0f;
    const float rr = 63.0f;
    for (int i = 0; i < ring.length(); ++i)
    {
        const String ch = String::charToString (ring[i]);
        const float cw = textW (f, ch) + 1.6f;
        const float da = cw / rr;
        if (a + da > kTwoPi - 0.02f) break;
        g.saveState();
        g.addTransform (AffineTransform::rotation (a + da * 0.5f, C, C));
        g.drawText (ch, Rectangle<float> (C - cw, C - rr - 4.0f, cw * 2.0f, 8.0f), Justification::centred);
        g.restoreState();
        a += da;
    }
}

void RecordView::startBlink (bool allowDouble)
{
    blinkT = 0.0f;
    sinceBlink = 0.0f;
    doublePending = allowDouble && rnd.nextFloat() < 0.18f;
}

void RecordView::lookAtNote (int note)
{
    gazeTarget = { jlimit (-10.0f, 10.0f, (float) (note - 60) / 24.0f * 10.0f), (rnd.nextFloat() - 0.5f) * 6.0f };
    saccadeTimer = 1.5f;
}

void RecordView::lookAtBone (int bone)
{
    if (bone < 0 || bone >= numBones || (int) fracs.size() <= bone + 1) return;
    const float mid = (float) (fracs[(size_t) bone] + fracs[(size_t) bone + 1]) * kPi + angle;
    gazeTarget = { std::sin (mid) * 10.0f, -std::cos (mid) * 8.0f };
    saccadeTimer = 1.2f;
}

void RecordView::tick (float dt, bool spinning, bool playing, double ppq, float lvl, int bone, float fl, float yrs)
{
    t += dt;
    sinceBlink += dt;
    years = yrs;
    activeBone = bone;
    flash = fl;
    if (spinning) { angle += dt * 0.5f * kTwoPi; if (angle > kTwoPi) angle -= kTwoPi; }
    level += (lvl - level) * (lvl > level ? 0.5f : 0.05f);

    // ---- human blink: fast close, short hold, slower open; on the beat while FL plays
    if (blinkT >= 0.0f)
    {
        blinkT += dt;
        if (blinkT > 0.27f)
        {
            blinkT = -1.0f;
            if (doublePending) { doublePending = false; blinkDelay = 0.07f; }
        }
    }
    if (blinkDelay > 0.0f)
    {
        blinkDelay -= dt;
        if (blinkDelay <= 0.0f) { blinkDelay = 0.0f; blinkT = 0.0f; sinceBlink = 0.0f; }
    }
    if (playing)
    {
        const long long beat = (long long) std::floor (ppq);
        if (beat != lastBeat)
        {
            const bool down = (beat % 4) == 0;
            lastBeat = beat;
            if (blinkT < 0.0f && blinkDelay <= 0.0f && sinceBlink > 1.1f && rnd.nextFloat() < (down ? 0.45f : 0.25f))
                startBlink (true);
        }
    }
    else
    {
        lastBeat = -1;
        idleTimer -= dt;
        if (idleTimer <= 0.0f)
        {
            if (blinkT < 0.0f) startBlink (true);
            idleTimer = 2.0f + rnd.nextFloat() * 3.5f;
        }
    }

    // ---- gaze: saccades
    saccadeTimer -= dt;
    if (saccadeTimer <= 0.0f)
    {
        gazeTarget = { (rnd.nextFloat() - 0.5f) * 10.0f, (rnd.nextFloat() - 0.5f) * 8.0f };
        saccadeTimer = 1.2f + rnd.nextFloat() * 2.8f;
    }
    gaze += (gazeTarget - gaze) * jmin (1.0f, dt * 22.0f);

    // ---- pupil breathes and opens with the sound
    const float target = 10.5f + 7.0f * jmin (1.0f, level * 1.6f) + 0.4f * std::sin (t * 2.1f);
    pupil += (target - pupil) * jmin (1.0f, dt * 12.0f);

    repaint();
}

void RecordView::drawEye (Graphics& g)
{
    float b = 0.0f;
    if (blinkT >= 0.0f)
    {
        if (blinkT < 0.07f)      { const float x = blinkT / 0.07f; b = x * x; }
        else if (blinkT < 0.11f) b = 1.0f;
        else                     { const float x = jmin (1.0f, (blinkT - 0.11f) / 0.16f); b = (1.0f - x) * (1.0f - x); }
    }

    Path clip; clip.addEllipse (C - RL, C - RL, RL * 2, RL * 2);
    g.saveState();
    g.reduceClipRegion (clip);

    ColourGradient sg (Colour (0xffEFE7D4), C, C, Colour (0xffB9A586), C + RL, C, true);
    sg.addColour (0.7, Colour (0xffE2D6BC));
    g.setGradientFill (sg);
    g.fillEllipse (C - RL, C - RL, RL * 2, RL * 2);

    const float va = jlimit (0.25f, 1.0f, 0.3f + years / 80.0f * 0.7f);
    g.setColour (Colour (0xff9E2A20).withAlpha (va * 0.85f));
    for (auto& v : veins)
        g.strokePath (v.first, PathStrokeType (v.second, PathStrokeType::curved, PathStrokeType::rounded));

    const Point<float> ic (C + 6.0f + gaze.x, C + 3.0f + gaze.y);
    ColourGradient ig (Colour (0xff1a0504), ic.x, ic.y, Colour (0xff3a0907), ic.x + 30.0f, ic.y, true);
    ig.addColour (0.55, Colour (0xff5c0f0a));
    ig.addColour (0.85, Colour (0xffb3261c));
    g.setGradientFill (ig);
    g.fillEllipse (ic.x - 30, ic.y - 30, 60, 60);
    g.setColour (Colour (0xffe0473a).withAlpha (0.4f));
    g.strokePath (streaks, PathStrokeType (0.35f), AffineTransform::translation (ic.x, ic.y));
    g.setColour (Colours::black);
    g.fillEllipse (ic.x - pupil, ic.y - pupil, pupil * 2, pupil * 2);
    g.setColour (Colours::white.withAlpha (0.85f));
    g.fillEllipse (ic.x - 9.2f, ic.y - 10.2f, 6.4f, 6.4f);

    // eyelids
    const float mid = C - RL * 0.98f + RL * 2.18f * b;
    const float side = mid - RL * 0.45f * (1.0f - b);
    Path up;
    up.startNewSubPath (C - RL - 4, C - RL - 4);
    up.lineTo (C + RL + 4, C - RL - 4);
    up.lineTo (C + RL + 4, side);
    up.quadraticTo (C, 2.0f * mid - side, C - RL - 4, side);
    up.closeSubPath();

    const float lmid = C + RL * 0.98f - RL * 0.38f * b;
    const float lside = lmid + RL * 0.35f * (1.0f - b);
    Path lo;
    lo.startNewSubPath (C - RL - 4, C + RL + 4);
    lo.lineTo (C + RL + 4, C + RL + 4);
    lo.lineTo (C + RL + 4, lside);
    lo.quadraticTo (C, 2.0f * lmid - lside, C - RL - 4, lside);
    lo.closeSubPath();

    Path upEdge; upEdge.startNewSubPath (C + RL + 4, side); upEdge.quadraticTo (C, 2.0f * mid - side, C - RL - 4, side);
    Path loEdge; loEdge.startNewSubPath (C + RL + 4, lside); loEdge.quadraticTo (C, 2.0f * lmid - lside, C - RL - 4, lside);

    g.setColour (Colours::black.withAlpha (0.3f));
    g.strokePath (upEdge, PathStrokeType (6.0f), AffineTransform::translation (0, 3));
    g.setGradientFill (ColourGradient (Colour (0xff1a0d0b), C, C - RL, Colour (0xff4a2620), C, jmax (mid, C - RL + 1.0f), false));
    g.fillPath (up);
    g.setGradientFill (ColourGradient (Colour (0xff1a0d0b), C, C + RL, Colour (0xff3e1f1a), C, jmin (lmid, C + RL - 1.0f), false));
    g.fillPath (lo);
    g.setColour (Colour (0xff070303));
    g.strokePath (upEdge, PathStrokeType (2.2f));
    g.strokePath (loEdge, PathStrokeType (1.4f));
    g.restoreState();

    g.setColour (Colours::black.withAlpha (0.55f));
    g.drawEllipse (C - RL, C - RL, RL * 2, RL * 2, 3.0f);
}

void RecordView::paint (Graphics& g)
{
    const auto rot = AffineTransform::rotation (angle, C, C);
    g.drawImageTransformed (img, AffineTransform::scale (0.5f).followedBy (rot));

    if (numBones > 0 && activeBone >= 0 && activeBone < numBones && (int) fracs.size() > activeBone + 1)
    {
        g.setColour (col::red.withAlpha (0.18f + 0.35f * flash));
        g.fillPath (sectorPath (activeBone, RL + 2.0f, R), rot);
        const float a0 = (float) fracs[(size_t) activeBone] * kTwoPi + 0.021f;
        const float a1 = (float) fracs[(size_t) activeBone + 1] * kTwoPi - 0.021f;
        Path rim; rim.addCentredArc (C, C, R, R, 0.0f, a0, jmax (a0 + 0.01f, a1), true);
        g.setColour (col::red);
        g.strokePath (rim, PathStrokeType (2.5f), rot);
    }

    const int st = proc.capState.load();
    if (st == capRecording)
    {
        Path pr;
        pr.addCentredArc (C, C, R + 4.0f, R + 4.0f, 0.0f, 0.0f, jmax (0.01f, proc.capProgress.load() * kTwoPi), true);
        g.setColour (col::red);
        g.strokePath (pr, PathStrokeType (3.0f));
    }
    else if (st == capArmed)
    {
        g.setColour (col::red.withAlpha (0.3f + 0.3f * std::sin (t * 6.0f)));
        g.drawEllipse (C - R - 4, C - R - 4, (R + 4) * 2, (R + 4) * 2, 2.0f);
    }

    drawEye (g);

    const auto tip = pol (C, C, R - 40.0f, 0.75f);
    g.setColour (col::bone.withAlpha (0.85f));
    g.drawLine (352.0f, 8.0f, tip.x, tip.y, 1.6f);
    g.setColour (col::red);
    g.fillEllipse (tip.x - 3, tip.y - 3, 6, 6);
}

// ================================================================== midi roll
void MidiRoll::push (uint64_t a, uint64_t b)
{
    cols.push_back ({ a, b });
    while (cols.size() > 220) cols.pop_front();
}

void MidiRoll::paint (Graphics& g)
{
    const float W = (float) getWidth(), H = (float) getHeight();
    for (int i = 0; i <= 8; ++i)
    {
        g.setColour (i % 4 == 0 ? Colour (0xff3a342f) : Colour (0xff221f1c));
        g.fillRect (W * (float) i / 8.0f, 0.0f, 1.0f, H);
    }
    int lo = 128, hi = -1;
    for (auto& c : cols)
        for (int n = 0; n < 128; ++n)
            if ((c[(size_t) (n >> 6)] >> (n & 63)) & 1u) { lo = jmin (lo, n); hi = jmax (hi, n); }
    if (hi < 0) { lo = 55; hi = 79; }
    if (hi - lo < 14) { const int m = (lo + hi) / 2; lo = m - 7; hi = m + 7; }
    lo -= 1; hi += 1;
    const float rowH = H / (float) (hi - lo + 1);
    const float colW = W / 220.0f;
    for (size_t i = 0; i < cols.size(); ++i)
    {
        const float x = W - (float) (cols.size() - i) * colW;
        for (int n = lo; n <= hi; ++n)
            if (n >= 0 && n < 128 && ((cols[i][(size_t) (n >> 6)] >> (n & 63)) & 1u))
            {
                g.setColour (col::bone.withAlpha (0.85f));
                g.fillRect (x, H - (float) (n - lo + 1) * rowH + 1.0f, colW + 0.6f, jmax (1.5f, rowH - 2.0f));
            }
    }
    g.setColour (col::redTxt);
    g.fillRect (W - 1.5f, 0.0f, 1.2f, H);
}

// ================================================================== bone pad
void BonePad::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (active ? col::deep : col::card);
    g.fillRect (b);
    if (flash > 0.01f) { g.setColour (col::red.withAlpha (0.45f * flash)); g.fillRect (b); }
    g.setColour (active ? col::red : col::line);
    g.drawRect (b, 1.0f);

    g.setFont (mono (10.0f));
    g.setColour (! exists ? col::muted.withAlpha (0.5f) : (active ? col::redTxt : col::bone));
    g.drawText (String (index + 1).paddedLeft ('0', 2), Rectangle<float> (7, 5, 30, 12), Justification::centredLeft);
    g.setColour (col::muted.withAlpha (exists ? 1.0f : 0.5f));
    g.drawText (flNoteName (kFirstNote + index), Rectangle<float> (b.getWidth() - 37, 5, 30, 12), Justification::centredRight);

    const float baseY = b.getHeight() - 17.0f, hh = 11.0f;
    const float bw = (b.getWidth() - 14.0f) / (float) kPeakBins;
    g.setColour (active ? col::redTxt : Colour (0xff8a8175));
    for (int k = 0; k < kPeakBins; ++k)
    {
        const float a = exists ? jmax (0.06f, peaks[(size_t) k]) * hh : 0.5f;
        g.fillRect (7.0f + (float) k * bw, baseY - a, jmax (1.0f, bw - 1.0f), a * 2.0f);
    }
}

void BonePad::mouseDown (const MouseEvent&)
{
    dragged = false;
    if (exists) proc.previewBone (index);
}

void BonePad::mouseDrag (const MouseEvent& e)
{
    if (! exists || dragged || e.getDistanceFromDragStart() < 6) return;
    dragged = true;
    const auto f = proc.exportBone (index);
    if (f.existsAsFile())
        DragAndDropContainer::performExternalDragDropOfFiles (StringArray (f.getFullPathName()), false, this);
}

// ================================================================== main view
static String pct (float v) { return String (roundToInt (v * 100.0f)) + "%"; }

static StringArray bodyDescs()
{
    StringArray d;
    d.add ("upright " + DOT() + " felt " + DOT() + " old hammers");
    d.add ("electric " + DOT() + " bell " + DOT() + " warm");
    d.add ("gut strings " + DOT() + " fingers");
    d.add ("small section " + DOT() + " 1960s");
    d.add ("ooh / aah " + DOT() + " church hall");
    return d;
}

MainView::MainView (GdProcessor& p)
    : proc (p),
      body (p, "body", { "PIANO", "RHODES", "NYLON", "STRINGS", "CHOIR" }, OptionStrip::BodyList, bodyDescs()),
      tomb (p, "tomb", { "SOUL '72", "BASEMENT LO-FI", "HAUNTED REEL '64", "DETROIT DARK" }, OptionStrip::Grid2),
      capLen (p, "caplen", { "4 BAR", "8 BAR", "16 BAR" }, OptionStrip::Segmented),
      mode (p, "mode", { "MELODY", "BONES" }, OptionStrip::Segmented),
      chop (p, "chop", { "HITS", "EVEN" }, OptionStrip::Segmented),
      years (p.apvts, "years", "YEARS BURIED", "one knob ages everything.", [] (float v) { return String (roundToInt (v)); }, true),
      rot   (p.apvts, "rot",   "ROT",   "vinyl crackle",  pct),
      drown (p.apvts, "drown", "DROWN", "tape wobble",    pct),
      sink  (p.apvts, "sink",  "SINK",  "pitch down",     [] (float v) { return v < 0.05f ? String ("0 st") : MINUS() + String (v, 1) + " st"; }),
      mold  (p.apvts, "mold",  "MOLD",  "dull the highs", [this] (float v)
             {
                 const float hz = moldCutoff (v, proc.apvts.getRawParameterValue ("years")->load(),
                                              (int) std::lround (proc.apvts.getRawParameterValue ("tomb")->load()));
                 return hz >= 1000.0f ? String (hz / 1000.0f, 1) + " kHz" : String (roundToInt (hz)) + " Hz";
             }),
      ghost (p.apvts, "ghost", "GHOST", "old room tail",  pct),
      bleed (p.apvts, "bleed", "BLEED", "warm saturation", pct),
      record (p)
{
    setLookAndFeel (&look);

    grain = Image (Image::ARGB, 1180, 780, true);
    Random gr (7);
    for (int i = 0; i < 70000; ++i)
        grain.setPixelAt (gr.nextInt (1180), gr.nextInt (780), col::bone.withAlpha (gr.nextFloat() * 0.16f));

    for (auto* c : std::initializer_list<Component*> { &body, &tomb, &capLen, &mode, &chop, &years, &rot, &drown, &sink,
                                                       &mold, &ghost, &bleed, &record, &roll, &captureBtn, &dragAll,
                                                       &reverseBtn, &redigBtn })
        addAndMakeVisible (c);

    for (int i = 0; i < kNumBones; ++i)
    {
        pads.push_back (std::make_unique<BonePad> (p, i));
        addAndMakeVisible (*pads.back());
    }

    captureBtn.draw = [this] (Graphics& g, Rectangle<float> r, bool over)
    {
        const int st = proc.capState.load();
        g.setColour (over ? col::red.brighter (0.15f) : col::red);
        g.fillRect (r);
        String txt = "CAPTURE";
        if (st == capArmed) txt = "ARMED";
        else if (st == capRecording) txt = "BURYING " + String (roundToInt (proc.capProgress.load() * 100.0f)) + "%";
        else if (st == capFinishing) txt = "DIGGING";
        const float pulse = st == capArmed ? 0.5f + 0.5f * std::sin ((float) Time::getMillisecondCounter() * 0.012f) : 1.0f;
        g.setColour (col::bg.withAlpha (pulse));
        g.fillEllipse (r.getX() + 14.0f, r.getCentreY() - 4.5f, 9.0f, 9.0f);
        g.setColour (col::bg);
        g.setFont (oswald (15.0f).withExtraKerningFactor (0.12f));
        g.drawText (txt, r.withTrimmedLeft (30.0f), Justification::centredLeft);
    };
    captureBtn.onClick = [this] { proc.toggleCapture(); };

    dragAll.draw = [this] (Graphics& g, Rectangle<float> r, bool over)
    {
        const bool has = proc.hasBones.load();
        const float a = has ? 1.0f : 0.4f;
        if (over && has) { g.setColour (col::deep); g.fillRect (r); }
        g.setColour (col::red.withAlpha (a));
        const float dash[] = { 4.0f, 3.0f };
        g.drawDashedLine ({ r.getX(), r.getY() + 0.5f, r.getRight(), r.getY() + 0.5f }, dash, 2, 1.0f);
        g.drawDashedLine ({ r.getX(), r.getBottom() - 0.5f, r.getRight(), r.getBottom() - 0.5f }, dash, 2, 1.0f);
        g.drawDashedLine ({ r.getX() + 0.5f, r.getY(), r.getX() + 0.5f, r.getBottom() }, dash, 2, 1.0f);
        g.drawDashedLine ({ r.getRight() - 0.5f, r.getY(), r.getRight() - 0.5f, r.getBottom() }, dash, 2, 1.0f);
        g.setColour (col::bone.withAlpha (a));
        g.setFont (oswald (14.0f).withExtraKerningFactor (0.14f));
        g.drawText ("DRAG TO FL", Rectangle<float> (r.getX() + 12, r.getY() + 9, 110, 18), Justification::centredLeft);
        const float ax = r.getRight() - 22.0f, ay = r.getY() + 12.0f;
        Path arrow;
        arrow.startNewSubPath (ax, ay); arrow.lineTo (ax, ay + 10);
        arrow.startNewSubPath (ax - 4, ay + 6); arrow.lineTo (ax, ay + 10); arrow.lineTo (ax + 4, ay + 6);
        arrow.startNewSubPath (ax - 6, ay + 13); arrow.lineTo (ax + 6, ay + 13);
        g.strokePath (arrow, PathStrokeType (1.5f));
        g.setColour (col::muted.withAlpha (a));
        g.setFont (mono (10.0f));
        g.drawText ("full sample or one bone", Rectangle<float> (r.getX() + 12, r.getY() + 29, r.getWidth() - 16, 14), Justification::centredLeft);
    };
    dragAll.onDragOut = [this] { dragFile (proc.exportBone (-1), &dragAll); };
    dragAll.onClick = [this]
    {
        const auto f = proc.exportBone (-1);
        if (f.existsAsFile()) f.revealToUser();
    };

    reverseBtn.draw = [this] (Graphics& g, Rectangle<float> r, bool over)
    {
        const bool on = proc.apvts.getRawParameterValue ("reverse")->load() > 0.5f;
        g.setColour (over ? col::sel : col::panel);
        g.fillRect (r);
        g.setColour (col::line);
        g.drawRect (r, 1.0f);
        g.setFont (mono (10.5f));
        g.setColour (col::muted);
        g.drawText ("REVERSE BONES", r.reduced (12.0f, 0.0f), Justification::centredLeft);
        g.setColour (on ? col::redTxt : col::muted);
        g.drawText (on ? "ON" : "OFF", r.reduced (12.0f, 0.0f), Justification::centredRight);
    };
    reverseBtn.onClick = [this]
    {
        if (auto* prm = proc.apvts.getParameter ("reverse"))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f);
            prm->endChangeGesture();
        }
    };

    redigBtn.draw = [] (Graphics& g, Rectangle<float> r, bool over)
    {
        g.setColour (over ? col::sel : col::panel);
        g.fillRect (r);
        g.setColour (col::line);
        g.drawRect (r, 1.0f);
        g.setFont (mono (10.5f));
        g.setColour (col::muted);
        g.drawText ("RE-DIG", r.reduced (12.0f, 0.0f), Justification::centredLeft);
        const float cx = r.getRight() - 18.0f, cy = r.getCentreY();
        Path p;
        p.addCentredArc (cx, cy, 5.0f, 5.0f, 0.0f, 0.6f, kTwoPi - 0.2f, true);
        p.startNewSubPath (cx - 1.5f, cy - 8.0f); p.lineTo (cx + 1.5f, cy - 5.0f); p.lineTo (cx - 1.5f, cy - 2.5f);
        g.strokePath (p, PathStrokeType (1.3f));
    };
    redigBtn.onClick = [this] { proc.redig(); };

    setSize (1180, 780);
    startTimerHz (60);
}

MainView::~MainView()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MainView::dragFile (const File& f, Component* src)
{
    if (f.existsAsFile())
        DragAndDropContainer::performExternalDragDropOfFiles (StringArray (f.getFullPathName()), false, src);
}

void MainView::resized()
{
    body.setBounds (36, 150, 230, 244);
    roll.setBounds (42, 430, 218, 104);

    record.setBounds (396, 112, 360, 360);
    captureBtn.setBounds (341, 486, 130, 44);
    capLen.setBounds (481, 486, 150, 44);
    mode.setBounds (641, 486, 170, 44);

    years.setBounds (886, 146, 258, 100);
    Knob* smallKnobs[] = { &rot, &drown, &sink, &mold, &ghost, &bleed };
    for (int i = 0; i < 6; ++i) smallKnobs[i]->setBounds (886 + (i % 3) * 86, 258 + (i / 3) * 94, 86, 94);
    tomb.setBounds (886, 470, 258, 66);

    chop.setBounds (36, 708, 196, 30);
    const float pw = (708.0f - 56.0f) / 8.0f;
    for (int i = 0; i < kNumBones; ++i)
        pads[(size_t) i]->setBounds (roundToInt (250.0f + (float) (i % 8) * (pw + 8.0f)), 597 + (i / 8) * 68, roundToInt (pw), 60);
    dragAll.setBounds (976, 588, 168, 52);
    reverseBtn.setBounds (976, 648, 168, 34);
    redigBtn.setBounds (976, 690, 168, 34);
}

void MainView::heading (Graphics& g, float x, float y, const String& num, const String& name, const String& plain)
{
    const auto nf = oswald (13.0f).withExtraKerningFactor (0.15f);
    g.setFont (nf);
    g.setColour (col::redTxt);
    g.drawText (num, Rectangle<float> (x, y + 3, 40, 16), Justification::centredLeft);
    const float off = textW (nf, num) + 10.0f;
    g.setFont (oswald (21.0f).withExtraKerningFactor (0.12f));
    g.setColour (col::bone);
    g.drawText (name, Rectangle<float> (x + off, y - 2, 220, 24), Justification::centredLeft);
    g.setFont (mono (11.0f));
    g.setColour (col::muted);
    g.drawText (plain, Rectangle<float> (x, y + 24, 260, 14), Justification::centredLeft);
}

void MainView::paint (Graphics& g)
{
    g.fillAll (col::bg);

    // ---- header
    const auto lf = fraktur (46.0f);
    g.setFont (lf);
    g.setColour (col::bone);
    g.drawText ("Gravedigger", Rectangle<float> (20, 10, 320, 52), Justification::centredLeft);
    const float lw = textW (lf, "Gravedigger");
    g.setFont (mono (10.0f).withExtraKerningFactor (0.2f));
    g.setColour (col::redTxt);
    g.drawText ("EXHUME YOUR OWN SAMPLES", Rectangle<float> (20 + lw + 14, 34, 280, 14), Justification::centredLeft);

    const bool playing = proc.hostPlaying.load();
    g.setFont (mono (11.0f));
    g.setColour (playing ? col::red : Colour (0xff4a433d));
    g.fillEllipse (752, 31, 7, 7);
    g.setColour (col::muted);
    g.drawText (playing ? "SYNCED TO FL" : "FL STOPPED", Rectangle<float> (766, 24, 110, 20), Justification::centredLeft);
    g.setColour (col::bone);
    g.drawText (String (roundToInt (proc.hostBpm.load())), Rectangle<float> (900, 24, 40, 20), Justification::centredRight);
    g.setColour (col::muted);
    g.drawText ("BPM", Rectangle<float> (944, 24, 40, 20), Justification::centredLeft);
    g.drawText ("OUT", Rectangle<float> (1062, 24, 36, 20), Justification::centredLeft);
    for (int ch = 0; ch < 2; ++ch)
    {
        const float lvl = ch == 0 ? meterL : meterR;
        const float lit = jlimit (0.0f, 1.0f, 1.0f + Decibels::gainToDecibels (lvl, -60.0f) / 60.0f);
        for (int k = 0; k < 12; ++k)
        {
            const bool on = (float) k / 12.0f < lit;
            g.setColour (on ? (k >= 10 ? col::red : (k >= 7 ? Colour (0xffc9bfa9) : Colour (0xff8a8175))) : Colour (0xff26221f));
            g.fillRect (1098.0f + (float) k * 4.5f, 22.0f + (float) ch * 14.0f, 3.2f, 9.0f);
        }
    }
    g.setColour (col::line);
    g.fillRect (20, 68, 1140, 1);

    // ---- panels
    auto panelRect = [&g] (Rectangle<float> r)
    {
        g.setColour (col::panel); g.fillRect (r);
        g.setColour (col::line);  g.drawRect (r, 1.0f);
    };
    panelRect ({ 20, 80, 262, 470 });
    panelRect ({ 870, 80, 290, 470 });
    panelRect ({ 20, 562, 1140, 198 });

    heading (g, 36, 96, "I", "THE BODY", "what your melody is played on");
    heading (g, 886, 96, "II", "THE GRAVE", "age it like a record from the dirt");
    heading (g, 36, 576, "III", "THE BONES", "auto-chopped onto keys " + DASH() + " play them");

    // midi in
    g.setFont (mono (10.0f).withExtraKerningFactor (0.1f));
    g.setColour (col::muted);
    g.drawText ("MIDI IN " + DOT() + " YOUR MELODY", Rectangle<float> (36, 404, 200, 14), Justification::centredLeft);
    if (roll.live)
    {
        g.setColour (col::redTxt);
        g.fillEllipse (226, 408, 6, 6);
        g.drawText ("LIVE", Rectangle<float> (236, 404, 30, 14), Justification::centredLeft);
    }
    g.setColour (Colour (0xff0E0C0B));
    g.fillRect (36.0f, 424.0f, 230.0f, 116.0f);
    g.setColour (col::line);
    g.drawRect (36.0f, 424.0f, 230.0f, 116.0f, 1.0f);

    // steps
    {
        struct Piece { String s; Colour c; };
        const String rule = DASH() + DASH() + DASH();
        const Piece pieces[] = { { "I ", col::redTxt }, { "PLAY A MELODY  ", col::muted }, { rule + "  ", Colour (0xff4a433d) },
                                 { "II ", col::redTxt }, { "BURY IT  ", col::muted }, { rule + "  ", Colour (0xff4a433d) },
                                 { "III ", col::redTxt }, { "DIG UP THE BONES", col::muted } };
        const auto f = mono (10.0f).withExtraKerningFactor (0.12f);
        float total = 0;
        for (auto& p : pieces) total += textW (f, p.s);
        float x = 296.0f + (560.0f - total) * 0.5f;
        g.setFont (f);
        for (auto& p : pieces)
        {
            const float w = textW (f, p.s);
            g.setColour (p.c);
            g.drawText (p.s, Rectangle<float> (x, 84, w + 2, 20), Justification::centredLeft);
            x += w;
        }
    }

    // tomb label
    g.setFont (mono (10.0f).withExtraKerningFactor (0.1f));
    g.setColour (col::muted);
    g.drawText ("TOMB " + DOT() + " AGING STYLE", Rectangle<float> (886, 452, 200, 14), Justification::centredLeft);

    // status
    {
        StringArray lines;
        const int st = proc.capState.load();
        if (st == capArmed)
        {
            lines.add ("ARMED.");
            lines.add (playing ? "digging starts on the next bar" : "digging starts on your first note");
        }
        else if (st == capRecording)
        {
            lines.add ("BURYING " + String (roundToInt (proc.capProgress.load() * 100.0f)) + "%");
            lines.add ("keep playing...");
        }
        else if (view.numBones > 0)
        {
            lines.add (String (view.bars) + " bars buried " + DOT() + " " + String (view.numBones) + " bones");
            lines.add ("BONES mode: keys C5" + DASH() + "D#6");
            lines.add ("drag a pad into FL to keep it");
        }
        else
        {
            lines.add ("1. play a melody");
            lines.add ("2. hit CAPTURE (red button)");
            lines.add ("3. play the bones on C5 and up");
        }
        g.setFont (mono (10.5f));
        for (int i = 0; i < lines.size(); ++i)
        {
            g.setColour (i == 0 ? col::bone : col::muted);
            g.drawText (lines[i], Rectangle<float> (36, 622.0f + (float) i * 15.0f, 205, 14), Justification::centredLeft);
        }
        g.setFont (mono (10.0f).withExtraKerningFactor (0.1f));
        g.setColour (col::muted);
        g.drawText ("CHOP BY", Rectangle<float> (36, 690, 120, 14), Justification::centredLeft);
    }

    // ---- grime
    g.setGradientFill (ColourGradient (Colours::transparentBlack, 590, 350, Colours::black.withAlpha (0.5f), 0, 0, true));
    g.fillAll();
    g.setOpacity (0.5f);
    g.drawImageAt (grain, 0, 0);
    g.setOpacity (1.0f);
}

void MainView::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    const float dt = lastTime > 0.0 ? (float) jlimit (0.0, 0.1, now - lastTime) : 1.0f / 60.0f;
    lastTime = now;

    const int v = proc.bankVersion.load();
    if (v != seenVersion)
    {
        seenVersion = v;
        proc.getBankView (view);
        record.setBank (view);
        for (int i = 0; i < kNumBones; ++i)
        {
            pads[(size_t) i]->exists = i < view.numBones;
            pads[(size_t) i]->peaks = view.peaks[(size_t) i];
        }
        if (activeBone >= view.numBones) activeBone = -1;
    }

    const uint32_t nc = proc.noteCounter.load();
    if (nc != seenNotes)
    {
        seenNotes = nc;
        if (mode.current() == 0) record.lookAtNote (proc.lastNote.load());
    }

    const float decay = std::exp (-dt * 6.0f);
    for (int i = 0; i < kNumBones; ++i)
    {
        const uint32_t h = proc.boneHits[i].load();
        if (h != seenHits[i])
        {
            seenHits[i] = h;
            padFlash[i] = 1.0f;
            activeBone = i;
            boneFlash = 1.0f;
            record.lookAtBone (i);
        }
        padFlash[i] *= decay;
        pads[(size_t) i]->flash = padFlash[i];
        pads[(size_t) i]->active = (i == activeBone);
    }
    boneFlash *= decay;

    meterL = jmax (proc.outL.load(), meterL * 0.85f);
    meterR = jmax (proc.outR.load(), meterR * 0.85f);

    const bool playing = proc.hostPlaying.load();
    const bool notesOn = (proc.noteBits[0].load() | proc.noteBits[1].load()) != 0;
    const float yrs = proc.apvts.getRawParameterValue ("years")->load();
    record.tick (dt, playing || notesOn || proc.capState.load() == capRecording, playing, proc.hostPpq.load(),
                 jmax (meterL, meterR), activeBone, boneFlash, yrs);

    ++frame;
    if (frame % 2 == 0)
    {
        const uint64_t a = proc.noteBits[0].load() | proc.noteLatch[0].exchange (0);
        const uint64_t b = proc.noteBits[1].load() | proc.noteLatch[1].exchange (0);
        roll.live = (a | b) != 0;
        roll.push (a, b);
        roll.repaint();
        for (auto& pd : pads) pd->repaint();
    }
    if (frame % 4 == 0)
    {
        repaint (0, 0, 1180, 70);                 // header
        repaint (20, 400, 262, 20);               // live label
        repaint (296, 480, 560, 60);              // capture row
        repaint (20, 612, 220, 80);               // status
        for (auto* c : std::initializer_list<Component*> { &body, &tomb, &capLen, &mode, &chop, &years, &rot, &drown,
                                                           &sink, &mold, &ghost, &bleed, &dragAll, &reverseBtn, &redigBtn })
            c->repaint();
    }
}

// ================================================================== editor shell (scalable)
GdEditor::GdEditor (GdProcessor& p) : AudioProcessorEditor (p), view (p)
{
    addAndMakeVisible (view);
    setResizable (true, true);
    setResizeLimits (826, 546, 1475, 975);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio (1180.0 / 780.0);
    setSize (1180, 780);
}

void GdEditor::resized()
{
    const float s = (float) getWidth() / 1180.0f;
    view.setBounds (0, 0, 1180, 780);
    view.setTransform (AffineTransform::scale (s));
}
} // namespace gd
