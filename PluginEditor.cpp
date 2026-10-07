#include "PluginEditor.h"

ChromaCompEditor::ChromaCompEditor (ChromaCompProcessor& p)
    : AudioProcessorEditor (&p), proc (p), waveform (p),
      kThr (p.apvts, "threshold", "THRESHOLD"), kRatio (p.apvts, "ratio", "RATIO"),
      kKnee (p.apvts, "knee", "KNEE"),          kAtk (p.apvts, "attack", "ATTACK"),
      kRel (p.apvts, "release", "RELEASE"),     kMakeup (p.apvts, "makeup", "MAKEUP"),
      kMix (p.apvts, "mix", "MIX"),
      kHold (p.apvts, "hold", "HOLD"),          kRange (p.apvts, "range", "RANGE"),
      kLookTime (p.apvts, "lookaheadTime", "LOOK TIME"), kAdapt (p.apvts, "adapt", "ADAPT"),
      kneeHard (p.apvts, "kneeHardness"),
      feelRow (p.apvts, "feel", { "TIGHT", "SUBTLE", "BALANCE", "GLUE" }, 101),
      transRow (p.apvts, "transients", { "PRESERVE", "UNPRESERVED" }, 102),
      intRow (p.apvts, "intensity", { "LOW", "MID", "HIGH" }, 103)
{
    setLookAndFeel (&lnf);
    setOpaque (true);

    addAndMakeVisible (waveform);
    for (auto* k : { &kThr, &kRatio, &kKnee, &kAtk, &kRel, &kMakeup, &kMix, &kHold, &kRange, &kLookTime, &kAdapt })
        addAndMakeVisible (*k);
    addAndMakeVisible (kneeHard);

    for (auto* b : { &prevBtn, &nextBtn, &saveBtn, &delBtn, &lookBtn, &bypassBtn, &autoRelBtn, &adaptiveOnBtn })
        addAndMakeVisible (*b);
    addAndMakeVisible (presetBox);

    juce::Component* adaptiveUi[] = { &feelRow, &transRow, &intRow, &adaptBtn, &adaptBar, &feelCap, &transCap, &intCap, &adaptStatus, &report };
    for (auto* c : adaptiveUi)
        addAndMakeVisible (*c);
    for (auto* l : { &feelCap, &transCap, &intCap })
        l->setColour (juce::Label::textColourId, ChromaLookAndFeel::dim);
    feelCap.setText ("FEEL", juce::dontSendNotification);
    transCap.setText ("TRANSIENTS", juce::dontSendNotification);
    intCap.setText ("INTENSITY", juce::dontSendNotification);
    adaptStatus.setJustificationType (juce::Justification::centredRight);
    adaptStatus.setColour (juce::Label::textColourId, ChromaLookAndFeel::dim);
    report.setJustificationType (juce::Justification::topLeft);
    report.setMinimumHorizontalScale (1.0f);
    report.setColour (juce::Label::textColourId, ChromaLookAndFeel::dim);
    adaptBtn.onClick = [this]
    {
        if (proc.analysisState.load() == ChromaCompProcessor::Idle) proc.startAnalysis(); else proc.cancelAnalysis();
        timerCallback();
    };

    lookBtn.setClickingTogglesState (true);
    bypassBtn.setClickingTogglesState (true);
    autoRelBtn.setClickingTogglesState (true);
    adaptiveOnBtn.setClickingTogglesState (true);
    lookAtt    = std::make_unique<APVTS::ButtonAttachment> (p.apvts, "lookahead", lookBtn);
    bypassAtt  = std::make_unique<APVTS::ButtonAttachment> (p.apvts, "bypass", bypassBtn);
    autoRelAtt = std::make_unique<APVTS::ButtonAttachment> (p.apvts, "autoRelease", autoRelBtn);
    adaptiveOnAtt = std::make_unique<APVTS::ButtonAttachment> (p.apvts, "adaptiveOn", adaptiveOnBtn);

    prevBtn.onClick = [this] { if (proc.presets.step (-1)) refreshPresetBox(); };
    nextBtn.onClick = [this] { if (proc.presets.step (+1)) refreshPresetBox(); };
    saveBtn.onClick = [this] { showSaveDialog(); };
    delBtn.onClick  = [this] { showDeleteConfirm(); };
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (juce::isPositiveAndBelow (idx, presetNames.size()))
        {
            proc.presets.load (presetNames[idx]);
            updateDeleteEnabled();
        }
    };
    refreshPresetBox();

    // limits keep the same 600 : 552 ratio as kBaseW : kBaseH (480 x 442 ... 900 x 828)
    setResizable (true, true);
    setResizeLimits (480, 442, 900, 828);
    getConstrainer()->setFixedAspectRatio ((double) kBaseW / (double) kBaseH);
    setSize (kBaseW, kBaseH);

    proc.analyzerOn = true;   // the audio thread only feeds the analyser while the editor is open
    timerCallback();
    startTimerHz (15);
}

