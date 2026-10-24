// STYM2149 — small SNDH files built for the tests: minimal header and hand-assembled 68000 routines.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace stym::test {

// 68000 code in 16-bit words. Short addresses (abs.w) sign-extended: $8800 = $ff8800, $fa19 = $fffa19.
using M68kCode = std::vector<uint16_t>;

inline void moveByte(M68kCode& c, uint8_t value, uint16_t address) {  // move.b #value,address.w
    c.insert(c.end(), {0x11fc, value, address});
}
inline void ymWrite(M68kCode& c, uint8_t reg, uint8_t value) {
    moveByte(c, reg, 0x8800);
    moveByte(c, value, 0x8802);
}

struct SndhSource {
    std::string title = "Test";
    int subsongs = 1;
    int tickRate = 50;    // TC tag
    M68kCode init;        // followed by rts
    M68kCode play;        // followed by rts
    M68kCode timerA;      // timer A routine (followed by rte); empty: no timer
    uint8_t timerAControl = 1;  // MFP divider (1: ÷4)
    uint8_t timerAData = 200;
};

// Unpacked SNDH file: bra.w init / exit / play, "SNDH", TITL, ##, TC, HDNS, then the code. The init programs timer A
// (vector $134, TACR, TADR, IERA, IMRA) before the code of the source.
inline std::vector<uint8_t> buildSndh(const SndhSource& s) {
    constexpr uint32_t kLoad = 0x10002;
    std::vector<uint8_t> f(16, 0);
    f[12] = 'S'; f[13] = 'N'; f[14] = 'D'; f[15] = 'H';
    auto text = [&](const std::string& t) {
        f.insert(f.end(), t.begin(), t.end());
        f.push_back(0);
    };
    text("TITL" + s.title);
    std::string n = std::to_string(s.subsongs);
    if (n.size() < 2) n = "0" + n;
    const std::string sub = "##" + n;
    f.insert(f.end(), sub.begin(), sub.end());
    text("TC" + std::to_string(s.tickRate));
    for (char ch : std::string("HDNS")) f.push_back(uint8_t(ch));
    if (f.size() & 1) f.push_back(0);

    M68kCode init;
    size_t vectorAt = SIZE_MAX;  // word of the timer routine's address, rewritten once the code is placed
    if (!s.timerA.empty()) {
        vectorAt = init.size() + 1;
        init.insert(init.end(), {0x21fc, 0, 0, 0x0134});  // move.l #timerA,$134.w
        moveByte(init, 0, 0xfa19);                          // TACR: stop
        moveByte(init, s.timerAData, 0xfa1f);               // TADR
        moveByte(init, s.timerAControl, 0xfa19);            // TACR: divider
        init.insert(init.end(), {0x08f8, 5, 0xfa07});       // bset #5,$fffa07.w (IERA)
        init.insert(init.end(), {0x08f8, 5, 0xfa13});       // bset #5,$fffa13.w (IMRA)
    }
    init.insert(init.end(), s.init.begin(), s.init.end());
    init.push_back(0x4e75);  // rts
    M68kCode play = s.play;
    play.push_back(0x4e75);
    M68kCode timer = s.timerA;
    if (!timer.empty()) timer.push_back(0x4e73);  // rte

    const size_t initAt = f.size();
    const size_t exitAt = initAt + init.size() * 2;
    const size_t playAt = exitAt + 2;
    const size_t timerAt = playAt + play.size() * 2;
    if (vectorAt != SIZE_MAX) {
        const uint32_t address = kLoad + uint32_t(timerAt);
        init[vectorAt] = uint16_t(address >> 16);
        init[vectorAt + 1] = uint16_t(address);
    }
    auto bra = [&](size_t at, size_t target) {  // bra.w: displacement from at + 2
        const uint16_t d = uint16_t(target - (at + 2));
        f[at] = 0x60;
        f[at + 1] = 0;
        f[at + 2] = uint8_t(d >> 8);
        f[at + 3] = uint8_t(d);
    };
    bra(0, initAt);
    bra(4, exitAt);
    bra(8, playAt);
    auto emit = [&](const M68kCode& c) {
        for (uint16_t w : c) {
            f.push_back(uint8_t(w >> 8));
            f.push_back(uint8_t(w));
        }
    };
    emit(init);
    emit({0x4e75});  // exit: rts
    emit(play);
    emit(timer);
    return f;
}

}  // namespace stym::test
