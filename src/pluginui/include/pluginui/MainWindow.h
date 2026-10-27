// STYM2149 — pluginui: GEM main window (#mainWin of the prototype)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The editor desktop (1344 × 984 logical px: D-067, D-461) and the prototype's main window (980 px wide, centred,
// 18 px from the top): title bar, menu bar and CHIP / MIDI indicators, track tabs, SOURCE, ENVELOPE, VIBRATO, ADSR,
// GENERATOR, ADSR curve and SYSTEM panels, window buttons, oscilloscope, register monitor, keyboard or pads, bottom
// row. Positions and sizes: the prototype's CSS boxes at 100 % (STYM2149_SPEC.md §6).
//
// The window reads everything from its host (Host.h) at each tick() and keeps of its own only the screen state:
// notes held with the mouse, Latch, keyboard octave. Everything happens on the message thread.
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
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

namespace stym::pluginui {

// Root widget of its own desktop (desk_): owned by the host (StymEditor keeps it in a unique_ptr), which paints the
// desktop and passes it the input. The floating windows are members built with a reference to it, and friends: they
// read and write its state (values_, extras_, cur_, the undo helpers...) directly.
class MainWindow : public gemui::Widget {
public:
    // Editor desktop at 100 % (1272 × 944, D-067), larger by a desktop icon in width and height (D-461): 1344 × 984.
    static constexpr int kWidth = 1272 + gemui::DeskIcon::kW, kHeight = 944 + gemui::DeskIcon::kH;
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

