#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

static const juce::StringArray kBars { "1", "2", "4", "8" };

APVTS::ParameterLayout MOProcessor::layout()
{
    APVTS::ParameterLayout l;
    using PID = juce::ParameterID;
    auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100)) + " %"; });
    l.add (std::make_unique<juce::AudioParameterChoice> (PID { "bars", 1 }, "Length (bars)", kBars, 1));
    l.add (std::make_unique<juce::AudioParameterFloat> (PID { "gate0", 1 }, "Gate Start", juce::NormalisableRange<float> (0.05f, 1.0f), 0.35f, pct));
    l.add (std::make_unique<juce::AudioParameterFloat> (PID { "gate1", 1 }, "Gate End", juce::NormalisableRange<float> (0.05f, 1.0f), 1.0f, pct));
    l.add (std::make_unique<juce::AudioParameterInt> (PID { "pitch", 1 }, "Pitch Up", 0, 12, 0, juce::AudioParameterIntAttributes().withLabel ("st")));
    l.add (std::make_unique<juce::AudioParameterFloat> (PID { "filter", 1 }, "Filter", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct));
    l.add (std::make_unique<juce::AudioParameterBool> (PID { "reverse", 1 }, "Reverse Last Hit", false));
    l.add (std::make_unique<juce::AudioParameterFloat> (PID { "sbpm", 1 }, "BPM (standalone)", juce::NormalisableRange<float> (60.0f, 200.0f, 0.1f), 124.0f));
    return l;
}

MOProcessor::MOProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MINDROLL", layout())
{
    pBars = apvts.getRawParameterValue ("bars");
    pG0 = apvts.getRawParameterValue ("gate0");    pG1 = apvts.getRawParameterValue ("gate1");
    pPitch = apvts.getRawParameterValue ("pitch"); pFilter = apvts.getRawParameterValue ("filter");
    pRev = apvts.getRawParameterValue ("reverse"); pSbpm = apvts.getRawParameterValue ("sbpm");
    for (auto id : { "bars", "gate0", "gate1", "pitch", "filter", "reverse", "sbpm" }) apvts.addParameterListener (id, this);
    loadGlobal();
    startTimerHz (10);
}

MOProcessor::~MOProcessor()
{
    stopTimer();
    cancelPendingUpdate();
    for (auto id : { "bars", "gate0", "gate1", "pitch", "filter", "reverse", "sbpm" }) apvts.removeParameterListener (id, this);
}

// ------------------------------------------------------------------ ajustes globales (tema)
static juce::File globalFile()
{
    auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support").getChildFile ("MINDROLL");
    d.createDirectory();
    return d.getChildFile ("settings.json");
}

void MOProcessor::loadGlobal()
{
    auto v = juce::JSON::parse (globalFile().loadFileAsString());
    if (v.isObject()) lightTheme = v.getProperty ("theme", "dark").toString() == "light";
}

void MOProcessor::saveGlobal()
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("theme", lightTheme ? "light" : "dark");
    globalFile().replaceWithText (juce::JSON::toString (juce::var (o), true));
}

// ------------------------------------------------------------------ audio
void MOProcessor::prepareToPlay (double sampleRate, int)
{
    if (std::abs (sampleRate - sampleRateNow) > 1.0) dirty = true;
    sampleRateNow = sampleRate;
    capBuf.setSize (2, (int) (sampleRate * 4.0) + 16);
    capState = 0; prevState = 0; playing = nullptr;
}

bool MOProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

double MOProcessor::currentBpm() const
{
    return hostInfo.load() && ! isStandalone() ? hostBpm.load() : (double) pSbpm->load();
}

void MOProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    midi.clear();
    const int n = buffer.getNumSamples();
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch) buffer.clear (ch, 0, n);

    double bpm = pSbpm->load(), ppq = 0;
    bool playingHost = false, info = false;
    if (! isStandalone())
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (auto b = pos->getBpm()) { bpm = *b; info = true; }
                if (auto q = pos->getPpqPosition()) ppq = *q; else info = false;
                playingHost = pos->getIsPlaying() && info;
            }
    hostInfo = info;
    if (info) hostBpm = bpm;
    const double spb = getSampleRate() * 60.0 / juce::jlimit (40.0, 250.0, bpm);

    // ---- CAPTURE: graba el siguiente tiempo de la pista (o en cuanto suene algo si está parada) ----
    int cs = capState.load();
    if (cs == 1)
    {
        int startAt = -1;
        if (playingHost)
        {
            const double nb = std::ceil (ppq - 1.0e-9);
            const int off = (int) std::round ((nb - ppq) * spb);
            if (off < n) startAt = juce::jmax (0, off);
        }
        else
        {
            for (int i = 0; i < n && startAt < 0; ++i)
                for (int ch = 0; ch < getTotalNumInputChannels(); ++ch)
                    if (std::abs (buffer.getSample (ch, i)) > 0.01f) { startAt = i; break; }
        }
        if (startAt >= 0)
        {
            capWritten = 0;
            capTarget = juce::jlimit ((int) (getSampleRate() * 0.2), capBuf.getNumSamples() - 1, (int) (spb * 1.25));
            capState = cs = 2;
            const int m = juce::jmin (n - startAt, capTarget);
            for (int ch = 0; ch < 2; ++ch) capBuf.copyFrom (ch, 0, buffer, juce::jmin (ch, getTotalNumInputChannels() - 1), startAt, m);
            capWritten = m;
        }
    }
    else if (cs == 2)
    {
        const int m = juce::jmin (n, capTarget - capWritten);
        for (int ch = 0; ch < 2; ++ch) capBuf.copyFrom (ch, capWritten, buffer, juce::jmin (ch, getTotalNumInputChannels() - 1), 0, m);
        capWritten += m;
    }
    if (capState.load() == 2 && capWritten >= capTarget) { capState = 3; triggerAsyncUpdate(); }

    // ---- PLAY: el build suena encima de la pista desde donde se quedó (PAUSE guarda la posición) ----
    if (prevState.load() == 1)
    {
        auto* now = active.load();
        if (now == nullptr) { prevState = 0; prevPos = 0; playing = nullptr; return; }
        playing = now;   // si se re-renderiza mientras suena, sigue con el build nuevo
        int pos = prevPos.load();
        const int len = playing->buf.getNumSamples();
        if (pos >= len) pos = 0;
        const int m = juce::jmin (n, len - pos);
        for (int ch = 0; ch < 2; ++ch) buffer.addFrom (ch, 0, playing->buf, ch, pos, m);
        pos += m;
        if (pos >= len) { prevState = 0; prevPos = 0; playing = nullptr; }
        else prevPos = pos;
    }
    else playing = nullptr;
}

// ------------------------------------------------------------------ hilo de mensajes
mroll::Settings MOProcessor::settings() const
{
    mroll::Settings s;
    static const int bars[] { 1, 2, 4, 8 };
    s.bars = bars[juce::jlimit (0, 3, (int) pBars->load())];
    s.line = line;
    s.gateStart = pG0->load(); s.gateEnd = pG1->load();
    s.pitch = std::round (pPitch->load());
    s.filter = pFilter->load();
    s.reverse = pRev->load() > 0.5f;
    return s;
}

void MOProcessor::timerCallback()
{
    if (build != nullptr && std::abs (build->bpm - currentBpm()) > 0.01) dirty = true;
    // los builds viejos se liberan cuando lleva parado dos vueltas del timer (el audio ya no los lee)
    stoppedTicks = prevState.load() == 0 ? stoppedTicks + 1 : 0;
    if (stoppedTicks >= 2 && ! retired.empty()) retired.clear();
    if (dirty.exchange (false)) rerender();
}

void MOProcessor::rerender()
{
    if (! source.valid())
    {
        if (build != nullptr) { retired.push_back (build); build = nullptr; active = nullptr; }
        if (onPlanChanged) onPlanChanged();
        return;
    }
    Build::Ptr b = new Build();
    b->bpm = currentBpm(); b->sr = sampleRateNow;
    b->buf = mroll::render (source, settings(), b->bpm, b->sr);
    if (build != nullptr) retired.push_back (build);
    build = b;
    active = b.get();
    if (onPlanChanged) onPlanChanged();
}

