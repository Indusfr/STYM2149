// STYM2149 — File menu, Import > SNDH / YM file: dialogue, tune read under the bee, grid dialogue,
// Authentic 3ch with tracks 1 to 3, instruments, timing, alert, one undo step for the whole import, Cancel and
// refusals without change, subsong of an SNDH; Import > Tracker song of a maxYMiser song: the voiceset
// found beside the song or asked for, digidrums on track 4, undo, refusals; an SNDH saved by maxYMiser imported from
// the song it holds; Export song > maxYMiser: the song and its voiceset written side by side; Import > Tracker
// song of a YM-Tracker song and Export song > YM-Tracker.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>
#include <string>
#include <vector>

#include "../support/SndhBuild.h"
#include "gemui/Alert.h"
#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"
#include "ymio/MaxYm.h"
#include "ymio/TTrak.h"
#include "ymio/YmTracker.h"

#include "FileHost.h"

using namespace stym;
using namespace stym::testing;
using pluginui::MainWindow;
using plugin::Global;
using plugin::paramIndex;
using plugin::Track;

namespace {

// YM3 dump: "YM3!" then the 14 registers, each for every frame (interleaved), at 50 Hz.
std::string ym3(const std::vector<std::array<uint8_t, 14>>& frames) {
    std::string f = "YM3!";
    for (size_t r = 0; r < 14; ++r)
        for (const auto& x : frames) f.push_back(char(x[r]));
    return f;
}

// 16 plucked notes on voice A, one every 6 frames, then silence.
std::string melody() {
    std::vector<std::array<uint8_t, 14>> frames(104);
    for (auto& x : frames) x[7] = 0x3f;
    const int tps[] = {284, 253, 225, 213, 190, 169, 159, 142};
    for (int k = 0; k < 16; ++k)
        for (int i = 0; i < 6; ++i) {
            auto& x = frames[size_t(6 * k + i)];
            x[0] = uint8_t(tps[k % 8] & 255);
            x[1] = uint8_t(tps[k % 8] >> 8);
            x[7] = 0x3e;
            x[8] = uint8_t(15 - 2 * i);
        }
    return ym3(frames);
}

// maxYMiser song at speed 3: channel A plays C4, E4 and G4 with instrument 1 (square), channel C a digidrum every 8
// rows with instrument 2 (digidrum 1 at 7680 Hz, 1536 samples).
std::string mys() {
    ymio::MysSong s;
    s.speed = 3;
    s.header[36] = 7;  // the timers of the three channels
    s.patterns.resize(2);
    s.patterns[0].rows[0] = {60, 1};
    s.patterns[0].rows[16] = {64, 1};
    s.patterns[0].rows[32] = {67, 1};
    for (int r = 0; r < 64; r += 8) s.patterns[1].rows[size_t(r)] = {60, 2};
    s.steps[0].channel = {0, ymio::kMysEmpty, 1};
    const std::vector<uint8_t> b = ymio::writeMys(s);
    return std::string(b.begin(), b.end());
}

std::string myv() {
    ymio::MyvVoiceset v;
    v.sequences.resize(4);
    v.sequences[1].data[0] = 15;      // volume
    v.sequences[2].data[0] = 0x0100;  // mixer: square
    v.sequences[3].data[0] = 0x000D;  // mixer: digidrum
    using I = ymio::MyvInstrument;
    for (int k = 0; k < 2; ++k) {
        I& i = v.instruments[size_t(k)];
        i.at(I::kSequenceSpeed) = 1;
        i.at(I::kSequences) = 1;
        i.at(I::kSequences + 3) = uint8_t(2 + k);
    }
    v.instruments[1].at(I::kDigidrum) = 1;
    v.instruments[1].at(I::kDigidrumRate) = 80;
    for (int i = 0; i < 1536; ++i) v.digidrums[0].push_back(uint8_t(i * 7 % 200));
    const std::vector<uint8_t> b = ymio::writeMyv(v);
    return std::string(b.begin(), b.end());
}

}  // namespace

