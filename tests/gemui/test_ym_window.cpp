// STYM2149 — YM PLAYER window (M7, A2, D-169): File > Import > YM dump, alerts for a refused file, Play and Stop,
// Rewind, position read again in steps of 8 frames, subsong of an SNDH, register monitor that follows the tune,
// closing that stops playback and forgets the tune, window kept by the session.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "gemui/Alert.h"
#include "gemui/Menu.h"
#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"

using namespace stym;
using pluginui::MainWindow;
using pluginui::YmPlayerWindow;

namespace {

class YmHost : public pluginui::Host {
public:
    plugin::ParamValues values = plugin::defaultValues();
    plugin::NonParamState state;
    pluginui::EditorSession editor;
    std::map<std::string, std::string> disk;
    std::vector<FileDialog> opens;
    std::function<void(const std::string&)> pending;
    // Player: tune loaded if `refuse` is empty; calls kept.
    std::string refuse;
    YmPlayer ym;
    std::vector<std::string> loads;
    std::vector<bool> plays;
    std::vector<int> subsongs;
    int rewinds = 0, closes = 0;

    float param(int i) const override { return values[size_t(i)]; }
    void beginGesture(int) override {}
    void setParam(int i, float v) override { values[size_t(i)] = plugin::clampToSpec(plugin::paramSpec(i), v); }
    void endGesture(int) override {}
    uint64_t extrasVersion() const override { return 1; }
    plugin::NonParamState extras() const override { return state; }
    void editExtras(const std::function<void(plugin::NonParamState&)>& edit) override { edit(state); }
    void screenMidi(uint8_t, uint8_t, uint8_t) override {}
    void setMidiCapture(const plugin::MidiKeys&, bool) override {}
    bool learnMidi(std::array<uint8_t, 3>&) override { return false; }
    bool hostNote(std::array<uint8_t, 3>&) override { return false; }
    void seqCommand(SeqCommand, int, int, int) override {}
    const plugin::EngineStatus* engineStatus() override { return nullptr; }
    pluginui::EditorSession& session() override { return editor; }
    bool standalone() const override { return false; }
    std::string midiStatus() const override { return "none"; }
    std::string version() const override { return "test"; }
    void quit() override {}
    bool hasAudioSettings() const override { return false; }
    void openAudioSettings() override {}
    void loadDigidrum(int) override {}
    std::string preference(std::string_view, std::string_view fallback) const override { return std::string(fallback); }
    void setPreference(std::string_view, std::string_view) override {}
    std::string patchLibrary() const override { return {}; }
    void setPatchLibrary(const std::string&) override {}
    void chooseFileToOpen(const FileDialog& d, std::function<void(const std::string&)> done) override {
        opens.push_back(d);
        pending = std::move(done);
    }
    std::optional<std::string> readFile(const std::string& path, size_t maxBytes) const override {
        const auto it = disk.find(path);
        if (it == disk.end() || it->second.size() > maxBytes) return std::nullopt;
        return it->second;
    }

    std::string loadYm(std::string_view bytes, const std::string& fileName) override {
        loads.push_back(fileName + ":" + std::string(bytes));
        if (!refuse.empty()) return refuse;
        ym = {};
        ym.loaded = true;
        ym.name = fileName + "  -  YM3!";
        ym.frames = 200;
        return {};
    }
    YmPlayer ymPlayer() const override { return ym; }
    void ymPlay(bool on) override {
        plays.push_back(on);
        ym.playing = on;
    }
    void ymRewind() override {
        ++rewinds;
        ym.playing = true;
        ym.position = 0;
    }
    void ymSubsong(int s) override {
        subsongs.push_back(s);
        ym.subsong = s;
        ym.position = 0;
    }
    void ymClose() override {
        ++closes;
        ym = {};
    }

