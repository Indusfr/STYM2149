// STYM2149 — gemshot: scenes of the main window
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "Scenes.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <numbers>
#include <optional>

#include "PrototypeFont.h"
#include "gemui/Color.h"
#include "gemui/Controls.h"
#include "gemui/Font.h"
#include "plugincore/Instruments.h"
#include "plugincore/Mapping.h"
#include "plugincore/YmTune.h"
#include "pluginui/MainWindow.h"

namespace stym::gemshot {
namespace {

using plugin::Global;
using plugin::paramIndex;
using plugin::Track;

// Test host: parameters and state in memory, standalone application without MIDI (like the capture machine).
class FakeHost : public pluginui::Host {
public:
    plugin::ParamValues values = plugin::defaultValues();
    plugin::NonParamState state;
    uint64_t serial = 1;
    std::optional<plugin::EngineStatus> pending;
    plugin::EngineStatus last;
    std::map<std::string, std::string, std::less<>> prefs;
    bool alone = true;  // standalone application, otherwise a plugin

    float param(int i) const override { return values[size_t(i)]; }
    void beginGesture(int) override {}
    void setParam(int i, float v) override { values[size_t(i)] = plugin::clampToSpec(plugin::paramSpec(i), v); }
    void endGesture(int) override {}
    uint64_t extrasVersion() const override { return serial; }
    plugin::NonParamState extras() const override { return state; }
    void editExtras(const std::function<void(plugin::NonParamState&)>& edit) override {
        edit(state);
        ++serial;
    }
    void screenMidi(uint8_t, uint8_t, uint8_t) override {}
    void setMidiCapture(const plugin::MidiKeys&, bool) override {}
    bool learnMidi(std::array<uint8_t, 3>& m) override {
        if (learned.empty()) return false;
        m = learned.front();
        learned.pop_front();
        return true;
    }
    bool hostNote(std::array<uint8_t, 3>&) override { return false; }
    void seqCommand(SeqCommand, int, int, int) override {}
    pluginui::EditorSession& session() override { return editor; }
    pluginui::EditorSession editor;
    const plugin::EngineStatus* engineStatus() override {
        if (!pending) return nullptr;
        last = *pending;
        pending.reset();
        return &last;
    }
    bool standalone() const override { return alone; }
    // Same state as the captures: Chromium without MIDI access.
    std::string midiStatus() const override { return "access denied"; }
    std::string version() const override { return STYM_VERSION; }
    void quit() override {}
    bool hasAudioSettings() const override { return true; }
    void openAudioSettings() override {}
    void loadDigidrum(int) override {}
    std::vector<TextFile> languageFiles() const override {
        static const std::vector<TextFile> files = installedLanguages();
        return files;
    }
    std::string preference(std::string_view key, std::string_view fallback) const override {
        const auto it = prefs.find(key);
        return it == prefs.end() ? std::string(fallback) : it->second;
    }
    void setPreference(std::string_view key, std::string_view value) override { prefs[std::string(key)] = value; }
    std::string patchLibrary() const override { return patches; }
    void setPatchLibrary(const std::string& json) override { patches = json; }

    Recorder recorder() const override { return rec; }
    void armRecorder(int chip) override {
        rec.state = Recorder::State::Armed;
        rec.chip = chip;
    }
    void disarmRecorder() override { rec.state = Recorder::State::Idle; }
    void clearRecording() override { rec.takeFrames = 0; }
    // YM Player: tune read by plugincore (name and duration of the real player), never played.
    std::string loadYm(std::string_view bytes, const std::string& fileName) override {
        const plugin::YmTuneLoad r = plugin::loadYmTune(
            {reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()}, fileName);
        if (!r.tune) return r.error;
        ym = {};
        ym.loaded = true;
        ym.name = r.tune->displayName();
        ym.frames = r.tune->frames(1);
        ym.rate = r.tune->frameRate();
        ym.subsongs = r.tune->subsongs();
        ym.sndh = r.tune->sndh != nullptr;
        return {};
    }
    YmPlayer ymPlayer() const override { return ym; }
    void ymClose() override { ym = {}; }

