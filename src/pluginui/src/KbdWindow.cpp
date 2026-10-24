// STYM2149 — pluginui: Computer Keyboard window (TKL drawing, playing on the computer keyboard)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Behaviours taken from the prototype: TKL_ROWS, TKL_LABELS, TKL_OTHER, CODEMAP, LAYOUTS, NUMPAD, buildTkl,
// buildTklNum, refreshKbdOct, kbdOctShift, refreshHeldBtn, retuneHeld, tklLight, tklNote, tklNotes, tklAllOff,
// openKbdWin, #kbdClose, the numeric keypad and the playing part of the keydown and keyup handlers. Pads of a
// percussion track (D-229): STYM2149.
#include "pluginui/KbdWindow.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <string>

#include "gemui/Font.h"
#include "pluginui/MainWindow.h"
#include "ymseq/Tables.h"

namespace stym::pluginui {

using gemui::Box;
using gemui::Color;
using gemui::Font;
using gemui::Rect;
namespace colors = gemui::colors;

namespace {

constexpr double kKu = 39, kKeyGap = 4;  // --ku, gap of #tkl, .krow and #tklnum
constexpr double kGap = 6, kPad = 8;
constexpr int kPlayLo = 23, kPlayHi = 120;  // PLAY_LO, MAP_HI: B0 to C9

// TKL_ROWS: engraving, physical code (empty: space between blocks), width in key units.
struct KeyDef {
    const char* label;
    const char* code;
    double wu;
};
const std::vector<std::vector<KeyDef>>& tklRows() {
    static const std::vector<std::vector<KeyDef>> rows = {
        {{"Esc", "Escape", 1}, {"", "", 0.5}, {"F1", "F1", 1}, {"F2", "F2", 1}, {"F3", "F3", 1}, {"F4", "F4", 1},
         {"", "", 0.5}, {"F5", "F5", 1}, {"F6", "F6", 1}, {"F7", "F7", 1}, {"F8", "F8", 1}, {"", "", 0.5},
         {"F9", "F9", 1}, {"F10", "F10", 1}, {"F11", "F11", 1}, {"F12", "F12", 1}, {"", "", 0.5},
         {"PrtSc", "PrintScreen", 1}, {"ScrLk", "ScrollLock", 1}, {"Pause", "Pause", 1}},
        {{"`", "Backquote", 1}, {"1", "Digit1", 1}, {"2", "Digit2", 1}, {"3", "Digit3", 1}, {"4", "Digit4", 1},
         {"5", "Digit5", 1}, {"6", "Digit6", 1}, {"7", "Digit7", 1}, {"8", "Digit8", 1}, {"9", "Digit9", 1},
         {"0", "Digit0", 1}, {"-", "Minus", 1}, {"=", "Equal", 1}, {"Bksp", "Backspace", 2}, {"", "", 0.5},
         {"Ins", "Insert", 1}, {"Home", "Home", 1}, {"PgUp", "PageUp", 1}},
        {{"Tab", "Tab", 1.5}, {"Q", "KeyQ", 1}, {"W", "KeyW", 1}, {"E", "KeyE", 1}, {"R", "KeyR", 1},
         {"T", "KeyT", 1}, {"Y", "KeyY", 1}, {"U", "KeyU", 1}, {"I", "KeyI", 1}, {"O", "KeyO", 1}, {"P", "KeyP", 1},
         {"[", "BracketLeft", 1}, {"]", "BracketRight", 1}, {"\\", "Backslash", 1.5}, {"", "", 0.5},
         {"Del", "Delete", 1}, {"End", "End", 1}, {"PgDn", "PageDown", 1}},
        {{"Caps", "CapsLock", 1.75}, {"A", "KeyA", 1}, {"S", "KeyS", 1}, {"D", "KeyD", 1}, {"F", "KeyF", 1},
         {"G", "KeyG", 1}, {"H", "KeyH", 1}, {"J", "KeyJ", 1}, {"K", "KeyK", 1}, {"L", "KeyL", 1},
         {";", "Semicolon", 1}, {"'", "Quote", 1}, {"Enter", "Enter", 2.25}},
        {{"Shift", "ShiftLeft", 2.25}, {"Z", "KeyZ", 1}, {"X", "KeyX", 1}, {"C", "KeyC", 1}, {"V", "KeyV", 1},
         {"B", "KeyB", 1}, {"N", "KeyN", 1}, {"M", "KeyM", 1}, {",", "Comma", 1}, {".", "Period", 1},
         {"/", "Slash", 1}, {"Shift", "ShiftRight", 2.75}, {"", "", 1.5}, {"↑", "ArrowUp", 1}},
        {{"Ctrl", "ControlLeft", 1.25}, {"Win", "MetaLeft", 1.25}, {"Alt", "AltLeft", 1.25}, {"Space", "Space", 6.25},
         {"Alt", "AltRight", 1.25}, {"Win", "MetaRight", 1.25}, {"Menu", "ContextMenu", 1.25},
         {"Ctrl", "ControlRight", 1.25}, {"", "", 0.5}, {"←", "ArrowLeft", 1}, {"↓", "ArrowDown", 1},
         {"→", "ArrowRight", 1}},
    };
    return rows;
}

// CODEMAP: keys that play, from C to the F of the next octave.
constexpr std::array<std::string_view, 18> kCodeMap = {"KeyA", "KeyW", "KeyS", "KeyE", "KeyD", "KeyF",
                                                       "KeyT", "KeyG", "KeyY", "KeyH", "KeyU", "KeyJ",
                                                       "KeyK", "KeyO", "KeyL", "KeyP", "Semicolon", "Quote"};
constexpr std::array<const char*, 18> kNN = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#",
                                             "A", "A#", "B", "C", "C#", "D", "D#", "E", "F"};
// Percussion track (D-229): pads 1 to 8 on the middle row, from A to K (QWERTY positions), like the pads of the main
// window from left to right.
constexpr std::array<std::string_view, 8> kPadKeys = {"KeyA", "KeyS", "KeyD", "KeyF", "KeyG", "KeyH", "KeyJ", "KeyK"};

int padOf(std::string_view code) {
    const auto it = std::find(kPadKeys.begin(), kPadKeys.end(), code);
    return it == kPadKeys.end() ? -1 : int(it - kPadKeys.begin());
}

// TKL_LABELS: what is engraved on the key, per layout (QWERTY: the engraving of TKL_ROWS). AZERTY: KeyM carries ","
// (the prototype wrote "M" there, which is on Semicolon: D-029, D-131).
struct Engraving {
    std::string_view code, label;
};
constexpr std::array<Engraving, 16> kAzerty = {{{"KeyQ", "A"},
                                                {"KeyW", "Z"},
                                                {"KeyA", "Q"},
                                                {"KeyZ", "W"},
                                                {"KeyM", ","},
                                                {"Semicolon", "M"},
                                                {"Quote", "ù"},
                                                {"BracketLeft", "^"},
                                                {"BracketRight", "$"},
                                                {"Backslash", "*"},
                                                {"Comma", ";"},
                                                {"Period", ":"},
                                                {"Slash", "!"},
                                                {"Backquote", "²"},
                                                {"Minus", ")"},
                                                {"Equal", "="}}};
constexpr std::array<Engraving, 10> kQwertz = {{{"KeyY", "Z"},
                                                {"KeyZ", "Y"},
                                                {"Semicolon", "Ö"},
                                                {"Quote", "Ä"},
                                                {"BracketLeft", "Ü"},
                                                {"BracketRight", "+"},
                                                {"Backslash", "#"},
                                                {"Minus", "ß"},
                                                {"Equal", "´"},
                                                {"Slash", "-"}}};

// TKL_OTHER: other shortcuts shown.
struct Other {
    std::string_view code, what;
};
constexpr std::array<Other, 13> kOther = {{{"Space", "Play/Stop"},
                                           {"Escape", "Panic"},
                                           {"KeyM", "Metro"},
                                           {"ArrowLeft", "Channel"},
                                           {"ArrowRight", "Channel"},
                                           {"Digit1", "Pattern"},
                                           {"Digit2", "Pattern"},
                                           {"Digit3", "Pattern"},
                                           {"Digit4", "Pattern"},
                                           {"Digit5", "Pattern"},
                                           {"Digit6", "Pattern"},
                                           {"Digit7", "Pattern"},
                                           {"Digit8", "Pattern"}}};

// NUMPAD: engraving, code, place in the 4-column grid (column, row, width and height in cells).
struct PadDef {
    const char* label;
    const char* code;
    int col, row, w, h;
};
constexpr std::array<PadDef, 17> kNumpad = {{{"Num", "NumLock", 0, 0, 1, 1},
                                             {"/", "NumpadDivide", 1, 0, 1, 1},
                                             {"*", "NumpadMultiply", 2, 0, 1, 1},
                                             {"-", "NumpadSubtract", 3, 0, 1, 1},
                                             {"7", "Numpad7", 0, 1, 1, 1},
                                             {"8", "Numpad8", 1, 1, 1, 1},
                                             {"9", "Numpad9", 2, 1, 1, 1},
                                             {"+", "NumpadAdd", 3, 1, 1, 2},
                                             {"4", "Numpad4", 0, 2, 1, 1},
                                             {"5", "Numpad5", 1, 2, 1, 1},
                                             {"6", "Numpad6", 2, 2, 1, 1},
                                             {"1", "Numpad1", 0, 3, 1, 1},
                                             {"2", "Numpad2", 1, 3, 1, 1},
                                             {"3", "Numpad3", 2, 3, 1, 1},
                                             {"Ent", "NumpadEnter", 3, 3, 1, 2},
                                             {"0", "Numpad0", 0, 4, 2, 1},
                                             {".", "NumpadDecimal", 2, 4, 1, 1}}};

std::string_view canonicalLayout(std::string_view layout) {
    return layout == "azerty" || layout == "qwertz" ? layout : std::string_view("qwerty");
}

// Engraving of a key in a layout (graved).
std::string_view engraving(std::string_view code, std::string_view layout, std::string_view fallback) {
    layout = canonicalLayout(layout);
    if (layout == "azerty") {
        for (const Engraving& e : kAzerty)
            if (e.code == code) return e.label;
    } else if (layout == "qwertz") {
        for (const Engraving& e : kQwertz)
            if (e.code == code) return e.label;
    }
    return fallback;
}

// QWERTY engraving of a code (TKL_ROWS).
std::string_view qwertyLabel(std::string_view code) {
    for (const auto& row : tklRows())
        for (const KeyDef& k : row)
            if (code == k.code) return k.label;
    return {};
}

// Upper case of a character (ASCII and Latin-1: ù → Ù, like getLayoutMap().toUpperCase()).
// Latin-1 small letters are U+00E0 to U+00FE, their capitals 0x20 below; U+00F7 (÷) is not a letter.
char32_t upper(char32_t c) {
    if (c >= U'a' && c <= U'z') return c - 0x20;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 0x20;
    return c;
}

char32_t firstCodepoint(std::string_view s) {
    size_t pos = 0;
    return s.empty() ? 0 : gemui::nextCodepoint(s, pos);
}

std::string noteName(int midi, bool latin) {
    static constexpr std::array<const char*, 12> en = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static constexpr std::array<const char*, 12> lat = {"Do", "Do#", "Re", "Re#", "Mi", "Fa",
                                                        "Fa#", "Sol", "Sol#", "La", "La#", "Si"};
    const int pc = ((midi % 12) + 12) % 12;
    return std::string(latin ? lat[size_t(pc)] : en[size_t(pc)]) +
           std::to_string(int(std::floor(midi / 12.0)) - 1);
}

// Key colours: keys without a role (background, engraving, border), octave keys, shortcut keys, and the hatching of a
// white or black key out of the chip's range.
constexpr Color kDeadBg = gemui::rgb(0xDC, 0xDC, 0xDC), kDeadInk = gemui::rgb(0x8A, 0x8A, 0x8A);
constexpr Color kDeadLine = gemui::rgb(0x9A, 0x9A, 0x9A), kOctBg = gemui::rgb(0xA8, 0xA8, 0xA8);
constexpr Color kTrBg = gemui::rgb(0xC8, 0xC8, 0xC8), kNopeLight = gemui::rgb(0xC0, 0xC0, 0xC0);
constexpr Color kNopeDark = gemui::rgb(0x58, 0x58, 0x58), kNopeDarkInk = gemui::rgb(0xB0, 0xB0, 0xB0);

// line-height:1.1 of 13 and 11 px, rounded down to 1/64 of a pixel like the browser.
constexpr double kLabelLine = 915.0 / 64, kSubLine = 774.0 / 64;

}  // namespace

