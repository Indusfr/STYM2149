// STYM2149 — pluginui: GEM main window (#mainWin of the prototype)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The editor desktop (1344 × 984 logical px) and the prototype's main window (980 px wide, centred,
// 18 px from the top): title bar, menu bar and CHIP / MIDI indicators, track tabs, SOURCE, ENVELOPE, VIBRATO, ADSR,
// GENERATOR, ADSR curve and SYSTEM panels, window buttons, oscilloscope, register monitor, keyboard or pads, bottom
// row. Positions and sizes: the prototype's CSS boxes at 100 % (STYM2149_SPEC.md §6).
//
// The window reads everything from its host (Host.h) at each tick() and keeps of its own only the screen state:
// notes held with the mouse, Latch, keyboard octave. Everything happens on the message thread.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gemui/Controls.h"
#include "gemui/Desktop.h"
#include "gemui/Menu.h"
#include "plugincore/AudioExport.h"
#include "plugincore/Parameters.h"
#include "plugincore/RegisterFiles.h"
#include "plugincore/State.h"
#include "pluginui/ArpWindow.h"
#include "pluginui/BackgroundJob.h"
#include "pluginui/CurveWindow.h"
#include "pluginui/EchoWindow.h"
#include "pluginui/StepSoundWindow.h"
#include "pluginui/HelpWindow.h"
#include "pluginui/KbdWindow.h"
#include "pluginui/Host.h"
#include "pluginui/MixerWindow.h"
#include "pluginui/KitWindow.h"
#include "pluginui/ModulationWindow.h"
#include "pluginui/InstrumentsWindow.h"
#include "pluginui/PatchesWindow.h"
#include "pluginui/ScopeWindow.h"
#include "pluginui/SeqWindow.h"
#include "pluginui/SettingsWindow.h"
#include "pluginui/TipLanguage.h"
#include "pluginui/DeskIcons.h"
#include "pluginui/DigidrumWindow.h"
#include "pluginui/DmaWindow.h"
#include "pluginui/RecorderWindow.h"
#include "pluginui/YmPlayerWindow.h"
#include "pluginui/SongWindow.h"
#include "pluginui/Widgets.h"

namespace stym::plugin {
struct MidiImport;
struct RegisterTune;
struct RegisterGrid;
struct RegisterImport;
struct RegisterImportResult;
struct YmTune;
struct NativeRead;
struct XmImport;
enum class ModuleFormat : uint8_t;
}  // namespace stym::plugin

namespace stym::ymio {
struct MaxYmSong;
struct TtrakModule;
struct YmtSong;
}  // namespace stym::ymio

namespace stym::pluginui {

class InfoDemo;

// Root widget of its own desktop (desk_): owned by the host (StymEditor keeps it in a unique_ptr), which paints the
// desktop and passes it the input. The floating windows are members built with a reference to it, and friends: they
// read and write its state (values_, extras_, cur_, the undo helpers...) directly.
class MainWindow : public gemui::Widget {
public:
    // Editor desktop at 100 % (1272 × 944), larger by a desktop icon in width and height: 1344 × 984.
    // The icons drawn twice as big leave it at that size: 72 × 40 px is the former size of an icon.
    static constexpr int kWidth = 1272 + 72, kHeight = 944 + 40;
    // Menu bar at the top of the desktop, as on a GEM desktop (ninth DAW test, item 101): the windows stay below it.
    static constexpr int kMenuH = gemui::MenuBar::kHeight;
    // Main window: 980 px wide, centred as in the prototype's page (body flex, align-items:center), 18 px below the
    // menu bar. It lost the menu row it had under its title, so its content keeps the prototype's places. The
    // positions in MainWindow.cpp count from its left edge.
    static constexpr int kWinW = 980, kWinX = (kWidth - kWinW) / 2, kWinY = kMenuH + 18;

    explicit MainWindow(Host& host);
    ~MainWindow() override;

    gemui::Desktop& desktop() noexcept { return desk_; }

    // Message thread, at the editor's rate: reads the parameters, the non-automatable state and the engine status
    // again, advances the desktop's timers (menus, tooltips).
    void tick(double nowMs);
    // Frames a second the host asks for: 60 while the demo of the Information box plays, 30 otherwise.
    int frameRate() const noexcept { return demo_.expired() ? 30 : 60; }

