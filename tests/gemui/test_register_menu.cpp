// STYM2149 — register exports: Export song > VBL dump, YM3, YM6 and SNDH
// (Authentic 3ch only, empty Song refused, worker thread under the bee, alert with what the file does not render),
// and export of the take from the Recorder window.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "gemui/Menu.h"
#include "plugincore/Parameters.h"
#include "plugincore/RegisterFiles.h"
#include "pluginui/MainWindow.h"
#include "ymio/YmFile.h"

#include "FileHost.h"

using namespace stym;
using namespace stym::testing;
using pluginui::MainWindow;
using plugin::Global;
using plugin::RegisterFormat;
using plugin::Track;

namespace {

void note(ymseq::Lane& l, int step, int midi) {
    const int oc = midi / 12 - 1;
    l.pat[size_t(step)] = int16_t(midi - (oc + 1) * 12);
    l.oct[size_t(step)] = int8_t(ymseq::octaveCode(oc, 3));
}

// Authentic 3ch, 16-step pattern at 120 BPM (2 s), Song of one entry; channel B as SID.
void project(FileHost& h) {
    h.p(Global::VoiceMode) = 1;
    h.p(Global::FreeTempo) = 120;
    h.state.seq.cur().steps = 16;
    note(h.state.seq.cur().edit(0), 0, 60);
    note(h.state.seq.cur().edit(0), 8, 67);
    note(h.state.seq.cur().edit(1), 4, 64);
    h.p(1, Track::On) = 1;
    h.p(1, Track::Generator) = float(int(ymengine::Generator::Sid));
    h.state.seq.song.entries = {{0, 1, 0}};
}

// Entries of the Export song submenu.
std::vector<gemui::MenuItem> songMenu(MainWindow& w) {
    w.openMenu(1);
    const auto* list = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
    REQUIRE(list);
    std::vector<gemui::MenuItem> out;
    for (const gemui::MenuItem& m : list->items())
        if (m.label == "Export song") out = m.sub;
    w.desktop().closePopups();
    return out;
}

std::string expected(const FileHost& h, RegisterFormat f, const std::string& name) {
    plugin::RegisterFileOptions o;
    o.name = name;
    const auto r = plugin::exportSongRegisters(f, h.values, h.state, o);
    REQUIRE(r);
    REQUIRE(r->error.empty());
    return {r->bytes.begin(), r->bytes.end()};
}

void press(MainWindow& w, const std::string& id) {
    const auto r = w.recorderWindow().rectOf(id);
    REQUIRE(r);
    const gemui::Point at = centre(*r);
    w.desktop().mouseMove(at, {});
    w.desktop().mouseDown(at, gemui::MouseButton::Left, {});
    w.desktop().mouseUp(at, {});
}

}  // namespace

TEST_CASE("Export song > VBL dump, YM3, YM6, SNDH: Authentic 3ch, files, alerts", "[ui][regexport]") {
    FileHost h;
    project(h);
    MainWindow w(h);
    double now = 0;
    w.tick(now);

    // The prototype's regSub: register formats, separator, then MIDI, WAV and Stems; YM6 added.
    std::vector<gemui::MenuItem> menu = songMenu(w);
    REQUIRE(menu.size() == 8);
    const std::vector<std::string> labels = {"VBL dump", "YM3", "YM6", "SNDH"};
    for (size_t i = 0; i < 4; ++i) {
        CHECK(menu[i].label == labels[i]);
        CHECK_FALSE(menu[i].disabled);
    }
    CHECK(menu[4].separator);
    // 16 voices and dual chip: greyed out.
    for (const float mode : {0.0f, 2.0f}) {
        h.p(Global::VoiceMode) = mode;
        w.tick(now += 20);
        menu = songMenu(w);
        for (size_t i = 0; i < 4; ++i) {
            CHECK(menu[i].disabled);
            CHECK(menu[i].why == "Register dumps carry three YM voices: switch to Authentic 3ch");
        }
        CHECK_FALSE(menu[5].disabled);  // MIDI
    }
    h.p(Global::VoiceMode) = 1;
    w.tick(now += 20);

    // Empty Song: the prototype's requireSong.
    h.state.seq.song.entries.clear();
    ++h.serial;
    w.tick(now += 20);
    fileMenu(w, "Export song", "SNDH");
    CHECK(alertText(w) == "Song is empty. Add entries first.");
    dismiss(w);
    CHECK(h.saves.empty());
    h.state.seq.song.entries = {{0, 1, 0}};
    ++h.serial;
    w.tick(now += 20);

    struct Case {
        const char* label;
        RegisterFormat format;
        const char* title;
        const char* patterns;
        const char* name;
        const char* pick;
        const char* file;
        const char* done;
    };
    const Case cases[] = {
        {"VBL dump", RegisterFormat::Vbl, "Export the song as a VBL dump", "*.s;*.asm", "stym2149-song.s", "/x/dump",
         "/x/dump.s", "VBL dump exported"},
        {"YM3", RegisterFormat::Ym3, "Export the song as YM3", "*.ym", "stym2149-song.ym", "/x/old.ym", "/x/old.ym",
         "YM3 exported"},
        {"YM6", RegisterFormat::Ym6, "Export the song as YM6", "*.ym", "stym2149-song.ym", "/x/tune", "/x/tune.ym",
         "YM6 exported"},
        {"SNDH", RegisterFormat::Sndh, "Export the song as SNDH", "*.sndh;*.snd", "stym2149-song.sndh", "/x/tune",
         "/x/tune.sndh", "SNDH exported"},
    };
    for (const Case& c : cases) {
        CAPTURE(c.label);
        const size_t before = h.saves.size();
        fileMenu(w, "Export song", c.label);
        REQUIRE(h.saves.size() == before + 1);
        CHECK(h.saves.back().title == c.title);
        CHECK(h.saves.back().patterns == c.patterns);
        CHECK(h.saves.back().name == c.name);
        h.pick(c.pick);
        CHECK(w.exporting());
        CHECK(w.desktop().busy());
        waitExport(w, now);
        CHECK_FALSE(w.desktop().busy());
        const std::string text = alertText(w);
        const std::string base = std::string(c.file).substr(3);
        CHECK(text.rfind(std::string(c.done) + "\n\n" + base + "\n100 frames at 50 Hz, 0:02.", 0) == 0);
        // VBL and YM3 freeze the SID of channel B; YM6 and SNDH play it.
        const bool frozen = c.format == RegisterFormat::Vbl || c.format == RegisterFormat::Ym3;
        CHECK((text.find("Timer effects frozen") != std::string::npos) == frozen);
        dismiss(w);
        REQUIRE(h.disk.count(c.file));
        if (c.format == RegisterFormat::Ym6) {
            // LHA archive dated at the export: the content read back is the expected one.
            const std::string want = expected(h, c.format, base);
            const auto a = ymio::parseYm({reinterpret_cast<const uint8_t*>(h.disk[c.file].data()), h.disk[c.file].size()});
            const auto b = ymio::parseYm({reinterpret_cast<const uint8_t*>(want.data()), want.size()});
            REQUIRE(a.file);
            REQUIRE(b.file);
            CHECK(a.file->frames == b.file->frames);
            CHECK(h.disk[c.file].find(base) != std::string::npos);  // name kept in the archive
        } else {
            CHECK(h.disk[c.file] == expected(h, c.format, base));
        }
    }

    // Write refused.
    fileMenu(w, "Export song", "YM3");
    h.pick("/ro/x.ym");
    waitExport(w, now);
    CHECK(alertText(w) == "The file could not be written:\n/ro/x.ym");
    dismiss(w);
}

