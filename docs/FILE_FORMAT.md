# STYM2149 file format

This document describes the binary format that STYM2149 writes for the host's saved state and for the files of
**File > Load / Save / Save as**: projects (`.stym`), patterns (`.stympat`), Songs (`.stymsng`) and Chipdrum kits
(`.stymkit`), and for the configurations of the standalone application (`.stymcfg`, File > Configuration). STYM2149 writes and reads no other project file: the JSON files of the prototype are gone;
only the Patches library stays in JSON.

The reference implementation is `src/plugincore/src/State.cpp` (`encodeState`, `decodeState`) and
`src/plugincore/src/ProjectFiles.cpp` (`encodeNativeFile`, `readNativeFile`).

## Conventions

- Every number is little-endian.
- `u8`, `u16`, `u32`, `u64`: unsigned integers of 8, 16, 32 and 64 bits. `i8`, `i16`: signed (two's complement).
- `f32`, `f64`: IEEE 754 single and double precision.
- `N × t`: N values of type `t` in a row.
- Text is ASCII, or UTF-8 where it says so, with no terminating zero; its length comes before it.
- `[...]` marks a field that may be missing at the end of its chunk: older versions did not write it, and a reader
  takes its default value when it is missing.

## Header

| Offset | Type      | Field                                                                    |
|-------:|-----------|--------------------------------------------------------------------------|
|      0 | 4 × u8    | Magic `"STYM"` (`53 54 59 4D`)                                           |
|      4 | u16       | Format version: 1                                                        |
|      6 | u16       | Flags. Bits 0 to 3: kind of the content (below). Bits 4 to 15: reserved, 0 |
|      8 | chunks    | Chunks until the end of the file                                         |

Kind of the content (bits 0 to 3 of the flags):

| Value | Kind                          | Extension  | Chunks written                                   |
|------:|-------------------------------|------------|--------------------------------------------------|
|     0 | Host state, or a project file | `.stym`    | all of them (below); a project file has no `RECD` or `RSYN` |
|     0 | Configuration (standalone)    | `.stymcfg` | the host state, `RECD` and `RSYN` included        |
|     1 | Pattern                       | `.stympat` | `PATN`, with the pattern alone as pattern 0       |
|     2 | Song                          | `.stymsng` | `SEQS`, `PATN` (the created patterns), `SONG`, `SNGX` |
|     3 | Chipdrum kit                  | `.stymkit` | `KIT `                                           |

The host's state and a project file are the same blob: a project file is the state without the register recorder
take. A configuration of the standalone application is the host's state itself, written as it is; its load
accepts a project file too. Versions of STYM2149 that came before the kinds wrote 0 in the flags, so their states read as kind 0.

## Chunks

Each chunk is:

| Type       | Field                                        |
|------------|----------------------------------------------|
| 4 × u8     | Identifier (fourcc), such as `PARM`          |
| u32        | Length of the content, in bytes              |
| length × u8| Content                                      |

Chunks are written in the order of the list below. Optional chunks are written only when their content differs from
the default, so a project that does not use a feature gives the same bytes as the version before that feature.

### PARM: parameter values

| Type      | Field                                                 |
|-----------|-------------------------------------------------------|
| u32       | Parameter count                                       |
| per parameter: | |
| u16       | Length of the identifier                              |
| N × u8    | Identifier, UTF-8 (such as `master`, `t01_level`)     |
| f32       | Plain value (not normalised)                          |

The identifiers and ranges are those of `src/plugincore/src/Parameters.cpp`. A reader ignores unknown identifiers,
gives missing parameters their default and clamps every value to its range.

### TRKX: track settings outside the parameters

| Type      | Field                                  |
|-----------|----------------------------------------|
| u8        | Track count (32; 16 in older files)    |
| per track: | |
| u32       | Length of the track's fields           |
| fields    | Below                                  |

Fields of a track, in this order:

| Type      | Field                                                                   |
|-----------|-------------------------------------------------------------------------|
| u8        | Pad the keyboard plays on a Digidrum track (0 to 7)                     |
| u8        | Modulation table (ornament) on: 0 or 1                                  |
| u8        | Ornament length                                                         |
| u8        | Ornament loop: 0 or 1                                                   |
| u8        | Ornament rate                                                           |
| 64 × i8   | Ornament pitch, per step                                                |
| 64 × u8   | Ornament noise, per step                                                |
| 64 × u8   | Ornament volume, per step                                               |
| u8        | Arpeggiator Track: 0 or 1                                               |
| u8        | Arpeggiator Land: 0 or 1                                                |
| u8        | Arpeggio mode: 0 Up, 1 Down, 2 Up/Down, 3 Order, 4 Random, 5 Table      |
| u8        | Arpeggio octaves                                                        |
| 16 × i8   | Arpeggio table, in semitones                                            |
| u8        | Arpeggio table length                                                   |
| [i8]      | DMA sample of the track; written only when it is not -1 ("by position") |
| [i16]     | DMA sample of the track (-1 to 254)                                     |
| [8 × u8]  | Digidrum each pad (drum row) plays, 0 to 254                            |

The last two fields are written only when the DMA sample is past 127 or a pad does not play the Digidrum of its
number; the `i8` is then always written, -1 for a sample past 127, which a reader from before them plays
as the sample of the track's position.

### GLBX: global settings outside the parameters

| Type      | Field                                                        |
|-----------|--------------------------------------------------------------|
| u8        | Channel taken by the Digidrum track of chip 1: 0 A, 1 B, 2 C |
| u8        | The same for chip 2                                          |
| u16       | Scale of the scale-locked slides: 12-bit mask, bit 0 = tonic |
| u8        | Tonic of the scale, 0 to 11                                  |
| 32 × f32  | Points of the Custom slide curve                             |

### KIT: Chipdrum kit

8 instruments × 5 `f64`, in the order Kick, Snare, Clap, Hat, Open hat, Closed hat, Tom 1, Tom 2. The five values of
an instrument depend on its family: kick and toms (pitch, sweep, decay, noise, volume), snare (body, tone, decay,
volume), clap (space, colour, tail, volume), hats (metal, tune, decay, volume). Units and ranges are those of the
Drum Kit window. The fourcc ends with a space: `KIT `.

### DIGI: Digidrum samples as played

| Type      | Field                                                               |
|-----------|---------------------------------------------------------------------|
| u8        | Slot count: 8, or up to the last sample loaded (255 at most) |
| per slot: | |
| u32       | Sample count                                                        |
| ⌈count / 2⌉ × u8 | 4-bit values, two per byte, the first in the low nibble; an odd count leaves the last high nibble at 0 |

### NAME: track names (optional)

Written only when a track is named.

| Type      | Field                                               |
|-----------|-----------------------------------------------------|
| u8        | Track count (32; 16 in older files)                 |
| per track: u8 length, then the name in printable ASCII (8 characters at most; empty: the track number) | |

### TINS: instrument of each track (optional)

Written only when a track has an instrument.

| Type      | Field                                                        |
|-----------|--------------------------------------------------------------|
| u8        | Track count (32; 16 in older files)                          |
| count × u8 | Instrument slot + 1 (1 to 127), 0 when the track has none    |

### SEQS: sequencer settings

| Type      | Field                                                                                 |
|-----------|---------------------------------------------------------------------------------------|
| u8        | Clock: 0 Free run, 1 MIDI, 2 Host sync                                                |
| u8        | Resolution: 0 1/4, 1 1/8, 2 1/16, 3 1/32, 4 1/4T, 5 1/8T, 6 1/16T, 7 1/64, 8 1/32T    |
| u8        | Flags: 1 Loop, 2 Song, 4 metronome, 8 Quantize to VBL, 16 Fade of the CUT             |
| u8        | Time signature numerator                                                              |
| u8        | Time signature denominator                                                            |
| u8        | Current pattern (0 to 31)                                                             |
| u8        | Displayed bank (0 to 3)                                                               |
| 4 × u8    | Last pattern of each bank                                                             |
| u32       | Created patterns: bit n set when pattern n exists                                     |
| u8        | Track count (32; 16 in older files)                                                   |
| per track: u8 octave of the grid rows (0 to 9), u16 own length in steps (0: the pattern's, 1 to 64) | |
| [u8]      | Metronome level, 0 to 100 % (100 when missing)                                        |
| [u8]      | Length of the fade of a CUT, 1 to 100 ms (5 when missing)                             |

In a Song file, only the Song flag and the created patterns are read from this chunk.

### PATN: patterns

| Type      | Field                                                    |
|-----------|----------------------------------------------------------|
| u8        | Count of the patterns that follow                        |
| per pattern: | |
| u8        | Pattern number (0 to 31)                                 |
| u32       | Length of the pattern's content                          |
| u16       | Steps (length of the pattern, up to 256)                 |
| u8        | Count of the tracks that follow (tracks with a cell set) |
| per track: u8 track number (0 to 31), then the track's rows (below) | |
| [u16]     | Count of the steps that carry a pattern tempo (BPM row)  |
| [per step: u8 step, u8 tempo in BPM] | |

The BPM row is written only when a step carries a tempo; older versions stop before it.

Rows of a track:

| Type      | Field                                                   |
|-----------|---------------------------------------------------------|
| u8        | Count of the rows that follow (rows with a cell set)    |
| per row: | |
| u8        | Row identifier (table below)                            |
| u16       | Count of the cells that follow                          |
| per cell: u8 step (0 to 255), i16 value | |

Only cells whose value differs from the row's empty value are written. A reader ignores unknown row identifiers and
clamps each value to the row's range.

| Id      | Row                                   | Range        | Empty |
|--------:|---------------------------------------|--------------|------:|
|       0 | PAT: note in the octave (or percussion row) | -1 to 12 | -1 |
|       1 | Tie (held note)                       | 0 to 1       |     0 |
|       2 | Slide                                 | 0 to 1       |     0 |
|       3 | ARP flags of the prototype; read only, turned into Jxx | 0 to 1 | 0 |
|       4 | VOL                                   | 0 to 15      |    15 |
|       5 | Source (mixer, Env entries included)  | 0 to 7       |     0 |
|       6 | Digidrum slot                         | 0 to 8       |     0 |
|       7 | CUT                                   | 0 to 1       |     0 |
|       8 | FX                                    | 0 to 4       |     0 |
|       9 | Octave code                           | -3 to 9      |     0 |
|      10 | Gate, %                               | 0 to 100     |     0 |
|      11 | Pan: 0 none, or v × 10 + 101 for v from -10 to +10 | 0 to 201 | 0 |
|      12 | Retrigger                             | 0 to 3       |     0 |
|      13 | Condition                             | 0 to 7       |     0 |
|      14 | Slide curve                           | 0 to 12      |     0 |
|      15 | Transpose, semitones                  | -12 to 12    |     0 |
| 16 to 30| Chord voices 2 to 6, three rows each: note (-1 to 12, empty -1), octave code (-3 to 9), duration in steps (0 to 256; 0 is one step) | | |
|      31 | INS: instrument slot + 1              | 0 to 32      |     0 |
| 32 to 52| Commands, one row per kind (below)    | kind × 256 + parameter | 0 |

Command rows, in order from 32: X (retired, dropped when read), S, R, Z, T, V, A, P (retired, dropped when read), D,
C, B, N, E, O, F, L, M, J, G, K, Q. The value of a command cell is its kind × 256 + its parameter (0 to 255), the
kind being 1 for X, 2 for S and so on in the same order. A command read in a row of another kind goes to the row of
its own kind; an unknown kind leaves the cell empty.

### SONG: Song

| Type      | Field                                         |
|-----------|-----------------------------------------------|
| u16       | Entry count                                   |
| per entry: u8 pattern (0 to 31), u16 repeats (1 to 65 535), u16 tempo in BPM (0: the tune's) | |

### SNGX: extended Song entries (optional)

Written only when an entry is not a plain pattern or marks the restart point.

| Type      | Field                                                                 |
|-----------|-----------------------------------------------------------------------|
| u16       | Entry count; must equal that of `SONG`, otherwise the chunk is ignored |
| per entry: | |
| u8        | Kind: 0 Pattern, 1 Rest, 2 Note-off, 3 Loop                           |
| u16       | Argument: length in steps (Rest, Note-off), or entries replayed (Loop) |
| u8        | Flags: 1 restart point                                                |

### INST: project instruments (optional)

Written only when an instrument exists.

| Type      | Field                                                                  |
|-----------|------------------------------------------------------------------------|
| u8        | Count of the instruments that follow                                   |
| per instrument: | |
| u8        | Slot (0 to 31)                                                         |
| u32       | Length of the text                                                     |
| N × u8    | The instrument as compact JSON, in the format of a Patches library entry (name included) |

### TSEQ: instrument sequences and sound settings of the tracks (optional)

Written for the tracks whose sequences, timer waves, buzzer, LFO or echo differ from the default.

| Type      | Field                         |
|-----------|-------------------------------|
| u8        | Count of the tracks that follow |
| per track: u8 track number, u32 length, then the fields below | |

| Type      | Field                                                                       |
|-----------|-----------------------------------------------------------------------------|
| 64 × i8   | PER row                                                                     |
| 64 × u8   | NP row                                                                      |
| 64 × u8   | MIX row                                                                     |
| 64 × u8   | GEN row                                                                     |
| 64 × u8   | FIX row                                                                     |
| u8        | Fixed note                                                                  |
| f64       | Detune, cents                                                               |
| f64       | Analog randomness, cents                                                    |
| u8        | Hard sync: 0 or 1                                                           |
| u8        | Chained instrument                                                          |
| u16       | Pitch matrix                                                                |
| u8        | Steps of the SID wave                                                       |
| 16 × u8   | Volumes of the SID wave bars                                                |
| u8        | Steps of the Sync Buzzer                                                    |
| 8 × u8    | R13 shapes of the Sync Buzzer steps                                         |
| u8        | Tuned envelope: 0 or 1                                                      |
| u8        | N of the tuned envelope ratio                                               |
| u8        | D of the tuned envelope ratio                                               |
| f64       | Fine tune of the envelope, cents                                            |
| u8        | YM Pad: 0 or 1                                                              |
| u8        | LFO on: 0 or 1                                                              |
| u8        | LFO shape: 0 Triangle, 1 Sine, 2 Square, 3 Saw up, 4 Saw down, 5 Random     |
| u8        | LFO target: 0 Volume, 1 Noise period, 2 Envelope period, 3 SID ratio, 4 Pitch, 5 Pan |
| u8        | LFO synced to the tempo: 0 or 1                                             |
| f64       | LFO rate, Hz                                                                |
| u8        | LFO division (when synced)                                                  |
| f64       | LFO depth                                                                   |
| u8        | LFO retrigger at each note: 0 or 1                                          |
| u8        | Echo of the notes on: 0 or 1                                                |
| u8        | Echo division                                                               |
| u8        | Echo repeats                                                                |
| u8        | Echo drop                                                                   |
| i8        | Echo target track (-1: its own)                                             |
| u8        | Sine of the SID wave lit: 0 or 1                                            |
| u8        | Phase of the sine, 0 to 15                                                  |
| u8        | Period of the sine, 2 to 16 bars                                            |
| u8        | Width of the square wave, 1 to 15 sixteenths                                |
| u8        | Form of the SID wave: 0 Base, 1 Square, 2 Sine, 3 Custom, 4 Bounce           |
| u8        | Steps of the FM wave, 1 to 16 |
| 16 × u8   | Multiples of the FM wave steps, 0 to 7: x0.5, x1, x1.5, x2, x3, x4, x5, x6   |
| u8        | Scale of the FM wave: 0 Ratios, 1 Semitones |
| 16 × u8   | Semitones of the FM wave steps, 0 to 127, played in Semitones               |

Fields missing at the end of the chunk keep their default; the reader rebuilds the form of the SID wave from the
older fields when it is missing, and a missing FM wave is the note then its octave (x1, x2); a missing scale is
Ratios, with the semitones 0 then 12.

### TIMG: Ticks timing (optional)

Written only when the settings are not the defaults.

| Type      | Field                                     |
|-----------|-------------------------------------------|
| u8        | Timing: 0 BPM, 1 Ticks                    |
| u8        | Speed: ticks per line                     |
| u8        | Groove length (1 to 16)                   |
| 16 × u8   | Ticks added to the successive lines       |

### DMAS: STE DMA bank (optional)

Written only when a slot is not empty or not at its defaults.

| Type      | Field                                                               |
|-----------|---------------------------------------------------------------------|
| u8        | Slot count: 8, or up to the last slot used (255 at most)     |
| per slot: | |
| u8        | Rate: 0 6 258 Hz, 1 12 517 Hz, 2 25 033 Hz, 3 50 066 Hz            |
| u8        | Stereo: 0 or 1 (bytes interleaved left, right)                      |
| u8        | Root note (MIDI)                                                    |
| u8        | Loop: 0 or 1                                                        |
| u32       | Byte count                                                          |
| N × i8    | Signed 8-bit samples                                                |
| [count × (u32, u32)] | Start and End of each slot, in frames (End 0: to the end) |
| [per slot: u8 length, name] | Names of the slots                                |
| [count × f64] | Fine tune of each slot, cents (-100 to 100)                     |

### DIGS: Digidrum editor (optional)

Written only when a slot is not at its defaults. The played 4-bit values stay in `DIGI`.

| Type      | Field                                                   |
|-----------|---------------------------------------------------------|
| u8        | Slot count: 8, or up to the last slot used (255 at most) |
| per slot: | |
| u8        | Length of the name, then the name                       |
| f64       | Rate, Hz                                                |
| u8        | Root note (MIDI)                                        |
| f64       | Fine tune, cents                                        |
| u8        | Loop: 0 or 1                                            |
| u32       | Start, in samples                                       |
| u32       | End, in samples                                         |
| u8        | Conversion to 4 bits: 0 Linear, 1 YM levels             |
| u32       | Byte count of the source                                |
| N × i8    | Signed 8-bit source                                     |

### STEM: memory of the Atari (optional)

Written only when it is not 4 MB. The samples share this memory in Authentic 3ch and Dual chip 2x3ch.

| Type      | Field                                                   |
|-----------|---------------------------------------------------------|
| u8        | Memory: 0 512 KB, 1 1 MB, 2 2 MB, 3 4 MB                |

### RECD: register recorder take (host state only)

Written only when a take exists, and never in a project file.

| Type      | Field                                                       |
|-----------|-------------------------------------------------------------|
| u8        | Chip recorded                                               |
| u32       | Replay tick, Hz                                             |
| u64       | Chip clock numerator                                        |
| u32       | Chip clock denominator                                      |
| u32       | Frame count                                                 |
| per frame: | |
| 14 × u8   | Registers R0 to R13                                         |
| u8        | Flags: 1 R13 written; 2, 4, 8 start of the timer effect of channel A, B, C |
| per channel A, B, C: u8 generator of the timer effect (1 SID Voice, 2 Digidrum, 3 Sync Buzzer, 6 FM Voice; 255 none), u8 value (FM Voice: its steps), u8 MFP prescaler, u16 MFP data | |

### RSYN: Zxx of the take (host state only, optional)

Written only when a frame of the take carries a Zxx command; kept apart so that `RECD` keeps its frame size.

| Type      | Field                                 |
|-----------|---------------------------------------|
| u32       | Entry count                           |
| per entry: u32 frame, u8 value of the Zxx | |

### RFMW: FM Voice steps of the take (host state only, optional)

Written only when a frame of the take plays an FM Voice; kept apart so that `RECD` keeps its frame size.

| Type      | Field                                 |
|-----------|---------------------------------------|
| u32       | Entry count                           |
| per entry: u32 frame, u8 channel (0 to 2), u64 multiples of the steps, 4 bits each from the first step up | |

### RFMS: FM Voice steps in semitones of the take (host state only, optional)

Written only when a frame of the take plays an FM Voice in Semitones; those frames are not in `RFMW`.

| Type      | Field                                 |
|-----------|---------------------------------------|
| u32       | Entry count                           |
| per entry: u32 frame, u8 channel (0 to 2), 16 × u8 semitones of the steps, 0 to 127 | |

## Reading rules

- A file that does not start with `STYM` is not a STYM2149 file.
- A version above 1 comes from a newer STYM2149: the file is refused and nothing changes.
- A kind above 3 is refused.
- Unknown chunks are skipped; unknown parameters and rows are ignored.
- Missing parameters, chunks and trailing fields take their default values.
- Every value is clamped to its range.
- **File > Load** checks the kind: a pattern file loaded with Load > Song, for example, is refused with an alert that
  says what the file holds.
- **File > Play...** reads the kind and plays the file apart from the project: a project as it is; a pattern or a
  Song over a copy of the current project.
