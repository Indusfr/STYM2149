// STYM2149 — pluginui: Mixer window (strips, faders, VU meters, pan, mute, solo, outputs)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Behaviours taken from the prototype functions quoted in comments (buildMixer, refreshMixer, refreshOutMeter,
// openMixer, closeMixer, drawScopes, drawMeters, vuPct, pushMixUndo).
//
// The faders, pans, sends, M and S are host parameters set through MainWindow (MainWindow::set, bindSlider). Undo: a
// drag or a click pushes one mixer step (UndoEntry::Kind::Mix) before its first change, except the fader of a chip
// channel in 3-channel mode, a setting of the track (Kind::Track, D-260), and the Masters, which have none. The meters
// and oscilloscopes read the engine status, published by the audio thread about 30 times per second.
#include "pluginui/MixerWindow.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "gemui/Alert.h"
#include "gemui/Menu.h"
#include "plugincore/Mapping.h"
#include "pluginui/MainWindow.h"

namespace stym::pluginui {

using gemui::Box;
using gemui::Rect;
using plugin::Global;
using plugin::paramIndex;
using plugin::Track;
namespace colors = gemui::colors;

namespace {

// Strip (.mstrip, flex as a centred column, 4 px gap): label 17, scope 22, fader and VU meter 96, pan 16 (16 channels),
// M 17, S 17.
constexpr double kStripW16 = 60, kStripW3 = 36;  // width of the pan, of the fader + VU meter row
constexpr double kGap = 4;
constexpr double kLabelH = 17, kScopeH = 22, kRowH = 96, kPanH = 16;
constexpr double kPad = 8;                        // .mixbody: padding 8
constexpr double kOutLabelW = 44, kOutGap = 8, kOutMinW = 150;
constexpr double kOutRowH = 16, kOutRowMargin = 6, kVuhH = 11;
constexpr double kValueH = 8;  // value under a fader or a pan, 6×6 font (D-300)

std::string number(double v, bool sign) {
    const long n = std::lround(v);
    return (sign && n > 0 ? "+" : "") + std::to_string(n);
}

}  // namespace

// ------------------------------------------------------------------------------------------- MiniScope

MiniScope::MiniScope() { setPoints({}); }

void MiniScope::setPoints(std::span<const float> points) {
    // drawScopes: 26 points taken every 128/26 points, y = H/2 - v × H × 0.85. Without anti-aliasing (STYM2149_SPEC.md
    // §6.1): column x carries the segment from x to x + 1 (1 px canvas stroke, flat ends), from the row of one to that
    // of the other.
    canvas_.clear(colors::kWhite);
    const size_t n = points.size();
    auto row = [&](int x) {
        const double v = n ? double(points[size_t(x) * n / kW]) : 0.0;
        return int(std::floor(std::clamp(kH / 2.0 - v * kH * 0.85, -1.0, double(kH))));
    };
    int y0 = row(0);
    for (int x = 0; x + 1 < kW; ++x) {
        const int y1 = row(x + 1);
        canvas_.fill(x, std::min(y0, y1), 1, std::abs(y1 - y0) + 1, colors::kBlack);
        y0 = y1;
    }
    repaint();
}

void MiniScope::paint(gemui::Painter& p) {
    const Rect r = bounds();
    p.frame(r, 2, colors::kBlack);
    canvas_.blit(p, r.inset(2));
}

void MiniScope::mouseDown(const gemui::MouseEvent& e) { pressed_ = e.button == gemui::MouseButton::Left; }

void MiniScope::mouseUp(const gemui::MouseEvent& e) {
    // Browser click: press and release on the canvas.
    const bool click = pressed_ && bounds().contains(e.pos);
    pressed_ = false;
    if (click && onClick) onClick();
}

// --------------------------------------------------------------------------------------------- VuMeter

void VuMeter::setLevel(double fill, double hold, bool clip) {
    if (fill == fill_ && hold == hold_ && clip == clip_) return;
    fill_ = fill;
    hold_ = hold;
    clip_ = clip;
    repaint();
}

void VuMeter::paint(gemui::Painter& p) {
    const Rect r = bounds();
    p.fill(r, colors::kWhite);
    p.frame(r, 2, colors::kBlack);
    // Fill and peak in % of the inner box (position:absolute, overflow:hidden).
    const Box b = box();
    const Box in{b.x + 2, b.y + 2, b.w - 4, b.h - 4};
    const Rect clipR = r.inset(2);
    const gemui::Color fillC = clip_ ? kClip : colors::kBlack;
    if (horizontal_) {
        const double w = in.w * fill_ / 100;
        p.fill(Box{in.x, in.y, w, in.h}.snapped().intersect(clipR), fillC);
        if (hold_ > 1) p.fill(Box{in.x + in.w * hold_ / 100, in.y, 2, in.h}.snapped().intersect(clipR), colors::kBlack);
    } else {
        const double h = in.h * fill_ / 100;
        p.fill(Box{in.x, in.bottom() - h, in.w, h}.snapped().intersect(clipR), fillC);
        if (hold_ > 1)
            p.fill(Box{in.x, in.bottom() - in.h * hold_ / 100 - 2, in.w, 2}.snapped().intersect(clipR), colors::kBlack);
    }
}

// ------------------------------------------------------------------------------------------- construction

MixerWindow::MixerWindow(MainWindow& main) : m_(main) {
    build();
    wire();
    place({double(kX), double(kY), 1048, 325});
}

void MixerWindow::build() {
    setTitle("MIXER");
    // Prototype ids of the sliders that MIDI learn can learn (D-146): volume and pan.
    for (size_t i = 0; i < strips_.size(); ++i) {
        strips_[i].fader.setId("mixFad" + std::to_string(i));
        strips_[i].pan.setId("mixPan" + std::to_string(i));
    }
    auto style = [this](Strip& s) {
        // .mstrip .btn: 11 px, margins 1 px / 4 px, 22 px at least.
        for (gemui::Button* b : {&s.label, &s.mute, &s.solo}) {
            b->setPx(11);
            b->setPadding(1, 4);
            b->setMinWidth(22);
        }
        // Label: name of the track in a 36 px strip in 3-channel mode ("Digi"): 6×6 font (D-071).
        s.label.setFace(gemui::Face::kSmall);
        s.fader.setVertical(true);
        s.fader.setRange(0, 100, 1);
        s.pan.setRange(-100, 100, 1);
        s.pan.setTip("Pan");
        s.send.setArrows(false);  // like the pan (D-297)
        s.send.setRange(0, 100, 1);
        s.send.setTip("Send to the delay, after the fader (Edit > Effects)");
        for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&s.label, &s.scope, &s.fader, &s.vu, &s.pan,
                                                                      &s.send, &s.faderVal, &s.panVal, &s.sendVal,
                                                                      &s.mute, &s.solo})
            add(*w);
    };
    for (Strip& s : strips_) style(s);
    master_.setVertical(true);
    master_.setRange(0, 100, 1);
    master_.setTip("Master output level");
    // Two Masters (seventh DAW test, item 83, D-284): second fader, balance and Link, styled like the strips'.
    master2_.setVertical(true);
    master2_.setRange(0, 100, 1);
    masterPan_.setRange(-100, 100, 1);
    masterPan_.setTip("Balance of the outputs (double click: centre)");
    link_.setPx(11);
    link_.setPadding(1, 4);
    link_.setMinWidth(22);
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&masterSep_, &masterLab_, &master_, &master1Lab_,
                                                                  &master2Lab_, &master2_, &masterPan_, &masterVal_,
                                                                  &master2Val_, &masterPanVal_, &link_})
        add(*w);
    // Values (D-300): 6×6 font, centred under their slider.
    std::vector<gemui::Label*> values = {&masterVal_, &master2Val_, &masterPanVal_};
    for (Strip& s : strips_) values.insert(values.end(), {&s.faderVal, &s.panVal, &s.sendVal});
    for (gemui::Label* l : values) {
        l->setPx(int(kValueH));
        l->setFace(gemui::Face::kSmall);
        l->setAlign(gemui::Label::Align::Center);
    }
    add(outLab_);
    add(outRLab_);
    add(outL_);
    add(outR_);
}

