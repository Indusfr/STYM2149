// STYM2149 — ymio: small 68000 assembler for the routine of exported SNDH files (M7, A5, D-162)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Just the instructions the routine needs, encoded from the 68000 reference manual (Motorola M68000 Family
// Programmer's Reference Manual, encoding tables of each instruction); labels are resolved at the end (PC-relative
// displacements). The produced code is checked by the tests by running it in the Musashi 68000 (AtariAudio's
// machine), and its encodings against those the prototype wrote by hand (sndhBlob).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace stym::ymio::m68k {

enum class Size : uint8_t { B, W, L };

// Conditions of the branches (Bcc) and of DBcc.
enum Cond : uint8_t { T = 0, F = 1, HI = 2, LS = 3, CC = 4, CS = 5, NE = 6, EQ = 7, PL = 10, MI = 11, GE = 12, LT = 13, GT = 14, LE = 15 };

// Registers: D0-D7 = 0-7, A0-A7 = 8-15 (index and MOVEM lists).
inline constexpr int A0 = 8, SP = 15;

// Effective addressing mode (mode and register of the EA field, extensions). Mode 7 selects by its register field:
// 0 = absolute short, 1 = absolute long, 2 = d16(PC), 3 = d8(PC,Xn), 4 = immediate.
struct Ea {
    uint8_t mode = 0, reg = 0;
    uint32_t value = 0;   // displacement, short address, immediate value
    int label = -1;       // d16(PC), d8(PC,Xn): target label
    uint8_t index = 0;    // d8(An,Xn), d8(PC,Xn): index register (0-15)
    bool indexLong = false;
};
// Constructors of the modes, in the order of the mode field (0: Dn, 1: An, 2: (An), 3: (An)+, 4: -(An), 5: d16(An),
// 6: d8(An,Xn)).
// Address register: 0-7 or 8-15 (A0 = 8, SP = 15); the EA field keeps only its three low bits.
inline Ea dn(int n) { return {0, uint8_t(n & 7)}; }
inline Ea an(int n) { return {1, uint8_t(n & 7)}; }
inline Ea ind(int n) { return {2, uint8_t(n & 7)}; }
inline Ea postInc(int n) { return {3, uint8_t(n & 7)}; }
inline Ea preDec(int n) { return {4, uint8_t(n & 7)}; }
inline Ea disp(int n, int d) { return {5, uint8_t(n & 7), uint32_t(uint16_t(int16_t(d)))}; }
inline Ea idx(int n, int xn, int d = 0, bool longIndex = false) {
    Ea e{6, uint8_t(n & 7), uint32_t(uint8_t(int8_t(d)))};
    e.index = uint8_t(xn);
    e.indexLong = longIndex;
    return e;
}
inline Ea absW(uint32_t a) { return {7, 0, a & 0xffffu}; }  // $FFFF8800.w is written absW(0x8800)
inline Ea pcRel(int label, int addend = 0) {  // label + addend, PC-relative
    Ea e{7, 2, uint32_t(addend)};
    e.label = label;
    return e;
}
inline Ea pcIdx(int label, int xn) {
    Ea e{7, 3};
    e.label = label;
    e.index = uint8_t(xn);
    return e;
}
inline Ea imm(uint32_t v) { return {7, 4, v}; }

// Big-endian code buffer. A label is created by label(), placed by bind(); every reference to it is recorded as a
// fixup and patched by finish(), so code may refer to labels placed later.
class Asm {
public:
    std::vector<uint8_t> out;

    uint32_t pos() const noexcept { return uint32_t(out.size()); }
    int label() {
        labels_.push_back(-1);
        return int(labels_.size()) - 1;
    }
    void bind(int l) { labels_[size_t(l)] = int64_t(out.size()); }
    uint32_t at(int l) const { return uint32_t(labels_[size_t(l)]); }

    void b(uint8_t v) { out.push_back(v); }
    void w(uint16_t v) {
        out.push_back(uint8_t(v >> 8));
        out.push_back(uint8_t(v));
    }
    void l(uint32_t v) {
        w(uint16_t(v >> 16));
        w(uint16_t(v));
    }
    void align() {
        if (out.size() & 1) out.push_back(0);
    }
    // Data: word target - base, long target - base (jump and offset tables).
    void dcwDiff(int target, int base) {
        fix(Fix::Word, target, base, pos());
        w(0);
    }
    void dclDiff(int target, int base) {
        fix(Fix::Long, target, base, pos());
        l(0);
    }

