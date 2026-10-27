// STYM2149 — pluginui: GEM main window
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Behaviours taken from the prototype functions quoted in comments (refreshUI, syncTabs, focusCh, setEffect, setMode3,
// refreshAdsrScale, refreshAdsrEnable, buildPads, press / release, chanMenu, chipIssues, menus of the GEM bar). The
// functions that come with later versions keep their buttons and menu entries in place, which explains them.
//
// Map of the file (the File menu, its dialogues and its exports are in FileMenu.cpp):
// - Widgets. MainWindow is the root widget of the gemui::Desktop desk_, which covers the whole editor (kWidth ×
//   kHeight logical pixels). Its children, from back to front: the desktop icons, the menu bar, the main window
//   (frame_: title bar, track tabs, the SOURCE, ENVELOPE, VIBRATO, ADSR, GENERATOR, ADSR ENVELOPE and SYSTEM frames,
//   window buttons, oscilloscope, register monitor, keyboard or pads), then the floating windows and the MIDI learn
//   banner. The floating windows are members built with a reference to this window; as friends, they read its copies
//   of the state and call its private functions. Only the oscilloscopes of the tracks are created on demand (scopes_).
// - Data flow. The host (Host.h) holds the truth. tick(), called by the host on the message thread (30 times a second
//   in the plugin), copies the parameter values into values_ and, when its version changes, the non-automatable state
//   into extras_, then calls refresh() if anything changed. A control writes a parameter through set() (one host
//   gesture), which updates values_ at once, or the state through Host::editExtras, after which pullExtras() reads it
//   back when the screen must follow without waiting for the next tick.
// - refresh() pushes values_ and extras_ into every control of the main window (texts, values, greyed controls and
//   their tooltips), then calls layout() and the refresh() of each floating window. layoutHome() places the rows of the
//   frames like the prototype's CSS (placeRow below); layout() runs it at the home place of the window and carries the
//   result to where the user dragged it.
// - Menus. buildMenus() installs a callback that builds the entries of a menu title each time it opens, so that greyed
//   entries and check marks follow the current state; the pop-up menus of the controls are built on click (popup()).
// - Keyboard. The desktop passes to keyDown() the keys that no dialogue box or focused widget takes: shortcuts in the
//   order of the prototype's keydown handler, then the keys that play, handed to the Computer Keyboard window (which
//   also receives every key release, keyUp()).
// - Undo. The history lives in the EditorSession (Session.h). This file builds and applies the steps that are neither
//   a pattern nor the Song (undoEntryLike, applyUndoEntry: track, mixer, kit, samples, imports); armCur and pushCur
//   make one step per gesture on the focused track. The session also keeps the places of the windows while the editor
//   is closed (saveWindows, restoreWindows).
#include "pluginui/MainWindow.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <utility>
#include <vector>

#include "gemui/Alert.h"
#include "gemui/Font.h"
#include "plugincore/Mapping.h"
#include "pluginui/AppIcon.h"
#include "pluginui/BuzzerPresets.h"
#include "pluginui/IconData.h"
#include "ymengine/Pitch.h"
#include "ymseq/Tables.h"

namespace stym::pluginui {

using gemui::Box;
using gemui::Button;
using gemui::Label;
using gemui::MenuItem;
using gemui::Rect;
using plugin::Global;
using plugin::paramIndex;
using plugin::Track;
using ymengine::Generator;
namespace colors = gemui::colors;

namespace {

// Forms of the SID wave, in the order of ymengine::SidForm (24/10/2026), and in that of its menu: Bounce after
// Sine.
constexpr std::array<const char*, ymengine::kSidForms> kSidFormNames = {"Base", "Square", "Sine", "Custom", "Bounce"};
constexpr std::array<ymengine::SidForm, ymengine::kSidForms> kSidFormMenu = {
    ymengine::SidForm::Base, ymengine::SidForm::Square, ymengine::SidForm::Sine, ymengine::SidForm::Bounce,
    ymengine::SidForm::Custom};

// Prototype names; the Source menu adds the envelope (fifth DAW test, item 48): entry = tone + 2 × noise − 1
// + 3 × envelope, Env alone being entry 3.
constexpr std::array<const char*, 7> kSrcNames = {"Tone",    "Noise",    "Tone+Noise",    "Env",
                                                  "Tone+Env", "Noise+Env", "Tone+Noise+Env"};
// The following name tables are indexed by the value of their parameter: Track::VolMode, Global::VoiceMode
// (ymengine::VoiceMode), Global::ArpClock (the Clock selector), Track::AdsrMode and Track::Generator
// (ymengine::Generator).
constexpr std::array<const char*, 2> kVmNames = {"Vol 4-bit", "Envelope"};
constexpr std::array<const char*, 3> kModeNames = {"16 ch", "Authentic 3ch", "Dual chip 2x3ch"};
constexpr std::array<const char*, 2> kClkModes = {"Internal", "MIDI ext"};
constexpr std::array<const char*, 2> kAdsrModes = {"Smooth", "Chip"};
constexpr std::array<const char*, 6> kFxNames = {"Chip Voice", "SID Voice", "Digidrum", "Sync Buzzer", "Chipdrum", "DMA"};
constexpr std::array<int, 6> kFxOrder = {0, 1, 3, 4, 2, 5};  // melodic, drums, then the STE DMA
// Per generator code; DMA took ^D from the demo pattern (eighth DAW test, item 100).
constexpr std::array<char, 6> kFxKeys = {'V', 'S', 'I', 'U', 'C', 'D'};
// Register monitor: generator of the track (the prototype said NOISE DRUM for the Chipdrum).
// Chip Voice named like the others (ninth DAW test, item 105): the prototype showed "-".
constexpr std::array<const char*, 6> kFxShort = {"CHIP VOICE", "SID VOICE", "DIGIDRUM",
                                                 "SYNC BUZZER", "CHIPDRUM",  "DMA"};
constexpr std::array<const char*, 8> kDrumFull = {"Kick",     "Snare",      "Clap",  "Hat",
                                                  "Open Hat", "Closed Hat", "Tom 1", "Tom 2"};  // Chipdrum kit slots
// Names of the tracks in 3-channel mode: per chip, its three YM voices then its Digidrum track (1: Authentic 3ch,
// 2: Dual chip, chip 1 on tracks 1 to 4 and chip 2 on tracks 5 to 8).
constexpr std::array<const char*, 4> kVoiceNames1 = {"A", "B", "C", "Digi"};
constexpr std::array<const char*, 8> kVoiceNames2 = {"A1", "B1", "C1", "Digi1", "A2", "B2", "C2", "Digi2"};
constexpr std::array<const char*, 4> kVoiceLong1 = {"Voice A", "Voice B", "Voice C", "Digidrum"};
constexpr std::array<const char*, 8> kVoiceLong2 = {"Voice A1", "Voice B1", "Voice C1", "Digidrum 1",
                                                    "Voice A2", "Voice B2", "Voice C2", "Digidrum 2"};
constexpr std::array<const char*, 12> kNotesEn = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
constexpr std::array<const char*, 12> kNotesLat = {"Do",  "Do#", "Re",   "Re#", "Mi",  "Fa",
                                                   "Fa#", "Sol", "Sol#", "La",  "La#", "Si"};

// Parameters of a channel undo step (chanUndoEntry: patch, level, gate; neither the activation nor the mixer) and of a
// mixer undo step (mixUndoEntry: level, pan, mute, solo and send of each track, the right side of a stereo DMA sample).
bool chanField(Track f) noexcept { return f != Track::On && !plugin::mixerOnly(f); }
constexpr std::array<Track, 9> kMixFields = {Track::Level, Track::Pan,    Track::Mute,  Track::Solo, Track::Send,
                                             Track::LevelR, Track::MuteR, Track::SoloR, Track::Link};

constexpr int kPlayLo = 23, kPlayHi = 120;  // PLAY_LO, MAP_HI: B0 to C9
constexpr int kPolyMax = 6;                 // voices of a polyphonic track (named in the Voicing tooltip)
constexpr double kRowW = 204.5;             // usable width of a frame (228.5 - 2 × 12)
constexpr int kHelpLine = 32;               // help line of the prototype under the buttons, and its margin
// X coordinates: from the left edge of the main window (kWx + 18 = inner edge of its frames).
// Y coordinates: from the top of the desktop, the window being at its home place (kWinY); layout() carries both to
// where the window was dragged.
constexpr int kWx = MainWindow::kWinX;

// Frequency typed in MHz: digits with one decimal point or comma, four decimals kept (the fifth rounds); other
// characters are ignored. Nothing typed: nullopt. Hz, not clamped.
std::optional<long long> parseMHz(const std::string& text) {
    long long whole = 0, frac = 0;
    int decimals = 0, roundUp = 0;
    bool any = false, point = false;
    for (const char c : text) {
        if (c == '.' || c == ',') {
            point = true;
            continue;
        }
        if (c < '0' || c > '9') continue;
        any = true;
        // The whole part saturates at 1000 MHz, far above any clock, so that a long string of digits cannot overflow.
        if (!point) whole = std::min<long long>(whole * 10 + (c - '0'), 1000);
        else if (decimals < 4) {
            frac = frac * 10 + (c - '0');
            ++decimals;
        } else if (decimals++ == 4)
            roundUp = c >= '5';
    }
    if (!any) return std::nullopt;
    for (; decimals < 4; ++decimals) frac *= 10;
    // In 100 Hz units (1 MHz = 10 000 of them), then in Hz.
    return (whole * 10000 + frac + roundUp) * 100;
}

std::string fmt(const char* f, double v) { return gemui::formatDecimal(f, v); }

// ` "name"` after a slot number, nothing for a sample without a name.
std::string quotedName(const std::string& name) { return name.empty() ? std::string() : " \"" + name + "\""; }

// Item of a flex row of the prototype (.row: gap 8 px, items centred vertically).
struct Item {
    gemui::Widget* w;
    double width;        // < 0: takes the remaining space (slider)
    double height = 16;
    double marginTop = 0;  // .hint: margin-top 6 px
    double gap = 8;        // gap before the item (0 in the value + unit groups)
};

// Places a row and returns its height. The items keep their width; the one of negative width (one per row: the
// slider) takes what the others and the gaps leave of `width`. Each item is centred vertically in the height of the
// tallest, below its top margin. Unlike flexRow (Layout.h), the row never wraps.
double placeRow(double x, double y, std::vector<Item> items, double width = kRowW) {
    double fixed = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].width >= 0) fixed += items[i].width;
        if (i) fixed += items[i].gap;
    }
    double h = 0;
    for (Item& it : items) {
        if (it.width < 0) it.width = std::max(0.0, width - fixed);
        h = std::max(h, it.height + it.marginTop);
    }
    double cx = x;
    for (size_t i = 0; i < items.size(); ++i) {
        Item& it = items[i];
        if (i) cx += it.gap;
        const double top = y + (h - it.height - it.marginTop) / 2 + it.marginTop;
        it.w->setBox({cx, top, it.width, it.height});
        cx += it.width;
    }
    return h;
}

// Items of the usual kinds, at their natural size; minWidth lines up the sliders of a column behind labels of
// different widths.
Item label(Label& l, double minWidth = 0) { return {&l, std::max(minWidth, l.naturalWidth()), double(l.px())}; }
Item hintItem(Label& l) { return {&l, l.naturalWidth(), 12, 6}; }
Item button(Button& b) { return {&b, b.naturalWidth(), double(b.naturalHeight())}; }
Item value(gemui::ValueField& v, double gap = 8) { return {&v, v.naturalWidth(), 16, 0, gap}; }
Item unit(Label& l) { return {&l, l.naturalWidth(), 16, 0, 0}; }
Item unitW(Label& l, double width) { return {&l, width, 16, 0, 0}; }  // unit whose text changes: the row stays
Item slider(gemui::Slider& s) { return {&s, -1, 16}; }

void hintStyle(Label& l) { l.setColor(colors::kHint); }
// .hint of a frame, next to a button: 6×6 font, the 8×8 one would overflow the frame ("audio gain").
void frameHintStyle(Label& l) {
    hintStyle(l);
    l.setFace(gemui::Face::kSmall);
}

// div.hint across the width of the frame: 12 px lines; returns the height.
double placeParagraph(Paragraph& p, double x, double y) {
    // The line count depends on the width of the box: set the width first, then the height.
    p.setBox({x, y, kRowW, 12});
    const double h = 12.0 * p.lineCount();
    p.setBox({x, y, kRowW, h});
    return h;
}

int roundInt(double v) { return int(std::floor(v + 0.5)); }  // halves upwards, like Math.round in JavaScript

}  // namespace

// ------------------------------------------------------------------------------------------- construction

// The widgets exist as members; the constructor adds them to the tree, places the fixed ones, connects them, then
// reads the host once (tick) before putting the floating windows back where the session left them.
MainWindow::MainWindow(Host& host) : host_(host) {
    setBounds({0, 0, kWidth, kHeight});
    desk_.setRoot(this);
    desk_.onPress = [this](gemui::Widget* w) { pressed(w); };
    build();
    buildMenus();
    wireControls();
    desk_.setTooltips(host_.preference("tooltips", "1") != "0");
    // Tooltip language: the chosen one, otherwise the system's if the menu has it (its file installed
    // or added), otherwise English.
    scanTipLanguages();
    auto known = [this](std::string_view wanted) -> std::string {
        const std::string code = tipLanguageCode(wanted);
        if (code.empty()) return {};
        for (const TipChoice& c : tipChoices_)
            if (c.id == code) return c.id;
        for (const TipChoice& c : tipChoices_)  // "fr-FR": the language without its region
            if (c.id.size() == 2 && code.compare(0, 2, c.id) == 0 && code.size() > 2 && code[2] == '-') return c.id;
        return {};
    };
    tipLang_ = known(host_.preference("tipLanguage", ""));
    if (tipLang_.empty()) tipLang_ = known(host_.systemLanguage());
    if (tipLang_.empty()) tipLang_ = "en";
    applyTipLanguage();
    desk_.translateTip = [this](const std::string& tip) { return tipBook_.translate(tip); };
    // MIDI learn: map from the preferences (read again at each opening: another instance may have changed it), handed
    // back to the host; presses taken in learn mode.
    midiMap_ = plugin::parseMidiMap(host_.preference("midiMap", "{}"));
    pushCapture();
    desk_.interceptPress = [this](gemui::Widget* w) { return learnPress(w); };
    tick(0);
    restoreWindows();
}

MainWindow::~MainWindow() {
    // Editor closed: the places of the windows go to the session, a grid note still previewed is released (its timer
    // dies with the desktop), and the desktop lets go of this widget and its callbacks before the members go.
    saveWindows();
    if (previewN_ >= 0) host_.screenMidi(0x80, uint8_t(previewN_), 0);
    desk_.closePopups();
    desk_.onPress = nullptr;
    desk_.interceptPress = nullptr;
    desk_.setRoot(nullptr);
}

void MainWindow::build() {
    // GEM desktop (ninth DAW test, items 101 and 102): its icons, then the menu bar across its top; the windows stay
    // below the bar (Desktop::workTop).
    buildDeskIcons();
    add(menu_);
    menu_.setBounds({0, 0, kWidth, kMenuH});
    desk_.setWorkTop(kMenuH);
    add(frame_);
    frame_.add(title_);
    title_.setBounds({kWx + 6, kWinY + 6, 968, gemui::TitleBar::kHeight});
    title_.setTitle("STYM2149");
    menu_.addTitle("", &icons::kFuji);
    menu_.addTitle("File");
    menu_.addTitle("Edit");
    menu_.addTitle("Options");
    chipIndicator_ = menu_.addIndicator("CHIP: OK");
    midiIndicator_ = menu_.addIndicator("MIDI: -");
    menu_.setIndicatorTip(chipIndicator_, "Chip conformity");
    menu_.layout();

    for (int i = 0; i < plugin::kTracks; ++i) {
        Button& b = tabs_[size_t(i)];
        b.setStyle(Button::Style::Tab);
        b.setDisabledOpacity(gemui::kOpacityTabOff);
        frame_.add(b);
    }
    allOn_.setTip("Enable every track");
    allOff_.setTip("Disable every track");
    frame_.add(allOn_);
    frame_.add(allOff_);

    // Frames (sixth DAW test, item 70): SOURCE then VIBRATO; ENVELOPE, ADSR then ADSR ENVELOPE; GENERATOR;
    // SYSTEM. Each column ends at the bottom of the prototype's frames.
    // Four columns 228.5 px wide and 10 px apart, from kWx + 18; the first frame of each starts at y = 122. Here
    // the frames get their boxes and the controls their ranges and tooltips; layoutHome() places the rows inside.
    // SOURCE.
    frame_.add(srcFs_);
    srcFs_.setBox({kWx + 18, 122, 228.5, 274});
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&labSrc_, &srcSel_, &labPoly_, &polySel_, &labNp_,
                                                                  &np_, &npv_, &labVol_, &vol_, &volv_, &labVm_,
                                                                  &vmSel_, &labLevel_, &level_, &levelv_})
        srcFs_.add(*w);
    np_.setRange(1, 31, 1);  // tips set by refresh
    npv_.setMinChars(2);
    npv_.setPadChars(1);  // Volume's width: the slider ends with Volume's (twelfth DAW test, item 160)
    vol_.setRange(0, 15, 1);
    volv_.setMinChars(2);
    volv_.setPadChars(1);  // the width of Level's value: both sliders as wide (ninth DAW test, item 104)
    level_.setRange(0, 100, 1);
    level_.setTip("Channel level in the mix");
    levelv_.setMinChars(3);

    // ENVELOPE.
    frame_.add(envFs_);
    envFs_.setBox({kWx + 256.5, 122, 228.5, 118});  // without the space above Shape R13 (item 57)
    for (gemui::Widget* w :
         std::initializer_list<gemui::Widget*>{&labEf_, &ef_, &efv_, &efUnit_, &labEp_, &epv_, &shapeLab_, &shapeSel_})
        envFs_.add(*w);
    ef_.setRange(0, 4000, 1);
    ef_.setTip("Envelope frequency: the shape repeat rate");
    efv_.setMinChars(4);  // "_0.6" beside EP
    efv_.setMask("999.9");
    epv_.setMinChars(8);
    shapeSel_.setTip("Hardware envelope shape (register R13)");

    // VIBRATO.
    frame_.add(vibFs_);
    vibFs_.setBox({kWx + 18, 406, 228.5, 116});
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&labVibOn_, &vibOn_, &labDepth_, &vibDepth_,
                                                                  &vibDepthv_, &depthUnit_, &labRate_, &vibRate_,
                                                                  &vibRatev_, &rateUnit_})
        vibFs_.add(*w);  // tips set by refresh
    vibDepth_.setRange(0, 200, 1);
    vibDepthv_.setMinChars(3);
    vibDepthv_.setPadChars(1);  // the width of Rate's value: both sliders as wide (ninth DAW test, item 103)
    vibDepthv_.setMask("999");
    vibRate_.setRange(1, 20, 0.5);
    vibRatev_.setMinChars(4);
    vibRatev_.setMask("99.9");

    // ADSR.
    frame_.add(adsrFs_);
    adsrFs_.setBox({kWx + 256.5, 250, 228.5, 160});
    for (size_t k = 0; k < 4; ++k) {
        adsrFs_.add(adsrLab_[k]);
        adsrFs_.add(adsr_[k]);
        adsrFs_.add(adsrv_[k]);
        adsrFs_.add(adsrUnit_[k]);
    }
    adsrFs_.add(adsrModeLab_);
    adsrFs_.add(adsrMode_);
    adsrFs_.add(adsrHint_);
    frameHintStyle(adsrHint_);

    // GENERATOR.
    frame_.add(genFs_);
    genFs_.setBox({kWx + 495, 122, 228.5, 400});
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{
             &labType_, &fxSel_, &genSep_, &labRatio_, &ratio_, &ratioX_, &ratiov_, &labVmin_, &vmin_, &vminv_,
             &openKit_, &kitName_, &labSlot_, &labDigiCh_, &digiChSel_, &digiChHint_, &digiEdit_, &drumWave_,
             &digiHint_, &labDmaSlot_, &dmaEdit_, &dmaWave_, &dmaHint_})
        genFs_.add(*w);
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{
             &labSidWave_, &waveForm_, &sidWave_, &labWaveSteps_, &waveSteps_, &waveStepsv_, &labWavePeriod_,
             &wavePeriod_, &wavePeriodv_, &labWaveWidth_, &waveWidth_, &waveWidthv_,
             &labBuzTune_, &buzTuned_, &labBuzN_, &buzN_, &buzNv_, &labBuzD_, &buzD_, &buzDv_, &labBuzFine_, &buzFine_,
             &buzFinev_, &buzPad_, &buzPresets_, &labBuzzSteps_, &labBuzzLen_, &buzzLen_, &buzzLenv_, &buzzUnit_})
        genFs_.add(*w);
    for (Button& b : buzzStep_) genFs_.add(b);
    for (Button& b : slots_) genFs_.add(b);
    for (Button& b : dmaSlots_) genFs_.add(b);
    fxSel_.setTip("Sound generator of this channel: Chip Voice, SID Voice, Sync Buzzer, Chipdrum, Digidrum or the DMA "
                  "sound of the STE");
    genSep_.setBox({kWx + 497, 190, 224.5, 2});
    ratio_.setRange(25, 800, 1);
    ratio_.setTip("SID timer rate as a multiple of the note's frequency: 1 plays the note, 2 one octave higher");
    ratiov_.setMinChars(4);
    ratiov_.setMask("9.99");
    vmin_.setRange(0, 15, 1);
    vmin_.setTip("Floor of the software envelope: the level the sound never falls below");
    vminv_.setMinChars(2);
    // As wide as "x" and the ratio's field together: the slider ends with Timer ratio's (thirteenth DAW test, item 163).
    vminv_.setPadChars(3);
    // SID wave and Sync Buzzer steps, formerly in the Timer Wave window.
    static_assert(SidWaveGrid::kSteps == ymengine::kSidWaveSteps);
    // Steps from 2 (the square of Base has 2).
    waveSteps_.setRange(2, ymengine::kSidWaveSteps, 1);
    waveStepsv_.setMinChars(2);
    waveStepsv_.setMask("99");
    waveStepsv_.setPadChars(3);  // like SID min vol: the slider ends with Timer ratio's
    waveSteps_.setTip("Steps of the SID wave, one per timer interrupt: those of a period of the Square, or the bars "
                      "played of Sine, Bounce and Custom. Base has none");
    waveForm_.setTip("Form of the SID wave: Base, the plain square; Square, a square of Steps steps, the note volume "
                     "over Width of them; Sine, a sine of Period bars; Bounce, a sine of the chip's level, which the "
                     "bars show as a bounce; Custom, the bars drawn with the mouse");
    // As wide as its widest form, so that it keeps its size from one form to the other.
    double formW = 0;
    for (const char* name : kSidFormNames) {
        waveForm_.setText(name);
        formW = std::max(formW, waveForm_.naturalWidth());
    }
    waveForm_.setMinWidth(formW);
    // Period of the sine in bars, 8 by default (24/10/2026; formerly a Phase slider): like Steps, its
    // field as wide as Timer ratio's.
    wavePeriod_.setRange(ymengine::kSidWavePeriodMin, ymengine::kSidWaveSteps, 1);
    wavePeriodv_.setMinChars(2);
    wavePeriodv_.setMask("99");
    wavePeriodv_.setPadChars(3);
    wavePeriod_.setTip("Period of the Sine-SID in bars (8 by default): the bars of one cycle of the sine. Sine and "
                       "Bounce only");
    // Width of the Square in steps, up to Steps - 1 (refresh), 4 of the 8 Steps (half and half) by default (24/10/2026).
    waveWidth_.setRange(1, ymengine::kSidWidthMax, 1);
    waveWidthv_.setMinChars(2);
    waveWidthv_.setMask("99");
    waveWidthv_.setPadChars(3);
    waveWidth_.setTip("Width of the Square: the note volume over Width of its Steps, SID min vol over the rest (half "
                      "and half by default). Square only");
    labSidWave_.setTip("SID wave: one volume per timer interrupt, scaled by the note volume; the timer runs Steps/2 "
                       "times faster, so the pitch stays.");
    buzzLen_.setRange(0, ymengine::kBuzzSteps, 1);
    buzzLenv_.setMinChars(1);
    buzzLenv_.setMask("9");
    buzzLen_.setTip("Steps of the Sync Buzzer, one per restart (0: the track's shape every time)");
    labBuzzSteps_.setTip("Sync Buzzer: each restart writes the next envelope shape (R13). Trk: the track's shape. "
                         "Right click clears a step.");
    for (int k = 0; k < ymengine::kBuzzSteps; ++k) {
        buzzStep_[size_t(k)].setPadding(2, 4);
        buzzStep_[size_t(k)].setTip("Sync Buzzer step " + std::to_string(k + 1) +
                                    ": envelope shape. Right click: the track's shape");
    }
    // Sync Buzzer: the controls of the Buzzer window, with its tips.
    buzTuned_.setTip("Tuned envelope: the envelope period follows the note (Env freq is then unused)");
    buzN_.setRange(1, ymengine::kEnvRatioMax, 1);
    buzD_.setRange(1, ymengine::kEnvRatioMax, 1);
    buzFine_.setRange(-ymengine::kEnvFineMax, ymengine::kEnvFineMax, 1);
    // Typed values (sixth DAW test, item 66), as wide as "+100c" so that the three sliders line up.
    for (gemui::ValueField* f : {&buzNv_, &buzDv_}) {
        f->setMinChars(2);
        f->setPadChars(3);
        f->setMask("99");
    }
    buzFinev_.setMinChars(5);  // signed: typed without a mask
    buzFinev_.setUnit("c");    // out of the reverse video
    buzN_.setTip("N: the envelope plays the note x N / D");
    buzD_.setTip("D: the envelope plays the note x N / D");
    buzFine_.setTip("Fine tune of the envelope, in cents");
    buzPad_.setTip("YM Pad: the tone runs at period 0 under the envelope, about -6 dB");
    buzPresets_.setTip("Sync Buzzer presets: shape, tuning (N / D) and YM Pad in one click");
    openKit_.setTip("Drum Kit: shape the eight Chipdrum instruments (Ctrl+K)");
    frameHintStyle(kitName_);
    for (int k = 0; k < ymengine::kDigidrumSlots; ++k) {
        Button& b = slots_[size_t(k)];
        b.setText(std::to_string(k + 1));
        b.setTip("Digidrum slot " + std::to_string(k + 1) +
                 ": pick it. In the sequencer, the row you write on chooses the slot. Load and edit the samples with "
                 "Digidrum samples.");
    }
    digiChSel_.setTip("ST voice used by the Digidrum track in 3 ch mode");
    digiChHint_.setText("3ch: the drum takes over this channel");
    digiEdit_.setTip("Digidrums window: load the eight samples, set their rate, root note, tuning and loop");
    // an empty slot plays nothing (the prototype announced a built-in DrumNoise kit).
    // the rate is that of the slot (7 800 Hz by default), set in the Digidrum editor. the drawing no
    // longer loads a file, the Digidrum samples button opens the editor on the slot.
    digiHint_.setText("4-bit samples at the slot rate.");
    // DMA type: one of the eight samples of the bank, tuned from its root note.
    for (int k = 0; k < ymengine::kDmaSlots; ++k) dmaSlots_[size_t(k)].setText(std::to_string(k + 1));
    dmaEdit_.setTip("DMA window: load the eight samples, set their rate, format, root note and loop");

    // ADSR curve, under ADSR: 22 px taller than the prototype's, down to the bottom of the column.
    frame_.add(curveFs_);
    curveFs_.setBox({kWx + 256.5, 420, 228.5, 102});
    curveFs_.setLegendInset(10);
    curveFs_.add(curve_);
    curve_.setBox({kWx + 266.5, 442, 208.5, 74});

    // SYSTEM.
    frame_.add(sysFs_);
    sysFs_.setBox({kWx + 733.5, 122, 228.5, 400});
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{
             &labVoices_, &modeSel_, &labComputer_, &compSel_, &labClock_, &clkSel_, &clkStatus_,
             &labMaster_, &master_, &masterv_, &labLeds_, &leds_, &labNote_, &tpread_})
        sysFs_.add(*w);
    modeSel_.setTip(
        "16 ch: sixteen channels with panning and polyphony. 3 ch: the authentic YM, three voices, mono");
    // Machine (STYM2149 row): the STE adds the DMA sound. A menu button since the thirteenth DAW test
    // (item 164); its tooltip, the lines of the former ST and STE buttons.
    compSel_.setTip("Computer: Atari ST, the YM alone\nComputer: Atari STE, the YM plus the DMA sound: eight DMA tracks "
                    "in 3 ch, the DMA type on any track in 16 ch");
    clkSel_.setTip("Clock source: internal tempo, or follow an incoming MIDI clock");
    master_.setRange(0, 100, 1);
    master_.setTip("Master output level");
    masterv_.setMinChars(3);

    // Window buttons.
    static constexpr std::array<const char*, 6> kWinTips = {
        "Mixer: levels, panning, mute and solo for every channel (Ctrl+M)",
        "Arpeggiator: play a chord one note at a time (Ctrl+B)",
        "Modulation: per-tick table for pitch, noise and volume (Ctrl+R)",
        "Effects: echo of the notes, and the audio delay in 16 ch (Ctrl+F)",
        "Step Sequencer: write patterns and chain them into a song (Ctrl+E)",
        "Patches: save and recall channel settings (Ctrl+P)"};
    // Centred in the window, over the width of the oscilloscope (fifth DAW test, item 49); the prototype
    // put them at its left edge.
    double total = 6.0 * double(winBtns_.size() - 1);
    for (const Button& b : winBtns_) total += b.naturalWidth();
    double x = kWx + 18 + std::round((944 - total) / 2);
    for (size_t i = 0; i < winBtns_.size(); ++i) {
        Button& b = winBtns_[i];
        b.setTip(kWinTips[i]);
        frame_.add(b);
        const double w = b.naturalWidth();
        b.setBox({x, 532, w, double(b.naturalHeight())});
        x += w + 6;
    }

    // Oscilloscope, register monitor, then the keyboard or the pads in the same place (one of them is shown, according
    // to the generator), placed by layoutHome: the oscilloscope can be hidden.
    frame_.add(scope_);
    frame_.add(regs_);
    frame_.add(kbd_);
    frame_.add(pads_);

    latchBtn_.setTip("Latch: keys stay held until you press them again");
    layerBtn_.setTip("Play every enabled track instead of the focused one");
    octDown_.setTip("Move the virtual keyboard one octave down (key Z)");
    octUp_.setTip("Move the virtual keyboard one octave up (key X)");
    octv_.setTip("Octaves shown on the virtual keyboard");
    kbdMap_.setTip("Open the computer keyboard: play with the keys or click them. Typing only plays while this "
                   "window is open.");
    // No help line of shortcuts under the buttons (sixth DAW test, item 69).
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&latchBtn_, &layerBtn_, &octDown_, &octUp_, &octv_,
                                                                  &kbdMap_})
        frame_.add(*w);

    // Floating windows, in front of the main window.
    add(seq_);
    add(song_);
    add(mix_);
    add(arp_);
    add(orn_);
    add(kit_);
    add(lib_);
    add(ins_);
    add(dma_);
    add(digi_);
    add(recWin_);
    add(ymWin_);
    add(curveWin_);
    add(kbdWin_);
    add(echo_);
    add(settings_);
    add(stepWin_);
    add(help_);
    add(badge_);  // #learnBadge, in front of the windows

    // Prototype ids of the sliders and buttons that MIDI learn can learn. The learned map (preferences) stores
    // these ids, so they must not change.
    for (const auto& [w, id] : std::initializer_list<std::pair<gemui::Widget*, const char*>>{
            {&srcSel_, "srcSel"}, {&polySel_, "polySel"}, {&vmSel_, "vmSel"}, {&np_, "np"}, {&vol_, "vol"},
            {&level_, "chLevel"}, {&ef_, "ef"}, {&shapeSel_, "shapeSel"}, {&vibDepth_, "vibDepth"},
            {&vibOn_, "vibOn"}, {&vibRate_, "vibRate"}, {&adsr_[0], "atk"}, {&adsr_[1], "dcy"}, {&adsr_[2], "sus"},
            {&adsr_[3], "rel"},
            {&adsrMode_, "adsrMode"}, {&fxSel_, "fxSel"}, {&ratio_, "ratio"}, {&vmin_, "vmin"},
            {&openKit_, "openKitBtn"}, {&digiChSel_, "digiChSel"}, {&clkSel_, "clkSel"}, {&modeSel_, "modeSel"},
            {&master_, "master"}, {&winBtns_[0], "openMixBtn"}, {&winBtns_[1], "openArpBtn"},
            {&winBtns_[2], "openOrnBtn"}, {&winBtns_[3], "openFxBtn"}, {&winBtns_[4], "openSeqBtn"},
            {&winBtns_[5], "openLibBtn"}, {&latchBtn_, "kbLatch"}, {&layerBtn_, "kbLayer"}, {&octDown_, "octDown"},
            {&octUp_, "octUp"},
            {&kbdMap_, "kbdMapBtn"}})
        w->setId(id);
    // Direct entry fields, by prototype id (tests): MIDI learn learns only the sliders and the buttons.
    for (const auto& [f, id] : std::initializer_list<std::pair<gemui::ValueField*, const char*>>{
            {&efv_, "efv"}, {&epv_, "epv"}, {&adsrv_[0], "atkv"}, {&adsrv_[1], "dcyv"}, {&adsrv_[2], "susv"},
            {&adsrv_[3], "relv"}, {&buzNv_, "genNv"}, {&buzDv_, "genDv"}, {&buzFinev_, "genFinev"},
            {&waveStepsv_, "waveStepsv"}, {&wavePeriodv_, "wavePeriodv"}, {&waveWidthv_, "waveWidthv"},
            {&buzzLenv_, "buzzStepsv"}})
        f->setId(id);
}

