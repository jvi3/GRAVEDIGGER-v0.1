#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

namespace gd
{
// ------------------------------------------------------------------ embedded sample library
const std::vector<LibInfo>& libraryInfo()
{
    static const std::vector<LibInfo> infos = []
    {
        std::vector<LibInfo> out;
        int size = 0;
        const char* data = BinaryData::getNamedResource ("library_json", size);
        if (data == nullptr || size <= 0) return out;
        const auto json = juce::JSON::parse (juce::String::fromUTF8 (data, size));
        if (auto* arr = json.getProperty ("instruments", {}).getArray())
            for (auto& v : *arr)
            {
                LibInfo li;
                li.id = v.getProperty ("id", {}).toString();
                li.name = v.getProperty ("name", {}).toString();
                li.desc = v.getProperty ("desc", {}).toString();
                li.cat = v.getProperty ("cat", {}).toString();
                li.release = (float) (double) v.getProperty ("release", 0.3);
                li.oneshot = (bool) v.getProperty ("oneshot", false);
                if (auto* smp = v.getProperty ("samples", {}).getArray())
                    for (auto& z : *smp)
                        li.files.push_back ({ z.getProperty ("file", {}).toString(), (float) (double) z.getProperty ("root", 60.0) });
                if (! li.files.empty()) out.push_back (li);
            }
        return out;
    }();
    return infos;
}

juce::StringArray bodyNames()
{
    juce::StringArray s { "PIANO", "RHODES", "NYLON", "STRINGS", "CHOIR" };
    for (auto& li : libraryInfo()) s.add (li.name);
    return s;
}

juce::StringArray bodyDescs()
{
    const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0x00B7);
    juce::StringArray s;
    s.add ("synth " + dot + " felt " + dot + " old hammers");
    s.add ("synth " + dot + " electric bell");
    s.add ("synth " + dot + " plucked gut strings");
    s.add ("synth " + dot + " 1960s section");
    s.add ("synth " + dot + " ooh / aah");
    for (auto& li : libraryInfo()) s.add (li.desc);
    return s;
}

std::vector<int> bodyCats()
{
    std::vector<int> c (kNumSynthBodies, 0);
    for (auto& li : libraryInfo()) c.push_back (li.cat == "HAUNTED" ? 2 : 1);
    return c;
}

SampleSetPtr GdProcessor::loadSet (const LibInfo& info)
{
    auto set = std::make_shared<SampleSet>();
    set->release = info.release;
    set->oneshot = info.oneshot;
    juce::OggVorbisAudioFormat ogg;
    for (auto& f : info.files)
    {
        int size = 0;
        const juce::String res = f.first.replaceCharacter ('.', '_').replaceCharacter ('-', '_');
        const char* data = BinaryData::getNamedResource (res.toRawUTF8(), size);
        if (data == nullptr || size <= 0) continue;
        std::unique_ptr<juce::AudioFormatReader> reader (ogg.createReaderFor (new juce::MemoryInputStream (data, (size_t) size, false), true));
        if (reader == nullptr || reader->lengthInSamples <= 0) continue;
        Zone z;
        const int len = (int) reader->lengthInSamples;
        z.audio.setSize (2, len);
        z.audio.clear();
        reader->read (&z.audio, 0, len, 0, true, true);
        z.sr = reader->sampleRate;
        z.root = f.second;
        set->zones.push_back (std::move (z));
    }
    return set;
}

SampleSetPtr GdProcessor::currentSet (int body)
{
    if (loadedBody.load() != body) return nullptr;
    juce::SpinLock::ScopedTryLockType tl (setLock);
    if (! tl.isLocked()) return nullptr;
    return loaded;
}

void GdProcessor::ensureLibraryLoaded()
{
    const int body = (int) std::lround (pBody->load());
    const int li = body - kNumSynthBodies;
    if (li >= 0 && li < (int) libraryInfo().size() && loadedBody.load() != body)
    {
        auto s = loadSet (libraryInfo()[(size_t) li]);
        juce::SpinLock::ScopedLockType l (setLock);
        if (loaded != nullptr) graveyard.push_back (loaded);
        loaded = s;
        loadedBody.store (body);
    }
    // free sets no voice is using any more (only the graveyard still holds them)
    juce::SpinLock::ScopedLockType l (setLock);
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                     [] (const SampleSetPtr& p) { return p.use_count() == 1; }),
                     graveyard.end());
}