// A plugin receives the character typed, not the physical key: the key is found back by looking for the position
// whose engraving, in the chosen layout, is that character (an AZERTY "Q" is the QWERTY KeyA position, which plays C).
KbdAction kbdAction(const gemui::KeyEvent& e, std::string_view layout) {
    KbdAction a;
    if (e.key != gemui::Key::Character) return a;
    if (e.code >= gemui::KeyEvent::kNumpad0 && e.code <= gemui::KeyEvent::kNumpad0 + 9) {
        a.kind = KbdAction::Kind::Numpad;
        a.value = e.code - gemui::KeyEvent::kNumpad0;
        return a;
    }
    const char32_t c = upper(e.code ? char32_t(e.code) : e.ch);
    if (!c) return a;
    // Physical code of the key that carries this character in the chosen layout.
    auto is = [&](std::string_view code) {
        const std::string_view g = engraving(code, layout, qwertyLabel(code));
        return upper(firstCodepoint(g)) == c;
    };
    for (size_t i = 0; i < kCodeMap.size(); ++i)
        if (is(kCodeMap[i])) {
            a.kind = KbdAction::Kind::Note;
            a.value = int(i);
            return a;
        }
    if (is("KeyZ")) a.kind = KbdAction::Kind::OctDown;
    else if (is("KeyX")) a.kind = KbdAction::Kind::OctUp;
    else if (is("KeyM")) a.kind = KbdAction::Kind::Metro;
    return a;
}

