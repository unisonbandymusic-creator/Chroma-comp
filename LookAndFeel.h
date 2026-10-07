#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

// White surface + neon-blue accents, with soft 3D depth on knobs and buttons.
class ChromaLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    static inline const juce::Colour accent   { 0xff00a8ff };
    static inline const juce::Colour accentHi { 0xff63d3ff };
    static inline const juce::Colour ink      { 0xff1c2b3d };
    static inline const juce::Colour dim      { 0xff7f90a8 };
    static inline const juce::Colour line     { 0xffdbe3ee };
    static inline const juce::Colour panel    { 0xfff7f9fc };
    static inline const juce::Colour coral    { 0xffff3d71 };

    float fontScale = 1.0f;

    ChromaLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, juce::Colours::white);
        setColour (juce::Slider::textBoxTextColourId, ink);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.25f));
        setColour (juce::Label::textColourId, ink);
        setColour (juce::TextButton::textColourOffId, ink);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::ComboBox::textColourId, ink);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colours::white);
        setColour (juce::PopupMenu::textColourId, ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::PopupMenu::headerTextColourId, dim);
        setColour (juce::TextEditor::backgroundColourId, juce::Colours::white);
        setColour (juce::TextEditor::textColourId, ink);
        setColour (juce::TextEditor::outlineColourId, line);
        setColour (juce::TextEditor::focusedOutlineColourId, accent);
        setColour (juce::AlertWindow::backgroundColourId, juce::Colours::white);
        setColour (juce::AlertWindow::textColourId, ink);
        setColour (juce::AlertWindow::outlineColourId, line);
    }

    static juce::Font makeFont (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    }

    juce::Font getLabelFont (juce::Label& l) override
    {
        if (dynamic_cast<juce::Slider*> (l.getParentComponent()) != nullptr)
            return makeFont (12.5f * fontScale);
        return l.getFont();
    }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return makeFont ((float) h * 0.46f, true); }
    juce::Font getComboBoxFont (juce::ComboBox& c) override        { return makeFont ((float) c.getHeight() * 0.5f); }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        using namespace juce;
        const auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
        const float r = jmin (b.getWidth(), b.getHeight()) * 0.5f;
        const auto c = b.getCentre();
        const float ang = startAngle + pos * (endAngle - startAngle);
        const float thick = jmax (2.0f, r * 0.10f);
        const float arcR = r - thick;
        const bool hot = s.isMouseOverOrDragging();

        // track + neon value arc (with glow)
        Path track;  track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (line);
        g.strokePath (track, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));
        if (pos > 0.002f)
        {
            Path val;  val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, ang, true);
            g.setColour (accent.withAlpha (hot ? 0.34f : 0.20f));
            g.strokePath (val, PathStrokeType (thick * 2.6f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (accent);
            g.strokePath (val, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));
        }

        // knob body: soft shadow, domed gradient, rim, inset cap
        const float br = arcR - thick * 1.7f;
        for (int i = 0; i < 3; ++i)
        {
            g.setColour (Colours::black.withAlpha (0.055f));
            g.fillEllipse (c.x - br - i * 1.4f, c.y - br + 2.0f + i * 1.1f, (br + i * 1.4f) * 2.0f, (br + i * 1.4f) * 2.0f);
        }
        g.setGradientFill (ColourGradient (Colours::white, c.x - br * 0.45f, c.y - br * 0.55f,
                                           Colour (0xffcdd8e6), c.x + br * 0.75f, c.y + br * 0.85f, true));
        g.fillEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f);
        g.setColour (Colour (0xffb9c8da));
        g.drawEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f, 1.0f);
        g.setColour (Colours::white.withAlpha (0.9f));
        g.drawEllipse (c.x - br + 1.0f, c.y - br + 1.0f, br * 2.0f - 2.0f, br * 2.0f - 2.0f, 0.8f);

        const float cr = br * 0.64f;
        g.setGradientFill (ColourGradient (Colour (0xffe3eaf4), c.x - cr * 0.5f, c.y - cr * 0.6f,
                                           Colours::white, c.x + cr * 0.6f, c.y + cr * 0.7f, false));
        g.fillEllipse (c.x - cr, c.y - cr, cr * 2.0f, cr * 2.0f);
        g.setColour (Colour (0xffd0dbe8));
        g.drawEllipse (c.x - cr, c.y - cr, cr * 2.0f, cr * 2.0f, 0.8f);

        // pointer
        Path ptr;
        ptr.startNewSubPath (c.x + br * 0.30f * std::sin (ang), c.y - br * 0.30f * std::cos (ang));
        ptr.lineTo          (c.x + br * 0.90f * std::sin (ang), c.y - br * 0.90f * std::cos (ang));
        g.setColour (accent.withAlpha (0.28f));
        g.strokePath (ptr, PathStrokeType (thick * 1.6f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (ptr, PathStrokeType (jmax (2.0f, thick * 0.7f), PathStrokeType::curved, PathStrokeType::rounded));
    }

    // Slim neon horizontal slider (knee hardness). Other slider styles fall back to the stock drawing.
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        using namespace juce;
        if (style != Slider::LinearHorizontal)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
            return;
        }
        const float cy = (float) y + (float) h * 0.5f;
        const float thick = jlimit (3.0f, 6.0f, (float) h * 0.34f);
        const float tr = jmin (thick * 1.7f, (float) h * 0.5f - 0.5f);   // thumb radius
        const bool hot = s.isMouseOverOrDragging();

        Path track;  track.startNewSubPath (minPos, cy);  track.lineTo (maxPos, cy);
        g.setColour (line);
        g.strokePath (track, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));
        if (pos > minPos + 0.5f)
        {
            Path val;  val.startNewSubPath (minPos, cy);  val.lineTo (pos, cy);
            g.setColour (accent.withAlpha (hot ? 0.34f : 0.20f));
            g.strokePath (val, PathStrokeType (thick * 2.2f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (accent);
            g.strokePath (val, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));
        }

        // thumb: soft shadow, domed body, rim, accent dot
        g.setColour (Colours::black.withAlpha (0.08f));
        g.fillEllipse (pos - tr, cy - tr + 1.2f, tr * 2.0f, tr * 2.0f);
        g.setGradientFill (ColourGradient (Colours::white, pos - tr * 0.4f, cy - tr * 0.5f,
                                           Colour (0xffcdd8e6), pos + tr * 0.6f, cy + tr * 0.8f, true));
        g.fillEllipse (pos - tr, cy - tr, tr * 2.0f, tr * 2.0f);
        g.setColour (Colour (0xffb9c8da));
        g.drawEllipse (pos - tr, cy - tr, tr * 2.0f, tr * 2.0f, 1.0f);
        g.setColour (accent);
        g.fillEllipse (pos - tr * 0.38f, cy - tr * 0.38f, tr * 0.76f, tr * 0.76f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool highlighted, bool down) override
    {
        using namespace juce;
        const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const float cr = r.getHeight() * 0.3f;
        const bool on = b.getToggleState();

        if (on)
        {
            g.setColour (accent.withAlpha (0.25f));
            g.fillRoundedRectangle (r.expanded (1.5f), cr + 1.5f);
            g.setGradientFill (ColourGradient (accentHi, 0, r.getY(), accent, 0, r.getBottom(), false));
        }
        else
            g.setGradientFill (ColourGradient (Colours::white, 0, r.getY(), Colour (0xffeaf0f7), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, cr);

        g.setColour (on ? accent.darker (0.2f) : Colour (0xffc9d5e3));
        g.drawRoundedRectangle (r, cr, 1.0f);
        g.setColour (Colours::white.withAlpha (on ? 0.35f : 0.8f));
        g.drawLine (r.getX() + cr, r.getY() + 1.0f, r.getRight() - cr, r.getY() + 1.0f, 1.0f);

        if (highlighted && ! on) { g.setColour (accent.withAlpha (0.08f)); g.fillRoundedRectangle (r, cr); }
        if (down)                { g.setColour (Colours::black.withAlpha (0.08f)); g.fillRoundedRectangle (r, cr); }
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override
    {
        using namespace juce;
        const auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (1.0f);
        const float cr = r.getHeight() * 0.3f;
        g.setGradientFill (ColourGradient (Colours::white, 0, r.getY(), Colour (0xffeef3f9), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, cr);
        g.setColour (Colour (0xffc9d5e3));
        g.drawRoundedRectangle (r, cr, 1.0f);
        Path a;  const float cx = r.getRight() - h * 0.45f, cy = r.getCentreY(), s = h * 0.13f;
        a.startNewSubPath (cx - s, cy - s * 0.5f);  a.lineTo (cx, cy + s * 0.6f);  a.lineTo (cx + s, cy - s * 0.5f);
        g.setColour (accent);
        g.strokePath (a, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
    }
};