TEST_CASE("Import > SNDH / YM file, YM file: grid dialogue, Authentic 3ch, tracks, instruments, timing, alert, one undo step",
          "[ui][register-import]") {
    FileHost h;
    h.disk["/t/tune.ym"] = melody();
    h.disk["/t/bad.ym"] = "YM3!";
    h.disk["/t/text.ym"] = "not a tune";
    // Before the import: 32 ch with MPE, a track on beyond the third, an instrument, a timing and a groove.
    h.state.instruments[0].used = true;
    h.state.instruments[0].name = "mine";
    h.state.global.speed = 4;
    h.state.global.grooveLength = 2;
    h.state.global.groove = {1, 0};
    h.state.seq.song.entries = {{0, 2, 0}};
    h.p(Global::Mpe) = 1;
    h.p(Global::VblRate) = 2;
    h.p(Global::A4Hz) = 432;
    h.p(5, Track::On) = 1;
    h.p(1, Track::Level) = 40;
    const plugin::NonParamState before = h.state;
    const plugin::ParamValues valuesBefore = h.values;
    MainWindow w(h);
    double now = 0;
    w.tick(now);

    fileMenu(w, "Import", "SNDH / YM file");
    REQUIRE(h.opens.size() == 1);
    CHECK(h.opens[0].title == "Import an SNDH or YM file");
    CHECK(h.opens[0].patterns == "*.sndh;*.snd;*.ym;*.ym2;*.ym3;*.ym5;*.ym6");
    h.pick("/t/tune.ym");
    CHECK(h.editor.sndhFile == "/t/tune.ym");
    // The tune is read on the worker thread, then the grid dialogue shows the guess.
    waitExport(w, now);
    CHECK(alertText(w) == "Import the YM file as patterns\ntune.ym\nGrid guessed from 16 notes: 100 % of them start "
                          "on a step.\nA frame is a replay tick of the tune (50 Hz).");
    gemui::Alert* a = topAlert(w);
    REQUIRE(a);
    REQUIRE(a->option(0, 3));
    CHECK(a->option(0, 3)->text() == "6");
    CHECK(a->option(1, 3)->text() == "64");
    CHECK(a->picks() == std::vector<int>{3, 3});
    // Cancel: nothing changes.
    alertButton(w, 0);
    CHECK_FALSE(topAlert(w));
    CHECK(h.state.instruments == before.instruments);
    CHECK(h.editor.undo.undoSize() == 0);

    fileMenu(w, "Import", "SNDH / YM file");
    CHECK(h.opens.back().path == "/t/tune.ym");
    h.pick("/t/tune.ym");
    waitExport(w, now);
    alertButton(w, 1);  // Import
    waitExport(w, now);
    CHECK(alertText(w) == "YM imported\n\ntune.ym\n3 tracks, 1 pattern, 1 instrument, 1 Song entry.\n"
                          "Ticks: speed 6 at 50 Hz, Authentic 3ch, A4 440.1 Hz.");
    dismiss(w);

    CHECK(h.p(Global::VoiceMode) == 1.0f);
    CHECK(h.p(Global::Mpe) == 0.0f);
    CHECK(h.p(Global::VblRate) == 0.0f);
    CHECK(h.p(Global::A4Hz) == 440.1f);  // period 284 plays 440.14 Hz
    for (int t = 0; t < plugin::kTracks; ++t) {
        CAPTURE(t);
        CHECK(h.p(t, Track::On) == (t < 3 ? 1.0f : 0.0f));
        if (t < 3) {
            CHECK(h.p(t, Track::Generator) == float(int(ymengine::Generator::Chip)));
            CHECK(h.p(t, Track::Level) == 100.0f);
        }
    }
    CHECK(h.state.instruments[0].used);
    CHECK(h.state.instruments[0].name != "mine");
    CHECK(h.state.global.timing == ymengine::SeqTiming::Ticks);
    CHECK(h.state.global.speed == 6);
    CHECK(h.state.global.grooveLength == 1);
    CHECK(h.state.seq.patterns[0].lane(0).ins[0] == 1);
    CHECK(h.state.seq.song.size() == 1);
    CHECK(h.editor.undo.undoSize() == 1);

    // One undo step: everything comes back together; then Redo.
    const plugin::NonParamState after = h.state;
    const plugin::ParamValues valuesAfter = h.values;
    w.tick(now += 10);
    undoKey(w);
    CHECK(h.state.seq.patterns[0].sameContent(before.seq.patterns[0]));
    CHECK(h.state.seq.song == before.seq.song);
    CHECK(h.state.instruments == before.instruments);
    CHECK(h.state.global.timing == before.global.timing);
    CHECK(h.state.global.speed == 4);
    CHECK(h.state.global.grooveLength == 2);
    CHECK(h.state.global.groove == before.global.groove);
    for (int i = 0; i < plugin::kParamCount; ++i) {
        CAPTURE(i);
        CHECK(h.values[size_t(i)] == valuesBefore[size_t(i)]);
    }
    undoKey(w, true);
    CHECK(h.state.seq.patterns[0].sameContent(after.seq.patterns[0]));
    CHECK(h.state.instruments == after.instruments);
    CHECK(h.state.global.speed == 6);
    for (int i = 0; i < plugin::kParamCount; ++i) {
        CAPTURE(i);
        CHECK(h.values[size_t(i)] == valuesAfter[size_t(i)]);
    }

    // Refusals: alert with the reason, nothing changes, no undo step.
    const size_t steps = h.editor.undo.undoSize();
    fileMenu(w, "Import", "SNDH / YM file");
    h.pick("/t/bad.ym");
    CHECK(alertText(w) == "YM not imported\n\nbad.ym\nThe YM file is damaged.");
    dismiss(w);
    fileMenu(w, "Import", "SNDH / YM file");
    h.pick("/t/text.ym");
    CHECK(alertText(w) == "YM not imported\n\ntext.ym\nUnsupported file\n"
                          "Expected a YM register dump (YM2 / YM3 / YM5 / YM6) or an SNDH file.");
    dismiss(w);
    fileMenu(w, "Import", "SNDH / YM file");
    h.pick("/t/missing.ym");
    CHECK(alertText(w) == "The file could not be read.");
    dismiss(w);
    CHECK(h.state.instruments == after.instruments);
    CHECK(h.editor.undo.undoSize() == steps);
}