    // Window commands, also used by the capture tool and the tests.
    void pickGenerator(int generator);   // setEffect of the prototype
    void openMenu(int title);            // 0 Fuji, 1 File, 2 Edit, 3 Options
    // Tooltip language (Settings window, Language, "tipLanguage" preference): "en", built in, or the code
    // of a language file, installed or in the user's Languages folder.
    const std::string& tipLanguage() const noexcept { return tipLang_; }
    void setTipLanguage(std::string_view id);
    // Interface language (Settings window, Interface language, "uiLanguage" preference; Indus, 28/10/2026 03:06),
    // chosen apart from the tooltip language: "en", the base language built in, or the code of a language
    // file, whose entries of context "ui" translate the texts of the interface (gemui/Language.h). One for the process:
    // the windows are laid out again in it, those of another editor at its next tick.
    const std::string& uiLanguage() const noexcept { return uiLang_; }
    void setUiLanguage(std::string_view id);
    void showChipAlert();
    // Alert with one OK button (host errors: unreadable audio file...).
    void showMessage(std::string_view text);
    // A sample file loaded by the host (Digidrums or DMA window): `fn` writes it into the state, after an alert when
    // the samples would then go past the memory of the Atari.
    void applySampleLoad(std::function<void(plugin::NonParamState&)> fn);
    // Octave of the keys (baseOct, from 0 to 9; the piano shows at most 6 of them): piano and keyboard drawing
    // rebuilt, notes held on the keyboard transposed in Follow unless `retune` is false (octave keys of the drawing).
    void setBaseOctave(int oct, bool retune = true);
    int baseOctave() const noexcept { return baseOct_; }
    // Demo pattern (File > Demo pattern; its ^D went to the DMA generator): confirmation if tracks 1 to 4
    // already hold data (demoPattern), then writing (doDemo).
    void demoPattern();
    void doDemo();
    // New project (File > New, ^N): confirmation if anything differs from a new project (newProject), then
    // every setting, pattern, instrument and sample back to its default (doNew). The preferences stay.
    void newProject();
    void doNew();
    void lightKey(int note, bool on);    // key lit without playing (sequencer, captures)
    // Layout the computer keyboard plays and the Computer Keyboard window draws: the one chosen in Settings > Keyboard
    // layout, or the system's for Auto.
    std::string keyLayout() const;
    // Box of a control, by the prototype's id ("fxSel", "shapeSel", "srcSel", "chipTxt", "mFile", "seqWin"...); empty
    // if unknown or hidden.
    std::optional<gemui::Rect> rectOf(std::string_view id) const;
    // SOURCE controls greyed out because the sound does not use them, which the prototype left
    // black: the visual test compares them with the capture under their veil.
    std::vector<const gemui::Widget*> greyedSource() const;
    // Controls greyed out inside a dimmed frame (ADSR, ADSR ENVELOPE, VIBRATO of a percussion track), which the
    // prototype left as they are: compared under both veils.
    std::vector<const gemui::Widget*> greyedInert() const;
    // Control of the page by the prototype's id ($(): main window, then floating windows; tests).
    const gemui::Widget* widgetOf(std::string_view id) const { return findById(id); }
    // SID wave and Sync Buzzer steps of the GENERATOR frame (tests): the grid, the form of the SID wave
    // chosen in its menu, a step button, the Steps sliders ("waveSteps", "buzzSteps"), the Period slider of
    // the sine ("wavePeriod") and the Width slider of the square ("waveWidth").
    // The forms of the SID wave menu (24/10/2026): Base, the prototype's square, without settings; Square, a
    // square of Steps steps, the note's volume over Width of them; Sine, a Sine-SID of Period bars; Bounce, the
    // Sine-SID on the chip's levels; Custom, the bars drawn.
    using WaveForm = ymengine::SidForm;  // form of the SID wave
    static WaveForm waveFormOf(const ymengine::TrackPatch& p) noexcept;
    WaveForm waveForm() const noexcept;  // of the focused track
    const gemui::Button& waveFormButton() const noexcept { return waveForm_; }
    const gemui::Button& fmScaleButton() const noexcept { return fmScale_; }
    const SidWaveGrid& fmWave() const noexcept { return fmWave_; }
    const SidWaveGrid& sidWave() const noexcept { return sidWave_; }
    const gemui::Button& buzzStep(int k) const { return buzzStep_[size_t(k)]; }
    const gemui::Slider* stepsSlider(std::string_view id) const noexcept {
        return id == "waveSteps"   ? &waveSteps_
               : id == "buzzSteps"  ? &buzzLen_
               : id == "wavePeriod" ? &wavePeriod_
               : id == "waveWidth"  ? &waveWidth_
                                    : nullptr;
    }
    // Name of a track in its tab (YM channels in 3-channel mode, DMA1 to DMA8 for the DMA tracks).
    std::string trackTitle(int track) const { return trackName(track); }
    // Instrument row of the GENERATOR frame (tests): the number ("1" to "127"; "--") and the name field.
    const std::string& instrumentNumber() const noexcept { return insNum_.text(); }
    const gemui::TextField& instrumentName() const noexcept { return insName_; }
    // The close box of the standalone application's window (Indus, 26/10/2026 03:57): the Quit box of
    // File > Quit, once (a second click while it is open does nothing); nothing in a plugin.
    void requestQuit();
    bool quitAsked() const noexcept { return quitAsked_; }
    // Value of the SYSTEM "Note" line: "TP=… -> … Hz" of the newest note, "-" when no voice sounds.
    const std::string& noteReadout() const noexcept { return tpread_.text(); }
    // Lines of the SYSTEM MIDI monitor, oldest first, and the monitor (its history and scroll bar).
    std::vector<std::string> midiLog() const { return midiLog_.lines(); }
    MidiLog& midiMonitor() noexcept { return midiLog_; }
    // Floating windows.
    SeqWindow& sequencer() noexcept { return seq_; }
    SongWindow& songWindow() noexcept { return song_; }
    MixerWindow& mixer() noexcept { return mix_; }
    ArpWindow& arpeggiator() noexcept { return arp_; }
    ModulationWindow& modulation() noexcept { return orn_; }
    KitWindow& drumKit() noexcept { return kit_; }
    PatchesWindow& patches() noexcept { return lib_; }
    InstrumentsWindow& instruments() noexcept { return ins_; }
    DmaWindow& dmaWindow() noexcept { return dma_; }
    RecorderWindow& recorderWindow() noexcept { return recWin_; }
    YmPlayerWindow& ymPlayerWindow() noexcept { return ymWin_; }
    // Main window moved by its title bar (third DAW test, item 25): top-left corner of its frame, kept on the
    // desktop, below the menu bar and never past its right and bottom edges; the plugin keeps the place as
    // long as it lives.
    void moveMainTo(int x, int y);
    // Desktop icons (floppy disks A and B, trash; ninth DAW test, item 102), for the tests and the capture tool.
    const std::array<gemui::DeskIcon, 3>& deskIcons() const noexcept { return deskIcons_; }
    gemui::Point mainPlace() const noexcept { return {frame_.bounds().x, frame_.bounds().y}; }
    // Title of the main window: STYM2149, with the copy's number from 2 on.
    const std::string& mainTitle() const noexcept { return title_.title(); }
    const gemui::WindowFrame& mainFrame() const noexcept { return frame_; }
    // Content of a file loaded into the YM Player (end of File > Play > YM or SNDH): YM PLAYER window opened, or an
    // alert if it is refused.
    void loadYmBytes(std::string_view bytes, const std::string& fileName);
    // Content of an imported MIDI file (end of File > Import > MIDI file, midiImport of the prototype): patterns,
    // Song, tracks and tempo replaced in one undo step, then the alert "MIDI imported", or "Unsupported MIDI file".
    // Patterns it replaces that hold notes: an alert asks first; then, for drums (channel 10) on a track that
    // plays neither Chipdrum nor Digidrum, a second one asks which of the two.
    void loadMidiBytes(std::string_view bytes, const std::string& fileName);
    // Files dropped on the window at `at`: on the drawing of the Digidrums or DMA window, the first file that
    // is not a MIDI file loads into its slot as Load does; otherwise the first MIDI file is imported as by
    // File > Import > MIDI file. False if neither applies.
    bool dropFiles(const std::vector<std::string>& paths, std::optional<gemui::Point> at = std::nullopt);
    // Load of a Digidrum slot (dma false) or of a DMA slot: the choice of a file, then loadSampleFile.
    void chooseSample(bool dma, int slot);
    // A file chosen or dropped for a slot: when the slot holds a sample, a GEM box (Cancel by default) asks
    // first whether to replace it (Indus, 30/10/2026 02:50); then the host loads the file.
    void loadSampleFile(bool dma, int slot, const std::string& path);
    static bool isMidiFile(std::string_view path);
    // Content of a file imported by File > Import > Tracker song (Indus, 29/10/2026 21:37), whose format its
    // content gives: a maxYMiser song or voiceset (maxYmPair, then loadMaxYmBytes), a YM-Tracker song (loadYmtBytes),
    // a TTrak module (loadTtrakBytes), an XM, MOD, S3M or OKT module (loadXmBytes); anything else is refused
    // with an alert.
    void loadTrackerBytes(const std::string& bytes, const std::string& path);
    // Content of an imported module (B5; MOD; S3M; OKT): patterns, Song, instruments and
    // DMA slots, tracks, STE machine and rate replaced in one undo step, then the alert "XM imported" ("MOD imported",
    // "S3M imported", "OKT imported"), or "XM not imported" (…) with the reason.
    void loadXmBytes(std::string_view bytes, const std::string& fileName, plugin::ModuleFormat format);
    // Content of a YM or SNDH file imported as patterns (end of File > Import > SNDH / YM file), its
    // format from its content: the subsong asked for an SNDH that has several, the tune read (the Atari emulated for an
    // SNDH) under the bee, the grid dialogue (frames per step, steps per pattern), then the conversion: patterns, Song,
    // instruments, Authentic 3ch with tracks 1 to 3, VBL rate and Tuning A4 replaced in one undo step, then the alert
    // "YM imported" or "SNDH imported", or "... not imported" with the reason. An SNDH that maxYMiser saved, one song
    // per subsong, is imported from the song it holds as loadMaxYmBytes does; the routine is played as above
    // if that song is refused.
    void loadRegisterBytes(std::string_view bytes, const std::string& fileName);
    // Content of a maxYMiser song and of its voiceset (File > Import > Tracker song): converted under
    // the bee as a YM file is, without the grid dialogue (the rows are the steps), digidrums on track 4; then the alert
    // "maxYMiser song imported", or "maxYMiser song not imported" with the reason.
    void loadMaxYmBytes(std::string_view song, std::string_view voices, const std::string& fileName);
    // Export in progress (worker thread, bee): File > Export pattern and Export song.
    bool exporting() const noexcept { return job_.running(); }
    // Line of the register monitor: channel, sound (generator, or instrument chosen by Program Change), mode.
    const std::string& registerInfo() const noexcept { return regs_.info(); }
    DigidrumWindow& digidrums() noexcept { return digi_; }
    CurveWindow& slideCurve() noexcept { return curveWin_; }
    KbdWindow& keyboardWindow() noexcept { return kbdWin_; }
    EchoWindow& echoWindow() noexcept { return echo_; }  // Edit > Effects
    SettingsWindow& settingsWindow() noexcept { return settings_; }  // Options > Settings
    // State of the MIDI clock with Clock MIDI ext, shown under Clock in Settings > MIDI: waiting for
    // the engine status, then STOP or SYNC; empty with Clock Internal.
    static constexpr std::array<const char*, 3> kClockStates = {"waiting for clock...", "STOP", "SYNC"};
    std::string clockState() const;
    StepSoundWindow& stepSoundWindow() noexcept { return stepWin_; }  // right click on GEN, SRC, ENV
    // Pads of the main window (tests).
    const Pads& pads() const noexcept { return pads_; }
    const Scope& scope() const noexcept { return scope_; }
    const Keyboard& piano() const noexcept { return kbd_; }
    // SHORTCUTS window (#helpWin, openHelp): ^/, ^? and the Shortcuts entry of the Fuji menu.
    HelpWindow& help() noexcept { return help_; }
    // Oscilloscope of a track (openScope): opened, or brought to the front if it already is.
    void openScope(int track);
    ScopeWindow* scopeWindow(int track) noexcept { return scopes_[size_t(track)].get(); }
    // Modifier keys held (Alt: scissors on the grid's notes, setCutCursor of the prototype).
    void setModifiers(const gemui::Modifiers& m);
    // MIDI learn (Options > MIDI learn): mode, learned map (shared by every project), text of the banner
    // (empty if it is hidden).
    void setMidiLearn(bool on);  // setLearn
    bool midiLearn() const noexcept { return learn_; }
    // Escape with a control armed: the control disarmed, MIDI learn still on to arm another.
    void disarmLearn();
    void clearMidiMap();         // Options > Clear MIDI map
    const plugin::MidiMap& midiMap() const noexcept { return midiMap_; }
    std::string learnBadge() const { return badge_.visible() ? badge_.text() : std::string(); }

