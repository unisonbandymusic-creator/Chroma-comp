#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "LookAndFeel.h"
#include "WaveformDisplay.h"

using APVTS = juce::AudioProcessorValueTreeState;

// One caption + rotary knob + value read-out, bound to a parameter.
class Knob final : public juce::Component
{
public:
    Knob (APVTS& apvts, const juce::String& paramId, const juce::String& caption)
        : slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow)
    {
        addAndMakeVisible (slider);
        addAndMakeVisible (label);
        label.setText (caption, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setInterceptsMouseClicks (false, false);
        label.setColour (juce::Label::textColourId, ChromaLookAndFeel::dim);

        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        slider.setMouseDragSensitivity (220);
        slider.setTextBoxIsEditable (true);
        attachment = std::make_unique<APVTS::SliderAttachment> (apvts, paramId, slider);
        if (auto* p = apvts.getParameter (paramId))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (juce::jmax (12, (int) (getHeight() * 0.12f)));
        label.setBounds (top);
        label.setFont (ChromaLookAndFeel::makeFont ((float) top.getHeight() * 0.82f, true));
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, getWidth(), juce::jmax (14, (int) (getHeight() * 0.13f)));
        slider.setBounds (r);
    }

private:
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

// Slim horizontal soft-to-hard slider with a SOFT / HARD caption strip and the value read-out, bound to a parameter.
class HardnessSlider final : public juce::Component
{
public:
    HardnessSlider (APVTS& apvts, const juce::String& paramId)
    {
        addAndMakeVisible (slider);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.onValueChange = [this] { repaint(); };
        attachment = std::make_unique<APVTS::SliderAttachment> (apvts, paramId, slider);
        if (auto* p = apvts.getParameter (paramId))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (captionHeight());
        slider.setBounds (r);
    }

    void paint (juce::Graphics& g) override
    {
        using L = ChromaLookAndFeel;
        const auto cap = getLocalBounds().removeFromTop (captionHeight());
        g.setFont (L::makeFont ((float) cap.getHeight() * 0.9f, true));
        g.setColour (L::dim);
        g.drawText ("SOFT", cap, juce::Justification::centredLeft, false);
        g.drawText ("HARD", cap, juce::Justification::centredRight, false);
        g.setColour (L::ink);
        g.drawText (slider.getTextFromValue (slider.getValue()), cap, juce::Justification::centred, false);
    }

private:
    int captionHeight() const { return juce::jmax (8, (int) ((float) getHeight() * 0.42f)); }

    juce::Slider slider;
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

// Radio-style segmented selector bound to a choice parameter (Feel / Intensity / Transients).
class SegmentRow final : public juce::Component
{
public:
    SegmentRow (APVTS& apvts, const juce::String& paramId, const juce::StringArray& names, int radioGroup)
        : param (*apvts.getParameter (paramId)),
          attach (param, [this] (float v) { setIndex (juce::roundToInt (v)); })
    {
        for (int i = 0; i < names.size(); ++i)
        {
            auto* b = buttons.add (new juce::TextButton (names[i]));
            addAndMakeVisible (b);
            b->setClickingTogglesState (true);
            b->setRadioGroupId (radioGroup);
            b->onClick = [this, i] { attach.setValueAsCompleteGesture ((float) i); };
        }
        attach.sendInitialUpdate();
    }

    void resized() override
    {
        const int n = buttons.size(), gap = 2;
        const int w = (getWidth() - gap * (n - 1)) / juce::jmax (1, n);
        for (int i = 0; i < n; ++i) buttons[i]->setBounds (i * (w + gap), 0, w, getHeight());
    }

private:
    void setIndex (int idx)
    {
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState (i == idx, juce::dontSendNotification);
    }

    juce::OwnedArray<juce::TextButton> buttons;   // declared before attach: attach is destroyed first
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attach;
};

// Slim neon progress bar
class MiniBar final : public juce::Component
{
public:
    void set (float v) { v = juce::jlimit (0.0f, 1.0f, v); if (v != value) { value = v; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        using L = ChromaLookAndFeel;
        const auto r = getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), juce::jmin (8.0f, (float) getHeight()));
        g.setColour (juce::Colour (0xffe3eaf3));  g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        if (value > 0.0f)
        {
            auto f = r.withWidth (juce::jmax (r.getHeight(), r.getWidth() * value));
            g.setColour (L::accent.withAlpha (0.25f));  g.fillRoundedRectangle (f.expanded (0.0f, 2.0f), r.getHeight());
            g.setGradientFill (juce::ColourGradient (L::accentHi, f.getX(), 0, L::accent, f.getRight(), 0, false));
            g.fillRoundedRectangle (f, r.getHeight() * 0.5f);
        }
    }
private:
    float value = 0.0f;
};

class ChromaCompEditor final : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit ChromaCompEditor (ChromaCompProcessor&);
    ~ChromaCompEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refreshPresetBox();
    void showSaveDialog();
    void showDeleteConfirm();
    void updateDeleteEnabled();
    void timerCallback() override;

    // A second knob row (Hold / Range / Look time / Adapt + Auto release) was added under the first one,
    // so the base height grew from 448 to 552. Width and the fixed-aspect-ratio logic are unchanged.
    static constexpr int kBaseW = 600, kBaseH = 552;

    void syncAdaptiveEnabled();      // greys out / disables the adaptive controls to follow the engine switch
    bool adaptiveShown = true;

    ChromaCompProcessor& proc;
    ChromaLookAndFeel lnf;

    WaveformPanel waveform;     // waveform + GR meter + loudness meter + IN / OUT buttons
    Knob kThr, kRatio, kKnee, kAtk, kRel, kMakeup, kMix;
    Knob kHold, kRange, kLookTime, kAdapt;      // second row: parameter ids hold / range / lookaheadTime / adapt
    HardnessSlider kneeHard;                    // under the Knee knob: parameter id kneeHardness

    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "SAVE" }, delBtn { "DEL" };
    juce::TextButton lookBtn { "LOOKAHEAD" }, bypassBtn { "BYPASS" };
    juce::TextButton autoRelBtn { "AUTO RELEASE" }, adaptiveOnBtn { "ADAPTIVE ENGINE" };
    SegmentRow feelRow, transRow, intRow;
    juce::TextButton adaptBtn { "ADAPTIVE ANALYZE" };
    MiniBar adaptBar;
    juce::Label feelCap, transCap, intCap, adaptStatus, report;
    juce::ComboBox presetBox;
    juce::StringArray presetNames;
    std::unique_ptr<APVTS::ButtonAttachment> lookAtt, bypassAtt, autoRelAtt, adaptiveOnAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChromaCompEditor)
};