// A slider held with the mouse is a host gesture that lasts until it is released: while dragging_ is set, set() opens
// a gesture at the first value of each parameter it writes and leaves it open, and refresh() does not move the slider
// under the pointer.
void MainWindow::bindSlider(gemui::Slider& s, std::function<void(double)> apply) {
    s.onGestureStart = [this, &s] { dragging_ = &s; };
    s.onChange = std::move(apply);
    s.onGestureEnd = [this] {
        // One gesture per parameter touched during the drag, opened at its first value (set()).
        for (int i : gestures_) host_.endGesture(i);
        gestures_.clear();
        dragging_ = nullptr;
        refresh();
    };
}

// Callbacks of the controls. The pattern repeats: a slider writes its parameter through bindSlider, the value field
// beside it (onCommit) parses the typed text, clamps it to the slider's range and returns false to refuse it, and a
// selector button opens a pop-up menu built at the click from the current values.
void MainWindow::wireControls() {
    title_.onClose = [this] {
        if (host_.standalone()) confirmQuit();
    };
    title_.setCloseTip(host_.standalone() ? "Quit STYM2149" : "Close the plugin window from the host");
    // Moved by its title bar like the other windows (third DAW test, item 25).
    title_.setMovable(true);
    title_.onGrab = [this](gemui::Point p) { grab_ = {p.x - frame_.bounds().x, p.y - frame_.bounds().y}; };
    title_.onDrag = [this](gemui::Point p) { moveMainTo(p.x - grab_.x, p.y - grab_.y); };

    for (int i = 0; i < plugin::kTracks; ++i) {
        tabs_[size_t(i)].onClick = [this, i] { tabClicked(i); };
        tabs_[size_t(i)].onContext = [this, i](gemui::Point p) { tabMenu(i, p); };
    }
    allOn_.onClick = [this] { setAllEnabled(true); };
    allOff_.onClick = [this] { setAllEnabled(false); };

    // SOURCE.
    srcSel_.onClick = [this] {
        std::vector<MenuItem> items;
        for (int i = 0; i < int(kSrcNames.size()); ++i) {
            MenuItem m;
            m.label = kSrcNames[size_t(i)];
            // A SID Voice writes its own volumes: the envelope entries do nothing there.
            m.disabled = i >= 3 && generator() == int(Generator::Sid) && !drumTrack();
            if (m.disabled) m.why = "A SID Voice writes its own volumes: the envelope is not used";
            m.pick = [this, i] {  // one undo step for the three settings
                const int bits = i < 3 ? i + 1 : i - 3;  // tone + 2 × noise
                set(Track::Tone, float(bits & 1));
                set(Track::Noise, float(bits >> 1));
                set(Track::VolMode, i < 3 ? 0.0f : 1.0f);
            };
            items.push_back(std::move(m));
        }
        trackPopup(srcSel_, std::move(items), srcIndex());  // "Off" (-1): no marks
    };
    polySel_.onClick = [this] {
        std::vector<MenuItem> items;
        for (int i = 0; i < 2; ++i) {
            MenuItem m;
            m.label = i ? "Monophonic" : "Polyphonic";
            m.pick = [this, i] { set(Track::Voicing, float(i)); };
            items.push_back(std::move(m));
        }
        trackPopup(polySel_, std::move(items), iv(Track::Voicing));
    };
    bindSlider(np_, [this](double v) {
        set(Track::NoisePeriod, float(v));
        if (mode3())  // 3 channels: a single noise generator
            for (int t = 0; t < plugin::kTracks; ++t)
                if (t != cur_) set(paramIndex(t, Track::NoisePeriod), float(v));
    });
    npv_.onCommit = [this](const std::string& s) {
        const double v = gemui::readDecimal(s);
        if (s.empty() || !std::isfinite(v)) return false;
        np_.onChange(std::clamp(std::round(v), 1.0, 31.0));
        return true;
    };
    bindSlider(vol_, [this](double v) { set(Track::Volume, float(v)); });
    volv_.onCommit = [this](const std::string& s) {
        const double v = gemui::readDecimal(s);
        if (s.empty() || !std::isfinite(v)) return false;
        set(Track::Volume, float(std::clamp(std::round(v), 0.0, 15.0)));
        return true;
    };
    vmSel_.onClick = [this] {
        std::vector<MenuItem> items;
        for (int i = 0; i < 2; ++i) {
            MenuItem m;
            m.label = kVmNames[size_t(i)];
            m.pick = [this, i] {
                set(Track::VolMode, float(i));
                // Vol 4-bit without square or noise would be a constant level: the square comes back.
                if (i == 0 && iv(Track::Tone) == 0 && iv(Track::Noise) == 0) set(Track::Tone, 1.0f);
            };
            items.push_back(std::move(m));
        }
        trackPopup(vmSel_, std::move(items), iv(Track::VolMode));
    };
    bindSlider(level_, [this](double v) { set(Track::Level, float(v)); });
    levelv_.onCommit = [this](const std::string& s) {
        const double v = gemui::readDecimal(s);
        if (s.empty() || !std::isfinite(v)) return false;
        set(Track::Level, float(std::clamp(std::round(v), 0.0, 100.0)));
        return true;
    };

    // ENVELOPE: geometric slider over the whole period range of the chip, EP = 65535^(1 - ef / 4000).
    bindSlider(ef_, [this](double v) {
        const double ep = std::pow(65535.0, 1 - v / 4000);
        set(Track::EnvPeriod, float(std::clamp(std::round(ep), 1.0, 65535.0)));
    });
    // Env freq typed in Hz: the envelope cycle lasts 256 × EP clock periods, so EP = clock / (256 × f).
    efv_.onCommit = [this](const std::string& s) {
        const double f = gemui::readDecimal(s);
        if (!std::isfinite(f) || f <= 0) return false;
        const double clk = plugin::chipClock(values_).hz();
        set(Track::EnvPeriod, float(std::clamp(std::round(clk / (256 * f)), 1.0, 65535.0)));
        return true;
    };
    // EP typed in decimal, or in hexadecimal after a "$" as the field shows it ("12800 ($3200)").
    epv_.onCommit = [this](const std::string& s) {
        char* end = nullptr;
        const long v = !s.empty() && s[0] == '$' ? std::strtol(s.c_str() + 1, &end, 16) : std::strtol(s.c_str(), &end, 10);
        if (end == s.c_str() || (s[0] == '$' && end == s.c_str() + 1)) return false;
        set(Track::EnvPeriod, float(std::clamp<long>(v, 1, 65535)));
        return true;
    };
    shapeSel_.onClick = [this] {
        // Shapes 8 to F only (third DAW test, item 22): 0 to 3 play as 9 and 4 to 7 as F. A value below 8 (YM
        // import, MIDI CC) stays as it is and the menu marks the shape it plays as.
        std::vector<MenuItem> items;
        for (int i = 8; i < 16; ++i) {
            MenuItem m;
            m.label = std::string(1, "0123456789ABCDEF"[i]);
            m.icon = &icons::kShapes[size_t(i)];
            m.pick = [this, i] { set(Track::EnvShape, float(i)); };
            items.push_back(std::move(m));
        }
        const int shape = std::clamp(iv(Track::EnvShape), 0, 15);
        trackPopup(shapeSel_, std::move(items), shape >= 8 ? shape - 8 : shape < 4 ? 9 - 8 : 15 - 8);
    };

    // VIBRATO.
    vibOn_.onClick = [this] { set(Track::VibOn, iv(Track::VibOn) ? 0.0f : 1.0f); };
    bindSlider(vibDepth_, [this](double v) { set(Track::VibDepth, float(v)); });
    vibDepthv_.onCommit = [this](const std::string& s) {
        const double v = gemui::readDecimal(s);
        if (s.empty() || !std::isfinite(v)) return false;
        set(Track::VibDepth, float(std::clamp(std::round(v), 0.0, 200.0)));
        return true;
    };
    bindSlider(vibRate_, [this](double v) { set(Track::VibRate, float(v)); });
    vibRatev_.onCommit = [this](const std::string& s) {
        const double v = gemui::readDecimal(s);
        if (s.empty() || !std::isfinite(v)) return false;
        set(Track::VibRate, float(std::clamp(std::round(v * 2) / 2, 1.0, 20.0)));
        return true;
    };

    // ADSR: sliders in the unit of the mode (ms and % in Smooth, VBL ticks and level 0-15 in Chip).
    static constexpr std::array<Track, 4> kAdsrParams = {Track::Attack, Track::Decay, Track::Sustain, Track::Release};
    for (size_t k = 0; k < 4; ++k) {
        // The parameters stay in ms and % whatever the mode; in Chip, the shown value is converted back: level 0-15
        // to %, ticks to ms at the VBL rate.
        auto store = [this, k](double shown) {
            if (!adsrChip()) return shown;
            return k == 2 ? std::round(shown / 15 * 100) : std::round(shown * msPerTick());
        };
        bindSlider(adsr_[k], [this, k, store](double v) { set(kAdsrParams[k], float(store(v))); });
        adsrv_[k].onCommit = [this, k, store](const std::string& s) {
            const double n = gemui::readDecimal(s);
            if (s.empty() || !std::isfinite(n)) return false;
            const double shown = std::clamp(std::round(n), adsr_[k].min(), adsr_[k].max());
            set(kAdsrParams[k], float(store(shown)));
            return true;
        };
    }
    adsrMode_.onClick = [this] {
        if (mode3()) return;  // 3 channels: Chip forced
        std::vector<MenuItem> items;
        for (int i = 0; i < 2; ++i) {
            MenuItem m;
            m.label = kAdsrModes[size_t(i)];
            m.pick = [this, i] { set(Track::AdsrMode, float(i)); };
            items.push_back(std::move(m));
        }
        trackPopup(adsrMode_, std::move(items), iv(Track::AdsrMode));
    };

    // GENERATOR.
    fxSel_.onClick = [this] {
        const ymengine::VoiceMode mode = plugin::voiceMode(values_);
        // In 3-channel mode, the fourth track of each chip (4, and 8 in Dual chip) is its Digidrum track.
        const bool digi = mode3() && (cur_ & 3) == 3 && cur_ < 4 * chips();
        const bool dmaTr = dmaTrack(cur_);
        std::vector<MenuItem> items;
        int checked = -1;
        for (size_t k = 0; k < kFxOrder.size(); ++k) {
            const int fx = kFxOrder[k];
            if (fx == int(Generator::Dma) && !ste()) continue;  // the DMA type exists only with the STE machine
            if (k == 3 || k == 5) items.push_back(MenuItem::separatorItem());  // before the drums, the DMA
            MenuItem m;
            m.label = kFxNames[size_t(fx)];
            if (kFxKeys[size_t(fx)]) m.key = std::string("^") + kFxKeys[size_t(fx)];
            if (!plugin::generatorAllowed(mode, ste(), cur_, Generator(fx))) {
                m.disabled = true;
                if (digi) m.why = "The Digidrum track plays samples only";
                else if (dmaTr) m.why = "This DMA track plays the DMA sound of the STE only";
                else if (fx == int(Generator::Dma)) m.why = "In 3 ch mode, the DMA tracks play the DMA sound";
                else m.why = "Samples are played by the Digidrum track, not by a hardware voice";
            }
            m.pick = [this, fx] { pickGenerator(fx); };
            if (fx == generator()) checked = int(k);
            items.push_back(std::move(m));
        }
        trackPopup(fxSel_, std::move(items), checked);
    };
    bindSlider(ratio_, [this](double v) { set(Track::SidRatio, float(v / 100)); });
    ratiov_.onCommit = [this](const std::string& s) {
        const double v = gemui::readDecimal(s);
        if (s.empty() || !std::isfinite(v)) return false;
        set(Track::SidRatio, float(std::clamp(std::round(v * 100), 25.0, 800.0) / 100));
        return true;
    };
    bindSlider(vmin_, [this](double v) { set(Track::SidVmin, float(v)); });
    vminv_.onCommit = [this](const std::string& s) {
        const double v = gemui::readDecimal(s);
        if (s.empty() || !std::isfinite(v)) return false;
        set(Track::SidVmin, float(std::clamp(std::round(v), 0.0, 15.0)));
        return true;
    };
    openKit_.onClick = [this] { kit_.show(); };  // openKit: already open, comes back to the front
    for (int k = 0; k < ymengine::kDigidrumSlots; ++k) slots_[size_t(k)].onClick = [this, k] { selectSlot(k); };
    // ST channel that the Digidrum track of the focused track's chip takes over (tracks 1-4: chip 1, 5-8: chip 2).
    digiChSel_.onClick = [this] {
        const int chip = chips() > 1 ? (cur_ >> 2) & 1 : 0;
        std::vector<MenuItem> items;
        for (int i = 0; i < 3; ++i) {
            MenuItem m;
            m.label = std::string(1, char('A' + i)) + (chips() > 1 ? std::to_string(chip + 1) : std::string());
            m.pick = [this, chip, i] {
                host_.editExtras([chip, i](plugin::NonParamState& s) { s.global.digidrumChannel[size_t(chip)] = uint8_t(i); });
            };
            items.push_back(std::move(m));
        }
        popup(digiChSel_, std::move(items), extras_.global.digidrumChannel[size_t(chip)]);
    };
    digiEdit_.onClick = [this] {
        digi_.selectSlot(extras_.tracks[size_t(cur_)].patch.digidrumSlot);
        digi_.show();
    };
    for (int k = 0; k < ymengine::kDmaSlots; ++k) dmaSlots_[size_t(k)].onClick = [this, k] { selectDmaSlot(k); };
    dmaEdit_.onClick = [this] { dma_.showSlot(dmaSlot(cur_)); };
    // Sync Buzzer (third DAW test, item 28): the settings of the former Buzzer window, one undo step
    // each.
    buzTuned_.onClick = [this] { editPatch([](ymengine::TrackPatch& p) { p.envTuned = !p.envTuned; }); };
    buzPad_.onClick = [this] { editPatch([](ymengine::TrackPatch& p) { p.ymPad = !p.ymPad; }); };
    bindSlider(buzN_, [this](double v) { editPatch([v](ymengine::TrackPatch& p) { p.envRatioN = uint8_t(v); }); });
    bindSlider(buzD_, [this](double v) { editPatch([v](ymengine::TrackPatch& p) { p.envRatioD = uint8_t(v); }); });
    bindSlider(buzFine_, [this](double v) { editPatch([v](ymengine::TrackPatch& p) { p.envFineCents = v; }); });
    // Their values typed (sixth DAW test, item 66), like the Steps of the SID wave and of the Sync Buzzer.
    auto typeInto = [](gemui::Slider& sl) {
        return [&sl](const std::string& t) {
            const auto v = gemui::typedNumber(t);
            if (!v) return false;
            sl.input(*v);
            return true;
        };
    };
    buzNv_.onCommit = typeInto(buzN_);
    buzDv_.onCommit = typeInto(buzD_);
    buzFinev_.onCommit = typeInto(buzFine_);
    waveStepsv_.onCommit = typeInto(waveSteps_);
    wavePeriodv_.onCommit = typeInto(wavePeriod_);
    waveWidthv_.onCommit = typeInto(waveWidth_);
    buzzLenv_.onCommit = typeInto(buzzLen_);
    // SID wave and Sync Buzzer steps of the former Timer Wave window (sixth DAW test, items 52 to 54 and 80):
    // one undo step per setting or gesture.
    // Sine lit, or Bounce: a bar moved shifts the sine to the phase that brings it nearest
    // (24/10/2026), the other bars following.
    sidWave_.onSet = [this](int step, int v) {
        editPatch([step, v](ymengine::TrackPatch& p) {
            if (!ymengine::sineForm(p.sidForm)) {
                p.sidWave[size_t(step)] = uint8_t(v);
                return;
            }
            const bool bounce = p.sidForm == WaveForm::Bounce;
            p.sidWavePhase =
                uint8_t(ymengine::sineSidWavePhase(p.sidWave, p.sidWavePhase, p.sidWavePeriod, step, v, bounce));
            p.sidWave = ymengine::sineSidWaveAt(p.sidWave, p.sidWavePhase, p.sidWavePeriod, bounce);
        });
    };
    sidWave_.onWheel = [this](int step, int v) {
        armCur();  // one undo step per wheel notch, like a key
        // Sine lit, or Bounce: the next volume the sine can give the bar that way, so that each notch moves it.
        const ymengine::TrackPatch& tp = extras_.tracks[size_t(cur_)].patch;
        if (ymengine::sineForm(tp.sidForm)) {
            const bool bounce = tp.sidForm == WaveForm::Bounce;
            const int now = tp.sidWave[size_t(step)], dir = v > now ? 1 : -1;
            for (int t = v; t >= 0 && t <= 15; t += dir) {
                const int ph =
                    ymengine::sineSidWavePhase(tp.sidWave, tp.sidWavePhase, tp.sidWavePeriod, step, t, bounce);
                if ((ymengine::sineSidWaveAt(tp.sidWave, ph, tp.sidWavePeriod, bounce)[size_t(step)] - now) * dir > 0) {
                    v = t;
                    break;
                }
            }
        }
        sidWave_.onSet(step, v);
    };
    sidWave_.onMenu = [this](int step, const Rect& cell) {
        // The slider of the Modulation window's cells (seventh DAW test, item 84): 0 to 15, typed or dragged,
        // back to 0 on a right click on it (what a right click on the step did before); each move applies.
        desk_.closePopups();
        SliderPopupSpec spec;
        spec.value = extras_.tracks[size_t(cur_)].patch.sidWave[size_t(step)];
        spec.reset = 0;
        spec.set = [this, step](int v) { sidWave_.onSet(step, v); };
        desk_.openPopup(makeSliderPopup(std::move(spec), cell, desk_.width(), desk_.height()));
    };
    // Steps: the steps of a period of the Square, the bars played of Sine, Bounce and Custom; Base has none
    // (greyed).
    bindSlider(waveSteps_, [this](double v) {
        const int n = std::clamp(int(v), 2, ymengine::kSidWaveSteps);
        editPatch([n](ymengine::TrackPatch& p) {
            if (p.sidForm != WaveForm::Base) p.sidWaveLen = uint8_t(n);
        });
    });
    waveForm_.onClick = [this] { waveFormMenu(waveForm_); };
    // Period: the sine redrawn between its volumes, only with Sine or Bounce (the slider shown then).
    bindSlider(wavePeriod_, [this](double v) {
        editPatch([v](ymengine::TrackPatch& p) {
            p.sidWavePeriod = uint8_t(v);
            if (ymengine::sineForm(p.sidForm))
                p.sidWave = ymengine::sineSidWaveAt(p.sidWave, p.sidWavePhase, p.sidWavePeriod,
                                                    p.sidForm == WaveForm::Bounce);
        });
    });
    // Width: the Square's, shown with it.
    bindSlider(waveWidth_, [this](double v) {
        editPatch([v](ymengine::TrackPatch& p) { p.sidWidth = uint8_t(v); });
    });
    bindSlider(buzzLen_, [this](double v) { editPatch([v](ymengine::TrackPatch& p) { p.buzzLen = uint8_t(v); }); });
    for (int k = 0; k < ymengine::kBuzzSteps; ++k) {
        buzzStep_[size_t(k)].onClick = [this, k] { buzzStepMenu(k, buzzStep_[size_t(k)]); };
        buzzStep_[size_t(k)].onContext = [this, k](gemui::Point) {
            editPatch([k](ymengine::TrackPatch& p) { p.buzzShapes[size_t(k)] = 0; });
        };
    }
    buzPresets_.onClick = [this] { buzPresetsMenu(buzPresets_); };

    // SYSTEM.
    modeSel_.onClick = [this] {
        std::vector<MenuItem> items;
        for (int i = 0; i < 3; ++i) {
            MenuItem m;
            m.label = kModeNames[size_t(i)];
            m.pick = [this, i] {
                if (i == iv(Global::VoiceMode)) return;
                set(Global::VoiceMode, float(i));
                panic();  // the prototype cuts everything on a mode change
                // Only a track the new mode lacks hands the focus over to the first one (the DMA tracks of the STE
                // stay, as on a change of Computer).
                if (cur_ >= voices()) focusTrack(0);
            };
            items.push_back(std::move(m));
        }
        // The machine is set in the Computer row and the Settings window, no longer here (24/10/2026).
        popup(modeSel_, std::move(items), iv(Global::VoiceMode));
    };
    compSel_.onClick = [this] {
        std::vector<MenuItem> items;
        for (int i = 0; i < 2; ++i) {
            MenuItem m;
            m.label = i ? "STE" : "ST";
            m.pick = [this, i] { setComputer(i); };
            items.push_back(std::move(m));
        }
        popup(compSel_, std::move(items), ste() ? 1 : 0);
    };
    clkSel_.onClick = [this] {
        std::vector<MenuItem> items;
        for (int i = 0; i < 2; ++i) {
            MenuItem m;
            m.label = kClkModes[size_t(i)];
            m.pick = [this, i] { set(Global::ArpClock, float(i)); };
            items.push_back(std::move(m));
        }
        popup(clkSel_, std::move(items), iv(Global::ArpClock));
    };
    bindSlider(master_, [this](double v) { set(Global::Master, float(v)); });
    masterv_.onCommit = [this](const std::string& s) {
        const double v = gemui::readDecimal(s);
        if (s.empty() || !std::isfinite(v)) return false;
        set(Global::Master, float(std::clamp(std::round(v), 0.0, 100.0)));
        return true;
    };

    // Floating windows: Mixer, Arpeggiator, Modulation, Effects, Step Sequencer and Patches.
    winBtns_[0].onClick = [this] { mix_.show(); };
    winBtns_[1].onClick = [this] { arp_.show(); };
    winBtns_[2].onClick = [this] { orn_.show(); };  // openOrn
    winBtns_[3].onClick = [this] { echo_.show(); };
    winBtns_[4].onClick = [this] { seq_.show(); };
    winBtns_[5].onClick = [this] { lib_.show(); };  // openLib
    kbdMap_.onClick = [this] { kbdWin_.show(); };  // openKbdWin

    // Keyboard and pads.
    kbd_.onPress = [this](int note) { press(note, 127, true); };
    kbd_.onRelease = [this](int note) { release(note); };
    // Menu under the key (or the pad) clicked, like closest('.wk, .bk, .pad') in the prototype.
    kbd_.onContext = [this](gemui::Point p) { trackMenu(kbd_.keyBoxAt(p)); };
    pads_.onPress = [this](int pad) { pressPad(pad); };
    pads_.onRelease = [this](int pad) { release(kPadNote + pad); };
    pads_.onContext = [this](gemui::Point p) { trackMenu(pads_.padBoxAt(p)); };
    latchBtn_.onClick = [this] {
        latch_ = !latch_;
        latchBtn_.setOn(latch_);
        if (!latch_) releaseAll();
    };
    layerBtn_.onClick = [this] {
        const bool layer = plugin::midiRouting(values_) == plugin::MidiRouting::Layer;
        set(Global::MidiRouting, float(layer ? plugin::MidiRouting::Omni : plugin::MidiRouting::Layer));
        manPanic();
    };
    // Piano octave (0 to 6): Oct + brings an octave of 7 to 9 chosen on the computer keyboard back to 6, like the
    // prototype.
    octDown_.onClick = [this] { setBaseOctave(std::max(0, baseOct_ - 1)); };
    octUp_.onClick = [this] { setBaseOctave(std::min(6, baseOct_ + 1)); };

    menu_.onIndicator = [this](int entry) {
        if (entry == chipIndicator_) showChipAlert();
    };
}