TEST_CASE("Import > SNDH / YM file, YM file: a tune without notes is refused after reading, a step chosen in the dialogue",
          "[ui][register-import]") {
    FileHost h;
    std::vector<std::array<uint8_t, 14>> quiet(50);
    for (auto& x : quiet) x[7] = 0x3f;
    h.disk["/t/quiet.ym"] = ym3(quiet);
    h.disk["/t/tune.ym"] = melody();
    MainWindow w(h);
    double now = 0;
    w.tick(now);
    fileMenu(w, "Import", "SNDH / YM file");
    h.pick("/t/quiet.ym");
    waitExport(w, now);
    CHECK(alertText(w) == "YM not imported\n\nquiet.ym\nThe tune plays no note.");
    dismiss(w);
    CHECK(h.editor.undo.undoSize() == 0);

    // Steps of 3 frames and patterns of 16 steps: two steps per note, 35 steps in three patterns.
    fileMenu(w, "Import", "SNDH / YM file");
    h.pick("/t/tune.ym");
    waitExport(w, now);
    gemui::Alert* a = topAlert(w);
    REQUIRE(a);
    REQUIRE(a->option(0, 1));
    CHECK(a->option(0, 1)->text() == "3");
    CHECK(a->option(1, 0)->text() == "16");
    a->option(0, 1)->onClick();
    a->option(1, 0)->onClick();
    alertButton(w, 1);
    waitExport(w, now);
    CHECK(alertText(w).find("YM imported") == 0);
    CHECK(alertText(w).find("Ticks: speed 3 at 50 Hz") != std::string::npos);
    dismiss(w);
    CHECK(h.state.global.speed == 3);
    CHECK(h.state.seq.patterns[0].steps == 16);
    CHECK(h.state.seq.song.size() == 3);
}

TEST_CASE("Import > SNDH / YM file, SNDH file: subsong asked, the Atari plays it, refusal of a routine without notes",
          "[ui][register-import]") {
    // Two subsongs; the play routine holds A4 on voice A (period 284), volume 15.
    test::SndhSource src;
    src.subsongs = 2;
    test::ymWrite(src.play, 0, 284 & 255);
    test::ymWrite(src.play, 1, 284 >> 8);
    test::ymWrite(src.play, 7, 0x3e);
    test::ymWrite(src.play, 8, 15);
    const std::vector<uint8_t> tone = test::buildSndh(src);
    test::SndhSource silent;
    silent.subsongs = 1;
    test::ymWrite(silent.play, 7, 0x3f);
    const std::vector<uint8_t> none = test::buildSndh(silent);
    FileHost h;
    h.disk["/s/tone.sndh"] = std::string(tone.begin(), tone.end());
    h.disk["/s/none.sndh"] = std::string(none.begin(), none.end());
    MainWindow w(h);
    double now = 0;
    w.tick(now);

    fileMenu(w, "Import", "SNDH / YM file");
    h.pick("/s/tone.sndh");
    CHECK(h.editor.sndhFile == "/s/tone.sndh");
    CHECK(alertText(w) == "Subsong to import\n(1 to 2):");
    // Cancel: nothing is read.
    pressKey(w, gemui::Key::Escape);
    CHECK_FALSE(topAlert(w));
    CHECK_FALSE(w.exporting());

    fileMenu(w, "Import", "SNDH / YM file");
    h.pick("/s/tone.sndh");
    dismiss(w);  // OK with the default subsong
    waitExport(w, now);
    REQUIRE(topAlert(w));
    CHECK(alertText(w).find("Import the SNDH file as patterns\ntone.sndh\n") == 0);
    alertButton(w, 1);
    waitExport(w, now);
    CHECK(alertText(w).find("SNDH imported\n\ntone.sndh\nTest\n") == 0);
    dismiss(w);
    CHECK(h.p(Global::VoiceMode) == 1.0f);
    CHECK(h.state.seq.patterns[0].lane(0).pat[0] != 0);
    CHECK(h.editor.undo.undoSize() == 1);

    fileMenu(w, "Import", "SNDH / YM file");
    h.pick("/s/none.sndh");
    waitExport(w, now);
    CHECK(alertText(w) == "SNDH not imported\n\nnone.sndh\nThe tune plays no note.");
    dismiss(w);
    CHECK(h.editor.undo.undoSize() == 1);
}

