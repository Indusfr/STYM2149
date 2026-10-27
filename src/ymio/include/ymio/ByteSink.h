// STYM2149 — ymio: destination of a file written in pieces (WAV and stem exports)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// A long export is written as the render goes, without keeping the whole file in memory: bytes are appended, and a
// seek back completes a header whose sizes are only known at the end (RIFF, ZIP local headers). A failure is final:
// ok() becomes false and the following writes do nothing. commit() makes the file visible (a file destroyed without
// commit() is abandoned: export cancelled or failed).
#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace stym::ymio {

// Interface implemented by MemorySink below and, in the plugin, by a temporary file that replaces the target on
// commit() (Host::createFile). Used from one thread at a time (the export worker).
class ByteSink {
public:
    virtual ~ByteSink() = default;
    // Writes at the current position, which advances by as much.
    virtual bool write(std::span<const uint8_t> bytes) = 0;
    // Absolute position, at most the size already written.
    virtual bool seek(uint64_t position) = 0;
    virtual uint64_t position() const = 0;
    virtual uint64_t size() const = 0;
    // False once any write or seek has failed.
    virtual bool ok() const = 0;
    // Makes the written file the final one; false if the sink failed before or during the commit.
    virtual bool commit() = 0;
};

// In-memory file (tests, small exports).
class MemorySink final : public ByteSink {
public:
    bool write(std::span<const uint8_t> bytes) override;
    bool seek(uint64_t position) override;
    uint64_t position() const override { return pos_; }
    uint64_t size() const override { return bytes.size(); }
    bool ok() const override { return ok_; }
    bool commit() override {
        committed = ok_;
        return ok_;
    }

    std::vector<uint8_t> bytes;
    bool committed = false;

private:
    uint64_t pos_ = 0;
    bool ok_ = true;
};

}  // namespace stym::ymio