    void paint(gemui::Painter& p) override;      // desktop background
    void paintOver(gemui::Painter& p) override;  // outline of a desktop icon being dragged, above the windows
    bool keyDown(const gemui::KeyEvent& e) override;
    void keyUp(const gemui::KeyEvent& e) override;

private:
    friend class SeqWindow;
    friend class SeqGrid;
    friend class SeqOverview;
    friend class SongWindow;
    friend class MixerWindow;
    friend class ArpWindow;
    friend class ModulationWindow;
    friend class KitWindow;
    friend class PatchesWindow;
    friend class InstrumentsWindow;
    friend class SequencesPanel;
    friend class LfoPanel;
    friend class DmaWindow;
    friend class RecorderWindow;
    friend class YmPlayerWindow;
    friend class DigidrumWindow;
    friend class CurveWindow;
    friend class KbdWindow;
    friend class EchoWindow;
    friend class StepSoundWindow;
    friend class SettingsWindow;

    struct Held {
        bool mouse;  // played on the piano (otherwise the Computer Keyboard or a pad); with Latch, one at a time
    };

    // Reading from the host.
    float pv(plugin::Global g) const noexcept { return values_[size_t(plugin::paramIndex(g))]; }
    float pv(plugin::Track f) const noexcept { return values_[size_t(plugin::paramIndex(cur_, f))]; }
    int iv(plugin::Global g) const noexcept;
    int iv(plugin::Track f) const noexcept;
    int iv(int track, plugin::Track f) const noexcept;
    // Clock that the sequencer follows: MIDI when the Clock selector is "MIDI ext", otherwise the state's.
    ymengine::SeqClock seqClock() const noexcept;
    bool mode3() const noexcept;
    bool mode32() const noexcept;  // 32 ch
    int chips() const noexcept;
    int voices() const noexcept;  // available tracks (4 per chip in 3-channel mode, + 8 DMA with the STE; or 16)
    bool ste() const noexcept;    // STE machine (Computer)
    bool dmaTrack(int track) const noexcept;  // DMA track in 3-channel mode with the STE
    bool scopeShown() const;                  // Options > Oscilloscope (preference "mainScope", on by default)
    bool scopeTrack() const;                  // the oscilloscope shows the focused track, the main output by default
    bool scopeMerged() const;                 // one trace of a stereo source, off by default
    bool scopeStereo() const;                 // the oscilloscope's source can be stereo: Merge enabled
    void refreshScope();                      // the oscilloscope's buttons and trace from scopeFeed_
    bool regsShown() const;                   // Options > Register monitor (preference "mainRegs", on by default)
    bool noteShown() const;                   // Options > Note monitor (preference "mainNote", on by default)
    bool midiShown() const;                   // Options > MIDI monitor (preference "mainMidi", on by default)
    bool infoDemoShown() const;  // Options > Information demo (preference "infoDemo", on by default)
    int midiLines() const;  // Settings > MIDI monitor: lines it keeps ("midiLines"; Custom)
    int dmaSlot(int track) const noexcept;    // DMA sample played by the track
    // Digidrum played by pad (drum row) `pad` of track `track` (TrackPatch::digiPads).
    int digiPad(int track, int pad) const noexcept;
    // Memory of the Atari the samples take now, and with a state, voice mode and machine to come.
    plugin::SteMemory memory() const noexcept;
    // Before a change of the samples, the voice mode, the machine or the memory: when it would silence samples
    // that are heard now, a GEM box (Cancel by default) says so first, and `apply` runs on Proceed; otherwise at once.
    void confirmMemory(const plugin::NonParamState& next, ymengine::VoiceMode mode, bool ste,
                       std::function<void()> apply);
    void confirmMemory(const std::function<void(plugin::NonParamState&)>& change, std::function<void()> apply);
    // "1234 KB of 3968 KB": the memory line of the Digidrums and DMA windows in the 3-channel modes.
    std::string memoryText() const;
    int generator() const noexcept;
    bool drumTrack() const noexcept;
    bool chipdrumTrack() const noexcept;
    bool adsrChip() const noexcept;
    double msPerTick() const noexcept;
    int srcIndex() const noexcept;  // entry of the Source menu; -1: no square, no noise, fixed volume

