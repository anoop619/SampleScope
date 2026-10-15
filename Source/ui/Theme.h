#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "core/Types.h"

namespace ss::ui
{

namespace Colours
{
    const juce::Colour bg        { 0xff111317 };
    const juce::Colour panel     { 0xff161920 };
    const juce::Colour raised    { 0xff1c2029 };
    const juce::Colour rowAlt    { 0xff14171d };
    const juce::Colour line      { 0xff262b35 };
    const juce::Colour text      { 0xffe8eaef };
    const juce::Colour dim       { 0xff8b93a3 };
    const juce::Colour faint     { 0xff5c6370 };
    const juce::Colour accent    { 0xff4fd1a5 };
    const juce::Colour star      { 0xffffb454 };
    const juce::Colour warn      { 0xffffb454 };
}

juce::Colour categoryColour (Category);
juce::Font   font (float height, bool bold = false);
juce::Font   monoFont (float height);

/** One LookAndFeel for the whole plug-in: flat, dark, quiet. */
class Theme : public juce::LookAndFeel_V4
{
public:
    Theme();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (13.0f); }
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (13.0f); }
    juce::Font getPopupMenuFont() override { return font (14.0f); }
    void drawTextEditorOutline (juce::Graphics&, int, int, juce::TextEditor&) override {}
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawTableHeaderColumn (juce::Graphics&, juce::TableHeaderComponent&, const juce::String& name, int columnId,
                                int width, int height, bool over, bool down, int flags) override;
    void drawTableHeaderBackground (juce::Graphics&, juce::TableHeaderComponent&) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart,
                        int thumbSize, bool over, bool down) override;
    int getDefaultScrollbarWidth() override { return 10; }
};

/** Small toggle "pill" button used for Sync BPM / Sync Key / Auto-play. */
class PillToggle : public juce::Button
{
public:
    explicit PillToggle (const juce::String& label);
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

/** Icon button (play/stop, gear, plus). */
class IconButton : public juce::Button
{
public:
    enum class Icon { Play, Stop, Gear, Plus, Search };
    IconButton (const juce::String& name, Icon);
    void setIcon (Icon i) { icon = i; repaint(); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
    static void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float>, juce::Colour);

private:
    Icon icon;
};

} // namespace ss::ui
