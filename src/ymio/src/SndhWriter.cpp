// STYM2149 — ymio: writing SNDH files (M7, A5, B2, STYM2149_SPEC.md §8, D-162)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// File layout: three BRA.W (init, exit, play), the SNDH header, the routine, its variables, the tables (subsongs,
// digidrums, DMA copies), the frame streams, then the data. Everything is addressed relative to the PC.
// Chip addresses (Atari ST/STE): YM2149 $FF8800 (select, read) and $FF8802 (write); MFP 68901 at $FFFA01; STE DMA at
// $FF8901. Values taken from the MC68901 data sheet and the Atari memory map (those that AtariAudio emulates,
// AtariMachine.cpp).
//
// Stream of a frame: mask of the written registers on two bytes (bit r: Rr, bit 14: commands follow), the values in
// order from R0 to R13, then the commands (one byte and its operands), terminated by 0. All reads of the stream are
// done byte by byte (no word access at an odd address on the 68000).
#include "ymio/SndhWriter.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

#include "Asm68k.h"
#include "ymio/Sndh.h"

namespace stym::ymio {

namespace {

using namespace m68k;
using ymengine::Generator;
using ymengine::MfpSetting;
using ymengine::RegisterFrame;

constexpr double kAtariClockHz = 2'000'000;

// Variables of the routine, offsets from VARS. Longs: kPos = stream address of the next frame, kCount = frames left
// before the loop, kBase = address of the first frame, kTotal = frames of the subsong.
constexpr int kPos = 0, kCount = 4, kBase = 8, kTotal = 12;
constexpr int kVoiceVars = 16, kVoiceSize = 16;
constexpr int kPtr = 0, kEnd = 4, kVal = 8, kPhase = 9;  // per channel: digidrum (pointer, end), value, SID phase
constexpr int kSaved = 64;    // vectors and MFP registers saved by the init
constexpr int kSaveVec = 66;  // vectors of timers A, B and D
constexpr int kSaveMfp = 78;  // IERA, IERB, IMRA, IMRB, TACR, TBCR, TCDCR
constexpr int kSync = 86;     // demo sync byte: last Zxx command played (D-150), 0 at init
constexpr int kVarsSize = 88;
constexpr int voiceVar(int v, int field) { return kVoiceVars + v * kVoiceSize + field; }

// Timer of each channel: A, B, D (C stays with the system, D-138). MC68901: TACR $FFFA19, TBCR $FFFA1B, TCDCR $FFFA1D
// (C in bits 4-6, D in bits 0-2), TADR $FFFA1F, TBDR $FFFA21, TDDR $FFFA25; end of interrupt in software mode (S bit
// of the vector register, set by TOS): 0 written to the timer's bit in ISRA $FFFA0F (A: bit 5, B: bit 0) or ISRB
// $FFFA11 (D: bit 4), the 1s change nothing there. Atari ST vectors (MFP at $40): A $134, B $120, D $110.
struct TimerRegs {
    uint16_t cr, dr, vec, isr;  // control, data, vector address, in-service register (low 16 bits, absolute short)
    uint8_t isrClear;           // byte written to the in-service register to end the interrupt
    bool shared;  // timer D: prescaler in TCDCR, next to that of timer C
};
constexpr TimerRegs kTimers[3] = {
    {0xFA19, 0xFA1F, 0x0134, 0xFA0F, 0xDF, false},
    {0xFA1B, 0xFA21, 0x0120, 0xFA0F, 0xFE, false},
    {0xFA1D, 0xFA25, 0x0110, 0xFA11, 0xEF, true},
};
// Enable (IERA $FFFA07, IERB $FFFA09) and mask (IMRA $FFFA13, IMRB $FFFA15) registers: A bit 5 and B bit 0, D bit 4.
constexpr uint16_t kIera = 0xFA07, kIerb = 0xFA09, kImra = 0xFA13, kImrb = 0xFA15;
constexpr uint8_t kAbBits = 0x21, kDBits = 0x10;
// Absolute short addresses (absW), sign-extended by the 68000: $8800.w is the chip at $FF8800, $8900.w the DMA sound
// registers at $FF8900; the MFP addresses above work the same way.
constexpr uint16_t kYm = 0x8800, kDma = 0x8900;

// Stream commands: 0 end; per channel, SID, Sync Buzzer or Digidrum start (prescaler, data, value), stop, data,
// prescaler, value; then DMA start (copy on two bytes) and DMA stop (STE; never written for an ST, whose table skips
// them); then Zxx (value of the sync byte).
// Command byte = 1 + channel × kVoiceOps + operation, so the three channels take 1 to 21.
enum VoiceOp : int { kSidStart, kBuzStart, kDigStart, kStop, kData, kCtrl, kValue, kVoiceOps };
constexpr uint8_t voiceOp(int v, VoiceOp k) { return uint8_t(1 + v * kVoiceOps + k); }
constexpr uint8_t kOpDmaStart = 1 + 3 * kVoiceOps;
constexpr uint8_t kOpDmaStop = kOpDmaStart + 1;
constexpr uint8_t kOpSync = kOpDmaStop + 1;

enum EffectType : int { kNone = -1, kSid = 0, kBuzzer = 1, kDrum = 2 };

constexpr int D0 = 0, D1 = 1, D2 = 2, D3 = 3, D4 = 4, D5 = 5, D6 = 6;
// MOVEM register mask from a list of register numbers (0-7: D0-D7, 8-15: A0-A7).
constexpr uint16_t regs(std::initializer_list<int> list) {
    uint16_t m = 0;
    for (int r : list) m = uint16_t(m | 1u << r);
    return m;
}

// Labels shared by the routine and the data placed after it by sndhFile.
struct Labels {
    int init, exit, play, vars, sync, subtab, drumtab, dmatab, stopfx, writeR7;
};

// Timer D shares TCDCR with timer C: only its own bits are cleared or set.
void stopTimer(Asm& a, int v) {
    const TimerRegs& t = kTimers[v];
    if (t.shared) a.andi(Size::B, 0xF0, absW(t.cr));
    else a.move(Size::B, imm(0), absW(t.cr));
}
void startTimer(Asm& a, int v, int d) {  // prescaler in Dd
    const TimerRegs& t = kTimers[v];
    if (t.shared) a.or_(Size::B, d, absW(t.cr));
    else a.move(Size::B, dn(d), absW(t.cr));
}

// Timer interrupts: chip writes by two MOVEs (no other MFP interrupt cuts them, the 68000 masks level 6 during the
// handling). Each handler saves the registers it uses, ends its interrupt in the MFP's in-service register and
// returns with RTE.
void emitIrqHandlers(Asm& a, const Labels& L, int v, int sid, int buz, int dig) {
    const TimerRegs& t = kTimers[v];
    // SID: toggles between the note level (value) and 0; phase 1: high level written last.
    a.bind(sid);
    {
        const int hi = a.label(), wr = a.label();
        // D0 kept whole: moveq below clears its high word, which belongs to the interrupted program.
        a.move(Size::L, dn(D0), preDec(SP));
        a.move(Size::L, an(0), preDec(SP));
        a.lea(pcRel(L.vars), 0);
        a.bchg(0, disp(0, voiceVar(v, kPhase)));
        a.bccS(EQ, hi);
        a.moveq(0, D0);
        a.braS(wr);
        a.bind(hi);
        a.move(Size::B, disp(0, voiceVar(v, kVal)), dn(D0));
        a.bind(wr);
        a.move(Size::B, imm(uint32_t(8 + v)), absW(kYm));
        a.move(Size::B, dn(D0), absW(kYm + 2));
        a.move(Size::B, imm(t.isrClear), absW(t.isr));
        a.move(Size::L, postInc(SP), an(0));
        a.move(Size::L, postInc(SP), dn(D0));
        a.rte();
    }
    // Sync Buzzer: R13 rewritten, the envelope restarts.
    a.bind(buz);
    a.move(Size::W, dn(D0), preDec(SP));
    a.move(Size::B, pcRel(L.vars, voiceVar(v, kVal)), dn(D0));
    a.move(Size::B, imm(13), absW(kYm));
    a.move(Size::B, dn(D0), absW(kYm + 2));
    a.move(Size::B, imm(t.isrClear), absW(t.isr));
    a.move(Size::W, postInc(SP), dn(D0));
    a.rte();
    // Digidrum: next value of the sample; at the end, volume 0 and timer stopped. A1 = this channel's variables,
    // A0 = sample pointer, compared with the end address.
    a.bind(dig);
    {
        const int end = a.label(), out = a.label();
        a.movemPush(regs({D0, 8, 9}));
        a.lea(pcRel(L.vars, voiceVar(v, 0)), 1);
        a.move(Size::L, ind(1), an(0));
        a.cmpa(disp(1, kEnd), 0);
        a.bccS(CC, end);
        a.move(Size::B, postInc(0), dn(D0));
        a.move(Size::L, an(0), ind(1));
        a.move(Size::B, imm(uint32_t(8 + v)), absW(kYm));
        a.move(Size::B, dn(D0), absW(kYm + 2));
        a.bind(out);
        a.move(Size::B, imm(t.isrClear), absW(t.isr));
        a.movemPop(regs({D0, 8, 9}));
        a.rte();
        a.bind(end);
        stopTimer(a, v);
        a.move(Size::B, imm(uint32_t(8 + v)), absW(kYm));
        a.move(Size::B, imm(0), absW(kYm + 2));
        a.braS(out);
    }
}

// Complete routine (init, exit, play, interrupts); the tables and the streams follow, placed by the caller.
// Init, exit and play keep A2 = VARS (and A1 = $FFFF8800 in exit and play) and restore every register they use
// (playRegs), except A0, which the init returns.
void emitRoutine(Asm& a, const Labels& L, bool ste, int subsongs) {
    const uint16_t playRegs = regs({D0, D1, D2, D3, D4, D5, D6, 8, 9, 10, 11, 12});
    std::array<int, 3> sid{}, buz{}, dig{};
    for (int v = 0; v < 3; ++v) {
        sid[size_t(v)] = a.label();
        buz[size_t(v)] = a.label();
        dig[size_t(v)] = a.label();
    }

    // ------------------------------------------------------------------------------------- init (d0 = subsong)
    a.bind(L.init);
    {
        const int saved = a.label(), ok = a.label();
        a.movemPush(playRegs);
        a.lea(pcRel(L.vars), 2);
        // The system's vectors and MFP registers are saved by the first init only (kSaved, cleared by the exit):
        // after it, the vectors may already point to the routine's handlers.
        a.tst(Size::B, disp(2, kSaved));
        a.bcc(NE, saved);
        for (int v = 0; v < 3; ++v) a.move(Size::L, absW(kTimers[v].vec), disp(2, kSaveVec + 4 * v));
        const uint16_t mfp[7] = {kIera, kIerb, kImra, kImrb, kTimers[0].cr, kTimers[1].cr, kTimers[2].cr};
        for (int k = 0; k < 7; ++k) a.move(Size::B, absW(mfp[k]), disp(2, kSaveMfp + k));
        a.move(Size::B, imm(1), disp(2, kSaved));
        a.bind(saved);
        a.bsr(L.stopfx);
        a.ori(Size::B, kAbBits, absW(kIera));
        a.ori(Size::B, kAbBits, absW(kImra));
        a.ori(Size::B, kDBits, absW(kIerb));
        a.ori(Size::B, kDBits, absW(kImrb));
        // Subsong: low word of d0, from 1 to N (out of range: 1).
        a.andi(Size::L, 0xFFFF, dn(D0));
        a.subq(Size::L, 1, dn(D0));
        a.cmpi(Size::L, uint32_t(subsongs), dn(D0));
        a.bccS(CS, ok);
        a.moveq(0, D0);
        a.bind(ok);
        // Subsong table: 8 bytes per subsong, offset of its stream from the table (long), then its frame count.
        a.lsl(Size::L, 3, D0);
        a.lea(pcRel(L.subtab), 3);
        a.move(Size::L, idx(3, D0, 0, true), dn(D1));
        a.add(Size::L, an(3), D1);
        a.move(Size::L, dn(D1), disp(2, kPos));
        a.move(Size::L, dn(D1), disp(2, kBase));
        a.move(Size::L, idx(3, D0, 4, true), dn(D1));
        a.move(Size::L, dn(D1), disp(2, kCount));
        a.move(Size::L, dn(D1), disp(2, kTotal));
        a.move(Size::B, imm(0), disp(2, kSync));
        a.movemPop(playRegs);
        a.lea(pcRel(L.sync), 0);  // A0 on return: address of the sync byte (Zxx), for a demo
        a.rts();
    }

    // ---------------------------------------------------------------------------------------------------- exit
    a.bind(L.exit);
    {
        const int nosave = a.label();
        a.movemPush(playRegs);
        a.lea(pcRel(L.vars), 2);
        a.lea(absW(kYm), 1);
        a.bsr(L.stopfx);
        a.tst(Size::B, disp(2, kSaved));
        a.bcc(EQ, nosave);
        for (int v = 0; v < 3; ++v) a.move(Size::L, disp(2, kSaveVec + 4 * v), absW(kTimers[v].vec));
        // Only the bits of timers A, B and D are restored.
        const struct {
            uint16_t reg;
            int save;
            uint8_t bits;
        } restore[5] = {{kIera, 0, kAbBits}, {kIerb, 1, kDBits}, {kImra, 2, kAbBits}, {kImrb, 3, kDBits}, {kTimers[2].cr, 6, 0x0F}};
        for (const auto& r : restore) {
            a.move(Size::B, disp(2, kSaveMfp + r.save), dn(D0));
            a.andi(Size::B, r.bits, dn(D0));
            a.andi(Size::B, uint8_t(~r.bits), absW(r.reg));
            a.or_(Size::B, D0, absW(r.reg));
        }
        a.move(Size::B, disp(2, kSaveMfp + 4), absW(kTimers[0].cr));
        a.move(Size::B, disp(2, kSaveMfp + 5), absW(kTimers[1].cr));
        a.move(Size::B, imm(0), disp(2, kSaved));
        a.bind(nosave);
        // Volumes R8-R10 to 0, then every tone and noise off (R7 = $3F).
        for (int r = 8; r <= 10; ++r) {
            a.move(Size::W, imm(uint32_t(r << 8)), dn(D0));
            a.movepW(D0, 1, 0);
        }
        a.moveq(0x3F, D1);
        a.bsr(L.writeR7);
        a.movemPop(playRegs);
        a.rts();
    }

    // -------------------------------------------------------------------------------------------- subroutines
    a.bind(L.stopfx);
    for (int v = 0; v < 3; ++v) stopTimer(a, v);
    if (ste) a.move(Size::B, imm(0), absW(kDma + 1));
    a.rts();
    // R7: bits 0-5 from d1, bits 6-7 (directions of the chip ports) kept; a1 = $FFFF8800; d5, d6 clobbered.
    // Select, read and write happen with interrupts masked (SR level 7), so that no interrupt selects another
    // register in between.
    a.bind(L.writeR7);
    a.moveFromSr(D5);
    a.oriSr(0x0700);
    a.move(Size::B, imm(7), ind(1));
    a.move(Size::B, ind(1), dn(D6));
    a.andi(Size::B, 0xC0, dn(D6));
    a.andi(Size::B, 0x3F, dn(D1));
    a.or_(Size::B, dn(D6), D1);
    a.move(Size::B, dn(D1), disp(1, 2));
    a.moveToSr(D5);
    a.rts();

    // ---------------------------------------------------------------------------------------------------- play
    a.bind(L.play);
    const int opLoop = a.label(), done = a.label();
    {
        const int reg = a.label(), skip = a.label(), r7 = a.label(), out = a.label(), table = a.label();
        // A0 = stream of this frame; D3 = its register mask (two bytes, read one at a time), D4 = copy for bit 14.
        // For each of R0-R13, a right shift puts the register's bit in the carry; D0 holds the register number in
        // its high byte, D1 gets the value in its low byte, and MOVEP.W writes both bytes to $FF8800 and $FF8802.
        a.movemPush(playRegs);
        a.lea(pcRel(L.vars), 2);
        a.lea(absW(kYm), 1);
        a.move(Size::L, ind(2), an(0));
        a.moveq(0, D3);
        a.move(Size::B, postInc(0), dn(D3));
        a.lsl(Size::W, 8, D3);
        a.move(Size::B, postInc(0), dn(D3));
        a.move(Size::W, dn(D3), dn(D4));
        a.moveq(0, D0);
        a.moveq(13, D2);
        a.bind(reg);
        a.lsr(Size::W, 1, D3);
        a.bccS(CC, skip);
        a.move(Size::W, dn(D0), dn(D1));
        a.move(Size::B, postInc(0), dn(D1));
        a.cmpi(Size::W, 0x0700, dn(D0));
        a.bccS(EQ, r7);
        a.movepW(D1, 1, 0);  // select and write in one instruction
        a.braS(skip);
        a.bind(r7);
        a.bsr(L.writeR7);
        a.bind(skip);
        a.addi(Size::W, 0x0100, dn(D0));
        a.dbf(D2, reg);
        a.btst(14, dn(D4));
        a.bccS(EQ, done);
        // Commands: each byte indexes a table of word offsets from `table` (command k at table + 2k); its handler
        // reads its operands from A0 and branches back here. Byte 0 ends the frame.
        a.bind(opLoop);
        a.moveq(0, D0);
        a.move(Size::B, postInc(0), dn(D0));
        a.bccS(EQ, done);
        a.add(Size::W, dn(D0), D0);
        a.move(Size::W, pcIdx(table, D0), dn(D0));
        a.jmp(pcIdx(table, D0));
        a.bind(table);
        std::vector<int> handlers;
        const int ops = kOpSync;
        a.w(0);  // command 0: end, never jumped to
        for (int k = 1; k <= ops; ++k) {
            handlers.push_back(a.label());
            a.dcwDiff(handlers.back(), table);
        }
        a.bind(done);
        a.move(Size::L, an(0), ind(2));
        a.subq(Size::L, 1, disp(2, kCount));
        a.bccS(NE, out);
        a.bsr(L.stopfx);  // end of the subsong: effects stopped, back to the first frame
        a.move(Size::L, disp(2, kBase), ind(2));
        a.move(Size::L, disp(2, kTotal), disp(2, kCount));
        a.bind(out);
        a.movemPop(playRegs);
        a.rts();

        // Commands.
        for (int v = 0; v < 3; ++v) {
            const TimerRegs& t = kTimers[v];
            const auto h = [&](VoiceOp k) { a.bind(handlers[size_t(voiceOp(v, k) - 1)]); };
            const auto startWith = [&](int handler) {
                stopTimer(a, v);
                a.lea(pcRel(handler), 3);
                a.move(Size::L, an(3), absW(t.vec));
                a.move(Size::B, postInc(0), dn(D1));
                a.move(Size::B, postInc(0), absW(t.dr));  // timer stopped: the counter is loaded too
            };
            h(kSidStart);
            startWith(sid[size_t(v)]);
            a.move(Size::B, postInc(0), disp(2, voiceVar(v, kVal)));
            a.move(Size::B, imm(1), disp(2, voiceVar(v, kPhase)));
            startTimer(a, v, D1);
            a.bra(opLoop);
            h(kBuzStart);
            startWith(buz[size_t(v)]);
            a.move(Size::B, postInc(0), disp(2, voiceVar(v, kVal)));
            startTimer(a, v, D1);
            a.bra(opLoop);
            h(kDigStart);
            // Slot number → drum table entry of 8 bytes (offset of the sample from the table, length): start and end
            // addresses of the sample into the channel's variables.
            startWith(dig[size_t(v)]);
            a.moveq(0, D2);
            a.move(Size::B, postInc(0), dn(D2));
            a.lsl(Size::W, 3, D2);
            a.lea(pcRel(L.drumtab), 3);
            a.move(Size::L, idx(3, D2), dn(D3));
            a.add(Size::L, an(3), D3);
            a.move(Size::L, dn(D3), dn(D4));
            a.addq(Size::L, 1, dn(D3));  // value 0 is written by the frame
            a.move(Size::L, dn(D3), disp(2, voiceVar(v, kPtr)));
            a.add(Size::L, idx(3, D2, 4), D4);
            a.move(Size::L, dn(D4), disp(2, voiceVar(v, kEnd)));
            startTimer(a, v, D1);
            a.bra(opLoop);
            h(kStop);
            stopTimer(a, v);
            a.bra(opLoop);
            h(kData);
            a.move(Size::B, postInc(0), absW(t.dr));  // timer running: new reload value
            a.bra(opLoop);
            h(kCtrl);
            // Timer D: read-modify-write of TCDCR, so that timer C keeps its prescaler.
            if (t.shared) {
                a.move(Size::B, postInc(0), dn(D1));
                a.move(Size::B, absW(t.cr), dn(D2));
                a.andi(Size::B, 0xF0, dn(D2));
                a.or_(Size::B, dn(D1), D2);
                a.move(Size::B, dn(D2), absW(t.cr));
            } else {
                a.move(Size::B, postInc(0), absW(t.cr));
            }
            a.bra(opLoop);
            h(kValue);
            a.move(Size::B, postInc(0), disp(2, voiceVar(v, kVal)));
            a.bra(opLoop);
        }
        if (ste) {
            // DMA start: sound stopped, copy number (two bytes) → DMA table entry of 16 bytes (offset of the copy
            // from the entry, length, mode, control); frame start and end addresses written byte by byte (high byte
            // at +3 or +$0F, middle and low bytes by MOVEP), then mode and control.
            a.bind(handlers[size_t(kOpDmaStart - 1)]);
            a.lea(absW(kDma), 4);
            a.move(Size::B, imm(0), disp(4, 1));
            a.moveq(0, D2);
            a.move(Size::B, postInc(0), dn(D2));
            a.lsl(Size::W, 8, D2);
            a.move(Size::B, postInc(0), dn(D2));
            a.lsl(Size::L, 4, D2);
            a.lea(pcRel(L.dmatab), 3);
            a.adda(dn(D2), 3);
            a.move(Size::L, ind(3), dn(D3));
            a.add(Size::L, an(3), D3);
            a.move(Size::L, dn(D3), dn(D4));
            a.add(Size::L, disp(3, 4), D4);
            a.movepW(D3, 4, 0x05);  // start: $FF8905, $FF8907
            a.swap(D3);
            a.move(Size::B, dn(D3), disp(4, 0x03));
            a.movepW(D4, 4, 0x11);  // end: $FF8911, $FF8913
            a.swap(D4);
            a.move(Size::B, dn(D4), disp(4, 0x0F));
            a.move(Size::B, disp(3, 8), disp(4, 0x21));  // mode: rate, mono
            a.move(Size::B, disp(3, 9), disp(4, 0x01));  // on, loop
            a.bra(opLoop);
            a.bind(handlers[size_t(kOpDmaStop - 1)]);
            a.move(Size::B, imm(0), absW(kDma + 1));
            a.bra(opLoop);
        } else {  // ST: no DMA command in the stream; no access to the DMA
            a.bind(handlers[size_t(kOpDmaStart - 1)]);
            a.bind(handlers[size_t(kOpDmaStop - 1)]);
            a.bra(opLoop);
        }
        a.bind(handlers[size_t(kOpSync - 1)]);
        a.move(Size::B, postInc(0), disp(2, kSync));
        a.bra(opLoop);
    }

    for (int v = 0; v < 3; ++v) emitIrqHandlers(a, L, v, sid[size_t(v)], buz[size_t(v)], dig[size_t(v)]);
}

// ----------------------------------------------------------------------------------------------------- Frames
// Text of a header tag: printable ASCII kept; a control byte or a UTF-8 character becomes '?'.
std::string atariText(const std::string& s) {
    std::string out;
    for (const char ch : s) {
        const auto u = uint8_t(ch);
        if (u >= 0x20 && u < 0x7F) out += ch;
        else if (u >= 0xC0 || u < 0x20) out += '?';  // a UTF-8 character (its continuation bytes are skipped)
    }
    return out;
}

// Periods for the Atari's 2 MHz clock: tone (12 bits), noise (5 bits) and envelope (16 bits) periods scaled by
// ratio = 2 MHz / capture clock and clamped to their range (counted in `clamped`); a period of 0 stays 0.
std::array<uint8_t, 14> atariRegisters(const RegisterFrame& f, double ratio, uint32_t& clamped) {
    std::array<uint8_t, 14> r = f.reg;
    if (ratio == 1) return r;
    const auto scale = [&](uint32_t p, uint32_t max) {
        if (p == 0) return p;
        const double q = std::round(p * ratio);
        if (q < 1 || q > max) ++clamped;
        return uint32_t(std::clamp(q, 1.0, double(max)));
    };
    for (int v = 0; v < 3; ++v) {
        const uint32_t p = scale(uint32_t(r[size_t(2 * v)] | (r[size_t(2 * v + 1)] & 15) << 8), 4095);
        r[size_t(2 * v)] = uint8_t(p);
        r[size_t(2 * v + 1)] = uint8_t(p >> 8);
    }
    r[6] = uint8_t(scale(r[6] & 31u, 31));
    const uint32_t e = scale(uint32_t(r[11] | r[12] << 8), 65535);
    r[11] = uint8_t(e);
    r[12] = uint8_t(e >> 8);
    return r;
}

// A DMA sample as stored in the file, with the values of the STE's sound mode ($FF8921: rate in bits 0-1, bit 7 =
// mono) and control ($FF8901: bit 0 = play, bit 1 = loop) registers.
struct DmaCopy {
    std::vector<int8_t> data;
    uint8_t mode = 0, control = 1;
};

// Turns the frames of a take into the stream that the play routine reads (see the file comment), and collects the
// DMA copies they need. One encoder for all the subsongs, so that they share the copies.
class Encoder {
public:
    Encoder(const SndhWriteInfo& info, SndhReport& report) : info_(info), report_(report) {
        const double hz = info.clock.num && info.clock.den ? double(info.clock.num) / double(info.clock.den) : kAtariClockHz;
        ratio_ = std::abs(hz - kAtariClockHz) < 0.5 ? 1.0 : kAtariClockHz / hz;
    }

