// STYM2149 — pluginui: Effects window (echo of the notes of the focused track, delay of the 16 channels)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "pluginui/EchoWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "plugincore/Mapping.h"
#include "pluginui/Layout.h"
#include "pluginui/MainWindow.h"

namespace stym::pluginui {

using gemui::Button;
using gemui::MenuItem;
using gemui::Rect;
using plugin::Global;
using ymengine::TrackEcho;

namespace {

constexpr double kSliderW = 129;  // like the sliders of the Arpeggiator
constexpr double kGap = 8, kRowMargin = 6, kPad = 8;  // between items, below each row, padding of the content (px)
constexpr double kToMargin = 8;  // between Division and To on their row

constexpr const char* kEchoTip =
    "Echo: play each note of this sound again, quieter each time; Authentic 3 ch plays the repeats on the chip";
constexpr const char* kDelayTip = "Delay: echoes of the sound of every track, as much as its Send in the Mixer";
constexpr const char* kDelay3Tip = "Delay: 16 ch only. In 3 ch, the Echo repeats the notes on the chip";
constexpr const char* kEchoBuzzTip =
    "Echo: none for Sync Buzzer in 3 ch, a repeat would restart the shared hardware envelope at full level";

// Number at the start of a typed value ("250 ms" gives 250), rounded to a whole number; nullopt when there is none.
std::optional<double> typed(const std::string& txt) {
    size_t used = 0;
    const double raw = gemui::readDecimal(txt, &used);
    if (used == 0 || !std::isfinite(raw)) return std::nullopt;
    return std::round(raw);
}

// Names of the divisions of the tempo (those of the Delay Division parameter, D-299).
const plugin::ParamSpec& divisions() { return plugin::paramSpec(plugin::paramIndex(Global::DelayDivision)); }
std::string divisionName(int k) {
    const plugin::ParamSpec& s = divisions();
    return std::string(s.choices[size_t(std::clamp(k, 0, int(s.choices.size()) - 1))]);
}

// Logarithmic slider: position 0 to kLogPositions ↔ value lo to hi, the same ratio for each step of position.
double logOf(int pos, double lo, double hi) {
    const double t = std::clamp(pos, 0, EchoWindow::kLogPositions) / double(EchoWindow::kLogPositions);
    return lo * std::pow(hi / lo, t);
}
int positionOf(double v, double lo, double hi) {
    v = std::clamp(std::isfinite(v) ? v : lo, lo, hi);
    const double t = std::log(v / lo) / std::log(hi / lo);
    return std::clamp(int(std::lround(t * EchoWindow::kLogPositions)), 0, EchoWindow::kLogPositions);
}

}  // namespace

int EchoWindow::timeOfPosition(int pos) noexcept {
    return int(std::lround(logOf(pos, ymengine::kDelayMinMs, ymengine::kDelayMaxMs)));
}
int EchoWindow::positionOfTime(double ms) noexcept {
    return positionOf(ms, ymengine::kDelayMinMs, ymengine::kDelayMaxMs);
}
int EchoWindow::cutOfPosition(int pos) noexcept {
    return int(std::lround(logOf(pos, ymengine::kDelayHiCutMin, ymengine::kDelayHiCutMax)));
}
int EchoWindow::positionOfCut(double hz) noexcept {
    return positionOf(hz, ymengine::kDelayHiCutMin, ymengine::kDelayHiCutMax);
}

// ------------------------------------------------------------------------------------------- construction

EchoWindow::EchoWindow(MainWindow& main) : m_(main) {
    build();
    wire();
    place({double(kX), double(kY), 177, 90});  // provisional size: layout() sets the real one at the first refresh
}

void EchoWindow::build() {
    setTitle("EFFECTS");  // Echo / Delay up to the ninth DAW test (item 110, D-311)
    on_.setTip(kEchoTip);
    div_.setTip("Time between two repeats: a division of the tempo");
    to_.setTip(
        "Track that plays the repeats. In 3 ch, a channel only takes a repeat while it plays no note of its own");
    rep_.setRange(1, ymengine::kEchoRepeatsMax, 1);
    rep_.setTip("Number of repeats of each note");
    drop_.setRange(0, ymengine::kEchoDropMax, 1);
    drop_.setTip("Volume steps lost at each repeat (0: every repeat at the level of the note)");
    ping_.setTip("Ping-pong: the echoes go from one side to the other");
    sync_.setTip("Sync to the tempo: one echo per Division, at the host's tempo in a DAW, the sequencer's otherwise; "
                 "off: Time in ms");
    delDiv_.setTip("Delay time: a division of the tempo");
    time_.setRange(0, kLogPositions, 1);
    time_.setTip("Delay time in milliseconds");
    fb_.setRange(0, ymengine::kDelayFeedbackMax * 100, 1);  // in percent, the unit of the Delay Feedback parameter
    fb_.setTip("Feedback: how much of each echo comes back in the next one");
    cut_.setRange(0, kLogPositions, 1);
    cut_.setTip("Hi cut: each echo loses the treble above this frequency");
    level_.setRange(0, 100, 1);
    level_.setTip("Level of the echoes in the mix");
    // Typed values: the field's width in characters (its longest text) and its input mask (one 9 per digit); a unit
    // after the digits stays out of the reverse video under the pointer and shows while typing (D-464).
    repv_.setMinChars(1);
    repv_.setMask("9");
    dropv_.setMinChars(2);
    dropv_.setMask("99");
    timev_.setMinChars(7);  // "2000 ms"
    timev_.setMask("9999 ms");
    fbv_.setMinChars(4);  // "90 %"
    fbv_.setMask("99 %");
    cutv_.setMinChars(8);  // "20000 Hz"
    cutv_.setMask("99999 Hz");
    levelv_.setMinChars(5);  // "100 %"
    levelv_.setMask("999 %");
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{
             &labEcho_, &on_,      &labDiv_, &div_,    &labTo_,   &to_,      &labRep_,   &rep_,  &repv_,
             &labDrop_, &drop_,    &dropv_,  &sep_,    &labDelay_, &delayOn_, &ping_,    &sync_, &labDelDiv_,
             &delDiv_,  &labTime_, &time_,   &timev_,  &labFb_,   &fb_,      &fbv_,      &labCut_, &cut_,
             &cutv_,    &labLevel_, &level_, &levelv_})
        add(*w);
}