void MixerWindow::wire() {
    onClose = [this] { m_.syncWindowButtons(); };  // closeMixer
    for (int i = 0; i < plugin::kTracks; ++i) {
        Strip& s = strips_[size_t(i)];
        s.label.onClick = [this, i] { m_.focusTrack(i); };  // focusCh
        s.scope.onClick = [this, i] { m_.openScope(i); };
        // One undo step at the first move of each drag (fArm, pArm). A chip channel in 3 channels: its Volume. The
        // gesture start arms the step, then calls the one of bindSlider (which marks the slider as held).
        m_.bindSlider(s.fader, [this, i](double v) {
            Strip& st = strips_[size_t(i)];
            const bool volume = !panned(i);
            if (st.faderArmed) {
                if (volume) pushTrackUndo(i);
                else pushUndo();
                st.faderArmed = false;
            }
            m_.set(paramIndex(i, volume ? Track::Volume : Track::Level), float(v));
        });
        s.fader.onGestureStart = [this, i, start = s.fader.onGestureStart] {
            strips_[size_t(i)].faderArmed = true;
            start();
        };
        m_.bindSlider(s.pan, [this, i](double v) {
            Strip& st = strips_[size_t(i)];
            if (st.panArmed) {
                pushUndo();
                st.panArmed = false;
            }
            m_.set(paramIndex(i, Track::Pan), float(v));
        });
        s.pan.onGestureStart = [this, i, start = s.pan.onGestureStart] {
            strips_[size_t(i)].panArmed = true;
            start();
        };
        // Double click: pan to the centre. The prototype did not undo it; here, one undo step (D-128).
        s.pan.onDoubleClick = [this, i] {
            if (m_.iv(i, Track::Pan) == 0) return;
            pushUndo();
            m_.set(paramIndex(i, Track::Pan), 0.0f);
        };
        m_.bindSlider(s.send, [this, i](double v) {
            Strip& st = strips_[size_t(i)];
            if (st.sendArmed) {
                pushUndo();
                st.sendArmed = false;
            }
            m_.set(paramIndex(i, Track::Send), float(v));
        });
        s.send.onGestureStart = [this, i, start = s.send.onGestureStart] {
            strips_[size_t(i)].sendArmed = true;
            start();
        };
        s.mute.onClick = [this, i] { toggle(i, Track::Mute); };
        s.solo.onClick = [this, i] { toggle(i, Track::Solo); };
        // Right click (D-300): typed value or default, one undo step like a drag.
        s.fader.onContext = [this, i](gemui::Point at) {
            sliderMenu(
                strips_[size_t(i)].fader, at, "Volume",
                [this, i] { return paramIndex(i, panned(i) ? Track::Level : Track::Volume); },
                [this, i](double v) {
                    const bool volume = !panned(i);
                    if (volume) pushTrackUndo(i);
                    else pushUndo();
                    m_.set(paramIndex(i, volume ? Track::Volume : Track::Level), float(v));
                });
        };
        s.pan.onContext = [this, i](gemui::Point at) {
            sliderMenu(
                strips_[size_t(i)].pan, at, "Pan", [i] { return paramIndex(i, Track::Pan); },
                [this, i](double v) {
                    pushUndo();
                    m_.set(paramIndex(i, Track::Pan), float(v));
                });
        };
        s.send.onContext = [this, i](gemui::Point at) {
            sliderMenu(
                strips_[size_t(i)].send, at, "Send", [i] { return paramIndex(i, Track::Send); },
                [this, i](double v) {
                    pushUndo();
                    m_.set(paramIndex(i, Track::Send), float(v));
                });
        };
    }
    // Master: the level of the SYSTEM frame, without an undo step like it; with two Masters, the left output's or chip
    // 1's, the second fader beside it, the balance and Link (D-284), without an undo step either.
    m_.bindSlider(master_, [this](double v) { setMaster(false, v); });
    m_.bindSlider(master2_, [this](double v) { setMaster(true, v); });
    m_.bindSlider(masterPan_, [this](double v) { m_.set(paramIndex(Global::MasterPan), float(v)); });
    masterPan_.onDoubleClick = [this] { m_.set(paramIndex(Global::MasterPan), 0.0f); };
    master_.onContext = [this](gemui::Point at) {
        sliderMenu(
            master_, at, "Master", [] { return paramIndex(Global::Master); },
            [this](double v) { setMaster(false, v); });
    };
    master2_.onContext = [this](gemui::Point at) {
        sliderMenu(
            master2_, at, "Master",
            [this] { return paramIndex(m_.iv(Global::MasterLink) != 0 ? Global::Master : Global::Master2); },
            [this](double v) { setMaster(true, v); });
    };
    masterPan_.onContext = [this](gemui::Point at) {
        sliderMenu(
            masterPan_, at, "Balance", [] { return paramIndex(Global::MasterPan); },
            [this](double v) { m_.set(paramIndex(Global::MasterPan), float(v)); });
    };
    link_.onClick = [this] {
        // Unlinked, the second Master keeps its level (that of Master); linked again, it follows Master.
        const bool on = m_.iv(Global::MasterLink) != 0;
        if (on) m_.set(paramIndex(Global::Master2), float(m_.iv(Global::Master)));
        m_.set(paramIndex(Global::MasterLink), on ? 0.0f : 1.0f);
    };
}

