#include "DragBar.h"

namespace mind
{
const juce::StringArray& DragBar::audioExtensions()
{
    static const juce::StringArray e { ".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg", ".m4a", ".caf" };
    return e;
}

void DragBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f, 1.0f);
    r.removeFromRight (3.0f); r.removeFromBottom (3.0f);          // sitio para la sombra dura
    const bool live = enabled || dropHover;
    if (hover && enabled && ! dropHover) r.translate (-1.0f, -1.0f);
    const float shadow = (hover && enabled) ? 4.0f : 3.0f;

    juce::Graphics::ScopedSaveState ss (g);
    g.setOpacity (live ? 1.0f : 0.45f);

    // sombra dura en el color oscuro del plugin
    g.setColour (col.shadow);
    g.fillRoundedRectangle (r.translated (shadow, shadow), 11.0f);
    // degradado pastel
    juce::ColourGradient grad (dropHover ? juce::Colours::white : col.a, r.getTopLeft(), col.b, r.getBottomRight(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 11.0f);
    // borde negro de 2 px por dentro
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r.reduced (1.0f), 10.0f, 2.0f);

    // flecha + texto
    g.setColour (col.text);
    const juce::String text = dropHover ? dropLabel : label;
    juce::Font f = font != nullptr ? juce::Font (juce::FontOptions (font).withHeight (14.5f)) : juce::Font (juce::FontOptions (14.5f, juce::Font::bold));
    f.setExtraKerningFactor (0.06f);
    g.setFont (f);
    const float tw = juce::GlyphArrangement::getStringWidth (f, text);
    const auto c = r.getCentre();
    const float tx = c.x - tw * 0.5f - 14.0f;
    juce::Path p;
    p.startNewSubPath (tx, c.y - 6.0f); p.lineTo (tx, c.y + 6.0f);
    p.startNewSubPath (tx - 4.5f, c.y + 1.5f); p.lineTo (tx, c.y + 6.0f); p.lineTo (tx + 4.5f, c.y + 1.5f);
    g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.drawText (text, juce::Rectangle<float> (c.x - tw * 0.5f, r.getY(), tw + 4.0f, r.getHeight()), juce::Justification::centredLeft);
}

void DragBar::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 4) return;
    dragging = true;
    auto f = makeFile ? makeFile() : juce::File();
    if (! f.existsAsFile()) return;
    lastFile = f;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragging = false; });
}

void DragBar::mouseDoubleClick (const juce::MouseEvent&)
{
    if (lastFile.existsAsFile()) lastFile.revealToUser();
}

bool DragBar::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) for (auto& e : audioExtensions()) if (f.endsWithIgnoreCase (e)) return true;
    return false;
}

void DragBar::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false; repaint();
    if (! files.isEmpty() && onDrop) onDrop (juce::File (files[0]));
}
}