void EchoWindow::wire() {
    onClose = [this] { m_.syncWindowButtons(); };
    on_.onClick = [this] { editEcho([](TrackEcho& x) { x.on = !x.on; }); };
    div_.onClick = [this] {
        std::vector<MenuItem> items;
        for (int k = 0; k < ymengine::kDelayDivisions; ++k) {
            MenuItem m;
            m.label = divisionName(k);
            m.pick = [this, k] { editEcho([k](TrackEcho& x) { x.division = uint8_t(k); }); };
            items.push_back(std::move(m));
        }
        m_.trackPopup(div_, std::move(items), echo().division);
    };
    // To: "Same track" (target -1), then each track of the mode; the checked item is therefore target + 1.
    to_.onClick = [this] {
        std::vector<MenuItem> items;
        for (int t = -1; t < m_.voices(); ++t) {
            MenuItem m;
            m.label = targetText(t);
            m.pick = [this, t] { editEcho([t](TrackEcho& x) { x.target = int8_t(t); }); };
            items.push_back(std::move(m));
        }
        const int t = echo().target;
        m_.trackPopup(to_, std::move(items), t >= 0 && t < m_.voices() ? t + 1 : 0);
    };
    // Repeats and Drop: state that cannot be automated (the held slider is not read back).
    for (gemui::Slider* s : {&rep_, &drop_}) {
        s->onGestureStart = [this, s] { m_.dragging_ = s; };
        s->onGestureEnd = [this] {
            m_.dragging_ = nullptr;
            m_.refresh();
        };
    }
    auto setRepeats = [this](double v) {
        const auto n = uint8_t(std::clamp(int(v), 1, ymengine::kEchoRepeatsMax));
        if (n != echo().repeats) editEcho([n](TrackEcho& x) { x.repeats = n; });
    };
    auto setDrop = [this](double v) {
        const auto n = uint8_t(std::clamp(int(v), 0, ymengine::kEchoDropMax));
        if (n != echo().drop) editEcho([n](TrackEcho& x) { x.drop = n; });
    };
    rep_.onChange = setRepeats;
    drop_.onChange = setDrop;
    repv_.onCommit = [setRepeats](const std::string& t) {
        const auto v = typed(t);
        if (v) setRepeats(*v);
        return v.has_value();
    };
    dropv_.onCommit = [setDrop](const std::string& t) {
        const auto v = typed(t);
        if (v) setDrop(*v);
        return v.has_value();
    };

    // Delay: host parameters, 16 channels only. MainWindow::bindSlider opens one host gesture per parameter at the
    // first move of a slider and closes it at the release; the typed values are in the parameters' units (ms, %, Hz).
    auto toggle = [this](Global g) {
        if (studio()) m_.set(g, m_.iv(g) != 0 ? 0.0f : 1.0f);
    };
    delayOn_.onClick = [toggle] { toggle(Global::DelayOn); };
    ping_.onClick = [toggle] { toggle(Global::DelayPingPong); };
    sync_.onClick = [toggle] { toggle(Global::DelaySync); };
    delDiv_.onClick = [this] {
        std::vector<MenuItem> items;
        for (int k = 0; k < ymengine::kDelayDivisions; ++k) {
            MenuItem m;
            m.label = divisionName(k);
            m.pick = [this, k] { m_.set(Global::DelayDivision, float(k)); };
            items.push_back(std::move(m));
        }
        m_.popup(delDiv_, std::move(items), m_.iv(Global::DelayDivision));
    };
    m_.bindSlider(time_, [this](double pos) { m_.set(Global::DelayTime, float(timeOfPosition(int(pos)))); });
    m_.bindSlider(fb_, [this](double v) { m_.set(Global::DelayFeedback, float(v)); });
    m_.bindSlider(cut_, [this](double pos) { m_.set(Global::DelayHiCut, float(cutOfPosition(int(pos)))); });
    m_.bindSlider(level_, [this](double v) { m_.set(Global::DelayLevel, float(v)); });
    auto commit = [this](Global g, double lo, double hi) {
        return [this, g, lo, hi](const std::string& t) {
            const auto v = typed(t);
            if (!v) return false;
            m_.set(g, float(std::clamp(*v, lo, hi)));
            return true;
        };
    };
    timev_.onCommit = commit(Global::DelayTime, ymengine::kDelayMinMs, ymengine::kDelayMaxMs);
    fbv_.onCommit = commit(Global::DelayFeedback, 0, ymengine::kDelayFeedbackMax * 100);
    cutv_.onCommit = commit(Global::DelayHiCut, ymengine::kDelayHiCutMin, ymengine::kDelayHiCutMax);
    levelv_.onCommit = commit(Global::DelayLevel, 0, 100);
}

