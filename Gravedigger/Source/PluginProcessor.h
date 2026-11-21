#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include "Voices.h"
#include "Grave.h"
#include "Bones.h"

namespace gd
{
enum CaptureState { capIdle = 0, capArmed = 1, capRecording = 2, capFinishing = 3 };

struct BankView
{
    int numBones = 0, bars = 0;
    double seconds = 0;
    std::vector<double> fracs;                                // numBones + 1 positions, 0..1
    std::vector<float> env;
    std::array<std::array<float, kPeakBins>, kNumBones> peaks {};
};

class GdProcessor : public juce::AudioProcessor,
                    private juce::AsyncUpdater,
                    private juce::AudioProcessorValueTreeState::Listener
{
public:
    GdProcessor();
    ~GdProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Gravedigger"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- used by the UI (message thread)
    juce::AudioProcessorValueTreeState apvts;
    void toggleCapture();
    void previewBone (int idx) { previewReq.store (idx); }
    void redig();
    juce::File exportBone (int idx);   // idx -1 = whole sample
    void getBankView (BankView& v);
    void setChoice (const juce::String& id, int index);
    GraveParams currentGraveParams() const;

    // ---- live info for the UI
    std::atomic<int> capState { capIdle };
    std::atomic<float> capProgress { 0.0f };
    std::atomic<int> capBarsUi { 8 };
    std::atomic<float> outL { 0 }, outR { 0 };
    std::atomic<float> hostBpm { 120.0f };
    std::atomic<double> hostPpq { 0.0 };
    std::atomic<bool> hostPlaying { false };
    std::atomic<uint64_t> noteBits[2], noteLatch[2];
    std::atomic<int> lastNote { 60 };
    std::atomic<uint32_t> noteCounter { 0 };
    std::atomic<uint32_t> boneHits[kNumBones];
    std::atomic<int> lastBone { -1 };
    std::atomic<int> bankVersion { 0 };
    std::atomic<bool> hasBones { false };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void handleAsyncUpdate() override;
    void parameterChanged (const juce::String& id, float) override;
    void finishCapture();
    void rechop();

    std::atomic<float>* pBody;  std::atomic<float>* pYears; std::atomic<float>* pRot;   std::atomic<float>* pDrown;
    std::atomic<float>* pSink;  std::atomic<float>* pMold;  std::atomic<float>* pGhost; std::atomic<float>* pBleed;
    std::atomic<float>* pTomb;  std::atomic<float>* pCapLen; std::atomic<float>* pChop; std::atomic<float>* pMode;
    std::atomic<float>* pReverse;

    juce::Synthesiser synth;
    Grave grave;
    BonePlayer bonePlayer;
    juce::MidiBuffer emptyMidi;
    double sr = 44100.0;
    int lastMode = -1;

    // capture
    juce::AudioBuffer<float> capBuf;
    int capLen = 0, capPos = 0, capBars = 8;
    bool capSynced = false;
    std::atomic<int> previewReq { -1 };
    std::atomic<bool> rechopPending { false };

    // bank
    juce::SpinLock bankLock;
    Bank bank;
    int variant = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GdProcessor)
};
} // namespace gd
