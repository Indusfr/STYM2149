// STYM2149 — pluginui: editor state kept by the plugin while it lives (tools, view, undo, windows)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// In the prototype, these settings are variables of the page (seqTool, figIdx, chordIdx, scaleSnap, followOn,
// ghostOn, overOn, viewStart, viewAbsBase, curStep, curRow, undoStack, clipboards...): a reload resets them and the
// project does not save them. Here, the plugin window can be closed and reopened endlessly: the processor keeps this
// state (Host::session) so that reopening it finds the tool, the view, the open windows and the undo history again.
// It is not saved with the project, as in the prototype.
#pragma once
#include <array>
#include <bitset>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "gemui/Geometry.h"
#include "plugincore/Digidrum.h"
#include "plugincore/Parameters.h"
#include "plugincore/ProjectFiles.h"
#include "plugincore/State.h"
#include "ymengine/Dma.h"
#include "ymengine/DrumKit.h"
#include "ymengine/Params.h"
#include "ymseq/Edit.h"
#include "ymseq/Pattern.h"
#include "ymseq/Song.h"
#include "ymseq/Undo.h"

namespace stym::pluginui {

// Modulation table of a track (orn* fields of the prototype's patch), STYM2149 instrument sequences played with it
// and LFO, kept by an undo step of the track. Each field is a copy of the ymengine::TrackPatch
// field of the same name (ornament* for the first seven), whose meaning and range Params.h gives.
struct TrackOrnament {
    bool on = false;
    uint8_t length = 8;
    bool loop = true;
    uint8_t rate = 1;
    std::array<int8_t, ymengine::kOrnamentSteps> pitch{};
    std::array<uint8_t, ymengine::kOrnamentSteps> noise{};
    std::array<uint8_t, ymengine::kOrnamentSteps> volume{};
    ymengine::SeqRows rows;
    uint8_t fixedNote = 60;
    double detuneCents = 0, analogCents = 0;
    bool hardSync = false;
    uint8_t link = 0;
    uint16_t pitchMatrix = ymengine::kPitchMatrixDefault;
    std::array<uint8_t, ymengine::kSidWaveSteps> sidWave{};  // timer waves
    uint8_t sidWaveLen = ymengine::kSidSquareSteps;
    ymengine::SidForm sidForm = ymengine::SidForm::Base;
    uint8_t sidWavePhase = ymengine::kSidWavePhase;
    uint8_t sidWavePeriod = ymengine::kSidWavePeriod;
    uint8_t sidWidth = ymengine::kSidWidth;
    std::array<uint8_t, ymengine::kBuzzSteps> buzzShapes{};
    uint8_t buzzLen = 0;
    bool envTuned = false;  // buzzer
    uint8_t envRatioN = 1, envRatioD = 1;
    double envFineCents = 0;
    bool ymPad = false;
    ymengine::TrackLfo lfo;
    ymengine::TrackEcho echo;

