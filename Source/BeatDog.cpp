#include "BeatDog.h"

#include "BeatDogMesh.h"
#include "BinaryData.h"
#include "PluginEditor.h"
#include "UiSettings.h"

#include <array>
#include <cmath>

using namespace juce;
using namespace pedalstyle;

namespace
{
const Colour kGreen(0xff19ff3a);

struct V3
{
    float x, y, z;
    V3 operator+(V3 o) const { return { x + o.x, y + o.y, z + o.z }; }
    V3 operator-(V3 o) const { return { x - o.x, y - o.y, z - o.z }; }
    V3 operator*(float s) const { return { x * s, y * s, z * s }; }
};

V3 fromArray(const float* a) { return { a[0], a[1], a[2] }; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }

// A rotation about an axis through the origin (Rodrigues), kept as a 3x3 matrix so it is built once per frame.
struct Rot
{
    float m[3][3];
    static Rot about(V3 axis, float angle)
    {
        const float c = std::cos(angle), s = std::sin(angle), t = 1.0f - c;
        const float x = axis.x, y = axis.y, z = axis.z;
        return { { { t * x * x + c, t * x * y - s * z, t * x * z + s * y },
                   { t * x * y + s * z, t * y * y + c, t * y * z - s * x },
                   { t * x * z - s * y, t * y * z + s * x, t * z * z + c } } };
    }
    V3 operator()(V3 v) const
    {
        return { m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                 m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z };
    }
    Rot operator*(const Rot& o) const
    {
        Rot r {};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                r.m[i][j] = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] + m[i][2] * o.m[2][j];
        return r;
    }
};

float table(const float* t, float phase)
{
    const float x = (phase - std::floor(phase)) * (float) beatdog::kTableSize;
    const int i = (int) x % beatdog::kTableSize, j = (i + 1) % beatdog::kTableSize;
    return t[i] + (t[j] - t[i]) * (x - std::floor(x));
}
} // namespace

BeatDog::BeatDog(PedalLookAndFeel& lookAndFeel) : laf(lookAndFeel)
{
    setOpaque(true);
    // decoded once and shared: the image cache keeps it for every editor that is open
    sheet = ImageCache::getFromMemory(BinaryData::beat_dog_jpg, BinaryData::beat_dog_jpgSize);
    setTooltip("BEAT DOG: headbangs in time with the tempo (or the host's, with SYNC on): the head hits bottom on every beat. "
               "The lights count the beats of the bar.");

    liveButton = std::make_unique<PedalButton>("LIVE 3D"); // lit while the live version is on
    auto* button = liveButton.get();
    button->setTooltip("LIVE 3D: draw the dog in real time from its 3D mesh instead of the pre-rendered frames: smoother, at the "
                       "frame rate chosen below, and uses a little more CPU. Off: the pre-rendered animation (the lightest).");
    button->onClick = [this] { setLive(!live); };
    addAndMakeVisible(*button);

    fpsBox.addItem("60 FPS", 60);
    fpsBox.addItem("30 FPS", 30);
    fpsBox.addItem("15 FPS", 15);
    fpsBox.setLookAndFeel(&laf);
    fpsBox.setTooltip("FRAME RATE of the live dog: 60 is the smoothest, 30 or 15 use less CPU.");
    fpsBox.onChange = [this] { setFrameRate(fpsBox.getSelectedId()); };
    addAndMakeVisible(fpsBox);

    fps = jlimit(15, 60, uisettings::beatDogFps());
    live = uisettings::beatDogLive();
    setLive(live);
}

BeatDog::~BeatDog()
{
    stopTimer();
    fpsBox.setLookAndFeel(nullptr);
}

void BeatDog::setLive(bool on)
{
    if (on != uisettings::beatDogLive())
        uisettings::setBeatDogLive(on);
    live = on;
    updateControls();
    if (live)
        startTimerHz(fps);
    else
        stopTimer();
    repaint();
}

void BeatDog::setFrameRate(int newFps)
{
    newFps = newFps >= 60 ? 60 : (newFps >= 30 ? 30 : 15);
    if (newFps != uisettings::beatDogFps())
        uisettings::setBeatDogFps(newFps);
    fps = newFps;
    updateControls();
    if (live)
        startTimerHz(fps);
}

void BeatDog::updateControls()
{
    auto* button = liveButton.get();
    if (button->lit != live)
    {
        button->lit = live;
        button->repaint();
    }
    fpsBox.setSelectedId(fps, dontSendNotification);
    fpsBox.setEnabled(live);
    fpsBox.setAlpha(live ? 1.0f : 0.4f);
}

