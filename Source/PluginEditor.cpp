#include "PluginEditor.h"

namespace {
    const char* kNotes[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    const juce::Colour bg{ 0xff0a0c10 }, panel{ 0xff13171e }, line{ 0xff2a303a }, yellow{ 0xffffcf33 }, muted{ 0xff9299a5 }, dark{ 0xff080a0d };
    juce::Font F(float h, bool bold = false) { return juce::Font(juce::FontOptions(h, bold ? juce::Font::bold : juce::Font::plain)); }
    int noteIdx(float m) { return ((int) std::lround(m) % 12 + 12) % 12; }
}

CrayLook::CrayLook()
{
    setColour(juce::Slider::rotarySliderFillColourId, yellow); setColour(juce::Slider::thumbColourId, yellow);
    setColour(juce::Slider::textBoxTextColourId, juce::Colours::white); setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::backgroundColourId, panel); setColour(juce::ComboBox::textColourId, juce::Colours::white);
    setColour(juce::ComboBox::outlineColourId, line); setColour(juce::ComboBox::arrowColourId, yellow);
    setColour(juce::PopupMenu::backgroundColourId, panel); setColour(juce::PopupMenu::textColourId, juce::Colours::white);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, yellow); setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
}

void CrayLook::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const float d = (float) juce::jmin(w, h) - 8.f, cx = (float) x + w * 0.5f, cy = (float) y + h * 0.5f, r = d * 0.5f;
    const float ang = a0 + pos * (a1 - a0); const bool en = s.isEnabled();
    g.setColour(juce::Colour(0xff1b2028)); g.fillEllipse(cx - r, cy - r, d, d);
    juce::Path track; track.addCentredArc(cx, cy, r - 3, r - 3, 0.f, a0, a1, true);
    g.setColour(line); g.strokePath(track, juce::PathStrokeType(3.f));
    juce::Path val; val.addCentredArc(cx, cy, r - 3, r - 3, 0.f, a0, ang, true);
    g.setColour(en ? yellow : muted.withAlpha(0.4f)); g.strokePath(val, juce::PathStrokeType(3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path tick; tick.addRectangle(-1.5f, -r + 6, 3.f, r * 0.42f);
    g.setColour(en ? juce::Colours::white : muted.withAlpha(0.5f)); g.fillPath(tick, juce::AffineTransform::rotation(ang).translated(cx, cy));
}

void CrayLook::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced(1.f); const bool on = b.getToggleState();
    g.setColour(on ? yellow : panel); g.fillRoundedRectangle(r, 5.f);
    g.setColour(on ? yellow : line); g.drawRoundedRectangle(r, 5.f, 1.2f);
    g.setColour(on ? juce::Colours::black : juce::Colours::white); g.setFont(F(12.f, true)); g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

CrayAutoTuneEditor::CrayAutoTuneEditor(CrayAutoTuneProcessor& p) : AudioProcessorEditor(&p), processor(p)
{
    setLookAndFeel(&look);
    brand.setText("CRAY LDR", juce::dontSendNotification); brand.setFont(F(20.f, true)); brand.setColour(juce::Label::textColourId, juce::Colours::white);
    product.setText("AUTOTUNE", juce::dontSendNotification); product.setFont(F(12.f, true)); product.setColour(juce::Label::textColourId, yellow);
    latencyLabel.setColour(juce::Label::textColourId, muted); latencyLabel.setFont(F(11.f)); latencyLabel.setJustificationType(juce::Justification::centredRight);
    for (auto* c : { &brand, &product, &latencyLabel }) { c->setInterceptsMouseClicks(false, false); addAndMakeVisible(*c); }

    juce::Label* lbs[4] = { &inputLbl, &keyLbl, &scaleLbl, &modeLbl }; const char* txts[4] = { "INPUT TYPE", "KEY", "SCALE", "MODE" };
    for (int i = 0; i < 4; ++i) { lbs[i]->setText(txts[i], juce::dontSendNotification); lbs[i]->setFont(F(10.f, true)); lbs[i]->setColour(juce::Label::textColourId, muted); addAndMakeVisible(*lbs[i]); }
    makeCombo(input, { "Low Male", "Alto/Tenor", "Soprano" }, "input", inputAtt);
    makeCombo(key, { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }, "key", keyAtt);
    makeCombo(scale, { "Chromatic","Major","Minor","Dorian","Mixolydian","Pentatonic","Minor Pent","Blues" }, "scale", scaleAtt);
    makeCombo(mode, { "Live (low latency)", "Quality (formants)" }, "mode", modeAtt);

    makeKnob(retune, "RETUNE SPEED", "retune", " ms"); makeKnob(humanize, "HUMANIZE", "humanize", " %"); makeKnob(flex, "FLEX-TUNE", "flex", " %");
    makeKnob(formant, "FORMANT", "fpres", " %"); makeKnob(tracking, "TRACKING", "tracking", " %");
    makeKnob(snap, "SNAP", "snap", " %"); makeKnob(glide, "GLIDE", "glide", " ms"); makeKnob(transition, "TRANSITION", "trans", " %");
    makeKnob(correction, "CORRECTION", "corr", " %"); makeKnob(gate, "GATE", "gate", " dB"); makeKnob(mix, "MIX", "mix", " %");
    makeKnob(output, "OUTPUT", "gain", " dB"); makeKnob(formantShift, "FORMANT SHIFT", "fshift", " st");
    formant.s.setTooltip("Solo en modo Quality (el modo Live es causal y no preserva formantes)");

    bypassAtt = std::make_unique<BA>(processor.apvts, "bypass", bypass); bypass.setClickingTogglesState(true); addAndMakeVisible(bypass);
    advanced.setClickingTogglesState(true); advanced.onClick = [this] { advancedOpen = advanced.getToggleState(); setSize(getWidth(), advancedOpen ? 560 : 400); };
    addAndMakeVisible(advanced);

    setResizable(true, true); setResizeLimits(620, 340, 1400, 900);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio(0.0);
    setSize(760, 400); startTimerHz(30);
}

CrayAutoTuneEditor::~CrayAutoTuneEditor() { stopTimer(); setLookAndFeel(nullptr); }

void CrayAutoTuneEditor::makeKnob(Knob& k, const juce::String& name, const juce::String& id, const juce::String& unit)
{
    k.s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag); k.s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
    k.s.setTextValueSuffix(unit); k.s.setDoubleClickReturnValue(true, processor.apvts.getParameter(id)->convertFrom0to1(processor.apvts.getParameter(id)->getDefaultValue()));
    k.l.setText(name, juce::dontSendNotification); k.l.setFont(F(10.f, true)); k.l.setColour(juce::Label::textColourId, muted); k.l.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(k.s); addAndMakeVisible(k.l);
    k.att = std::make_unique<SA>(processor.apvts, id, k.s);
}
void CrayAutoTuneEditor::makeCombo(juce::ComboBox& c, const juce::StringArray& items, const juce::String& id, std::unique_ptr<CA>& att)
{ c.addItemList(items, 1); addAndMakeVisible(c); att = std::make_unique<CA>(processor.apvts, id, c); }

