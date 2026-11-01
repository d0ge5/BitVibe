#include "SamplePanel.h"

using namespace juce;
using namespace pedalstyle;

SamplePanel::SamplePanel(BitVibeProcessor& p, PedalLookAndFeel& lookAndFeel) : proc(p), bank(p.getSampleBank()), laf(lookAndFeel)
{
    setSize(kWidth, kHeight);

    for (auto* s : { &mixSlider, &chanceSlider, &baseSlider, &maxSlider, &lengthSlider })
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
    attachKnobs();
    lengthAtt = std::make_unique<APVTS::SliderAttachment>(proc.apvts, ids::sampleMaxBeats, lengthSlider);
    lengthSlider.setDoubleClickReturnValue(true, 0.0);
    lengthSlider.setTooltip("MAX BEATS: the longest a random sound plays, in beats of the tempo; then it fades out. Turn it down to tame long "
                            "sounds. Fully left = the whole sound.");

    // Letting go of the base or max knob plays one of the picked sounds at that volume (the same sound until the
    // next time a knob is picked up), so the player hears what they set.
    for (auto* s : { &baseSlider, &maxSlider })
    {
        s->onDragStart = [this] { previewPick = (std::uint32_t) Random::getSystemRandom().nextInt(); };
        s->onDragEnd = [this, s] { previewAt((float) s->getValue()); };
    }
    previewButton.onClick = [this] { previewBoth(); };
    addAndMakeVisible(previewButton);
    onButton.setLookAndFeel(&laf);
    makePowerButton(onButton);
    onAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, ids::sampleOn, onButton);
    onButton.onClick = [this]
    {
        if (onButton.getToggleState() && rawValue(mixId()) <= 0.0f)
        {
            pedalstyle::raiseMixIfSilent(proc.apvts, mixId(), 0.75f);
            rememberKnobs();
        }
    };
    onButton.setTooltip("SAMPLES ON: switches the random sounds on or off. Off, no sound is added to the sequence. Your list, banks and volumes "
                        "stay as they are. The light glows while the sequence plays, which is when the sounds come in.");
    addAndMakeVisible(onButton);

    bankBox.setLookAndFeel(&laf);
    bankBox.onChange = [this]
    {
        if (updatingControls)
            return;
        if (editingLoop() && bankBox.getSelectedId() == 1)
            bank.setLoopOwnSounds(false); // "Same as MAIN"
        else if (const auto name = bankBox.getText(); name.isNotEmpty())
            bank.loadBank(name, section);
        refresh();
    };

    // which steps the panel sets up, while the sequence has a loop section
    mainButton.onClick = [this] { showSection(SampleBank::kMain); };
    loopButton.onClick = [this] { showSection(SampleBank::kLoop); };
    mainButton.setTooltip("MAIN: set up the sounds of the steps outside the loop section.");
    loopButton.setTooltip("LOOP: set up the sounds of the loop section's steps (the LOOP steps in the global panel). They can "
                          "have a sound bank of their own, and with OWN LEVELS their own volumes and chance.");
    addChildComponent(mainButton);
    addChildComponent(loopButton);
    samplerButton.onClick = [this] { if (onOpenSampler) onOpenSampler(); };
    samplerButton.setTooltip("SAMPLER: record the sound of another program on this computer, pick a part, shape it with distortion, "
                             "bit crush and EQ, and save it as a new sound (optionally into a bank).");
    addAndMakeVisible(samplerButton);
    ownLevelsButton.setLookAndFeel(&laf);
    ownLevelsAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, ids::sampleLoopOwn, ownLevelsButton);
    ownLevelsButton.onClick = [this]
    {
        // switched on: the loop section's knobs start where the main ones are
        if (ownLevelsButton.getToggleState())
        {
            const std::array<std::pair<const char*, const char*>, 4> pairs { { { ids::sampleMix, ids::sampleLoopMix }, { ids::sampleChance, ids::sampleLoopChance },
                                                                               { ids::sampleBase, ids::sampleLoopBase }, { ids::sampleMax, ids::sampleLoopMax } } };
            for (const auto& [from, to] : pairs)
                if (auto* param = proc.apvts.getParameter(to))
                    param->setValueNotifyingHost(proc.apvts.getParameter(from)->getValue());
        }
        attachKnobs();
        rememberKnobs();
        repaint();
    };
    ownLevelsButton.setTooltip("OWN LEVELS FOR THE LOOP: off, the loop steps use the same SAMPLE MIX, CHANCE, BASE and MAX as the "
                               "other steps. On, they get their own (starting from the same values); the knobs then set the loop's.");
    addChildComponent(ownLevelsButton);
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
    previewButton.setTooltip("PREVIEW: plays one of the sounds you picked at the BASE volume, then the same sound at the MAX volume, so you can set both by ear. "
                             "Letting go of the BASE or MAX knob plays a sound at that volume too.");
    chanceSlider.setTooltip("SAMPLE CHANCE: how often a step gets a sound. 100% = every step, lower = now and then. Remembered next time.");

    numAvailable = SampleBank::libraryFiles().size();
    refresh();
}

