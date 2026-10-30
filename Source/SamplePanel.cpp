#include "SamplePanel.h"

using namespace juce;
using namespace pedalstyle;

SamplePanel::SamplePanel(BitVibeProcessor& p, PedalLookAndFeel& lookAndFeel) : proc(p), bank(p.getSampleBank()), laf(lookAndFeel)
{
    setSize(kWidth, kHeight);

    for (auto* s : { &mixSlider, &chanceSlider, &baseSlider, &maxSlider })
    {
        s->setSliderStyle(Slider::RotaryVerticalDrag);
        s->setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
        s->setRotaryParameters(MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
        s->setMouseDragSensitivity(180);
        s->setLookAndFeel(&laf);
        // Remember where the player leaves the knobs (a change made by the host or a project load is not the player's).
        s->onValueChange = [this, s] { if (s->isMouseOver(true)) rememberKnobs(); };
        addAndMakeVisible(*s);
    }
    mixAtt = std::make_unique<APVTS::SliderAttachment>(proc.apvts, ids::sampleMix, mixSlider);
    chanceAtt = std::make_unique<APVTS::SliderAttachment>(proc.apvts, ids::sampleChance, chanceSlider);
    baseAtt = std::make_unique<APVTS::SliderAttachment>(proc.apvts, ids::sampleBase, baseSlider);
    maxAtt = std::make_unique<APVTS::SliderAttachment>(proc.apvts, ids::sampleMax, maxSlider);

    // Letting go of the base or max knob plays one of the picked sounds at that volume (the same sound until the
    // next time a knob is picked up), so the player hears what they set.
    for (auto* s : { &baseSlider, &maxSlider })
    {
        s->onDragStart = [this] { previewPick = (std::uint32_t) Random::getSystemRandom().nextInt(); };
        s->onDragEnd = [this, s] { previewAt((float) s->getValue()); };
    }
    previewButton.onClick = [this] { previewBoth(); };
    addAndMakeVisible(previewButton);

    bankBox.setLookAndFeel(&laf);
    bankBox.onChange = [this]
    {
        if (updatingControls)
            return;
        const auto name = bankBox.getText();
        if (name.isNotEmpty())
            bank.loadBank(name);
        refresh();
    };
    addAndMakeVisible(bankBox);

    saveButton.onClick = [this] { askBankName(); };
    deleteButton.onClick = [this] { askDeleteBank(); };
    chooseButton.onClick = [this] { showChooser(); };
    folderButton.onClick = [this] { openFolder(); };
    for (auto* b : { &saveButton, &deleteButton, &chooseButton, &folderButton })
        addAndMakeVisible(*b);

    bankBox.setTooltip("SOUND BANK: a saved list of sounds. Pick one to switch to its list straight away. "
                       "Make your own with CHOOSE SOUNDS and SAVE.");
    saveButton.setTooltip("SAVE: store the sounds you have picked as a sound bank. Type a name; saving under an existing name replaces that bank.");
    deleteButton.setTooltip("DELETE: remove the sound bank that is showing. Your sound files are not touched.");
    chooseButton.setTooltip("CHOOSE SOUNDS: pick the list of sounds to choose from. At every step of the sequence one of "
                            "them is picked at random and blended in. Your picks are remembered next time.");
    folderButton.setTooltip("OPEN SOUNDS FOLDER: opens the folder for your own sounds (WAV, AIFF, FLAC or OGG files). "
                            "Put files in it, then pick them with CHOOSE SOUNDS.");
    mixSlider.setTooltip("SAMPLE MIX: how loud the random sounds are, between the BASE volume (fully left) and the MAX volume (fully right). "
                         "With the base left at Off, fully left means no sounds. Remembered next time.");
    baseSlider.setTooltip("BASE VOLUME: how loud the sounds are with MIX fully left. Fully left = Off. Let go of the knob to hear a sound at this volume.");
    maxSlider.setTooltip("MAX VOLUME: how loud the sounds are with MIX fully right. Let go of the knob to hear a sound at this volume.");
    previewButton.setTooltip("PREVIEW: plays one of the sounds you picked at the BASE volume, then the same sound at the MAX volume, so you can set both by ear.");
    chanceSlider.setTooltip("SAMPLE CHANCE: how often a step gets a sound. 100% = every step, lower = now and then. Remembered next time.");

    numAvailable = SampleBank::libraryFiles().size();
    refresh();
}

SamplePanel::~SamplePanel()
{
    mixSlider.setLookAndFeel(nullptr);
    chanceSlider.setLookAndFeel(nullptr);
    bankBox.setLookAndFeel(nullptr);
}

float SamplePanel::rawValue(const char* id) const
{
    return proc.apvts.getRawParameterValue(id)->load();
}

void SamplePanel::rememberKnobs()
{
    SampleBank::Levels levels;
    levels.mix = rawValue(ids::sampleMix);
    levels.chance = rawValue(ids::sampleChance);
    levels.baseDb = rawValue(ids::sampleBase);
    levels.maxDb = rawValue(ids::sampleMax);
    bank.rememberLevels(levels);
}

void SamplePanel::resized()
{
    bankBox.setBounds(12, 24, 108, 24);
    saveButton.setBounds(124, 24, 60, 24);
    deleteButton.setBounds(188, 24, 62, 24);
    chooseButton.setBounds(12, 52, 238, 24);
    folderButton.setBounds(12, 80, 238, 24);
    mixSlider.setBounds(Rectangle<int>(44, 44).withCentre({ 322, 44 }));
    chanceSlider.setBounds(Rectangle<int>(44, 44).withCentre({ 402, 44 }));
    baseSlider.setBounds(Rectangle<int>(44, 44).withCentre({ 62, 148 }));
    maxSlider.setBounds(Rectangle<int>(44, 44).withCentre({ 154, 148 }));
    previewButton.setBounds(228, 132, 100, 24);
}

void SamplePanel::refresh()
{
    String text;
    if (numAvailable == 0)
        text = "NO SOUNDS FOUND";
    else
        text = "SOUNDS: " + String(bank.getNumSelected()) + " OF " + String(numAvailable);
    if (text != shownButtonText)
    {
        shownButtonText = text;
        chooseButton.setText(text);
    }

    // The bank list: its names, and which one is showing (none once the picked list has been changed since).
    const auto names = bank.getBankNames();
    const auto active = bank.getActiveBank();
    const String state = names.joinIntoString("|") + "#" + active + (bank.selectionMatchesActiveBank() ? "=" : "~");
    if (state != shownBankState)
    {
        shownBankState = state;
        const ScopedValueSetter<bool> guard(updatingControls, true);
        bankBox.clear(dontSendNotification);
        for (int i = 0; i < names.size(); ++i)
            bankBox.addItem(names[i], i + 1);
        bankBox.setTextWhenNothingSelected(names.isEmpty() ? "No banks yet" : (active.isEmpty() ? "Sound bank" : "Custom list"));
        if (bank.selectionMatchesActiveBank())
        {
            const int index = names.indexOf(active, true);
            if (index >= 0)
                bankBox.setSelectedId(index + 1, dontSendNotification);
        }
        deleteButton.setEnabled(!active.isEmpty());
        deleteButton.setAlpha(active.isEmpty() ? 0.45f : 1.0f);
    }
    repaint();
}

// One of the picked sounds, at a volume. The audio thread plays it through the same crush and filter stage as the
// sequence (so it sounds as it will in use), and does not need the sequencer to be running.
void SamplePanel::previewAt(float volumeDb)
{
    if (volumeDb > bitvibe::Engine::kSampleOffDb + 0.05f)
        proc.requestSamplePreview(volumeDb, previewPick);
}

// The same sound at the base volume, then at the max volume.
void SamplePanel::previewBoth()
{
    previewPick = (std::uint32_t) Random::getSystemRandom().nextInt();
    const float baseDb = rawValue(ids::sampleBase), maxDb = rawValue(ids::sampleMax);
    const bool baseAudible = baseDb > bitvibe::Engine::kSampleOffDb + 0.05f;
    if (baseAudible)
        previewAt(baseDb);
    const int waitMs = baseAudible ? (int) jlimit(400.0, 2500.0, bank.getClipSeconds(previewPick) * 1000.0 + 300.0) : 0;
    const auto pick = previewPick;
    Component::SafePointer<SamplePanel> safe(this);
    Timer::callAfterDelay(waitMs, [safe, maxDb, pick]
    {
        if (safe != nullptr)
        {
            safe->previewPick = pick;
            safe->previewAt(maxDb);
        }
    });
}

void SamplePanel::openFolder()
{
    const auto folder = SampleBank::userFolder();
    folder.createDirectory();
    folder.startAsProcess();
}

void SamplePanel::askBankName()
{
    const auto suggestion = bank.getActiveBank().isNotEmpty() ? bank.getActiveBank() : String("My bank");
    auto* window = new AlertWindow("Save sound bank", "Name this bank. Saving under a name that exists replaces that bank.",
                                   MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", suggestion);
    window->addButton("Save", 1, KeyPress(KeyPress::returnKey));
    window->addButton("Cancel", 0, KeyPress(KeyPress::escapeKey));
    Component::SafePointer<SamplePanel> safe(this);
    window->enterModalState(true,
                            ModalCallbackFunction::create(
                                [safe, window](int result)
                                {
                                    if (result == 1 && safe != nullptr)
                                    {
                                        safe->bank.saveBank(window->getTextEditorContents("name"));
                                        safe->refresh();
                                    }
                                }),
                            true);
}

void SamplePanel::askDeleteBank()
{
    const auto name = bank.getActiveBank();
    if (name.isEmpty())
        return;
    Component::SafePointer<SamplePanel> safe(this);
    AlertWindow::showOkCancelBox(MessageBoxIconType::QuestionIcon, "Delete sound bank",
                                 "Delete the bank \"" + name + "\"? Your sound files are not deleted.", "Delete", "Cancel", this,
                                 ModalCallbackFunction::create(
                                     [safe, name](int result)
                                     {
                                         if (result == 1 && safe != nullptr)
                                         {
                                             safe->bank.deleteBank(name);
                                             safe->refresh();
                                         }
                                     }));
}

// A menu of every sound with a tick on the picked ones. A click flips the sound and the menu comes straight back,
// so a whole list can be picked in one go; a click outside closes it.
void SamplePanel::showChooser()
{
    const auto files = SampleBank::libraryFiles();
    numAvailable = files.size();
    const auto picked = bank.getSelected();

    PopupMenu menu;
    menu.addItem(1, "Pick all");
    menu.addItem(2, "Pick none");
    menu.addSeparator();
    for (int i = 0; i < files.size(); ++i)
        menu.addItem(100 + i, files[i].getFileNameWithoutExtension(), true, picked.contains(files[i].getFileName()));
    if (files.isEmpty())
        menu.addItem(3, "No sounds found. Put audio files in the sounds folder.", false, false);

    Component::SafePointer<SamplePanel> safe(this);
    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&chooseButton).withMinimumWidth(240),
                       [safe, files, picked](int result)
                       {
                           if (safe == nullptr)
                               return;
                           auto& self = *safe;
                           if (result == 0)
                           {
                               self.refresh();
                               return;
                           }
                           if (result == 1)
                           {
                               self.bank.selectAll();
                           }
                           else if (result == 2)
                           {
                               self.bank.selectNone();
                           }
                           else if (result >= 100 && result < 100 + files.size())
                           {
                               auto list = picked;
                               const auto name = files[result - 100].getFileName();
                               if (list.contains(name))
                                   list.removeString(name);
                               else
                                   list.add(name);
                               self.bank.setSelected(list);
                           }
                           self.refresh();
                           self.showChooser();
                       });
}

