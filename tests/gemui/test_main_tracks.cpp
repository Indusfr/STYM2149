// STYM2149 — gemui tests: main window keyboard Latch (P147), a held key let go when the sequencer cuts its note
// (D-431), the track tabs' context menu, Copy settings and Paste settings (P230, P231), and tracks switched off from
// their tabs during playback, heard through the driver.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Prototype: press(), forceRelease(), release() and the #kbLatch handler; chanCopy(i), chanPaste(i) and the
// contextmenu handler of the #chtabs buttons; the chan message of the worklet (laneOn 1 → 0: killLane).
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <span>
#include <string>
#include <vector>

#include "UiHost.h"
#include "plugincore/Driver.h"
#include "plugincore/Parameters.h"
#include "plugincore/State.h"
#include "pluginui/KbdWindow.h"

using namespace stym;
using namespace stym::ui_testing;
using plugin::Global;
using plugin::Track;

namespace {

// Point on white key `index` of the main keyboard (28 white keys from C of the keyboard octave), below the black
// keys.
gemui::Point whiteKey(pluginui::MainWindow& w, int index) {
    const auto r = w.rectOf("kbd");
    REQUIRE(r);
    return {r->x + int((index + 0.5) * r->w / 28.0), r->bottom() - 6};
}

void mouseDown(pluginui::MainWindow& w, gemui::Point p) {
    w.desktop().mouseMove(p, {});
    w.desktop().mouseDown(p, gemui::MouseButton::Left, {});
}

void mouseUp(pluginui::MainWindow& w, gemui::Point p) { w.desktop().mouseUp(p, {}); }

void keyDown(pluginui::MainWindow& w, char32_t c) { w.desktop().keyDown(charKey(c)); }
void keyUp(pluginui::MainWindow& w, char32_t c) { w.desktop().keyUp(charKey(c)); }

// Right click on tab `track`, then entry `item` of its menu.
void tabMenuPick(pluginui::MainWindow& w, int track, int item) {
    click(w, "tab" + std::to_string(track), gemui::MouseButton::Right);
    REQUIRE(topMenu(w));
    pickItem(w, item);
}

// The Send of the delay goes with the pan: a setting of the Mixer (D-299), like the right side of a stereo DMA sample
// (D-459).
constexpr bool notCopied(Track t) {
    return t == Track::On || t == Track::Pan || t == Track::Mute || t == Track::Solo || t == Track::Send ||
           t == Track::LevelR || t == Track::MuteR || t == Track::SoloR || t == Track::Link;
}

// The plugin's driver playing what the editor wrote (Free run, separate outputs of tracks 1 to 4), in blocks of 256
// samples; RMS of each output, step by step, over the blocks since the last clear().
struct Playback {
    plugin::Driver d;
    plugin::ParamValues v;
    std::vector<float> l = std::vector<float>(256), r = std::vector<float>(256);
    std::array<std::vector<float>, 4> t;
    std::array<std::array<double, 16>, 5> sum{};  // main output, then tracks 1 to 4
    std::array<int, 16> count{};

