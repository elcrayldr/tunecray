#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "CrayAutoTuneDSP.h"

class CrayAutoTuneProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    CrayAutoTuneProcessor();
    ~CrayAutoTuneProcessor() override { cancelPendingUpdate(); }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override { processBlock(b, m); } // el bypass lo gestiona el motor (alineado al PDC)

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter("bypass"); }

    const juce::String getName() const override { return "CRAY AutoTune LDR"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    cray::Engine& dsp() { return engine; }

private:
    void handleAsyncUpdate() override { setLatencySamples(engine.latencySamples()); }   // hilo de mensajes
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    cray::TuneParams readParams() const;

    cray::Engine engine;
    std::atomic<float> *pInput{}, *pKey{}, *pScale{}, *pRetune{}, *pHumanize{}, *pFlex{}, *pTracking{}, *pBypass{}, *pMode{},
                       *pSnap{}, *pGlide{}, *pTrans{}, *pFPres{}, *pFShift{}, *pGate{}, *pCorr{}, *pMix{}, *pGain{};
    int lastLatency = -1;
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CrayAutoTuneProcessor)
};
