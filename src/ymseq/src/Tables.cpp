// STYM2149 — ymseq: sequencer tables
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymseq/Tables.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace stym::ymseq {

namespace {
constexpr Scale scale(std::string_view name, std::initializer_list<int> d) {
    Scale s{name, {}, 0};
    for (int v : d) s.degrees[s.count++] = int8_t(v);
    return s;
}
int mod12(int v) noexcept { return ((v % 12) + 12) % 12; }
// JavaScript's Math.round, which the prototype uses: halves go up (2.5 → 3, -2.5 → -2), unlike std::round.
double jsRound(double x) noexcept { return std::floor(x + 0.5); }
}  // namespace

const std::array<Scale, kScaleCount> kScales = {{
    scale("Off", {}),
    // major and minor modes
    scale("Major (Ionian)", {0, 2, 4, 5, 7, 9, 11}),
    scale("Minor (Aeolian)", {0, 2, 3, 5, 7, 8, 10}),
    scale("Harmonic minor", {0, 2, 3, 5, 7, 8, 11}),
    scale("Melodic minor", {0, 2, 3, 5, 7, 9, 11}),
    // Greek modes
    scale("Dorian", {0, 2, 3, 5, 7, 9, 10}),
    scale("Phrygian", {0, 1, 3, 5, 7, 8, 10}),
    scale("Lydian", {0, 2, 4, 6, 7, 9, 11}),
    scale("Mixolydian", {0, 2, 4, 5, 7, 9, 10}),
    scale("Locrian", {0, 1, 3, 5, 6, 8, 10}),
    // dark colours
    scale("Phrygian dominant", {0, 1, 4, 5, 7, 8, 10}),
    scale("Hungarian minor", {0, 2, 3, 6, 7, 8, 11}),
    scale("Double harmonic", {0, 1, 4, 5, 7, 8, 11}),
    scale("Neapolitan minor", {0, 1, 3, 5, 7, 8, 11}),
    scale("Ukrainian Dorian", {0, 2, 3, 6, 7, 9, 10}),
    // gapped scales
    scale("Pentatonic minor", {0, 3, 5, 7, 10}),
    scale("Pentatonic major", {0, 2, 4, 7, 9}),
    scale("Japanese (Hirajoshi)", {0, 2, 3, 7, 8}),
    scale("In Sen", {0, 1, 5, 7, 10}),
    scale("Blues", {0, 3, 5, 6, 7, 10}),
    scale("Blues major", {0, 2, 3, 4, 7, 9}),
    // symmetric
    scale("Whole tone", {0, 2, 4, 6, 8, 10}),
    scale("Diminished (H-W)", {0, 1, 3, 4, 6, 7, 9, 10}),
    scale("Diminished (W-H)", {0, 2, 3, 5, 6, 8, 9, 11}),
    scale("Augmented", {0, 3, 4, 7, 8, 11}),
    scale("Chromatic", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}),
}};

uint16_t scaleMask(int s) noexcept {
    if (s <= 0 || s >= kScaleCount) return 0;
    uint16_t m = 0;
    const Scale& sc = kScales[size_t(s)];
    for (int i = 0; i < sc.count; ++i) m = uint16_t(m | (1u << mod12(sc.degrees[size_t(i)])));
    return m;
}

bool inScale(int absNote, int s, int root) noexcept {
    if (s <= 0 || s >= kScaleCount) return true;
    const Scale& sc = kScales[size_t(s)];
    const int pc = mod12(absNote - root);
    for (int i = 0; i < sc.count; ++i)
        if (sc.degrees[size_t(i)] == pc) return true;
    return false;
}

int snapToScale(int absNote, int s, int root, bool snap) noexcept {
    if (s <= 0 || s >= kScaleCount || !snap) return absNote;
    // Every pitch class lies within 6 semitones of any degree, so the search always finds one. At equal distance the
    // lower degree wins.
    for (int d = 0; d <= 6; ++d) {
        if (inScale(absNote - d, s, root)) return absNote - d;
        if (inScale(absNote + d, s, root)) return absNote + d;
    }
    return absNote;
}

