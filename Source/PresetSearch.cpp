#include "PresetSearch.h"

using namespace juce;
using namespace pedalstyle;

PresetSearch::PresetSearch(BitVibeProcessor& p) : proc(p)
{
    setTitle("FIND A PRESET");
    setWantsKeyboardFocus(false);
    query.setTextToShowWhenEmpty("Type a name or what it uses: flanger, ping, swing, formant...", theme.inkDim);
    query.setFont(bold(13.0f));
    query.setIndents(8, 6);
    query.addListener(this);
    query.onKey = [this](const KeyPress& key) { return handleKey(key); };
    query.setTitle("Search presets");
    addAndMakeVisible(query);

    list.setModel(this);
    list.setRowHeight(26);
    list.setOutlineThickness(1);
    list.setTitle("Matching presets");
    addAndMakeVisible(list);
    applyColours();
}

PresetSearch::~PresetSearch() { list.setModel(nullptr); }

void PresetSearch::applyColours()
{
    query.setColour(TextEditor::backgroundColourId, theme.well);
    query.setColour(TextEditor::textColourId, theme.ink);
    query.setColour(TextEditor::outlineColourId, theme.outline);
    query.setColour(TextEditor::focusedOutlineColourId, theme.accent);
    query.setColour(TextEditor::highlightColourId, theme.selection);
    query.setColour(TextEditor::highlightedTextColourId, theme.ink);
    query.setColour(CaretComponent::caretColourId, theme.accent);
    list.setColour(ListBox::backgroundColourId, theme.well);
    list.setColour(ListBox::outlineColourId, theme.outline);
}

String PresetSearch::searchText(BitVibeProcessor& p, int slot)
{
    String text = String(slot + 1) + " " + p.getProgramName(slot);
    const uint32_t uses = p.presetFeatures(slot);
    for (int f = 0; f < BitVibeProcessor::kNumPresetFeatures; ++f)
        if (((uses >> f) & 1u) != 0)
            text << " " << BitVibeProcessor::presetFeatureName(f) << " " << BitVibeProcessor::presetFeatureGroup(f);
    return text.toLowerCase();
}

bool PresetSearch::matchesWords(const String& haystack, const StringArray& words)
{
    for (const auto& w : words)
        if (w.isNotEmpty() && !haystack.contains(w))
            return false;
    return true;
}

void PresetSearch::open()
{
    applyColours();
    const int count = proc.getNumPrograms();
    haystacks.assign((size_t) count, {});
    usesText.assign((size_t) count, {});
    for (int i = 0; i < count; ++i)
    {
        haystacks[(size_t) i] = searchText(proc, i);
        StringArray uses;
        const uint32_t bits = proc.presetFeatures(i);
        for (int f = 0; f < BitVibeProcessor::kNumPresetFeatures; ++f)
            if (((bits >> f) & 1u) != 0)
                uses.add(BitVibeProcessor::presetFeatureName(f));
        usesText[(size_t) i] = uses.joinIntoString(", ");
    }
    query.clear();
    refilter();
    setVisible(true);
    toFront(false);
    if (isShowing())
        query.grabKeyboardFocus();
}

void PresetSearch::setQuery(const String& text)
{
    query.setText(text, false);
    refilter();
}

void PresetSearch::refilter()
{
    const auto words = StringArray::fromTokens(query.getText().toLowerCase().trim(), " ", "");
    matches.clear();
    for (int i = 0; i < (int) haystacks.size(); ++i)
        if (matchesWords(haystacks[(size_t) i], words))
            matches.push_back(i);
    list.updateContent();
    // the first result is highlighted, but nothing plays until you move to it, click it or press Return
    quiet = true;
    if (matches.empty())
        list.deselectAllRows();
    else
        list.selectRow(0);
    quiet = false;
    repaint();
}

int PresetSearch::selectedRow() const { return list.getSelectedRow(); }