// ------------------------------------------------------------------------------------------- TklDrawing

void TklDrawing::setKeys(std::vector<Key> keys) {
    keys_ = std::move(keys);
    repaint();
}

const TklDrawing::Key* TklDrawing::key(std::string_view code) const noexcept {
    for (const Key& k : keys_)
        if (k.code == code) return &k;
    return nullptr;
}

Box TklDrawing::keyBox(const Key& k) const noexcept {
    return {box().x + k.box.x, box().y + k.box.y, k.box.w, k.box.h};
}

void TklDrawing::setDown(std::string_view code, bool down) {
    const auto it = down_.find(code);
    const bool was = it != down_.end() && it->second;
    if (was == down) return;
    down_[std::string(code)] = down;
    repaint();
}

void TklDrawing::setSeqDown(const std::vector<std::string>& codes) {
    std::map<std::string, bool, std::less<>> next;
    for (const std::string& c : codes) next[c] = true;
    if (next == seqDown_) return;
    seqDown_ = std::move(next);
    repaint();
}

void TklDrawing::clearDown() {
    if (down_.empty() && seqDown_.empty()) return;
    down_.clear();
    seqDown_.clear();
    repaint();
}

bool TklDrawing::down(std::string_view code) const {
    const auto a = down_.find(code);
    const auto b = seqDown_.find(code);
    return (a != down_.end() && a->second) || (b != seqDown_.end() && b->second);
}

void TklDrawing::paint(gemui::Painter& p) {
    for (const Key& k : keys_) paintKey(p, k);
}