SamplePanel::~SamplePanel()
{
    ownLevelsButton.setLookAndFeel(nullptr);
    mixSlider.setLookAndFeel(nullptr);
    chanceSlider.setLookAndFeel(nullptr);
    lengthSlider.setLookAndFeel(nullptr);
    onButton.setLookAndFeel(nullptr);
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
    levels.loopOwn = rawValue(ids::sampleLoopOwn) > 0.5f;
    levels.loopMix = rawValue(ids::sampleLoopMix);
    levels.loopChance = rawValue(ids::sampleLoopChance);
    levels.loopBaseDb = rawValue(ids::sampleLoopBase);
    levels.loopMaxDb = rawValue(ids::sampleLoopMax);
    bank.rememberLevels(levels);
}

bool SamplePanel::loopOwnLevels() const { return rawValue(ids::sampleLoopOwn) > 0.5f; }
const char* SamplePanel::mixId() const { return knobsOnLoop ? ids::sampleLoopMix : ids::sampleMix; }
const char* SamplePanel::chanceId() const { return knobsOnLoop ? ids::sampleLoopChance : ids::sampleChance; }
const char* SamplePanel::baseId() const { return knobsOnLoop ? ids::sampleLoopBase : ids::sampleBase; }
const char* SamplePanel::maxId() const { return knobsOnLoop ? ids::sampleLoopMax : ids::sampleMax; }

// The knobs set the main values, or the loop section's own ones while the LOOP section with OWN LEVELS is showing.
void SamplePanel::attachKnobs()
{
    knobsOnLoop = editingLoop() && loopOwnLevels();
    mixAtt.reset();
    chanceAtt.reset();
    baseAtt.reset();
    maxAtt.reset();
    mixAtt = std::make_unique<APVTS::SliderAttachment>(proc.apvts, mixId(), mixSlider);
    chanceAtt = std::make_unique<APVTS::SliderAttachment>(proc.apvts, chanceId(), chanceSlider);
    baseAtt = std::make_unique<APVTS::SliderAttachment>(proc.apvts, baseId(), baseSlider);
    maxAtt = std::make_unique<APVTS::SliderAttachment>(proc.apvts, maxId(), maxSlider);
    // double-click puts a knob back to its default
    const std::array<std::pair<Slider*, const char*>, 4> knobIds { { { &mixSlider, mixId() }, { &chanceSlider, chanceId() },
                                                                     { &baseSlider, baseId() }, { &maxSlider, maxId() } } };
    for (const auto& [slider, id] : knobIds)
        if (auto* param = proc.apvts.getParameter(id))
            slider->setDoubleClickReturnValue(true, (double) param->convertFrom0to1(param->getDefaultValue()));
}

