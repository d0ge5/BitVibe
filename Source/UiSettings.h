#pragma once

#include "SampleBank.h"

// The window size the player left the plug-in at, remembered between sessions in the same folder as the sample
// settings (see SampleBank::settingsFolder()). 1.0 is the size the editor is designed at.
namespace uisettings
{
inline constexpr float kMinScale = 0.6f, kMaxScale = 2.0f;

inline juce::File file() { return SampleBank::settingsFolder().getChildFile("ui_settings.xml"); }

inline float loadWindowScale()
{
    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file()));
    if (xml == nullptr || !xml->hasTagName("BITVIBE_UI"))
        return 1.0f;
    return juce::jlimit(kMinScale, kMaxScale, (float) xml->getDoubleAttribute("scale", 1.0));
}

inline void saveWindowScale(float scale)
{
    juce::XmlElement xml("BITVIBE_UI");
    xml.setAttribute("scale", (double) juce::jlimit(kMinScale, kMaxScale, scale));
    file().getParentDirectory().createDirectory();
    file().replaceWithText(xml.toString());
}
} // namespace uisettings
