#include "EqPanel.h"
#include "UiSettings.h"

using namespace juce;
using namespace bitvibe;
using namespace pedalstyle;

namespace
{
constexpr double kGraphRate = 48000.0; // the curve is drawn as it sounds at 48 kHz
constexpr float kRangeDb = 15.0f;
constexpr int kRowY = 132, kKnobY = 158;
constexpr std::array<float, 7> kClipLengths { 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f }; // seconds, in the length box
} // namespace

EqPanel::EqPanel(BitVibeProcessor& p, PedalLookAndFeel& lookAndFeel, const String& prefix, bool withReferenceTools)
    : proc(p), laf(lookAndFeel), idPrefix(prefix), onId(prefix + "On"), outputId(prefix + "Output"), referenceTools(withReferenceTools)
{
    onButton.setLookAndFeel(&laf);
    makePowerButton(onButton);
    onAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, onId, onButton);
    onButton.setTooltip("EQ ON: switches the EQ on or off. It is the last thing the sequenced sound goes through; your clean signal never does. "
                        "The light glows while the sequence or stutter plays.");
    addAndMakeVisible(onButton);

    bandOnButton.setLookAndFeel(&laf);
    bandOnButton.setTooltip("BAND ON: switches the selected band on or off (double-clicking its point in the graph does the same).");
    addAndMakeVisible(bandOnButton);

    for (int t = 0; t < kNumEqTypes; ++t)
        typeBox.addItem(eqTypeName((EqType) t), t + 1);
    typeBox.setLookAndFeel(&laf);
    typeBox.setTooltip("BAND TYPE: Low Cut and High Cut remove everything below or above the frequency, the shelves lift or lower it, "
                       "a Bell boosts or cuts around it, a Notch takes out a narrow slice.");
    addAndMakeVisible(typeBox);

    for (int b = 0; b < kNumEqBands; ++b)
    {
        bandButtons[(size_t) b] = std::make_unique<PedalButton>(String(b + 1));
        bandButtons[(size_t) b]->onClick = [this, b] { selectBand(b); };
        bandButtons[(size_t) b]->setTooltip("Band " + String(b + 1) + ": click to edit it below (or click its point in the graph).");
        addAndMakeVisible(*bandButtons[(size_t) b]);
    }

    for (auto* s : { &freqSlider, &gainSlider, &qSlider, &outputSlider })
    {
        s->setSliderStyle(Slider::RotaryVerticalDrag);
        s->setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
        s->setRotaryParameters(MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
        s->setMouseDragSensitivity(180);
        s->setLookAndFeel(&laf);
        addAndMakeVisible(*s);
    }
    freqSlider.setTooltip("FREQ: where the selected band works, 20 Hz to 20 kHz. Or drag its point in the graph left and right.");
    gainSlider.setTooltip("GAIN: how much the selected band lifts (right) or lowers (left) its frequencies, up to 15 dB. Or drag its point up and down.");
    qSlider.setTooltip("Q: how narrow the selected band is (right = narrower). Or turn the mouse wheel over its point in the graph.");
    outputSlider.setTooltip("OUTPUT: the level after the EQ, -15 to +15 dB, to make up for what the bands took away or added.");
    outputAtt = std::make_unique<APVTS::SliderAttachment>(proc.apvts, outputId, outputSlider);
    if (auto* param = proc.apvts.getParameter(outputId))
        outputSlider.setDoubleClickReturnValue(true, (double) param->convertFrom0to1(param->getDefaultValue()));

    captureButton.setTooltip("CAPTURE: keep the last few seconds (up to 8) of the sound reaching the EQ as a reference clip. "
                             "Play a part, then press it. The box next to it sets how much of it is used.");
    playButton.setTooltip("PLAY CLIP: loop the captured clip through the EQ instead of the live sound, so you can set the EQ "
                          "with your hands free. Press again to go back to the live sound.");
    saveClipButton.setTooltip("SAVE CLIP: save the captured clip as a WAV file (before the EQ), to use as a reference elsewhere.");
    captureButton.onClick = [this]
    {
        auto& ref = proc.getEngine().getEqReference();
        ref.setAudition(false);
        ref.requestCapture();
        clipStatus = "capturing...";
        updateClipButtons();
    };
    playButton.onClick = [this]
    {
        auto& ref = proc.getEngine().getEqReference();
        ref.setAudition(!ref.getAudition() && ref.getClipSamples() > 0);
        updateClipButtons();
    };
    saveClipButton.onClick = [this] { saveClip(); };
    {
        const float remembered = uisettings::eqClipSeconds();
        int chosen = 4;
        for (int i = 0; i < (int) kClipLengths.size(); ++i)
        {
            const float s = kClipLengths[(size_t) i];
            lengthBox.addItem(s < 1.0f ? String(s, 1) + " s" : String((int) s) + " s", i + 1);
            if (std::abs(s - remembered) < 0.01f)
                chosen = i;
        }
        lengthBox.setSelectedId(chosen + 1, dontSendNotification);
        if (referenceTools)
            proc.getEngine().getEqReference().setClipSeconds(kClipLengths[(size_t) chosen]);
        lengthBox.onChange = [this]
        {
            const float s = kClipLengths[(size_t) jlimit(0, (int) kClipLengths.size() - 1, lengthBox.getSelectedId() - 1)];
            proc.getEngine().getEqReference().setClipSeconds(s);
            uisettings::setEqClipSeconds(s);
            clipStatus = {};
            updateClipButtons();
        };
        lengthBox.setLookAndFeel(&laf);
        lengthBox.setTooltip("CLIP LENGTH: how much of the captured clip is played and saved (its last part). You can change it "
                             "after capturing; a capture keeps up to 8 seconds.");
        addAndMakeVisible(lengthBox);
    }
    for (auto* b : { &captureButton, &playButton, &saveClipButton })
        addChildComponent(*b);
    for (Component* c : std::initializer_list<Component*> { &captureButton, &playButton, &saveClipButton, &lengthBox })
        c->setVisible(referenceTools);
    for (int i = 0; i < kColumns; ++i)
        levelBefore[(size_t) i] = levelAfter[(size_t) i] = -120.0f;
    updateClipButtons();

    selectBand(selected);
    setSize(kWidth, kHeight); // last: resized() needs the band buttons
}

