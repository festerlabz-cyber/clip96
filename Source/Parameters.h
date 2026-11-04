// Parameter definitions, value formatting and factory presets.
// Parameter IDs match the browser prototype keys, so presets carry over 1:1.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Clip96DSP.h"

namespace cl96
{
enum class Unit { percent, dB, dBPos, dBPlain, hzOff, ms, toggle, choice };

struct ParamSpec
{
    juce::String id, name;
    int engineIndex;
    Unit unit;
    float min, max, def;
    bool logScale = false;
    juce::StringArray choices {};
};

inline juce::String formatHz (double v)
{
    if (juce::roundToInt (v) >= 1000) return juce::String (v / 1000.0, v >= 9995.0 ? 1 : 2) + " kHz";
    return juce::String (juce::roundToInt (v)) + " Hz";
}

inline juce::String formatValue (Unit unit, double v)
{
    switch (unit)
    {
        case Unit::percent: return juce::String (juce::roundToInt (v)) + " %";
        case Unit::dB:      return (v > 0.04 ? "+" : "") + juce::String (v, 1) + " dB";
        case Unit::dBPos:
        case Unit::dBPlain: return juce::String (v, 1) + " dB";
        case Unit::hzOff:   return v <= kSplitOff ? juce::String ("Off") : formatHz (v);
        case Unit::ms:      return juce::String (juce::roundToInt (v)) + " ms";
        case Unit::toggle:  return v > 0.5 ? "On" : "Off";
        case Unit::choice:  break;
    }
    return juce::String (v);
}

inline const std::vector<ParamSpec>& paramSpecs()
{
    static const std::vector<ParamSpec> specs = []
    {
        std::vector<ParamSpec> s;
        s.push_back ({ "mode",     "Mode",      P::mode,     Unit::choice,  0, 2, 1, false, { "Clip", "Clip + Limit", "Limit" } });
        s.push_back ({ "quality",  "Quality",   P::quality,  Unit::choice,  0, 2, 1, false, { "Eco", "Normal 2x", "High 4x" } });
        s.push_back ({ "push",     "Push",      P::push,     Unit::dBPos,   0, 24, 6 });
        s.push_back ({ "shape",    "Hardness",  P::shape,    Unit::percent, 0, 100, 50 });
        s.push_back ({ "asym",     "Asym",      P::asym,     Unit::percent, 0, 100, 0 });
        s.push_back ({ "split",    "Split",     P::split,    Unit::hzOff,   40, 400, 40, true });
        s.push_back ({ "lowDrive", "Low Drive", P::lowDrive, Unit::dB,      -12, 12, 0 });
        s.push_back ({ "release",  "Release",   P::release,  Unit::ms,      5, 500, 60, true });
        s.push_back ({ "link",     "Link",      P::link,     Unit::percent, 0, 100, 100 });
        s.push_back ({ "ceiling",  "Ceiling",   P::ceiling,  Unit::dBPlain, -12, 0, -0.3f });
        s.push_back ({ "out",      "Output",    P::out,      Unit::dB,      -12, 6, 0 });
        s.push_back ({ "delta",    "Delta",     P::delta,    Unit::toggle,  0, 1, 0 });
        s.push_back ({ "match",    "Match",     P::match,    Unit::toggle,  0, 1, 0 });
        s.push_back ({ "bypass",   "Bypass",    P::bypass,   Unit::toggle,  0, 1, 0 });
        return s;
    }();
    return specs;
}

inline const ParamSpec* findSpec (const juce::String& id)
{
    for (auto& s : paramSpecs()) if (s.id == id) return &s;
    return nullptr;
}

inline juce::NormalisableRange<float> makeRange (const ParamSpec& s)
{
    if (s.logScale)
        return { s.min, s.max,
                 [] (float a, float b, float x) { return a * std::pow (b / a, x); },
                 [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
                 nullptr };
    return { s.min, s.max };
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (auto& s : paramSpecs())
    {
        const juce::ParameterID pid { s.id, 1 };
        if (s.unit == Unit::toggle)
            layout.add (std::make_unique<juce::AudioParameterBool> (pid, s.name, s.def > 0.5f));
        else if (s.unit == Unit::choice)
            layout.add (std::make_unique<juce::AudioParameterChoice> (pid, s.name, s.choices, (int) s.def));
        else
        {
            const auto unit = s.unit;
            auto attrs = juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([unit] (float v, int) { return formatValue (unit, v); })
                .withValueFromStringFunction ([unit] (const juce::String& t)
                {
                    auto v = t.retainCharacters ("-0123456789.").getFloatValue();
                    if (unit == Unit::hzOff && t.containsIgnoreCase ("k")) v *= 1000.0f;
                    return v;
                });
            layout.add (std::make_unique<juce::AudioParameterFloat> (pid, s.name, makeRange (s), s.def, attrs));
        }
    }
    return layout;
}

// choice parameters arrive as indices; the quality index maps to the oversampling factor
inline double engineValue (const ParamSpec& s, float raw)
{
    if (s.engineIndex == P::quality) return raw < 0.5f ? 1.0 : (raw < 1.5f ? 2.0 : 4.0);
    return raw;
}

//==============================================================================
struct Preset { juce::String name; std::vector<std::pair<juce::String, float>> values; };

// Choice values are indices. Quality, Delta, Match and Bypass are never changed by presets; anything not listed goes to
// its default.
inline const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> presets {
        { "Init", {} },
        { "Transparent Limit",   { {"mode",2},{"push",4},{"release",120},{"ceiling",-0.3f} } },
        { "Hardcore Master",     { {"mode",1},{"push",8},{"shape",70},{"release",50},{"ceiling",-0.3f} } },
        { "Kick Smash",          { {"mode",0},{"push",10},{"shape",85},{"asym",15},{"ceiling",-0.5f} } },
        { "Soft Glue",           { {"mode",1},{"push",4},{"shape",15},{"release",150},{"link",80} } },
        { "Loud & Proud",        { {"mode",1},{"push",12},{"shape",60},{"split",110},{"lowDrive",3},{"release",40} } },
        { "Split Saver",         { {"mode",1},{"push",9},{"shape",50},{"split",150},{"lowDrive",-6},{"release",60} } },
        { "Hakkuh Loudness War", { {"mode",1},{"push",20},{"shape",100},{"asym",25},{"split",120},{"lowDrive",6},{"release",20},{"link",60},{"ceiling",-0.1f} } },
    };
    return presets;
}
} // namespace cl96
