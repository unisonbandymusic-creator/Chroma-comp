#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>       // dsp::Oversampling (add juce_dsp to target_link_libraries / the Projucer modules)
#include <memory>
#include <array>
#include <atomic>
#include <cstring>
#include <vector>
#include "CompressorCore.h"
#include "LoudnessMeter.h"
#include "AdaptiveEngine.h"
#include "PresetManager.h"

// Single-producer / single-consumer sample FIFO used to feed the waveform display.
struct AnalyzerFifo
{
    static constexpr int size = 1 << 14;

    void push (const float* src, int n) noexcept
    {
        int s1, z1, s2, z2;
        fifo.prepareToWrite (n, s1, z1, s2, z2);
        if (z1 > 0) std::memcpy (data.data() + s1, src, (size_t) z1 * sizeof (float));
        if (z2 > 0) std::memcpy (data.data() + s2, src + z1, (size_t) z2 * sizeof (float));
        fifo.finishedWrite (z1 + z2);
    }
    int pull (float* dst, int n) noexcept
    {
        int s1, z1, s2, z2;
        fifo.prepareToRead (n, s1, z1, s2, z2);
        if (z1 > 0) std::memcpy (dst, data.data() + s1, (size_t) z1 * sizeof (float));
        if (z2 > 0) std::memcpy (dst + z1, data.data() + s2, (size_t) z2 * sizeof (float));
        fifo.finishedRead (z1 + z2);
        return z1 + z2;
    }
    int available() const noexcept { return fifo.getNumReady(); }

private:
    juce::AbstractFifo fifo { size };
    std::array<float, (size_t) size> data {};
};

class ChromaCompProcessor final : public juce::AudioProcessor,
                                  private juce::AsyncUpdater
{
public:
    ChromaCompProcessor();
    ~ChromaCompProcessor() override { cancelPendingUpdate(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Chroma Comp"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter ("bypass"); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets { apvts };

    // ---- Adaptive analyze (message thread API; audio thread does the listening) ----
    enum AnalysisState { Idle = 0, Waiting = 1, Listening = 2, Deciding = 3 };
    void startAnalysis();
    void cancelAnalysis();
    std::atomic<int>   analysisState { Idle };
    std::atomic<float> analysisSeconds { 0.0f };
    std::atomic<bool>  analysisAborted { false };
    juce::String analysisReport = "Pick a feel and intensity, play your track, then press Adaptive analyze. It listens for at least 5 seconds before it sets the controls.";
    bool analysisOk = false;

    // read by the GUI
    std::atomic<float> grMeterDb { 0.0f };
    std::atomic<bool>  analyzerOn { false };
    AnalyzerFifo inFifo, outFifo;
    chroma::LoudnessMeter loudIn, loudOut;     // momentary LUFS of the input / output (run only while the editor is open)

private:
    void handleAsyncUpdate() override;
    void finishAnalysis();

    struct Tween final : private juce::Timer
    {
        explicit Tween (juce::AudioProcessorValueTreeState& s) : state (s) {}
        ~Tween() override
        {
            // balance any gestures still open if we are destroyed mid-tween
            if (isTimerRunning())
            {
                stopTimer();
                for (auto* p : params) if (p != nullptr) p->endChangeGesture();
            }
        }
        void start (const float* targets);
    private:
        void timerCallback() override;
        juce::AudioProcessorValueTreeState& state;
        float from[6] {}, to[6] {};
        juce::RangedAudioParameter* params[6] {};
        juce::uint32 t0 = 0;
    } tween { apvts };

    chroma::Listener listener;
    int listenFeel = 0, listenInt = 0, listenTrans = 0;
    void feedAnalyzer (AnalyzerFifo&, const juce::AudioBuffer<float>&, int numCh);

    // ---- 4x oversampling around the compressor core ----
    // The compressor runs at 4x the host rate; its own time constants, lookahead and detector are all derived from
    // that rate in prepare(). Reported latency = oversampling filter latency + lookahead, both in host samples.
    static constexpr int  kOversampleStages = 2;                      // 2^2 = 4x
    static constexpr int  kOversampleFactor = 1 << kOversampleStages;
    static constexpr bool kLinearPhaseOversampling = true;            // FIR equiripple (linear phase, more latency) vs polyphase IIR
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    int    osMaxBlock = 512;           // largest block the oversampler was prepared for (longer host blocks are chunked)
    int    osLatency = 0;              // oversampling filter latency, host samples
    double hostSampleRate = 0.0;       // set in prepareToPlay; used to round the lookahead time to host samples
    int    lookaheadHost = 0;          // lookahead length, host samples (a whole number, so latency stays an integer);
                                       // re-derived from the "lookaheadTime" parameter every block (audio thread only)
    float  lookaheadQuantMs = chroma::Compressor::lookaheadMs;   // lookaheadHost expressed in ms (-> Params::lookaheadTimeMs)
    void runCompressorOversampled (juce::AudioBuffer<float>&, int numCh);

    chroma::Compressor comp;
    chroma::Compressor::Params lastParams;
    bool paramsValid = false;
    std::atomic<int> latencyTarget { 0 };
    std::vector<float> scratch;

    std::atomic<float>* pThr = nullptr; std::atomic<float>* pRatio = nullptr; std::atomic<float>* pKnee = nullptr;
    std::atomic<float>* pAtk = nullptr; std::atomic<float>* pRel = nullptr;   std::atomic<float>* pMakeup = nullptr;
    std::atomic<float>* pMix = nullptr; std::atomic<float>* pLook = nullptr;  std::atomic<float>* pBypass = nullptr;
    std::atomic<float>* pFeel = nullptr; std::atomic<float>* pInt = nullptr;  std::atomic<float>* pTrans = nullptr;
    std::atomic<float>* pHold = nullptr;     std::atomic<float>* pRange = nullptr;   // hold ms, range dB
    std::atomic<float>* pLookTime = nullptr; std::atomic<float>* pAuto = nullptr;    // lookahead ms, auto release (bool)
    std::atomic<float>* pAdapt = nullptr;                                            // adapt amount, 0..100 %
    std::atomic<float>* pKneeHard = nullptr;                                         // knee hardness, 0..100 %
    std::atomic<float>* pAdaptiveOn = nullptr;                                       // adaptive engine master switch (bool)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChromaCompProcessor)
};
