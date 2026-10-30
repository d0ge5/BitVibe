#pragma once

#include "SampleBank.h"

// Editor settings the player leaves behind, remembered between sessions in the same folder as the sample settings
// (see SampleBank::settingsFolder()): the window size (1.0 is the size the editor is designed at) and whether the
// quick-start help has been seen (it opens by itself the first time).
namespace uisettings
{
inline constexpr float kMinScale = 0.6f, kMaxScale = 2.0f;

inline juce::File file() { return SampleBank::settingsFolder().getChildFile("ui_settings.xml"); }

inline std::unique_ptr<juce::XmlElement> read()
{
    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file()));
    if (xml == nullptr || !xml->hasTagName("BITVIBE_UI"))
        xml = std::make_unique<juce::XmlElement>("BITVIBE_UI");
    return xml;
}

inline void write(const juce::XmlElement& xml)
{
    file().getParentDirectory().createDirectory();
    file().replaceWithText(xml.toString());
}

inline float loadWindowScale()
{
    return juce::jlimit(kMinScale, kMaxScale, (float) read()->getDoubleAttribute("scale", 1.0));
}

inline void saveWindowScale(float scale)
{
    auto xml = read();
    xml->setAttribute("scale", (double) juce::jlimit(kMinScale, kMaxScale, scale));
    write(*xml);
}

// The right-hand column: false = SIMPLE (the controls that matter most), true = ADVANCED (everything).
inline bool advancedRight() { return read()->getBoolAttribute("advancedRight", false); }

inline void setAdvancedRight(bool advanced)
{
    auto xml = read();
    xml->setAttribute("advancedRight", advanced);
    write(*xml);
}

inline bool helpSeen() { return read()->getBoolAttribute("helpSeen", false); }

inline void markHelpSeen()
{
    auto xml = read();
    xml->setAttribute("helpSeen", true);
    write(*xml);
}
} // namespace uisettings