void TklDrawing::paintKey(gemui::Painter& p, const Key& k) const {
    const Box b = keyBox(k);
    const Rect r = b.snapped();
    const Rect in{r.x + 2, r.y + 2, r.w - 4, r.h - 4};  // padding box (2 px border)
    const bool lit = (k.kind == Kind::Note || k.kind == Kind::Sharp) && !k.nope && down(k.code);
    // Background: colour of the class, 3 px hatching at 135° (.nope), halftone of a held key (.down, .sdown).
    // Hatching: d is the distance of the pixel's centre from the padding box's corner along the diagonal; 600, a
    // multiple of the 6 px period, keeps the operand of fmod positive on the border.
    auto stripes = [&](Color first, Color second) {
        for (int y = r.y; y < r.bottom(); ++y)
            for (int x = r.x; x < r.right(); ++x) {
                const double d = (x - in.x + 0.5 + y - in.y + 0.5) / std::sqrt(2.0);
                p.pixel(x, y, std::fmod(d + 600, 6.0) < 3 ? first : second);
            }
    };
    Color ink = colors::kBlack, line = colors::kBlack;
    switch (k.kind) {
    case Kind::Dead:
        p.fill(r, kDeadBg);
        ink = kDeadInk;
        line = kDeadLine;
        break;
    case Kind::Note:
        if (k.nope) {
            stripes(colors::kWhite, kNopeLight);
            ink = kDeadInk;
        } else if (lit) p.tile(r, gemui::tiles::halftone(colors::kBlack, colors::kWhite), {in.x, in.y});
        else p.fill(r, colors::kWhite);
        break;
    case Kind::Sharp:
        if (k.nope) {
            stripes(colors::kBlack, kNopeDark);
            ink = kNopeDarkInk;
        } else if (lit) p.tile(r, gemui::tiles::halftone(colors::kWhite, colors::kBlack), {in.x, in.y});
        else p.fill(r, colors::kBlack);
        if (!k.nope) ink = colors::kWhite;
        break;
    case Kind::Oct: p.fill(r, kOctBg); break;
    case Kind::Tr: p.fill(r, kTrBg); break;
    case Kind::OctNum:
        p.fill(r, k.cur ? colors::kBlack : kOctBg);
        if (k.cur) ink = colors::kWhite;
        break;
    }
    p.frame(r, 2, line);

    // Engraving (13 px, bold on a white key) and role (11 px, 6×6 font) centred in the key, clipped to its padding box
    // (overflow:hidden). Baseline: half the line spacing rounded up, like the browser; the synthetic bold does not
    // change the advance.
    const Rect clip = p.clip();
    p.setClip(in.intersect(clip));
    const bool bold = k.kind == Kind::Note;
    const double innerX = b.x + 2, innerW = b.w - 4;
    const double contentH = kLabelLine + (k.sub.empty() ? 0 : kSubLine);
    const double top = b.y + 2 + (b.h - 4 - contentH) / 2;
    {
        const Font::LineBox lb = Font::lineBox(k.label, 13);
        const double x = innerX + (innerW - Font::width(k.label, 13)) / 2;
        const int base = gemui::snap(top + std::floor((kLabelLine - lb.height()) / 2) + lb.ascent);
        p.textAt(k.label, x, base, 13, ink);
        if (bold) p.textAt(k.label, x + 1, base, 13, ink);  // synthetic bold: one pixel to the right
    }
    if (!k.sub.empty()) {
        // In 6×6 (6 px per letter instead of 5.5), a role wider than the key is tightened by one pixel per letter
        // rather than clipped (D-131).
        const gemui::Face small = gemui::Face::kSmall;
        const double sp = Font::width(k.sub, 11, 0, small) > innerW ? -1 : 0;
        const Font::LineBox lb = Font::lineBox(k.sub, 11, small);
        const double x = innerX + (innerW - Font::width(k.sub, 11, sp, small)) / 2;
        const int base = gemui::snap(top + kLabelLine + std::floor((kSubLine - lb.height()) / 2) + lb.ascent);
        p.textAt(k.sub, x, base, 11, ink, sp, small);
        if (bold) p.textAt(k.sub, x + 1, base, 11, ink, sp, small);
    }
    p.setClip(clip);
}

const TklDrawing::Key* TklDrawing::keyAt(gemui::Point pt) const noexcept {
    for (const Key& k : keys_)
        if (keyBox(k).snapped().contains(pt)) return &k;
    return nullptr;
}

void TklDrawing::mouseDown(const gemui::MouseEvent& e) {
    // pointerdown (left button): a key that plays is held until release or until the pointer leaves.
    if (e.button != gemui::MouseButton::Left) return;
    const Key* k = keyAt(e.pos);
    if (!k) return;
    const bool plays = (k->kind == Kind::Note || k->kind == Kind::Sharp) && !k->nope;
    if (!plays && k->kind != Kind::Oct && k->kind != Kind::OctNum) return;
    if (plays) pressed_ = k->code;
    const Key copy = *k;  // onPress may rebuild the keys
    if (onPress) onPress(copy);
}

void TklDrawing::mouseDrag(const gemui::MouseEvent& e) {
    // pointerleave: the pointer leaves the held key.
    if (pressed_.empty()) return;
    const Key* k = key(pressed_);
    if (k && keyBox(*k).snapped().contains(e.pos)) return;
    const Key copy = k ? *k : Key{};
    pressed_.clear();
    if (k && onRelease) onRelease(copy);
}

void TklDrawing::mouseUp(const gemui::MouseEvent&) {
    if (pressed_.empty()) return;
    const Key* k = key(pressed_);
    const Key copy = k ? *k : Key{};
    pressed_.clear();
    if (k && onRelease) onRelease(copy);
}