// ------------------------------------------------------------------------------------------- state

bool EchoWindow::studio() const noexcept { return !m_.mode3(); }
bool EchoWindow::buzzer3() const noexcept {
    return m_.mode3() && plugin::effectiveGenerator(m_.values_, m_.cur_) == ymengine::Generator::SyncBuzzer;
}
const TrackEcho& EchoWindow::echo() const noexcept { return m_.extras_.tracks[size_t(m_.cur_)].patch.echo; }

void EchoWindow::editEcho(const std::function<void(TrackEcho&)>& fn) {
    // A percussion track has its echo too: the whole patch of the track, unlike editPatch.
    const int t = m_.cur_;
    m_.host_.editExtras([t, &fn](plugin::NonParamState& s) { fn(s.tracks[size_t(t)].patch.echo); });
    m_.pullExtras();
    m_.pushCur();  // the undo step prepared by the press in this window (MainWindow::pressed), once per gesture
}

std::string EchoWindow::targetText(int target) const {
    if (target < 0 || target >= m_.voices()) return "Same track";
    return m_.trackName(target);
}

void EchoWindow::show() {
    open();
    refresh();
    m_.syncWindowButtons();
}

void EchoWindow::refresh() {
    setTitle("EFFECTS - " + m_.trackName(m_.cur_));  // also while closed: right as soon as it opens
    if (!isOpen()) return;
    const TrackEcho& x = echo();
    on_.setOn(x.on);
    on_.setText(x.on ? "On" : "Off");
    // Sync Buzzer in 3 channels: the echo greyed out, its settings kept (D-316).
    const bool echoOk = !buzzer3();
    on_.setEnabled(echoOk);
    on_.setTip(echoOk ? kEchoTip : kEchoBuzzTip);
    for (Button* b : {&div_, &to_}) b->setEnabled(echoOk);
    for (gemui::Slider* s : {&rep_, &drop_}) s->setEnabled(echoOk);
    for (gemui::ValueField* f : {&repv_, &dropv_}) f->setDim(!echoOk);
    for (gemui::Label* l : {&labEcho_, &labDiv_, &labTo_, &labRep_, &labDrop_}) l->setDim(!echoOk);
    div_.setText(divisionName(x.division));
    to_.setText(targetText(x.target));
    if (m_.dragging_ != &rep_) rep_.setValue(x.repeats);
    if (m_.dragging_ != &drop_) drop_.setValue(x.drop);
    if (!repv_.editing()) repv_.setText(std::to_string(x.repeats));
    if (!dropv_.editing()) dropv_.setText(std::to_string(x.drop));

    // Delay: read from the host parameters (an automation moves them too); the sliders show positions, the fields the
    // values.
    const bool ok = studio(), on = m_.iv(Global::DelayOn) != 0;
    delayOn_.setOn(on);
    delayOn_.setText(on ? "On" : "Off");
    delayOn_.setEnabled(ok);
    delayOn_.setTip(ok ? kDelayTip : kDelay3Tip);
    ping_.setOn(m_.iv(Global::DelayPingPong) != 0);
    ping_.setEnabled(ok);
    sync_.setOn(m_.iv(Global::DelaySync) != 0);
    delDiv_.setText(divisionName(m_.iv(Global::DelayDivision)));
    const int ms = m_.iv(Global::DelayTime), fb = m_.iv(Global::DelayFeedback), hz = m_.iv(Global::DelayHiCut),
              lvl = m_.iv(Global::DelayLevel);
    if (m_.dragging_ != &time_) time_.setValue(positionOfTime(ms));
    if (m_.dragging_ != &fb_) fb_.setValue(fb);
    if (m_.dragging_ != &cut_) cut_.setValue(positionOfCut(hz));
    if (m_.dragging_ != &level_) level_.setValue(lvl);
    if (!timev_.editing()) timev_.setText(std::to_string(ms) + " ms");
    if (!fbv_.editing()) fbv_.setText(std::to_string(fb) + " %");
    if (!cutv_.editing()) cutv_.setText(std::to_string(hz) + " Hz");
    if (!levelv_.editing()) levelv_.setText(std::to_string(lvl) + " %");
    layout();
}