int snapTransposed(int absNote, int semis, int s, int root, bool snap) noexcept {
    const int to = snapToScale(absNote + semis, s, root, snap);
    if (to != absNote || !semis) return to;
    // Snap brought the note back where it was: the first degree past absNote + semis in the way of the move.
    const int dir = semis > 0 ? 1 : -1;
    for (int d = 1; d <= 11; ++d)
        if (inScale(absNote + semis + dir * d, s, root)) return absNote + semis + dir * d;
    return to;
}

const std::array<Figure, kFigureCount> kFigures = {{
    {"Step", 0},
    {"1/1", 4}, {"1/2", 2}, {"1/4", 1}, {"1/8", 0.5}, {"1/16", 0.25}, {"1/32", 0.125}, {"1/64", 0.0625},
    {"1/4.", 1.5}, {"1/8.", 0.75}, {"1/16.", 0.375},
    {"1/4T", 2.0 / 3}, {"1/8T", 1.0 / 3}, {"1/16T", 1.0 / 6}, {"1/32T", 1.0 / 12},
}};

const std::array<Resolution, kResolutionCount> kResolutions = {{
    {"1/4  quarter", "1/4", 1}, {"1/8  eighth", "1/8", 2}, {"1/16  sixteenth", "1/16", 4},
    {"1/32  thirty-second", "1/32", 8}, {"1/4T  triplet", "1/4T", 1.5}, {"1/8T  triplet", "1/8T", 3},
    {"1/16T triplet", "1/16T", 6}, {"1/64  sixty-fourth", "1/64", 16}, {"1/32T triplet", "1/32T", 12},
}};

const std::array<Signature, kSignatureCount> kSignatures = {{
    {"2/4", 2, 4}, {"3/4", 3, 4}, {"4/4", 4, 4}, {"5/4", 5, 4}, {"6/4", 6, 4}, {"7/4", 7, 4},
    {"5/8", 5, 8}, {"6/8", 6, 8}, {"7/8", 7, 8}, {"9/8", 9, 8}, {"11/8", 11, 8}, {"12/8", 12, 8},
}};

namespace {
const Resolution& reso(int r) noexcept { return kResolutions[size_t(std::clamp(r, 0, kResolutionCount - 1))]; }
const Signature& sig(int s) noexcept { return kSignatures[size_t(std::clamp(s, 0, kSignatureCount - 1))]; }
}  // namespace

int figureSteps(int figure, int resolution) noexcept {
    const double beats = kFigures[size_t(std::clamp(figure, 0, kFigureCount - 1))].beats;
    if (beats == 0) return 1;
    return std::max(1, int(jsRound(beats * reso(resolution).stepsPerBeat)));
}

double figureStepsExact(int figure, int resolution) noexcept {
    const double beats = kFigures[size_t(std::clamp(figure, 0, kFigureCount - 1))].beats;
    return beats == 0 ? 1 : beats * reso(resolution).stepsPerBeat;
}

bool figureFits(int figure, int resolution) noexcept {
    const double n = figureStepsExact(figure, resolution);  // 2/3 × 1.5 is not exactly 1 in binary
    return n > 0.999999 && std::abs(n - std::round(n)) < 1e-6;
}

int beatSteps(int signature, int resolution) noexcept {
    return std::max(1, int(jsRound(reso(resolution).stepsPerBeat * 4 / sig(signature).denominator)));
}

int barSteps(int signature, int resolution) noexcept {
    return std::max(1, sig(signature).numerator * beatSteps(signature, resolution));
}

namespace {
constexpr std::array<std::string_view, 13> kNotesEn = {"C", "C#", "D", "D#", "E", "F", "F#",
                                                        "G", "G#", "A", "A#", "B", "C"};
constexpr std::array<std::string_view, 13> kNotesLat = {"Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#",
                                                         "Sol", "Sol#", "La", "La#", "Si", "Do"};
}  // namespace

std::string_view noteName(int pitchClass, NoteNames names) noexcept {
    const size_t i = size_t(std::clamp(pitchClass, 0, 12));
    return names == NoteNames::Latin ? kNotesLat[i] : kNotesEn[i];
}

// MIDI numbering: notes 0-11 are octave -1, 60 is C4 (the octave rounded down for a negative note).
std::string absNoteName(int absNote, NoteNames names) {
    const int oct = int(std::floor(absNote / 12.0)) - 1;
    return std::string(noteName(mod12(absNote), names)) + std::to_string(oct);
}

