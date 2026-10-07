#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <algorithm>
#include <array>
#include <cmath>
#include "PluginProcessor.h"
#include "LookAndFeel.h"

// Waveform display (scrolling min/max envelope of the input and / or output), with a gain-reduction meter and a
// loudness (LUFS) meter to its right, and IN / OUT buttons that choose what is shown and metered.
namespace wave
{
constexpr int   kCols      = 512;      // columns of history across the plot
constexpr float kWindowSec = 4.0f;     // time span shown by the plot
constexpr float kDesignW   = 576.0f;   // panel width the layout was designed at (editor 600 px - 2 x 12 px margins)
constexpr float kGrMaxDb   = 24.0f;    // full scale of the gain-reduction meter
constexpr float kLufsMin   = -50.0f, kLufsMax = 0.0f;

inline float lufsToUnit (float l) { return juce::jlimit (0.0f, 1.0f, (l - kLufsMin) / (kLufsMax - kLufsMin)); }

// Shared geometry so the grid, the waveform and the meters always agree.
struct Geo
{
    float s = 1.0f;
    juce::Rectangle<float> plot, grCol, loudCol;

    explicit Geo (juce::Rectangle<float> b)
    {
        s = b.getWidth() / kDesignW;
        auto inner = b.reduced (8.0f * s);
        auto meters = inner.removeFromRight (104.0f * s);
        inner.removeFromRight (8.0f * s);
        plot = inner;
        grCol = meters.removeFromLeft (42.0f * s);
        loudCol = meters;
    }
};

// A meter column = title row, bar area, value row.
struct Strip { juce::Rectangle<float> title, bar, value; };
inline Strip stripOf (juce::Rectangle<float> col, float s)
{
    Strip st;
    st.title = col.removeFromTop (12.0f * s);
    st.value = col.removeFromBottom (12.0f * s);
    st.bar   = col.reduced (0.0f, 4.0f * s);
    return st;
}

inline const juce::Colour inColour { 0xff8aa0bd };
}

// Animated waveform. Driven by the display's v-sync; costs nothing while the editor is closed.
class WaveformView final : public juce::Component
{
public:
    explicit WaveformView (ChromaCompProcessor& p) : proc (p), vblank (this, [this] { onFrame(); })
    {
        setOpaque (false);
        setInterceptsMouseClicks (false, false);
        lastMs = juce::Time::getMillisecondCounterHiRes();
    }

    void setShown (bool in, bool out) { showIn = in; showOut = out; repaint(); }

    void paint (juce::Graphics& g) override
    {
        using L = ChromaLookAndFeel;
        const wave::Geo geo (getLocalBounds().toFloat());
        const auto plot = geo.plot;
        const float s = geo.s, mid = plot.getCentreY(), half = plot.getHeight() * 0.5f - 1.0f;

        // grid: centre line and +-0.5 / +-1.0 (-6 dB / 0 dB) guides
        for (float amp : { 1.0f, 0.5f, 0.0f, -0.5f, -1.0f })
        {
            g.setColour (amp == 0.0f ? juce::Colour (0xffd3deeb) : juce::Colour (0xffe7edf5));
            g.fillRect (plot.getX(), mid - amp * half - 0.5f, plot.getWidth(), 1.0f);
        }
        g.setColour (L::dim);
        g.setFont (L::makeFont (juce::jmax (7.0f, 8.5f * s)));
        g.drawText ("0 dB", juce::Rectangle<float> (plot.getRight() - 34.0f * s, mid - half + 1.0f, 32.0f * s, 10.0f * s), juce::Justification::centredRight);
        g.drawText ("-6",   juce::Rectangle<float> (plot.getRight() - 34.0f * s, mid - 0.5f * half + 1.0f, 32.0f * s, 10.0f * s), juce::Justification::centredRight);
        g.drawText ("-4 s", juce::Rectangle<float> (plot.getX() + 2.0f * s, plot.getBottom() - 11.0f * s, 30.0f * s, 10.0f * s), juce::Justification::centredLeft);
        g.drawText ("now",  juce::Rectangle<float> (plot.getRight() - 32.0f * s, plot.getBottom() - 11.0f * s, 30.0f * s, 10.0f * s), juce::Justification::centredRight);

        g.saveState();
        g.reduceClipRegion (plot.getSmallestIntegerContainer());
        if (showIn)
        {
            build (inPath, plot, mid, half, false);
            g.setColour (wave::inColour.withAlpha (showOut ? 0.34f : 0.55f));
            g.fillPath (inPath);
            if (! showOut) { g.setColour (wave::inColour); g.strokePath (inPath, juce::PathStrokeType (1.0f)); }
        }
        if (showOut)
        {
            build (outPath, plot, mid, half, true);
            g.setColour (L::accent.withAlpha (0.45f));
            g.fillPath (outPath);
            g.setColour (L::accent);
            g.strokePath (outPath, juce::PathStrokeType (1.0f));
        }
        g.restoreState();
    }

private:
    struct Col { float inMin = 0.0f, inMax = 0.0f, outMin = 0.0f, outMax = 0.0f; };