    // ---------------------------------------------------------------------------------------- Instructions
    // Each one writes its operation word, then the extension words of its operands, source first. A data register
    // argument `d` is 0-7; an address register argument `a` is 0-7 or 8-15.
    void move(Size s, Ea src, Ea dst) {
        const uint16_t sz = s == Size::B ? 1 : s == Size::W ? 3 : 2;
        op(uint16_t(sz << 12 | dst.reg << 9 | dst.mode << 6 | eaBits(src)));
        ext(src, s);
        ext(dst, s);
    }
    void moveq(int v, int d) { w(uint16_t(0x7000 | d << 9 | (uint8_t(int8_t(v))))); }
    void lea(Ea src, int a) {
        op(uint16_t(0x41C0 | (a & 7) << 9 | eaBits(src)));
        ext(src, Size::L);
    }
    // MOVEM.L: registers (bit n: Dn for n < 8, A(n - 8) otherwise) pushed by -(SP), popped by (SP)+.
    // The predecrement form takes the mask in reverse bit order (bit 0 = A7), hence the reversal.
    void movemPush(uint16_t regs) {
        uint16_t rev = 0;
        for (int i = 0; i < 16; ++i)
            if (regs >> i & 1) rev = uint16_t(rev | 1u << (15 - i));
        w(0x48E7);
        w(rev);
    }
    void movemPop(uint16_t regs) {
        w(0x4CDF);
        w(regs);
    }
    // MOVEP: the bytes of Dn, high byte first, to every other address from dsp(An) (dsp, dsp + 2...).
    void movepW(int d, int a, int dsp) { movep(6, d, a, dsp); }  // Dn → memory, word
    void movepL(int d, int a, int dsp) { movep(7, d, a, dsp); }
    // ADDQ / SUBQ: v from 1 to 8 (8 is encoded 0).
    void addq(Size s, int v, Ea dst) { quick(0, s, v, dst); }
    void subq(Size s, int v, Ea dst) { quick(1, s, v, dst); }
    void ori(Size s, uint32_t v, Ea dst) { immOp(0x0000, s, v, dst); }
    void andi(Size s, uint32_t v, Ea dst) { immOp(0x0200, s, v, dst); }
    void subi(Size s, uint32_t v, Ea dst) { immOp(0x0400, s, v, dst); }
    void addi(Size s, uint32_t v, Ea dst) { immOp(0x0600, s, v, dst); }
    void cmpi(Size s, uint32_t v, Ea dst) { immOp(0x0C00, s, v, dst); }
    // <ea>,Dn and Dn,<ea> of ADD, SUB, AND, OR, CMP (CMP: <ea>,Dn only).
    void add(Size s, Ea src, int d) { regOp(0xD000, s, src, d, false); }
    void add(Size s, int d, Ea dst) { regOp(0xD000, s, dst, d, true); }
    void sub(Size s, Ea src, int d) { regOp(0x9000, s, src, d, false); }
    void and_(Size s, Ea src, int d) { regOp(0xC000, s, src, d, false); }
    void or_(Size s, Ea src, int d) { regOp(0x8000, s, src, d, false); }
    void or_(Size s, int d, Ea dst) { regOp(0x8000, s, dst, d, true); }
    void cmp(Size s, Ea src, int d) { regOp(0xB000, s, src, d, false); }
    void adda(Ea src, int a) {
        op(uint16_t(0xD1C0 | (a & 7) << 9 | eaBits(src)));
        ext(src, Size::L);
    }
    void cmpa(Ea src, int a) {
        op(uint16_t(0xB1C0 | (a & 7) << 9 | eaBits(src)));
        ext(src, Size::L);
    }
    // Immediate shift count from 1 to 8 (8 is encoded 0).
    void lsl(Size s, int count, int d) { shift(s, count, d, true); }
    void lsr(Size s, int count, int d) { shift(s, count, d, false); }
    // Static bit number (immediate word).
    void btst(int bit, Ea dst) { bitOp(0, bit, dst); }
    void bchg(int bit, Ea dst) { bitOp(1, bit, dst); }
    void bclr(int bit, Ea dst) { bitOp(2, bit, dst); }
    void bset(int bit, Ea dst) { bitOp(3, bit, dst); }
    void clr(Size s, Ea dst) {
        op(uint16_t(0x4200 | sizeBits(s) << 6 | eaBits(dst)));
        ext(dst, s);
    }
    void tst(Size s, Ea dst) {
        op(uint16_t(0x4A00 | sizeBits(s) << 6 | eaBits(dst)));
        ext(dst, s);
    }
    void swap(int d) { w(uint16_t(0x4840 | d)); }
    void moveFromSr(int d) { w(uint16_t(0x40C0 | d)); }
    void moveToSr(int d) { w(uint16_t(0x46C0 | d)); }
    void oriSr(uint16_t v) {
        w(0x007C);
        w(v);
    }
    void jmp(Ea dst) {
        op(uint16_t(0x4EC0 | eaBits(dst)));
        ext(dst, Size::L);
    }
    void rts() { w(0x4E75); }
    void rte() { w(0x4E73); }
    // Branches: long form (16-bit displacement) or short (8 bits, checked at resolution).
    void bra(int target) { bcc(T, target); }
    void bsr(int target) { bcc(F, target); }  // condition code 1 (F) in a Bcc word is BSR
    void bcc(Cond c, int target) {
        w(uint16_t(0x6000 | c << 8));
        fix(Fix::Disp16, target, -1, pos());
        w(0);
    }
    void braS(int target) { bccS(T, target); }
    void bccS(Cond c, int target) {
        fix(Fix::Disp8, target, -1, pos() + 1);
        w(uint16_t(0x6000 | c << 8));
    }
    void dbf(int d, int target) {
        w(uint16_t(0x51C8 | d));
        fix(Fix::Disp16, target, -1, pos());
        w(0);
    }