    bool operator==(const TrackOrnament&) const = default;
    static TrackOrnament of(const ymengine::TrackPatch& p) {
        return {p.ornamentOn,   p.ornamentLength, p.ornamentLoop,  p.ornamentRate, p.ornamentPitch,
                p.ornamentNoise, p.ornamentVolume, p.rows,          p.fixedNote,    p.detuneCents,
                p.analogCents,  p.hardSync,       p.link,          p.pitchMatrix,  p.sidWave,
                p.sidWaveLen,   p.sidForm,        p.sidWavePhase,  p.sidWavePeriod, p.sidWidth,
                p.buzzShapes,   p.buzzLen,        p.envTuned,      p.envRatioN,    p.envRatioD,
                p.envFineCents, p.ymPad,          p.lfo,           p.echo};
    }
    void applyTo(ymengine::TrackPatch& p) const {
        p.ornamentOn = on;
        p.ornamentLength = length;
        p.ornamentLoop = loop;
        p.ornamentRate = rate;
        p.ornamentPitch = pitch;
        p.ornamentNoise = noise;
        p.ornamentVolume = volume;
        p.rows = rows;
        p.fixedNote = fixedNote;
        p.detuneCents = detuneCents;
        p.analogCents = analogCents;
        p.hardSync = hardSync;
        p.link = link;
        p.pitchMatrix = pitchMatrix;
        p.sidWave = sidWave;
        p.sidWaveLen = sidWaveLen;
        p.sidForm = sidForm;
        p.sidWavePhase = sidWavePhase;
        p.sidWavePeriod = sidWavePeriod;
        p.sidWidth = sidWidth;
        p.buzzShapes = buzzShapes;
        p.buzzLen = buzzLen;
        p.envTuned = envTuned;
        p.envRatioN = envRatioN;
        p.envRatioD = envRatioD;
        p.envFineCents = envFineCents;
        p.ymPad = ymPad;
        p.lfo = lfo;
        p.echo = echo;
    }
};

// Undo step (undoEntry, songUndoEntry, chanUndoEntry, mixUndoEntry, kitUndoEntry of the prototype; Dma: DMA track of
// the STE; Digi: a Digidrum slot of the editor; Import: import of a MIDI file; Grid:
// a resolution change that fits the notes).
struct UndoEntry {
    enum class Kind : uint8_t { Pattern, Song, Track, Mix, Kit, Dma, Digi, Import, Grid };
    Kind kind = Kind::Pattern;
    int index = 0;                    // pattern (Pattern), track (Track), kept sample (Dma, -1: none), Digidrum
                                      // slot (Digi), number of replaced patterns (Import) or resolution (Grid)
    ymseq::Pattern pattern;           // Pattern
    ymseq::Song song;                 // Song, Import, Grid
    std::vector<float> params;        // Track: parameters of the track; Mix: level, pan, mute, solo and send of
                                      // each track; Import: On and Generator of the tracks,
                                      // FreeTempo, then (XM) Computer and VblRate
    ymengine::TrackArp arp;           // Track: arpeggio (Track, Land, mode, octaves, table, length)
    TrackOrnament orn;                // Track: Modulation table
    ymengine::KitParams kit;          // Kit: the eight instruments of the Chipdrum kit
    ymengine::DmaParams dma;          // Dma, Import XM: settings of the track and of its samples
    std::vector<int8_t> dmaBytes;     // Dma: bytes of sample `index`
    std::array<std::string, ymengine::kDmaSlots> dmaNames;  // Dma, Import XM: names of the samples
    plugin::DigidrumSlotState digi;   // Digi: settings of slot `index` (and its source if digiBytes)
    std::vector<uint8_t> digiNibbles; // Digi: 4-bit values played (if digiBytes)
    bool digiBytes = false;           // Digi: the step keeps the source and the values (otherwise settings only)
    std::vector<ymseq::Pattern> patterns;  // Import: patterns 0 to index-1; Grid: all of them
    std::bitset<ymseq::kPatterns> used;    // Import: created patterns
    std::array<int, ymseq::kTracks> lengths{};  // Grid: own lengths of the tracks (ymseq::TrackSeq::length)
    int current = 0;                       // Import: current pattern
    // Import XM: instruments and DMA slots 0 to slots-1, sequencer rate (timing, speed).
    bool xm = false;
    int slots = 0;
    std::vector<plugin::Instrument> instruments;
    std::vector<std::vector<int8_t>> dmaSamples;
    ymengine::SeqTiming timing = ymengine::SeqTiming::Bpm;  // Import XM, Grid
    uint8_t speed = 6;