    std::string patches;
    std::deque<std::array<uint8_t, 3>> learned;  // messages taken for the interface (MIDI learn)
    Recorder rec;  // register recorder (D-157): state given by the scene
    YmPlayer ym;   // YM Player (D-169)
};

void set(FakeHost& h, Global g, float v) { h.values[size_t(paramIndex(g))] = v; }
void set(FakeHost& h, int track, Track f, float v) { h.values[size_t(paramIndex(track, f))] = v; }
void noCmdRow(FakeHost& h) { h.prefs["seqCmdRow"] = "0"; }

// Mouse resting at the centre of a control until its tooltip appears.
void hover(pluginui::MainWindow& w, const char* id) {
    const auto r = w.rectOf(id);
    if (!r) return;
    w.desktop().mouseMove({r->x + r->w / 2, r->y + r->h / 2}, {});
    w.desktop().tick(gemui::Desktop::kTipDelayMs + 1);
}

// Left click at the centre of a control, then the mouse goes back to the corner (as in capture.mjs).
void click(pluginui::MainWindow& w, const char* id) {
    const auto r = w.rectOf(id);
    if (!r) return;
    const gemui::Point p{r->x + r->w / 2, r->y + r->h / 2};
    w.desktop().mouseMove(p, {});
    w.desktop().mouseDown(p, gemui::MouseButton::Left, {});
    w.desktop().mouseUp(p, {});
    w.desktop().mouseMove({2, pluginui::MainWindow::kHeight - 2}, {});
}

// Entry of a submenu of a menu bar ("Export pattern" > "WAV" in the File menu), or the entry itself if `sub` is
// empty.
void menuPick(pluginui::MainWindow& w, int title, const std::string& entry, const std::string& sub = {}) {
    w.openMenu(title);
    const auto* list = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
    std::function<void()> pick;
    if (list)
        for (const gemui::MenuItem& m : list->items())
            if (m.label == entry) {
                if (sub.empty()) pick = m.pick;
                for (const gemui::MenuItem& s : m.sub)
                    if (s.label == sub) pick = s.pick;
            }
    w.desktop().closePopups();
    if (pick) pick();
    w.desktop().mouseMove({2, pluginui::MainWindow::kHeight - 2}, {});
}

struct Scene {
    std::string name;
    std::function<void(FakeHost&)> before;               // starting settings
    std::function<void(FakeHost&, pluginui::MainWindow&)> act;  // action on the window
    std::vector<const char*> masked = {};                 // controls ignored by the comparison (rectOf)
    const char* decision = "";                            // decision that justifies them
    int budget = 1;                                       // allowed deviation in % (Shot::budget)
    const char* alt = "";                                 // same window without the menu or the alert (Shot::alt)
};

const std::vector<Scene>& scenes() {
    static const std::vector<Scene> list = {
        {"default", {}, {}},
        // The SID wave under the SID settings (D-251): its dense controls ignored, the texts compared (with the frame's
        // white of the capture, where they are not).
        {"sid", {}, [](FakeHost&, pluginui::MainWindow& w) { w.pickGenerator(1); },
         {"waveGrid", "waveSteps", "waveSquare", "waveSine"}, "D-251"},
        // Mode button of the ADSR greyed out on a drum track (D-205).
        {"chipdrum", {}, [](FakeHost&, pluginui::MainWindow& w) { w.pickGenerator(4); }, {"adsrMode"}, "D-205"},
        // Waveform and note of the frame: rewritten texts (D-078), in the 6×6 font (D-071); with the small texts of
        // the rest of the window, the scene would exceed 1 % for these intended deviations alone. Mode button of the
        // ADSR greyed out on a drum track (D-205). Digidrum samples button where the prototype drew the waveform, the
        // waveform below it (D-213).
        {"digidrum", {}, [](FakeHost&, pluginui::MainWindow& w) { w.pickGenerator(2); },
         {"drumwave", "digiHint", "digiEdit", "adsrMode"}, "D-078, D-205, D-213"},
        {"envelope",
         [](FakeHost& h) {
             set(h, 0, Track::VolMode, 1);
             set(h, 0, Track::AdsrMode, 1);
             set(h, 0, Track::Attack, 200);
             set(h, 0, Track::Decay, 300);
             set(h, 0, Track::Sustain, 60);
             set(h, 0, Track::Release, 500);
         },
         {}},
        {"smooth",
         [](FakeHost& h) {
             set(h, 0, Track::Attack, 120);
             set(h, 0, Track::Decay, 260);
             set(h, 0, Track::Sustain, 45);
             set(h, 0, Track::Release, 700);
             set(h, 0, Track::VibDepth, 35);
         },
         {}},
        {"mode3", [](FakeHost& h) { set(h, Global::VoiceMode, 1); }, {}},
        {"dual", [](FakeHost& h) { set(h, Global::VoiceMode, 2); }, {}},
        {"tabs",
         [](FakeHost& h) {
             for (int t : {2, 5, 11}) set(h, t, Track::On, 1);
             set(h, Global::FocusTrack, 6);
             plugin::EngineStatus t;
             t.active[0] = t.active[5] = true;
             h.pending = t;
         },
         [](FakeHost&, pluginui::MainWindow& w) {
             w.setBaseOctave(3);
             w.lightKey(60, true);
             w.lightKey(63, true);
         }},
        {"menu_generator", {}, [](FakeHost&, pluginui::MainWindow& w) { click(w, "fxSel"); }},
        // The frames moved by the sixth DAW test (D-268), whose former place the menu or the alert covers in the
        // capture, compared with the default scene's (the envelope mode changes none of them).
        {"menu_shape", [](FakeHost& h) { set(h, 0, Track::VolMode, 1); },
         [](FakeHost&, pluginui::MainWindow& w) { click(w, "shapeSel"); }, {}, "", 1, "default"},
        {"alert_chip", {}, [](FakeHost&, pluginui::MainWindow& w) { w.showChipAlert(); }, {}, "", 1, "default"},
        // Floating windows (M5), at the prototype's places: 2 % (D-121).
        // Without the CMD row, shown by default since the ninth DAW test (item 117): the prototype's grid.
        {"seq", noCmdRow, [](FakeHost&, pluginui::MainWindow& w) { w.sequencer().show(); }, {}, "", 2},
        {"seq_demo", noCmdRow,
         [](FakeHost&, pluginui::MainWindow& w) {
             w.doDemo();
             w.sequencer().show();
         },
         {}, "", 2},
        {"song", {}, [](FakeHost&, pluginui::MainWindow& w) { w.songWindow().show(); }, {}, "", 2},
        // Written Song: repeats, own tempo, selection (Shift) and keyboard cursor on two different blocks.
        {"song_demo", {},
         [](FakeHost& h, pluginui::MainWindow& w) {
             w.doDemo();
             h.editExtras([](plugin::NonParamState& s) { s.seq.song.entries = {{0, 2, 0}, {1, 1, 140}, {9, 4, 0}, {0, 1, 0}}; });
             w.tick(0);
             w.songWindow().show();
             w.songWindow().setSelection({1}, 2);
         },
         {}, "", 2},
        // Arpeggiator: stopped, running (Up); the Sync button after the mode, not in the prototype, ignored (D-256). On
        // the MIDI clock and in Table mode: STYM2149 scenes (arp_midi, D-290; arp_table, D-322).
        {"arp", {}, [](FakeHost&, pluginui::MainWindow& w) { w.arpeggiator().show(); }},
        {"arp_on", [](FakeHost& h) { set(h, 0, Track::ArpOn, 1); },
         [](FakeHost&, pluginui::MainWindow& w) { w.arpeggiator().show(); }, {"arpSync"}, "D-256"},
        // Slide Curve: straight line by default, then a drawn curve that stops at 80 % (warning).
        {"curve", {}, [](FakeHost&, pluginui::MainWindow& w) { w.slideCurve().show(); }},
        {"curve_custom",
         [](FakeHost& h) {
             h.editExtras([](plugin::NonParamState& s) {
                 auto& t = s.global.slideTable;
                 for (size_t i = 0; i < t.size(); ++i)
                     t[i] = float(0.8 * std::sin(double(i) / double(t.size() - 1) * std::numbers::pi / 2));
             });
         },
         [](FakeHost&, pluginui::MainWindow& w) { w.slideCurve().show(); }},
        // Computer Keyboard: at octave 2, C and D# held on the keyboard (A and E), in AZERTY, at octave 8.
        {"kbd", {}, [](FakeHost&, pluginui::MainWindow& w) { w.keyboardWindow().show(); }},
        {"kbd_held", {},
         [](FakeHost&, pluginui::MainWindow& w) {
             w.keyboardWindow().show();
             for (char c : {'A', 'E'}) {
                 gemui::KeyEvent e;
                 e.key = gemui::Key::Character;
                 e.ch = char32_t(c - 'A' + 'a');
                 e.code = c;
                 w.desktop().keyDown(e);
             }
         }},
        {"kbd_azerty", [](FakeHost& h) { h.prefs["kbdLayout"] = "azerty"; },
         [](FakeHost&, pluginui::MainWindow& w) { w.keyboardWindow().show(); }},
        {"kbd_high", {},
         [](FakeHost&, pluginui::MainWindow& w) {
             w.setBaseOctave(8);
             w.keyboardWindow().show();
         }},
        // Drum Kit (M6): on a track that is not a Chipdrum (note, inert content), on a Chipdrum (Kick, Classic
        // kit), then the TR-808-ish bank with the Clap tail at 55 (Custom kit, settings on two columns). On a
        // Chipdrum, the Mode button of the ADSR is greyed out (D-205).
        {"kit_off", {}, [](FakeHost&, pluginui::MainWindow& w) { w.drumKit().show(); }},
        {"kit", {},
         [](FakeHost&, pluginui::MainWindow& w) {
             w.pickGenerator(4);
             w.drumKit().show();
         },
         {"adsrMode"}, "D-205"},
        {"kit_clap",
         [](FakeHost& h) {
             for (const auto& b : ymengine::kKitBanks)
                 if (b.name == "TR-808-ish") h.state.kit = b.kit;
             h.state.kit.inst[size_t(ymengine::DrumSlot::Clap)].dec = 55;  // tail
             h.editor.kitInst = int(ymengine::DrumSlot::Clap);
         },
         [](FakeHost&, pluginui::MainWindow& w) {
             w.pickGenerator(4);
             w.drumKit().show();
         },
         {"adsrMode"}, "D-205"},
        // Patches (M6): empty library (note), then eight patches on three lines.
        {"lib", {}, [](FakeHost&, pluginui::MainWindow& w) { w.patches().show(); }},
        {"lib_list",
         [](FakeHost& h) {
             std::vector<plugin::PatchEntry> lib;
             for (const char* n : {"Bass", "Lead 1", "Snare hit", "Pad", "Arp chords", "Kick", "Bell", "Hi hat long name"})
                 lib.push_back(plugin::snapPatch(n, h.values, h.state, 0));
             h.patches = plugin::dumpPatchLibrary(lib);
         },
         [](FakeHost&, pluginui::MainWindow& w) { w.patches().show(); }},
        // MIDI learn (M6): banner of the learn mode, then after a CC learnt for the Noise period slider.
        {"learn", {}, [](FakeHost&, pluginui::MainWindow& w) { w.setMidiLearn(true); }},
        // Noise in the source, so that the noise period slider can be learnt (greyed out without noise, D-253); the
        // Source button then reads Tone+Noise.
        {"learn_bound", [](FakeHost& h) { set(h, 0, Track::Noise, 1); },
         [](FakeHost& h, pluginui::MainWindow& w) {
             w.setMidiLearn(true);
             click(w, "np");
             h.learned.push_back({0xB0, 21, 0});
             w.tick(0);
         },
         {"srcSel"}, "D-253"},
        // YM Player (M7): YM3 dump of 250 frames loaded, stopped.
        {"ym", {},
         [](FakeHost&, pluginui::MainWindow& w) {
             std::string ym3 = "YM3!";
             ym3.resize(4 + 14 * 250, '\0');
             w.loadYmBytes(ym3, "tune.ym");
         }},
        // SHORTCUTS window (M8, D-189): opened as by ^? (openHelp), scroll bar at the top; in the visible part, the
        // text of the M key changes and ^D moves from File to Generator (D-301); the rows added by STYM2149 are
        // further down.
        {"help", {}, [](FakeHost&, pluginui::MainWindow& w) { w.help().show(); }},
    };
    return list;
}

// Instruments of the project of the STYM2149 scenes: two columns partly filled, a slot without a name ($05), a
// 16-character name ($11).
void someInstruments(FakeHost& h) {
    h.editExtras([&](plugin::NonParamState& s) {
        s.instruments[0] = plugin::captureInstrument("Lead SID", h.values, s, 0);
        s.instruments[1] = plugin::captureInstrument("Bass", h.values, s, 0);
        s.instruments[4] = plugin::captureInstrument("", h.values, s, 0);
        s.instruments[16] = plugin::captureInstrument("Hi hat long name", h.values, s, 0);
    });
}

// Demo pattern, track 1: INS row on a few notes, one of which ($03) points to an empty slot.
void demoWithIns(FakeHost& h, pluginui::MainWindow& w) {
    w.doDemo();
    h.editExtras([](plugin::NonParamState& s) {
        ymseq::Lane& l = s.seq.patterns[size_t(s.seq.current)].edit(0);
        l.ins[0] = 1;
        l.ins[4] = 2;
        l.ins[8] = 5;
        l.ins[12] = 3;
        l.ins[14] = 17;
    });
    w.tick(0);
    w.sequencer().show();
}

// Demo pattern with instruments, plus the CMD row (D-150), the rows of their own of the eleventh DAW test (ENV, VIB,
// DLY, CUT: items 132 and 138 to 140) and of the twelfth (LFO, MOD, TUN, the restart of ENV, the BPM row of the
// pattern under VOL, an SMP crossed on a chip track: items 142, 143, 145, 146, 150, 152, 153).
void demoWithCmd(FakeHost& h, pluginui::MainWindow& w) {
    using ymengine::SeqCmd;
    using ymengine::seqCmd;
    demoWithIns(h, w);
    h.editExtras([](plugin::NonParamState& s) {
        s.tracks[0].patch.lfo.on = true;
        s.tracks[0].patch.ornamentOn = true;
        ymseq::Pattern& p = s.seq.patterns[size_t(s.seq.current)];
        p.bpm[12] = 0x8c;
        p.bpm[6] = 0x78;
        ymseq::Lane& l = p.edit(0);
        l.setCmd(0, seqCmd(SeqCmd::VolSlide, 0x37));
        l.setCmd(4, seqCmd(SeqCmd::Speed, 0x03));
        l.setCmd(8, seqCmd(SeqCmd::Retrig, 0x02));
        l.setCmd(12, seqCmd(SeqCmd::Sync, 0x1a));
        l.setCmd(12, seqCmd(SeqCmd::VolSlide, 0x02));  // several commands: MUL (D-330)
        l.setCmd(12, seqCmd(SeqCmd::Fine, 0x5a));
        l.setCmd(12, seqCmd(SeqCmd::Vibrato, 0));
        l.setCmd(14, seqCmd(SeqCmd::Offset, 0x0c));
        l.setCmd(10, seqCmd(SeqCmd::Vibrato, 1));
        l.setCmd(4, seqCmd(SeqCmd::Lfo, 0));
        l.setCmd(12, seqCmd(SeqCmd::Lfo, 1));
        l.setCmd(8, seqCmd(SeqCmd::Mod, 0));
        l.mix[8] = uint8_t(ymengine::kSeqMixEnv + 1);  // Tone+Env: the ENV row
        l.setCmd(8, seqCmd(SeqCmd::Shape, 0x0a));
        l.setCmd(2, seqCmd(SeqCmd::Delay, 0x02));
        l.setCmd(5, seqCmd(SeqCmd::Cut, 0x04));
    });
    w.tick(0);
}

// Box of the commands of step 13 (tenth DAW test, items 121 and 122): sync mark and volume slide.
void cmdBox(FakeHost& h, pluginui::MainWindow& w) {
    demoWithCmd(h, w);
    const gemui::Rect r = w.sequencer().grid().laneRect(12, pluginui::SeqLane::Cmd);
    const gemui::Point p{r.x + r.w / 2, r.y + r.h / 2};
    w.desktop().mouseMove(p, {});
    w.desktop().mouseDown(p, gemui::MouseButton::Left, {});
    w.desktop().mouseUp(p, {});
    w.desktop().mouseMove({2, pluginui::MainWindow::kHeight - 2}, {});
}

// Instrument sequences of track 1 (D-148): looped table of 16 steps, vibrato in PER, noise, voice modes,
// generators, fixed notes, pitch settings and chained instrument.
void someSequences(FakeHost& h) {
    someInstruments(h);
    h.editExtras([](plugin::NonParamState& s) {
        ymengine::TrackPatch& p = s.tracks[0].patch;
        p.ornamentOn = true;
        p.ornamentLength = 16;
        p.rows.period = pluginui::SequencesPanel::vibratoTable(-64, 64, 2, 4, 16);
        p.rows.noise = {0, 31, 20, 12, 6, 3, 1};
        p.rows.mix = {4, 3, 1, 1, 0, 0, 0, 0, 7, 8};
        p.rows.gen = {0, 0, 0, 0, 2, 2, 3, 3, 1};
        p.rows.fix = {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
        p.fixedNote = 36;
        p.detuneCents = -7;
        p.analogCents = 12;
        p.hardSync = true;
        p.link = 2;
        p.pitchMatrix = uint16_t(ymengine::kPitchMatrixDefault | ymengine::pitchBit(ymengine::PitchSource::Vibrato,
                                                                                      ymengine::PitchTarget::Buzzer));
    });
}

// Track 1 in SID Voice: SID wave of 12 steps (D-151), in the GENERATOR frame (D-251).
void someSidWave(FakeHost& h) {
    set(h, 0, Track::Generator, float(ymengine::Generator::Sid));
    h.editExtras([](plugin::NonParamState& s) {
        ymengine::TrackPatch& p = s.tracks[0].patch;
        p.sidWave = {15, 13, 11, 9, 7, 5, 3, 1, 0, 4, 8, 12, 15, 2};
        p.sidWaveLen = 12;
    });
}

// Track 1 as a tuned Sync Buzzer (fifth 3/2, fine tuning -7 cents, YM Pad): D-152, settings in the GENERATOR frame
// since the Buzzer window left (D-238); five steps, one of them at the track's shape (D-151, D-252).
void someBuzzer(FakeHost& h) {
    set(h, 0, Track::Generator, float(ymengine::Generator::SyncBuzzer));
    set(h, 0, Track::EnvShape, 8);
    h.editExtras([](plugin::NonParamState& s) {
        ymengine::TrackPatch& p = s.tracks[0].patch;
        p.envTuned = true;
        p.envRatioN = 3;
        p.envRatioD = 2;
        p.envFineCents = -7;
        p.ymPad = true;
        p.buzzShapes = {8, 10, 0, 14, 9};
        p.buzzLen = 5;
    });
}

// STE DMA bank (D-153): STE machine, 3 channels; slot 3 (stereo, looped, A3), slots 1 and 2 mono. Shapes computed
// in integers (decaying triangle and square). DMA track 2 (track 6) plays slot 3.
void someDma(FakeHost& h) {
    set(h, Global::Computer, 1);
    set(h, Global::VoiceMode, 1);
    set(h, 5, Track::Level, 60);
    set(h, 5, Track::Pan, -20);
    h.editExtras([](plugin::NonParamState& s) {
        s.dma.slots[2] = {2, true, 57, true};
        s.dma.slots[0].rate = 1;
        s.tracks[5].patch.dmaSlot = 2;
        auto tri = [](int i, int period) { const int k = i % period; return k < period / 2 ? 4 * k - period : 3 * period - 4 * k; };
        std::vector<int8_t>& a = s.dmaSamples[2];
        const int frames = 12000;
        for (int i = 0; i < frames; ++i) {
            const int env = 127 * (frames - i) / frames;
            a.push_back(int8_t(tri(i, 1500) * env / 1500));
            a.push_back(int8_t(((i / 1000) % 2 ? env : -env) * 3 / 4));
        }
        for (int i = 0; i < 6000; ++i) s.dmaSamples[0].push_back(int8_t(tri(i, 200) * 120 / 200 * (6000 - i) / 6000));
        s.dmaSamples[1].assign(900, int8_t(40));
    });
}

// Digidrum editor: slot 4 selected (11,025 Hz, C3, -12 cents, YM levels, sustain loop), other slots filled (one
// of them from before M6, without a source or a name).
void someDigi(FakeHost& h) {
    h.session().digiSlot = 3;
    h.editExtras([](plugin::NonParamState& s) {
        auto tri = [](int i, int period) {
            const int k = i % period;
            return k < period / 2 ? 4 * k - period : 3 * period - 4 * k;
        };
        plugin::DigidrumSlotState& d = s.digiSlots[3];
        d.name = "Bass hit";
        d.play.rateHz = 11025;
        d.play.root = 48;
        d.play.cents = -12;
        d.play.loop = true;
        d.convert = plugin::DigiConvert::YmLevels;
        const int n = 9000;
        for (int i = 0; i < n; ++i) d.source.push_back(int8_t(tri(i, 600) * 127 / 600 * (n - i / 2) / n));
        d.play.loopStart = 3000;
        d.play.loopEnd = 6600;
        s.digidrums[3] = plugin::digidrumNibbles(d.source, d.convert);
        s.digiSlots[0].name = "Kick";
        std::vector<int8_t> k;
        for (int i = 0; i < 2400; ++i) {
            const int period = 120 + i / 20;  // the pitch falls
            k.push_back(int8_t(tri(i, period) * 120 / period * (2400 - i) / 2400));
        }
        s.digiSlots[0].source = k;
        s.digidrums[0] = plugin::digidrumNibbles(k, plugin::DigiConvert::Linear);
        s.digidrums[1].assign(1200, 8);
    });
}

void publishDma(FakeHost& h, pluginui::MainWindow& w) {
    plugin::EngineStatus t;
    t.serial = 1;
    t.peak[0] = 0.5f;
    t.peak[5] = 0.35f;
    t.outPeak = {0.42f, 0.3f};
    for (size_t i = 0; i < t.trackScope[5].size(); ++i) t.trackScope[5][i] = float(int(i % 16) - 8) / 24.0f;
    h.pending = t;
    w.tick(0);
}

// Windows and rows specific to STYM2149 (no capture of the prototype).
const std::vector<Scene>& stymScenes() {
    static const std::vector<Scene> list = {
        // The GEM desktop (ninth DAW test, items 101 and 102, D-302, D-303): the menu bar across its top, the icons
        // of the floppy disks side by side at its top left and the trash at its bottom right (tenth DAW test, item
        // 124, D-332), the trash being dragged.
        {"desk", {},
         [](FakeHost&, pluginui::MainWindow& w) {
             const gemui::Rect t = w.deskIcons()[2].bounds();
             w.desktop().mouseDown({t.x + 36, t.y + 16}, gemui::MouseButton::Left, {});
             w.desktop().mouseDrag({t.x + 26, t.y - 64}, {});
         }},
        // The menus of the menu bar, compared with the prototype's captures up to the ninth DAW test: the bar moved
        // to the top of the desktop (D-302). The File menu with the submenu of Load open; STYM2149's entries: New
        // (D-225), Recorder (D-157), Instruments and Samples (D-147, D-154), Sound engine (D-080), one Audio/MIDI
        // settings entry (D-217), the tooltip language in the Tooltips submenu (D-265, scene menu_tips).
        {"menu_file", {},
         [](FakeHost&, pluginui::MainWindow& w) {
             w.openMenu(1);
             const gemui::Rect n = *w.rectOf("menuNew");
             w.desktop().mouseMove({n.x + 20, n.bottom() + 8}, {});
         }},
        {"menu_edit", {}, [](FakeHost&, pluginui::MainWindow& w) { w.openMenu(2); }},
        {"menu_options", {}, [](FakeHost&, pluginui::MainWindow& w) { w.openMenu(3); }},
        {"menu_fuji", {}, [](FakeHost&, pluginui::MainWindow& w) { w.openMenu(0); }},
        {"ins_window", someInstruments, [](FakeHost&, pluginui::MainWindow& w) { w.instruments().show(); }},
        // Source menu: the prototype's three entries and the four with the envelope (fifth DAW test, item 48, D-246),
        // compared with the prototype's capture up to then.
        {"menu_source", {}, [](FakeHost&, pluginui::MainWindow& w) { click(w, "srcSel"); }},
        {"seq_ins", someInstruments, demoWithIns},
        // Menu of the INS cell of step 13: Channel, the instruments, the empty slot it names (checked), Instruments...
        {"seq_ins_menu", someInstruments,
         [](FakeHost& h, pluginui::MainWindow& w) {
             demoWithIns(h, w);
             const gemui::Rect r = w.sequencer().grid().laneRect(12, pluginui::SeqLane::Ins);
             const gemui::Point p{r.x + r.w / 2, r.y + r.h / 2};
             w.desktop().mouseMove(p, {});
             w.desktop().mouseDown(p, gemui::MouseButton::Left, {});
             w.desktop().mouseUp(p, {});
             w.desktop().mouseMove({2, pluginui::MainWindow::kHeight - 2}, {});
         }},
        {"seq_cmd", someInstruments, demoWithCmd},
        {"seq_cmd_box", someInstruments, cmdBox},
        // In a plugin there is no BPM row: the DAW sets the tempo (item 133, D-342).
        {"seq_cmd_plugin",
         [](FakeHost& h) {
             someInstruments(h);
             h.alone = false;
         },
         demoWithCmd},
        // Menu of the ENV cell of step 9: No change, the shapes 8 to F with their pictures, A checked (D-347), Restart
        // (D-355).
        {"seq_env_menu", someInstruments,
         [](FakeHost& h, pluginui::MainWindow& w) {
             demoWithCmd(h, w);
             const gemui::Rect r = w.sequencer().grid().laneRect(8, pluginui::SeqLane::Env);
             const gemui::Point p{r.x + r.w / 2, r.y + r.h / 2};
             w.desktop().mouseMove(p, {});
             w.desktop().mouseDown(p, gemui::MouseButton::Left, {});
             w.desktop().mouseUp(p, {});
             w.desktop().mouseMove({2, pluginui::MainWindow::kHeight - 2}, {});
         }},
        // Arpeggiator on the MIDI clock: compared with the prototype's capture up to the sixth DAW test; the divisions
        // in a menu button (seventh DAW test, item 89, D-290) make the window narrower than the capture's.
        {"arp_midi",
         [](FakeHost& h) {
             set(h, 0, Track::ArpOn, 1);
             set(h, Global::ArpClock, 1);
             h.pending = plugin::EngineStatus{};  // one publication: MIDI clock stopped (STOP), like the capture
         },
         [](FakeHost&, pluginui::MainWindow& w) { w.arpeggiator().show(); }},
        // Modulation (M6): stopped, then running with a drawn table (pitch, NSE, VOL), length 12, Hold,
        // 2 frames per step, keyboard cursor on step 4. Compared with the prototype's #ornWin up to the fifth DAW
        // test; the sequences below the table (D-240) make it a window of STYM2149.
        {"orn", {}, [](FakeHost&, pluginui::MainWindow& w) { w.modulation().show(); }},
        {"orn_on",
         [](FakeHost& h) {
             ymengine::TrackPatch& p = h.state.tracks[0].patch;
             p.ornamentOn = true;
             const int8_t pitch[] = {0, 12, 7, 3, 0, -5, -12, 4, 0, 0, 2, -1};
             const uint8_t vol[] = {15, 13, 11, 9, 7, 5, 3, 1, 0, 8, 12, 15};
             for (size_t i = 0; i < std::size(pitch); ++i) {
                 p.ornamentPitch[i] = pitch[i];
                 p.ornamentVolume[i] = vol[i];
             }
             for (size_t i : {2, 5, 9}) p.ornamentNoise[i] = 1;
             p.ornamentLength = 12;
             p.ornamentLoop = false;
             p.ornamentRate = 2;
             h.editor.ornCur = 3;
             h.editor.ornRow = 9;
         },
         [](FakeHost&, pluginui::MainWindow& w) { w.modulation().show(); }},
        // Sequences in the Modulation table, between its pitch and NSE rows (D-148, D-240, D-270).
        {"seqx_window", someSequences, [](FakeHost&, pluginui::MainWindow& w) { w.modulation().show(); }},
        // Menu of the MIX cell of step 9: the track's mixing table and the eight voice modes (6 Noise+Env checked).
        {"seqx_menu", someSequences,
         [](FakeHost&, pluginui::MainWindow& w) {
             w.modulation().show();
             const gemui::Rect r = w.modulation().sequences().grid().cellRect(pluginui::SeqRowsGrid::Row::Mix, 8);
             const gemui::Point p{r.x + r.w / 2, r.y + r.h / 2};
             w.desktop().mouseMove(p, {});
             w.desktop().mouseDown(p, gemui::MouseButton::Left, {});
             w.desktop().mouseUp(p, {});
             w.desktop().mouseMove({2, pluginui::MainWindow::kHeight - 2}, {});
         }},
        // Slider of the PER cell of step 3, opened by a right click; its value can be typed (sixth DAW test, items 74
        // to 76, D-272 to D-274).
        {"seqx_slider", someSequences,
         [](FakeHost&, pluginui::MainWindow& w) {
             w.modulation().show();
             const gemui::Rect r = w.modulation().sequences().grid().cellRect(pluginui::SeqRowsGrid::Row::Period, 2);
             const gemui::Point p{r.x + r.w / 2, r.y + r.h / 2};
             w.desktop().mouseMove(p, {});
             w.desktop().mouseDown(p, gemui::MouseButton::Right, {});
             w.desktop().mouseUp(p, {});
             w.desktop().mouseMove({2, pluginui::MainWindow::kHeight - 2}, {});
         }},
        // LFO of the track below the settings of the sequences (sixth DAW test, item 59, D-257): on, Sine on the pitch,
        // synchronised to the tempo by 1/8 triplets, 350 cents.
        {"lfo_on",
         [](FakeHost& h) {
             ymengine::TrackLfo& l = h.state.tracks[0].patch.lfo;
             l.on = true;
             l.shape = ymengine::LfoShape::Sine;
             l.target = ymengine::LfoTarget::Pitch;
             l.sync = true;
             l.division = 6;
             l.depth = 350;
         },
         [](FakeHost&, pluginui::MainWindow& w) { w.modulation().show(); }},
        // The same, the menu of the shapes open: their drawings (seventh DAW test, item 90, D-291).
        {"lfo_shapes",
         [](FakeHost& h) {
             ymengine::TrackLfo& l = h.state.tracks[0].patch.lfo;
             l.on = true;
             l.shape = ymengine::LfoShape::Random;
         },
         [](FakeHost&, pluginui::MainWindow& w) {
             w.modulation().show();
             click(w, "lfoShape");
         }},
        {"sid_wave", someSidWave, {}},
        {"sync_buzzer", someBuzzer, {}},
        // Sync Buzzer generator: compared with the prototype's capture up to the eighth DAW test (the settings of the
        // former Buzzer window ignored, D-227, D-252); with the arrows of the horizontal sliders (item 96, D-297), its
        // ignored areas would exceed the 2 % of the masks.
        {"sync", {}, [](FakeHost&, pluginui::MainWindow& w) { w.pickGenerator(3); }},
        {"dma_window", someDma, [](FakeHost&, pluginui::MainWindow& w) { w.dmaWindow().showSlot(2); }},
        {"digi_window", someDigi, [](FakeHost&, pluginui::MainWindow& w) { w.digidrums().show(); }},
        // Mixer in 3 channels with the STE machine: the four YM strips, then the eight DMA tracks (panning).
        {"dma_mixer", someDma,
         [](FakeHost& h, pluginui::MainWindow& w) {
             w.mixer().show();
             publishDma(h, w);
         }},
        // Mixer: compared with the prototype's captures up to the eighth DAW test; the values under the faders and the
        // pans (item 99, D-300) and the Send of the delay (item 98, D-299) make it taller. 3 channels: channels A, B, C and Digidrum, faders on their 4-bit
        // volumes, no panning, mono output. Oscilloscope of track 3, opened from its Mixer strip.
        {"mixer", {}, [](FakeHost&, pluginui::MainWindow& w) { w.mixer().show(); }},
        {"mixer_3ch", [](FakeHost& h) { set(h, Global::VoiceMode, 1); },
         [](FakeHost&, pluginui::MainWindow& w) { w.mixer().show(); }},
        {"mixer_scope", {},
         [](FakeHost&, pluginui::MainWindow& w) {
             w.mixer().show();
             click(w, "mixScope2");
         }},
        // Effects window (eighth DAW test, item 98, D-299; named by the ninth, D-311): echo of track 1 on, repeated
        // by track 3, four repeats losing two steps each; delay on, synchronised to the tempo. 3 channels: the echo,
        // the delay's row greyed out.
        {"echo_delay",
         [](FakeHost& h) {
             ymengine::TrackEcho& e = h.state.tracks[0].patch.echo;
             e.on = true;
             e.target = 2;
             e.repeats = 4;
             e.drop = 2;
             set(h, Global::DelayOn, 1);
         },
         [](FakeHost&, pluginui::MainWindow& w) { w.echoWindow().show(); }},
        {"echo_3ch",
         [](FakeHost& h) {
             h.state.tracks[0].patch.echo.on = true;
             set(h, Global::VoiceMode, 1);
             set(h, Global::DelayOn, 1);
         },
         [](FakeHost&, pluginui::MainWindow& w) { w.echoWindow().show(); }},
        // Arpeggiator in Table mode (grid, keyboard cursor), compared with the prototype's capture up to the grid of
        // +0 to +24 (D-322), whose 9 added rows make the window taller than the capture's.
        {"arp_table",
         [](FakeHost& h) {
             set(h, 0, Track::ArpOn, 1);
             h.state.tracks[0].arp.mode = ymengine::ArpMode::Table;
         },
         [](FakeHost&, pluginui::MainWindow& w) { w.arpeggiator().show(); }},
        // Arpeggio table of a chord that reaches two octaves (D-322): the offsets +0 to +24, the keyboard cursor on +0.
        {"arp_chord",
         [](FakeHost& h) {
             set(h, 0, Track::ArpOn, 1);
             ymengine::TrackArp& a = h.state.tracks[0].arp;
             a.mode = ymengine::ArpMode::Table;
             a.table = {0, 4, 7, 10, 14, 21, 24};
             a.length = 7;
         },
         [](FakeHost&, pluginui::MainWindow& w) { w.arpeggiator().show(); }},
        // Effects of a Sync Buzzer in 3 channels (ninth DAW test, item 115, D-316): the echo greyed out, its setting
        // kept; the delay off, without its Ping-pong (D-315).
        {"echo_buzzer3",
         [](FakeHost& h) {
             h.state.tracks[0].patch.echo.on = true;
             set(h, Global::VoiceMode, 1);
             set(h, 0, Track::Generator, float(ymengine::Generator::SyncBuzzer));
         },
         [](FakeHost&, pluginui::MainWindow& w) { w.echoWindow().show(); }},
        // Mixer in 2x3 channels with the ST (D-284): eight YM strips, a Master per chip (1 and 2) and Link, no balance.
        {"mixer_dual", [](FakeHost& h) { set(h, Global::VoiceMode, 2); },
         [](FakeHost&, pluginui::MainWindow& w) { w.mixer().show(); }},
        // DMA track 2 selected: GENERATOR frame of the DMA type, tabs DMA1 to DMA8, Computer row on STE.
        {"dma_track",
         [](FakeHost& h) {
             someDma(h);
             set(h, Global::FocusTrack, 6);
         },
         {}},
        // Digi track of Authentic 3ch on slot 4 (D-213): Digidrum samples under slots 5 to 8, ST channel row, drawing.
        {"digi_track",
         [](FakeHost& h) {
             someDigi(h);
             set(h, Global::VoiceMode, 1);
             set(h, Global::FocusTrack, 4);
             h.editExtras([](plugin::NonParamState& s) { s.tracks[3].patch.digidrumSlot = 3; });
         },
         {}},
        // Recorder (D-157) in dual chip: take in progress on chip 2, kept take of chip 1.
        {"rec_window",
         [](FakeHost& h) {
             set(h, Global::VoiceMode, 2);
             h.rec.state = pluginui::Host::Recorder::State::Rolling;
             h.rec.chip = 1;
             h.rec.frames = 3117;
             h.rec.takeFrames = 9000;
             h.rec.takeHz = 50;
         },
         [](FakeHost&, pluginui::MainWindow& w) { w.recorderWindow().show(); }},
        // Extended Song (D-158): rest, note-off, backward jump, restart point; menu of the jump open.
        {"song_special", {},
         [](FakeHost& h, pluginui::MainWindow& w) {
             w.doDemo();
             h.editExtras([](plugin::NonParamState& s) {
                 using ymseq::SongEntry;
                 using ymseq::SongKind;
                 s.seq.song.entries = {{0, 2, 0}, SongEntry::rest(SongKind::Rest, 8), {9, 1, 140},
                                       SongEntry::rest(SongKind::NoteOff, 4), SongEntry::loop(3, 2), {0, 1, 0}};
                 s.seq.song.entries[2].restart = true;
             });
             w.tick(0);
             w.songWindow().show();
             const gemui::Rect r = *w.rectOf("songChip4");
             const gemui::Point p{r.x + r.w / 2, r.y + r.h / 2};
             w.desktop().mouseMove(p, {});
             w.desktop().mouseDown(p, gemui::MouseButton::Right, {});
             w.desktop().mouseUp(p, {});
             w.desktop().mouseMove({2, pluginui::MainWindow::kHeight - 2}, {});
         },
         {}, "", 2},
        // Translated tooltips (D-185): accented Atari ST glyphs (EmuTOS), French non-breaking space before ":".
        {"tip_fr", [](FakeHost& h) { h.prefs["tipLanguage"] = "fr"; },
         [](FakeHost&, pluginui::MainWindow& w) { hover(w, "srcSel"); }},
        {"tip_de", [](FakeHost& h) { h.prefs["tipLanguage"] = "de"; },
         [](FakeHost&, pluginui::MainWindow& w) { hover(w, "modeSel"); }},
        // 16 voices, STE machine: the DMA type in the generator menu.
        // Options > Tooltips: On, Off, then the Language submenu after a separator (D-265), French chosen: English,
        // built in, the installed languages by name, a separator and Languages folder... (D-283).
        {"menu_tips", [](FakeHost& h) { h.prefs["tipLanguage"] = "fr"; },
         [](FakeHost&, pluginui::MainWindow& w) {
             w.openMenu(3);
             auto hover = [&w](const char* label) {
                 const auto* menu = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
                 for (int i = 0; menu && i < int(menu->items().size()); ++i)
                     if (menu->items()[size_t(i)].label == label) {
                         const gemui::Rect r = menu->itemRect(i);
                         w.desktop().mouseMove({r.x + 20, r.y + r.h / 2}, {});
                         return;
                     }
             };
             hover("Tooltips");
             hover("Language");
         }},
        {"dma_menu", [](FakeHost& h) { set(h, Global::Computer, 1); },
         [](FakeHost&, pluginui::MainWindow& w) { click(w, "fxSel"); }},
        // WAV export dialog (D-164, D-171): Format and Rate rows, 24-bit kept in the preferences.
        {"wav_dialog", [](FakeHost& h) { h.prefs["wavFormat"] = "24"; },
         [](FakeHost&, pluginui::MainWindow& w) { menuPick(w, 1, "Export pattern", "WAV"); }},
        // Computer Keyboard on a Chipdrum track (D-229): pads 1 to 8 from A to K, Kick (A) and Hat (F) held.
        {"kbd_drums", [](FakeHost& h) { set(h, 0, Track::Generator, float(ymengine::Generator::Chipdrum)); },
         [](FakeHost&, pluginui::MainWindow& w) {
             w.keyboardWindow().show();
             for (char c : {'A', 'F'}) {
                 gemui::KeyEvent e;
                 e.key = gemui::Key::Character;
                 e.ch = char32_t(c - 'A' + 'a');
                 e.code = c;
                 w.desktop().keyDown(e);
             }
         }},
        // Information window: credits reviewed in M8 (D-186), EmuTOS é in "Carré".
        {"info", {}, [](FakeHost&, pluginui::MainWindow& w) { menuPick(w, 0, "Information"); }},
    };
    return list;
}

std::vector<std::string> namesOf(const std::vector<Scene>& list) {
    std::vector<std::string> n;
    for (const Scene& s : list) n.push_back(s.name);
    return n;
}

// Rectangle a without rectangle b: up to four rectangles (above, below, left, right of b).
std::vector<gemui::Rect> minus(const gemui::Rect& a, const gemui::Rect& b) {
    if (a.empty()) return {};
    const gemui::Rect i = a.intersect(b);
    if (i.empty()) return {a};
    std::vector<gemui::Rect> out;
    auto add = [&](gemui::Rect r) {
        if (!r.empty()) out.push_back(r);
    };
    add({a.x, a.y, a.w, i.y - a.y});
    add({a.x, i.bottom(), a.w, a.bottom() - i.bottom()});
    add({a.x, i.y, i.x - a.x, i.h});
    add({i.right(), i.y, a.right() - i.right(), i.h});
    return out;
}

}  // namespace

const std::vector<std::string>& sceneNames() {
    static const std::vector<std::string> names = namesOf(scenes());
    return names;
}

const std::vector<std::string>& stymSceneNames() {
    static const std::vector<std::string> names = namesOf(stymScenes());
    return names;
}

std::vector<pluginui::Host::TextFile> installedLanguages() {
    std::vector<std::filesystem::path> paths;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(STYM_LANGUAGES_DIR, ec))
        if (e.path().extension() == ".po") paths.push_back(e.path());
    std::sort(paths.begin(), paths.end());
    std::vector<pluginui::Host::TextFile> files;
    for (const std::filesystem::path& p : paths) {
        std::ifstream in(p, std::ios::binary);
        files.push_back({p.filename().string(), std::string(std::istreambuf_iterator<char>(in), {}), true});
    }
    return files;
}

Shot renderScene(const std::string& name) {
    const Scene* it = nullptr;
    for (const auto* list : {&scenes(), &stymScenes()})
        for (const Scene& s : *list)
            if (s.name == name) it = &s;
    if (!it) return {};
    const bool proto = std::any_of(scenes().begin(), scenes().end(), [&](const Scene& sc) { return sc.name == name; });
    // Scenes of the prototype: its TrueType's glyphs, which start at the origin, where gemui keeps EmuTOS's place in
    // the cell (D-266); the STYM2149 scenes check gemui's.
    struct PrototypeGlyphs {
        explicit PrototypeGlyphs(bool on) { gemui::Font::useSizes(on ? gemui::fontdata::prototype::kSizes : nullptr); }
        ~PrototypeGlyphs() { gemui::Font::useSizes(nullptr); }
        PrototypeGlyphs(const PrototypeGlyphs&) = delete;
        PrototypeGlyphs& operator=(const PrototypeGlyphs&) = delete;
    } glyphs(proto);
    FakeHost host;
    // Ghost notes off, as in the prototype's captures (ghostOn = 0) and the reference renderings; STYM2149 starts
    // with them on (D-209).
    host.editor.ghost = false;
    if (it->before) it->before(host);
    pluginui::MainWindow win(host);
    win.desktop().mouseMove({2, pluginui::MainWindow::kHeight - 2}, {});
    if (it->act) it->act(host, win);
    win.tick(0);
    win.desktop().paint();
    const gemui::Surface& s = win.desktop().surface();
    Shot shot;
    shot.image.width = s.width();
    shot.image.height = s.height();
    shot.image.pixels = s.pixels();
    for (const char* id : it->masked)
        if (const auto r = win.rectOf(id)) shot.masks.push_back({r->x, r->y, r->w, r->h, it->decision});
    // Step Sequencer: the notes scrolled by a bar at the edge of the window (D-294), no longer by the prototype's
    // slider beside the grid; neither place compared.
    if (proto)
        for (const char* id : {"seqTr", "seqTrProto"})
            if (const auto r = win.rectOf(id)) shot.masks.push_back({r->x, r->y, r->w, r->h, "D-294"});
    // Scenes of the prototype: STYM2149's Computer row inserted in the SYSTEM frame (D-155), the following rows
    // compared with their place in the capture.
    const auto row = win.rectOf("computerRow"), below = win.rectOf("systemBelow");
    if (proto && row && below)
        shot.shifts.push_back({below->x, below->y, below->w, below->h, row->h, "D-155"});
    // Menu bar at the top of the desktop (ninth DAW test, item 101, D-302): the band from the top of the desktop down
    // to the main window's title bar is not compared (the STYM2149 scene desk checks it); the window's content keeps
    // the prototype's places. The desktop icons (item 102, D-303), absent from the prototype: their visible parts not
    // compared either.
    if (proto) {
        constexpr int kBand = pluginui::MainWindow::kWinY + 6 + gemui::TitleBar::kHeight;
        shot.masks.push_back({0, 0, s.width(), kBand, "D-302"});
        std::vector<gemui::Rect> front;  // what covers the icons: the windows, then the menus and the alerts
        bool above = false;
        for (const gemui::Widget* c : win.children()) {
            above = above || c == &win.deskIcons().back();
            if (above && c->visible() && dynamic_cast<const gemui::WindowFrame*>(c)) front.push_back(c->bounds());
        }
        for (gemui::Widget* c : {win.desktop().topModal(), win.desktop().firstPopup(), win.desktop().topPopup()})
            if (c) front.push_back(c->bounds());
        for (const gemui::DeskIcon& icon : win.deskIcons()) {
            std::vector<gemui::Rect> parts = {icon.bounds()};
            for (const gemui::Rect& f : front) {
                std::vector<gemui::Rect> left;
                for (const gemui::Rect& r : parts)
                    for (const gemui::Rect& m : minus(r, f)) left.push_back(m);
                parts = std::move(left);
            }
            for (const gemui::Rect& r : parts) shot.masks.push_back({r.x, r.y, r.w, r.h, "D-303"});
        }
    }
    // Shape R13 menu without the shapes 0 to 7 (D-221): its eight rows compared with the last eight of the capture,
    // eight rows lower; the place of those, under the menu, ignored.
    const auto shapes = win.rectOf("shapeMenu"), gone = win.rectOf("shapeMenuGone");
    if (proto && shapes && gone) {
        shot.shifts.push_back({shapes->x, shapes->y, shapes->w, shapes->h, -gone->h, "D-221"});
        shot.masks.push_back({gone->x, gone->y, gone->w, gone->h, "D-221"});
    }
    // Rows of the main window changed by the ninth DAW test, not compared (the STYM2149 scenes check them to the
    // pixel): Vibrato's Depth and Rate (item 103, D-304), Source's Volume and Level (item 104, D-305), the info cell of
    // the register monitor (item 105, D-306), the ADSR rows (item 106, D-307), and the row of the window buttons with
    // Effects (item 110, D-311), across the width of the oscilloscope: it replaces the comparison of the centred row
    // with the prototype's (D-247). The same for the rows that the twelfth DAW test aligned on the sliders of Volume
    // and Level: Source, Voicing and Mode (item 159, D-368), the Noise period slider (item 160, D-369). What a window,
    // a menu or an alert covers there stays compared.
    if (proto) {
        std::vector<gemui::Rect> front;
        bool above = false;
        for (const gemui::Widget* c : win.children()) {
            if (above && c->visible() && dynamic_cast<const gemui::WindowFrame*>(c)) front.push_back(c->bounds());
            above = above || c == &win.mainFrame();
        }
        for (gemui::Widget* c : {win.desktop().topModal(), win.desktop().firstPopup(), win.desktop().topPopup()})
            if (c) front.push_back(c->bounds());
        auto uncovered = [&](const gemui::Rect& r, const char* decision) {
            std::vector<gemui::Rect> parts = {r};
            for (const gemui::Rect& f : front) {
                std::vector<gemui::Rect> left;
                for (const gemui::Rect& p : parts)
                    for (const gemui::Rect& m : minus(p, f)) left.push_back(m);
                parts = std::move(left);
            }
            for (const gemui::Rect& p : parts) shot.masks.push_back({p.x, p.y, p.w, p.h, decision});
        };
        for (const auto& [id, decision] : {std::pair{"vibRows", "D-304"}, std::pair{"volRow", "D-305"},
                                           std::pair{"levelRow", "D-305"}, std::pair{"regInfo", "D-306"},
                                           std::pair{"adsrRows", "D-307"}, std::pair{"srcRow", "D-368"},
                                           std::pair{"polyRow", "D-368"}, std::pair{"vmRow", "D-368"},
                                           std::pair{"npRow", "D-369"}})
            if (const auto r = win.rectOf(id)) uncovered(*r, decision);
        const auto btns = win.rectOf("winButtons"), scope = win.rectOf("scope");
        if (btns && scope) uncovered({scope->x, btns->y, scope->w, btns->h}, "D-311");
    }
    // Main window without the prototype's help line (D-267): its bottom margin and border kHelpLine px higher, the
    // desktop where the line was.
    const auto bottom = win.rectOf("mainBottom"), helpLine = win.rectOf("mainGone");
    if (proto && bottom && helpLine) {
        shot.shifts.push_back({bottom->x, bottom->y, bottom->w, bottom->h, -helpLine->h, "D-267"});
        Dim desk{helpLine->x, helpLine->y, helpLine->w, helpLine->h, 0, 0, "D-267"};
        desk.desktopRow = helpLine->bottom();  // the capture's window ends there
        shot.dims.push_back(desk);
    }
    // Frames moved by the sixth DAW test (D-268), compared with their place in the capture: VIBRATO under SOURCE
    // (its new On/Off row not compared, the STYM2149 scenes check it), ADSR and ADSR ENVELOPE under ENVELOPE, whose
    // Shape R13 row moved up by the 16 px space it lost (D-255); SOURCE and GENERATOR end at the new bottoms of their
    // columns.
    const auto src = win.rectOf("srcFrame"), env = win.rectOf("envFrame"), vib = win.rectOf("vibFrame"),
               adsr = win.rectOf("adsrFrame"), curve = win.rectOf("curveFrame"), gen = win.rectOf("genFrame"),
               shape = win.rectOf("shapeSel"), vibOn = win.rectOf("vibOn");
    // Prototype: VIBRATO 266-352 and ADSR 362-522 in the second column, ADSR ENVELOPE 442-522 under GENERATOR
    // (122-432), SOURCE 122-522.
    // ADSR ENVELOPE got 22 px higher: its top and its bottom (the curve's last rows, the time labels, the frame's
    // bottom: kProtoCurveLow px) each compared with the capture's.
    constexpr int kProtoVib = 266, kProtoVibRows = 296, kProtoAdsr = 362, kProtoCurve = 442, kProtoGenEnd = 432,
                  kProtoEnd = 522, kProtoShapeDrop = 16, kProtoCurveLow = 52;
    int curveDx = 0, curveDy = 0, curveLow = 0, curveLowDy = 0, adsrDy = 0;
    if (proto && src && env && vib && adsr && curve && gen && shape && vibOn) {
        const int c1 = src->x, c2 = env->x, c3 = gen->x, w = env->w;
        auto move = [&](int x, int y, int h, int dx, int dy, int band = 0) {
            Shift m{x, y, w, h, dy, "D-268", band};
            m.dx = dx;
            shot.shifts.push_back(m);
        };
        move(c2, shape->y - 4, adsr->y - (shape->y - 4), 0, -kProtoShapeDrop);  // Shape R13 and the frame's bottom
        adsrDy = adsr->y - kProtoAdsr;
        move(c2, adsr->y, curve->y - adsr->y, 0, adsrDy);                         // ADSR and the gap under it
        curveDx = c2 - c3;
        curveDy = curve->y - kProtoCurve;
        curveLow = curve->bottom() - kProtoCurveLow;
        curveLowDy = curve->bottom() - kProtoEnd;
        move(c2, curve->y, curveLow - curve->y, curveDx, curveDy);                // ADSR ENVELOPE
        move(c2, curveLow, kProtoCurveLow, curveDx, curveLowDy);
        move(c1, src->bottom() - 6, 6, 0, src->bottom() - kProtoEnd);              // bottom of SOURCE
        move(c1, src->bottom(), vibOn->y - src->bottom(), c1 - c2, src->bottom() - (kProtoVib - 10));
        const int rows = vibOn->bottom() + 6;                                      // Depth and Rate, under On/Off
        move(c1, rows, vib->bottom() - rows, c1 - c2, rows - kProtoVibRows, rows - vibOn->y);
        // VIBRATO of a percussion track dimmed like ADSR (D-254), the prototype left it as it is.
        if (win.rectOf("vibInert")) {
            const auto veil = [&](int y, int h, int dy) {
                Dim d{c1, y, w, h, gemui::kOpacityInert, gemui::colors::kWhite, "D-254, D-268"};
                d.dx = c1 - c2;
                d.dy = dy;
                shot.dims.push_back(d);
            };
            veil(vib->y, vibOn->y - vib->y, vib->y - kProtoVib);
            veil(rows, vib->bottom() - rows, rows - kProtoVibRows);
        }
        // GENERATOR, 90 px longer: its sides as higher up, its inside the frame's background (white, or veiled under an
        // alert).
        const int extra = gen->bottom() - 4 - (kProtoGenEnd - 4);
        move(c3, kProtoGenEnd - 4, extra, 0, extra);
        const uint32_t inside = s.pixels()[size_t(gen->bottom() - 6) * size_t(s.width()) + size_t(c3 + 6)];
        shot.dims.push_back({c3 + 3, kProtoGenEnd - 4, w - 6, extra, 0, inside, "D-268"});
        // A menu or an alert covers the same place in both images: where it covers the former place of a moved zone
        // in the capture but not its new place, that part is compared with the capture of the same window without it
        // (Scene::alt), or not compared.
        std::vector<gemui::Rect> covers;
        for (gemui::Widget* c : {win.desktop().topModal(), win.desktop().firstPopup(), win.desktop().topPopup()})
            if (c) covers.push_back(c->bounds());
        const size_t moved = shot.shifts.size();
        for (size_t k = 0; k < moved; ++k) {
            const Shift m = shot.shifts[k];  // a copy: the loop adds to shot.shifts
            if (m.decision != "D-268") continue;
            for (const gemui::Rect& c : covers)
                for (const gemui::Rect& r : minus(c.translated(m.dx, m.dy).intersect({m.x, m.y, m.w, m.h}), c)) {
                    if (!*it->alt) {
                        shot.masks.push_back({r.x, r.y, r.w, r.h, "D-268"});
                        continue;
                    }
                    Shift seen{r.x, r.y, r.w, r.h, m.dy, "D-268", 0};
                    seen.dx = m.dx;
                    seen.alt = true;
                    shot.shifts.push_back(seen);
                }
        }
    }
    // Frames dimmed where the prototype left them as they are: ADSR and ADSR ENVELOPE of a drum track, ADSR ENVELOPE
    // of a Sync Buzzer in Chip mode (D-220); at their new places (D-268).
    if (proto)
        for (const char* id : {"adsrInert", "curveInert"})
            if (const auto r = win.rectOf(id)) {
                Dim d{r->x, r->y, r->w, r->h, gemui::kOpacityInert, gemui::colors::kWhite, "D-220"};
                d.dy = adsrDy;
                if (id[0] == 'c') {
                    d.dx = curveDx;
                    d.dy = curveDy;
                    if (curveLow > r->y) {
                        d.h = curveLow - r->y;
                        Dim low = d;
                        low.y = curveLow;
                        low.h = r->bottom() - curveLow;
                        low.dy = curveLowDy;
                        shot.dims.push_back(low);
                    }
                }
                shot.dims.push_back(d);
            }
    // Controls greyed out inside those frames: under the veil of the control and the frame's.
    if (proto)
        for (const gemui::Widget* g : win.greyedInert()) {
            const gemui::Rect r = g->bounds();
            for (size_t k = 0, n = shot.dims.size(); k < n; ++k)
                if (shot.dims[k].contains(r.x + r.w / 2, r.y + r.h / 2) && shot.dims[k].alpha > 0) {
                    Dim d = shot.dims[k];
                    d.x = r.x;
                    d.y = r.y;
                    d.w = r.w;
                    d.h = r.h;
                    d.alpha *= g->opacity();
                    shot.dims.push_back(d);
                    break;
                }
        }
    // SOURCE controls greyed out where the sound does not use them (D-253, D-261, D-262), black in the prototype: under
    // the veil of the frame's background (white, or veiled under an alert); a greyed label also turns grey, which on
    // the capture's black text amounts to a lighter veil.
    const auto srcFrame = win.rectOf("srcFrame");
    if (proto && srcFrame) {
        const uint32_t under =
            s.pixels()[size_t(srcFrame->bottom() - 6) * size_t(s.width()) + size_t(srcFrame->x + 6)];
        for (const gemui::Widget* g : win.greyedSource()) {
            const gemui::Rect r = g->bounds();
            double alpha = g->opacity();
            if (dynamic_cast<const gemui::Label*>(g))
                alpha *= 1 - double(gemui::colors::kGreyDisabled >> 16 & 0xFF) / 255;
            shot.dims.push_back({r.x, r.y, r.w, r.h, alpha, under, "D-253, D-261, D-262"});
        }
    }
    // Horizontal sliders: an arrow at each end (D-297); its 8 × 10 glyph not compared, the rest of the cell and the
    // thumb's shortened run are. Not for a glyph already ignored, or hidden under a window, a menu or an alert.
    if (proto) {
        const gemui::Desktop& desk = win.desktop();
        std::vector<gemui::Rect> front;  // in front of the window being walked
        if (desk.topModal()) front.push_back(desk.topModal()->bounds());
        const auto hidden = [&](const gemui::Rect& g) {
            for (const Mask& m : shot.masks)
                if (gemui::Rect{m.x, m.y, m.w, m.h}.intersect(g) == g) return true;
            return std::any_of(front.begin(), front.end(), [&](const gemui::Rect& r) { return r.intersect(g) == g; });
        };
        const std::function<void(const gemui::Widget&)> walk = [&](const gemui::Widget& w) {
            if (!w.visible()) return;
            if (const auto* sl = dynamic_cast<const gemui::Slider*>(&w); sl && sl->arrows())
                for (const bool right : {false, true}) {
                    const gemui::Rect r = sl->arrowRect(right);
                    const gemui::Rect g{r.x + (r.w - 8) / 2, r.y + (r.h - 10) / 2, 8, 10};
                    if (!hidden(g)) shot.masks.push_back({g.x, g.y, g.w, g.h, "D-297"});
                }
            for (const gemui::Widget* c : w.children()) walk(*c);
        };
        // From the front: the last menu opened, the first, then the root's windows; each hides those behind.
        std::vector<const gemui::Widget*> layers;
        if (desk.topPopup()) layers.push_back(desk.topPopup());
        if (desk.firstPopup() && desk.firstPopup() != desk.topPopup()) layers.push_back(desk.firstPopup());
        if (const gemui::Widget* root = desk.root())
            layers.insert(layers.end(), root->children().rbegin(), root->children().rend());
        for (const gemui::Widget* l : layers) {
            walk(*l);
            if (l->visible()) front.push_back(l->bounds());
        }
    }
    shot.budget = it->budget;
    shot.alt = it->alt;
    if (!shot.alt.empty() && win.desktop().topModal()) shot.altVeil = 0.25;  // Desktop: rgba(0, 0, 0, 0.25)
    return shot;
}

tools::Image altCapture(const Shot& shot, const tools::Image& capture) {
    tools::Image out = capture;
    if (shot.altVeil > 0)
        for (uint32_t& p : out.pixels) p = gemui::mix(p, gemui::colors::kBlack, 1 - shot.altVeil);
    return out;
}

namespace {
// Ignored pixel: zone of a scene, or band of an inserted row.
bool masked(const std::vector<Mask>& masks, const std::vector<Shift>& shifts, size_t i, int width) {
    const int x = int(i % size_t(width)), y = int(i / size_t(width));
    return std::any_of(masks.begin(), masks.end(), [&](const Mask& m) { return m.contains(x, y); }) ||
           std::any_of(shifts.begin(), shifts.end(), [&](const Shift& s) { return s.inBand(x, y); });
}
bool near(uint32_t p, uint32_t q, int tolerance) {
    for (int s = 0; s < 24; s += 8)
        if (std::abs(int(p >> s & 0xFF) - int(q >> s & 0xFF)) > tolerance) return false;
    return true;
}
// Pixel i of a matches b: at the same place, under the veil of a dimmed zone, or at the former place of a shifted zone.
bool same(const tools::Image& a, const tools::Image& b, size_t i, int tolerance, const std::vector<Shift>& shifts,
          const std::vector<Dim>& dims, const tools::Image* alt) {
    if (near(a.pixels[i], b.pixels[i], tolerance)) return true;
    const int x = int(i % size_t(a.width)), y = int(i / size_t(a.width));
    // A shift on the capture without the menu or the alert decides alone: the scene's capture shows them there.
    for (const Shift& s : shifts)
        if (s.alt && s.contains(x, y))
            return alt && alt->width == a.width && alt->height == a.height && y - s.dy >= 0 && y - s.dy < b.height &&
                   x - s.dx >= 0 && x - s.dx < b.width &&
                   near(a.pixels[i], alt->pixels[size_t(y - s.dy) * size_t(a.width) + size_t(x - s.dx)], tolerance);
    for (const Dim& m : dims) {
        if (!m.contains(x, y)) continue;
        if (m.desktopRow >= 0) {
            // Any row of the capture's desktop below, in the same column and phase (one may be under a toast).
            for (int ry = m.desktopRow + ((y - m.desktopRow) % 4 + 4) % 4; ry < b.height; ry += 4)
                if (near(a.pixels[i], b.pixels[size_t(ry) * size_t(a.width) + size_t(x)], tolerance)) return true;
        } else {
            const int bx = x - m.dx, by = y - m.dy;
            if (bx < 0 || by < 0 || bx >= b.width || by >= b.height) continue;
            const size_t j = size_t(by) * size_t(b.width) + size_t(bx);
            if (near(a.pixels[i], gemui::mix(b.pixels[j], m.under, m.alpha), tolerance)) return true;
        }
    }
    for (const Shift& s : shifts)
        if (s.contains(x, y) && y - s.dy >= 0 && y - s.dy < b.height && x - s.dx >= 0 && x - s.dx < b.width &&
            near(a.pixels[i], b.pixels[size_t(y - s.dy) * size_t(a.width) + size_t(x - s.dx)], tolerance))
            return true;
    return false;
}
}  // namespace

Diff compare(const tools::Image& a, const tools::Image& b, int tolerance, const std::vector<Mask>& masks,
             const std::vector<Shift>& shifts, const std::vector<Dim>& dims, const tools::Image* alt) {
    Diff d;
    d.total = long(a.width) * long(a.height);
    if (a.width != b.width || a.height != b.height) {
        d.differing = d.total;
        return d;
    }
    for (size_t i = 0; i < a.pixels.size(); ++i) {
        if (masked(masks, shifts, i, a.width)) {
            --d.total;
            continue;
        }
        if (!same(a, b, i, tolerance, shifts, dims, alt)) ++d.differing;
    }
    return d;
}

tools::Image diffImage(const tools::Image& a, const tools::Image& b, int tolerance, const std::vector<Mask>& masks,
                       const std::vector<Shift>& shifts, const std::vector<Dim>& dims, const tools::Image* alt) {
    tools::Image out;
    if (a.width != b.width || a.height != b.height) return out;
    out.width = a.width;
    out.height = a.height;
    out.pixels.resize(a.pixels.size());
    for (size_t i = 0; i < a.pixels.size(); ++i) {
        if (masked(masks, shifts, i, a.width)) {
            out.pixels[i] = 0xFFC8D8FFu;
            continue;
        }
        const uint32_t p = a.pixels[i];
        if (!same(a, b, i, tolerance, shifts, dims, alt)) {
            out.pixels[i] = 0xFFFF0000u;
        } else {
            const uint32_t g = (((p >> 16 & 0xFF) + (p >> 8 & 0xFF) + (p & 0xFF)) / 3 + 3 * 255) / 4;  // paled
            out.pixels[i] = 0xFF000000u | g << 16 | g << 8 | g;
        }
    }
    return out;
}

}  // namespace stym::gemshot
