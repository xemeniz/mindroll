#include "RollEngine.h"
#include <cmath>

namespace mroll
{
double totalBeats (const Settings& s)
{
    return 4.0 * juce::jlimit (1, 32, s.bars);
}

Line shapeLine (int shape)
{
    switch (shape)
    {
        case Linear: return { { 0.0f, 0.2f }, { 1.0f, 0.8f } };
        case Late:   return { { 0.0f, 0.2f }, { 0.7f, 0.24f }, { 0.9f, 0.55f }, { 1.0f, 0.85f } };
        default:     return { { 0.0f, 0.2f }, { 0.55f, 0.36f }, { 1.0f, 0.85f } };
    }
}

double hitsPerBeat (double y) { return 0.5 * std::pow (32.0, juce::jlimit (0.0, 1.0, y)); }

Line sanitise (const Line& in)
{
    Line l;
    for (auto [x, y] : in)
        if (std::isfinite (x) && std::isfinite (y)) l.push_back ({ juce::jlimit (0.0f, 1.0f, x), juce::jlimit (0.0f, 1.0f, y) });
    if (l.size() < 2) return shapeLine (Expo);
    std::stable_sort (l.begin(), l.end(), [] (auto& a, auto& b) { return a.first < b.first; });
    if (l.size() > 12) l.resize (12);
    l.front().first = 0.0f; l.back().first = 1.0f;
    return l;
}

double lineY (const Line& l, double x)
{
    if (l.empty()) return 0.5;
    if (x <= l.front().first) return l.front().second;
    for (size_t i = 1; i < l.size(); ++i)
        if (x <= l[i].first)
        {
            const double x0 = l[i - 1].first, x1 = l[i].first;
            const double f = x1 > x0 ? (x - x0) / (x1 - x0) : 1.0;
            return l[i - 1].second + (l[i].second - l[i - 1].second) * f;
        }
    return l.back().second;
}

std::vector<Hit> planHits (const Settings& s)
{
    const double total = totalBeats (s);
    // Con REVERSE, el último medio tiempo (1 tiempo en builds de 4 y 8 compases) es el golpe al revés
    // y el roll se reparte en lo que queda.
    const double revLen = s.reverse ? (total >= 16.0 ? 1.0 : 0.5) : 0.0;
    const double end = total - revLen;
    const Line line = sanitise (s.line.empty() ? shapeLine (Expo) : s.line);
    const double g0 = juce::jlimit (0.05, 1.0, (double) s.gateStart);
    const double g1 = juce::jlimit (0.05, 1.0, (double) s.gateEnd);
    const double pitch = juce::jlimit (0.0, 12.0, (double) s.pitch);

    // Golpes: se integra la velocidad de la línea. Sin rejilla: cada golpe cae donde lo lleva la aceleración.
    std::vector<double> starts { 0.0 };
    const double dt = 1.0 / 512.0;
    double phase = 0.0;
    for (double t = 0.0; t < end - 1.0e-9; t += dt)
    {
        const double step = std::min (dt, end - t);
        const double r = hitsPerBeat (lineY (line, (t + step * 0.5) / end));
        const double next = phase + r * step;
        if (next >= 1.0)
        {
            const double at = t + step * (1.0 - phase) / (r * step);
            if (at < end - 1.0e-6) starts.push_back (at);
            phase = next - 1.0;
        }
        else phase = next;
    }

    std::vector<Hit> hits;
    for (size_t i = 0; i < starts.size(); ++i)
    {
        Hit h;
        h.beat = starts[i];
        h.len = (i + 1 < starts.size() ? starts[i + 1] : end) - h.beat;
        const double p = h.beat / total;
        h.gate = g0 + (g1 - g0) * p;
        h.semis = pitch * p * p;     // el pitch sube sobre todo al final
        h.rate = hitsPerBeat (lineY (line, h.beat / end));
        hits.push_back (h);
    }
    if (revLen > 0)
    {
        Hit h; h.beat = end; h.len = revLen; h.gate = 1.0; h.semis = pitch; h.rev = true; h.rate = hits.empty() ? 8.0 : hits.back().rate;
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

Source slice (const Source& src, float a, float b)
{
    if (! src.valid()) return src;
    const int n = src.buf.getNumSamples();
    a = juce::jlimit (0.0f, 1.0f, a); b = juce::jlimit (0.0f, 1.0f, b);
    if (b < a) std::swap (a, b);
    if (a <= 0.0f && b >= 1.0f) return src;
    const int i0 = juce::jlimit (0, n - 1, (int) std::round (a * n));
    const int len = juce::jlimit (1, n - i0, juce::jmax ((int) (src.sr * 0.005), (int) std::round ((b - a) * n)));
    Source s; s.sr = src.sr;
    s.buf.setSize (2, len);
    for (int ch = 0; ch < 2; ++ch) s.buf.copyFrom (ch, 0, src.buf, ch, i0, len);
    // si el corte empieza a mitad de onda, una entrada de 1 ms para que no haga clic
    const int fade = juce::jmin (len / 4, (int) (src.sr * 0.001));
    for (int ch = 0; ch < 2; ++ch) s.buf.applyGainRamp (ch, 0, fade, 0.0f, 1.0f);
    s.peak = juce::jmax (s.buf.getMagnitude (0, 0, len), s.buf.getMagnitude (1, 0, len));
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