    // Writing: one setting = one gesture.
    void set(int index, float plain);
    void set(plugin::Global g, float plain) { set(plugin::paramIndex(g), plain); }
    void set(plugin::Track f, float plain) { set(plugin::paramIndex(cur_, f), plain); }
    void bindSlider(gemui::Slider& s, std::function<void(double)> apply);

    // Building and updating.
    void build();
    void buildMenus();
    void wireControls();
    void refresh();
    void layout();
    void layoutHome();  // layout at the window's home place (kWinX, kWinY)
    void placeEnvHz();  // the Env freq value and its unit at the right end of the row of EP =
    void refreshEngineStatus(const plugin::EngineStatus& t);
    void refreshRegisters();
    void refreshChip();

    // Actions.
    void focusTrack(int track);
    void tabClicked(int track);
    void tabMenu(int track, gemui::Point at);
    void setAllEnabled(bool on);
    void copyTrack(int track);
    void pasteTrack(int track);
    void renameTrack(int track);
    void trackMenu(const gemui::Box& anchor);  // chanMenu: track played from the keyboard, under the key or the pad
    void press(int note, int velocity, bool mouse);
    void release(int note);
    // Pad of a percussion track (pads, Computer Keyboard): note kPadNote + pad; a Digidrum pad selects its slot.
    static constexpr int kPadNote = ymengine::kPadNote;
    void pressPad(int pad);
    void forceRelease(int note);
    void unlightPad(int pad);
    void hostNote(const std::array<uint8_t, 3>& message);  // host note (onMIDI: press, release)
    void followOctave(int note);  // the piano brought to a note played on a MIDI keyboard
    // seqKbdLight, seqKbdClear: notes of the focused track's played step lit on the piano (the pads for percussion)
    // and the keyboard drawing; a tie keeps the root, a chord voice stays lit for its steps; step < 0
    // (stop): everything goes dark.
    void seqKbdLight(int step);
    bool seqLit(int note) const;  // piano key lit by the sequencer
    void releaseAll();
    void manPanic();
    void panic();
    void notAvailable(std::string_view what);
    // File menu: Load, Save and Save as of a project, a pattern, a Song or a kit, in the binary
    // format (no JSON). Save rewrites the file of the last Save or Save as of that kind, otherwise opens
    // the dialog like Save as.
    void loadFile(plugin::FileKind kind);
    void saveFile(plugin::FileKind kind, bool ask);
    void putFile(const plugin::NativeRead& r);  // file read put in place
    // File > Configuration of the standalone application: Load, Save and Save as of the whole state it reopens
    // at its start (.stymcfg); Save rewrites this copy's configuration file, otherwise asks like Save as.
    void loadConfiguration();
    void saveConfiguration(bool ask);
    // Host dialogs (FileChooser) of the patch library (JSON, Import and Export of the Patches window): file chosen then
    // read (its text to `read`); file chosen then written, ".json" added if it is missing (ensureJson), and remembered
    // for the next dialog. writeFile: an alert if the file cannot be written.
    void openPatchLibrary(std::function<void(const std::string& text)> read);
    void savePatchLibrary(std::string text);
    bool writeFile(const std::string& path, std::string_view bytes);
    // File > Play...: host dialog, file read then given to playBytes, which recognises
    // its format from its content; a tune goes to the YM Player, a song or a STYM2149 file becomes a copy of the project
    // played by a second engine: playStym, playModule (XM, MOD, S3M, OKT), playMaxYm, playYmt, playTtrak,
    // through playConverted (worker thread) and playProject.
    void playFile();
    void playBytes(const std::string& bytes, const std::string& path);
    void playStym(const plugin::NativeRead& r, const std::string& name);
    void playModule(std::span<const uint8_t> data, const std::string& name, plugin::ModuleFormat format);
    void playMaxYm(std::string_view songBytes, std::string_view voiceBytes, const std::string& name);
    void playYmt(std::shared_ptr<const ymio::YmtSong> song, const std::string& name);
    void playTtrak(std::shared_ptr<const ymio::TtrakModule> module, const std::string& name);
    void playConverted(std::function<plugin::RegisterImportResult(plugin::NonParamState&)> convert,
                       const std::string& name);
    void playProject(const plugin::ParamValues& values, plugin::NonParamState x, bool song, const std::string& name);
    // File > Import > MIDI file: host dialog, file read then given to loadMidiBytes.
    void importMidi();
    // The converted project put in place; `drums`: generator of the channel 10 track.
    void applyMidiImport(const ymseq::Project& converted, const plugin::MidiImport& r, const std::string& fileName,
                         ymengine::Generator drums);
    // File > Import > Tracker song (Indus, 29/10/2026 21:37): one host dialog for every tracker format, the file
    // read then given to loadTrackerBytes.
    void importTracker();
    // File > Import > SNDH / YM file (one entry): host dialog, file read then given to
    // loadRegisterBytes; the steps after it.
    void importRegisters();
    void readRegisterTune(std::shared_ptr<const plugin::YmTune> tune, int subsong);
    void registerGridDialog(std::shared_ptr<const plugin::RegisterTune> tune, const plugin::RegisterGrid& guess,
                            const std::string& fileName, bool sndh);
    // `message`: the lines of the alert that ends the import.
    void applyRegisterImport(const plugin::NonParamState& converted, const plugin::RegisterImport& r,
                             std::vector<std::string> message);
    // After the memory alert, if any: the import written in one undo step, then the alert that ends it.
    void commitRegisterImport(const plugin::NonParamState& converted, const plugin::RegisterImport& r,
                              std::vector<std::string> message);
    // The memory alert of the converted module, if any, then applyXmImport.
    void confirmXmImport(std::shared_ptr<plugin::NonParamState> converted, std::shared_ptr<const plugin::XmImport> m,
                         const std::string& fileName);
    void applyXmImport(const plugin::NonParamState& converted, const plugin::XmImport& m, const std::string& fileName);
    // Parameters that a YM, SNDH or maxYMiser import sets besides On and Generator, in the order of its undo step.
    static std::vector<int> registerImportParams();
    // A maxYMiser song (.MYS) or its voiceset (.MYV) chosen: the other one of the same name read beside it, or
    // asked for in a second dialogue (File > Import > Tracker song and File > Play).
    using MaxYmUse = std::function<void(std::string_view song, std::string_view voices, const std::string& name)>;
    void maxYmPair(const std::string& path, const std::string& bytes, MaxYmUse use);
    // A maxYMiser song and its voiceset converted under the bee, on a copy of the project, then put in place with the
    // alert "maxYMiser song imported"; if the conversion refuses them, `orElse` runs, or without it the alert "maxYMiser
    // song not imported" says why.
    void convertMaxYm(std::shared_ptr<const ymio::MaxYmSong> song, const std::string& fileName,
                      std::function<void()> orElse = {});
    // A YM-Tracker song given to File > Import > Tracker song: read here, converted under the bee on a copy of
    // the project, then put in place with the alert "YM-Tracker song imported" (or "not imported" and why).
    void loadYmtBytes(std::span<const uint8_t> data, const std::string& fileName);
    // A TTrak module given to File > Import > Tracker song: read here, played once through by our model of its
    // routine and converted under the bee on a copy of the project, then put in place with the alert "TTrak module
    // imported" (or "not imported" and why).
    void loadTtrakBytes(std::span<const uint8_t> data, const std::string& fileName);
    // Alert for a file written by a newer version of STYM2149.
    void newerFileAlert();
    // File > Export pattern > MIDI and Export song > MIDI (exportMidi, exportSongMidi of the prototype): host dialog,
    // then offline render and file written by the worker thread, alert at the end.
    void exportMidi(bool song);
    // File > Export pattern and Export song, WAV and Stems (ZIP): GEM dialog of the format and the
    // rate, kept in the preferences, then host dialog and render by the worker thread.
    void exportAudio(bool stems, bool song);
    void saveAudio(bool stems, bool song, const plugin::WavSettings& settings);
    // File > Export song > VBL dump, YM3, YM6, SNDH and the recorder's take from the Recorder
    // window: host dialog, capture and file written by the worker thread, alert at the end.
    void exportRegisters(plugin::RegisterFormat format);
    // File > Export song > maxYMiser: host dialog for the song (.MYS), its voiceset (.MYV) written beside it
    // under the same name; capture and conversion by the worker thread, alert at the end.
    void exportMaxYm();
    // File > Export song > YM-Tracker: host dialog for the song (.ymt), named after the file; conversion by
    // the worker thread, alert at the end.
    void exportYmt();
    // File > Export song > TTrak: host dialog for the module (.TPM), its title the name of the file; capture,
    // conversion and comparison by the worker thread, alert at the end.
    void exportTtrak();
    void exportTakeFile(plugin::RegisterFormat format);
    void finishRegisters(plugin::RegisterFormat format, const std::string& path, const plugin::RegisterFile& file);
    // Starts a long job (bee); alert if one is already running.
    bool startJob(BackgroundJob::Work work, std::function<void()> done);
    void confirmQuit();
    // Tab: the open window at the back comes to the front, Shift+Tab sends the front one to the back.
    bool cycleWindows(bool back);
    void showInfo();
    void selectSlot(int slot);
    void selectDmaSlot(int slot);
    // Right click on a Digidrum pad of the GENERATOR frame: the Digidrum it plays, from a menu of those loaded
    // or by its number.
    void digiPadMenu(int pad, gemui::Widget& anchor);
    void setDigiPad(int pad, int slot);
    // Pages of the pads: page p gives pads 1 to 8 the Digidrums from digiPageStart(p) + 1, the last page ending
    // on the 127th. digiPageOf: the page whose Digidrums the pads of `track` play, otherwise that of pad 1.
    static int digiPageStart(int page) noexcept;
    int digiPageOf(int track) const noexcept;
    void setDigiPage(int page);
    void setComputer(int machine);  // 0 ST, 1 STE
    // Settings > Chip clock > Custom: frequency typed in a prompt box, then the Custom clock.
    void customClock();
    // Settings > MIDI monitor > Custom: the lines it keeps, typed in a prompt box, 1 to MidiLog::kKeptMax.
    void customMidiLines();
    // Clear of the MIDI monitor's row: a GEM box asks first, then every line goes.
    void clearMidiLog();
    // Chip clocks of the ZX Spectrum and the Amstrad CPC after the two Atari ones; Free timer, after the MFP.
    static constexpr int kFirstAyClock = 2, kTimerFree = 1;
    // Custom chip clock in MHz with four decimals (fifth DAW test): "2.0000", to the nearest 100 Hz.
    static std::string clockMHz(uint32_t hz);
    // Values of a global parameter as menu entries (Settings window), from its description in the parameter table:
    // the choices of a Choice, Off/On for a Bool, otherwise every `stride` steps from min to max with the unit;
    // picking one sets the parameter. `checked` gets the entry of the current value, -1 if none.
    std::vector<gemui::MenuItem> paramItems(plugin::Global g, int stride, int& checked);
    // The generators of the focused track (the GENERATOR button and Edit > Generator), with their Shift + ^
    // keys; one this track cannot play is greyed with the reason. `checked` gets the entry of the current one.
    std::vector<gemui::MenuItem> generatorItems(int& checked);
    // columns: a long list (the 127 instruments) in as many columns as the desktop's height needs.
    void popup(gemui::Widget& anchor, std::vector<gemui::MenuItem> items, int checked, bool columns = false);
    // Menu whose entries write into the focused track: they act only while that track is still the focused one.
    void trackPopup(gemui::Widget& anchor, std::vector<gemui::MenuItem> items, int checked, bool columns = false);
    std::function<bool()> stillFocused() const;
    // SID Voice and Sync Buzzer settings of the focused track: a patch edit, one undo step
    // (nothing on a percussion track); a preset (BuzzerPresets.h) in one undo step; the shape of a Sync Buzzer step.
    void editPatch(const std::function<void(ymengine::TrackPatch&)>& fn);
    void applyBuzzerPreset(int k);
    // Menus of a Sync Buzzer step's shape and of the buzzer presets, at `anchor` (the GENERATOR frame's button, or
    // that of the step settings window).
    void buzzStepMenu(int step, gemui::Widget& anchor);
    void buzPresetsMenu(gemui::Widget& anchor);
    void waveFormMenu(gemui::Widget& anchor);  // Base, Square, Sine, Bounce, Custom
    void fmScaleMenu(gemui::Widget& anchor);   // Ratios, Semitones
    bool fmSemitones() const noexcept;         // the focused track's FM wave is in semitones
    void setWaveForm(WaveForm f);