ChromaCompEditor::~ChromaCompEditor()
{
    stopTimer();
    proc.analyzerOn = false;
    setLookAndFeel (nullptr);
}

// The engine switch is a normal parameter (host automation / presets can flip it), so follow its button state here.
void ChromaCompEditor::syncAdaptiveEnabled()
{
    const bool on = adaptiveOnBtn.getToggleState();
    if (on == adaptiveShown) return;
    adaptiveShown = on;

    for (juce::Component* c : { (juce::Component*) &kAdapt, (juce::Component*) &feelRow, (juce::Component*) &transRow,
                                (juce::Component*) &intRow, (juce::Component*) &adaptBtn, (juce::Component*) &adaptBar })
    {
        c->setEnabled (on);
        c->setAlpha (on ? 1.0f : 0.4f);
    }
    if (! on)
    {
        if (proc.analysisState.load() != ChromaCompProcessor::Idle) proc.cancelAnalysis();
        proc.analysisReport = "The adaptive engine is off: Adapt and Adaptive analyze do nothing. Switch it on to use them.";
    }
    else
        proc.analysisReport = "Adaptive engine on. Pick a feel and intensity, play your track, then press Adaptive analyze.";
}

void ChromaCompEditor::timerCallback()
{
    syncAdaptiveEnabled();
    if (proc.analysisAborted.exchange (false))
        proc.analysisReport = "Settings changed. Press Adaptive analyze to listen again and retune.";

    const int st = proc.analysisState.load();
    const juce::String btn = st == ChromaCompProcessor::Idle ? "ADAPTIVE ANALYZE" : "CANCEL";
    if (adaptBtn.getButtonText() != btn) adaptBtn.setButtonText (btn);
    adaptBtn.setToggleState (st != ChromaCompProcessor::Idle, juce::dontSendNotification);

    juce::String status;
    float prog = 0.0f;
    switch (st)
    {
        case ChromaCompProcessor::Waiting:   status = "waiting for audio"; break;
        case ChromaCompProcessor::Listening:
        {
            const float sec = proc.analysisSeconds.load();
            prog = sec / 5.0f;
            status = "listening " + juce::String (juce::jmin (5.0f, sec), 1) + " / 5 s";
            break;
        }
        case ChromaCompProcessor::Deciding:  status = "analysing"; prog = 1.0f; break;
        default:                             status = proc.analysisOk ? "done" : "idle"; prog = proc.analysisOk ? 1.0f : 0.0f; break;
    }
    if (! adaptiveShown) { status = "engine off"; prog = 0.0f; }
    adaptBar.set (prog);
    adaptStatus.setText (status, juce::dontSendNotification);
    report.setText (proc.analysisReport, juce::dontSendNotification);
}

void ChromaCompEditor::refreshPresetBox()
{
    presetBox.clear (juce::dontSendNotification);
    presetNames.clear();

    int id = 1;
    presetBox.addSectionHeading ("Factory");
    for (auto& n : proc.presets.getFactoryNames()) { presetNames.add (n); presetBox.addItem (n, id++); }

    const auto user = proc.presets.getUserNames();
    if (! user.isEmpty())
    {
        presetBox.addSeparator();
        presetBox.addSectionHeading ("User");
        for (auto& n : user) { presetNames.add (n); presetBox.addItem (n, id++); }
    }

    const int cur = presetNames.indexOf (proc.presets.getCurrentName());
    if (cur >= 0) presetBox.setSelectedId (cur + 1, juce::dontSendNotification);
    updateDeleteEnabled();
}

