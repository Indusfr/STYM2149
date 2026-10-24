// STYM2149 — pluginui: LFO of the focused track (sixth DAW test, item 59, D-257), a part of the Modulation window
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "pluginui/LfoPanel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <vector>

#include "pluginui/Layout.h"
#include "pluginui/MainWindow.h"

namespace stym::pluginui {

using gemui::Button;
using gemui::Font;
using gemui::MenuItem;
using gemui::Rect;
using plugin::Track;
using ymengine::Generator;
using ymengine::LfoShape;
using ymengine::LfoTarget;
using ymengine::TrackLfo;

namespace {

constexpr double kGap = 8;
// Names in the order of ymengine::LfoShape and ymengine::LfoTarget.
constexpr std::array<const char*, ymengine::kLfoShapes> kShapes = {"Triangle", "Sine",     "Square",
                                                                   "Saw up",   "Saw down", "Random"};
constexpr std::array<const char*, ymengine::kLfoTargets> kTargets = {"Volume",    "Noise period", "Env period",
                                                                     "SID ratio", "Pitch",        "Pan"};
// In the order of kLfoDivisionTicks.
constexpr std::array<const char*, ymengine::kLfoDivisions> kDivisions = {
    "4/1", "2/1", "1/1", "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64"};
constexpr const char* kPanWhy = "Pan: 16 channels only; the 3-channel modes are mono";
constexpr const char* kToTip =
    "What the LFO moves: volume, noise period, envelope period, SID ratio, pitch, or pan (16 channels)";
constexpr const char* kDivTip = "LFO cycle: a division of the tempo";

// Rate to the hundredth below 1 Hz, to the tenth above, within its range.
double roundRate(double hz) noexcept {
    hz = std::clamp(hz, ymengine::kLfoMinHz, ymengine::kLfoMaxHz);
    return hz < 1 ? std::round(hz * 100) / 100 : std::round(hz * 10) / 10;
}

double depthMax(LfoTarget t) noexcept { return ymengine::kLfoDepthMax[size_t(t)]; }

// Drawings of the shapes (D-291): 36 × 16, two cycles of 16 px from x = 1 between rows 1 (top) and 14 (bottom), a
// 2 px stroke whose jumps are joined by vertical runs, like the envelope shapes of Shape R13. Triangle and Sine (ninth
// DAW test, item 118, D-319, after the single cycle of D-292): the triangle in straight steep runs meeting in a point,
// the sine with its crests rounded off (the square root of the sine, its sign kept), so that the one is sharp and the
// other round.
constexpr int kIconW = 36, kIconH = 16, kIconBytes = (kIconW + 7) / 8 * kIconH;
using IconBits = std::array<std::array<uint8_t, kIconBytes>, ymengine::kLfoShapes>;

IconBits makeIcons() {
    // Random: one held value per 4 px, a fixed draw.
    constexpr std::array<double, 8> kHeld = {0.6, 0.15, 0.9, 0.35, 1.0, 0.0, 0.7, 0.45};
    IconBits out{};
    for (int k = 0; k < ymengine::kLfoShapes; ++k) {
        auto level = [k, &kHeld](int x) {  // 0 bottom to 1 top, at column x of the two cycles
            const double t = (x + 0.5) / 16.0, f = t - std::floor(t);
            const double u = x / 16.0 + 0.25;  // triangle: from the middle upward, the apex on one column
            const double s = std::sin(2 * 3.141592653589793 * t);
            switch (LfoShape(k)) {
            case LfoShape::Triangle: return 1 - std::abs(2 * (u - std::floor(u)) - 1);
            case LfoShape::Sine: return 0.5 + 0.5 * std::copysign(std::sqrt(std::abs(s)), s);
            case LfoShape::Square: return f < 0.5 ? 1.0 : 0.0;
            case LfoShape::SawUp: return f;
            case LfoShape::SawDown: return 1 - f;
            case LfoShape::Random: return kHeld[size_t(x / 4)];
            }
            return 0.0;
        };
        auto set = [&out, k](int x, int y) {
            out[size_t(k)][size_t(y * ((kIconW + 7) / 8) + x / 8)] |= uint8_t(0x80 >> (x % 8));
        };
        // Each of the 32 columns is drawn 2 px wide (x + 1 and x + 2), from the previous column's row to its own:
        // level 0 is row 14, level 1 row 1.
        int prev = -1;
        for (int x = 0; x < 32; ++x) {
            const int y = 14 - int(std::lround(level(x) * 13));
            const int lo = prev < 0 ? y : std::min(prev, y), hi = prev < 0 ? y : std::max(prev, y);
            for (int r = lo; r <= hi; ++r) {
                set(1 + x, r);
                set(2 + x, r);
            }
            prev = y;
        }
    }
    return out;
}

}  // namespace

// Drawn once, at the first call; the bitmaps point into the static bits.
const gemui::Bitmap& LfoPanel::shapeIcon(LfoShape shape) noexcept {
    static const IconBits bits = makeIcons();
    static const std::array<gemui::Bitmap, ymengine::kLfoShapes> icons = [] {
        std::array<gemui::Bitmap, ymengine::kLfoShapes> a{};
        for (size_t k = 0; k < a.size(); ++k) a[k] = {kIconW, kIconH, bits[k].data()};
        return a;
    }();
    return icons[size_t(std::clamp(int(shape), 0, ymengine::kLfoShapes - 1))];
}

LfoPanel::LfoPanel(MainWindow& main) : m_(main) {
    build();
    wire();
}

// ------------------------------------------------------------------------------------------- values

// Position p gives kLfoMinHz × (kLfoMaxHz / kLfoMinHz)^(p / kRatePositions): the same ratio for each step of the
// slider, rounded as the rate is shown.
double LfoPanel::rateOfPosition(int pos) noexcept {
    const double t = std::clamp(pos, 0, kRatePositions) / double(kRatePositions);
    return roundRate(ymengine::kLfoMinHz * std::pow(ymengine::kLfoMaxHz / ymengine::kLfoMinHz, t));
}

int LfoPanel::positionOfRate(double hz) noexcept {
    hz = std::clamp(std::isfinite(hz) ? hz : ymengine::kLfoMinHz, ymengine::kLfoMinHz, ymengine::kLfoMaxHz);
    const double t = std::log(hz / ymengine::kLfoMinHz) / std::log(ymengine::kLfoMaxHz / ymengine::kLfoMinHz);
    return std::clamp(int(std::lround(t * kRatePositions)), 0, kRatePositions);
}

std::string LfoPanel::rateText(double hz) {
    return gemui::formatDecimal(hz < 1 ? "%.2f Hz" : "%.1f Hz", roundRate(hz));
}

std::string LfoPanel::divisionText(int division) {
    return kDivisions[size_t(std::clamp(division, 0, ymengine::kLfoDivisions - 1))];
}

std::string LfoPanel::depthText(LfoTarget target, double depth) {
    const std::string n = std::to_string(int(std::lround(depth)));
    switch (target) {
    case LfoTarget::Volume:
    case LfoTarget::NoisePeriod: return n;
    case LfoTarget::EnvPeriod:
    case LfoTarget::SidRatio:
    case LfoTarget::Pitch: return n + "c";
    case LfoTarget::Pan: return n + "%";
    }
    return n;
}

int LfoPanel::divisionOf(std::string_view text) {
    std::string t;
    for (char c : text)
        if (c != ' ') t += char(std::toupper(static_cast<unsigned char>(c)));
    for (int k = 0; k < ymengine::kLfoDivisions; ++k)
        if (t == kDivisions[size_t(k)]) return k;
    return -1;
}

// ------------------------------------------------------------------------------------------- widgets

void LfoPanel::build() {
    rate_.setRange(0, kRatePositions, 1);
    depth_.setRange(0, depthMax(LfoTarget::Volume), 1);
    sync_.setTip("Sync to the tempo: one LFO cycle per division, at the host's tempo in a DAW, the sequencer's "
                 "otherwise; off: Rate in Hz");
    retrig_.setTip("Retrig: the LFO restarts at each note; off: it runs free and a note takes it where it is");
    depth_.setTip("LFO depth on either side: volume steps, noise period steps, cents or percent of pan");
    for (gemui::ValueField* f : {&ratev_, &depthv_}) f->setPx(14);
    ratev_.setWholeText(true);  // a division (1/8T) as well as a number
    ratev_.setTip("Click to type a rate in Hz, or a division (1/8T)");
    div_.setTip(kDivTip);
}

void LfoPanel::addTo(gemui::Widget& window) {
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&sep_, &labLfo_, &on_, &sync_, &retrig_, &labShape_,
                                                                  &shape_, &labTo_, &to_, &labRate_, &rate_,
                                                                  &ratev_, &div_, &labDepth_, &depth_, &depthv_})
        window.add(*w);
}

