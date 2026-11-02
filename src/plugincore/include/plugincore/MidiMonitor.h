// STYM2149 — plugincore: a message received from the host, as the MIDI monitor lists it
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Filled on the audio thread, read by the editor: a message of up to three bytes as it came (status, data 1, data 2);
// a SysEx as F0, its first data bytes (after F0, F7 left out) and its length in bytes, F0 and F7 included.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace stym::plugin {

// Bytes 1 to kSysExShown: the first data bytes of a SysEx (0 past its end); the last two: its length, low byte first,
// up to 65535.
using MonitorMessage = std::array<uint8_t, 9>;
constexpr int kSysExShown = 6;

inline MonitorMessage monitorMessage(const uint8_t* data, int size) noexcept {
    MonitorMessage m{};
    if (size <= 0) return m;
    m[0] = data[0];
    if (data[0] != 0xF0) {
        for (int i = 1; i < std::min(size, 3); ++i) m[size_t(i)] = data[i];
        return m;
    }
    const int bytes = data[size - 1] == 0xF7 ? size - 2 : size - 1;  // data bytes
    for (int i = 0; i < std::min(bytes, kSysExShown); ++i) m[size_t(1 + i)] = data[1 + i];
    const int length = std::min(size, 0xFFFF);
    m[7] = uint8_t(length & 0xFF);
    m[8] = uint8_t(length >> 8);
    return m;
}

// Length of a SysEx in bytes, F0 and F7 included.
inline int sysExLength(const MonitorMessage& m) noexcept { return m[7] | m[8] << 8; }

}  // namespace stym::plugin
