// STYM2149 — gemui tests: fake host that records what the editor sends (notes, sequencer commands, quit), and helpers
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Used by the tests of the main window, the sequencer and the GEM boxes; the mouse and the keyboard go through the
// desktop, as the plugin's editor sends them.
#pragma once
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gemui/Alert.h"
#include "gemui/Menu.h"
#include "plugincore/EngineStatus.h"
#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"

namespace stym::ui_testing {

using Midi = std::array<uint8_t, 3>;
inline constexpr uint8_t kNoteOn = 0x90, kNoteOff = 0x80;

class UiHost : public pluginui::Host {
public:
    struct Command {
        SeqCommand command;
        int a, b, c;
        bool operator==(const Command&) const = default;
    };

    plugin::ParamValues values = plugin::defaultValues();
    plugin::NonParamState state;
    uint64_t serial = 1;
    pluginui::EditorSession editor;
    std::vector<Midi> midi;           // notes played by the screen (screenMidi)
    std::vector<Command> commands;    // sequencer commands
    int quits = 0;
    std::map<std::string, std::string, std::less<>> prefs;
    std::string patches, digidrums, dmaBanks;
    std::string systemLang = "en";       // systemLanguage
    std::vector<TextFile> languages;     // .po files, the user's and the installed ones (D-283)
    std::vector<TextFile> folderFiles;   // files written when the folder was last opened
    int foldersOpened = 0;
    Recorder rec;                     // recorder state and kept take
    std::optional<plugin::EngineStatus> status;  // engine status handed to the next tick
    plugin::EngineStatus lastStatus;

    float param(int i) const override { return values[size_t(i)]; }
    void beginGesture(int) override {}
    void setParam(int i, float v) override { values[size_t(i)] = plugin::clampToSpec(plugin::paramSpec(i), v); }
    void endGesture(int) override {}
    uint64_t extrasVersion() const override { return serial; }
    plugin::NonParamState extras() const override { return state; }
    void editExtras(const std::function<void(plugin::NonParamState&)>& edit) override {
        edit(state);
        ++serial;
    }
    void screenMidi(uint8_t s, uint8_t d1, uint8_t d2) override { midi.push_back({s, d1, d2}); }
    void setMidiCapture(const plugin::MidiKeys&, bool) override {}
    bool learnMidi(std::array<uint8_t, 3>&) override { return false; }
    bool hostNote(std::array<uint8_t, 3>&) override { return false; }
    void seqCommand(SeqCommand c, int a, int b, int d) override { commands.push_back({c, a, b, d}); }
    const plugin::EngineStatus* engineStatus() override {
        if (!status) return nullptr;
        lastStatus = *status;
        status.reset();
        return &lastStatus;
    }
    pluginui::EditorSession& session() override { return editor; }
    bool standalone() const override { return true; }
    std::string midiStatus() const override { return "none"; }
    std::string version() const override { return "test"; }
    void quit() override { ++quits; }
    bool hasAudioSettings() const override { return false; }
    void openAudioSettings() override {}
    void loadDigidrum(int) override {}
    std::string preference(std::string_view key, std::string_view fallback) const override {
        const auto it = prefs.find(key);
        return it == prefs.end() ? std::string(fallback) : it->second;
    }
    void setPreference(std::string_view key, std::string_view value) override { prefs[std::string(key)] = value; }
    std::string systemLanguage() const override { return systemLang; }
    std::vector<TextFile> languageFiles() const override { return languages; }
    bool openLanguagesFolder(const std::vector<TextFile>& files) override {
        folderFiles = files;
        ++foldersOpened;
        return true;
    }
    std::string patchLibrary() const override { return patches; }
    void setPatchLibrary(const std::string& json) override { patches = json; }
    std::string digidrumLibrary() const override { return digidrums; }
    void setDigidrumLibrary(const std::string& json) override { digidrums = json; }
    std::string dmaLibrary() const override { return dmaBanks; }
    void setDmaLibrary(const std::string& json) override { dmaBanks = json; }
    Recorder recorder() const override { return rec; }
    void clearRecording() override { rec.takeFrames = 0; }