EqPanel::~EqPanel()
{
    if (referenceTools)
        proc.getEngine().getEqReference().setAudition(false); // the clip never plays without its button in sight
    for (Component* c : std::initializer_list<Component*> { &onButton, &bandOnButton, &typeBox, &lengthBox, &freqSlider, &gainSlider, &qSlider, &outputSlider })
        c->setLookAndFeel(nullptr);
}

void EqPanel::visibilityChanged()
{
    if (!isVisible() && referenceTools)
        proc.getEngine().getEqReference().setAudition(false);
}

void EqPanel::parentHierarchyChanged()
{
    if (auto* parent = getParentComponent())
        for (auto* s : { &freqSlider, &gainSlider, &qSlider, &outputSlider })
            s->setPopupDisplayEnabled(true, false, parent);
}

// The settings below the graph follow the selected band.
void EqPanel::selectBand(int band)
{
    selected = jlimit(0, kNumEqBands - 1, band);
    freqAtt.reset();
    gainAtt.reset();
    qAtt.reset();
    typeAtt.reset();
    bandOnAtt.reset();
    freqAtt = std::make_unique<APVTS::SliderAttachment>(proc.apvts, bandId(selected, "Freq"), freqSlider);
    gainAtt = std::make_unique<APVTS::SliderAttachment>(proc.apvts, bandId(selected, "Gain"), gainSlider);
    qAtt = std::make_unique<APVTS::SliderAttachment>(proc.apvts, bandId(selected, "Q"), qSlider);
    typeAtt = std::make_unique<APVTS::ComboBoxAttachment>(proc.apvts, bandId(selected, "Type"), typeBox);
    bandOnAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, bandId(selected, "On"), bandOnButton);
    const std::array<std::pair<Slider*, const char*>, 3> knobs { { { &freqSlider, "Freq" }, { &gainSlider, "Gain" }, { &qSlider, "Q" } } };
    for (const auto& [slider, what] : knobs)
        if (auto* param = proc.apvts.getParameter(bandId(selected, what)))
            slider->setDoubleClickReturnValue(true, (double) param->convertFrom0to1(param->getDefaultValue()));
    for (int b = 0; b < kNumEqBands; ++b)
    {
        bandButtons[(size_t) b]->lit = b == selected;
        bandButtons[(size_t) b]->repaint();
    }
    repaint();
}