void ChromaCompEditor::updateDeleteEnabled()
{
    delBtn.setEnabled (! proc.presets.isFactory (proc.presets.getCurrentName()));
}

void ChromaCompEditor::showSaveDialog()
{
    auto* aw = new juce::AlertWindow ("Save preset", "Preset name:", juce::MessageBoxIconType::NoIcon, this);
    const auto cur = proc.presets.getCurrentName();
    aw->addTextEditor ("name", proc.presets.isFactory (cur) ? juce::String() : cur, juce::String());
    aw->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<ChromaCompEditor> safe (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, aw] (int result)
    {
        if (result != 1 || safe == nullptr) return;
        const auto name = aw->getTextEditorContents ("name").trim();
        if (safe->proc.presets.save (name))
            safe->refreshPresetBox();
        else
            juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (
                juce::MessageBoxIconType::WarningIcon, "Could not save",
                "Please enter a name that is not a factory preset name."), [] (int) {});
    }), true);
}

void ChromaCompEditor::showDeleteConfirm()
{
    const auto name = proc.presets.getCurrentName();
    if (proc.presets.isFactory (name)) return;

    juce::Component::SafePointer<ChromaCompEditor> safe (this);
    juce::AlertWindow::showAsync (
        juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, "Delete preset",
                                                      "Delete \"" + name + "\"?", "Delete", "Cancel", this),
        [safe, name] (int result)
        {
            if (result != 1 || safe == nullptr) return;
            if (safe->proc.presets.remove (name))
            {
                safe->proc.presets.load ("Init");
                safe->refreshPresetBox();
            }
        });
}

void ChromaCompEditor::paint (juce::Graphics& g)
{
    using L = ChromaLookAndFeel;
    const float s = (float) getWidth() / (float) kBaseW;

    g.setGradientFill (juce::ColourGradient (juce::Colours::white, 0, 0, juce::Colour (0xfff1f5fa), 0, (float) getHeight(), false));
    g.fillAll();

    // title
    juce::GlyphArrangement ga;
    const auto f1 = L::makeFont (19.0f * s, true);
    ga.addLineOfText (f1, "CHROMA ", 0, 0);
    const float w1 = ga.getBoundingBox (0, -1, true).getWidth();
    g.setFont (f1);
    g.setColour (L::ink);
    g.drawText ("CHROMA", juce::Rectangle<float> (14 * s, 6 * s, 120 * s, 32 * s), juce::Justification::centredLeft);
    g.setColour (L::accent);
    g.drawText ("COMP", juce::Rectangle<float> (14 * s + w1, 6 * s, 80 * s, 32 * s), juce::Justification::centredLeft);

    // neon rule under the header
    const float ry = 44.0f * s;
    g.setColour (L::accent.withAlpha (0.18f));
    g.fillRect (12 * s, ry - 1.0f, getWidth() - 24 * s, 3.0f);
    g.setGradientFill (juce::ColourGradient (L::accent.withAlpha (0.0f), 12 * s, ry, L::accent, getWidth() * 0.5f, ry, false));
    g.fillRect (12 * s, ry, getWidth() * 0.5f - 12 * s, 1.0f);
    g.setGradientFill (juce::ColourGradient (L::accent, getWidth() * 0.5f, ry, L::accent.withAlpha (0.0f), getWidth() - 12 * s, ry, false));
    g.fillRect (getWidth() * 0.5f, ry, getWidth() * 0.5f - 12 * s, 1.0f);

    // adaptive card (bottom edge stays at 532; trimmed from 106 to 98 px tall to give the waveform more room)
    const auto card = juce::Rectangle<float> (12 * s, 434 * s, getWidth() - 24 * s, 98 * s);
    g.setColour (juce::Colours::white);  g.fillRoundedRectangle (card, 8.0f * s);
    g.setColour (L::line);               g.drawRoundedRectangle (card.reduced (0.5f), 8.0f * s, 1.0f);

    g.setColour (L::dim);
    g.setFont (L::makeFont (9.0f * s));
    g.drawText ("DEVELOPED BY HIM'Z DSP", juce::Rectangle<float> (14 * s, getHeight() - 17 * s, 220 * s, 14 * s), juce::Justification::left);
    g.drawText ("V1", juce::Rectangle<float> (getWidth() - 60 * s, getHeight() - 17 * s, 46 * s, 14 * s), juce::Justification::right);
}

