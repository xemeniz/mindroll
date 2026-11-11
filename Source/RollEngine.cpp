#include "RollEngine.h"
#include <cmath>

namespace mroll
{
double totalBeats (const Settings& s)
{
    const int b = (s.bars == 1 || s.bars == 2 || s.bars == 4 || s.bars == 8) ? s.bars : 2;
    return 4.0 * b;
}

static double q (double v, double grid) { return std::round (v / grid) * grid; }

std::vector<Hit> planHits (const Settings& s)
{
    const double total = totalBeats (s);

    // Parte del build que se queda cada división. LATE aguanta 1/4 casi todo y acelera al final.
    static const double fr[3][4] { { 0.25, 0.25, 0.25, 0.25 },
                                   { 0.50, 0.25, 0.125, 0.125 },
                                   { 0.75, 0.125, 0.0625, 0.0625 } };
    const int c = juce::jlimit (0, 2, s.curve);

    // Con REVERSE, el último medio tiempo (1 tiempo en builds de 4 y 8 compases) es el golpe al revés
    // y el roll se reparte en lo que queda.
    const double revLen = s.reverse ? (total >= 16.0 ? 1.0 : 0.5) : 0.0;
    const double end = total - revLen;

    // Límites de cada tramo: cada uno empieza en su propia rejilla (1/8 en corchea, 1/16 en semicorchea...)
    // y deja sitio para al menos un golpe de cada división que viene detrás.
    static const double grid[3] { 1.0, 0.5, 0.25 }, minAfter[3] { 0.875, 0.375, 0.125 };
    double b[4]; double acc = 0;
    for (int i = 0; i < 3; ++i)
    {
        acc += fr[c][i] * end;
        const double g = grid[i];
        const double lo = i == 0 ? 1.0 : b[i - 1] + kSteps[i];
        const double hi = std::floor ((end - minAfter[i]) / g + 1.0e-9) * g;
        b[i] = juce::jlimit (lo, std::max (lo, hi), q (acc, g));
    }
    b[3] = end;

    const double g0 = juce::jlimit (0.05, 1.0, (double) s.gateStart);
    const double g1 = juce::jlimit (0.05, 1.0, (double) s.gateEnd);
    const double pitch = juce::jlimit (0.0, 12.0, (double) s.pitch);

    std::vector<Hit> hits;
    double start = 0;
    for (int st = 0; st < 4; ++st)
    {
        const double step = kSteps[st];
        for (double t = start; t < b[st] - 1.0e-9 && t < end - 1.0e-9; t += step)
        {
            Hit h;
            h.beat = t;
            h.len = std::min ({ step, b[st] - t, end - t });
            const double p = t / total;
            h.gate = g0 + (g1 - g0) * p;
            h.semis = pitch * p * p;     // el pitch sube sobre todo al final
            h.stage = st;
            hits.push_back (h);
        }
        start = b[st];
    }
    if (revLen > 0)
    {
        Hit h; h.beat = end; h.len = revLen; h.gate = 1.0; h.semis = pitch; h.rev = true; h.stage = 3;
        hits.push_back (h);
    }
    return hits;
}

Source prepareSource (const juce::AudioBuffer<float>& in, double sr, double maxSeconds)
{
    Source s; s.sr = sr;
    const int n = in.getNumSamples(), chs = in.getNumChannels();
    if (n <= 0 || chs <= 0) return s;

    float peak = 0.0f;
    for (int ch = 0; ch < chs; ++ch) peak = std::max (peak, in.getMagnitude (ch, 0, n));
    if (peak < 1.0e-5f) return s;

    // Recorta el silencio del principio para que el corte empiece justo en la sílaba (deja 2 ms antes).
    const float thr = peak * 0.03f;
    int first = 0;
    for (; first < n; ++first)
    {
        bool hit = false;
        for (int ch = 0; ch < chs; ++ch) if (std::abs (in.getSample (ch, first)) > thr) { hit = true; break; }
        if (hit) break;
    }
    first = std::max (0, first - (int) (sr * 0.002));
    const int len = std::min (n - first, (int) (sr * maxSeconds));

    s.buf.setSize (2, len);
    for (int ch = 0; ch < 2; ++ch)
        s.buf.copyFrom (ch, 0, in, std::min (ch, chs - 1), first, len);
    s.peak = std::max (s.buf.getMagnitude (0, 0, len), s.buf.getMagnitude (1, 0, len));
    return s;
}

// Pasa-bajos TPT de 2 polos (estable al moverlo rápido).
struct LowPass
{
    float ic1 = 0, ic2 = 0;
    float g = 1, k = 1.2f, a1 = 0, a2 = 0, a3 = 0;
    void set (double fc, double sr)
    {
        g = (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (20.0, sr * 0.45, fc) / sr);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    float process (float x)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        return v2;
    }
};

static float readLinear (const float* d, int n, double pos)
{
    if (pos < 0 || pos >= n - 1) return 0.0f;
    const int i = (int) pos; const float f = (float) (pos - i);
    return d[i] + (d[i + 1] - d[i]) * f;
}

juce::AudioBuffer<float> render (const Source& src, const Settings& s, double bpm, double sr)
{
    bpm = juce::jlimit (40.0, 250.0, bpm);
    const double spb = sr * 60.0 / bpm;
    const double total = totalBeats (s);
    const int N = (int) std::round (total * spb);
    juce::AudioBuffer<float> out (2, std::max (1, N));
    out.clear();
    if (! src.valid()) return out;

    const int srcN = src.buf.getNumSamples();
    const double baseRate = src.sr / sr;
    const int fadeIn = std::max (8, (int) (sr * 0.0015));

    for (const auto& h : planHits (s))
    {
        const int start = (int) std::round (h.beat * spb);
        const int stepN = (int) std::round ((h.beat + h.len) * spb) - start;
        const int play = std::max (32, std::min (stepN, (int) std::round (stepN * h.gate)));
        const double rate = baseRate * std::pow (2.0, h.semis / 12.0);
        const int fadeOut = std::max (16, std::min ((int) (sr * 0.005), play / 3));

        if (! h.rev)
        {
            const int avail = (int) std::floor ((srcN - 1) / rate);
            const int n = std::min (play, avail);
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* d = src.buf.getReadPointer (ch);
                float* o = out.getWritePointer (ch);
                for (int i = 0; i < n && start + i < N; ++i)
                {
                    float env = 1.0f;
                    if (i < fadeIn) env = (float) i / (float) fadeIn;
                    const int left = n - i;
                    if (left < fadeOut) env *= (float) left / (float) fadeOut;
                    o[start + i] += env * readLinear (d, srcN, i * rate);
                }
            }
        }
        else
        {
            // Al revés: el trozo suena hacia atrás y termina justo en el drop (el ataque queda al final).
            const int region = std::min (play, (int) std::floor ((srcN - 1) / rate));
            const int off = play - region;
            const int swell = std::max (16, region * 3 / 10);
            const int tail = std::max (16, (int) (sr * 0.002));
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* d = src.buf.getReadPointer (ch);
                float* o = out.getWritePointer (ch);
                for (int i = 0; i < region; ++i)
                {
                    const int w = start + off + i;
                    if (w >= N) break;
                    float env = i < swell ? (float) i / (float) swell : 1.0f;
                    const int left = region - i;
                    if (left < tail) env *= (float) left / (float) tail;
                    o[w] += env * readLinear (d, srcN, (region - 1 - i) * rate);
                }
            }
        }
    }

    // Pasa-bajos que se abre durante el build.
    if (s.filter > 0.001f)
    {
        const double f0 = 20000.0 * std::pow (250.0 / 20000.0, juce::jlimit (0.0, 1.0, (double) s.filter));
        LowPass lp[2];
        for (int i = 0; i < N; i += 32)
        {
            const double p = (double) i / N;
            const double fc = f0 * std::pow (20000.0 / f0, std::pow (p, 1.4));
            for (auto& f : lp) f.set (fc, sr);
            const int m = std::min (32, N - i);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* o = out.getWritePointer (ch, i);
                for (int j = 0; j < m; ++j) o[j] = lp[ch].process (o[j]);
            }
        }
    }

    // Nunca más fuerte que el trozo original.
    const float pk = std::max (out.getMagnitude (0, 0, N), out.getMagnitude (1, 0, N));
    if (pk > src.peak * 0.999f && pk > 0.0f) out.applyGain (src.peak * 0.999f / pk);
    for (int ch = 0; ch < 2; ++ch)
    {
        float* o = out.getWritePointer (ch);
        for (int i = 0; i < N; ++i) if (! std::isfinite (o[i])) o[i] = 0.0f;
    }
    return out;
}
}