const TrackLfo& LfoPanel::lfo() const noexcept { return m_.extras_.tracks[size_t(m_.cur_)].patch.lfo; }

void LfoPanel::edit(const std::function<void(TrackLfo&)>& fn) {
    m_.editPatch([&fn](ymengine::TrackPatch& p) { fn(p.lfo); });  // nothing on a percussion track
}

void LfoPanel::wire() {
    on_.onClick = [this] { edit([](TrackLfo& l) { l.on = !l.on; }); };
    sync_.onClick = [this] { edit([](TrackLfo& l) { l.sync = !l.sync; }); };
    retrig_.onClick = [this] { edit([](TrackLfo& l) { l.retrig = !l.retrig; }); };  // D-279
    shape_.onClick = [this] { shapeMenu(); };
    to_.onClick = [this] { targetMenu(); };
    div_.onClick = [this] { divisionMenu(); };
    // Sliders: every move applies, one undo step per gesture (armed on press, MainWindow::pressed). While a slider is
    // held, MainWindow::dragging_ points to it so that refresh() does not move it under the pointer.
    for (gemui::Slider* s : {&rate_, &depth_}) {
        s->onGestureStart = [this, s] { m_.dragging_ = s; };
        s->onGestureEnd = [this] {
            m_.dragging_ = nullptr;
            refresh();
        };
    }
    rate_.onChange = [this](double v) { edit([v](TrackLfo& l) { l.hz = rateOfPosition(int(std::lround(v))); }); };
    depth_.onChange = [this](double v) {
        edit([v](TrackLfo& l) { l.depth = std::clamp(std::round(v), 0.0, depthMax(l.target)); });
    };
    // Typed: a division turns Sync on, a number gives a rate in Hz (Sync off).
    ratev_.onCommit = [this](const std::string& t) {
        if (const int d = divisionOf(t); d >= 0) {
            edit([d](TrackLfo& l) {
                l.sync = true;
                l.division = uint8_t(d);
            });
            return true;
        }
        const auto v = gemui::typedNumber(t);
        if (!v) return false;
        const double hz = roundRate(*v);
        edit([hz](TrackLfo& l) {
            l.sync = false;
            l.hz = hz;
        });
        return true;
    };
    depthv_.onCommit = [this](const std::string& t) {
        const auto v = gemui::typedNumber(t);
        if (!v) return false;
        depth_.input(*v);
        return true;
    };
}

