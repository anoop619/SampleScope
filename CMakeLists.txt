cmake_minimum_required(VERSION 3.24)

# Universal binary (Apple Silicon + Intel) unless the caller overrides it.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(SampleScope VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(SAMPLESCOPE_STANDALONE "Also build a standalone app (handy for development)" OFF)
option(SAMPLESCOPE_TESTS      "Build the analysis/search/warp test harness"        OFF)

# ---------------------------------------------------------------------------
# Dependencies. Pinned versions; FetchContent downloads them on first configure.
# To use local checkouts instead pass e.g. -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.3
    GIT_SHALLOW    ON)

FetchContent_Declare(signalsmith-linear
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/linear.git
    GIT_TAG        0.6.4
    GIT_SHALLOW    ON)

FetchContent_Declare(signalsmith-stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG        1.4.0
    GIT_SHALLOW    ON)

FetchContent_MakeAvailable(JUCE signalsmith-linear signalsmith-stretch)

# Treat the stretcher's headers as system headers so our strict warning flags don't apply to them.
foreach(dep signalsmith-linear signalsmith-stretch)
    get_target_property(_inc ${dep} INTERFACE_INCLUDE_DIRECTORIES)
    set_target_properties(${dep} PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_inc}")
endforeach()

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(CORE_SOURCES
    Source/core/Types.cpp
    Source/core/Resampler.cpp
    Source/core/DSPUtils.cpp
    Source/core/NameHints.cpp
    Source/core/AudioAnalyzer.cpp
    Source/core/SampleDatabase.cpp
    Source/core/Scanner.cpp
    Source/core/SearchEngine.cpp
    Source/core/Collections.cpp
    Source/core/Warper.cpp
    Source/core/RenderService.cpp
    Source/core/KeyListener.cpp
    Source/core/Library.cpp)

set(PLUGIN_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/core/PreviewPlayer.cpp
    Source/ui/Theme.cpp
    Source/ui/SampleTable.cpp
    Source/ui/Sidebar.cpp
    Source/ui/WaveformView.cpp
    Source/ui/SyncBar.cpp)

set(CORE_MODULES
    juce::juce_core
    juce::juce_events
    juce::juce_data_structures
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_dsp)

set(COMMON_DEFINES
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_SILENCE_XCODE_15_LINKER_WARNING=1)

# ---------------------------------------------------------------------------
# The plug-in (VST3 only; Standalone optional for development)
# ---------------------------------------------------------------------------
set(SAMPLESCOPE_FORMATS VST3)
if(SAMPLESCOPE_STANDALONE)
    list(APPEND SAMPLESCOPE_FORMATS Standalone)
endif()

juce_add_plugin(SampleScope
    COMPANY_NAME               "Anoop"
    BUNDLE_ID                  "com.anoop.samplescope"
    PLUGIN_MANUFACTURER_CODE   Anop
    PLUGIN_CODE                SmSc
    PRODUCT_NAME               "SampleScope"
    FORMATS                    ${SAMPLESCOPE_FORMATS}
    IS_SYNTH                   FALSE
    NEEDS_MIDI_INPUT           TRUE
    NEEDS_MIDI_OUTPUT          FALSE
    IS_MIDI_EFFECT             FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES            Fx Analyzer Tools
    COPY_PLUGIN_AFTER_BUILD    FALSE)

target_sources(SampleScope PRIVATE ${CORE_SOURCES} ${PLUGIN_SOURCES})
target_include_directories(SampleScope PRIVATE Source)
target_compile_definitions(SampleScope PUBLIC ${COMMON_DEFINES})
target_link_libraries(SampleScope
    PRIVATE
        ${CORE_MODULES}
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_audio_processors
        juce::juce_audio_utils
        signalsmith-stretch
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# The stretcher is number-crunching heavy: keep it optimised even in Debug builds.
set_source_files_properties(Source/core/Warper.cpp PROPERTIES COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/O2>$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O3>")

# ---------------------------------------------------------------------------
# Test harness (synthesises a labelled corpus and checks analysis/search/warp)
# ---------------------------------------------------------------------------
if(SAMPLESCOPE_TESTS)
    juce_add_console_app(SampleScopeTests PRODUCT_NAME "SampleScopeTests")
    target_sources(SampleScopeTests PRIVATE Tests/AnalysisTests.cpp ${CORE_SOURCES} Source/core/PreviewPlayer.cpp)
    target_include_directories(SampleScopeTests PRIVATE Source Tests)
    target_compile_definitions(SampleScopeTests PRIVATE ${COMMON_DEFINES} JUCE_STANDALONE_APPLICATION=1 SAMPLESCOPE_FULL_TESTS=1)
    target_link_libraries(SampleScopeTests PRIVATE ${CORE_MODULES} signalsmith-stretch
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