// ---------------------------------------------------------------------------------------------- menus

// The menus of the bar: Fuji (0), File (1), Edit (2), Options (3). Their entries are built again at each opening, from
// the state of that moment: an entry that cannot act is greyed out with its reason (MenuItem::why, shown as its
// tooltip), and a submenu of choices marks the current one (subCur).
void MainWindow::buildMenus() {
    menu_.onTitle = [this](int title) -> std::vector<MenuItem> {
        std::vector<MenuItem> items;
        menuExtra_ = -1;
        menuNew_ = -1;
        // Helpers: a plain entry; an entry that only says its feature is not available yet (notAvailable), and a
        // submenu of such entries. The callers replace the `pick` of the entries that work.
        auto item = [](std::string label, std::string key = {}) {
            MenuItem m;
            m.label = std::move(label);
            m.key = std::move(key);
            return m;
        };
        auto later = [this](std::string label, std::string what, std::string key = {}) {
            MenuItem m;
            m.label = std::move(label);
            m.key = std::move(key);
            m.pick = [this, what] { notAvailable(what); };
            return m;
        };
        auto subOf = [&](std::string label, std::vector<std::string> names, std::string what) {
            MenuItem m;
            m.label = std::move(label);
            for (std::string& n : names) m.sub.push_back(later(std::move(n), what));
            return m;
        };
        switch (title) {
        case 0: {  // Fuji
            MenuItem info = item("Information");
            info.pick = [this] { showInfo(); };
            items.push_back(std::move(info));
            MenuItem keys = item("Shortcuts", "^?");
            keys.pick = [this] { help_.show(); };  // openHelp
            items.push_back(std::move(keys));
            break;
        }
        case 1: {  // File
            // JSON files of the prototype: Preset, Pattern, Song, Drum kit submenus (order of FileKind).
            auto files = [&](std::string label, std::function<void(plugin::FileKind)> act) {
                MenuItem m;
                m.label = std::move(label);
                for (const char* n : {"Preset", "Pattern", "Song", "Drum kit"}) {
                    MenuItem k = item(n);
                    k.pick = [act, kind = plugin::FileKind(m.sub.size())] { act(kind); };
                    m.sub.push_back(std::move(k));
                }
                return m;
            };
            // New: the only STYM2149 entry above Load (rectOf "menuNew").
            menuNew_ = int(items.size());
            MenuItem fresh = item("New", "^N");
            fresh.pick = [this] { newProject(); };
            items.push_back(std::move(fresh));
            items.push_back(files("Load", [this](plugin::FileKind k) { loadFile(k); }));
            items.push_back(files("Save", [this](plugin::FileKind k) { saveFile(k, false); }));
            items.push_back(files("Save as", [this](plugin::FileKind k) { saveFile(k, true); }));
            items.push_back(MenuItem::separatorItem());
            MenuItem import = subOf("Import", {"YM dump", "MIDI file", "XM module"}, "Importing files");
            import.sub[0].pick = [this] { importYm(); };
            import.sub[1].pick = [this] { importMidi(); };
            import.sub[2].pick = [this] { importXm(); };
            items.push_back(std::move(import));
            items.push_back(MenuItem::separatorItem());
            MenuItem pattern = subOf("Export pattern", {"MIDI", "WAV", "Stems (ZIP)"}, "Exporting patterns");
            pattern.sub[0].pick = [this] { exportMidi(false); };
            pattern.sub[1].pick = [this] { exportAudio(false, false); };
            pattern.sub[2].pick = [this] { exportAudio(true, false); };
            items.push_back(std::move(pattern));
            // regSub of the prototype, YM6 added: register formats in Authentic 3ch only.
            MenuItem song = subOf("Export song", {"VBL dump", "YM3", "YM6", "SNDH"}, "Exporting songs");
            const plugin::RegisterFormat regs[4] = {plugin::RegisterFormat::Vbl, plugin::RegisterFormat::Ym3,
                                                    plugin::RegisterFormat::Ym6, plugin::RegisterFormat::Sndh};
            for (size_t k = 0; k < 4; ++k) {
                MenuItem& m = song.sub[k];
                m.pick = [this, f = regs[k]] { exportRegisters(f); };
                if (plugin::voiceMode(values_) != ymengine::VoiceMode::Authentic3) {
                    m.disabled = true;
                    m.why = "Register dumps carry three YM voices: switch to Authentic 3ch";
                }
            }
            song.sub.push_back(MenuItem::separatorItem());
            for (const char* n : {"MIDI", "WAV", "Stems (ZIP)"}) song.sub.push_back(later(n, "Exporting songs"));
            // Entries 5 to 7: after the four register formats (0 to 3) and the separator (4).
            song.sub[5].pick = [this] { exportMidi(true); };
            song.sub[6].pick = [this] { exportAudio(false, true); };
            song.sub[7].pick = [this] { exportAudio(true, true); };
            items.push_back(std::move(song));
            // Register recorder: under the exports, which it prepares (YM6 and SNDH export); the only
            // STYM2149 entry in this menu.
            MenuItem rec = item("Recorder");
            rec.pick = [this] { recWin_.show(); };
            items.push_back(std::move(rec));
            items.push_back(MenuItem::separatorItem());
            MenuItem demo = item("Demo pattern");  // ^D went to the DMA generator
            demo.pick = [this] { demoPattern(); };
            items.push_back(std::move(demo));
            items.push_back(MenuItem::separatorItem());
            MenuItem quit = item("Quit", "^Q");
            if (host_.standalone()) {
                quit.pick = [this] { confirmQuit(); };
            } else {
                quit.disabled = true;
                quit.why = "The host closes the plugin";
            }
            items.push_back(std::move(quit));
            break;
        }
        case 2: {  // Edit
            const bool drum = drumTrack();
            MenuItem seq = item("Step Sequencer", "^E");
            seq.pick = [this] { seq_.show(); };
            items.push_back(std::move(seq));
            MenuItem arp = item("Arpeggiator", "^B");
            arp.pick = [this] { arp_.show(); };
            if (drum) {
                arp.disabled = true;
                arp.why = "Not used by percussion tracks";
            }
            items.push_back(std::move(arp));
            MenuItem mix = item("Mixer", "^M");
            mix.pick = [this] { mix_.show(); };
            items.push_back(std::move(mix));
            MenuItem song = item("Song", "^G");
            song.pick = [this] { openSong(); };
            items.push_back(std::move(song));
            MenuItem mod = item("Modulation", "^R");
            mod.pick = [this] { orn_.show(); };
            if (drum) {
                mod.disabled = true;
                mod.why = "Not used by percussion tracks";
            }
            items.push_back(std::move(mod));
            // Echo / Delay renamed Effects, between Modulation and Patches (ninth DAW test, items 110 to 112).
            MenuItem fx = item("Effects", "^F");
            fx.pick = [this] { echo_.show(); };
            items.push_back(std::move(fx));
            MenuItem lib = item("Patches", "^P");
            lib.pick = [this] { lib_.show(); };
            items.push_back(std::move(lib));
            MenuItem kit = item("Drum kit", "^K");
            kit.pick = [this] { kit_.show(); };
            if (!chipdrumTrack()) {  // the kit is played by Chipdrum tracks only (second DAW test, item 18)
                kit.disabled = true;
                kit.why = "Used by Chipdrum tracks only";
            }
            items.push_back(std::move(kit));
            items.push_back(MenuItem::separatorItem());
            // editCutState: rectangular selection, otherwise the note under the cursor.
            const SeqWindow::CutState cs = seq_.cutState();
            for (int act : {1, 0}) {  // Cut, Copy (editClip: 0 copy, 1 cut)
                const char* n = act ? "Cut" : "Copy";
                MenuItem m = item(cs.cut ? cs.what + ": " + n : std::string(n), act ? "^X" : "^C");
                if (cs.cut) {
                    m.pick = [this, act] { seq_.editClip(act); };
                } else {
                    m.disabled = true;
                    m.why = "Nothing selected in the sequencer";
                }
                items.push_back(std::move(m));
            }
            MenuItem paste = item("Paste", "^V");
            if (cs.paste) {
                paste.pick = [this] { seq_.editClip(2); };
            } else {
                paste.disabled = true;
                paste.why = "Clipboard is empty";
            }
            items.push_back(std::move(paste));
            items.push_back(MenuItem::separatorItem());
            MenuItem undo = item("Undo", "^Z"), redo = item("Redo", "^Y");
            undo.pick = [this] { seq_.undo(); };
            redo.pick = [this] { seq_.redo(); };
            items.push_back(std::move(undo));
            items.push_back(std::move(redo));
            // STYM2149 windows that the prototype does not have, under its entries.
            menuExtra_ = int(items.size());
            items.push_back(MenuItem::separatorItem());
            MenuItem ins = item("Instruments", "^T");
            ins.pick = [this] { ins_.show(); };
            items.push_back(std::move(ins));
            // Sequences left the menu for the Modulation window (fifth DAW test, items 42 and 43).
            // Timer wave and Buzzer left the menu for the GENERATOR frame (fourth DAW test, item 40).
            // Samples: Digidrum editor and STE DMA track, under one entry so that the menu does not
            // grow.
            MenuItem samples = item("Samples");
            MenuItem digi = item("Digidrums");
            digi.pick = [this] { digi_.show(); };
            samples.sub.push_back(std::move(digi));
            MenuItem dma = item("DMA (STE)");
            dma.pick = [this] { dma_.show(); };
            if (!ste()) {  // fourth DAW test, item 36, like the STE output chain
                dma.disabled = true;
                dma.why = "STE only: set Computer to STE";
            }
            samples.sub.push_back(std::move(dma));
            items.push_back(std::move(samples));
            break;
        }
        case 3: {  // Options
            // No Zoom entry: the size follows the window, resized in the host or by the system (tenth DAW test,
            // item 127).
            // One entry for the prototype's Audio output and MIDI input: the standalone application sets both in the
            // same window (second DAW test, item 16).
            MenuItem devices = item("Audio/MIDI settings");
            if (host_.hasAudioSettings()) {
                devices.pick = [this] { host_.openAudioSettings(); };
            } else {
                devices.disabled = true;
                devices.why = "The host chooses the audio output and sends MIDI to the plugin";
            }
            items.push_back(std::move(devices));
            items.push_back(MenuItem::separatorItem());
            // MIDI learn: MIDI always comes from the host (or the inputs of the standalone application), the entries
            // are never greyed out (midiOff of the prototype: without Web MIDI access).
            MenuItem learn = item(learn_ ? "Stop MIDI learn" : "MIDI learn"), clear = item("Clear MIDI map");
            learn.pick = [this] { setMidiLearn(!learn_); };
            clear.pick = [this] { clearMidiMap(); };
            items.push_back(std::move(learn));
            items.push_back(std::move(clear));
            items.push_back(MenuItem::separatorItem());
            // Parts of the main window shown or hidden, a preference each, shown by default: the oscilloscope (twelfth
            // DAW test, item 149), the register monitor (Indus, 25/10/2026 21:53) and the Note line of
            // SYSTEM, its TP and Hz (26/10/2026 04:05).
            auto shownEntry = [this, &item](const char* label, bool shown, const char* key) {
                MenuItem m = item(label);
                m.subMarks = true;
                m.subCur = shown ? 0 : 1;
                for (int i = 0; i < 2; ++i) {
                    MenuItem e = item(i ? "Off" : "On");
                    e.pick = [this, key, i] {
                        host_.setPreference(key, i ? "0" : "1");
                        this->layout();
                    };
                    m.sub.push_back(std::move(e));
                }
                return m;
            };
            items.push_back(shownEntry("Oscilloscope", scopeShown(), "mainScope"));
            items.push_back(shownEntry("Register monitor", regsShown(), "mainRegs"));
            items.push_back(shownEntry("Note monitor", noteShown(), "mainNote"));
            // Sound engine, Keyboard layout, Note names, Renderer, Pixels and Tooltips
            // left the menu for the Settings window (Indus, 25/10/2026 19:33), then VBL frequency
            // (26/10/2026 04:03).
            menuExtra_ = int(items.size());
            items.push_back(MenuItem::separatorItem());
            MenuItem settings = item("Settings...");
            settings.pick = [this] { settings_.show(); };
            items.push_back(std::move(settings));
            break;
        }
        default: break;
        }
        return items;
    };
}

// Pop-up menu of a control, opened under it. `checked`: the entry marked, -1 for none, gemui::kNoMarks for a menu
// without marks. The anchor is remembered for rectOf("shapeMenu").
void MainWindow::popup(gemui::Widget& anchor, std::vector<MenuItem> items, int checked) {
    popupAnchor_ = &anchor;
    gemui::MenuList::open(desk_, std::move(items), checked, anchor.box(), false);
}

// The host can focus another track while the menu is open (Focus Track automated, MIDI CC 84, a state set again): the
// entries would then write into that track.
void MainWindow::trackPopup(gemui::Widget& anchor, std::vector<MenuItem> items, int checked) {
    gemui::pickOnlyWhile(items, stillFocused());
    popup(anchor, std::move(items), checked);
}

std::function<bool()> MainWindow::stillFocused() const {
    return [this, t = cur_] { return cur_ == t; };
}

void MainWindow::openMenu(int title) { menu_.openTitle(title); }

void MainWindow::setTipLanguage(std::string_view id) {
    tipLang_ = std::string(id);
    host_.setPreference("tipLanguage", tipLang_);
    applyTipLanguage();
}

// Choices of the Settings window's Language: English (built in, no file), then one entry per language code of the
// .po files, sorted by name; a file without a valid language code is skipped (parseTipFile).
void MainWindow::scanTipLanguages() {
    tipChoices_.clear();
    tipChoices_.push_back({"en", "English", {}});
    // The user's files first: their lines come before those of the installed file of the same language.
    std::vector<Host::TextFile> files = host_.languageFiles();
    std::stable_partition(files.begin(), files.end(), [](const Host::TextFile& f) { return !f.installed; });
    for (const Host::TextFile& f : files) {
        std::string_view stem = f.name;
        if (stem.size() > 3 && stem.substr(stem.size() - 3) == ".po") stem.remove_suffix(3);
        std::optional<TipFile> file = parseTipFile(f.text, stem);
        if (!file) continue;
        auto same = std::find_if(tipChoices_.begin(), tipChoices_.end(),
                                 [&file](const TipChoice& c) { return c.id == file->id; });
        if (same == tipChoices_.end()) {
            tipChoices_.push_back({file->id, file->name, {}});
            same = std::prev(tipChoices_.end());
        } else if (same->name == same->id) {
            same->name = file->name;  // named by the first file that gives a name
        }
        same->files.push_back(std::move(*file));
    }
    std::stable_sort(tipChoices_.begin() + 1, tipChoices_.end(),
                     [](const TipChoice& a, const TipChoice& b) { return a.name < b.name; });
}

void MainWindow::applyTipLanguage() {
    // The files of the chosen language, then, for a regional variant ("fr-ca"), those of its language; none (its files
    // gone, or English): English.
    std::vector<const TipFile*> layers;
    auto add = [this, &layers](std::string_view id) {
        for (const TipChoice& c : tipChoices_)
            if (c.id == id)
                for (const TipFile& f : c.files) layers.push_back(&f);
    };
    add(tipLang_);
    if (const size_t dash = tipLang_.find('-'); dash != std::string::npos)
        add(std::string_view(tipLang_).substr(0, dash));
    tipBook_ = TipBook(layers);
    nameDeskIcons();
}

void MainWindow::openLanguagesFolder() {
    std::vector<Host::TextFile> files;
    for (auto& [name, text] : tipFolderFiles(host_.version(), host_.installedLanguagesFolder()))
        files.push_back({std::move(name), std::move(text)});
    if (!host_.openLanguagesFolder(files)) showMessage("The Languages folder cannot be opened");
}

// ------------------------------------------------------------------------------------------- reading from the host

// Small readers of the copies values_ and extras_: pv gives a value in the units of the parameter table, iv the same
// as an integer (index of a choice); the Track forms read the focused track (cur_).
int MainWindow::iv(Global g) const noexcept { return plugin::intValue(values_, g); }
ymengine::SeqClock MainWindow::seqClock() const noexcept {
    return plugin::seqClockOf(values_, extras_.global.seqClock);
}
int MainWindow::iv(Track f) const noexcept { return plugin::intValue(values_, cur_, f); }
int MainWindow::iv(int track, Track f) const noexcept { return plugin::intValue(values_, track, f); }
bool MainWindow::mode3() const noexcept { return iv(Global::VoiceMode) != 0; }  // Authentic 3ch or Dual chip
std::vector<const gemui::Widget*> MainWindow::greyedSource() const {
    std::vector<const gemui::Widget*> out;
    for (const gemui::Widget* w : std::initializer_list<const gemui::Widget*>{
             &labSrc_, &srcSel_, &labNp_, &np_, &npv_, &labVol_, &vol_, &volv_, &labVm_, &vmSel_})
        if (w->opacity() < 1) out.push_back(w);
    return out;
}
std::vector<const gemui::Widget*> MainWindow::greyedInert() const {
    std::vector<const gemui::Widget*> out;
    for (const gemui::Widget* f : std::initializer_list<const gemui::Widget*>{&adsrFs_, &curveFs_, &vibFs_, &envFs_})
        if (f->opacity() < 1)
            for (const gemui::Widget* w : f->children())
                if (w->showing() && w->opacity() < 1) out.push_back(w);
    return out;
}
int MainWindow::srcIndex() const noexcept {
    // Inverse of the Source menu's mapping (kSrcNames): tone + 2 × noise - 1 with the fixed volume, + 3 with the
    // envelope; neither tone nor noise with the fixed volume gives -1 (Off).
    const int bits = (iv(Track::Tone) != 0 ? 1 : 0) | (iv(Track::Noise) != 0 ? 2 : 0);
    if (iv(Track::VolMode) != 0) return bits + 3;
    return bits - 1;
}
int MainWindow::chips() const noexcept { return iv(Global::VoiceMode) == 2 ? 2 : 1; }
int MainWindow::voices() const noexcept { return plugin::trackCount(values_); }
bool MainWindow::ste() const noexcept { return plugin::steMachine(values_); }
bool MainWindow::scopeShown() const { return host_.preference("mainScope", "1") != "0"; }
bool MainWindow::regsShown() const { return host_.preference("mainRegs", "1") != "0"; }
bool MainWindow::noteShown() const { return host_.preference("mainNote", "1") != "0"; }
bool MainWindow::dmaTrack(int track) const noexcept {
    return plugin::dmaTrack(plugin::voiceMode(values_), ste(), track);
}
int MainWindow::dmaSlot(int track) const noexcept {
    // Slot played by a DMA track: its own, otherwise its rank.
    const int own = extras_.tracks[size_t(std::clamp(track, 0, plugin::kTracks - 1))].patch.dmaSlot;
    if (own >= 0) return own & 7;
    return (mode3() ? track - 4 * chips() : track) & 7;
}
int MainWindow::generator() const noexcept { return int(plugin::effectiveGenerator(values_, cur_)); }
bool MainWindow::drumTrack() const noexcept {
    const int g = generator();
    return g == int(Generator::Digidrum) || g == int(Generator::Chipdrum);
}
std::string MainWindow::keyLayout() const {
    const std::string l = host_.preference("kbdLayout", "auto");
    if (l == "qwerty" || l == "azerty" || l == "qwertz") return l;
    const std::string sys = host_.keyboardLayout();
    return sys == "azerty" || sys == "qwertz" ? sys : "qwerty";
}
bool MainWindow::chipdrumTrack() const noexcept { return generator() == int(Generator::Chipdrum); }
bool MainWindow::adsrChip() const noexcept {
    return (mode3() && generator() != int(Generator::Dma)) || iv(Track::AdsrMode) == 1;  // DMA: ADSR of the track
}
double MainWindow::msPerTick() const noexcept {
    // Length of one VBL tick in ms at the chosen rate (Settings > VBL frequency).
    const int i = std::clamp(iv(Global::VblRate), 0, int(plugin::kVblRates.size()) - 1);
    return 1000.0 / plugin::kVblRates[size_t(i)];
}

// Writes one parameter (plain value, index of the parameter table). Outside a slider drag, the value is a whole
// gesture of its own; during a drag, the gesture of each parameter stays open until bindSlider's onGestureEnd closes
// them all. The copy values_ takes back the value the host now holds, then the screen follows at once.
void MainWindow::set(int index, float plain) {
    if (dragging_) {
        if (std::find(gestures_.begin(), gestures_.end(), index) == gestures_.end()) {
            gestures_.push_back(index);
            host_.beginGesture(index);
        }
        host_.setParam(index, plain);
    } else {
        host_.beginGesture(index);
        host_.setParam(index, plain);
        host_.endGesture(index);
    }
    values_[size_t(index)] = host_.param(index);
    // Setting of the focused track: one undo step (pushCur of the prototype's handlers).
    if (index >= plugin::kGlobalParams) {
        const int rel = index - plugin::kGlobalParams;
        if (rel / plugin::kTrackParams == cur_ && chanField(Track(rel % plugin::kTrackParams))) pushCur();
    }
    refresh();
}

// Called by the host at its frame rate (StymEditor: 30 Hz) on the message thread, with a time in ms. Nothing notifies
// the window: each tick compares every parameter with its copy, whatever changed it (host automation, a MIDI CC...),
// and the non-automatable state by its version number. Then it drains the host's queues of notes and learned MIDI
// messages (at most 256 each per tick), refreshes the register monitor every 120 ms (the prototype's interval),
// collects a finished export and runs the desktop's timers.
void MainWindow::tick(double nowMs) {
    bool changed = first_;
    for (int i = 0; i < plugin::kParamCount; ++i) {
        const float v = host_.param(i);
        if (v != values_[size_t(i)]) {
            values_[size_t(i)] = v;
            changed = true;
        }
    }
    if (host_.extrasVersion() != extrasVersion_) {
        extrasVersion_ = host_.extrasVersion();
        extras_ = host_.extras();
        changed = true;
    }
    const std::string midi = "MIDI: " + host_.midiStatus();
    if (midi != midiText_) {
        midiText_ = midi;
        menu_.setIndicator(midiIndicator_, midi, false);
    }
    if (changed) {
        first_ = false;
        refresh();
    }
    if (const plugin::EngineStatus* t = host_.engineStatus()) refreshEngineStatus(*t);
    std::array<uint8_t, 3> note{};
    for (int i = 0; i < 256 && host_.hostNote(note); ++i) hostNote(note);
    for (int i = 0; i < 256 && host_.learnMidi(note); ++i) learnMessage(note);
    if (nowMs - lastRegs_ >= 120) {
        lastRegs_ = nowMs;
        refreshRegisters();
    }
    kbdWin_.followLayout();
    recWin_.poll();
    ymWin_.poll();
    if (job_.poll()) desk_.setBusy(false);  // export finished: its alert is open
    desk_.tick(nowMs);
}

// ------------------------------------------------------------------------------------------- update

// Name of a track in its tab: in 3-channel mode, the chip's voice or DMA1 to DMA8 (beyond: the number of an unused
// tab); in 16 ch, the name the user gave it, otherwise its number.
std::string MainWindow::trackName(int i) const {
    if (mode3()) {
        if (dmaTrack(i)) return "DMA" + std::to_string(i - 4 * chips() + 1);
        if (chips() > 1) return i < 8 ? kVoiceNames2[size_t(i)] : std::to_string(i + 1);
        return i < 4 ? kVoiceNames1[size_t(i)] : std::to_string(i + 1);
    }
    const std::string& n = extras_.trackNames[size_t(i)];
    return n.empty() ? std::to_string(i + 1) : n;
}

std::string MainWindow::scopeName(int i) const {
    // scopeName: name of the channel in 3-channel mode, otherwise name of the track or "TRACK n".
    if (mode3()) return i < voices() ? trackName(i) : std::to_string(i + 1);
    const std::string& n = extras_.trackNames[size_t(i)];
    return n.empty() ? "TRACK " + std::to_string(i + 1) : n;
}

std::string MainWindow::tabTip(int i) const {
    if (mode3() && i >= voices())
        return std::string("Unused in 3 ch mode: the YM has three voices plus the Digidrum track") +
               (ste() ? "" : ". Computer STE adds eight DMA tracks");
    const std::string& n = extras_.trackNames[size_t(i)];
    std::string head;
    if (dmaTrack(i)) head = "DMA track " + std::to_string(i - 4 * chips() + 1) + " (STE)";
    else if (mode3()) head = chips() > 1 ? kVoiceLong2[size_t(i)] : kVoiceLong1[size_t(i)];
    else head = "Track " + std::to_string(i + 1);
    if (!n.empty()) head += " - " + n;
    return head + "\nClick: edit this track. Click again: mute or unmute it.\nRight click: copy, paste or rename.";
}