void MixerWindow::setMaster(bool right, double v) {
    // Linked, either fader sets Master, which the engine plays on both sides (both chips).
    const bool apart = right && m_.iv(Global::MasterLink) == 0;
    m_.set(paramIndex(apart ? Global::Master2 : Global::Master), float(v));
}

// The menu's entries keep a reference to the slider: the sliders are members of the window, which outlives its menus.
void MixerWindow::sliderMenu(gemui::Slider& s, gemui::Point at, std::string name, std::function<int()> param,
                             std::function<void(double)> apply) {
    std::vector<gemui::MenuItem> items(2);
    items[0].label = "Value...";
    items[0].pick = [this, &s, name, apply] { typeValue(s, name, apply); };
    items[1].label = "Reset";
    items[1].pick = [&s, param, apply] {
        const double def = plugin::paramSpec(param()).def;
        if (def != s.value()) apply(def);
    };
    gemui::MenuList::open(m_.desk_, std::move(items), -1, gemui::Box{double(at.x), double(at.y), 0, 0}, false);
}

void MixerWindow::typeValue(gemui::Slider& s, const std::string& name, std::function<void(double)> apply) {
    gemui::Alert::Spec spec;
    spec.lines = {{name + " (" + number(s.min(), false) + " to " + number(s.max(), false) + "):"}};
    spec.promptButtons();
    spec.inputText = number(s.value(), false);
    spec.inputChars = 6;
    spec.inputMax = 4;
    gemui::Alert::open(m_.desk_, std::move(spec), [&s, apply](int button, const std::string& text) {
        if (button != gemui::Alert::kPromptOk) return;
        // Nothing typed: nothing changes. Out of range: clamped.
        const auto typed = gemui::typedNumber(text);
        if (!typed) return;
        const double v = std::clamp(std::round(*typed), s.min(), s.max());
        if (v != s.value()) apply(v);
    });
}

