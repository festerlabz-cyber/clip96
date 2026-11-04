#pragma once

#include "PluginProcessor.h"
#include "UIComponents.h"

//==============================================================================
// Everything is laid out at the design size; the editor scales this component as a whole.
// Layout (left to right): Ceiling / Output column, big Push knob, Clip / Split / Limit groups, Monitor group (Delta,
// Match, latency) and a meter group (In, GR, Out); bottom bar with Mode, Quality and Presets. Same as the prototype.
class MainPanel : public juce::Component
{
public:
    static constexpr int W = 1160, H = 372;

    explicit MainPanel (Clip96Processor&);
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void tick();

private:
    struct Group { juce::String title; juce::Colour colour; juce::Rectangle<int> bounds; };

    Clip96Processor& proc;
    juce::Image logo;
    std::vector<std::unique_ptr<btui::Knob>> knobs;
    btui::Knob *ceiling = nullptr, *out = nullptr, *push = nullptr;
    std::array<std::pair<btui::Knob*, btui::Knob*>, 3> pairs {};
    std::array<Group, 5> groups;
    juce::Rectangle<int> meterArea;

    btui::LedButton bypassBtn { "Bypass", btui::col::magenta, true };
    btui::LedButton deltaBtn { "Delta", btui::col::amber }, matchBtn { "Match", btui::col::amber };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt, deltaAtt, matchAtt;
    juce::ComboBox modeBox, qualityBox, presetBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt, qualityAtt;
    btui::ArrowButton prevBtn { "Previous preset", true }, nextBtn { "Next preset", false };
    int shownPreset = -2; bool shownModified = false;
    int shownLatency = -1;

    // meter ballistics (GUI side)
    float lvIn = 0, lvOut = 0, lvGr = 0, holdIn = -99, holdOut = -99, holdGr = 0;
    double tIn = 0, tOut = 0, tGr = 0;

    btui::Knob* addKnob (const juce::String& id, const juce::String& label, juce::Colour c, int diameter, bool big = false);
    void refreshPresetBox();
    void stepPreset (int delta);
    void paintMeters (juce::Graphics&);
};

//==============================================================================
class Clip96Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Clip96Editor (Clip96Processor&);
    ~Clip96Editor() override;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (btui::col::ink); }

private:
    void timerCallback() override { panel.tick(); }
    btui::LookAndFeel lnf;
    MainPanel panel;
};