TEST_CASE("Recorder: export of the take as YM6, SNDH and VBL", "[ui][regexport][recorder]") {
    FileHost h;
    h.p(Global::VoiceMode) = 1;
    MainWindow w(h);
    double now = 0;
    w.tick(now);
    w.recorderWindow().show();
    // Without a take: buttons greyed out.
    for (const char* id : {"recYm6", "recSndh", "recVbl"}) {
        CAPTURE(id);
        REQUIRE(w.recorderWindow().rectOf(id));
        press(w, id);
        CHECK(h.saves.empty());
    }
    // Take of 100 frames at 50 Hz: channel A, C then G.
    h.take.frameHz = 50;
    h.take.frames.resize(100);
    for (size_t i = 0; i < h.take.frames.size(); ++i) {
        ymengine::RegisterFrame& f = h.take.frames[i];
        f.reg = {uint8_t(i < 50 ? 0xDE : 0x95), 1, 0, 0, 0, 0, 0, 0x3E, 12, 0, 0, 0, 0, 0};
    }
    h.rec.takeFrames = 100;
    w.tick(now += 20);

    struct Case {
        const char* id;
        RegisterFormat format;
        const char* title;
        const char* name;
        const char* file;
        const char* done;
    };
    const Case cases[] = {
        {"recYm6", RegisterFormat::Ym6, "Export the take as YM6", "stym2149-take.ym", "/x/take.ym", "YM6 exported"},
        {"recSndh", RegisterFormat::Sndh, "Export the take as SNDH", "stym2149-take.sndh", "/x/take.sndh",
         "SNDH exported"},
        {"recVbl", RegisterFormat::Vbl, "Export the take as a VBL dump", "stym2149-take.s", "/x/take.s",
         "VBL dump exported"},
    };
    for (const Case& c : cases) {
        CAPTURE(c.id);
        const size_t before = h.saves.size();
        press(w, c.id);
        REQUIRE(h.saves.size() == before + 1);
        CHECK(h.saves.back().title == c.title);
        CHECK(h.saves.back().name == c.name);
        h.pick(c.file);
        waitExport(w, now);
        const std::string base = std::string(c.file).substr(3);
        CHECK(alertText(w) == std::string(c.done) + "\n\n" + base + "\n100 frames at 50 Hz, 0:02.");
        dismiss(w);
        REQUIRE(h.disk.count(c.file));
        plugin::RegisterFileOptions o;
        o.name = base;
        const plugin::RegisterFile want = plugin::exportTake(c.format, h.take, h.values, h.state, o);
        if (c.format == RegisterFormat::Ym6) {
            const auto a = ymio::parseYm({reinterpret_cast<const uint8_t*>(h.disk[c.file].data()), h.disk[c.file].size()});
            const auto b = ymio::parseYm(want.bytes);
            REQUIRE(a.file);
            REQUIRE(b.file);
            CHECK(a.file->frames == b.file->frames);
        } else {
            CHECK(h.disk[c.file] == std::string(want.bytes.begin(), want.bytes.end()));
        }
    }
}
