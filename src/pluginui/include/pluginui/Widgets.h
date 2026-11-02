// STYM2149 — pluginui: elements specific to the main window (oscilloscope, register monitor, voice LEDs, keyboard,
// pads, ADSR curve, Digidrum waveform, SID wave, rule, memory gauge, paragraph)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Each one reproduces an element of the prototype (CSS and canvas drawing), named at the head of its class. The
// prototype's canvases are drawn smoothed; here, without anti-aliasing (STYM2149_SPEC.md §6.1): Bresenham lines, same
// coordinates and same rounding.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "gemui/Controls.h"
#include "gemui/Scroll.h"
#include "gemui/Widget.h"

namespace stym::pluginui {

// Working image of a prototype canvas (canvas size), enlarged or reduced with nearest-neighbour scaling into its
// display box (image-rendering:pixelated). Owned by the widget that draws into it; pixels and fills outside the
// canvas are dropped.
class Canvas {
public:
    Canvas(int w, int h);  // white
    int width() const noexcept { return w_; }
    int height() const noexcept { return h_; }
    void clear(gemui::Color c);
    void pixel(int x, int y, gemui::Color c) noexcept;
    void fill(int x, int y, int w, int h, gemui::Color c) noexcept;
    // Bresenham line between two pixels.
    void line(int x0, int y0, int x1, int y1, gemui::Color c) noexcept;
    // 2 px line centred on the path (lineWidth 2 of the canvas): a 2 × 2 square whose top-left corner is at
    // (x - 1, y - 1), at each pixel of the Bresenham line.
    void line2(int x0, int y0, int x1, int y1, gemui::Color c) noexcept;
    // Text in the GEM font, baseline at y.
    void text(std::string_view s, double x, int baseline, int px, gemui::Color c,
              gemui::Face face = gemui::Face::kSystem);
    // Copy into r with nearest-neighbour scaling (centre of the destination pixel mapped back into the canvas).
    void blit(gemui::Painter& p, const gemui::Rect& r) const;
    gemui::Color at(int x, int y) const noexcept { return px_[size_t(y * w_ + x)]; }

private:
    int w_, h_;
    std::vector<gemui::Color> px_;
};

// canvas#scope: 920 × 90 inside a 2 px line, stretched to 940 × 86 with nearest-neighbour scaling (a narrower box keeps
// the 24 px between the box and its canvas). 2 px black line: rising edge searched in the first half of the samples,
// then at most 1024 samples across the width, y = 45 - v × gain × 40.5 (drawScope of the prototype, gain 1).
// A stereo source (Indus, 29/10/2026 10:54): the left channel in the left half, the right one in the right
// half, each with its own rising edge and its own 2 px frame, kSplitGap px of white between the two frames (Indus,
// 29/10/2026 14:37 and 15:37), and "L" / "R" at their top left; Merge draws (left + right) / 2 across the width instead.
class Scope : public gemui::Widget {
public:
    // Gains of the trace: a single voice at volume 15, at the track's default level and velocity (75 %, 100),
    // reaches 90 % of the half height: 0.287 on the main output (Master 70 %), 0.591 on a track's oscilloscope; a
    // louder peak goes off the edge. The prototype's gain was 1.
    static constexpr double kMainGain = 3.5, kTrackGain = 1.7;
    // Stereo: each half in a frame of its own, kSplitGap canvas pixels of white between the two frames.
    static constexpr int kSplitGap = 10;
    // Width of each half of a stereo trace on a canvas `width` px wide: the left one starts at 0, the right one at
    // width − splitHalf(width); between them, the 2 px inner edges of the two frames and the gap.
    static constexpr int splitHalf(int width) noexcept { return (width - 4 - kSplitGap) / 2; }

