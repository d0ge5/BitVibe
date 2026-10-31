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

// The highest the head goes on the headbang curve (the bottom is where it starts, on the beat).
float headTop()
{
    static const float top = [] {
        float a = beatdog::kHeadAngle[0];
        for (int i = 0; i < beatdog::kTableSize; ++i)
            a = jmin(a, beatdog::kHeadAngle[i]);
        return a;
    }();
    return top;
}

// The headbang, from one picked beat (0) to the next (1): the head starts at the top and drops hard the moment the beat
// comes, hits bottom kDrop of a beat later, the impact carries it a little further down, then it rises smoothly back to
// the top, eased off the bottom and into the top, ready for the next beat. The ears follow on a spring, as in the
// Blender script.
constexpr float kDrop = 0.15f, kBounce = 0.14f; // in beats: kept whatever the gap between picked beats

float headbangAngle(float p)
{
    const float down = beatdog::kHeadAngle[0], up = headTop();
    if (p < kDrop)
    {
        const float u = p / kDrop;
        return up + (down - up) * u * u * u; // accelerating all the way into the impact
    }
    const float u = (p - kDrop) / (1.0f - kDrop);
    float a = down + (up - down) * u * u * (3.0f - 2.0f * u); // eased off the bottom and into the top
    if (p < kDrop + kBounce)
        a += degreesToRadians(5.0f) * std::sin(MathConstants<float>::pi * (p - kDrop) / kBounce);
    return a;
}

struct Curve
{
    std::array<float, beatdog::kTableSize> head {}, ears {};
};

const Curve& headbangCurve()
{
    static const Curve curve = [] {
        Curve c;
        constexpr int n = beatdog::kTableSize, steps = n * 5;
        constexpr float k = 1400.0f, damping = 26.0f; // per beat, as in Tools/beatdog/build_beat_dog.py
        for (int i = 0; i < n; ++i)
            c.head[(size_t) i] = headbangAngle((float) i / (float) n);
        float theta = c.head[0], vel = 0.0f;
        for (int cycle = 0; cycle < 6; ++cycle) // run until the ears have settled into their loop
            for (int s = 0; s < steps; ++s)
            {
                const float head = headbangAngle((float) s / (float) steps), dt = 1.0f / (float) steps;
                vel += (k * (head - theta) - damping * vel) * dt;
                theta += vel * dt;
                if (cycle == 5 && s % 5 == 0)
                    c.ears[(size_t) (s / 5)] = (theta - head) * 1.6f;
            }
        return c;
    }();
    return curve;
}

// How far the sway goes at full SWAY: the lean (radians) and the slide and the lift (as parts of the screen).
constexpr float kSwayLean = 0.13f, kSwaySlide = 0.06f, kSwayLift = 0.03f;

void setUpKnob(Slider& s, PedalLookAndFeel& laf, double defaultValue)
{
    s.setSliderStyle(Slider::RotaryVerticalDrag);
    s.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
    s.setRotaryParameters(MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
    s.setMouseDragSensitivity(180);
    s.setRange(0.0, 1.0);
    s.setDoubleClickReturnValue(true, defaultValue);
    s.getProperties().set("ticks", 7);
    s.setLookAndFeel(&laf);
}
} // namespace

BeatDog::BeatDog(PedalLookAndFeel& lookAndFeel) : laf(lookAndFeel)
{
    setOpaque(true);
    // decoded once and shared: the image cache keeps it for every editor that is open
    sheet = ImageCache::getFromMemory(BinaryData::beat_dog_jpg, BinaryData::beat_dog_jpgSize);

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

    setUpKnob(smoothKnob, laf, 0.0);
    smoothKnob.setTooltip("SMOOTH (LIVE 3D): left, the headbang: a hard drop on each picked beat and a smooth rise. "
                          "Right, an even, rounded bob. Double-click: back to the headbang.");
    smoothKnob.onValueChange = [this] { setSmoothness((float) smoothKnob.getValue()); };
    smoothKnob.onDragEnd = [this] { saveSettings(); };
    addAndMakeVisible(smoothKnob);
    setUpKnob(swayKnob, laf, 0.5);
    swayKnob.setTooltip("SWAY: floats the head from side to side in time, landing on one side at a picked beat and on the "
                        "other at the next. Left: no sway. Double-click: the middle.");
    swayKnob.onValueChange = [this] { setSway((float) swayKnob.getValue()); };
    swayKnob.onDragEnd = [this] { saveSettings(); };
    addAndMakeVisible(swayKnob);

    fps = jlimit(15, 60, uisettings::beatDogFps());
    beatMask = uisettings::beatDogBeats() & ((1 << kMaxBeats) - 1);
    smoothness = jlimit(0.0f, 1.0f, uisettings::beatDogSmooth());
    sway = jlimit(0.0f, 1.0f, uisettings::beatDogSway());
    smoothKnob.setValue(smoothness, dontSendNotification);
    swayKnob.setValue(sway, dontSendNotification);
    shown = liveShown = motionAt(0.0f, 0, barSteps);
    live = uisettings::beatDogLive();
    setLive(live);
}