    // Same kind, same index and same content in the fields that kind uses (patterns by content, sameContent). The
    // history skips a step equal to the current state (UndoHistory), and pushCur pushes none when nothing changed.
    bool operator==(const UndoEntry& o) const {
        if (kind != o.kind || index != o.index) return false;
        switch (kind) {
        case Kind::Pattern: return pattern.sameContent(o.pattern);
        case Kind::Song: return song == o.song;
        case Kind::Mix: return params == o.params;
        case Kind::Kit: return kit == o.kit;
        case Kind::Dma: return dma == o.dma && dmaBytes == o.dmaBytes && dmaNames == o.dmaNames;
        case Kind::Digi:
            return digiBytes == o.digiBytes && digi == o.digi && digiNibbles == o.digiNibbles;
        case Kind::Import:
            if (song != o.song || params != o.params || used != o.used || current != o.current ||
                patterns.size() != o.patterns.size() || xm != o.xm || slots != o.slots ||
                instruments != o.instruments || dma != o.dma || dmaSamples != o.dmaSamples || dmaNames != o.dmaNames ||
                timing != o.timing || speed != o.speed)
                return false;
            for (size_t i = 0; i < patterns.size(); ++i)
                if (!patterns[i].sameContent(o.patterns[i])) return false;
            return true;
        case Kind::Grid:
            if (song != o.song || lengths != o.lengths || timing != o.timing || patterns.size() != o.patterns.size())
                return false;
            for (size_t i = 0; i < patterns.size(); ++i)
                if (!patterns[i].sameContent(o.patterns[i])) return false;
            return true;
        case Kind::Track:
            return params == o.params && arp.track == o.arp.track && arp.land == o.arp.land &&
                   arp.mode == o.arp.mode && arp.octaves == o.arp.octaves && arp.table == o.arp.table &&
                   arp.length == o.arp.length && orn == o.orn;
        }
        return false;
    }
};

// Floating window: place left by the user and state.
struct WindowPlace {
    gemui::Point at;  // top-left corner of the window, in desktop px
    bool open = false;
    int order = 0;  // stacking rank (the highest in front)
};

// Kept by the processor (StymProcessor::editorSession) and reached through Host::session; message thread only.
struct EditorSession {
    // Sequencer: tool (TOOLS: 0 Move, 1 Draw, 2 Erase, 3 Split, 4 Join, 5 Select), follow playback, ghost notes (on
    // when the editor starts, where the prototype started with ghostOn = 0), overview, step input, recording,
    // Snap, input note value (FIGS) and chord (CHORDS).
    int tool = 1;
    bool follow = true, ghost = true, overview = false, stepRec = false, record = false, snap = false;
    int figure = 0, chord = -1;
    // View: first step shown, absolute note at the bottom of the grid (C3), edit cursor (step, row 0 at the top).
    int viewStart = 0, viewAbsBase = 48;
    int curStep = 0, curRow = 6;
    // Keyboard cursor of the arpeggio table (arpCur, arpRow: step, row 0 at the top = +24).
    int arpCur = 0, arpRow = 24;
    // Keyboard cursor of the Modulation table (ornCur, ornRow: step, row 0 at the top = +12).
    int ornCur = 0, ornRow = 12;
    // Instrument shown in the Drum Kit window (kitInst: slot 0 kick to 7 tom 2).
    int kitInst = 0;
    // Slot shown in the Digidrums editor.
    int digiSlot = 0;
    // Computer keyboard: the held notes follow an octave change (Follow) or keep their pitch (Keep, kbdFollow of the
    // prototype).
    bool kbdFollow = false;

    // Clipboards (noteClip, rectClip, blockClip, rowClip, patClip).
    std::optional<ymseq::NoteClip> noteClip;
    std::optional<std::vector<ymseq::RectNote>> rectClip;
    std::optional<ymseq::BlockClip> blockClip;
    std::optional<std::pair<ymseq::Row, ymseq::Lane>> rowClip;
    std::optional<ymseq::Pattern> patClip;
    std::optional<std::vector<ymseq::SongEntry>> songClip;

    ymseq::UndoHistory<UndoEntry> undo;

    // Open floating windows and their places, by name ("seq", "song", "mix"...).
    std::map<std::string, WindowPlace> windows;

    // File of each kind (plugin::FileKind) chosen by the last Save or Save as: the one that Save rewrites (fileNames
    // of the prototype, forgotten with the page); empty: Save opens the dialog.
    std::array<std::string, plugin::kFileKinds> files{};
    // Last file imported into the YM Player, last imported MIDI file and last imported XM module:
    // suggested by the next dialog.
    std::string ymFile, midiFile, xmFile;
};

}  // namespace stym::pluginui
