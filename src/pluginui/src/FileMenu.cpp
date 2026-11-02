// STYM2149 — pluginui: File menu, STYM2149 files and the other formats
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Load, Save and Save as of a preset, a pattern, a Song and a kit (File menu of the prototype) in the binary format,
// through the file dialogues of the host; Play... in the YM Player; Import > MIDI file, Export pattern > MIDI and
// Export song > MIDI; Import > Tracker song: XM, MOD, S3M and OKT
// modules, maxYMiser songs and YM-Tracker songs, with Export song > YM-Tracker; Import > SNDH /
// YM file as patterns. The refusals reuse the alerts of the prototype; loading a pattern, a Song or a kit
// and the MIDI, tracker, YM and SNDH imports take an undo step (pushUndo, pushSongUndo, pushKitUndo). The exports (MIDI, WAV and stems, registers,
// trackers) and the reading of a YM, SNDH, maxYMiser or YM-Tracker tune run on a worker thread, under the busy bee, and
// end with an alert (PLAN_M7).
//
// Every host dialogue is asynchronous: the function returns at once, and the callback comes later on the message
// thread with the chosen path (never if the user cancels or the editor closes). An export follows the same pattern
// each time: copy the parameter values and the non-automatable state, start the job (startJob) with a `work` that
// renders on the worker thread from those copies only, and a `done` that runs on the message thread (from tick, through
// BackgroundJob::poll) to write the file or commit it and open the final alert. Both lambdas hold the same shared_ptr
// to the result: `work` fills it, `done` reads it (empty when the render was cancelled).
#include "pluginui/MainWindow.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <memory>
#include <span>
#include <utility>

#include "gemui/Alert.h"
#include "plugincore/AudioExport.h"
#include "plugincore/Mapping.h"
#include "plugincore/MaxYmExport.h"
#include "plugincore/MaxYmImport.h"
#include "plugincore/MidiFiles.h"
#include "plugincore/ProjectFiles.h"
#include "plugincore/RegisterFiles.h"
#include "plugincore/RegisterImport.h"
#include "plugincore/XmImport.h"
#include "plugincore/TTrakExport.h"
#include "plugincore/TTrakImport.h"
#include "plugincore/YmtExport.h"
#include "plugincore/YmtImport.h"
#include "plugincore/YmTune.h"
#include "ymio/Lzh.h"
#include "ymio/MaxYm.h"
#include "ymio/TTrak.h"
#include "ymio/YmTracker.h"
#include "ymseq/Tables.h"

