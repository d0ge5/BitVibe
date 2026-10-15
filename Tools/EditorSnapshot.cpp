// Instantiates the processor and editor directly (no plugin host) and renders the GUI to PNG files,
// so the look can be checked without opening a DAW.   OttobitEditorSnapshot <output directory>

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

using namespace juce;

namespace
{
void setValue(OttobitProcessor& p, const char* id, float plain)
{
    if (auto* prm = p.apvts.getParameter(id))
        prm->setValueNotifyingHost(prm->convertTo0to1(plain));
}

void save(OttobitEditor& ed, const File& file)
{
    auto img = ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.5f);
    file.deleteFile();
    PNGImageFormat png;
    if (auto out = std::unique_ptr<FileOutputStream>(file.createOutputStream()))
        png.writeImageToStream(img, *out);
    std::printf("wrote %s (%d bytes)\n", file.getFullPathName().toRawUTF8(), (int) file.getSize());
}
} // namespace

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2)
    {
        std::puts("usage: OttobitEditorSnapshot <output dir>");
        return 2;
    }
    ScopedJuceInitialiser_GUI init;
    const File outDir(argv[1]);
    outDir.createDirectory();

    OttobitProcessor processor;
    processor.setPlayConfigDetails(2, 2, 48000.0, 512);
    processor.prepareToPlay(48000.0, 512);

    {
        std::unique_ptr<OttobitEditor> ed(static_cast<OttobitEditor*>(processor.createEditor()));
        save(*ed, outDir.getChildFile("editor_front.png"));
        ed->setAltLayer(true);
        save(*ed, outDir.getChildFile("editor_alt_pitch.png"));
    }

    // A "played" state: crushed, stutter, filter sequencer, steps showing percentages.
    setValue(processor, "sample", 0.55f);
    setValue(processor, "bits", 0.3f);
    setValue(processor, "filter", 0.7f);
    setValue(processor, "stutter", 12.0f);
    setValue(processor, "seqType", 1.0f);
    setValue(processor, "hold", 1.0f);
    {
        std::unique_ptr<OttobitEditor> ed(static_cast<OttobitEditor*>(processor.createEditor()));
        save(*ed, outDir.getChildFile("editor_played.png"));
        ed->setAltLayer(true);
        save(*ed, outDir.getChildFile("editor_alt_filter.png"));
    }

    // A few blocks through the processor after the editor has come and gone.
    AudioBuffer<float> buffer(2, 512);
    MidiBuffer midi;
    for (int i = 0; i < 50; ++i)
    {
        for (int n = 0; n < 512; ++n)
            buffer.setSample(0, n, 0.4f * std::sin(0.03f * (float) (n + i * 512))), buffer.setSample(1, n, buffer.getSample(0, n));
        processor.processBlock(buffer, midi);
    }
    std::puts("processor + editor lifecycle completed");
    return 0;
}
