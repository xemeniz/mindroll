#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

// Motor de MINDROLL: coge un trozo de vocal y lo repite cada vez más rápido hasta el drop.
// La velocidad sigue una línea continua, sin rejilla: el roll se va saliendo del tempo poco a poco. Todo se calcula offline: el plugin renderiza el build entero y lo reproduce o lo exporta.
namespace mroll
{
    enum Shape { Linear = 0, Expo = 1, Late = 2 };

    // La línea de velocidad: puntos (x = 0..1 del roll, y = 0..1 de velocidad).
    // y = 0 → 1/2, 0.2 → 1/4, 0.4 → 1/8, 0.6 → 1/16, 0.8 → 1/32, 1 → 1/64 (escala logarítmica).
    using Line = std::vector<std::pair<float, float>>;
    Line shapeLine (int shape);                 // formas de partida LINEAR / EXPO / LATE
    double hitsPerBeat (double y);              // velocidad en golpes por negra
    double lineY (const Line&, double x);       // valor de la línea en x (tramos rectos)
    Line sanitise (const Line&);                // ordena, limita y fija el primer y último punto

    struct Settings
    {
        int bars = 2;            // de 1 a 32 compases (4/4)
        Line line;               // velocidad del roll a lo largo del build (vacía = EXPO)
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
        double rate = 1;    // golpes por negra en ese momento (1 = 1/4, 8 = 1/32)
    };

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
    // El trozo elegido a mano con los tiradores de la onda (a y b de 0 a 1).
    Source slice (const Source&, float a, float b);
    // Renderiza el build: estéreo, al sample rate y BPM pedidos. Nunca suena más fuerte que el trozo original.
    juce::AudioBuffer<float> render (const Source&, const Settings&, double bpm, double sr);
}
