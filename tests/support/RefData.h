// STYM2149 — reference tunes (D-032, tools/fetch_refdata.sh)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace stym::test {

struct RefTune {
    const char* file;
    bool effects;     // YM5/YM6 effects or YM2 digidrums: played in M2, excluded from bit-exact comparisons
    const char* role; // what the tune exercises
};

inline const std::vector<RefTune>& refTunes() {
    static const std::vector<RefTune> tunes = {
        {"Decade Where's my willy.ym", false, "YM5, hi-hat + buzzer"},
        {"DHS Intro.ym", false, "YM5, hi-hat + buzzer"},
        {"Digizak.ym", false, "YM5, hi-hat + buzzer"},
        {"Gritty.ym", false, "YM5, buzzer"},
        {"Scout.ym", false, "YM5, hi-hat"},
        {"ST News 61.ym", false, "YM6 without effects, loop"},
        {"Mon Tune 8.ym", false, "YM5, loop"},
        {"Ashtray.ym", false, "YM5, hi-hat"},
        {"Prehistoric Tale (title).ym", false, "YM2, buzzer"},
        {"Simpsons.ym", false, "YM3, buzzer"},
        {"jimpowr5.ym", false, "YM3b, loop"},
        {"Credits.ym", true, "YM6, effects"},
        {"Iceage (digi).ym", true, "YM5, digidrums"},
        {"Prelude.ym", true, "YM6, effects"},
        {"Steps.ym", true, "YM6, effects"},
        {"ND-Toxygene.ym", true, "YM6, Sync Buzzer"},
        {"LethalXcess1.ym", true, "YM5, effects"},
        {"Synth Dream 5.ym", true, "YM5, effects"},
        {"Wings2.ym", true, "YM2, digidrums"},
        {"WingsLeveltune3.ym", true, "YM2, digidrums"},
        {"WingsLeveltune5.ym", true, "YM2, digidrums"},
        {"Amazine 2.ym", true, "YM6, effects"},
    };
    return tunes;
}

// Reference SNDH files (M7, D-159), in refdata/sndh.
struct RefSndh {
    const char* file;
    bool ste;          // STE DMA sound
    const char* role;  // what the tune exercises
};

inline const std::vector<RefSndh>& refSndh() {
    static const std::vector<RefSndh> tunes = {
        {"Bomber_(ST).sndh", false, "Ice, chip only"},
        {"Bomber_(STe).sndh", true, "Ice, STE DMA"},
        {"Car_Race.sndh", false, "Ice, small"},
        {"Master_of_Magic.sndh", false, "Ice, SID voices (timers), 3 subsongs"},
        {"Sanxion.sndh", false, "Ice, digidrums (timers)"},
        {"Buzzmosphere.sndh", false, "unpacked, 70 Hz"},
        {"Outpost.sndh", false, "Ice, 57 Hz"},
        {"Lethal_Xcess_(STe).sndh", true, "Ice, STE DMA, 7 subsongs"},
        {"Wings_Of_Death.sndh", false, "Ice, VBL (!V), 9 subsongs"},
        {"Intensity_200hz.sndh", false, "Ice, 200 Hz"},
        {"Schwerlasttransport.sndh", true, "unpacked, STE DMA"},
        {"mOdulator.sndh", false, "unpacked, Tao"},
    };
    return tunes;
}

// Folder of the tunes: $STYM_REFDATA, otherwise the path set at CMake configuration. Empty if it is missing.
std::string refDataDir();
// True if $STYM_REQUIRE_REFDATA is set (CI): the absence of the tunes is then an error, not a SKIP.
bool refDataRequired();
// Reads a reference tune as is (LHA-compressed: parseYm and AtariAudio decompress it). Empty if missing.
std::vector<uint8_t> loadRefTune(const std::string& file);
// Reads a reference SNDH as is (Ice-packed or not). Empty if missing.
std::vector<uint8_t> loadRefSndh(const std::string& file);

}  // namespace stym::test