TEST_CASE("Import > Tracker song, maxYMiser song: voiceset beside the song, digidrums on track 4, one undo step, refusals",
          "[ui][register-import]") {
    FileHost h;
    h.disk["/m/Tune.MYS"] = mys();
    h.disk["/m/Tune.myv"] = myv();
    h.disk["/m/Lone.MYV"] = myv();
    h.disk["/m/text.mys"] = "not a song";
    h.disk["/m/Cut.MYS"] = "MYM0TRAK";
    h.disk["/m/Cut.MYV"] = myv();
    h.p(3, Track::On) = 0;
    h.p(3, Track::Level) = 50;
    h.state.global.digidrumChannel[0] = 0;
    h.state.digiSlots[0].name = "mine";
    const plugin::NonParamState before = h.state;
    const plugin::ParamValues valuesBefore = h.values;
    MainWindow w(h);
    double now = 0;
    w.tick(now);

    fileMenu(w, "Import", "Tracker song");
    REQUIRE(h.opens.size() == 1);
    CHECK(h.opens[0].title == "Import a tracker song");
    h.pick("/m/Tune.MYS");
    CHECK(h.editor.trackerFile == "/m/Tune.MYS");
    CHECK(h.opens.size() == 1);  // the voiceset of the same name is read without a dialogue
    waitExport(w, now);
    CHECK(alertText(w) == "maxYMiser song imported\n\nTune.MYS\n4 tracks, 1 pattern, 1 instrument, 1 Song entry.\n"
                          "Digidrum samples on track 4: 1.\nTicks: speed 3 at 50 Hz, Authentic 3ch, A4 440.0 Hz.");
    dismiss(w);
    CHECK(h.p(Global::VoiceMode) == 1.0f);
    for (int t = 0; t < plugin::kTracks; ++t) {
        CAPTURE(t);
        CHECK(h.p(t, Track::On) == (t < 4 ? 1.0f : 0.0f));
        if (t < 4) {
            CHECK(h.p(t, Track::Generator) ==
                  float(int(t == 3 ? ymengine::Generator::Digidrum : ymengine::Generator::Chip)));
            CHECK(h.p(t, Track::Level) == 100.0f);
        }
    }
    CHECK(h.state.global.speed == 3);
    CHECK(h.state.global.digidrumChannel[0] == 2);
    CHECK(h.state.digiSlots[0].name == "Digi 1");
    CHECK(h.state.digiSlots[0].source.size() == 1536);
    CHECK_FALSE(h.state.digidrums[0].empty());
    CHECK(h.state.seq.patterns[0].lane(3).pat[0] == 0);
    CHECK(h.state.seq.patterns[0].lane(3).pat[8] == 0);
    CHECK(h.editor.undo.undoSize() == 1);

    // One undo step: the Digidrum slots, their channel and track 4 come back too.
    w.tick(now += 10);
    undoKey(w);
    CHECK(h.state.digiSlots == before.digiSlots);
    CHECK(h.state.digidrums == before.digidrums);
    CHECK(h.state.global.digidrumChannel[0] == 0);
    CHECK(h.state.seq.song == before.seq.song);
    for (int i = 0; i < plugin::kParamCount; ++i) {
        CAPTURE(i);
        CHECK(h.values[size_t(i)] == valuesBefore[size_t(i)]);
    }
    undoKey(w, true);
    CHECK(h.state.digiSlots[0].name == "Digi 1");
    CHECK(h.p(3, Track::Generator) == float(int(ymengine::Generator::Digidrum)));

    // A voiceset without its song beside it: the song is asked for.
    fileMenu(w, "Import", "Tracker song");
    h.pick("/m/Lone.MYV");
    REQUIRE(h.opens.size() == 3);
    CHECK(h.opens[2].title == "Choose the song (.MYS) of the voiceset");
    CHECK(h.opens[2].patterns == "*.mys;*.MYS");
    h.pick("/m/Tune.MYS");
    waitExport(w, now);
    CHECK(alertText(w).find("maxYMiser song imported\n\nTune.MYS\n") == 0);
    dismiss(w);
    CHECK(h.editor.undo.undoSize() == 2);

    // Refusals: nothing changes, no undo step.
    const plugin::NonParamState after = h.state;
    fileMenu(w, "Import", "Tracker song");
    h.pick("/m/text.mys");
    CHECK(alertText(w) == "Tracker song not imported\n\ntext.mys\nExpected an XM, MOD, S3M, OKT, maxYMiser,\n"
                          "YM-Tracker or TTrak file.");
    dismiss(w);
    fileMenu(w, "Import", "Tracker song");
    h.pick("/m/Cut.MYS");
    CHECK(alertText(w).find("maxYMiser song not imported\n\nCut.MYS\n") == 0);
    dismiss(w);
    CHECK(h.state.digiSlots == after.digiSlots);
    CHECK(h.editor.undo.undoSize() == 2);
}

