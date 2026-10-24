// STYM2149 — File menu tests (M7): fake host with an in-memory disk and the file dialogues, helpers for the menus
// and the alerts.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#pragma once
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "gemui/Alert.h"
#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"
#include "ymio/ByteSink.h"

namespace stym::testing {

class FileHost : public pluginui::Host {
public:
    plugin::ParamValues values = plugin::defaultValues();
    plugin::NonParamState state;
    uint64_t serial = 1;
    pluginui::EditorSession editor;
    std::string patches;
    // Disk: path → content; the paths under /ro/ refuse writing.
    std::map<std::string, std::string> disk;
    std::vector<FileDialog> opens, saves;
    std::function<void(const std::string&)> pending;
    std::vector<int> selects;  // patterns chosen (SeqCommand::Select)
    int stops = 0;             // SeqCommand::Stop received (File > New, D-249)
    std::map<std::string, std::string> prefs;
    uint32_t rate = 0;  // host sample rate (0: unknown)
    Recorder rec;              // register recorder: state given by the test
    plugin::RegisterTake take;  // kept take

    float param(int i) const override { return values[size_t(i)]; }
    void beginGesture(int) override {}
    void setParam(int i, float v) override { values[size_t(i)] = plugin::clampToSpec(plugin::paramSpec(i), v); }
    void endGesture(int) override {}
    uint64_t extrasVersion() const override { return serial; }
    plugin::NonParamState extras() const override { return state; }
    void editExtras(const std::function<void(plugin::NonParamState&)>& edit) override {
        edit(state);
        plugin::sanitize(state);
        ++serial;
    }
    void screenMidi(uint8_t, uint8_t, uint8_t) override {}
    void setMidiCapture(const plugin::MidiKeys&, bool) override {}
    bool learnMidi(std::array<uint8_t, 3>&) override { return false; }
    bool hostNote(std::array<uint8_t, 3>&) override { return false; }
    void seqCommand(SeqCommand c, int a, int, int) override {
        if (c == SeqCommand::Select) selects.push_back(a);
        if (c == SeqCommand::Stop) ++stops;
    }
    const plugin::EngineStatus* engineStatus() override { return nullptr; }
    pluginui::EditorSession& session() override { return editor; }
    bool standalone() const override { return true; }
    std::string midiStatus() const override { return "none"; }
    std::string version() const override { return "test"; }
    void quit() override {}
    bool hasAudioSettings() const override { return false; }
    void openAudioSettings() override {}
    void loadDigidrum(int) override {}
    std::string preference(std::string_view key, std::string_view fallback) const override {
        const auto it = prefs.find(std::string(key));
        return it == prefs.end() ? std::string(fallback) : it->second;
    }
    void setPreference(std::string_view key, std::string_view value) override {
        prefs[std::string(key)] = std::string(value);
    }
    std::string patchLibrary() const override { return patches; }
    void setPatchLibrary(const std::string& json) override { patches = json; }
    uint32_t hostSampleRate() const override { return rate; }
    Recorder recorder() const override { return rec; }
    plugin::RegisterTake recordedTake() const override { return take; }

    void chooseFileToOpen(const FileDialog& d, std::function<void(const std::string&)> done) override {
        opens.push_back(d);
        pending = std::move(done);
    }
    void chooseFileToSave(const FileDialog& d, std::function<void(const std::string&)> done) override {
        saves.push_back(d);
        pending = std::move(done);
    }
    std::optional<std::string> readFile(const std::string& path, size_t maxBytes) const override {
        const auto it = disk.find(path);
        if (it == disk.end() || it->second.size() > maxBytes) return std::nullopt;
        return it->second;
    }
    bool writeFile(const std::string& path, std::string_view bytes) override {
        if (path.rfind("/ro/", 0) == 0) return false;
        disk[path] = std::string(bytes);
        return true;
    }
    // File written in chunks: on the disk at its commit() only; refused under /ro/, and under /full/ it fails after
    // 1,000 bytes.
    std::unique_ptr<ymio::ByteSink> createFile(const std::string& path) override {
        if (path.rfind("/ro/", 0) == 0) return nullptr;
        return std::make_unique<Sink>(*this, path, path.rfind("/full/", 0) == 0 ? 1000 : SIZE_MAX);
    }