void PresetSearch::select(int row)
{
    if (matches.empty())
        return;
    row = jlimit(0, (int) matches.size() - 1, row);
    if (row == list.getSelectedRow())
        audition(row); // already highlighted (as the first result is): play it anyway
    else
        list.selectRow(row); // selectedRowsChanged plays it
}

void PresetSearch::audition(int row)
{
    if (row >= 0 && row < (int) matches.size() && onAudition)
        onAudition(matches[(size_t) row]);
    list.repaint();
}

bool PresetSearch::handleKey(const KeyPress& key)
{
    const int row = list.getSelectedRow();
    if (key == KeyPress::downKey)
        select(row < 0 ? 0 : row + 1);
    else if (key == KeyPress::upKey)
        select(row <= 0 ? 0 : row - 1);
    else if (key == KeyPress::pageDownKey)
        select(row + 8);
    else if (key == KeyPress::pageUpKey)
        select(row - 8);
    else if (key == KeyPress::returnKey)
        keep(row);
    else if (key == KeyPress::escapeKey)
    {
        if (onClose)
            onClose(false);
    }
    else
        return false;
    return true;
}

bool PresetSearch::keyPressed(const KeyPress& key) { return handleKey(key); }

void PresetSearch::keep(int row)
{
    if (row >= 0 && row < (int) matches.size() && onAudition)
        onAudition(matches[(size_t) row]); // the one highlighted, playing or not
    if (onClose)
        onClose(true);
}

int PresetSearch::getNumRows() { return (int) matches.size(); }

void PresetSearch::paintListBoxItem(int row, Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= (int) matches.size())
        return;
    const int slot = matches[(size_t) row];
    if (selected)
    {
        g.setColour(theme.selection);
        g.fillRect(0, 0, width, height);
    }
    const bool playing = slot == proc.getCurrentProgram();
    if (playing)
    {
        g.setColour(theme.accent);
        g.fillEllipse(6.0f, (float) height * 0.5f - 3.0f, 6.0f, 6.0f);
    }
    g.setColour(theme.inkDim);
    g.setFont(bold(11.0f));
    g.drawText(String(slot + 1).paddedLeft('0', 2), 16, 0, 24, height, Justification::centredLeft);
    g.setColour(playing ? theme.accent : theme.ink);
    g.setFont(bold(12.5f));
    g.drawFittedText(proc.getProgramName(slot), 44, 0, 150, height, Justification::centredLeft, 1, 0.8f);
    g.setColour(theme.inkDim);
    g.setFont(plain(11.0f));
    g.drawFittedText(usesText[(size_t) slot], 200, 0, width - 208, height, Justification::centredRight, 1, 0.75f);
}

void PresetSearch::selectedRowsChanged(int lastRowSelected)
{
    if (!quiet && lastRowSelected >= 0)
        audition(lastRowSelected);
}

void PresetSearch::listBoxItemDoubleClicked(int row, const MouseEvent&) { keep(row); }

void PresetSearch::returnKeyPressed(int row) { keep(row); }

void PresetSearch::textEditorTextChanged(TextEditor&) { refilter(); }

void PresetSearch::paint(Graphics& g)
{
    panelFrame(g, getLocalBounds().toFloat().reduced(4.0f));
    drawSectionTitle(g, 18, 10, "FIND A PRESET");
    g.setColour(theme.inkDim);
    g.setFont(plain(11.0f));
    g.drawText(String((int) matches.size()) + " of " + String((int) haystacks.size()), getWidth() - 118, 10, 100, 18,
               Justification::centredRight);
    g.drawFittedText("Up / Down: hear each one     Return: keep it     Esc: go back", 18, getHeight() - 30, getWidth() - 36, 20,
                     Justification::centred, 1, 0.8f);
}

void PresetSearch::resized()
{
    query.setBounds(16, 34, getWidth() - 32, 28);
    list.setBounds(16, 70, getWidth() - 32, getHeight() - 70 - 36);
}