    Scope();
    // Box and canvas: the canvas is 24 px narrower than the box (920 in 944), 90 px high.
    void place(const gemui::Box& b);
    // Redraws the trace from samples ordered from oldest to most recent, ±1 at full scale: mid = (left + right) / 2,
    // side = (right − left) / 2 (empty: the same on both sides). trigger: start at the first rising zero
    // crossing of the first half, at most 1024 samples across (the main output at the host's rate); otherwise every
    // sample across (a track's 128 points).
    void setSamples(std::span<const float> mid, std::span<const float> side, double gain, bool trigger);
    // One trace of the mean even when the source is stereo; redraws the last samples.
    void setMerge(bool merge);
    // The source is stereo (MainWindow::scopeStereo): two halves whatever the sound, a centred one too, unless Merge is
    // on (Indus, 29/10/2026 20:03); a mono source, one trace.
    void setStereo(bool stereo);
    bool merged() const noexcept { return merge_; }
    // The last trace was drawn in two halves, left and right (tests).
    bool split() const noexcept { return split_; }
    // Frames of the left and right halves on screen, the gap between them (split).
    std::pair<gemui::Rect, gemui::Rect> splitFrames() const noexcept;
    const Canvas& canvas() const noexcept { return canvas_; }
    void paint(gemui::Painter& p) override;

private:
    void redraw();
    // One channel across [x0, x0 + w) of the canvas.
    void trace(std::span<const float> v, int x0, int w);

    Canvas canvas_{920, 90};
    std::vector<float> mid_, side_;
    double gain_ = kMainGain;
    bool trigger_ = true, merge_ = false, stereo_ = false, split_ = false;
};

// .regstrip: 14 columns, 10 px headers in reverse video (R0 TPlo … R13 SHP, then "YM2149F REGISTER MONITOR" across
// six columns), 14 px hexadecimal values, 2 px rules.
class RegisterMonitor : public gemui::Widget {
public:
    // Values of R0, R1, R6, R7, R8, R11, R12 and R13, in the order of the columns.
    void setValues(const std::array<uint8_t, 8>& v);
    // Text of the wide cell on the right: channel, sound and voice mode of the focused track, or the YM Player.
    void setInfo(std::string info);
    const std::string& info() const noexcept { return info_; }
    gemui::Rect infoRect() const noexcept;  // the cell of the info text, under its header
    void paint(gemui::Painter& p) override;

private:
    double columnX(int k) const noexcept;
    // Until the first setValues: TP 284 (0x11C), NP 8, tone on and noise off, volume 13, EP 400 (0x190), shape 8.
    std::array<uint8_t, 8> values_{0x1C, 0x01, 0x08, 0x3E, 0x0D, 0x90, 0x01, 0x08};
    std::string info_ = "-";
};

// .voices: one LED per track, #CC0000 lit, #440000 off; LED without a channel (3-channel mode) at 25 %. The prototype's
// 16 were a grid of 8 × 2 LEDs of 12 px (4 px gap, 2 px line); the 32 tracks are 16 × 2 LEDs of 6 × 12 px
// (2 px gap, 1 px line), in the same height.
class VoiceLeds : public gemui::Widget {
public:
    static constexpr int kCount = 32;
    // on: lit LED of each voice; available: number of voices that have a channel, the LEDs from that one on are faded;
    // shown: 32 in 32 ch, two rows of sixteen narrow LEDs, 1-16 on top; 16 in the other modes, two rows of
    // eight as in the prototype (Indus, 30/10/2026 02:20).
    void setState(const std::array<bool, kCount>& on, int available, int shown);
    void setAvailable(int available, int shown) { setState(on_, available, shown); }
    int shown() const noexcept { return shown_; }
    void paint(gemui::Painter& p) override;

private:
    std::array<bool, kCount> on_{};
    int available_ = 16, shown_ = 16;
};

// MIDI monitor of the SYSTEM frame (Options > MIDI monitor): white box with a 2 px line, the messages received
// from the host one per line in the GEM's 8×8 font, the most recent at the bottom. It keeps the number of lines
// chosen in Settings, the oldest leaving first, and scrolls back through them with the scroll bar at its right
// edge or the wheel; scrolled back, the lines shown stay put while new ones arrive. A message may take several lines
// (a SysEx); one that comes again just after itself is counted on a line under it, "repeated xN". The Clear
// button of its row empties it (Indus, 28/10/2026 10:20).
class MidiLog : public gemui::Widget {
public:
    static constexpr std::array kKeptChoices = {100, 500, 1000, 5000};  // Settings > MIDI monitor
    static constexpr int kKeptDefault = 500;
    // Custom, typed (Indus, 28/10/2026 10:02): 1 to 100 000 lines, about 8 MB at the most (Indus's choice
    // on the card, 10:20).
    static constexpr int kKeptMin = 1, kKeptMax = 100000;
    static constexpr double kLine = 10;  // line pitch of the 8×8 font
    MidiLog();
    void setKept(int lines);  // the oldest beyond leave
    int kept() const noexcept { return kept_; }
    void push(const std::string& message);  // its lines separated by '\n'
    void clear();  // every line gone
    bool empty() const noexcept { return lines_.empty(); }
    std::vector<std::string> lines() const { return {lines_.begin(), lines_.end()}; }  // oldest first
    int rows() const noexcept;  // lines the box shows at its height
    // Lines scrolled back from the most recent (0: the most recent at the bottom), clamped to what is kept.
    int back() const noexcept { return back_; }
    void scrollBack(int lines);
    // Its box and its scroll bar's (the children do not follow setBox).
    void place(const gemui::Box& b);
    gemui::ScrollBar& bar() noexcept { return bar_; }
    const gemui::ScrollBar& bar() const noexcept { return bar_; }
    bool acceptsMouse() const noexcept override { return true; }  // the wheel
    bool mouseWheel(const gemui::WheelEvent& e) override;
    void paint(gemui::Painter& p) override;
    void paintOver(gemui::Painter& p) override;  // its line over the scroll bar's edges

private:
    int maxBack() const noexcept { return std::max(0, int(lines_.size()) - rows()); }
    void syncBar();
    void addLine(std::string line);
    std::deque<std::string> lines_;
    int kept_ = kKeptDefault;
    int back_ = 0;
    std::string last_;  // the last message, while its lines are the last ones kept
    int repeats_ = 0;   // times it came in a row
    gemui::ScrollBar bar_;
};

// .kbd: four octaves of white keys (flex, 2 px line at the top and bottom, 1 px on the sides, 9 px name at the
// bottom) and black keys (62 % of a white key wide, as buildKbd sets it, 58 % high); played key in halftone; key out
// of the chip's range (B0 to C9) hatched. Press: plays; drag with the button held: the note follows the pointer; right
// click: menu of the played track.
class Keyboard : public gemui::Widget {
public:
    static constexpr int kPlayLo = 23, kPlayHi = 120;  // B0 to C9 (PLAY_LO, MAP_HI)

