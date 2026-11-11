#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "RollEngine.h"

// MINDROLL: efecto que va en la pista de la vocal. Deja pasar la vocal tal cual y, aparte,
// genera el build (roll) con el trozo cargado o capturado: se escucha con PREVIEW y se arrastra como WAV.
class MOProcessor : public juce::AudioProcessor,
                    private juce::AsyncUpdater,
                    private juce::Timer,
                    private juce::AudioProcessorValueTreeState::Listener
{
public:
    MOProcessor();
    ~MOProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    juce::AudioProcessorValueTreeState apvts;

    // ---- hilo de mensajes ----
    void setParam (const juce::String& id, float value);
    void gesture (const juce::String& id, bool begin);
    juce::var stateVar() const;          // parámetros para la interfaz
    juce::var sourceInfo() const;        // nombre, duración y forma de onda del trozo
    juce::var planVar() const;           // los golpes del build, para dibujarlos
    juce::String loadBytes (const juce::MemoryBlock&, const juce::String& name);   // "" si va bien
    void clearSource();
    void toggleCapture();
    void togglePreview();
    juce::File exportBuild();            // WAV del build para arrastrar a Ableton
    bool hasSource() const { return source.valid(); }
    bool hasBuild() const { return build != nullptr; }
    bool isStandalone() const { return wrapperType == wrapperType_Standalone; }
    double currentBpm() const;

    int captureState() const { return capState.load(); }      // 0 nada, 1 esperando, 2 grabando
    int previewState() const { return prevState.load(); }     // 0 parado, 1 esperando el compás, 2 sonando
    float previewProgress() const;

    std::function<void()> onSourceChanged, onPlanChanged;
    std::function<void (const juce::String&)> onToast;

    bool lightTheme = false;
    void saveGlobal();

private:
    struct Build : juce::ReferenceCountedObject
    {
        juce::AudioBuffer<float> buf;
        double bpm = 124, sr = 48000;
        using Ptr = juce::ReferenceCountedObjectPtr<Build>;
    };

    void parameterChanged (const juce::String&, float) override { dirty = true; }
    void handleAsyncUpdate() override;   // captura terminada
    void timerCallback() override;       // re-render cuando cambia algo
    void rerender();
    mroll::Settings settings() const;
    void loadGlobal();
    bool setSourceFromBuffer (const juce::AudioBuffer<float>&, double sr, const juce::String& name);

    mroll::Source source;
    juce::String sourceName;
    juce::MemoryBlock sourceBytes;       // el archivo original (o la captura en WAV) para guardarlo con el proyecto
    std::vector<float> wave;

    Build::Ptr build;                    // el build actual (solo hilo de mensajes)
    std::vector<Build::Ptr> retired;     // builds viejos que el audio aún puede estar leyendo
    std::atomic<Build*> active { nullptr };   // lo que reproduce PREVIEW
    juce::SpinLock buildLock;

    std::atomic<bool> dirty { true };
    std::atomic<double> hostBpm { 124.0 };
    std::atomic<bool> hostInfo { false };
    double sampleRateNow = 48000.0;

    // captura
    juce::AudioBuffer<float> capBuf;
    std::atomic<int> capState { 0 };
    int capWritten = 0, capTarget = 0;
    bool capDone = false;

    // preview
    std::atomic<int> prevState { 0 };
    std::atomic<int> prevPos { 0 };
    Build* playing = nullptr;

    std::atomic<float>* pBars = nullptr; std::atomic<float>* pCurve = nullptr;
    std::atomic<float>* pG0 = nullptr; std::atomic<float>* pG1 = nullptr;
    std::atomic<float>* pPitch = nullptr; std::atomic<float>* pFilter = nullptr;
    std::atomic<float>* pRev = nullptr; std::atomic<float>* pSbpm = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MOProcessor)
};