    // Window commands, also used by the capture tool and the tests.
    void pickGenerator(int generator);   // setEffect of the prototype
    void openMenu(int title);            // 0 Fuji, 1 File, 2 Edit, 3 Options
    // Tooltip language (Settings window, Language, "tipLanguage" preference, D-185): "en", built in, or the code
    // of a language file, installed or in the user's Languages folder (D-283).
    const std::string& tipLanguage() const noexcept { return tipLang_; }
    void setTipLanguage(std::string_view id);
    void showChipAlert();
    // Alert with one OK button (host errors: unreadable audio file...).
    void showMessage(std::string_view text);
    // Octave of the keys (baseOct, from 0 to 9; the piano shows at most 6 of them): piano and keyboard drawing
    // rebuilt, notes held on the keyboard transposed in Follow unless `retune` is false (octave keys of the drawing).
    void setBaseOctave(int oct, bool retune = true);
    int baseOctave() const noexcept { return baseOct_; }
    // Demo pattern (File > Demo pattern; its ^D went to the DMA generator, D-301): confirmation if tracks 1 to 4
    // already hold data (demoPattern), then writing (doDemo).
    void demoPattern();
    void doDemo();
    // New project (File > New, ^N, D-225): confirmation if anything differs from a new project (newProject), then
    // every setting, pattern, instrument and sample back to its default (doNew). The preferences stay.
    void newProject();
    void doNew();
    void lightKey(int note, bool on);    // key lit without playing (sequencer, captures)
    // Layout the computer keyboard plays and the Computer Keyboard window draws: the one chosen in Settings > Keyboard
    // layout, or the system's for Auto (D-215).
    std::string keyLayout() const;
    // Box of a control, by the prototype's id ("fxSel", "shapeSel", "srcSel", "chipTxt", "mFile", "seqWin"...); empty
    // if unknown or hidden.
    std::optional<gemui::Rect> rectOf(std::string_view id) const;
    // SOURCE controls greyed out because the sound does not use them (D-253, D-261, D-262), which the prototype left
    // black: the visual test compares them with the capture under their veil.
    std::vector<const gemui::Widget*> greyedSource() const;
    // Controls greyed out inside a dimmed frame (ADSR, ADSR ENVELOPE, VIBRATO of a percussion track), which the
    // prototype left as they are: compared under both veils.
    std::vector<const gemui::Widget*> greyedInert() const;
    // Control of the page by the prototype's id ($(): main window, then floating windows; tests).
    const gemui::Widget* widgetOf(std::string_view id) const { return findById(id); }
    // SID wave and Sync Buzzer steps of the GENERATOR frame (D-251, D-252; tests): the grid, the form of the SID wave
    // chosen in its menu (D-411), a step button, the Steps sliders ("waveSteps", "buzzSteps"), the Period slider of
    // the sine ("wavePeriod", D-406) and the Width slider of the square ("waveWidth", D-408).
    // The forms of the SID wave menu (24/10/2026, D-411): Base, the prototype's square, without settings; Square, a
    // square of Steps steps, the note's volume over Width of them; Sine, a Sine-SID of Period bars; Bounce, the
    // Sine-SID on the chip's levels (D-414); Custom, the bars drawn.
    using WaveForm = ymengine::SidForm;  // form of the SID wave (D-411)
    static WaveForm waveFormOf(const ymengine::TrackPatch& p) noexcept;
    WaveForm waveForm() const noexcept;  // of the focused track
    const gemui::Button& waveFormButton() const noexcept { return waveForm_; }
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
    // The close box of the standalone application's window (Indus, 26/10/2026 03:57, D-439): the Quit box of
    // File > Quit, once (a second click while it is open does nothing); nothing in a plugin.
    void requestQuit();
    bool quitAsked() const noexcept { return quitAsked_; }
    // Value of the SYSTEM "Note" line: "TP=… -> … Hz" of the newest note, "-" when no voice sounds (D-440).
    const std::string& noteReadout() const noexcept { return tpread_.text(); }
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
    // Main window moved by its title bar (third DAW test, item 25, D-224): top-left corner of its frame, kept on the
    // desktop, below the menu bar and never past its right and bottom edges (D-460); the plugin keeps the place as
    // long as it lives.
    void moveMainTo(int x, int y);
    // Desktop icons (floppy disks A and B, trash; ninth DAW test, item 102), for the tests and the capture tool.
    const std::array<gemui::DeskIcon, 3>& deskIcons() const noexcept { return deskIcons_; }
    gemui::Point mainPlace() const noexcept { return {frame_.bounds().x, frame_.bounds().y}; }
    const gemui::WindowFrame& mainFrame() const noexcept { return frame_; }
    // Content of a file loaded into the YM Player (end of File > Import > YM dump): YM PLAYER window opened, or an
    // alert if it is refused.
    void loadYmBytes(std::string_view bytes, const std::string& fileName);
    // Content of an imported MIDI file (end of File > Import > MIDI file, midiImport of the prototype): patterns,
    // Song, tracks and tempo replaced in one undo step, then the alert "MIDI imported", or "Unsupported MIDI file".
    void loadMidiBytes(std::string_view bytes, const std::string& fileName);
    // Content of an imported XM module (end of File > Import > XM module, B5, D-166): patterns, Song, instruments and
    // DMA slots, tracks, STE machine and rate replaced in one undo step, then the alert "XM imported", or
    // "XM not imported" with the reason.
    void loadXmBytes(std::string_view bytes, const std::string& fileName);
    // Export in progress (worker thread, bee): File > Export pattern and Export song (M7, D-170).
    bool exporting() const noexcept { return job_.running(); }
    // Line of the register monitor: channel, sound (generator, or instrument chosen by Program Change), mode.
    const std::string& registerInfo() const noexcept { return regs_.info(); }
    DigidrumWindow& digidrums() noexcept { return digi_; }
    CurveWindow& slideCurve() noexcept { return curveWin_; }
    KbdWindow& keyboardWindow() noexcept { return kbdWin_; }
    EchoWindow& echoWindow() noexcept { return echo_; }  // Edit > Effects (D-299, D-311)
    SettingsWindow& settingsWindow() noexcept { return settings_; }  // Options > Settings (D-430)
    StepSoundWindow& stepSoundWindow() noexcept { return stepWin_; }  // right click on GEN, SRC, ENV (D-392)
    // Pads of the main window (tests).
    const Pads& pads() const noexcept { return pads_; }
    // SHORTCUTS window (#helpWin, openHelp): ^/, ^? and the Shortcuts entry of the Fuji menu (D-189).
    HelpWindow& help() noexcept { return help_; }
    // Oscilloscope of a track (openScope): opened, or brought to the front if it already is.
    void openScope(int track);
    ScopeWindow* scopeWindow(int track) noexcept { return scopes_[size_t(track)].get(); }
    // Modifier keys held (Alt: scissors on the grid's notes, setCutCursor of the prototype).
    void setModifiers(const gemui::Modifiers& m);
    // MIDI learn (Options > MIDI learn, P462, D-146): mode, learned map (shared by every project), text of the banner
    // (empty if it is hidden).
    void setMidiLearn(bool on);  // setLearn
    bool midiLearn() const noexcept { return learn_; }
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
    // Clock that the sequencer follows: MIDI when the Clock selector is "MIDI ext" (D-188), otherwise the state's.
    ymengine::SeqClock seqClock() const noexcept;
    bool mode3() const noexcept;
    int chips() const noexcept;
    int voices() const noexcept;  // available tracks (4 per chip in 3-channel mode, + 8 DMA with the STE; or 16)
    bool ste() const noexcept;    // STE machine (Computer, D-153)
    bool dmaTrack(int track) const noexcept;  // DMA track in 3-channel mode with the STE
    bool scopeShown() const;                  // Options > Oscilloscope (preference "mainScope", on by default)
    bool regsShown() const;                   // Options > Register monitor (preference "mainRegs", on by default)
    bool noteShown() const;                   // Options > Note monitor (preference "mainNote", on by default, D-442)
    int dmaSlot(int track) const noexcept;    // DMA sample played by the track
    int generator() const noexcept;
    bool drumTrack() const noexcept;
    bool chipdrumTrack() const noexcept;
    bool adsrChip() const noexcept;
    double msPerTick() const noexcept;
    int srcIndex() const noexcept;  // entry of the Source menu (D-246); -1: no square, no noise, fixed volume

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
    void placeEnvHz();  // the Env freq value and its unit at the right end of the row of EP = (D-296)
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
    // Pad of a percussion track (pads, Computer Keyboard, D-229): note kPadNote + pad; a Digidrum pad selects its slot.
    static constexpr int kPadNote = 48;
    void pressPad(int pad);
    void forceRelease(int note);
    void hostNote(const std::array<uint8_t, 3>& message);  // host note (onMIDI: press, release)
    // seqKbdLight, seqKbdClear: notes of the focused track's played step lit on the piano (the pads for percussion)
    // and the keyboard drawing; a tie keeps the root, a chord voice stays lit for its steps (D-281); step < 0
    // (stop): everything goes dark.
    void seqKbdLight(int step);
    bool seqLit(int note) const;  // piano key lit by the sequencer
    void releaseAll();
    void manPanic();
    void panic();
    void notAvailable(std::string_view what);
    // File menu (M7, A1, D-168): Load, Save and Save as of a preset, a pattern, a Song or a kit. Save rewrites the
    // file of the last Save or Save as of that kind, otherwise opens the dialog like Save as.
    void loadFile(plugin::FileKind kind);
    void saveFile(plugin::FileKind kind, bool ask);
    std::string fileText(plugin::FileKind kind) const;               // what Save writes
    void applyFile(plugin::FileKind kind, const std::string& text);  // file read: prototype's alert if it is refused
    // Host dialogs (FileChooser): file chosen then read (its text to `read`); file chosen then written, ".json" added
    // if it is missing (ensureJson); writing and remembering the file for Save.
    void openFile(plugin::FileKind kind, std::function<void(const std::string& text)> read);
    void saveFileAs(plugin::FileKind kind, std::string text);
    void writeFile(plugin::FileKind kind, const std::string& path, const std::string& text);
    // File > Import > YM dump (M7, A2, D-169): host dialog, file read then given to loadYmBytes.
    void importYm();
    // File > Import > MIDI file (A3, D-170): host dialog, file read then given to loadMidiBytes.
    void importMidi();
    // File > Import > XM module (B5): host dialog, file read then given to loadXmBytes.
    void importXm();
    // Alert for a file written by a newer version of STYM2149 (B6, D-174).
    void newerFileAlert();
    // File > Export pattern > MIDI and Export song > MIDI (exportMidi, exportSongMidi of the prototype): host dialog,
    // then offline render and file written by the worker thread, alert at the end.
    void exportMidi(bool song);
    // File > Export pattern and Export song, WAV and Stems (ZIP) (A4, D-164, D-171): GEM dialog of the format and the
    // rate, kept in the preferences, then host dialog and render by the worker thread.
    void exportAudio(bool stems, bool song);
    void saveAudio(bool stems, bool song, const plugin::WavSettings& settings);
    // File > Export song > VBL dump, YM3, YM6, SNDH (A5, B1, B2, D-172) and the recorder's take from the Recorder
    // window (B3): host dialog, capture and file written by the worker thread, alert at the end.
    void exportRegisters(plugin::RegisterFormat format);
    void exportTakeFile(plugin::RegisterFormat format);
    void finishRegisters(plugin::RegisterFormat format, const std::string& path, const plugin::RegisterFile& file);
    // Starts a long job (bee); alert if one is already running.
    bool startJob(BackgroundJob::Work work, std::function<void()> done);
    void confirmQuit();
    // Tab: the open window at the back comes to the front, Shift+Tab sends the front one to the back (D-389).
    bool cycleWindows(bool back);
    void showInfo();
    void selectSlot(int slot);
    void selectDmaSlot(int slot);
    void setComputer(int machine);  // 0 ST, 1 STE
    // Settings > Chip clock > Custom (D-237, D-430): frequency typed in a prompt box, then the Custom clock.
    void customClock();
    // Chip clocks of the ZX Spectrum and the Amstrad CPC after the two Atari ones (D-058); Free timer, after the MFP.
    static constexpr int kFirstAyClock = 2, kTimerFree = 1;
    // Custom chip clock in MHz with four decimals (fifth DAW test, D-239): "2.0000", to the nearest 100 Hz.
    static std::string clockMHz(uint32_t hz);
    // Values of a global parameter as menu entries (Settings window), from its description in the parameter table:
    // the choices of a Choice, Off/On for a Bool, otherwise every `stride` steps from min to max with the unit;
    // picking one sets the parameter. `checked` gets the entry of the current value, -1 if none.
    std::vector<gemui::MenuItem> paramItems(plugin::Global g, int stride, int& checked);
    void popup(gemui::Widget& anchor, std::vector<gemui::MenuItem> items, int checked);
    // Menu whose entries write into the focused track: they act only while that track is still the focused one.
    void trackPopup(gemui::Widget& anchor, std::vector<gemui::MenuItem> items, int checked);
    std::function<bool()> stillFocused() const;
    // SID Voice and Sync Buzzer settings of the focused track (D-227, D-238, D-278): a patch edit, one undo step
    // (nothing on a percussion track); a preset (BuzzerPresets.h) in one undo step; the shape of a Sync Buzzer step.
    void editPatch(const std::function<void(ymengine::TrackPatch&)>& fn);
    void applyBuzzerPreset(int k);
    // Menus of a Sync Buzzer step's shape and of the buzzer presets, at `anchor` (the GENERATOR frame's button, or
    // that of the step settings window, D-392).
    void buzzStepMenu(int step, gemui::Widget& anchor);
    void buzPresetsMenu(gemui::Widget& anchor);
    void waveFormMenu(gemui::Widget& anchor);  // Base, Square, Sine, Bounce, Custom (D-411, D-414)
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
    std::string scopeName(int track) const;  // title of an oscilloscope (scopeName)
    std::string tabTip(int track) const;
    std::vector<std::string> chipIssues() const;