    // Octave of the leftmost C (clamped to 0-9, C4 = MIDI 60); the four octaves drawn start at C6 at most.
    void setBaseOctave(int oct);
    int baseOctave() const noexcept { return oct_; }
    // Shows the key of a MIDI note (0-127) as played or not.
    void setHeld(int note, bool held);
    bool held(int note) const noexcept { return note >= 0 && note < 128 && held_[size_t(note)]; }
    void clearHeld();
    void setNoteNames(const std::array<std::string, 12>& names);

    std::function<void(int note)> onPress;    // press or drag on a key
    std::function<void(int note)> onRelease;  // end of the press (the latch decides whether to release)
    std::function<void(gemui::Point)> onContext;

    void paint(gemui::Painter& p) override;
    bool acceptsMouse() const noexcept override { return true; }
    void mouseDown(const gemui::MouseEvent& e) override;
    void mouseDrag(const gemui::MouseEvent& e) override;
    void mouseUp(const gemui::MouseEvent& e) override;
    void mouseCancel() override { mouseUp({}); }  // the held note or pad is released
    gemui::Pointer pointer() const noexcept override { return gemui::Pointer::Hand; }
    std::string tipAt(gemui::Point p) const override;
    // Box of the key under the point, otherwise that of the keyboard: anchor of the right-click menu
    // (closest('.wk, .bk, .pad') || #kbd of the prototype).
    gemui::Box keyBoxAt(gemui::Point p) const;

private:
    struct Key {
        int note;
        bool black;
        gemui::Box box;
    };
    std::vector<Key> keys() const;
    std::optional<Key> keyAt(gemui::Point p) const;  // black keys first, laid over the white ones
    int noteAt(gemui::Point p) const;
    std::string noteName(int note) const;

