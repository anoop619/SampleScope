#pragma once

#include "Theme.h"

namespace ss::ui
{

/** In-window text prompt (plug-in windows don't always play nicely with separate dialogs). */
class NamePrompt : public juce::Component
{
public:
    NamePrompt (const juce::String& titleText, const juce::String& initial, const juce::String& okText,
                std::function<void (const juce::String&)> onOk)
        : title (titleText), callback (std::move (onOk))
    {
        editor.setText (initial, false);
        editor.setFont (font (14.0f));
        editor.setIndents (10, 7);
        editor.onReturnKey = [this] { commit(); };
        editor.onEscapeKey = [this] { dismiss(); };
        addAndMakeVisible (editor);

        ok.setButtonText (okText);
        ok.setToggleState (true, juce::dontSendNotification);
        ok.onClick = [this] { commit(); };
        cancel.setButtonText ("Cancel");
        cancel.onClick = [this] { dismiss(); };
        addAndMakeVisible (ok);
        addAndMakeVisible (cancel);
    }

    void show (juce::Component& parent)
    {
        parent.addAndMakeVisible (this);
        setBounds (parent.getLocalBounds());
        editor.grabKeyboardFocus();
        editor.selectAll();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.55f));
        g.setColour (Colours::raised);
        g.fillRoundedRectangle (panel.toFloat(), 12.0f);
        g.setColour (Colours::line);
        g.drawRoundedRectangle (panel.toFloat(), 12.0f, 1.0f);
        g.setColour (Colours::text);
        g.setFont (font (15.0f, true));
        g.drawText (title, panel.reduced (18, 14).removeFromTop (22), juce::Justification::centredLeft);
    }

    void resized() override
    {
        panel = getLocalBounds().withSizeKeepingCentre (360, 150);
        auto r = panel.reduced (18, 14);
        r.removeFromTop (30);
        editor.setBounds (r.removeFromTop (34));
        r.removeFromTop (14);
        auto buttons = r.removeFromTop (30);
        ok.setBounds (buttons.removeFromRight (90));
        buttons.removeFromRight (8);
        cancel.setBounds (buttons.removeFromRight (90));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! panel.contains (e.getPosition())) dismiss();
    }

private:
    void commit()
    {
        auto text = editor.getText().trim();
        auto cb = callback;
        dismiss();
        if (text.isNotEmpty() && cb) cb (text);
    }

    void dismiss()
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (this)]
        {
            if (safe != nullptr) delete safe.getComponent();
        });
        setVisible (false);
    }

    juce::String title;
    std::function<void (const juce::String&)> callback;
    juce::TextEditor editor;
    juce::TextButton ok, cancel;
    juce::Rectangle<int> panel;
};

/** Shown in place of the list while the library is empty. */
class EmptyState : public juce::Component
{
public:
    std::function<void (const juce::File&)> onAddFolder;
    std::function<void()> onChooseFolder;

    void update (bool scanning, int found, int analysed, const juce::Array<juce::File>& suggestions)
    {
        const bool changed = scanning != isScanning || suggestions != shownSuggestions;
        isScanning = scanning; numFound = found; numAnalysed = analysed;
        if (changed)
        {
            shownSuggestions = suggestions;
            buttons.clear();
            if (! scanning)
            {
                const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
                for (int i = 0; i < std::min (3, suggestions.size()); ++i)
                {
                    const auto f = suggestions[i];
                    auto shown = f.isAChildOf (home) ? "~/" + f.getRelativePathFrom (home) : f.getFullPathName();
                    auto* b = buttons.add (new juce::TextButton ("Add " + shown));
                    b->onClick = [this, f] { if (onAddFolder) onAddFolder (f); };
                    addAndMakeVisible (b);
                }
                auto* choose = buttons.add (new juce::TextButton ("Choose a folder..."));
                choose->setToggleState (true, juce::dontSendNotification);
                choose->onClick = [this] { if (onChooseFolder) onChooseFolder(); };
                addAndMakeVisible (choose);
            }
            resized();
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Colours::bg);
        auto r = getLocalBounds().withSizeKeepingCentre (std::min (520, getWidth() - 40), 260);
        g.setColour (Colours::text);
        g.setFont (font (20.0f, true));
        g.drawText (isScanning ? "Indexing your samples" : "Point SampleScope at your samples", r.removeFromTop (34),
                    juce::Justification::centred);
        g.setColour (Colours::dim);
        g.setFont (font (13.5f));
        const auto body = isScanning
            ? juce::String (numFound) + " files found, " + juce::String (numAnalysed) + " analysed. Samples appear as soon as they're ready."
            : juce::String ("SampleScope reads your folders in the background and works out the type, key, tempo and "
                            "character of every sample. Files are never moved or copied.");
        g.drawFittedText (body, r.removeFromTop (48), juce::Justification::centred, 3);
    }

    void resized() override
    {
        auto r = getLocalBounds().withSizeKeepingCentre (std::min (360, getWidth() - 40), 260).withTrimmedTop (100);
        for (auto* b : buttons)
        {
            b->setBounds (r.removeFromTop (32));
            r.removeFromTop (8);
        }
    }

private:
    bool isScanning = false;
    int numFound = 0, numAnalysed = 0;
    juce::Array<juce::File> shownSuggestions { juce::File ("/__none__") };
    juce::OwnedArray<juce::TextButton> buttons;
};

} // namespace ss::ui