void SamplePanel::showSection(int newSection)
{
    section = newSection == SampleBank::kLoop && loopShown ? SampleBank::kLoop : SampleBank::kMain;
    mainButton.lit = section == SampleBank::kMain;
    loopButton.lit = section == SampleBank::kLoop;
    mainButton.repaint();
    loopButton.repaint();
    shownBankState = {}; // the bank list is rebuilt for this section
    shownButtonText = {};
    attachKnobs();
    resized();
    refresh();
}

// While a knob is turned, a bubble shows its value; it lives in the editor so it is not cut off by this panel.
void SamplePanel::parentHierarchyChanged()
{
    if (auto* parent = getParentComponent())
        for (auto* s : { &mixSlider, &chanceSlider, &baseSlider, &maxSlider, &lengthSlider })
            s->setPopupDisplayEnabled(true, false, parent);
}

void SamplePanel::setAdvanced(bool on)
{
    advanced = on;
    for (Component* c : std::initializer_list<Component*> { &saveButton, &deleteButton, &baseSlider, &maxSlider, &previewButton })
        c->setVisible(on);
    resized();
    repaint();
}

void SamplePanel::resized()
{
    onButton.setBounds(8, 3, 50, 19);
    samplerButton.setBounds(196, 2, 82, 20);
    mainButton.setBounds(318, 2, 60, 20);
    loopButton.setBounds(382, 2, 60, 20);
    // the third row: the sounds folder (ADVANCED, main section), or the loop section's OWN LEVELS switch
    folderButton.setVisible((advanced || numAvailable == 0) && !editingLoop());
    folderButton.setText(numAvailable == 0 ? "ADD SOUNDS: OPEN THE FOLDER" : "OPEN SOUNDS FOLDER");
    ownLevelsButton.setVisible(editingLoop());
    ownLevelsButton.setBounds(12, 80, 238, 24);
    bankBox.setBounds(12, 24, advanced ? 108 : 238, 24);
    saveButton.setBounds(124, 24, 60, 24);
    deleteButton.setBounds(188, 24, 62, 24);
    chooseButton.setBounds(12, 52, 238, 24);
    folderButton.setBounds(12, 80, 238, 24);
    mixSlider.setBounds(Rectangle<int>(44, 44).withCentre({ 284, 44 }));
    chanceSlider.setBounds(Rectangle<int>(44, 44).withCentre({ 352, 44 }));
    lengthSlider.setBounds(Rectangle<int>(44, 44).withCentre({ 420, 44 }));
    baseSlider.setBounds(Rectangle<int>(44, 44).withCentre({ 62, 148 }));
    maxSlider.setBounds(Rectangle<int>(44, 44).withCentre({ 154, 148 }));
    previewButton.setBounds(228, 132, 100, 24);
}

void SamplePanel::lookForNewSounds(bool now)
{
    if (numAvailable > 0)
        return;
    const double nowMs = Time::getMillisecondCounterHiRes();
    if (!now && nowMs - lastScanMs < 2000.0)
        return;
    lastScanMs = nowMs;
    numAvailable = SampleBank::libraryFiles().size();
    if (numAvailable == 0)
        return;
    if (bank.getNumSelected(SampleBank::kMain) == 0)
        bank.selectAll(SampleBank::kMain);
    shownButtonText = {};
    resized();
}

