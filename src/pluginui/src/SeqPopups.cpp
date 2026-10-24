// STYM2149 — pluginui: Step Sequencer boxes (step volume, gate, pan, track length, Euclidean generator)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// .gemmenu boxes of the prototype (openVolMenu, gatPopup, panPopup, plenBtn.onclick, euclidBtn.onclick): white
// background, 2 px border, closed by a press outside them or by Escape. They live in the menu layer of the desktop.
// The desktop owns an open box and destroys it when it closes: a box keeps no pointer that outlives it, and whatever
// points to a box must forget it on destruction (EuclidBox).
#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "gemui/Alert.h"
#include "gemui/Desktop.h"
#include "pluginui/MainWindow.h"
#include "pluginui/SeqWindow.h"
#include "ymseq/Euclid.h"
#include "ymseq/Tables.h"

namespace stym::pluginui {

using gemui::Box;
using gemui::Button;
using gemui::Font;
using gemui::Painter;
using gemui::Rect;
using gemui::Slider;
namespace colors = gemui::colors;

namespace {

// JavaScript parseInt: integer at the start of the text (spaces and sign allowed), nothing otherwise.
std::optional<int> parseIntJs(const std::string& t) {
    const char* s = t.c_str();
    while (*s == ' ' || *s == '\t') ++s;
    char* end = nullptr;
    const long v = std::strtol(s, &end, 10);
    if (end == s) return std::nullopt;
    return int(std::clamp<long>(v, -1000000, 1000000));
}

double lineH(int px) { return Font::lineBox("0", px).height(); }

// .gemmenu: white box with a 2 px border; it keeps the presses made on its background.
class PopupBox : public gemui::Widget {
public:
    void paint(Painter& p) override {
        p.fill(bounds(), colors::kWhite);
        p.frame(bounds(), 2, colors::kBlack);
    }
    bool acceptsMouse() const noexcept override { return true; }

protected:
    void close() {
        if (gemui::Desktop* d = desktop()) d->closePopup(*this);
    }
};

// One-line text (bold of the prototype: second pass offset by one pixel).
class Text : public gemui::Widget {
public:
    Text(std::string t, int px, gemui::Color c = colors::kBlack, bool bold = false)
        : text_(std::move(t)), px_(px), color_(c), bold_(bold) {}
    void setText(std::string t) {
        text_ = std::move(t);
        repaint();
    }
    double naturalWidth() const { return Font::width(text_, px_) + (bold_ ? 1 : 0); }
    void setCenter(bool c) { center_ = c; }
    void paint(Painter& p) override {
        const Box& b = box();
        const double x = center_ ? b.x + (b.w - Font::width(text_, px_)) / 2 : b.x;
        const double top = b.y + (b.h - lineH(px_)) / 2;
        p.text(text_, x, top, px_, color_);
        if (bold_) p.text(text_, x + 1, top, px_, color_);
    }

private:
    std::string text_;
    int px_;
    gemui::Color color_;
    bool bold_;
    bool center_ = false;
};

// Box with a slider, a field and two buttons (gatPopup, panPopup, Track length).
class ValuePopup : public PopupBox {
public:
    struct Spec {
        std::string title, titleTip;
        int min = 0, max = 100, value = 0;
        std::string text;
        int textW = 36;
        std::string suffix;
        std::string rangeTip, textTip;
        std::string chanLabel, chanTip, setTip;
        int fallback = 0;  // value of the slider when the field cannot be read
        // Right button (Set, with the text entered) or left button (Channel, Center, Global: chan = true).
        std::function<void(bool chan, const std::string& text)> apply;
    };

    ValuePopup(Spec spec, const Rect& anchor, int deskW) : spec_(std::move(spec)), title_(spec_.title, 16) {
        for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&title_, &range_, &field_, &chan_, &set_})
            add(*w);
        if (!spec_.suffix.empty()) add(suffix_);
        title_.setTip(spec_.titleTip);
        range_.setRange(spec_.min, spec_.max, 1);
        range_.setValue(spec_.value);
        range_.setTip(spec_.rangeTip);
        field_.setText(spec_.text);
        field_.setTip(spec_.textTip);
        chan_.setText(spec_.chanLabel);
        chan_.setTip(spec_.chanTip);
        set_.setText("Set");
        set_.setTip(spec_.setTip);
        range_.onChange = [this](double v) { field_.setText(std::to_string(int(v))); };
        field_.onChange = [this] {
            const auto v = parseIntJs(field_.text());
            range_.setValue(std::clamp(v && *v ? *v : spec_.fallback, spec_.min, spec_.max));
        };
        field_.onEnter = [this] { set_.onClick(); };
        chan_.onClick = [this] { finish(true); };
        set_.onClick = [this] { finish(false); };