void ChromaCompEditor::resized()
{
    const float s = (float) getWidth() / (float) kBaseW;
    auto S = [s] (float v) { return juce::roundToInt (v * s); };
    lnf.fontScale = s;

    // header row (preset browser + toggles)
    const int by = S (10), bh = S (26);
    prevBtn.setBounds   (S (156), by, S (22), bh);
    presetBox.setBounds (S (180), by, S (130), bh);
    nextBtn.setBounds   (S (312), by, S (22), bh);
    saveBtn.setBounds   (S (340), by, S (42), bh);
    delBtn.setBounds    (S (386), by, S (36), bh);
    lookBtn.setBounds   (S (430), by, S (88), bh);
    bypassBtn.setBounds (S (524), by, S (64), bh);

    // Layout budget (base px, total height 552): header 0-48, waveform 52-222, knob row 1 226-320 (+ hardness strip
    // 320-342 under Knee), knob row 2 342-430, adaptive card 434-532, footer text below.
    waveform.setBounds (S (12), S (52), getWidth() - S (24), S (170));

    // knob row 1: 7 columns
    const int ky = S (226), kh = S (94);
    const int kw = (getWidth() - S (24)) / 7;
    Knob* knobs[] = { &kThr, &kRatio, &kKnee, &kAtk, &kRel, &kMakeup, &kMix };
    for (int i = 0; i < 7; ++i)
        knobs[i]->setBounds (S (12) + i * kw, ky, kw, kh);

    // soft-to-hard slider directly underneath the Knee knob (column 3)
    kneeHard.setBounds (S (12) + 2 * kw + S (8), ky + kh, kw - S (16), S (22));

    // knob row 2: same column grid, so Hold / Range / Look time / Adapt line up under Threshold ... Attack
    const int ky2 = S (342), kh2 = S (88);
    Knob* knobs2[] = { &kHold, &kRange, &kLookTime, &kAdapt };
    for (int i = 0; i < 4; ++i)
        knobs2[i]->setBounds (S (12) + i * kw, ky2, kw, kh2);

    // Auto release + adaptive engine switch: stacked, columns 5-7 of row 2, centred vertically
    const int abh = S (26), agap = S (8);
    const int ax = S (12) + 4 * kw + S (6), aw = 3 * kw - S (12);
    const int ay = ky2 + (kh2 - (2 * abh + agap)) / 2;
    autoRelBtn.setBounds    (ax, ay,             aw, abh);
    adaptiveOnBtn.setBounds (ax, ay + abh + agap, aw, abh);

    // adaptive card contents (card spans 434-532)
    const float capF = 9.0f * s;
    for (auto* l : { &feelCap, &transCap, &intCap }) l->setFont (ChromaLookAndFeel::makeFont (capF, true));
    adaptStatus.setFont (ChromaLookAndFeel::makeFont (9.5f * s));
    report.setFont (ChromaLookAndFeel::makeFont (9.5f * s));

    const int ra = S (440), rb = S (468), rh = S (24);
    feelCap.setBounds  (S (22),  ra, S (40),  rh);   feelRow.setBounds  (S (64),  ra, S (250), rh);
    transCap.setBounds (S (326), ra, S (72),  rh);   transRow.setBounds (S (400), ra, S (188), rh);
    intCap.setBounds   (S (22),  rb, S (58),  rh);   intRow.setBounds   (S (82),  rb, S (170), rh);
    adaptBtn.setBounds (S (260), rb, S (132), rh);
    adaptBar.setBounds (S (400), rb, S (96),  rh);
    adaptStatus.setBounds (S (498), rb, S (90), rh);
    report.setBounds   (S (22),  S (496), getWidth() - S (44), S (34));
}
