#pragma once

#include "SampleDatabase.h"
#include "Warper.h"

namespace ss
{

/** What the user wants synced. */
struct SyncSettings
{
    bool   syncTempo = true;
    bool   syncKey = true;
    double hostBpm = 0.0;           // 0 = unknown
    int    projectKey = Key::none;
    int    transpose = 0;           // manual semitones on top
    WarpMode mode = WarpMode::Auto;
};

/** What will happen to one sample under the current settings. Cheap to compute; the UI
    shows it before anything is rendered. */
struct RenderPlan
{
    bool     altered = false;
    double   sourceBpm = 0.0, targetBpm = 0.0;
    double   timeRatio = 1.0;
    double   loopBeats = 0.0;       // whole beats in the loop (after half/double-time folding)
    int      semitones = 0;
    WarpMode mode = WarpMode::Pro;
    juce::String note;              // e.g. "Re-Pitch: pitch follows tempo"

    juce::String describe() const;  // "124 → 128 BPM · +3 st · Pro"
    juce::String fileTag() const;   // "128bpm +3st"
};

RenderPlan planFor (const SampleRecord&, const SyncSettings&);

/** A sample, loaded and warped, at its own sample rate. */
struct RenderedSample
{
    juce::String sourcePath;
    juce::String displayName;
    int64_t      sourceModTime = 0;
    juce::AudioBuffer<float> audio;
    double       sampleRate = 44100.0;
    RenderPlan   plan;
    bool         isLoop = false;
    juce::String error;
};

/** Audio for the preview voice, already at the host's sample rate. */
struct PreviewBuffer
{
    juce::AudioBuffer<float> audio;
    bool   isLoop = false;
    double loopBeats = 0.0;
    uint32_t id = 0;
};

enum class RenderFormat { Flac24 = 0, Wav24 };

class RenderService
{
public:
    static constexpr double maxWarpSeconds = 300.0;   // longer files are dragged as-is

    /** Loads + warps. Thread-safe (pass a format manager owned by the calling thread). */
    static std::shared_ptr<RenderedSample> render (const juce::File& source, const SampleRecord& record,
                                                   const SyncSettings& settings, juce::AudioFormatManager& formats);

    static std::shared_ptr<PreviewBuffer> makePreview (const RenderedSample&, double hostSampleRate, uint32_t id);

    /** The file to hand to the DAW: the original when nothing changed, otherwise a rendered
        copy in `folder` (reused if the same render already exists). */
    static juce::File fileForDrag (const RenderedSample&, const juce::File& folder, RenderFormat);

    /** Total size of rendered files, and removal of ones older than `days`. */
    static int64_t folderSize (const juce::File& folder);
    static int cleanOlderThan (const juce::File& folder, int days);
};

/** Renders the most recently requested sample on a background thread. Requests made while
    busy replace each other, so scrolling quickly through the list never queues up work. */
class BackgroundRenderer : private juce::Thread
{
public:
    using Callback = std::function<void (std::shared_ptr<const RenderedSample>)>;

    explicit BackgroundRenderer (Callback onDone);
    ~BackgroundRenderer() override;

    void request (const juce::File& source, const SampleRecord& record, const SyncSettings& settings);
    std::shared_ptr<const RenderedSample> getLatest() const;
    bool isBusy() const noexcept { return busy.load(); }

    /** Renders synchronously on the calling thread (used if a drag starts before the
        background render finished). */
    std::shared_ptr<const RenderedSample> renderNow (const juce::File& source, const SampleRecord& record, const SyncSettings& settings);

private:
    void run() override;

    struct Request { juce::File source; SampleRecord record; SyncSettings settings; };

    Callback callback;
    juce::CriticalSection lock;
    std::optional<Request> pending;
    std::shared_ptr<const RenderedSample> latest;
    std::atomic<bool> busy { false };
    juce::WaitableEvent wake;
};

} // namespace ss