gemui::Pointer TklDrawing::pointerAt(gemui::Point p) const noexcept {
    const Key* k = keyAt(p);
    if (!k) return gemui::Pointer::Arrow;
    switch (k->kind) {
    case Kind::Note:
    case Kind::Sharp: return k->nope ? gemui::Pointer::NotAllowed : gemui::Pointer::Hand;
    case Kind::Oct:
    case Kind::OctNum: return gemui::Pointer::Hand;
    default: return gemui::Pointer::Arrow;
    }
}

std::string TklDrawing::tipAt(gemui::Point p) const {
    const Key* k = keyAt(p);
    return k ? k->tip : std::string();
}

void LegendSwatch::paint(gemui::Painter& p) {
    p.fill(bounds(), fill_);
    p.frame(bounds(), 2, colors::kBlack);
}

// ------------------------------------------------------------------------------------------- construction

KbdWindow::KbdWindow(MainWindow& main) : m_(main) {
    build();
    wire();
    place({double(kX), double(kY), 1005.5, 378});
}

void KbdWindow::build() {
    // Prototype ids of the sliders and buttons that MIDI learn can learn (D-146).
    for (const auto& [w, id] : std::initializer_list<std::pair<gemui::Widget*, const char*>>{
            {&octDn_, "kbdOctDn"}, {&octUp_, "kbdOctUp"}, {&heldSel_, "kbdHeldSel"}})
        w->setId(id);
    setTitle("COMPUTER KEYBOARD");
    closesOnEscape = false;  // not in the Escape CLOSERS
    octDn_.setTip("Move the played range one octave down (key Z)");
    octUp_.setTip("Move the played range one octave up (key X)");
    octV_.setTip("Lowest octave played by the keys");
    // 12 px .hint and 11 px legend: 6×6 font, at the widths of the prototype's font (D-071).
    range_.setFace(gemui::Face::kSmall);
    range_.setColor(colors::kHint);
    static constexpr std::array<const char*, 4> kLegend = {"note", "sharp", "octave", "transport & edit"};
    static constexpr std::array<Color, 4> kSwatch = {colors::kWhite, colors::kBlack, kOctBg, kTrBg};
    for (size_t i = 0; i < legend_.size(); ++i) {
        swatch_[i].setFill(kSwatch[i]);
        legend_[i].setText(kLegend[i]);
        legend_[i].setPx(11);
        legend_[i].setFace(gemui::Face::kSmall);
    }
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&top_, &labOct_, &octDn_, &octUp_, &octV_, &labHeld_,
                                                                   &heldSel_, &range_, &tkl_})
        add(*w);
    for (size_t i = 0; i < legend_.size(); ++i) {
        add(swatch_[i]);
        add(legend_[i]);
    }
}

void KbdWindow::wire() {
    onClose = [this] {
        // #kbdClose: nothing must stay held.
        m_.manPanic();
        tkl_.clearDown();
        held_.clear();
    };
    octDn_.onClick = [this] { m_.setBaseOctave(m_.baseOct_ - 1); };  // kbdOctShift(-1)
    octUp_.onClick = [this] { m_.setBaseOctave(m_.baseOct_ + 1); };
    heldSel_.onClick = [this] {
        std::vector<gemui::MenuItem> items;
        for (int i = 0; i < 2; ++i) {
            gemui::MenuItem it;
            it.label = i ? "Follow" : "Keep";
            it.pick = [this, i] {
                m_.host_.session().kbdFollow = i == 1;
                refresh();
            };
            items.push_back(std::move(it));
        }
        m_.popup(heldSel_, std::move(items), m_.host_.session().kbdFollow ? 1 : 0);
    };
    tkl_.onPress = [this](const TklDrawing::Key& k) {
        if (k.kind == TklDrawing::Kind::Oct) {
            // Octave key of the drawing: like Z and X, without transposing the held notes (buildKbd, buildTkl).
            m_.setBaseOctave(std::clamp(m_.baseOct_ + (k.code == "KeyZ" ? -1 : 1), 0, 9), false);
            return;
        }
        if (k.kind == TklDrawing::Kind::OctNum) {
            m_.setBaseOctave(k.octave);  // buildKbd, buildTkl, retuneHeld
            return;
        }
        if (k.pad >= 0) m_.pressPad(k.pad);  // percussion track (D-229)
        else m_.press((m_.baseOct_ + 1) * 12 + k.offset, 118, false);  // a click plays at velocity 118, a key at 127
        tkl_.setDown(k.code, true);
    };
    tkl_.onRelease = [this](const TklDrawing::Key& k) {
        m_.release(k.pad >= 0 ? MainWindow::kPadNote + k.pad : (m_.baseOct_ + 1) * 12 + k.offset);
        tkl_.setDown(k.code, false);
    };
}

// ------------------------------------------------------------------------------------------- state

std::string KbdWindow::layoutName() const {
    const std::string l = m_.host_.preference("kbdLayout", "auto");
    return l == "qwerty" || l == "azerty" || l == "qwertz" ? l : "auto";
}

void KbdWindow::show() {
    // openKbdWin: drawing rebuilt, window in front.
    open();
    refresh();
}

void KbdWindow::followLayout() {
    if (isOpen() && m_.keyLayout() != builtLayout_) refresh();
}