BeatDog::~BeatDog()
{
    stopTimer();
    saveSettings();
    fpsBox.setLookAndFeel(nullptr);
    smoothKnob.setLookAndFeel(nullptr);
    swayKnob.setLookAndFeel(nullptr);
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

void BeatDog::setBeatMask(int mask)
{
    mask &= (1 << kMaxBeats) - 1;
    if (mask == beatMask)
        return;
    beatMask = mask;
    uisettings::setBeatDogBeats(mask);
    repaint();
}

void BeatDog::setSmoothness(float amount)
{
    amount = jlimit(0.0f, 1.0f, amount);
    if (amount == smoothness)
        return;
    smoothness = amount;
    knobsChanged = true;
    if (smoothKnob.getValue() != amount)
        smoothKnob.setValue(amount, dontSendNotification);
    repaint(smoothKnob.getBounds().expanded(30, 30));
}

void BeatDog::setSway(float amount)
{
    amount = jlimit(0.0f, 1.0f, amount);
    if (amount == sway)
        return;
    sway = amount;
    knobsChanged = true;
    if (swayKnob.getValue() != amount)
        swayKnob.setValue(amount, dontSendNotification);
    repaint();
}

void BeatDog::saveSettings()
{
    if (!knobsChanged)
        return;
    knobsChanged = false;
    uisettings::setBeatDogKnobs(smoothness, sway);
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
    smoothKnob.setEnabled(live); // the pre-rendered frames have the headbang baked in
    repaint(smoothKnob.getBounds().expanded(30, 30));
}

int BeatDog::beatsInBar() const { return jlimit(1, kMaxBeats, (barSteps + 3) / 4); }

// The headbang runs from one picked beat to the next: the head drops on a picked beat, bounces and rises back to the top
// by the next one. With picked beats further apart the rise takes the extra time, so the drop and the bounce keep their
// snap; with none picked the head waits at the top, still.
BeatDog::Motion BeatDog::motionAt(float phase, int beatInBar, int steps)
{
    const float barBeats = (float) jlimit(1, 32, steps) / 4.0f;
    const int n = jlimit(1, kMaxBeats, (int) std::ceil(barBeats - 0.001f));
    const int mask = beatMask & ((1 << n) - 1);
    Motion m;
    if (mask == 0)
    {
        m.side = m.lift = 0.0f;
        return m; // the top of the curve
    }
    else
    {
        const float pos = jlimit(0.0f, barBeats - 1.0e-4f, (float) jlimit(0, n - 1, beatInBar) + jlimit(0.0f, 0.9999f, phase));
        int last = 0, next = 0;
        for (int k = 0; k < n; ++k)
            if (const int b = ((int) pos - k + n) % n; ((mask >> b) & 1) != 0)
            {
                last = b;
                break;
            }
        for (int k = 1; k <= n; ++k)
            if (const int b = (last + k) % n; ((mask >> b) & 1) != 0)
            {
                next = b;
                break;
            }
        float elapsed = pos - (float) last, gap = (float) (next - last);
        if (elapsed < 0.0f)
            elapsed += barBeats;
        if (gap <= 0.0f)
            gap += barBeats;
        constexpr float snap = kDrop + kBounce;
        if (gap <= 1.0f)
            m.bang = elapsed / gap;
        else if (elapsed < snap)
            m.bang = elapsed;
        else
            m.bang = snap + (elapsed - snap) / (gap - snap) * (1.0f - snap);
        m.bang = jlimit(0.0f, 0.9999f, m.bang);
        m.between = jlimit(0.0f, 0.9999f, elapsed / gap);
    }
    if (m.between < lastBetween - 0.5f) // a picked beat went by: sway back the other way
        swings = (swings + 1) & 1;
    lastBetween = m.between;
    m.side = std::cos(MathConstants<float>::pi * ((float) swings + m.between));
    m.lift = 0.5f - 0.5f * std::cos(MathConstants<float>::twoPi * m.between);
    return m;
}

// The pre-rendered frame for a motion. The frames were rendered on a different curve (the head springing up off the beat
// and falling back into the next, at the top 0.6 of the way through), so this picks the frame whose head is nearest the
// angle wanted, from the frames going the same way: falling while the head drops, rising (with the bounce) after.
int BeatDog::frameFor(const Motion& m) const
{
    const float want = table(headbangCurve().head.data(), m.bang);
    const bool falling = m.bang < kDrop;
    const int topFrame = roundToInt(0.6f * (float) kFrames);
    int best = 0;
    float bestDistance = 1.0e9f;
    for (int f = falling ? topFrame : 0; f < (falling ? kFrames : topFrame + 1); ++f)
        if (const float d = std::abs(table(beatdog::kHeadAngle, (float) f / (float) kFrames) - want); d < bestDistance)
        {
            bestDistance = d;
            best = f;
        }
    return best;
}

float BeatDog::headAngle(const Motion& m) const
{
    const float bang = table(headbangCurve().head.data(), m.bang);
    if (smoothness <= 0.0f)
        return bang;
    const float down = beatdog::kHeadAngle[0], up = headTop();
    const float even = up + (down - up) * (0.5f - 0.5f * std::cos(MathConstants<float>::twoPi * m.between)); // top on the beat
    return bang + (even - bang) * smoothness;
}

void BeatDog::setBeat(float phase, int beatInBar, float bpm, int steps)
{
    const int b = jlimit(0, kMaxBeats - 1, beatInBar);
    const int t = roundToInt(bpm * 10.0f);
    const int s = jlimit(1, 32, steps);
    if (!live) // the live version draws at its own rate
    {
        shown = motionAt(phase, beatInBar, s);
        const int f = frameFor(shown);
        if (f != frame || sway > 0.0f)
        {
            frame = f;
            repaint(screenArea().expanded(4));
        }
    }
    if (b != beat || t != tempoTenths || s != barSteps)
    {
        beat = b;
        tempoTenths = t;
        barSteps = s;
        repaint(Rectangle<int>(0, 0, screenArea().getX(), 70));                       // the tempo
        repaint(Rectangle<int>(screenArea().getRight(), 0, getWidth(), beatButton(0).getBottom() + 2)); // and the beats
    }
}

void BeatDog::timerCallback()
{
    if (!isShowing() || !beatSource)
        return;
    const auto position = beatSource();
    renderLiveFrame(position.phase, position.beatInBar, position.barSteps);
    repaint(screenArea());
}

void BeatDog::renderLiveFrame(float phase, int beatInBar, int steps)
{
    liveShown = motionAt(phase, beatInBar, steps);
    drawLiveFrame(liveShown);
}

// One frame of the live dog: pose the head for this moment (the head's nod and dip, each ear's flop and wobble, blended
// by the skin weights, then the sway), project it with the Blender camera, and draw the edges of the faces that look at
// the camera, three times over for the glow.
void BeatDog::drawLiveFrame(const Motion& motion)
{
    using namespace beatdog;
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
    const float head = headAngle(motion);
    const float earFlop = table(headbangCurve().ears.data(), motion.bang) * (1.0f - 0.7f * smoothness); // an even bob flops the ears less
    const float side = kEarSide * std::sin(MathConstants<float>::twoPi * motion.bang - 0.5f);
    const V3 pitchAxis = fromArray(kPitchAxis), snout = fromArray(kSnout), neck = fromArray(kNeck);
    // the sway leans the head about its snout (which points at the camera) and slides it across and up the screen
    const V3 right { kView[0][0], kView[0][1], kView[0][2] }, up { kView[1][0], kView[1][1], kView[1][2] };
    const float depth0 = -(kView[2][0] * neck.x + kView[2][1] * neck.y + kView[2][2] * neck.z + kView[2][3]);
    const float screenToWorld = depth0 / kFocal; // the width of the screen, in world units at the neck
    const Rot headRot = Rot::about(snout, -kSwayLean * sway * motion.side) * Rot::about(pitchAxis, head);
    const V3 shift = V3 { 0.0f, 0.0f, -kDip * jmax(0.0f, head) / kDown }
                     + right * (kSwaySlide * sway * motion.side * screenToWorld)
                     + up * (kSwayLift * sway * motion.lift * screenToWorld);
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
        const V3 w = neck + headRot(v - neck) + shift;
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

Rectangle<int> BeatDog::beatButton(int b) const { return { screenArea().getRight() + 12 + b * 24, 40, 20, 20 }; }

void BeatDog::resized()
{
    buildScanlines();
    const auto screen = screenArea();
    liveButton->setBounds(screen.getRight() + 12, 104, 92, 22);
    fpsBox.setBounds(screen.getRight() + 12, 130, 92, 22);
    smoothKnob.setBounds(30, 90, 32, 32);
    swayKnob.setBounds(94, 90, 32, 32);
    if (live)
        drawLiveFrame(liveShown);
}

void BeatDog::mouseDown(const MouseEvent& e)
{
    for (int b = 0; b < beatsInBar(); ++b)
        if (beatButton(b).expanded(2).contains(e.getPosition()))
            setBeatMask(beatMask ^ (1 << b));
}

String BeatDog::getTooltip()
{
    const auto mouse = getMouseXYRelative();
    for (int b = 0; b < beatsInBar(); ++b)
        if (beatButton(b).expanded(2).contains(mouse))
            return "Beat " + String(b + 1) + ": click to have the head drop on it, or not. Pick fewer beats for a slower, "
                   "heavier headbang; the sway follows the beats picked. The playing beat lights up.";
    return "BEAT DOG: headbangs in time with the tempo (or the host's, with SYNC on). Pick the beats it hits with the numbered "
           "buttons; SWAY floats it from side to side between them.";
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

    // the screen: the live frame, or the pre-rendered frame for this moment, swayed as a picture
    const auto screen = screenArea();
    g.setColour(Colours::black);
    g.fillRoundedRectangle(screen.toFloat().expanded(3.0f), 6.0f);
    g.setImageResamplingQuality(Graphics::mediumResamplingQuality);
    if (live && liveImage.isValid())
    {
        g.drawImage(liveImage, screen.toFloat());
    }
    else if (sheet.isValid())
    {
        Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(screen);
        const float w = (float) screen.getWidth();
        g.addTransform(AffineTransform::rotation(kSwayLean * sway * shown.side, (float) screen.getCentreX(), (float) screen.getBottom())
                           .translated(kSwaySlide * sway * shown.side * w, -kSwayLift * sway * shown.lift * w));
        const int sx = (frame % kColumns) * kTile, sy = (frame / kColumns) * kTile;
        g.drawImage(sheet, screen.getX(), screen.getY(), screen.getWidth(), screen.getHeight(), sx, sy, kTile, kTile);
    }
    if (scanlines.isValid())
        g.drawImageAt(scanlines, screen.getX(), screen.getY());
    g.setColour(Colour(0xff1f3a26));
    g.drawRoundedRectangle(screen.toFloat().expanded(3.0f), 6.0f, 1.5f);

    // the tempo, in the screen's green
    const auto mono = [](float size) { return Font(FontOptions(Font::getDefaultMonospacedFontName(), size, Font::bold)); };
    g.setColour(kGreen);
    g.setFont(mono(30.0f));
    g.drawText(String(tempoTenths / 10.0, 1), 24, 26, screen.getX() - 34, 36, Justification::centredLeft);
    g.setColour(kGreen.withAlpha(0.6f));
    g.setFont(mono(12.0f));
    g.drawText("BPM", 24, 60, 60, 14, Justification::centredLeft);

    // the knobs' names and settings
    for (const auto* knob : { &smoothKnob, &swayKnob })
    {
        const bool on = knob->isEnabled();
        const auto column = Rectangle<int>(64, 11).withCentre({ knob->getBounds().getCentreX(), knob->getBottom() + 8 });
        g.setColour(on ? ink : inkDim.withAlpha(0.5f));
        g.setFont(bold(10.0f));
        g.drawText(knob == &smoothKnob ? "SMOOTH" : "SWAY", column, Justification::centred);
        g.setColour(on ? inkDim : inkDim.withAlpha(0.4f));
        g.setFont(bold(9.5f));
        const String value = knob->getValue() <= 0.0 ? (knob == &smoothKnob ? "HEADBANG" : "OFF") : String(roundToInt(knob->getValue() * 100.0)) + "%";
        g.drawText(on ? value : "LIVE 3D", column.translated(0, 11), Justification::centred);
    }

    // the beats the head hits: lit green when picked, filled while playing
    g.setColour(inkDim);
    g.setFont(bold(9.0f));
    g.drawText("HEADBANG ON", beatButton(0).getX(), 24, 140, 12, Justification::left);
    for (int b = 0; b < beatsInBar(); ++b)
    {
        const auto r = beatButton(b).toFloat();
        const bool picked = ((beatMask >> b) & 1) != 0, playing = b == beat;
        g.setColour(playing ? (picked ? kGreen : Colour(0xff2a3a2e)) : Colour(0xff0b0f0c));
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(picked ? kGreen.withAlpha(playing ? 1.0f : 0.8f) : Colour(0xff2d3830));
        g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.2f);
        g.setColour(playing && picked ? Colours::black : (picked ? kGreen : Colour(0xff4a5a4e)));
        g.setFont(mono(12.0f));
        g.drawText(String(b + 1), r, Justification::centred);
    }
}
