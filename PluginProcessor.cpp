#include "PluginProcessor.h"
#include "PluginEditor.h"

using juce::String;

juce::AudioProcessorValueTreeState::ParameterLayout ChromaCompProcessor::createLayout()
{
    using NR = juce::NormalisableRange<float>;
    using Attr = juce::AudioParameterFloatAttributes;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto fromText = [] (const String& s) { return s.retainCharacters ("0123456789.-+").getFloatValue(); };
    auto add = [&] (const char* id, const char* name, NR range, float def, std::function<String (float, int)> toText)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, range, def,
            Attr().withStringFromValueFunction (std::move (toText)).withValueFromStringFunction (fromText)));
    };

    add ("threshold", "Threshold", NR (-60.0f, 0.0f, 0.1f), -18.0f,
         [] (float v, int) { return String (v, 1) + " dB"; });

    NR ratio (1.0f, 20.0f, 0.1f);  ratio.setSkewForCentre (4.0f);
    add ("ratio", "Ratio", ratio, 3.0f, [] (float v, int) { return String (v, 1) + ":1"; });

    add ("knee", "Knee", NR (0.0f, 30.0f, 0.1f), 8.0f, [] (float v, int) { return String (v, 1) + " dB"; });
    add ("kneeHardness", "Knee Hardness", NR (0.0f, 100.0f, 1.0f), 0.0f,
         [] (float v, int) { return String (juce::roundToInt (v)) + " %"; });   // 0 = soft (as before) .. 100 = hard

    NR atk (0.1f, 200.0f, 0.01f);  atk.setSkewForCentre (10.0f);
    add ("attack", "Attack", atk, 10.0f,
         [] (float v, int) { return String (v, v < 10.0f ? 2 : (v < 100.0f ? 1 : 0)) + " ms"; });

    NR rel (5.0f, 1000.0f, 0.1f);  rel.setSkewForCentre (120.0f);
    add ("release", "Release", rel, 120.0f, [] (float v, int) { return String (v, v < 100.0f ? 1 : 0) + " ms"; });

    add ("makeup", "Makeup", NR (0.0f, 24.0f, 0.1f), 0.0f, [] (float v, int) { return "+" + String (v, 1) + " dB"; });
    add ("mix", "Mix", NR (0.0f, 100.0f, 1.0f), 100.0f, [] (float v, int) { return String (juce::roundToInt (v)) + " %"; });

    // ---- hold / range / lookahead time / adapt ----
    NR hold (0.0f, 500.0f, 0.1f);  hold.setSkewForCentre (50.0f);
    add ("hold", "Hold", hold, 0.0f,
         [] (float v, int) { return v < 0.05f ? String ("Off") : String (v, v < 100.0f ? 1 : 0) + " ms"; });

    add ("range", "Range", NR (0.0f, 120.0f, 0.1f), 60.0f,
         [] (float v, int) { return String (v, 1) + " dB"; });

    add ("lookaheadTime", "Lookahead Time", NR (0.0f, chroma::Compressor::maxLookaheadMs, 0.01f),
         chroma::Compressor::lookaheadDefaultMs,
         [] (float v, int) { return String (v, 2) + " ms"; });

    add ("adapt", "Adapt", NR (0.0f, 100.0f, 1.0f), 0.0f,
         [] (float v, int) { return v < 0.5f ? String ("Off") : String (juce::roundToInt (v)) + " %"; });

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "feel", 1 }, "Feel",
                juce::StringArray { "Tight", "Subtle", "Balance", "Glue" }, 1));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "intensity", 1 }, "Intensity",
                juce::StringArray { "Low", "Mid", "High" }, 1));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "transients", 1 }, "Transients",
                juce::StringArray { "Preserve", "Unpreserved" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "lookahead", 1 }, "Lookahead", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "autoRelease", 1 }, "Auto Release", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "adaptiveOn", 1 }, "Adaptive Engine", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 }, "Bypass", false));
    return layout;
}

