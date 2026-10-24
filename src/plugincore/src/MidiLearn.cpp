// STYM2149 — plugincore: MIDI learn (P462)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Behaviours taken from the prototype: midiMap, saveMidiMap, midiBind and the sorting of the messages of onMIDI and
// onCC.
#include "plugincore/MidiLearn.h"

#include <algorithm>
#include <charconv>

#include "plugincore/Json.h"

namespace stym::plugin {

namespace {

// "CC<n>" or "Note<n>", n from 0 to 127 in decimal without a leading zero: 0 for CC, 1 for Note, -1 otherwise.
int parseKey(std::string_view key, int& n) noexcept {
    int kind = -1;
    if (key.substr(0, 2) == "CC") {
        kind = 0;
        key.remove_prefix(2);
    } else if (key.substr(0, 4) == "Note") {
        kind = 1;
        key.remove_prefix(4);
    }
    if (kind < 0 || key.empty() || key.size() > 3 || (key.size() > 1 && key[0] == '0')) return -1;
    const auto [end, ec] = std::from_chars(key.data(), key.data() + key.size(), n);
    if (ec != std::errc() || end != key.data() + key.size() || n < 0 || n > 127) return -1;
    if (kind == 0 && n >= 120) return -1;  // channel mode: never learned
    return kind;
}

}  // namespace

std::optional<std::string> midiKey(const std::array<uint8_t, 3>& m) {
    const int st = m[0] & 0xF0, n = m[1] & 0x7F;
    if (st == 0xB0) {
        if (n >= 120) return std::nullopt;
        return "CC" + std::to_string(n);
    }
    if (st == 0x90 || st == 0x80) return "Note" + std::to_string(n);
    return std::nullopt;
}

MidiMap parseMidiMap(std::string_view text) {
    MidiMap map;
    const auto v = json::parse(text);
    if (!v || !v->isObject()) return map;
    for (const auto& [key, target] : v->members()) {
        int n = 0;
        if (parseKey(key, n) < 0 || !target.isObject()) continue;
        const json::Value* id = target.find("id");
        const json::Value* kind = target.find("kind");
        if (!id || !id->isString() || id->string().empty() || !kind || !kind->isString()) continue;
        if (kind->string() != "range" && kind->string() != "btn") continue;
        // A key repeated in the text keeps its first place and takes its last value, as with JSON.parse.
        bindMidi(map, key, id->string(), kind->string() == "range");
    }
    return map;
}

std::string dumpMidiMap(const MidiMap& map) {
    json::Value o = json::Value::object();
    for (const MidiBinding& b : map) {
        json::Value t = json::Value::object();
        t.set("id", b.id);
        t.set("kind", b.range ? "range" : "btn");
        o.set(b.key, std::move(t));
    }
    return json::dump(o);
}

void bindMidi(MidiMap& map, std::string_view key, std::string id, bool range) {
    for (MidiBinding& b : map)
        if (b.key == key) {
            b.id = std::move(id);
            b.range = range;
            return;
        }
    map.push_back({std::string(key), std::move(id), range});
}

const MidiBinding* findMidi(const MidiMap& map, std::string_view key) noexcept {
    for (const MidiBinding& b : map)
        if (b.key == key) return &b;
    return nullptr;
}

MidiKeys midiKeys(const MidiMap& map) {
    MidiKeys k;
    for (const MidiBinding& b : map) {
        int n = 0;
        const int kind = parseKey(b.key, n);
        if (kind == 0) k.cc.set(size_t(n));
        else if (kind == 1) k.notes.set(size_t(n));
    }
    return k;
}

// ------------------------------------------------------------------------------------------- audio thread

void MidiCapture::set(const MidiKeys& keys, bool armed) noexcept {
    for (size_t w = 0; w < 2; ++w) {
        uint64_t c = 0, n = 0;
        for (size_t b = 0; b < 64; ++b) {
            if (keys.cc.test(w * 64 + b)) c |= uint64_t(1) << b;
            if (keys.notes.test(w * 64 + b)) n |= uint64_t(1) << b;
        }
        cc_[w].store(c, std::memory_order_relaxed);
        notes_[w].store(n, std::memory_order_relaxed);
    }
    armed_.store(armed, std::memory_order_release);
}

bool MidiCapture::test(const std::array<std::atomic<uint64_t>, 2>& bits, int n) noexcept {
    return (bits[size_t(n >> 6)].load(std::memory_order_relaxed) >> (n & 63)) & 1;
}

void MidiCapture::mark(std::array<std::atomic<uint64_t>, 2>& bits, int n) noexcept {
    bits[size_t(n >> 6)].fetch_or(uint64_t(1) << (n & 63), std::memory_order_relaxed);
}

bool MidiCapture::claim(const uint8_t* data, int size) noexcept {
    if (size != 3) return false;
    const int st = data[0] & 0xF0, n = data[1] & 0x7F;
    // onMIDI and onCC: when armed, the CC or the note played is learned (midiBind); when learned, it goes to the
    // interface.
    // The plain load skips the read-modify-write when nothing is armed; the exchange makes sure that a single message
    // is taken for the armed control.
    auto learn = [this](std::array<std::atomic<uint64_t>, 2>& bits, int k) {
        if (!armed_.load(std::memory_order_acquire) || !armed_.exchange(false, std::memory_order_acq_rel))
            return false;
        mark(bits, k);
        return true;
    };
    if (st == 0xB0) {
        if (n >= 120) return false;
        return learn(cc_, n) || test(cc_, n);
    }
    // A note on at velocity 0 is a note off.
    if (st == 0x90 && data[2] > 0) return learn(notes_, n) || test(notes_, n);
    if (st == 0x80 || st == 0x90) return test(notes_, n);  // release of a learned pad: re-arms the rising edge
    return false;
}

}  // namespace stym::plugin
