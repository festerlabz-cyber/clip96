#include "PluginEditor.h"

using namespace btui;

namespace
{
    constexpr int kTopH = 66, kBarH = 60, kPad = 22;
    constexpr int kSmall = 46, kBig = 140;
}

//==============================================================================
MainPanel::MainPanel (Clip96Processor& p) : proc (p)
{
    logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);

    ceiling = addKnob ("ceiling", "Ceiling", col::amber, kSmall);
    out     = addKnob ("out", "Output", col::amber, kSmall);
    push    = addKnob ("push", "Push", col::amber, kBig, true);

    pairs = {{ { addKnob ("shape", "Hardness", col::magenta, kSmall), addKnob ("asym", "Asym", col::magenta, kSmall) },
               { addKnob ("split", "Freq", col::gold, kSmall),        addKnob ("lowDrive", "Low Drive", col::gold, kSmall) },
               { addKnob ("release", "Release", col::olive, kSmall),  addKnob ("link", "Link", col::olive, kSmall) } }};
    groups = {{ { "Clip", col::magenta, {} }, { "Split", col::gold, {} }, { "Limit", col::olive, {} },
                { "Monitor", col::amber, {} }, { "Meters", col::magenta, {} } }};

    addAndMakeVisible (bypassBtn);
    bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "bypass", bypassBtn);
    deltaBtn.setTooltip ("Hear only what is clipped away");
    matchBtn.setTooltip ("Turn the level down by the Push amount for a fair comparison");
    addAndMakeVisible (deltaBtn); addAndMakeVisible (matchBtn);
    deltaAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "delta", deltaBtn);
    matchAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "match", matchBtn);

    modeBox.addItemList ({ "Clip", "Clip + Limit", "Limit" }, 1);
    qualityBox.addItemList ({ "Eco", juce::String (juce::CharPointer_UTF8 ("Normal 2\xc3\x97")), juce::String (juce::CharPointer_UTF8 ("High 4\xc3\x97")) }, 1);
    modeBox.setTitle ("Mode"); qualityBox.setTitle ("Quality"); presetBox.setTitle ("Preset");
    addAndMakeVisible (modeBox); addAndMakeVisible (qualityBox);
    modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "mode", modeBox);
    qualityAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "quality", qualityBox);

    const auto& presets = cl96::factoryPresets();
    for (int i = 0; i < (int) presets.size(); ++i) presetBox.addItem (presets[(size_t) i].name, i + 1);
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id > 0) { proc.loadPreset (id - 1); shownPreset = -2; }
    };
    addAndMakeVisible (presetBox);
    prevBtn.onClick = [this] { stepPreset (-1); };
    nextBtn.onClick = [this] { stepPreset (1); };
    addAndMakeVisible (prevBtn); addAndMakeVisible (nextBtn);

    setSize (W, H);
    tick();
}

Knob* MainPanel::addKnob (const juce::String& id, const juce::String& label, juce::Colour c, int diameter, bool big)
{
    const auto* spec = cl96::findSpec (id);
    const auto unit = spec != nullptr ? spec->unit : cl96::Unit::percent;
    auto fmt = [unit] (double v) { return cl96::formatValue (unit, v); };
    std::optional<float> ref;
    if (id == "out" || id == "lowDrive") ref = 0.0f;
    knobs.push_back (std::make_unique<Knob> (proc.apvts, id, label, c, fmt, diameter, big, ref));
    addAndMakeVisible (*knobs.back());
    return knobs.back().get();
}

