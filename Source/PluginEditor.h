#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "DragBar.h"

class MOEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MOEditor (MOProcessor&);
    ~MOEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kW = 1038, kWebH = 560, kBarH = 68;

private:
    void timerCallback() override;
    void emit (const juce::Identifier& ev, const juce::var& v);
    void toast (const juce::String& msg);
    void sendSettings();
    void sendSource();
    void sendPlan();
    void loadFile (const juce::File& f);

    MOProcessor& proc;
    mind::DragBar bar;
    juce::Image frame;
    std::unique_ptr<juce::WebBrowserComponent> web;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String lastState;
    bool ready = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MOEditor)
};