    // Floating windows and sequencer.
    void pullExtras();          // reads the non-automatable state again at once (after a modification)
    void syncWindowButtons();   // syncWinBtns
    void openSong();
    void openCurve();
    void songStep(int dir);     // previous or next entry of the Song
    void previewNote(int note); // short preview of a note written in the grid (melodic track)
    UndoEntry undoEntryLike(const UndoEntry& e) const;  // chanUndoEntry, mixUndoEntry
    // Undo step of the focused track (curSnap of the prototype): armCur keeps its state (at each press in the main
    // window or the Arpeggiator, at each key), pushCur pushes it if it has changed since, then forgets it.
    void armCur();
    void pushCur();
    void pressed(gemui::Widget* w);  // press on the desktop (Desktop::onPress)
    void applyUndoEntry(const UndoEntry& e);            // Track and Mix steps
    // MIDI learn: press in learn mode (the prototype's pointerdown listener), message caught by the host (midiBind,
    // applyMidiMap), control by id, keys and armed control given back to the host.
    bool learnPress(gemui::Widget* w);
    void learnMessage(const std::array<uint8_t, 3>& m);
    bool applyMidiMap(const std::string& key, int value);
    gemui::Widget* findById(std::string_view id) const;
    void pushCapture();
    void saveWindows();
    void restoreWindows();

