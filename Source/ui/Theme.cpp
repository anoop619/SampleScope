#include "Theme.h"

namespace ss::ui
{

juce::Colour categoryColour (Category c)
{
    if (isDrumCategory (c))                              return juce::Colour (0xffffa94d);
    if (c == Category::Bass || c == Category::Sub808)    return juce::Colour (0xffb197fc);
    if (c == Category::Vocal)                            return juce::Colour (0xfff783ac);
    if (c == Category::FX || c == Category::Texture)     return juce::Colour (0xff74c0fc);
    if (c == Category::Unknown)                          return Colours::faint;
    return juce::Colour (0xff63e6be);
}

juce::Font font (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font monoFont (float height)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
}

//==============================================================================
Theme::Theme()
{
    using namespace juce;
    auto scheme = LookAndFeel_V4::ColourScheme (Colours::bg, Colours::panel, Colours::raised, Colours::line,
                                                 Colours::text, Colours::raised, Colours::text, Colours::accent, Colours::text);
    setColourScheme (scheme);

    setColour (ResizableWindow::backgroundColourId, Colours::bg);
    setColour (TextEditor::backgroundColourId, Colours::raised);
    setColour (TextEditor::textColourId, Colours::text);
    setColour (TextEditor::highlightColourId, Colours::accent.withAlpha (0.3f));
    setColour (TextEditor::highlightedTextColourId, Colours::text);
    setColour (CaretComponent::caretColourId, Colours::accent);
    setColour (ListBox::backgroundColourId, Colours::bg);
    setColour (ListBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (TableHeaderComponent::backgroundColourId, Colours::bg);
    setColour (TableHeaderComponent::textColourId, Colours::dim);
    setColour (TableHeaderComponent::outlineColourId, Colours::line);
    setColour (ComboBox::backgroundColourId, Colours::raised);
    setColour (ComboBox::textColourId, Colours::text);
    setColour (ComboBox::outlineColourId, Colours::line);
    setColour (ComboBox::arrowColourId, Colours::dim);
    setColour (PopupMenu::backgroundColourId, Colours::raised);
    setColour (PopupMenu::textColourId, Colours::text);
    setColour (PopupMenu::highlightedBackgroundColourId, Colours::accent.withAlpha (0.2f));
    setColour (PopupMenu::highlightedTextColourId, Colours::text);
    setColour (PopupMenu::headerTextColourId, Colours::dim);
    setColour (TextButton::buttonColourId, Colours::raised);
    setColour (TextButton::textColourOffId, Colours::text);
    setColour (TextButton::textColourOnId, Colours::bg);
    setColour (Slider::textBoxTextColourId, Colours::text);
    setColour (Slider::textBoxBackgroundColourId, Colours::raised);
    setColour (Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (Slider::trackColourId, Colours::accent);
    setColour (Slider::backgroundColourId, Colours::line);
    setColour (Slider::thumbColourId, Colours::text);
    setColour (Label::textColourId, Colours::text);
    setColour (ScrollBar::thumbColourId, Colours::line.brighter (0.3f));
    setColour (AlertWindow::backgroundColourId, Colours::raised);
    setColour (AlertWindow::textColourId, Colours::text);
    setColour (TooltipWindow::backgroundColourId, Colours::raised);
    setColour (TooltipWindow::textColourId, Colours::text);
    setColour (TooltipWindow::outlineColourId, Colours::line);
}

void Theme::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? Colours::accent : Colours::raised;
    if (over) c = c.brighter (0.08f);
    if (down) c = c.darker (0.1f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Colours::line);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void Theme::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.isMouseOver (true) ? Colours::raised.brighter (0.06f) : Colours::raised);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Colours::line);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    juce::Path arrow;
    const float cx = (float) w - 12.0f, cy = (float) h * 0.5f;
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
    arrow.lineTo (cx, cy + 2.0f);
    arrow.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (Colours::dim);
    g.strokePath (arrow, juce::PathStrokeType (1.5f));
}

void Theme::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& ed)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (Colours::raised);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (ed.hasKeyboardFocus (true) ? Colours::accent.withAlpha (0.7f) : Colours::line);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
}

void Theme::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                              juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
        return;
    }
    const float cy = (float) y + (float) h * 0.5f;
    g.setColour (Colours::line);
    g.fillRoundedRectangle ((float) x, cy - 2.0f, (float) w, 4.0f, 2.0f);
    g.setColour (s.isEnabled() ? Colours::accent : Colours::faint);
    g.fillRoundedRectangle ((float) x, cy - 2.0f, pos - (float) x, 4.0f, 2.0f);
    g.setColour (Colours::text);
    g.fillEllipse (pos - 6.0f, cy - 6.0f, 12.0f, 12.0f);
}