// Brings every control of the main window in line with values_ and extras_, then the layout and the floating windows.
// Called after every change (several times in a row when one action sets several parameters). Two rules run through
// it: a slider held by the mouse (keep) and a value field being typed into (editing) are left alone; a control the
// current sound does not use is greyed out, and its tooltip says why. The focused track is a host parameter
// (Global::FocusTrack, 1 to 16).
void MainWindow::refresh() {
    cur_ = std::clamp(iv(Global::FocusTrack), 1, plugin::kTracks) - 1;
    const bool m3 = mode3();
    const int gen = generator();
    const bool drum = drumTrack();
    const auto& patch = extras_.tracks[size_t(cur_)].patch;
    auto keep = [this](const gemui::Slider& s) { return dragging_ == &s; };

    leds_.setAvailable(voices());  // LED without a channel at 25 % (setMode3 of the prototype)

    // Windows not applicable to a drum track (refreshSeqGrid of the prototype).
    winBtns_[1].setEnabled(!drum);
    winBtns_[1].setTip(drum ? "Percussion tracks do not use the arpeggiator"
                            : "Arpeggiator: play a chord one note at a time (Ctrl+B)");
    winBtns_[2].setEnabled(!drum);
    winBtns_[2].setTip(drum ? "Percussion tracks do not use modulation tables"
                            : "Modulation: per-tick table for pitch, noise and volume (Ctrl+R)");

    // Tabs (syncTabs, retabs).
    for (int j = 0; j < plugin::kTracks; ++j) {
        Button& b = tabs_[size_t(j)];
        b.setText(trackName(j));
        b.setTip(tabTip(j));
        b.setOn(iv(j, Track::On) != 0);
        b.setSelected(j == cur_);
        b.setEnabled(!m3 || j < voices());
    }

    // SOURCE.
    srcSel_.setText(srcIndex() < 0 ? "Off" : kSrcNames[size_t(srcIndex())]);
    const bool mono = iv(Track::Voicing) == 1;
    polySel_.setText(mono ? "Monophonic" : "Polyphonic");
    polySel_.setEnabled(!m3);
    labPoly_.setDim(m3);
    const std::string polyTip =
        drum ? (mono ? "Monophonic: a new hit chokes the one still ringing, like a closed hi-hat"
                     : "Polyphonic: hits ring on together, up to " + std::to_string(kPolyMax) + " at once")
        : m3 ? "Authentic 3 ch: one voice per channel, always monophonic"
        : mono ? "Monophonic: one note at a time. Holding a new key glides to it instead of retriggering."
               : "Polyphonic: chords in the grid and under your fingers, up to " + std::to_string(kPolyMax) +
                     " voices";
    polySel_.setTip(polyTip);
    if (!keep(np_)) np_.setValue(iv(Track::NoisePeriod));
    if (!npv_.editing()) npv_.setText(std::to_string(iv(Track::NoisePeriod)));
    if (!keep(vol_)) vol_.setValue(iv(Track::Volume));
    if (!volv_.editing()) volv_.setText(std::to_string(iv(Track::Volume)));
    vmSel_.setText(kVmNames[size_t(std::clamp(iv(Track::VolMode), 0, 1))]);
    // What the sound does not use is greyed out, with the reason in its tip (sixth DAW test, items 55, 63 and 64):
    // without noise, the noise period is not heard; a Sync Buzzer plays the hardware envelope
    // alone (no square, no noise, no fixed volume); a SID Voice writes its own volumes and ignores the envelope.
    const bool sbSrc = gen == int(Generator::SyncBuzzer) && !drum;
    const bool sidSrc = gen == int(Generator::Sid) && !drum;
    // A DMA track plays its sample, a percussion track its own sounds (Drum Kit frames or 4-bit samples): the square,
    // the noise, the fixed volume and the envelope of the track are not used (thirteenth DAW test, items 169 and 170).
    // Voicing and Level still act.
    const bool dma = gen == int(Generator::Dma);
    const bool ownSound = dma || drum;
    static const std::string kDmaWhy = "A DMA track plays its sample: no square, no noise, no 4-bit volume, no "
                                       "envelope";
    static const std::string kDrumWhy = "Percussion tracks play their own sounds (Drum Kit or sample): no source, no "
                                        "fixed volume, no envelope";
    const std::string& ownWhy = dma ? kDmaWhy : kDrumWhy;
    const bool srcOff = sbSrc || ownSound;
    const bool noiseUsed = !srcOff && iv(Track::Noise) != 0;
    static const std::string kSbWhy = "Sync Buzzer plays the hardware envelope alone: no square, no noise, no fixed "
                                      "volume";
    const std::string& offWhy = ownSound ? ownWhy : kSbWhy;
    srcSel_.setEnabled(!srcOff);
    labSrc_.setDim(srcOff);
    const std::string srcTip =
        "Sound source of this channel: square wave, noise, both or neither, with the fixed volume or the envelope";
    srcSel_.setTip(srcOff ? offWhy : srcTip);
    labSrc_.setTip(srcOff ? offWhy : srcTip);
    np_.setEnabled(noiseUsed);
    labNp_.setDim(!noiseUsed);
    npv_.setDim(!noiseUsed);
    np_.setTip(srcOff      ? offWhy
               : noiseUsed ? "Noise period: low is bright, high is rumbling"
                           : "Noise period: no noise in this channel's source");
    vol_.setEnabled(!srcOff);
    labVol_.setDim(srcOff);
    volv_.setDim(srcOff);
    vol_.setTip(srcOff ? offWhy : "Fixed 4-bit volume, as on the chip");
    const bool vmUsed = !srcOff && !sidSrc;
    vmSel_.setEnabled(vmUsed);
    labVm_.setDim(!vmUsed);
    vmSel_.setTip(srcOff   ? offWhy
                  : sidSrc ? "A SID Voice writes its own volumes: the envelope is not used"
                           : "Volume source: fixed level or hardware envelope");
    if (!keep(level_)) level_.setValue(iv(Track::Level));
    if (!levelv_.editing()) levelv_.setText(std::to_string(iv(Track::Level)));

    // ENVELOPE (showEnv): period of the chip and cycle frequency clk / (256 × EP).
    envFs_.setHint(m3 ? "(shared)" : "(per channel)");
    const int ep = std::clamp(iv(Track::EnvPeriod), 1, 65535);
    const double clk = plugin::chipClock(values_).hz();
    // Slider position: inverse of EP = 65535^(1 - ef / 4000) (wireControls), so ef = 4000 × (1 - ln EP / ln 65535).
    if (!keep(ef_)) ef_.setValue(std::round(4000 * (1 - std::log(double(ep)) / std::log(65535.0))));
    if (!efv_.editing()) {
        efv_.setText(fmt("%.1f", clk / (256.0 * ep)));
        placeEnvHz();  // right-aligned: its width follows its text
    }
    char epText[24];
    std::snprintf(epText, sizeof epText, "%d ($%04X)", ep, unsigned(ep));
    if (!epv_.editing()) epv_.setText(epText);
    // Tuned envelope, a Sync Buzzer setting: the period follows the note, Env freq and EP are no
    // longer used; their labels and unit are greyed out with them (fourth DAW test, item 34).
    const ymengine::TrackPatch& tp = extras_.tracks[size_t(cur_)].patch;
    const bool tunedEnv = tp.envTuned && gen == int(Generator::SyncBuzzer) && !drum;
    for (Label* l : {&labEf_, &efUnit_, &labEp_}) l->setDim(tunedEnv);
    efv_.setDim(tunedEnv);
    epv_.setDim(tunedEnv);
    if (tunedEnv) epv_.setText("tuned x" + std::to_string(tp.envRatioN) + "/" + std::to_string(tp.envRatioD));
    const int shape = std::clamp(iv(Track::EnvShape), 0, 15);
    shapeSel_.setText(std::string(1, "0123456789ABCDEF"[shape]));
    shapeSel_.setIcon(&icons::kShapes[size_t(shape)]);
    // R13 ignored otherwise; a SID Voice ignores the envelope.
    const bool shapeUsed = !ownSound && ((iv(Track::VolMode) == 1 && gen != int(Generator::Sid)) ||
                                         gen == int(Generator::SyncBuzzer));
    shapeSel_.setEnabled(shapeUsed);
    shapeLab_.setDim(!shapeUsed);
    // ENVELOPE of a DMA or percussion track: dimmed with its title and content, as ADSR on a drum track.
    ef_.setEnabled(!tunedEnv && !ownSound);
    envFs_.setOpacity(ownSound ? gemui::kOpacityInert : 1.0);
    envFs_.setInert(ownSound);

    // VIBRATO: Off greys Depth and Rate out, like Tuning greys Env freq out. Percussion plays no vibrato (no
    // VBL frames): the frame is dimmed like ADSR.
    const bool vibOn = iv(Track::VibOn) != 0;
    vibOn_.setOn(vibOn);
    vibOn_.setText(vibOn ? "On" : "Off");
    vibOn_.setEnabled(!drum);
    vibFs_.setOpacity(drum ? gemui::kOpacityInert : 1.0);
    vibFs_.setInert(drum);
    for (gemui::Slider* s : {&vibDepth_, &vibRate_}) s->setEnabled(vibOn && !drum);
    for (Label* l : {&labDepth_, &depthUnit_, &labRate_, &rateUnit_}) l->setDim(!vibOn);
    vibDepthv_.setDim(!vibOn);
    vibRatev_.setDim(!vibOn);
    constexpr const char* kVibDrum = "Percussion tracks do not use the vibrato";
    vibOn_.setTip(drum ? kVibDrum
                       : "Vibrato of this track On or Off (the VIB row of the Step Sequencer switches it from a step "
                         "on)");
    vibDepth_.setTip(drum ? kVibDrum : "Vibrato depth in cents");
    vibRate_.setTip(drum ? kVibDrum : "Vibrato speed in Hz");
    if (!keep(vibDepth_)) vibDepth_.setValue(iv(Track::VibDepth));
    if (!vibDepthv_.editing()) vibDepthv_.setText(std::to_string(iv(Track::VibDepth)));
    if (!keep(vibRate_)) vibRate_.setValue(pv(Track::VibRate));
    if (!vibRatev_.editing()) vibRatev_.setText(fmt("%.1f", pv(Track::VibRate)));

    // ADSR (refreshAdsrScale, refreshAdsrEnable).
    const bool chip = adsrChip();
    const double tick = msPerTick();
    static constexpr std::array<Track, 4> kAdsrParams = {Track::Attack, Track::Decay, Track::Sustain, Track::Release};
    const int refCols = chip ? 3 : 4;  // positions of the time maximum: 100 or 2000
    for (size_t k = 0; k < 4; ++k) {
        // Ranges shown: Smooth, A, D and R 0-2000 ms, S 0-100 %; Chip, A, D and R 0-100 ticks, the 2000 ms of Smooth
        // at 50 Hz, S 0-15 (Indus, 25/10/2026 18:30; the prototype stopped A and D at 1000 ms and 64 ticks, R at
        // 64 ticks). The stored ms and % are converted to the shown unit, the inverse of `store` in wireControls.
        const bool sus = k == 2;
        const int max = chip ? (sus ? 15 : 100) : (sus ? 100 : 2000);
        const double stored = pv(kAdsrParams[k]);
        const int shown = !chip ? roundInt(stored) : sus ? roundInt(stored / 100 * 15) : roundInt(stored / tick);
        adsr_[k].setRange(0, max, 1);
        if (!keep(adsr_[k])) adsr_[k].setValue(shown);
        const int cols = int(std::to_string(max).size());
        adsrv_[k].setMinChars(cols);
        adsrv_[k].setMask(std::string(size_t(cols), '9'));
        const std::string u = chip ? (sus ? "" : "tk") : (sus ? "%" : "ms");
        adsrUnit_[k].setText(u);
        // The missing positions (paddingLeft of the prototype), and for Sustain the shorter unit: the four sliders
        // stay as wide.
        adsrv_[k].setPadChars((refCols - cols) + (sus ? 2 - int(u.size()) : 0));
        if (!adsrv_[k].editing()) adsrv_[k].setText(std::to_string(shown));
    }
    adsrMode_.setText(kAdsrModes[chip ? 1 : 0]);
    // ADSR not applicable (the frame takes no click): Sync Buzzer in Chip mode and the percussion in every mode, each
    // hit playing to its end with its own shape (the engine ignores the ADSR of a drum). The ADSR and ADSR
    // ENVELOPE frames are dimmed with their titles and content (third DAW test, item 21); the prototype dimmed
    // only the ADSR frame of a Sync Buzzer.
    const bool buzzer = gen == int(Generator::SyncBuzzer) && chip;
    const bool inert = buzzer || drum;
    const bool lockMode = m3 && gen != int(Generator::Dma);  // 3 channels: ADSR of the chip, except for a DMA track
    adsrMode_.setEnabled(!lockMode && !drum);
    adsrModeLab_.setDim(lockMode);
    adsrHint_.setOpacity(lockMode ? gemui::kOpacityLabelOff : 1.0);
    // Sync Buzzer: "envelope only", the prototype's "buzzer: envelope only" ran past the frame into ADSR ENVELOPE.
    adsrHint_.setText(drum ? "drums: off" : buzzer ? "envelope only" : chip ? "4-bit reg" : "audio gain");
    adsrFs_.setOpacity(inert ? gemui::kOpacityInert : 1.0);
    curveFs_.setOpacity(inert ? gemui::kOpacityInert : 1.0);
    const std::string inertWhy = drum ? "Percussion tracks do not use the ADSR: each hit plays to its end with its own "
                                        "shape (Drum Kit or sample)"
                                      : "Sync Buzzer takes its level from the hardware envelope: the 4-bit volume "
                                        "register is ignored, so a chip ADSR has nothing to write to";
    adsrMode_.setTip(inert ? inertWhy
                           : "Envelope shape: Smooth for a continuous curve, Chip for 4-bit steps at VBL rate");
    static constexpr std::array<const char*, 4> kAdsrTips = {
        "Attack: time to reach full level after the note starts",
        "Decay: time to fall from full level down to the sustain level", "Sustain: level held while the note lasts",
        "Release: time to fade out once the note ends"};
    for (size_t k = 0; k < 4; ++k) {
        adsr_[k].setEnabled(!inert);
        adsr_[k].setTip(inert ? inertWhy : kAdsrTips[k]);
        adsrLab_[k].setTip(adsr_[k].tip());  // the initial alone: its tip names it
    }
    adsrFs_.setInert(inert);
    AdsrCurve::Shape cs;
    cs.attackMs = pv(Track::Attack);
    cs.decayMs = pv(Track::Decay);
    cs.sustain = pv(Track::Sustain) / 100.0;
    cs.releaseMs = pv(Track::Release);
    cs.chip = chip;
    cs.off = inert;
    curve_.setShape(cs);
    const std::string curveTip =
        std::string("Time runs left to right. A, D and R are durations, S is the level held while the key is down") +
        (chip ? ". Steps: one register write per VBL tick" : "");
    curveFs_.setTip(drum     ? "Percussion tracks do not use the ADSR, this shape is not used"
                    : buzzer ? "Sync Buzzer: the hardware envelope takes over, this shape is not used"
                             : curveTip);

    // GENERATOR.
    fxSel_.setText(kFxNames[size_t(gen)]);
    if (!keep(ratio_)) ratio_.setValue(std::round(pv(Track::SidRatio) * 100));
    if (!ratiov_.editing()) ratiov_.setText(fmt("%.2f", pv(Track::SidRatio)));
    if (!keep(vmin_)) vmin_.setValue(iv(Track::SidVmin));
    if (!vminv_.editing()) vminv_.setText(std::to_string(iv(Track::SidVmin)));
    // Sync Buzzer: settings of the Buzzer window; N, D and Fine apply to a tuned envelope.
    buzTuned_.setOn(tp.envTuned);
    buzTuned_.setText(tp.envTuned ? "On" : "Off");  // after the Tuning label (fourth DAW test, item 35)
    if (!keep(buzN_)) buzN_.setValue(tp.envRatioN);
    if (!keep(buzD_)) buzD_.setValue(tp.envRatioD);
    if (!keep(buzFine_)) buzFine_.setValue(tp.envFineCents);
    if (!buzNv_.editing()) buzNv_.setText(std::to_string(tp.envRatioN));
    if (!buzDv_.editing()) buzDv_.setText(std::to_string(tp.envRatioD));
    const int fineCents = int(std::lround(tp.envFineCents));
    if (!buzFinev_.editing()) buzFinev_.setText((fineCents > 0 ? "+" : "") + std::to_string(fineCents) + "c");
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&buzN_, &buzD_, &buzFine_})
        w->setEnabled(tp.envTuned);
    for (Label* l : {&labBuzN_, &labBuzD_, &labBuzFine_}) l->setDim(!tp.envTuned);
    for (gemui::ValueField* f : {&buzNv_, &buzDv_, &buzFinev_}) f->setDim(!tp.envTuned);
    buzPad_.setOn(tp.ymPad);
    // SID wave and Sync Buzzer steps: beyond the length of the wave or of the steps, faded.
    SidWaveGrid::State wave;
    wave.wave = tp.sidWave;
    // The form of the menu. Base and Square (24/10/2026): the bars show the square played, the note's
    // volume (15) over the first half or the first Width of its Steps, SID min vol over the others, inert; the
    // bars drawn before are kept for Custom. Base has two steps, the halves of its period, its bars and Steps hidden.
    const WaveForm form = waveFormOf(tp);
    const bool square = form == WaveForm::Base || form == WaveForm::Square;
    wave.length = form == WaveForm::Base ? 2 : std::min<int>(tp.sidWaveLen, ymengine::kSidWaveSteps);
    wave.square = square;
    const int width = std::clamp<int>(tp.sidWidth, 1, std::max(1, wave.length - 1));
    if (square) {
        const uint8_t low = uint8_t(std::clamp(iv(Track::SidVmin), 0, 15));
        const int high = form == WaveForm::Base ? 1 : width;
        for (int k = 0; k < ymengine::kSidWaveSteps; ++k) wave.wave[size_t(k)] = k < high ? 15 : low;
    }
    sidWave_.setState(wave);
    if (!keep(waveSteps_)) waveSteps_.setValue(wave.length);
    if (!waveStepsv_.editing()) waveStepsv_.setText(std::to_string(wave.length));
    waveForm_.setText(kSidFormNames[size_t(form)]);
    // Period and Width: each shown only with its form (layoutHome), its value kept for the next time.
    // Width counts steps of the Square, up to Steps - 1: alone at 2 Steps, greyed.
    if (!keep(wavePeriod_)) wavePeriod_.setValue(tp.sidWavePeriod);
    if (!wavePeriodv_.editing()) wavePeriodv_.setText(std::to_string(tp.sidWavePeriod));
    waveWidth_.setRange(1, std::max(2, wave.length - 1), 1);
    if (!keep(waveWidth_)) waveWidth_.setValue(width);
    if (!waveWidthv_.editing()) waveWidthv_.setText(std::to_string(width));
    waveWidth_.setEnabled(wave.length > 2);
    labWaveWidth_.setDim(wave.length <= 2);
    waveWidthv_.setDim(wave.length <= 2);
    const int buzzN = std::min<int>(tp.buzzLen, ymengine::kBuzzSteps);
    if (!keep(buzzLen_)) buzzLen_.setValue(buzzN);
    if (!buzzLenv_.editing()) buzzLenv_.setText(std::to_string(buzzN));  // 0: the track's shape (tip)
    buzzUnit_.setText(buzzN == 1 ? " step" : " steps");
    for (int k = 0; k < ymengine::kBuzzSteps; ++k) {
        // The shape's icon alone (four buttons across the frame), "Trk" for the track's shape.
        Button& b = buzzStep_[size_t(k)];
        const int s = tp.buzzShapes[size_t(k)] & 15;
        b.setText(s ? "" : "Trk");
        b.setIcon(s ? &icons::kShapes[size_t(s)] : nullptr);
        b.setEnabled(k < buzzN);
    }
    // Kit name (kitLabel): identical factory bank, otherwise Custom. The five settings of each of the eight
    // instruments are compared exactly.
    std::string kit = "Custom";
    for (const auto& bank : ymengine::kKitBanks) {
        bool same = true;
        for (size_t i = 0; i < bank.kit.inst.size() && same; ++i) {
            const auto& a = bank.kit.inst[i];
            const auto& b = extras_.kit.inst[i];
            same = a.a == b.a && a.b == b.b && a.dec == b.dec && a.noise == b.noise && a.vol == b.vol;
        }
        if (same) {
            kit = bank.name;
            break;
        }
    }
    kitName_.setText(kit);
    const int slot = std::clamp(int(patch.digidrumSlot), 0, ymengine::kDigidrumSlots - 1);
    for (int k = 0; k < ymengine::kDigidrumSlots; ++k) {
        slots_[size_t(k)].setOn(k == slot);
        const std::string& nm = extras_.digiSlots[size_t(k)].name;
        slots_[size_t(k)].setTip("Digidrum slot " + std::to_string(k + 1) + quotedName(nm) +
                                 ": pick it. In the sequencer, the row you write on chooses the slot. Load and edit "
                                 "the samples with Digidrum samples.");
    }
    const plugin::DigidrumSlotState& ds0 = extras_.digiSlots[size_t(slot)];
    drumWave_.setSample(slot, extras_.digidrums[size_t(slot)], ds0.play.rateHz, ds0.name);
    if (gen == int(Generator::Dma)) {
        const int ds = dmaSlot(cur_);
        for (int k = 0; k < ymengine::kDmaSlots; ++k) {
            const size_t n = extras_.dmaSamples[size_t(k)].size();
            const std::string& nm = extras_.dmaNames[size_t(k)];
            dmaSlots_[size_t(k)].setOn(k == ds);
            dmaSlots_[size_t(k)].setTip("DMA sample " + std::to_string(k + 1) + quotedName(nm) + ": " +
                                        (n ? std::to_string(n) + " bytes" : std::string("empty")) +
                                        ". This track plays it, tuned from its root note.");
        }
        const ymengine::DmaSlot& sl = extras_.dma.slots[size_t(ds)];
        const auto& smp = extras_.dmaSamples[size_t(ds)];
        const auto frames = uint32_t(smp.size() / (sl.stereo ? 2 : 1));
        const ymengine::SampleSpan part = ymengine::sampleSpan(sl.start, sl.end, frames);
        dmaWave_.setSample(ds, smp, ymengine::kDmaRateHz[sl.rate], sl.stereo, sl.loop, part.start, part.end,
                           extras_.dmaNames[size_t(ds)]);
        const bool latin = host_.preference("noteNames", "0") == "1";
        const auto names = latin ? ymseq::NoteNames::Latin : ymseq::NoteNames::English;
        dmaHint_.setText(extras_.dmaSamples[size_t(ds)].empty()
                             ? "Sample " + std::to_string(ds + 1) + " is empty: load it in the DMA window."
                             : "Sample " + std::to_string(ds + 1) + quotedName(extras_.dmaNames[size_t(ds)]) + ": " +
                                   DmaWindow::rateName(sl.rate) + "Hz, " +
                                   (sl.stereo ? "stereo" : "mono") + ", root " + ymseq::absNoteName(sl.root, names) +
                                   (sl.loop ? ", loop." : ", one shot."));
    }
    const int digiChip = chips() > 1 ? (cur_ >> 2) & 1 : 0;
    const int digiCh = std::clamp(int(extras_.global.digidrumChannel[size_t(digiChip)]), 0, 2);
    digiChSel_.setText(std::string(1, char('A' + digiCh)) + (chips() > 1 ? std::to_string(digiChip + 1) : ""));

    // Pads (buildPads): the eight Chipdrum instruments, or the eight Digidrum slots (an empty one dimmed).
    if (drum) {
        std::array<std::string, 8> labels, tips;
        std::array<bool, 8> empty{};
        for (int i = 0; i < 8; ++i) {
            const bool cd = gen == int(Generator::Chipdrum);
            const std::string n = std::to_string(i + 1);
            labels[size_t(i)] = cd ? kDrumFull[size_t(i)] : "Slot " + n;
            empty[size_t(i)] = !cd && extras_.digidrums[size_t(i)].empty();
            tips[size_t(i)] = cd ? std::string(kDrumFull[size_t(i)]) +
                                       ": play it. Shape it in the Drum Kit window (Ctrl+K)."
                                 : "Digidrum slot " + n + quotedName(extras_.digiSlots[size_t(i)].name) +
                                       ": play it and select it for editing." +
                                       (empty[size_t(i)] ? " No sample loaded yet." : "");
        }
        pads_.setLabels(labels);
        pads_.setTips(tips);
        pads_.setEmpty(empty);
    }

    // SYSTEM.
    modeSel_.setText(kModeNames[size_t(std::clamp(iv(Global::VoiceMode), 0, 2))]);
    compSel_.setText(ste() ? "STE" : "ST");
    const bool midiClock = iv(Global::ArpClock) == 1;
    clkSel_.setText(kClkModes[midiClock ? 1 : 0]);
    // setClkMode: waiting for the clock, then SYNC or STOP at each publication of the engine state.
    if (midiClock != midiClockShown_) {
        midiClockShown_ = midiClock;
        clockRun_ = -1;
    }
    clkStatus_.setText(!midiClock ? "" : clockRun_ < 0 ? "waiting for clock..." : clockRun_ ? "SYNC" : "STOP");
    if (!keep(master_)) master_.setValue(iv(Global::Master));
    if (!masterv_.editing()) masterv_.setText(std::to_string(iv(Global::Master)));

    // Keyboard.
    const bool latin = host_.preference("noteNames", "0") == "1";
    std::array<std::string, 12> names;
    for (size_t i = 0; i < 12; ++i) names[i] = latin ? kNotesLat[i] : kNotesEn[i];
    kbd_.setNoteNames(names);
    layerBtn_.setOn(plugin::midiRouting(values_) == plugin::MidiRouting::Layer);
    latchBtn_.setOn(latch_);
    const int kbOct = std::min(baseOct_, 6);
    octv_.setText("Oct " + std::to_string(kbOct) + "-" + std::to_string(kbOct + 3));  // the four octaves drawn

    // Then everything that depends on the same state: CHIP indicator, register monitor, layout (the rows follow the
    // texts and the generator) and the floating windows.
    refreshChip();
    refreshRegisters();
    layout();
    seq_.refresh();
    song_.syncMode();
    song_.refresh();
    mix_.refresh();
    arp_.refresh();
    orn_.refresh();
    kit_.refresh();
    lib_.refresh();
    ins_.refresh();
    dma_.refresh();
    digi_.refresh();
    recWin_.refresh();
    curveWin_.refresh();
    kbdWin_.refresh();
    echo_.refresh();
    settings_.refresh();
    stepWin_.refresh();
    for (int i = 0; i < plugin::kTracks; ++i)
        if (scopes_[size_t(i)]) scopes_[size_t(i)]->setName(scopeName(i));  // refreshScopeTitle
}

void MainWindow::moveMainTo(int x, int y) {
    // Like the floating windows: left edge never negative and top edge below the menu bar, right and bottom edges
    // never past the desktop's.
    const gemui::Rect f = frame_.bounds();
    x = std::max(0, std::min(x, kWidth - f.w));
    y = std::max(int(kMenuH), std::min(y, kHeight - f.h));
    const int dx = x - frame_.bounds().x, dy = y - frame_.bounds().y;
    if (dx == 0 && dy == 0) return;
    frame_.moveBy(dx, dy);
    off_ = {off_.x + dx, off_.y + dy};
}

void MainWindow::layout() {
    // Moved main window: the layout is made at the window's home place, then carried by the offset; the
    // areas to repaint are those of the moved window.
    const gemui::Point off = off_;
    const bool moved = off.x != 0 || off.y != 0;
    // The frame goes back home without repainting (translate); while it is there, each area the layout invalidates is
    // shifted by the offset, so that what gets repainted is where the window really is.
    if (moved) {
        frame_.translate(-off.x, -off.y);
        desk_.setInvalidateShift(off);
    }
    layoutHome();
    if (moved) {
        desk_.setInvalidateShift({});
        frame_.translate(off.x, off.y);
    }
}

void MainWindow::placeEnvHz() {
    // At its natural width, which follows its text: "12800 ($3200)" and "_0.6" fit side by side.
    const gemui::Box e = epv_.box();
    const double right = labEp_.box().x + kRowW, uw = efUnit_.naturalWidth(), vw = efv_.naturalWidth();
    efUnit_.setBox({right - uw, e.y, uw, 16});
    efv_.setBox({right - uw - vw, e.y, vw, 16});
}

