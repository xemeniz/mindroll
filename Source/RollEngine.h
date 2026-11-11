#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

// Motor de MINDROLL: coge un trozo de vocal y lo repite cada vez más rápido (1/4 → 1/8 → 1/16 → 1/32)
// hasta el drop. Todo se calcula offline: el plugin renderiza el build entero y lo reproduce o lo exporta.
namespace mroll
{
    enum Curve { Linear = 0, Expo = 1, Late = 2 };

    struct Settings
    {
        int bars = 2;            // 1, 2, 4 u 8 compases (4/4)
        int curve = Expo;        // cómo se reparte el tiempo entre 1/4, 1/8, 1/16 y 1/32
        float gateStart = 0.35f; // trozo que suena en cada golpe al principio (0-1 del paso)
        float gateEnd = 1.0f;    // ... y al final, justo antes del drop
        float pitch = 0.0f;      // semitonos que sube al final (0-12)
        float filter = 0.5f;     // 0 = sin filtro; 1 = empieza muy cerrado (pasa-bajos que se abre)
        bool reverse = false;    // el último golpe va al revés y entra en el drop
    };

    struct Hit
    {
        double beat = 0;    // dónde empieza (en negras desde el inicio del build)
        double len = 0;     // largo del paso (negras)
        double gate = 1;    // parte del paso que suena (0-1)
        double semis = 0;   // pitch de este golpe
        bool rev = false;
        int stage = 0;      // 0 = 1/4, 1 = 1/8, 2 = 1/16, 3 = 1/32
    };

    constexpr double kSteps[4] { 1.0, 0.5, 0.25, 0.125 };

    // El trozo de vocal ya preparado (silencio inicial recortado, estéreo).
    struct Source
    {
        juce::AudioBuffer<float> buf;
        double sr = 44100.0;
        float peak = 0.0f;
        bool valid() const { return buf.getNumSamples() > 16 && peak > 1.0e-5f; }
    };

    double totalBeats (const Settings&);
    std::vector<Hit> planHits (const Settings&);
    Source prepareSource (const juce::AudioBuffer<float>& in, double sr, double maxSeconds = 4.0);
    // Renderiza el build: estéreo, al sample rate y BPM pedidos. Nunca suena más fuerte que el trozo original.
    juce::AudioBuffer<float> render (const Source&, const Settings&, double bpm, double sr);
}