int KbdWindow::drumKind() const noexcept {
    if (!m_.drumTrack()) return 0;
    return m_.generator() == int(ymengine::Generator::Chipdrum) ? 1 : 2;
}

// Bits 0-1: drumKind(); bit 2 + i: Digidrum slot i is empty (its key says "No sample loaded yet").
int KbdWindow::padSignature() const {
    const int kind = drumKind();
    int sig = kind;
    if (kind == 2)
        for (int i = 0; i < 8; ++i)
            if (m_.extras_.digidrums[size_t(i)].empty()) sig |= 4 << i;
    return sig;
}

void KbdWindow::buildKeys() {
    // buildTkl, buildTklNum: keys at the current octave and in the current layout (Auto: the system's, D-215).
    const int oct = m_.baseOct_;
    const std::string lay = m_.keyLayout();
    const int drum = drumKind();
    std::vector<TklDrawing::Key> keys;
    const double ox = 0, oy = 0;  // boxes from the corner of the drawing; #tkl at (14, 82) in the window (layout)
    double tklW = 0;
    double y = oy;
    for (const auto& row : tklRows()) {
        double x = ox;
        for (const KeyDef& d : row) {
            if (!*d.code) {  // .kgap
                x += kKu * d.wu + kKeyGap;
                continue;
            }
            TklDrawing::Key k;
            k.code = d.code;
            // A key of wu units also covers the wu - 1 gaps of 4 px it spans (rounded), so the rows line up.
            const double w = kKu * d.wu + std::floor((d.wu - 1) * 4 + 0.5);
            k.box = {x, y, w, kKu};
            const auto cm = std::find(kCodeMap.begin(), kCodeMap.end(), std::string_view(d.code));
            const int idx = cm == kCodeMap.end() ? -1 : int(cm - kCodeMap.begin());
            const int pad = drum ? padOf(d.code) : -1;
            if (pad >= 0) {
                // Percussion track (D-229): the pad and its sound; the other note keys have no role.
                const std::string n = std::to_string(pad + 1);
                k.kind = TklDrawing::Kind::Note;
                k.offset = idx;
                k.pad = pad;
                k.sub = drum == 1 ? std::string(ymseq::kDrumNames[size_t(pad)]) : "Slot " + n;
                // The tooltips of the pads of the main window.
                k.tip = drum == 1 ? std::string(ymseq::kDrumFullNames[size_t(pad)]) +
                                        ": play it. Shape it in the Drum Kit window (Ctrl+K)."
                                  : "Digidrum slot " + n + ": play it and select it for editing." +
                                        (m_.extras_.digidrums[size_t(pad)].empty() ? " No sample loaded yet." : "");
            } else if (drum && (idx >= 0 || k.code == "KeyZ" || k.code == "KeyX")) {
                // Dead key.
            } else if (idx >= 0) {
                const int absN = (oct + 1) * 12 + idx;
                const std::string nm = kNN[size_t(idx)];
                k.kind = nm.size() > 1 ? TklDrawing::Kind::Sharp : TklDrawing::Kind::Note;
                k.sub = nm + (idx >= 12 ? "+" : "");
                k.offset = idx;
                k.nope = absN < kPlayLo || absN > kPlayHi;
                k.tip = k.nope ? nm + ": out of the chip range (B0 to C9)"
                               : "Plays " + nm + (idx >= 12 ? " one octave up" : "");
            } else if (k.code == "KeyZ" || k.code == "KeyX") {
                const bool dn = k.code == "KeyZ";
                k.kind = TklDrawing::Kind::Oct;
                k.sub = dn ? "Oct -" : "Oct +";
                k.tip = std::string("Move the keyboard one octave ") + (dn ? "down" : "up");
            } else {
                for (const Other& o : kOther)
                    if (o.code == k.code) {
                        k.kind = TklDrawing::Kind::Tr;
                        k.sub = std::string(o.what);
                        k.tip = std::string(o.what);
                    }
            }
            k.label = std::string(engraving(k.code, lay, d.label));
            keys.push_back(std::move(k));
            x += w + kKeyGap;
        }
        tklW = std::max(tklW, x - kKeyGap - ox);
        y += kKu + kKeyGap;
    }
    const double tklH = y - kKeyGap - oy;
    // #tklnum: 16 px from the drawing, aligned to the bottom (align-items:flex-end).
    const double nx = ox + tklW + 16, numH = 5 * kKu + 4 * kKeyGap, ny = oy + tklH - numH;
    for (const PadDef& d : kNumpad) {
        TklDrawing::Key k;
        k.code = d.code;
        k.label = d.label;
        k.box = {nx + d.col * (kKu + kKeyGap), ny + d.row * (kKu + kKeyGap), d.w * kKu + (d.w - 1) * kKeyGap,
                 d.h * kKu + (d.h - 1) * kKeyGap};
        const std::string_view code = d.code;
        if (!drum && code.size() == 7 && code.substr(0, 6) == "Numpad" && code[6] >= '0' && code[6] <= '9') {
            const int o = code[6] - '0';
            k.kind = TklDrawing::Kind::OctNum;
            k.octave = o;
            k.cur = o == oct;
            k.sub = "Oct " + std::to_string(o);
            k.tip = "Play from octave " + std::to_string(o);
        }
        keys.push_back(std::move(k));
    }
    tkl_.setKeys(std::move(keys));
    tkl_.setBox({box().x + 14, box().y + 82, nx + 4 * kKu + 3 * kKeyGap, tklH});
    builtOct_ = oct;
    builtLayout_ = lay;
    builtPads_ = padSignature();
}

