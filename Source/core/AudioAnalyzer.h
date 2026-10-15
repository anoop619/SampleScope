#pragma once

#include "Types.h"
#include "NameHints.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace ss
{

/** Everything the scanner learns about one file. Compact by design: the index keeps only this. */
struct AnalysisResult
{
    bool ok = false;
    juce::String error;

    double   duration = 0.0;          // seconds (whole file)
    uint32_t sampleRate = 0;
    uint8_t  numChannels = 0;

    Category category = Category::Unknown;
    bool  isLoop = false;
    float bpm = 0.0f;                 // 0 when not a loop / unknown
    bool  bpmFromName = false;
    int   key = Key::none;            // tonal centre (loops, chords, pads)
    bool  keyFromName = false;
    float keyConfidence = 0.0f;
    int   rootNote = -1;              // MIDI note of a single-pitch one-shot (808, bass hit, pluck...)

    FeatureVector features {};
    uint32_t tags = 0;
};

/** Measures the sonic profile of a sample and decides type, key and tempo.

    Memory/CPU budget per file: the first 30 s are mixed to mono and resampled to 22.05 kHz,
    then analysed with a handful of FFT passes. Nothing is cached; one analyser per thread.
*/
class AudioAnalyzer
{
public:
    AudioAnalyzer();

    AnalysisResult analyzeFile (const juce::File& file);

    /** Analyse audio already in memory (used by the tests and for rendered material). */
    AnalysisResult analyzeBuffer (const juce::AudioBuffer<float>& buffer, double sampleRate,
                                  const NameHints& hints = {});

    juce::AudioFormatManager& getFormatManager() noexcept { return formats; }

    static constexpr double analysisRate = 22050.0;
    static constexpr double maxAnalysisSeconds = 30.0;

private:
    AnalysisResult analyzeMono (std::vector<float>& mono, double sampleRate, double fullDuration,
                                int numChannels, float stereoWidth, const NameHints& hints);

    juce::AudioFormatManager formats;
};

} // namespace ss
