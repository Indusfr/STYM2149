// STYM2149 — ymio
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymio/YmFile.h"

#include <cstring>

#include "ymio/Lzh.h"

namespace stym::ymio {

namespace {
// Sequential big-endian reader; every read checks the remaining size and fails without moving.
class Reader {
public:
    explicit Reader(std::span<const uint8_t> d) : d_(d) {}
    bool has(size_t n) const { return pos_ <= d_.size() && d_.size() - pos_ >= n; }
    size_t pos() const { return pos_; }
    size_t remaining() const { return pos_ <= d_.size() ? d_.size() - pos_ : 0; }
    const uint8_t* ptr() const { return d_.data() + pos_; }
    bool skip(size_t n) {
        if (!has(n)) return false;
        pos_ += n;
        return true;
    }
    bool be16(uint32_t& v) {
        if (!has(2)) return false;
        v = (uint32_t(d_[pos_]) << 8) | d_[pos_ + 1];
        pos_ += 2;
        return true;
    }
    bool be32(uint32_t& v) {
        if (!has(4)) return false;
        v = (uint32_t(d_[pos_]) << 24) | (uint32_t(d_[pos_ + 1]) << 16) | (uint32_t(d_[pos_ + 2]) << 8) | d_[pos_ + 3];
        pos_ += 4;
        return true;
    }
    bool cstring(std::string& s) {
        size_t end = pos_;
        while (end < d_.size() && d_[end] != 0) ++end;
        if (end >= d_.size()) return false;
        s.assign(reinterpret_cast<const char*>(d_.data() + pos_), end - pos_);
        pos_ = end + 1;
        return true;
    }

private:
    std::span<const uint8_t> d_;
    size_t pos_ = 0;
};

YmParseResult fail(std::string message) { return {std::nullopt, std::move(message)}; }

// Deinterleaves (register by register → frame by frame) or copies the frames. Interleaved data stores register r of
// every frame together: byte frameCount × r + t.
void loadFrames(YmFile& f, const uint8_t* src, unsigned srcStride, bool interleaved) {
    f.frames.assign(size_t(f.frameCount) * YmFile::kStride, 0);
    for (uint32_t t = 0; t < f.frameCount; ++t)
        for (unsigned r = 0; r < srcStride; ++r)
            f.frames[size_t(t) * YmFile::kStride + r] =
                interleaved ? src[size_t(f.frameCount) * r + t] : src[size_t(t) * srcStride + r];
}
}  // namespace

const char* formatName(YmFormat format) noexcept {
    switch (format) {
    case YmFormat::YM2: return "YM2!";
    case YmFormat::YM3: return "YM3!";
    case YmFormat::YM3b: return "YM3b";
    case YmFormat::YM5: return "YM5!";
    case YmFormat::YM6: return "YM6!";
    }
    return "?";
}

namespace {
YmParseResult parseRaw(std::span<const uint8_t> data) {
    if (data.size() < 4) return fail("file too short");
    YmFile f;
    const char* id = reinterpret_cast<const char*>(data.data());
    if (std::memcmp(id, "YM2!", 4) == 0 || std::memcmp(id, "YM3!", 4) == 0 || std::memcmp(id, "YM3b", 4) == 0) {
        // No header: the mark, then 14 registers per frame, interleaved.
        f.format = id[2] == '2' ? YmFormat::YM2 : (id[3] == 'b' ? YmFormat::YM3b : YmFormat::YM3);
        size_t body = data.size() - 4;
        if (f.format == YmFormat::YM3b) {
            if (body < 4) return fail("YM3b without a loop frame number");
            // Loop frame on 4 bytes at the end of the file, read little-endian like AtariAudio on x86/ARM (native
            // read of a uint32_t). TO CONFIRM on a YM3b file whose loop is known.
            const uint8_t* p = data.data() + data.size() - 4;
            f.loopFrame = uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
        }
        // Like AtariAudio: (size - 4) / 14, the 4 loop bytes of the YM3b fall into the remainder.
        f.frameCount = uint32_t(body / 14);
        if (f.frameCount == 0) return fail("no frames");
        loadFrames(f, data.data() + 4, 14, true);
        f.attributes = 1;
    } else if ((std::memcmp(id, "YM5!", 4) == 0 || std::memcmp(id, "YM6!", 4) == 0)) {
        f.format = id[2] == '5' ? YmFormat::YM5 : YmFormat::YM6;
        Reader r(data);
        r.skip(4);
        if (!r.has(8) || std::memcmp(r.ptr(), "LeOnArD!", 8) != 0) return fail("missing LeOnArD! signature");
        r.skip(8);
        // Header, big-endian: frame count (32 bits), attributes (32), digidrum count (16), master clock in Hz (32),
        // player rate in Hz (16), loop frame (32), size of extra data to skip (16). Then each digidrum (32-bit
        // length and bytes), title, author and comment (zero-terminated), and the frames of 16 registers.
        uint32_t frames = 0, attrs = 0, drums = 0, clock = 0, rate = 0, loop = 0, extra = 0;
        if (!r.be32(frames) || !r.be32(attrs) || !r.be16(drums) || !r.be32(clock) || !r.be16(rate) || !r.be32(loop) ||
            !r.be16(extra) || !r.skip(extra))
            return fail("truncated YM5/YM6 header");
        if (frames == 0) return fail("no frames");
        if (rate == 0) return fail("zero player rate");
        if (clock == 0) return fail("zero clock");
        if (drums > 64) return fail("more than 64 digidrums");  // AtariAudio's limit (kYmMaxSamples)
        f.frameCount = frames;
        f.attributes = attrs;
        f.clock = {clock, 1};
        f.playerRate = uint16_t(rate);
        f.loopFrame = loop;
        for (uint32_t d = 0; d < drums; ++d) {
            uint32_t len = 0;
            if (!r.be32(len) || !r.has(len)) return fail("truncated digidrum");
            f.digidrums.emplace_back(r.ptr(), r.ptr() + len);
            r.skip(len);
        }
        if (!r.cstring(f.title) || !r.cstring(f.author) || !r.cstring(f.comment)) return fail("truncated header strings");
        if (r.remaining() / 16 < frames) return fail("truncated frame data");
        loadFrames(f, r.ptr(), 16, (attrs & 1) != 0);
    } else {
        return fail("unrecognised format (YM2!, YM3!, YM3b, YM5!, YM6! expected)");
    }
    if (f.loopFrame >= f.frameCount) f.loopFrame = 0;  // like AtariAudio
    return {std::move(f), {}};
}
}  // namespace

YmParseResult parseYm(std::span<const uint8_t> data) {
    if (!isLzh(data)) return parseRaw(data);
    const LzhResult unpacked = unpackLzh(data);
    if (!unpacked.error.empty()) return fail("LHA archive: " + unpacked.error);
    YmParseResult r = parseRaw(unpacked.data);
    if (!r.file) r.error = "after LHA decompression: " + r.error;
    return r;
}

}  // namespace stym::ymio
