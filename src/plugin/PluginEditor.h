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

class StymEditor final : public juce::AudioProcessorEditor, private juce::Timer, public pluginui::Host {
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

    pluginui::MainWindow& window() noexcept { return *window_; }
    // The close box of the standalone application's window: the GEM Quit box, as File > Quit; false in a host,
    // where the editor does not quit.
    bool askQuit();
    // One editor frame (window tick, desktop drawing, copy): 30 Hz timer; also called by the tests, which have no
    // message loop.
    void frame();

private:
    void timerCallback() override;
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
    void setMidiCapture(const MidiKeys& keys, bool armed) override;
    bool learnMidi(std::array<uint8_t, 3>& message) override;
    void seqCommand(SeqCommand command, int a, int b, int c) override;
    const EngineStatus* engineStatus() override;
    pluginui::EditorSession& session() override;
    bool standalone() const override;
    std::string midiStatus() const override;
    std::string version() const override;
    void quit() override;
    bool hasAudioSettings() const override;
    void openAudioSettings() override;
    void loadDigidrum(int slot) override;
    void loadDmaSample(int slot) override;
    Recorder recorder() const override;
    void armRecorder(int chip) override;
    void disarmRecorder() override;
    void clearRecording() override;
    RegisterTake recordedTake() const override;
    std::string loadYm(std::string_view bytes, const std::string& fileName) override;
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
    juce::Image image_;  // the desktop at the screen pixels of the component (made by the first paint)
    JucePainter::Cache cache_;  // masks and pattern cells at the image's scale (screen pixels per desktop pixel)
    int zoom_ = 100;            // window size in %, 50 to 200, remembered in the preference "zoom"
    bool applyingZoom_ = false;  // the editor is setting its own size: resized() does not store it as a choice
    gemui::Pointer pointer_ = gemui::Pointer::Arrow;  // last pointer given to JUCE (changed only when it differs)
    std::unique_ptr<juce::FileChooser> chooser_;  // the open file dialog; it must live until its callback has run
    // Held keys (JUCE code → press passed on): JUCE reports neither auto-repeat nor the release of a given key
    // (keyStateChanged only says that the keyboard state changed).
    std::map<int, gemui::KeyEvent> keysDown_;
    mutable std::string layout_;  // system keyboard layout, asked again after a second
    mutable juce::uint32 layoutAt_ = 0;  // when it was last asked (juce::Time::getMillisecondCounter)
    std::unique_ptr<pluginui::MainWindow> window_;  // last: destroyed first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StymEditor)
};

}  // namespace stym::plugin
