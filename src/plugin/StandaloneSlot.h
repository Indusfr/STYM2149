// STYM2149 — plugin: the copies of the standalone application, each with its own settings
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Several copies of the standalone application may run at once, one MIDI controller each (Indus, 31/10/2026 10:14
// and 10:17). Each running copy takes the lowest free number: the first keeps STYM2149.settings and the window title
// "STYM2149", copy 2 has "STYM2149 (2).settings" and "STYM2149 (2)", and so on. A copy's file holds what JUCE's
// standalone application keeps (audio and MIDI devices, the session reopened at the next start, the window's place),
// so that each copy keeps its own. A number is held by an inter-process lock for as long as its copy runs.
#pragma once
#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace stym::plugin {

inline constexpr int kStandaloneSlots = 64;  // copies running at once, at most

// The lowest number from 1 to kStandaloneSlots that `take` takes (true); 0 when every one is taken.
int lowestFreeSlot(const std::function<bool(int)>& take);

// Name of copy `slot`, used for its settings file and its window title: "STYM2149", "STYM2149 (2)"...
juce::String slotName(const juce::String& app, int slot);

// How far the window of copy `slot` is moved down and right from the first copy's, so that the windows do not cover
// one another: 32 pixels per number, back to 0 every 16 copies.
int slotWindowOffset(int slot);

// Settings of a copy started for the first time, from those of copy 1: the audio device without the MIDI inputs
// (each copy chooses its controller), the input muting, and the window's place moved by slotWindowOffset. The session
// (filterState, lastStateFile, configFile) is not copied: a new copy starts with a new project.
void seedSlotSettings(const juce::PropertySet& first, juce::PropertySet& settings, int slot);

// Number of the copy of the standalone application running in this process: the one its StandaloneSlot holds, 1 when
// none was taken (a plugin in a host, tests). The editor shows it in the main window's title from 2 on.
int standaloneCopyNumber() noexcept;

// Settings file of this copy of the standalone application, set by the application while it runs; nullptr in a host
// and in the tests. It keeps the copy's MIDI learn map too: each copy plays its own controller (Indus, 31/10/2026
// 11:03), so the map is not one of the preferences shared by every copy.
juce::PropertySet* standaloneSettings() noexcept;
void setStandaloneSettings(juce::PropertySet* settings) noexcept;

// The MIDI learn map of a copy that has none yet: the first copy takes the map of the shared preferences, where every
// copy kept it before (an empty one if there is none, so that a map learned later in a host stays apart); another copy
// starts without one, its controller being another.
void adoptMidiMap(const juce::PropertySet& preferences, juce::PropertySet& settings, int slot);

// At the start of a copy: the state that JUCE's holder reopens ("filterState") is kept only when it is a
// configuration saved or loaded (File > Configuration, "configFile" set); the state an earlier version saved at each
// Quit is dropped, the work not saved being lost as the Quit box says (Indus, 01/11/2026 18:25).
void forgetUnsavedSession(juce::PropertySet& settings);

// The audio device of `settings` is an ASIO driver (Windows).
bool usesAsio(const juce::PropertySet& settings);

// Lines of the alert of a new copy that took the first copy's ASIO driver (Indus, 31/10/2026 11:05): most ASIO drivers
// serve one application at a time. `asioOpen`: the copy plays through that driver; otherwise JUCE fell back on
// another output, or none (`anyOpen` false).
std::vector<std::string> asioNotice(bool asioOpen, bool anyOpen);

// The number of this copy of the standalone application, held while it lives.
class StandaloneSlot {
public:
    explicit StandaloneSlot(const juce::String& app);
    ~StandaloneSlot();
    StandaloneSlot(const StandaloneSlot&) = delete;
    StandaloneSlot& operator=(const StandaloneSlot&) = delete;
    int number() const noexcept { return number_; }

private:
    int number_ = 1;
    std::unique_ptr<juce::InterProcessLock> lock_;
};

}  // namespace stym::plugin