    int oct_ = 2;
    std::array<bool, 128> held_{};
    std::array<std::string, 12> names_{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int dragNote_ = -1;
};

// .pads: eight pads (8-column grid, 4 px gap, 56 px high, 11 px text) that replace the keyboard for percussion; held
// pad grey (#AAAAAA), Digidrum pad without a sample at 45 %.
class Pads : public gemui::Widget {
public:
    void setLabels(const std::array<std::string, 8>& labels);
    void setTips(const std::array<std::string, 8>& tips);
    void setEmpty(const std::array<bool, 8>& empty);
    void setHeld(int pad, bool held);
    void clearHeld();

    std::function<void(int pad)> onPress, onRelease;
    std::function<void(gemui::Point)> onContext;

    void paint(gemui::Painter& p) override;
    bool acceptsMouse() const noexcept override { return true; }
    void mouseDown(const gemui::MouseEvent& e) override;
    void mouseDrag(const gemui::MouseEvent& e) override;
    void mouseUp(const gemui::MouseEvent& e) override;
    void mouseCancel() override { mouseUp({}); }  // the held note or pad is released
    gemui::Pointer pointer() const noexcept override { return gemui::Pointer::Hand; }
    std::string tipAt(gemui::Point p) const override;
    // Box of the pad under the point, otherwise that of the pads: anchor of the right-click menu.
    gemui::Box padBoxAt(gemui::Point p) const noexcept;
    // Pad (tests): box, label, Digidrum slot without a sample, held.
    gemui::Rect padRect(int i) const noexcept { return padBox(i).snapped(); }
    const std::string& label(int i) const noexcept { return labels_[size_t(i)]; }
    bool empty(int i) const noexcept { return empty_[size_t(i)]; }
    bool held(int i) const noexcept { return held_[size_t(i)]; }

private:
    gemui::Box padBox(int i) const noexcept;
    int padAt(gemui::Point p) const noexcept;

    std::array<std::string, 8> labels_{}, tips_{};
    std::array<bool, 8> empty_{}, held_{};
    int down_ = -1;
};

// canvas#adsrCurve (drawAdsrCurve): real time axis (A, D, a nominal 400 ms sustain, R), grey marks, continuous
// trace (Smooth) or in steps of 16 levels (Chip), letters under the segments wide enough, position of the followed
// voice (green bar and dot).
class AdsrCurve : public gemui::Widget {
public:
    struct Shape {
        double attackMs = 0, decayMs = 0, sustain = 1, releaseMs = 0;  // sustain: level, 0 to 1
        bool chip = false;
        bool off = false;  // Sync Buzzer in Chip mode or percussion: the ADSR drives nothing (no position)
        bool operator==(const Shape&) const = default;
    };
    void setShape(const Shape& s);
    // Stage (1 attack, 2 decay, 3 sustain, 4 release) and level (0 to 1) of a voice sounding on the track, from
    // EngineStatus::env; stage < 0: none.
    struct LivePoint {
        int stage = -1;
        double level = 0;
    };
    // One point per note sounding (24/10/2026), the first one, the newest, drawn over the others.
    void setLive(std::span<const LivePoint> points);
    const std::vector<LivePoint>& live() const noexcept { return live_; }  // tests
    // Time (ms, on the drawing's axis, its 400 ms sustain included) of a voice's point, found back from its level;
    // < 0: no point.
    static double liveTime(const Shape& s, int stage, double level) noexcept;
    static constexpr double kHoldMs = 400;  // time drawn for the sustain
    void paint(gemui::Painter& p) override;

private:
    Shape shape_;
    std::vector<LivePoint> live_;  // the points with a stage
};

// canvas#drumwave of the prototype, 360 × 48 inside a 2 px line: empty slot, frame; otherwise the sample, its length
// and its playback rate (drawDrum of the prototype, which always wrote 7800 Hz; the rate is set in the Digidrums
// window), after the name of the slot when it has one. The sample is drawn at the box's pixels, under
// the line of text (the prototype's canvas, a value every n / 360 reduced into the box, dropped most of them).
// A drawing only: the samples are loaded in the Digidrums window (the prototype loaded a file on a click). The
// line of text is in the GEM 6×6 font.
class DrumWave : public gemui::Widget {
public:
    static constexpr int kW = 360, kH = 48;  // the prototype's canvas: columns and rows of the design