namespace stym::pluginui {

using plugin::FileKind;
using plugin::Track;

namespace {

// Maximum size of a STYM2149 file or a patch library read: a project with all 32 patterns created and its samples is a
// few MB.
constexpr size_t kMaxFileBytes = size_t(64) << 20;
// YM or SNDH dump: an SNDH fits in the memory of the machine (4 MB); an uncompressed YM6 of several minutes, with its
// digidrums, is a few MB.
constexpr size_t kMaxYmBytes = size_t(32) << 20;

// Configuration file of the standalone application: the state as a host saves it.
constexpr const char* kConfigExtension = ".stymcfg";

std::string_view kindName(FileKind k) {
    switch (k) {
    case FileKind::Preset: return "preset";
    case FileKind::Pattern: return "pattern";
    case FileKind::Song: return "song";
    case FileKind::Kit: return "drum kit";
    case FileKind::Patches: break;  // Patches window
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

// WAV dialogue: formats and rates, and their keys in the preferences ("host": the host's own rate, like the
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

// Open dialogue of a patch library (Import of the Patches window, JSON): suggests the library file last exported, reads
// it whole (refused beyond kMaxFileBytes) and gives its text to `read`.
void MainWindow::openPatchLibrary(std::function<void(const std::string& text)> read) {
    Host::FileDialog d;
    d.title = "Import a patch library";
    d.patterns = "*.json";
    d.path = host_.session().patchFile;
    host_.chooseFileToOpen(d, [this, read = std::move(read)](const std::string& path) {
        const std::optional<std::string> text = host_.readFile(path, kMaxFileBytes);
        if (!text) {
            showMessage(readError(FileKind::Patches));
            return;
        }
        read(*text);
    });
}

// File > Play...: one dialogue that shows every format the YM Player plays; the content of the file decides,
// never its extension (playBytes).
void MainWindow::playFile() {
    Host::FileDialog d;
    d.title = "Play a file";
    d.patterns = "*.ym;*.ym2;*.ym3;*.ym5;*.ym6;*.sndh;*.snd;*.mys;*.myv;*.ymt;*.tpm;*.xm;*.mod;*.s3m;*.okt;*.stym;"
                 "*.stympat;*.stymsng";
    d.path = host_.session().playFile;
    host_.chooseFileToOpen(d, [this](const std::string& path) {
        host_.session().playFile = path;
        const std::optional<std::string> bytes = host_.readFile(path, kMaxFileBytes);
        if (!bytes) {
            showMessage("The file could not be read.");
            return;
        }
        playBytes(*bytes, path);
    });
}

namespace {

void playRefusal(gemui::Desktop& desk, const std::string& fileName, const std::string& why) {
    gemui::Alert::Spec spec;
    spec.lines = {{"The file cannot be played."}, {""}, {fileName}, {why}};
    gemui::Alert::open(desk, std::move(spec));
}

// Kind of content of a STYM2149 file, in the alerts of Load and Play (by FileKind; a patch library is never a binary
// file).
constexpr std::array<const char*, plugin::kFileKinds - 1> kHolds = {
    "This file holds a project.", "This file holds a pattern.", "This file holds a song.", "This file holds a drum kit."};

bool isMys(std::string_view b) { return b.substr(0, 8) == "MYM0TRAK"; }
bool isMyv(std::string_view b) { return b.size() >= 40 && b.substr(32, 8) == "MYM1INST"; }

}  // namespace

// The content of a file played: a YM dump or an SNDH file (a maXYmizer .SND too, played by its own
// routine) goes to the YM Player as before (loadYm); a STYM2149 file (binary), a maxYMiser song, a YM-Tracker
// song, a TTrak module, an XM, MOD, S3M or OKT module becomes a copy of the project that a second engine plays
// (playProject); anything else is refused with an alert.
void MainWindow::playBytes(const std::string& bytes, const std::string& path) {
    const std::string name = baseName(path);
    const std::span<const uint8_t> data(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    // STYM2149 file.
    if (plugin::isNativeFile(bytes)) {
        const plugin::NativeRead r = plugin::readNativeFile(bytes);
        if (!r.ok) {
            playRefusal(desk_, name, r.newer ? "It comes from a newer STYM2149." : "Not a valid STYM2149 file.");
            return;
        }
        playStym(r, name);
        return;
    }
    // maxYMiser song and its voiceset, converted on the worker thread.
    if (isMys(bytes) || isMyv(bytes)) {
        maxYmPair(path, bytes, [this](std::string_view song, std::string_view voices, const std::string& songName) {
            playMaxYm(song, voices, songName);
        });
        return;
    }
    // XM, OKT or S3M module: a signature.
    const std::optional<plugin::ModuleFormat> module = plugin::moduleFormat(data);
    if (module && *module != plugin::ModuleFormat::Mod) {
        playModule(data, name, *module);
        return;
    }
    // YM-Tracker song: "YMT1".
    if (bytes.rfind("YMT1", 0) == 0) {
        ymio::YmtParseResult parsed = ymio::parseYmt(data);
        if (!parsed.song) {
            playRefusal(desk_, name, parsed.error);
            return;
        }
        playYmt(std::make_shared<const ymio::YmtSong>(std::move(*parsed.song)), name);
        return;
    }
    // TTrak module: "GODMOD30".
    if (ymio::isTtrakModule(data)) {
        ymio::TtrakResult<ymio::TtrakModule> parsed = ymio::parseTtrakModule(data);
        if (!parsed.value) {
            playRefusal(desk_, name, parsed.error);
            return;
        }
        playTtrak(std::make_shared<const ymio::TtrakModule>(std::move(*parsed.value)), name);
        return;
    }
    // YM dump or SNDH file, which the YM Player reads itself.
    const std::string error = host_.loadYm(bytes, name);
    if (error.empty()) {
        ymWin_.show();
        return;
    }
    if (error != "Unsupported file") {
        gemui::Alert::Spec spec;
        spec.lines = {{error}};
        gemui::Alert::open(desk_, std::move(spec));
        return;
    }
    // MOD module: no signature at its start, tried last.
    if (module) {
        playModule(data, name, *module);
        return;
    }
    gemui::Alert::Spec spec;
    spec.lines = {{"Unsupported file"},
                  {""},
                  {"Expected a YM, SNDH, maxYMiser, YM-Tracker,"},
                  {"TTrak, XM, MOD, S3M, OKT or STYM2149 file."}};
    gemui::Alert::open(desk_, std::move(spec));
}

// A STYM2149 file played: a project as it is; a pattern or a Song over a copy of the current project.
void MainWindow::playStym(const plugin::NativeRead& r, const std::string& name) {
    plugin::ParamValues v{};
    for (int i = 0; i < plugin::kParamCount; ++i) v[size_t(i)] = host_.param(i);
    plugin::NonParamState x = host_.extras();
    bool song = true;
    switch (r.kind) {
    case FileKind::Preset:
        v = r.values;
        x = r.extras;
        break;
    case FileKind::Pattern:
        x.seq.cur() = r.pattern;
        song = false;
        break;
    case FileKind::Song: {
        bool on = false;
        plugin::applySongFile(r.song, x.seq, on);
        break;
    }
    case FileKind::Kit:
    case FileKind::Patches: {
        gemui::Alert::Spec spec;
        spec.lines = {{"Nothing to play in this file."}, {kHolds[size_t(r.kind)]}};
        gemui::Alert::open(desk_, std::move(spec));
        return;
    }
    }
    playProject(v, std::move(x), song, name + "  -  STYM2149");
}

// An XM, MOD, S3M or OKT module played: converted as File > Import does it, on a copy of the project.
void MainWindow::playModule(std::span<const uint8_t> data, const std::string& name, plugin::ModuleFormat format) {
    plugin::NonParamState x = host_.extras();
    auto convert = [&](ymengine::VoiceMode mode) {
        x = host_.extras();
        return plugin::importModule(format, data, mode, x);
    };
    plugin::XmImportResult r = convert(plugin::voiceMode(values_));
    // More channels than the mode plays: the copy plays in 16 or 32 ch, without asking.
    if (r.import && plugin::xmWantsStudio(*r.import)) r = convert(plugin::xmStudioMode(*r.import));
    if (!r.import) {
        playRefusal(desk_, name, r.error);
        return;
    }
    plugin::ParamValues v{};
    for (int i = 0; i < plugin::kParamCount; ++i) v[size_t(i)] = host_.param(i);
    plugin::xmImportValues(*r.import, v);
    x.seq.select(0, false);
    playProject(v, std::move(x), true, name + "  -  " + plugin::moduleFormatName(format));
}

// A maxYMiser song played: read, then converted under the bee on a copy of the project as File > Import does it.
void MainWindow::playMaxYm(std::string_view songBytes, std::string_view voiceBytes, const std::string& name) {
    auto span = [](std::string_view b) {
        return std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(b.data()), b.size());
    };
    ymio::MaxYmResult<ymio::MysSong> song = ymio::parseMys(span(songBytes));
    ymio::MaxYmResult<ymio::MyvVoiceset> voices = ymio::parseMyv(span(voiceBytes));
    if (!song.value || !voices.value) {
        playRefusal(desk_, name, song.value ? voices.error : song.error);
        return;
    }
    auto tune = std::make_shared<const ymio::MaxYmSong>(ymio::MaxYmSong{std::move(*song.value), std::move(*voices.value)});
    playConverted([tune](plugin::NonParamState& x) { return plugin::importMaxYm(tune->song, tune->voices, x); },
                  name + "  -  maxYMiser");
}

void MainWindow::playYmt(std::shared_ptr<const ymio::YmtSong> song, const std::string& name) {
    playConverted([song](plugin::NonParamState& x) { return plugin::importYmt(*song, x); }, name + "  -  YM-Tracker");
}

void MainWindow::playTtrak(std::shared_ptr<const ymio::TtrakModule> module, const std::string& name) {
    playConverted([module](plugin::NonParamState& x) { return plugin::importTtrak(*module, x); }, name + "  -  TTrak");
}

// A tracker song converted on the worker thread (registerImportValues for the settings), then played.
void MainWindow::playConverted(std::function<plugin::RegisterImportResult(plugin::NonParamState&)> convert,
                               const std::string& name) {
    if (exporting()) {
        showMessage("An export is already running.");
        return;
    }
    auto x = std::make_shared<plugin::NonParamState>(host_.extras());
    auto out = std::make_shared<plugin::RegisterImportResult>();
    startJob([convert = std::move(convert), x, out](const std::atomic<bool>&) { *out = convert(*x); },
             [this, x, out, name] {
                 if (!out->import) {
                     playRefusal(desk_, name.substr(0, name.find("  -  ")), out->error);
                     return;
                 }
                 plugin::ParamValues v{};
                 for (int i = 0; i < plugin::kParamCount; ++i) v[size_t(i)] = host_.param(i);
                 plugin::registerImportValues(*out->import, v);
                 x->seq.select(0, false);
                 playProject(v, std::move(*x), true, name);
             });
}

// The copy of a project given to the YM Player, from the start of its Song, or of its current pattern when
// it has no Song (or `song` is false).
void MainWindow::playProject(const plugin::ParamValues& values, plugin::NonParamState x, bool song,
                             const std::string& name) {
    x.seq.viewBank = 0;
    const std::string error = host_.playProject(values, x, song && !x.seq.song.entries.empty(), name);
    if (!error.empty()) {
        playRefusal(desk_, name.substr(0, name.find("  -  ")), error);
        return;
    }
    ymWin_.show();
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

bool MainWindow::isMidiFile(std::string_view path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string_view::npos) return false;
    std::string ext(path.substr(dot + 1));
    for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    return ext == "mid" || ext == "midi";
}

// A MIDI file dropped on the window, the Step Sequencer's or any other (Indus, 27/10/2026 21:02); a sample
// dropped on the drawing of the Digidrums or DMA window (Indus, 30/10/2026 02:42).
bool MainWindow::dropFiles(const std::vector<std::string>& paths, std::optional<gemui::Point> at) {
    if (at) {
        const gemui::Widget* w = desk_.widgetAt(*at);
        const auto sample =
            std::find_if(paths.begin(), paths.end(), [](const std::string& f) { return !isMidiFile(f); });
        if (sample != paths.end() && w && (digi_.isWave(*w) || dma_.isWave(*w))) {
            const bool dma = dma_.isWave(*w);
            loadSampleFile(dma, dma ? dma_.slot() : digi_.slot(), *sample);
            return true;
        }
    }
    const auto it = std::find_if(paths.begin(), paths.end(), [](const std::string& f) { return isMidiFile(f); });
    if (it == paths.end()) return false;
    host_.session().midiFile = *it;
    const std::optional<std::string> bytes = host_.readFile(*it, plugin::kMidiMaxBytes);
    if (!bytes) showMessage("The file could not be read.");
    else loadMidiBytes(*bytes, baseName(*it));
    return true;
}

// Load... and a click on the drawing of the Digidrums and DMA windows: the audio files the host decodes, and the raw
// bytes of a DMA sample.
void MainWindow::chooseSample(bool dma, int slot) {
    Host::FileDialog d;
    d.title = (dma ? "Load a DMA sample into slot " : "Load a sample into slot ") + std::to_string(slot + 1);
    d.patterns = host_.audioFilePatterns() + (dma ? ";*.raw;*.spl;*.sam" : "");
    host_.chooseFileToOpen(d, [this, dma, slot](const std::string& path) { loadSampleFile(dma, slot, path); });
}

void MainWindow::loadSampleFile(bool dma, int slot, const std::string& path) {
    const int count = dma ? ymengine::kDmaSlotsMax : ymengine::kDigidrumSlotsMax;
    if (slot < 0 || slot >= count || path.empty()) return;
    auto load = [this, dma, slot, path] {
        if (dma) host_.loadDmaSample(slot, path);
        else host_.loadDigidrum(slot, path);
    };
    const bool full = dma ? !extras_.dmaSamples[size_t(slot)].empty() : !extras_.digidrums[size_t(slot)].empty();
    if (!full) return load();
    // The sample in place, named when it has a name, and the file that would replace it.
    const std::string& name = dma ? extras_.dmaNames[size_t(slot)] : extras_.digiSlots[size_t(slot)].name;
    std::string file = baseName(path);
    if (const size_t dot = file.find_last_of('.'); dot != std::string::npos && dot > 0) file.resize(dot);
    gemui::Alert::Spec spec;
    const std::string where = "Slot " + std::to_string(slot + 1);
    spec.lines = {{name.empty() ? where + " already holds a sample." : where + " already holds \"" + name + "\"."},
                  {"Replace it with \"" + file + "\"?"}};
    spec.confirmButtons("Replace");  // Cancel, the safest choice, by default
    gemui::Alert::open(desk_, std::move(spec), [load](int button, const std::string&) {
        if (button == gemui::Alert::kConfirmOk) load();
    });
}

void MainWindow::loadMidiBytes(std::string_view bytes, const std::string& fileName) {
    // midiImport: the file is converted on a copy of the project; if refused, nothing changes. Its steps are those of
    // the sequencer's resolution, its patterns as long as the file in the sequencer's bars.
    using ymengine::Generator;
    const std::span<const uint8_t> data(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    const ymengine::VoiceMode mode = plugin::voiceMode(values_);
    const int reso = std::clamp<int>(extras_.global.resolution, 0, ymseq::kResolutionCount - 1);
    int sig = ymseq::kDefaultSignature;
    for (int i = 0; i < ymseq::kSignatureCount; ++i)
        if (ymseq::kSignatures[size_t(i)].numerator == extras_.global.sigNum &&
            ymseq::kSignatures[size_t(i)].denominator == extras_.global.sigDen)
            sig = i;
    plugin::MidiImportOptions options;
    options.stepsPerQuarter = ymseq::kResolutions[size_t(reso)].stepsPerBeat;
    options.barSteps = ymseq::barSteps(sig, reso);
    // A file with one channel goes to the focused track, as drums on a drum track.
    options.track = cur_;
    options.trackDrums = drumTrack();
    auto convert = [&](Generator drums) {
        auto p = std::make_shared<ymseq::Project>(extras_.seq);
        options.drums = drums;
        std::optional<plugin::MidiImport> r = plugin::importMidi(data, mode, *p, options);
        return std::make_pair(std::shared_ptr<const ymseq::Project>(std::move(p)), std::move(r));
    };
    const auto chip = convert(Generator::Chipdrum);
    const std::shared_ptr<const ymseq::Project> p = chip.first;
    const std::optional<plugin::MidiImport>& r = chip.second;
    if (!r) {
        gemui::Alert::Spec spec;
        spec.lines = {{"Unsupported MIDI file"}, {""}, {"Expected a standard MIDI file (format 0 or 1) with notes."}};
        gemui::Alert::open(desk_, std::move(spec));
        return;
    }
    // Drums (channel 10, Indus, 27/10/2026 21:18): a track that already plays Chipdrum or Digidrum keeps it;
    // otherwise, or with no track left for them, an alert asks which of the two plays them (Digidrum in 3 channels:
    // the Digidrum track, converted apart). Cancel imports nothing.
    int drumAt = -1;
    for (int t = 0; t < plugin::kTracks; ++t)
        if (r->drum[size_t(t)]) drumAt = t;
    const Generator now = drumAt >= 0 ? plugin::effectiveGenerator(values_, drumAt) : Generator::Chip;
    std::function<void()> place;
    std::array<bool, plugin::kTracks> fills = r->on;  // tracks the import fills, whichever generator plays the drums
    if (!r->percussion || now == Generator::Chipdrum || now == Generator::Digidrum) {
        place = [this, p, r = *r, fileName, now] {
            applyMidiImport(*p, r, fileName, now == Generator::Digidrum ? Generator::Digidrum : Generator::Chipdrum);
        };
    } else {
        const bool chips = !ymengine::studioMode(mode);
        const auto digi = chips ? convert(Generator::Digidrum) : chip;
        if (!digi.second) return;  // the same file: cannot happen
        int digiAt = -1;
        for (int t = 0; t < plugin::kTracks; ++t) {
            fills[size_t(t)] = fills[size_t(t)] || digi.second->on[size_t(t)];
            if (digi.second->drum[size_t(t)]) digiAt = t;
        }
        place = [this, p, r = *r, pd = digi.first, rd = *digi.second, fileName, chips, digiAt] {
            gemui::Alert::Spec spec;
            spec.lines = {{"The MIDI file holds drums (channel 10)."}, {"Play them with Chipdrum or Digidrum?"}};
            if (chips)
                spec.lines.push_back({"Digidrum: on track " + std::to_string(digiAt + 1) + ", the Digidrum track.", 12});
            spec.buttons = {"Cancel", "Chipdrum", "Digidrum"};
            spec.defaultButton = spec.focusButton = 1;
            spec.escapeButton = 0;
            gemui::Alert::open(desk_, std::move(spec), [this, p, r, pd, rd, fileName](int button, const std::string&) {
                if (button == 1) applyMidiImport(*p, r, fileName, Generator::Chipdrum);
                if (button == 2) applyMidiImport(*pd, rd, fileName, Generator::Digidrum);
            });
        };
    }
    // Notes that the file replaces, on the tracks it fills in the patterns it takes: asked first (Indus, 27/10/2026
    // 21:02); the other tracks keep theirs, so a file on an empty track asks nothing (Indus, 28/10/2026
    // 02:06). Cancel, the default button, imports nothing.
    std::vector<int> held;
    for (int t = 0; t < plugin::kTracks; ++t) {
        if (!fills[size_t(t)]) continue;
        bool notes = false;
        for (int k = r->first; k < r->first + r->patterns && !notes; ++k) {
            const ymseq::Pattern& q = extras_.seq.patterns[size_t(k)];
            if (extras_.seq.used.test(size_t(k)) || k == extras_.seq.current) notes = q.lane(t).hasNotes(q.steps);
        }
        if (notes) held.push_back(t + 1);
    }
    if (held.empty()) {
        place();
        return;
    }
    std::string tracks = held.size() == 1 ? "Track " + std::to_string(held[0]) + " holds"
                         : held.size() > 3 ? std::to_string(held.size()) + " tracks hold"
                                           : "Tracks ";
    if (held.size() == 2 || held.size() == 3) {
        for (size_t i = 0; i < held.size(); ++i)
            tracks += (i == 0 ? "" : i + 1 == held.size() ? " and " : ", ") + std::to_string(held[i]);
        tracks += " hold";
    }
    const std::string which = r->patterns > 1 ? "patterns " + std::to_string(r->first + 1) + " to " +
                                                    std::to_string(r->first + r->patterns)
                                              : "pattern " + std::to_string(r->first + 1);
    gemui::Alert::Spec spec;
    spec.lines = {{tracks + " notes. Replace them?"}, {"The MIDI file fills " + which + ".", 12}};
    spec.confirmButtons("Replace");
    gemui::Alert::open(desk_, std::move(spec), [place](int button, const std::string&) {
        if (button == gemui::Alert::kConfirmOk) place();
    });
}

void MainWindow::applyMidiImport(const ymseq::Project& p, const plugin::MidiImport& r, const std::string& fileName,
                                 ymengine::Generator drums) {
    // One undo step for the whole import (the prototype kept only the current pattern, pushUndo): the patterns up to
    // the last one filled.
    UndoEntry e;
    e.kind = UndoEntry::Kind::Import;
    e.index = r.first + r.patterns;
    host_.session().undo.push(undoEntryLike(e));
    // The filled tracks switched on, Chipdrum or Digidrum for the drums (channel 10); a melodic track keeps a
    // melodic generator, Chip Voice instead of a drum one. The other tracks stay as they are. Tempo of the
    // file (GLOB.bpm) unless the file joins notes of other tracks. One host gesture per parameter that changes,
    // values_ updated at once; the import's undo step holds them all.
    auto gesture = [this](int idx, float v) {
        if (host_.param(idx) == v) return;
        host_.beginGesture(idx);
        host_.setParam(idx, v);
        host_.endGesture(idx);
        values_[size_t(idx)] = host_.param(idx);
    };
    for (int t = 0; t < plugin::kTracks; ++t) {
        if (!r.on[size_t(t)]) continue;
        gesture(plugin::paramIndex(t, Track::On), 1.0f);
        const auto g = ymengine::Generator(plugin::intValue(values_, t, Track::Generator));
        const bool drumNow = g == ymengine::Generator::Chipdrum || g == ymengine::Generator::Digidrum;
        if (r.drum[size_t(t)]) gesture(plugin::paramIndex(t, Track::Generator), float(int(drums)));
        else if (drumNow) gesture(plugin::paramIndex(t, Track::Generator), float(int(ymengine::Generator::Chip)));
    }
    if (!r.kept) gesture(plugin::paramIndex(plugin::Global::FreeTempo), plugin::midiImportTempo(r));
    // Patterns and Song; the first pattern filled becomes current (while playing, at the end of the loop, like a
    // pattern choice).
    const bool play = seq_.playing();
    bool changed = false;
    host_.editExtras([&](plugin::NonParamState& s) {
        for (int k = r.first; k < r.first + r.patterns; ++k) s.seq.patterns[size_t(k)] = p.patterns[size_t(k)];
        for (int t = 0; t < plugin::kTracks; ++t)  // the filled tracks lose their own length
            if (r.on[size_t(t)]) s.seq.tracks[size_t(t)].length = p.tracks[size_t(t)].length;
        s.seq.used |= p.used;
        if (r.song) s.seq.song = p.song;
        s.seq.viewBank = r.first / ymseq::kBankSize;
        changed = s.seq.select(r.first, play);
    });
    if (changed) host_.seqCommand(Host::SeqCommand::Select, r.first);
    pullExtras();
    gemui::Alert::Spec spec;
    for (std::string& l : plugin::midiImportMessage(r, fileName)) spec.lines.push_back({std::move(l)});
    gemui::Alert::open(desk_, std::move(spec));
}

// File > Import > Tracker song (Indus, 29/10/2026 21:37): one entry for the trackers, the format given by the
// content of the file: XM, MOD, S3M and OKT modules, maxYMiser songs,
// YM-Tracker songs, TTrak modules.
void MainWindow::importTracker() {
    if (exporting()) {
        showMessage("An export is already running.");
        return;
    }
    Host::FileDialog d;
    d.title = "Import a tracker song";
    d.patterns = "*.xm;*.mod;*.s3m;*.okt;*.mys;*.myv;*.ymt;*.tpm;*.XM;*.MOD;*.S3M;*.OKT;*.MYS;*.MYV;*.YMT;*.TPM";
    d.path = host_.session().trackerFile;
    host_.chooseFileToOpen(d, [this](const std::string& path) {
        host_.session().trackerFile = path;
        const std::optional<std::string> bytes = host_.readFile(path, std::max(plugin::kXmMaxBytes, kMaxYmBytes));
        if (!bytes) {
            showMessage("The file could not be read.");
            return;
        }
        loadTrackerBytes(*bytes, path);
    });
}

void MainWindow::loadTrackerBytes(const std::string& bytes, const std::string& path) {
    const std::string name = baseName(path);
    const std::span<const uint8_t> data(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    if (isMys(bytes) || isMyv(bytes)) {
        maxYmPair(path, bytes, [this](std::string_view song, std::string_view voices, const std::string& songName) {
            loadMaxYmBytes(song, voices, songName);
        });
        return;
    }
    if (bytes.rfind("YMT1", 0) == 0) {
        loadYmtBytes(data, name);
        return;
    }
    if (ymio::isTtrakModule(data)) {
        loadTtrakBytes(data, name);
        return;
    }
    if (const std::optional<plugin::ModuleFormat> format = plugin::moduleFormat(data)) {
        loadXmBytes(bytes, name, *format);
        return;
    }
    gemui::Alert::Spec spec;
    spec.lines = {{"Tracker song not imported"},
                  {""},
                  {name},
                  {"Expected an XM, MOD, S3M, OKT, maxYMiser,"},
                  {"YM-Tracker or TTrak file."}};
    gemui::Alert::open(desk_, std::move(spec));
}

void MainWindow::loadXmBytes(std::string_view bytes, const std::string& fileName, plugin::ModuleFormat format) {
    // The module is converted on a copy of the project; if refused, nothing changes.
    auto data = std::make_shared<const std::string>(bytes);
    auto convert = [this, data, format](ymengine::VoiceMode mode) {
        auto xp = std::make_shared<plugin::NonParamState>(extras_);
        const std::span<const uint8_t> d(reinterpret_cast<const uint8_t*>(data->data()), data->size());
        plugin::XmImportResult r = plugin::importModule(format, d, mode, *xp);
        return std::pair{xp, std::move(r)};
    };
    auto first = convert(plugin::voiceMode(values_));
    const std::shared_ptr<plugin::NonParamState> xp = first.first;
    const plugin::XmImportResult& r = first.second;
    if (!r.import) {
        gemui::Alert::Spec spec;
        spec.lines = {{std::string(plugin::moduleFormatName(format)) + " not imported"}, {""}, {fileName}, {r.error}};
        gemui::Alert::open(desk_, std::move(spec));
        return;
    }
    if (!plugin::xmWantsStudio(*r.import)) {
        confirmXmImport(xp, std::make_shared<const plugin::XmImport>(*r.import), fileName);
        return;
    }
    // More channels than the 8 DMA tracks of a 3-channel mode (Indus, 29/10/2026 19:08), or than the 16 tracks
    // of 16 ch: the studio mode that plays them all, 16 or 32 ch, or the current mode. The studio mode by
    // default, the one that plays the whole module.
    const ymengine::VoiceMode now = plugin::voiceMode(values_), wide = plugin::xmStudioMode(*r.import);
    const bool dual = now == ymengine::VoiceMode::Dual3, sixteen = now == ymengine::VoiceMode::Studio16;
    const bool to32 = wide == ymengine::VoiceMode::Studio32;
    gemui::Alert::Spec spec;
    spec.lines = {{"The module plays " + std::to_string(r.import->played) + " channels."},
                  {to32 ? "32 ch plays up to 32 of them on DMA tracks;" : "16 ch plays up to 16 of them on DMA tracks;"},
                  {sixteen ? std::string("16 ch plays 16 and leaves the others out.")
                           : std::string(dual ? "Dual chip 2x3ch" : "Authentic 3ch") +
                                 " plays 8 and leaves the others out."}};
    spec.buttons = {"Cancel", sixteen ? "16 ch" : dual ? "2x3ch" : "3ch", to32 ? "32 ch" : "16 ch"};
    spec.defaultButton = spec.focusButton = 2;
    spec.escapeButton = 0;
    auto kept = std::make_shared<const plugin::XmImport>(*r.import);
    gemui::Alert::open(desk_, std::move(spec), [this, convert, xp, kept, fileName, wide](int b, const std::string&) {
        if (b == 1) {
            confirmXmImport(xp, kept, fileName);
        } else if (b == 2) {
            auto studio = convert(wide);
            if (studio.second.import)
                confirmXmImport(studio.first, std::make_shared<const plugin::XmImport>(*studio.second.import),
                                fileName);
        }
    });
}

// Its samples, on the STE it sets, may go past the memory of the Atari: asked first. None in 16 or 32 ch.
void MainWindow::confirmXmImport(std::shared_ptr<plugin::NonParamState> xp, std::shared_ptr<const plugin::XmImport> mp,
                                 const std::string& fileName) {
    plugin::ParamValues after{};
    for (int i = 0; i < plugin::kParamCount; ++i) after[size_t(i)] = host_.param(i);
    plugin::xmImportValues(*mp, after);
    plugin::NonParamState next = extras_;
    for (int k = 0; k < mp->instruments; ++k) next.dmaSamples[size_t(k)] = xp->dmaSamples[size_t(k)];
    confirmMemory(next, plugin::voiceMode(after), plugin::steMachine(after),
                  [this, xp, mp, fileName] { applyXmImport(*xp, *mp, fileName); });
}

void MainWindow::applyXmImport(const plugin::NonParamState& x, const plugin::XmImport& m, const std::string& fileName) {
    UndoEntry e;
    e.kind = UndoEntry::Kind::Import;
    e.index = m.patterns;
    e.xm = true;
    e.slots = m.instruments;
    host_.session().undo.push(undoEntryLike(e));
    // STE machine (DMA sound), tracks switched on or off, DMA type of the tracks in 32-channel mode (in
    // 3-channel mode, the DMA tracks are DMA anyway), VBL rate (Ticks) or free tempo (BPM): xmImportValues.
    plugin::ParamValues v{};
    for (int i = 0; i < plugin::kParamCount; ++i) v[size_t(i)] = host_.param(i);
    plugin::xmImportValues(m, v);
    // 16 or 32 ch chosen for a module of many channels.
    const bool newMode = iv(plugin::Global::VoiceMode) != int(m.mode);
    for (int i = 0; i < plugin::kParamCount; ++i) {
        if (host_.param(i) == v[size_t(i)]) continue;
        host_.beginGesture(i);
        host_.setParam(i, v[size_t(i)]);
        host_.endGesture(i);
        values_[size_t(i)] = host_.param(i);
    }
    if (newMode) panic();  // as a change of Voices
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

// File > Import > SNDH / YM file (one entry, Indus, 29/10/2026 21:40): the tune becomes patterns
// (RegisterImport.h), its format read from its content as the YM Player does.
void MainWindow::importRegisters() {
    if (exporting()) {
        showMessage("An export is already running.");
        return;
    }
    Host::FileDialog d;
    d.title = "Import an SNDH or YM file";
    d.patterns = "*.sndh;*.snd;*.ym;*.ym2;*.ym3;*.ym5;*.ym6";
    d.path = host_.session().sndhFile;
    host_.chooseFileToOpen(d, [this](const std::string& path) {
        host_.session().sndhFile = path;
        const std::optional<std::string> bytes = host_.readFile(path, kMaxYmBytes);
        if (!bytes) {
            showMessage("The file could not be read.");
            return;
        }
        loadRegisterBytes(*bytes, baseName(path));
    });
}

namespace {

void registerRefusal(gemui::Desktop& desk, bool sndh, const std::string& fileName, const std::string& why) {
    gemui::Alert::Spec spec;
    spec.lines = {{sndh ? "SNDH not imported" : "YM not imported"}, {""}, {fileName}, {why}};
    if (why == "Unsupported file")  // the YM Player's alert (loadYmBytes)
        spec.lines.push_back({"Expected a YM register dump (YM2 / YM3 / YM5 / YM6) or an SNDH file."});
    gemui::Alert::open(desk, std::move(spec));
}

// Options of the grid dialogue: common values, and the guess when it is none of them.
std::vector<int> withGuess(std::vector<int> values, int guess) {
    if (std::find(values.begin(), values.end(), guess) == values.end()) {
        values.push_back(guess);
        std::sort(values.begin(), values.end());
    }
    return values;
}

}  // namespace

void MainWindow::loadRegisterBytes(std::string_view bytes, const std::string& fileName) {
    const std::span<const uint8_t> data(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    plugin::YmTuneLoad l = plugin::loadYmTune(data, fileName);
    if (!l.tune) {
        registerRefusal(desk_, ymio::looksLikeSndh(data), fileName, l.error);
        return;
    }
    std::shared_ptr<const plugin::YmTune> tune = std::move(l.tune);
    const int subsongs = tune->subsongs();
    // An SNDH saved by maxYMiser: the song of the subsong, read from the file rather than heard.
    std::vector<ymio::MaxYmSong> found;
    if (tune->sndh) found = ymio::maxYmSongsIn(tune->sndh->image);
    if (found.size() != size_t(std::max(subsongs, 1))) found.clear();
    auto songs = std::make_shared<const std::vector<ymio::MaxYmSong>>(std::move(found));
    auto read = [this, tune, songs](int subsong) {
        if (songs->empty()) {
            readRegisterTune(tune, subsong);
            return;
        }
        auto song = std::make_shared<const ymio::MaxYmSong>((*songs)[size_t(subsong - 1)]);
        convertMaxYm(song, tune->fileName, [this, tune, subsong] { readRegisterTune(tune, subsong); });
    };
    if (subsongs <= 1) {
        read(1);
        return;
    }
    // Several subsongs: which one (the file's default first).
    gemui::Alert::Spec spec;
    spec.lines = {{"Subsong to import"}, {"(1 to " + std::to_string(subsongs) + "):"}};
    spec.promptButtons();
    spec.inputText = std::to_string(tune->defaultSubsong());
    spec.inputChars = 10;
    spec.inputMax = 3;
    gemui::Alert::open(desk_, std::move(spec), [read, subsongs](int button, const std::string& text) {
        if (button != gemui::Alert::kPromptOk) return;
        // Nothing typed (no digit): no import. Out of range: clamped.
        int k = 0;
        bool digit = false;
        for (char c : text)
            if (c >= '0' && c <= '9') {
                k = std::min(k * 10 + (c - '0'), 1000);
                digit = true;
            }
        if (digit) read(std::clamp(k, 1, subsongs));
    });
}

// The frames of the tune (the emulated Atari plays an SNDH for its length, 3 minutes without one) and the grid they
// suggest, on the worker thread; then the grid dialogue, or the refusal of a tune without notes.
void MainWindow::readRegisterTune(std::shared_ptr<const plugin::YmTune> tune, int subsong) {
    struct Read {
        std::shared_ptr<const plugin::RegisterTune> frames;
        plugin::RegisterGrid grid;
        std::string error;
    };
    auto out = std::make_shared<Read>();
    const bool sndh = tune->sndh != nullptr;
    startJob(
        [tune, subsong, out](const std::atomic<bool>& cancel) {
            std::optional<plugin::RegisterTune> t;
            if (tune->ym) {
                t = plugin::registerTune(*tune->ym);
            } else {
                t = plugin::registerTune(*tune->sndh, subsong, plugin::sndhImportFrames(*tune->sndh, subsong), &cancel);
                if (!t) {
                    out->error = "The replay routine stopped before its first frame.";
                    return;
                }
            }
            out->grid = plugin::guessRegisterGrid(*t);
            if (out->grid.notes == 0) {
                // The reason, as the conversion gives it (on a state of its own: nothing is kept).
                plugin::NonParamState scratch;
                out->error = plugin::importRegisters(*t, out->grid, scratch).error;
                return;
            }
            out->frames = std::make_shared<const plugin::RegisterTune>(std::move(*t));
        },
        [this, out, tune, sndh] {
            if (!out->frames) {
                if (!out->error.empty()) registerRefusal(desk_, sndh, tune->fileName, out->error);
                return;
            }
            registerGridDialog(out->frames, out->grid, tune->fileName, sndh);
        });
}

// Grid dialogue: the guess picked, the user may choose another step or pattern length; the conversion runs on the
// worker thread on a copy of the project.
void MainWindow::registerGridDialog(std::shared_ptr<const plugin::RegisterTune> tune, const plugin::RegisterGrid& guess,
                                    const std::string& fileName, bool sndh) {
    const std::vector<int> frames = withGuess({2, 3, 4, 6, 8, 12, 16, 24}, guess.framesPerStep);
    const std::vector<int> steps = withGuess({16, 32, 48, 64, 96, 128, 192, 256}, guess.stepsPerPattern);
    auto names = [](const std::vector<int>& v) {
        std::vector<std::string> out;
        for (int x : v) out.push_back(std::to_string(x));
        return out;
    };
    auto index = [](const std::vector<int>& v, int x) {
        return int(std::find(v.begin(), v.end(), x) - v.begin());
    };
    gemui::Alert::Spec spec;
    spec.lines = {{sndh ? "Import the SNDH file as patterns" : "Import the YM file as patterns"},
                  {fileName},
                  {"Grid guessed from " + std::to_string(guess.notes) + " notes: " + std::to_string(guess.aligned) +
                   " % of them start on a step."},
                  {"A frame is a replay tick of the tune (" + std::to_string(tune->rate) + " Hz).", 12}};
    spec.choices = {{"Frames per step", names(frames), index(frames, guess.framesPerStep)},
                    {"Steps per pattern", names(steps), index(steps, guess.stepsPerPattern)}};
    spec.buttons = {"Cancel", "Import"};
    spec.defaultButton = 1;
    spec.escapeButton = 0;
    spec.enterOnFocus = true;
    spec.maxWidth = 640;
    gemui::Alert::open(desk_, std::move(spec),
                       [this, tune, frames, steps, fileName, sndh](int button, const std::vector<int>& picks) {
                           if (button != 1 || picks.size() != 2) return;
                           const int S = frames[size_t(std::clamp(picks[0], 0, int(frames.size()) - 1))];
                           const int L = steps[size_t(std::clamp(picks[1], 0, int(steps.size()) - 1))];
                           if (exporting()) {
                               showMessage("An export is already running.");
                               return;
                           }
                           auto x = std::make_shared<plugin::NonParamState>(host_.extras());
                           auto out = std::make_shared<plugin::RegisterImportResult>();
                           startJob(
                               [tune, S, L, x, out](const std::atomic<bool>&) {
                                   plugin::RegisterGrid g = plugin::guessRegisterGrid(*tune, S);
                                   g.stepsPerPattern = L;
                                   *out = plugin::importRegisters(*tune, g, *x);
                               },
                               [this, x, out, fileName, sndh] {
                                   if (!out->import) registerRefusal(desk_, sndh, fileName, out->error);
                                   else
                                       applyRegisterImport(*x, *out->import,
                                                           plugin::registerImportMessage(*out->import, fileName, sndh));
                               });
                       });
}

std::vector<int> MainWindow::registerImportParams() {
    std::vector<int> v = {plugin::paramIndex(plugin::Global::VoiceMode), plugin::paramIndex(plugin::Global::Mpe),
                          plugin::paramIndex(plugin::Global::VblRate), plugin::paramIndex(plugin::Global::A4Hz)};
    for (int t = 0; t < 4; ++t) v.push_back(plugin::paramIndex(t, Track::Level));
    return v;
}

void MainWindow::applyRegisterImport(const plugin::NonParamState& x, const plugin::RegisterImport& m,
                                     std::vector<std::string> message) {
    // The digidrums of the song, in Authentic 3ch, may go past the memory of the Atari: asked first.
    plugin::ParamValues after{};
    for (int i = 0; i < plugin::kParamCount; ++i) after[size_t(i)] = host_.param(i);
    plugin::registerImportValues(m, after);
    plugin::NonParamState next = extras_;
    if (m.digidrums) next.digidrums = x.digidrums;
    confirmMemory(next, plugin::voiceMode(after), plugin::steMachine(after),
                  [this, xp = std::make_shared<const plugin::NonParamState>(x),
                   mp = std::make_shared<const plugin::RegisterImport>(m), message = std::move(message)] {
                      commitRegisterImport(*xp, *mp, message);
                  });
}

void MainWindow::commitRegisterImport(const plugin::NonParamState& x, const plugin::RegisterImport& m,
                                      std::vector<std::string> message) {
    UndoEntry e;
    e.kind = UndoEntry::Kind::Import;
    e.index = m.patterns;
    e.registers = true;
    e.slots = m.instruments;
    e.digis = m.digidrums;
    host_.session().undo.push(undoEntryLike(e));
    auto gesture = [this](int idx, float v) {
        if (host_.param(idx) == v) return;
        host_.beginGesture(idx);
        host_.setParam(idx, v);
        host_.endGesture(idx);
        values_[size_t(idx)] = host_.param(idx);
    };
    // Authentic 3ch, tracks, VBL rate, Tuning A4 and tempo of the tune (registerImportValues; the digidrums of a
    // maxYMiser or YM-Tracker song on track 4); MPE Off and every note cut first when the voice mode
    // changes, as the Voices menu does.
    plugin::ParamValues v{};
    for (int i = 0; i < plugin::kParamCount; ++i) v[size_t(i)] = host_.param(i);
    plugin::registerImportValues(m, v);
    if (iv(plugin::Global::VoiceMode) != 1) {
        for (plugin::Global g : {plugin::Global::VoiceMode, plugin::Global::Mpe})
            gesture(plugin::paramIndex(g), v[size_t(plugin::paramIndex(g))]);
        panic();
    }
    for (int i = 0; i < plugin::kParamCount; ++i) gesture(i, v[size_t(i)]);
    // Patterns, Song, instruments, timing and the tracks' own lengths; the first pattern becomes current (while
    // playing, at the end of the loop, like a pattern choice).
    const bool play = seq_.playing();
    bool changed = false;
    host_.editExtras([&](plugin::NonParamState& s) {
        for (int k = 0; k < m.patterns; ++k) s.seq.patterns[size_t(k)] = x.seq.patterns[size_t(k)];
        s.seq.used |= x.seq.used;
        s.seq.song = x.seq.song;
        for (int k = 0; k < m.instruments; ++k) s.instruments[size_t(k)] = x.instruments[size_t(k)];
        for (int t = 0; t < 3; ++t) s.seq.tracks[size_t(t)].length = x.seq.tracks[size_t(t)].length;
        s.global.timing = x.global.timing;
        s.global.speed = x.global.speed;
        s.global.grooveLength = x.global.grooveLength;
        s.global.groove = x.global.groove;
        if (m.digidrums) {
            // The Digidrum track, its channel and the slots (those without a sample of the song stay as they were).
            s.seq.tracks[3].length = x.seq.tracks[3].length;
            s.global.digidrumChannel[0] = x.global.digidrumChannel[0];
            s.digiSlots = x.digiSlots;
            s.digidrums = x.digidrums;
        }
        s.seq.viewBank = 0;
        changed = s.seq.select(0, play);
    });
    if (changed) host_.seqCommand(Host::SeqCommand::Select, 0);
    pullExtras();
    if (cur_ >= voices()) focusTrack(0);
    gemui::Alert::Spec spec;
    for (std::string& l : message) spec.lines.push_back({std::move(l)});
    gemui::Alert::open(desk_, std::move(spec));
}

namespace {

void maxYmRefusal(gemui::Desktop& desk, const std::string& fileName, const std::string& why) {
    gemui::Alert::Spec spec;
    spec.lines = {{"maxYMiser song not imported"}, {""}, {fileName}, {why}};
    gemui::Alert::open(desk, std::move(spec));
}

}  // namespace

// The song and the voiceset of a maxYMiser file chosen (`bytes`, read from `path`, one or the other): the other one is
// the file of the same name beside it (extension in capitals or not), or the one chosen in a second dialogue.
void MainWindow::maxYmPair(const std::string& path, const std::string& bytes, MaxYmUse use) {
    const bool song = isMys(bytes);
    const size_t dot = path.find_last_of('.');
    const size_t slash = path.find_last_of("/\\");
    const std::string stem =
        dot != std::string::npos && (slash == std::string::npos || dot > slash) ? path.substr(0, dot) : path;
    auto load = [song, use](const std::string& first, const std::string& firstPath, const std::string& other,
                            const std::string& otherPath) {
        const std::string& songPath = song ? firstPath : otherPath;
        use(song ? first : other, song ? other : first, baseName(songPath));
    };
    for (const char* ext : song ? std::array{".MYV", ".myv", ".Myv"} : std::array{".MYS", ".mys", ".Mys"})
        if (const std::optional<std::string> other = host_.readFile(stem + ext, kMaxYmBytes)) {
            load(bytes, path, *other, stem + ext);
            return;
        }
    Host::FileDialog o;
    o.title = song ? "Choose the voiceset (.MYV) of the song" : "Choose the song (.MYS) of the voiceset";
    o.patterns = song ? "*.myv;*.MYV" : "*.mys;*.MYS";
    o.path = path;
    auto first = std::make_shared<const std::string>(bytes);
    host_.chooseFileToOpen(o, [this, first, path, load](const std::string& otherPath) {
        const std::optional<std::string> other = host_.readFile(otherPath, kMaxYmBytes);
        if (!other) {
            showMessage("The file could not be read.");
            return;
        }
        load(*first, path, *other, otherPath);
    });
}

void MainWindow::loadMaxYmBytes(std::string_view songBytes, std::string_view voiceBytes, const std::string& fileName) {
    auto span = [](std::string_view b) {
        return std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(b.data()), b.size());
    };
    ymio::MaxYmResult<ymio::MysSong> song = ymio::parseMys(span(songBytes));
    if (!song.value) {
        maxYmRefusal(desk_, fileName, song.error);
        return;
    }
    ymio::MaxYmResult<ymio::MyvVoiceset> voices = ymio::parseMyv(span(voiceBytes));
    if (!voices.value) {
        maxYmRefusal(desk_, fileName, voices.error);
        return;
    }
    convertMaxYm(std::make_shared<const ymio::MaxYmSong>(
                     ymio::MaxYmSong{std::move(*song.value), std::move(*voices.value)}),
                 fileName);
}

void MainWindow::convertMaxYm(std::shared_ptr<const ymio::MaxYmSong> song, const std::string& fileName,
                              std::function<void()> orElse) {
    if (exporting()) {
        showMessage("An export is already running.");
        return;
    }
    // The song played and converted on the worker thread, on a copy of the project.
    auto x = std::make_shared<plugin::NonParamState>(host_.extras());
    auto out = std::make_shared<plugin::RegisterImportResult>();
    startJob([song, x, out](const std::atomic<bool>&) { *out = plugin::importMaxYm(song->song, song->voices, *x); },
             [this, x, out, fileName, orElse = std::move(orElse)] {
                 if (out->import)
                     applyRegisterImport(*x, *out->import, plugin::maxYmImportMessage(*out->import, fileName));
                 else if (orElse) orElse();
                 else maxYmRefusal(desk_, fileName, out->error);
             });
}

namespace {

void ymtRefusal(gemui::Desktop& desk, const std::string& fileName, const std::string& why) {
    gemui::Alert::Spec spec;
    spec.lines = {{"YM-Tracker song not imported"}, {""}, {fileName}, {why}};
    gemui::Alert::open(desk, std::move(spec));
}

}  // namespace

// A YM-Tracker song of File > Import > Tracker song: unpacked here, its rows converted on the worker thread.
void MainWindow::loadYmtBytes(std::span<const uint8_t> data, const std::string& fileName) {
    if (exporting()) {
        showMessage("An export is already running.");
        return;
    }
    ymio::YmtParseResult parsed = ymio::parseYmt(data);
    if (!parsed.song) {
        ymtRefusal(desk_, fileName, parsed.error);
        return;
    }
    auto song = std::make_shared<const ymio::YmtSong>(std::move(*parsed.song));
    auto x = std::make_shared<plugin::NonParamState>(host_.extras());
    auto out = std::make_shared<plugin::RegisterImportResult>();
    startJob([song, x, out](const std::atomic<bool>&) { *out = plugin::importYmt(*song, *x); },
             [this, x, out, fileName] {
                 if (out->import)
                     applyRegisterImport(*x, *out->import, plugin::ymtImportMessage(*out->import, fileName));
                 else
                     ymtRefusal(desk_, fileName, out->error);
             });
}

namespace {

void ttrakRefusal(gemui::Desktop& desk, const std::string& fileName, const std::string& why) {
    gemui::Alert::Spec spec;
    spec.lines = {{"TTrak module not imported"}, {""}, {fileName}, {why}};
    gemui::Alert::open(desk, std::move(spec));
}

}  // namespace

// A TTrak module of File > Import > Tracker song: read here, played and converted on the worker thread.
void MainWindow::loadTtrakBytes(std::span<const uint8_t> data, const std::string& fileName) {
    if (exporting()) {
        showMessage("An export is already running.");
        return;
    }
    ymio::TtrakResult<ymio::TtrakModule> parsed = ymio::parseTtrakModule(data);
    if (!parsed.value) {
        ttrakRefusal(desk_, fileName, parsed.error);
        return;
    }
    auto module = std::make_shared<const ymio::TtrakModule>(std::move(*parsed.value));
    auto x = std::make_shared<plugin::NonParamState>(host_.extras());
    auto out = std::make_shared<plugin::RegisterImportResult>();
    startJob([module, x, out](const std::atomic<bool>&) { *out = plugin::importTtrak(*module, *x); },
             [this, x, out, fileName] {
                 if (out->import)
                     applyRegisterImport(*x, *out->import, plugin::ttrakImportMessage(*out->import, fileName));
                 else
                     ttrakRefusal(desk_, fileName, out->error);
             });
}

// File from a newer version of STYM2149 (key "stym"): read for what is known of it.
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
    d.name = song ? "stym2149-song.mid" : "stym2149-pattern.mid";  // prism-ym2149-*.mid of the prototype
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
// exportSongWav, exportSongStems of the prototype): GEM dialogue for the format, dialogue of the host, then the
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
        // Choice kept in the preferences: the next dialogue offers it.
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
    // prism-ym2149-pattern.wav, prism-ym2149-song-stems.zip... of the prototype.
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
// STYM2149) and export of the recorder take: dialogue of the host, capture and writing by the worker
// thread, alert at the end with what the file does not render.
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
    d.name = plugin::registerFileName(format, false);  // prism-ym2149-song.* of the prototype
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

// File > Export song > maxYMiser: the Song as maxYMiser's routine plays it, in a song and a voiceset of the
// same name (a name that TOS reads, 8.3, by default); the files are written once the conversion is done, then the
// alert gives what the song holds and what the routine does not play as heard.
void MainWindow::exportMaxYm() {
    if (extras_.seq.song.entries.empty()) {  // requireSong
        showMessage("Song is empty. Add entries first.");
        return;
    }
    if (exporting()) {
        showMessage("An export is already running.");
        return;
    }
    Host::FileDialog d;
    d.title = "Export the song for maxYMiser";
    d.patterns = "*.mys;*.MYS";
    d.name = "STYM2149.MYS";
    host_.chooseFileToSave(d, [this](const std::string& chosen) {
        const std::string path = plugin::ensureExtension(chosen, {".mys"});
        if (path.empty()) return;
        // The voiceset beside the song, its extension in the same case.
        const std::string voices = path.substr(0, path.size() - 4) + (path.back() == 'S' ? ".MYV" : ".myv");
        plugin::ParamValues v{};
        for (int i = 0; i < plugin::kParamCount; ++i) v[size_t(i)] = host_.param(i);
        auto x = std::make_shared<const plugin::NonParamState>(host_.extras());
        auto out = std::make_shared<std::optional<plugin::MaxYmExportResult>>();
        startJob([v, x, out](const std::atomic<bool>& cancel) { *out = plugin::exportMaxYm(v, *x, &cancel); },
                 [this, path, voices, out] {
                     if (!*out) return;
                     gemui::Alert::Spec spec;
                     const plugin::MaxYmExportResult& r = **out;
                     auto write = [this](const std::string& file, const std::vector<uint8_t>& bytes) {
                         return host_.writeFile(file, {reinterpret_cast<const char*>(bytes.data()), bytes.size()});
                     };
                     if (!r.value) {
                         spec.lines = {{"The song could not be exported:"}, {r.error}};
                     } else if (!write(path, ymio::writeMys(r.value->song))) {
                         spec.lines = {{"The file could not be written:"}, {path}};
                     } else if (!write(voices, ymio::writeMyv(r.value->voices))) {
                         spec.lines = {{"The file could not be written:"}, {voices}};
                     } else {
                         for (const std::string& l : plugin::maxYmExportMessage(*r.value, baseName(path), baseName(voices)))
                             spec.lines.push_back({l});
                     }
                     gemui::Alert::open(desk_, std::move(spec));
                 });
    });
}

// File > Export song > YM-Tracker: the song named after its file.
void MainWindow::exportYmt() {
    if (extras_.seq.song.entries.empty()) {  // requireSong
        showMessage("Song is empty. Add entries first.");
        return;
    }
    if (exporting()) {
        showMessage("An export is already running.");
        return;
    }
    Host::FileDialog d;
    d.title = "Export the song for YM-Tracker";
    d.patterns = "*.ymt;*.YMT";
    d.name = "STYM2149.ymt";
    host_.chooseFileToSave(d, [this](const std::string& chosen) {
        const std::string path = plugin::ensureExtension(chosen, {".ymt"});
        if (path.empty()) return;
        std::string title = baseName(path);
        title = title.substr(0, title.size() - 4);
        plugin::ParamValues v{};
        for (int i = 0; i < plugin::kParamCount; ++i) v[size_t(i)] = host_.param(i);
        auto x = std::make_shared<const plugin::NonParamState>(host_.extras());
        auto out = std::make_shared<plugin::YmtExportResult>();
        startJob([v, x, out, title](const std::atomic<bool>&) { *out = plugin::exportYmt(v, *x, title); },
                 [this, path, out] {
                     gemui::Alert::Spec spec;
                     if (!out->value) {
                         spec.lines = {{"The song could not be exported:"}, {out->error}};
                     } else {
                         const std::vector<uint8_t> bytes = ymio::writeYmt(out->value->song);
                         if (!host_.writeFile(path, {reinterpret_cast<const char*>(bytes.data()), bytes.size()}))
                             spec.lines = {{"The file could not be written:"}, {path}};
                         else
                             for (const std::string& l : plugin::ymtExportMessage(*out->value, baseName(path)))
                                 spec.lines.push_back({l});
                     }
                     gemui::Alert::open(desk_, std::move(spec));
                 });
    });
}

// File > Export song > TTrak: the module titled after its file (16 characters kept).
void MainWindow::exportTtrak() {
    if (extras_.seq.song.entries.empty()) {  // requireSong
        showMessage("Song is empty. Add entries first.");
        return;
    }
    if (exporting()) {
        showMessage("An export is already running.");
        return;
    }
    Host::FileDialog d;
    d.title = "Export the song for TTrak";
    d.patterns = "*.tpm;*.TPM";
    d.name = "STYM2149.TPM";
    host_.chooseFileToSave(d, [this](const std::string& chosen) {
        const std::string path = plugin::ensureExtension(chosen, {".tpm"});
        if (path.empty()) return;
        std::string title = baseName(path);
        title = title.substr(0, title.size() - 4);
        plugin::ParamValues v{};
        for (int i = 0; i < plugin::kParamCount; ++i) v[size_t(i)] = host_.param(i);
        auto x = std::make_shared<const plugin::NonParamState>(host_.extras());
        auto out = std::make_shared<std::optional<plugin::TtrakExportResult>>();
        startJob(
            [v, x, out, title](const std::atomic<bool>& cancel) { *out = plugin::exportTtrak(v, *x, title, &cancel); },
            [this, path, out] {
                if (!*out) return;
                const plugin::TtrakExportResult& r = **out;
                gemui::Alert::Spec spec;
                if (!r.value) {
                    spec.lines = {{"The song could not be exported:"}, {r.error}};
                } else {
                    const std::vector<uint8_t> bytes = ymio::writeTtrakModule(r.value->module);
                    if (!host_.writeFile(path, {reinterpret_cast<const char*>(bytes.data()), bytes.size()}))
                        spec.lines = {{"The file could not be written:"}, {path}};
                    else
                        for (const std::string& l : plugin::ttrakExportMessage(*r.value, baseName(path)))
                            spec.lines.push_back({l});
                }
                gemui::Alert::open(desk_, std::move(spec));
            });
    });
}

// Export of the recorder's take (Recorder window): the frames are already captured, so the job only builds the
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

// Patch library written (Export of the Patches window, JSON): always the dialogue, on the library file last exported,
// ".json" added if it is missing.
void MainWindow::savePatchLibrary(std::string text) {
    Host::FileDialog d;
    d.title = "Export the patch library";
    d.patterns = "*.json";
    d.path = host_.session().patchFile;
    d.name = std::string(plugin::kPatchLibraryFileName);
    host_.chooseFileToSave(d, [this, text = std::move(text)](const std::string& path) {
        const std::string file = plugin::ensureJson(path);  // ensureJson of the prototype
        if (!file.empty() && writeFile(file, text)) host_.session().patchFile = file;
    });
}

bool MainWindow::writeFile(const std::string& path, std::string_view bytes) {
    if (host_.writeFile(path, bytes)) return true;
    gemui::Alert::Spec spec;
    spec.lines = {{"The file could not be written:"}, {path}};
    gemui::Alert::open(desk_, std::move(spec));
    return false;
}

// File > Load: the binary file of that kind (.stym, .stympat, .stymsng, .stymkit). No JSON file is read: STYM2149
// has not been released, so none was written by it (Indus, 29/10/2026 14:20).
void MainWindow::loadFile(FileKind kind) {
    Host::FileDialog d;
    d.title = "Load " + std::string(kindName(kind));
    d.patterns = "*" + std::string(plugin::nativeExtension(kind));
    d.path = host_.session().files[size_t(kind)];
    host_.chooseFileToOpen(d, [this, kind](const std::string& path) {
        const std::optional<std::string> bytes = host_.readFile(path, kMaxFileBytes);
        if (!bytes) {
            showMessage(readError(kind));
            return;
        }
        const plugin::NativeRead r = plugin::readNativeFile(*bytes);
        if (!r.ok) {
            if (r.newer) {
                gemui::Alert::Spec spec;
                spec.lines = {{"This file comes from a newer STYM2149."}, {"It cannot be read by this version."}};
                gemui::Alert::open(desk_, std::move(spec));
            } else {
                showMessage(readError(kind));
            }
            return;
        }
        if (r.kind != kind) {  // a file of another kind: said, nothing changed
            gemui::Alert::Spec spec;
            spec.lines = {{std::string(readError(kind))}, {kHolds[size_t(r.kind)]}};
            gemui::Alert::open(desk_, std::move(spec));
            return;
        }
        putFile(r);
    });
}

// Save (ask false) rewrites the file remembered in the session for this kind without a dialogue; Save as, or a first
// Save, asks where. The file is made now, before the dialogue, so it is what was on screen when the entry was picked
// (binary format; its extension added if it is missing).
void MainWindow::saveFile(FileKind kind, bool ask) {
    plugin::ParamValues v{};
    for (int i = 0; i < plugin::kParamCount; ++i) v[size_t(i)] = host_.param(i);
    const std::vector<uint8_t> bytes = plugin::encodeNativeFile(kind, v, host_.extras());
    std::string data(bytes.begin(), bytes.end());
    const std::string known = host_.session().files[size_t(kind)];
    if (!ask && !known.empty()) {
        writeFile(known, data);
        return;
    }
    Host::FileDialog d;
    d.title = "Save " + std::string(kindName(kind)) + " as";
    d.patterns = "*" + std::string(plugin::nativeExtension(kind));
    d.path = known;
    d.name = plugin::nativeFileName(kind);
    host_.chooseFileToSave(d, [this, kind, data = std::move(data)](const std::string& path) {
        const std::string file = plugin::ensureExtension(path, {plugin::nativeExtension(kind)});
        if (!file.empty() && writeFile(file, data)) host_.session().files[size_t(kind)] = file;
    });
}

// File > Configuration > Load: a configuration file (.stymcfg), or a project file, which holds the same state
// without the recorder's take; the whole state replaced as at the start of the application (no undo step, like a
// project loaded), then kept as this copy's configuration, reopened at its next start.
void MainWindow::loadConfiguration() {
    Host::FileDialog d;
    d.title = "Load configuration";
    d.patterns = std::string("*") + kConfigExtension + ";*" + std::string(plugin::nativeExtension(FileKind::Preset));
    d.path = host_.configurationFile();
    host_.chooseFileToOpen(d, [this](const std::string& path) {
        const std::optional<std::string> bytes = host_.readFile(path, kMaxFileBytes);
        const plugin::NativeRead r = bytes ? plugin::readNativeFile(*bytes) : plugin::NativeRead{};
        if (!r.ok || r.kind != FileKind::Preset) {
            gemui::Alert::Spec spec;
            if (r.newer)
                spec.lines = {{"This file comes from a newer STYM2149."}, {"It cannot be read by this version."}};
            else if (r.ok)  // a pattern, a Song or a kit
                spec.lines = {{"Configuration read error."}, {kHolds[size_t(r.kind)]}};
            else
                spec.lines = {{"Configuration read error."}};
            gemui::Alert::open(desk_, std::move(spec));
            return;
        }
        panic();
        if (const std::string error = host_.loadConfiguration(*bytes); !error.empty()) {
            showMessage("Configuration read error.");
            return;
        }
        for (int i = 0; i < plugin::kParamCount; ++i) values_[size_t(i)] = host_.param(i);
        pullExtras();
        host_.keepConfiguration(path, *bytes);
    });
}

// File > Configuration > Save and Save as: the state as the application keeps it, written now, before the
// dialogue; Save rewrites this copy's configuration file without a dialogue once there is one. The file written
// becomes the configuration reopened at the next start.
void MainWindow::saveConfiguration(bool ask) {
    std::string data = host_.configuration();
    if (data.empty()) return;
    const std::string known = host_.configurationFile();
    if (!ask && !known.empty()) {
        if (writeFile(known, data)) host_.keepConfiguration(known, data);
        return;
    }
    Host::FileDialog d;
    d.title = "Save configuration as";
    d.patterns = std::string("*") + kConfigExtension;
    d.path = known;
    d.name = std::string("stym2149") + kConfigExtension;
    host_.chooseFileToSave(d, [this, data = std::move(data)](const std::string& path) {
        const std::string file = plugin::ensureExtension(path, {kConfigExtension});
        if (!file.empty() && writeFile(file, data)) host_.keepConfiguration(file, data);
    });
}

// A binary file read, put in place: a project replaces the whole project (no undo step, like loadPresetObj of
// the prototype); a pattern replaces the current pattern; a Song file replaces the Song, the Song mode and the patterns
// it carries (applySongFile); a kit file replaces the Chipdrum kit. Each of the last three takes an undo step.
void MainWindow::putFile(const plugin::NativeRead& r) {
    switch (r.kind) {
    case FileKind::Preset:
        panic();
        host_.loadProject(r.values, r.extras);
        for (int i = 0; i < plugin::kParamCount; ++i) values_[size_t(i)] = host_.param(i);
        pullExtras();
        return;
    case FileKind::Pattern:
        seq_.pushUndo();
        seq_.edit([&r](plugin::NonParamState& s) { s.seq.cur() = r.pattern; });
        return;
    case FileKind::Song:
        song_.pushUndo();
        seq_.edit([&r](plugin::NonParamState& s) {
            bool on = s.global.songMode;
            plugin::applySongFile(r.song, s.seq, on);
            s.global.songMode = on;
        });
        return;
    case FileKind::Kit: {
        UndoEntry e;  // pushKitUndo (the prototype took none)
        e.kind = UndoEntry::Kind::Kit;
        host_.session().undo.push(undoEntryLike(e));
        host_.editExtras([&r](plugin::NonParamState& s) { s.kit = r.kit; });
        pullExtras();
        return;
    }
    case FileKind::Patches: return;  // Patches window
    }
}

}  // namespace stym::pluginui