juce::AudioProcessorValueTreeState::ParameterLayout GdProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "body", 1 }, "Body", bodyNames(), 1));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "years", 1 }, "Years Buried",
                    NormalisableRange<float> (0.0f, 80.0f, 1.0f), 52.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "rot", 1 },   "Rot",   NormalisableRange<float> (0.0f, 1.0f), 0.38f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "drown", 1 }, "Drown", NormalisableRange<float> (0.0f, 1.0f), 0.22f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "sink", 1 },  "Sink",  NormalisableRange<float> (0.0f, 12.0f, 0.01f), 3.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "mold", 1 },  "Mold",  NormalisableRange<float> (0.0f, 1.0f), 0.55f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "ghost", 1 }, "Ghost", NormalisableRange<float> (0.0f, 1.0f), 0.45f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "bleed", 1 }, "Bleed", NormalisableRange<float> (0.0f, 1.0f), 0.41f));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "tomb", 1 }, "Tomb",
                    StringArray { "Soul '72", "Basement Lo-Fi", "Haunted Reel '64", "Detroit Dark" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "caplen", 1 }, "Capture Length",
                    StringArray { "4 Bars", "8 Bars", "16 Bars" }, 1));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "chop", 1 }, "Chop By",
                    StringArray { "Hits", "Even" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Play Mode",
                    StringArray { "Melody", "Bones" }, 0));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "reverse", 1 }, "Reverse Bones", false));
    return { p.begin(), p.end() };
}

GdProcessor::GdProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (auto& b : noteBits)  b.store (0);
    for (auto& b : noteLatch) b.store (0);
    for (auto& h : boneHits)  h.store (0);

    pBody  = apvts.getRawParameterValue ("body");   pYears = apvts.getRawParameterValue ("years");
    pRot   = apvts.getRawParameterValue ("rot");    pDrown = apvts.getRawParameterValue ("drown");
    pSink  = apvts.getRawParameterValue ("sink");   pMold  = apvts.getRawParameterValue ("mold");
    pGhost = apvts.getRawParameterValue ("ghost");  pBleed = apvts.getRawParameterValue ("bleed");
    pTomb  = apvts.getRawParameterValue ("tomb");   pCapLen = apvts.getRawParameterValue ("caplen");
    pChop  = apvts.getRawParameterValue ("chop");   pMode  = apvts.getRawParameterValue ("mode");
    pReverse = apvts.getRawParameterValue ("reverse");

    synth.addSound (new GdSound());
    for (int i = 0; i < 16; ++i) synth.addVoice (new GdVoice (pBody, pSink, this, i));

    apvts.addParameterListener ("chop", this);
    triggerAsyncUpdate();
}

GdProcessor::~GdProcessor()
{
    apvts.removeParameterListener ("chop", this);
    cancelPendingUpdate();
}

bool GdProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void GdProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    synth.setCurrentPlaybackSampleRate (sr);
    grave.prepare (sr);
    bonePlayer.prepare (sr);
    capBuf.setSize (2, (int) (sr * 66.0));
    capBuf.clear();
    if (capState.load() != capFinishing) capState.store (capIdle);
}

GraveParams GdProcessor::currentGraveParams() const
{
    GraveParams g;
    g.years = pYears->load(); g.rot = pRot->load(); g.drown = pDrown->load();
    g.mold = pMold->load(); g.ghost = pGhost->load(); g.bleed = pBleed->load();
    g.tomb = (int) std::lround (pTomb->load());
    return g;
}

void GdProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();

    // ---- host transport
    double bpm = 120.0, ppq = 0.0;
    bool playing = false, ppqOk = false;
    int num = 4, den = 4;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = juce::jlimit (20.0, 400.0, *b);
            playing = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) { ppq = *q; ppqOk = true; }
            if (auto ts = pos->getTimeSignature()) { num = juce::jmax (1, ts->numerator); den = juce::jmax (1, ts->denominator); }
        }
    hostBpm.store ((float) bpm);
    hostPlaying.store (playing);
    hostPpq.store (ppq);

    // ---- scan midi for the UI and for capture start
    int firstNoteOn = -1;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            const int nn = m.getNoteNumber();
            const uint64_t bit = (uint64_t) 1 << (nn & 63);
            noteBits[nn >> 6].fetch_or (bit);
            noteLatch[nn >> 6].fetch_or (bit);
            lastNote.store (nn);
            noteCounter.fetch_add (1);
            if (firstNoteOn < 0) firstNoteOn = meta.samplePosition;
        }
        else if (m.isNoteOff())
        {
            const int nn = m.getNoteNumber();
            noteBits[nn >> 6].fetch_and (~((uint64_t) 1 << (nn & 63)));
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            noteBits[0].store (0); noteBits[1].store (0);
        }
    }

    // ---- sound sources
    {
        const int body = (int) std::lround (pBody->load());
        if (body >= kNumSynthBodies && loadedBody.load() != body) triggerAsyncUpdate();
    }
    const int mode = (int) std::lround (pMode->load());
    if (mode != lastMode)
    {
        synth.allNotesOff (0, false);
        bonePlayer.allOff();
        lastMode = mode;
    }
    if (mode == 0) synth.renderNextBlock (buffer, midi, 0, n);

    const int preview = previewReq.exchange (-1);
    {
        juce::SpinLock::ScopedTryLockType tl (bankLock);
        if (tl.isLocked())
        {
            const float rate = std::pow (2.0f, -pSink->load() / 12.0f);
            bonePlayer.render (buffer, mode == 1 ? midi : emptyMidi, n, bank, rate, pReverse->load() > 0.5f,
                               preview, boneHits, lastBone);
        }
    }

    // ---- capture the dry melody
    const int st = capState.load();
    auto record = [&] (int from)
    {
        const int toCopy = juce::jmin (n - from, capLen - capPos);
        if (toCopy > 0)
        {
            for (int c = 0; c < 2; ++c) capBuf.copyFrom (c, capPos, buffer, juce::jmin (c, buffer.getNumChannels() - 1), from, toCopy);
            capPos += toCopy;
        }
        capProgress.store (capLen > 0 ? (float) capPos / (float) capLen : 0.0f);
        if (capPos >= capLen)
        {
            capState.store (capFinishing);
            triggerAsyncUpdate();
        }
    };
    if (st == capArmed && mode == 0)
    {
        int startAt = -1;
        if (playing && ppqOk)
        {
            const double barQ = num * 4.0 / den;
            const double spq = 60.0 / bpm * sr;
            const double nextBar = std::ceil (ppq / barQ - 1.0e-6) * barQ;
            const double off = (nextBar - ppq) * spq;
            if (off < (double) n) { startAt = juce::jlimit (0, n - 1, (int) off); capSynced = true; }
        }
        else if (! playing && firstNoteOn >= 0)
        {
            startAt = firstNoteOn;
            capSynced = false;
        }
        if (startAt >= 0)
        {
            const double barQ = num * 4.0 / den;
            capLen = juce::jlimit (1024, capBuf.getNumSamples(), (int) (capBars * barQ * 60.0 / bpm * sr));
            capPos = 0;
            capState.store (capRecording);
            record (startAt);
        }
    }
    else if (st == capRecording)
    {
        record (0);
    }

    // ---- THE GRAVE
    grave.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n, currentGraveParams());

    outL.store (juce::jmax (outL.load() * 0.8f, buffer.getMagnitude (0, 0, n)));
    outR.store (juce::jmax (outR.load() * 0.8f, buffer.getMagnitude (1, 0, n)));
}

// ------------------------------------------------------------------ message-thread side
void GdProcessor::setChoice (const juce::String& id, int index)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
        p->endChangeGesture();
    }
}

