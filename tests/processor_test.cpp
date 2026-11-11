#pragma GCC diagnostic ignored "-Wfloat-equal"
// Prueba del procesador de MINDROLL (sin interfaz): carga, PREVIEW por processBlock, CAPTURE y guardar/abrir proyecto.
#include "../Source/PluginProcessor.h"
#include <cstdio>

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++fails; std::printf ("  FALLO: " __VA_ARGS__); std::printf ("\n"); } } while (0)

static juce::AudioBuffer<float> vocal (double sr)
{
    juce::AudioBuffer<float> b (2, (int) (sr * 0.6)); b.clear();
    for (int i = (int) (sr * 0.05); i < b.getNumSamples(); ++i)
    {
        const double t = i / sr;
        const float v = (float) (0.4 * std::sin (juce::MathConstants<double>::twoPi * 230.0 * t) * std::exp (-t * 2.0));
        b.setSample (0, i, v); b.setSample (1, i, v);
    }
    return b;
}

static juce::MemoryBlock wavBytes (const juce::AudioBuffer<float>& b, double sr)
{
    juce::MemoryBlock mb;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::MemoryOutputStream (mb, false), sr, 2, 24, {}, 0));
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    return mb;
}

namespace juce::detail { bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages); }
static void pump (int ms)   // deja correr los timers y mensajes del procesador (solo Linux)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
        if (! juce::detail::dispatchNextMessageOnSystemQueue (true)) juce::Thread::sleep (5);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::printf ("MINDROLL · prueba del procesador\n");
    const double SR = 48000.0; const int BS = 512;
    auto make = [&] { auto p = std::make_unique<MOProcessor>(); p->setPlayConfigDetails (2, 2, SR, BS); p->prepareToPlay (SR, BS); return p; };
    auto p = make();
    juce::MidiBuffer midi;

    CHECK (! p->hasBuild(), "sin vocal no debería haber build");
    const auto vb = vocal (44100.0);
    CHECK (p->loadBytes (wavBytes (vb, 44100.0), "hey_chop.wav").isEmpty(), "no carga el WAV");
    CHECK (p->hasSource() && p->hasBuild(), "tras cargar debería haber build");
    CHECK (p->loadBytes (juce::MemoryBlock ("hola", 4), "x.txt").isNotEmpty(), "debería rechazar un archivo que no es audio");
    CHECK (p->hasSource(), "un archivo malo no debe borrar la vocal buena");

    // la vocal de la pista pasa tal cual
    {
        juce::AudioBuffer<float> b (2, BS);
        for (int i = 0; i < BS; ++i) { b.setSample (0, i, 0.25f); b.setSample (1, i, -0.25f); }
        p->processBlock (b, midi);
        CHECK (b.getSample (0, 100) == 0.25f && b.getSample (1, 100) == -0.25f, "la pista no pasa limpia sin PREVIEW");
    }

    // PLAY arranca ya, suena y se para solo al terminar el build (2 compases a 124 = 3,87 s)
    p->togglePlay();
    float peak = 0; int blocks = 0, sounding = 0;
    for (; blocks < 1000; ++blocks)
    {
        juce::AudioBuffer<float> b (2, BS); b.clear();
        p->processBlock (b, midi);
        const float m = b.getMagnitude (0, 0, BS);
        peak = std::max (peak, m); if (m > 1e-4f) ++sounding;
        if (p->previewState() == 0) break;
    }
    const double secs = (blocks + 1) * BS / SR;
    CHECK (sounding > 50, "PREVIEW no suena (%d bloques con señal)", sounding);
    CHECK (std::abs (secs - 8 * 60.0 / 124.0) < 0.05, "PREVIEW no dura el build (%.2f s)", secs);
    CHECK (peak <= 0.4f + 1e-3f, "PREVIEW más fuerte que la vocal (%.3f)", peak);

    // PAUSE guarda la posición y PLAY sigue desde ahí; STOP vuelve al principio
    {
        juce::AudioBuffer<float> b (2, BS);
        p->togglePlay();
        for (int k = 0; k < 40; ++k) { b.clear(); p->processBlock (b, midi); }
        const float at = p->previewProgress();
        p->togglePlay();   // pausa
        CHECK (p->previewState() == 2, "PAUSE no pausa");
        for (int k = 0; k < 20; ++k) { b.clear(); p->processBlock (b, midi); }
        CHECK (std::abs (p->previewProgress() - at) < 1e-6f && b.getMagnitude (0, 0, BS) == 0.0f, "en pausa sigue avanzando o sonando");
        p->togglePlay();
        for (int k = 0; k < 5; ++k) { b.clear(); p->processBlock (b, midi); }
        CHECK (p->previewProgress() > at, "PLAY no sigue desde la pausa");
        p->stopPlay();
        b.clear(); p->processBlock (b, midi);
        CHECK (p->previewState() == 0 && p->previewProgress() == 0.0f && b.getMagnitude (0, 0, BS) == 0.0f, "STOP no para ni vuelve al principio");
    }

    // la línea se guarda con el proyecto
    p->setLine ({ { 0.0f, 0.3f }, { 0.5f, 0.3f }, { 1.0f, 0.9f } });
    pump (300);

    // cambiar LENGTH re-renderiza (el timer del procesador)
    p->setParam ("bars", 3);
    pump (400);
    {
        const auto f = p->exportBuild();
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        CHECK (r != nullptr && std::abs (r->lengthInSamples / SR - 32 * 60.0 / 124.0) < 0.01, "el WAV exportado no son 8 compases");
        CHECK (f.getFileName() == "hey_chop_ROLL_8BAR_124BPM.wav", "nombre del WAV: %s", f.getFileName().toRawUTF8());
    }

    // guardar y abrir el proyecto: vuelve la vocal y los ajustes
    {
        juce::MemoryBlock st; p->getStateInformation (st);
        auto q = make();
        q->setStateInformation (st.getData(), (int) st.getSize());
        pump (300);
        CHECK (q->hasSource() && q->hasBuild(), "al abrir el proyecto no vuelve la vocal");
        CHECK ((int) q->apvts.getRawParameterValue ("bars")->load() == 3, "al abrir el proyecto no vuelve LENGTH");
        CHECK (q->lineVar().size() == 3 && std::abs ((double) q->lineVar()[2][1] - 0.9) < 1e-3, "al abrir el proyecto no vuelve la línea");
    }

    // CAPTURE (sin host: empieza cuando suena la vocal) graba 1,25 tiempos de la pista
    {
        auto c = make();
        c->toggleCapture();
        CHECK (c->captureState() == 1, "CAPTURE no se arma");
        juce::AudioBuffer<float> b (2, BS); b.clear();
        for (int k = 0; k < 10; ++k) { b.clear(); c->processBlock (b, midi); }
        CHECK (c->captureState() == 1, "CAPTURE empieza a grabar sin señal");
        const auto v = vocal (SR);
        for (int pos = 0; pos < 4 * (int) SR && c->captureState() != 0; pos += BS)
        {
            b.clear();
            if (pos + BS <= v.getNumSamples()) for (int ch = 0; ch < 2; ++ch) b.copyFrom (ch, 0, v, ch, pos, BS);
            c->processBlock (b, midi);
            if (c->captureState() == 3) pump (100);
        }
        pump (300);
        CHECK (c->captureState() == 0 && c->hasSource() && c->hasBuild(), "CAPTURE no deja la vocal capturada");
        juce::MemoryBlock st; c->getStateInformation (st);
        auto q = make(); q->setStateInformation (st.getData(), (int) st.getSize()); pump (300);
        CHECK (q->hasSource(), "la captura no se guarda con el proyecto");
    }

    std::printf ("%d comprobaciones, %d fallos\n", checks, fails);
    return fails == 0 ? 0 : 1;
}
