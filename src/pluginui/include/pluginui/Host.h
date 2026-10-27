// STYM2149 — pluginui: what the main window asks of its host (JUCE editor, capture tool, tests)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The window knows neither JUCE nor the processor: it reads and writes the plain parameter values (Parameters.h),
// reads and changes the non-automatable state, sends the screen's notes and the sequencer commands, reads the engine
// status and keeps its editor state at the host (Session.h). Everything happens on the message thread.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "plugincore/MidiLearn.h"
#include "plugincore/State.h"
#include "plugincore/EngineStatus.h"
#include "pluginui/Session.h"
#include "ymio/ByteSink.h"

namespace stym::pluginui {

// Implemented by the plugin editor (StymEditor, PluginEditor.h), the capture tool (FakeHost, tools/gemshot) and the
// test hosts (tests/gemui); MainWindow keeps a reference to it for its whole life. Every host provides the pure
// virtual functions; the others have a default for a host without files or engine, which does nothing.
class Host {
public:
    virtual ~Host() = default;

    // Parameters, as plain values. A setting from the interface is a gesture: begin, one or more values, end.
    // index: plugin::paramIndex (0 to kParamCount - 1, Parameters.h); plain: the value in the unit and range of its
    // ParamSpec.
    virtual float param(int index) const = 0;
    virtual void beginGesture(int index) = 0;
    virtual void setParam(int index, float plain) = 0;
    virtual void endGesture(int index) = 0;

    // Non-automatable state: version number (changes on every modification, state read back included), reading,
    // modification (the host publishes the result to the audio thread).
    virtual uint64_t extrasVersion() const = 0;
    virtual plugin::NonParamState extras() const = 0;
    virtual void editExtras(const std::function<void(plugin::NonParamState&)>& edit) = 0;

    // On-screen playing (plugin::MidiEvent::screen): note on/off, CC 120 (manual playing cut), CC 123 (panic).
    virtual void screenMidi(uint8_t status, uint8_t data1, uint8_t data2) = 0;
    // Note on and off received from the host (MIDI, not on-screen playing), CC 120 and 123 (all cut), in order of
    // arrival: true and `message` filled while some are left (lit keys, recording into the sequencer, like press and
    // release in the prototype).
    virtual bool hostNote(std::array<uint8_t, 3>& message) = 0;
    // MIDI learn (P462, D-146): learned CCs and notes, which the host keeps for the interface instead of the engine,
    // and armed control (the next CC or the next note played is taken for it, once). The caught messages come back
    // through learnMidi, in order of arrival: true and `message` filled while some are left.
    virtual void setMidiCapture(const plugin::MidiKeys& keys, bool armed) = 0;
    virtual bool learnMidi(std::array<uint8_t, 3>& message) = 0;

    // Sequencer, through the same queue as on-screen playing (Driver.h, UiCommand): Play, Stop (no effect in Host
    // sync), choice of a pattern (a; when stopped at once, when playing at the end of the loop), preview of a grid
    // note (a track, b note, c accent 0-127), jump to a Song entry (a, 0 to 65535), audition of a Chipdrum kit
    // instrument (a slot 0-7, Audition button of the Drum Kit window), preview of a DMA track sample (a slot 0-7, Play
    // button of the DMA window, M6), preview of a Digidrum slot (a slot 0-7, Play button of the Digidrums editor, M6);
    // for these two, b 1 stops the preview (D-449).
    enum class SeqCommand : uint8_t { Play, Stop, Select, Preview, SongJump, DrumPreview, DmaPreview, DigiPreview };
    virtual void seqCommand(SeqCommand command, int a = 0, int b = 0, int c = 0) = 0;

    // Last published engine status, nullptr if nothing new since the previous call.
    virtual const plugin::EngineStatus* engineStatus() = 0;

    // Editor state kept while the plugin lives (tools, view, undo, open windows).
    virtual EditorSession& session() = 0;