    explicit Playback(const UiHost& h) : v(h.values) {
        for (auto& x : t) x.assign(256, 0.0f);
        plugin::Snapshot s;
        s.state = h.state;
        s.state.global.seqClock = ymengine::SeqClock::Internal;
        plugin::fillSequence(s.state.seq, s.seq);
        s.patternSerial = 1;
        d.prepare({48000, 0b1111}, v, s);
    }
    void block(bool play = false) {
        plugin::MidiEvent e{};
        e.screen = true;
        e.command = plugin::UiCommand::SeqPlay;
        ymengine::Outputs o;
        o.left = l.data();
        o.right = r.data();
        for (size_t k = 0; k < t.size(); ++k) o.track[k] = t[k].data();
        d.process(v, play ? std::span<const plugin::MidiEvent>(&e, 1) : std::span<const plugin::MidiEvent>(), o, 256,
                  false, nullptr);
        const int st = d.engine().seqStatus().step;
        if (st < 0) return;
        auto energy = [](const std::vector<float>& x) {
            double e2 = 0;
            for (float y : x) e2 += double(y) * double(y);
            return e2;
        };
        sum[0][size_t(st % 16)] += energy(l);
        for (size_t k = 0; k < t.size(); ++k) sum[k + 1][size_t(st % 16)] += energy(t[k]);
        count[size_t(st % 16)] += 256;
    }
    void steps(int n) {
        for (int64_t end = d.engine().seqStatus().stepAbs + n; d.engine().seqStatus().stepAbs < end;) block();
    }
    void clear() {
        for (auto& x : sum) x.fill(0);
        count.fill(0);
    }
    // out: 0 = main output, 1 to 4 = track 1 to 4.
    double rms(int out, int step) const {
        return count[size_t(step)] ? std::sqrt(sum[size_t(out)][size_t(step)] / count[size_t(step)]) : 0;
    }
};

}  // namespace

TEST_CASE("Latch: keys stay held until pressed again, one mouse note at a time, released when Latch goes off (P147)",
          "[ui][kbd][latch]") {
    UiHost h;
    pluginui::MainWindow w(h);
    const gemui::Point c2 = whiteKey(w, 0), d2 = whiteKey(w, 1), e2 = whiteKey(w, 2);
    h.take();

    // Latch off: the note stops when the mouse button is released.
    mouseDown(w, c2);
    CHECK(h.take() == std::vector<Midi>{{kNoteOn, 36, 127}});
    mouseUp(w, c2);
    CHECK(h.take() == std::vector<Midi>{{kNoteOff, 36, 0}});

    // Latch on: the note holds after the release; another key under the mouse takes its place (mono).
    click(w, "kbLatch");
    mouseDown(w, c2);
    mouseUp(w, c2);
    CHECK(h.take() == std::vector<Midi>{{kNoteOn, 36, 127}});
    mouseDown(w, d2);
    CHECK(h.take() == std::vector<Midi>{{kNoteOff, 36, 0}, {kNoteOn, 38, 127}});
    mouseUp(w, d2);
    CHECK(h.take().empty());

    // Computer keyboard: each key holds its own note, next to the mouse note.
    w.keyboardWindow().show();
    REQUIRE(w.rectOf("kbdWin"));
    REQUIRE(w.rectOf("kbdWin")->bottom() <= w.rectOf("kbd")->y);
    keyDown(w, U'a');
    keyUp(w, U'a');
    keyDown(w, U'e');
    keyUp(w, U'e');
    CHECK(h.take() == std::vector<Midi>{{kNoteOn, 36, 127}, {kNoteOn, 39, 127}});

    // Pressing a held key again releases it; the mouse rule only touches mouse notes.
    mouseDown(w, d2);
    mouseUp(w, d2);
    CHECK(h.take() == std::vector<Midi>{{kNoteOff, 38, 0}});
    mouseDown(w, e2);
    mouseUp(w, e2);
    CHECK(h.take() == std::vector<Midi>{{kNoteOn, 40, 127}});
    keyDown(w, U'a');
    keyUp(w, U'a');
    CHECK(h.take() == std::vector<Midi>{{kNoteOff, 36, 0}});

    // Latch off: every held note is released.
    click(w, "kbLatch");
    CHECK(h.take() == std::vector<Midi>{{kNoteOff, 39, 0}, {kNoteOff, 40, 0}});

    // Glissando with Latch: the note follows the pointer and the last one holds.
    click(w, "kbLatch");
    mouseDown(w, c2);
    w.desktop().mouseDrag(d2, {});
    mouseUp(w, d2);
    CHECK(h.take() == std::vector<Midi>{{kNoteOn, 36, 127}, {kNoteOff, 36, 0}, {kNoteOn, 38, 127}});
    click(w, "kbLatch");
    CHECK(h.take() == std::vector<Midi>{{kNoteOff, 38, 0}});
    // Off again: back to plain playing.
    mouseDown(w, e2);
    mouseUp(w, e2);
    CHECK(h.take() == std::vector<Midi>{{kNoteOn, 40, 127}, {kNoteOff, 40, 0}});
}