void LfoPanel::shapeMenu() {
    std::vector<MenuItem> items;
    for (int k = 0; k < ymengine::kLfoShapes; ++k) {
        MenuItem m;
        m.label = kShapes[size_t(k)];
        m.icon = &shapeIcon(LfoShape(k));  // the drawing, then its name (D-291)
        m.pick = [this, k] { edit([k](TrackLfo& l) { l.shape = LfoShape(k); }); };
        items.push_back(std::move(m));
    }
    m_.trackPopup(shape_, std::move(items), int(lfo().shape));
}

// Synced: the divisions in a menu, in place of the slider (ninth DAW test, item 119, D-320), like the Division of the
// Arpeggiator.
void LfoPanel::divisionMenu() {
    std::vector<MenuItem> items;
    for (int k = 0; k < ymengine::kLfoDivisions; ++k) {
        MenuItem m;
        m.label = kDivisions[size_t(k)];
        m.pick = [this, k] { edit([k](TrackLfo& l) { l.division = uint8_t(k); }); };
        items.push_back(std::move(m));
    }
    m_.trackPopup(div_, std::move(items), std::min<int>(lfo().division, ymengine::kLfoDivisions - 1));
}

// A target that moves nothing on the focused track, from its settings: generator, source, mode (ninth DAW test, item
// 120, D-321). The rows of the sequences (MIX, GEN) can change them during a note; the menu goes by the track.
std::string LfoPanel::whyOff(LfoTarget target) const {
    const Generator g = Generator(m_.generator());
    const bool buzzer = g == Generator::SyncBuzzer, sid = g == Generator::Sid, dma = g == Generator::Dma;
    const bool envVolume = g == Generator::Chip && m_.iv(Track::VolMode) == 1;  // a source with Env
    switch (target) {
    case LfoTarget::Volume:
        if (buzzer)
            return "Volume: a Sync Buzzer takes its level from the hardware envelope, the 4-bit volume is ignored";
        if (envVolume) return "Volume: the source plays the hardware envelope, the 4-bit volume is ignored";
        return {};
    case LfoTarget::NoisePeriod:
        if (buzzer) return "Noise period: a Sync Buzzer plays the hardware envelope alone, without noise";
        if (dma) return "Noise period: a DMA track plays a sample, without noise";
        if (m_.iv(Track::Noise) == 0) return "Noise period: no noise in this track's source";
        return {};
    case LfoTarget::EnvPeriod:
        if (sid) return "Env period: a SID Voice writes its own volumes, the envelope is not used";
        if (dma) return "Env period: a DMA track plays a sample, without the envelope";
        if (!buzzer && !envVolume) return "Env period: the source uses the fixed volume, not the envelope";
        return {};
    case LfoTarget::SidRatio: return sid ? std::string() : "SID ratio: SID Voice only";
    case LfoTarget::Pitch: return {};
    case LfoTarget::Pan: return m_.mode3() ? kPanWhy : std::string();
    }
    return {};
}