void KbdWindow::refresh() {
    // refreshKbdOct, refreshHeldBtn, title (#kbdLay).
    const std::string lay = layoutName();
    std::string up = lay;
    for (char& c : up) c = char(std::toupper(static_cast<unsigned char>(c)));
    setTitle(lay == "auto" ? "COMPUTER KEYBOARD" : "COMPUTER KEYBOARD: " + up);
    if (!isOpen()) return;
    const int oct = m_.baseOct_;
    const bool follow = m_.host_.session().kbdFollow;
    octV_.setText("Oct " + std::to_string(oct));
    heldSel_.setText(follow ? "Follow" : "Keep");
    heldSel_.setTip(follow ? "Follow: a held note jumps to the new octave when you change it"
                           : "Keep: a held note stays at the pitch it was played, until you release it");
    const bool latin = m_.host_.preference("noteNames", "0") == "1";
    const int lo = (oct + 1) * 12, hi = lo + 17;  // C to F of the next octave
    const int drum = drumKind();
    // textContent with two spaces: the browser shows only one (white-space:normal).
    if (drum == 1) range_.setText("Pads 1 to 8: Kick to Tom 2");
    else if (drum == 2) range_.setText("Pads 1 to 8: Digidrum slots 1 to 8");
    else
        range_.setText(noteName(lo, latin) + " to " + noteName(hi, latin) +
                       (hi > kPlayHi || lo < kPlayLo ? " (partly out of the chip range)" : ""));
    // Percussion track (D-229): the pads do not depend on the octave.
    octDn_.setEnabled(!drum && oct > 0);
    octUp_.setEnabled(!drum && oct < 9);
    heldSel_.setEnabled(!drum);
    octV_.setDim(drum != 0);
    labOct_.setDim(drum != 0);
    labHeld_.setDim(drum != 0);
    legend_[0].setText(drum ? "pad" : "note");
    for (size_t i : {size_t(1), size_t(2)}) {
        swatch_[i].setVisible(!drum);
        legend_[i].setVisible(!drum);
    }
    if (oct != builtOct_ || m_.keyLayout() != builtLayout_ || padSignature() != builtPads_) {
        // Drawing rebuilt: only the keys held on the keyboard stay marked (kbdOctShift).
        tkl_.clearDown();
        buildKeys();
        for (const auto& [offset, h] : held_) tkl_.setDown(kCodeMap[size_t(offset)], true);
    }
    layout();
}

std::string KbdWindow::codeOf(int note) const {
    if (builtPads_ > 0) {
        // Percussion track (D-229): the pad of the note, as the engine plays it (Chipdrum: the note in the octave,
        // Digidrum: the slot of the pad that played it, otherwise the selected slot).
        const int pc = ((note % 12) + 12) % 12;
        int pad = pc % 8;
        if ((builtPads_ & 3) == 2)  // Digidrum (low bits of padSignature)
            pad = note >= MainWindow::kPadNote && note < MainWindow::kPadNote + 8
                      ? note - MainWindow::kPadNote
                      : int(m_.extras_.tracks[size_t(m_.cur_)].patch.digidrumSlot & 7);
        return std::string(kPadKeys[size_t(pad)]);
    }
    const int idx = note - (m_.baseOct_ + 1) * 12;
    return idx >= 0 && idx < int(kCodeMap.size()) ? std::string(kCodeMap[size_t(idx)]) : std::string();
}

void KbdWindow::light(int note, bool on) {
    // tklNote: key that plays this note at the current octave.
    const std::string c = codeOf(note);
    if (c.empty()) return;
    const TklDrawing::Key* k = tkl_.key(c);
    if (k && !k->nope) tkl_.setDown(c, on);
}

void KbdWindow::lightSeq(const std::vector<int>& notes) {
    // tklNotes: what the sequencer plays, without touching the hold.
    std::vector<std::string> codes;
    for (int n : notes) {
        const std::string c = codeOf(n);
        const TklDrawing::Key* k = c.empty() ? nullptr : tkl_.key(c);
        if (k && !k->nope) codes.push_back(c);
    }
    tkl_.setSeqDown(codes);
}

void KbdWindow::lightSeqPads(const std::vector<int>& pads) {
    std::vector<std::string> codes;
    for (int p : pads)
        if (p >= 0 && p < 8) codes.emplace_back(kPadKeys[size_t(p)]);
    tkl_.setSeqDown(codes);
}

void KbdWindow::notePressed(int offset) {
    // Playing is active while the window is open; auto-repeat is ignored; the actual note is kept to release it as it
    // is.
    if (!isOpen() || held_.count(offset)) return;
    if (drumKind()) {
        // Percussion track (D-229): only the pad keys play, like the pads.
        const int pad = padOf(kCodeMap[size_t(offset)]);
        if (pad < 0) return;
        held_[offset] = {MainWindow::kPadNote + pad, pad};
        m_.pressPad(pad);
        tkl_.setDown(kCodeMap[size_t(offset)], true);
        return;
    }
    const int n = (m_.baseOct_ + 1) * 12 + offset;
    held_[offset] = {n, -1};
    m_.press(n, 127, false);
    tkl_.setDown(kCodeMap[size_t(offset)], true);
}

