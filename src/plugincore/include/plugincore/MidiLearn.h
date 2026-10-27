// STYM2149 — plugincore: MIDI learn, map of the learned controls and capture of the messages on the audio thread
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// midiMap of the prototype: a key "CC<n>" or "Note<n>" (channel ignored) leads to an interface control, slider
// ("range") or button ("btn"), designated by its prototype id. The map is shared by all projects (preferences file,
// like the localStorage of the prototype), in the format of JSON.stringify(midiMap).
//
// Audio thread: MidiCapture tells which messages go to the interface rather than to the engine: those of a learned
// key (a learned note does not play, a learned CC comes before the controllers of the prototype), and, when a
// control is armed, the first CC or the first note played, taken for it. Channel mode messages (CC 120 to 127) are
// never learned: they keep their meaning (all off, reset).
#pragma once
#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stym::plugin {

// One learned control: the key of the message and the control it leads to.
struct MidiBinding {
    std::string key;     // "CC74", "Note36"
    std::string id;      // control: prototype id ("ef", "seqPlay"...)
    bool range = false;  // slider (otherwise button)
    bool operator==(const MidiBinding&) const = default;
};
using MidiMap = std::vector<MidiBinding>;  // in learning order (keys of the JavaScript object)

// Key of a message: CC (except 120-127), note on or off; nullopt for the others.
std::optional<std::string> midiKey(const std::array<uint8_t, 3>& message);

// Tolerant reading (invalid entry ignored, unreadable text: empty map); writing like JSON.stringify.
MidiMap parseMidiMap(std::string_view json);
std::string dumpMidiMap(const MidiMap& map);
// Learns `key` for a control: replaced in place if it exists, appended otherwise (midiMap[key] = ...).
void bindMidi(MidiMap& map, std::string_view key, std::string id, bool range);
// Binding of `key`; nullptr if it is not learned.
const MidiBinding* findMidi(const MidiMap& map, std::string_view key) noexcept;

// The learned keys as sets of CC numbers and note numbers, for MidiCapture (invalid keys are left out).
struct MidiKeys {
    std::bitset<128> cc, notes;
    bool operator==(const MidiKeys&) const = default;
};
MidiKeys midiKeys(const MidiMap& map);

// Written on the message thread, read on the audio thread: each set of 128 keys lives in two atomic 64-bit words, so
// that both threads reach it without a lock.
class MidiCapture {
public:
    // Message thread: learned keys, armed control (the next CC or the next note played is taken for it).
    void set(const MidiKeys& keys, bool armed) noexcept;
    void disarm() noexcept { armed_.store(false, std::memory_order_release); }
    bool armed() const noexcept { return armed_.load(std::memory_order_acquire); }

    // Audio thread, without lock or allocation: true if the message goes to the interface. Taken for the armed
    // control, the message disarms and its key counts as learned at once (the interface then confirms it by set()).
    bool claim(const uint8_t* data, int size) noexcept;

private:
    static bool test(const std::array<std::atomic<uint64_t>, 2>& bits, int n) noexcept;
    static void mark(std::array<std::atomic<uint64_t>, 2>& bits, int n) noexcept;

    std::array<std::atomic<uint64_t>, 2> cc_{}, notes_{};  // key n: bit n % 64 of word n / 64
    std::atomic<bool> armed_{false};
};

}  // namespace stym::plugin