// ------------------------------------------------------------------------------------------- layout

void EchoWindow::layout() {
    const double ox = box().x, oy = box().y;
    const double x = ox + 14;  // content (border 2, margin 4, padding 8)
    const bool echoOn = echo().on;
    const bool delayRows = studio() && m_.iv(Global::DelayOn) != 0;
    const bool synced = m_.iv(Global::DelaySync) != 0;

    // What shows: the Echo's rows when it is on; the Delay's rows when it applies and is on, Division (Sync) or Time.
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&labDiv_, &div_, &labTo_, &to_, &labRep_, &rep_,
                                                                  &repv_, &labDrop_, &drop_, &dropv_})
        w->setVisible(echoOn);
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&ping_, &sync_, &labFb_, &fb_, &fbv_, &labCut_,
                                                                  &cut_, &cutv_, &labLevel_, &level_, &levelv_})
        w->setVisible(delayRows);  // Ping-pong too (ninth DAW test, item 114, D-315)
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&labDelDiv_, &delDiv_})
        w->setVisible(delayRows && synced);
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&labTime_, &time_, &timev_})
        w->setVisible(delayRows && !synced);

    // Labels of the slider rows at one width, so that the sliders line up.
    auto widest = [](std::initializer_list<const gemui::Label*> ls) {
        double w = 0;
        for (const gemui::Label* l : ls) w = std::max(w, l->naturalWidth());
        return w;
    };
    const double labW = widest({&labRep_, &labDrop_, &labTime_, &labFb_, &labCut_, &labLevel_});
    auto lab = [labW](gemui::Label& l, bool aligned = true) {
        return FlexItem{&l, aligned ? labW : l.naturalWidth(), 16};
    };
    auto btn = [](Button& b) { return FlexItem{&b, b.naturalWidth(), double(b.naturalHeight())}; };
    auto slider = [](gemui::Slider& s) {
        FlexItem it{&s, 0, 16};
        it.grow = 1;
        return it;
    };
    const double valW = std::max({repv_.naturalWidth(), dropv_.naturalWidth(), timev_.naturalWidth(),
                                  fbv_.naturalWidth(), cutv_.naturalWidth(), levelv_.naturalWidth()});

    // Content width: the widest row that can show, sliders at their default width; the Delay row with its Ping-pong,
    // so that the window keeps its width when it shows.
    const double delayRowW =
        labDelay_.naturalWidth() + kGap + delayOn_.naturalWidth() + kGap + ping_.naturalWidth();
    double contentW = std::max(
        {labW + kGap + kSliderW + kGap + valW, labEcho_.naturalWidth() + kGap + on_.naturalWidth(), delayRowW});
    if (echoOn)
        contentW = std::max(contentW, labDiv_.naturalWidth() + kGap + div_.naturalWidth() + kGap + kToMargin +
                                          labTo_.naturalWidth() + kGap + to_.naturalWidth());
    if (delayRows && synced)
        contentW = std::max(contentW, sync_.naturalWidth() + kGap + labDelDiv_.naturalWidth() + kGap +
                                          delDiv_.naturalWidth());

    // Rows from below the frame (6) and the title bar (26). A slider row keeps an empty span for its value field,
    // then puts the field at the left of that span with its own width.
    double y = oy + 32 + kPad + kRowMargin;
    auto row = [&](std::vector<FlexItem> items) {
        y += flexRow(x, y, contentW, items) + kRowMargin;
        return items;
    };
    auto sliderRow = [&](gemui::Label& l, gemui::Slider& s, gemui::ValueField& f) {
        std::vector<FlexItem> r = {lab(l), slider(s), {nullptr, valW, 16}};
        const double h = flexRow(x, y, contentW, r);
        f.setBox({r[2].box.x, r[2].box.y, f.naturalWidth(), 16});
        y += h + kRowMargin;
    };
    row({lab(labEcho_, false), btn(on_)});
    if (echoOn) {
        FlexItem t = lab(labTo_, false);
        t.marginLeft = kToMargin;
        row({lab(labDiv_, false), btn(div_), t, btn(to_)});
        sliderRow(labRep_, rep_, repv_);
        sliderRow(labDrop_, drop_, dropv_);
    }
    // Rule up to the edges of the window, then the delay.
    sep_.setBox({x - kPad, y, contentW + 2 * kPad, 2});
    y += 2 + kRowMargin;
    if (delayRows) row({lab(labDelay_, false), btn(delayOn_), btn(ping_)});
    else row({lab(labDelay_, false), btn(delayOn_)});
    if (delayRows) {
        if (synced) row({btn(sync_), lab(labDelDiv_, false), btn(delDiv_)});
        else {
            std::vector<FlexItem> r = {btn(sync_), lab(labTime_, false), slider(time_), {nullptr, valW, 16}};
            const double h = flexRow(x, y, contentW, r);
            timev_.setBox({r[3].box.x, r[3].box.y, timev_.naturalWidth(), 16});
            y += h + kRowMargin;
        }
        sliderRow(labFb_, fb_, fbv_);
        sliderRow(labCut_, cut_, cutv_);
        sliderRow(labLevel_, level_, levelv_);
    }

    // Window: content, padding of 8, border and margin of .gwin (6 on each side).
    const double w = contentW + 2 * kPad + 12;
    const double h = y + kPad + 6 - oy;
    if (std::abs(h - box().h) > 0.01 || std::abs(w - box().w) > 0.01) place({ox, oy, w, h});
    repaint();
}