TEST_CASE("Latch: a held key lets go when the sequencer cuts its note, and plays again (D-431)", "[ui][kbd][latch]") {
    // Indus, 25/10/2026 19:41: a latched key stayed lit once the sequencer, started with Space, took its voice.
    UiHost h;
    pluginui::MainWindow w(h);
    const gemui::Point c2 = whiteKey(w, 0);
    click(w, "kbLatch");
    mouseDown(w, c2);
    mouseUp(w, c2);
    CHECK(h.take() == std::vector<Midi>{{kNoteOn, 36, 127}});

    // The engine reports the note cut: the key lets go (the note off is harmless, the voice already plays another).
    plugin::EngineStatus t;
    t.manualCut[36] = 1;
    h.status = t;
    w.tick(0);
    CHECK(h.take() == std::vector<Midi>{{kNoteOff, 36, 0}});
    // The same counters again: nothing more; a cut note that no key holds: nothing.
    h.status = t;
    w.tick(0);
    t.manualCut[40] = 1;
    h.status = t;
    w.tick(0);
    CHECK(h.take().empty());
    // The key plays its note again instead of releasing it.
    mouseDown(w, c2);
    mouseUp(w, c2);
    CHECK(h.take() == std::vector<Midi>{{kNoteOn, 36, 127}});
    // Without Latch too: a key held down whose note was cut lets go, its release then sends nothing.
    click(w, "kbLatch");
    CHECK(h.take() == std::vector<Midi>{{kNoteOff, 36, 0}});
    mouseDown(w, c2);
    CHECK(h.take() == std::vector<Midi>{{kNoteOn, 36, 127}});
    t.manualCut[36] = 2;
    h.status = t;
    w.tick(0);
    CHECK(h.take() == std::vector<Midi>{{kNoteOff, 36, 0}});
    mouseUp(w, c2);
    CHECK(h.take().empty());
}

TEST_CASE("SYSTEM Note line: the newest note's period, back to \"-\" once no voice sounds (D-440)", "[ui][note]") {
    // Indus, 26/10/2026 04:00-04:02: the period stayed shown with no note playing; the line stays, as at launch.
    UiHost h;
    pluginui::MainWindow w(h);
    CHECK(w.noteReadout() == "-");
    plugin::EngineStatus t;
    t.noteSerial = 1;
    t.notePeriod = 284;
    t.noteSounding = true;
    h.status = t;
    w.tick(0);
    CHECK(w.noteReadout() == "TP=284 -> 440.14 Hz");
    // The release goes on: the line stays.
    t.serial = 2;
    h.status = t;
    w.tick(0);
    CHECK(w.noteReadout() == "TP=284 -> 440.14 Hz");
    // No voice left: "-", the register monitor keeps the last period.
    t.noteSounding = false;
    h.status = t;
    w.tick(0);
    CHECK(w.noteReadout() == "-");
    // The next note shows again.
    t.noteSerial = 2;
    t.notePeriod = 142;
    t.noteSounding = true;
    h.status = t;
    w.tick(0);
    CHECK(w.noteReadout() == "TP=142 -> 880.28 Hz");
}