void MOProcessor::handleAsyncUpdate()
{
    if (capState.load() != 3) return;
    juce::AudioBuffer<float> cap (2, capWritten);
    for (int ch = 0; ch < 2; ++ch) cap.copyFrom (ch, 0, capBuf, ch, 0, capWritten);
    capState = 0;
    if (! setSourceFromBuffer (cap, getSampleRate(), "Captured vocal"))
    {
        if (onToast) onToast ("Nothing came in from the track. Play the vocal and try again");
        if (onSourceChanged) onSourceChanged();
        return;
    }
    // Se guarda la captura como WAV para que vuelva al abrir el proyecto.
    sourceBytes.reset();
    {
        juce::WavAudioFormat wav;
        auto* mos = new juce::MemoryOutputStream (sourceBytes, false);
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (mos, getSampleRate(), 2, 24, {}, 0));
        if (w != nullptr) w->writeFromAudioSampleBuffer (cap, 0, cap.getNumSamples());
        else delete mos;
    }
    if (onToast) onToast ("Captured");
}

bool MOProcessor::setSourceFromBuffer (const juce::AudioBuffer<float>& in, double sr, const juce::String& name)
{
    auto s = mroll::prepareSource (in, sr);
    if (! s.valid()) return false;
    source = std::move (s);
    sourceName = name;
    // forma de onda para la interfaz
    wave.assign (240, 0.0f);
    const int n = source.buf.getNumSamples();
    for (int i = 0; i < 240; ++i)
    {
        const int a = (int) ((int64_t) n * i / 240), b = juce::jmax (a + 1, (int) ((int64_t) n * (i + 1) / 240));
        wave[(size_t) i] = juce::jmax (source.buf.getMagnitude (0, a, b - a), source.buf.getMagnitude (1, a, b - a)) / juce::jmax (1.0e-4f, source.peak);
    }
    dirty = false;
    rerender();
    if (onSourceChanged) onSourceChanged();
    return true;
}

juce::String MOProcessor::loadBytes (const juce::MemoryBlock& mb, const juce::String& name)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (std::make_unique<juce::MemoryInputStream> (mb, false)));
    if (r == nullptr) return "That file is not audio MINDROLL can read";
    const int len = (int) juce::jmin<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 10.0));
    if (len <= 16) return "That file is empty";
    juce::AudioBuffer<float> b ((int) juce::jlimit (1u, 2u, r->numChannels), len);
    r->read (&b, 0, len, 0, true, b.getNumChannels() > 1);
    if (! setSourceFromBuffer (b, r->sampleRate, juce::File::createFileWithoutCheckingPath (name).getFileNameWithoutExtension()))
        return "That file is silent";
    sourceBytes = mb;
    return {};
}

void MOProcessor::clearSource()
{
    prevState = 0;
    source = {};
    sourceName = {}; sourceBytes.reset(); wave.clear();
    rerender();
    if (onSourceChanged) onSourceChanged();
}

void MOProcessor::toggleCapture()
{
    if (capState.load() == 0) capState = 1;
    else capState = 0;
}

void MOProcessor::togglePlay()
{
    if (prevState.load() == 1) { prevState = 2; return; }
    if (build == nullptr) { if (onToast) onToast (source.valid() ? "Rendering, try again" : "Load or capture a vocal first"); return; }
    if (prevState.load() == 0) prevPos = 0;
    prevState = 1;
}

void MOProcessor::stopPlay() { prevState = 0; prevPos = 0; }

void MOProcessor::setLine (const mroll::Line& l)
{
    line = mroll::sanitise (l);
    dirty = true;
}

juce::var MOProcessor::lineVar() const
{
    juce::Array<juce::var> a;
    for (auto [x, y] : line) a.add (juce::var (juce::Array<juce::var> { std::round (x * 1000.0f) / 1000.0f, std::round (y * 1000.0f) / 1000.0f }));
    return juce::var (a);
}

float MOProcessor::previewProgress() const
{
    auto* b = active.load();
    if (prevState.load() == 0 || b == nullptr) return 0.0f;
    return (float) prevPos.load() / (float) juce::jmax (1, b->buf.getNumSamples());
}

juce::File MOProcessor::exportBuild()
{
    if (build == nullptr) return {};
    const auto s = settings();
    auto base = sourceName.isNotEmpty() ? sourceName : juce::String ("vocal");
    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("MINDROLL").getChildFile ("Drag");
    dir.createDirectory();
    auto f = dir.getChildFile (juce::File::createLegalFileName (base + "_ROLL_" + juce::String (s.bars) + "BAR_"
                                                                + juce::String (juce::roundToInt (build->bpm)) + "BPM.wav"));
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    if (os == nullptr) return {};
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), build->sr, 2, 24, {}, 0));
    if (w == nullptr) return {};
    os.release();
    w->writeFromAudioSampleBuffer (build->buf, 0, build->buf.getNumSamples());
    w.reset();
    return f;
}