    // closed polygon: upper envelope left -> right, lower envelope right -> left
    void build (juce::Path& path, juce::Rectangle<float> plot, float mid, float half, bool useOut) const
    {
        path.clear();
        const float x0 = plot.getX(), w = plot.getWidth();
        for (int j = 0; j < wave::kCols; ++j)
        {
            const Col& c = cols[(size_t) ((head + j) % wave::kCols)];
            const float hi = juce::jlimit (-1.0f, 1.0f, useOut ? c.outMax : c.inMax);
            const float x = x0 + w * (float) j / (float) (wave::kCols - 1), y = mid - hi * half - 0.4f;
            if (j == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        for (int j = wave::kCols - 1; j >= 0; --j)
        {
            const Col& c = cols[(size_t) ((head + j) % wave::kCols)];
            const float lo = juce::jlimit (-1.0f, 1.0f, useOut ? c.outMin : c.inMin);
            path.lineTo (x0 + w * (float) j / (float) (wave::kCols - 1), mid - lo * half + 0.4f);
        }
        path.closeSubPath();
    }

    void pushCol (const Col& c) { cols[(size_t) head] = c; head = (head + 1) % wave::kCols; }

    void accumulate (float a, float b, double colSamples)
    {
        if (curCount == 0) cur = Col { a, a, b, b };
        else
        {
            cur.inMin = std::min (cur.inMin, a);  cur.inMax = std::max (cur.inMax, a);
            cur.outMin = std::min (cur.outMin, b); cur.outMax = std::max (cur.outMax, b);
        }
        ++curCount;
        colAcc += 1.0;
        if (colAcc >= colSamples) { colAcc -= colSamples; pushCol (cur); curCount = 0; }
    }

    void onFrame()
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const float dt = (float) juce::jlimit (1.0, 100.0, now - lastMs);
        lastMs = now;

        const double sr = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 44100.0;
        const double colSamples = juce::jmax (1.0, sr * (double) wave::kWindowSec / (double) wave::kCols);

        int got = 0;
        for (;;)   // always drain both FIFOs together so they stay time-aligned, whatever is shown
        {
            const int n = juce::jmin ((int) tmpIn.size(), proc.inFifo.available(), proc.outFifo.available());
            if (n <= 0) break;
            proc.inFifo.pull (tmpIn.data(), n);
            proc.outFifo.pull (tmpOut.data(), n);
            for (int i = 0; i < n; ++i) accumulate (tmpIn[(size_t) i], tmpOut[(size_t) i], colSamples);
            got += n;
        }

        bool dirty = false;
        if (got > 0)
        {
            idleMs = 0.0f;  idleCols = 0;  dirty = true;
        }
        else if (idleCols < wave::kCols)   // no audio: scroll silence in so the trace clears, then stop repainting
        {
            curCount = 0;  colAcc = 0.0;
            idleMs += dt;
            const float colMs = 1000.0f * wave::kWindowSec / (float) wave::kCols;
            while (idleMs >= colMs && idleCols < wave::kCols)
            {
                pushCol (Col {});
                idleMs -= colMs;  ++idleCols;  dirty = true;
            }
        }
        if (dirty) repaint();
    }

    ChromaCompProcessor& proc;
    std::array<Col, (size_t) wave::kCols> cols {};
    int head = 0, curCount = 0, idleCols = wave::kCols;
    Col cur;
    double colAcc = 0.0, lastMs = 0.0;
    float idleMs = 0.0f;
    std::array<float, 1024> tmpIn {}, tmpOut {};
    juce::Path inPath, outPath;
    bool showIn = true, showOut = true;
    juce::VBlankAttachment vblank;   // keep last
};

// Gain-reduction meter + loudness meter (momentary LUFS of the shown source(s)).
class MeterView final : public juce::Component
{
public:
    explicit MeterView (ChromaCompProcessor& p) : proc (p), vblank (this, [this] { onFrame(); })
    {
        setOpaque (false);
        setInterceptsMouseClicks (false, false);
        lastMs = juce::Time::getMillisecondCounterHiRes();
    }