    void pick(const std::string& path) {
        REQUIRE(pending);
        auto done = std::move(pending);
        pending = nullptr;
        done(path);
    }
};

gemui::Point centre(const gemui::Rect& r) { return {r.x + r.w / 2, r.y + r.h / 2}; }

void click(MainWindow& w, const std::string& id) {
    const auto r = w.rectOf(id);
    REQUIRE(r);
    const gemui::Point at = centre(*r);
    w.desktop().mouseMove(at, {});
    w.desktop().mouseDown(at, gemui::MouseButton::Left, {});
    w.desktop().mouseUp(at, {});
}

void importYm(MainWindow& w) {
    w.openMenu(1);
    const auto* list = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
    REQUIRE(list);
    std::function<void()> pick;
    for (const gemui::MenuItem& m : list->items())
        if (m.label == "Import")
            for (const gemui::MenuItem& s : m.sub)
                if (s.label == "YM dump" && !s.disabled) pick = s.pick;
    w.desktop().closePopups();
    REQUIRE(pick);
    pick();
}

std::string alertText(MainWindow& w) {
    const auto* a = dynamic_cast<const gemui::Alert*>(w.desktop().topModal());
    return a ? a->text() : std::string();
}

void dismiss(MainWindow& w) {
    gemui::KeyEvent e;
    e.key = gemui::Key::Enter;
    w.desktop().keyDown(e);
}

}  // namespace

TEST_CASE("YM Player: position of the prototype (ymSetPos)", "[gemui][ymplayer]") {
    CHECK(YmPlayerWindow::positionText(0, 200, 50) == "1 / 200 (4.0 s)");
    CHECK(YmPlayerWindow::positionText(199, 200, 50) == "200 / 200 (4.0 s)");
    CHECK(YmPlayerWindow::positionText(7, 1000, 60) == "8 / 1000 (16.7 s)");
    CHECK(YmPlayerWindow::positionText(15, 0, 50) == "16 / ? (? s)");  // SNDH without a duration
}

TEST_CASE("YM Player: Import > YM dump, Play and Stop, Rewind, closing", "[gemui][ymplayer]") {
    YmHost h;
    h.disk["/music/Mad Max/song.ym"] = "YM3!data";
    MainWindow w(h);
    w.tick(0);
    YmPlayerWindow& y = w.ymPlayerWindow();
    CHECK_FALSE(y.isOpen());

    importYm(w);
    REQUIRE(h.opens.size() == 1);
    CHECK(h.opens[0].title == "Import a YM dump");
    CHECK(h.opens[0].patterns == "*.ym;*.ym2;*.ym3;*.ym5;*.ym6;*.snd;*.sndh");
    CHECK(h.opens[0].path.empty());
    h.pick("/music/Mad Max/song.ym");
    REQUIRE(h.loads == std::vector<std::string>{"song.ym:YM3!data"});
    REQUIRE(y.isOpen());
    CHECK(y.nameText() == "song.ym - YM3!");  // spaces collapsed as in the page
    CHECK(y.posText() == "1 / 200 (4.0 s)");
    CHECK_FALSE(y.rectOf("ymSubPrev"));  // no subsong for a dump
    CHECK(w.rectOf("ymWin")->x == YmPlayerWindow::kX);
    CHECK(w.rectOf("ymWin")->w == YmPlayerWindow::kW);
    // The next dialogue proposes the same file.
    importYm(w);
    CHECK(h.opens.back().path == "/music/Mad Max/song.ym");
    h.pending = nullptr;

    // Play: playback, "Stop" button; the register monitor follows channel A of the tune.
    click(w, "ymPlay");
    CHECK(h.plays == std::vector<bool>{true});
    CHECK(y.playing());
    h.ym.position = 13;
    h.ym.regs[0] = 0x12;
    w.tick(200);
    CHECK(y.posText() == "9 / 200 (4.0 s)");  // read again every 8 frames
    CHECK(w.registerInfo() == "CH A / YM PLAYER");
    h.ym.position = 15;
    w.tick(250);
    CHECK(y.posText() == "9 / 200 (4.0 s)");
    h.ym.position = 16;
    w.tick(300);
    CHECK(y.posText() == "17 / 200 (4.0 s)");

    // Stop: the monitor goes back to the focused track.
    click(w, "ymPlay");
    CHECK(h.plays == std::vector<bool>{true, false});
    CHECK_FALSE(y.playing());
    w.tick(500);
    CHECK(w.registerInfo() != "CH A / YM PLAYER");

    // Rewind: restarts from the beginning.
    click(w, "ymRew");
    CHECK(h.rewinds == 1);
    CHECK(y.playing());
    CHECK(y.posText() == "1 / 200 (4.0 s)");

    // Closing (close box: close()): playback stopped, tune forgotten.
    y.close();
    CHECK(h.closes == 1);
    CHECK_FALSE(h.ym.loaded);
    CHECK_FALSE(y.isOpen());
    y.show();  // nothing to reopen
    CHECK_FALSE(y.isOpen());
}