    float& p(int track, plugin::Track f) { return values[size_t(plugin::paramIndex(track, f))]; }
    float& p(plugin::Global g) { return values[size_t(plugin::paramIndex(g))]; }
    // Notes sent since the last call.
    std::vector<Midi> take() { return std::exchange(midi, {}); }
    std::vector<Command> takeCommands() { return std::exchange(commands, {}); }
    // Status of the sequencer as the engine publishes it, read by the next tick.
    void publish(const ymengine::Engine::SeqStatus& seq) {
        plugin::EngineStatus t;
        t.serial = ++statusSerial;
        t.seq = seq;
        status = t;
    }
    uint64_t statusSerial = 0;
};

inline gemui::Point centre(const gemui::Rect& r) { return {r.x + r.w / 2, r.y + r.h / 2}; }

inline void press(pluginui::MainWindow& w, gemui::Point p, gemui::MouseButton b = gemui::MouseButton::Left) {
    w.desktop().mouseMove(p, {});
    w.desktop().mouseDown(p, b, {});
    w.desktop().mouseUp(p, {});
}

inline void click(pluginui::MainWindow& w, const std::string& id, gemui::MouseButton b = gemui::MouseButton::Left) {
    const auto r = w.rectOf(id);
    REQUIRE(r);
    press(w, centre(*r), b);
}

inline bool key(pluginui::MainWindow& w, gemui::Key k, bool shift = false) {
    gemui::KeyEvent e;
    e.key = k;
    e.mods.shift = shift;
    return w.desktop().keyDown(e);
}

// Key of a character, as the JUCE editor gives it: the character and the unshifted character in upper case.
inline gemui::KeyEvent charKey(char32_t c, bool ctrl = false) {
    gemui::KeyEvent e;
    e.key = gemui::Key::Character;
    e.ch = c;
    e.code = int(c >= U'a' && c <= U'z' ? c - 0x20 : c);
    e.mods.ctrl = ctrl;
    return e;
}

inline bool ctrlKey(pluginui::MainWindow& w, char32_t c) { return w.desktop().keyDown(charKey(c, true)); }

inline const gemui::Alert* topAlert(pluginui::MainWindow& w) {
    return dynamic_cast<const gemui::Alert*>(w.desktop().topModal());
}

inline const gemui::MenuList* topMenu(pluginui::MainWindow& w) {
    return dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
}

// Labels of the open menu (separators as "---").
inline std::vector<std::string> menuLabels(pluginui::MainWindow& w) {
    std::vector<std::string> out;
    const gemui::MenuList* m = topMenu(w);
    REQUIRE(m);
    for (const gemui::MenuItem& it : m->items()) out.push_back(it.separator ? "---" : it.label);
    return out;
}

// Clicks entry `item` of the open menu (or of its open submenu).
inline void pickItem(pluginui::MainWindow& w, int item) {
    const gemui::MenuList* m = topMenu(w);
    REQUIRE(m);
    const gemui::Rect r = m->itemRect(item);
    REQUIRE(r.w > 0);
    press(w, centre(r));
}

// Hovers entry `item` of the open menu (opens its submenu).
inline void hoverItem(pluginui::MainWindow& w, int item) {
    const gemui::MenuList* m = topMenu(w);
    REQUIRE(m);
    w.desktop().mouseMove(centre(m->itemRect(item)), {});
}

// Clicks button `index` of the open GEM box.
inline void alertButton(pluginui::MainWindow& w, size_t index) {
    const gemui::Alert* a = topAlert(w);
    REQUIRE(a);
    REQUIRE(index < a->buttonCount());
    press(w, centre(a->button(index).bounds()));
}

// A setting of the Settings window (Options > Settings..., D-430) as the Options submenu that held it: its label,
// greyed out with its button's tooltip as the reason, the entries of its menu and the checked one. Opens the window.
inline gemui::MenuItem setting(pluginui::MainWindow& w, std::string_view id) {
    pluginui::SettingsWindow& s = w.settingsWindow();
    if (!s.isOpen()) s.show();
    s.refresh();
    const gemui::Button* b = s.button(id);
    REQUIRE(b);
    gemui::MenuItem m;
    m.label = std::string(pluginui::SettingsWindow::labelOf(id));
    m.disabled = !b->enabled();
    if (m.disabled) m.why = b->tip();
    m.subMarks = true;
    m.sub = s.items(id, m.subCur);
    return m;
}

}  // namespace stym::ui_testing
