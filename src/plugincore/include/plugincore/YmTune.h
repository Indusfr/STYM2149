// STYM2149 — plugincore: YM Player tune and its playback (M7, A2, D-159, D-169)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The YM Player of the prototype (ymLoad, #ymWin) reads a register dump and plays it at the rate of its file,
// instead of the engine output. Here:
// - YmTune: the tune read, YM dump (YM2 to YM6, LHA archives included, ymio::parseYm) or SNDH (68000 routine played
//   by the AtariAudio machine, ymio::parseSndh);
// - YmPlayback: its playback in a separate chip (ymcore::ChipRenderer at the clock of the file, output chain and
//   resampling of the project), built by the message thread and played by the audio thread without allocation.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "ymcore/ChipRenderer.h"
#include "ymio/Sndh.h"
#include "ymio/YmFile.h"
#include "ymio/YmPlayer.h"

namespace stym::plugin {

// A tune loaded in the YM Player: exactly one of `ym` and `sndh` is set. Never changed once loaded, it is shared by
// the message thread and the playbacks.
struct YmTune {
    std::string fileName;
    std::shared_ptr<const ymio::YmFile> ym;      // register dump
    std::shared_ptr<const ymio::SndhFile> sndh;  // 68000 routine

    // Name line (#ymName): "file  -  format - title (author)".
    std::string displayName() const;
    // Subsongs are numbered from 1; a YM dump has a single one.
    int subsongs() const noexcept;
    int defaultSubsong() const noexcept;
    // Frames per second: rate of the dump (VBL of the file), or calls of the SNDH play.
    int frameRate() const noexcept;
    // Frames of one pass of the subsong; 0: unknown (SNDH without a TIME or FRMS tag).
    uint32_t frames(int subsong) const noexcept;
    // Chip clock: the dump's (2 MHz for a YM2 or YM3), 2 MHz for an SNDH (AtariAudio machine).
    ymcore::MasterClock clock() const noexcept;
};

// Clocks accepted by the player (load safeguard: clk/8 internal steps per second to compute).
inline constexpr uint32_t kYmMinClockHz = 100000;
inline constexpr uint32_t kYmMaxClockHz = 8000000;

// Result of loadYmTune: the tune, or the alert that explains why it was refused.
struct YmTuneLoad {
    std::shared_ptr<const YmTune> tune;
    std::string error;  // alert in English, empty if tune is present
};

// Message thread: reads a file (YM dump, LHA archive of a dump, SNDH packed by Ice or not).
YmTuneLoad loadYmTune(std::span<const uint8_t> data, std::string fileName);

// The playback of one subsong of a tune, which owns its chip renderer. A new one is built for each change of tune,
// subsong or output settings; after a change of output settings only, it resumes where the old one was
// (continueFrom).
class YmPlayback {
public:
    // Message thread: `render` comes from the project settings (engineConfig); the clock is replaced by the tune's,
    // and the AtariAudio averaging by the sinc if the host rate exceeds clk/8 (D-058). `stream`: SNDH stream already
    // started, taken over by continueFrom(); otherwise a new one is started (init of the subsong).
    YmPlayback(std::shared_ptr<const YmTune> tune, int subsong, const ymcore::RenderConfig& render,
               std::shared_ptr<ymio::SndhStream> stream = nullptr);
    ~YmPlayback();
    YmPlayback(const YmPlayback&) = delete;
    YmPlayback& operator=(const YmPlayback&) = delete;

    // Audio thread, before the first render(): resumes the playback of `old` (same tune and subsong) where it is,
    // registers of the old chip included. Without allocation.
    void continueFrom(YmPlayback& old) noexcept;
    // Audio thread: n mono samples, in units of the mixing table (1 = level 32768). `offline`: render outside real
    // time, which waits for the capture of an SNDH.
    void render(float* out, uint32_t n, bool offline) noexcept;

    // Frames played since the start (ticks of the play for an SNDH).
    uint64_t framesPlayed() const noexcept;
    // Frame in the tune (0 = first): dump loop included; for an SNDH of known length, modulo that length.
    uint32_t position() const noexcept;
    uint8_t reg(unsigned r) const noexcept { return renderer_.chip().readRegister(r); }
    // Bass and treble of the STE chain, without allocation (audio thread).
    void setBoardTone(int bassDb, int trebleDb) noexcept { renderer_.setBoardTone(bassDb, trebleDb); }
    // SNDH routine crashed, and everything it wrote played.
    bool finished() const noexcept { return sndh_ && sndh_->stream().exhausted(); }

    const YmTune& tune() const noexcept { return *tune_; }
    int subsong() const noexcept { return subsong_; }
    const ymcore::RenderConfig& renderConfig() const noexcept { return renderer_.config(); }
    // Message thread only: SNDH stream (for a takeover on another render).
    std::shared_ptr<ymio::SndhStream> stream() const noexcept { return stream_; }

private:
    std::shared_ptr<const YmTune> tune_;
    int subsong_;
    ymcore::ChipRenderer renderer_;
    std::optional<ymio::YmPlayer> ym_;          // YM dump
    std::shared_ptr<ymio::SndhStream> stream_;  // SNDH: the running routine, which a later playback can take over
    std::optional<ymio::SndhPlayer> sndh_;
};

}  // namespace stym::plugin
