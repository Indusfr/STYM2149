// STYM2149 — pluginui: File menu, JSON files of the prototype (M7, A1, D-165, D-168)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Load, Save and Save as of a preset, a pattern, a Song and a kit (File menu of the prototype), through the file
// dialogues of the host; Import > YM dump to the YM Player (A2); Import > MIDI file, Export pattern > MIDI and Export
// song > MIDI (A3, D-170); Import > XM module (B5, D-166). The refusals reuse the alerts of the prototype; loading a
// pattern, a Song or a kit and the MIDI and XM imports take an undo step (pushUndo, pushSongUndo, pushKitUndo). The
// exports (MIDI, WAV and stems, registers) run on a worker thread, under the busy bee, and end with an alert (PLAN_M7).
//
// Every host dialogue is asynchronous: the function returns at once, and the callback comes later on the message
// thread with the chosen path (never if the user cancels or the editor closes). An export follows the same pattern
// each time: copy the parameter values and the non-automatable state, start the job (startJob) with a `work` that
// renders on the worker thread from those copies only, and a `done` that runs on the message thread (from tick, through
// BackgroundJob::poll) to write the file or commit it and open the final alert. Both lambdas hold the same shared_ptr
// to the result: `work` fills it, `done` reads it (empty when the render was cancelled).
#include "pluginui/MainWindow.h"

#include <array>
#include <cstdio>
#include <ctime>
#include <memory>
#include <span>
#include <utility>

#include "gemui/Alert.h"
#include "plugincore/AudioExport.h"
#include "plugincore/Mapping.h"
#include "plugincore/MidiFiles.h"
#include "plugincore/ProjectFiles.h"
#include "plugincore/RegisterFiles.h"
#include "plugincore/XmImport.h"
#include "ymio/Lzh.h"

