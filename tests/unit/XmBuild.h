// STYM2149 — tests: writing an XM 1.04 module (public Fasttracker II format), to build the modules read by
// ymio::parseXm and the XM import. Packed cells (flag byte) except full cells, written as five bytes; delta-coded
// samples.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "ymio/Xm.h"

namespace stym::test {

inline void xmPut16(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}
inline void xmPut32(std::vector<uint8_t>& b, uint32_t v) {
    xmPut16(b, v & 0xFFFF);
    xmPut16(b, v >> 16);
}
inline void xmText(std::vector<uint8_t>& b, const std::string& s, size_t n) {
    for (size_t i = 0; i < n; ++i) b.push_back(i < s.size() ? uint8_t(s[i]) : 0);
}

inline std::vector<uint8_t> buildXm(const ymio::XmModule& m) {
    std::vector<uint8_t> b;
    xmText(b, "Extended Module: ", 17);
    xmText(b, m.name, 20);
    b.push_back(0x1A);
    xmText(b, m.tracker, 20);
    xmPut16(b, m.version);
    xmPut32(b, 276);
    xmPut16(b, uint32_t(m.order.size()));
    xmPut16(b, uint32_t(m.restart));
    xmPut16(b, uint32_t(m.channels));
    xmPut16(b, uint32_t(m.patterns.size()));
    xmPut16(b, uint32_t(m.instruments.size()));
    xmPut16(b, m.linear ? 1 : 0);
    xmPut16(b, uint32_t(m.tempo));
    xmPut16(b, uint32_t(m.bpm));
    for (size_t i = 0; i < 256; ++i) b.push_back(i < m.order.size() ? m.order[i] : 0);
    for (const ymio::XmPattern& p : m.patterns) {
        std::vector<uint8_t> d;
        bool any = false;
        for (const ymio::XmCell& c : p.cells) {
            any = any || !(c == ymio::XmCell{});
            if (c.note && c.instrument && c.volume && c.effect && c.param) {
                d.insert(d.end(), {c.note, c.instrument, c.volume, c.effect, c.param});
                continue;
            }
            const uint8_t mask = uint8_t(0x80 | (c.note ? 1 : 0) | (c.instrument ? 2 : 0) | (c.volume ? 4 : 0) |
                                         (c.effect ? 8 : 0) | (c.param ? 16 : 0));
            d.push_back(mask);
            for (uint8_t v : {c.note, c.instrument, c.volume, c.effect, c.param})
                if (v) d.push_back(v);
        }
        if (!any) d.clear();  // empty pattern: no data
        xmPut32(b, 9);
        b.push_back(0);
        xmPut16(b, uint32_t(p.rows));
        xmPut16(b, uint32_t(d.size()));
        b.insert(b.end(), d.begin(), d.end());
    }
    for (const ymio::XmInstrument& ins : m.instruments) {
        const size_t start = b.size();
        xmPut32(b, ins.samples.empty() ? 29 : 263);
        xmText(b, ins.name, 22);
        b.push_back(0);
        xmPut16(b, uint32_t(ins.samples.size()));
        if (!ins.samples.empty()) {
            xmPut32(b, 40);
            b.insert(b.end(), ins.keymap.begin(), ins.keymap.end());
            b.resize(start + 263, 0);  // envelopes and settings: zero
        }
        std::vector<uint8_t> data;
        for (const ymio::XmSample& s : ins.samples) {
            const uint32_t unit = s.sixteenBit ? 2 : 1;
            xmPut32(b, uint32_t(s.data.size()) * unit);
            xmPut32(b, s.loopStart * unit);
            xmPut32(b, s.loopLength * unit);
            b.push_back(s.volume);
            b.push_back(uint8_t(s.finetune));
            b.push_back(uint8_t(s.loopType | (s.sixteenBit ? 0x10 : 0)));
            b.push_back(s.panning);
            b.push_back(uint8_t(s.relativeNote));
            b.push_back(0);
            xmText(b, s.name, 22);
            int prev = 0;
            for (int16_t v : s.data) {
                if (s.sixteenBit) {
                    xmPut16(data, uint16_t(int16_t(v - prev)));
                    prev = v;
                } else {
                    const int8_t x = int8_t(v / 256);
                    data.push_back(uint8_t(int8_t(x - prev)));
                    prev = x;
                }
            }
        }
        b.insert(b.end(), data.begin(), data.end());
    }
    return b;
}

}  // namespace stym::test
