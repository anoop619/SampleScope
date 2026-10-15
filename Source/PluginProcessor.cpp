#include "PluginProcessor.h"
#include "PluginEditor.h"

SampleScopeProcessor::SampleScopeProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false))
{
    state.setProperty (idSyncTempo, true, nullptr);
    state.setProperty (idSyncKey, true, nullptr);
    state.setProperty (idKeyMode, 0, nullptr);            // 0 = detect from input, 1..24 = fixed key + 1
    state.setProperty (idTranspose, 0, nullptr);
    state.setProperty (idWarpMode, (int) ss::WarpMode::Auto, nullptr);
    state.setProperty (idGain, 0.8, nullptr);
    state.setProperty (idAutoPlay, true, nullptr);
    state.setProperty (idListen, true, nullptr);
    state.setProperty (idSyncPreview, true, nullptr);
    state.setProperty (idManualBpm, 120.0, nullptr);
    state.setProperty (idWidth, 1040, nullptr);
    state.setProperty (idHeight, 660, nullptr);

    renderer = std::make_unique<ss::BackgroundRenderer> ([this] (std::shared_ptr<const ss::RenderedSample> r)
    {
        if (r == nullptr || r->audio.getNumSamples() == 0)
            return;
        auto pb = ss::RenderService::makePreview (*r, sampleRate.load(), nextPreviewId++);
        preview.setBuffer (std::move (pb), playWhenRendered.exchange (false));
    });

    keyListener.setEnabled (true);
}

SampleScopeProcessor::~SampleScopeProcessor()
{
    renderer.reset();
    keyListener.setEnabled (false);
}

//==============================================================================
void SampleScopeProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    keyListener.prepare (sr);
    preview.prepare (sr);
}

void SampleScopeProcessor::releaseResources() {}

bool SampleScopeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainInputChannelSet() != out)
        return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void SampleScopeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm(); bpm.hasValue() && *bpm > 0.0)
            {
                hostBpm = *bpm;
                hostHasBpm = true;
            }
            hostPlaying = pos->getIsPlaying();
            if (auto ppq = pos->getPpqPosition())
                hostPpq = *ppq;
        }
    }

    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            keyListener.pushMidiNote (msg.getNoteNumber(), msg.getFloatVelocity());
    }

    // Listen to what flows through (and the side-chain, if routed) for key detection.
    {
        auto mainIn = getBusBuffer (buffer, true, 0);
        keyListener.pushAudio (mainIn.getArrayOfReadPointers(), mainIn.getNumChannels(), n);
        if (getBusCount (true) > 1 && getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled())
        {
            auto sc = getBusBuffer (buffer, true, 1);
            if (sc.getNumChannels() > 0)
                keyListener.pushAudio (sc.getArrayOfReadPointers(), sc.getNumChannels(), n);
        }
    }

    // Audio passes through untouched; the preview is mixed on top.
    for (int ch = getMainBusNumInputChannels(); ch < getMainBusNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, n);

    auto mainOut = getBusBuffer (buffer, false, 0);
    preview.process (mainOut, n, { hostPlaying.load(), hostPpq.load(), hostBpm.load() });
}

//==============================================================================
double SampleScopeProcessor::getEffectiveBpm() const
{
    if (hostHasBpm.load() && hostBpm.load() > 0.0)
        return hostBpm.load();
    return (double) state.getProperty (idManualBpm, 120.0);
}

int SampleScopeProcessor::getProjectKey() const
{
    const int mode = (int) state.getProperty (idKeyMode, 0);
    if (mode >= 1 && mode <= 24)
        return mode - 1;
    if (keyListener.getConfidence() >= 0.12f && keyListener.getSecondsHeard() >= 4.0f)
        return keyListener.getKey();
    return ss::Key::none;
}

ss::SyncSettings SampleScopeProcessor::getSyncSettings() const
{
    ss::SyncSettings s;
    s.syncTempo = (bool) state.getProperty (idSyncTempo, true);
    s.syncKey = (bool) state.getProperty (idSyncKey, true);
    s.hostBpm = getEffectiveBpm();
    s.projectKey = getProjectKey();
    s.transpose = (int) state.getProperty (idTranspose, 0);
    s.mode = (ss::WarpMode) juce::jlimit (0, 4, (int) state.getProperty (idWarpMode, 0));
    return s;
}

void SampleScopeProcessor::requestPreview (const juce::File& file, const ss::SampleRecord& record, bool play)
{
    playWhenRendered = play;
    preview.setGain ((float) (double) state.getProperty (idGain, 0.8));
    preview.setSyncToHost ((bool) state.getProperty (idSyncPreview, true));
    renderer->request (file, record, getSyncSettings());
}

//==============================================================================
juce::AudioProcessorEditor* SampleScopeProcessor::createEditor()
{
    return new SampleScopeEditor (*this);
}

void SampleScopeProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void SampleScopeProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (state.getType()))
        return;

    auto apply = [this, alive = std::weak_ptr<bool> (aliveToken), tree = juce::ValueTree::fromXml (*xml)]
    {
        if (alive.expired())
            return;
        state.copyPropertiesFrom (tree, nullptr);
        keyListener.setEnabled ((bool) state.getProperty (idListen, true));
        preview.setGain ((float) (double) state.getProperty (idGain, 0.8));
        preview.setSyncToHost ((bool) state.getProperty (idSyncPreview, true));
    };

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        apply();
    else
        juce::MessageManager::callAsync (apply);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SampleScopeProcessor();
}