namespace stym::pluginui {

using plugin::FileKind;
using plugin::Track;

namespace {

// Maximum size of a file read: a prototype preset with all 32 patterns created is about ten MB.
constexpr size_t kMaxJsonBytes = size_t(64) << 20;
// YM or SNDH dump: an SNDH fits in the memory of the machine (4 MB); an uncompressed YM6 of several minutes, with its
// digidrums, is a few MB.
constexpr size_t kMaxYmBytes = size_t(32) << 20;

std::string_view kindName(FileKind k) {
    switch (k) {
    case FileKind::Preset: return "preset";
    case FileKind::Pattern: return "pattern";
    case FileKind::Song: return "song";
    case FileKind::Kit: return "drum kit";
    case FileKind::Patches: return "patch library";
    }
    return "file";
}

// Alerts of the prototype's readers (unreadable or refused file).
std::string_view readError(FileKind k) {
    switch (k) {
    case FileKind::Preset: return "Preset read error.";
    case FileKind::Pattern: return "Pattern read error.";
    case FileKind::Song: return "Song read error.";
    case FileKind::Kit: return "Not a valid drum kit file.";
    case FileKind::Patches: return "Not a valid patch library file.";
    }
    return "Read error.";
}

std::string baseName(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Duration of an export ("1:05", "0:00.5" under one second).
std::string duration(double seconds) {
    char buf[32];
    if (seconds < 1) {
        std::snprintf(buf, sizeof buf, "0:00.%d", int(seconds * 10));
    } else {
        const int s = int(seconds + 0.5);
        std::snprintf(buf, sizeof buf, "%d:%02d", s / 60, s % 60);
    }
    return buf;
}

// WAV dialogue (D-164): formats and rates, and their keys in the preferences ("host": the host's own rate, like the
// prototype, which rendered at the rate of its AudioContext).
constexpr std::array<const char*, 3> kWavFormatNames = {"16-bit", "24-bit", "32-bit float"};
constexpr std::array<const char*, 3> kWavFormatKeys = {"16", "24", "32f"};
constexpr std::array<const char*, plugin::kWavRates.size() + 1> kWavRateNames = {"44.1", "48",  "88.2", "96",
                                                                                 "176.4", "192", "Host"};

std::string kHz(uint32_t rate) {
    if (rate % 1000 == 0) return std::to_string(rate / 1000) + " kHz";
    return gemui::formatDecimal("%.1f kHz", rate / 1000.0);
}

}  // namespace

// Load dialogue of a JSON file: suggests the file last saved of that kind, reads it whole (refused beyond
// kMaxJsonBytes) and gives its text to `read`.
void MainWindow::openFile(FileKind kind, std::function<void(const std::string& text)> read) {
    Host::FileDialog d;
    d.title = kind == FileKind::Patches ? "Import a patch library" : "Load " + std::string(kindName(kind));
    d.patterns = "*.json";
    d.path = host_.session().files[size_t(kind)];
    host_.chooseFileToOpen(d, [this, kind, read = std::move(read)](const std::string& path) {
        const std::optional<std::string> text = host_.readFile(path, kMaxJsonBytes);
        if (!text) {
            showMessage(readError(kind));
            return;
        }
        read(*text);
    });
}

// File > Import > YM dump: #ymFile of the prototype (.ym, .ym2, .ym3, .ym5, .ym6, .snd, .sndh).
void MainWindow::importYm() {
    Host::FileDialog d;
    d.title = "Import a YM dump";
    d.patterns = "*.ym;*.ym2;*.ym3;*.ym5;*.ym6;*.snd;*.sndh";
    d.path = host_.session().ymFile;
    host_.chooseFileToOpen(d, [this](const std::string& path) {
        host_.session().ymFile = path;
        const std::optional<std::string> bytes = host_.readFile(path, kMaxYmBytes);
        if (!bytes) {
            showMessage("The file could not be read.");
            return;
        }
        loadYmBytes(*bytes, baseName(path));
    });
}

void MainWindow::loadYmBytes(std::string_view bytes, const std::string& fileName) {
    const std::string error = host_.loadYm(bytes, fileName);
    if (error.empty()) {
        ymWin_.show();
        return;
    }
    gemui::Alert::Spec spec;
    if (error == "Unsupported file")  // ymLoad of the prototype, without the mention of PRISM
        spec.lines = {{error}, {""}, {"Expected a YM register dump (YM2 / YM3 / YM5 / YM6) or an SNDH file."}};
    else
        spec.lines = {{error}};
    gemui::Alert::open(desk_, std::move(spec));
}

// File > Import > MIDI file: #midiInFile of the prototype.
void MainWindow::importMidi() {
    Host::FileDialog d;
    d.title = "Import a MIDI file";
    d.patterns = "*.mid;*.midi";
    d.path = host_.session().midiFile;
    host_.chooseFileToOpen(d, [this](const std::string& path) {
        host_.session().midiFile = path;
        const std::optional<std::string> bytes = host_.readFile(path, plugin::kMidiMaxBytes);
        if (!bytes) {
            showMessage("The file could not be read.");
            return;
        }
        loadMidiBytes(*bytes, baseName(path));
    });
}

void MainWindow::loadMidiBytes(std::string_view bytes, const std::string& fileName) {
    // midiImport: the file is converted on a copy of the project; if refused, nothing changes.
    ymseq::Project p = extras_.seq;
    const std::span<const uint8_t> data(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    const std::optional<plugin::MidiImport> r = plugin::importMidi(data, plugin::voiceMode(values_), p);
    if (!r) {
        gemui::Alert::Spec spec;
        spec.lines = {{"Unsupported MIDI file"}, {""}, {"Expected a standard MIDI file (format 0 or 1) with notes."}};
        gemui::Alert::open(desk_, std::move(spec));
        return;
    }
    // One undo step for the whole import (the prototype kept only the current pattern, pushUndo).
    UndoEntry e;
    e.kind = UndoEntry::Kind::Import;
    e.index = r->patterns;
    host_.session().undo.push(undoEntryLike(e));
    // Tracks switched on or off, Chipdrum (channel 10) or Chip Voice; tempo of the file (GLOB.bpm).
    // One host gesture per parameter that changes, values_ updated at once; the import's undo step holds them all.
    auto gesture = [this](int idx, float v) {
        if (host_.param(idx) == v) return;
        host_.beginGesture(idx);
        host_.setParam(idx, v);
        host_.endGesture(idx);
        values_[size_t(idx)] = host_.param(idx);
    };
    for (int t = 0; t < plugin::kTracks; ++t) {
        gesture(plugin::paramIndex(t, Track::On), r->on[size_t(t)] ? 1.0f : 0.0f);
        if (r->on[size_t(t)])
            gesture(plugin::paramIndex(t, Track::Generator),
                    float(int(r->drum[size_t(t)] ? ymengine::Generator::Chipdrum : ymengine::Generator::Chip)));
    }
    gesture(plugin::paramIndex(plugin::Global::FreeTempo), plugin::midiImportTempo(*r));
    // Patterns and Song; the first pattern becomes current (while playing, at the end of the loop, like a pattern
    // choice).
    const bool play = seq_.playing();
    bool changed = false;
    host_.editExtras([&](plugin::NonParamState& s) {
        for (int k = 0; k < r->patterns; ++k) s.seq.patterns[size_t(k)] = p.patterns[size_t(k)];
        s.seq.used |= p.used;
        s.seq.song = p.song;
        s.seq.viewBank = 0;
        changed = s.seq.select(0, play);
    });
    if (changed) host_.seqCommand(Host::SeqCommand::Select, 0);
    pullExtras();
    gemui::Alert::Spec spec;
    for (std::string& l : plugin::midiImportMessage(*r, fileName)) spec.lines.push_back({std::move(l)});
    gemui::Alert::open(desk_, std::move(spec));
}

// File > Import > XM module (B5, D-056): Fasttracker II module.
void MainWindow::importXm() {
    Host::FileDialog d;
    d.title = "Import an XM module";
    d.patterns = "*.xm";
    d.path = host_.session().xmFile;
    host_.chooseFileToOpen(d, [this](const std::string& path) {
        host_.session().xmFile = path;
        const std::optional<std::string> bytes = host_.readFile(path, plugin::kXmMaxBytes);
        if (!bytes) {
            showMessage("The file could not be read.");
            return;
        }
        loadXmBytes(*bytes, baseName(path));
    });
}

void MainWindow::loadXmBytes(std::string_view bytes, const std::string& fileName) {
    // The module is converted on a copy of the project; if refused, nothing changes.
    plugin::NonParamState x = extras_;
    const std::span<const uint8_t> data(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    const plugin::XmImportResult r = plugin::importXm(data, plugin::voiceMode(values_), x);
    if (!r.import) {
        gemui::Alert::Spec spec;
        spec.lines = {{"XM not imported"}, {""}, {fileName}, {r.error}};
        gemui::Alert::open(desk_, std::move(spec));
        return;
    }
    const plugin::XmImport& m = *r.import;
    UndoEntry e;
    e.kind = UndoEntry::Kind::Import;
    e.index = m.patterns;
    e.xm = true;
    e.slots = m.instruments;
    host_.session().undo.push(undoEntryLike(e));
    // STE machine (DMA sound, D-153), tracks switched on or off, DMA type of the tracks in 16-channel mode (in
    // 3-channel mode, the DMA tracks are DMA anyway), VBL rate (Ticks) or free tempo (BPM).
    auto gesture = [this](int idx, float v) {
        if (host_.param(idx) == v) return;
        host_.beginGesture(idx);
        host_.setParam(idx, v);
        host_.endGesture(idx);
        values_[size_t(idx)] = host_.param(idx);
    };
    const bool studio = plugin::voiceMode(values_) == ymengine::VoiceMode::Studio16;
    gesture(plugin::paramIndex(plugin::Global::Computer), 1.0f);
    for (int t = 0; t < plugin::kTracks; ++t) {
        gesture(plugin::paramIndex(t, Track::On), m.on[size_t(t)] ? 1.0f : 0.0f);
        if (m.on[size_t(t)] && studio)
            gesture(plugin::paramIndex(t, Track::Generator), float(int(ymengine::Generator::Dma)));
    }
    if (m.ticks) gesture(plugin::paramIndex(plugin::Global::VblRate), float(m.vblRate));
    else gesture(plugin::paramIndex(plugin::Global::FreeTempo), m.tempo);
    // Patterns, Song, instruments, DMA samples and rate; the first pattern becomes current (while playing, at the end
    // of the loop, like a pattern choice).
    const bool play = seq_.playing();
    bool changed = false;
    host_.editExtras([&](plugin::NonParamState& s) {
        for (int k = 0; k < m.patterns; ++k) s.seq.patterns[size_t(k)] = x.seq.patterns[size_t(k)];
        s.seq.used |= x.seq.used;
        s.seq.song = x.seq.song;
        for (int k = 0; k < m.instruments; ++k) {
            s.instruments[size_t(k)] = x.instruments[size_t(k)];
            s.dma.slots[size_t(k)] = x.dma.slots[size_t(k)];
            s.dmaSamples[size_t(k)] = x.dmaSamples[size_t(k)];
            s.dmaNames[size_t(k)] = x.dmaNames[size_t(k)];
        }
        s.global.timing = x.global.timing;
        s.global.speed = x.global.speed;
        s.seq.viewBank = 0;
        changed = s.seq.select(0, play);
    });
    if (changed) host_.seqCommand(Host::SeqCommand::Select, 0);
    pullExtras();
    gemui::Alert::Spec spec;
    for (std::string& l : plugin::xmImportMessage(m, fileName)) spec.lines.push_back({std::move(l)});
    gemui::Alert::open(desk_, std::move(spec));
}

// File from a newer version of STYM2149 (key "stym", B6, D-174): read for what is known of it.
void MainWindow::newerFileAlert() {
    gemui::Alert::Spec spec;
    spec.lines = {{"This file comes from a newer STYM2149."}, {"Settings this version does not know are not used."}};
    gemui::Alert::open(desk_, std::move(spec));
}

// One export at a time: the bee pointer stays until tick() sees the job finished (BackgroundJob::poll).
bool MainWindow::startJob(BackgroundJob::Work work, std::function<void()> done) {
    if (!job_.start(std::move(work), std::move(done))) {
        showMessage("An export is already running.");
        return false;
    }
    desk_.setBusy(true);
    return true;
}

// File > Export pattern > MIDI (exportMidi) and Export song > MIDI (exportSongMidi).
void MainWindow::exportMidi(bool song) {
    if (song && extras_.seq.song.entries.empty()) {  // requireSong
        showMessage("Song is empty. Add entries first.");
        return;
    }
    if (exporting()) {
        showMessage("An export is already running.");
        return;
    }
    Host::FileDialog d;
    d.title = song ? "Export the song as MIDI" : "Export the pattern as MIDI";
    d.patterns = "*.mid;*.midi";
    d.name = song ? "stym2149-song.mid" : "stym2149-pattern.mid";  // prism-ym2149-*.mid of the prototype (D-060)
    host_.chooseFileToSave(d, [this, song](const std::string& chosen) {
        const std::string path = plugin::ensureExtension(chosen, {".mid", ".midi"});
        if (path.empty()) return;
        // The worker thread gets a copy of the settings and the state: the editor stays free in the meantime.
        plugin::ParamValues v{};
        for (int i = 0; i < plugin::kParamCount; ++i) v[size_t(i)] = host_.param(i);
        auto x = std::make_shared<const plugin::NonParamState>(host_.extras());
        auto out = std::make_shared<std::optional<plugin::MidiExport>>();
        const auto source = song ? plugin::OfflineRender::Source::Song : plugin::OfflineRender::Source::Pattern;
        startJob([v, x, out, source](const std::atomic<bool>& cancel) {
                     *out = plugin::exportMidi(source, v, *x, &cancel);
                 },
                 [this, path, out] {
                     if (!*out) return;
                     const plugin::MidiExport& m = **out;
                     const std::string_view bytes(reinterpret_cast<const char*>(m.bytes.data()), m.bytes.size());
                     gemui::Alert::Spec spec;
                     if (!host_.writeFile(path, bytes)) {
                         spec.lines = {{"The file could not be written:"}, {path}};
                     } else {
                         spec.lines = {{"MIDI exported"}, {""}, {baseName(path)},
                                       {std::to_string(m.tracks) + (m.tracks == 1 ? " track, " : " tracks, ") +
                                        duration(m.seconds) + "."}};
                         if (m.truncated) spec.lines.push_back({"The song was longer than one hour and has been cut."});
                     }
                     gemui::Alert::open(desk_, std::move(spec));
                 });
    });
}

// File > Export pattern > WAV and Stems (ZIP), Export song > WAV and Stems (ZIP) (exportWav, exportStems,
// exportSongWav, exportSongStems of the prototype): GEM dialogue for the format (D-164), dialogue of the host, then the
// render written as it goes by the worker thread, alert at the end.
void MainWindow::exportAudio(bool stems, bool song) {
    if (song && extras_.seq.song.entries.empty()) {  // requireSong
        showMessage("Song is empty. Add entries first.");
        return;
    }
    if (exporting()) {
        showMessage("An export is already running.");
        return;
    }
    int format = 0;
    const std::string f = host_.preference("wavFormat", kWavFormatKeys[0]);
    for (size_t i = 0; i < kWavFormatKeys.size(); ++i)
        if (f == kWavFormatKeys[i]) format = int(i);
    const std::string r = host_.preference("wavRate", "host");
    // Index into kWavRateNames: one of plugin::kWavRates, or the entry after them, Host.
    int rate = int(plugin::kWavRates.size());  // Host
    for (size_t i = 0; i < plugin::kWavRates.size(); ++i)
        if (r == std::to_string(plugin::kWavRates[i])) rate = int(i);
    gemui::Alert::Spec spec;
    spec.lines = {{stems ? (song ? "Export the song stems" : "Export the pattern stems")
                         : (song ? "Export the song as WAV" : "Export the pattern as WAV")}};
    spec.choices = {{"Format", {kWavFormatNames.begin(), kWavFormatNames.end()}, format},
                    {"Rate (kHz)", {kWavRateNames.begin(), kWavRateNames.end()}, rate}};
    spec.buttons = {"Cancel", "Export"};
    spec.defaultButton = 1;
    spec.escapeButton = 0;
    spec.enterOnFocus = true;  // not a destructive action: Enter activates the button that has the focus (Tab)
    spec.maxWidth = 640;
    gemui::Alert::open(desk_, std::move(spec), [this, stems, song](int button, const std::vector<int>& picks) {
        if (button != 1 || picks.size() != 2) return;
        // Choice kept in the preferences: the next dialogue offers it (D-069).
        const size_t fi = size_t(std::clamp(picks[0], 0, int(kWavFormatKeys.size()) - 1));
        const size_t ri = size_t(std::clamp(picks[1], 0, int(plugin::kWavRates.size())));
        host_.setPreference("wavFormat", kWavFormatKeys[fi]);
        host_.setPreference("wavRate", ri < plugin::kWavRates.size() ? std::to_string(plugin::kWavRates[ri]) : "host");
        plugin::WavSettings ws;
        ws.format = ymio::WavFormat(fi);  // same order as kWavFormatNames
        // Host: the host's rate, or 48 kHz when the host does not tell it.
        const uint32_t hostRate = host_.hostSampleRate();
        ws.rate = ri < plugin::kWavRates.size() ? plugin::kWavRates[ri] : hostRate ? hostRate : 48000;
        saveAudio(stems, song, ws);
    });
}

void MainWindow::saveAudio(bool stems, bool song, const plugin::WavSettings& ws) {
    Host::FileDialog d;
    d.title = stems ? (song ? "Export the song stems" : "Export the pattern stems")
                    : (song ? "Export the song as WAV" : "Export the pattern as WAV");
    d.patterns = stems ? "*.zip" : "*.wav";
    // prism-ym2149-pattern.wav, prism-ym2149-song-stems.zip... of the prototype (D-060).
    d.name = std::string("stym2149-") + (song ? "song" : "pattern") + (stems ? "-stems.zip" : ".wav");
    host_.chooseFileToSave(d, [this, stems, song, ws](const std::string& chosen) {
        const std::string path = plugin::ensureExtension(chosen, {stems ? ".zip" : ".wav"});
        if (path.empty()) return;
        // The file is written as the render goes, from the worker thread, into a provisional copy that replaces the
        // chosen file only at commit(), in `done`; a cancelled or failed export leaves the file as it was.
        std::shared_ptr<ymio::ByteSink> sink = host_.createFile(path);
        if (!sink) {
            gemui::Alert::Spec spec;
            spec.lines = {{"The file could not be written:"}, {path}};
            gemui::Alert::open(desk_, std::move(spec));
            return;
        }
        plugin::ParamValues v{};
        for (int i = 0; i < plugin::kParamCount; ++i) v[size_t(i)] = host_.param(i);
        auto x = std::make_shared<const plugin::NonParamState>(host_.extras());
        auto out = std::make_shared<std::optional<plugin::AudioExport>>();
        const auto source = song ? plugin::OfflineRender::Source::Song : plugin::OfflineRender::Source::Pattern;
        startJob(
            [v, x, out, source, ws, sink, stems](const std::atomic<bool>& cancel) {
                *out = stems ? plugin::exportStems(source, v, *x, ws, *sink, &cancel)
                             : plugin::exportWav(source, v, *x, ws, *sink, &cancel);
            },
            [this, path, out, sink, stems, ws] {
                if (!*out) return;  // cancelled: the temporary copy is dropped
                const plugin::AudioExport& a = **out;
                if (stems && a.files == 0 && !a.failed) {
                    showMessage("No tracks with notes.");  // exportStems of the prototype
                    return;
                }
                gemui::Alert::Spec spec;
                if (a.failed || !sink->commit()) {
                    spec.lines = {{"The file could not be written:"}, {path}};
                    gemui::Alert::open(desk_, std::move(spec));
                    return;
                }
                std::string what = std::string(kWavFormatNames[size_t(ws.format)]) + ", " + kHz(ws.rate) + ", " +
                                   duration(a.seconds) + ".";
                if (stems) what = std::to_string(a.files) + (a.files == 1 ? " track, " : " tracks, ") + what;
                spec.lines = {{stems ? "Stems exported" : "WAV exported"}, {""}, {baseName(path)}, {what}};
                // Cut at one hour of music (kOfflineMaxSeconds, within a second), otherwise by the file size limit.
                if (a.truncated)
                    spec.lines.push_back({a.seconds >= plugin::kOfflineMaxSeconds - 1
                                              ? "The song was longer than one hour and has been cut."
                                              : "The file reached its 4 GB limit and has been cut."});
                gemui::Alert::open(desk_, std::move(spec));
            });
    });
}

namespace {

// Formats of the register exports: name in the titles and the alerts, patterns of the host's dialogue.
const char* formatName(plugin::RegisterFormat f) {
    switch (f) {
    case plugin::RegisterFormat::Vbl: return "VBL dump";
    case plugin::RegisterFormat::Ym3: return "YM3";
    case plugin::RegisterFormat::Ym6: return "YM6";
    case plugin::RegisterFormat::Sndh: return "SNDH";
    }
    return "";
}

std::string exportTitle(plugin::RegisterFormat f, bool take) {
    return std::string(take ? "Export the take as " : "Export the song as ") +
           (f == plugin::RegisterFormat::Vbl ? "a VBL dump" : formatName(f));
}

// Patterns of the host's dialogue; the first extension is added to the chosen name if it has none.
const char* filePatterns(plugin::RegisterFormat f) {
    switch (f) {
    case plugin::RegisterFormat::Vbl: return "*.s;*.asm";
    case plugin::RegisterFormat::Ym3:
    case plugin::RegisterFormat::Ym6: return "*.ym";
    case plugin::RegisterFormat::Sndh: return "*.sndh;*.snd";
    }
    return "*";
}

std::string withExtension(const std::string& chosen, plugin::RegisterFormat f) {
    switch (f) {
    case plugin::RegisterFormat::Vbl: return plugin::ensureExtension(chosen, {".s", ".asm"});
    case plugin::RegisterFormat::Ym3:
    case plugin::RegisterFormat::Ym6: return plugin::ensureExtension(chosen, {".ym"});
    case plugin::RegisterFormat::Sndh: return plugin::ensureExtension(chosen, {".sndh", ".snd"});
    }
    return chosen;
}

// Date of the LHA archive of a YM6: the local time of the export, in the MS-DOS format of the LHA header
// (ymio::lzhDosTime), through the reentrant localtime of each platform (localtime_s on Windows, localtime_r elsewhere).
uint32_t dosNow() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return ymio::lzhDosTime(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

}  // namespace

// File > Export song > VBL dump, YM3, YM6, SNDH (exportSongVbl, exportSongYm3, exportSongSndh of the prototype; YM6 of
// STYM2149, D-161) and export of the recorder take (B3): dialogue of the host, capture and writing by the worker
// thread, alert at the end with what the file does not render (D-172).
void MainWindow::exportRegisters(plugin::RegisterFormat format) {
    if (extras_.seq.song.entries.empty()) {  // requireSong
        showMessage("Song is empty. Add entries first.");
        return;
    }
    if (exporting()) {
        showMessage("An export is already running.");
        return;
    }
    Host::FileDialog d;
    d.title = exportTitle(format, false);
    d.patterns = filePatterns(format);
    d.name = plugin::registerFileName(format, false);  // prism-ym2149-song.* of the prototype (D-060)
    host_.chooseFileToSave(d, [this, format](const std::string& chosen) {
        const std::string path = withExtension(chosen, format);
        if (path.empty()) return;
        plugin::ParamValues v{};
        for (int i = 0; i < plugin::kParamCount; ++i) v[size_t(i)] = host_.param(i);
        auto x = std::make_shared<const plugin::NonParamState>(host_.extras());
        auto out = std::make_shared<std::optional<plugin::RegisterFile>>();
        plugin::RegisterFileOptions o;
        o.name = baseName(path);
        o.dosTime = dosNow();
        startJob([v, x, out, format, o](const std::atomic<bool>& cancel) {
                     *out = plugin::exportSongRegisters(format, v, *x, o, &cancel);
                 },
                 [this, path, out, format] {
                     if (*out) finishRegisters(format, path, **out);
                 });
    });
}

// Export of the recorder's take (Recorder window, B3): the frames are already captured, so the job only builds the
// file, and it cannot be cancelled.
void MainWindow::exportTakeFile(plugin::RegisterFormat format) {
    if (exporting()) {
        showMessage("An export is already running.");
        return;
    }
    auto take = std::make_shared<const plugin::RegisterTake>(host_.recordedTake());
    if (take->empty()) return;
    Host::FileDialog d;
    d.title = exportTitle(format, true);
    d.patterns = filePatterns(format);
    d.name = plugin::registerFileName(format, true);
    host_.chooseFileToSave(d, [this, format, take](const std::string& chosen) {
        const std::string path = withExtension(chosen, format);
        if (path.empty()) return;
        plugin::ParamValues v{};
        for (int i = 0; i < plugin::kParamCount; ++i) v[size_t(i)] = host_.param(i);
        auto x = std::make_shared<const plugin::NonParamState>(host_.extras());
        auto out = std::make_shared<std::optional<plugin::RegisterFile>>();
        plugin::RegisterFileOptions o;
        o.name = baseName(path);
        o.dosTime = dosNow();
        startJob([v, x, out, format, o, take](const std::atomic<bool>&) {
                     *out = plugin::exportTake(format, *take, v, *x, o);
                 },
                 [this, path, out, format] {
                     if (*out) finishRegisters(format, path, **out);
                 });
    });
}

// End of a register export, on the message thread: the file is written whole, then the alert gives its frames and
// duration, and lists under "Not as heard" what the format does not render.
void MainWindow::finishRegisters(plugin::RegisterFormat format, const std::string& path, const plugin::RegisterFile& f) {
    gemui::Alert::Spec spec;
    if (!f.error.empty()) {
        spec.lines = {{"The file could not be exported:"}, {f.error}};
    } else if (!host_.writeFile(path, {reinterpret_cast<const char*>(f.bytes.data()), f.bytes.size()})) {
        spec.lines = {{"The file could not be written:"}, {path}};
    } else {
        spec.lines = {{std::string(formatName(format)) + " exported"}, {""}, {baseName(path)},
                      {std::to_string(f.frames) + (f.frames == 1 ? " frame at " : " frames at ") +
                       std::to_string(f.frameHz) + " Hz, " + duration(f.seconds()) + "."}};
        if (f.truncated) spec.lines.push_back({"The song was longer than one hour and has been cut."});
        if (!f.notes.empty()) {
            spec.lines.push_back({""});
            spec.lines.push_back({"Not as heard:"});
            for (const std::string& n : f.notes) spec.lines.push_back({"• " + n});
        }
    }
    gemui::Alert::open(desk_, std::move(spec));
}

void MainWindow::saveFileAs(FileKind kind, std::string text) {
    Host::FileDialog d;
    d.title = kind == FileKind::Patches ? "Export the patch library" : "Save " + std::string(kindName(kind)) + " as";
    d.patterns = "*.json";
    d.path = host_.session().files[size_t(kind)];
    d.name = std::string(plugin::defaultFileName(kind));
    host_.chooseFileToSave(d, [this, kind, text = std::move(text)](const std::string& path) {
        const std::string file = plugin::ensureJson(path);  // ensureJson of the prototype
        if (!file.empty()) writeFile(kind, file, text);
    });
}

void MainWindow::writeFile(FileKind kind, const std::string& path, const std::string& text) {
    if (!host_.writeFile(path, text)) {
        gemui::Alert::Spec spec;
        spec.lines = {{"The file could not be written:"}, {path}};
        gemui::Alert::open(desk_, std::move(spec));
        return;
    }
    host_.session().files[size_t(kind)] = path;
}

void MainWindow::loadFile(FileKind kind) {
    openFile(kind, [this, kind](const std::string& text) { applyFile(kind, text); });
}

// Save (ask false) rewrites the file remembered in the session for this kind without a dialogue; Save as, or a first
// Save, asks where. The text is made now, before the dialogue, so it is what was on screen when the entry was picked.
void MainWindow::saveFile(FileKind kind, bool ask) {
    std::string text = fileText(kind);
    const std::string known = host_.session().files[size_t(kind)];
    if (!ask && !known.empty()) {
        writeFile(kind, known, text);
        return;
    }
    saveFileAs(kind, std::move(text));
}

// JSON text of a file kind, in the prototype's format (plugincore/ProjectFiles.h), from the host's current state.
std::string MainWindow::fileText(FileKind kind) const {
    const plugin::NonParamState x = host_.extras();
    switch (kind) {
    case FileKind::Preset: {
        plugin::ParamValues v{};
        for (int i = 0; i < plugin::kParamCount; ++i) v[size_t(i)] = host_.param(i);
        return plugin::dumpPreset(v, x);
    }
    case FileKind::Pattern: return plugin::dumpPatternFile(x.seq.cur());
    case FileKind::Song: return plugin::dumpSongFile(x.seq, x.global.songMode);
    case FileKind::Kit: return plugin::dumpKitFile(x.kit);
    case FileKind::Patches: break;  // Patches window
    }
    return {};
}

// A JSON file read: parsed, then applied, or refused with the prototype's alert and nothing changed. A preset replaces
// the whole project; a pattern replaces the current pattern; a Song file replaces the Song, the Song mode and the
// patterns it carries (applySongFile); a kit file replaces the Chipdrum kit.
void MainWindow::applyFile(FileKind kind, const std::string& text) {
    switch (kind) {
    case FileKind::Preset: {
        // loadPresetObj: no undo step, like the prototype.
        plugin::ParamValues now{};
        for (int i = 0; i < plugin::kParamCount; ++i) now[size_t(i)] = host_.param(i);
        const plugin::PresetRead r = plugin::parsePreset(text, now, host_.extras());
        if (r.status == plugin::PresetRead::Status::NotPreset) {
            showMessage("Not a STYM2149 preset.");
            return;
        }
        if (r.status != plugin::PresetRead::Status::Ok) {
            showMessage(readError(kind));
            return;
        }
        panic();
        host_.loadProject(r.values, r.extras);
        for (int i = 0; i < plugin::kParamCount; ++i) values_[size_t(i)] = host_.param(i);
        pullExtras();
        if (r.newer) {
            gemui::Alert::Spec spec;
            spec.lines = {{"This preset comes from a newer STYM2149."}, {"Only its prototype settings were read."}};
            gemui::Alert::open(desk_, std::move(spec));
        }
        return;
    }
    case FileKind::Pattern: {
        bool newer = false;
        const std::optional<ymseq::Pattern> p = plugin::parsePatternFile(text, &newer);
        if (!p) {
            showMessage(readError(kind));
            return;
        }
        seq_.pushUndo();
        seq_.edit([&p](plugin::NonParamState& s) { s.seq.cur() = *p; });
        if (newer) newerFileAlert();
        return;
    }
    case FileKind::Song: {
        const std::optional<plugin::SongFile> f = plugin::parseSongFile(text);
        if (!f) {
            showMessage(readError(kind));
            return;
        }
        song_.pushUndo();
        seq_.edit([&f](plugin::NonParamState& s) {
            bool on = s.global.songMode;
            plugin::applySongFile(*f, s.seq, on);
            s.global.songMode = on;
        });
        if (f->newer) newerFileAlert();
        return;
    }
    case FileKind::Kit: {
        bool newer = false;
        const std::optional<ymengine::KitParams> k = plugin::parseKitFile(text, extras_.kit, &newer);
        if (!k) {
            showMessage(readError(kind));
            return;
        }
        UndoEntry e;  // pushKitUndo (the prototype took none)
        e.kind = UndoEntry::Kind::Kit;
        host_.session().undo.push(undoEntryLike(e));
        host_.editExtras([&k](plugin::NonParamState& s) { s.kit = *k; });
        pullExtras();
        if (newer) newerFileAlert();
        return;
    }
    case FileKind::Patches: break;  // Patches window
    }
}

}  // namespace stym::pluginui