void EqPanel::resized()
{
    onButton.setBounds(8, 3, 50, 19);
    captureButton.setBounds(106, 2, 62, 20);
    lengthBox.setBounds(171, 2, 52, 20);
    playButton.setBounds(226, 2, 68, 20);
    saveClipButton.setBounds(297, 2, 68, 20);
    for (int b = 0; b < kNumEqBands; ++b)
        bandButtons[(size_t) b]->setBounds(12 + b * 25, kRowY, 23, 22);
    typeBox.setBounds(216, kRowY, 112, 22);
    bandOnButton.setBounds(334, kRowY, 110, 22);
    std::array<Slider*, 4> knobs { &freqSlider, &gainSlider, &qSlider, &outputSlider };
    for (size_t i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds(12 + (int) i * 112, kKnobY, 34, 34);
}

String EqPanel::bandId(int band, const char* what) const { return idPrefix + String(band + 1) + what; }

float EqPanel::rawValue(const String& id) const
{
    return proc.apvts.getRawParameterValue(id)->load();
}

std::array<EqBand, kNumEqBands> EqPanel::currentBands() const
{
    std::array<EqBand, kNumEqBands> bands {};
    for (int b = 0; b < kNumEqBands; ++b)
    {
        auto& band = bands[(size_t) b];
        band.on = rawValue(bandId(b, "On")) > 0.5f;
        band.type = (EqType) jlimit(0, kNumEqTypes - 1, roundToInt(rawValue(bandId(b, "Type"))));
        band.freq = rawValue(bandId(b, "Freq"));
        band.gainDb = rawValue(bandId(b, "Gain"));
        band.q = rawValue(bandId(b, "Q"));
    }
    return bands;
}

Rectangle<float> EqPanel::graphArea() const { return { 12.0f, 24.0f, 432.0f, 102.0f }; }

float EqPanel::xForHz(float hz) const
{
    const auto g = graphArea();
    return g.getX() + g.getWidth() * std::log(jlimit(kEqMinHz, kEqMaxHz, hz) / kEqMinHz) / std::log(kEqMaxHz / kEqMinHz);
}

float EqPanel::hzForX(float x) const
{
    const auto g = graphArea();
    return kEqMinHz * std::pow(kEqMaxHz / kEqMinHz, jlimit(0.0f, 1.0f, (x - g.getX()) / g.getWidth()));
}

float EqPanel::yForDb(float db) const
{
    const auto g = graphArea();
    return g.getCentreY() - (g.getHeight() * 0.5f - 6.0f) * jlimit(-1.2f, 1.2f, db / kRangeDb);
}

float EqPanel::dbForY(float y) const
{
    const auto g = graphArea();
    return jlimit(-kEqMaxGainDb, kEqMaxGainDb, (g.getCentreY() - y) / (g.getHeight() * 0.5f - 6.0f) * kRangeDb);
}

Point<float> EqPanel::bandPoint(const EqBand& b) const
{
    return { xForHz(b.freq), yForDb(eqTypeHasGain(b.type) ? b.gainDb : 0.0f) };
}

int EqPanel::bandAt(Point<float> p) const
{
    const auto bands = currentBands();
    int best = -1;
    float bestDistance = 10.0f;
    for (int b = 0; b < kNumEqBands; ++b)
    {
        const float d = bandPoint(bands[(size_t) b]).getDistanceFrom(p);
        if (d < bestDistance)
        {
            bestDistance = d;
            best = b;
        }
    }
    return best;
}

void EqPanel::setParam(const String& id, float value)
{
    if (auto* param = proc.apvts.getParameter(id))
        param->setValueNotifyingHost(param->convertTo0to1(value));
}

void EqPanel::mouseDown(const MouseEvent& e)
{
    const int b = bandAt(e.position);
    if (b < 0)
        return;
    if (b != selected)
        selectBand(b);
    dragging = b;
    for (const char* what : { "Freq", "Gain" })
        if (auto* param = proc.apvts.getParameter(bandId(b, what)))
            param->beginChangeGesture();
}

void EqPanel::mouseDrag(const MouseEvent& e)
{
    if (dragging < 0)
        return;
    setParam(bandId(dragging, "Freq"), hzForX(e.position.x));
    const auto type = (EqType) jlimit(0, kNumEqTypes - 1, roundToInt(rawValue(bandId(dragging, "Type"))));
    if (eqTypeHasGain(type))
        setParam(bandId(dragging, "Gain"), dbForY(e.position.y));
    repaint();
}

void EqPanel::mouseUp(const MouseEvent&)
{
    if (dragging >= 0)
        for (const char* what : { "Freq", "Gain" })
            if (auto* param = proc.apvts.getParameter(bandId(dragging, what)))
                param->endChangeGesture();
    dragging = -1;
}

void EqPanel::mouseDoubleClick(const MouseEvent& e)
{
    const int b = bandAt(e.position);
    if (b >= 0)
        if (auto* param = proc.apvts.getParameter(bandId(b, "On")))
            param->setValueNotifyingHost(rawValue(bandId(b, "On")) > 0.5f ? 0.0f : 1.0f);
}

void EqPanel::mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& wheel)
{
    const int b = bandAt(e.position);
    if (b < 0)
        return;
    const float q = rawValue(bandId(b, "Q")) * std::pow(1.15f, wheel.deltaY > 0.0f ? 1.0f : -1.0f);
    setParam(bandId(b, "Q"), jlimit(kEqMinQ, kEqMaxQ, q));
}