    DrumWave();
    void setSample(int slot, std::span<const uint8_t> nibbles, double rateHz = 7800, std::string_view name = {});
    // Line of text written in the frame (tests).
    const std::string& caption() const noexcept { return caption_; }

    void paint(gemui::Painter& p) override;

private:
    std::vector<uint8_t> nibbles_;
    std::string caption_, name_;
    int baseline_ = 14;  // of the line of text, in the rows of the design
};

// Sample of the DMA track: 360 × 56 inside a 2 px line in the design. Empty slot: frame (and an invitation
// to load when it loads); otherwise the envelope around zero, the left channel at the top and the right one at the
// bottom for a stereo sample, marked L and R, the part played (grey lines at Start and End, the frames
// outside it grey, the top of the band when it loops) and a line of text (name of the sample when it has one;
// size, duration, rate, format). The sample is drawn at the box's pixels. In the DMA window, a click loads a
// file; in the GENERATOR frame, it is a drawing only. The line of text is in the GEM 6×6 font, at the screen's
// pixels.
class DmaWave : public gemui::Widget {
public:
    static constexpr int kW = 360, kH = 56;  // columns and rows of the design, stretched to the box

    explicit DmaWave(bool loads = true);  // loads: a click calls onClick (DMA window), otherwise a drawing only
    // Signed 8-bit bytes of sample `slot` (0-based; the slot buttons show its number, the caption does not),
    // interleaved left then right when stereo; rateHz is the playback rate written in the caption. The part played
    // covers frames start to end - 1 and is drawn like that of a DigiWave when start < end <= the number of
    // frames; `name` (empty: none) opens the line of text. Repaints only when something changed.
    void setSample(int slot, std::span<const int8_t> bytes, double rateHz, bool stereo, bool loop = false,
                   uint32_t start = 0, uint32_t end = 0, std::string_view name = {});
    std::function<void()> onClick;
    // Line of text written in the frame (tests).
    const std::string& caption() const noexcept { return caption_; }

    void paint(gemui::Painter& p) override;
    bool acceptsMouse() const noexcept override { return loads_; }
    void mouseUp(const gemui::MouseEvent& e) override;
    gemui::Pointer pointer() const noexcept override { return loads_ ? gemui::Pointer::Hand : gemui::Pointer::Arrow; }

private:
    bool loads_;
    std::string caption_;
    int baseline_ = 16;  // of the line of text, in the rows of the design
    // What is drawn (the drawing is repainted only if one of them changes).
    int slot_ = -1;
    double rate_ = 0;
    bool stereo_ = false, loop_ = false;
    uint32_t start_ = 0, end_ = 0;
    std::string name_;
    std::vector<int8_t> bytes_;
};

// Digidrum sample of the editor: 360 × 72 inside a 2 px line. Empty slot: frame and an invitation to
// load; otherwise the 4-bit values (0 at the bottom, 15 at the top) drawn at the box's pixels, the part played
// (two grey lines at Start and End, the values outside it grey, the top of the band when it loops) and a line of
// text (values, duration, rate) in the GEM 6×6 font. A click loads a file.
class DigiWave : public gemui::Widget {
public:
    static constexpr int kW = 360, kH = 72;  // columns and rows of the design, stretched to the box

    DigiWave();
    // 4-bit values (0 to 15) of slot `slot` (0-based; its number is on the slot buttons, not in the caption);
    // the part played covers values start to end - 1 and is drawn only when start < end <= the number of values (the
    // lines when it is not the whole sample or it loops); `name` (empty: none) opens the line of text.
    // Repaints only when something changed.
    void setSample(int slot, std::span<const uint8_t> nibbles, double rateHz, bool loop, uint32_t start,
                   uint32_t end, std::string_view name = {});
    std::function<void()> onClick;