    Host& host_;
    gemui::Desktop desk_{kWidth, kHeight};
    // Tooltip languages (D-185, D-283): English, built in, then the languages of the files by name, read again when
    // the Options menu opens. The files of a language: the user's first, then the installed one.
    struct TipChoice {
        std::string id, name;
        std::vector<TipFile> files;
    };
    std::vector<TipChoice> tipChoices_;
    std::string tipLang_ = "en";  // chosen code
    TipBook tipBook_;             // its translations
    void scanTipLanguages();
    void applyTipLanguage();
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
    std::vector<int> seqLitKeys_, seqLitPads_;  // lit by the sequencer (seqLitKeys, seqLitPads)
    int seqRootKey_ = -1;                       // root lit by the sequencer, kept while a tie holds it
    // Copy Track (copyTrack): the track's parameters, and its non-automatable part in tracks[0] of a default state.
    std::optional<plugin::NonParamState> clipboardExtras_;
    std::optional<std::array<float, plugin::kTrackParams>> clipboard_;
    uint64_t noteSerial_ = 0;   // last EngineStatus::noteSerial seen
    std::array<uint8_t, 128> manualCut_{};  // last EngineStatus::manualCut seen (D-431)
    uint16_t lastTp_ = 284;     // last period attacked (lastTP of the prototype)
    std::array<uint8_t, plugin::kTracks> program_{};  // instrument chosen by Program Change (engine status, D-156)
    double lastRegs_ = -1e9;    // register monitor: every 120 ms
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