void EqPanel::refresh()
{
    setPowerActive(onButton, referenceTools && proc.getEngine().isEffectSideActive());
    {
        // switched off: the band settings dim (the reference clip still works, to hear the sound without the EQ)
        const float alpha = rawValue(onId) > 0.5f ? 1.0f : kOffAlpha;
        for (Component* c : std::initializer_list<Component*> { &bandOnButton, &typeBox, &freqSlider, &gainSlider, &qSlider, &outputSlider })
            c->setAlpha(alpha);
        for (auto& b : bandButtons)
            b->setAlpha(alpha);
    }
    if (referenceTools) // the sampler's copy has no reference clip and no live spectrum
    {
        updateClipButtons();
        updateSpectrum();
    }
    repaint();
}

void EqPanel::updateClipButtons()
{
    const auto& ref = proc.getEngine().getEqReference();
    const bool pending = ref.isCapturePending();
    const int samples = ref.getClipSamples();
    if (!pending && clipStatus == "capturing...")
        clipStatus = samples > 0 ? String() : String("heard nothing");
    const bool has = samples > 0 && !pending;
    if (has && clipStatus.isEmpty())
    {
        const double rate = proc.getEngine().getSampleRate();
        const int used = ref.getUsedSamples();
        clipStatus = used < samples ? String(used / rate, 1) + " of " + String(samples / rate, 1) + " s" : String(samples / rate, 1) + " s clip";
    }
    if (!has && (clipStatus.endsWith("clip") || clipStatus.endsWith(" s")))
        clipStatus = {};
    const bool playing = has && ref.getAudition();
    if (playButton.lit != playing)
    {
        playButton.lit = playing;
        playButton.repaint();
    }
    for (auto* b : { &playButton, &saveClipButton })
    {
        b->setEnabled(has);
        b->setAlpha(has ? 1.0f : 0.4f);
    }
}

void EqPanel::saveClip()
{
    auto folder = File::getSpecialLocation(File::userDocumentsDirectory).getChildFile("BitVibe").getChildFile("Clips");
    folder.createDirectory();
    clipChooser = std::make_unique<FileChooser>("Save the reference clip", folder.getChildFile("BitVibe clip.wav"), "*.wav");
    Component::SafePointer<EqPanel> safe(this);
    clipChooser->launchAsync(FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                             [safe](const FileChooser& chooser)
                             {
                                 if (safe == nullptr)
                                     return;
                                 auto file = chooser.getResult();
                                 if (file == File())
                                     return;
                                 if (!file.hasFileExtension(".wav"))
                                     file = file.withFileExtension(".wav");
                                 String message;
                                 const bool ok = safe->proc.saveCapturedClip(file, message);
                                 AlertWindow::showMessageBoxAsync(ok ? MessageBoxIconType::InfoIcon : MessageBoxIconType::WarningIcon,
                                                                  ok ? "Clip saved" : "Could not save the clip", message);
                             });
}