// ------------------------------------------------------------------------------------------- state

int MixerWindow::strips() const noexcept { return m_.voices(); }

void MixerWindow::pushUndo() {
    UndoEntry e;
    e.kind = UndoEntry::Kind::Mix;
    m_.host_.session().undo.push(m_.undoEntryLike(e));
}

void MixerWindow::pushTrackUndo(int track) {
    UndoEntry e;
    e.kind = UndoEntry::Kind::Track;
    e.index = track;
    m_.host_.session().undo.push(m_.undoEntryLike(e));
}

void MixerWindow::toggle(int track, Track field) {
    pushUndo();
    m_.set(paramIndex(track, field), m_.iv(track, field) ? 0.0f : 1.0f);
}

// 3 channels: the ST is mono; with the STE, the DMA tracks have their pan and the output is stereo (D-153).
bool MixerWindow::stereoOut() const noexcept { return !m_.mode3() || m_.ste(); }
bool MixerWindow::perChip() const noexcept { return m_.mode3() && m_.chips() == 2; }
bool MixerWindow::twoMasters() const noexcept { return stereoOut() || perChip(); }
bool MixerWindow::panned(int track) const noexcept { return !m_.mode3() || m_.dmaTrack(track); }

void MixerWindow::show() {
    // openMixer: strips rebuilt (held peaks and VU meters at zero), window in front.
    for (Strip& s : strips_) {
        s.hold = 0;
        s.vu.setLevel(0, 0, false);
    }
    outHold_ = {};
    outL_.setLevel(0, 0, false);
    outR_.setLevel(0, 0, false);
    open();
    refresh();
    m_.syncWindowButtons();
}