        // Layout: padding 8, title (margin 6), 150 px slider, space, field [, suffix], buttons on the right (margin 8).
        // Width: content, padding (2 × 8) and border (2 × 2); the box opens under the cell, its left edge kept
        // between 4 px and 220 px from the desktop's sides.
        const double lh = lineH(16), sp = Font::advance(' ', 16);
        const double sufW = spec_.suffix.empty() ? 0 : Font::width(spec_.suffix, 16);
        const double rowW = 150 + sp + spec_.textW + sufW;
        const double bw = chan_.naturalWidth() + sp + set_.naturalWidth();
        const double content = std::max({rowW, title_.naturalWidth(), bw});
        const double w = content + 16 + 4;
        const double x = std::max(4.0, std::min(double(anchor.x), double(deskW - 220)));
        const double y = anchor.bottom() + 2;
        const double x0 = x + 10;
        double y0 = y + 10;
        title_.setBox({x0, y0, title_.naturalWidth(), lh});
        y0 += lh + 6;
        const double rowH = std::max(lh, 16.0);
        range_.setBox({x0, y0 + (rowH - 16) / 2, 150, 16});
        field_.setBox({x0 + 150 + sp, y0 + (rowH - lh) / 2, double(spec_.textW), lh});
        if (!spec_.suffix.empty()) {
            suffix_.setText(spec_.suffix);
            suffix_.setBox({x0 + 150 + sp + spec_.textW, y0 + (rowH - lh) / 2, sufW, lh});
        }
        y0 += rowH + 8;
        const double bh = set_.naturalHeight();
        set_.setBox({x0 + content - set_.naturalWidth(), y0, set_.naturalWidth(), bh});
        chan_.setBox({x0 + content - bw, y0, chan_.naturalWidth(), bh});
        y0 += bh;
        setBox({x, y, w, y0 + 10 - y});
    }

private:
    void finish(bool chan) {
        // The box closes (and is destroyed) before the action.
        const auto apply = spec_.apply;
        const std::string text = field_.text();
        close();
        if (apply) apply(chan, text);
    }

    Spec spec_;
    Text title_;
    Slider range_;
    gemui::TextField field_;
    Text suffix_{"", 16};
    Button chan_, set_;
};

// Slider that a right click sets back (sixth DAW test, item 75, D-273).
class ResetSlider : public Slider {
public:
    std::function<void()> onReset;
    void mouseDown(const gemui::MouseEvent& e) override {
        if (e.button == gemui::MouseButton::Right) {
            if (onReset) onReset();
            return;
        }
        Slider::mouseDown(e);
    }
};

// #volmenu: vertical slider (96 px), value below; each move is heard at once. The value can be typed (D-274): two
// positions, or the signed values without a mask.
class VolPopup : public PopupBox {
public:
    VolPopup(SliderPopupSpec spec, const Rect& anchor, int deskW, int deskH) : set_(std::move(spec.set)) {
        add(range_);
        add(value_);
        range_.setVertical(true);
        range_.setRange(spec.min, spec.max, 1);
        range_.setValue(spec.value);
        range_.setTip("Drag to set the value, right click to set it back");
        const int chars = int(std::max(std::to_string(spec.min).size(), std::to_string(spec.max).size()));
        value_.setMinChars(chars);
        if (spec.min >= 0) value_.setMask(std::string(size_t(chars), '9'));
        value_.setText(std::to_string(spec.value));
        range_.onChange = [this](double v) {
            if (!value_.editing()) value_.setText(std::to_string(int(v)));
            if (set_) set_(int(v));
        };
        range_.onReset = [this, reset = spec.reset] { range_.input(reset); };
        value_.onCommit = [this](const std::string& t) {
            const auto v = gemui::typedNumber(t);
            if (!v) return false;
            range_.input(*v);
            value_.setText(std::to_string(int(range_.value())));
            return true;
        };
        // Size: column as wide as the value (20 px at least), padding 4 and border 2 on each side; height: border,
        // padding, 96 px slider, 2 px, value line, padding, border. Under the cell, or above it at the bottom of the
        // desktop.
        const double lh = lineH(16);
        const double cw = std::max(20.0, value_.naturalWidth());
        const double mw = cw + 8 + 4, mh = 2 + 6 + 96 + 2 + lh + 6 + 2;
        double x = anchor.x, y = anchor.bottom() + 2;
        if (x + mw > deskW - 4) x = deskW - mw - 4;
        if (y + mh > deskH - 4) y = anchor.y - mh - 2;
        setBox({x, y, mw, mh});
        const double wrapX = x + 2 + 4 + (cw - 20) / 2;
        range_.setBox({wrapX + 2, y + 8, 16, 96});  // 96 px rotated inside a 20 px .trwrap
        value_.setBox({x + 6, y + 8 + 96 + 2, cw, lh});
    }

private:
    std::function<void(int)> set_;
    ResetSlider range_;
    gemui::ValueField value_;
};