ChromaCompProcessor::ChromaCompProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pThr = apvts.getRawParameterValue ("threshold");  pRatio = apvts.getRawParameterValue ("ratio");
    pKnee = apvts.getRawParameterValue ("knee");      pAtk = apvts.getRawParameterValue ("attack");
    pRel = apvts.getRawParameterValue ("release");    pMakeup = apvts.getRawParameterValue ("makeup");
    pMix = apvts.getRawParameterValue ("mix");        pLook = apvts.getRawParameterValue ("lookahead");
    pBypass = apvts.getRawParameterValue ("bypass");
    pFeel = apvts.getRawParameterValue ("feel");  pInt = apvts.getRawParameterValue ("intensity");
    pTrans = apvts.getRawParameterValue ("transients");
    pHold = apvts.getRawParameterValue ("hold");              pRange = apvts.getRawParameterValue ("range");
    pLookTime = apvts.getRawParameterValue ("lookaheadTime"); pAuto = apvts.getRawParameterValue ("autoRelease");
    pAdapt = apvts.getRawParameterValue ("adapt");
    pKneeHard = apvts.getRawParameterValue ("kneeHardness");  pAdaptiveOn = apvts.getRawParameterValue ("adaptiveOn");
    presets.load ("Init");   // a fresh instance starts on the Init preset: no compression, adaptive engine off
}

bool ChromaCompProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& o = l.getMainOutputChannelSet();
    return (o == juce::AudioChannelSet::mono() || o == juce::AudioChannelSet::stereo())
           && o == l.getMainInputChannelSet();
}

void ChromaCompProcessor::prepareToPlay (double sr, int blockSize)
{
    const int channels = juce::jmax (1, getTotalNumInputChannels());

    // 4x oversampler. useIntegerLatency = true keeps the reported latency a whole number of host samples.
    using OS = juce::dsp::Oversampling<float>;
    osMaxBlock = juce::jmax (1, blockSize);
    oversampling = std::make_unique<OS> ((size_t) channels, (size_t) kOversampleStages,
                                         kLinearPhaseOversampling ? OS::filterHalfBandFIREquiripple
                                                                  : OS::filterHalfBandPolyphaseIIR,
                                         true, true);
    oversampling->initProcessing ((size_t) osMaxBlock);
    oversampling->reset();
    osLatency = (int) std::lround (oversampling->getLatencyInSamples());

    // lookahead is a whole number of *host* samples, so it is an exact multiple of the oversampling factor.
    // The time is a parameter now; processBlock re-derives these whenever it changes.
    hostSampleRate = sr;
    const float timeMs = pLookTime != nullptr ? pLookTime->load() : chroma::Compressor::lookaheadDefaultMs;
    lookaheadHost    = chroma::Compressor::lookaheadSamplesFor (timeMs, sr);
    lookaheadQuantMs = (float) ((double) lookaheadHost * 1000.0 / sr);

    loudIn.prepare (sr);  loudOut.prepare (sr);                 // BS.1770 K-weighting is sample-rate dependent
    comp.prepare (sr * (double) kOversampleFactor, channels);   // the core runs at 4x the host rate
    listener.prepare (sr);                                      // analysis stays at the host rate
    if (analysisState.load() != Idle) analysisState = Idle;
    scratch.assign ((size_t) juce::jmax (64, blockSize), 0.0f);
    paramsValid = false;

    const bool look = pLook != nullptr && pLook->load() > 0.5f;
    const int lat = osLatency + (look ? lookaheadHost : 0);
    latencyTarget = lat;
    setLatencySamples (lat);
}

void ChromaCompProcessor::reset()
{
    if (oversampling != nullptr) oversampling->reset();   // flush the up/down filter state
    comp.reset();
    loudIn.reset();  loudOut.reset();
}

// Up-sample -> compressor at 4x -> down-sample, in place. Dry and wet both travel through the oversampler, so the
// Mix knob and bypass stay phase-aligned. Host blocks longer than the prepared size are processed in chunks.
void ChromaCompProcessor::runCompressorOversampled (juce::AudioBuffer<float>& buffer, int numCh)
{
    if (oversampling == nullptr) return;                  // processBlock before prepareToPlay

    float* const* data = buffer.getArrayOfWritePointers();
    const int total = buffer.getNumSamples();

    for (int pos = 0; pos < total; pos += osMaxBlock)
    {
        const int n = juce::jmin (osMaxBlock, total - pos);
        juce::dsp::AudioBlock<float> block (data, (size_t) numCh, (size_t) pos, (size_t) n);

        auto up = oversampling->processSamplesUp (block);
        float* ptrs[2] = { up.getChannelPointer (0), numCh > 1 ? up.getChannelPointer (1) : nullptr };
        comp.process (ptrs, numCh, (int) up.getNumSamples());

        oversampling->processSamplesDown (block);
    }
}

void ChromaCompProcessor::feedAnalyzer (AnalyzerFifo& fifo, const juce::AudioBuffer<float>& b, int numCh)
{
    const int n = b.getNumSamples();
    const float* l = b.getReadPointer (0);
    const float* r = b.getReadPointer (numCh > 1 ? 1 : 0);
    for (int pos = 0; pos < n;)
    {
        const int m = juce::jmin ((int) scratch.size(), n - pos);
        for (int i = 0; i < m; ++i) scratch[(size_t) i] = 0.5f * (l[pos + i] + r[pos + i]);
        fifo.push (scratch.data(), m);
        pos += m;
    }
}

void ChromaCompProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (2, getTotalNumInputChannels());
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, n);
    if (numCh == 0 || n == 0) return;

    // null-safe parameter read: a missing parameter falls back to its default
    auto get = [] (const std::atomic<float>* a, float def) noexcept { return a != nullptr ? a->load() : def; };

    // lookahead time -> whole host samples (so latency is an integer and the 4x core delay is an exact multiple of 4)
    const bool look = get (pLook, 0.0f) > 0.5f;
    const float timeMs = juce::jlimit (0.0f, chroma::Compressor::maxLookaheadMs,
                                       get (pLookTime, chroma::Compressor::lookaheadDefaultMs));
    if (hostSampleRate > 0.0)
    {
        lookaheadHost    = chroma::Compressor::lookaheadSamplesFor (timeMs, hostSampleRate);
        lookaheadQuantMs = (float) ((double) lookaheadHost * 1000.0 / hostSampleRate);
    }

    chroma::Compressor::Params p;
    p.thresholdDb = get (pThr, -18.0f);   p.ratio     = get (pRatio, 3.0f);
    p.kneeDb      = get (pKnee, 8.0f);    p.attackMs  = get (pAtk, 10.0f);
    p.releaseMs   = get (pRel, 120.0f);   p.makeupDb  = get (pMakeup, 0.0f);
    const bool bypass = get (pBypass, 0.0f) > 0.5f;
    p.mix         = bypass ? 0.0f : get (pMix, 100.0f) * 0.01f;   // bypass = delay-matched dry, click-free
    p.lookahead   = look;
    p.lookaheadTimeMs = lookaheadQuantMs;   // whole host samples -> exact multiple of the oversampling factor
    p.holdMs      = get (pHold, 0.0f);
    p.rangeDb     = get (pRange, 60.0f);
    p.autoRelease = get (pAuto, 0.0f) > 0.5f;
    p.dynamicsAmount     = juce::jlimit (0.0f, 1.0f, get (pAdapt, 0.0f) * 0.01f);
    p.preserveTransients = get (pTrans, 0.0f) < 0.5f;   // existing "transients" choice: 0 = Preserve
    p.kneeHardness       = juce::jlimit (0.0f, 1.0f, get (pKneeHard, 0.0f) * 0.01f);
    p.adaptiveEnabled    = get (pAdaptiveOn, 1.0f) > 0.5f;   // off: detector + listener are not run at all

    // reported latency = oversampler latency + lookahead (host samples); re-report whenever it changes
    const int lat = osLatency + (look ? lookaheadHost : 0);
    if (! paramsValid || lat != latencyTarget.load (std::memory_order_relaxed))
    {
        latencyTarget = lat;
        triggerAsyncUpdate();
    }

    if (! paramsValid || ! (p == lastParams))
    {
        comp.setParams (p);
        lastParams = p;
        paramsValid = true;
    }

    // ---- adaptive listening: hears the input *before* compression ----
    int ast = analysisState.load (std::memory_order_relaxed);
    if (! p.adaptiveEnabled && (ast == Waiting || ast == Listening))
    {
        // adaptive engine switched off: abandon any running analysis, the listener is not touched
        int e = Waiting;  if (! analysisState.compare_exchange_strong (e, Idle)) { e = Listening; analysisState.compare_exchange_strong (e, Idle); }
        ast = analysisState.load (std::memory_order_relaxed);
    }
    if (ast == Waiting)
    {
        listener.begin();
        listenFeel = juce::roundToInt (get (pFeel, 1.0f));  listenInt = juce::roundToInt (get (pInt, 1.0f));
        listenTrans = juce::roundToInt (get (pTrans, 0.0f));
        analysisSeconds = 0.0f;
        analysisState = Listening;
        ast = Listening;
    }
    if (ast == Listening)
    {
        if (listenFeel != juce::roundToInt (get (pFeel, 1.0f)) || listenInt != juce::roundToInt (get (pInt, 1.0f))
            || listenTrans != juce::roundToInt (get (pTrans, 0.0f)))
        {
            analysisAborted = true;               // user changed feel / intensity / transients mid-listen
            analysisState = Idle;
        }
        else
        {
            const float* l = buffer.getReadPointer (0);
            const float* r = buffer.getReadPointer (numCh > 1 ? 1 : 0);
            const bool enough = listener.process (l, r, n);
            analysisSeconds = (float) listener.seconds();
            if (enough) { analysisState = Deciding; triggerAsyncUpdate(); }
        }
    }

    const bool analyze = analyzerOn.load (std::memory_order_relaxed);
    if (analyze)
    {
        feedAnalyzer (inFifo, buffer, numCh);
        loudIn.process (buffer.getArrayOfReadPointers(), numCh, n);
    }

    runCompressorOversampled (buffer, numCh);

    grMeterDb.store (bypass ? 0.0f : comp.takeBlockGrMax(), std::memory_order_relaxed);
    if (bypass) comp.takeBlockGrMax();
    if (analyze)
    {
        feedAnalyzer (outFifo, buffer, numCh);
        loudOut.process (buffer.getArrayOfReadPointers(), numCh, n);
    }
}