    std::vector<uint8_t> stream(const SndhSubsong& sub) {
        std::vector<uint8_t> out;
        // Last value written to each register by the stream (-1: unknown, so written at the next frame).
        std::array<int, 14> shadow;
        shadow.fill(-1);
        std::array<Voice, 3> voices{};
        bool dmaOn = false;
        ymengine::DmaFrame dmaCur;
        for (size_t k = 0; k < sub.frames.size(); ++k) {
            const RegisterFrame& f = sub.frames[k];
            const std::array<uint8_t, 14> r = atariRegisters(f, ratio_, report_.clampedPeriods);
            std::vector<uint8_t> ops;
            // owned: the channel's volume belongs to its interrupt (SID, Digidrum); started: effect (re)started here.
            std::array<bool, 3> owned{}, started{};
            std::array<uint8_t, 3> startLevel{};
            int buzzStart = -1;
            bool buzzOn = false;
            for (int v = 0; v < 3; ++v) {
                const RegisterFrame::Effect& e = f.fx[size_t(v)];
                Voice& s = voices[size_t(v)];
                int type = kNone;
                if (e.on) {
                    type = e.gen == Generator::Sid ? kSid : e.gen == Generator::SyncBuzzer ? kBuzzer
                                                    : e.gen == Generator::Digidrum ? kDrum : kNone;
                    if (type == kDrum && info_.digidrums[e.value & 7].empty()) type = kNone;
                    if (!e.timer.running()) type = kNone;
                    if (type == kNone) ++report_.droppedEffects;
                }
                if (type == kNone) {
                    if (s.type != kNone) ops.push_back(voiceOp(v, kStop));
                    s.type = kNone;
                    continue;
                }
                // value: Digidrum slot, otherwise SID level or buzzer shape. A start (re)loads the timer; otherwise
                // only what changed is sent.
                const uint8_t value = uint8_t(type == kDrum ? (e.value & 7) : (e.value & 15));
                const uint8_t ctrl = uint8_t(e.timer.prescaler & 7), data = uint8_t(e.timer.data & 0xFF);
                const bool start = e.restart || s.type != type || (type == kDrum && value != s.value);
                if (start) {
                    ops.insert(ops.end(), {voiceOp(v, type == kSid ? kSidStart : type == kBuzzer ? kBuzStart : kDigStart), ctrl, data, value});
                    s = Voice{type, e.timer, value, 0.0};
                    started[size_t(v)] = true;
                    if (type == kSid) startLevel[size_t(v)] = value;
                    if (type == kDrum) startLevel[size_t(v)] = uint8_t(info_.digidrums[value][0] & 15);
                    if (type == kBuzzer) buzzStart = v;
                } else {
                    if (e.timer.data != s.timer.data) ops.insert(ops.end(), {voiceOp(v, kData), data});
                    if (e.timer.prescaler != s.timer.prescaler) ops.insert(ops.end(), {voiceOp(v, kCtrl), ctrl});
                    if (type != kDrum && value != s.value) ops.insert(ops.end(), {voiceOp(v, kValue), value});
                    s.timer = e.timer;
                    s.value = value;
                }
                owned[size_t(v)] = type != kBuzzer;
                buzzOn = buzzOn || type == kBuzzer;
                if (type == kDrum) {  // the engine still loops the sample (D-154): the SNDH has finished it
                    if (s.irqs > double(info_.digidrums[value].size()) + 1) ++report_.heldDrums;
                    s.irqs += e.timer.hz() / std::max(1u, info_.frameHz);
                }
            }
            if (!sub.dma.empty() && k < sub.dma.size()) dmaFrame(sub.dma[k], ops, dmaOn, dmaCur);
            if (f.sync >= 0) ops.insert(ops.end(), {kOpSync, uint8_t(f.sync)});

            // R0-R12: only the values that differ from the last ones written.
            uint16_t mask = 0;
            std::vector<uint8_t> values;
            for (int reg = 0; reg < 13; ++reg) {
                const int v = reg - 8;
                if (v >= 0 && v < 3 && owned[size_t(v)]) {  // register of the interrupt: written at the attack only
                    if (started[size_t(v)]) {
                        mask = uint16_t(mask | 1u << reg);
                        values.push_back(startLevel[size_t(v)]);
                    }
                    shadow[size_t(reg)] = -1;
                    continue;
                }
                const int value = reg == 7 ? (r[7] & 0x3F) : r[size_t(reg)];
                if (shadow[size_t(reg)] == value) continue;
                shadow[size_t(reg)] = value;
                mask = uint16_t(mask | 1u << reg);
                values.push_back(uint8_t(value));
            }
            // R13 restarts the envelope: at the attack of a Sync Buzzer (its shape), never while it runs (its
            // interrupt restarts it), otherwise when the engine wrote it.
            if (buzzStart >= 0 || (!buzzOn && f.r13)) {
                mask = uint16_t(mask | 1u << 13);
                values.push_back(uint8_t(buzzStart >= 0 ? f.fx[size_t(buzzStart)].value & 15 : r[13] & 15));
            }
            if (!ops.empty()) {
                mask = uint16_t(mask | 1u << 14);
                ops.push_back(0);
            }
            out.push_back(uint8_t(mask >> 8));
            out.push_back(uint8_t(mask));
            out.insert(out.end(), values.begin(), values.end());
            out.insert(out.end(), ops.begin(), ops.end());
        }
        return out;
    }