// Layout of the main window at its home place, redone at each refresh() and when a text of the SYSTEM frame changes:
// the widths follow the texts (track names, values, units) and the GENERATOR frame shows only the controls of the
// focused track's generator. Rows start at the inner edge of their frame, frame x + 12 (sx, ex, gx, yx below), and at
// y = 152 in the frames of the top row (30 px under their top edge, 122).
void MainWindow::layoutHome() {
    const bool m3 = mode3();
    const int gen = generator();
    const bool drum = drumTrack();

    // Tabs: gap 3 px, All on / All off aligned to the right.
    double x = kWx + 18;
    for (Button& b : tabs_) {
        const double w = b.naturalWidth();
        b.setBox({x, 88, w, 24});
        x += w + 3;
    }
    const double offW = allOff_.naturalWidth(), onW = allOn_.naturalWidth();
    allOff_.setBox({kWx + 962 - offW, 88, offW, 24});
    allOn_.setBox({kWx + 962 - offW - 3 - onW, 88, onW, 24});

    // SOURCE (frame as a flex column: 12 px between the rows).
    double y = 152;
    const double sx = kWx + 30;
    // Volume and Level as in ADSR: one label column, one value column, sliders as wide (item 104). The Source,
    // Voicing and Mode buttons start with these sliders (twelfth DAW test, item 159).
    y += placeRow(sx, y, {label(labSrc_, 58), button(srcSel_)}) + 12;
    y += placeRow(sx, y, {label(labPoly_, 58), button(polySel_)}) + 12;
    y += placeRow(sx, y, {label(labNp_)}) + 12;
    y += placeRow(sx, y, {slider(np_), value(npv_)}) + 12;
    y += placeRow(sx, y, {label(labVol_, 58), slider(vol_), value(volv_)}) + 12;
    y += placeRow(sx, y, {label(labVm_, 58), button(vmSel_)}) + 12;
    placeRow(sx, y, {label(labLevel_, 58), slider(level_), value(levelv_)});

    // ENVELOPE (frame as a block: collapsed margins, 6 px).
    const double ex = kWx + 268.5;
    y = 152;
    // The slider over the whole row, its value on the row of EP = (eighth DAW test, item 95).
    y += placeRow(ex, y, {label(labEf_, 64), slider(ef_)}) + 6;
    // Shape R13 right under EP =, without the prototype's 10 px space (sixth DAW test, item 57).
    y += placeRow(ex, y, {label(labEp_), value(epv_)}) + 6;
    placeEnvHz();
    placeRow(ex, y, {label(shapeLab_, 72), button(shapeSel_)});

    // VIBRATO, under SOURCE: On/Off, Depth, Rate.
    y = 436;
    y += placeRow(sx, y, {label(labVibOn_, 58), button(vibOn_)}) + 6;
    // Depth and Rate: a label column as wide as "Depth", not the 58 px of the On/Off row, for wider sliders, both as
    // wide (twelfth DAW test, item 158).
    const double vw = std::max(labDepth_.naturalWidth(), labRate_.naturalWidth());
    y += placeRow(sx, y, {label(labDepth_, vw), slider(vibDepth_), value(vibDepthv_), unit(depthUnit_)}) + 6;
    placeRow(sx, y, {label(labRate_, vw), slider(vibRate_), value(vibRatev_), unit(rateUnit_)});

    // ADSR, under ENVELOPE.
    y = 280;
    for (size_t k = 0; k < 4; ++k)
        y += placeRow(ex, y, {label(adsrLab_[k]), slider(adsr_[k]), value(adsrv_[k]), unit(adsrUnit_[k])}) + 6;
    placeRow(ex, y, {label(adsrModeLab_, 58), button(adsrMode_), hintItem(adsrHint_)});

    // GENERATOR.
    const double gx = kWx + 507;
    placeRow(gx, 152, {label(labType_), button(fxSel_)});
    const bool sid = gen == int(Generator::Sid), digi = gen == int(Generator::Digidrum),
               cdrum = gen == int(Generator::Chipdrum), pcm = gen == int(Generator::Dma);
    const bool sync = gen == int(Generator::SyncBuzzer);
    genSep_.setVisible(sid || digi || cdrum || pcm || sync);
    labDmaSlot_.setVisible(pcm);
    for (Button& b : dmaSlots_) b.setVisible(pcm);
    dmaEdit_.setVisible(pcm);
    dmaWave_.setVisible(pcm);
    dmaHint_.setVisible(pcm);
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&labRatio_, &ratio_, &ratioX_, &ratiov_, &labVmin_,
                                                                  &vmin_, &vminv_})
        w->setVisible(sid);
    // The bars and Steps hidden with Base, the plain square (Indus, 24/10/2026 20:30); Period only with Sine
    // (and Bounce), Width only with Square (16:19; the menu's forms).
    const WaveForm form = waveForm();
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&labSidWave_, &waveForm_}) w->setVisible(sid);
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&sidWave_, &labWaveSteps_, &waveSteps_, &waveStepsv_})
        w->setVisible(sid && form != WaveForm::Base);
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&labWavePeriod_, &wavePeriod_, &wavePeriodv_})
        w->setVisible(sid && ymengine::sineForm(form));
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&labWaveWidth_, &waveWidth_, &waveWidthv_})
        w->setVisible(sid && form == WaveForm::Square);
    for (gemui::Widget* w :
         std::initializer_list<gemui::Widget*>{&labBuzzSteps_, &labBuzzLen_, &buzzLen_, &buzzLenv_, &buzzUnit_})
        w->setVisible(sync);
    for (Button& b : buzzStep_) b.setVisible(sync);
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&labBuzTune_, &buzTuned_, &labBuzN_, &buzN_, &buzNv_,
                                                                  &labBuzD_, &buzD_, &buzDv_, &labBuzFine_, &buzFine_,
                                                                  &buzFinev_, &buzPad_, &buzPresets_})
        w->setVisible(sync);
    openKit_.setVisible(cdrum);
    kitName_.setVisible(cdrum);
    labSlot_.setVisible(digi);
    for (Button& b : slots_) b.setVisible(digi);
    digiEdit_.setVisible(digi);
    drumWave_.setVisible(digi);
    digiHint_.setVisible(digi);
    labDigiCh_.setVisible(digi && m3);
    digiChSel_.setVisible(digi && m3);
    digiChHint_.setVisible(digi && m3);
    const double stepsW = gemui::Font::width(" steps", buzzUnit_.px());  // unit of the buzzer's Steps row
    if (sid) {
        y = 208;
        y += placeRow(gx, y, {label(labRatio_)}) + 6;
        y += placeRow(gx, y, {slider(ratio_), {&ratioX_, ratioX_.naturalWidth()}, value(ratiov_, 0)}) + 6;
        y += placeRow(gx, y, {label(labVmin_)}) + 6;
        y += placeRow(gx, y, {slider(vmin_), value(vminv_)}) + 12;
        // The SID wave of the former Timer Wave window, centred (sixth DAW test, items 53 and 80), the
        // menu of its forms after its name (24/10/2026).
        y += placeRow(gx, y, {label(labSidWave_), button(waveForm_)}) + 6;
        sidWave_.setBox({gx + std::round((kRowW - SidWaveGrid::kWidth) / 2), y, double(SidWaveGrid::kWidth),
                         double(SidWaveGrid::kHeight)});
        y += SidWaveGrid::kHeight + 8;
        // Steps on the line of its label (Indus, 24/10/2026 17:42; above it before), then right under it
        // (17:43), in the same place, the period of the sine or the width of the square, the one of
        // the form chosen; the three sliders lined up after the widest label.
        const double lw = std::max({labWaveSteps_.naturalWidth(), labWavePeriod_.naturalWidth(),
                                    labWaveWidth_.naturalWidth()});
        y += placeRow(gx, y, {label(labWaveSteps_, lw), slider(waveSteps_), value(waveStepsv_)}) + 6;
        placeRow(gx, y, {label(labWavePeriod_, lw), slider(wavePeriod_), value(wavePeriodv_)});
        placeRow(gx, y, {label(labWaveWidth_, lw), slider(waveWidth_), value(waveWidthv_)});
    }
    if (sync) {
        // The Buzzer window's settings (third DAW test, item 28): sliders aligned after the widest label.
        const double lw = labBuzTune_.naturalWidth();
        y = 208;
        y += placeRow(gx, y, {label(labBuzTune_, lw), button(buzTuned_)}) + 6;
        y += placeRow(gx, y, {label(labBuzN_, lw), slider(buzN_), value(buzNv_)}) + 6;
        y += placeRow(gx, y, {label(labBuzD_, lw), slider(buzD_), value(buzDv_)}) + 6;
        y += placeRow(gx, y, {label(labBuzFine_, lw), slider(buzFine_), value(buzFinev_)}) + 12;
        // YM Pad and Presets centred in the frame (sixth DAW test, item 61).
        const double padsW = buzPad_.naturalWidth() + 8 + buzPresets_.naturalWidth();
        y += placeRow(gx + std::round((kRowW - padsW) / 2), y, {button(buzPad_), button(buzPresets_)}) + 12;
        // Its steps, from the former Timer Wave window (sixth DAW test, items 54 and 80): four buttons
        // across the frame, the shape's icon alone.
        y += placeRow(gx, y, {label(labBuzzSteps_)}) + 6;
        const double bw = std::floor((kRowW - 3 * 4) / 4), bh = icons::kShapes[0].h + 8;
        const double bx = gx + std::round((kRowW - (4 * bw + 3 * 4)) / 2);
        for (int k = 0; k < ymengine::kBuzzSteps; ++k)
            buzzStep_[size_t(k)].setBox({bx + (k % 4) * (bw + 4), y + (k / 4) * (bh + 4), bw, bh});
        y += 2 * bh + 4 + 8;
        placeRow(gx, y, {label(labBuzzLen_), slider(buzzLen_), value(buzzLenv_), unitW(buzzUnit_, stepsW)});
    }
    if (cdrum) placeRow(gx, 208, {button(openKit_), hintItem(kitName_)});
    // Samples button under slots 5 to 8, as wide as them (second DAW test, item 12).
    auto samplesButton = [&](Button& b, double gridX, double gridW, double top) {
        const double h = b.naturalHeight();
        b.setBox({gridX, top, gridW, h});
        return h;
    };
    if (digi) {
        // Slots: 4-column grid (31 px, 4 px gap) next to "Slot".
        const double slotW = slots_[0].naturalWidth(), slotH = slots_[0].naturalHeight();
        const double gridW = 4 * slotW + 3 * 4, gridH = 2 * slotH + 4;
        const double lx = gx, gridX = gx + labSlot_.naturalWidth() + 8;
        labSlot_.setBox({lx, 208 + (gridH - 16) / 2, labSlot_.naturalWidth(), 16});
        for (int k = 0; k < 8; ++k)
            slots_[size_t(k)].setBox({gridX + (k % 4) * (slotW + 4), 208 + (k / 4) * (slotH + 4), slotW, slotH});
        y = 208 + gridH + 4;
        y += samplesButton(digiEdit_, gridX, gridW, y) + 6;
        if (m3) {
            // Wrapped row: "ST channel" and the button, then the note across the width of the frame.
            const double h1 = placeRow(gx, y, {label(labDigiCh_), button(digiChSel_)});
            y += h1 + 8 + 6;
            y += placeParagraph(digiChHint_, gx, y) + 6;
        }
        drumWave_.setBox({gx, y, kRowW, 48});
        placeParagraph(digiHint_, gx, y + 48 + 6);
    }
    if (pcm) {
        // Same grid and margins as the Digidrum slots.
        const double slotW = dmaSlots_[0].naturalWidth(), slotH = dmaSlots_[0].naturalHeight();
        const double gridW = 4 * slotW + 3 * 4, gridH = 2 * slotH + 4, gridX = gx + labDmaSlot_.naturalWidth() + 8;
        labDmaSlot_.setBox({gx, 208 + (gridH - 16) / 2, labDmaSlot_.naturalWidth(), 16});
        for (int k = 0; k < ymengine::kDmaSlots; ++k)
            dmaSlots_[size_t(k)].setBox({gridX + (k % 4) * (slotW + 4), 208 + (k / 4) * (slotH + 4), slotW, slotH});
        y = 208 + gridH + 4;
        y += samplesButton(dmaEdit_, gridX, gridW, y) + 6;
        // Drawing of the sample, as the Digidrum frame shows its slot (item 20).
        dmaWave_.setBox({gx, y, kRowW, 48});
        placeParagraph(dmaHint_, gx, y + 48 + 6);
    }

    // SYSTEM.
    const double yx = kWx + 745.5;
    y = 152;
    // Voices, Computer and Clock: their buttons start together, after the widest label (thirteenth DAW test, item
    // 161).
    const double sw = std::max({labVoices_.naturalWidth(), labComputer_.naturalWidth(), labClock_.naturalWidth()});
    y += placeRow(yx, y, {label(labVoices_, sw), button(modeSel_)}) + 12;
    // Computer row under Voices (Indus's choice): the following rows move down by as much.
    y += placeRow(yx, y, {label(labComputer_, sw), button(compSel_)}) + 12;
    const double fit = sw + 8 + clkSel_.naturalWidth() + 8 + clkStatus_.naturalWidth();
    if (fit <= kRowW) {
        y += placeRow(yx, y, {label(labClock_, sw), button(clkSel_), label(clkStatus_)}) + 12;
    } else {  // flex-wrap: the state wraps to the next line
        const double h1 = placeRow(yx, y, {label(labClock_, sw), button(clkSel_)});
        clkStatus_.setBox({yx, y + h1 + 8, clkStatus_.naturalWidth(), 16});
        y += h1 + 8 + 16 + 12;
    }
    y += placeRow(yx, y, {label(labMaster_), slider(master_), value(masterv_)}) + 12;
    y += placeRow(yx, y, {label(labLeds_), {&leds_, 124, 28}}) + 12;
    placeRow(yx, y, {label(labNote_), label(tpread_)});
    // Options > Note monitor: hidden, the SYSTEM frame keeps its size, nothing moves.
    const bool note = noteShown();
    labNote_.setVisible(note);
    tpread_.setVisible(note);

    // Oscilloscope (Options > Oscilloscope, twelfth DAW test, item 149) and register monitor (Options >
    // Register monitor): hidden, what follows moves up by its height and its margin; keyboard or pads, bottom
    // row, height of the window.
    const bool scope = scopeShown(), regs = regsShown();
    const double up = scope ? 0 : 100, below = up + (regs ? 0 : 50);
    scope_.setVisible(scope);
    scope_.setBox({kWx + 18, 566, 944, 90});
    regs_.setVisible(regs);
    regs_.setBox({kWx + 18, 666 - up, 944, 40});
    kbd_.setBox({kWx + 18, 716 - below, 944, 110});
    pads_.setBox({kWx + 18, 714 - below, 944, 56});
    kbd_.setVisible(!drum);
    pads_.setVisible(drum);
    // 6 px under the pads (714 + 56) or the keyboard (716 + 110): the window is shorter for a percussion track.
    const double y0 = (drum ? 776 : 832) - below;
    double bx = kWx + 18;
    for (Button* b : {&latchBtn_, &layerBtn_, &octDown_, &octUp_}) {
        if (b == &octDown_) bx += 18;  // margin-left
        const double w = b->naturalWidth();
        b->setBox({bx, y0 + 6, w, 24});
        bx += w + 6;
    }
    octv_.setBox({bx, y0 + 10, octv_.naturalWidth(), 16});
    const double mapW = kbdMap_.naturalWidth();
    kbdMap_.setBox({kWx + 962 - mapW, y0 + 6, mapW, 24});
    // Without the prototype's help line, the window ends kHelpLine px higher: same margin under the buttons
    // as the prototype under that line.
    frame_.setBox({kWx, kWinY, kWinW, y0 + 88 - kHelpLine - kWinY});
}

// Live data published by the audio thread (EngineStatus, read through Host::engineStatus only when something new
// came): voice LEDs, period of the last note attacked, ADSR position of each note sounding, oscilloscope, instruments
// chosen by Program Change, MIDI clock state, then the same for the floating windows that show live data.
void MainWindow::refreshEngineStatus(const plugin::EngineStatus& t) {
    leds_.setState(t.active, voices());
    // Note: TP and the tone frequency clock / (16 × TP).
    if (t.noteSerial != noteSerial_ && t.notePeriod > 0) {
        noteSerial_ = t.noteSerial;
        lastTp_ = t.notePeriod;
        const double clk = plugin::chipClock(values_).hz();
        tpread_.setText("TP=" + std::to_string(t.notePeriod) + " -> " +
                        fmt("%.2f Hz", clk / (16.0 * t.notePeriod)));
        layout();
    }
    // No voice sounds any more, its release ended: "Note -", as at launch (Indus, 26/10/2026 04:00-04:02).
    if (!t.noteSounding && tpread_.text() != "-") {
        tpread_.setText("-");
        layout();
    }
    // A note held on screen, Latch or not, whose voice the sequencer took no longer sounds: its key lets go (Indus,
    // 25/10/2026 19:41). In 16 channels the engine moves such a note to a free voice instead.
    for (int n = 0; n < 128; ++n)
        if (t.manualCut[size_t(n)] != manualCut_[size_t(n)]) {
            manualCut_[size_t(n)] = t.manualCut[size_t(n)];
            forceRelease(n);
        }
    std::array<AdsrCurve::LivePoint, plugin::kEnvPoints> live;  // one per note sounding on the track
    const int points = std::min<int>(t.envCount, plugin::kEnvPoints);
    for (int k = 0; k < points; ++k) live[size_t(k)] = {t.env[size_t(k)].stage, t.env[size_t(k)].level};
    curve_.setLive(std::span(live.data(), size_t(points)));
    scope_.setSamples(t.scope);
    if (t.program != program_) {
        program_ = t.program;
        refreshRegisters();
    }
    // SYNC / STOP (st.run of the prototype): MIDI clock received running (Start, Continue) or stopped.
    if (midiClockShown_ && int(t.seq.clockRunning) != clockRun_) {
        clockRun_ = int(t.seq.clockRunning);
        clkStatus_.setText(clockRun_ ? "SYNC" : "STOP");
        layout();
    }
    seq_.refreshEngineStatus(t);
    song_.refreshEngineStatus(t);
    mix_.refreshEngineStatus(t);
    arp_.refreshEngineStatus(t);
    digi_.refreshEngineStatus(t);
    dma_.refreshEngineStatus(t);
    for (int i = 0; i < plugin::kTracks; ++i)  // drawBigScope
        if (scopes_[size_t(i)] && scopes_[size_t(i)]->isOpen()) scopes_[size_t(i)]->setPoints(t.trackScope[size_t(i)]);
}

void MainWindow::refreshRegisters() {
    // YM Player playing: the monitor follows the registers of the tune, channel A ("The register strip and scope
    // follow", note of #ymWin; the prototype's monitor stayed on the focused track).
    if (ymWin_.isOpen() && ymWin_.playing()) {
        const Host::YmPlayer y = host_.ymPlayer();
        if (y.playing) {
            regs_.setValues({y.regs[0], y.regs[1], y.regs[6], y.regs[7], y.regs[8], y.regs[11], y.regs[12], y.regs[13]});
            regs_.setInfo("CH A / YM PLAYER");
            return;
        }
    }
    // Registers of the focused track (setInterval of 120 ms in the prototype).
    // As written for channel A: R0/R1 the 12-bit tone period of the last note attacked, R6 the noise period, R7 the
    // mixer (a cleared bit enables: tone bit 0, noise bit 3; bits 6 and 7 left at 0), R8 the volume (bit 4: envelope
    // mode, bits 0-3: fixed level), R11/R12 the 16-bit envelope period, R13 the shape.
    const bool tone = iv(Track::Tone) != 0, noise = iv(Track::Noise) != 0;
    const int ep = std::clamp(iv(Track::EnvPeriod), 1, 65535);
    const std::array<uint8_t, 8> v = {uint8_t(lastTp_ & 0xFF),
                                      uint8_t((lastTp_ >> 8) & 0x0F),
                                      uint8_t(iv(Track::NoisePeriod)),
                                      uint8_t(~((tone ? 1 : 0) | (noise ? 8 : 0)) & 0x3F),
                                      uint8_t((iv(Track::VolMode) << 4) | iv(Track::Volume)),
                                      uint8_t(ep & 0xFF),
                                      uint8_t((ep >> 8) & 0xFF),
                                      uint8_t(iv(Track::EnvShape))};
    regs_.setValues(v);
    std::string ch;
    if (mode3()) {
        ch = chips() > 1 ? (cur_ < 8 ? kVoiceLong2[size_t(cur_)] : "?") : (cur_ < 4 ? kVoiceLong1[size_t(cur_)] : "?");
        if (ch.rfind("Voice ", 0) == 0) ch = ch.substr(6);
    } else {
        ch = std::to_string(cur_ + 1);
    }
    // Instrument chosen by Program Change: it replaces the sound of the track, its name replaces the generator.
    std::string sound = kFxShort[size_t(generator())];
    if (const int pc = program_[size_t(cur_)]; pc > 0 && !drumTrack()) {
        char b[12];
        std::snprintf(b, sizeof b, "INS %02X", unsigned(pc));
        sound = b;
    }
    regs_.setInfo("CH " + ch + " / " + sound + (mode3() ? " / 3CH AUTH (shared env+noise)" : " / 16CH"));
}

std::vector<std::string> MainWindow::chipIssues() const {
    // chipIssues: what a register dump (VBL, YM3, YM6, SNDH) would not render, according to the current pattern and the
    // settings of the tracks that have notes in it. Digidrum, DMA and dual chip lines according to the register exports.
    std::vector<std::string> out;
    const ymseq::Pattern& pat = extras_.seq.cur();
    const bool m3 = mode3();
    auto plural = [](int n, const char* word) { return std::to_string(n) + " " + word + (n > 1 ? "s" : ""); };
    std::vector<int> live;
    for (int i = 0; i < plugin::kTracks; ++i) {
        const ymseq::Lane& l = pat.lane(i);
        if (iv(i, Track::On) && std::any_of(l.pat.begin(), l.pat.end(), [](int16_t v) { return v >= 0; }))
            live.push_back(i);
    }

    // Number of channels.
    if (!m3 && live.size() > 3)
        out.push_back(std::to_string(live.size()) + " tracks with notes \u2014 a YM2149 has three voices (A, B, C)");
    if (m3 && chips() > 1) out.push_back("Dual chip: VBL, YM and SNDH exports need Authentic 3ch (one chip)");

    // Machine: the ZX Spectrum and Amstrad CPC clocks, greyed out in Authentic 3ch.
    const auto& clocks = plugin::paramSpec(paramIndex(Global::ChipClock)).choices;
    const int clock = std::clamp(iv(Global::ChipClock), 0, int(clocks.size()) - 1);
    if (clock >= kFirstAyClock)
        out.push_back("Chip clock " + std::string(clocks[size_t(clock)]) +
                      " is not an Atari clock (an ST or STE runs its YM at 2 MHz)");
    // Custom clock: another frequency than 2 MHz, in 16 ch (2 MHz in the other modes).
    if (clock == 0 && plugin::chipClock(values_).num != uint64_t(plugin::kCustomClockDefaultHz))
        out.push_back("Chip clock Custom " + clockMHz(uint32_t(plugin::chipClock(values_).num)) +
                      " MHz is not an Atari clock (an ST or STE runs its YM at 2 MHz)");

    // Chords: the chip has only one note per channel.
    for (int i : live) {
        const ymseq::Lane& l = pat.lane(i);
        int poly = 0;
        for (int s = 0; s < ymseq::kSteps; ++s) {
            int n = 0;
            for (int v = 1; v <= ymseq::kPolyMax; ++v) n += l.vp(v, s) >= 0 ? 1 : 0;
            poly += n > 1 ? 1 : 0;
        }
        if (poly)
            out.push_back(trackName(i) + ": " + plural(poly, "step") +
                          " with a chord \u2014 one YM voice plays one note at a time");
    }

    // Representable pitches: TP fits in 12 bits (at the chosen clock; 2 MHz in the prototype).
    const double tpClock = plugin::chipClock(values_).hz() / 16.0;
    const double a4 = double(pv(Global::A4Hz));
    for (int i : live) {
        const ymseq::Lane& l = pat.lane(i);
        const int trackOct = extras_.seq.tracks[size_t(i)].octave;
        int low = 0, high = 0;
        for (int s = 0; s < ymseq::kSteps; ++s) {
            if (l.pat[size_t(s)] < 0) continue;
            // MIDI note of the step (C4 = 60), its equal-tempered frequency (from the Tuning A4), then TP =
            // clock / (16 × f).
            const int abs = (ymseq::octaveOf(l.oct[size_t(s)], trackOct) + 1) * 12 + l.pat[size_t(s)];
            const double f = ymengine::noteHz(abs, a4);
            const double tp = std::floor(tpClock / f + 0.5);
            low += tp > 4095 ? 1 : 0;
            high += tp < 1 ? 1 : 0;
        }
        if (low) out.push_back(trackName(i) + ": " + plural(low, "note") + " below the chip range (period would exceed 4095)");
        if (high) out.push_back(trackName(i) + ": " + plural(high, "note") + " above the chip range");
    }

    // What cannot be written to the registers.
    bool timed = false;  // a live track plays a timer effect
    for (int i : live) {
        const int g = int(plugin::effectiveGenerator(values_, i));
        const bool drumGen = g == int(Generator::Chipdrum) || g == int(Generator::Digidrum);  // ADSR not used
        const bool adsr = !drumGen && (iv(i, Track::Attack) || iv(i, Track::Decay) || iv(i, Track::Release) ||
                                       iv(i, Track::Sustain) < 100);
        // Effective mode, like adsrChip(): Authentic and dual chip force the Chip ADSR outside the DMA tracks.
        const bool adsrChip = (m3 && g != int(Generator::Dma)) || iv(i, Track::AdsrMode) == 1;
        // Timer effects that the exports write with MFP settings (captureEffect of the engine): SID, Sync Buzzer and
        // Digidrum. The Chipdrum is played from registers there.
        timed = timed || g == int(Generator::Sid) || g == int(Generator::SyncBuzzer) || g == int(Generator::Digidrum);
        if (adsr && !adsrChip && !m3) out.push_back(trackName(i) + ": ADSR in Smooth mode (audio gain, not in registers)");
        if (g == int(Generator::SyncBuzzer) && adsr && !adsrChip)
            out.push_back(trackName(i) + ": ADSR on a Sync Buzzer \u2014 the hardware envelope drives the level");
        if (g == int(Generator::Digidrum))
            out.push_back(trackName(i) + ": Digidrum replays its sample in YM6 and SNDH only (silent in VBL and YM3)");
        if (g == int(Generator::Dma)) out.push_back(trackName(i) + ": DMA sound of the STE, carried by SNDH only");
        if (g == int(Generator::Chipdrum) && !m3)
            out.push_back(trackName(i) +
                          ": Chipdrum uses sample tables here (Authentic 3ch synthesises it from registers)");
        // The prototype's limit, a timer at 4 times the note: ratio 2 since ratio 1 plays the note.
        const float ratio = values_[size_t(paramIndex(i, Track::SidRatio))];
        if (g == int(Generator::Sid) && ratio > 2)
            out.push_back(trackName(i) + ": SID ratio x" + fmt("%.2f", ratio) +
                          " needs a timer faster than most ST replayers run");
    }
    // Free timer (greyed out in Authentic 3ch): the exports round its rates to the MFP's, like an ST or STE.
    if (timed && iv(Global::Timer) == kTimerFree)
        out.push_back("Timer Free: exact timer rates, which the MFP of an ST or STE rounds");

    // A single DMA channel on the STE: the SNDH plays the most recent DMA note.
    const auto dmaLive = std::count_if(live.begin(), live.end(), [&](int i) {
        return plugin::effectiveGenerator(values_, i) == Generator::Dma;
    });
    if (dmaLive > 1)
        out.push_back(std::to_string(dmaLive) +
                      " DMA tracks with notes \u2014 the STE has one DMA channel, the SNDH plays the latest note");

    // Rates: everything plays in VBL frames.
    const int vbl = plugin::kVblRates[size_t(std::clamp(iv(Global::VblRate), 0, int(plugin::kVblRates.size()) - 1))];
    const double stepHz = double(pv(Global::FreeTempo)) / 60.0 * 4.0;  // BPM, four steps per beat
    if (stepHz > vbl)
        out.push_back("Tempo: " + fmt("%.1f", stepHz) + " steps/s exceeds the " + std::to_string(vbl) +
                      " Hz VBL rate \u2014 steps would be dropped");
    for (int i : live) {
        if (iv(i, Track::ArpOn) && iv(i, Track::ArpRate) > vbl)
            out.push_back(trackName(i) + ": arpeggio at " + std::to_string(iv(i, Track::ArpRate)) +
                          " Hz is faster than the VBL rate");
        const ymengine::TrackPatch& patch = extras_.tracks[size_t(i)].patch;
        if (patch.ornamentOn && patch.ornamentRate > 4)
            out.push_back(trackName(i) + ": modulation table runs faster than one step per VBL");
        if (iv(i, Track::VibDepth) > 0 && !m3)
            out.push_back(trackName(i) + ": vibrato is smooth here; on hardware it steps at the VBL rate");
    }

    // Glissando: the curves are not register writes.
    for (int i : live) {
        const auto& slc = pat.lane(i).slc;
        if (std::any_of(slc.begin(), slc.end(), [](uint8_t v) { return v > 1; }))
            out.push_back(trackName(i) + ": slide curves other than Linear are interpolated, not written per VBL");
    }

    // Mixing settings ignored by a register dump.
    if (!m3) {
        auto any = [&](auto pred) { return std::any_of(live.begin(), live.end(), pred); };
        if (any([&](int i) { return iv(i, Track::Pan) != 0; })) out.push_back("Mixer pan is ignored (the ST is mono)");
        if (any([&](int i) {
                const auto& pnr = pat.lane(i).pnr;
                return std::any_of(pnr.begin(), pnr.end(), [](uint8_t v) { return v != 0; });
            }))
            out.push_back("Per-step PAN row is ignored (the ST is mono)");
        if (any([&](int i) { return iv(i, Track::Level) < 100; }))
            out.push_back("Mixer levels are ignored (only the 4-bit volume is written)");
        // 70: the default of Master (Parameters.cpp).
        if (iv(Global::Master) != 70) out.push_back("Master level is a playback gain, not a register value");
        if (iv(Global::MasterPan) != 0 || (iv(Global::MasterLink) == 0 && iv(Global::Master2) != iv(Global::Master)))
            out.push_back("Master L / R and balance are ignored (the ST is mono)");
    }
    return out;
}