// ============================ Adaptive analyze ============================
void ChromaCompProcessor::startAnalysis()
{
    if (analysisState.load() != Idle) return;
    if (pAdaptiveOn != nullptr && pAdaptiveOn->load() < 0.5f)
    {
        analysisReport = "The adaptive engine is off. Switch it on to use Adaptive analyze.";
        return;
    }
    analysisOk = false;
    analysisAborted = false;
    analysisSeconds = 0.0f;
    analysisReport = "Play your track. Listening for at least 5 seconds...";
    analysisState = Waiting;
}

void ChromaCompProcessor::cancelAnalysis()
{
    int e = Waiting;  if (! analysisState.compare_exchange_strong (e, Idle)) { e = Listening; analysisState.compare_exchange_strong (e, Idle); }
    analysisReport = "Analysis cancelled. Press Adaptive analyze to listen again.";
}

void ChromaCompProcessor::handleAsyncUpdate()
{
    setLatencySamples (latencyTarget.load());
    if (analysisState.load() == Deciding) finishAnalysis();
}

void ChromaCompProcessor::finishAnalysis()
{
    if (pAdaptiveOn != nullptr && pAdaptiveOn->load() < 0.5f)   // switched off while deciding: apply nothing
    {
        analysisReport = "The adaptive engine is off. Switch it on to use Adaptive analyze.";
        analysisState = Idle;
        return;
    }
    const auto a = chroma::analyse (listener);
    if (! a.ok)
    {
        analysisReport = "Not enough signal to decide. Press Adaptive analyze again on a louder section.";
        analysisState = Idle;
        return;
    }
    const auto d = chroma::decide (a, juce::roundToInt (pFeel != nullptr ? pFeel->load() : 1.0f),
                                      juce::roundToInt (pInt != nullptr ? pInt->load() : 1.0f),
                                      (pTrans != nullptr ? pTrans->load() : 0.0f) < 0.5f);
    const float targets[6] = { d.threshold, d.ratio, d.knee, d.attack, d.release, d.makeup };
    tween.start (targets);
    analysisReport = juce::String (d.report);
    analysisOk = true;
    analysisState = Idle;
}

void ChromaCompProcessor::Tween::start (const float* targets)
{
    static const char* ids[6] = { "threshold", "ratio", "knee", "attack", "release", "makeup" };
    if (isTimerRunning()) for (auto* p : params) if (p != nullptr) p->endChangeGesture();
    for (int i = 0; i < 6; ++i)
    {
        params[i] = state.getParameter (ids[i]);
        if (params[i] == nullptr) continue;   // unknown ID: skip it (timerCallback also skips nulls)
        from[i] = params[i]->convertFrom0to1 (params[i]->getValue());
        to[i] = targets[i];
        params[i]->beginChangeGesture();
    }
    t0 = juce::Time::getMillisecondCounter();
    startTimerHz (60);
}

void ChromaCompProcessor::Tween::timerCallback()
{
    const float t = juce::jmin (1.0f, (float) (juce::Time::getMillisecondCounter() - t0) / 800.0f);
    const float e = 1.0f - std::pow (1.0f - t, 3.0f);                       // ease-out, as in the prototype
    for (int i = 0; i < 6; ++i)
        if (auto* p = params[i])
            p->setValueNotifyingHost (p->convertTo0to1 (from[i] + (to[i] - from[i]) * e));
    if (t >= 1.0f)
    {
        stopTimer();
        for (auto* p : params) if (p != nullptr) p->endChangeGesture();
    }
}

juce::AudioProcessorEditor* ChromaCompProcessor::createEditor() { return new ChromaCompEditor (*this); }

void ChromaCompProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void ChromaCompProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChromaCompProcessor(); }
