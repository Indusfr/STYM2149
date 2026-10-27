// STYM2149 — ymseq: arrangement (Song), sequence of pattern × repeat entries
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Model of the prototype's Song window (SONG = [{pat, rep, bpm?}]) and its edits: add, insert, duplicate, repeats,
// cut/copy/paste, moving a block, own tempo. The selections are lists of indices (songSel), sorted and bounded as in
// the prototype. No undo step here: the caller takes it (pushSongUndo) at the same moment as the prototype.
//
// Extended Song (maxYMiser's X18, TTrak's T1): special entries without a pattern, a silence (Rest) or
// a release of all the sequencer's notes followed by a silence (Note-off), with a length in steps; a backward jump
// (Loop) that replays the entries before it; a restart point (restart) where the Song goes back to at its end when
// Loop is on.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace stym::ymseq {

inline constexpr int kSongBpmMin = 20, kSongBpmMax = 300;  // tempo of a block of its own (gemPrompt "Tempo...")
inline constexpr std::array<int, 5> kSongRepeats = {1, 2, 4, 8, 16};  // menu Repeat x1 … x16
inline constexpr int kSongRepeatMax = 65535;  // repeats of a block (the prototype had no limit)
inline constexpr int kSongRestSteps = 16;     // length of a Rest or Note-off entry when created
inline constexpr int kSongLoopMax = 255;      // entries replayed by a jump (Loop)

// Kind of an entry. Pattern: the prototype's.
enum class SongKind : uint8_t { Pattern, Rest, NoteOff, Loop };

// One block of the Song: the prototype's {pat, rep, bpm}, plus the kind, the argument and the restart mark.
struct SongEntry {
    int pat = 0;  // 0-31 (Pattern only)
    int rep = 1;  // at least 1; Loop: number of jumps before going on
    int bpm = 0;  // 0: tempo of the song
    SongKind kind = SongKind::Pattern;
    int arg = 0;  // Rest, Note-off: length in steps (1-256); Loop: entries replayed (1-255), those before it
    bool restart = false;  // restart point of the Song (the first one marked counts)
    bool operator==(const SongEntry&) const = default;

    static SongEntry rest(SongKind k, int steps = kSongRestSteps) { return {0, 1, 0, k, steps, false}; }
    static SongEntry loop(int back, int times = 1) { return {0, times, 0, SongKind::Loop, back, false}; }
    bool special() const noexcept { return kind != SongKind::Pattern; }
};

// Label of a block (songLabel) without the tempo: "A1x2"; Rest "Rest16x1", Note-off "Off16x1", Loop "Back3x2"
// (replays the 3 previous entries, 2 times); the restart point is preceded by "|:".
std::string songLabel(const SongEntry& e);

using SongSel = std::vector<int>;  // indices, sorted, without duplicates

// The entries in play order (Project::song), edited by the Song window on the message thread. The functions taking a
// selection normalize it first, so out-of-range or repeated indices are ignored.
class Song {
public:
    std::vector<SongEntry> entries;

    bool operator==(const Song&) const = default;
    int size() const noexcept { return int(entries.size()); }
    bool empty() const noexcept { return entries.empty(); }
    // Restart point: the first marked entry, otherwise 0 (the prototype restarted from the beginning).
    int restartIndex() const noexcept;

    // Valid indices, sorted, without duplicates (filter(x < SONG.length).sort).
    SongSel normalize(const SongSel& sel) const;

    // Add button: same pattern as the last block → one more repeat, otherwise (or Shift) a block.
    void add(int pat, bool separate);
    // Menu of the pattern button "Add to song": always a block.
    void append(int pat);
    // Insert / A: the current pattern after the cursor (at the start if the list is empty); returns its position.
    int insertAfter(int cursor, int pat);
    // Any entry (Rest, Note-off, Loop) after the cursor; returns its position.
    int insertEntryAfter(int cursor, const SongEntry& e);
    // Length in steps of the Rest and Note-off entries of the selection (limited to 1-256); the others do not change.
    void setLength(const SongSel& sel, int steps);
    // Entries replayed by the jumps (Loop) of the selection (limited to 1-255).
    void setLoopBack(const SongSel& sel, int back);
    // Restart point on entry i (only one); on the one that already has it, it goes back to the start (0).
    void toggleRestart(int i);
    // Delete: the selection, or the cursor's block without a selection; returns the clamped cursor.
    int remove(const SongSel& sel, int cursor);
    // D: duplicates the cursor's block just after it; returns the new cursor.
    int duplicateAt(int cursor);
    // Duplicate menu: the selection just after its last block; returns the new selection.
    SongSel duplicate(const SongSel& sel);
    void setRepeat(const SongSel& sel, int rep);
    void incRepeat(int i);
    void decRepeat(int i);
    // Tempo of the selection's own: limited to 20-300; without a value (empty input), the block follows the song.
    // The jumps (Loop) play nothing: their tempo does not change.
    void setTempo(const SongSel& sel, std::optional<int> bpm);
    // Copy of the selection, without the restart point (it stays in its place).
    std::vector<SongEntry> copy(const SongSel& sel) const;
    void erase(const SongSel& sel);
    // Pastes after block `after` (-1: at the start); returns the pasted selection.
    SongSel pasteAfter(int after, const std::vector<SongEntry>& clip);
    // Move left / Move right menu: false at a limit (nothing changes).
    bool moveLeft(SongSel& sel);
    bool moveRight(SongSel& sel);
    // Drag and drop: `from` is the grabbed block (the whole selection if it is part of it), `drop` the block hovered
    // on release. False if nothing changes; otherwise `sel` becomes the moved block.
    bool dragMove(SongSel& sel, int from, int drop);

private:
    std::vector<SongEntry> take(const SongSel& sel) const;  // copy as is (moves)
};

}  // namespace stym::ymseq