    std::string trackName(int track) const;
    // The menu of the Track buttons of the Step Sequencer and the Modulation window: the tracks of the mode, each with
    // its generator; the one picked is edited, as a click on its tab does.
    std::vector<gemui::MenuItem> trackItems();
    // The track's instrument of the GENERATOR frame: its number (1 to 127), 0 when it has none or its slot was
    // cleared; the menu of the number; the name typed, committed.
    int trackInstrument(int track) const;
    // A row of the track's instrument (GENERATOR frame, Modulation window): the number's menu loads an
    // instrument, the name typed renames it or stores the track's sound (for nameTrack, the track it was typed for).
    void wireInstrument(gemui::Button& num, NameField& name, const int& nameTrack);
    void showInstrument(gemui::Button& num, gemui::TextField& name, int& nameTrack) const;
    void showInstrumentNumber(gemui::Button& num) const;  // without a name field: the Step Sequencer
    void instrumentMenu(gemui::Button& anchor);
    void commitInstrumentName(const gemui::TextField& field, int track);
    bool trackNamed(int track) const;  // its name is the user's, shown as it is
    std::string scopeName(int track) const;  // title of an oscilloscope (scopeName)
    std::string tabTip(int track) const;
    std::vector<std::string> chipIssues() const;

    Host& host_;
    gemui::Desktop desk_{kWidth, kHeight};
    // Tooltip languages: English, built in, then the languages of the files by name, read again when
    // the Options menu opens. The files of a language: the user's first, then the installed one.
    struct TipChoice {
        std::string id, name;
        std::vector<TipFile> files;
    };
    std::vector<TipChoice> tipChoices_;
    std::string tipLang_ = "en";  // chosen code
    TipBook tipBook_;             // its translations
    std::string uiLang_ = "en";   // interface language
    uint64_t uiGeneration_ = 0;   // the process's interface language the windows are laid out in
    void scanTipLanguages();
    void applyTipLanguage();
    std::vector<const TipFile*> languageLayers(std::string_view id) const;
    void applyUiLanguage();
    void relayoutLanguage();
    // Desktop icons: pictures and places ("deskIcons" preference, x and y of each), names in the tooltip language.
    void buildDeskIcons();
    void nameDeskIcons();
    void openLanguagesFolder();