// The spectrum of the latest samples before and after the EQ (Hann window), one level per column of the graph: the
// loudest bin in the column's frequency range. Levels rise at once and fall back slowly, so the picture is readable.
void EqPanel::updateSpectrum()
{
    scopeBefore.resize((size_t) kFftSize);
    scopeAfter.resize((size_t) kFftSize);
    frame.resize((size_t) kFftSize);
    proc.getEngine().getEqReference().readScope(scopeBefore.data(), scopeAfter.data(), kFftSize);
    const double rate = proc.getEngine().getSampleRate();
    const double binHz = rate / (double) kFftSize;
    const float toDb = 20.0f * std::log10(4.0f / (float) kFftSize); // a full-scale sine reads 0 dBFS
    const auto area = graphArea();
    auto analyse = [&](const std::vector<float>& samples, std::array<float, kColumns>& levels)
    {
        for (int i = 0; i < kFftSize; ++i)
            frame[(size_t) i] = samples[(size_t) i] * (0.5f - 0.5f * std::cos(MathConstants<float>::twoPi * (float) i / (float) kFftSize));
        fft.powerSpectrum(frame.data(), power);
        for (int col = 0; col < kColumns; ++col)
        {
            const float hzLo = hzForX(area.getX() + area.getWidth() * (float) col / (float) kColumns);
            const float hzHi = hzForX(area.getX() + area.getWidth() * (float) (col + 1) / (float) kColumns);
            const int k0 = jlimit(1, kFftSize / 2, (int) std::floor(hzLo / binHz));
            const int k1 = jlimit(k0, kFftSize / 2, (int) std::ceil(hzHi / binHz));
            double loudest = 0.0;
            for (int k = k0; k <= k1; ++k)
                loudest = std::max(loudest, power[(size_t) k]);
            const float db = loudest > 0.0 ? 10.0f * (float) std::log10(loudest) + toDb : -120.0f;
            auto& level = levels[(size_t) col];
            level = db > level ? db : std::max(db, level - 1.5f);
        }
    };
    analyse(scopeBefore, levelBefore);
    analyse(scopeAfter, levelAfter);
}

// The spectrum's scale: 0 dBFS at the top of the graph, -84 dBFS at the bottom.
float EqPanel::yForLevel(float dbfs) const
{
    const auto g = graphArea();
    return g.getBottom() - g.getHeight() * jlimit(0.0f, 1.0f, (dbfs + 84.0f) / 84.0f);
}