void MainWindow::refreshChip() {
    const size_t n = chipIssues().size();
    menu_.setIndicator(chipIndicator_, n ? "CHIP: " + std::to_string(n) : "CHIP: OK", n > 0);
}

void MainWindow::showChipAlert() {
    const auto issues = chipIssues();
    gemui::Alert::Spec spec;
    spec.lines.push_back({"Chip conformity"});
    spec.lines.push_back({""});
    if (issues.empty()) {
        spec.lines.push_back({"Everything in this pattern can be written to YM registers."});
        spec.lines.push_back({"VBL, YM3, YM6 and SNDH exports will match what you hear."});
    } else {
        spec.lines.push_back({"These may sound different in VBL, YM3, YM6 or SNDH exports:"});
        for (const std::string& s : issues) spec.lines.push_back({"• " + s});
    }
    gemui::Alert::open(desk_, std::move(spec));
}

// ------------------------------------------------------------------------------------------------ actions

// What the controls, the menus and the keys do: generator and track choices, track clipboard, notes played on screen
// (piano, pads, Computer Keyboard) and from the host, panic.

void MainWindow::pickGenerator(int g) {
    if (g < 0 || g >= ymengine::kGenerators) return;
    if (!plugin::generatorAllowed(plugin::voiceMode(values_), ste(), cur_, Generator(g))) return;  // fixed role
    if (g != iv(Track::Generator)) manPanic();  // the old timbre must not survive
    set(Track::Generator, float(g));
    // Sync Buzzer: the envelope must crawl between two restarts of the timer (prototype EP > 100 → 1, × 32).
    if (g == int(Generator::SyncBuzzer) && iv(Track::EnvPeriod) > 3200) set(Track::EnvPeriod, 32);
}

void MainWindow::focusTrack(int track) {
    track = std::clamp(track, 0, plugin::kTracks - 1);
    if (track == cur_) return;
    set(Global::FocusTrack, float(track + 1));
}

void MainWindow::tabClicked(int track) {
    // First click: editing. Click on the tab already chosen: track active or not.
    if (track == cur_) set(paramIndex(track, Track::On), iv(track, Track::On) ? 0.0f : 1.0f);
    else focusTrack(track);
}

void MainWindow::tabMenu(int track, gemui::Point) {
    std::vector<MenuItem> items(4);
    items[0].label = "Copy settings";
    items[0].pick = [this, track] { copyTrack(track); };
    items[1].label = "Paste settings";
    items[1].pick = [this, track] { pasteTrack(track); };
    items[2].separator = true;
    items[3].label = "Rename...";
    if (mode3()) {
        items[3].disabled = true;
        items[3].why = "The three YM voices are named by the chip";
    }
    items[3].pick = [this, track] { renameTrack(track); };
    popup(tabs_[size_t(track)], std::move(items), -1);
}

void MainWindow::setAllEnabled(bool on) {
    for (int i = 0; i < voices(); ++i) set(paramIndex(i, Track::On), on ? 1.0f : 0.0f);
}

void MainWindow::copyTrack(int track) {
    // chanCopy: patch, arpeggio and level (neither the activation, nor the pan, nor Mute / Solo).
    // Every parameter of the track is copied (pasteTrack skips the ones above), with its non-automatable part kept in
    // tracks[0] of an otherwise default state.
    std::array<float, plugin::kTrackParams> v{};
    for (int f = 0; f < plugin::kTrackParams; ++f) v[size_t(f)] = values_[size_t(paramIndex(track, Track(f)))];
    clipboard_ = v;
    plugin::NonParamState s;
    s.tracks[0] = extras_.tracks[size_t(track)];
    clipboardExtras_ = std::move(s);
}

void MainWindow::pasteTrack(int track) {
    if (!clipboard_ || !clipboardExtras_) return;
    for (int f = 0; f < plugin::kTrackParams; ++f) {
        const Track t = Track(f);
        if (t == Track::On || plugin::mixerOnly(t)) continue;
        set(paramIndex(track, t), (*clipboard_)[size_t(f)]);
    }
    const ymengine::TrackParams src = clipboardExtras_->tracks[0];
    host_.editExtras([track, src](plugin::NonParamState& s) {
        s.tracks[size_t(track)].patch = src.patch;
        s.tracks[size_t(track)].arp = src.arp;
    });
}

void MainWindow::renameTrack(int track) {
    if (mode3()) return;  // 3 channels: channels named by the chip
    gemui::Alert::Spec spec;
    spec.lines = {{"Track name, 8 chars (blank = number):"}};
    spec.promptButtons();
    spec.inputText = extras_.trackNames[size_t(track)];
    spec.inputChars = 10;
    spec.inputMax = int(plugin::kTrackNameMax);
    gemui::Alert::open(desk_, std::move(spec), [this, track](int button, const std::string& text) {
        if (button != gemui::Alert::kPromptOk) return;
        const std::string name = plugin::cleanTrackName(text);
        host_.editExtras([track, name](plugin::NonParamState& s) { s.trackNames[size_t(track)] = name; });
    });
}

void MainWindow::trackMenu(const gemui::Box& anchor) {
    // chanMenu: choose the track played by the keyboard.
    std::vector<MenuItem> items;
    for (int i = 0; i < voices(); ++i) {
        std::string nm;
        if (dmaTrack(i)) nm = trackName(i);  // STE DMA tracks in 3-channel mode: DMA1 to DMA8, like their tabs
        else if (mode3()) nm = chips() > 1 ? kVoiceLong2[size_t(i)] : (i == 3 ? "Digi" : kVoiceLong1[size_t(i)]);
        else nm = extras_.trackNames[size_t(i)].empty() ? "Channel " + std::to_string(i + 1)
                                                        : extras_.trackNames[size_t(i)];
        MenuItem m;
        // One space before the type: the prototype writes two, which the browser collapses to one (white-space:nowrap).
        m.label = nm + " [" + kFxNames[size_t(plugin::effectiveGenerator(values_, i))] + "]";
        m.disabled = iv(i, Track::On) == 0;
        m.pick = [this, i] { focusTrack(i); };
        items.push_back(std::move(m));
    }
    gemui::MenuList::open(desk_, std::move(items), cur_, anchor, false);
}

// Note played on screen (MIDI note number; mouse: from the piano, otherwise the Computer Keyboard or a pad): sent to
// the engine as a screen note on, recorded by the sequencer when it records, and lit on the piano, the pads and the
// keyboard drawing. held_ lists these notes until their release.
void MainWindow::press(int note, int velocity, bool mouse) {
    const bool drumNow = drumTrack();
    if (!drumNow && (note < kPlayLo || note > kPlayHi)) return;  // out of the YM's range
    if (held_.count(note)) {
        if (latch_) forceRelease(note);  // Latch: a new press releases
        return;
    }
    if (latch_ && mouse)  // Latch with the mouse: one note at a time
        for (auto it = held_.begin(); it != held_.end();) {
            const int n = it->first;
            const bool m = it->second.mouse;
            ++it;
            if (m) forceRelease(n);
        }
    held_[note] = {mouse};
    seq_.recordNote(note, velocity);
    host_.screenMidi(0x90, uint8_t(note), uint8_t(velocity));
    kbd_.setHeld(note, true);
    if (drumNow && note >= kPadNote && note < kPadNote + 8) pads_.setHeld(note - kPadNote, true);
    kbdWin_.light(note, true);  // tklNote
}

void MainWindow::pressPad(int pad) {
    if (generator() == int(Generator::Digidrum)) selectSlot(pad);  // the pad chooses the slot
    press(kPadNote + pad, 118, false);
}

// Release whatever the Latch: note off to the engine; the key goes dark unless the host or the sequencer still lights
// it.
void MainWindow::forceRelease(int note) {
    if (!held_.erase(note)) return;
    host_.screenMidi(0x80, uint8_t(note), 0);
    if (hostHeld_[size_t(note)]) return;  // still held by the host
    if (!seqLit(note)) kbd_.setHeld(note, false);
    const int pad = note - kPadNote;
    if (pad >= 0 && pad < 8 && std::find(seqLitPads_.begin(), seqLitPads_.end(), pad) == seqLitPads_.end())
        pads_.setHeld(pad, false);
    kbdWin_.light(note, false);
}

bool MainWindow::seqLit(int note) const {
    return std::find(seqLitKeys_.begin(), seqLitKeys_.end(), note) != seqLitKeys_.end();
}

void MainWindow::seqKbdLight(int step) {
    std::vector<int> keys, pads;
    const bool drum = drumTrack();
    if (step >= 0 && step < ymseq::kSteps) {
        const ymseq::Lane& l = extras_.seq.cur().lane(cur_);
        const bool held = l.pat[size_t(step)] < 0 && l.tie[size_t(step)];
        if (drum && held && !seqLitPads_.empty()) return;  // held: the pads stay
        const int trackOct = extras_.seq.tracks[size_t(cur_)].octave;
        auto noteOf = [&](int v, int k) { return (ymseq::octaveOf(l.vo(v, k), trackOct) + 1) * 12 + l.vp(v, k); };
        // Only what the engine plays: the root, plus the chord voices that the grid does not grey out.
        const int played = seq_.chordsPlay() ? ymseq::kPolyMax : 1;
        if (drum) {
            for (int v = 1; v <= played; ++v)
                if (l.vp(v, step) >= 0) pads.push_back(l.vp(v, step) % 8);
        } else {
            // The root of the step, or the one a tie holds.
            if (l.pat[size_t(step)] >= 0) keys.push_back(noteOf(1, step));
            else if (held && seqRootKey_ >= 0) keys.push_back(seqRootKey_);
            seqRootKey_ = keys.empty() ? -1 : keys.front();
            // Deviation: every chord voice that sounds at this step, started here or earlier and still within
            // its steps. The prototype lit only the voices starting at the step and kept the keys of a held root, so
            // a voice started under a held note never lit and went dark with the root.
            for (int v = 2; v <= played; ++v)
                for (int k = 0; k <= step; ++k) {
                    if (l.vp(v, k) < 0 || k + std::max(1, int(l.vdur[size_t(v - 2)][size_t(k)])) <= step) continue;
                    const int n = noteOf(v, k);
                    if (std::find(keys.begin(), keys.end(), n) == keys.end()) keys.push_back(n);
                }
        }
    }
    // litSwap: switches off the outgoing ones (except a note held on screen or by the host), lights the incoming ones.
    auto swap = [](std::vector<int>& lit, const std::vector<int>& next, const auto& set) {
        for (int n : lit)
            if (std::find(next.begin(), next.end(), n) == next.end()) set(n, false);
        for (int n : next) set(n, true);
        lit = next;
    };
    auto key = [this](int n, bool on) {
        if (!on && (held_.count(n) || (n >= 0 && n < 128 && hostHeld_[size_t(n)]))) return;
        kbd_.setHeld(n, on);
    };
    auto pad = [this](int n, bool on) {
        if (!on && held_.count(kPadNote + n)) return;  // still held on the Computer Keyboard
        pads_.setHeld(n, on);
    };
    if (step < 0) {  // seqKbdClear
        seqRootKey_ = -1;
        swap(seqLitKeys_, {}, key);
        swap(seqLitPads_, {}, pad);
        kbdWin_.lightSeq({});
        return;
    }
    if (drum) {
        swap(seqLitPads_, pads, pad);
        kbdWin_.lightSeqPads(pads);
        return;
    }
    swap(seqLitKeys_, keys, key);
    kbdWin_.lightSeq(keys);  // tklNotes: the drawing follows playback
}

void MainWindow::hostNote(const std::array<uint8_t, 3>& m) {
    // onMIDI of the prototype (press, release), without sending the note back to the engine, which has already played
    // it: keys lit and recording into the sequencer. Only the notes of the focused track count (with routing by
    // channel, those of its channel).
    const int status = m[0] & 0xF0, note = m[1] & 0x7F, vel = m[2] & 0x7F;
    auto unlight = [this](int n) {
        hostHeld_[size_t(n)] = false;
        if (held_.count(n)) return;  // still held on screen
        if (!seqLit(n)) kbd_.setHeld(n, false);
        kbdWin_.light(n, false);
    };
    if (status == 0xB0) {  // CC 120 and 123: everything is cut
        for (int n = 0; n < 128; ++n)
            if (hostHeld_[size_t(n)]) unlight(n);
        return;
    }
    if (status == 0x90 && vel > 0) {
        if (plugin::midiRouting(values_) == plugin::MidiRouting::PerChannel && (m[0] & 0x0F) != cur_) return;
        if (!drumTrack() && (note < kPlayLo || note > kPlayHi)) return;  // out of the YM's range
        if (hostHeld_[size_t(note)]) return;
        hostHeld_[size_t(note)] = true;
        if (held_.count(note)) return;  // already held on screen: neither written nor relit (prototype press)
        seq_.recordNote(note, vel);
        kbd_.setHeld(note, true);
        kbdWin_.light(note, true);
        return;
    }
    if (hostHeld_[size_t(note)]) unlight(note);
}

void MainWindow::release(int note) {
    if (latch_) return;  // Latch: the note holds after release
    forceRelease(note);
}

void MainWindow::releaseAll() {
    while (!held_.empty()) forceRelease(held_.begin()->first);
}

// Two levels of silence, sent as screen MIDI: manPanic cuts what is played by hand (CC 120), panic cuts every voice of
// the engine (CC 123, Host::screenMidi). Both forget the held keys.
void MainWindow::manPanic() {
    // Silence of the on-screen playing and of the host notes (manOff) and state of the keys.
    for (const auto& [note, h] : held_) kbd_.setHeld(note, false);
    held_.clear();
    for (int n = 0; n < 128; ++n)
        if (std::exchange(hostHeld_[size_t(n)], false)) kbd_.setHeld(n, false);
    pads_.clearHeld();
    host_.screenMidi(0xB0, 120, 0);
}

void MainWindow::panic() {
    held_.clear();
    hostHeld_.fill(false);
    kbd_.clearHeld();
    pads_.clearHeld();
    host_.screenMidi(0xB0, 123, 0);
}

void MainWindow::selectDmaSlot(int slot) {
    const int track = cur_;
    host_.editExtras([track, slot](plugin::NonParamState& s) { s.tracks[size_t(track)].patch.dmaSlot = int8_t(slot); });
}

void MainWindow::setComputer(int machine) {
    // Changing machine cuts the notes like a mode change; in 3-channel mode, a DMA track that is left hands
    // control back to the first track.
    if (machine == iv(Global::Computer)) return;
    set(Global::Computer, float(machine));
    panic();
    if (cur_ >= voices()) focusTrack(0);
}

void MainWindow::editPatch(const std::function<void(ymengine::TrackPatch&)>& fn) {
    if (drumTrack()) return;
    const int t = cur_;
    host_.editExtras([t, &fn](plugin::NonParamState& s) { fn(s.tracks[size_t(t)].patch); });
    // Read back before pushCur: it compares the armed step with extras_, which must already hold the edit.
    pullExtras();
    pushCur();
}

void MainWindow::buzPresetsMenu(gemui::Widget& anchor) {
    std::vector<MenuItem> items;
    for (int k = 0; k < int(kBuzzerPresets.size()); ++k) {
        MenuItem m;
        m.label = kBuzzerPresets[size_t(k)].name;
        m.pick = [this, k] { applyBuzzerPreset(k); };
        items.push_back(std::move(m));
    }
    trackPopup(anchor, std::move(items), gemui::kNoMarks);
}

MainWindow::WaveForm MainWindow::waveFormOf(const ymengine::TrackPatch& p) noexcept { return p.sidForm; }

MainWindow::WaveForm MainWindow::waveForm() const noexcept { return waveFormOf(extras_.tracks[size_t(cur_)].patch); }

void MainWindow::waveFormMenu(gemui::Widget& anchor) {
    std::vector<MenuItem> items;
    int checked = 0;
    for (const WaveForm f : kSidFormMenu) {
        if (f == waveForm()) checked = int(items.size());
        MenuItem m;
        m.label = kSidFormNames[size_t(f)];
        m.pick = [this, f] { setWaveForm(f); };
        items.push_back(std::move(m));
    }
    trackPopup(anchor, std::move(items), checked);
}

// A form of the SID wave menu (24/10/2026), one undo step; each form keeps the Steps, Period and Width of the
// others. Sine and Bounce: the sine at the phase and period of the track, between volumes 3 and 15;
// from Base, as many Steps as its Period, one cycle in a period of the note, which plays the note (16 Steps
// before). Custom: the bars drawn; bars all at 0 from Base or Square take the square played then, to draw from.
void MainWindow::setWaveForm(WaveForm f) {
    const WaveForm was = waveForm();
    if (f == was) return;
    const uint8_t low = uint8_t(std::clamp(iv(Track::SidVmin), 0, 15));
    editPatch([f, was, low](ymengine::TrackPatch& p) {
        p.sidForm = f;
        const int n = std::clamp<int>(p.sidWaveLen, 2, ymengine::kSidWaveSteps);
        if (ymengine::sineForm(f)) {
            p.sidWave = ymengine::sineSidWave(3, 15, p.sidWavePhase, p.sidWavePeriod, f == WaveForm::Bounce);
            if (was == WaveForm::Base)
                p.sidWaveLen = uint8_t(std::clamp<int>(p.sidWavePeriod, 2, ymengine::kSidWaveSteps));
        } else if (f == WaveForm::Custom && !ymengine::sineForm(was) &&
                   std::all_of(p.sidWave.begin(), p.sidWave.end(), [](uint8_t v) { return v == 0; })) {
            const int high = was == WaveForm::Square ? std::clamp<int>(p.sidWidth, 1, n - 1) : n / 2;
            for (int k = 0; k < ymengine::kSidWaveSteps; ++k) p.sidWave[size_t(k)] = k < high ? 15 : k < n ? low : 0;
        }
    });
}

void MainWindow::buzzStepMenu(int step, gemui::Widget& anchor) {
    // The shape of the track, then the eight distinct shapes of R13 (0 to 7 repeat 9 and F).
    static constexpr const char* kHex = "0123456789ABCDEF";
    const int cur = extras_.tracks[size_t(cur_)].patch.buzzShapes[size_t(step)];
    const int track = std::clamp(iv(Track::EnvShape), 0, 15);
    std::vector<MenuItem> items;
    int checked = 0;
    auto add = [&](std::string label, int v, int icon) {
        if (v == cur) checked = int(items.size());
        MenuItem m;
        m.label = std::move(label);
        m.icon = &icons::kShapes[size_t(icon)];
        m.pick = [this, step, v] {
            editPatch([step, v](ymengine::TrackPatch& p) { p.buzzShapes[size_t(step)] = uint8_t(v); });
        };
        items.push_back(std::move(m));
    };
    add(std::string("Track (") + kHex[track] + ")", 0, track);
    for (int s = 8; s < 16; ++s) add(std::string(1, kHex[s]), s, s);
    if (cur > 0 && cur < 8) add(std::string(1, kHex[cur]), cur, cur);  // value from an old state or a patch
    trackPopup(anchor, std::move(items), checked);
}

void MainWindow::applyBuzzerPreset(int k) {
    // A single undo step: the first setting pushes the previous state (armed when the button is pressed). The track
    // stays a Sync Buzzer, its volume mode, tone and noise untouched.
    if (drumTrack() || generator() != int(Generator::SyncBuzzer)) return;
    const BuzzerPreset& p = kBuzzerPresets[size_t(k)];
    set(Track::EnvShape, float(p.shape));
    editPatch([&p](ymengine::TrackPatch& t) {
        t.envTuned = true;
        t.envRatioN = uint8_t(p.n);
        t.envRatioD = uint8_t(p.d);
        t.envFineCents = 0;
        t.ymPad = p.pad;
    });
}

void MainWindow::selectSlot(int slot) {
    const int track = cur_;
    host_.editExtras([track, slot](plugin::NonParamState& s) {
        s.tracks[size_t(track)].patch.digidrumSlot = uint8_t(slot);
    });
}

void MainWindow::showMessage(std::string_view text) {
    gemui::Alert::Spec spec;
    spec.lines = {{std::string(text)}};
    gemui::Alert::open(desk_, std::move(spec));
}

void MainWindow::notAvailable(std::string_view what) {
    gemui::Alert::Spec spec;
    spec.lines = {{std::string(what) + ":"}, {"not available in this version yet."}};
    gemui::Alert::open(desk_, std::move(spec));
}

// ------------------------------------------------------------------------------------------ floating windows

// Services the floating windows share through the main window: state read back, undo steps that are not patterns or
// the Song, and the places of the windows in the editor session.

void MainWindow::pullExtras() {
    extrasVersion_ = host_.extrasVersion();
    extras_ = host_.extras();
    refresh();
}

void MainWindow::syncWindowButtons() {
    // syncWinBtns: the buttons open the window and bring it back to the front; they show no state.
    for (Button& b : winBtns_) b.setOn(false);
}

void MainWindow::openSong() { song_.show(); }

void MainWindow::openScope(int track) {
    // openScope: already open, to the front; otherwise one more window, offset by 24 px per open scope.
    if (track < 0 || track >= plugin::kTracks) return;
    auto& w = scopes_[size_t(track)];
    if (w && w->isOpen()) {
        w->open();
        return;
    }
    int n = 0;
    for (const auto& o : scopes_) n += o && o->isOpen();
    if (!w) {
        w = std::make_unique<ScopeWindow>(track);
        add(*w);
    }
    w->setName(scopeName(track));
    w->openAt(n);
}
void MainWindow::openCurve() { curveWin_.show(); }

void MainWindow::songStep(int dir) {
    // songStep: previous or next entry, only in Song mode with a written Song; this mode is switched on from the Song
    // window.
    if (!extras_.global.songMode || extras_.seq.song.empty()) return;
    const auto& entries = extras_.seq.song.entries;
    const int n = int(entries.size());
    // Jumps are not played: the arrow skips them.
    int i = song_.songPos_;
    for (int k = 0; k < n; ++k) {
        i = ((i + dir) % n + n) % n;  // wraps around at both ends
        if (entries[size_t(i)].kind != ymseq::SongKind::Loop) break;
    }
    song_.jump(i);
}

void MainWindow::previewNote(int note) {
    // previewNote: the previous note cut, a note of 240 ms at most, velocity 110.
    if (note < 0 || note > 127) return;
    if (previewN_ >= 0) host_.screenMidi(0x80, uint8_t(previewN_), 0);
    host_.screenMidi(0x90, uint8_t(note), 110);
    previewN_ = note;
    // The note off comes from a desktop timer, run by tick(): "at most" 240 ms, as a new preview cuts it first.
    desk_.cancel(previewTimer_);
    previewTimer_ = desk_.after(240, [this] {
        previewTimer_ = 0;
        if (previewN_ >= 0) host_.screenMidi(0x80, uint8_t(previewN_), 0);
        previewN_ = -1;
    });
}

// Undo step of the same kind and target as `e` (track, sample slot, number of patterns...), holding the state as it
// is now. Used to keep the state before an edit, by Undo and Redo to keep the state they are about to replace (the
// step of the other direction, SeqWindow::entryLike), and by pushCur to see whether anything changed.
UndoEntry MainWindow::undoEntryLike(const UndoEntry& e) const {
    UndoEntry u;
    u.kind = e.kind;
    // The parameters go into `params` in a fixed order, which applyUndoEntry rebuilds to know which parameter each
    // value belongs to.
    if (e.kind == UndoEntry::Kind::Track) {
        u.index = e.index;
        for (int f = 0; f < plugin::kTrackParams; ++f)
            if (chanField(Track(f))) u.params.push_back(values_[size_t(paramIndex(e.index, Track(f)))]);
        u.arp = extras_.tracks[size_t(e.index)].arp;
        u.orn = TrackOrnament::of(extras_.tracks[size_t(e.index)].patch);
    } else if (e.kind == UndoEntry::Kind::Mix) {
        for (int t = 0; t < plugin::kTracks; ++t)
            for (Track f : kMixFields) u.params.push_back(values_[size_t(paramIndex(t, f))]);
    } else if (e.kind == UndoEntry::Kind::Kit) {
        u.kit = extras_.kit;
    } else if (e.kind == UndoEntry::Kind::Dma) {
        u.index = e.index;
        u.dma = extras_.dma;
        u.dmaNames = extras_.dmaNames;
        if (e.index >= 0 && e.index < ymengine::kDmaSlots) u.dmaBytes = extras_.dmaSamples[size_t(e.index)];
    } else if (e.kind == UndoEntry::Kind::Digi) {
        const int k = std::clamp(e.index, 0, ymengine::kDigidrumSlots - 1);
        u.index = k;
        u.digiBytes = e.digiBytes;
        u.digi = extras_.digiSlots[size_t(k)];
        if (e.digiBytes) u.digiNibbles = extras_.digidrums[size_t(k)];
        else u.digi.source.clear();  // settings only: the source stays the current one
    } else if (e.kind == UndoEntry::Kind::Grid) {
        // Resolution change that fitted the notes: every pattern, the Song, the tracks' own lengths and the
        // rate.
        const ymseq::Project& p = extras_.seq;
        u.index = extras_.global.resolution;
        u.timing = extras_.global.timing;
        u.patterns.assign(p.patterns.begin(), p.patterns.end());
        u.used = p.used;
        u.song = p.song;
        for (int t = 0; t < ymseq::kTracks; ++t) u.lengths[size_t(t)] = p.tracks[size_t(t)].length;
    } else if (e.kind == UndoEntry::Kind::Import) {
        // MIDI import: patterns replaced, Song, current pattern, tracks (On, Generator) and free tempo; XM import: also
        // instruments, DMA slots, machine, VBL rate and sequencer rate.
        u.index = std::clamp(e.index, 0, ymseq::kPatterns);
        u.patterns.assign(extras_.seq.patterns.begin(), extras_.seq.patterns.begin() + u.index);
        u.used = extras_.seq.used;
        u.current = extras_.seq.current;
        u.song = extras_.seq.song;
        for (int t = 0; t < plugin::kTracks; ++t) {
            u.params.push_back(values_[size_t(paramIndex(t, Track::On))]);
            u.params.push_back(values_[size_t(paramIndex(t, Track::Generator))]);
        }
        u.params.push_back(values_[size_t(paramIndex(Global::FreeTempo))]);
        if (e.xm) {
            u.xm = true;
            u.slots = std::clamp(e.slots, 0, ymengine::kDmaSlots);
            u.instruments.assign(extras_.instruments.begin(), extras_.instruments.begin() + u.slots);
            u.dma = extras_.dma;
            u.dmaSamples.assign(extras_.dmaSamples.begin(), extras_.dmaSamples.begin() + u.slots);
            u.dmaNames = extras_.dmaNames;
            u.timing = extras_.global.timing;
            u.speed = extras_.global.speed;
            u.params.push_back(values_[size_t(paramIndex(Global::Computer))]);
            u.params.push_back(values_[size_t(paramIndex(Global::VblRate))]);
        }
    }
    return u;
}