    void setShown (bool in, bool out) { showIn = in; showOut = out; repaint(); }

    void paint (juce::Graphics& g) override
    {
        using L = ChromaLookAndFeel;
        const wave::Geo geo (getLocalBounds().toFloat());
        const float s = geo.s;
        const auto smallFont = L::makeFont (juce::jmax (7.0f, 8.0f * s));
        const auto bold  = L::makeFont (juce::jmax (7.5f, 9.0f * s), true);

        // ---------------- gain reduction ----------------
        {
            const auto st = wave::stripOf (geo.grCol, s);
            g.setFont (bold);  g.setColour (L::dim);
            g.drawText ("GR", st.title, juce::Justification::centred);

            const juce::Rectangle<float> bar (st.bar.getX() + 4.0f * s, st.bar.getY(), 9.0f * s, st.bar.getHeight());
            g.setColour (juce::Colour (0xffe7edf5));  g.fillRoundedRectangle (bar, 3.0f * s);
            const float gh = bar.getHeight() * juce::jlimit (0.0f, 1.0f, dispGr / wave::kGrMaxDb);
            if (gh > 0.5f)
            {
                g.setGradientFill (juce::ColourGradient (L::coral, 0, bar.getY(), L::coral.withAlpha (0.55f), 0, bar.getBottom(), false));
                g.fillRoundedRectangle (bar.withHeight (gh), 3.0f * s);
            }
            g.setFont (smallFont);
            for (float db : { 0.0f, 3.0f, 6.0f, 12.0f, 18.0f, 24.0f })
            {
                const float y = bar.getY() + bar.getHeight() * db / wave::kGrMaxDb;
                g.setColour (juce::Colour (0xffc9d5e3));
                g.fillRect (bar.getRight() + 1.0f * s, y - 0.5f, 3.0f * s, 1.0f);
                if (db == 0.0f || db == 6.0f || db == 12.0f || db == 24.0f)
                {
                    g.setColour (L::dim);
                    g.drawText (juce::String ((int) db),
                                juce::Rectangle<float> (bar.getRight() + 5.0f * s, juce::jlimit (bar.getY(), bar.getBottom() - 9.0f * s, y - 4.5f * s), 20.0f * s, 9.0f * s),
                                juce::Justification::centredLeft);
                }
            }
            g.setFont (bold);  g.setColour (L::coral);
            g.drawText (juce::String (-dispGr, 1), st.value.expanded (4.0f * s, 0.0f), juce::Justification::centred);
        }

        // ---------------- loudness ----------------
        {
            const auto st = wave::stripOf (geo.loudCol, s);
            g.setFont (bold);  g.setColour (L::dim);
            g.drawText ("LUFS", st.title, juce::Justification::centred);

            const float bw = 8.0f * s, gap = 3.0f * s, x0 = st.bar.getX() + 4.0f * s;
            const int nBars = (showIn ? 1 : 0) + (showOut ? 1 : 0);
            float x = x0;
            auto drawBar = [&] (float lufs, juce::Colour c)
            {
                const juce::Rectangle<float> bar (x, st.bar.getY(), bw, st.bar.getHeight());
                g.setColour (juce::Colour (0xffe7edf5));  g.fillRoundedRectangle (bar, 3.0f * s);
                const float h = bar.getHeight() * wave::lufsToUnit (lufs);
                if (h > 0.5f)
                {
                    g.setGradientFill (juce::ColourGradient (c.brighter (0.25f), 0, bar.getY(), c, 0, bar.getBottom(), false));
                    g.fillRoundedRectangle (bar.withTop (bar.getBottom() - h), 3.0f * s);
                }
                x += bw + gap;
            };
            if (showIn)  drawBar (dispIn,  wave::inColour);
            if (showOut) drawBar (dispOut, L::accent);

            const float barsRight = x0 + (float) nBars * bw + (float) juce::jmax (0, nBars - 1) * gap;
            g.setFont (smallFont);
            for (float l = wave::kLufsMax; l >= wave::kLufsMin - 0.1f; l -= 10.0f)
            {
                const float y = st.bar.getY() + st.bar.getHeight() * (1.0f - wave::lufsToUnit (l));
                g.setColour (juce::Colour (0xffc9d5e3));
                g.fillRect (barsRight + 1.0f * s, y - 0.5f, 3.0f * s, 1.0f);
                g.setColour (L::dim);
                g.drawText (juce::String ((int) l),
                            juce::Rectangle<float> (barsRight + 5.0f * s, juce::jlimit (st.bar.getY(), st.bar.getBottom() - 9.0f * s, y - 4.5f * s), 24.0f * s, 9.0f * s),
                            juce::Justification::centredLeft);
            }
            // -14 LUFS reference (common streaming target)
            const float yRef = st.bar.getY() + st.bar.getHeight() * (1.0f - wave::lufsToUnit (-14.0f));
            g.setColour (L::ink.withAlpha (0.35f));
            g.fillRect (x0 - 2.0f * s, yRef - 0.5f, barsRight - x0 + 4.0f * s, 1.0f);

            // read-out: OUT when shown, otherwise IN
            const bool useOut = showOut;
            const float v = useOut ? dispOut : dispIn;
            g.setFont (bold);  g.setColour (useOut ? L::accent : wave::inColour.darker (0.2f));
            g.drawText (v <= -70.0f ? juce::String ("-inf") : juce::String (v, 1), st.value.expanded (6.0f * s, 0.0f), juce::Justification::centred);
        }
    }

private:
    static float follow (float cur, float target, float dt, float riseMs, float fallMs)
    {
        const float tc = target > cur ? riseMs : fallMs;
        return cur + (target - cur) * (1.0f - std::exp (-dt / tc));
    }

