#include "SamplePanel.h"

using namespace juce;
using namespace pedalstyle;

SamplePanel::SamplePanel(BitVibeProcessor& p, PedalLookAndFeel& lookAndFeel) : proc(p), bank(p.getSampleBank()), laf(lookAndFeel)
{
    setSize(kWidth, kHeight);

    for (auto* s : { &mixSlider, &chanceSlider })
    {
        s->setSliderStyle(Slider::RotaryVerticalDrag);
        s->setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
        s->setRotaryParameters(MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
        s->setMouseDragSensitivity(180);
        s->setLookAndFeel(&laf);
        addAndMakeVisible(*s);
    }
    mixAtt = std::make_unique<APVTS::SliderAttachment>(proc.apvts, ids::sampleMix, mixSlider);
    chanceAtt = std::make_unique<APVTS::SliderAttachment>(proc.apvts, ids::sampleChance, chanceSlider);

    chooseButton.onClick = [this] { showChooser(); };
    folderButton.onClick = [this] { openFolder(); };
    addAndMakeVisible(chooseButton);
    addAndMakeVisible(folderButton);

    chooseButton.setTooltip("CHOOSE SOUNDS: pick the list of sounds to choose from. At every step of the sequence one of "
                            "them is picked at random and blended in.");
    folderButton.setTooltip("OPEN SOUNDS FOLDER: opens the folder for your own sounds (WAV, AIFF, FLAC or OGG files). "
                            "Put files in it, then pick them with CHOOSE SOUNDS.");
    mixSlider.setTooltip("SAMPLE MIX: how loud the random sound is in the sequence. Fully left = no sounds (the default).");
    chanceSlider.setTooltip("SAMPLE CHANCE: how often a step gets a sound. 100% = every step, lower = now and then.");

    numAvailable = SampleBank::libraryFiles().size();
    refresh();
}

SamplePanel::~SamplePanel()
{
    mixSlider.setLookAndFeel(nullptr);
    chanceSlider.setLookAndFeel(nullptr);
}

float SamplePanel::rawValue(const char* id) const
{
    return proc.apvts.getRawParameterValue(id)->load();
}

void SamplePanel::resized()
{
    chooseButton.setBounds(12, 24, 196, 24);
    folderButton.setBounds(12, 56, 196, 24);
    mixSlider.setBounds(Rectangle<int>(44, 44).withCentre({ 300, 36 }));
    chanceSlider.setBounds(Rectangle<int>(44, 44).withCentre({ 396, 36 }));
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
    repaint();
}

void SamplePanel::openFolder()
{
    const auto folder = SampleBank::userFolder();
    folder.createDirectory();
    folder.startAsProcess();
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

    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&chooseButton).withMinimumWidth(240),
                       [this, files, picked](int result)
                       {
                           if (result == 0)
                           {
                               refresh();
                               return;
                           }
                           if (result == 1)
                           {
                               bank.selectAll();
                           }
                           else if (result == 2)
                           {
                               bank.selectNone();
                           }
                           else if (result >= 100 && result < 100 + files.size())
                           {
                               auto list = picked;
                               const auto name = files[result - 100].getFileName();
                               if (list.contains(name))
                                   list.removeString(name);
                               else
                                   list.add(name);
                               bank.setSelected(list);
                           }
                           refresh();
                           showChooser();
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

    struct Entry { const char* name; String value; int cx; };
    const Entry entries[] = { { "MIX", String(roundToInt(rawValue(ids::sampleMix) * 100.0f)) + "%", 300 },
                              { "CHANCE", String(roundToInt(rawValue(ids::sampleChance) * 100.0f)) + "%", 396 } };
    for (const auto& e : entries)
    {
        g.setColour(ink);
        g.setFont(bold(11.0f));
        g.drawText(e.name, Rectangle<int>(90, 14).withCentre({ e.cx, 63 }), Justification::centred);
        g.setColour(inkDim);
        g.setFont(plain(10.5f));
        g.drawText(e.value, Rectangle<int>(90, 13).withCentre({ e.cx, 77 }), Justification::centred);
    }
}