void MainWindow::applyUndoEntry(const UndoEntry& e) {
    // applyUndoEntry (chan, mix, kit): each parameter comes back through a host gesture, the arpeggio, the Modulation
    // table and the kit through the state.
    if (e.kind == UndoEntry::Kind::Kit) {
        const ymengine::KitParams kit = e.kit;
        host_.editExtras([&kit](plugin::NonParamState& s) { s.kit = kit; });
        pullExtras();
        return;
    }
    if (e.kind == UndoEntry::Kind::Dma) {
        const UndoEntry d = e;
        host_.editExtras([&d](plugin::NonParamState& s) {
            s.dma = d.dma;
            s.dmaNames = d.dmaNames;
            if (d.index >= 0 && d.index < ymengine::kDmaSlots) s.dmaSamples[size_t(d.index)] = d.dmaBytes;
        });
        pullExtras();
        return;
    }
    if (e.kind == UndoEntry::Kind::Digi) {
        // Settings only: the source and the played values stay; otherwise the whole slot comes back.
        const UndoEntry d = e;
        host_.editExtras([&d](plugin::NonParamState& s) {
            const size_t k = size_t(std::clamp(d.index, 0, ymengine::kDigidrumSlots - 1));
            plugin::DigidrumSlotState slot = d.digi;
            if (d.digiBytes) {
                s.digidrums[k] = d.digiNibbles;
            } else {
                // A load (not undoable) may have changed the sample since: loop brought back within its length.
                slot.source = s.digiSlots[k].source;
                const auto len = uint32_t(s.digidrums[k].size());
                slot.play.end = std::min(slot.play.end, len);
                slot.play.start = std::min(slot.play.start, slot.play.end);
            }
            s.digiSlots[k] = std::move(slot);
        });
        pullExtras();
        return;
    }
    if (e.kind == UndoEntry::Kind::Grid) {
        const UndoEntry d = e;
        host_.editExtras([&d](plugin::NonParamState& s) {
            for (size_t k = 0; k < d.patterns.size() && k < size_t(ymseq::kPatterns); ++k)
                s.seq.patterns[k] = d.patterns[k];
            s.seq.used = d.used;
            s.seq.song = d.song;
            for (int t = 0; t < ymseq::kTracks; ++t) s.seq.tracks[size_t(t)].length = d.lengths[size_t(t)];
            s.global.resolution = uint8_t(std::clamp(d.index, 0, ymseq::kResolutionCount - 1));
            s.global.timing = d.timing;
        });
        pullExtras();
        return;
    }
    // Parameter steps: the indices in the order undoEntryLike stored the values, then one host gesture per value that
    // differs.
    std::vector<int> indices;
    if (e.kind == UndoEntry::Kind::Import) {
        // The previous current pattern comes back like a pattern choice (while playing, at the end of the loop).
        const UndoEntry d = e;
        const bool play = seq_.playing();
        bool changed = false;
        host_.editExtras([&d, play, &changed](plugin::NonParamState& s) {
            for (size_t k = 0; k < d.patterns.size(); ++k) s.seq.patterns[k] = d.patterns[k];
            s.seq.used = d.used;
            s.seq.song = d.song;
            s.seq.viewBank = d.current / ymseq::kBankSize;
            changed = s.seq.select(d.current, play);
            if (!d.xm) return;
            for (size_t k = 0; k < d.instruments.size(); ++k) s.instruments[k] = d.instruments[k];
            s.dma = d.dma;
            for (size_t k = 0; k < d.dmaSamples.size(); ++k) s.dmaSamples[k] = d.dmaSamples[k];
            s.dmaNames = d.dmaNames;
            s.global.timing = d.timing;
            s.global.speed = d.speed;
        });
        if (changed) host_.seqCommand(Host::SeqCommand::Select, e.current);
        for (int t = 0; t < plugin::kTracks; ++t) {
            indices.push_back(paramIndex(t, Track::On));
            indices.push_back(paramIndex(t, Track::Generator));
        }
        indices.push_back(paramIndex(Global::FreeTempo));
        if (e.xm) {
            indices.push_back(paramIndex(Global::Computer));
            indices.push_back(paramIndex(Global::VblRate));
        }
    } else if (e.kind == UndoEntry::Kind::Track) {
        for (int f = 0; f < plugin::kTrackParams; ++f)
            if (chanField(Track(f))) indices.push_back(paramIndex(e.index, Track(f)));
    } else if (e.kind == UndoEntry::Kind::Mix) {
        for (int t = 0; t < plugin::kTracks; ++t)
            for (Track f : kMixFields) indices.push_back(paramIndex(t, f));
    } else {
        return;
    }
    for (size_t i = 0; i < indices.size() && i < e.params.size(); ++i) {
        const int idx = indices[i];
        if (host_.param(idx) == e.params[i]) continue;
        host_.beginGesture(idx);
        host_.setParam(idx, e.params[i]);
        host_.endGesture(idx);
        values_[size_t(idx)] = host_.param(idx);
    }
    if (e.kind == UndoEntry::Kind::Track) {
        const int t = e.index;
        const ymengine::TrackArp arp = e.arp;
        const TrackOrnament orn = e.orn;
        host_.editExtras([t, &arp, &orn](plugin::NonParamState& s) {
            ymengine::TrackArp& a = s.tracks[size_t(t)].arp;
            a.track = arp.track;
            a.land = arp.land;
            a.mode = arp.mode;
            a.octaves = arp.octaves;
            a.table = arp.table;
            a.length = arp.length;
            orn.applyTo(s.tracks[size_t(t)].patch);
        });
    }
    pullExtras();
}

// Keeps the focused track's state as it is before the edit that may follow (curSnap_).
void MainWindow::armCur() {
    UndoEntry e;
    e.kind = UndoEntry::Kind::Track;
    e.index = cur_;
    curSnap_ = undoEntryLike(e);
}

void MainWindow::pushCur() {
    // One undo step per gesture: the first setting that changes the track pushes the previous state; the next ones of
    // the same gesture (held slider, menu that sets several parameters) find nothing left to push. The undo step of
    // another track (tab changed since) is forgotten.
    if (!curSnap_) return;
    if (curSnap_->index != cur_) {
        curSnap_.reset();
        return;
    }
    if (undoEntryLike(*curSnap_) == *curSnap_) return;  // nothing changed: the undo step stays ready
    host_.session().undo.push(std::move(*curSnap_));
    curSnap_.reset();
}

void MainWindow::pressed(gemui::Widget* w) {
    // pointerdown listener of the prototype: a press in the main window or the Arpeggiator (and the Modulation and
    // Effects windows of STYM2149) prepares an undo step of the track; a press elsewhere on the desktop (other windows,
    // background) forgets it; the menus and the dialogue boxes keep the one of the control that opened them.
    for (gemui::Widget* p = w; p; p = p->parent()) {
        if (p == &frame_ || p == &arp_ || p == &orn_ || p == &echo_ || p == &stepWin_) {
            armCur();
            return;
        }
        if (p == this) {
            curSnap_.reset();
            return;
        }
    }
    if (!w && !desk_.hasPopups() && !desk_.hasModal()) curSnap_.reset();
}

void MainWindow::saveWindows() {
    // Places, state and order of the floating windows, kept by the plugin as long as it lives (Session.h).
    EditorSession& s = host_.session();
    int order = 0;
    const std::pair<const gemui::Widget*, const char*> fixed[] = {
        {&seq_, "seq"}, {&song_, "song"}, {&mix_, "mix"}, {&arp_, "arp"}, {&orn_, "orn"},
        {&kit_, "kit"}, {&lib_, "lib"}, {&ins_, "ins"},
        {&dma_, "dma"}, {&digi_, "digi"}, {&recWin_, "rec"}, {&ymWin_, "ym"}, {&curveWin_, "curve"},
        {&kbdWin_, "kbd"}, {&echo_, "echo"}, {&settings_, "settings"}, {&help_, "help"}};
    // The children are in stacking order (the last one in front): numbering them in that order gives the rank that
    // restoreWindows sorts on.
    for (gemui::Widget* w : children()) {
        std::string name;
        for (const auto& [f, n] : fixed)
            if (w == f) name = n;
        for (int i = 0; i < plugin::kTracks; ++i)
            if (w == scopes_[size_t(i)].get()) name = "scope" + std::to_string(i);
        if (name.empty()) continue;
        WindowPlace& p = s.windows[name];
        p.at = {w->bounds().x, w->bounds().y};
        p.open = w->visible();
        p.order = ++order;
    }
    WindowPlace& main = s.windows["main"];
    main.at = mainPlace();
    main.open = true;
}

// Editor reopened: every window known to the session goes back to its place, then the open ones are shown from the
// lowest rank up, so that the last one shown, the one that was in front, is in front again. The windows' own show()
// is used where it has one, as it also refreshes the window (and the YM Player shows only with a tune loaded).
void MainWindow::restoreWindows() {
    EditorSession& s = host_.session();
    if (const auto it = s.windows.find("main"); it != s.windows.end()) moveMainTo(it->second.at.x, it->second.at.y);
    std::vector<std::pair<int, gemui::FloatWindow*>> open;
    auto restore = [&](const char* name, gemui::FloatWindow& f) {
        const auto it = s.windows.find(name);
        if (it == s.windows.end()) return;
        f.moveBy(it->second.at.x - f.bounds().x, it->second.at.y - f.bounds().y);
        if (it->second.open) open.emplace_back(it->second.order, &f);
    };
    restore("seq", seq_);
    restore("song", song_);
    restore("mix", mix_);
    restore("arp", arp_);
    restore("orn", orn_);
    restore("kit", kit_);
    restore("lib", lib_);
    restore("ins", ins_);
    restore("dma", dma_);
    restore("digi", digi_);
    restore("rec", recWin_);
    restore("ym", ymWin_);
    restore("curve", curveWin_);
    restore("kbd", kbdWin_);
    restore("echo", echo_);
    restore("settings", settings_);
    restore("help", help_);
    // Scopes: only those that were open (a closed window is forgotten, closeScope).
    for (int i = 0; i < plugin::kTracks; ++i) {
        const auto it = s.windows.find("scope" + std::to_string(i));
        if (it == s.windows.end() || !it->second.open) continue;
        auto& w = scopes_[size_t(i)];
        w = std::make_unique<ScopeWindow>(i);
        add(*w);
        w->setName(scopeName(i));
        // Opened at its default place then closed: restore() then moves it to the place kept, and the loop below opens
        // it again in its rank.
        w->openAt(0);
        w->close();
        restore(("scope" + std::to_string(i)).c_str(), *w);
    }
    std::sort(open.begin(), open.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [order, f] : open) {
        if (f == &seq_) seq_.show();
        else if (f == &song_) song_.show();
        else if (f == &mix_) mix_.show();
        else if (f == &arp_) arp_.show();
        else if (f == &orn_) orn_.show();
        else if (f == &kit_) kit_.show();
        else if (f == &lib_) lib_.show();
        else if (f == &ins_) ins_.show();
        else if (f == &dma_) dma_.show();
        else if (f == &digi_) digi_.show();
        else if (f == &recWin_) recWin_.show();
        else if (f == &ymWin_) ymWin_.show();  // only if a tune is still loaded
        else if (f == &curveWin_) curveWin_.show();
        else if (f == &kbdWin_) kbdWin_.show();
        else if (f == &echo_) echo_.show();
        else if (f == &settings_) settings_.show();
        else f->open();
    }
}

// ------------------------------------------------------------------------------------------ new project

void MainWindow::newProject() {
    // Anything a new project would not have (a setting, a pattern, the Song, an instrument, a sample, the recorder's
    // take, a tune in the YM Player): confirm before losing it. The focused track is not a setting; a parameter that
    // went through the host's normalised value can be off by a rounding error.
    const plugin::ParamValues defaults = plugin::defaultValues();
    bool used = false;
    for (int i = 0; i < plugin::kParamCount && !used; ++i) {
        const plugin::ParamSpec& spec = plugin::paramSpec(i);
        used = i != plugin::paramIndex(Global::FocusTrack) &&
               std::abs(host_.param(i) - defaults[size_t(i)]) > 1e-4f * (spec.max - spec.min);
    }
    // The non-automatable states are compared by their encoded form (State.h), after sanitize() on both: every field
    // that the plugin state saves counts.
    plugin::PluginState now, blank;  // same parameter values: the non-automatable states compared
    now.extras = host_.extras();
    blank.extras = host_.newExtras();
    plugin::sanitize(now.extras);
    plugin::sanitize(blank.extras);
    const Host::Recorder rec = host_.recorder();
    used = used || plugin::encodeState(now) != plugin::encodeState(blank) || rec.takeFrames > 0 ||
           rec.state != Host::Recorder::State::Idle || host_.ymPlayer().loaded;
    if (!used) {
        doNew();
        return;
    }
    gemui::Alert::Spec spec;
    spec.lines = {{"New project?"}, {"Unsaved work will be lost."}};  // like Quit
    spec.confirmButtons();  // the safest choice
    gemui::Alert::open(desk_, std::move(spec), [this](int button, const std::string&) {
        if (button == gemui::Alert::kConfirmOk) doNew();
    });
}

void MainWindow::doNew() {
    // Like File > Load > Preset with the defaults; the recorder's take, the YM Player's tune, the undo history and the
    // files that Save rewrites go too. The preferences (Patches library, Digidrum banks, MIDI map, window size...) stay.
    // The sequencer stops and Rec is disarmed (sixth DAW test, item 51); in Host sync the host plays it.
    panic();
    if (seqClock() != ymengine::SeqClock::Host) host_.seqCommand(Host::SeqCommand::Stop);
    host_.session().record = false;
    if (host_.recorder().state != Host::Recorder::State::Idle) host_.disarmRecorder();
    host_.clearRecording();
    if (host_.ymPlayer().loaded) host_.ymClose();
    host_.loadProject(plugin::defaultValues(), host_.newExtras());
    EditorSession& s = host_.session();
    s.undo.clear();
    s.files = {};
    for (int i = 0; i < plugin::kParamCount; ++i) values_[size_t(i)] = host_.param(i);
    pullExtras();
}

// ----------------------------------------------------------------------------------------- demo pattern

void MainWindow::demoPattern() {
    // Tracks 1 to 4 of the current pattern already written (notes or playing rows): confirm before overwriting.
    bool used = false;
    const ymseq::Pattern& pat = extras_.seq.cur();
    for (int t = 0; t < 4 && !used; ++t) {
        const ymseq::Lane& l = pat.lane(t);
        for (int s = 0; s < ymseq::kSteps && !used; ++s) {
            used = l.pat[size_t(s)] >= 0;
            for (ymseq::Row r : {ymseq::Row::Tie, ymseq::Row::Slide, ymseq::Row::CmdArpeggiator, ymseq::Row::Mix,
                                 ymseq::Row::Fx, ymseq::Row::Slt, ymseq::Row::Cut, ymseq::Row::Oct, ymseq::Row::Gat})
                used = used || l.get(r, s) != 0;
        }
    }
    if (!used) {
        doDemo();
        return;
    }
    gemui::Alert::Spec spec;
    spec.lines = {{"Demo pattern"}, {""}, {"Channels 1-4 already contain data."}, {"Overwrite them?"}};
    spec.confirmButtons();  // the safest choice
    gemui::Alert::open(desk_, std::move(spec), [this](int button, const std::string&) {
        if (button == gemui::Alert::kConfirmOk) doDemo();
    });
}

void MainWindow::doDemo() {
    // doDemo: arpeggiated bass (track 1), SID lead (2), Chipdrum beat (3, voice C in 3-channel mode), Sync Buzzer stabs
    // (4, in 16-channel mode only) in the current pattern; octaves 4, 2, 4, 3, common length, levels 100, 80, 100, 50.
    // Each demo track: its sound (parameters) and its rows over the first 16 steps, in the units of the pattern rows
    // (Pattern.h): Pat in semitones above the track's octave or Chipdrum instrument 0-7 (-1: nothing), Vol 0-15, Gat
    // in %, Pnr = pan × 10 + 101 (0: the track's pan), the others 0 / 1 or a small index.
    using ymseq::Row;
    struct Rows {
        Row row;
        std::array<int, 16> v;
    };
    struct Demo {
        int track;
        std::vector<std::pair<Track, float>> patch;
        std::vector<Rows> rows;
    };
    const bool m3 = mode3();
    std::vector<Demo> demo = {
        {0,
         {{Track::Generator, float(Generator::Chip)}, {Track::Tone, 1}, {Track::Noise, 0}, {Track::Volume, 13}},
         {{Row::Pat, {0, -1, 12, 7, 0, -1, 12, 7, 3, 3, 12, 8, 5, 3, 2, 0}},
          {Row::Tie, {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0}},
          {Row::Slide, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0}},
          {Row::Afl, {1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
          {Row::Vol, {15, 11, 13, 11, 15, 11, 13, 11, 15, 12, 13, 12, 15, 13, 12, 11}},
          {Row::Mix, {0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 2, 0, 0, 0, 0, 0}},
          {Row::Fx, {0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 2, 0, 0, 0}},
          {Row::Gat, {0, 0, 20, 30, 0, 0, 20, 30, 0, 0, 20, 90, 0, 0, 0, 40}}}},
        {1,
         {{Track::Generator, float(Generator::Sid)}, {Track::SidRatio, 1}, {Track::SidVmin, 0}, {Track::Tone, 1},
          {Track::Volume, 12}},
         {{Row::Pat, {-1, -1, 12, -1, -1, -1, 7, -1, -1, -1, 10, -1, -1, -1, 12, -1}},
          {Row::Tie, {0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1}},
          {Row::Vol, {15, 15, 13, 13, 15, 15, 12, 12, 15, 15, 13, 13, 15, 15, 14, 14}},
          {Row::Fx, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0}},
          {Row::Pnr, {0, 0, 41, 41, 0, 0, 161, 161, 0, 0, 41, 41, 0, 0, 161, 161}}}},
        {2,
         {{Track::Generator, float(Generator::Chipdrum)}, {Track::Volume, 15}},
         {{Row::Pat, {0, -1, 2, -1, 1, 0, 2, -1, 0, -1, 2, 0, 1, 0, 2, 2}},
          {Row::Vol, {15, 15, 9, 15, 13, 14, 9, 15, 15, 15, 9, 14, 13, 14, 9, 11}},
          {Row::Pnr, {0, 0, 141, 0, 71, 0, 141, 0, 0, 0, 141, 0, 71, 0, 141, 141}}}},
    };
    if (!m3)  // EP 1 of the prototype: 32 as a chip value
        demo.push_back({3,
                        {{Track::Generator, float(Generator::SyncBuzzer)}, {Track::EnvPeriod, 32},
                         {Track::EnvShape, 8}, {Track::Volume, 14}, {Track::Tone, 0}},
                        {{Row::Pat, {-1, -1, -1, -1, 0, -1, -1, -1, -1, -1, -1, -1, 5, -1, -1, -1}},
                         {Row::Tie, {0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0}},
                         {Row::Vol, {15, 15, 15, 15, 14, 14, 15, 15, 15, 15, 15, 15, 14, 14, 15, 15}},
                         {Row::Pnr, {0, 0, 0, 0, 181, 181, 0, 0, 0, 0, 0, 0, 21, 21, 0, 0}}}});

    seq_.pushUndo();  // a pattern undo step: Undo brings back the rows of the pattern, not the settings below
    // Parameters: timbre, active track, mixer level, arpeggio of the bass.
    std::vector<std::pair<int, float>> params;
    for (const Demo& d : demo) {
        for (const auto& [f, v] : d.patch) params.emplace_back(paramIndex(d.track, f), v);
        params.emplace_back(paramIndex(d.track, Track::On), 1.0f);
    }
    const std::array<float, 4> levels = {100, 80, 100, m3 ? 100.0f : 50.0f};
    for (int t = 0; t < 4; ++t) params.emplace_back(paramIndex(t, Track::Level), levels[size_t(t)]);
    params.emplace_back(paramIndex(0, Track::ArpOn), 1.0f);
    // 3 channels: track 4 plays the Digidrum; its generator, restored on the return to 16 channels, becomes
    // Sync Buzzer (ch3Saved[0].fx = 3 of the prototype).
    if (m3) params.emplace_back(paramIndex(3, Track::Generator), float(Generator::SyncBuzzer));
    // Written to the host directly, not through set(): no undo step of the track is taken, and the screen is refreshed
    // once, at the end (pullExtras).
    for (const auto& [idx, v] : params) {
        host_.beginGesture(idx);
        host_.setParam(idx, v);
        host_.endGesture(idx);
    }
    // State: rows of the tracks (cleared then written over 16 steps), octaves and lengths, arpeggio table.
    // Length 0: the track follows the length of the pattern; the arpeggio table plays 0, +3 and +7 semitones (a minor
    // chord).
    host_.editExtras([&demo](plugin::NonParamState& s) {
        static constexpr std::array<int, 4> kOct = {4, 2, 4, 3};
        for (int t = 0; t < 4; ++t) {
            s.seq.tracks[size_t(t)].octave = kOct[size_t(t)];
            s.seq.tracks[size_t(t)].length = 0;
        }
        ymengine::TrackArp& a = s.tracks[0].arp;
        a.mode = ymengine::ArpMode::Table;
        a.table.fill(0);
        a.table[1] = 3;
        a.table[2] = 7;
        a.length = 3;
        ymseq::Pattern& p = s.seq.cur();
        for (const Demo& d : demo) {
            ymseq::Lane& l = p.edit(d.track);
            for (int st = 0; st < ymseq::kSteps; ++st)
                for (Row r : {Row::Pat, Row::Tie, Row::Slide, Row::CmdArpeggiator, Row::Vol, Row::Mix, Row::Fx,
                              Row::Slt, Row::Cut, Row::Oct, Row::Gat, Row::Pnr})
                    l.set(r, st, ymseq::Lane::emptyValue(r));
            for (const Rows& r : d.rows)
                for (int st = 0; st < 16; ++st) l.set(r.row, st, r.v[size_t(st)]);
            ymseq::migrateArpFlags(l);  // the ARP steps of the prototype: J01 and J00 of the ARP row (item 144)
        }
        p.compact();
    });
    for (int i = 0; i < plugin::kParamCount; ++i) values_[size_t(i)] = host_.param(i);
    if (cur_ != 0) set(Global::FocusTrack, 1.0f);  // focusCh(0)
    pullExtras();
}

// The main window takes its turn with the floating windows; with a single window open, the key goes to the host.
bool MainWindow::cycleWindows(bool back) {
    desk_.closePopups();
    std::vector<gemui::Widget*> open;  // back to front
    for (gemui::Widget* c : children())
        if (c->visible() && (c == &frame_ || dynamic_cast<gemui::FloatWindow*>(c))) open.push_back(c);
    if (open.size() < 2) return false;
    if (back)
        for (size_t i = 0; i + 1 < open.size(); ++i) open[i]->toFront();
    else
        open.front()->toFront();
    return true;
}

void MainWindow::requestQuit() {
    if (host_.standalone() && !quitAsked_) confirmQuit();
}

void MainWindow::confirmQuit() {
    gemui::Alert::Spec spec;
    spec.lines = {{"Quit STYM2149?"}, {"Unsaved work will be lost."}};
    spec.confirmButtons();  // the safest choice
    quitAsked_ = true;
    gemui::Alert::open(desk_, std::move(spec), [this](int button, const std::string&) {
        quitAsked_ = false;
        if (button != gemui::Alert::kConfirmOk) return;
        panic();
        host_.quit();
    });
}

void MainWindow::showInfo() {
    gemui::Alert::Spec spec;
    spec.lines = {{"STYM2149"},
                  {""},
                  {"Programming: Pascal \"Indus\" Nowak (with Claude's help)"},
                  {"YM2149F emulator - GEM edition"},
                  {"Version: " + host_.version()},
                  {"Copyright (c) 2026 Indus / PRISM"},
                  {""},
                  // Notes in the 8×8 font: lines of 55 characters at most, the width of the alert.
                  {"Free software under the GNU Affero General Public", 12, colors::kHint},
                  {"License v3: you may share and modify it under its", 12, colors::kHint},
                  {"terms (see LICENSE).", 12, colors::kHint},
                  {""},
                  // Credits required by THIRD_PARTY.md.
                  {"Emulation after AtariAudio by Leonard/Oxygene", 12, colors::kHint},
                  {"(Arnaud Carr\u00e9) and ym2149-rs by slippyex (MIT).", 12, colors::kHint},
                  {"SNDH played by AtariAudio's machine and the Musashi", 12, colors::kHint},
                  {"68000 emulator by Karl Stenerud (MIT); ICE unpacking", 12, colors::kHint},
                  {"after Hans Wessels (public domain).", 12, colors::kHint},
                  {"LHA decompression after pylzh by Hirofumi Ukawa", 12, colors::kHint},
                  {"(Apache-2.0). Fonts and icons from EmuTOS (GPL).", 12, colors::kHint},
                  {"Built with JUCE (AGPLv3). Licences: see the", 12, colors::kHint},
                  {"licenses folder and THIRD_PARTY.md.", 12, colors::kHint}};
    // The application icon above the text, each pixel doubled; OK is the default button, Enter closes the
    // box, so it gets the bold border of GEM's default button (the prototype's #infoOk had none).
    spec.picture = icons::kApp.px;
    spec.pictureW = icons::kApp.w;
    spec.pictureH = icons::kApp.h;
    spec.pictureScale = 2;
    spec.maxWidth = 0;
    gemui::Alert::open(desk_, std::move(spec));
}

std::string MainWindow::clockMHz(uint32_t hz) {
    const uint32_t units = (hz + 50) / 100;  // 100 Hz steps
    // 10 000 steps of 100 Hz per MHz: whole MHz, then the four decimals.
    char buf[24];
    std::snprintf(buf, sizeof buf, "%u.%04u", unsigned(units / 10000), unsigned(units % 10000));
    return buf;
}

std::vector<MenuItem> MainWindow::paramItems(Global g, int stride, int& checked) {
    const plugin::ParamSpec& s = plugin::paramSpec(paramIndex(g));
    const int now = iv(g);
    std::vector<int> vals;
    if (s.kind == plugin::ParamKind::Choice) {
        for (int i = 0; i < int(s.choices.size()); ++i) vals.push_back(i);
    } else {
        const int step = std::max(1, int(s.step)) * stride;
        for (int v = int(s.min); v <= int(s.max); v += step) vals.push_back(v);
    }
    std::vector<MenuItem> out;
    checked = -1;
    for (size_t i = 0; i < vals.size(); ++i) {
        const int v = vals[i];
        MenuItem e;
        if (s.kind == plugin::ParamKind::Choice) e.label = std::string(s.choices[size_t(v)]);
        else if (s.kind == plugin::ParamKind::Bool) e.label = v ? "On" : "Off";
        else e.label = (v > 0 && s.min < 0 ? "+" : "") + std::to_string(v) +
                       (s.unit.empty() ? "" : " " + std::string(s.unit));
        e.pick = [this, g, v] { set(g, float(v)); };
        if (v == now) checked = int(i);
        out.push_back(std::move(e));
    }
    return out;
}

void MainWindow::customClock() {
    if (mode3()) return;  // 16 ch only
    gemui::Alert::Spec spec;
    // In MHz, ##.#### (fifth DAW test): 100 Hz steps, the parameter stays in Hz.
    const std::string range = "(" + clockMHz(plugin::kCustomClockMinHz) + " to " +
                              clockMHz(plugin::kCustomClockMaxHz) + "):";
    spec.lines = {{"Custom chip clock in MHz"}, {range}};
    spec.promptButtons();
    spec.inputText = clockMHz(plugin::customClockHz(values_));
    spec.inputChars = 10;
    spec.inputMax = 7;
    gemui::Alert::open(desk_, std::move(spec), [this](int button, const std::string& text) {
        if (button != gemui::Alert::kPromptOk) return;
        // Nothing typed: nothing changes. Out of range: clamped.
        const auto typed = parseMHz(text);
        if (!typed) return;
        const long long hz = std::clamp<long long>(*typed, plugin::kCustomClockMinHz, plugin::kCustomClockMaxHz);
        set(Global::ChipClockHz, float(hz));
        set(Global::ChipClock, 0);
    });
}

void MainWindow::setBaseOctave(int oct, bool retune) {
    // buildKbd, buildTkl, retuneHeld.
    // The Computer Keyboard reaches octaves 0 to 9; the piano shows four octaves from at most octave 6 (C6 to B9).
    baseOct_ = std::clamp(oct, 0, 9);
    kbd_.setBaseOctave(std::min(baseOct_, 6));
    if (retune) kbdWin_.retune();
    refresh();
}

void MainWindow::lightKey(int note, bool on) { kbd_.setHeld(note, on); }