TEST_CASE("Import > SNDH / YM file: an SNDH saved by maxYMiser imports its song, the routine played if it is refused",
          "[ui][register-import]") {
    // The routine is silent (read as registers, it would be refused); the voiceset and the song follow it, as in the
    // SNDH files that maxYMiser saves.
    test::SndhSource silent;
    test::ymWrite(silent.play, 7, 0x3f);
    std::vector<uint8_t> saved = test::buildSndh(silent);
    for (const std::string& part : {myv(), mys()}) saved.insert(saved.end(), part.begin(), part.end());
    // A song without notes: the routine, which holds A4 on voice A, is read instead.
    test::SndhSource tone;
    test::ymWrite(tone.play, 0, 284 & 255);
    test::ymWrite(tone.play, 1, 284 >> 8);
    test::ymWrite(tone.play, 7, 0x3e);
    test::ymWrite(tone.play, 8, 15);
    std::vector<uint8_t> empty = test::buildSndh(tone);
    ymio::MysSong blank;
    blank.patterns.resize(1);
    blank.steps[0].channel = {0, 0, 0};
    const std::vector<uint8_t> blankBytes = ymio::writeMys(blank);
    for (const std::string& part : {myv(), std::string(blankBytes.begin(), blankBytes.end())})
        empty.insert(empty.end(), part.begin(), part.end());
    FileHost h;
    h.disk["/s/Saved.snd"] = std::string(saved.begin(), saved.end());
    h.disk["/s/Empty.snd"] = std::string(empty.begin(), empty.end());
    MainWindow w(h);
    double now = 0;
    w.tick(now);

    fileMenu(w, "Import", "SNDH / YM file");
    h.pick("/s/Saved.snd");
    waitExport(w, now);
    // No grid dialogue: the rows are the steps, as with Import > maxYMiser song.
    CHECK(alertText(w) == "maxYMiser song imported\n\nSaved.snd\n4 tracks, 1 pattern, 1 instrument, 1 Song entry.\n"
                          "Digidrum samples on track 4: 1.\nTicks: speed 3 at 50 Hz, Authentic 3ch, A4 440.0 Hz.");
    dismiss(w);
    CHECK(h.p(3, Track::Generator) == float(int(ymengine::Generator::Digidrum)));
    CHECK(h.state.digiSlots[0].source.size() == 1536);
    CHECK(h.editor.undo.undoSize() == 1);

    fileMenu(w, "Import", "SNDH / YM file");
    h.pick("/s/Empty.snd");
    waitExport(w, now);
    CHECK(alertText(w).find("Import the SNDH file as patterns\nEmpty.snd\n") == 0);
    alertButton(w, 1);
    waitExport(w, now);
    CHECK(alertText(w).find("SNDH imported\n\nEmpty.snd\n") == 0);
    dismiss(w);
    CHECK(h.editor.undo.undoSize() == 2);
}

TEST_CASE("Export song > maxYMiser: song and voiceset of the same name, alert, greyed outside Authentic 3ch",
          "[ui][register-import]") {
    FileHost h;
    h.disk["/m/Tune.MYS"] = mys();
    h.disk["/m/Tune.MYV"] = myv();
    MainWindow w(h);
    double now = 0;
    w.tick(now);
    fileMenu(w, "Import", "Tracker song");
    h.pick("/m/Tune.MYS");
    waitExport(w, now);
    dismiss(w);
    w.tick(now += 10);

    fileMenu(w, "Export song", "maxYMiser");
    REQUIRE(h.saves.size() == 1);
    CHECK(h.saves[0].title == "Export the song for maxYMiser");
    CHECK(h.saves[0].patterns == "*.mys;*.MYS");
    CHECK(h.saves[0].name == "STYM2149.MYS");
    h.pick("/m/BACK");
    CHECK(w.exporting());
    waitExport(w, now);
    const std::string text = alertText(w);
    CHECK(text.rfind("maxYMiser song exported\n\nBACK.mys + BACK.myv\n", 0) == 0);
    dismiss(w);
    REQUIRE(h.disk.count("/m/BACK.mys"));
    REQUIRE(h.disk.count("/m/BACK.myv"));
    auto bytes = [&](const std::string& f) {
        return std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(h.disk[f].data()), h.disk[f].size());
    };
    const auto song = ymio::parseMys(bytes("/m/BACK.mys"));
    const auto voices = ymio::parseMyv(bytes("/m/BACK.myv"));
    REQUIRE(song.value);
    REQUIRE(voices.value);
    CHECK(song.value->speed == 3);
    CHECK(voices.value->digidrums[0].size() == 1536);

    // An extension in capitals: the voiceset's too.
    fileMenu(w, "Export song", "maxYMiser");
    h.pick("/m/Up.MYS");
    waitExport(w, now);
    dismiss(w);
    CHECK(h.disk.count("/m/Up.MYS"));
    CHECK(h.disk.count("/m/Up.MYV"));

    // Write refused.
    fileMenu(w, "Export song", "maxYMiser");
    h.pick("/ro/x.mys");
    waitExport(w, now);
    CHECK(alertText(w) == "The file could not be written:\n/ro/x.mys");
    dismiss(w);

    // 16 voices: greyed.
    h.p(Global::VoiceMode) = 0;
    w.tick(now += 20);
    const gemui::MenuItem e = fileEntry(w, "Export song");
    bool found = false;
    for (const gemui::MenuItem& m : e.sub)
        if (m.label == "maxYMiser") {
            found = true;
            CHECK(m.disabled);
        }
    CHECK(found);
}