    void paint(gemui::Painter& p) override;
    bool acceptsMouse() const noexcept override { return true; }
    void mouseUp(const gemui::MouseEvent& e) override;
    gemui::Pointer pointer() const noexcept override { return gemui::Pointer::Hand; }

private:
    std::string caption_;
    int baseline_ = 24;  // of the line of text, in the rows of the design
    int slot_ = -1;
    double rate_ = 0;
    bool loop_ = false;
    uint32_t start_ = 0, end_ = 0;
    std::string name_;
    std::vector<uint8_t> nibbles_;
};

// Bars of the SID wave of a SID Voice, in the GENERATOR frame (formerly in the Timer Wave window): 16 columns
// of 11 px spaced by 1 px; a bar rises from the bottom by 3 px per volume step (0 to 15), the volume is written in
// hexadecimal under the column (6×6 text). Beyond the length of the wave, or with the square wave, the columns are
// faded as in #orngrid. The FM wave of an FM Voice takes the same grid on another scale: values 0 to 7 (the
// multiples of the note), 5 px per notch with a 5 px floor, its own tips. In semitones (0 to 127) the values are
// written in hexadecimal on two digits ($00 to $7F, like the other values; Indus, 01/11/2026 17:52) on two rows, the
// even steps on the first and the odd steps on the second, so that neighbouring values do not run together.
class SidWaveGrid : public gemui::Widget {
public:
    static constexpr int kSteps = 16;  // ymengine::kSidWaveSteps
    static constexpr int kCellW = 11, kGap = 1, kBarH = 47, kDigitH = 9;
    static constexpr int kWidth = kSteps * (kCellW + kGap) - kGap;  // 191
    static constexpr int kHeight = kBarH + kGap + kDigitH;          // 57, one row of digits

    struct State {
        std::array<uint8_t, kSteps> wave{};
        int length = 0;  // bars played, the others faded
        // The square wave played, drawn in `wave` (24/10/2026; Base and Square): the mouse without
        // effect.
        bool square = false;
        bool operator==(const State&) const = default;
    };
    void setState(const State& s);
    const State& state() const noexcept { return st_; }
    // Scale of the bars: highest value, pixels per notch, a floor of one notch under value 0 (the FM wave's ½). The
    // value under each bar: one hexadecimal digit up to 15; beyond (the FM wave in semitones, 0 to 127, whose bar as
    // high as the column shows 45 and more), two hexadecimal digits on two rows.
    void setScale(int maxValue, int notchPx, bool floor) noexcept {
        if (maxValue == max_ && notchPx == notch_ && floor == floor_) return;
        max_ = maxValue;
        notch_ = notchPx;
        floor_ = floor;
        repaint();
    }
    void copyScale(const SidWaveGrid& o) noexcept { setScale(o.max_, o.notch_, o.floor_); }
    int maxValue() const noexcept { return max_; }
    // Rows of digits under the bars, and the height of the grid with them (kHeight with one row).
    int digitRows() const noexcept { return max_ > 15 ? 2 : 1; }
    int height() const noexcept { return kBarH + kGap + digitRows() * kDigitH; }
    // Tip of a step, in place of the SID wave's.
    std::function<std::string(int step, int value)> tipFor;

    // Column of a step, on the desktop.
    gemui::Rect cellRect(int step) const noexcept;

    // Writes requested by the mouse; one wheel notch is one on its own (onWheel, otherwise onSet).
    std::function<void(int step, int value)> onSet, onWheel;
    // Right click on a step: its slider (seventh DAW test, item 84); without it, the step returns to 0.
    std::function<void(int step, const gemui::Rect& cell)> onMenu;

    void paint(gemui::Painter& p) override;
    bool acceptsMouse() const noexcept override { return true; }
    void mouseDown(const gemui::MouseEvent& e) override;
    void mouseDrag(const gemui::MouseEvent& e) override;
    void mouseUp(const gemui::MouseEvent& e) override;
    void mouseCancel() override { mouseUp({}); }  // the stroke stops where it is
    bool mouseWheel(const gemui::WheelEvent& e) override;
    gemui::Pointer pointerAt(gemui::Point p) const noexcept override;
    std::string tipAt(gemui::Point p) const override;

private:
    int stepAt(gemui::Point p) const noexcept;  // -1 outside the columns
    int valueAt(double y) const noexcept;

