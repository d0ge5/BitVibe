#pragma once

#include "SampleBank.h"

#include <optional>

// Editor settings the player leaves behind, remembered between sessions in the same folder as the sample settings
// (see SampleBank::settingsFolder()): the window size (1.0 is the size the editor is designed at; a window is opened
// smaller when that would not fit on the screen, without forgetting the size chosen), the theme, and whether the
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

// The screen the editor opens on (the main display's area without the taskbar or dock). Tests set their own.
inline std::optional<juce::Rectangle<int>>& screenAreaOverride()
{
    static std::optional<juce::Rectangle<int>> area;
    return area;
}

inline juce::Rectangle<int> screenArea()
{
    if (screenAreaOverride().has_value())
        return *screenAreaOverride();
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        return display->userArea;
    return {};
}

// The size to open a window of baseWidth x baseHeight at: the one wanted, or smaller if that would not fit on the
// screen, leaving room for the host's own window frame and title bar. Never below kMinScale.
inline float fittingScale(float wanted, int baseWidth, int baseHeight, juce::Rectangle<int> screen)
{
    constexpr int kFrameWidth = 16, kFrameHeight = 90;
    if (screen.isEmpty() || baseWidth <= 0 || baseHeight <= 0)
        return juce::jlimit(kMinScale, kMaxScale, wanted);
    const float fit = juce::jmin((float) (screen.getWidth() - kFrameWidth) / (float) baseWidth,
                                 (float) (screen.getHeight() - kFrameHeight) / (float) baseHeight);
    return juce::jlimit(kMinScale, kMaxScale, juce::jmin(wanted, std::floor(fit * 100.0f) / 100.0f));
}

// The right-hand column: false = SIMPLE (the controls that matter most), true = ADVANCED (everything).
inline bool advancedRight() { return read()->getBoolAttribute("advancedRight", false); }

inline void setAdvancedRight(bool advanced)
{
    auto xml = read();
    xml->setAttribute("advancedRight", advanced);
    write(*xml);
}

// The length of the EQ's reference clip, in seconds.
inline float eqClipSeconds() { return (float) read()->getDoubleAttribute("eqClipSeconds", 4.0); }

inline void setEqClipSeconds(float seconds)
{
    auto xml = read();
    xml->setAttribute("eqClipSeconds", (double) seconds);
    write(*xml);
}

// The effect panel showing on the right-hand side (0 = the sample blend .. 5 = the EQ).
inline int effectTab() { return juce::jlimit(0, 6, read()->getIntAttribute("effectTab", 0)); }

inline void setEffectTab(int tab)
{
    auto xml = read();
    xml->setAttribute("effectTab", juce::jlimit(0, 6, tab));
    write(*xml);
}

// The theme the page is drawn in, by name (so a theme can be added without changing what was picked).
// A player who has not chosen a theme gets SYNTHWAVE.
inline juce::String themeName() { return read()->getStringAttribute("theme", "SYNTHWAVE"); }

inline void setThemeName(const juce::String& name)
{
    auto xml = read();
    xml->setAttribute("theme", name);
    write(*xml);
}

// FAVOURITES: the presets the player has starred, by the key BitVibeProcessor::favouriteKey() gives each one. They
// belong to the player, not to a project, so every project and every copy of the plug-in on this computer shares them.
inline juce::StringArray favourites()
{
    juce::StringArray keys;
    const auto xml = read();
    if (const auto* list = xml->getChildByName("FAVOURITES"))
        for (const auto* e : list->getChildWithTagNameIterator("PRESET"))
            keys.addIfNotAlreadyThere(e->getStringAttribute("key"));
    keys.removeEmptyStrings();
    return keys;
}

inline void setFavourites(const juce::StringArray& keys)
{
    auto xml = read();
    xml->deleteAllChildElementsWithTagName("FAVOURITES");
    auto* list = xml->createNewChildElement("FAVOURITES");
    for (const auto& key : keys)
        list->createNewChildElement("PRESET")->setAttribute("key", key);
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
