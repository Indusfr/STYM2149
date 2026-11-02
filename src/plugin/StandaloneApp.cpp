// STYM2149 — plugin: the standalone application, its window's close box asking as File > Quit does, several
// copies running at once
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP replaces JUCE's StandaloneFilterApp with this one, so that the close box of
// the system's window (and Alt+F4) opens the GEM Quit box instead of quitting at once (Indus, 26/10/2026 03:57). The
// application is JUCE's own, adapted from juce_audio_plugin_client_Standalone.cpp of JUCE 8.0.15
// (© Raw Material Software Limited, used under the AGPLv3 as the rest of JUCE, see THIRD_PARTY.md): same settings
// file, same plugin holder; the window differs, and the state is saved only by File > Configuration. Several
// copies may run at once, each with its own settings file and its number in the title (Indus, 31/10/2026 10:17;
// StandaloneSlot.h).
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

#include "PluginEditor.h"
#include "StandaloneSlot.h"

#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP

// Not self-contained: after the JUCE modules, as in JUCE's own application.
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace stym::plugin {
namespace {

class StandaloneWindow final : public juce::StandaloneFilterWindow {
public:
    using StandaloneFilterWindow::StandaloneFilterWindow;

    // The system's close box: the editor's Quit box, which quits on OK; without an editor, quits at once. The state is
    // not saved, unlike JUCE's window: the next start reopens the configuration last saved or loaded.
    void closeButtonPressed() override {
        if (askQuit()) return;
        juce::JUCEApplicationBase::quit();
    }

    // The editor's Quit box, if the editor is open (false otherwise).
    bool askQuit() {
        auto* editor = dynamic_cast<StymEditor*>(pluginHolder->processor->getActiveEditor());
        return editor != nullptr && editor->askQuit();
    }
};

class StandaloneApp final : public juce::JUCEApplication {
public:
    StandaloneApp() {
        // Each running copy its own settings file: STYM2149.settings for the first, "STYM2149 (2).settings"
        // for the second... A copy started for the first time takes the first copy's audio device.
        const juce::PropertiesFile::Options options = settingsOptions(slotName(appName(), slot_.number()));
        const bool fresh = slot_.number() > 1 && !options.getDefaultFile().existsAsFile();
        appProperties_.setStorageParameters(options);
        juce::PropertiesFile& settings = *appProperties_.getUserSettings();
        if (fresh) {
            const juce::PropertiesFile first(settingsOptions(appName()));
            seedSlotSettings(first, settings, slot_.number());
            asioTaken_ = usesAsio(settings);  // said at the start (initialise)
        }
        // The copy's own MIDI learn map (Indus, 31/10/2026 11:03), read by createPluginFilter and the editor.
        adoptMidiMap(*StymEditor::userPreferences(), settings, slot_.number());
        // The session an earlier version saved at Quit is not reopened.
        forgetUnsavedSession(settings);
        settings.saveIfNeeded();
        setStandaloneSettings(&settings);
    }

    ~StandaloneApp() override { setStandaloneSettings(nullptr); }

    const juce::String getApplicationName() override { return appName(); }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted(const juce::String&) override {}

    void initialise(const juce::String&) override {
        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty()) {
            holder_ = createHolder();  // no display: no window
            return;
        }
        const juce::PropertiesFile& settings = *appProperties_.getUserSettings();
        const bool placed = settings.containsKey("windowX") && settings.containsKey("windowY");
        // The copy's number in the title from 2 on: "STYM2149 (2)".
        window_ = std::make_unique<StandaloneWindow>(
            slotName(getApplicationName(), slot_.number()),
            juce::LookAndFeel::getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
            createHolder());
        if (!placed && slot_.number() > 1) {
            // Centred as the first copy's window would be: moved aside.
            const int step = slotWindowOffset(slot_.number());
            window_->setTopLeftPosition(window_->getX() + step, window_->getY() + step);
        }
        window_->setVisible(true);
        if (asioTaken_) {
            // A new copy that took the first copy's ASIO driver says so, opened or not (Indus, 31/10/2026 11:05).
            const juce::AudioDeviceManager& devices = window_->pluginHolder->deviceManager;
            const bool anyOpen = devices.getCurrentAudioDevice() != nullptr;
            const bool asioOpen = anyOpen && devices.getCurrentAudioDeviceType() == "ASIO";
            if (auto* editor = dynamic_cast<StymEditor*>(window_->pluginHolder->processor->getActiveEditor()))
                editor->showAlert(asioNotice(asioOpen, anyOpen));
        }
    }

    void shutdown() override {
        holder_ = nullptr;
        window_ = nullptr;
        setStandaloneSettings(nullptr);
        appProperties_.saveIfNeeded();
    }

    // Quit asked by the system (the application menu of macOS, the end of the session): the Quit box, as File > Quit
    // and the close box, since the state is no longer saved at Quit; the system waits for the answer (Windows
    // offers to end the session anyway). Without an editor, quits without saving.
    void systemRequestedQuit() override {
        if (window_ != nullptr && window_->askQuit()) return;
        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents()) {
            juce::Timer::callAfterDelay(100, [] {
                if (auto* app = juce::JUCEApplicationBase::getInstance()) app->systemRequestedQuit();
            });
        } else {
            quit();
        }
    }

private:
    static juce::String appName() { return juce::CharPointer_UTF8(JucePlugin_Name); }

    // The file of JUCE's standalone application: in ~/.config on Linux, Application Support on macOS,
    // %APPDATA%\STYM2149 on Windows, the same folder for every copy (JUCE would make one per name on Windows).
    static juce::PropertiesFile::Options settingsOptions(const juce::String& name) {
        juce::PropertiesFile::Options options;
        options.applicationName = name;
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
#if JUCE_LINUX || JUCE_BSD
        options.folderName = "~/.config";
#elif JUCE_WINDOWS
        options.folderName = appName();
#else
        options.folderName = "";
#endif
        return options;
    }

    std::unique_ptr<juce::StandalonePluginHolder> createHolder() {
#ifdef JucePlugin_PreferredChannelConfigurations
        constexpr juce::StandalonePluginHolder::PluginInOuts channels[]{JucePlugin_PreferredChannelConfigurations};
        const juce::Array<juce::StandalonePluginHolder::PluginInOuts> channelConfig(channels,
                                                                                    juce::numElementsInArray(channels));
#else
        const juce::Array<juce::StandalonePluginHolder::PluginInOuts> channelConfig;
#endif
        return std::make_unique<juce::StandalonePluginHolder>(appProperties_.getUserSettings(), false, juce::String{},
                                                               nullptr, channelConfig, false);
    }

    StandaloneSlot slot_{appName()};  // first: the settings file depends on it
    bool asioTaken_ = false;          // a new copy took the first copy's ASIO driver
    juce::ApplicationProperties appProperties_;
    std::unique_ptr<StandaloneWindow> window_;
    std::unique_ptr<juce::StandalonePluginHolder> holder_;
};

}  // namespace
}  // namespace stym::plugin

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new stym::plugin::StandaloneApp(); }

#endif