    // The user picks a file in the open dialogue.
    void pick(const std::string& path) {
        REQUIRE(pending);
        auto done = std::move(pending);
        pending = nullptr;
        done(path);
    }
    float& p(int track, plugin::Track f) { return values[size_t(plugin::paramIndex(track, f))]; }
    float& p(plugin::Global g) { return values[size_t(plugin::paramIndex(g))]; }

private:
    class Sink final : public ymio::ByteSink {
    public:
        Sink(FileHost& h, std::string path, size_t limit) : h_(h), path_(std::move(path)), limit_(limit) {}
        bool write(std::span<const uint8_t> b) override {
            if (mem_.position() + b.size() > limit_) return failed_ = true, false;
            return mem_.write(b);
        }
        bool seek(uint64_t p) override { return mem_.seek(p); }
        uint64_t position() const override { return mem_.position(); }
        uint64_t size() const override { return mem_.size(); }
        bool ok() const override { return mem_.ok() && !failed_; }
        bool commit() override {
            if (!ok()) return false;
            h_.disk[path_] = std::string(mem_.bytes.begin(), mem_.bytes.end());
            return true;
        }

    private:
        FileHost& h_;
        std::string path_;
        size_t limit_;
        ymio::MemorySink mem_;
        bool failed_ = false;
    };
};

inline gemui::Point centre(const gemui::Rect& r) { return {r.x + r.w / 2, r.y + r.h / 2}; }

inline void click(pluginui::MainWindow& w, const std::string& id) {
    const auto r = w.rectOf(id);
    REQUIRE(r);
    const gemui::Point at = centre(*r);
    w.desktop().mouseMove(at, {});
    w.desktop().mouseDown(at, gemui::MouseButton::Left, {});
    w.desktop().mouseUp(at, {});
}

inline bool undoKey(pluginui::MainWindow& w, bool redo = false) {
    gemui::KeyEvent e;
    e.key = gemui::Key::Character;
    e.ch = redo ? U'y' : U'z';
    e.code = redo ? 'Y' : 'Z';
    e.mods.ctrl = true;
    return w.desktop().keyDown(e);
}

// Entry of a submenu of the File menu ("Save" > "Preset"), if it is enabled.
inline void fileMenu(pluginui::MainWindow& w, const std::string& entry, const std::string& sub) {
    w.openMenu(1);
    const auto* list = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
    REQUIRE(list);
    std::function<void()> pick;
    for (const gemui::MenuItem& m : list->items())
        if (m.label == entry)
            for (const gemui::MenuItem& s : m.sub)
                if (s.label == sub && !s.disabled) pick = s.pick;
    w.desktop().closePopups();
    REQUIRE(pick);
    pick();
}

inline gemui::Alert* topAlert(pluginui::MainWindow& w) {
    return dynamic_cast<gemui::Alert*>(w.desktop().topModal());
}

inline std::string alertText(pluginui::MainWindow& w) {
    const gemui::Alert* a = topAlert(w);
    return a ? a->text() : std::string();
}

inline void pressKey(pluginui::MainWindow& w, gemui::Key k) {
    gemui::KeyEvent e;
    e.key = k;
    w.desktop().keyDown(e);
}

inline void dismiss(pluginui::MainWindow& w) { pressKey(w, gemui::Key::Enter); }

// Lets the worker thread of an export finish, editor ticks included.
inline void waitExport(pluginui::MainWindow& w, double& now) {
    for (int i = 0; i < 10000 && w.exporting(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        w.tick(now += 2);
    }
    REQUIRE_FALSE(w.exporting());
}

}  // namespace stym::testing