namespace {

// YM-Tracker song at speed 6 and 125 BPM: C-5, E-5 and G-5 on channel 1 with the instrument "Lead".
std::string ymt(bool notes = true) {
    ymio::YmtSong s;
    s.title = "Demo";
    s.channelNames = {"Ch 1", "Ch 2", "Ch 3", "Ch 4"};
    s.order = {0};
    s.patterns.resize(1);
    s.patterns[0].rows = 16;
    s.patterns[0].cells.assign(16 * 4, ymio::YmtCell{});
    if (notes)
        for (int k = 0; k < 3; ++k) s.patterns[0].cells[size_t(4 * 4 * k)] = {uint8_t(37 + 4 * k - (k == 2)), 1, 0, 0, 0};
    s.instruments.resize(2);
    s.instruments[0].name = "--";
    s.instruments[1].name = "Lead";
    const std::vector<uint8_t> b = ymio::writeYmt(s);
    return std::string(b.begin(), b.end());
}

// A TTrak module: instrument 1 a square at volume 15, notes C-4, E-4 and G-4 (TTrak notes 37, 41 and 44) on channel
// A, four rows apart, at speed 2 (three ticks a row).
std::string tpm(bool notes = true) {
    ymio::TtrakModule m = ymio::TtrakModule::blank();
    m.setTitle("Demo");
    m.instruments[0].at(ymio::TtrakInstrument::kAmpEnv) = 0x0F;
    m.instruments[0].setName("Lead");
    m.patterns[0].rows[0][1] = {0, 0, 0x1C, 2};
    if (notes)
        for (int k = 0; k < 3; ++k) m.patterns[0].rows[size_t(4 * k)][0] = {uint8_t(37 + 4 * k - (k == 2)), 1, 0, 0};
    const std::vector<uint8_t> b = ymio::writeTtrakModule(m);
    return std::string(b.begin(), b.end());
}

}  // namespace

TEST_CASE("Import > Tracker song, TTrak module: patterns in Authentic 3ch, one undo step, refusals",
          "[ui][register-import]") {
    FileHost h;
    h.disk["/t/Demo.TPM"] = tpm();
    h.disk["/t/Empty.TPM"] = tpm(false);
    h.disk["/t/text.TPM"] = "GODMOD30";
    h.p(Global::VoiceMode) = 0;
    MainWindow w(h);
    double now = 0;
    w.tick(now);

    fileMenu(w, "Import", "Tracker song");
    REQUIRE(h.opens.size() == 1);
    CHECK(h.opens[0].patterns.find("*.tpm") != std::string::npos);
    h.pick("/t/Demo.TPM");
    waitExport(w, now);
    CHECK(alertText(w).rfind("TTrak module imported\n\nDemo.TPM\nDemo\n", 0) == 0);
    dismiss(w);
    CHECK(h.p(Global::VoiceMode) == 1.0f);
    CHECK(h.state.global.timing == ymengine::SeqTiming::Ticks);
    CHECK(h.state.global.speed == 3);
    const ymseq::Lane& a = h.state.seq.patterns[0].lane(0);
    CHECK(ymseq::absoluteNote(a, 1, 0, h.state.seq.tracks[0].octave) == 60);
    CHECK(ymseq::absoluteNote(a, 1, 4, h.state.seq.tracks[0].octave) == 64);
    CHECK(ymseq::absoluteNote(a, 1, 8, h.state.seq.tracks[0].octave) == 67);
    CHECK(h.editor.undo.undoSize() == 1);
    w.tick(now += 10);
    undoKey(w);
    CHECK(h.p(Global::VoiceMode) == 0.0f);

    // Refusals: nothing changes, no undo step.
    fileMenu(w, "Import", "Tracker song");
    h.pick("/t/text.TPM");
    CHECK(alertText(w) == "TTrak module not imported\n\ntext.TPM\nThe TTrak module is cut.");
    dismiss(w);
    fileMenu(w, "Import", "Tracker song");
    h.pick("/t/Empty.TPM");
    waitExport(w, now);
    CHECK(alertText(w).rfind("TTrak module not imported\n\nEmpty.TPM\n", 0) == 0);
    dismiss(w);
    CHECK(h.p(Global::VoiceMode) == 0.0f);
    CHECK(h.editor.undo.undoSize() == 0);

    // Play: converted on the side.
    fileMenuPath(w, {"Play..."});
    CHECK(h.opens.back().patterns.find("*.tpm") != std::string::npos);
    h.pick("/t/Demo.TPM");
    waitExport(w, now);
    CHECK(alertText(w).empty());
    REQUIRE(h.played.size() == 1);
    CHECK(h.played[0].name == "Demo.TPM  -  TTrak");
    CHECK(ymseq::absoluteNote(h.played[0].extras.seq.patterns[0].lane(0), 1, 4,
                              h.played[0].extras.seq.tracks[0].octave) == 64);
    fileMenuPath(w, {"Play..."});
    h.pick("/t/text.TPM");
    CHECK(alertText(w) == "The file cannot be played.\n\ntext.TPM\nThe TTrak module is cut.");
    dismiss(w);
    CHECK(h.editor.undo.undoSize() == 0);
}