    // Context.
    virtual bool standalone() const = 0;
    virtual std::string midiStatus() const = 0;  // text of the "MIDI:" indicator
    virtual std::string version() const = 0;
    virtual void quit() = 0;                     // standalone application only
    // Audio and MIDI settings of the standalone application (Options > Audio/MIDI settings menu); a plugin has
    // none: the host chooses.
    virtual bool hasAudioSettings() const = 0;
    virtual void openAudioSettings() = 0;
    // Opens the choice of an audio file for a Digidrum slot (asynchronous; the host converts the sample and stores it
    // in the non-automatable state: 4-bit values and 8-bit source at the slot's rate, name taken from the file, loop
    // over the whole sample, D-154).
    virtual void loadDigidrum(int slot) = 0;
    // Opens the choice of a file for a slot of the DMA track (M6, D-153; asynchronous): audio file resampled at the
    // slot's rate, mono or stereo depending on its setting, or raw file (.raw, .spl, .sam) taken byte for byte. A host
    // without files (tests) does nothing.
    virtual void loadDmaSample(int /*slot*/) {}

    // Register recorder (M6, B11, D-157): state (take armed or rolling, its chip and its frames), take kept with the
    // project (frames, replay tick, chip), arming on a chip and stop, clearing of the kept take. A host without an
    // engine (tests) records nothing.
    struct Recorder {
        enum class State : uint8_t { Idle, Armed, Rolling };
        State state = State::Idle;
        int chip = 0;             // chip armed or recorded: 0, or 1 in dual chip
        uint32_t frames = 0;      // frames recorded by the take in progress
        uint32_t takeFrames = 0;  // kept take: 0, none
        uint32_t takeHz = 50;     // replay tick of the kept take, in frames per second
        int takeChip = 0;
        bool operator==(const Recorder&) const = default;
    };
    virtual Recorder recorder() const { return {}; }
    virtual void armRecorder(int /*chip*/) {}
    virtual void disarmRecorder() {}
    virtual void clearRecording() {}
    // Copy of the kept take (export of the Recorder window, B3); empty if there is none.
    virtual plugin::RegisterTake recordedTake() const { return {}; }

    // YM Player (M7, A2, D-169): tune read from File > Import > YM dump (YM dump or SNDH), played in place of the
    // engine output. loadYm returns the alert of a refused file (empty: loaded, stopped). A host without an engine
    // (tests) loads nothing.
    struct YmPlayer {
        bool loaded = false, playing = false, sndh = false;
        std::string name;        // name line (#ymName)
        int subsong = 1, subsongs = 1;
        uint32_t position = 0;   // frame in the tune (0 = first)
        uint32_t frames = 0;     // frames of one pass (0: unknown)
        int rate = 50;           // frames per second
        std::array<uint8_t, 14> regs{};  // R0 to R13 of the player's chip after the last block played
        bool operator==(const YmPlayer&) const = default;
    };
    virtual std::string loadYm(std::string_view /*bytes*/, const std::string& /*fileName*/) { return "Unsupported file"; }
    virtual YmPlayer ymPlayer() const { return {}; }
    virtual void ymPlay(bool /*on*/) {}
    virtual void ymRewind() {}
    virtual void ymSubsong(int /*subsong*/) {}
    virtual void ymClose() {}

