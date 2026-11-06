#include "BeatDog.h"

#include "BeatDogMesh.h"
#include "BinaryData.h"
#include "PluginEditor.h"
#include "UiSettings.h"
#include "dsp/Mapping.h"
#include "dsp/Scope.h"

#include <array>
#include <cmath>

using namespace juce;
using namespace pedalstyle;

namespace
{

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

// How far forward the head nods on the curve rendered in Blender (its angles are the snout's pitch, up positive: the
// head is pulled back, snout up, at kHeadAngle[0]).
float headForward()
{
    static const float forward = [] {
        float a = beatdog::kHeadAngle[0];
        for (int i = 0; i < beatdog::kTableSize; ++i)
            a = jmin(a, beatdog::kHeadAngle[i]);
        return a;
    }();
    return forward;
}

// The headbang, from one picked beat to the next: on the beat the head, pulled back, nods forward, then pulls back
// smoothly (eased out of the nod and into the back) in time for the next. SMOOTH sets how much quicker the nod is:
// all the way left it takes kNod of a beat, speeding up all the way into the hit, which carries the head a little
// further forward; all the way right it takes half the time between the beats and eases like the pull back, so the
// two are the same.
constexpr float kNod = 0.15f, kBounce = 0.14f; // in beats, whatever the gap between picked beats

float nodTime(float gap, float smooth) { return kNod + (0.5f * gap - kNod) * smooth; }

float headbangAngle(float since, float gap, float smooth)
{
    const float back = beatdog::kHeadAngle[0], forward = headForward();
    if (gap <= 0.0f)
        return back;
    const float nod = nodTime(gap, smooth);
    if (since < nod)
    {
        const float u = since / nod;
        const float eased = 0.5f - 0.5f * std::cos(MathConstants<float>::pi * u);
        return back + (forward - back) * (u * u * u + (eased - u * u * u) * smooth);
    }
    const float u = (since - nod) / (gap - nod);
    float a = forward + (back - forward) * (0.5f - 0.5f * std::cos(MathConstants<float>::pi * u));
    if (since - nod < kBounce)
        a -= degreesToRadians(5.0f) * (1.0f - smooth) * std::sin(MathConstants<float>::pi * (since - nod) / kBounce);
    return a;
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
    liveButton->toggle = true;
    auto* button = liveButton.get();
    button->setTooltip("LIVE 3D: draw the dog in real time from its 3D mesh instead of the pre-rendered frames: smoother, at the "
                       "frame rate chosen below, and uses a little more CPU. Off: the pre-rendered animation (the lightest).");
    button->onClick = [this] { setLive(!live); };
    addChildComponent(*button); // LIVE 3D, the frame rate, SMOOTH, SWAY and the beats are in the right-click menu

    scopeButton = std::make_unique<PedalButton>("SCOPE");
    scopeButton->toggle = true;
    scopeButton->setTooltip("SCOPE: the screen shows the last two beats of sound instead of the dog: your clean input on top (IN), "
                            "what comes out below (OUT), the beats as lines and a strip in the colour of each step as it played. "
                            "See the stutter repeat, the steps mute, the crush and the tremolo cut.");
    scopeButton->onClick = [this] { setScope(!scope); };
    addAndMakeVisible(*scopeButton);

    fpsBox.addItem("60 FPS", 60);
    fpsBox.addItem("30 FPS", 30);
    fpsBox.addItem("15 FPS", 15);
    fpsBox.setLookAndFeel(&laf);
    fpsBox.setTooltip("FRAME RATE of the live dog: 60 is the smoothest, 30 or 15 use less CPU.");
    fpsBox.onChange = [this] { setFrameRate(fpsBox.getSelectedId()); };
    addChildComponent(fpsBox);

    setUpKnob(smoothKnob, laf, 0.0);
    smoothKnob.setTooltip("SMOOTH: how much quicker the nod forward is than the smooth pull back. Left, the headbang: a "
                          "sudden nod on each picked beat. Right, an even bob, forward and back alike. Double-click: the headbang.");
    smoothKnob.onValueChange = [this] { setSmoothness((float) smoothKnob.getValue()); };
    smoothKnob.onDragEnd = [this] { saveSettings(); };
    addChildComponent(smoothKnob);
    setUpKnob(swayKnob, laf, 0.5);
    swayKnob.setTooltip("SWAY: floats the head from side to side in time, landing on one side at a picked beat and on the "
                        "other at the next. Left: no sway. Double-click: the middle.");
    swayKnob.onValueChange = [this] { setSway((float) swayKnob.getValue()); };
    swayKnob.onDragEnd = [this] { saveSettings(); };
    addChildComponent(swayKnob);

    fps = jlimit(15, 60, uisettings::beatDogFps());
    beatMask = uisettings::beatDogBeats() & ((1 << kMaxBeats) - 1);
    smoothness = jlimit(0.0f, 1.0f, uisettings::beatDogSmooth());
    sway = jlimit(0.0f, 1.0f, uisettings::beatDogSway());
    smoothKnob.setValue(smoothness, dontSendNotification);
    swayKnob.setValue(sway, dontSendNotification);
    shown = liveShown = motionAt(0.0f, 0, barSteps);
    live = uisettings::beatDogLive();
    scope = uisettings::beatDogScope();
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
    updateTimer();
    repaint();
}

void BeatDog::setScope(bool on)
{
    if (on != uisettings::beatDogScope())
        uisettings::setBeatDogScope(on);
    scope = on;
    updateControls();
    updateTimer();
    repaint();
}

// The timer redraws the live dog or the scope; the pre-rendered dog follows the editor's own updates.
void BeatDog::updateTimer()
{
    if (live || scope)
        startTimerHz(scope ? jmin(fps, 30) : fps);
    else
        stopTimer();
}

void BeatDog::setFrameRate(int newFps)
{
    newFps = newFps >= 60 ? 60 : (newFps >= 30 ? 30 : 15);
    if (newFps != uisettings::beatDogFps())
        uisettings::setBeatDogFps(newFps);
    fps = newFps;
    updateControls();
    updateTimer();
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
    if (bitvibe::sameFloat(amount, smoothness))
        return;
    smoothness = amount;
    knobsChanged = true;
    if (!bitvibe::sameFloat((float) smoothKnob.getValue(), amount))
        smoothKnob.setValue(amount, dontSendNotification);
    repaint();
}

void BeatDog::setSway(float amount)
{
    amount = jlimit(0.0f, 1.0f, amount);
    if (bitvibe::sameFloat(amount, sway))
        return;
    sway = amount;
    knobsChanged = true;
    if (!bitvibe::sameFloat((float) swayKnob.getValue(), amount))
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
    if (scopeButton->lit != scope)
    {
        scopeButton->lit = scope;
        scopeButton->repaint();
    }
    fpsBox.setSelectedId(fps, dontSendNotification);
    fpsBox.setEnabled(live && !scope);
    fpsBox.setAlpha(live && !scope ? 1.0f : 0.4f);
    liveButton->setAlpha(scope ? 0.55f : 1.0f); // the dog is not on the screen
}

int BeatDog::beatsInBar() const { return jlimit(1, kMaxBeats, (barSteps + 3) / 4); }

// The headbang runs from one picked beat to the next: the head nods forward on a picked beat and pulls back by the next
// one. With none picked it waits, pulled back and still.
BeatDog::Motion BeatDog::motionAt(float phase, int beatInBar, int steps)
{
    const float barBeats = (float) jlimit(1, 32, steps) / 4.0f;
    const int n = jlimit(1, kMaxBeats, (int) std::ceil(barBeats - 0.001f));
    const int mask = beatMask & ((1 << n) - 1);
    Motion m;
    if (mask == 0)
    {
        m.gap = m.side = m.lift = 0.0f;
        return m;
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
        m.since = jlimit(0.0f, gap, elapsed);
        m.gap = gap;
        m.between = jlimit(0.0f, 0.9999f, elapsed / gap);
    }
    if (m.between < lastBetween - 0.5f) // a picked beat went by: sway back the other way
        swings = (swings + 1) & 1;
    lastBetween = m.between;
    m.side = std::cos(MathConstants<float>::pi * ((float) swings + m.between));
    m.lift = 0.5f - 0.5f * std::cos(MathConstants<float>::twoPi * m.between);
    return m;
}

// The pre-rendered frame for a motion. The frames were rendered on a curve of their own (a nod forward over the first
// 0.6 of the beat, then a pull back), so this picks the frame whose head is nearest the angle wanted, from the frames
// going the same way, so the ears flop the right way too.
int BeatDog::frameFor(const Motion& m) const
{
    const float want = headAngle(m);
    const bool nodding = m.gap > 0.0f && m.since < nodTime(m.gap, smoothness);
    const int forwardFrame = roundToInt(0.6f * (float) kFrames);
    int best = 0;
    float bestDistance = 1.0e9f;
    for (int f = nodding ? 0 : forwardFrame; f < (nodding ? forwardFrame + 1 : kFrames); ++f)
        if (const float d = std::abs(table(beatdog::kHeadAngle, (float) f / (float) kFrames) - want); d < bestDistance)
        {
            bestDistance = d;
            best = f;
        }
    return best;
}

float BeatDog::headAngle(const Motion& m) const
{
    return headbangAngle(m.since, m.gap, smoothness);
}

// The ears' flop through one headbang, from one picked beat to the next (gap beats apart), behind the head on a damped
// spring as in the Blender script. Worked out again only when the gap or SMOOTH changes.
const float* BeatDog::earsFor(float gap)
{
    static_assert(kEarSteps == beatdog::kTableSize);
    if (bitvibe::sameFloat(gap, earsGap) && bitvibe::sameFloat(smoothness, earsSmooth))
        return ears.data();
    earsGap = gap;
    earsSmooth = smoothness;
    ears.fill(0.0f);
    if (gap <= 0.0f)
        return ears.data();
    constexpr int steps = kEarSteps * 5;
    constexpr float k = 1400.0f, damping = 26.0f; // per beat, as in Tools/beatdog/build_beat_dog.py
    const float dt = gap / (float) steps;
    float theta = headbangAngle(0.0f, gap, smoothness), vel = 0.0f;
    for (int cycle = 0; cycle < 6; ++cycle) // run until the ears have settled into their loop
        for (int s = 0; s < steps; ++s)
        {
            const float head = headbangAngle((float) s * dt, gap, smoothness);
            vel += (k * (head - theta) - damping * vel) * dt;
            theta += vel * dt;
            if (cycle == 5 && s % 5 == 0)
                ears[(size_t) (s / 5)] = (theta - head) * 1.6f;
        }
    return ears.data();
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
    if (scope)
    {
        if (isShowing())
            repaint(screenArea());
        return;
    }
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
    const float earFlop = table(earsFor(motion.gap), motion.between);
    const float side = kEarSide * std::sin(MathConstants<float>::twoPi * motion.between - 0.5f);
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
                v = v + (earBase[(size_t) e] + earRot[(size_t) e](rest - earBase[(size_t) e])) * kWeights[i][1 + e];
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
    g.setColour(theme.screen.withAlpha(0.13f));
    g.fillPath(wide);
    g.setColour(theme.screen.withAlpha(0.35f));
    g.fillPath(mid);
    g.setColour(theme.screen.interpolatedWith(Colours::white, 0.25f));
    g.fillPath(core);
}

// The scope: the last kScopeBeats beats, right up to now at the right edge. IN on top, OUT below, each bucket drawn
// from its lowest to its highest sample (on a square-root scale, so a quiet guitar still shows), the beats as lines,
// and under OUT a strip in the colour of the step that was playing.
void BeatDog::paintScope(Graphics& g, Rectangle<int> screen) const
{
    using bitvibe::ScopeFeed;
    const auto area = screen.toFloat();
    const Colour colour = theme.screen;
    g.setColour(Colours::black);
    g.fillRect(area);
    const float stripH = 5.0f;
    const auto inLane = area.withHeight((area.getHeight() - stripH) * 0.5f).reduced(0.0f, 3.0f);
    const auto outLane = area.withTrimmedTop((area.getHeight() - stripH) * 0.5f).withTrimmedBottom(stripH).reduced(0.0f, 3.0f);
    const auto strip = area.withTop(area.getBottom() - stripH);

    const int buckets = kScopeBeats * ScopeFeed::kBucketsPerBeat;
    const auto newest = scopeFeed != nullptr ? scopeFeed->newest() : (std::int64_t) -1;
    const float step = area.getWidth() / (float) buckets;
    auto shape = [](float v) { return (v < 0.0f ? -1.0f : 1.0f) * std::sqrt(std::min(1.0f, std::abs(v))); };

    // the beats
    g.setColour(colour.withAlpha(0.18f));
    if (newest >= 0)
        for (int i = 0; i < buckets; ++i)
        {
            const auto index = newest - (buckets - 1) + i;
            if (index % ScopeFeed::kBucketsPerBeat == 0)
                g.fillRect(area.getX() + (float) i * step, area.getY(), 1.0f, area.getHeight());
        }
    g.setColour(colour.withAlpha(0.12f));
    g.fillRect(area.getX(), inLane.getCentreY(), area.getWidth(), 1.0f);
    g.fillRect(area.getX(), outLane.getCentreY(), area.getWidth(), 1.0f);

    if (newest >= 0)
        for (int i = 0; i < buckets; ++i)
        {
            const auto index = newest - (buckets - 1) + i;
            if (!scopeFeed->has(index))
                continue;
            const auto b = scopeFeed->get(index);
            const float x = area.getX() + (float) i * step, w = std::max(1.0f, step);
            auto column = [&](Rectangle<float> lane, float lo, float hi)
            {
                const float half = lane.getHeight() * 0.5f;
                const float top = lane.getCentreY() - shape(hi) * half, bottom = lane.getCentreY() - shape(lo) * half;
                g.fillRect(x, top, w, std::max(1.0f, bottom - top));
            };
            g.setColour(colour.withAlpha(0.45f));
            column(inLane, b.inLo, b.inHi);
            g.setColour(colour.interpolatedWith(Colours::white, 0.2f));
            column(outLane, b.outLo, b.outHi);
            if (b.step != ScopeFeed::kNoStep && stepColour)
            {
                g.setColour(stepColour(b.step));
                g.fillRect(x, strip.getY() + 1.0f, w, strip.getHeight() - 1.0f);
            }
        }

    g.setColour(colour.withAlpha(0.7f));
    g.setFont(bold(9.0f));
    g.drawText("IN", area.withHeight(14.0f).reduced(4.0f, 0.0f).toNearestInt(), Justification::topLeft);
    g.drawText("OUT", outLane.withHeight(14.0f).reduced(4.0f, 0.0f).toNearestInt(), Justification::topLeft);
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
    scopeButton->setBounds(screen.getRight() + 12, 74, 92, 22);
    liveButton->setBounds(screen.getRight() + 12, 104, 92, 22);
    fpsBox.setBounds(screen.getRight() + 12, 130, 92, 22);
    smoothKnob.setBounds(30, 90, 32, 32);
    swayKnob.setBounds(94, 90, 32, 32);
    if (live)
        drawLiveFrame(liveShown);
}

void BeatDog::mouseDown(const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showMenu();
}

// The settings that used to be knobs and buttons on the panel: the beats the head hits, SWAY, SMOOTH, LIVE 3D and its frame rate.
void BeatDog::showMenu()
{
    PopupMenu menu;
    menu.addSectionHeader("HEADBANG ON");
    for (int b = 0; b < beatsInBar(); ++b)
        menu.addItem(1 + b, "Beat " + String(b + 1), true, ((beatMask >> b) & 1) != 0);
    menu.addSeparator();
    PopupMenu swayMenu, smoothMenu, fpsMenu;
    static const char* swayNames[3] { "Off", "Half", "Full" };
    static const char* smoothNames[3] { "Headbang (quick nod)", "Halfway", "Even bob" };
    for (int i = 0; i < 3; ++i)
    {
        swayMenu.addItem(10 + i, swayNames[i], true, std::abs(sway - 0.5f * (float) i) < 0.25f);
        smoothMenu.addItem(20 + i, smoothNames[i], true, std::abs(smoothness - 0.5f * (float) i) < 0.25f);
    }
    menu.addSubMenu("Sway", swayMenu);
    menu.addSubMenu("Smooth", smoothMenu);
    menu.addSeparator();
    menu.addItem(30, "LIVE 3D (drawn in real time)", true, live);
    static const int rates[3] { 60, 30, 15 };
    for (int i = 0; i < 3; ++i)
        fpsMenu.addItem(40 + i, String(rates[i]) + " FPS", true, fps == rates[i]);
    menu.addSubMenu("Frame rate", fpsMenu, live);
    Component::SafePointer<BeatDog> safe(this);
    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(this), [safe](int result)
    {
        if (safe == nullptr || result == 0)
            return;
        if (result >= 1 && result <= 6)
            safe->setBeatMask(safe->beatMask ^ (1 << (result - 1)));
        else if (result >= 10 && result <= 12)
            safe->setSway(0.5f * (float) (result - 10));
        else if (result >= 20 && result <= 22)
            safe->setSmoothness(0.5f * (float) (result - 20));
        else if (result == 30)
            safe->setLive(!safe->live);
        else if (result >= 40 && result <= 42)
            safe->setFrameRate(rates[result - 40]);
        if (result >= 10 && result <= 22)
            safe->saveSettings();
    });
}