// Preview of the Euclidean pattern: 11 × 16 px cells, 2 px border, black (accent), grey (hit) or white.
class EuclidPreview : public gemui::Widget {
public:
    void set(const ymseq::EuclidFigure& f) {
        fig_ = f;
        repaint();
    }
    void paint(Painter& p) override {
        const Rect r = bounds();
        int x = r.x;
        const int y = r.y + (r.h - 16) / 2;
        for (int i = 0; i < fig_.steps; ++i, x += 11 + 2) {
            const bool on = i < int(fig_.hits.size()) && fig_.hits[size_t(i)];
            const bool hi = i < int(fig_.accents.size()) && fig_.accents[size_t(i)];
            p.fill({x, y, 11, 16}, hi ? colors::kBlack : on ? gemui::rgb(0x7A, 0x7A, 0x7A) : colors::kWhite);
            p.frame({x, y, 11, 16}, 2, colors::kBlack);
        }
    }

private:
    ymseq::EuclidFigure fig_;
};

}  // namespace

std::unique_ptr<gemui::Widget> makeSliderPopup(SliderPopupSpec spec, const Rect& anchor, int deskW, int deskH) {
    return std::make_unique<VolPopup>(std::move(spec), anchor, deskW, deskH);
}

std::unique_ptr<gemui::Widget> makeVolumePopup(int value, std::function<void(int)> set, const Rect& anchor, int deskW,
                                               int deskH) {
    SliderPopupSpec spec;
    spec.value = value;
    spec.set = std::move(set);
    return makeSliderPopup(std::move(spec), anchor, deskW, deskH);
}

// ------------------------------------------------------------------------------------------- volume, gate, pan

void SeqWindow::openVol(const Rect& cell, int s) {
    // openVolMenu: the value applies live (the undo step armed by the press is taken at the first change, D-282).
    desk().closePopups();
    const int v = std::clamp<int>(lane().vol[size_t(s)], 0, 15);
    auto set = [this, s, at = stepTarget()](int nv) {
        if (!stillOn(at)) return;
        editLane([&](ymseq::LaneEditor&, ymseq::Lane& l) {
            l.vol[size_t(s)] = uint8_t(nv);
            return true;
        });
    };
    desk().openPopup(makeVolumePopup(v, set, cell, desk().width(), desk().height()));
}

// gatPopup: gate of step s in % (0 = the channel's gate, also Channel); the change takes the undo step armed by the
// press on the GAT cell.
void SeqWindow::openGate(const Rect& cell, int s) {
    desk().closePopups();
    const int cur0 = lane().gat[size_t(s)];
    ValuePopup::Spec spec;
    spec.title = "Gate";
    spec.min = 0;
    spec.max = 100;
    spec.value = cur0;
    spec.text = cur0 ? std::to_string(cur0) : "";
    spec.textW = 36;
    spec.suffix = "%";
    spec.chanLabel = "Channel";
    spec.apply = [this, s, at = stepTarget()](bool chan, const std::string& t) {
        if (!stillOn(at)) return;
        const int v = chan ? 0 : std::clamp(parseIntJs(t).value_or(0), 0, 100);
        editLane([&](ymseq::LaneEditor&, ymseq::Lane& l) {
            l.gat[size_t(s)] = uint8_t(v);
            return true;
        });
    };
    desk().openPopup(std::make_unique<ValuePopup>(std::move(spec), cell, desk().width()));
}