    const std::vector<DmaCopy>& copies() const noexcept { return copies_; }

private:
    struct Voice {
        int type = kNone;
        MfpSetting timer{};
        uint8_t value = 0;
        double irqs = 0;  // interrupts since the attack (digidrum)
    };

    // DMA commands of one frame: start at the attack of a note (copy at its pitch and volume), stop at its end.
    // Changes during the note are only counted (the copy cannot follow them).
    void dmaFrame(const ymengine::DmaFrame& d, std::vector<uint8_t>& ops, bool& on, ymengine::DmaFrame& cur) {
        if (d.voices > 1) ++report_.dmaOverlap;
        if (d.on && d.restart) {
            const SndhDmaSample& s = info_.dma[d.slot & 7];
            const size_t ch = s.stereo ? 2 : 1;
            if (!info_.ste || s.data.size() < ch) {
                ++report_.dmaDropped;
                if (on) ops.push_back(kOpDmaStop);
                on = false;
                return;
            }
            if (d.start * ch >= s.data.size()) {  // Oxx past the end (D-338): the engine does not play it either
                if (on) ops.push_back(kOpDmaStop);
                on = false;
                return;
            }
            const uint16_t copy = copyFor(d.slot & 7, d.step, d.gain, d.start);
            ops.insert(ops.end(), {kOpDmaStart, uint8_t(copy >> 8), uint8_t(copy)});
            on = true;
            cur = d;
        } else if (d.on && on) {
            if (d.step != cur.step || gainLevel(d.gain) != gainLevel(cur.gain)) ++report_.dmaChanges;
        } else if (!d.on && on) {
            ops.push_back(kOpDmaStop);
            on = false;
        }
    }