void CrayAutoTuneEditor::timerCallback()
{
    auto& e = processor.dsp(); auto& t = e.tracker();
    midi = t.infoMidi.load(); target = t.infoTarget.load(); cents = t.infoCents.load(); voiced = t.infoVoiced.load();
    const double sr = juce::jmax(1.0, processor.getSampleRate());
    declared = e.latencySamples(); delayMs = (float) (1000.0 * e.liveDelaySamples() / sr);
    const int m = (int) processor.apvts.getRawParameterValue("mode")->load();
    if (m != shownMode) { shownMode = m; formant.s.setEnabled(m == 1); formantShift.s.setEnabled(m == 1); formant.l.setAlpha(m == 1 ? 1.f : 0.4f); formantShift.l.setAlpha(m == 1 ? 1.f : 0.4f); }
    latencyLabel.setText(m == 0 ? juce::String::formatted("LIVE  PDC %.1f ms  |  now %.1f ms", 1000.0 * declared / sr, delayMs)
                                : juce::String::formatted("QUALITY  PDC %.1f ms", 1000.0 * declared / sr), juce::dontSendNotification);
    repaint(0, 40, getWidth(), 130);
}

void CrayAutoTuneEditor::paint(juce::Graphics& g)
{
    const int W = getWidth(); g.fillAll(bg); g.setColour(yellow); g.fillRect(0, 0, W, 3);
    const juce::Rectangle<float> pn(16.f, 46.f, (float) W - 32.f, 116.f);
    g.setColour(panel); g.fillRoundedRectangle(pn, 9.f); g.setColour(line); g.drawRoundedRectangle(pn, 9.f, 1.f);
    auto box = [&](juce::Rectangle<float> r, bool tgt, const juce::String& big, const juce::String& small) {
        g.setColour(tgt ? juce::Colour(0xff29230f) : juce::Colour(0xff1b2028)); g.fillRoundedRectangle(r, 7.f);
        if (tgt) { g.setColour(yellow); g.drawRoundedRectangle(r, 7.f, 1.4f); }
        g.setColour(tgt ? yellow : (voiced > .5f ? juce::Colours::white : muted)); g.setFont(F(30.f, true)); g.drawText(big, r.withTrimmedBottom(26), juce::Justification::centred);
        g.setColour(muted); g.setFont(F(10.f, true)); g.drawText(small, r.withTrimmedTop(r.getHeight() - 24), juce::Justification::centred); };
    box({ pn.getX() + 12, pn.getY() + 12, 92.f, 92.f }, false, kNotes[noteIdx(midi)], juce::String(440.0 * std::pow(2.0, (midi - 69.0) / 12.0), 1) + " Hz  INPUT");
    box({ pn.getRight() - 104, pn.getY() + 12, 92.f, 92.f }, true, kNotes[noteIdx(target)], "TARGET");
    const float x = pn.getX() + 120, w = pn.getWidth() - 240, y = pn.getY() + 62;
    g.setColour(dark); g.fillRoundedRectangle(x, y, w, 14.f, 6.f);
    for (int i = -5; i <= 5; ++i) { g.setColour(i == 0 ? yellow.withAlpha(0.8f) : line); g.drawVerticalLine((int) (x + w * (0.5f + i * 0.1f)), y - (i % 5 == 0 ? 4.f : 0.f), y + 14.f + (i % 5 == 0 ? 4.f : 0.f)); }
    const float c = juce::jlimit(-50.f, 50.f, voiced > .5f ? cents : 0.f), px = x + w * (0.5f + c / 100.f);
    g.setColour(juce::Colour(0x33ffcf33)); g.fillRoundedRectangle(juce::jmin(px, x + w * 0.5f), y, std::abs(px - (x + w * 0.5f)), 14.f, 4.f);
    g.setColour(yellow); g.fillRoundedRectangle(px - 3, y - 5, 6.f, 24.f, 3.f);
    g.setColour(juce::Colours::white); g.setFont(F(12.f, true));
    g.drawText(voiced > .5f ? (juce::String(c > 0 ? "+" : "") + juce::String((int) std::lround(c)) + " CENTS") : juce::String("NO SIGNAL"), (int) x, (int) (y - 30), (int) w, 18, juce::Justification::centred);
    g.setColour(muted); g.setFont(F(10.f)); g.drawText("FLAT", (int) x, (int) (y + 22), 60, 12, juce::Justification::left); g.drawText("SHARP", (int) (x + w - 60), (int) (y + 22), 60, 12, juce::Justification::right);
    g.drawText("CRAY LDR  -  REAL-TIME VOCAL PITCH CORRECTION", 18, getHeight() - 18, 420, 12, juce::Justification::left);
}

