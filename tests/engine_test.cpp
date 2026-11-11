// Prueba del motor de MINDROLL sin host ni interfaz.
#include "../Source/RollEngine.h"
#include <cstdio>
#include <cmath>

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++fails; std::printf ("  FALLO: " __VA_ARGS__); std::printf ("\n"); } } while (0)

static const double SR = 48000.0;

// "Vocal" de prueba: 100 ms de silencio y luego una sílaba (tono con armónicos y envolvente) de 600 ms.
static juce::AudioBuffer<float> fakeVocal (double sr, float gain)
{
    const int pre = (int) (sr * 0.1), n = (int) (sr * 0.7);
    juce::AudioBuffer<float> b (1, n); b.clear();
    for (int i = pre; i < n; ++i)
    {
        const double t = (i - pre) / sr;
        const double env = std::min (1.0, t / 0.004) * std::exp (-t * 2.5);
        const double v = std::sin (juce::MathConstants<double>::twoPi * 220.0 * t) + 0.5 * std::sin (juce::MathConstants<double>::twoPi * 440.0 * t)
                       + 0.25 * std::sin (juce::MathConstants<double>::twoPi * 880.0 * t);
        b.setSample (0, i, (float) (gain * env * v / 1.75));
    }
    return b;
}

int main()
{
    std::printf ("MINDROLL · prueba del motor\n");

    // ---------- plan de golpes ----------
    {
        mroll::Settings s; s.bars = 1; s.curve = mroll::Linear;
        auto h = mroll::planHits (s);
        int count[4] {};
        for (auto& x : h) count[x.stage]++;
        CHECK (h.size() == 15 && count[0] == 1 && count[1] == 2 && count[2] == 4 && count[3] == 8,
               "1 compás LINEAR debería ser 1+2+4+8 golpes (sale %d)", (int) h.size());
    }
    const int barsList[4] { 1, 2, 4, 8 };
    for (int curve = 0; curve < 3; ++curve)
        for (int bars : barsList)
            for (int rev = 0; rev < 2; ++rev)
            {
                mroll::Settings s; s.bars = bars; s.curve = curve; s.reverse = rev; s.gateStart = 0.3f; s.gateEnd = 1.0f; s.pitch = 7;
                const auto h = mroll::planHits (s);
                const double total = mroll::totalBeats (s);
                bool grid = true, contiguous = true, accel = true, gateUp = true, pitchUp = true;
                for (size_t i = 0; i < h.size(); ++i)
                {
                    const double g = h[i].beat / (h[i].rev ? 0.125 : mroll::kSteps[h[i].stage]);
                    if (std::abs (g - std::round (g)) > 1e-9) grid = false;   // cada golpe en la rejilla de su división
                    if (i > 0)
                    {
                        if (std::abs (h[i - 1].beat + h[i - 1].len - h[i].beat) > 1e-9) contiguous = false;
                        if (! h[i].rev && h[i].stage < h[i - 1].stage) accel = false;
                        if (! h[i].rev && h[i].gate < h[i - 1].gate - 1e-9) gateUp = false;
                        if (h[i].semis < h[i - 1].semis - 1e-9) pitchUp = false;
                    }
                }
                const auto& last = h.back();
                bool allStages = true;
                for (int st = 0; st < 4; ++st)
                {
                    bool any = false; for (auto& x : h) if (x.stage == st && ! x.rev) any = true;
                    if (! any) allStages = false;
                }
                const char* cn[] { "LINEAR", "EXPO", "LATE" };
                CHECK (grid, "%s %d compases rev=%d: golpes fuera de la rejilla", cn[curve], bars, rev);
                CHECK (contiguous, "%s %d compases rev=%d: huecos o solapes entre golpes", cn[curve], bars, rev);
                CHECK (std::abs (last.beat + last.len - total) < 1e-9, "%s %d compases rev=%d: no termina justo en el drop", cn[curve], bars, rev);
                CHECK (accel, "%s %d compases: la división no siempre acelera", cn[curve], bars);
                CHECK (gateUp, "%s %d compases: el gate no crece", cn[curve], bars);
                CHECK (pitchUp, "%s %d compases: el pitch baja en algún golpe", cn[curve], bars);
                CHECK (allStages, "%s %d compases rev=%d: falta alguna división (1/4 a 1/32)", cn[curve], bars, rev);
                CHECK (h.front().stage == 0 && h.front().beat == 0.0, "%s %d compases: no empieza en 1/4 en el tiempo 1", cn[curve], bars);
                CHECK (last.rev == (bool) rev, "%s %d compases: el reverse no va en el último golpe", cn[curve], bars);
            }
    {
        mroll::Settings a; a.bars = 4; a.curve = mroll::Late;
        mroll::Settings b = a; b.curve = mroll::Linear;
        auto firstFast = [] (const std::vector<mroll::Hit>& h) { for (auto& x : h) if (x.stage == 3) return x.beat; return 99.0; };
        CHECK (firstFast (mroll::planHits (a)) > firstFast (mroll::planHits (b)), "LATE debería llegar a 1/32 más tarde que LINEAR");
    }

    // ---------- trozo de vocal ----------
    const auto raw = fakeVocal (44100.0, 0.5f);
    const auto src = mroll::prepareSource (raw, 44100.0);
    CHECK (src.valid(), "el trozo de vocal no se carga");
    CHECK (src.buf.getNumSamples() < raw.getNumSamples() - (int) (44100 * 0.09), "no recorta el silencio del principio");
    CHECK (std::abs (src.peak - raw.getMagnitude (0, 0, raw.getNumSamples())) < 1e-4f, "el pico del trozo no cuadra (%.3f)", src.peak);
    {
        juce::AudioBuffer<float> silent (2, 4800); silent.clear();
        CHECK (! mroll::prepareSource (silent, SR).valid(), "un archivo en silencio no debería valer");
    }

    // ---------- render ----------
    const double bpm = 124.0;
    for (int curve = 0; curve < 3; ++curve)
        for (int bars : barsList)
            for (int k = 0; k < 4; ++k)
            {
                mroll::Settings s; s.bars = bars; s.curve = curve;
                s.pitch = (k & 1) ? 12.0f : 0.0f; s.filter = (k & 2) ? 1.0f : 0.0f; s.reverse = k == 3;
                const auto out = mroll::render (src, s, bpm, SR);
                const int N = out.getNumSamples();
                const int expect = (int) std::round (mroll::totalBeats (s) * SR * 60.0 / bpm);
                const float pk = std::max (out.getMagnitude (0, 0, N), out.getMagnitude (1, 0, N));
                bool finite = true;
                for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < N; ++i) if (! std::isfinite (out.getSample (ch, i))) finite = false;
                CHECK (N == expect, "%d compases: el render no dura lo que debe (%d vs %d)", bars, N, expect);
                CHECK (pk <= src.peak + 1e-4f, "curva %d, %d compases, k=%d: más fuerte que el original (%.3f > %.3f)", curve, bars, k, pk, src.peak);
                CHECK (pk > src.peak * 0.2f, "curva %d, %d compases, k=%d: casi no suena (%.3f)", curve, bars, k, pk);
                CHECK (finite, "valores raros (NaN) en el render");
            }

    // Cada golpe suena en su sitio (sin filtro ni pitch): hay señal justo después del inicio del golpe
    // y silencio justo antes cuando el gate es corto.
    {
        mroll::Settings s; s.bars = 2; s.curve = mroll::Linear; s.gateStart = 0.4f; s.gateEnd = 0.4f; s.filter = 0;
        const auto out = mroll::render (src, s, bpm, SR);
        const double spb = SR * 60.0 / bpm;
        int onTime = 0, total = 0;
        for (auto& h : mroll::planHits (s))
        {
            const int st = (int) std::round (h.beat * spb);
            const int a = st + (int) (SR * 0.002), b = st + (int) (SR * 0.004);
            if (b >= out.getNumSamples()) continue;
            ++total;
            const float after = out.getMagnitude (0, a, b - a);
            const float before = st > 64 ? out.getMagnitude (0, st - 48, 32) : 0.0f;
            if (after > 0.02f && before < 0.01f) ++onTime;
        }
        CHECK (onTime == total, "golpes fuera de sitio: %d de %d en su tiempo", onTime, total);
    }

    // Reverse: el último medio tiempo sube hacia el drop (más energía al final que al principio).
    {
        mroll::Settings s; s.bars = 1; s.curve = mroll::Expo; s.reverse = true; s.filter = 0;
        const auto out = mroll::render (src, s, bpm, SR);
        const int N = out.getNumSamples(), half = (int) (0.5 * SR * 60.0 / bpm);
        const float early = out.getRMSLevel (0, N - half, half / 3);
        const float late = out.getRMSLevel (0, N - half / 3 - 200, half / 3);
        CHECK (late > early * 1.5f, "el reverse no crece hacia el drop (%.3f vs %.3f)", early, late);
    }

    // Filtro: al principio apaga los agudos, al final está abierto (tono de 2 kHz).
    {
        juce::AudioBuffer<float> hi (1, (int) SR); 
        for (int i = 0; i < hi.getNumSamples(); ++i) hi.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 2000.0 * i / SR));
        const auto hs = mroll::prepareSource (hi, SR);
        mroll::Settings s; s.bars = 4; s.curve = mroll::Linear; s.filter = 1.0f; s.gateStart = s.gateEnd = 1.0f;
        mroll::Settings d = s; d.filter = 0.0f;
        const auto f = mroll::render (hs, s, bpm, SR), o = mroll::render (hs, d, bpm, SR);
        const int beat = (int) (SR * 60.0 / bpm), N = f.getNumSamples();
        const float rf0 = f.getRMSLevel (0, 0, beat / 2), ro0 = o.getRMSLevel (0, 0, beat / 2);
        const float rf1 = f.getRMSLevel (0, N - beat / 2, beat / 2 - 64), ro1 = o.getRMSLevel (0, N - beat / 2, beat / 2 - 64);
        CHECK (rf0 < ro0 * 0.2f, "el filtro no cierra al principio (%.3f vs %.3f)", rf0, ro0);
        CHECK (rf1 > ro1 * 0.7f, "el filtro no se abre al final (%.3f vs %.3f)", rf1, ro1);
    }

    // Más BPM = build más corto.
    {
        mroll::Settings s; s.bars = 2;
        CHECK (mroll::render (src, s, 128.0, SR).getNumSamples() < mroll::render (src, s, 120.0, SR).getNumSamples(), "el BPM no cambia la duración");
    }

    std::printf ("%d comprobaciones, %d fallos\n", checks, fails);
    return fails == 0 ? 0 : 1;
}