// Box of a control or of a zone by name, for the interface tests, the visual comparison with the prototype's captures
// and the capture tool: mostly the prototype's element ids, plus names of zones that STYM2149 changed. The main
// window's names first, then each floating window's rectOf, then any control with that id (findById).
std::optional<Rect> MainWindow::rectOf(std::string_view id) const {
    struct Named {
        const char* id;
        const gemui::Widget* w;
    };
    const Named named[] = {{"srcSel", &srcSel_},   {"polySel", &polySel_}, {"vmSel", &vmSel_},
                           {"mainTitle", &title_},
                           {"shapeSel", &shapeSel_}, {"fxSel", &fxSel_},   {"adsrMode", &adsrMode_},
                           {"modeSel", &modeSel_}, {"clkSel", &clkSel_},   {"digiChSel", &digiChSel_},
                           {"digiEdit", &digiEdit_}, {"drumwave", &drumWave_}, {"digiHint", &digiHint_}, {"kbd", &kbd_},
                           {"pads", &pads_},       {"openMixBtn", &winBtns_[0]}, {"openArpBtn", &winBtns_[1]}, {"openOrnBtn", &winBtns_[2]},
                           {"openFxBtn", &winBtns_[3]}, {"openSeqBtn", &winBtns_[4]}, {"openLibBtn", &winBtns_[5]},
                           {"openKitBtn", &openKit_},
                           {"clkStatus", &clkStatus_}, {"compSel", &compSel_},
                           {"dmaEdit", &dmaEdit_}, {"genDmaWave", &dmaWave_}, {"dmaHint", &dmaHint_},
                           {"adsrFrame", &adsrFs_}, {"curveFrame", &curveFs_}, {"adsrHint", &adsrHint_},
                           {"envFrame", &envFs_}, {"vibFrame", &vibFs_}, {"vibOn", &vibOn_},
                           {"scope", &scope_},
                           {"genSep", &genSep_}, {"genTuned", &buzTuned_},
                           {"waveGrid", &sidWave_}, {"waveSteps", &waveSteps_}, {"waveForm", &waveForm_},
                           {"wavePeriod", &wavePeriod_}, {"waveWidth", &waveWidth_},
                           {"buzzSteps", &buzzLen_},
                           {"genN", &buzN_}, {"genD", &buzD_}, {"genFine", &buzFine_}, {"genPad", &buzPad_},
                           {"genPresets", &buzPresets_},
                           {"regs", &regs_},       {"kbdMapBtn", &kbdMap_}, {"octDown", &octDown_},
                           {"octUp", &octUp_},     {"allOn", &allOn_},     {"allOff", &allOff_}};
    for (const Named& n : named)
        if (id == n.id) return n.w->showing() ? std::optional<Rect>(n.w->bounds()) : std::nullopt;
    // Rows of the Sync Buzzer settings in the GENERATOR frame: "genBuzzer0" Tuning, 1 to 3 N, D and Fine, 4
    // YM Pad and Presets (its steps below, "buzz0" to "buzz7", "buzzSteps").
    if (id.size() == 10 && id.substr(0, 9) == "genBuzzer" && id[9] >= '0' && id[9] <= '4') {
        if (!buzTuned_.showing()) return std::nullopt;
        using Row = std::initializer_list<const gemui::Widget*>;
        const std::array<Row, 5> rows = {Row{&labBuzTune_, &buzTuned_}, Row{&labBuzN_, &buzN_, &buzNv_},
                                         Row{&labBuzD_, &buzD_, &buzDv_}, Row{&labBuzFine_, &buzFine_, &buzFinev_},
                                         Row{&buzPad_, &buzPresets_}};
        const Row& row = rows[size_t(id[9] - '0')];
        Rect r = (*row.begin())->bounds();
        for (const gemui::Widget* w : row) r = r.unite(w->bounds());
        return r;
    }
    // Tabs "tab0" to "tab15", DMA samples and Digidrum slots of the GENERATOR frame "genDma0" to "genDma7" and
    // "genSlot0" to "genSlot7", the frame itself "genFrame" (tests).
    auto number = [&](std::string_view prefix, int n) -> int {
        if (id.size() <= prefix.size() || id.substr(0, prefix.size()) != prefix) return -1;
        int k = 0;
        for (char c : id.substr(prefix.size())) {
            if (c < '0' || c > '9') return -1;
            k = k * 10 + (c - '0');
        }
        return k < n ? k : -1;
    };
    if (const int k = number("buzz", ymengine::kBuzzSteps); k >= 0)
        return buzzStep_[size_t(k)].showing() ? std::optional<Rect>(buzzStep_[size_t(k)].bounds()) : std::nullopt;
    if (const int k = number("tab", plugin::kTracks); k >= 0)
        return tabs_[size_t(k)].showing() ? std::optional<Rect>(tabs_[size_t(k)].bounds()) : std::nullopt;
    if (const int k = number("genDma", ymengine::kDmaSlots); k >= 0)
        return dmaSlots_[size_t(k)].showing() ? std::optional<Rect>(dmaSlots_[size_t(k)].bounds()) : std::nullopt;
    if (const int k = number("genSlot", ymengine::kDigidrumSlots); k >= 0)
        return slots_[size_t(k)].showing() ? std::optional<Rect>(slots_[size_t(k)].bounds()) : std::nullopt;
    if (id == "genFrame") return genFs_.bounds();
    // Computer row, across the inner width of the SYSTEM frame: "computerRow", the row and the gap that follows
    // it; "systemBelow", the rows moved down by as much, down to the bottom of the frame (comparison with the
    // captures).
    if (id == "computerRow" || id == "systemBelow") {
        const Rect f = sysFs_.bounds();
        const int top = compSel_.bounds().y, below = clkSel_.bounds().y, x = f.x + 3, w = f.w - 6;
        if (id == "computerRow") return Rect{x, top, w, below - top};
        return Rect{x, below, w, f.bottom() - 3 - below};
    }
    // Menu bar: the four titles then the two indicators, numbered in that order by the bar.
    static constexpr std::array<const char*, 6> kBar = {"mFuji", "mFile", "mEdit", "mOptions", "chipTxt", "miditxt"};
    for (size_t i = 0; i < kBar.size(); ++i)
        if (id == kBar[i]) return menu_.entryRect(int(i));
    if (auto r = seq_.rectOf(id)) return r;
    if (auto r = mix_.rectOf(id)) return r;
    if (auto r = arp_.rectOf(id)) return r;
    if (auto r = orn_.rectOf(id)) return r;
    if (auto r = kit_.rectOf(id)) return r;
    if (auto r = lib_.rectOf(id)) return r;
    if (auto r = ins_.rectOf(id)) return r;
    if (auto r = dma_.rectOf(id)) return r;
    if (auto r = recWin_.rectOf(id)) return r;
    if (auto r = ymWin_.rectOf(id)) return r;
    if (auto r = digi_.rectOf(id)) return r;
    if (auto r = curveWin_.rectOf(id)) return r;
    if (auto r = kbdWin_.rectOf(id)) return r;
    if (auto r = echo_.rectOf(id)) return r;
    if (auto r = settings_.rectOf(id)) return r;
    if (auto r = stepWin_.rectOf(id)) return r;
    if (auto r = help_.rectOf(id)) return r;
    if (id == "learnBadge") return badge_.visible() ? std::optional<Rect>(badge_.bounds()) : std::nullopt;
    // Menu of a control open under it, its frame on the control's bottom rule.
    if (id == "anchoredMenu") {
        const auto* m = dynamic_cast<const gemui::MenuList*>(desk_.topPopup());
        if (!m || !popupAnchor_ || m->bounds().y != popupAnchor_->bounds().bottom() - 2) return std::nullopt;
        return m->bounds();
    }
    // Shape R13 menu open, eight shapes where the prototype listed sixteen, and the place of the eight rows it
    // no longer has, under it.
    if (id == "shapeMenu" || id == "shapeMenuGone") {
        const auto* m = dynamic_cast<const gemui::MenuList*>(desk_.topPopup());
        if (!m || popupAnchor_ != &shapeSel_ || m->items().size() != 8) return std::nullopt;
        const Rect b = m->bounds();
        if (id == "shapeMenu") return b;
        // Eight more rows of the height of one (distance between the tops of rows 0 and 1).
        return Rect{b.x, b.bottom(), b.w, 8 * (m->rowsFrom(1).y - m->rowsFrom(0).y)};
    }
    // ADSR frames dimmed where the prototype left them as they are: ADSR of a drum track (the prototype dimmed
    // only that of a Sync Buzzer), ADSR ENVELOPE of either ("adsrInert", "curveInert", further down).
    // Window buttons, centred: their row.
    if (id == "winButtons") {
        const Rect first = winBtns_.front().bounds(), last = winBtns_.back().bounds();
        return Rect{first.x, first.y, last.right() - first.x, first.h};
    }
    // Rows whose sliders got the width of their neighbours' (ninth DAW test, items 103, 104 and 106),
    // from the label to the unit; the info cell of the register monitor (item 105).
    if (id == "volRow") return labVol_.bounds().unite(volv_.bounds());
    if (id == "levelRow") return labLevel_.bounds().unite(levelv_.bounds());
    if (id == "vibRows") return labDepth_.bounds().unite(rateUnit_.bounds());
    // Rows aligned on the sliders of Volume and Level (twelfth DAW test, items 159 and 160).
    if (id == "srcRow") return labSrc_.bounds().unite(srcSel_.bounds());
    if (id == "polyRow") return labPoly_.bounds().unite(polySel_.bounds());
    if (id == "vmRow") return labVm_.bounds().unite(vmSel_.bounds());
    if (id == "npRow") return np_.bounds().unite(npv_.bounds());
    // Voices and Clock rows, their buttons after the Computer label (thirteenth DAW test, item 161).
    if (id == "voicesRow") return labVoices_.bounds().unite(modeSel_.bounds());
    if (id == "clockRow") return labClock_.bounds().unite(clkSel_.bounds()).unite(clkStatus_.bounds());
    if (id == "adsrRows") return adsrLab_.front().bounds().unite(adsrUnit_.back().bounds());
    if (id == "noteRow")
        return labNote_.visible() ? std::optional<Rect>(labNote_.bounds().unite(tpread_.bounds())) : std::nullopt;
    if (id == "regInfo") return regs_.showing() ? std::optional<Rect>(regs_.infoRect()) : std::nullopt;
    if (id == "srcFrame") return srcFs_.bounds();
    if (id == "mainBottom" || id == "mainGone") {
        // Bottom margin and border of the window, kHelpLine px higher than in the prototype, and the place of
        // the prototype's help line that the desktop now shows.
        const Rect f = frame_.bounds();
        if (id == "mainBottom") return Rect{f.x, f.bottom() - 26, f.w, 26};
        return Rect{f.x, f.bottom(), f.w, kHelpLine};
    }
    if (id == "adsrInert")
        return adsrFs_.opacity() < 1 && drumTrack() ? std::optional<Rect>(adsrFs_.bounds()) : std::nullopt;
    if (id == "curveInert") return curveFs_.opacity() < 1 ? std::optional<Rect>(curveFs_.bounds()) : std::nullopt;
    if (id == "vibInert") return vibFs_.opacity() < 1 ? std::optional<Rect>(vibFs_.bounds()) : std::nullopt;
    if (id == "envInert") return envFs_.opacity() < 1 ? std::optional<Rect>(envFs_.bounds()) : std::nullopt;
    if (id == "menuExtra") {  // STYM2149 entries under those of the prototype, menu open
        const auto* m = dynamic_cast<const gemui::MenuList*>(desk_.topPopup());
        if (!m || menuExtra_ < 0) return std::nullopt;
        const Rect r = m->rowsFrom(menuExtra_);
        return r.w > 0 ? std::optional<Rect>(r) : std::nullopt;
    }
    if (id == "menuNew") {
        // New, STYM2149 entry above those of the prototype (File menu), in the open menu.
        const auto* m = dynamic_cast<const gemui::MenuList*>(desk_.firstPopup());
        if (!m || menuNew_ < 0) return std::nullopt;
        const Rect from = m->rowsFrom(menuNew_), below = m->rowsFrom(menuNew_ + 1);
        if (from.w <= 0 || below.w <= 0) return std::nullopt;
        return Rect{from.x, from.y, from.w, below.y - from.y};
    }
    if (id.substr(0, 5) == "scWin") {
        const std::string num(id.substr(5));
        const int i = num.empty() || num.size() > 2 ? -1 : std::atoi(num.c_str());
        if (i >= 0 && i < plugin::kTracks && scopes_[size_t(i)] && scopes_[size_t(i)]->isOpen())
            return scopes_[size_t(i)]->bounds();
        return std::nullopt;
    }
    if (auto r = song_.rectOf(id)) return r;
    // Other controls that MIDI learn can learn (sliders of the main window...).
    if (const gemui::Widget* w = findById(id)) return w->showing() ? std::optional<Rect>(w->bounds()) : std::nullopt;
    return std::nullopt;
}

// ------------------------------------------------------------------------------------------- MIDI learn

// MIDI learn works on the controls, not on the parameters: a binding links a MIDI key (a CC or a note,
// plugin::midiKey) to the prototype id of a slider or a button, and a message moves the slider or clicks the button as
// the mouse would. The map is a user preference ("midiMap"), shared by every instance. The host keeps the learned keys
// for the editor instead of the engine (pushCapture) and hands them back through Host::learnMidi, read at each tick.

void MainWindow::setMidiLearn(bool on) {
    // setLearn: the armed control is forgotten.
    learn_ = on;
    learnTarget_.reset();
    if (on) badge_.show("MIDI learn: click a slider or button, then move a control...", kHeight);
    else badge_.setVisible(false);
    pushCapture();
}

void MainWindow::clearMidiMap() {
    midiMap_.clear();
    ccLast_.clear();
    host_.setPreference("midiMap", plugin::dumpMidiMap(midiMap_));
    if (learn_) badge_.show("MIDI learn: map cleared", kHeight);
    pushCapture();
}

void MainWindow::pushCapture() { host_.setMidiCapture(plugin::midiKeys(midiMap_), learnTarget_.has_value()); }

bool MainWindow::learnPress(gemui::Widget* w) {
    // Capturing pointerdown listener of the prototype: in learn mode, a press on a slider or a button (the closest to
    // the point touched) that has an id arms it. The press stops there: the control does not act (the click of a button
    // still went through in the prototype).
    if (!learn_) return false;
    for (gemui::Widget* p = w; p; p = p->parent()) {
        const bool range = dynamic_cast<gemui::Slider*>(p) != nullptr;
        if (!range && !dynamic_cast<gemui::Button*>(p)) continue;
        if (p->id().empty()) return false;
        learnTarget_ = LearnTarget{p->id(), range};
        badge_.show("MIDI learn: [" + p->id() + "] armed - move a knob or press a pad", kHeight);
        pushCapture();
        return true;
    }
    return false;
}

void MainWindow::learnMessage(const std::array<uint8_t, 3>& m) {
    const auto key = plugin::midiKey(m);
    if (!key) return;
    const int st = m[0] & 0xF0;
    const bool noteOff = st == 0x80 || (st == 0x90 && m[2] == 0);
    // midiBind: the CC or the note played is learnt for the armed control.
    if (!noteOff && learn_ && learnTarget_) {
        plugin::bindMidi(midiMap_, *key, learnTarget_->id, learnTarget_->range);
        host_.setPreference("midiMap", plugin::dumpMidiMap(midiMap_));
        badge_.show("MIDI learn: " + *key + " -> [" + learnTarget_->id + "] - click another control or exit via Options",
                    kHeight);
        learnTarget_.reset();
        pushCapture();
        return;
    }
    bool known = false;
    if (noteOff) {
        // Release of a learnt pad: re-arms the rising edge (plays nothing).
        known = plugin::findMidi(midiMap_, *key) != nullptr;
        if (known) ccLast_[*key] = 0;
    } else {
        known = applyMidiMap(*key, st == 0xB0 ? m[2] : 127);
    }
    // Key taken by the host for a control disarmed since: it again receives only the learnt keys.
    if (!known) pushCapture();
}

bool MainWindow::applyMidiMap(const std::string& key, int value) {
    // applyMidiMap: slider at min + v / 127 × (max − min), button clicked on the rising edge (v ≥ 64).
    const plugin::MidiBinding* b = plugin::findMidi(midiMap_, key);
    if (!b) return false;
    gemui::Widget* w = findById(b->id);
    if (!w) return true;  // target missing
    const int last = ccLast_[key];
    ccLast_[key] = value;
    if (b->range) {
        if (auto* s = dynamic_cast<gemui::Slider*>(w)) s->input(s->min() + value / 127.0 * (s->max() - s->min()));
    } else if (auto* btn = dynamic_cast<gemui::Button*>(w)) {
        if (value >= 64 && last < 64) btn->click();
    }
    return true;
}

gemui::Widget* MainWindow::findById(std::string_view id) const {
    // First element of the page that carries this id ($() of the prototype): the main window, then the floating windows
    // in page order (the Song before the Step Sequencer: trPlay, trRec, trLoop).
    std::function<gemui::Widget*(const gemui::Widget&)> find = [&](const gemui::Widget& w) -> gemui::Widget* {
        if (w.id() == id) return const_cast<gemui::Widget*>(&w);
        for (gemui::Widget* c : w.children())
            if (gemui::Widget* f = find(*c)) return f;
        return nullptr;
    };
    if (id.empty()) return nullptr;
    for (const gemui::Widget* root : std::initializer_list<const gemui::Widget*>{
             &frame_, &song_, &seq_, &mix_, &arp_, &orn_, &kit_, &lib_, &curveWin_, &kbdWin_, &dma_, &digi_, &echo_})
        if (gemui::Widget* f = find(*root)) return f;
    return nullptr;
}

// ------------------------------------------------------------------------------------------ drawing, keyboard

void MainWindow::paint(gemui::Painter& p) { p.tile(bounds(), gemui::tiles::desktop(), {0, 0}); }

// Drawn after the children: the outline of a desktop icon being dragged stays visible over the windows.
void MainWindow::paintOver(gemui::Painter& p) {
    for (const gemui::DeskIcon& i : deskIcons_) i.paintDrag(p);
}

void MainWindow::buildDeskIcons() {
    // On an invisible grid of cells of the icons' size (thirteenth DAW test, item 167), its first row level
    // with the main window's top at its home place: A and B side by side in the first two cells, beside the main window,
    // the trash in the last cell at the bottom right (tenth DAW test, item 124), or where they were dragged.
    constexpr int kW = gemui::DeskIcon::kW, kH = gemui::DeskIcon::kH;
    static_assert(2 * kW <= kWinX, "A and B fit at the left of the main window");
    constexpr gemui::Point kGrid{0, kWinY};
    const std::array<gemui::Point, 3> home = {
        {{0, kWinY}, {kW, kWinY}, {(kWidth - kW) / kW * kW, kWinY + (kHeight - kH - kWinY) / kH * kH}}};
    std::array<gemui::Point, 3> at = home;
    // "deskIcons" preference: "xA,yA,xB,yB,xT,yT" in desktop pixels; anything else keeps the home places, and a place
    // outside the area below the menu bar is brought back inside it.
    // Read with from_chars: a number too large for an int in a settings file edited by hand is refused (sscanf's %d
    // made it undefined behaviour).
    std::array<int, 6> xy{};
    const std::string pref = host_.preference("deskIcons", "");
    const char* p = pref.data();
    const char* const end = p + pref.size();
    bool ok = true;
    for (size_t k = 0; ok && k < xy.size(); ++k) {
        if (k > 0) ok = p < end && *p++ == ',';
        while (ok && p < end && *p == ' ') ++p;
        const auto [next, ec] = std::from_chars(p, end, xy[k]);
        ok = ok && ec == std::errc();
        p = next;
    }
    // The places of the preference are brought to their nearest cell (free places in older versions); two icons in one cell:
    // the home places.
    const gemui::Rect area{0, kMenuH, kWidth, kHeight - kMenuH};
    for (size_t k = 0; ok && k < at.size(); ++k) {
        gemui::DeskIcon& i = deskIcons_[k];
        i.setLimits(area);
        i.setGrid(kGrid);
        at[k] = i.snapped({xy[2 * k], xy[2 * k + 1]});
    }
    if (at[0] == at[1] || at[0] == at[2] || at[1] == at[2]) at = home;
    for (size_t k = 0; k < deskIcons_.size(); ++k) {
        gemui::DeskIcon& i = deskIcons_[k];
        add(i);
        i.setBounds({at[k].x, at[k].y, gemui::DeskIcon::kW, gemui::DeskIcon::kH});
        i.setLimits(area);
        i.setGrid(kGrid);
        // A cell that another icon holds refuses the drop.
        i.canLand = [this, k](gemui::Point to) {
            for (size_t j = 0; j < deskIcons_.size(); ++j)
                if (j != k && deskIcons_[j].bounds().x == to.x && deskIcons_[j].bounds().y == to.y) return false;
            return true;
        };
        if (k < 2) {
            i.setPicture(&icons::kFloppy, &icons::kFloppyMask);
            i.setLetter(char('A' + k), icons::kFloppyLetterX, icons::kFloppyLetterY);
        } else {
            i.setPicture(&icons::kTrash, &icons::kTrashMask);
            // A click on the trash is File > New, with its confirmation (thirteenth DAW test, item 168).
            i.onClick = [this] { newProject(); };
        }
        i.onMoved = [this](gemui::Point) {
            std::string v;
            for (const gemui::DeskIcon& d : deskIcons_)
                v += (v.empty() ? "" : ",") + std::to_string(d.bounds().x) + "," + std::to_string(d.bounds().y);
            host_.setPreference("deskIcons", v);
        };
    }
    nameDeskIcons();
}

void MainWindow::nameDeskIcons() {
    // Names of TOS, in the tooltip language (ninth DAW test, item 102).
    for (size_t k = 0; k < deskIcons_.size(); ++k)
        deskIcons_[k].setName(tipBook_.translate(k < 2 ? "FLOPPY DISK" : "TRASH"));
}

// Keys that no dialogue box or focused widget took (Desktop::keyDown). Returns true when the key is used; StymEditor
// returns the result to JUCE, so that a key the editor does not use can reach the host.
bool MainWindow::keyDown(const gemui::KeyEvent& e) {
    // Order of the prototype's keydown handler.
    // c: the key in upper case, from the host's key code when it gives one (KeyEvent::code: the character the key
    // bears without Shift), so that Shift+1 still reads as 1; otherwise from the character typed.
    const int c = e.code ? e.code : (e.ch < 128 ? std::toupper(int(e.ch)) : 0);
    if (e.repeat) {
        // Auto-repeat: only the arrows, Delete and Backspace, grid open (REPEATABLE); the other repeated keys have no
        // effect (and do not go to the host).
        const bool repeatable = e.key == gemui::Key::Left || e.key == gemui::Key::Right || e.key == gemui::Key::Up ||
                                e.key == gemui::Key::Down || e.key == gemui::Key::Delete ||
                                e.key == gemui::Key::Backspace;
        if (!(repeatable && seq_.isOpen()))
            return e.key == gemui::Key::Character || e.key == gemui::Key::Escape || e.key == gemui::Key::Enter ||
                   repeatable;
    }
    armCur();  // a key prepares an undo step of the track, like a press
    if (settings_.dialogKey(e)) return true;  // Enter: Cancel, the default button of Settings in front
    // A key that changes the Song, the current pattern or the focused track closes an open menu or value popup, as a
    // click elsewhere does: its entries were built for what the key changes (entry indices, track, pattern).
    if (song_.songKey(e)) {  // arrangement from the keyboard
        desk_.closePopups();
        return true;
    }
    // Step Sequencer in front: the keys that act on the note under the cursor, or on the rectangular selection.
    const bool seqOpen = seq_.isOpen();
    const bool seqTop = seqOpen && gemui::FloatWindow::topmost(*this) == &seq_;
    if (seqTop && !seq_.hasRect() && !drumTrack() && seq_.noteKey(e)) return true;
    if (seqTop && seq_.hasRect() && seq_.rectKey(e)) return true;
    // Ctrl shortcuts: undo and redo, the windows (one the focused track does not use stays closed, like its greyed
    // menu entry), the generators (kFxKeys), Shortcuts, New and Quit; all but undo and redo close an open menu first.
    if (e.mods.ctrl) {
        switch (c) {
        case 'Z':
            if (e.mods.shift) seq_.redo();
            else seq_.undo();
            return true;
        case 'Y': seq_.redo(); return true;
        case 'E':
            desk_.closePopups();
            seq_.show();
            return true;
        case 'M':
            desk_.closePopups();
            mix_.show();
            return true;
        case 'G': desk_.closePopups(); openSong(); return true;
        case 'P': desk_.closePopups(); lib_.show(); return true;
        case 'F': desk_.closePopups(); echo_.show(); return true;  // Effects
        case 'T': desk_.closePopups(); ins_.show(); return true;
        case 'K':
            desk_.closePopups();
            if (chipdrumTrack()) kit_.show();  // like the Edit > Drum kit entry
            return true;
        case 'B':
            desk_.closePopups();
            if (!drumTrack()) arp_.show();  // not applicable to drums
            return true;
        case 'R':
            desk_.closePopups();
            if (!drumTrack()) orn_.show();  // not applicable to drums
            return true;
        default: break;
        }
        for (int g = 0; g < ymengine::kGenerators; ++g)
            if (kFxKeys[size_t(g)] && c == kFxKeys[size_t(g)]) {
                desk_.closePopups();
                pickGenerator(g);
                return true;
            }
        switch (c) {
        case '/':
        case '?':
            desk_.closePopups();
            help_.show();  // openHelp
            return true;
        case 'N':
            desk_.closePopups();
            newProject();
            return true;
        case 'Q':
            desk_.closePopups();
            if (host_.standalone()) confirmQuit();
            return host_.standalone();
        default: return false;
        }
    }
    if (e.key == gemui::Key::Tab && !e.mods.ctrl && !e.mods.alt) return cycleWindows(e.mods.shift);
    if (e.key == gemui::Key::Character && e.ch == U' ') {  // Play / Stop
        seq_.togglePlay();
        return true;
    }
    if (e.key == gemui::Key::Escape) {
        // Menu open, otherwise the top window (one that Escape closes), otherwise panic (prototype).
        if (desk_.hasPopups()) desk_.closePopups();
        else if (gemui::FloatWindow* w = gemui::FloatWindow::topmost(*this, true)) w->requestClose();
        else panic();
        return true;
    }
    if (e.key == gemui::Key::Character && c >= '0' && c <= '9') {
        // Digits: Shift+1-4 chooses the bank, 1-8 the pattern (the physical key when the host gives it).
        const int n = c - '0';
        desk_.closePopups();
        if (e.mods.shift) {
            if (n >= 1 && n <= 4) seq_.pickBank(n - 1);
        } else if (n >= 1 && n <= 8) {
            seq_.pickPattern(n - 1);
        }
        return true;
    }
    if (arp_.tableKey(e)) return true;  // arpeggio table: window in front
    if (orn_.tableKey(e)) return true;  // Modulation table: window in front
    if (e.key == gemui::Key::Left || e.key == gemui::Key::Right) {
        // Grid open: the cursor; otherwise (or Shift): the channel.
        if (seqOpen && !e.mods.shift && seq_.cursorKey(e)) return true;
        const int n = voices();
        desk_.closePopups();
        focusTrack((cur_ + (e.key == gemui::Key::Right ? 1 : n - 1)) % n);
        return true;
    }
    if (seqOpen) {
        switch (e.key) {
        case gemui::Key::Up:
        case gemui::Key::Down:
        case gemui::Key::Enter:
        case gemui::Key::Delete:
        case gemui::Key::Backspace:
            if (seq_.cursorKey(e)) return true;
            break;
        default: break;
        }
    }
    // Numeric keypad (keyboard window open: direct octave), M, Z, X, then the keys that play (window open only),
    // according to the chosen layout.
    const KbdAction a = kbdAction(e, keyLayout());
    switch (a.kind) {
    case KbdAction::Kind::Numpad:
        if (!kbdWin_.isOpen()) return false;
        setBaseOctave(a.value);
        return true;
    case KbdAction::Kind::Metro: seq_.toggleMetro(); return true;
    case KbdAction::Kind::OctDown: setBaseOctave(std::max(0, baseOct_ - 1)); return true;
    case KbdAction::Kind::OctUp: setBaseOctave(std::min(9, baseOct_ + 1)); return true;
    case KbdAction::Kind::Note:
        if (!kbdWin_.isOpen()) return false;
        kbdWin_.notePressed(a.value);
        return true;
    case KbdAction::Kind::None: break;
    }
    return false;
}

void MainWindow::keyUp(const gemui::KeyEvent& e) { kbdWin_.keyReleased(e); }  // keyup

void MainWindow::setModifiers(const gemui::Modifiers& m) { seq_.setAlt(m.alt); }

}  // namespace stym::pluginui