    void onFrame()
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const float dt = (float) juce::jlimit (1.0, 100.0, now - lastMs);
        lastMs = now;

        bool dirty = false;

        const float gr = proc.grMeterDb.load (std::memory_order_relaxed);
        const float prevGr = dispGr;
        dispGr = gr > dispGr ? gr : follow (dispGr, gr, dt, 1.0f, 220.0f);   // instant attack, slow release
        if (dispGr < 0.02f && gr == 0.0f) dispGr = 0.0f;
        if (std::fabs (dispGr - prevGr) > 0.01f) dirty = true;

        const float li = proc.loudIn.momentary.load (std::memory_order_relaxed);
        const float lo = proc.loudOut.momentary.load (std::memory_order_relaxed);
        const float pi = dispIn, po = dispOut;
        dispIn  = follow (dispIn,  li, dt, 60.0f, 250.0f);
        dispOut = follow (dispOut, lo, dt, 60.0f, 250.0f);
        if (std::fabs (dispIn - pi) > 0.05f || std::fabs (dispOut - po) > 0.05f) dirty = true;

        if (dirty) repaint();
    }

    ChromaCompProcessor& proc;
    bool showIn = true, showOut = true;
    double lastMs = 0.0;
    float dispGr = 0.0f, dispIn = chroma::LoudnessMeter::kFloorLufs, dispOut = chroma::LoudnessMeter::kFloorLufs;
    juce::VBlankAttachment vblank;   // keep last
};