TEST_CASE("Track tab context menu: Copy settings, Paste settings, Rename...; disabled in 3ch (P230)", "[ui][tracks]") {
    UiHost h;
    pluginui::MainWindow w(h);
    const auto tab = w.rectOf("tab2");
    REQUIRE(tab);

    // Right click: the menu under the tab, nothing checked; the tab is neither focused nor switched.
    click(w, "tab2", gemui::MouseButton::Right);
    REQUIRE(menuLabels(w) == std::vector<std::string>{"Copy settings", "Paste settings", "---", "Rename..."});
    const gemui::MenuList* m = topMenu(w);
    CHECK(m->bounds().y == tab->bottom() - 2);  // its frame on the tab's bottom rule
    CHECK_FALSE(m->items()[3].disabled);
    CHECK(h.p(Global::FocusTrack) == 1.0f);
    CHECK(h.p(2, Track::On) == 0.0f);
    // Esc closes it.
    key(w, gemui::Key::Escape);
    CHECK_FALSE(topMenu(w));

    // Rename... asks for the name.
    tabMenuPick(w, 2, 3);
    REQUIRE(topAlert(w));
    CHECK(topAlert(w)->text() == "Track name, 8 chars (blank = number):");
    key(w, gemui::Key::Escape);

    // 3ch: the voices are named by the chip, Rename... is disabled and does nothing.
    UiHost h3;
    h3.p(Global::VoiceMode) = 1;
    pluginui::MainWindow w3(h3);
    click(w3, "tab1", gemui::MouseButton::Right);
    REQUIRE(menuLabels(w3) == std::vector<std::string>{"Copy settings", "Paste settings", "---", "Rename..."});
    const gemui::MenuItem& ren = topMenu(w3)->items()[3];
    CHECK(ren.disabled);
    CHECK(ren.why == "The three YM voices are named by the chip");
    pickItem(w3, 3);
    CHECK_FALSE(topAlert(w3));
    CHECK(h3.state.trackNames[1].empty());
}

TEST_CASE("Copy settings and Paste settings: patch, arpeggio, level and gate, not on/pan/mute/solo/send (P231)",
          "[ui][tracks]") {
    UiHost h;
    // Track 1: every setting away from its default.
    for (int f = 0; f < plugin::kTrackParams; ++f) {
        const plugin::ParamSpec& sp = plugin::paramSpec(plugin::paramIndex(0, Track(f)));
        float v = plugin::clampToSpec(sp, (sp.min + sp.max) / 2);
        if (v == sp.def) v = plugin::clampToSpec(sp, sp.max);
        h.p(0, Track(f)) = v;
    }
    h.p(0, Track::Generator) = float(int(ymengine::Generator::Sid));
    ymengine::TrackParams& src = h.state.tracks[0];
    src.patch.ornamentPitch[2] = 5;
    src.patch.ornamentVolume[3] = 7;
    src.arp.mode = ymengine::ArpMode::Table;
    src.arp.table[1] = 3;
    src.arp.length = 2;
    h.state.trackNames[0] = "Bass";
    h.state.seq.tracks[0].octave = 5;
    pluginui::MainWindow w(h);
    const plugin::ParamValues before = h.values;

    // Paste with nothing copied: nothing changes.
    tabMenuPick(w, 2, 1);
    CHECK(h.values == before);

    tabMenuPick(w, 0, 0);
    // Changes made after the copy are not pasted (the copy is a snapshot).
    h.editExtras([](plugin::NonParamState& s) { s.tracks[0].patch.ornamentPitch[2] = 9; });
    h.p(0, Track::Gate) = plugin::paramSpec(plugin::paramIndex(0, Track::Gate)).min;
    w.tick(0);
    tabMenuPick(w, 2, 1);

    for (int f = 0; f < plugin::kTrackParams; ++f) {
        const Track t = Track(f);
        INFO("field " << plugin::paramSpec(plugin::paramIndex(2, t)).id);
        if (notCopied(t)) CHECK(h.p(2, t) == before[size_t(plugin::paramIndex(2, t))]);
        else CHECK(h.p(2, t) == before[size_t(plugin::paramIndex(0, t))]);
    }
    const ymengine::TrackParams& dst = h.state.tracks[2];
    CHECK(dst.patch.ornamentPitch[2] == 5);
    CHECK(dst.patch.ornamentVolume[3] == 7);
    CHECK(dst.arp == h.state.tracks[0].arp);
    // Neither the name nor the sequencer octave of the track.
    CHECK(h.state.trackNames[2].empty());
    CHECK(h.state.seq.tracks[2].octave == 3);
    // The source track is untouched.
    CHECK(h.state.tracks[0].patch.ornamentPitch[2] == 9);
}