TEST_CASE("Export song > TTrak: the module titled after its file, alert, greyed outside Authentic 3ch",
          "[ui][register-import]") {
    FileHost h;
    h.disk["/t/Demo.TPM"] = tpm();
    MainWindow w(h);
    double now = 0;
    w.tick(now);
    fileMenu(w, "Import", "Tracker song");
    h.pick("/t/Demo.TPM");
    waitExport(w, now);
    dismiss(w);
    w.tick(now += 10);

    fileMenu(w, "Export song", "TTrak");
    REQUIRE(h.saves.size() == 1);
    CHECK(h.saves[0].title == "Export the song for TTrak");
    CHECK(h.saves[0].patterns == "*.tpm;*.TPM");
    CHECK(h.saves[0].name == "STYM2149.TPM");
    h.pick("/t/Back");
    CHECK(w.exporting());
    waitExport(w, now);
    const std::string text = alertText(w);
    CHECK(text.rfind("TTrak module exported\n\nBack.tpm\n", 0) == 0);
    CHECK(text.find("50 Hz, loops at position 1.") != std::string::npos);
    dismiss(w);
    REQUIRE(h.disk.count("/t/Back.tpm"));
    const std::string& f = h.disk["/t/Back.tpm"];
    const ymio::TtrakResult<ymio::TtrakModule> back =
        ymio::parseTtrakModule(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(f.data()), f.size()));
    REQUIRE(back.value);
    CHECK(back.value->titleText() == "Back");
    const ymio::TtrakPattern& p = back.value->patterns[back.value->order[0]];
    CHECK(p.rows[0][0].note == 37);
    CHECK(p.rows[0][0].instrument >= 1);
    CHECK(p.rows[4][0].note == 41);
    CHECK(p.rows[8][0].note == 44);

    // Write refused.
    fileMenu(w, "Export song", "TTrak");
    h.pick("/ro/x.tpm");
    waitExport(w, now);
    CHECK(alertText(w) == "The file could not be written:\n/ro/x.tpm");
    dismiss(w);

    // 16 voices: greyed.
    h.p(Global::VoiceMode) = 0;
    w.tick(now += 20);
    const gemui::MenuItem e = fileEntry(w, "Export song");
    bool found = false;
    for (const gemui::MenuItem& m : e.sub)
        if (m.label == "TTrak") {
            found = true;
            CHECK(m.disabled);
            CHECK(m.why == "TTrak plays the three channels of one chip: switch to Authentic 3ch");
        }
    CHECK(found);
}

TEST_CASE("Import > Tracker song, YM-Tracker song: patterns in Authentic 3ch, one undo step, refusals",
          "[ui][register-import]") {
    FileHost h;
    h.disk["/t/Demo.ymt"] = ymt();
    h.disk["/t/Empty.ymt"] = ymt(false);
    h.disk["/t/text.ymt"] = std::string("YMT1\x01\x01\x08\x00", 8);  // its gzip member missing
    h.p(Global::VoiceMode) = 0;
    MainWindow w(h);
    double now = 0;
    w.tick(now);

    fileMenu(w, "Import", "Tracker song");
    REQUIRE(h.opens.size() == 1);
    CHECK(h.opens[0].title == "Import a tracker song");
    h.pick("/t/Demo.ymt");
    CHECK(h.editor.trackerFile == "/t/Demo.ymt");
    waitExport(w, now);
    CHECK(alertText(w) == "YM-Tracker song imported\n\nDemo.ymt\nDemo\n3 tracks, 1 pattern, 1 instrument, 1 Song entry.\n"
                          "Ticks: speed 6 at 50 Hz, Authentic 3ch.");
    dismiss(w);
    CHECK(h.p(Global::VoiceMode) == 1.0f);
    for (int t = 0; t < 3; ++t) CHECK(h.p(t, Track::On) == 1.0f);
    CHECK(h.p(3, Track::On) == 0.0f);
    CHECK(h.state.global.timing == ymengine::SeqTiming::Ticks);
    CHECK(h.state.global.speed == 6);
    const ymseq::Lane& a = h.state.seq.patterns[0].lane(0);
    CHECK(ymseq::absoluteNote(a, 1, 0, h.state.seq.tracks[0].octave) == 60);
    CHECK(ymseq::absoluteNote(a, 1, 4, h.state.seq.tracks[0].octave) == 64);
    CHECK(ymseq::absoluteNote(a, 1, 8, h.state.seq.tracks[0].octave) == 67);
    CHECK(h.editor.undo.undoSize() == 1);
    w.tick(now += 10);
    undoKey(w);
    CHECK(h.p(Global::VoiceMode) == 0.0f);
    CHECK(h.state.seq.patterns[0].lane(0).pat[0] < 0);

    // Refusals: nothing changes, no undo step.
    fileMenu(w, "Import", "Tracker song");
    h.pick("/t/text.ymt");
    CHECK(alertText(w) == "YM-Tracker song not imported\n\ntext.ymt\nNot a YM-Tracker song.");
    dismiss(w);
    fileMenu(w, "Import", "Tracker song");
    h.pick("/t/Empty.ymt");
    waitExport(w, now);
    CHECK(alertText(w) ==
          "YM-Tracker song not imported\n\nEmpty.ymt\nNothing to import: no notes on the first three channels.");
    dismiss(w);
    CHECK(h.p(Global::VoiceMode) == 0.0f);
    CHECK(h.editor.undo.undoSize() == 0);
}