void MainPanel::paint (juce::Graphics& g)
{
    auto& f = Fonts::get();
    juce::ColourGradient bg (col::panel2, 0, 0, col::panel, 0, (float) H, false);
    g.setGradientFill (bg);
    g.fillAll();
    g.setColour (juce::Colours::white.withAlpha (0.012f));
    for (int x = 0; x < W; x += 3) g.drawVerticalLine (x, 0, (float) (H - kBarH));

    // title strip
    if (logo.isValid())
    {
        const float lh = 48.0f, lw = lh * (float) logo.getWidth() / (float) logo.getHeight();
        g.setOpacity (1.0f);   // drawImage uses the current opacity, which the texture lines above left at 1 %
        g.drawImage (logo, juce::Rectangle<float> ((float) kPad, 10.0f, lw, lh), juce::RectanglePlacement::centred);
        const auto title = f.display (36.0f, 0.04f);
        const float tx = (float) kPad + lw + 18.0f;
        g.setFont (title);
        g.setColour (col::text);
        g.drawText ("CLIP", juce::Rectangle<float> (tx, 12.0f, 200.0f, 44.0f), juce::Justification::centredLeft, false);
        const float rw = juce::GlyphArrangement::getStringWidth (title, "CLIP ");
        g.setColour (col::amber);
        g.drawText ("96", juce::Rectangle<float> (tx + rw, 12.0f, 80.0f, 44.0f), juce::Justification::centredLeft, false);
    }
    g.setColour (col::gold);
    g.setFont (f.label (9.5f, 0.14f));
    g.drawText ("VERSION 0.1", bypassBtn.getX() - 220, 18, 200, 13, juce::Justification::centredRight, false);
    g.drawText ("BOOMTOWNBEATS.COM", bypassBtn.getX() - 220, 33, 200, 13, juce::Justification::centredRight, false);

    for (auto& grp : groups)
    {
        const auto r = grp.bounds.toFloat();
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.22f), 0, r.getY(), juce::Colours::black.withAlpha (0.05f), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (grp.colour);
        g.drawRoundedRectangle (r.reduced (1.0f), 13.0f, 2.0f);
        g.setFont (f.display (16.0f, 0.16f));
        g.drawText (grp.title.toUpperCase(), grp.bounds.withHeight (32).withTrimmedTop (8), juce::Justification::centred, false);
    }

    // monitor group: latency
    {
        const auto& gm = groups[3].bounds;
        g.setColour (col::faint);
        g.setFont (f.numbers (10.0f));
        const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
        const int lat = proc.getLatencyNow();
        g.drawText ("LATENCY", gm.getX(), gm.getBottom() - 44, gm.getWidth(), 14, juce::Justification::centred, false);
        g.drawText (juce::String (lat) + " smp " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + " " + juce::String (lat / sr * 1000.0, 1) + " ms",
                    gm.getX(), gm.getBottom() - 30, gm.getWidth(), 14, juce::Justification::centred, false);
    }
    paintMeters (g);

    // bottom bar
    const int by = H - kBarH;
    g.setColour (juce::Colours::black.withAlpha (0.22f));
    g.fillRect (0, by, W, kBarH);
    g.setColour (col::line);
    g.drawHorizontalLine (by, 0, (float) W);
    const auto barFont = f.display (17.0f, 0.12f);
    auto barLabel = [&] (const juce::Component& c, const juce::String& t)
    {
        g.setFont (barFont);
        g.setColour (col::text);
        const int w = (int) std::ceil (juce::GlyphArrangement::getStringWidth (barFont, t));
        g.drawText (t, c.getX() - w - 10, c.getY(), w + 2, c.getHeight(), juce::Justification::centredRight, false);
    };
    barLabel (modeBox, "MODE:");
    barLabel (qualityBox, "QUALITY:");
    barLabel (presetBox, "PRESETS:");
}

void MainPanel::paintOverChildren (juce::Graphics& g)
{
    for (auto pt : { juce::Point<float> (12, 12), { (float) W - 12, 12 }, { 12, (float) H - 12 }, { (float) W - 12, (float) H - 12 } })
    {
        juce::ColourGradient sg (juce::Colour (0xff6b5f52), pt.x - 2, pt.y - 2, juce::Colour (0xff2b241e), pt.x + 4, pt.y + 4, true);
        g.setGradientFill (sg);
        g.fillEllipse (pt.x - 5.5f, pt.y - 5.5f, 11, 11);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawLine (juce::Line<float> (pt.x - 3.5f, pt.y - 2.5f, pt.x + 3.5f, pt.y + 2.5f), 1.0f);
    }
}

