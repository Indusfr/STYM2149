// STYM2149 — pluginui: Settings window (sound engine, MIDI and interface settings, D-430)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "pluginui/SettingsWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "gemui/Alert.h"
#include "plugincore/Mapping.h"
#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"
#include "ymengine/Pitch.h"

namespace stym::pluginui {

using gemui::Button;
using gemui::MenuItem;
using gemui::Rect;
using plugin::Global;

namespace {

constexpr double kPad = 8;        // padding of the content (px)
constexpr double kGap = 8;        // between a label and its button
constexpr double kRowGap = 6;     // between two rows
constexpr double kFrameInset = 6; // from a frame's sides to its rows
constexpr double kTop = 26;       // from a frame's top to its first row (the legend's line, then the rule)
constexpr double kBottom = 10;    // from the last row to the frame's bottom rule
constexpr double kColGap = 12;    // between the two columns
constexpr double kFrameGap = 8;   // between two frames of a column
constexpr double kButtonsTop = 12;  // from the frames' bottom to Cancel and OK (D-435)
constexpr double kButtonsGap = 14;  // between Cancel and OK, as in the alert boxes

constexpr std::array<const char*, 4> kLayoutNames = {"Auto (detect)", "QWERTY", "AZERTY", "QWERTZ"};
constexpr std::array<const char*, 4> kLayoutIds = {"auto", "qwerty", "azerty", "qwertz"};
constexpr std::array<const char*, 2> kNoteLangs = {"English (C D E)", "Latin (Do Re Mi)"};

struct RowSpec {
    std::string_view id, label;
    const char* tip;
};
// One per row of SettingsWindow::Row, in its order.
constexpr std::array<RowSpec, 27> kSpecs = {{
    {"setComputer", "Computer", nullptr},  // the tooltip of the Computer button of the SYSTEM frame
    {"setClock", "Chip clock",
     "Chip clock: the frequency that drives the YM, and so its pitch. Custom, typed in MHz, plays in 16 ch only"},
    {"setA4", "Tuning A4",
     "Tuning: the frequency of A4, from which every note is tuned (350 to 480 Hz, 440 by default)\n"
     "The chip plays the nearest period it can: at 2 MHz, an A4 at 440 Hz plays 440.14 Hz"},
    {"setChain", "Output chain",
     "Output chain: Raw, the chip alone; ST, the filter of the Atari ST board; STE, the STE output with its bass and "
     "treble"},
    {"setResampling", "Resampling",
     "Resampling: how the chip's rate is brought to the host's. Box and Retro fall back to Sinc above clk/8"},
    {"setBass", "STE bass", "STE bass: -12 to +12 dB, with the STE output chain"},
    {"setTreble", "STE treble", "STE treble: -12 to +12 dB, with the STE output chain"},
    {"setSoftClip", "Soft clip", "Soft clip: a gentle saturation of the output instead of a hard clip"},
    {"setTimer", "Timer", "Timer: the timer effects on the steps of the Atari MFP 68901, or Free, continuous"},
    {"setVbl", "VBL frequency",
     "VBL frequency: the replay tick, which paces the Chip ADSR, the Modulation tables, the Ticks timing and the "
     "register exports"},
    {"setQuantize", "Quantize to VBL",
     "Quantize to VBL: notes and steps start on the replay frames, as an Atari replay routine plays them"},
    {"setRouting", "MIDI routing",
     "MIDI routing: Omni, the focused track; Layer, every enabled track; Per channel, channel n plays track n"},
    {"setVelocity", "Velocity", "Velocity: Accent (a gain in 16 ch, the 4-bit volume in 3 ch), Vol 4-bit or Off"},
    {"setBendTarget", "Bend",
     "Pitch bend: what it drives. Pitch, the note; Volume, the 4-bit volume (up towards 15, down towards 0)\n"
     "Envelope, the envelope frequency by the bend's semitones, the note unchanged (Sync Buzzer, Env sources)"},
    {"setBend", "Bend range", "Pitch bend range, in semitones"},
    {"setWheelTarget", "Wheel",
     "Modulation wheel: what it drives, from nothing at the bottom to all of it at the top. Vibrato; Volume, up to 15\n"
     "Envelope, up to an octave higher (Sync Buzzer, Env sources); LFO, the depth of the track's LFO; or Off"},
    {"setWheel", "Wheel depth", "Vibrato of the wheel, the aftertouch and the MPE slide at their top, in cents"},
    {"setAftertouch", "Aftertouch",
     "Aftertouch: what the pressure of the keys drives (of the channel or of each note), with the wheel's choices"},
    {"setMpe", "MPE",
     "MPE: channel 1 plays as usual, channels 2 to 16 one note each, with its own bend, pressure and slide\n"
     "The notes go to the focused track (or Layer); 16 ch only"},
    {"setMpeBend", "MPE bend range", "MPE: pitch bend range of each note, in semitones (48 is the MPE default)"},
    {"setMpeSlide", "MPE slide (CC74)",
     "MPE slide (CC74) of each note: Volume, from 0 at the bottom to 15 at the top (64 leaves it)\n"
     "Envelope, an octave either way (Sync Buzzer, Env sources); Vibrato, from the middle up to the Wheel depth"},
    {"setPixels", "Pixels",
     "Pixels: Sharp puts each pixel on whole screen pixels; Smooth draws the letters and icons with anti-aliasing"},
    {"setRenderer", "Renderer", "Renderer: how the window reaches the screen on Windows, Software or Direct2D"},
    {"setNoteNames", "Note names", "Note names: English (C D E) or Latin (Do Re Mi)"},
    {"setLayout", "Keyboard layout", "Layout of the computer keyboard; Auto follows the system's"},
    {"setTips", "Tooltips", "Tooltips: show these help boxes, or not"},
    {"setLanguage", "Language", "Language of the tooltips"},
}};

constexpr const char* kSteWhy = "STE only: set Computer to STE";
constexpr const char* kRendererWhy = "Renderer: Windows only";
constexpr const char* kMpeModeWhy = "MPE: 16 ch only";
constexpr const char* kMpeOffWhy = "MPE: set MPE to On";
// Bend ranges of the MPE notes offered (D-432): small ones, then the octaves up to the 96 semitones of the
// parameter; 48 is the MPE default.
constexpr std::array kMpeBends = {1, 2, 3, 4, 5, 7, 12, 24, 36, 48, 60, 72, 84, 96};
constexpr const char* kFolderTip = "Open the folder of the language files, to add a translation or change one";
constexpr const char* kCancelTip = "Cancel: put back the settings changed since the window opened, then close it";
constexpr const char* kOkTip = "OK: keep the settings and close the window";

MenuItem entry(std::string label) {
    MenuItem m;
    m.label = std::move(label);
    return m;
}

}  // namespace

// ------------------------------------------------------------------------------------------- construction

SettingsWindow::SettingsWindow(MainWindow& main) : m_(main) {
    static_assert(kSpecs.size() == size_t(kRows));
    build();
    wire();
    place({double(kX), double(kY), 400, 300});  // provisional size: layout() sets the real one at the first refresh
}

void SettingsWindow::build() {
    setTitle("SETTINGS");
    for (gemui::GroupBox* f : {&engineFs_, &midiFs_, &uiFs_}) add(*f);
    for (int r = 0; r < kRows; ++r) {
        labs_[size_t(r)].setText(std::string(kSpecs[size_t(r)].label));
        add(labs_[size_t(r)]);
        if (r == kA4) continue;
        if (kSpecs[size_t(r)].tip) btns_[size_t(r)].setTip(kSpecs[size_t(r)].tip);
        add(btns_[size_t(r)]);
    }
    // Tuning A4 in Hz with two decimals ("440.00 Hz"), its input mask one 9 per digit, its unit shown while it is
    // typed too (Indus, 25/10/2026 21:50, D-434).
    a4v_.setMinChars(9);
    a4v_.setMask("999.99 Hz");
    a4v_.setTip(kSpecs[kA4].tip);
    add(a4v_);
    folder_.setTip(kFolderTip);
    add(folder_);
    cancel_.setTip(kCancelTip);
    ok_.setTip(kOkTip);
    add(cancel_);
    add(ok_);
}

void SettingsWindow::wire() {
    // Closed by Cancel, the close box or Escape: the settings changed in the window put back (D-435).
    onClose = [this] {
        if (!keep_) putBack();
        keep_ = false;
        saved_ = {};
        m_.syncWindowButtons();
    };
    for (int r = 0; r < kRows; ++r) {
        if (r == kA4) continue;
        btns_[size_t(r)].onClick = [this, r] {
            // The languages of the files read again at each opening, as the Options menu did (D-283).
            if (r == kLanguage) {
                m_.scanTipLanguages();
                m_.applyTipLanguage();
            }
            int checked = -1;
            std::vector<MenuItem> list = notedItems(r, checked);
            m_.popup(btns_[size_t(r)], std::move(list), checked);
        };
    }
    a4v_.onCommit = [this](const std::string& t) {
        const auto v = parseA4(t);
        if (v) {
            remember(kA4);
            m_.set(Global::A4Hz, float(*v));
        }
        return v.has_value();
    };
    folder_.onClick = [this] { m_.openLanguagesFolder(); };
    cancel_.onClick = [this] { cancel(); };
    ok_.onClick = [this] { ok(); };
}

// ------------------------------------------------------------------------------------------- Cancel and OK

void SettingsWindow::ok() {
    keep_ = true;
    close();
}

void SettingsWindow::cancel() {
    if (!changed()) {
        close();
        return;
    }
    // Changes to lose: asked first, as File > New and Quit do (Indus, 26/10/2026 03:51, D-437); Cancel, the default
    // button of the box, keeps the window open with the changes.
    gemui::Alert::Spec spec;
    spec.lines = {{"Discard the changes?"}, {"The settings changed in this window will be lost."}};
    spec.confirmButtons();
    gemui::Alert::open(m_.desk_, std::move(spec), [this](int button, const std::string&) {
        if (button == gemui::Alert::kConfirmOk) close();
    });
}

bool SettingsWindow::changed() const {
    for (int r = 0; r < kRows; ++r)
        if (differs(r)) return true;
    return false;
}

bool SettingsWindow::differs(int row) const {
    if (!saved_[size_t(row)]) return false;
    const Saved& s = *saved_[size_t(row)];
    for (const auto& [index, value] : s.params)
        if (m_.values_[size_t(index)] != value) return true;
    switch (row) {
    case kQuantize: return m_.extras_.global.quantizeVbl != s.flag;
    case kPixels: return m_.host_.preference("pixels", "smooth") != s.text;
    case kRenderer: return m_.host_.preference("renderer", "software") != s.text;
    case kNoteNames: return m_.host_.preference("noteNames", "0") != s.text;
    case kLayout: return m_.host_.preference("kbdLayout", "auto") != s.text;
    case kTips: return m_.desk_.tooltips() != s.flag;
    case kLanguage: return m_.tipLang_ != s.text;
    default: return false;
    }
}

bool SettingsWindow::dialogKey(const gemui::KeyEvent& e) {
    if (e.key != gemui::Key::Enter || !isOpen() || a4v_.editing() || m_.desk_.hasPopups()) return false;
    if (!parent() || gemui::FloatWindow::topmost(*parent()) != this) return false;
    cancel();
    return true;
}

std::vector<MenuItem> SettingsWindow::notedItems(int row, int& checked) {
    std::vector<MenuItem> list = rowItems(row, checked);
    for (MenuItem& e : list)
        if (e.pick)
            e.pick = [this, row, pick = std::move(e.pick)] {
                remember(row);
                pick();
            };
    return list;
}

// The value a row has before its first change in the window: kept until the window closes.
void SettingsWindow::remember(int row) {
    if (row < 0 || row >= kRows || saved_[size_t(row)]) return;
    Saved s;
    auto param = [this, &s](Global g) {
        const int i = plugin::paramIndex(g);
        s.params.emplace_back(i, m_.values_[size_t(i)]);
    };
    switch (row) {
    case kComputer: param(Global::Computer); break;
    case kClock:  // the frequency of the Custom clock first, so that putting it back plays it at once
        param(Global::ChipClockHz);
        param(Global::ChipClock);
        break;
    case kA4: param(Global::A4Hz); break;
    case kChain: param(Global::OutputChain); break;
    case kResampling: param(Global::Resampling); break;
    case kBass: param(Global::SteBass); break;
    case kTreble: param(Global::SteTreble); break;
    case kSoftClip: param(Global::SoftClip); break;
    case kTimer: param(Global::Timer); break;
    case kVbl: param(Global::VblRate); break;
    case kQuantize: s.flag = m_.extras_.global.quantizeVbl; break;
    case kRouting: param(Global::MidiRouting); break;
    case kVelocity: param(Global::Velocity); break;
    case kBendTarget: param(Global::BendTarget); break;
    case kBend: param(Global::BendRange); break;
    case kWheelTarget: param(Global::WheelTarget); break;
    case kWheel: param(Global::WheelDepth); break;
    case kAftertouch: param(Global::Aftertouch); break;
    case kMpe: param(Global::Mpe); break;
    case kMpeBend: param(Global::MpeBendRange); break;
    case kMpeSlide: param(Global::MpeSlide); break;
    case kPixels: s.text = m_.host_.preference("pixels", "smooth"); break;
    case kRenderer: s.text = m_.host_.preference("renderer", "software"); break;
    case kNoteNames: s.text = m_.host_.preference("noteNames", "0"); break;
    case kLayout: s.text = m_.host_.preference("kbdLayout", "auto"); break;
    case kTips: s.flag = m_.desk_.tooltips(); break;
    case kLanguage: s.text = m_.tipLang_; break;
    default: return;
    }
    saved_[size_t(row)] = std::move(s);
}

void SettingsWindow::putBack() {
    static constexpr std::array<std::pair<int, const char*>, 4> kPrefs = {
        {{kPixels, "pixels"}, {kRenderer, "renderer"}, {kNoteNames, "noteNames"}, {kLayout, "kbdLayout"}}};
    for (int r = 0; r < kRows; ++r) {
        if (!saved_[size_t(r)]) continue;
        const Saved& s = *saved_[size_t(r)];
        if (r == kComputer) {  // as its menu does it: the notes cut, the focus brought back to a track left
            m_.setComputer(int(std::lround(s.params[0].second)));
            continue;
        }
        for (const auto& [index, value] : s.params)
            if (m_.values_[size_t(index)] != value) m_.set(index, value);
        if (r == kQuantize && m_.extras_.global.quantizeVbl != s.flag) {
            m_.host_.editExtras([on = s.flag](plugin::NonParamState& st) { st.global.quantizeVbl = on; });
            m_.pullExtras();
        }
        for (const auto& [row, key] : kPrefs)
            if (r == row) m_.host_.setPreference(key, s.text);
        if (r == kTips) {
            m_.desk_.setTooltips(s.flag);
            m_.host_.setPreference("tooltips", s.flag ? "1" : "0");
        }
        if (r == kLanguage && m_.tipLang_ != s.text) m_.setTipLanguage(s.text);
    }
    m_.refresh();  // the note names, the keyboard layout
}

// ------------------------------------------------------------------------------------------- settings

std::vector<std::string_view> SettingsWindow::menuIds() {
    std::vector<std::string_view> out;
    for (int r = 0; r < kRows; ++r)
        if (r != kA4) out.push_back(kSpecs[size_t(r)].id);
    return out;
}

int SettingsWindow::rowOf(std::string_view id) noexcept {
    for (int r = 0; r < kRows; ++r)
        if (kSpecs[size_t(r)].id == id) return r;
    return -1;
}

std::string_view SettingsWindow::labelOf(std::string_view id) noexcept {
    const int r = rowOf(id);
    return r < 0 ? std::string_view() : kSpecs[size_t(r)].label;
}

std::vector<MenuItem> SettingsWindow::items(std::string_view id, int& checked) {
    checked = -1;
    const int r = rowOf(id);
    if (r < 0 || r == kA4) return {};
    return notedItems(r, checked);
}

std::optional<double> SettingsWindow::parseA4(const std::string& text) {
    size_t used = 0;
    const double raw = gemui::readDecimal(text, &used);
    if (used == 0 || !std::isfinite(raw)) return std::nullopt;
    return std::clamp(std::round(raw * 100) / 100, ymengine::kA4MinHz, ymengine::kA4MaxHz);
}

// The entries of each row's menu. Authentic 3ch (D-207): the entries an Atari ST or STE cannot play are greyed out,
// the ones the CHIP list names; STE hardware with Computer ST (D-206): the STE output chain greyed out (its bass and
// treble buttons are). A value chosen before is kept and still plays, checked, until another entry is picked.
std::vector<MenuItem> SettingsWindow::rowItems(int row, int& checked) {
    checked = -1;
    MainWindow& m = m_;
    const bool authentic = plugin::voiceMode(m.values_) == ymengine::VoiceMode::Authentic3;
    auto notOnAtari = [authentic](MenuItem& e, const char* why) {
        if (!authentic) return;
        e.disabled = true;
        e.why = why;
    };
    // A preference: its entries, the one equal to the stored value checked; picking one stores it.
    auto preference = [this, &checked](const char* key, const char* fallback,
                                       std::initializer_list<std::pair<const char*, const char*>> values) {
        std::vector<MenuItem> out;
        const std::string now = m_.host_.preference(key, fallback);
        for (const auto& [label, value] : values) {
            if (now == value) checked = int(out.size());
            MenuItem e = entry(label);
            e.pick = [this, key, v = std::string(value)] {
                m_.host_.setPreference(key, v);
                preferenceSet();
            };
            out.push_back(std::move(e));
        }
        return out;
    };
    switch (row) {
    case kComputer: {
        std::vector<MenuItem> out = m.paramItems(Global::Computer, 1, checked);
        for (int i = 0; i < int(out.size()); ++i) out[size_t(i)].pick = [this, i] { m_.setComputer(i); };
        return out;
    }
    case kClock: {
        std::vector<MenuItem> out = m.paramItems(Global::ChipClock, 1, checked);
        for (size_t i = MainWindow::kFirstAyClock; i < out.size(); ++i)
            notOnAtari(out[i], "Authentic 3ch: an Atari ST or STE clocks its YM at 2 MHz");
        // Custom clock in place of 2 MHz (fourth DAW test, D-237): its frequency, typed in a prompt box, in 16
        // channels only; greyed out in the other modes, where it plays 2 MHz (checked if chosen before, D-207).
        MenuItem& custom = out[0];
        const uint32_t hz = m.mode3() ? uint32_t(plugin::kCustomClockDefaultHz) : plugin::customClockHz(m.values_);
        custom.label = "Custom " + MainWindow::clockMHz(hz) + " MHz...";
        custom.pick = [this] { m_.customClock(); };
        if (m.mode3()) {
            custom.disabled = true;
            custom.why = "Custom clock: 16 ch only (2 MHz in the other modes)";
        }
        return out;
    }
    case kChain: {
        std::vector<MenuItem> out = m.paramItems(Global::OutputChain, 1, checked);
        if (!m.ste()) {  // Raw, ST, STE
            out[2].disabled = true;
            out[2].why = kSteWhy;
        }
        return out;
    }
    case kResampling: return m.paramItems(Global::Resampling, 1, checked);
    case kBass: return m.paramItems(Global::SteBass, 1, checked);
    case kTreble: return m.paramItems(Global::SteTreble, 1, checked);
    case kSoftClip: return m.paramItems(Global::SoftClip, 1, checked);
    case kTimer: {
        std::vector<MenuItem> out = m.paramItems(Global::Timer, 1, checked);
        notOnAtari(out[MainWindow::kTimerFree], "Authentic 3ch: an Atari ST or STE times its effects with the MFP");
        return out;
    }
    // The replay tick, from the Options menu (Indus, 26/10/2026 04:03, D-441).
    case kVbl: return m.paramItems(Global::VblRate, 1, checked);
    case kQuantize: {  // a setting of the state, without a host parameter (spec §11, D-111)
        std::vector<MenuItem> out;
        for (int i = 0; i < 2; ++i) {
            MenuItem e = entry(i ? "On" : "Off");
            e.pick = [this, i] {
                m_.host_.editExtras([i](plugin::NonParamState& s) { s.global.quantizeVbl = i != 0; });
                m_.pullExtras();
                refresh();
            };
            out.push_back(std::move(e));
        }
        checked = m.extras_.global.quantizeVbl ? 1 : 0;
        return out;
    }
    case kRouting: return m.paramItems(Global::MidiRouting, 1, checked);
    case kVelocity: return m.paramItems(Global::Velocity, 1, checked);
    case kBendTarget: return m.paramItems(Global::BendTarget, 1, checked);
    case kBend: return m.paramItems(Global::BendRange, 1, checked);
    case kWheelTarget: return m.paramItems(Global::WheelTarget, 1, checked);
    case kWheel: return m.paramItems(Global::WheelDepth, 25, checked);
    case kAftertouch: return m.paramItems(Global::Aftertouch, 1, checked);
    case kMpe: return m.paramItems(Global::Mpe, 1, checked);
    case kMpeBend: {  // the bend ranges of kMpeBends, and the one chosen if it is not among them
        std::vector<int> vals(kMpeBends.begin(), kMpeBends.end());
        const int now = int(std::lround(m.pv(Global::MpeBendRange)));
        if (std::find(vals.begin(), vals.end(), now) == vals.end())
            vals.insert(std::upper_bound(vals.begin(), vals.end(), now), now);
        std::vector<MenuItem> out;
        for (int v : vals) {
            if (v == now) checked = int(out.size());
            MenuItem e = entry(std::to_string(v) + " st");
            e.pick = [this, v] { m_.set(Global::MpeBendRange, float(v)); };
            out.push_back(std::move(e));
        }
        return out;
    }
    case kMpeSlide: return m.paramItems(Global::MpeSlide, 1, checked);
    // Pixels of the plugin's window at a size that is not a whole multiple of the desktop (tenth DAW test, item 123,
    // D-331): Sharp puts each pixel on whole screen pixels, Smooth (default again, eleventh DAW test, item 130,
    // D-339) draws the letters and icons with anti-aliasing, as a browser shows the prototype.
    case kPixels: return preference("pixels", "smooth", {{"Sharp", "sharp"}, {"Smooth", "smooth"}});
    // How the window reaches the screen on Windows (D-420): JUCE's software renderer by default, or Direct2D. The
    // desktop is drawn into an image by the CPU either way; with Direct2D, a copy of the window flickered at the top
    // right of the Windows desktop for a few seconds (Indus, 24/10/2026 22:27). Greyed out elsewhere (its button).
    case kRenderer: return preference("renderer", "software", {{"Software", "software"}, {"Direct2D", "direct2d"}});
    case kNoteNames: return preference("noteNames", "0", {{kNoteLangs[0], "0"}, {kNoteLangs[1], "1"}});
    case kLayout:
        return preference("kbdLayout", "auto",
                          {{kLayoutNames[0], kLayoutIds[0]}, {kLayoutNames[1], kLayoutIds[1]},
                           {kLayoutNames[2], kLayoutIds[2]}, {kLayoutNames[3], kLayoutIds[3]}});
    case kTips: {
        std::vector<MenuItem> out;
        for (int i = 0; i < 2; ++i) {
            MenuItem e = entry(i ? "Off" : "On");
            e.pick = [this, i] {
                m_.desk_.setTooltips(i == 0);
                m_.host_.setPreference("tooltips", i ? "0" : "1");
                refresh();
            };
            out.push_back(std::move(e));
        }
        checked = m.desk_.tooltips() ? 0 : 1;
        return out;
    }
    // Tooltip language (D-185): English, built in, then the languages of the files, installed or the user's (D-283).
    case kLanguage: {
        std::vector<MenuItem> out;
        for (size_t i = 0; i < m.tipChoices_.size(); ++i) {
            const MainWindow::TipChoice& c = m.tipChoices_[i];
            if (c.id == m.tipLang_) checked = int(i);
            MenuItem e = entry(c.name);
            e.pick = [this, id = c.id] {
                m_.setTipLanguage(id);
                refresh();
            };
            out.push_back(std::move(e));
        }
        return out;
    }
    default: return {};
    }
}

void SettingsWindow::preferenceSet() {
    m_.refresh();  // the note names, the keyboard layout
    refresh();
}

// The value a row's button shows: the checked entry of its menu ("Custom 2.0000 MHz" without its "..."); English for
// a language whose files are gone.
std::string SettingsWindow::valueText(int row) {
    int checked = -1;
    std::vector<MenuItem> list = rowItems(row, checked);
    if (checked < 0 || checked >= int(list.size())) return row == kLanguage ? "English" : "";
    std::string t = list[size_t(checked)].label;
    if (t.size() > 3 && t.compare(t.size() - 3, 3, "...") == 0) t.resize(t.size() - 3);
    return t;
}

double SettingsWindow::buttonWidth(int row) {
    int checked = -1;
    double w = 0;
    for (const MenuItem& e : rowItems(row, checked)) {
        std::string t = e.label;
        if (t.size() > 3 && t.compare(t.size() - 3, 3, "...") == 0) t.resize(t.size() - 3);
        w = std::max(w, Button(t).naturalWidth());
    }
    return w;
}

void SettingsWindow::show() {
    if (!isOpen()) {  // a new opening: nothing changed yet (D-435)
        saved_ = {};
        keep_ = false;
    }
    open();
    refresh();
    m_.syncWindowButtons();
}

void SettingsWindow::refresh() {
    if (!isOpen()) return;
    for (int r = 0; r < kRows; ++r)
        if (r != kA4) btns_[size_t(r)].setText(valueText(r));
    // STE bass and treble with Computer ST (D-206): greyed out, the value chosen before kept and shown.
    const bool ste = m_.ste();
    for (int r : {kBass, kTreble}) {
        btns_[size_t(r)].setEnabled(ste);
        btns_[size_t(r)].setTip(ste ? kSpecs[size_t(r)].tip : kSteWhy);
        labs_[size_t(r)].setDim(!ste);
    }
    // MPE (D-432): 16 ch only; its bend range and slide with MPE on.
    const bool studio = !m_.mode3();
    const bool mpe = studio && m_.pv(Global::Mpe) >= 0.5f;
    btns_[kMpe].setEnabled(studio);
    btns_[kMpe].setTip(studio ? kSpecs[kMpe].tip : kMpeModeWhy);
    labs_[kMpe].setDim(!studio);
    for (int r : {kMpeBend, kMpeSlide}) {
        btns_[size_t(r)].setEnabled(mpe);
        btns_[size_t(r)].setTip(mpe ? kSpecs[size_t(r)].tip : studio ? kMpeOffWhy : kMpeModeWhy);
        labs_[size_t(r)].setDim(!mpe);
    }
    const bool renderer = m_.host_.rendererChoice();
    btns_[kRenderer].setEnabled(renderer);
    btns_[kRenderer].setTip(renderer ? kSpecs[kRenderer].tip : kRendererWhy);
    labs_[kRenderer].setDim(!renderer);
    if (!a4v_.editing()) a4v_.setText(gemui::formatDecimal("%.2f", double(m_.pv(Global::A4Hz))) + " Hz");
    layout();
}

// ------------------------------------------------------------------------------------------- layout

void SettingsWindow::layout() {
    const double ox = box().x, oy = box().y;
    const double x0 = ox + 14, y0 = oy + 32 + kPad;  // below the frame (6) and the title bar (26)
    const double rowH = btns_[kComputer].naturalHeight();

    // A column of frames: in each, the labels at the width of the column's widest, then the buttons at the width of
    // its widest, so that they line up from one frame to the next.
    struct Frame {
        gemui::GroupBox* box;
        std::vector<int> rows;
        bool folder = false;  // the Languages folder button under the last row
    };
    auto column = [&](double x, double y, const std::vector<Frame>& frames) {
        double labW = 0, btnW = 0;
        for (const Frame& f : frames)
            for (int r : f.rows) {
                labW = std::max(labW, labs_[size_t(r)].naturalWidth());
                btnW = std::max(btnW, r == kA4 ? a4v_.naturalWidth() : buttonWidth(r));
            }
        btnW = std::max(btnW, folder_.naturalWidth());
        double w = 0;
        for (const Frame& f : frames) w = std::max(w, f.box->legendWidth() + 2 * kFrameInset);
        w = std::max(w, kFrameInset + labW + kGap + btnW + kFrameInset);
        std::vector<double> bottoms;
        for (const Frame& f : frames) {
            const double top = y;
            double ry = top + kTop;
            for (int r : f.rows) {
                labs_[size_t(r)].setBox({x + kFrameInset, ry + (rowH - 16) / 2, labW, 16});
                const double bx = x + kFrameInset + labW + kGap;
                if (r == kA4) a4v_.setBox({bx, ry + (rowH - 16) / 2, a4v_.naturalWidth(), 16});
                else btns_[size_t(r)].setBox({bx, ry, btnW, rowH});
                ry += rowH + kRowGap;
            }
            if (f.folder) {
                folder_.setBox({x + kFrameInset + labW + kGap, ry, btnW, double(folder_.naturalHeight())});
                ry += folder_.naturalHeight() + kRowGap;
            }
            const double h = ry - kRowGap + kBottom - top;
            f.box->setBox({x, top, w, h});
            y = top + h + kFrameGap;
        }
        return std::pair<double, double>{w, y - kFrameGap};
    };
    // Three columns, one frame each (the MIDI expression made the MIDI frame as tall as SOUND ENGINE, D-432).
    const auto [leftW, leftBottom] = column(x0, y0, {{&engineFs_,
                                                       {kComputer, kClock, kA4, kChain, kResampling, kBass, kTreble,
                                                        kSoftClip, kTimer, kVbl, kQuantize}}});
    const double midX = x0 + leftW + kColGap;
    const auto [midW, midBottom] =
        column(midX, y0, {{&midiFs_, {kRouting, kVelocity, kBendTarget, kBend, kWheelTarget, kWheel, kAftertouch, kMpe,
                                      kMpeBend, kMpeSlide}}});
    const auto [rightW, rightBottom] =
        column(midX + midW + kColGap, y0,
               {{&uiFs_, {kPixels, kRenderer, kNoteNames, kLayout, kTips, kLanguage}, true}});
    // The columns end together: the shorter ones' frames grow down to the tallest one's bottom.
    const double bottom = std::max({leftBottom, midBottom, rightBottom});
    for (gemui::GroupBox* f : {&engineFs_, &midiFs_, &uiFs_}) {
        const gemui::Box b = f->box();
        f->setBox({b.x, b.y, b.w, bottom - b.y});
    }

    // Cancel and OK under the frames, centred, Cancel first (D-435).
    const double contentW = leftW + kColGap + midW + kColGap + rightW;
    const double cw = std::ceil(cancel_.naturalWidth()), okw = std::ceil(ok_.naturalWidth());
    const double by = bottom + kButtonsTop, bh = cancel_.naturalHeight();
    const double bx = x0 + (contentW - cw - kButtonsGap - okw) / 2;
    cancel_.setBox({std::round(bx), by, cw, bh});
    ok_.setBox({std::round(bx) + cw + kButtonsGap, by, okw, bh});

    // Window: content, padding of 8, border and margin of .gwin (6 on each side).
    const double w = contentW + 2 * kPad + 12;
    const double h = by + bh + kPad + 6 - oy;
    if (std::abs(h - box().h) > 0.01 || std::abs(w - box().w) > 0.01) place({ox, oy, w, h});
    repaint();
}

std::optional<Rect> SettingsWindow::rectOf(std::string_view id) const {
    if (!isOpen()) return std::nullopt;
    if (id == "settingsWin") return bounds();
    if (id == "setA4") return a4v_.bounds();
    if (id == "setFolder") return folder_.bounds();
    if (id == "setEngine") return engineFs_.bounds();
    if (id == "setMidi") return midiFs_.bounds();
    if (id == "setInterface") return uiFs_.bounds();
    if (id == "setCancel") return cancel_.bounds();
    if (id == "setOk") return ok_.bounds();
    const int r = rowOf(id);
    if (r < 0) return std::nullopt;
    return btns_[size_t(r)].bounds();
}

const Button* SettingsWindow::button(std::string_view id) const noexcept {
    if (id == "setFolder") return &folder_;
    if (id == "setCancel") return &cancel_;
    if (id == "setOk") return &ok_;
    const int r = rowOf(id);
    return r < 0 || r == kA4 ? nullptr : &btns_[size_t(r)];
}

}  // namespace stym::pluginui
