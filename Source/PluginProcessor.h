#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/KeyListener.h"
#include "core/Library.h"
#include "core/PreviewPlayer.h"

/** SampleScope: sample browser that sits on a track (ideally the master), listens to the
    project for key detection, follows the host tempo, and previews/exports warped samples. */
class SampleScopeProcessor : public juce::AudioProcessor
{
public:
    SampleScopeProcessor();
    ~SampleScopeProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                    { return true; }

    const juce::String getName() const override        { return "SampleScope"; }
    bool acceptsMidi() const override                  { return true; }
    bool producesMidi() const override                 { return false; }
    bool isMidiEffect() const override                 { return false; }
    double getTailLengthSeconds() const override       { return 0.0; }

    int getNumPrograms() override                      { return 1; }
    int getCurrentProgram() override                   { return 0; }
    void setCurrentProgram (int) override              {}
    const juce::String getProgramName (int) override   { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    ss::Library&            getLibrary() noexcept      { return *library; }
    ss::PreviewPlayer&      getPreview() noexcept      { return preview; }
    ss::KeyListener&        getKeyListener() noexcept  { return keyListener; }
    ss::BackgroundRenderer& getRenderer() noexcept     { return *renderer; }

    /** Per-instance session settings (saved with the DAW project). Message thread. */
    juce::ValueTree& getState() noexcept               { return state; }

    double getHostBpm() const noexcept                 { return hostBpm.load(); }
    bool   hostHasTempo() const noexcept               { return hostHasBpm.load(); }
    bool   isHostPlaying() const noexcept              { return hostPlaying.load(); }

    /** Tempo used for syncing: the host's, or the manual one when the host gives none. */
    double getEffectiveBpm() const;
    /** Key used for syncing: manual choice or the detected one (Key::none if unsure). */
    int getProjectKey() const;
    ss::SyncSettings getSyncSettings() const;

    /** Render (in the background) and optionally play the given sample. */
    void requestPreview (const juce::File& file, const ss::SampleRecord& record, bool play);

    // State property names
    static inline const juce::Identifier idSyncTempo { "syncTempo" }, idSyncKey { "syncKey" }, idKeyMode { "keyMode" },
        idTranspose { "transpose" }, idWarpMode { "warpMode" }, idGain { "previewGain" }, idAutoPlay { "autoPlay" },
        idQuery { "query" }, idSidebar { "sidebar" }, idWidth { "width" }, idHeight { "height" }, idManualBpm { "manualBpm" },
        idListen { "listen" }, idSyncPreview { "syncPreview" };

private:
    juce::SharedResourcePointer<ss::Library> library;
    ss::PreviewPlayer preview;
    ss::KeyListener keyListener;
    std::unique_ptr<ss::BackgroundRenderer> renderer;
    juce::ValueTree state { "SampleScopeState" };

    std::atomic<double> hostBpm { 0.0 }, hostPpq { 0.0 }, sampleRate { 44100.0 };
    std::atomic<bool> hostPlaying { false }, hostHasBpm { false }, playWhenRendered { false };
    std::atomic<uint32_t> nextPreviewId { 1 };
    std::shared_ptr<bool> aliveToken = std::make_shared<bool> (true);   // guards async callbacks

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleScopeProcessor)
};
