#include "RenderService.h"
#include "Resampler.h"

namespace ss
{

namespace
{
    juce::String bpmText (double bpm)
    {
        const bool whole = std::abs (bpm - std::round (bpm)) < 0.005;
        return juce::String (bpm, whole ? 0 : 2);
    }
}

juce::String RenderPlan::describe() const
{
    if (! altered)
        return note.isNotEmpty() ? "Original" + sep() + note : juce::String ("Original");

    juce::StringArray parts;
    if (std::abs (timeRatio - 1.0) > 1.0e-4)
        parts.add (bpmText (sourceBpm) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + bpmText (targetBpm) + " BPM");
    if (semitones != 0 && ! (mode == WarpMode::RePitch && std::abs (timeRatio - 1.0) > 1.0e-4))
        parts.add ((semitones > 0 ? "+" : "") + juce::String (semitones) + " st");
    parts.add (warpModeName (mode));
    if (note.isNotEmpty()) parts.add (note);
    return parts.joinIntoString (juce::String::fromUTF8 (" \xc2\xb7 "));
}

juce::String RenderPlan::fileTag() const
{
    juce::StringArray parts;
    if (std::abs (timeRatio - 1.0) > 1.0e-4)
        parts.add (bpmText (targetBpm) + "bpm");
    if (semitones != 0 && ! (mode == WarpMode::RePitch && std::abs (timeRatio - 1.0) > 1.0e-4))
        parts.add ((semitones > 0 ? "+" : "") + juce::String (semitones) + "st");
    if (mode == WarpMode::RePitch && std::abs (timeRatio - 1.0) > 1.0e-4)
        parts.add ("repitch");
    return parts.joinIntoString (" ");
}

RenderPlan planFor (const SampleRecord& rec, const SyncSettings& s)
{
    RenderPlan p;
    const auto cat = rec.getCategory();
    const bool loop = rec.loop();

    // --- tempo -----------------------------------------------------------------------
    bool stretch = false;
    if (loop && s.syncTempo && s.hostBpm > 0.0 && rec.bpm > 0.0f)
    {
        double src = rec.bpm;
        double beats = rec.duration * src / 60.0;

        // A 70 BPM loop in a 140 project is a half-time loop, not something to squash 2x.
        double m = 1.0;
        if (std::abs (std::log2 (s.hostBpm / src)) > 0.4)
            for (double mm : { 0.5, 2.0 })
                if (std::abs (std::log2 (s.hostBpm / (src * mm))) < std::abs (std::log2 (s.hostBpm / (src * m))))
                    m = mm;
        src *= m; beats *= m;

        const double rb = std::round (beats);
        if (rb >= 1.0 && std::abs (beats - rb) < 0.02 * rb) beats = rb;

        p.sourceBpm = src;
        p.targetBpm = s.hostBpm;
        p.loopBeats = beats;
        p.timeRatio = src / s.hostBpm;
        stretch = std::abs (p.timeRatio - 1.0) > 5.0e-4;
        if (! stretch) p.timeRatio = 1.0;
    }

    // --- key ---------------------------------------------------------------------------
    int semis = 0;
    if (s.syncKey && Key::isValid (s.projectKey))
    {
        const bool pitched = isPitchedCategory (cat) || (rec.flags & SampleRecord::keyFromName) != 0;
        if (! loop && rec.rootNote >= 0 && pitched)
            semis = Key::semitonesRootToKey (rec.rootNote, s.projectKey);
        else if (Key::isValid (rec.key) && pitched)
            semis = Key::semitonesBetween (rec.key, s.projectKey);
    }
    semis += s.transpose;
    p.semitones = semis;

    // --- algorithm ----------------------------------------------------------------------
    WarpMode m = s.mode;
    if (m == WarpMode::Auto)
    {
        if (loop)
            m = isDrumCategory (cat) ? WarpMode::Beats : (cat == Category::Vocal ? WarpMode::ProFormant : WarpMode::Pro);
        else
        {
            const bool sustained = rec.duration > 1.5f
                && (cat == Category::Pad || cat == Category::Texture || cat == Category::FX || cat == Category::Vocal
                    || cat == Category::Lead || cat == Category::Strings || cat == Category::Brass || cat == Category::Melodic);
            m = sustained ? (cat == Category::Vocal ? WarpMode::ProFormant : WarpMode::Pro) : WarpMode::RePitch;
        }
    }
    if (m == WarpMode::Beats && semis != 0)
        m = WarpMode::Pro;
    p.mode = m;

    if (m == WarpMode::RePitch && stretch)
    {
        const double st = 12.0 * std::log2 (1.0 / p.timeRatio);
        p.note = "pitch follows tempo (" + juce::String (st >= 0 ? "+" : "") + juce::String (st, 1) + " st)";
    }

    p.altered = stretch || semis != 0;
    return p;
}

std::shared_ptr<RenderedSample> RenderService::render (const juce::File& source, const SampleRecord& record,
                                                       const SyncSettings& settings, juce::AudioFormatManager& formats)
{
    auto out = std::make_shared<RenderedSample>();
    out->sourcePath = source.getFullPathName();
    out->displayName = source.getFileNameWithoutExtension();
    out->sourceModTime = source.getLastModificationTime().toMilliseconds();
    out->isLoop = record.loop();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));
    if (reader == nullptr)
    {
        out->error = "Can't read " + source.getFileName();
        return out;
    }

    const double sr = reader->sampleRate;
    const auto len = reader->lengthInSamples;
    const int ch = (int) juce::jlimit (1u, 2u, reader->numChannels);
    out->sampleRate = sr;

    auto plan = planFor (record, settings);
    if (plan.altered && len / sr > maxWarpSeconds)
    {
        plan = RenderPlan {};
        plan.note = "too long to warp";
    }
    out->plan = plan;

    // Unaltered material is dragged from its original file; only the preview needs audio.
    const auto toRead = (int) std::min<juce::int64> (len, (juce::int64) ((plan.altered ? maxWarpSeconds : 60.0) * sr));
    juce::AudioBuffer<float> in (ch, toRead);
    reader->read (&in, 0, toRead, 0, true, ch > 1);
    reader.reset();

    if (! plan.altered)
    {
        out->audio = std::move (in);
        return out;
    }

    WarpParams wp;
    wp.mode = plan.mode;
    const bool stretch = std::abs (plan.timeRatio - 1.0) > 1.0e-6;
    wp.semitones = (plan.mode == WarpMode::RePitch && stretch) ? 0.0 : plan.semitones;
    if (stretch)
    {
        wp.timeRatio = plan.timeRatio;
        if (plan.loopBeats >= 1.0 && std::abs (plan.loopBeats - std::round (plan.loopBeats)) < 1.0e-9)
            wp.exactOutputLength = (int) std::lround (plan.loopBeats * 60.0 / plan.targetBpm * sr);
    }

    out->audio = Warper::process (in, sr, wp);

    const float mag = out->audio.getMagnitude (0, out->audio.getNumSamples());
    if (mag > 0.999f)
        out->audio.applyGain (0.999f / mag);
    return out;
}

