#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    // Colores de MINDROLL en la skin v2: amarillo mantequilla con sombra mostaza.
    const mind::SkinColours kSkin { juce::Colour (0xfffdf3c4), juce::Colour (0xfff1d98a), juce::Colour (0xff3a2c06), juce::Colour (0xffa8862f) };

    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url)
    {
        const auto path = url.upToFirstOccurrenceOf ("?", false, false);
        if (path != "/" && path != "/index.html" && path.isNotEmpty())
            return std::nullopt;
        const auto* begin = reinterpret_cast<const std::byte*> (BinaryData::index_html);
        std::vector<std::byte> data (begin, begin + BinaryData::index_htmlSize);
        return juce::WebBrowserComponent::Resource { std::move (data), "text/html" };
    }
}

MOEditor::MOEditor (MOProcessor& p)
    : AudioProcessorEditor (&p), proc (p), bar (kSkin)
{
    frame = juce::ImageCache::getFromMemory (BinaryData::frame_jpg, BinaryData::frame_jpgSize);
    bar.font = juce::Typeface::createSystemTypefaceFor (BinaryData::NunitoExtraBold_ttf, BinaryData::NunitoExtraBold_ttfSize);
    bar.label = "DRAG THE BUILD TO ABLETON";
    bar.dropLabel = "DROP TO LOAD THE VOCAL";

    auto options = juce::WebBrowserComponent::Options{}
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([] (const juce::String& url) { return getResource (url); })
        .withEventListener ("moReady", [this] (const juce::var&) { ready = true; lastState = {}; sendSettings(); sendSource(); sendPlan(); })
        .withEventListener ("moSet", [this] (const juce::var& v) { proc.setParam (v.getProperty ("id", "").toString(), (float) (double) v.getProperty ("v", 0.0)); })
        .withEventListener ("moGesture", [this] (const juce::var& v) { proc.gesture (v.getProperty ("id", "").toString(), (bool) v.getProperty ("begin", false)); })
        .withEventListener ("moLoad", [this] (const juce::var& v)
        {
            juce::MemoryOutputStream out;
            if (! juce::Base64::convertFromBase64 (out, v.getProperty ("data", "").toString())) { toast ("Could not read that file"); return; }
            const auto err = proc.loadBytes (out.getMemoryBlock(), v.getProperty ("name", "vocal").toString());
            if (err.isNotEmpty()) toast (err);
        })
        .withEventListener ("moBrowse", [this] (const juce::var&)
        {
            chooser = std::make_unique<juce::FileChooser> ("Load a vocal", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                           "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a;*.caf");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) loadFile (fc.getResult()); });
        })
        .withEventListener ("moClear", [this] (const juce::var&) { proc.clearSource(); })
        .withEventListener ("moCapture", [this] (const juce::var&) { proc.toggleCapture(); })
        .withEventListener ("moPlay", [this] (const juce::var&) { proc.togglePlay(); })
        .withEventListener ("moStop", [this] (const juce::var&) { proc.stopPlay(); })
        .withEventListener ("moLine", [this] (const juce::var& v)
        {
            mroll::Line l;
            if (auto* a = v.getProperty ("pts", juce::var()).getArray())
                for (auto& pt : *a)
                    if (pt.isArray() && pt.size() >= 2) l.push_back ({ (float) (double) pt[0], (float) (double) pt[1] });
            proc.setLine (l);
        })
        .withEventListener ("moSettings", [this] (const juce::var& v)
        {
            if (v.hasProperty ("theme")) proc.lightTheme = v.getProperty ("theme", "dark").toString() == "light";
            repaint();
            proc.saveGlobal();
        });

    bar.makeFile = [this]
    {
        if (! proc.hasBuild()) { toast ("Load or capture a vocal first"); return juce::File(); }
        auto f = proc.exportBuild();
        if (! f.existsAsFile()) toast ("Could not write the WAV");
        return f;
    };
    bar.onDrop = [this] (const juce::File& f) { loadFile (f); };

    proc.onSourceChanged = [this] { sendSource(); };
    proc.onPlanChanged = [this] { sendPlan(); };
    proc.onToast = [this] (const juce::String& m) { toast (m); };

    web = std::make_unique<juce::WebBrowserComponent> (options);
    addAndMakeVisible (*web);
    addAndMakeVisible (bar);
    web->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    setResizable (true, true);
    const double ratio = (double) kW / (kWebH + kBarH);
    setResizeLimits (800, (int) (800 / ratio), 2400, (int) (2400 / ratio));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio (ratio);
    setSize (kW, kWebH + kBarH);
    bar.enabled = proc.hasBuild();
    startTimerHz (30);
}

MOEditor::~MOEditor()
{
    stopTimer();
    proc.onSourceChanged = nullptr; proc.onPlanChanged = nullptr; proc.onToast = nullptr;
}

void MOEditor::loadFile (const juce::File& f)
{
    juce::MemoryBlock mb;
    if (! f.loadFileAsData (mb)) { toast ("Could not open that file"); return; }
    const auto err = proc.loadBytes (mb, f.getFileName());
    if (err.isNotEmpty()) toast (err);
}

void MOEditor::paint (juce::Graphics& g)
{
    // El marco moteado sigue por detrás de la barra de arrastre, con su línea negra por fuera.
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    if (frame.isValid()) g.drawImage (frame, getLocalBounds().toFloat(), juce::RectanglePlacement::fillDestination);
    else g.fillAll (juce::Colour (0xff0b0a0e));
    g.setColour (juce::Colours::black);
    g.drawRect (getLocalBounds(), 3);
}

void MOEditor::resized()
{
    const float s = (float) getWidth() / (float) kW;
    const int webH = juce::roundToInt (kWebH * s);
    if (web != nullptr) web->setBounds (0, 0, getWidth(), webH);
    const int side = juce::roundToInt (35 * s);
    bar.setBounds (side, webH + juce::roundToInt (2 * s), getWidth() - 2 * side, getHeight() - webH - juce::roundToInt (21 * s));
}

void MOEditor::emit (const juce::Identifier& ev, const juce::var& v)
{
    if (web != nullptr) web->emitEventIfBrowserIsVisible (ev, v);
}

void MOEditor::toast (const juce::String& msg)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("msg", msg);
    emit ("moToast", juce::var (o));
}

void MOEditor::sendSettings()
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("theme", proc.lightTheme ? "light" : "dark");
    o->setProperty ("standalone", proc.isStandalone());
    emit ("moSettings", juce::var (o));
}

void MOEditor::sendSource() { emit ("moSource", proc.sourceInfo()); }

void MOEditor::sendPlan()
{
    bar.enabled = proc.hasBuild(); bar.repaint();
    emit ("moPlan", proc.planVar());
}

void MOEditor::timerCallback()
{
    if (! ready) return;
    auto* o = new juce::DynamicObject();
    const auto st = proc.stateVar();
    const auto js = juce::JSON::toString (st, true);
    if (js != lastState) { lastState = js; o->setProperty ("p", st); }
    o->setProperty ("cap", proc.captureState());
    o->setProperty ("prev", proc.previewState());
    o->setProperty ("pos", std::round (proc.previewProgress() * 1000.0f) / 1000.0f);
    o->setProperty ("bpm", std::round (proc.currentBpm() * 10.0) / 10.0);
    emit ("moState", juce::var (o));
}