    plugin::ParamValues values_{};
    plugin::NonParamState extras_;
    uint64_t extrasVersion_ = ~uint64_t(0);  // version of extras_ last read (~0: none yet)
    bool first_ = true;                      // the first tick refreshes everything
    int cur_ = 0;               // focused track
    gemui::Slider* dragging_ = nullptr;  // held slider: its position is not read again
    std::vector<int> gestures_;          // parameters opened by the gesture in progress (held slider)
    int baseOct_ = 2;
    bool latch_ = false;
    std::map<int, Held> held_;  // notes held on screen
    std::array<bool, 128> hostHeld_{};  // host notes held on the focused track
    uint16_t hostPads_ = 0;  // drum pads of the controller held (bit = pad)
    std::array<uint16_t, 16> hostKeyPads_{};  // keys 36 to 51 of each channel held as drum pads
    std::vector<int> seqLitKeys_, seqLitPads_;  // lit by the sequencer (seqLitKeys, seqLitPads)
    int seqRootKey_ = -1;                       // root lit by the sequencer, kept while a tie holds it
    // Copy Track (copyTrack): the track's parameters, and its non-automatable part in tracks[0] of a default state.
    std::optional<plugin::NonParamState> clipboardExtras_;
    std::optional<std::array<float, plugin::kTrackParams>> clipboard_;
    uint64_t noteSerial_ = 0;   // last EngineStatus::noteSerial seen
    std::array<uint8_t, 128> manualCut_{};  // last EngineStatus::manualCut seen
    uint16_t lastTp_ = 284;     // last period attacked (lastTP of the prototype)
    std::array<uint8_t, plugin::kTracks> program_{};  // instrument chosen by Program Change (engine status)
    double lastRegs_ = -1e9;    // register monitor: every 120 ms
    double nowMs_ = 0;          // time of the current tick
    std::weak_ptr<InfoDemo> demo_;  // the demo of the Information box, while the box is open
    std::string midiText_;      // text of the MIDI indicator, set again only when it changes
    int chipIndicator_ = -1, midiIndicator_ = -1;  // indices of the CHIP and MIDI indicators in the menu bar
    bool midiClockShown_ = false;  // Clock = MIDI ext shown
    int clockRun_ = -1;            // MIDI clock: -1 waiting for the engine status, 0 STOP, 1 SYNC

    // Desktop: menu bar and icons (floppy disks A and B, trash), decorative (ninth DAW test, items 101 and 102).
    gemui::MenuBar menu_;
    std::array<gemui::DeskIcon, 3> deskIcons_;

    // Window.
    gemui::WindowFrame frame_;
    gemui::TitleBar title_;

    // Tabs. In 32 channels, those that do not fit before All on scroll with < and >: tabFirst_ is the first
    // tab shown; the focused track's tab is brought into view when the focus changes (tabRevealed_).
    std::array<gemui::Button, plugin::kTracks> tabs_;
    gemui::Button allOn_{"All on", gemui::Button::Style::Tab}, allOff_{"All off", gemui::Button::Style::Tab};
    gemui::Button tabPrev_{"<", gemui::Button::Style::Tab}, tabNext_{">", gemui::Button::Style::Tab};
    int tabFirst_ = 0, tabRevealed_ = -1;

    // SOURCE.
    gemui::GroupBox srcFs_{"SOURCE", "(per channel)"};
    gemui::Label labSrc_{"Source"}, labPoly_{"Voicing"}, labNp_{"Noise period"}, labVol_{"Volume"},
        labVm_{"Mode"}, labLevel_{"Level"};
    gemui::Button srcSel_, polySel_, vmSel_;
    gemui::Slider np_, vol_, level_;
    gemui::ValueField npv_, volv_, levelv_;

    // ENVELOPE.
    gemui::GroupBox envFs_{"ENVELOPE", "(per channel)"};
    gemui::Label labEf_{"Env freq"}, efUnit_{"Hz"}, labEp_{"EP ="}, shapeLab_{"Shape R13"};
    gemui::Slider ef_;
    gemui::ValueField efv_, epv_;
    gemui::Button shapeSel_;

    // VIBRATO (On/Off: sixth DAW test).
    gemui::GroupBox vibFs_{"VIBRATO", "(per channel)"};
    gemui::Label labVibOn_{"Vibrato"}, labDepth_{"Depth"}, labRate_{"Rate"}, depthUnit_{"ct"}, rateUnit_{"Hz"};
    gemui::Button vibOn_{"On"};
    gemui::Slider vibDepth_, vibRate_;
    gemui::ValueField vibDepthv_, vibRatev_;

    // ADSR.
    gemui::GroupBox adsrFs_{"ADSR", "(per channel)"};
    // Initials only, the sliders get their width (ninth DAW test, item 106).
    std::array<gemui::Label, 4> adsrLab_{gemui::Label{"A"}, gemui::Label{"D"}, gemui::Label{"S"}, gemui::Label{"R"}};
    std::array<gemui::Slider, 4> adsr_;
    std::array<gemui::ValueField, 4> adsrv_;
    std::array<gemui::Label, 4> adsrUnit_;
    gemui::Label adsrModeLab_{"Mode"}, adsrHint_{"", 12};
    gemui::Button adsrMode_;

    // GENERATOR.
    gemui::GroupBox genFs_{"GENERATOR", "(per channel)"};
    gemui::Label labType_{"Type"};
    gemui::Button fxSel_;
    Rule genSep_;
    gemui::Label labRatio_{"Timer ratio"}, ratioX_{"x"}, labVmin_{"SID min vol"};
    gemui::Slider ratio_, vmin_;
    gemui::ValueField ratiov_, vminv_;
    // SID Voice: the SID wave of the former Timer Wave window (sixth DAW test, items 53 and 80).
    gemui::Label labSidWave_{"SID wave"}, labWaveSteps_{"Steps"}, labWavePeriod_{"Period"}, labWaveWidth_{"Width"};
    SidWaveGrid sidWave_;
    gemui::Slider waveSteps_, wavePeriod_, waveWidth_;  // Period of the Sine-SID, width of the square
    gemui::ValueField waveStepsv_, wavePeriodv_, waveWidthv_;
    gemui::Button waveForm_;  // menu of the forms
    // FM Voice: its wave, a multiple of the note per timer interrupt as TTrak's FM timers, over Steps steps.
    gemui::Label labFmWave_{"FM wave"}, labFmSteps_{"Steps"};
    gemui::Button fmScale_;  // menu of its scales, Ratios or Semitones
    SidWaveGrid fmWave_;
    gemui::Slider fmSteps_;
    gemui::ValueField fmStepsv_;
    Paragraph fmHint_;
    // Sync Buzzer: the tuning, YM Pad and presets of the former Buzzer window.
    gemui::Label labBuzTune_{"Tuning"}, labBuzN_{"N"}, labBuzD_{"D"}, labBuzFine_{"Fine"};
    gemui::Button buzTuned_{"Off"}, buzPad_{"YM Pad"}, buzPresets_{"Presets"};
    gemui::Slider buzN_, buzD_, buzFine_;
    gemui::ValueField buzNv_, buzDv_, buzFinev_;  // typed too (sixth DAW test, item 66)
    // Its steps, from the Timer Wave window (items 54 and 80).
    gemui::Label labBuzzSteps_{"Sync Buzzer steps"}, labBuzzLen_{"Steps"}, buzzUnit_{" steps"};
    std::array<gemui::Button, ymengine::kBuzzSteps> buzzStep_;
    gemui::Slider buzzLen_;
    gemui::ValueField buzzLenv_;
    gemui::Button openKit_{"Drum Kit"};
    gemui::Label kitName_{"", 12};
    gemui::Label labSlot_{"Slot"}, labDigiCh_{"ST channel"};
    Paragraph digiChHint_;
    // The eight pads of the Digidrum track (each shows the Digidrum it plays, chosen with a right click).
    std::array<gemui::Button, ymengine::kDigidrumPads> slots_;
    // The eight Digidrums before or after for the pads, over 16 pages.
    static constexpr int kDigiPages =
        (ymengine::kDigidrumSlotsMax + ymengine::kDigidrumPads - 1) / ymengine::kDigidrumPads;
    gemui::Button digiPrev_{"<"}, digiNext_{">"};
    gemui::Button digiChSel_;
    gemui::Button digiEdit_{"Digidrum samples"};
    DrumWave drumWave_;
    Paragraph digiHint_;
    gemui::Label labDmaSlot_{"Slot"};  // "Sample" until then (Indus, 29/10/2026 20:48)
    // Eight samples at a time out of 127, the arrows turning the pages (sixteen for a while,
    // back to eight: Indus, 30/10/2026 02:06).
    static constexpr int kDmaPageSlots = 8;
    std::array<gemui::Button, kDmaPageSlots> dmaSlots_;
    gemui::Button dmaPrev_{"<"}, dmaNext_{">"};
    int dmaPage_ = 0;
    int dmaSeen_ = -1, dmaSeenTrack_ = -1;  // sample and track the page last followed
    gemui::Button dmaEdit_{"DMA samples"};
    DmaWave dmaWave_{false};
    Paragraph dmaHint_;
    // The track's instrument at the bottom, under a rule: the Instruments window, the number (1 to 127 in
    // decimal; "--" when the track plays its own sound) and the name, typed in place in the 8x8 font.
    Rule insSep_;
    gemui::Button insOpen_{"Instrument"}, insNum_;
    NameField insName_;
    int insNameTrack_ = 0;  // track whose instrument name the field shows