TEST_CASE("Export song > YM-Tracker: the song named after its file, alert, greyed outside Authentic 3ch",
          "[ui][register-import]") {
    FileHost h;
    h.disk["/t/Demo.ymt"] = ymt();
    MainWindow w(h);
    double now = 0;
    w.tick(now);
    fileMenu(w, "Import", "Tracker song");
    h.pick("/t/Demo.ymt");
    waitExport(w, now);
    dismiss(w);
    w.tick(now += 10);

    fileMenu(w, "Export song", "YM-Tracker");
    REQUIRE(h.saves.size() == 1);
    CHECK(h.saves[0].title == "Export the song for YM-Tracker");
    CHECK(h.saves[0].patterns == "*.ymt;*.YMT");
    CHECK(h.saves[0].name == "STYM2149.ymt");
    h.pick("/t/Back");
    CHECK(w.exporting());
    waitExport(w, now);
    CHECK(alertText(w) == "YM-Tracker song exported\n\nBack.ymt\n1 position, 1 pattern, 1 instrument.\n"
                          "Speed 6 at 125 BPM, loops at position 1.");
    dismiss(w);
    REQUIRE(h.disk.count("/t/Back.ymt"));
    const std::string& f = h.disk["/t/Back.ymt"];
    const ymio::YmtParseResult back =
        ymio::parseYmt(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(f.data()), f.size()));
    REQUIRE(back.song);
    CHECK(back.song->title == "Back");
    CHECK(back.song->cell(0, 0, 0).note == 37);
    CHECK(back.song->cell(0, 4, 0).note == 41);

    // Write refused.
    fileMenu(w, "Export song", "YM-Tracker");
    h.pick("/ro/x.ymt");
    waitExport(w, now);
    CHECK(alertText(w) == "The file could not be written:\n/ro/x.ymt");
    dismiss(w);

    // 16 voices: greyed.
    h.p(Global::VoiceMode) = 0;
    w.tick(now += 20);
    const gemui::MenuItem e = fileEntry(w, "Export song");
    bool found = false;
    for (const gemui::MenuItem& m : e.sub)
        if (m.label == "YM-Tracker") {
            found = true;
            CHECK(m.disabled);
        }
    CHECK(found);
}

TEST_CASE("File > Play... of a maxYMiser or a YM-Tracker song: converted on the side, the project untouched",
          "[ui][register-import]") {
    FileHost h;
    h.disk["/m/Tune.MYS"] = mys();
    h.disk["/m/Tune.myv"] = myv();
    h.disk["/m/Cut.MYS"] = "MYM0TRAK";
    h.disk["/m/Cut.MYV"] = myv();
    h.disk["/t/Demo.ymt"] = ymt();
    h.disk["/t/Empty.ymt"] = ymt(false);
    h.disk["/t/Cut.ymt"] = std::string("YMT1\x01\x01\x08\x00", 8);
    h.p(Global::VoiceMode) = 0;
    h.state.digiSlots[0].name = "mine";
    const plugin::NonParamState before = h.state;
    const plugin::ParamValues valuesBefore = h.values;
    MainWindow w(h);
    double now = 0;
    w.tick(now);

    // maxYMiser: the voiceset of the same name read without a dialogue, as by Import.
    fileMenuPath(w, {"Play..."});
    h.pick("/m/Tune.MYS");
    CHECK(h.opens.size() == 1);
    waitExport(w, now);
    CHECK(alertText(w).empty());
    REQUIRE(h.played.size() == 1);
    const FileHost::Played& mym = h.played[0];
    CHECK(mym.name == "Tune.MYS  -  maxYMiser");
    CHECK(mym.song);
    CHECK(mym.values[size_t(paramIndex(Global::VoiceMode))] == 1.0f);  // Authentic 3ch, as the import
    CHECK(mym.values[size_t(paramIndex(3, Track::Generator))] == float(int(ymengine::Generator::Digidrum)));
    CHECK(mym.extras.digiSlots[0].name == "Digi 1");
    CHECK(mym.extras.global.speed == 3);
    CHECK(w.ymPlayerWindow().isOpen());

    // YM-Tracker.
    fileMenuPath(w, {"Play..."});
    CHECK(h.opens.back().path == "/m/Tune.MYS");
    h.pick("/t/Demo.ymt");
    waitExport(w, now);
    CHECK(alertText(w).empty());
    REQUIRE(h.played.size() == 2);
    CHECK(h.played[1].name == "Demo.ymt  -  YM-Tracker");
    CHECK(ymseq::absoluteNote(h.played[1].extras.seq.patterns[0].lane(0), 1, 4,
                              h.played[1].extras.seq.tracks[0].octave) == 64);

    // Refusals: the reason of the conversion, nothing played.
    fileMenuPath(w, {"Play..."});
    h.pick("/m/Cut.MYS");
    CHECK(alertText(w).find("The file cannot be played.\n\nCut.MYS\n") == 0);
    dismiss(w);
    fileMenuPath(w, {"Play..."});
    h.pick("/t/Empty.ymt");
    waitExport(w, now);
    CHECK(alertText(w) ==
          "The file cannot be played.\n\nEmpty.ymt\nNothing to import: no notes on the first three channels.");
    dismiss(w);
    fileMenuPath(w, {"Play..."});
    h.pick("/t/Cut.ymt");
    CHECK(alertText(w) == "The file cannot be played.\n\nCut.ymt\nNot a YM-Tracker song.");
    dismiss(w);
    CHECK(h.played.size() == 2);

    // Nothing of the project changed, no undo step, no import path remembered.
    CHECK(h.state.digiSlots == before.digiSlots);
    CHECK(h.state.seq.patterns[0].sameContent(before.seq.patterns[0]));
    CHECK(h.values == valuesBefore);
    CHECK(h.editor.undo.undoSize() == 0);
    CHECK(h.editor.trackerFile.empty());
}