void MixerWindow::refresh() {
    // refreshMixer, refreshOutMeter.
    if (!isOpen()) return;
    const bool m3 = m_.mode3();
    const int n = strips();
    for (int i = 0; i < plugin::kTracks; ++i) {
        Strip& s = strips_[size_t(i)];
        s.label.setText(m_.trackName(i));
        s.label.setOn(i == m_.cur_);
        // A chip channel in 3 channels (sixth DAW test, item 62, D-260): the fader is its 4-bit Volume, the one of the
        // SOURCE frame, both ways; greyed out with it on a Sync Buzzer (D-261).
        const bool level = panned(i);
        const bool buzzer = plugin::effectiveGenerator(m_.values_, i) == ymengine::Generator::SyncBuzzer;
        s.fader.setRange(0, level ? 100 : 15, 1);
        if (m_.dragging_ != &s.fader) s.fader.setValue(m_.iv(i, level ? Track::Level : Track::Volume));
        if (m_.dragging_ != &s.pan) s.pan.setValue(m_.iv(i, Track::Pan));
        if (m_.dragging_ != &s.send) s.send.setValue(m_.iv(i, Track::Send));
        s.fader.setEnabled(level || !buzzer);
        s.fader.setTip(level    ? "Volume"
                       : buzzer ? "Sync Buzzer plays the hardware envelope alone: no square, no noise, no fixed volume"
                                : "Volume: the channel's 4-bit volume, the same as in the SOURCE frame");
        s.mute.setOn(m_.iv(i, Track::Mute) != 0);
        s.solo.setOn(m_.iv(i, Track::Solo) != 0);
        s.faderVal.setText(number(s.fader.value(), false));
        s.faderVal.setDim(!s.fader.enabled());
        s.panVal.setText("Pan " + number(s.pan.value(), true));  // named like the send (ninth DAW test, D-310)
        s.sendVal.setText("Send " + number(s.send.value(), false));
    }
    if (m_.dragging_ != &master_) master_.setValue(m_.iv(Global::Master));
    const bool link = m_.iv(Global::MasterLink) != 0;
    if (m_.dragging_ != &master2_) master2_.setValue(m_.iv(link ? Global::Master : Global::Master2));
    if (m_.dragging_ != &masterPan_) masterPan_.setValue(m_.iv(Global::MasterPan));
    masterVal_.setText(number(master_.value(), false));
    master2Val_.setText(number(master2_.value(), false));
    masterPanVal_.setText(number(masterPan_.value(), true));
    link_.setOn(link);
    // Labels and tips of the two Masters: chips 1 and 2 in 2x3 channels, otherwise the outputs (D-284).
    const bool chips = perChip();
    master1Lab_.setText(chips ? "1" : "L");
    master2Lab_.setText(chips ? "2" : "R");
    master_.setTip(chips         ? "Master: level of chip 1 (both with Link)"
                   : stereoOut() ? "Master: level of the left output (both with Link)"
                                 : "Master output level");
    master2_.setTip(chips ? "Master: level of chip 2 (both with Link)"
                          : "Master: level of the right output (both with Link)");
    link_.setTip(chips ? "Link: one Master for both chips; off: a level for each"
                       : "Link: one Master for both outputs; off: a level for each");
    outLab_.setText(stereoOut() ? "Out L" : "Out");
    if (m3 != mode3_ || n != shown_) {
        // buildMixer: the number of strips changes with the mode.
        mode3_ = m3;
        for (Strip& s : strips_) {
            s.hold = 0;
            s.vu.setLevel(0, 0, false);
        }
    }
    layout();
}

void MixerWindow::refreshEngineStatus(const plugin::EngineStatus& t) {
    // drawScopes, drawMeters: open window only. The held peak falls by 2.5 % per message at 20 Hz, i.e. 50 % per
    // second: 50/30 % per publication at 30 Hz, counted in publications (a publication missed by the editor still makes
    // it fall). steps: publications since the last one read, at most 30 (one second).
    const uint64_t steps = serial_ ? std::clamp<uint64_t>(t.serial - serial_, 0, 30) : 1;
    serial_ = t.serial;
    if (!isOpen()) return;
    const double fall = 50.0 / 30.0 * double(steps);
    const int n = strips();
    for (int i = 0; i < n; ++i) {
        Strip& s = strips_[size_t(i)];
        const double pk = t.peak[size_t(i)];
        const double p = vuPct(pk);
        s.hold = p > s.hold ? p : std::max(0.0, s.hold - fall);
        s.vu.setLevel(p, s.hold, pk >= 0.999);
        s.scope.setPoints(t.trackScope[size_t(i)]);
    }
    const bool mono = !stereoOut();
    for (int k = 0; k < 2; ++k) {
        // 3 channels: the ST is mono, the louder of the two outputs (except with the STE DMA).
        const double v = mono ? std::max(t.outPeak[0], t.outPeak[1]) : t.outPeak[size_t(k)];
        const double p = vuPct(v);
        outHold_[size_t(k)] = p > outHold_[size_t(k)] ? p : std::max(0.0, outHold_[size_t(k)] - fall);
        (k ? outR_ : outL_).setLevel(p, outHold_[size_t(k)], v >= 0.999);
    }
}