    // User preferences (shared by every instance: window size, tooltips, note names...).
    virtual std::string preference(std::string_view key, std::string_view fallback) const = 0;
    virtual void setPreference(std::string_view key, std::string_view value) = 0;
    // The user's language (ISO 639 code: "fr", "fr-FR"): language of the tooltips as long as the user has not chosen
    // one (D-185).
    virtual std::string systemLanguage() const { return "en"; }
    // Tooltip languages (D-283): the .po files of the user's Languages folder, next to the preferences file, then
    // those installed with STYM2149 (French, German, Spanish, Portuguese, Italian), each folder by name. A host without
    // files has none: English only.
    struct TextFile {
        std::string name;  // "nl.po"; for openLanguagesFolder, a name in the folder ("template.pot")
        std::string text;
        bool installed = false;  // from the folder the installer fills, not the user's
    };
    virtual std::vector<TextFile> languageFiles() const { return {}; }
    // Folder of the languages installed with STYM2149, as the read-me of the user's folder names it (empty: unknown).
    virtual std::string installedLanguagesFolder() const { return {}; }
    // Settings > Languages folder: creates the user's folder if needed, writes the given files
    // into it (template, read-me) and shows it in the system's file browser. False if the folder cannot be made.
    virtual bool openLanguagesFolder(const std::vector<TextFile>& /*files*/) { return false; }
    // Layout of the system keyboard, "qwerty", "azerty" or "qwertz": the one Auto plays and draws in Settings >
    // Keyboard layout (D-215).
    virtual std::string keyboardLayout() const { return "qwerty"; }
    // Settings > Renderer (Windows, D-420): whether the system offers a choice of how the window reaches the screen,
    // the preference "renderer" being "software" (default) or "direct2d". None on macOS and Linux.
    virtual bool rendererChoice() const { return false; }
    // Whole project replaced (File > Load > Preset, M7): parameter values and non-automatable state, like a state
    // read back (the current pattern becomes the engine's); the recorder's take is kept. By default: each parameter
    // changed by a gesture, then the non-automatable state replaced.
    virtual void loadProject(const plugin::ParamValues& values, const plugin::NonParamState& extras) {
        for (int i = 0; i < plugin::kParamCount; ++i) {
            if (param(i) == values[size_t(i)]) continue;
            beginGesture(i);
            setParam(i, values[size_t(i)]);
            endGesture(i);
        }
        editExtras([&extras](plugin::NonParamState& s) { s = extras; });
    }
    // Non-automatable state of a new project (File > New, D-225): the defaults, with the sequencer clock the plugin
    // starts with (Host sync in a host, Free run in the standalone application).
    virtual plugin::NonParamState newExtras() const { return {}; }

    // Files (M7): asynchronous native dialogs (STYM2149_SPEC.md §8: FileChooser, never from the audio thread), then
    // reading or writing of a whole file. Absolute paths in UTF-8. The callback comes on the message thread with the
    // chosen file, and not at all if the user cancels or if the editor is closed in the meantime. A host without files
    // (tests) opens nothing.
    struct FileDialog {
        std::string title;     // "Load preset", "Save pattern as"
        std::string patterns;  // filters separated by ";" ("*.json")
        std::string path;      // suggested file (the last one of this kind), otherwise:
        std::string name;      // suggested name, in the last chosen folder (at first, the documents folder)
    };
    virtual void chooseFileToOpen(const FileDialog& /*dialog*/, std::function<void(const std::string& path)> /*done*/) {}
    virtual void chooseFileToSave(const FileDialog& /*dialog*/, std::function<void(const std::string& path)> /*done*/) {}
    // Content of a file (nullopt if it cannot be read or exceeds maxBytes); writing (replaces the file).
    virtual std::optional<std::string> readFile(const std::string& /*path*/, size_t /*maxBytes*/) const {
        return std::nullopt;
    }
    virtual bool writeFile(const std::string& /*path*/, std::string_view /*bytes*/) { return false; }
    // File written in pieces (WAV and stem exports): a provisional copy, which replaces the file only at its commit();
    // abandoned if it is destroyed before. It can be filled from the export's worker thread. nullptr if the file
    // cannot be created.
    virtual std::unique_ptr<ymio::ByteSink> createFile(const std::string& /*path*/) { return nullptr; }
    // Host sample rate ("Host" choice of the WAV dialog, D-164); 0 if unknown.
    virtual uint32_t hostSampleRate() const { return 0; }

    // Patches library (D-141): JSON text of the file shared by every project, next to the preferences file (empty if
    // it does not exist); writing replaces it.
    virtual std::string patchLibrary() const = 0;
    virtual void setPatchLibrary(const std::string& json) = 0;
    // Digidrum banks (D-154): same principle, a separate file. A host without files (tests) has none.
    virtual std::string digidrumLibrary() const { return {}; }
    virtual void setDigidrumLibrary(const std::string& /*json*/) {}
    // DMA banks (D-462): same principle, a third file.
    virtual std::string dmaLibrary() const { return {}; }
    virtual void setDmaLibrary(const std::string& /*json*/) {}
};

}  // namespace stym::pluginui