    // ADSR curve.
    gemui::GroupBox curveFs_{"ADSR ENVELOPE"};
    AdsrCurve curve_;

    // SYSTEM.
    gemui::GroupBox sysFs_{"SYSTEM"};
    gemui::Label labVoices_{"Voices"}, labMaster_{"Master"}, labLeds_{"Voices"}, labNote_{"Note"}, tpread_{"-"},
        labMidi_{"MIDI"};
    gemui::Button modeSel_;
    gemui::Label labComputer_{"Computer"};
    gemui::Button compSel_;
    gemui::Slider master_;
    gemui::ValueField masterv_;
    VoiceLeds leds_;
    MidiLog midiLog_;
    gemui::Button midiClear_{"Clear"};  // right of the MIDI label

    // Window buttons, oscilloscope, registers, keyboard.
    // Effects after Modulation, as in the Edit menu (ninth DAW test, items 110 and 111).
    std::array<gemui::Button, 6> winBtns_{gemui::Button{"Mixer"},          gemui::Button{"Arpeggiator"},
                                          gemui::Button{"Modulation"},     gemui::Button{"Effects"},
                                          gemui::Button{"Step Sequencer"}, gemui::Button{"Patches"}};
    Scope scope_;
    // At the right of the oscilloscope (Indus, 29/10/2026 10:54): its source, the main output or the focused track,
    // and Merge, one trace of a stereo source; preferences "scopeSource" ("main", "track") and
    // "scopeMerge" ("0", "1"). The last engine status, for a redraw when one of them changes.
    gemui::Button scopeSrc_{"Main"}, scopeMerge_{"Merge"};
    std::unique_ptr<plugin::EngineStatus> scopeFeed_;
    RegisterMonitor regs_;
    Keyboard kbd_;
    Pads pads_;
    gemui::Button latchBtn_{"Latch"}, layerBtn_{"Layer"}, octDown_{"Oct -"}, octUp_{"Oct +"},
        kbdMap_{"Computer keyboard"};
    gemui::Label octv_;

    // Preview of a grid note (previewN, previewT).
    int previewN_ = -1;
    gemui::Desktop::TimerId previewTimer_ = 0;

    std::optional<UndoEntry> curSnap_;  // state of the track before the modification in progress
    int menuExtra_ = -1;                // open menu: first entry added by STYM2149 (rectOf "menuExtra")
    int menuNew_ = -1;                  // open menu: File > New, added by STYM2149 (rectOf "menuNew")
    const gemui::Widget* popupAnchor_ = nullptr;  // control under the last popup menu (rectOf "shapeMenu")
    gemui::Point grab_{}, off_{};  // main window: grab point in its frame, offset from its home place

    // MIDI learn.
    struct LearnTarget {
        std::string id;  // prototype id of the control
        bool range;      // slider (otherwise button)
    };
    bool learn_ = false;
    bool quitAsked_ = false;  // the Quit box is open
    std::optional<LearnTarget> learnTarget_;  // armed control
    plugin::MidiMap midiMap_;
    std::map<std::string, int> ccLast_;       // last value of each key (rising edge of the buttons)
    LearnBadge badge_;
    BackgroundJob job_;  // exports (worker thread)

    // Floating windows, in front of the main window (declared last: they read everything before them).
    SeqWindow seq_{*this};
    SongWindow song_{*this};
    MixerWindow mix_{*this};
    ArpWindow arp_{*this};
    ModulationWindow orn_{*this};
    KitWindow kit_{*this};
    PatchesWindow lib_{*this};
    InstrumentsWindow ins_{*this};
    DmaWindow dma_{*this};
    RecorderWindow recWin_{*this};  // register recorder
    YmPlayerWindow ymWin_{*this};   // YM Player (#ymWin)
    DigidrumWindow digi_{*this};
    CurveWindow curveWin_{*this};
    KbdWindow kbdWin_{*this};
    EchoWindow echo_{*this};        // Effects: echo and delay (eighth DAW test, item 98)
    SettingsWindow settings_{*this};  // Options > Settings
    StepSoundWindow stepWin_{*this};  // a step's sound settings: right click on GEN, SRC, ENV
    HelpWindow help_;               // SHORTCUTS (#helpWin)
    std::array<std::unique_ptr<ScopeWindow>, plugin::kTracks> scopes_;  // created at the first opening
};

}  // namespace stym::pluginui