    // Tabs.
    std::array<gemui::Button, plugin::kTracks> tabs_;
    gemui::Button allOn_{"All on", gemui::Button::Style::Tab}, allOff_{"All off", gemui::Button::Style::Tab};

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

    // VIBRATO (On/Off: sixth DAW test, D-254).
    gemui::GroupBox vibFs_{"VIBRATO", "(per channel)"};
    gemui::Label labVibOn_{"Vibrato"}, labDepth_{"Depth"}, labRate_{"Rate"}, depthUnit_{"ct"}, rateUnit_{"Hz"};
    gemui::Button vibOn_{"On"};
    gemui::Slider vibDepth_, vibRate_;
    gemui::ValueField vibDepthv_, vibRatev_;

    // ADSR.
    gemui::GroupBox adsrFs_{"ADSR", "(per channel)"};
    // Initials only, the sliders get their width (ninth DAW test, item 106, D-307).
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
    // SID Voice: the SID wave of the former Timer Wave window (sixth DAW test, items 53 and 80, D-251, D-278).
    gemui::Label labSidWave_{"SID wave"}, labWaveSteps_{"Steps"}, labWavePeriod_{"Period"}, labWaveWidth_{"Width"};
    SidWaveGrid sidWave_;
    gemui::Slider waveSteps_, wavePeriod_, waveWidth_;  // Period of the Sine-SID (D-406), width of the square (D-408)
    gemui::ValueField waveStepsv_, wavePeriodv_, waveWidthv_;
    gemui::Button waveForm_;  // menu of the forms (D-411)
    // Sync Buzzer: the tuning, YM Pad and presets of the former Buzzer window (D-227, D-238).
    gemui::Label labBuzTune_{"Tuning"}, labBuzN_{"N"}, labBuzD_{"D"}, labBuzFine_{"Fine"};
    gemui::Button buzTuned_{"Off"}, buzPad_{"YM Pad"}, buzPresets_{"Presets"};
    gemui::Slider buzN_, buzD_, buzFine_;
    gemui::ValueField buzNv_, buzDv_, buzFinev_;  // typed too (sixth DAW test, item 66, D-264)
    // Its steps, from the Timer Wave window (items 54 and 80, D-252, D-278).
    gemui::Label labBuzzSteps_{"Sync Buzzer steps"}, labBuzzLen_{"Steps"}, buzzUnit_{" steps"};
    std::array<gemui::Button, ymengine::kBuzzSteps> buzzStep_;
    gemui::Slider buzzLen_;
    gemui::ValueField buzzLenv_;
    gemui::Button openKit_{"Drum Kit"};
    gemui::Label kitName_{"", 12};
    gemui::Label labSlot_{"Slot"}, labDigiCh_{"ST channel"};
    Paragraph digiChHint_;
    std::array<gemui::Button, ymengine::kDigidrumSlots> slots_;
    gemui::Button digiChSel_;
    gemui::Button digiEdit_{"Digidrum samples"};
    DrumWave drumWave_;
    Paragraph digiHint_;
    gemui::Label labDmaSlot_{"Sample"};
    std::array<gemui::Button, ymengine::kDmaSlots> dmaSlots_;
    gemui::Button dmaEdit_{"DMA samples"};
    DmaWave dmaWave_{false};
    Paragraph dmaHint_;