// Peak amplitude (1 = full scale) to the height of a meter: -48 dB (kVuDb) at 0 %, 0 dB and above at 100 %, linear in
// dB; nothing at -80 dB (0.0001) and below.
double MixerWindow::vuPct(double a) noexcept {
    if (!(a > 0.0001)) return 0;
    const double db = 20 * std::log10(std::min(1.0, a));
    return std::clamp((db + kVuDb) / kVuDb * 100, 0.0, 100.0);
}

const gemui::Slider* MixerWindow::slider(std::string_view id) const noexcept {
    if (id == "mixMaster") return &master_;
    if (id == "mixMaster2") return &master2_;
    if (id == "mixMasterPan") return &masterPan_;
    return nullptr;
}

const VuMeter& MixerWindow::meter(int strip) const noexcept {
    if (strip == -1) return outL_;
    if (strip == -2) return outR_;
    return strips_[size_t(std::clamp(strip, 0, plugin::kTracks - 1))].vu;
}

// ------------------------------------------------------------------------------------------- layout

void MixerWindow::layout() {
    const double ox = box().x, oy = box().y;
    const double x0 = ox + 14;  // .mixbody content (border 2, margin 4, padding 8)
    const double top = oy + 32; // title bar (6 + 26)
    const bool m3 = mode3_;
    const bool dma = m3 && m_.ste();  // DMA tracks in 3-channel mode
    const int n = strips();
    shown_ = n;

    // #mixBody: strips side by side (4 px gap), each as wide as its widest item. The DMA tracks have their pan: in
    // 3-channel mode, the YM strips take the same height. Under the fader and under the pan, their values (D-300).
    const double base = m3 ? kStripW3 : kStripW16;
    const bool panRow = !m3 || dma;
    const double panRows = panRow ? kPanH + kValueH + 2 * kGap : 0;
    const double sendRows = m3 ? 0 : kPanH + kValueH + 2 * kGap;  // Send of the delay: 16 channels (D-299)
    // Label, scope, fader row, its value, pan and send rows, M and S, and the 5 gaps between the fixed items.
    const double stripH = kLabelH + kScopeH + kRowH + kValueH + panRows + sendRows + 2 * kLabelH + 5 * kGap;
    const double y0 = top + kPad;
    double sx = x0;
    auto put = [&](Strip& s, bool on, bool pan, double minW) {
        for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&s.label, &s.scope, &s.fader, &s.vu,
                                                                      &s.faderVal, &s.mute, &s.solo})
            w->setVisible(on);
        s.pan.setVisible(on && pan);
        s.panVal.setVisible(on && pan);
        s.send.setVisible(on && !m3);
        s.sendVal.setVisible(on && !m3);
        if (!on) return;
        const double lw = s.label.naturalWidth();
        const double sw = std::max(minW, lw);
        const double c = sx + sw / 2;
        double y = y0;
        s.label.setBox({c - lw / 2, y, lw, kLabelH});
        y += kLabelH + kGap;
        s.scope.setBox({c - 15, y, 30, kScopeH});
        y += kScopeH + kGap;
        // .vrow: 26 px .vwrap (slider rotated by 20 px, 16 px slider track in the middle), gap 3, 7 px .vu: 36 px
        // centred on the strip.
        const double rx = c - 18;
        s.fader.setBox({rx + 3 + 2, y, 16, kRowH});
        s.vu.setBox({rx + 29, y, 7, kRowH});
        y += kRowH + kGap;
        s.faderVal.setBox({c - sw / 2, y, sw, kValueH});
        y += kValueH + kGap;
        if (pan) {
            s.pan.setBox({c - kStripW16 / 2, y, kStripW16, kPanH});
            y += kPanH + kGap;
            s.panVal.setBox({c - kStripW16 / 2, y, kStripW16, kValueH});
            y += kValueH + kGap;
        } else if (panRow) {
            y += panRows;  // YM channel next to the DMA tracks: M and S aligned with theirs
        }
        if (!m3) {
            s.send.setBox({c - kStripW16 / 2, y, kStripW16, kPanH});
            y += kPanH + kGap;
            s.sendVal.setBox({c - kStripW16 / 2, y, kStripW16, kValueH});
            y += kValueH + kGap;
        }
        const double mw = s.mute.naturalWidth(), smw = s.solo.naturalWidth();
        s.mute.setBox({c - mw / 2, y, mw, kLabelH});
        y += kLabelH + kGap;
        s.solo.setBox({c - smw / 2, y, smw, kLabelH});
        sx += sw + kGap;
    };
    for (int i = 0; i < plugin::kTracks; ++i)
        put(strips_[size_t(i)], i < n, panned(i), panned(i) ? kStripW16 : base);
    // Master (sixth DAW test, item 78, D-276): a 2 px rule, then the label and the fader, at the height of the tracks'.
    // Two Masters (D-284): their faders 4 px apart, their letters (L and R, or 1 and 2 for the chips) on the row of
    // the oscilloscopes, the balance (stereo outputs only) and Link on the rows of the pans and of M.
    const bool two = twoMasters();
    for (gemui::Widget* w :
         std::initializer_list<gemui::Widget*>{&master1Lab_, &master2Lab_, &master2_, &master2Val_, &link_})
        w->setVisible(two);
    masterPan_.setVisible(two && !perChip());
    masterPanVal_.setVisible(masterPan_.visible());
    const double sepX = sx + kGap;
    masterSep_.setBox({sepX, y0, 2, stripH});
    const double mlw = masterLab_.naturalWidth();
    const double colW = two ? std::max({mlw, kStripW16, link_.naturalWidth()}) : mlw;
    const double mc = sepX + 2 + kPad + colW / 2;
    const double faderY = y0 + kLabelH + kScopeH + 2 * kGap;
    masterLab_.setBox({mc - mlw / 2, y0, mlw, kLabelH});
    const double valueY = faderY + kRowH + kGap;  // the row of the faders' values (D-300)
    if (two) {
        master_.setBox({mc - 18, faderY, 16, kRowH});
        master2_.setBox({mc + 2, faderY, 16, kRowH});
        masterVal_.setBox({mc - 20, valueY, 20, kValueH});
        master2Val_.setBox({mc, valueY, 20, kValueH});
        const double ly = y0 + kLabelH + kGap + 6, lw = master1Lab_.naturalWidth(), rw = master2Lab_.naturalWidth();
        master1Lab_.setBox({mc - 10 - lw / 2, ly, lw, 11});
        master2Lab_.setBox({mc + 10 - rw / 2, ly, rw, 11});
        const double py = valueY + kValueH + kGap, kw = link_.naturalWidth();
        masterPan_.setBox({mc - kStripW16 / 2, py, kStripW16, kPanH});
        masterPanVal_.setBox({mc - kStripW16 / 2, py + kPanH + kGap, kStripW16, kValueH});
        link_.setBox({mc - kw / 2, py + (panRow ? panRows : 0) + sendRows, kw, kLabelH});  // on the row of M
    } else {
        master_.setBox({mc - 8, faderY, 16, kRowH});
        masterVal_.setBox({mc - colW / 2, valueY, colW, kValueH});
    }
    sx = mc + colW / 2 + kGap;
    const double stripsW = sx - kGap - x0;

    // Outputs (.mixbody as a block): Out L and Out R rows (6 px margins, collapsed between them), 44 px label, flex:1
    // VU meter of 150 px at least, centred on the 16 px row.
    const double contentW = std::max(stripsW, kOutLabelW + kOutGap + kOutMinW);
    const double outTop = top + kPad + stripH + kPad;
    double ry = outTop + kPad + kOutRowMargin;
    const double vuW = contentW - kOutLabelW - kOutGap;
    outLab_.setBox({x0, ry, kOutLabelW, kOutRowH});
    outL_.setBox({x0 + kOutLabelW + kOutGap, ry + (kOutRowH - kVuhH) / 2, vuW, kVuhH});
    const bool stereo = stereoOut();
    outRLab_.setVisible(stereo);
    outR_.setVisible(stereo);
    if (stereo) {
        ry += kOutRowH + kOutRowMargin;
        outRLab_.setBox({x0, ry, kOutLabelW, kOutRowH});
        outR_.setBox({x0 + kOutLabelW + kOutGap, ry + (kOutRowH - kVuhH) / 2, vuW, kVuhH});
    }
    const double outBottom = ry + kOutRowH + kOutRowMargin + kPad;

    // Window: content, padding of 8, border and margin of .gwin (6 on each side); bottom: margin and border (4 + 2).
    const double w = contentW + 2 * kPad + 12;
    const double h = outBottom + 6 - oy;
    if (std::abs(h - box().h) > 0.01 || std::abs(w - box().w) > 0.01) place({ox, oy, w, h});
    repaint();
}

