#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// Aspecto: panel oscuro, acento amarillo, medidor de afinacion central, controles principales visibles y ADVANCED plegable.
class CrayLook : public juce::LookAndFeel_V4
{
public:
    CrayLook();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool, bool) override;
};

class CrayAutoTuneEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CrayAutoTuneEditor(CrayAutoTuneProcessor&);
    ~CrayAutoTuneEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Knob {
        juce::Slider s; juce::Label l; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void makeKnob(Knob&, const juce::String& name, const juce::String& paramId, const juce::String& unit);
    void makeCombo(juce::ComboBox&, const juce::StringArray&, const juce::String& paramId, std::unique_ptr<CA>&);
    void timerCallback() override;
    void layoutKnob(Knob&, juce::Rectangle<int>);

    CrayAutoTuneProcessor& processor;
    CrayLook look;
    juce::Label brand, product, latencyLabel, inputLbl, keyLbl, scaleLbl, modeLbl;
    juce::ComboBox input, key, scale, mode;
    std::unique_ptr<CA> inputAtt, keyAtt, scaleAtt, modeAtt;
    Knob retune, humanize, flex, formant, tracking;                                  // principales
    Knob snap, glide, transition, correction, gate, mix, output, formantShift;       // ADVANCED
    juce::ToggleButton bypass{ "BYPASS" }, advanced{ "ADVANCED" };
    std::unique_ptr<BA> bypassAtt;

    float midi = 60.f, target = 60.f, cents = 0.f, voiced = 0.f, delayMs = 0.f; int declared = 0, shownMode = -1;
    bool advancedOpen = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CrayAutoTuneEditor)
};