std::shared_ptr<PreviewBuffer> RenderService::makePreview (const RenderedSample& r, double hostSr, uint32_t id)
{
    auto p = std::make_shared<PreviewBuffer>();
    p->id = id;
    p->isLoop = r.isLoop;
    p->loopBeats = r.plan.loopBeats;

    const int ch = r.audio.getNumChannels();
    const int maxIn = std::min (r.audio.getNumSamples(), (int) (60.0 * r.sampleRate));
    if (ch == 0 || maxIn == 0)
        return p;

    if (hostSr <= 0.0 || std::abs (hostSr - r.sampleRate) < 0.5)
    {
        p->audio.setSize (ch, maxIn);
        for (int c = 0; c < ch; ++c) p->audio.copyFrom (c, 0, r.audio, c, 0, maxIn);
        return p;
    }

    const double inPerOut = r.sampleRate / hostSr;
    const int outLen = (int) std::floor (maxIn / inPerOut);
    p->audio.setSize (ch, outLen);
    for (int c = 0; c < ch; ++c)
        Resampler::process (r.audio.getReadPointer (c), maxIn, p->audio.getWritePointer (c), outLen, inPerOut, 16);
    return p;
}

juce::File RenderService::fileForDrag (const RenderedSample& r, const juce::File& folder, RenderFormat format)
{
    if (! r.plan.altered || r.audio.getNumSamples() == 0)
        return juce::File (r.sourcePath);

    // One sub-folder per source keeps names readable ("Loop 03 [128bpm +3st].flac")
    // without collisions between identically named files from different packs.
    const auto key = juce::String::toHexString ((juce::int64) hashPath (r.sourcePath + ":" + juce::String (r.sourceModTime)));
    const auto dir = folder.getChildFile (key.substring (0, 10));
    const auto ext = format == RenderFormat::Flac24 ? ".flac" : ".wav";
    const auto name = juce::File::createLegalFileName (r.displayName + " [" + r.plan.fileTag() + "]" + ext);
    const auto target = dir.getChildFile (name);

    if (target.existsAsFile() && target.getSize() > 0)
        return target;

    dir.createDirectory();
    juce::TemporaryFile temp (target);
    {
        std::unique_ptr<juce::AudioFormat> fmt;
        if (format == RenderFormat::Flac24) fmt = std::make_unique<juce::FlacAudioFormat>();
        else                                fmt = std::make_unique<juce::WavAudioFormat>();

        std::unique_ptr<juce::OutputStream> os (temp.getFile().createOutputStream().release());
        if (os == nullptr)
            return {};

        auto writer = fmt->createWriterFor (os, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (r.sampleRate)
                                                   .withNumChannels (r.audio.getNumChannels())
                                                   .withBitsPerSample (24));
        if (writer == nullptr)
            return {};
        writer->writeFromAudioSampleBuffer (r.audio, 0, r.audio.getNumSamples());
    }
    return temp.overwriteTargetFileWithTemporary() ? target : juce::File();
}