const gemui::Label* MixerWindow::valueLabel(std::string_view id) const {
    if (id == "mixMasterVal") return &masterVal_;
    if (id == "mixMaster2Val") return &master2Val_;
    if (id == "mixMasterPanVal") return &masterPanVal_;
    using Prefix = std::pair<std::string_view, int>;
    for (const auto& [prefix, k] : {Prefix{"mixFadVal", 0}, Prefix{"mixPanVal", 1}, Prefix{"mixSendVal", 2}}) {
        if (id.substr(0, prefix.size()) != prefix) continue;
        const std::string_view num = id.substr(prefix.size());
        const auto digit = [](char c) { return c >= '0' && c <= '9'; };
        if (num.empty() || num.size() > 2 || !std::all_of(num.begin(), num.end(), digit)) return nullptr;
        const int i = std::stoi(std::string(num));
        if (i >= plugin::kTracks) return nullptr;
        const Strip& s = strips_[size_t(i)];
        return k == 2 ? &s.sendVal : k == 1 ? &s.panVal : &s.faderVal;
    }
    return nullptr;
}

std::string MixerWindow::valueText(std::string_view id) const {
    const gemui::Label* v = valueLabel(id);
    return v ? v->text() : std::string();
}

std::optional<Rect> MixerWindow::rectOf(std::string_view id) const {
    if (!isOpen()) return std::nullopt;
    if (id == "mixWin") return bounds();
    if (id == "vuhL") return outL_.bounds();
    if (id == "vuhR") return outR_.visible() ? std::optional<Rect>(outR_.bounds()) : std::nullopt;
    if (id == "mixSep") return masterSep_.bounds();
    if (id == "mixMasterLab") return masterLab_.bounds();
    if (id == "mixMaster") return master_.bounds();
    if (const gemui::Label* v = valueLabel(id)) return v->visible() ? std::optional<Rect>(v->bounds()) : std::nullopt;
    const std::pair<std::string_view, const gemui::Widget*> stereo[] = {{"mixMaster2", &master2_},
                                                                        {"mixMasterPan", &masterPan_},
                                                                        {"mixLink", &link_},
                                                                        {"mixMaster1Lab", &master1Lab_},
                                                                        {"mixMaster2Lab", &master2Lab_}};
    for (const auto& [name, w] : stereo)
        if (id == name) return w->visible() ? std::optional<Rect>(w->bounds()) : std::nullopt;
    if (id == "mixMasterArea") {
        // From the rule to the right edge of the widest Master element, over the height of the rule.
        const Rect sep = masterSep_.bounds();
        int right = std::max(masterLab_.bounds().right(), master_.bounds().right());
        for (const auto& [name, w] : stereo)
            if (w->visible()) right = std::max(right, w->bounds().right());
        return Rect{sep.x, sep.y, right - sep.x, sep.h};
    }
    struct Part {
        std::string_view prefix;
        const gemui::Widget* (*pick)(const Strip&);
    };
    static constexpr Part parts[] = {
        {"mixLab", [](const Strip& s) -> const gemui::Widget* { return &s.label; }},
        {"mixScope", [](const Strip& s) -> const gemui::Widget* { return &s.scope; }},
        {"mixFad", [](const Strip& s) -> const gemui::Widget* { return &s.fader; }},
        {"mixVu", [](const Strip& s) -> const gemui::Widget* { return &s.vu; }},
        {"mixPan", [](const Strip& s) -> const gemui::Widget* { return &s.pan; }},
        {"mixSend", [](const Strip& s) -> const gemui::Widget* { return &s.send; }},
        {"mixMute", [](const Strip& s) -> const gemui::Widget* { return &s.mute; }},
        {"mixSolo", [](const Strip& s) -> const gemui::Widget* { return &s.solo; }},
    };
    for (const Part& p : parts) {
        if (id.substr(0, p.prefix.size()) != p.prefix) continue;
        const std::string_view num = id.substr(p.prefix.size());
        if (num.empty() || num.size() > 2 || !std::all_of(num.begin(), num.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return std::nullopt;
        const int i = std::stoi(std::string(num));
        if (i >= plugin::kTracks) return std::nullopt;
        const gemui::Widget* w = p.pick(strips_[size_t(i)]);
        return w->visible() ? std::optional<Rect>(w->bounds()) : std::nullopt;
    }
    return std::nullopt;
}

}  // namespace stym::pluginui