String BeatDog::getTooltip()
{
    return "DOG5: headbangs in time with the tempo (or the host's, with SYNC on). SCOPE shows the sound instead. Right-click "
           "for the beats it hits, SWAY, SMOOTH, LIVE 3D and the frame rate.";
}

// The frames are drawn in green, whiter where the lines are brightest; for another screen colour each pixel keeps its
// brightness and its whiteness.
const Image& BeatDog::framesForTheme()
{
    static const Colour green(0xff19ff3a);
    if (theme.screen == green || !sheet.isValid())
        return sheet;
    if (!tintedSheet.isValid() || tintedFor != theme.screen)
    {
        tintedFor = theme.screen;
        tintedSheet = Image(Image::RGB, sheet.getWidth(), sheet.getHeight(), false, SoftwareImageType());
        const Image::BitmapData from(sheet, Image::BitmapData::readOnly);
        Image::BitmapData to(tintedSheet, Image::BitmapData::writeOnly);
        for (int y = 0; y < sheet.getHeight(); ++y)
            for (int x = 0; x < sheet.getWidth(); ++x)
            {
                const Colour p = from.getPixelColour(x, y);
                const float level = p.getFloatGreen();
                const float white = jlimit(0.0f, 1.0f, (jmin(p.getFloatRed(), p.getFloatBlue()) / jmax(level, 0.02f) - 0.1f) / 0.9f);
                to.setPixelColour(x, y, tintedFor.interpolatedWith(Colours::white, white).withMultipliedBrightness(level));
            }
    }
    return tintedSheet;
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
    g.fillAll(theme.background); // what is behind the rounded corners
    panelFrame(g, bounds);
    drawSectionTitle(g, 14, 4, "DOG5");

    // the screen: the live frame, or the pre-rendered frame for this moment, swayed as a picture
    const auto screen = screenArea();
    g.setColour(Colours::black);
    g.fillRoundedRectangle(screen.toFloat().expanded(3.0f), 6.0f);
    g.setImageResamplingQuality(Graphics::mediumResamplingQuality);
    if (scope)
        paintScope(g, screen);
    else if (live && liveImage.isValid())
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
        g.drawImage(framesForTheme(), screen.getX(), screen.getY(), screen.getWidth(), screen.getHeight(), sx, sy, kTile, kTile);
    }
    if (scanlines.isValid())
        g.drawImageAt(scanlines, screen.getX(), screen.getY());
    g.setColour(theme.screenEdge);
    g.drawRoundedRectangle(screen.toFloat().expanded(3.0f), 6.0f, 1.5f);

    // the beat of the bar (the tempo is on the pedal)
    const auto mono = [](float size) { return Font(FontOptions(Font::getDefaultMonospacedFontName(), size, Font::bold)); };
    g.setColour(inkDim);
    g.setFont(bold(9.5f));
    g.drawText("BEAT", 24, 24, 60, 12, Justification::centredLeft);
    g.setColour(accent);
    g.setFont(mono(34.0f));
    g.drawText(String(beat + 1), 22, 34, 30, 38, Justification::centredLeft);
    g.setColour(inkDim);
    g.setFont(mono(14.0f));
    g.drawText("/ " + String(beatsInBar()), 50, 48, 50, 18, Justification::centredLeft);

    // the beats the head hits, as small lights (the menu picks them)
    for (int b = 0; b < beatsInBar(); ++b)
    {
        const auto r = beatButton(b).toFloat().withSizeKeepingCentre(10.0f, 10.0f);
        const bool picked = ((beatMask >> b) & 1) != 0, playing = b == beat;
        g.setColour(picked ? accent.withAlpha(playing ? 1.0f : 0.55f) : theme.outline);
        g.fillEllipse(r);
    }
    g.setColour(inkDim);
    g.setFont(plain(9.5f));
    g.drawText("right-click for more", scopeButton->getX(), scopeButton->getBottom() + 6, 120, 12, Justification::left);
}
