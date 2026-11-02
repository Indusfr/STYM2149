// STYM2149 — plugin: JUCE editor, GEM window (pluginui) drawn with juce::Graphics at the screen's resolution
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The editor is the host of the main window (pluginui::Host): host parameters with gestures, the processor's
// non-automatable state and on-screen playing, engine status, user preferences (a file shared by every instance),
// a window size from 50 to 200 %, set by resizing the window (STYM2149_SPEC.md §6.6 without its Zoom menu, tenth DAW
// test, item 127; 100 % shows the desktop at 5/8 of its logical size). The GEM desktop is
// laid out in logical pixels (1344 × 984) and drawn into an image at the screen's resolution
// (JucePainter). A 30 Hz timer advances the window and redraws only the changed areas.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

#include <map>
#include <memory>
#include <vector>

#include "JucePainter.h"
#include "pluginui/Host.h"
#include "pluginui/MainWindow.h"

namespace stym::plugin {

class StymProcessor;

class StymEditor final : public juce::AudioProcessorEditor,
                         public juce::FileDragAndDropTarget,
                         private juce::Timer,
                         private juce::ChangeListener,
                         public pluginui::Host {
public:
    explicit StymEditor(StymProcessor& owner);
    // Preferences read from and written to `preferences` (tests) instead of the user's file.
    StymEditor(StymProcessor& owner, std::unique_ptr<juce::PropertiesFile> preferences);
    ~StymEditor() override;

    // The user's preferences file, specific to the interface (Preferences.settings).
    static std::unique_ptr<juce::PropertiesFile> userPreferences();
    // Opens `file`; if it does not exist yet, copies into it the preferences of the old STYM2149.settings in the same
    // folder, without the settings of the JUCE standalone application.
    static std::unique_ptr<juce::PropertiesFile> openPreferences(const juce::File& file,
                                                                 const juce::PropertiesFile::Options& options);
    // Writes the preferences `changed` into `file` after reading the file again: Preferences.settings is
    // shared by every copy of the standalone application and every instance of the plugin, and JUCE writes the whole
    // set it holds, which would put back the values another copy has changed since.
    static void writePreferences(juce::PropertiesFile& file, const juce::StringPairArray& changed);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void parentHierarchyChanged() override;  // the window's renderer once the editor is on screen
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyStateChanged(bool isKeyDown) override;
    void modifierKeysChanged(const juce::ModifierKeys& m) override;
    void focusLost(FocusChangeType cause) override;
    // A MIDI file dropped on the editor is imported into the sequencer; an audio file (or raw bytes, .raw,
    // .spl, .sam) dropped on the drawing of the Digidrums or DMA window loads into its slot.
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    pluginui::MainWindow& window() noexcept { return *window_; }
    // A GEM alert with these lines (in the interface language) and OK: what the standalone application has to say at
    // its start.
    void showAlert(const std::vector<std::string>& lines);
    // The close box of the standalone application's window: the GEM Quit box, as File > Quit; false in a host,
    // where the editor does not quit.
    bool askQuit();
    // One editor frame (window tick, desktop drawing, copy): 30 Hz timer; also called by the tests, which have no
    // message loop. `nowMs` (millisecond counter): the tests' own clock, so that what a frame does with the time (the
    // register line refreshed every 120 ms) does not depend on how fast the machine runs them.
    void frame();
    void frame(double nowMs);
    // Standalone application: the MIDI inputs available now and those of them enabled (identifiers). One enabled and
    // present before that is gone, unplugged or switched off, releases every key held: its note offs will never
    // come. Returns the inputs unplugged then plugged back, still enabled, which the device manager must close and open
    // again.
    juce::StringArray midiInputsSeen(const juce::StringArray& available, const juce::StringArray& enabled);

private:
    void timerCallback() override;
    // The standalone application's device manager changed (a MIDI device plugged in or out, an input switched).
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void scanMidiInputs(juce::StringArray& available, juce::StringArray& enabled) const;
    // Draws the changed areas of the desktop into the image and has them repainted.
    void flush();
    // Image of w × h screen pixels, the whole desktop drawn into it.
    void render(int w, int h);
    // Draws area r of the desktop into the image; returns the changed area of the component.
    juce::Rectangle<int> draw(const gemui::Rect& r);
    // Asynchronous file dialog: suggested file, folder remembered in the preferences ("lastFolder").
    void launchChooser(const FileDialog& dialog, bool save, std::function<void(const std::string& path)> done);
    gemui::Point logical(const juce::MouseEvent& e) const noexcept;  // mouse → desktop pixel, clamped to the desktop
    // Rendering engine of the window that shows the editor, from the "renderer" preference (Windows).
    void applyRenderer();
    void applyZoom(int percent);  // editor size for this zoom (clamped); the caller stores the preference if needed
    // A preference changed by this window: kept at once, written to the file half a second later (savePreferences).
    void storePreference(const juce::String& key, const juce::String& value);
    void savePreferences();  // writes the changed preferences now, if any (writePreferences)
    // Settings of this copy of the standalone application (its MIDI learn map); nullptr in a host.
    juce::PropertySet* copySettings() const;
    // Releases the held keys that the system no longer reports as pressed (all of them if `all`).
    void releaseKeys(bool all);
    juce::AffineTransform toScreen() const noexcept;  // logical pixels → component

    // pluginui::Host
    float param(int index) const override;
    void beginGesture(int index) override;
    void setParam(int index, float plain) override;
    void endGesture(int index) override;
    uint64_t extrasVersion() const override;
    NonParamState extras() const override;
    void editExtras(const std::function<void(NonParamState&)>& edit) override;
    void screenMidi(uint8_t status, uint8_t data1, uint8_t data2) override;
    bool hostNote(std::array<uint8_t, 3>& message) override;
    bool monitorMidi(plugin::MonitorMessage& message) override;
    void setMidiCapture(const MidiKeys& keys, bool armed) override;
    bool learnMidi(std::array<uint8_t, 3>& message) override;
    void seqCommand(SeqCommand command, int a, int b, int c) override;
    const EngineStatus* engineStatus() override;
    pluginui::EditorSession& session() override;
    bool standalone() const override;
    int copyNumber() const override;
    std::string midiStatus() const override;
    std::string version() const override;
    void quit() override;
    bool hasAudioSettings() const override;
    void openAudioSettings() override;
    std::string audioFilePatterns() const override;
    void loadDigidrum(int slot, const std::string& path) override;
    void loadDmaSample(int slot, const std::string& path) override;
    Recorder recorder() const override;
    void armRecorder(int chip) override;
    void disarmRecorder() override;
    void clearRecording() override;
    RegisterTake recordedTake() const override;
    std::string loadYm(std::string_view bytes, const std::string& fileName) override;
    std::string playProject(const plugin::ParamValues& values, const plugin::NonParamState& extras, bool song,
                            const std::string& name) override;
    YmPlayer ymPlayer() const override;
    void ymPlay(bool on) override;
    void ymRewind() override;
    void ymSubsong(int subsong) override;
    void ymClose() override;
    std::string digidrumLibrary() const override;
    void setDigidrumLibrary(const std::string& json) override;
    std::string dmaLibrary() const override;
    void setDmaLibrary(const std::string& json) override;
    std::string preference(std::string_view key, std::string_view fallback) const override;
    void setPreference(std::string_view key, std::string_view value) override;
    std::string systemLanguage() const override;
    std::vector<TextFile> languageFiles() const override;
    std::string installedLanguagesFolder() const override;
    bool openLanguagesFolder(const std::vector<TextFile>& files) override;
    std::string keyboardLayout() const override;
    bool rendererChoice() const override;
    // Files: whole project replaced (File > Load > Preset), state of a new project (File > New), dialogs and
    // whole-file access with absolute UTF-8 paths, streamed exports (createFile).
    void loadProject(const ParamValues& values, const NonParamState& extras) override;
    NonParamState newExtras() const override;
    // Configuration of the standalone application: the processor's state as a host saves it, put back as a
    // host reads it, kept in this copy's settings ("configFile", and "filterState" for the next start).
    std::string configuration() const override;
    std::string loadConfiguration(std::string_view bytes) override;
    void keepConfiguration(const std::string& path, std::string_view bytes) override;
    std::string configurationFile() const override;
    void chooseFileToOpen(const FileDialog& dialog, std::function<void(const std::string& path)> done) override;
    void chooseFileToSave(const FileDialog& dialog, std::function<void(const std::string& path)> done) override;
    std::optional<std::string> readFile(const std::string& path, size_t maxBytes) const override;
    bool writeFile(const std::string& path, std::string_view bytes) override;
    std::unique_ptr<ymio::ByteSink> createFile(const std::string& path) override;
    uint32_t hostSampleRate() const override;
    // Patches library: Patches.json, in the folder of the preferences file.
    std::string patchLibrary() const override;
    void setPatchLibrary(const std::string& json) override;

    StymProcessor& processor_;
    std::unique_ptr<juce::PropertiesFile> prefs_;
    juce::StringPairArray unsavedPrefs_{false};  // preferences changed and not written yet
    double prefsDueAt_ = 0;                      // when to write them (half a second after the first change)
    juce::Image image_;  // the desktop at the screen pixels of the component (made by the first paint)
    JucePainter::Cache cache_;  // masks and pattern cells at the image's scale (screen pixels per desktop pixel)
    int zoom_ = 100;            // window size in %, 50 to zoomMax_, remembered in the preference "zoom"
    int zoomMax_ = 200;         // the largest display filled, 200 at least
    juce::StringArray midiInputs_;  // standalone: MIDI inputs enabled and present at the last look
    juce::StringArray midiGone_;    // enabled inputs unplugged, to open again when they come back
    bool applyingZoom_ = false;  // the editor is setting its own size: resized() does not store it as a choice
    gemui::Pointer pointer_ = gemui::Pointer::Arrow;  // last pointer given to JUCE (changed only when it differs)
    std::unique_ptr<juce::FileChooser> chooser_;  // the open file dialog; it must live until its callback has run
    // Held keys (JUCE code → press passed on): JUCE reports neither auto-repeat nor the release of a given key
    // (keyStateChanged only says that the keyboard state changed).
    std::map<int, gemui::KeyEvent> keysDown_;
    mutable std::string layout_;  // system keyboard layout, asked again after a second
    mutable juce::uint32 layoutAt_ = 0;  // when it was last asked (juce::Time::getMillisecondCounter)
    int frameHz_ = 30;  // rate of the timer: MainWindow::frameRate
    std::unique_ptr<pluginui::MainWindow> window_;  // last: destroyed first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StymEditor)
};

}  // namespace stym::plugin