// panPopup: pan of step s from -10 to +10, stored as v·10 + 101 (0 = the channel's pan, also Center), so that a typed
// 0 is an explicit centre.
void SeqWindow::openPan(const Rect& cell, int s) {
    desk().closePopups();
    const int stored = lane().pnr[size_t(s)];
    const int cur0 = stored ? int(std::lround((stored - ymseq::kPanCenter) / 10.0)) : 0;
    ValuePopup::Spec spec;
    spec.title = "Pan";
    spec.min = -10;
    spec.max = 10;
    spec.value = cur0;
    spec.text = stored ? std::to_string(cur0) : "";
    spec.textW = 42;
    spec.chanLabel = "Center";
    spec.apply = [this, s, at = stepTarget()](bool chan, const std::string& t) {
        if (!stillOn(at)) return;
        const int v = chan ? 0 : std::clamp(parseIntJs(t).value_or(0), -10, 10) * 10 + ymseq::kPanCenter;
        editLane([&](ymseq::LaneEditor&, ymseq::Lane& l) {
            l.pnr[size_t(s)] = uint8_t(v);
            return true;
        });
    };
    desk().openPopup(std::make_unique<ValuePopup>(std::move(spec), cell, desk().width()));
}

void SeqWindow::openCmdValue(Rect cell, int s, ymengine::SeqCmd k) {
    // Rows of their own of the eleventh and twelfth DAW tests: DLY (Dxx, a tick of the line from 1, item 139, D-348),
    // CUT (Cxx, a tick of the line from 0, item 140, D-349), the restart tick of ENV (Rxx, from 0, item 146, D-355),
    // SMP (Oxx, item 152, D-361) and TUN (Fxx in signed cents, item 153, D-362). The slider of the ticks spans the
    // line at the current tempo; a tick typed beyond it is kept (the engine ignores it while the line is shorter).
    // None takes the command out of the step.
    using ymengine::SeqCmd;
    desk().closePopups();
    const uint16_t cur0 = lane().cmdAt(k, s);
    ValuePopup::Spec spec;
    int start = 0, shift = 0;  // value to start from; stored value = shown value + shift
    int hi = 255;              // highest value typed
    switch (k) {
    case SeqCmd::Offset:
        spec.title = "Sample offset";
        spec.titleTip = "The DMA sample of this step starts this far in, in steps of 256 frames";
        spec.min = 0;
        spec.max = 255;
        break;
    case SeqCmd::Fine:
        spec.title = "Fine tune";
        spec.titleTip = "Tuning of the track in cents from this step on, until the next TUN";
        spec.min = -kFineShown;
        spec.max = kFineShown;
        shift = ymengine::kFineCmdCenter;
        hi = kFineShown;
        break;
    default: {
        spec.title = k == SeqCmd::Delay ? "Note delay" : k == SeqCmd::Cut ? "Note cut" : "Envelope restart";
        spec.titleTip = k == SeqCmd::Delay ? "The notes of this step start at this tick of the line"
                        : k == SeqCmd::Cut ? "The notes of this step stop at this tick of the line (0: at once)"
                                           : "The envelope restarts at this tick of the line (0: at once)";
        spec.min = k == SeqCmd::Delay ? 1 : 0;
        spec.max = std::clamp(lineTicks() - 1, spec.min, 255);
        start = lineTicks() / 2;
        break;
    }
    }
    const int shown = cur0 ? int(ymengine::seqCmdParam(cur0)) - shift : start;
    spec.value = std::clamp(shown, spec.min, spec.max);
    spec.text = std::to_string(cur0 ? std::clamp(shown, spec.min, hi) : spec.value);
    spec.textW = 36;
    spec.rangeTip = "Drag to choose the value";
    spec.textTip = "Type a value";
    spec.chanLabel = "None";
    spec.chanTip = "Take this command out of the step";
    spec.setTip = "Write this command into the step";
    spec.fallback = spec.min;
    spec.apply = [this, s, k, lo = spec.min, hi, shift, at = stepTarget()](bool none, const std::string& t) {
        if (!stillOn(at)) return;
        const auto v = parseIntJs(t);
        editLane([&](ymseq::LaneEditor&, ymseq::Lane& l) {
            if (none || !v) l.clearCmd(k, s);
            else l.setCmd(s, ymengine::seqCmd(k, uint8_t(std::clamp(*v, lo, hi) + shift)));
            return true;
        });
    };
    desk().openPopup(std::make_unique<ValuePopup>(std::move(spec), cell, desk().width()));
}