// ------------------------------------------------------------------ interfaz
void MOProcessor::setParam (const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
    {
        const float norm = p->convertTo0to1 (value);
        if (std::abs (norm - p->getValue()) > 1.0e-6f) p->setValueNotifyingHost (norm);
    }
}

void MOProcessor::gesture (const juce::String& id, bool begin)
{
    if (auto* p = apvts.getParameter (id)) { if (begin) p->beginChangeGesture(); else p->endChangeGesture(); }
}

juce::var MOProcessor::stateVar() const
{
    auto* o = new juce::DynamicObject();
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            o->setProperty (rp->getParameterID(), rp->convertFrom0to1 (rp->getValue()));
    return juce::var (o);
}

juce::var MOProcessor::sourceInfo() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("loaded", source.valid());
    if (! source.valid()) return juce::var (o);
    o->setProperty ("name", sourceName);
    o->setProperty ("dur", source.buf.getNumSamples() / source.sr);
    juce::Array<juce::var> w; for (auto v : wave) w.add (std::round (v * 1000.0f) / 1000.0f);
    o->setProperty ("wave", w);
    return juce::var (o);
}

juce::var MOProcessor::planVar() const
{
    auto* o = new juce::DynamicObject();
    const auto s = settings();
    o->setProperty ("total", mroll::totalBeats (s));
    o->setProperty ("bpm", std::round (currentBpm() * 10.0) / 10.0);
    juce::Array<juce::var> hs;
    double longest = 0;
    for (auto& h : mroll::planHits (s))
    {
        juce::Array<juce::var> a { h.beat, h.len, std::round (h.gate * 1000.0) / 1000.0, std::round (h.semis * 100.0) / 100.0, h.rev ? 1 : 0, std::round (h.rate * 100.0) / 100.0 };
        hs.add (juce::var (a));
        longest = juce::jmax (longest, h.len * h.gate);
    }
    o->setProperty ("hits", hs);
    o->setProperty ("line", lineVar());
    // parte del trozo que llega a sonar (el golpe más largo)
    if (source.valid()) o->setProperty ("use", juce::jmin (1.0, longest * 60.0 / currentBpm() / (source.buf.getNumSamples() / source.sr)));
    return juce::var (o);
}

// ------------------------------------------------------------------ estado
void MOProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    juce::StringArray pts; for (auto [x, y] : line) pts.add (juce::String (x, 4) + "," + juce::String (y, 4));
    st.setProperty ("line", pts.joinIntoString (";"), nullptr);
    if (source.valid() && sourceBytes.getSize() > 0 && sourceBytes.getSize() < 40u * 1024u * 1024u)
    {
        st.setProperty ("sname", sourceName, nullptr);
        st.setProperty ("sdata", sourceBytes.toBase64Encoding(), nullptr);
    }
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void MOProcessor::setStateInformation (const void* d, int size)
{
    auto xml = getXmlFromBinary (d, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto st = juce::ValueTree::fromXml (*xml);
    const auto name = st.getProperty ("sname", "").toString();
    const auto b64 = st.getProperty ("sdata", "").toString();
    const auto ln = st.getProperty ("line", "").toString();
    st.removeProperty ("sname", nullptr); st.removeProperty ("sdata", nullptr); st.removeProperty ("line", nullptr);
    if (ln.isNotEmpty())
    {
        mroll::Line l;
        for (auto& p : juce::StringArray::fromTokens (ln, ";", ""))
            l.push_back ({ p.upToFirstOccurrenceOf (",", false, false).getFloatValue(), p.fromFirstOccurrenceOf (",", false, false).getFloatValue() });
        line = mroll::sanitise (l);
    }
    apvts.replaceState (st);
    if (b64.isNotEmpty())
    {
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (b64)) loadBytes (mb, name + ".wav");
    }
    dirty = true;
}

juce::AudioProcessorEditor* MOProcessor::createEditor() { return new MOEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MOProcessor(); }