void SamplePanel::refresh()
{
    lookForNewSounds();
    setPowerActive(onButton, proc.getEngine().isEffectSideActive());
    {
        const float alpha = rawValue(ids::sampleOn) > 0.5f ? 1.0f : kOffAlpha;
        for (auto* c : getChildren())
            if (c != &onButton)
                c->setAlpha(alpha);
    }
    // MAIN and LOOP are there while the sequence has a loop section
    const bool loopOn = proc.apvts.getRawParameterValue(ids::loopOn)->load() > 0.5f;
    if (loopOn != loopShown)
    {
        loopShown = loopOn;
        mainButton.setVisible(loopOn);
        loopButton.setVisible(loopOn);
        showSection(loopOn ? section : SampleBank::kMain);
    }
    if (knobsOnLoop != (editingLoop() && loopOwnLevels())) // the host or a project switched OWN LEVELS
        attachKnobs();

    const bool sameAsMain = editingLoop() && !bank.loopHasOwnSounds();
    String text;
    if (numAvailable == 0)
        text = "NO SOUNDS YET";
    else if (sameAsMain)
        text = "CHOOSE SOUNDS (SAME AS MAIN)";
    else
        text = "CHOOSE SOUNDS (" + String(bank.getNumSelected(section)) + " OF " + String(numAvailable) + ")";
    if (text != shownButtonText)
    {
        shownButtonText = text;
        chooseButton.setText(text);
    }

    // The bank list: its names, and which one is showing (none once the picked list has been changed since).
    // (in the LOOP section the list starts with "Same as MAIN")
    const auto names = bank.getBankNames();
    const auto active = bank.getActiveBank(section);
    const int first = editingLoop() ? 2 : 1;
    const String state = String(section) + (sameAsMain ? "S" : "O") + names.joinIntoString("|") + "#" + active
                         + (bank.selectionMatchesActiveBank(section) ? "=" : "~");
    if (state != shownBankState)
    {
        shownBankState = state;
        const ScopedValueSetter<bool> guard(updatingControls, true);
        bankBox.clear(dontSendNotification);
        if (editingLoop())
            bankBox.addItem("Same as MAIN", 1);
        for (int i = 0; i < names.size(); ++i)
            bankBox.addItem(names[i], i + first);
        bankBox.setTextWhenNothingSelected(names.isEmpty() ? "No banks yet" : (active.isEmpty() ? "Sound bank" : "Custom list"));
        if (sameAsMain)
        {
            bankBox.setSelectedId(1, dontSendNotification);
        }
        else if (bank.selectionMatchesActiveBank(section))
        {
            const int index = names.indexOf(active, true);
            if (index >= 0)
                bankBox.setSelectedId(index + first, dontSendNotification);
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
        proc.requestSamplePreview(volumeDb, previewPick, section);
}

// The same sound at the base volume, then at the max volume.
void SamplePanel::previewBoth()
{
    previewPick = (std::uint32_t) Random::getSystemRandom().nextInt();
    const float baseDb = rawValue(baseId()), maxDb = rawValue(maxId());
    const bool baseAudible = baseDb > bitvibe::Engine::kSampleOffDb + 0.05f;
    if (baseAudible)
        previewAt(baseDb);
    const int waitMs = baseAudible ? (int) jlimit(400.0, 2500.0, bank.getClipSeconds(previewPick, section) * 1000.0 + 300.0) : 0;
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
    lastScanMs = 0.0; // look again soon: the player is probably adding some
}

void SamplePanel::askBankName()
{
    const auto suggestion = bank.getActiveBank(section).isNotEmpty() ? bank.getActiveBank(section) : String("My bank");
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
                                        safe->bank.saveBank(window->getTextEditorContents("name"), safe->section);
                                        safe->refresh();
                                    }
                                }),
                            true);
}