int64_t RenderService::folderSize (const juce::File& folder)
{
    int64_t total = 0;
    if (folder.isDirectory())
        for (const auto& e : juce::RangedDirectoryIterator (folder, true, "*", juce::File::findFiles))
            total += e.getFileSize();
    return total;
}

int RenderService::cleanOlderThan (const juce::File& folder, int days)
{
    if (! folder.isDirectory())
        return 0;
    const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::days (days);
    int removed = 0;
    juce::Array<juce::File> victims;
    for (const auto& e : juce::RangedDirectoryIterator (folder, true, "*", juce::File::findFiles))
        if (e.getModificationTime() < cutoff)
            victims.add (e.getFile());
    for (auto& f : victims)
        if (f.deleteFile()) ++removed;
    for (const auto& e : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findDirectories))
        if (e.getFile().getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
            e.getFile().deleteFile();
    return removed;
}

//==============================================================================
BackgroundRenderer::BackgroundRenderer (Callback onDone)
    : juce::Thread ("SS render"), callback (std::move (onDone))
{
    startThread (juce::Thread::Priority::normal);
}

BackgroundRenderer::~BackgroundRenderer()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (5000);
}

void BackgroundRenderer::request (const juce::File& source, const SampleRecord& record, const SyncSettings& settings)
{
    {
        const juce::ScopedLock sl (lock);
        pending = Request { source, record, settings };
    }
    busy = true;
    wake.signal();
}

std::shared_ptr<const RenderedSample> BackgroundRenderer::getLatest() const
{
    const juce::ScopedLock sl (lock);
    return latest;
}

std::shared_ptr<const RenderedSample> BackgroundRenderer::renderNow (const juce::File& source, const SampleRecord& record,
                                                                     const SyncSettings& settings)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::shared_ptr<const RenderedSample> r = RenderService::render (source, record, settings, formats);
    const juce::ScopedLock sl (lock);
    latest = r;
    return r;
}

void BackgroundRenderer::run()
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    while (! threadShouldExit())
    {
        wake.wait (-1);
        if (threadShouldExit())
            break;

        for (;;)
        {
            std::optional<Request> req;
            {
                const juce::ScopedLock sl (lock);
                req.swap (pending);
            }
            if (! req.has_value())
                break;

            busy = true;
            std::shared_ptr<const RenderedSample> r = RenderService::render (req->source, req->record, req->settings, formats);

            bool superseded;
            {
                const juce::ScopedLock sl (lock);
                superseded = pending.has_value();
                if (! superseded) latest = r;
            }
            if (! superseded && callback)
                callback (r);
        }
        busy = false;
    }
}

} // namespace ss