std::optional<Rect> EchoWindow::rectOf(std::string_view id) const {
    if (!isOpen()) return std::nullopt;
    if (id == "echoWin") return bounds();
    struct Named {
        std::string_view id;
        const gemui::Widget* w;
    };
    const Named named[] = {{"echoOn", &on_},       {"echoDiv", &div_},       {"echoTo", &to_},
                           {"echoRep", &rep_},     {"echoRepv", &repv_},     {"echoDrop", &drop_},
                           {"echoDropv", &dropv_}, {"delaySep", &sep_},      {"delayOn", &delayOn_},
                           {"delayPing", &ping_},  {"delaySync", &sync_},    {"delayDiv", &delDiv_},
                           {"delayTime", &time_},  {"delayTimev", &timev_},  {"delayFb", &fb_},
                           {"delayFbv", &fbv_},    {"delayCut", &cut_},      {"delayCutv", &cutv_},
                           {"delayLevel", &level_}, {"delayLevelv", &levelv_}};
    for (const Named& n : named)
        if (id == n.id) return n.w->visible() ? std::optional<Rect>(n.w->bounds()) : std::nullopt;
    return std::nullopt;
}

const gemui::Slider* EchoWindow::slider(std::string_view id) const noexcept {
    const std::pair<std::string_view, const gemui::Slider*> all[] = {{"echoRep", &rep_},  {"echoDrop", &drop_},
                                                                      {"delayTime", &time_}, {"delayFb", &fb_},
                                                                      {"delayCut", &cut_}, {"delayLevel", &level_}};
    for (const auto& [name, s] : all)
        if (id == name) return s;
    return nullptr;
}

const gemui::Button* EchoWindow::button(std::string_view id) const noexcept {
    const std::pair<std::string_view, const gemui::Button*> all[] = {{"echoOn", &on_},       {"echoDiv", &div_},
                                                                      {"echoTo", &to_},       {"delayOn", &delayOn_},
                                                                      {"delayPing", &ping_},  {"delaySync", &sync_},
                                                                      {"delayDiv", &delDiv_}};
    for (const auto& [name, b] : all)
        if (id == name) return b;
    return nullptr;
}

}  // namespace stym::pluginui