    static int gainLevel(float g) { return int(std::lround(std::clamp(double(g), 0.0, 1.0) * 16)); }  // 0-16

    // Copy of the slot from the frame start (Oxx of the CMD row, D-338) at the pitch (read step, like the engine:
    // sample at the integer position) and the volume (sixteen levels) of the attack, at the slot's rate; even length
    // (even DMA addresses). The loop of the copy resumes at its first frame, as in the engine.
    uint16_t copyFor(int slot, float step, float gain, uint32_t start) {
        const int level = gainLevel(gain);
        const auto key = std::make_tuple(slot, step, level, start);
        if (const auto it = index_.find(key); it != index_.end()) return it->second;
        const SndhDmaSample& s = info_.dma[size_t(slot)];
        const size_t ch = s.stereo ? 2 : 1;
        const size_t frames = s.data.size() / ch;
        const double st = step > 0 ? double(step) : 1.0;
        size_t n = size_t(std::ceil(double(frames - start) / st));
        if (n * ch > kSndhMaxDmaCopy) {
            n = kSndhMaxDmaCopy / ch;
            ++report_.dmaTruncated;
        }
        DmaCopy c;
        c.data.reserve(n * ch + 1);
        for (size_t j = 0; j < n; ++j) {
            const size_t src = std::min(frames - 1, start + size_t(double(j) * st));
            for (size_t k = 0; k < ch; ++k) {
                const int v = s.data[src * ch + k];
                c.data.push_back(int8_t(level >= 16 ? v : std::clamp<long>(std::lround(v * level / 16.0), -128, 127)));
            }
        }
        if (c.data.size() & 1) c.data.push_back(0);
        c.mode = uint8_t((s.rate & 3) | (s.stereo ? 0 : 0x80));
        c.control = s.loop ? 3 : 1;
        copies_.push_back(std::move(c));
        const auto id = uint16_t(copies_.size() - 1);
        index_.emplace(key, id);
        return id;
    }