std::optional<int> parseAbsNote(std::string_view text) {
    auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; };
    std::string t;
    for (char c : text)
        if (c != ' ') t += lower(c);
    if (t.empty()) return std::nullopt;
    auto number = [](std::string_view s) -> std::optional<int> {
        if (s.empty() || s.size() > 4) return std::nullopt;
        size_t i = s[0] == '-' ? 1 : 0;
        if (i == s.size()) return std::nullopt;
        int v = 0;
        for (; i < s.size(); ++i) {
            if (s[i] < '0' || s[i] > '9') return std::nullopt;
            v = v * 10 + (s[i] - '0');
        }
        return s[0] == '-' ? -v : v;
    };
    std::optional<int> note;
    if (t[0] >= '0' && t[0] <= '9') {
        note = number(t);
    } else {
        // The longest natural name first (Sol before Si): the sharps are an accidental after it.
        static constexpr std::array<std::pair<std::string_view, int>, 14> kNames = {
            {{"sol", 7}, {"do", 0}, {"re", 2}, {"mi", 4}, {"fa", 5}, {"la", 9}, {"si", 11}, {"c", 0}, {"d", 2},
             {"e", 4}, {"f", 5}, {"g", 7}, {"a", 9}, {"b", 11}}};
        for (const auto& [name, pc] : kNames) {
            if (t.compare(0, name.size(), name) != 0) continue;
            size_t i = name.size();
            int n = pc;
            if (i < t.size() && t[i] == '#') n += 1, ++i;
            else if (i + 1 < t.size() && t[i] == 'b' && (t[i + 1] == '-' || (t[i + 1] >= '0' && t[i + 1] <= '9')))
                n -= 1, ++i;  // flat: "Bb3", not the B of "B3"
            // The rest is the octave, in MIDI numbering (C-1 = 0, C4 = 60); the first name that matches decides.
            const auto oct = number(std::string_view(t).substr(i));
            if (oct) note = (*oct + 1) * 12 + n;
            break;
        }
    }
    if (!note || *note < 0 || *note > 127) return std::nullopt;
    return note;
}

const std::array<std::string_view, 8> kDrumNames = {"Kick", "Snr", "Clap", "Hat", "OHat", "CHat", "Tom1", "Tom2"};
const std::array<std::string_view, 8> kDrumFullNames = {"Kick", "Snare", "Clap", "Hat",
                                                          "Open Hat", "Closed Hat", "Tom 1", "Tom 2"};

const std::array<std::string_view, 13> kSlideCurveNames = {
    "Linear", "Exponential", "Logarithmic", "S-curve", "Stepped", "Sawtooth", "Wobble",
    "Chromatic", "Scale-locked", "Overshoot", "Bounce", "Random walk", "Custom"};
const std::array<std::string_view, 13> kSlideCurveShort = {"LIN", "EXP", "LOG", "S",   "STP", "SAW", "WOB",
                                                            "CHR", "SCL", "OVR", "BNC", "RND", "CUS"};

const std::array<EuclidRhythm, 23> kEuclidRhythms = {{
    {2, 5, "Classical"}, {3, 4, "Cumbia"}, {3, 8, "Tresillo"}, {4, 9, "Aksak"},
    {4, 11, "Frank Zappa"}, {5, 8, "Cinquillo"}, {5, 12, "South African"}, {5, 16, "Bossa-Nova"},
    {7, 8, "Tuareg"}, {7, 12, "West African"}, {7, 16, "Brazilian samba"}, {9, 16, "Central African"},
    {11, 24, "Aka pygmies"}, {13, 24, "Aka pygmies"}, {3, 7, "Ruchenitza"}, {2, 3, "Rumba"},
    {3, 5, "Persian"}, {4, 7, "Ruchenitza"}, {5, 9, "Arab"}, {5, 7, "Nawakhat"},
    {6, 13, "Macedonian"}, {2, 7, "Turkish"}, {3, 11, "Mpre"},
}};

std::string_view euclidName(int pulses, int steps) noexcept {
    for (const EuclidRhythm& e : kEuclidRhythms)
        if (e.pulses == pulses && e.steps == steps) return e.name;
    return {};
}

}  // namespace stym::ymseq