void GdProcessor::toggleCapture()
{
    const int st = capState.load();
    if (st == capArmed || st == capRecording) { capState.store (capIdle); capProgress.store (0); return; }
    if (st == capFinishing) return;
    const int bars[3] = { 4, 8, 16 };
    capBars = bars[juce::jlimit (0, 2, (int) std::lround (pCapLen->load()))];
    capBarsUi.store (capBars);
    capProgress.store (0);
    setChoice ("mode", 0);
    capState.store (capArmed);
}

void GdProcessor::handleAsyncUpdate()
{
    ensureLibraryLoaded();
    if (capState.load() == capFinishing) finishCapture();
    if (rechopPending.exchange (false)) rechop();
}

void GdProcessor::parameterChanged (const juce::String&, float)
{
    rechopPending.store (true);
    triggerAsyncUpdate();
}

void GdProcessor::finishCapture()
{
    Bank nb;
    nb.audio.setSize (2, capLen);
    for (int c = 0; c < 2; ++c) nb.audio.copyFrom (c, 0, capBuf, c, 0, capLen);
    nb.sr = sr; nb.len = capLen; nb.bars = capBars; nb.synced = capSynced;
    variant = 0;
    chopBank (nb, (int) std::lround (pChop->load()), variant);
    {
        juce::SpinLock::ScopedLockType l (bankLock);
        std::swap (bank, nb);
    }
    hasBones.store (bank.numBones > 0);
    bankVersion.fetch_add (1);
    capState.store (capIdle);
    capProgress.store (0);
    if (bank.numBones > 0) setChoice ("mode", 1);
}

void GdProcessor::rechop()
{
    if (! hasBones.load()) return;
    juce::SpinLock::ScopedLockType l (bankLock);
    chopBank (bank, (int) std::lround (pChop->load()), variant);
    bankVersion.fetch_add (1);
}

void GdProcessor::redig()
{
    ++variant;
    rechop();
}

void GdProcessor::getBankView (BankView& v)
{
    juce::SpinLock::ScopedLockType l (bankLock);
    v.numBones = bank.numBones;
    v.bars = bank.bars;
    v.seconds = bank.len / juce::jmax (1.0, bank.sr);
    v.fracs.clear();
    for (int i = 0; i <= bank.numBones; ++i) v.fracs.push_back ((double) bank.starts[(size_t) i] / juce::jmax (1, bank.len));
    v.env = bank.env;
    v.peaks = bank.peaks;
}

juce::File GdProcessor::exportBone (int idx)
{
    juce::AudioBuffer<float> src;
    double bsr = 44100.0;
    {
        juce::SpinLock::ScopedLockType l (bankLock);
        if (bank.numBones <= 0) return {};
        if (idx >= bank.numBones) return {};
        const int s0 = idx < 0 ? 0 : bank.starts[(size_t) idx];
        const int s1 = idx < 0 ? bank.len : bank.starts[(size_t) idx + 1];
        if (s1 - s0 < 8) return {};
        src.setSize (2, s1 - s0);
        for (int c = 0; c < 2; ++c) src.copyFrom (c, 0, bank.audio, c, s0, s1 - s0);
        bsr = bank.sr;
    }

    const bool rev = pReverse->load() > 0.5f;
    const float rate = std::pow (2.0f, -pSink->load() / 12.0f);
    const int len = src.getNumSamples();
    const int outLen = (int) ((double) (len - 1) / rate);
    const int tail = (int) (bsr * (pGhost->load() > 0.05f ? 2.0 : 0.3));
    juce::AudioBuffer<float> out (2, outLen + tail);
    out.clear();
    const float fade = (float) (0.002 * bsr);
    for (int i = 0; i < outLen; ++i)
    {
        double p = (double) i * rate;
        if (rev) p = (double) (len - 2) - p;
        const int i0 = juce::jlimit (0, len - 2, (int) p);
        const float fr = (float) (p - (double) i0);
        const float e = juce::jmin (1.0f, (float) i / fade, (float) (outLen - i) / fade);
        for (int c = 0; c < 2; ++c)
        {
            const float* s = src.getReadPointer (c);
            out.setSample (c, i, (s[i0] + fr * (s[i0 + 1] - s[i0])) * e);
        }
    }

    Grave gv;
    gv.prepare (bsr);
    const auto gp = currentGraveParams();
    for (int pos = 0; pos < out.getNumSamples(); pos += 512)
    {
        const int nn = juce::jmin (512, out.getNumSamples() - pos);
        gv.process (out.getWritePointer (0) + pos, out.getWritePointer (1) + pos, nn, gp);
    }

    int last = out.getNumSamples() - 1;
    while (last > outLen && std::abs (out.getSample (0, last)) < 0.0003f && std::abs (out.getSample (1, last)) < 0.0003f) --last;
    const int finalLen = juce::jmax (1, last + 1);

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Gravedigger");
    dir.createDirectory();
    const juce::String stamp = juce::Time::getCurrentTime().formatted ("%H%M%S");
    const juce::String name = idx < 0 ? "gravedigger_sample_" + stamp + ".wav"
                                      : "gravedigger_bone" + juce::String (idx + 1).paddedLeft ('0', 2) + "_" + stamp + ".wav";
    auto file = dir.getChildFile (name);
    file.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> os (file.createOutputStream());
    if (os == nullptr) return {};
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (os.get(), bsr, 2, 24, {}, 0));
    if (writer == nullptr) return {};
    os.release();
    writer->writeFromAudioSampleBuffer (out, 0, finalLen);
    writer.reset();
    return file;
}