void KbdWindow::keyReleased(const gemui::KeyEvent& e) {
    // keyup: the note played by this key, not the one of the current octave.
    const KbdAction a = kbdAction(e, m_.keyLayout());
    if (a.kind != KbdAction::Kind::Note) return;
    const auto it = held_.find(a.value);
    if (it != held_.end()) {
        const int n = it->second.note;
        held_.erase(it);
        m_.release(n);
    }
    tkl_.setDown(kCodeMap[size_t(a.value)], false);
}

void KbdWindow::retune() {
    // retuneHeld: in Follow, each note held on the keyboard moves to the current octave (if it is playable).
    if (!m_.host_.session().kbdFollow) return;
    for (auto& [offset, h] : held_) {
        const int now = (m_.baseOct_ + 1) * 12 + offset;
        if (h.pad >= 0 || now == h.note || now < kPlayLo || now > kPlayHi) continue;  // a pad stays (D-229)
        m_.release(h.note);
        h.note = now;
        m_.press(now, 127, false);
    }
}

// ------------------------------------------------------------------------------------------- layout

void KbdWindow::layout() {
    const double ox = box().x, oy = box().y;
    const double x = ox + 14;
    // Octave row (.row, gap 6, margin-bottom 10): under the 2 px border of .mixbody, padding 8, margin 6.
    const double y = oy + 32 + 2 + kPad + 6;
    double bx = x;
    auto put = [&](gemui::Widget& w, double width, double h, double marginLeft = 0) {
        bx += marginLeft;
        w.setBox({bx, y + (24 - h) / 2, width, h});
        bx += width + kGap;
    };
    put(labOct_, labOct_.naturalWidth(), 16);
    put(octDn_, octDn_.naturalWidth(), 24);
    put(octUp_, octUp_.naturalWidth(), 24);
    put(octV_, octV_.naturalWidth(), 16);
    put(labHeld_, labHeld_.naturalWidth(), 16, 16);
    put(heldSel_, heldSel_.naturalWidth(), 24);
    bx += 10;
    range_.setBox({bx, y + 9, range_.naturalWidth(), 12});  // .hint: margin-top 6, centred in the row

    if (tkl_.keys().empty()) buildKeys();
    tkl_.setBox({x, oy + 82, tkl_.box().w, tkl_.box().h});  // #tkl: under the Octave row (margin-bottom 10)
    const Box d = tkl_.box();

    // Legend (.row, margin-top 10, gap 14, 11 px): 11 px square (border of 2), 4 px, then "note"...
    const double ly = d.bottom() + 10;
    double lx = x;
    // Places at the widths of the prototype (5.5 px per letter at 11 px); the 6×6 text advances by 6 px.
    const double letter = Font::size(11).adv64 / 64.0;
    for (size_t i = 0; i < legend_.size(); ++i) {
        gemui::Label& l = legend_[i];
        if (!l.visible()) continue;  // percussion track: no sharp nor octave (D-229)
        swatch_[i].setBox({lx, ly, 11, 11});
        l.setBox({lx + 15 + letter, ly, l.naturalWidth(), 12});  // square, margin-right 4, space, text
        lx += 15 + letter * double(1 + Font::length(l.text())) + 14;
    }

    // Window: the drawing, padding of 8, border and margin of .gwin (6 on each side); bottom: legend line, 6 px,
    // padding, border and margin.
    const double contentW = d.w;
    const double w = contentW + 2 * kPad + 12;
    const double h = ly + 12 + 6 + kPad + 6 - oy;
    if (std::abs(h - box().h) > 0.01 || std::abs(w - box().w) > 0.01) place({ox, oy, w, h});
    top_.setBox({ox + 6, oy + 32, w - 12, 2});
    repaint();
}

std::optional<Rect> KbdWindow::rectOf(std::string_view id) const {
    if (!isOpen()) return std::nullopt;
    if (id == "kbdWin") return bounds();
    struct Named {
        std::string_view id;
        const gemui::Widget* w;
    };
    const Named named[] = {{"kbdOctDn", &octDn_}, {"kbdOctUp", &octUp_}, {"kbdOctV", &octV_},
                           {"kbdHeldSel", &heldSel_}, {"kbdRange", &range_}};
    for (const Named& n : named)
        if (id == n.id) return n.w->bounds();
    if (id == "tkl" || id == "tklnum") {
        // #tkl: keys from the F row to the Ctrl row; #tklnum: the keypad.
        Box u{};
        bool any = false;
        for (const TklDrawing::Key& k : tkl_.keys()) {
            const bool pad = k.code.rfind("Num", 0) == 0;
            if (pad != (id == "tklnum")) continue;
            const Box b = tkl_.keyBox(k);
            if (!any) u = b;
            const double r = std::max(u.right(), b.right()), btm = std::max(u.bottom(), b.bottom());
            u.x = std::min(u.x, b.x);
            u.y = std::min(u.y, b.y);
            u.w = r - u.x;
            u.h = btm - u.y;
            any = true;
        }
        return any ? std::optional<Rect>(u.snapped()) : std::nullopt;
    }
    if (const TklDrawing::Key* k = tkl_.key(id)) return tkl_.keyBox(*k).snapped();
    return std::nullopt;
}

}  // namespace stym::pluginui