class WaveformPanel final : public juce::Component
{
public:
    explicit WaveformPanel (ChromaCompProcessor& p) : proc (p), view (p), meters (p)
    {
        addAndMakeVisible (view);
        addAndMakeVisible (meters);
        addAndMakeVisible (inBtn);
        addAndMakeVisible (outBtn);

        // the choice is stored in the plugin state, so it survives closing the editor and reloading the session
        bool in  = (bool) proc.apvts.state.getProperty ("monitorIn", true);
        bool out = (bool) proc.apvts.state.getProperty ("monitorOut", true);
        if (! in && ! out) in = out = true;

        for (auto* b : { &inBtn, &outBtn }) b->setClickingTogglesState (true);
        inBtn.setToggleState (in, juce::dontSendNotification);
        outBtn.setToggleState (out, juce::dontSendNotification);
        inBtn.onClick  = [this] { userToggled (inBtn); };
        outBtn.onClick = [this] { userToggled (outBtn); };
        applyShown();
    }

    void paint (juce::Graphics& g) override
    {
        using L = ChromaLookAndFeel;
        const auto b = getLocalBounds().toFloat();
        const wave::Geo geo (b);
        g.setColour (L::panel);  g.fillRoundedRectangle (b.reduced (0.5f), 8.0f);
        g.setColour (L::line);   g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (juce::Colour (0xffe3eaf3));   // divider between waveform and meters
        g.fillRect (geo.plot.getRight() + 4.0f * geo.s - 0.5f, b.getY() + 10.0f * geo.s, 1.0f, b.getHeight() - 20.0f * geo.s);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        // colour key: a dot in each button matches its trace (grey-blue = input, blue = output)
        const wave::Geo geo (getLocalBounds().toFloat());
        const float r = 2.6f * geo.s;
        auto dot = [&] (const juce::Button& b, juce::Colour c)
        {
            const auto bb = b.getBounds().toFloat();
            g.setColour (c);
            g.fillEllipse (bb.getX() + 7.0f * geo.s - r, bb.getCentreY() - r, r * 2.0f, r * 2.0f);
        };
        dot (inBtn, wave::inColour);
        dot (outBtn, outBtn.getToggleState() ? juce::Colours::white : ChromaLookAndFeel::accent);
    }

    void resized() override
    {
        view.setBounds (getLocalBounds());
        meters.setBounds (getLocalBounds());
        const wave::Geo geo (getLocalBounds().toFloat());
        const float s = geo.s;
        const int bh = juce::roundToInt (18.0f * s);
        const int x = juce::roundToInt (geo.plot.getX() + 6.0f * s), y = juce::roundToInt (geo.plot.getY() + 6.0f * s);
        inBtn.setBounds  (x, y, juce::roundToInt (36.0f * s), bh);
        outBtn.setBounds (x + juce::roundToInt (40.0f * s), y, juce::roundToInt (42.0f * s), bh);
    }

private:
    void userToggled (juce::TextButton& clicked)
    {
        // never let both go off: the last active source stays on
        if (! inBtn.getToggleState() && ! outBtn.getToggleState())
            clicked.setToggleState (true, juce::dontSendNotification);
        applyShown();
    }

    void applyShown()
    {
        const bool in = inBtn.getToggleState(), out = outBtn.getToggleState();
        proc.apvts.state.setProperty ("monitorIn", in, nullptr);
        proc.apvts.state.setProperty ("monitorOut", out, nullptr);
        view.setShown (in, out);
        meters.setShown (in, out);
        repaint();
    }

    ChromaCompProcessor& proc;
    WaveformView view;
    MeterView meters;
    juce::TextButton inBtn { "IN" }, outBtn { "OUT" };
};