void SeqWindow::openBpm(const Rect& cell, int s) {
    // BPM row of the pattern (Txx from 32, item 133, D-342; shared by the tracks, item 150, D-359).
    desk().closePopups();
    const int cur0 = proj().cur().bpm[size_t(s)];
    ValuePopup::Spec spec;
    spec.title = "BPM";
    spec.titleTip = "Tempo of every track from this step on, until the next BPM (standalone application, BPM timing)";
    spec.min = ymengine::kTempoCmdMin;
    spec.max = 255;
    spec.value = std::clamp(cur0 ? cur0 : int(std::lround(m_.pv(plugin::Global::FreeTempo))), spec.min, spec.max);
    spec.text = std::to_string(cur0 ? cur0 : spec.value);
    spec.textW = 36;
    spec.rangeTip = "Drag to choose the value";
    spec.textTip = "Type a value";
    spec.chanLabel = "None";
    spec.chanTip = "Take this tempo out of the step";
    spec.setTip = "Write this tempo into the step, for every track";
    spec.fallback = spec.min;
    spec.apply = [this, s, p0 = proj().current](bool none, const std::string& t) {
        if (proj().current != p0) return;  // the row of every track: only the pattern counts
        const auto v = parseIntJs(t);
        setBpm(s, none || !v ? 0 : std::clamp(*v, ymengine::kTempoCmdMin, 255));
    };
    desk().openPopup(std::make_unique<ValuePopup>(std::move(spec), cell, desk().width()));
}

void SeqWindow::setBpm(int s, int bpm) {
    // Like editLane: one undo step, taken at the first change of the gesture.
    std::optional<UndoEntry> armed;
    if (undoArmed_) armed = patternEntry(proj().current);
    edit([s, bpm](plugin::NonParamState& st) { st.seq.cur().bpm[size_t(s)] = uint8_t(bpm); });
    if (armed && !(*armed == patternEntry(proj().current))) {
        ses().undo.push(std::move(*armed));
        undoArmed_ = false;
    }
}

void SeqWindow::openTrackLength() {
    // plenBtn.onclick: own length of the track (polyrhythm), 1 to 64 steps; Global gives it back to the pattern.
    desk().closePopups();
    const int t = cur();
    const int plen = proj().tracks[size_t(t)].length;
    ValuePopup::Spec spec;
    spec.title = "Track length";
    spec.titleTip = "Give this track its own length: it loops on its own while the others keep going, which is how "
                    "polymeters are made";
    spec.min = 1;
    // The length the track plays, its own or the pattern's (twelfth DAW test, item 156: the field was empty when the
    // track followed the pattern); a pattern longer than 64 steps widens the range so that Set keeps it.
    spec.max = std::max(64, steps());
    spec.value = plen ? plen : steps();
    spec.text = std::to_string(spec.value);
    spec.textW = 36;
    spec.rangeTip = "Steps before this track loops back";
    spec.textTip = "Type a length";
    spec.chanLabel = "Global";
    spec.chanTip = "Follow the pattern length again";
    spec.setTip = "Apply this length to the track";
    spec.fallback = 1;
    spec.apply = [this, t](bool chan, const std::string& txt) {
        const auto v = parseIntJs(txt);
        const int n = chan ? 0 : std::clamp(v && *v ? *v : steps(), 1, std::max(64, steps()));
        edit([&](plugin::NonParamState& s) { s.seq.tracks[size_t(t)].length = n; });
    };
    desk().openPopup(std::make_unique<ValuePopup>(std::move(spec), plen_.bounds(), desk().width()));
}

// ------------------------------------------------------------------------------------------- Euclidean generator

namespace {

// Euclidean generator box (euclidBtn.onclick). Its controls are public: openEuclid wires and places them. onDestroy
// runs when the desktop destroys the box, so that SeqWindow::euclidSync_, which points into it, is cleared.
class EuclidBox : public PopupBox {
public:
    EuclidBox(SeqWindow& w, std::function<void(EuclidBox*)> onDestroy) : onDestroy_(std::move(onDestroy)) {
        (void)w;
    }
    ~EuclidBox() override {
        if (onDestroy_) onDestroy_(this);
    }

    Text title{"Euclidean generator", 16, colors::kBlack, true};
    Text name{"", 12, colors::kHint};
    std::array<Text, 4> labels{Text{"Steps", 16}, Text{"Pulses", 16}, Text{"Rotate", 16}, Text{"Accents", 16}};
    std::array<Slider, 4> sliders;
    std::array<gemui::ValueField, 4> values;
    EuclidPreview preview;
    Text target{"", 16};
    Button inst, merge{"Merge"}, rhythms{"Rhythms"}, apply{"Apply"};

    // State of the box (variables of euclidBtn.onclick).
    int instIndex = 0;           // percussion: drum (0-7) or Digidrum slot (0: the channel's, 1-8)
    int melRow = 0, refRow = 0;  // melodic: relative row written, and the cursor's row that centres the note menu
    bool pinned = false, mergeOn = false;  // pinned: note chosen in the menu, it no longer follows the cursor

private:
    std::function<void(EuclidBox*)> onDestroy_;
};

}  // namespace

