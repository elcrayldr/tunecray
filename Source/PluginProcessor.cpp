#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout CrayAutoTuneProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto F = [](const char* id, const char* name, float lo, float hi, float def, float skewCentre, const char* unit) {
        NormalisableRange<float> r(lo, hi); r.setSkewForCentre(lo + (hi - lo) * skewCentre);
        return std::make_unique<AudioParameterFloat>(ParameterID{ id, 1 }, name, r, def, AudioParameterFloatAttributes().withLabel(unit));
    };
    auto Lin = [](const char* id, const char* name, float lo, float hi, float def, const char* unit) {
        return std::make_unique<AudioParameterFloat>(ParameterID{ id, 1 }, name, NormalisableRange<float>(lo, hi, 0.01f), def, AudioParameterFloatAttributes().withLabel(unit));
    };
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ "input", 1 }, "Input Type", StringArray{ "Low Male", "Alto/Tenor", "Soprano" }, 1));
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ "key", 1 }, "Key", StringArray{ "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }, 0));
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ "scale", 1 }, "Scale", StringArray{ "Chromatic","Major","Minor","Dorian","Mixolydian","Pentatonic","Minor Pent","Blues" }, 2));
    p.push_back(F("retune", "Retune Speed", 0.5f, 150.f, 10.f, 0.2f, " ms"));
    p.push_back(Lin("humanize", "Humanize", 0.f, 100.f, 0.f, " %"));
    p.push_back(Lin("flex", "Flex-Tune", 0.f, 100.f, 0.f, " %"));
    p.push_back(Lin("tracking", "Tracking", 0.f, 100.f, 50.f, " %"));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID{ "bypass", 1 }, "Bypass", false));
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ "mode", 1 }, "Mode", StringArray{ "Live (low latency)", "Quality (formants, high latency)" }, 0));
    p.push_back(Lin("snap", "Snap", 0.f, 100.f, 95.f, " %"));
    p.push_back(F("glide", "Glide", 0.f, 250.f, 20.f, 0.25f, " ms"));
    p.push_back(Lin("trans", "Transition Smooth", 0.f, 100.f, 65.f, " %"));
    p.push_back(Lin("fpres", "Formant Preserve", 0.f, 100.f, 80.f, " %"));
    p.push_back(Lin("fshift", "Formant Shift", -12.f, 12.f, 0.f, " st"));
    p.push_back(Lin("gate", "Gate Threshold", -70.f, -20.f, -48.f, " dB"));
    p.push_back(Lin("corr", "Correction Amount", 0.f, 100.f, 100.f, " %"));
    p.push_back(Lin("mix", "Mix", 0.f, 100.f, 100.f, " %"));
    p.push_back(Lin("gain", "Output Gain", -24.f, 12.f, 0.f, " dB"));
    return { p.begin(), p.end() };
}

CrayAutoTuneProcessor::CrayAutoTuneProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "STATE", createLayout())
{
    auto g = [this](const char* id) { return apvts.getRawParameterValue(id); };
    pInput = g("input"); pKey = g("key"); pScale = g("scale"); pRetune = g("retune"); pHumanize = g("humanize"); pFlex = g("flex");
    pTracking = g("tracking"); pBypass = g("bypass"); pMode = g("mode"); pSnap = g("snap"); pGlide = g("glide"); pTrans = g("trans");
    pFPres = g("fpres"); pFShift = g("fshift"); pGate = g("gate"); pCorr = g("corr"); pMix = g("mix"); pGain = g("gain");
}

cray::TuneParams CrayAutoTuneProcessor::readParams() const
{
    cray::TuneParams p;
    p.inputType = (int) pInput->load(); p.key = (int) pKey->load(); p.scale = (int) pScale->load();
    p.retuneMs = pRetune->load(); p.humanize = pHumanize->load() * 0.01f; p.flex = pFlex->load() * 0.01f; p.tracking = pTracking->load() * 0.01f;
    p.bypass = pBypass->load() > 0.5f; p.mode = (int) pMode->load();
    p.snap = pSnap->load() * 0.01f; p.glideMs = pGlide->load(); p.transSmooth = pTrans->load() * 0.01f;
    p.formantPreserve = pFPres->load() * 0.01f; p.formantShiftSt = pFShift->load();
    p.gateDb = pGate->load(); p.correction = pCorr->load() * 0.01f; p.mix = pMix->load() * 0.01f; p.outGainDb = pGain->load();
    return p;
}

bool CrayAutoTuneProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet(), i = l.getMainInputChannelSet();
    return o == i && (o == juce::AudioChannelSet::mono() || o == juce::AudioChannelSet::stereo());
}

void CrayAutoTuneProcessor::prepareToPlay(double sr, int)
{
    engine.prepare(sr);                       // TODA la reserva de memoria ocurre aqui, nunca en processBlock
    engine.setParams(readParams());
    lastLatency = engine.latencySamples();
    setLatencySamples(lastLatency);           // PDC = retardo medio REAL del modo activo (ver README)
    prepared = true;
}

void CrayAutoTuneProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, n);
    if (!prepared || n <= 0 || getTotalNumInputChannels() < 1) return;

    engine.setParams(readParams());           // solo lectura de atomics + aritmetica
    const int lat = engine.latencySamples();
    if (lat != lastLatency) { lastLatency = lat; triggerAsyncUpdate(); }   // cambia de modo/tipo de voz -> avisar al host

    float* l = buffer.getWritePointer(0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    engine.processInPlace(l, r, n);
}

juce::AudioProcessorEditor* CrayAutoTuneProcessor::createEditor() { return new CrayAutoTuneEditor(*this); }

void CrayAutoTuneProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, dest);
}
void CrayAutoTuneProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CrayAutoTuneProcessor(); }