void LfoPanel::targetMenu() {
    std::vector<MenuItem> items;
    for (int k = 0; k < ymengine::kLfoTargets; ++k) {
        MenuItem m;
        m.label = kTargets[size_t(k)];
        m.why = whyOff(LfoTarget(k));
        m.disabled = !m.why.empty();
        // The depth keeps its share of the range of the target.
        m.pick = [this, k] {
            edit([k](TrackLfo& l) {
                const LfoTarget to = LfoTarget(k);
                if (to == l.target) return;
                l.depth = std::round(l.depth / depthMax(l.target) * depthMax(to));
                l.target = to;
            });
        };
        items.push_back(std::move(m));
    }
    m_.trackPopup(to_, std::move(items), int(lfo().target));
}

void LfoPanel::refresh() {
    const bool dr = m_.drumTrack();
    const TrackLfo& l = lfo();
    const bool live = l.on && !dr;
    on_.setEnabled(!dr);
    on_.setOn(live);
    on_.setText(live ? "On" : "Off");
    on_.setTip(dr ? "Percussion tracks do not use the LFO"
                  : "LFO: a slow wave that moves one setting of the sound");
    // A target that moves nothing on this track (D-321): the other settings greyed, To left to pick another one, the
    // reason in its tip.
    const std::string off = live ? whyOff(l.target) : std::string();
    const bool moves = live && off.empty();
    for (gemui::Widget* w :
         std::initializer_list<gemui::Widget*>{&sync_, &retrig_, &shape_, &rate_, &div_, &depth_})
        w->setEnabled(moves);
    to_.setEnabled(live);
    labLfo_.setDim(dr);
    labTo_.setDim(!live);
    for (gemui::Label* lb : {&labShape_, &labRate_, &labDepth_}) lb->setDim(!moves);
    for (gemui::ValueField* f : {&ratev_, &depthv_}) f->setDim(!moves);

    // The drawing of the shape in place of its name (D-291), named by the tooltip.
    const int shape = std::min<int>(int(l.shape), ymengine::kLfoShapes - 1);
    shape_.setIcon(&shapeIcon(LfoShape(shape)));
    shape_.setTip(std::string("LFO shape: ") + kShapes[size_t(shape)]);
    to_.setText(kTargets[size_t(std::min<int>(int(l.target), ymengine::kLfoTargets - 1))]);
    to_.setTip(off.empty() ? kToTip : off);
    sync_.setOn(l.sync);
    retrig_.setOn(l.retrig);
    // Synced, the division in a menu button; otherwise the rate in Hz, slider and typed value (D-320).
    div_.setText(divisionText(l.division));
    rate_.setTip("LFO rate in Hz (0.1 to 20)");
    if (m_.dragging_ != &rate_) rate_.setValue(positionOfRate(l.hz));
    if (!ratev_.editing()) ratev_.setText(rateText(l.hz));
    if (m_.dragging_ != &depth_) {
        depth_.setRange(0, depthMax(l.target), 1);
        depth_.setValue(l.depth);
    }
    if (!depthv_.editing()) depthv_.setText(depthText(l.target, l.depth));
}

// ------------------------------------------------------------------------------------------- layout