void CrayAutoTuneEditor::layoutKnob(Knob& k, juce::Rectangle<int> r) { k.l.setBounds(r.removeFromTop(14)); k.s.setBounds(r); }

void CrayAutoTuneEditor::resized()
{
    const int W = getWidth(), H = getHeight();
    brand.setBounds(18, 10, 110, 28); product.setBounds(124, 15, 90, 20);
    bypass.setBounds(W - 100, 11, 84, 26); latencyLabel.setBounds(W - 400, 14, 290, 20);
    // fila de selectores
    int y = 172, x = 20; auto place = [&](juce::Label& lb, juce::ComboBox& cb, int w) { lb.setBounds(x, y, w, 12); cb.setBounds(x, y + 13, w, 26); x += w + 10; };
    place(inputLbl, input, 130); place(keyLbl, key, 80); place(scaleLbl, scale, 150);
    advanced.setBounds(W - 120, y + 13, 100, 26);
    // perillas principales
    const juce::Rectangle<int> row(16, 222, W - 32, 150); const int cols = 5, cw = row.getWidth() / cols;
    Knob* main[5] = { &retune, &humanize, &flex, &formant, &tracking };
    for (int i = 0; i < cols; ++i) layoutKnob(*main[i], { row.getX() + i * cw, row.getY(), cw - 6, row.getHeight() });
    // ADVANCED
    Knob* adv[8] = { &snap, &glide, &transition, &correction, &gate, &mix, &output, &formantShift };
    if (advancedOpen && H > 450) {
        modeLbl.setBounds(20, 384, 100, 12); mode.setBounds(20, 397, 190, 26);
        const juce::Rectangle<int> ar(16, 430, W - 32, H - 430 - 22); const int ac = 8, aw = ar.getWidth() / ac;
        for (int i = 0; i < ac; ++i) { adv[i]->s.setVisible(true); adv[i]->l.setVisible(true); layoutKnob(*adv[i], { ar.getX() + i * aw, ar.getY(), aw - 4, ar.getHeight() }); }
        mode.setVisible(true); modeLbl.setVisible(true);
    } else {
        for (auto* k : adv) { k->s.setVisible(false); k->l.setVisible(false); } mode.setVisible(false); modeLbl.setVisible(false);
    }
}
