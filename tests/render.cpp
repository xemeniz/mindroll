// Renderiza un build a WAV para escucharlo o analizarlo:
// MoRender in.wav out.wav [bars=2] [curve=1] [pitch=0] [filter=0.5] [reverse=0] [bpm=124] [gate0=0.35] [gate1=1]
#include "../Source/RollEngine.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("uso: MoRender in.wav out.wav [bars] [curve] [pitch] [filter] [reverse] [bpm] [gate0] [gate1]\n"); return 1; }
    auto arg = [&] (int i, double d) { return argc > i ? std::atof (argv[i]) : d; };
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]))));
    if (r == nullptr) { std::printf ("no se puede leer %s\n", argv[1]); return 1; }
    juce::AudioBuffer<float> in ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&in, 0, (int) r->lengthInSamples, 0, true, true);
    mroll::Settings s;
    s.bars = (int) arg (3, 2); s.curve = (int) arg (4, 1); s.pitch = (float) arg (5, 0); s.filter = (float) arg (6, 0.5);
    s.reverse = arg (7, 0) > 0.5; s.gateStart = (float) arg (9, 0.35); s.gateEnd = (float) arg (10, 1.0);
    const double sr = 48000.0;
    auto out = mroll::render (mroll::prepareSource (in, r->sampleRate), s, arg (8, 124), sr);
    juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2])); f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (f.createOutputStream().release(), sr, 2, 24, {}, 0));
    w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
    std::printf ("ok: %s (%.2f s)\n", argv[2], out.getNumSamples() / sr);
    return 0;
}