void Theme::drawTableHeaderBackground (juce::Graphics& g, juce::TableHeaderComponent& h)
{
    g.fillAll (Colours::bg);
    g.setColour (Colours::line);
    g.fillRect (0, h.getHeight() - 1, h.getWidth(), 1);
}

void Theme::drawTableHeaderColumn (juce::Graphics& g, juce::TableHeaderComponent&, const juce::String& name, int,
                                   int width, int height, bool over, bool, int flags)
{
    auto r = juce::Rectangle<int> (0, 0, width, height).reduced (8, 0);
    g.setColour (over ? Colours::text : Colours::dim);
    g.setFont (font (11.5f, true));
    g.drawText (name.toUpperCase(), r, juce::Justification::centredLeft, true);

    if ((flags & (juce::TableHeaderComponent::sortedForwards | juce::TableHeaderComponent::sortedBackwards)) != 0)
    {
        juce::Path p;
        const float x = (float) r.getRight() - 6.0f, cy = (float) height * 0.5f;
        if ((flags & juce::TableHeaderComponent::sortedForwards) != 0)
            p.addTriangle (x - 4, cy + 2, x + 4, cy + 2, x, cy - 3);
        else
            p.addTriangle (x - 4, cy - 2, x + 4, cy - 2, x, cy + 3);
        g.setColour (Colours::accent);
        g.fillPath (p);
    }
}

void Theme::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                           int thumbStart, int thumbSize, bool over, bool down)
{
    auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, w, thumbSize) : juce::Rectangle<int> (thumbStart, y, thumbSize, h);
    g.setColour (Colours::line.brighter (over || down ? 0.6f : 0.3f));
    g.fillRoundedRectangle (thumb.reduced (2).toFloat(), 3.0f);
}

//==============================================================================
PillToggle::PillToggle (const juce::String& label) : juce::Button (label)
{
    setClickingTogglesState (true);
}

void PillToggle::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();
    g.setColour (on ? Colours::accent.withAlpha (over ? 0.28f : 0.2f) : (over ? Colours::raised.brighter (0.06f) : Colours::raised));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (on ? Colours::accent : Colours::line);
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);

    auto dot = r.removeFromLeft (22.0f).withSizeKeepingCentre (8.0f, 8.0f).translated (3.0f, 0.0f);
    g.setColour (on ? Colours::accent : Colours::faint);
    g.fillEllipse (dot);

    g.setColour (on ? Colours::text : Colours::dim);
    g.setFont (font (12.5f, on));
    g.drawText (getButtonText(), r.withTrimmedRight (6.0f), juce::Justification::centredLeft);
}

IconButton::IconButton (const juce::String& name, Icon i) : juce::Button (name), icon (i) {}

void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (down ? Colours::raised.darker (0.1f) : over ? Colours::raised.brighter (0.08f) : Colours::raised);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (Colours::line);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
    drawIcon (g, icon, r.reduced (r.getHeight() * 0.3f), isEnabled() ? Colours::text : Colours::faint);
}

void IconButton::drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> r, juce::Colour c)
{
    g.setColour (c);
    juce::Path p;
    switch (icon)
    {
        case Icon::Play:
            p.addTriangle (r.getX() + r.getWidth() * 0.15f, r.getY(), r.getX() + r.getWidth() * 0.15f, r.getBottom(),
                           r.getRight(), r.getCentreY());
            g.fillPath (p);
            break;
        case Icon::Stop:
            g.fillRoundedRectangle (r.reduced (r.getWidth() * 0.06f), 2.0f);
            break;
        case Icon::Plus:
            g.fillRect (r.withSizeKeepingCentre (r.getWidth(), 2.0f));
            g.fillRect (r.withSizeKeepingCentre (2.0f, r.getHeight()));
            break;
        case Icon::Search:
        {
            auto circle = r.withSize (r.getWidth() * 0.72f, r.getHeight() * 0.72f);
            g.drawEllipse (circle, 1.6f);
            g.drawLine (circle.getRight() - 1.5f, circle.getBottom() - 1.5f, r.getRight(), r.getBottom(), 1.8f);
            break;
        }
        case Icon::Gear:
        {
            const auto centre = r.getCentre();
            const float outer = r.getWidth() * 0.5f, inner = outer * 0.68f;
            for (int i = 0; i < 8; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
                juce::Path tooth;
                tooth.addRectangle (-1.7f, -outer, 3.4f, outer - inner + 1.5f);
                tooth.applyTransform (juce::AffineTransform::rotation (a).translated (centre));
                g.fillPath (tooth);
            }
            g.drawEllipse (juce::Rectangle<float> (inner * 2.0f, inner * 2.0f).withCentre (centre), 2.2f);
            break;
        }
    }
}

} // namespace ss::ui
