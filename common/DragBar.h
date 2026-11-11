#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// MIND SERIES · pieza común: botón nativo para arrastrar el clip (WAV o MIDI) a Ableton.
// El arrastre al sistema solo se puede empezar desde JUCE, no desde la interfaz web.
// También acepta que sueltes un archivo de audio encima. Dibujado con la skin v2 (pastel, borde negro, sombra dura).
namespace mind
{
struct SkinColours
{
    juce::Colour a, b, text, shadow;   // degradado pastel, texto y sombra del plugin
};

class DragBar : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit DragBar (SkinColours c) : col (c) {}

    std::function<juce::File()> makeFile;                 // crea el archivo justo antes de arrastrar
    std::function<void (const juce::File&)> onDrop;       // archivo soltado encima
    juce::String label { "DRAG TO ABLETON" }, dropLabel { "DROP TO LOAD" };
    bool enabled = false;
    juce::Typeface::Ptr font;                             // Nunito (la misma de la interfaz)

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override  { dragging = false; }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override    { dragging = false; }
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }

    static const juce::StringArray& audioExtensions();

private:
    SkinColours col;
    juce::File lastFile;
    bool hover = false, dragging = false, dropHover = false;
};
}
