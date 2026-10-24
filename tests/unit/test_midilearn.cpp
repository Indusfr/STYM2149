// STYM2149 — MIDI learn (M6, A4, D-146): map of the learned controls, capture of the messages on the audio thread
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

#include "plugincore/MidiLearn.h"

using namespace stym::plugin;

namespace {

bool claim(MidiCapture& c, uint8_t a, uint8_t b, uint8_t d, int size = 3) {
    const std::array<uint8_t, 3> m{a, b, d};
    return c.claim(m.data(), size);
}

}  // namespace

TEST_CASE("MIDI learn: message keys, map in the midiMap format", "[midilearn]") {
    CHECK(midiKey({0xB3, 74, 10}) == "CC74");  // channel ignored
    CHECK(midiKey({0x90, 36, 100}) == "Note36");
    CHECK(midiKey({0x80, 36, 0}) == "Note36");
    CHECK(midiKey({0x9F, 0, 0}) == "Note0");
    CHECK_FALSE(midiKey({0xB0, 120, 0}));  // channel mode: never learned
    CHECK_FALSE(midiKey({0xB0, 123, 0}));
    CHECK_FALSE(midiKey({0xE0, 0, 64}));   // pitch bend
    CHECK_FALSE(midiKey({0xC0, 5, 0}));    // Program Change

    // JSON.stringify(midiMap): keys in learning order; learning a key again replaces it in place.
    MidiMap map;
    bindMidi(map, "CC74", "ef", true);
    bindMidi(map, "Note36", "seqPlay", false);
    bindMidi(map, "CC74", "np", true);
    CHECK(dumpMidiMap(map) == R"({"CC74":{"id":"np","kind":"range"},"Note36":{"id":"seqPlay","kind":"btn"}})");
    CHECK(parseMidiMap(dumpMidiMap(map)) == map);
    CHECK(dumpMidiMap({}) == "{}");
    REQUIRE(findMidi(map, "Note36"));
    CHECK(findMidi(map, "Note36")->id == "seqPlay");
    CHECK_FALSE(findMidi(map, "CC1"));

    // Tolerant reading: invalid entries ignored, unreadable text = empty map.
    const MidiMap read = parseMidiMap(R"({"CC7":{"id":"master","kind":"range"},"CC120":{"id":"x","kind":"btn"},
        "CC07":{"id":"x","kind":"btn"},"CC128":{"id":"x","kind":"btn"},"Note5":{"id":"","kind":"btn"},
        "Note6":{"id":"a","kind":"knob"},"Pad1":{"id":"a","kind":"btn"},"Note127":{"id":"octUp","kind":"btn","x":1},
        "CC9":3})");
    REQUIRE(read.size() == 2);
    CHECK(read[0] == MidiBinding{"CC7", "master", true});
    CHECK(read[1] == MidiBinding{"Note127", "octUp", false});
    CHECK(parseMidiMap("{").empty());
    CHECK(parseMidiMap("[]").empty());

    const MidiKeys k = midiKeys(read);
    CHECK(k.cc.count() == 1);
    CHECK(k.cc.test(7));
    CHECK(k.notes.count() == 1);
    CHECK(k.notes.test(127));
}

TEST_CASE("MIDI learn: messages captured on the audio thread", "[midilearn]") {
    MidiCapture c;
    CHECK_FALSE(claim(c, 0xB0, 74, 10));
    MidiKeys k;
    k.cc.set(74);
    k.cc.set(100);
    k.notes.set(36);
    c.set(k, false);
    // Learned: CC (any channel), note played and released (note on with zero velocity included).
    CHECK(claim(c, 0xB0, 74, 10));
    CHECK(claim(c, 0xB5, 100, 0));
    CHECK(claim(c, 0x90, 36, 100));
    CHECK(claim(c, 0x80, 36, 0));
    CHECK(claim(c, 0x92, 36, 0));
    // Not learned, or of another type.
    CHECK_FALSE(claim(c, 0xB0, 75, 10));
    CHECK_FALSE(claim(c, 0x90, 37, 100));
    CHECK_FALSE(claim(c, 0xE0, 74, 64));
    CHECK_FALSE(claim(c, 0xA0, 36, 64));   // polyphonic pressure
    CHECK_FALSE(claim(c, 0xB0, 74, 10, 2));

    // Armed: CC 120 to 127 and releases pass, the first CC or note played is captured and learned at once.
    c.set(k, true);
    CHECK(c.armed());
    CHECK_FALSE(claim(c, 0xB0, 123, 0));
    CHECK_FALSE(claim(c, 0x80, 50, 0));
    CHECK_FALSE(claim(c, 0x90, 50, 0));
    CHECK(c.armed());
    CHECK(claim(c, 0x90, 50, 90));
    CHECK_FALSE(c.armed());
    CHECK(claim(c, 0x80, 50, 0));
    CHECK_FALSE(claim(c, 0xB0, 21, 1));  // disarmed: a single message learned
    c.set(k, true);
    CHECK(claim(c, 0xB0, 21, 1));
    CHECK(claim(c, 0xB0, 21, 2));
    // The interface gives its keys back: those captured in excess are forgotten.
    c.set(k, false);
    CHECK_FALSE(claim(c, 0xB0, 21, 3));
    CHECK_FALSE(claim(c, 0x90, 50, 90));
    c.set(k, true);
    c.disarm();
    CHECK_FALSE(claim(c, 0xB0, 22, 3));
}