void SamplePanel::paint(Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.setColour(panel);
    g.fillRoundedRectangle(bounds.toFloat(), 10.0f);
    g.setColour(panelEdge);
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 10.0f, 1.0f);
    g.setColour(inkDim);
    g.setFont(bold(10.0f));
    g.drawText("SAMPLE BLEND (RANDOM SOUND ON EACH STEP)", 14, 4, 290, 12, Justification::left);

    struct Entry { const char* name; String value; int cx, y; };
    auto volumeText = [](float db) { return db <= bitvibe::Engine::kSampleOffDb + 0.05f ? String("OFF") : String(db, 1) + " dB"; };
    const Entry entries[] = { { "MIX", String(roundToInt(rawValue(ids::sampleMix) * 100.0f)) + "%", 322, 76 },
                              { "CHANCE", String(roundToInt(rawValue(ids::sampleChance) * 100.0f)) + "%", 402, 76 },
                              { "BASE", volumeText(rawValue(ids::sampleBase)), 62, 180 },
                              { "MAX", volumeText(rawValue(ids::sampleMax)), 154, 180 } };
    for (const auto& e : entries)
    {
        g.setColour(ink);
        g.setFont(bold(11.0f));
        g.drawText(e.name, Rectangle<int>(80, 14).withCentre({ e.cx, e.y }), Justification::centred);
        g.setColour(inkDim);
        g.setFont(plain(10.5f));
        g.drawText(e.value, Rectangle<int>(80, 13).withCentre({ e.cx, e.y + 14 }), Justification::centred);
    }

    g.setColour(inkDim);
    g.setFont(bold(10.0f));
    g.drawText("VOLUME RANGE: THE MIX KNOB MOVES BETWEEN BASE AND MAX", 14, 116, 428, 12, Justification::left);
    g.setFont(plain(10.0f));
    g.drawFittedText("Plays a picked sound at the base volume, then at the max volume. Letting go of a knob plays it too.",
                     Rectangle<int>(228, 162, 214, 36), Justification::topLeft, 3);
}
