#pragma once

#include "PluginEditor.h"

// FIND A PRESET: type part of a name, or what a preset uses ("flanger", "ping", "swing", "formant", "lanes"), or its
// slot number, or "favourite" (or "favorite", "fav", "starred") for the starred ones; every word typed must match. The results list the presets in slot order with what each one uses.
// Up and Down move through them and play each one as it is reached, so you hear them as you browse; a click plays
// one too. Return (or a double-click) keeps the one highlighted and closes; Escape closes and goes back to the preset
// that was showing when the search opened.
class PresetSearch : public juce::Component, private juce::ListBoxModel, private juce::TextEditor::Listener
{
public:
    explicit PresetSearch(BitVibeProcessor&);
    ~PresetSearch() override;

    std::function<void(int slot)> onAudition; // a result was reached: play it
    std::function<void(bool keep)> onClose;   // keep: Return or a double-click (stay on it); not: Escape (go back)

    void open();                        // an empty query (every preset), the keyboard in the text box
    void setQuery(const juce::String&); // what the text box says (also for the tests)
    const std::vector<int>& results() const { return matches; }
    int selectedRow() const;
    void select(int row);                  // moves the highlight there and plays that preset
    bool handleKey(const juce::KeyPress&); // Up, Down, Page Up / Down, Return, Escape
    bool keyPressed(const juce::KeyPress&) override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // What a slot can be found by: its number, its name and the names of what it uses, in lower case.
    static juce::String searchText(BitVibeProcessor&, int slot);
    static bool matchesWords(const juce::String& haystack, const juce::StringArray& words);

private:
    struct QueryBox : juce::TextEditor
    {
        std::function<bool(const juce::KeyPress&)> onKey;
        bool keyPressed(const juce::KeyPress& key) override { return (onKey && onKey(key)) || juce::TextEditor::keyPressed(key); }
    };

    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged(int lastRowSelected) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    void returnKeyPressed(int row) override;
    void textEditorTextChanged(juce::TextEditor&) override;
    void refilter();
    void applyColours();
    void audition(int row); // plays the result in that row
    void keep(int row);     // plays it if it is not already, and closes, staying on it

    BitVibeProcessor& proc;
    QueryBox query;
    juce::ListBox list;
    std::vector<int> matches;
    std::vector<juce::String> haystacks, usesText; // per slot: what it is found by, and what it uses (for its row)
    std::vector<bool> starred;                      // per slot: a favourite (a star in its row)
    bool quiet = false;                             // highlighting the first result after a change of query: no playing
};
