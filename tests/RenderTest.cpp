// Offline check: runs a loud kick + bass + noise mix through the real plug-in processor (all presets) and checks that
// nothing passes the ceiling, measures the latency per quality against the reported one, checks that Delta + normal
// output adds up to the pushed input, a state round trip, and renders the editor to PNG.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    const double sr = 48000.0; const int block = 256;

    auto proc = std::make_unique<Clip96Processor>();
    proc->setPlayConfigDetails (2, 2, sr, block);
    proc->prepareToPlay (sr, block);
    auto set = [&] (const char* id, float v) { if (auto* p = proc->apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };

    const int beat = (int) (60.0 / 175.0 * sr);
    uint32_t seed = 7; double ph = 0;
    auto input = [&] (int n, int ch) {
        const int j = n % beat; const double t = j / sr; if (j == 0 && ch == 0) ph = 0;
        if (ch == 0) ph += 2 * juce::MathConstants<double>::pi * (50 + 180 * std::exp (-t / 0.03)) / sr;
        seed = seed * 1664525u + 1013904223u; const double noise = seed / 4294967296.0 * 2 - 1;
        const double saw = 2.0 * std::fmod (n * 55.0 / sr, 1.0) - 1.0;
        return (float) (0.62 * (0.9 * std::exp (-t / 0.3) * std::sin (ph) + 0.25 * saw + 0.08 * noise)); };

    int presetsOver = 0;
    for (int preset = 0; preset < proc->getNumPrograms(); ++preset)
    {
        proc->setCurrentProgram (preset);
        juce::AudioBuffer<float> buf (2, block); juce::MidiBuffer midi;
        double peak = 0, sum = 0, inSum = 0; int cnt = 0; bool bad = false; int n = 0;
        for (int b = 0; b < (int) (sr * 4 / block); ++b)
        {
            for (int i = 0; i < block; ++i, ++n) { const float l = input (n, 0), r = input (n, 1); buf.setSample (0, i, l); buf.setSample (1, i, r); if (n > sr) inSum += l * l; }
            proc->processBlock (buf, midi);
            if (n > sr)
                for (int i = 0; i < block; ++i)
                    for (int ch = 0; ch < 2; ++ch) { const float y = buf.getSample (ch, i); if (! std::isfinite (y)) bad = true; peak = std::max (peak, (double) std::abs (y)); if (ch == 0) { sum += y * y; ++cnt; } }
        }
        const float ceil = proc->apvts.getRawParameterValue ("ceiling")->load() + proc->apvts.getRawParameterValue ("out")->load();
        const bool over = 20 * std::log10 (peak) > ceil + 0.001;
        presetsOver += over ? 1 : 0;
        std::printf ("%-20s rms %6.1f dB (in %6.1f)  peak %6.2f dBFS (ceiling %5.1f)%s%s\n", proc->getProgramName (preset).toRawUTF8(),
                     10 * std::log10 (sum / cnt), 10 * std::log10 (inSum / cnt), 20 * std::log10 (peak), ceil, over ? "  OVER!" : "", bad ? "  NaN!" : "");
    }
    std::printf ("presets over the ceiling: %d\n", presetsOver);

    // latency: Limit mode, Push 0, quiet impulse -> comes out unchanged after the reported latency
    for (int q = 0; q < 3; ++q)
    {
        proc->setCurrentProgram (0);
        set ("quality", (float) q); set ("mode", 2); set ("push", 0);
        proc->prepareToPlay (sr, block);
        juce::AudioBuffer<float> buf (2, block); juce::MidiBuffer midi;
        std::vector<float> out;
        for (int b = 0; b < 4; ++b)
        {
            buf.clear(); if (b == 0) { buf.setSample (0, 10, 0.3f); buf.setSample (1, 10, 0.3f); }
            proc->processBlock (buf, midi);
            for (int i = 0; i < block; ++i) out.push_back (buf.getSample (0, i));
        }
        int at = 0; for (int i = 0; i < (int) out.size(); ++i) if (std::abs (out[(size_t) i]) > std::abs (out[(size_t) at])) at = i;
        std::printf ("quality %d: impulse peak after %d samples, reported latency %d\n", q, at - 10, proc->getLatencySamples());
    }
    set ("quality", 1.0f);

    // delta: normal + delta output = pushed input (aligned)
    {
        auto run = [&] (bool delta)
        {
            proc->setCurrentProgram (2); set ("delta", delta ? 1.0f : 0.0f);
            proc->prepareToPlay (sr, block);
            juce::AudioBuffer<float> buf (2, block); juce::MidiBuffer midi; std::vector<float> o; int n = 0;
            seed = 7;
            for (int b = 0; b < (int) (sr / block); ++b)
            {
                for (int i = 0; i < block; ++i, ++n) { buf.setSample (0, i, input (n, 0)); buf.setSample (1, i, input (n, 1)); }
                proc->processBlock (buf, midi);
                for (int i = 0; i < block; ++i) o.push_back (buf.getSample (0, i));
            }
            return o;
        };
        const auto a = run (false), d = run (true);
        set ("delta", 0.0f);
        seed = 7; std::vector<float> x; for (int n = 0; n < (int) a.size(); ++n) { x.push_back (input (n, 0)); (void) input (n, 1); }
        const int lat = proc->getLatencySamples(); const double pg = std::pow (10.0, proc->apvts.getRawParameterValue ("push")->load() / 20.0);
        double err = 0; for (int i = lat + 4800; i < (int) a.size(); ++i) err = std::max (err, std::abs (a[(size_t) i] + d[(size_t) i] - x[(size_t) (i - lat)] * pg));
        std::printf ("delta null: max error %.2e\n", err);
    }

    // state round trip
    proc->setCurrentProgram (7);
    juce::MemoryBlock mb; proc->getStateInformation (mb);
    proc->setCurrentProgram (0);
    proc->setStateInformation (mb.getData(), (int) mb.getSize());
    std::printf ("state restore: preset %d (%s), push %.0f\n", proc->getCurrentProgram(), proc->getProgramName (proc->getCurrentProgram()).toRawUTF8(),
                 proc->apvts.getRawParameterValue ("push")->load());

    // GUI with live meters: feed some audio first so the meters show something
    proc->setCurrentProgram (2); proc->prepareToPlay (sr, block);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
    {
        juce::AudioBuffer<float> buf (2, block); juce::MidiBuffer midi; int n = 0;
        for (int k = 0; k < 6; ++k)
        {
            for (int b = 0; b < 8; ++b) { for (int i = 0; i < block; ++i, ++n) { buf.setSample (0, i, input (n, 0)); buf.setSample (1, i, input (n, 1)); } proc->processBlock (buf, midi); }
            juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        }
    }
    for (float sc : { 1.0f, 2.0f })
    {
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, sc);
        juce::PNGImageFormat png; auto f = outDir.getChildFile ("clip96_gui_" + juce::String ((int) sc) + "x.png"); f.deleteFile();
        juce::FileOutputStream os (f); png.writeImageToStream (img, os);
        std::printf ("wrote %s (%dx%d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    }
    return 0;
}