double LfoPanel::layout(double x, double y, double width, double pad) {
    auto btn = [](Button& b) { return FlexItem{&b, b.naturalWidth(), double(b.naturalHeight())}; };
    auto lab = [](gemui::Label& l, double w) { return FlexItem{&l, w, 16}; };

    // Off (or on a percussion track): only "LFO" and its button, like the table above when it is off (eighth DAW
    // test, item 97, D-298).
    const bool live = lfo().on && !m_.drumTrack(), synced = lfo().sync;
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&labShape_, &shape_, &labTo_, &to_, &sync_,
                                                                  &retrig_, &labRate_, &labDepth_, &depth_, &depthv_})
        w->setVisible(live);
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&rate_, &ratev_}) w->setVisible(live && !synced);
    div_.setVisible(live && synced);

    // Rule up to the edges of the window, like the one above the settings of the sequences.
    y += 8;
    sep_.setBox({x - pad, y, width + 2 * pad, 2});
    y += 2 + 8;

    // The columns of the settings above: On / Off and Sync; shape and target; rate and depth (sliders aligned with
    // those of the vibrato table, values of a fixed width: the row does not move). The x + 260 and x + 452 columns,
    // the "Detune" and "Cycles" label widths and the 150 px sliders are those of SequencesPanel::layout.
    const double lw1 = Font::width("Detune", 14);
    const double x2 = x + 260, lw2 = Font::width("Shape", 14);
    const double x3 = x + 452, lw3 = Font::width("Cycles", 14);
    const double vw3 = std::max(Font::width("0.10 Hz", 14), Font::width("2400c", 14)), sw3 = 150;
    const double w1 = 252, w2 = 184, w3 = 272;  // width of the rows of each column

    std::vector<FlexItem> a1 = {lab(labLfo_, lw1), btn(on_)};
    // The drawing of the shape (16 px, blank top and bottom rows) in a button as high as those with a text.
    const FlexItem shape{&shape_, shape_.naturalWidth(), double(to_.naturalHeight())};
    std::vector<FlexItem> a2 = {lab(labShape_, lw2), shape};
    std::vector<FlexItem> a3 = {lab(labRate_, lw3), FlexItem{&rate_, sw3, 16}, FlexItem{&ratev_, vw3, 16}};
    if (synced) a3 = {lab(labRate_, lw3), btn(div_)};  // the division in a menu (D-320)
    const double r1 = flexRow(x, y, w1, a1, kGap, false);
    if (!live) return y + r1;
    y += std::max({r1, flexRow(x2, y, w2, a2, kGap, false), flexRow(x3, y, w3, a3, kGap, false)}) + 6;

    // Sync and Retrig under On / Off.
    std::vector<FlexItem> b1 = {FlexItem{nullptr, lw1, 16}, btn(sync_), btn(retrig_)};
    std::vector<FlexItem> b2 = {lab(labTo_, lw2), btn(to_)};
    std::vector<FlexItem> b3 = {lab(labDepth_, lw3), FlexItem{&depth_, sw3, 16}, FlexItem{&depthv_, vw3, 16}};
    const double h2 = std::max({flexRow(x, y, w1, b1, kGap, false), flexRow(x2, y, w2, b2, kGap, false),
                                flexRow(x3, y, w3, b3, kGap, false)});
    return y + h2;
}

std::optional<Rect> LfoPanel::rectOf(std::string_view id) const {
    struct Named {
        std::string_view id;
        const gemui::Widget* w;
    };
    const Named named[] = {{"lfoOn", &on_},       {"lfoShape", &shape_},   {"lfoTo", &to_},
                           {"lfoSync", &sync_},   {"lfoRetrig", &retrig_}, {"lfoRate", &rate_},
                           {"lfoDepth", &depth_}, {"lfoRateV", &ratev_},   {"lfoDepthV", &depthv_},
                           {"lfoDiv", &div_}};
    for (const Named& n : named)
        if (id == n.id) return n.w->visible() ? std::optional<Rect>(n.w->bounds()) : std::nullopt;
    return std::nullopt;
}

const gemui::Slider* LfoPanel::slider(std::string_view id) const noexcept {
    if (id == "lfoRate") return &rate_;
    if (id == "lfoDepth") return &depth_;
    return nullptr;
}

const gemui::Button* LfoPanel::button(std::string_view id) const noexcept {
    if (id == "lfoOn") return &on_;
    if (id == "lfoSync") return &sync_;
    if (id == "lfoRetrig") return &retrig_;
    if (id == "lfoTo") return &to_;
    if (id == "lfoDiv") return &div_;
    return nullptr;
}

}  // namespace stym::pluginui