void EqPanel::paint(Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.setColour(panel);
    g.fillRoundedRectangle(bounds.toFloat(), 10.0f);
    g.setColour(panelEdge);
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 10.0f, 1.0f);
    g.setColour(inkDim);
    g.setFont(bold(10.0f));
    if (referenceTools)
        drawPanelTitle(g, kPanelTitleX, 4, 5, "EQ");
    else
    {
        g.setColour(inkDim);
        g.setFont(bold(10.0f));
        g.drawText("EQ", kPanelTitleX, 5, 100, 12, Justification::left);
    }
    g.setColour(inkDim);
    g.setFont(plain(10.0f));
    g.drawText(clipStatus, 369, 4, 80, 14, Justification::left);

    const bool on = rawValue(onId) > 0.5f;
    const auto bands = currentBands();
    const float outputDb = rawValue(outputId);

    // the graph: dB lines every 6 dB, frequency lines at 1-2-5 steps
    const auto area = graphArea();
    g.setColour(Colour(0xff0e0e10));
    g.fillRoundedRectangle(area, 4.0f);
    g.setFont(plain(9.0f));
    for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        g.setColour(Colour(db == 0.0f ? 0xff3a3a40 : 0xff222226));
        g.drawHorizontalLine(roundToInt(yForDb(db)), area.getX(), area.getRight());
        g.setColour(inkDim.withAlpha(0.6f));
        g.drawText(String((int) db), roundToInt(area.getX()) + 2, roundToInt(yForDb(db)) - 10, 22, 10, Justification::left);
    }
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        g.setColour(Colour(0xff222226));
        g.drawVerticalLine(roundToInt(xForHz(hz)), area.getY(), area.getBottom());
        if (hz == 100.0f || hz == 1000.0f || hz == 10000.0f)
        {
            g.setColour(inkDim.withAlpha(0.6f));
            g.drawText(hz >= 1000.0f ? String((int) (hz / 1000.0f)) + "k" : String((int) hz), roundToInt(xForHz(hz)) + 2, roundToInt(area.getBottom()) - 11, 30, 10, Justification::left);
        }
    }

    // the spectrum behind the curve: before the EQ filled in grey, after it as an orange line
    {
        Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(area.toNearestInt());
        Path before, after;
        before.startNewSubPath(area.getX(), area.getBottom());
        for (int col = 0; col < kColumns; ++col)
        {
            const float x = area.getX() + area.getWidth() * ((float) col + 0.5f) / (float) kColumns;
            before.lineTo(x, yForLevel(levelBefore[(size_t) col]));
            if (col == 0)
                after.startNewSubPath(x, yForLevel(levelAfter[(size_t) col]));
            else
                after.lineTo(x, yForLevel(levelAfter[(size_t) col]));
        }
        before.lineTo(area.getRight(), area.getBottom());
        before.closeSubPath();
        g.setColour(inkDim.withAlpha(0.16f));
        g.fillPath(before);
        if (on)
        {
            g.setColour(accent.withAlpha(0.45f));
            g.strokePath(after, PathStrokeType(1.2f));
        }
    }

    // the curve the bands make together
    Path curve;
    for (int i = 0; i <= 216; ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / 216.0f;
        const float y = yForDb((float) eqResponseDb(bands, outputDb, hzForX(x), kGraphRate));
        if (i == 0)
            curve.startNewSubPath(x, y);
        else
            curve.lineTo(x, y);
    }
    {
        Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(area.toNearestInt());
        g.setColour(on ? accent : inkDim.withAlpha(0.7f));
        g.strokePath(curve, PathStrokeType(on ? 2.0f : 1.4f));
    }

    // one point per band
    for (int b = 0; b < kNumEqBands; ++b)
    {
        const auto& band = bands[(size_t) b];
        const auto p = bandPoint(band);
        const float r = 7.0f;
        g.setColour(band.on ? (on ? accent : accent.withAlpha(0.5f)) : Colour(0xff3a3a40));
        g.fillEllipse(p.x - r, p.y - r, r * 2.0f, r * 2.0f);
        if (b == selected)
        {
            g.setColour(ink);
            g.drawEllipse(p.x - r - 2.0f, p.y - r - 2.0f, (r + 2.0f) * 2.0f, (r + 2.0f) * 2.0f, 1.5f);
        }
        g.setColour(band.on ? Colour(0xff17171a) : inkDim);
        g.setFont(bold(9.5f));
        g.drawText(String(b + 1), Rectangle<float>(p.x - r, p.y - r, r * 2.0f, r * 2.0f), Justification::centred);
    }

    // the knobs' names and values, to the right of each knob
    const auto& sel = bands[(size_t) selected];
    const String freqText = sel.freq >= 1000.0f ? String(sel.freq / 1000.0f, 2) + " kHz" : String(sel.freq, 0) + " Hz";
    const std::array<std::pair<String, String>, 4> entries { { { "FREQ " + String(selected + 1), freqText },
                                                               { "GAIN", eqTypeHasGain(sel.type) ? String(sel.gainDb, 1) + " dB" : String("-") },
                                                               { "Q", String(sel.q, 2) },
                                                               { "OUTPUT", String(outputDb, 1) + " dB" } } };
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const int x = 52 + (int) i * 112;
        g.setColour(ink);
        g.setFont(bold(10.5f));
        g.drawText(entries[i].first, x, kKnobY + 4, 64, 13, Justification::left);
        g.setColour(inkDim);
        g.setFont(plain(10.0f));
        g.drawText(entries[i].second, x, kKnobY + 18, 64, 12, Justification::left);
    }
}