void MainPanel::resized()
{
    bypassBtn.setBounds (W - kPad - 104, 18, 104, 30);

    const int top = kTopH + 8, bottom = H - kBarH - 12, mainH = bottom - top;
    // Ceiling / Output column
    const int colW = ceiling->naturalWidth(), colH = ceiling->naturalHeight();
    ceiling->setBounds (kPad - 6, top + 6, colW, colH);
    out->setBounds (kPad - 6, bottom - colH - 6, colW, colH);

    // big Push knob
    const int bw = push->naturalWidth(), bh = push->naturalHeight();
    const int bx = kPad - 6 + colW + 10;
    push->setBounds (bx, top + (mainH - bh) / 2, bw, bh);

    // Clip, Split, Limit, Monitor and a wider Meters group
    const int gx0 = bx + bw + 14, gx1 = W - kPad, gap = 10;
    const double unit = (gx1 - gx0 - 4 * gap) / 5.3;
    const int gw = (int) unit, mw = gx1 - gx0 - 4 * gap - 4 * gw;
    const int kw = pairs[0].first->naturalWidth(), kh = pairs[0].first->naturalHeight();
    for (int i = 0; i < 5; ++i)
    {
        auto r = juce::Rectangle<int> (gx0 + i * (gw + gap), top, i == 4 ? mw : gw, mainH);
        groups[(size_t) i].bounds = r;
        if (i < 3)
        {
            const int kx = r.getCentreX() - kw / 2;
            pairs[(size_t) i].first->setBounds (kx, r.getY() + 34, kw, kh);
            pairs[(size_t) i].second->setBounds (kx, r.getY() + 34 + kh + 8, kw, kh);
        }
    }
    const auto& gm = groups[3].bounds;
    deltaBtn.setBounds (gm.getCentreX() - 46, gm.getY() + 44, 92, 30);
    matchBtn.setBounds (gm.getCentreX() - 46, gm.getY() + 84, 92, 30);
    meterArea = groups[4].bounds.reduced (14, 0).withTrimmedTop (36).withTrimmedBottom (12);

    // bottom bar
    const int by = H - kBarH, cy = by + (kBarH - 30) / 2;
    modeBox.setBounds (kPad + 66, cy, 140, 30);
    qualityBox.setBounds (modeBox.getRight() + 40 + 92, cy, 130, 30);
    nextBtn.setBounds (W - kPad - 28, cy + 1, 28, 28);
    prevBtn.setBounds (nextBtn.getX() - 34, cy + 1, 28, 28);
    presetBox.setBounds (prevBtn.getX() - 12 - 220, cy, 220, 30);
}

void MainPanel::paintMeters (juce::Graphics& g)
{
    auto& f = Fonts::get();
    constexpr float floorDb = -36.0f, grMax = 18.0f;
    const auto a = meterArea;
    const int colW = a.getWidth() / 3;
    const float barW = 14.0f;
    const int labelH = 14, valueH = 16;
    struct M { const char* label; float level; float hold; bool gr; };
    const M ms[3] = { { "IN", lvIn, holdIn, false }, { "GR", lvGr, holdGr, true }, { "OUT", lvOut, holdOut, false } };
    for (int i = 0; i < 3; ++i)
    {
        const float cx = (float) a.getX() + colW * (i + 0.5f);
        const auto bar = juce::Rectangle<float> (cx - barW / 2, (float) a.getY() + labelH + 4, barW, (float) a.getHeight() - labelH - valueH - 8);
        g.setColour (ms[i].gr ? col::magenta : col::dim);
        g.setFont (f.label (9.0f, 0.12f));
        g.drawText (ms[i].label, juce::Rectangle<float> (cx - 30, (float) a.getY(), 60, (float) labelH), juce::Justification::centred, false);
        g.setColour (juce::Colour (0xff0a0807));
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (col::line);
        g.drawRoundedRectangle (bar, 3.0f, 1.0f);
        if (ms[i].gr)
        {
            const float h = bar.getHeight() * juce::jlimit (0.0f, 1.0f, ms[i].level / grMax);
            g.setColour (col::magenta);
            g.fillRoundedRectangle (bar.withHeight (h), 3.0f);
        }
        else
        {
            const float n = juce::jlimit (0.0f, 1.0f, ms[i].level);
            auto fill = bar.withTrimmedTop (bar.getHeight() * (1.0f - n));
            juce::ColourGradient grad (juce::Colour (0xff8fd14f), 0, bar.getBottom(), col::magenta, 0, bar.getY(), false);
            grad.addColour (0.6, juce::Colour (0xff8fd14f));
            grad.addColour (0.85, col::amber);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (fill, 3.0f);
            if (i == 2)
            {
                // ceiling marker, including Match and Output
                auto& s = proc.apvts;
                const float ceil = s.getRawParameterValue ("ceiling")->load() + s.getRawParameterValue ("out")->load()
                                 - (s.getRawParameterValue ("match")->load() > 0.5f ? s.getRawParameterValue ("push")->load() : 0.0f);
                const float cn = juce::jlimit (0.0f, 1.0f, (ceil - floorDb) / -floorDb);
                g.setColour (col::text.withAlpha (0.75f));
                g.fillRect (bar.getX() - 3, bar.getBottom() - bar.getHeight() * cn - 1, bar.getWidth() + 6, 2.0f);
            }
        }
        juce::String v;
        if (ms[i].gr) v = (ms[i].hold > 0.05f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) : juce::String()) + juce::String (ms[i].hold, 1);
        else v = ms[i].hold > -90.0f ? juce::String (ms[i].hold, 1) : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94"));
        g.setColour (ms[i].gr ? col::magenta : col::text);
        g.setFont (f.numbers (10.5f));
        g.drawText (v, juce::Rectangle<float> (cx - 30, bar.getBottom() + 4, 60, (float) valueH), juce::Justification::centred, false);
    }
}