void BeatDog::setBeat(float phase, int beatInBar, float bpm)
{
    const int f = jlimit(0, kFrames - 1, (int) std::floor(jlimit(0.0f, 0.9999f, phase) * (float) kFrames));
    const int b = jlimit(0, 3, beatInBar);
    const int t = roundToInt(bpm * 10.0f);
    if (f != frame)
    {
        frame = f;
        if (!live) // the live version draws at its own rate
            repaint(screenArea());
    }
    if (b != beat || t != tempoTenths)
    {
        beat = b;
        tempoTenths = t;
        repaint(Rectangle<int>(0, 0, screenArea().getX(), getHeight()));             // the readout
        repaint(Rectangle<int>(screenArea().getRight(), 0, getWidth(), getHeight())); // and the lights
    }
}

void BeatDog::timerCallback()
{
    if (!isShowing() || !beatSource)
        return;
    const auto [phase, beatInBar] = beatSource();
    ignoreUnused(beatInBar);
    renderLiveFrame(phase);
    repaint(screenArea());
}

// One frame of the live dog: pose the head for this moment of the beat (the head's nod and dip, each ear's flop and
// wobble, blended by the skin weights), project it with the Blender camera, and draw the edges of the faces that look
// at the camera, three times over for the glow.
void BeatDog::renderLiveFrame(float phase)
{
    using namespace beatdog;
    livePhase = phase;
    const auto screen = screenArea();
    if (screen.isEmpty())
        return;
    float scale = Component::getApproximateScaleFactorForComponent(this);
    if (const auto* display = Desktop::getInstance().getDisplays().getPrimaryDisplay())
        scale *= (float) jmax(1.0, display->scale);
    scale = jlimit(1.0f, 4.0f, scale);
    const int size = roundToInt((float) screen.getWidth() * scale);
    if (liveImage.getWidth() != size)
        liveImage = Image(Image::RGB, size, size, false, SoftwareImageType());

    // the pose
    const float head = table(kHeadAngle, phase), earFlop = table(kEarAngle, phase);
    const float side = kEarSide * std::sin(MathConstants<float>::twoPi * phase - 0.5f);
    const V3 pitchAxis = fromArray(kPitchAxis), snout = fromArray(kSnout), neck = fromArray(kNeck);
    const Rot headRot = Rot::about(pitchAxis, head);
    const V3 dip { 0.0f, 0.0f, -kDip * jmax(0.0f, head) / kDown };
    const std::array<Rot, 2> earRot { Rot::about(pitchAxis, earFlop) * Rot::about(snout, side),
                                      Rot::about(pitchAxis, earFlop) * Rot::about(snout, -side) };
    const std::array<V3, 2> earBase { fromArray(kEarBase[0]), fromArray(kEarBase[1]) };

    // pose and project every point (camera space for facing, pixels for drawing)
    std::array<V3, kNumVerts> cam {};
    std::array<Point<float>, kNumVerts> px {};
    const float half = (float) size * 0.5f, f = kFocal * (float) size;
    for (int i = 0; i < kNumVerts; ++i)
    {
        const V3 rest = fromArray(kVerts[i]);
        V3 v = rest * kWeights[i][0];
        for (int e = 0; e < 2; ++e)
            if (kWeights[i][1 + e] > 0.0f)
                v = v + (earBase[e] + earRot[(size_t) e](rest - earBase[e])) * kWeights[i][1 + e];
        const V3 w = neck + headRot(v - neck) + dip;
        const V3 c { kView[0][0] * w.x + kView[0][1] * w.y + kView[0][2] * w.z + kView[0][3],
                     kView[1][0] * w.x + kView[1][1] * w.y + kView[1][2] * w.z + kView[1][3],
                     kView[2][0] * w.x + kView[2][1] * w.y + kView[2][2] * w.z + kView[2][3] };
        cam[(size_t) i] = c;
        const float depth = jmax(1.0e-4f, -c.z);
        px[(size_t) i] = { half + f * c.x / depth, half - f * c.y / depth };
    }
    std::array<bool, kNumFaces> facing {};
    for (int i = 0; i < kNumFaces; ++i)
    {
        const V3 a = cam[(size_t) kFaces[i][0]], b = cam[(size_t) kFaces[i][1]], c = cam[(size_t) kFaces[i][2]];
        facing[(size_t) i] = dot(cross(b - a, c - a), a) < 0.0f; // its outside looks toward the camera
    }

    // the visible edges, as one path per glow layer
    Path wide, mid, core;
    const float unit = (float) size / 224.0f; // the pre-rendered frames' scale
    for (int i = 0; i < kNumEdges; ++i)
    {
        const int fa = kEdges[i][2], fb = kEdges[i][3];
        if (!((fa >= 0 && facing[(size_t) fa]) || (fb >= 0 && facing[(size_t) fb])))
            continue;
        const Line<float> line(px[(size_t) kEdges[i][0]], px[(size_t) kEdges[i][1]]);
        wide.addLineSegment(line, 5.0f * unit);
        mid.addLineSegment(line, 2.6f * unit);
        core.addLineSegment(line, 1.3f * unit);
    }
    Graphics g(liveImage);
    g.fillAll(Colours::black);
    g.setColour(kGreen.withAlpha(0.13f));
    g.fillPath(wide);
    g.setColour(kGreen.withAlpha(0.35f));
    g.fillPath(mid);
    g.setColour(kGreen.interpolatedWith(Colours::white, 0.25f));
    g.fillPath(core);
}

