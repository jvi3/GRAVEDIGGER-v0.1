#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <deque>
#include "PluginProcessor.h"

namespace gd
{
namespace col
{
    const juce::Colour bg     { 0xff0B0A09 };
    const juce::Colour panel  { 0xff131110 };
    const juce::Colour line   { 0xff2A2622 };
    const juce::Colour bone   { 0xffE6DDC8 };
    const juce::Colour muted  { 0xff9A9183 };
    const juce::Colour red    { 0xffC4271B };
    const juce::Colour redTxt { 0xffE5493B };
    const juce::Colour deep   { 0xff2A0D0B };
    const juce::Colour card   { 0xff171412 };
    const juce::Colour sel    { 0xff26211E };
}

juce::Font fraktur (float h);
juce::Font oswald (float h);
juce::Font mono (float h);

class GdLook : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float a0, float a1, juce::Slider&) override;
};

// ------------------------------------------------------------ knob with creepy name + plain label
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& s, const juce::String& id, juce::String name, juce::String plain,
          std::function<juce::String (float)> fmt, bool big = false);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    std::atomic<float>* raw;
    juce::String name, plain;
    std::function<juce::String (float)> format;
    bool big;
};

// ------------------------------------------------------------ choice selector bound to a parameter
class OptionStrip : public juce::Component
{
public:
    enum Style { Segmented, Grid2, BodyList };
    OptionStrip (GdProcessor& p, const juce::String& id, juce::StringArray labels, Style st, juce::StringArray descs = {});
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    int current() const { return (int) std::lround (raw->load()); }

private:
    juce::Rectangle<float> cell (int i) const;
    GdProcessor& proc;
    juce::String paramId;
    std::atomic<float>* raw;
    juce::StringArray labels, descs;
    Style style;
};

// ------------------------------------------------------------ generic painted button
class ActionButton : public juce::Component
{
public:
    std::function<void()> onClick, onDragOut;
    std::function<void (juce::Graphics&, juce::Rectangle<float>, bool)> draw;
    void paint (juce::Graphics& g) override { if (draw) draw (g, getLocalBounds().toFloat(), isMouseOver (true)); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseDown (const juce::MouseEvent&) override  { dragged = false; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (onDragOut && ! dragged && e.getDistanceFromDragStart() > 6) { dragged = true; onDragOut(); }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! dragged && onClick && getLocalBounds().contains (e.getPosition())) onClick();
        dragged = false;
    }
private:
    bool dragged = false;
};

// ------------------------------------------------------------ the sliced record with the living eye
class RecordView : public juce::Component
{
public:
    explicit RecordView (GdProcessor& p);
    void setBank (const BankView& v);
    void tick (float dt, bool spinning, bool playing, double ppq, float level, int activeBone, float flash, float years);
    void lookAtNote (int note);
    void lookAtBone (int bone);
    void paint (juce::Graphics&) override;

private:
    void buildImage();
    juce::Path sectorPath (int i, float r0, float r1) const;
    void drawEye (juce::Graphics&);
    void startBlink (bool allowDouble);

    GdProcessor& proc;
    juce::Image img;
    std::vector<double> fracs;
    std::vector<float> env;
    int numBones = 0;

    float angle = 0, level = 0, years = 52, flash = 0, t = 0;
    int activeBone = -1;

    // eye
    std::vector<std::pair<juce::Path, float>> veins;
    juce::Path streaks;
    float blinkT = -1, blinkDelay = 0, idleTimer = 2.5f, sinceBlink = 0;
    bool doublePending = false;
    long long lastBeat = -1;
    juce::Point<float> gaze, gazeTarget;
    float saccadeTimer = 1.5f, pupil = 12.5f;
    juce::Random rnd { 666 };

    static constexpr float C = 180.0f, R = 172.0f, RL = 58.0f;
};

// ------------------------------------------------------------ MIDI-in mini roll
class MidiRoll : public juce::Component
{
public:
    void push (uint64_t a, uint64_t b);
    void paint (juce::Graphics&) override;
    bool live = false;
private:
    std::deque<std::array<uint64_t, 2>> cols;
};

// ------------------------------------------------------------ one bone pad
class BonePad : public juce::Component
{
public:
    BonePad (GdProcessor& p, int i) : proc (p), index (i) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    std::array<float, kPeakBins> peaks {};
    bool exists = false;
    float flash = 0;
    bool active = false;
private:
    GdProcessor& proc;
    int index;
    bool dragged = false;
};

// ------------------------------------------------------------ the whole 1180x780 face
class MainView : public juce::Component, private juce::Timer
{
public:
    explicit MainView (GdProcessor& p);
    ~MainView() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void heading (juce::Graphics&, float x, float y, const juce::String& num, const juce::String& name, const juce::String& plain);
    void dragFile (const juce::File& f, juce::Component* src);

    GdProcessor& proc;
    GdLook look;
    juce::Image grain;
    OptionStrip body, tomb, capLen, mode, chop;
    Knob years, rot, drown, sink, mold, ghost, bleed;
    RecordView record;
    MidiRoll roll;
    std::vector<std::unique_ptr<BonePad>> pads;
    ActionButton captureBtn, dragAll, reverseBtn, redigBtn;

    BankView view;
    int seenVersion = -1;
    uint32_t seenNotes = 0;
    uint32_t seenHits[kNumBones] {};
    float padFlash[kNumBones] {};
    int activeBone = -1;
    float boneFlash = 0;
    double lastTime = 0;
    int frame = 0;
    float meterL = 0, meterR = 0;
};

class GdEditor : public juce::AudioProcessorEditor
{
public:
    explicit GdEditor (GdProcessor&);
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (col::bg); }
private:
    MainView view;
};
} // namespace gd