// ------------------------------------------------------------------ state
void GdProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("GRAVEDIGGER");
    root.appendChild (apvts.copyState(), nullptr);
    {
        juce::SpinLock::ScopedLockType l (bankLock);
        if (bank.numBones > 0 && bank.len > 0)
        {
            juce::ValueTree b ("BANK");
            b.setProperty ("sr", bank.sr, nullptr);
            b.setProperty ("len", bank.len, nullptr);
            b.setProperty ("bars", bank.bars, nullptr);
            b.setProperty ("synced", bank.synced, nullptr);
            b.setProperty ("variant", variant, nullptr);
            juce::MemoryBlock mb ((size_t) bank.len * 2 * sizeof (int16_t));
            auto* d = static_cast<int16_t*> (mb.getData());
            const float* L = bank.audio.getReadPointer (0);
            const float* R = bank.audio.getReadPointer (1);
            for (int i = 0; i < bank.len; ++i)
            {
                d[2 * i]     = (int16_t) juce::jlimit (-32767.0f, 32767.0f, L[i] * 32767.0f);
                d[2 * i + 1] = (int16_t) juce::jlimit (-32767.0f, 32767.0f, R[i] * 32767.0f);
            }
            b.setProperty ("pcm", juce::var (mb), nullptr);
            root.appendChild (b, nullptr);
        }
    }
    juce::MemoryOutputStream mos (destData, false);
    root.writeToStream (mos);
}

void GdProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto root = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! root.isValid()) return;
    auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid()) apvts.replaceState (params);

    auto b = root.getChildWithName ("BANK");
    if (! b.isValid()) return;
    auto* mb = b.getProperty ("pcm").getBinaryData();
    const int len = (int) b.getProperty ("len", 0);
    if (mb == nullptr || len <= 0 || mb->getSize() < (size_t) len * 2 * sizeof (int16_t)) return;

    Bank nb;
    nb.sr = (double) b.getProperty ("sr", 44100.0);
    nb.len = len;
    nb.bars = (int) b.getProperty ("bars", 8);
    nb.synced = (bool) b.getProperty ("synced", false);
    nb.audio.setSize (2, len);
    const auto* d = static_cast<const int16_t*> (mb->getData());
    for (int i = 0; i < len; ++i)
    {
        nb.audio.setSample (0, i, d[2 * i] / 32767.0f);
        nb.audio.setSample (1, i, d[2 * i + 1] / 32767.0f);
    }
    variant = (int) b.getProperty ("variant", 0);
    chopBank (nb, (int) std::lround (pChop->load()), variant);
    {
        juce::SpinLock::ScopedLockType l (bankLock);
        std::swap (bank, nb);
    }
    hasBones.store (bank.numBones > 0);
    bankVersion.fetch_add (1);
}

juce::AudioProcessorEditor* GdProcessor::createEditor() { return new GdEditor (*this); }
} // namespace gd

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new gd::GdProcessor(); }