void MainPanel::refreshPresetBox()
{
    const int idx = proc.getPresetIndex();
    const bool mod = proc.isPresetModified();
    if (idx == shownPreset && mod == shownModified) return;
    shownPreset = idx; shownModified = mod;
    const auto& presets = cl96::factoryPresets();
    const auto name = presets[(size_t) juce::jlimit (0, (int) presets.size() - 1, idx)].name;
    if (mod) presetBox.setText (name + " *", juce::dontSendNotification);
    else presetBox.setSelectedId (idx + 1, juce::dontSendNotification);
}

void MainPanel::stepPreset (int delta)
{
    const int n = (int) cl96::factoryPresets().size();
    proc.loadPreset ((proc.getPresetIndex() + delta + n) % n);
    shownPreset = -2;
    refreshPresetBox();
}

void MainPanel::tick()
{
    refreshPresetBox();

    constexpr float floorDb = -36.0f;
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float pIn = proc.pushPeak.exchange (0.0f), pOut = proc.outPeak.exchange (0.0f), gr = proc.grGain.exchange (1.0f);
    const float dIn = pIn > 1e-5f ? juce::Decibels::gainToDecibels (pIn) : -99.0f, dOut = pOut > 1e-5f ? juce::Decibels::gainToDecibels (pOut) : -99.0f;
    const float dGr = juce::jlimit (0.0f, 18.0f, -juce::Decibels::gainToDecibels (gr, -60.0f));
    const float nIn = juce::jlimit (0.0f, 1.0f, (dIn - floorDb) / -floorDb), nOut = juce::jlimit (0.0f, 1.0f, (dOut - floorDb) / -floorDb);
    lvIn = nIn > lvIn ? nIn : lvIn * 0.85f + nIn * 0.15f;
    lvOut = nOut > lvOut ? nOut : lvOut * 0.85f + nOut * 0.15f;
    lvGr = dGr > lvGr ? dGr : lvGr * 0.88f + dGr * 0.12f;
    auto hold = [now] (float v, float& h, double& t) { if (v >= h || now - t > 1200.0) { h = v; t = now; } };
    hold (dIn, holdIn, tIn); hold (dOut, holdOut, tOut); hold (dGr, holdGr, tGr);
    repaint (groups[4].bounds);

    const int lat = proc.getLatencyNow();
    if (lat != shownLatency) { shownLatency = lat; repaint (groups[3].bounds); }
}

//==============================================================================
Clip96Editor::Clip96Editor (Clip96Processor& p)
    : AudioProcessorEditor (p), panel (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (panel);
    setResizable (true, true);
    setResizeLimits ((int) (MainPanel::W * 0.7), (int) (MainPanel::H * 0.7), (int) (MainPanel::W * 1.6), (int) (MainPanel::H * 1.6));
    getConstrainer()->setFixedAspectRatio ((double) MainPanel::W / MainPanel::H);
    setSize (MainPanel::W, MainPanel::H);
    startTimerHz (30);
}

Clip96Editor::~Clip96Editor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void Clip96Editor::resized()
{
    const float s = (float) getWidth() / (float) MainPanel::W;
    panel.setTransform (juce::AffineTransform::scale (s));
    panel.setBounds (0, 0, MainPanel::W, MainPanel::H);
}
