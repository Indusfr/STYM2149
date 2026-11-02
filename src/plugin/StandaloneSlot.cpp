// STYM2149 — plugin: the copies of the standalone application, each with its own settings
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "StandaloneSlot.h"

#include <algorithm>
#include <atomic>

namespace stym::plugin {
namespace {

std::atomic<int> copyNumber{1};
std::atomic<juce::PropertySet*> copySettings{nullptr};

}  // namespace

int lowestFreeSlot(const std::function<bool(int)>& take) {
    for (int n = 1; n <= kStandaloneSlots; ++n)
        if (take(n)) return n;
    return 0;
}

juce::String slotName(const juce::String& app, int slot) {
    return slot <= 1 ? app : app + " (" + juce::String(slot) + ")";
}

int slotWindowOffset(int slot) { return 32 * ((std::max(slot, 1) - 1) % 16); }

void seedSlotSettings(const juce::PropertySet& first, juce::PropertySet& settings, int slot) {
    if (first.containsKey("audioSetup")) {
        if (std::unique_ptr<juce::XmlElement> setup = first.getXmlValue("audioSetup")) {
            // JUCE's AudioDeviceManager::createStateXml: one MIDIINPUT element per enabled input.
            while (juce::XmlElement* input = setup->getChildByName("MIDIINPUT")) setup->removeChildElement(input, true);
            settings.setValue("audioSetup", setup.get());
        }
    }
    if (first.containsKey("shouldMuteInput"))
        settings.setValue("shouldMuteInput", first.getBoolValue("shouldMuteInput"));
    if (first.containsKey("windowX") && first.containsKey("windowY")) {
        const int step = slotWindowOffset(slot);
        settings.setValue("windowX", first.getIntValue("windowX") + step);
        settings.setValue("windowY", first.getIntValue("windowY") + step);
    }
}

juce::PropertySet* standaloneSettings() noexcept { return copySettings.load(std::memory_order_acquire); }

void setStandaloneSettings(juce::PropertySet* settings) noexcept {
    copySettings.store(settings, std::memory_order_release);
}

void adoptMidiMap(const juce::PropertySet& preferences, juce::PropertySet& settings, int slot) {
    if (settings.containsKey("midiMap")) return;
    if (slot <= 1) settings.setValue("midiMap", preferences.getValue("midiMap", "{}"));
}

void forgetUnsavedSession(juce::PropertySet& settings) {
    if (!settings.containsKey("configFile")) settings.removeValue("filterState");
}

bool usesAsio(const juce::PropertySet& settings) {
    const std::unique_ptr<juce::XmlElement> setup = settings.getXmlValue("audioSetup");
    return setup != nullptr && setup->getStringAttribute("deviceType") == "ASIO";
}

std::vector<std::string> asioNotice(bool asioOpen, bool anyOpen) {
    if (asioOpen)
        return {"This copy plays through the ASIO driver of the first copy.",
                "Most ASIO drivers serve one application at a time: if a copy stays silent, choose another output "
                "for it in Options > Audio/MIDI settings."};
    return {"The ASIO driver of the first copy serves one application at a time.",
            anyOpen ? "This copy plays through another output: choose it in Options > Audio/MIDI settings."
                    : "This copy has no audio output: choose one in Options > Audio/MIDI settings."};
}

int standaloneCopyNumber() noexcept { return copyNumber.load(std::memory_order_relaxed); }

StandaloneSlot::StandaloneSlot(const juce::String& app) {
    // The user's name in the lock's: on Linux, the lock is a file in /var/tmp, shared by every user of the computer.
    const juce::String base = app + " standalone " + juce::SystemStats::getLogonName() + " ";
    const int n = lowestFreeSlot([&](int slot) {
        const juce::String name = juce::File::createLegalFileName(base + juce::String(slot));
        auto lock = std::make_unique<juce::InterProcessLock>(name);
        if (!lock->enter(0)) return false;
        lock_ = std::move(lock);
        return true;
    });
    // Every number taken (or no lock to be had): the first copy's settings, shared as in earlier versions.
    number_ = n > 0 ? n : 1;
    copyNumber.store(number_, std::memory_order_relaxed);
}

StandaloneSlot::~StandaloneSlot() {
    if (lock_ != nullptr) lock_->exit();
    copyNumber.store(1, std::memory_order_relaxed);
}

}  // namespace stym::plugin
