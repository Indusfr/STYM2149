// STYM2149 — plugin: the standalone application, its window's close box asking as File > Quit does
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP replaces JUCE's StandaloneFilterApp with this one, so that the close box of
// the system's window (and Alt+F4) opens the GEM Quit box instead of quitting at once (Indus, 26/10/2026 03:57). The
// application is JUCE's own, adapted from juce_audio_plugin_client_Standalone.cpp of JUCE 8.0.15
// (© Raw Material Software Limited, used under the AGPLv3 as the rest of JUCE, see THIRD_PARTY.md): same settings
// file, same plugin holder, same saving of the state; only the window differs.
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

#include "PluginEditor.h"

#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP

// Not self-contained: after the JUCE modules, as in JUCE's own application.
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace stym::plugin {
namespace {

class StandaloneWindow final : public juce::StandaloneFilterWindow {
public:
    using StandaloneFilterWindow::StandaloneFilterWindow;

    // The system's close box: the editor's Quit box, which quits on OK (StymEditor::quit saves the state, as JUCE's
    // window does); without an editor, JUCE's behaviour.
    void closeButtonPressed() override {
        if (auto* editor = dynamic_cast<StymEditor*>(pluginHolder->processor->getActiveEditor()))
            if (editor->askQuit()) return;
        StandaloneFilterWindow::closeButtonPressed();
    }
};

class StandaloneApp final : public juce::JUCEApplication {
public:
    StandaloneApp() {
        juce::PropertiesFile::Options options;
        options.applicationName = juce::CharPointer_UTF8(JucePlugin_Name);
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
#if JUCE_LINUX || JUCE_BSD
        options.folderName = "~/.config";
#else
        options.folderName = "";
#endif
        appProperties_.setStorageParameters(options);
    }

    const juce::String getApplicationName() override { return juce::CharPointer_UTF8(JucePlugin_Name); }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted(const juce::String&) override {}

    void initialise(const juce::String&) override {
        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty()) {
            holder_ = createHolder();  // no display: no window
            return;
        }
        window_ = std::make_unique<StandaloneWindow>(
            getApplicationName(),
            juce::LookAndFeel::getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
            createHolder());
        window_->setVisible(true);
    }

    void shutdown() override {
        holder_ = nullptr;
        window_ = nullptr;
        appProperties_.saveIfNeeded();
    }

    // Quit asked by the system (the application menu of macOS, the end of the session): as JUCE does, the state saved
    // and no question asked.
    void systemRequestedQuit() override {
        if (holder_ != nullptr) holder_->savePluginState();
        if (window_ != nullptr) window_->pluginHolder->savePluginState();
        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents()) {
            juce::Timer::callAfterDelay(100, [] {
                if (auto* app = juce::JUCEApplicationBase::getInstance()) app->systemRequestedQuit();
            });
        } else {
            quit();
        }
    }

private:
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

    juce::ApplicationProperties appProperties_;
    std::unique_ptr<StandaloneWindow> window_;
    std::unique_ptr<juce::StandalonePluginHolder> holder_;
};

}  // namespace
}  // namespace stym::plugin

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new stym::plugin::StandaloneApp(); }

#endif