void SeqWindow::openEuclid() {
    desk().closePopups();
    const int gen = generator();
    const bool digid = gen == int(ymengine::Generator::Digidrum);
    const bool mel = !drum();
    const int track0 = cur();
    const int N0 = length();
    const int smax = std::min(ymseq::kEuclidMaxSteps, N0);  // longest figure: the track's length at most
    auto boxPtr = std::make_unique<EuclidBox>(*this, [this](EuclidBox*) { euclidSync_ = nullptr; });
    EuclidBox& b = *boxPtr;
    b.melRow = b.refRow = viewSemi() + 12 - ses().curRow;  // cursor position: centre of the list
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&b.title, &b.name, &b.preview, &b.target, &b.inst,
                                                                  &b.merge, &b.rhythms, &b.apply})
        b.add(*w);
    static const std::array<const char*, 4> tips = {
        "Length of the figure before it repeats", "Number of hits, spread as evenly as possible",
        "Shift the whole figure to the right", "A second euclidean figure raises the volume of those hits"};
    static const std::array<const char*, 4> labelTips = {
        "Length of the figure before it repeats. Shorter than the track gives a polyrhythm.",
        "Number of hits, spread as evenly as possible", "Shift the whole figure to the right",
        "A second euclidean figure raises the volume of those hits"};
    const int cols = int(std::to_string(std::max(1, smax)).size());  // bound of the pattern, not the current value
    for (size_t i = 0; i < 4; ++i) {
        b.add(b.labels[i]);
        b.add(b.sliders[i]);
        b.add(b.values[i]);
        b.labels[i].setTip(labelTips[i]);
        b.sliders[i].setTip(tips[i]);
        b.values[i].setMinChars(cols);
        b.values[i].setMask(std::string(size_t(cols), '9'));
    }
    // Steps, pulses, rotation, accents; first figure: 4 hits over 16 steps (or the track's length if shorter).
    Slider &es = b.sliders[0], &ep = b.sliders[1], &er = b.sliders[2], &ea = b.sliders[3];
    es.setRange(ymseq::kEuclidMinSteps, std::max(ymseq::kEuclidMinSteps, smax), 1);
    es.setValue(std::min(16, N0));
    ep.setRange(1, std::max(1, smax), 1);
    ep.setValue(4);
    er.setRange(0, 31, 1);
    er.setValue(0);
    ea.setRange(0, 32, 1);
    ea.setValue(0);
    b.inst.setTip(mel ? "Pitch written by the generator" : digid ? "Digidrum slot to trigger" : "Drum to trigger");
    b.merge.setTip("Merge: keep what is already written instead of clearing the track");
    b.rhythms.setTip("Pick a known rhythm: its steps and pulses");
    b.apply.setTip("Write the figure into the track");
    b.target.setText(mel ? "Note" : digid ? "Slot" : "Instrument");

    // The lambdas below capture the box by pointer: they belong to its own controls, or to euclidSync_, which the
    // box clears when it is destroyed.
    EuclidBox* bp = &b;
    auto instLabel = [this, bp, mel, digid] {
        bp->inst.setText(mel ? absName(bp->melRow + (trackOct() + 1) * 12)
                         : digid ? (bp->instIndex ? "Slot " + std::to_string(bp->instIndex) : "Chan")
                                 : std::string(ymseq::kDrumFullNames[size_t(bp->instIndex)]));
        const Box ib = bp->inst.box();  // the button follows its label
        if (ib.h > 0) bp->inst.setBox({ib.x, ib.y, bp->inst.naturalWidth(), ib.h});
    };
    auto figure = [bp] {
        return ymseq::euclidFigure(int(bp->sliders[0].value()), int(bp->sliders[1].value()),
                                   int(bp->sliders[2].value()), int(bp->sliders[3].value()));
    };
    auto draw = [bp, figure] {
        Slider &es2 = bp->sliders[0], &ep2 = bp->sliders[1], &er2 = bp->sliders[2], &ea2 = bp->sliders[3];
        const int S0 = int(es2.value());
        ep2.setRange(1, S0, 1);  // no more hits than steps
        if (ep2.value() > S0) ep2.setValue(S0);
        er2.setRange(0, std::max(0, S0 - 1), 1);
        if (er2.value() > er2.max()) er2.setValue(er2.max());
        ea2.setRange(0, ep2.value(), 1);  // no more accents than hits
        if (ea2.value() > ea2.max()) ea2.setValue(ea2.max());
        const ymseq::EuclidFigure f = figure();
        bp->values[0].setText(std::to_string(f.steps));
        bp->values[1].setText(std::to_string(f.pulses));
        bp->values[2].setText(std::to_string(int(er2.value())));
        bp->values[3].setText(std::to_string(int(ea2.value())));
        const std::string_view nm = ymseq::euclidName(f.pulses, f.steps);
        bp->name.setText("E(" + std::to_string(f.pulses) + "," + std::to_string(f.steps) + ")" +
                         (nm.empty() ? "" : " - " + std::string(nm)));
        bp->preview.set(f);
    };
    instLabel();
    draw();
    if (mel)
        euclidSync_ = [this, bp, instLabel] {  // wheel or slider: the note follows
            const int r = viewSemi() + 12 - ses().curRow;
            bp->refRow = r;
            if (!bp->pinned) {
                bp->melRow = r;
                instLabel();
            }
        };
    for (size_t i = 0; i < 4; ++i) {
        b.sliders[i].onChange = [draw](double) { draw(); };
        b.values[i].onCommit = [bp, i, draw](const std::string& txt) {  // direct entry of the value
            const auto n = parseIntJs(txt);
            if (!n) return false;
            Slider& sl = bp->sliders[i];
            sl.setValue(std::clamp(double(*n), sl.min(), sl.max()));
            draw();
            return true;
        };
    }
    b.inst.onClick = [this, bp, mel, digid, instLabel] {
        std::vector<gemui::MenuItem> items;
        int checked = -1;
        if (mel) {  // the grid cursor, plus one octave on either side
            const int base = (trackOct() + 1) * 12;
            for (int n = bp->refRow + 12; n >= bp->refRow - 12; --n) {
                const int abs = n + base;
                if (abs < ymseq::kPlayLow || abs > ymseq::kMapHigh) continue;  // out of the chip's range
                if (n == bp->melRow) checked = int(items.size());
                gemui::MenuItem m;
                m.label = absName(abs);
                m.pick = [bp, n, instLabel] {
                    bp->melRow = n;
                    bp->pinned = true;  // explicit choice: no longer follows
                    instLabel();
                };
                items.push_back(std::move(m));
            }
        } else {
            for (int k = 0; k < (digid ? 9 : 8); ++k) {
                gemui::MenuItem m;
                m.label = digid ? (k ? "Slot " + std::to_string(k) : "Chan")
                                : std::string(ymseq::kDrumFullNames[size_t(k)]);
                m.pick = [bp, k, instLabel] {
                    bp->instIndex = k;
                    instLabel();
                };
                items.push_back(std::move(m));
            }
            checked = bp->instIndex;
        }
        // The menu opens over the box, which stays open (item 154: it closed the box, and the pick was lost).
        gemui::MenuList::openOver(desk(), std::move(items), checked, bp->inst.box());
    };
    b.merge.onClick = [bp] {
        bp->mergeOn = !bp->mergeOn;
        bp->merge.setOn(bp->mergeOn);
    };
    b.rhythms.onClick = [this, bp, smax, draw] {
        // Known rhythms (twelfth DAW test, item 157), by length then pulses; one longer than the track is greyed.
        std::vector<ymseq::EuclidRhythm> list(ymseq::kEuclidRhythms.begin(), ymseq::kEuclidRhythms.end());
        std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b2) {
            return a.steps != b2.steps ? a.steps < b2.steps : a.pulses < b2.pulses;
        });
        std::vector<gemui::MenuItem> items;
        int checked = -1;
        for (const ymseq::EuclidRhythm& r : list) {
            gemui::MenuItem m;
            m.label = "E(" + std::to_string(r.pulses) + "," + std::to_string(r.steps) + ") " + std::string(r.name);
            if (r.steps > smax) {
                m.disabled = true;
                m.why = "Longer than the track (" + std::to_string(smax) + " steps)";
            } else {
                m.pick = [bp, r, draw] {
                    bp->sliders[0].setValue(r.steps);
                    draw();  // the ranges of the pulses, the rotation and the accents follow the steps
                    bp->sliders[1].setValue(r.pulses);
                    draw();
                };
            }
            if (r.steps == int(bp->sliders[0].value()) && r.pulses == int(bp->sliders[1].value()))
                checked = int(items.size());
            items.push_back(std::move(m));
        }
        gemui::MenuList::openOver(desk(), std::move(items), checked, bp->rhythms.box());
    };
    // The box was built for the kind of track focused when it opened (note or instrument, its rows): another track
    // focused since (automation, MIDI), before Apply or under its confirmation, is not written with them.
    auto sameTrack = [this, mel, digid, track0] {
        return cur() == track0 && !drum() == mel && (generator() == int(ymengine::Generator::Digidrum)) == digid;
    };
    b.apply.onClick = [this, bp, figure, mel, digid, sameTrack] {
        if (!sameTrack()) return;
        const ymseq::EuclidFigure f = figure();
        const ymseq::EuclidTarget target = mel     ? ymseq::EuclidTarget::Melodic
                                           : digid ? ymseq::EuclidTarget::Digidrum
                                                   : ymseq::EuclidTarget::Chipdrum;
        const int value = mel ? bp->melRow : bp->instIndex;
        const bool merge = bp->mergeOn;
        auto write = [this, f, target, value, merge, sameTrack] {
            if (!sameTrack()) return;
            pushUndo();
            editLane([&](ymseq::LaneEditor& ed, ymseq::Lane& l) {
                ymseq::euclidApply(ed, l, f, target, value, merge);
                return true;
            });
            armChan();
        };
        // Notes already written that the figure would take away or change: asked first (twelfth DAW test, item 155).
        if (!ymseq::euclidReplacesNotes(lane(), context(), f, target, value, merge)) {
            write();
            return;
        }
        const std::string& n = st().trackNames[size_t(cur())];
        gemui::Alert::Spec spec;
        spec.lines = {{"Write the figure into track " + std::to_string(cur() + 1) + (n.empty() ? "" : " \"" + n + "\"") +
                       "?"},
                      {merge ? "The notes on its hits will be replaced." : "The notes already written will be lost."}};
        spec.confirmButtons("Apply");
        spec.overPopups = true;  // the generator stays open
        gemui::Alert::open(desk(), std::move(spec), [write](int button, const std::string&) {
            if (button == gemui::Alert::kConfirmOk) write();
        });
    };

    // Layout: padding 10, width max(278, SMAX × 13); grid 58 px | slider | value, gaps 5 and 8. 13 px per step: an
    // 11 px cell of the preview and its 2 px gap. The box opens under the Euclid button, kept on the desktop.
    const double lh = lineH(16);
    const double EW = std::max(278, smax * 13);
    const Rect r = euclid_.bounds();
    const double x = std::max(4.0, std::min(double(r.x), double(desk().width() - 280)));
    double y = std::max(4.0, std::min(double(r.bottom() + 2), double(desk().height() - 320)));
    const double x0 = x + 12;
    double y0 = y + 12;
    b.title.setBox({x0, y0, b.title.naturalWidth(), lh});
    y0 += lh + 4;  // margins of 8 and -4 merged
    b.name.setBox({x0, y0, EW, 13});
    y0 += 13 + 6;
    const double valW = b.values[0].naturalWidth();
    for (size_t i = 0; i < 4; ++i) {
        b.labels[i].setBox({x0, y0, 58, lh});
        b.sliders[i].setBox({x0 + 58 + 8, y0 + (lh - 16) / 2, EW - 58 - 16 - valW, 16});
        b.values[i].setBox({x0 + EW - valW, y0, valW, lh});
        y0 += lh + (i < 3 ? 5 : 0);
    }
    y0 += 9;
    b.preview.setBox({x0, y0, EW, 18});
    y0 += 18 + 8;
    const double ih = b.inst.naturalHeight();
    const double rowH = std::max(lh, ih);
    b.target.setBox({x0, y0 + (rowH - lh) / 2, b.target.naturalWidth(), lh});
    b.inst.setBox({x0 + b.target.naturalWidth() + Font::advance(' ', 16), y0 + (rowH - ih) / 2,
                   std::max(b.inst.naturalWidth(), 20.0), ih});
    y0 += rowH + 10;
    const double bh = b.apply.naturalHeight();
    b.merge.setBox({x0, y0, b.merge.naturalWidth(), bh});
    b.rhythms.setBox({x0 + b.merge.naturalWidth() + Font::advance(' ', 16), y0, b.rhythms.naturalWidth(), bh});
    b.apply.setBox({x0 + EW - b.apply.naturalWidth(), y0, b.apply.naturalWidth(), bh});
    y0 += bh;
    b.setBox({x, y, EW + 24, y0 + 12 - y});
    desk().openPopup(std::move(boxPtr));
}

}  // namespace stym::pluginui