    const SndhWriteInfo& info_;
    SndhReport& report_;
    double ratio_ = 1;
    std::vector<DmaCopy> copies_;
    std::map<std::tuple<int, float, int, uint32_t>, uint16_t> index_;  // (slot, step, gain level, start) → copy number
};

// Header tag: its name, then its text and a zero byte.
void tag(Asm& a, const char* name, const std::string& text) {
    for (const char* p = name; *p; ++p) a.b(uint8_t(*p));
    for (const char ch : text) a.b(uint8_t(ch));
    a.b(0);
}

}  // namespace

SndhWriteResult sndhFile(std::span<const SndhSubsong> subsongs, const SndhWriteInfo& info) {
    SndhWriteResult res;
    if (subsongs.empty() || subsongs.size() > size_t(kSndhWriteMaxSubsongs)) {
        res.error = "invalid number of subsongs";
        return res;
    }
    for (const SndhSubsong& s : subsongs)
        if (s.frames.empty()) {
            res.error = "subsong without frames";
            return res;
        }
    const uint32_t hz = std::max(1u, info.frameHz);

    Encoder enc(info, res.report);
    std::vector<std::vector<uint8_t>> streams;
    for (const SndhSubsong& s : subsongs) streams.push_back(enc.stream(s));
    res.report.dmaCopies = uint32_t(enc.copies().size());
    if (enc.copies().size() > 0xFFFF) {
        res.error = "too many DMA copies";
        return res;
    }

    Asm a;
    Labels L{a.label(), a.label(), a.label(), a.label(), a.label(),
             a.label(), a.label(), a.label(), a.label(), a.label()};
    a.bra(L.init);
    a.bra(L.exit);
    a.bra(L.play);
    // Header: AtariAudio reads the tags between byte 16 and the target of the first BRA (init).
    for (const char ch : std::string_view("SNDH")) a.b(uint8_t(ch));
    if (!info.title.empty()) tag(a, "TITL", atariText(info.title));
    if (!info.composer.empty()) tag(a, "COMM", atariText(info.composer));
    tag(a, "CONV", "STYM2149");
    const std::string count = (subsongs.size() < 10 ? "0" : "") + std::to_string(subsongs.size());  // 01 to 99
    for (const char ch : "##" + count) a.b(uint8_t(ch));
    tag(a, "TC", std::to_string(hz));  // before TIME: AtariAudio counts the seconds there in ticks of this rate
    a.align();
    for (const char ch : std::string_view("TIME")) a.b(uint8_t(ch));
    for (const SndhSubsong& s : subsongs) a.w(uint16_t(std::clamp<uint64_t>((s.frames.size() + hz / 2) / hz, 1, 65535)));
    for (const char ch : std::string_view("FRMS")) a.b(uint8_t(ch));
    for (const SndhSubsong& s : subsongs) a.l(uint32_t(s.frames.size()));
    for (const char ch : std::string_view("HDNS")) a.b(uint8_t(ch));
    a.align();

    emitRoutine(a, L, info.ste, int(subsongs.size()));

    // Data after the routine: variables (zeroed), subsong table, drum table, DMA table, then the frame streams, the
    // digidrum samples and the DMA copies that the tables point to (offsets resolved by Asm::finish).
    a.align();
    a.bind(L.vars);
    for (int k = 0; k < kVarsSize; ++k) {
        if (k == kSync) {
            a.bind(L.sync);
            res.syncOffset = uint32_t(a.out.size());
        }
        a.b(0);
    }
    a.align();
    std::vector<int> streamLabels, drumLabels(8, -1), copyLabels;
    a.bind(L.subtab);
    for (size_t k = 0; k < subsongs.size(); ++k) {
        streamLabels.push_back(a.label());
        a.dclDiff(streamLabels.back(), L.subtab);
        a.l(uint32_t(subsongs[k].frames.size()));
    }
    a.bind(L.drumtab);
    for (size_t k = 0; k < 8; ++k) {
        // Empty slot: zero offset and length (the encoder never starts it).
        if (info.digidrums[k].empty()) {
            a.l(0);
            a.l(0);
            continue;
        }
        drumLabels[k] = a.label();
        a.dclDiff(drumLabels[k], L.drumtab);
        a.l(uint32_t(info.digidrums[k].size()));
    }
    a.bind(L.dmatab);
    for (const DmaCopy& c : enc.copies()) {
        const int entry = a.label();
        a.bind(entry);
        copyLabels.push_back(a.label());
        a.dclDiff(copyLabels.back(), entry);
        a.l(uint32_t(c.data.size()));
        a.b(c.mode);
        a.b(c.control);
        for (int k = 0; k < 6; ++k) a.b(0);
    }
    for (size_t k = 0; k < streams.size(); ++k) {
        a.bind(streamLabels[k]);
        a.out.insert(a.out.end(), streams[k].begin(), streams[k].end());
    }
    for (size_t k = 0; k < 8; ++k) {
        if (drumLabels[k] < 0) continue;
        a.bind(drumLabels[k]);
        for (const uint8_t v : info.digidrums[k]) a.b(uint8_t(v & 15));
    }
    for (size_t k = 0; k < enc.copies().size(); ++k) {
        a.align();  // the DMA reads from even addresses
        a.bind(copyLabels[k]);
        for (const int8_t v : enc.copies()[k].data) a.b(uint8_t(v));
    }
    a.align();
    if (!a.finish(res.error)) return res;
    if (a.out.size() > kSndhMaxSize - kSndhLoadAddress) {
        res.error = "SNDH too large (" + std::to_string(a.out.size() / 1024) + " KiB) for the emulated Atari's memory";
        return res;
    }
    res.data = std::move(a.out);
    return res;
}

}  // namespace stym::ymio