TEST_CASE("YM Player: refused files", "[gemui][ymplayer]") {
    YmHost h;
    h.disk["/a/x.bin"] = "junk";
    MainWindow w(h);
    w.tick(0);

    // Not found or too big.
    importYm(w);
    h.pick("/a/missing.ym");
    CHECK(alertText(w) == "The file could not be read.");
    dismiss(w);
    CHECK(h.loads.empty());

    // Unknown format: alert of the prototype, without the mention of PRISM.
    h.refuse = "Unsupported file";
    importYm(w);
    h.pick("/a/x.bin");
    CHECK(alertText(w) == "Unsupported file\n\nExpected a YM register dump (YM2 / YM3 / YM5 / YM6) or an SNDH file.");
    dismiss(w);
    CHECK_FALSE(w.ymPlayerWindow().isOpen());

    // Other refusal: the player's text.
    h.refuse = "Unsupported chip clock: 20000000 Hz.";
    importYm(w);
    h.pick("/a/x.bin");
    CHECK(alertText(w) == "Unsupported chip clock: 20000000 Hz.");
    dismiss(w);
    CHECK_FALSE(w.ymPlayerWindow().isOpen());
}

TEST_CASE("YM Player: subsong of an SNDH, tune unloaded, session", "[gemui][ymplayer]") {
    YmHost h;
    h.disk["/a.ym"] = "YM3!";
    h.disk["/b.sndh"] = "SNDH";
    {
        MainWindow w(h);
        w.tick(0);
        YmPlayerWindow& y = w.ymPlayerWindow();
        importYm(w);
        h.pick("/a.ym");
        const double ymHeight = w.rectOf("ymWin")->h;
        y.close();
        CHECK(h.closes == 1);

        importYm(w);
        h.pick("/b.sndh");
        h.ym.sndh = true;
        h.ym.subsongs = 3;
        h.ym.frames = 0;
        w.tick(10);
        REQUIRE(y.isOpen());
        CHECK(y.posText() == "1 / ? (? s)");
        CHECK(y.subsongText() == "1 / 3");
        REQUIRE(y.rectOf("ymSubPrev"));
        CHECK(w.rectOf("ymWin")->h > ymHeight);  // one more row
        click(w, "ymSubPrev");                   // greyed out at the first
        CHECK(h.subsongs.empty());
        click(w, "ymSubNext");
        click(w, "ymSubNext");
        click(w, "ymSubNext");  // greyed out at the last
        CHECK(h.subsongs == std::vector<int>{2, 3});
        CHECK(y.subsongText() == "3 / 3");
        click(w, "ymSubPrev");
        CHECK(y.subsongText() == "2 / 3");
    }
    // Window kept by the session as long as the tune is loaded.
    MainWindow again(h);
    again.tick(0);
    YmPlayerWindow& y = again.ymPlayerWindow();
    CHECK(y.isOpen());
    CHECK(y.subsongText() == "2 / 3");
    // Tune unloaded elsewhere (new processor instance): the window closes.
    h.ym = {};
    again.tick(20);
    CHECK_FALSE(y.isOpen());
}