// DAW test, item 9: demo pattern, Authentic 3ch, the other chip tracks switched off from their tabs during playback.
// Switched off in the middle of a tied Sync Buzzer (FX row: track A at step 8, track B at step 14), a track rang on
// for ever at the full level of the envelope, over the SID and Chip notes of the track left on. The editor writes
// the demo, the mode and the switches; the plugin's driver plays them.
TEST_CASE("Demo pattern in Authentic 3ch: tracks switched off during a tied note fall silent, the one left on is heard",
          "[ui][tracks][driver]") {
    struct Case {
        int keep, at;  // track left on (A or B), step where the others are switched off
    };
    for (const Case k : {Case{1, 8}, Case{0, 14}}) {
        CAPTURE(k.keep, k.at);
        UiHost h;
        pluginui::MainWindow w(h);
        w.demoPattern();  // File > Demo pattern (D-301); empty tracks: no confirmation
        REQUIRE(h.state.seq.cur().lane(1).pat[2] == 12);
        Playback g(h);
        g.block(true);
        g.steps(16);  // one loop in 16 channels

        click(w, "modeSel");
        pickItem(w, 1);
        REQUIRE(h.p(Global::VoiceMode) == 1.0f);
        g.v = h.values;
        for (int n = 0; n < 4000 && !(g.d.engine().global().mode == ymengine::VoiceMode::Authentic3 &&
                                       g.d.engine().seqStatus().step == k.at);
             ++n)
            g.block();
        REQUIRE(g.d.engine().seqStatus().step == k.at);
        for (int n = 0; n < 4; ++n) g.block();  // into the step
        const int other = 1 - k.keep;
        const ymengine::Engine::VoiceInfo tied = g.d.engine().voice(other);  // 3 channels: voice = channel
        REQUIRE(tied.on);
        REQUIRE(tied.track == other);
        REQUIRE(tied.gen == ymengine::Generator::SyncBuzzer);

        for (const int t : {other, 2}) {  // click: focus, click again: off
            click(w, "tab" + std::to_string(t));
            if (h.p(t, Track::On) != 0) click(w, "tab" + std::to_string(t));
            REQUIRE(h.p(t, Track::On) == 0.0f);
        }
        REQUIRE(h.p(k.keep, Track::On) == 1.0f);
        g.v = h.values;
        g.steps(16);
        g.clear();
        g.steps(16);

        for (int i = 0; i < ymengine::Engine::kVoices; ++i) {
            const ymengine::Engine::VoiceInfo v = g.d.engine().voice(i);
            CHECK_FALSE((v.on && (v.track == other || v.track == 2)));
        }
        // Second loop after the switch-off: the tracks switched off are silent (the Digidrum track has no sample),
        // the main output is the track left on, whose notes are heard (SID lead of B, Chip bass of A, and their
        // Sync Buzzer steps).
        const ymseq::Lane& lane = h.state.seq.cur().lane(k.keep);
        for (int s = 0; s < 16; ++s) {
            CAPTURE(s);
            CHECK(g.rms(1 + other, s) == 0.0);
            CHECK(g.rms(3, s) == 0.0);
            CHECK(g.rms(4, s) == 0.0);
            CHECK(std::fabs(g.rms(0, s) - g.rms(1 + k.keep, s)) < 1e-6);
            if (lane.pat[size_t(s)] >= 0) CHECK(g.rms(1 + k.keep, s) > 0.03);
        }
    }
}