    State st_;
    bool painting_ = false;
    int max_ = 15, notch_ = 3;
    bool floor_ = false;
};

// 2 px horizontal rule (#genSep).
class Rule : public gemui::Widget {
public:
    void paint(gemui::Painter& p) override;
};

// Memory of the Atari the samples take, in the Digidrums and DMA windows: a bar inside a 2 px line, black up
// to the bytes heard out of those the samples can take; red across when samples do not fit (silent); grey in
// 32-channel mode, which has no limit.
class MemoryGauge : public gemui::Widget {
public:
    // `used` and `total` bytes (SteMemory), out of `budget`; `limited`: a 3-channel mode.
    void set(uint64_t used, uint64_t total, uint64_t budget, bool limited);
    void paint(gemui::Painter& p) override;

private:
    uint64_t used_ = 0, total_ = 0, budget_ = 1;
    bool limited_ = true;
};

// Name typed in place (the instrument of the GENERATOR frame): Enter, or a press elsewhere that takes the focus,
// commits it; the field hidden or detached drops what was typed.
class NameField : public gemui::TextField {
public:
    std::function<void()> onCommit;
    void focusLost() override {
        gemui::TextField::focusLost();
        if (onCommit) onCommit();
    }
    void focusDropped() override { gemui::TextField::focusLost(); }
};

// Block without a drawing of its own whose opacity applies to all its content, like a CSS group (.inert: 40 %). Its
// box must cover those of its children (the group is rendered within its box).
class Layer : public gemui::Widget {
public:
    void setOpacity(double a);
    double opacity() const noexcept override { return opacity_; }

private:
    double opacity_ = 1;
};

// Note over several lines (div.hint): wrapped at spaces and after a hyphen between letters within the width of the
// box, 12 px, #222; 6×6 font, which keeps the line breaks of the prototype.
class Paragraph : public gemui::Widget {
public:
    explicit Paragraph(std::string text = {}) : text_(std::move(text)) {}
    void setText(std::string t);
    // Height of a line (line-height; 12 px by default, 16.8 px for line-height:1.4).
    void setLineHeight(double h);
    double lineHeight() const noexcept { return lineHeight_; }
    const std::string& text() const noexcept { return text_; }
    int lineCount() const;
    double naturalWidth() const;  // "\n": line break (<br>)
    // The text is a name (a tune's title): measured and drawn as it is, never translated.
    void setRaw(bool raw);
    void paint(gemui::Painter& p) override;

private:
    std::vector<std::string> lines() const;
    std::string text_;
    double lineHeight_ = 12;
    bool raw_ = false;
};

// #learnBadge: MIDI learn banner at the bottom left of the desktop (left 8, bottom 8), in front of the windows
// (z-index 95, below the menus and the dialog boxes): white background, 2 px line, 4 px / 10 px margins, 16 px text.
// It takes the presses (nothing goes through to what is below).
class LearnBadge : public gemui::Widget {
public:
    LearnBadge();
    // Shows `text` on a desktop `deskH` px high.
    void show(std::string text, int deskH);
    const std::string& text() const noexcept { return label_.text(); }
    void paint(gemui::Painter& p) override;
    bool acceptsMouse() const noexcept override { return true; }

private:
    gemui::Label label_;
};

// #volmenu (openVolMenu): vertical slider from `min` to `max` under the `anchor` cell (above it if there is not
// enough room), each movement passed at once to `set`; its value below can be typed (sixth DAW test, item 76)
// and a right click on the slider sets `reset` (item 75). To be opened by Desktop::openPopup (SeqPopups.cpp).
struct SliderPopupSpec {
    int min = 0, max = 15, value = 15, reset = 15;
    std::function<void(int)> set;
    // A toggle button under the value (the Slide of a VOL cell): its text (none when empty), state, help, and
    // the action given the new state; greyed when not enabled.
    std::string toggle, toggleTip;
    bool toggleOn = false, toggleEnabled = true;
    std::function<void(bool)> onToggle;
};
std::unique_ptr<gemui::Widget> makeSliderPopup(SliderPopupSpec spec, const gemui::Rect& anchor, int deskW, int deskH);
// Volume 0 to 15, back to 15 on a right click.
std::unique_ptr<gemui::Widget> makeVolumePopup(int value, std::function<void(int)> set, const gemui::Rect& anchor,
                                               int deskW, int deskH);

}  // namespace stym::pluginui