Rectangle<int> BeatDog::screenArea() const
{
    const int side = getHeight() - 30;
    return { (getWidth() - side) / 2, 22, side, side };
}

void BeatDog::resized()
{
    buildScanlines();
    const auto screen = screenArea();
    liveButton->setBounds(screen.getRight() + 12, 104, 92, 22);
    fpsBox.setBounds(screen.getRight() + 12, 130, 92, 22);
    if (live)
        renderLiveFrame(livePhase);
}

void BeatDog::buildScanlines()
{
    const auto area = screenArea();
    if (area.isEmpty())
        return;
    scanlines = Image(Image::ARGB, area.getWidth(), area.getHeight(), true, SoftwareImageType());
    Graphics g(scanlines);
    g.setColour(Colours::black.withAlpha(0.22f));
    for (int y = 0; y < area.getHeight(); y += 3)
        g.fillRect(0, y, area.getWidth(), 1);
    ColourGradient vignette(Colours::transparentBlack, area.getWidth() * 0.5f, area.getHeight() * 0.5f, Colours::black.withAlpha(0.55f),
                            0.0f, 0.0f, true);
    g.setGradientFill(vignette);
    g.fillRect(0, 0, area.getWidth(), area.getHeight());
}

void BeatDog::paint(Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.fillAll(Colour(0xff111113)); // what is behind the rounded corners
    g.setColour(panel);
    g.fillRoundedRectangle(bounds, 10.0f);
    g.setColour(panelEdge);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 10.0f, 1.0f);
    g.setColour(inkDim);
    g.setFont(bold(10.0f));
    g.drawText("BEAT DOG", 14, 4, 100, 12, Justification::left);

    // the screen: the live frame, or the pre-rendered frame for this moment of the beat
    const auto screen = screenArea();
    g.setColour(Colours::black);
    g.fillRoundedRectangle(screen.toFloat().expanded(3.0f), 6.0f);
    if (live && liveImage.isValid())
    {
        g.setImageResamplingQuality(Graphics::mediumResamplingQuality);
        g.drawImage(liveImage, screen.toFloat());
    }
    else if (sheet.isValid())
    {
        const int sx = (frame % kColumns) * kTile, sy = (frame / kColumns) * kTile;
        g.setImageResamplingQuality(Graphics::mediumResamplingQuality);
        g.drawImage(sheet, screen.getX(), screen.getY(), screen.getWidth(), screen.getHeight(), sx, sy, kTile, kTile);
    }
    if (scanlines.isValid())
        g.drawImageAt(scanlines, screen.getX(), screen.getY());
    g.setColour(Colour(0xff1f3a26));
    g.drawRoundedRectangle(screen.toFloat().expanded(3.0f), 6.0f, 1.5f);

    // the tempo and the beat, in the screen's green
    const Rectangle<int> readout(24, 30, screen.getX() - 40, getHeight() - 44);
    g.setColour(kGreen);
    g.setFont(Font(FontOptions(Font::getDefaultMonospacedFontName(), 30.0f, Font::bold)));
    g.drawText(String(tempoTenths / 10.0, 1), readout.withHeight(36), Justification::centredLeft);
    g.setColour(kGreen.withAlpha(0.6f));
    g.setFont(Font(FontOptions(Font::getDefaultMonospacedFontName(), 12.0f, Font::bold)));
    g.drawText("BPM", readout.withY(readout.getY() + 36).withHeight(16), Justification::centredLeft);
    g.drawText("BEAT " + String(beat + 1) + " / 4", readout.withY(readout.getY() + 70).withHeight(16), Justification::centredLeft);

    // the beats of the bar
    for (int i = 0; i < 4; ++i)
    {
        const Point<float> c((float) getWidth() - 24.0f, 34.0f + (float) i * 18.0f);
        const bool lit = i == beat;
        if (lit)
        {
            g.setColour(kGreen.withAlpha(0.25f));
            g.fillEllipse(c.x - 8.0f, c.y - 8.0f, 16.0f, 16.0f);
        }
        g.setColour(lit ? kGreen : Colour(0xff1d2b20));
        g.fillEllipse(c.x - 4.0f, c.y - 4.0f, 8.0f, 8.0f);
    }
}