void SamplePanel::askDeleteBank()
{
    const auto name = bank.getActiveBank(section);
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
    const auto picked = bank.getSelected(section); // in the LOOP section, the main list until it has its own

    // Sounds at the top of the sounds folder are listed straight away; those in subfolders get a submenu per folder,
    // each with its own pick all / none (item ids: 100 + sound, 10000 + 2 * folder for all, + 1 for none).
    StringArray groups;
    for (const auto& f : files)
        groups.addIfNotAlreadyThere(SampleBank::groupOf(f));
    groups.sortNatural();
    PopupMenu menu;
    menu.addItem(1, "Pick all");
    menu.addItem(2, "Pick none");
    menu.addSeparator();
    for (int g = 0; g < groups.size(); ++g)
    {
        const bool top = groups[g].isEmpty();
        PopupMenu folder;
        if (!top)
        {
            folder.addItem(10000 + 2 * g, "Pick all in " + groups[g]);
            folder.addItem(10001 + 2 * g, "Pick none in " + groups[g]);
            folder.addSeparator();
        }
        int pickedHere = 0, here = 0;
        for (int i = 0; i < files.size(); ++i)
            if (SampleBank::groupOf(files[i]) == groups[g])
            {
                const bool on = picked.contains(files[i].getFileName());
                (top ? menu : folder).addItem(100 + i, files[i].getFileNameWithoutExtension(), true, on);
                pickedHere += on ? 1 : 0;
                ++here;
            }
        if (!top)
            menu.addSubMenu(groups[g] + " (" + String(pickedHere) + " of " + String(here) + ")", folder, true, nullptr, pickedHere > 0);
    }
    if (files.isEmpty())
        menu.addItem(3, "No sounds found. Put audio files in the sounds folder.", false, false);

    Component::SafePointer<SamplePanel> safe(this);
    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&chooseButton).withMinimumWidth(240),
                       [safe, files, picked, groups](int result)
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
                               self.bank.selectAll(self.section);
                           }
                           else if (result == 2)
                           {
                               self.bank.selectNone(self.section);
                           }
                           else if (result >= 10000 && result < 10000 + 2 * groups.size())
                           {
                               // a whole folder at once
                               const auto& group = groups[(result - 10000) / 2];
                               const bool all = (result - 10000) % 2 == 0;
                               auto list = picked;
                               for (const auto& f : files)
                                   if (SampleBank::groupOf(f) == group)
                                   {
                                       list.removeString(f.getFileName());
                                       if (all)
                                           list.add(f.getFileName());
                                   }
                               self.bank.setSelected(list, true, self.section);
                           }
                           else if (result >= 100 && result < 100 + files.size())
                           {
                               auto list = picked;
                               const auto name = files[result - 100].getFileName();
                               if (list.contains(name))
                                   list.removeString(name);
                               else
                                   list.add(name);
                               self.bank.setSelected(list, true, self.section);
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
    drawPanelTitle(g, kPanelTitleX, 4, 1, "SAMPLE BLEND");
    const float alpha = rawValue(ids::sampleOn) > 0.5f ? 1.0f : kOffAlpha;

    struct Entry { const char* name; String value; int cx, y; };
    auto volumeText = [](float db) { return db <= bitvibe::Engine::kSampleOffDb + 0.05f ? String("OFF") : String(db, 1) + " dB"; };
    static const char* lengths[] { "WHOLE", "1/4 BEAT", "1/2 BEAT", "1 BEAT", "2 BEATS", "4 BEATS", "8 BEATS" };
    const Entry entries[] = { { "MIX", String(roundToInt(rawValue(mixId()) * 100.0f)) + "%", 284, 76 },
                              { "CHANCE", String(roundToInt(rawValue(chanceId()) * 100.0f)) + "%", 352, 76 },
                              { "MAX BEATS", lengths[jlimit(0, 6, roundToInt(rawValue(ids::sampleMaxBeats)))], 420, 76 },
                              { "BASE", volumeText(rawValue(baseId())), 62, 180 },
                              { "MAX", volumeText(rawValue(maxId())), 154, 180 } };
    for (const auto& e : entries)
        drawControlLabel(g, e.cx, e.y - 7, e.name, e.value, alpha);

    if (!advanced)
        return; // SIMPLE: no volume range row
    g.setColour(inkDim.withMultipliedAlpha(alpha));
    g.setFont(bold(10.0f));
    g.drawText("VOLUME RANGE", 14, 116, 428, 12, Justification::left);
}