    // Resolves the labels; false (and `error`) if a label is missing or a displacement leaves its range.
    bool finish(std::string& error) {
        for (const Fixup& f : fixups_) {
            const int64_t bound = labels_[size_t(f.target)];
            const int64_t target = bound + f.addend;
            if (bound < 0) {
                error = "68000 label not placed";
                return false;
            }
            int64_t base = f.base >= 0 ? labels_[size_t(f.base)] : int64_t(f.at);
            if (f.kind == Fix::Disp8) base = int64_t(f.at) + 1;  // low byte of the Bcc word, base = Bcc + 2
            if (f.kind == Fix::IndexDisp8) base = int64_t(f.at) - 1;  // extension word
            const int64_t d = target - base;
            switch (f.kind) {
            case Fix::Disp16:
            case Fix::Word:
                if (d < -32768 || d > 32767) return range(error);
                out[f.at] = uint8_t(d >> 8);
                out[f.at + 1] = uint8_t(d);
                break;
            case Fix::Long:
                for (int k = 0; k < 4; ++k) out[f.at + size_t(k)] = uint8_t(uint64_t(d) >> (24 - 8 * k));
                break;
            case Fix::Disp8:
                // 0 in the low byte means "16-bit displacement follows" ($FF a 32-bit one on later CPUs): refused.
                if (d < -128 || d > 127 || d == 0 || d == -1) return range(error);
                out[f.at] = uint8_t(d);
                break;
            case Fix::IndexDisp8:
                if (d < -128 || d > 127) return range(error);
                out[f.at] = uint8_t(d);
                break;
            }
        }
        return true;
    }

private:
    enum class Fix : uint8_t { Disp16, Disp8, IndexDisp8, Word, Long };
    struct Fixup {
        Fix kind;
        int target, base;  // base: label (data), otherwise -1 (address of the extension word)
        size_t at;
        int32_t addend = 0;
    };
    void fix(Fix k, int target, int base, size_t at, int32_t addend = 0) { fixups_.push_back({k, target, base, at, addend}); }
    static bool range(std::string& error) {
        error = "68000 displacement out of range";
        return false;
    }

    static uint16_t eaBits(const Ea& e) { return uint16_t(e.mode << 3 | e.reg); }
    static uint16_t sizeBits(Size s) { return s == Size::B ? 0 : s == Size::W ? 1 : 2; }
    void op(uint16_t v) { w(v); }  // operation word (its extensions follow)
    uint16_t indexWord(const Ea& e) const {
        return uint16_t((e.index >= 8 ? 0x8000 : 0) | (e.index & 7) << 12 | (e.indexLong ? 0x0800 : 0) | (e.value & 0xff));
    }
    // Extension words of an addressing mode (source before destination).
    void ext(const Ea& e, Size s) {
        if (e.mode == 5) w(uint16_t(e.value));
        else if (e.mode == 6) w(indexWord(e));
        else if (e.mode == 7 && e.reg == 0) w(uint16_t(e.value));
        else if (e.mode == 7 && e.reg == 1) l(e.value);
        else if (e.mode == 7 && e.reg == 2) {
            fix(Fix::Disp16, e.label, -1, pos(), int32_t(e.value));
            w(0);
        } else if (e.mode == 7 && e.reg == 3) {
            fix(Fix::IndexDisp8, e.label, -1, pos() + 1);
            w(indexWord(e));
        } else if (e.mode == 7 && e.reg == 4) {
            if (s == Size::L) l(e.value);
            else w(uint16_t(s == Size::B ? (e.value & 0xff) : e.value));
        }
    }
    void movep(int opmode, int d, int a, int dsp) {
        w(uint16_t(0x0008 | d << 9 | opmode << 6 | (a & 7)));
        w(uint16_t(int16_t(dsp)));
    }
    void quick(int sub, Size s, int v, Ea dst) {
        op(uint16_t(0x5000 | (v & 7) << 9 | sub << 8 | sizeBits(s) << 6 | eaBits(dst)));
        ext(dst, s);
    }
    void immOp(uint16_t base, Size s, uint32_t v, Ea dst) {
        op(uint16_t(base | sizeBits(s) << 6 | eaBits(dst)));
        ext(imm(v), s);
        ext(dst, s);
    }
    void regOp(uint16_t base, Size s, Ea e, int d, bool toEa) {
        op(uint16_t(base | d << 9 | (toEa ? 0x100 : 0) | sizeBits(s) << 6 | eaBits(e)));
        ext(e, s);
    }
    void shift(Size s, int count, int d, bool left) {
        w(uint16_t(0xE008 | (count & 7) << 9 | (left ? 0x100 : 0) | sizeBits(s) << 6 | d));
    }
    void bitOp(int kind, int bit, Ea dst) {
        op(uint16_t(0x0800 | kind << 6 | eaBits(dst)));
        w(uint16_t(bit));
        ext(dst, Size::B);
    }

    std::vector<int64_t> labels_;
    std::vector<Fixup> fixups_;
};

}  // namespace stym::ymio::m68k
