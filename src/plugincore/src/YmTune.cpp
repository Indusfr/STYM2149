// STYM2149 — plugincore: YM Player tune and its playback (M7, A2, D-159, D-169)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/YmTune.h"

#include <algorithm>
#include <cstring>

#include "ymio/Lzh.h"

namespace stym::plugin {

namespace {

// Signatures (first four bytes) of the uncompressed YM dumps.
bool ymMagic(std::span<const uint8_t> d) noexcept {
    if (d.size() < 4) return false;
    for (const char* m : {"YM2!", "YM3!", "YM3b", "YM5!", "YM6!"})
        if (std::memcmp(d.data(), m, 4) == 0) return true;
    return false;
}

std::string titled(std::string s, const std::string& title, const std::string& author) {
    if (!title.empty()) s += " - " + title;
    if (!author.empty()) s += " (" + author + ")";
    return s;
}

}  // namespace

std::string YmTune::displayName() const {
    std::string s;
    if (ym) s = titled(ymio::formatName(ym->format), ym->title, ym->author);
    else if (sndh) s = titled("SNDH", sndh->info.title, sndh->info.composer);
    return fileName.empty() ? s : fileName + "  -  " + s;
}

int YmTune::subsongs() const noexcept { return sndh ? sndh->info.subsongs : 1; }

int YmTune::defaultSubsong() const noexcept { return sndh ? sndh->info.defaultSubsong : 1; }

int YmTune::frameRate() const noexcept {
    if (ym) return ym->playerRate;
    if (sndh) return sndh->info.tickRate;
    return 50;
}

uint32_t YmTune::frames(int subsong) const noexcept {
    if (ym) return ym->frameCount;
    if (sndh && subsong >= 1 && size_t(subsong) <= sndh->info.frames.size()) return sndh->info.frames[size_t(subsong - 1)];
    return 0;
}

ymcore::MasterClock YmTune::clock() const noexcept {
    if (ym) return ym->clock;
    return {ymio::SndhCapture::kClock, 1};
}

YmTuneLoad loadYmTune(std::span<const uint8_t> data, std::string fileName) {
    YmTuneLoad r;
    auto tune = std::make_shared<YmTune>();
    tune->fileName = std::move(fileName);
    if (ymio::looksLikeSndh(data)) {
        ymio::SndhParseResult s = ymio::parseSndh(data);
        if (!s.file) {
            r.error = s.error;
            return r;
        }
        tune->sndh = std::make_shared<const ymio::SndhFile>(std::move(*s.file));
    } else {
        ymio::YmParseResult y = ymio::parseYm(data);
        if (!y.file) {
            // Dump recognised (signature or LHA archive) but unreadable, or a file of another kind.
            r.error = ymMagic(data) || ymio::isLzh(data) ? "The YM file is damaged." : "Unsupported file";
            return r;
        }
        tune->ym = std::make_shared<const ymio::YmFile>(std::move(*y.file));
    }
    // The clock range is a load safeguard (see kYmMinClockHz).
    const double hz = tune->clock().hz();
    if (hz < kYmMinClockHz || hz > kYmMaxClockHz) {
        r.error = "Unsupported chip clock: " + std::to_string(int64_t(hz)) + " Hz.";
        return r;
    }
    r.tune = std::move(tune);
    return r;
}

// ------------------------------------------------------------------------------------------------- playback

YmPlayback::YmPlayback(std::shared_ptr<const YmTune> tune, int subsong, const ymcore::RenderConfig& render,
                       std::shared_ptr<ymio::SndhStream> stream)
    : tune_(std::move(tune)), subsong_(std::clamp(subsong, 1, tune_->subsongs())) {
    ymcore::RenderConfig rc = render;
    rc.chip.clock = tune_->clock();
    // AtariAudio averaging and Retro: at least one internal step per sample; beyond that, sinc (D-058).
    if (rc.mode != ymcore::OutputMode::Raw && double(rc.hostRate) > rc.chip.clock.hz() / 8.0)
        rc.mode = ymcore::OutputMode::Raw;
    renderer_.prepare(rc);
    if (tune_->ym) {
        ym_.emplace(*tune_->ym, ymio::YmPlayerOptions{}, rc.hostRate);
    } else {
        stream_ = stream ? std::move(stream) : std::make_shared<ymio::SndhStream>(tune_->sndh, subsong_);
        sndh_.emplace(stream_);
    }
}

YmPlayback::~YmPlayback() = default;

void YmPlayback::continueFrom(YmPlayback& old) noexcept {
    if (old.tune_ != tune_ || old.subsong_ != subsong_) return;
    // YM dump: the new player starts at the next frame the old one would have written, and the old chip registers
    // (pending writes applied) are copied. SNDH: the player moves to the new renderer with its stream.
    if (ym_ && old.ym_) {
        old.renderer_.flushDue();
        ym_->startAt(old.ym_->framesScheduled());
        for (unsigned r = 0; r < 14; ++r) renderer_.write(0, uint8_t(r), old.renderer_.chip().readRegister(r));
    } else if (sndh_ && old.sndh_ && old.stream_ == stream_) {
        ymio::SndhPlayer& moved = *old.sndh_;
        moved.moveTo(old.renderer_, renderer_);
        *sndh_ = std::move(moved);
    }
}

void YmPlayback::render(float* out, uint32_t n, bool offline) noexcept {
    if (ym_) ym_->render(renderer_, out, n);
    else if (sndh_) sndh_->render(renderer_, out, n, offline);
}

uint64_t YmPlayback::framesPlayed() const noexcept {
    if (ym_) return ym_->framesScheduled();
    return sndh_ ? sndh_->frames() : 0;
}

uint32_t YmPlayback::position() const noexcept {
    // The last frame started, folded into the tune.
    const uint64_t played = framesPlayed();
    const uint64_t f = played > 0 ? played - 1 : 0;
    if (ym_) return ym_->songFrame(f);
    const uint32_t length = tune_->frames(subsong_);
    return uint32_t(length ? f % length : f);
}

}  // namespace stym::plugin