    // ADSR curve.
    gemui::GroupBox curveFs_{"ADSR ENVELOPE"};
    AdsrCurve curve_;

    // SYSTEM.
    gemui::GroupBox sysFs_{"SYSTEM"};
    gemui::Label labVoices_{"Voices"}, labClock_{"Clock"}, clkStatus_, labMaster_{"Master"}, labLeds_{"Voices"},
        labNote_{"Note"}, tpread_{"-"};
    gemui::Button modeSel_, clkSel_;
    gemui::Label labComputer_{"Computer"};
    gemui::Button compSel_;
    gemui::Slider master_;
    gemui::ValueField masterv_;
    VoiceLeds leds_;

    // Window buttons, oscilloscope, registers, keyboard.
    // Effects after Modulation, as in the Edit menu (ninth DAW test, items 110 and 111, D-311, D-312).
    std::array<gemui::Button, 6> winBtns_{gemui::Button{"Mixer"},          gemui::Button{"Arpeggiator"},
                                          gemui::Button{"Modulation"},     gemui::Button{"Effects"},
                                          gemui::Button{"Step Sequencer"}, gemui::Button{"Patches"}};
    Scope scope_;
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
    int menuNew_ = -1;                  // open menu: File > New, added by STYM2149 (rectOf "menuNew", D-225)
    const gemui::Widget* popupAnchor_ = nullptr;  // control under the last popup menu (rectOf "shapeMenu")
    gemui::Point grab_{}, off_{};  // main window: grab point in its frame, offset from its home place (D-224)

    // MIDI learn.
    struct LearnTarget {
        std::string id;  // prototype id of the control
        bool range;      // slider (otherwise button)
    };
    bool learn_ = false;
    bool quitAsked_ = false;  // the Quit box is open (D-439)
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
    RecorderWindow recWin_{*this};  // register recorder (D-157)
    YmPlayerWindow ymWin_{*this};   // YM Player (#ymWin, M7)
    DigidrumWindow digi_{*this};
    CurveWindow curveWin_{*this};
    KbdWindow kbdWin_{*this};
    EchoWindow echo_{*this};        // Effects: echo and delay (eighth DAW test, item 98, D-299; D-311)
    SettingsWindow settings_{*this};  // Options > Settings (D-430)
    StepSoundWindow stepWin_{*this};  // a step's sound settings: right click on GEN, SRC, ENV (D-392)
    HelpWindow help_;               // SHORTCUTS (#helpWin, D-189)
    std::array<std::unique_ptr<ScopeWindow>, plugin::kTracks> scopes_;  // created at the first opening
};

}  // namespace stym::pluginui
