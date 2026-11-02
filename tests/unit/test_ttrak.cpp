// STYM2149 — ymio: TTrak modules (.TPM), instruments (.TPI) and 4-bit samples (.4BI) read and written, and played tick
// by tick by our model of TTrak's routine. Synthetic modules only: no file made by TTrak is in the sources.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <span>
#include <string>
#include <vector>

#include "ymio/TTrak.h"
#include "ymio/TTrakPlayer.h"

using namespace stym;
using ymio::TtrakCell;
using ymio::TtrakInstrument;
using ymio::TtrakModule;
using I = ymio::TtrakInstrument;

namespace {

std::span<const uint8_t> bytes(const std::vector<uint8_t>& b) { return {b.data(), b.size()}; }

// Instrument k (1-32) of m: amplitude envelope `steps`, one step per `speed + 1` ticks, looping on `loop`.
TtrakInstrument& instrument(TtrakModule& m, int k, const std::vector<uint8_t>& steps, int speed = 0, int loop = -1) {
    TtrakInstrument& r = m.instruments[size_t(k - 1)];
    for (size_t i = 0; i < steps.size(); ++i) r.at(I::kAmpEnv + int(i)) = steps[i];
    r.at(I::kAmpSpeed) = uint8_t(speed);
    r.at(I::kAmpLast) = uint8_t(steps.size() - 1);
    r.at(I::kAmpLoop) = uint8_t(loop < 0 ? int(steps.size()) - 1 : loop);
    return r;
}

int tone(const ymio::TtrakTick& t, int c) { return t.reg[size_t(2 * c)] | (t.reg[size_t(2 * c + 1)] & 15) << 8; }

}  // namespace

TEST_CASE("TTrak modules: a module written and read back is the same, as TTrak lays it out", "[ttrak]") {
    TtrakModule m = TtrakModule::blank();
    m.setTitle("Round trip");
    m.setAuthor("Indus");
    m.length = 3;
    m.restart = 1;
    m.rate = 100;
    m.groove[0] = 1;
    m.groove[5] = 2;
    m.order[0] = 0;
    m.order[1] = 2;
    m.order[2] = 1;
    m.order[3] = 2;  // position 3 = the song's length: TTrak saves the patterns it names too
    m.patterns.resize(3);
    m.patterns[1].rows[5][2] = {46, 3, 0x1C, 4};
    m.patterns[2].rows[63][0] = {1, 32, 0x0B, 0};
    m.patterns[2].spare[7] = {1, 2, 3, 4};
    instrument(m, 3, {0x0F, 0x0E, 0x4D}, 1).setName("Lead");
    m.instruments[31].at(I::kTimerType) = 7;
    m.samples[0] = ymio::ttrakSampleBytes(std::vector<uint8_t>{1, 2, 15, 8});
    m.samples[5] = ymio::ttrakSampleBytes(std::vector<uint8_t>{3});

    const std::vector<uint8_t> file = ymio::writeTtrakModule(m);
    CHECK(ymio::isTtrakModule(bytes(file)));
    CHECK(std::string(file.begin(), file.begin() + 8) == "GODMOD30");
    // Header, 32 instruments, 3 patterns, then the samples: 4 bytes, an empty slot's $80 for each of the 30 others, 1.
    CHECK(file.size() == 0x84C6 + 3 * 0x400 + 4 + 30 + 1);
    const ymio::TtrakResult<TtrakModule> back = ymio::parseTtrakModule(bytes(file));
    INFO(back.error);
    REQUIRE(back.value);
    const TtrakModule& b = *back.value;
    CHECK(b.titleText() == "Round trip");
    CHECK(b.authorText() == "Indus");
    CHECK(b.length == 3);
    CHECK(b.restart == 1);
    CHECK(b.rate == 100);
    CHECK(b.groove == m.groove);
    CHECK(b.order == m.order);
    CHECK(b.instruments == m.instruments);
    CHECK(b.instruments[2].name() == "Lead");
    REQUIRE(b.patterns.size() == 3);
    CHECK(b.patterns == m.patterns);
    for (size_t s = 0; s < b.samples.size(); ++s) CHECK(b.samples[s] == m.samples[s]);
    CHECK(ymio::writeTtrakModule(b) == file);
}

TEST_CASE("TTrak modules: the patterns that TTrak saves, by the order list and the song's length",
          "[ttrak]") {
    std::array<uint8_t, ymio::kTtrakPositions> order{};
    order[0] = 0;
    order[1] = 5;
    order[2] = 2;
    order[3] = 9;
    CHECK(ymio::ttrakPatternCount(order, 2) == 6);   // positions 0 to 2
    CHECK(ymio::ttrakPatternCount(order, 3) == 10);  // position 3 too
    order[4] = 0x90;                                 // a signed byte: never the highest
    CHECK(ymio::ttrakPatternCount(order, 4) == 10);
    CHECK(ymio::ttrakPatternCount(order, 0) == 1);
}

TEST_CASE("TTrak modules: other files, cut files and damaged headers are refused", "[ttrak]") {
    TtrakModule m = TtrakModule::blank();
    m.samples[0] = ymio::ttrakSampleBytes(std::vector<uint8_t>(100, 7));
    const std::vector<uint8_t> file = ymio::writeTtrakModule(m);
    auto error = [](const std::vector<uint8_t>& f) { return ymio::parseTtrakModule(bytes(f)).error; };
    CHECK(error({}) == "Not a TTrak module (.TPM).");
    CHECK(error({'G', 'O', 'D', 'I', 'N', 'S', '3', '0'}) == "Not a TTrak module (.TPM).");
    CHECK(error({'G', 'O', 'D', 'M', 'O', 'D', '3', '0'}) == "The TTrak module is cut.");
    // Cut anywhere: refused, never read past the end.
    for (size_t n = 8; n < file.size(); n += 97) {
        const std::vector<uint8_t> cut(file.begin(), file.begin() + long(n));
        CHECK_FALSE(ymio::parseTtrakModule(bytes(cut)).value);
    }
    std::vector<uint8_t> bad = file;
    bad[0x42] ^= 1;  // the offset of instrument 1
    CHECK(error(bad) == "Corrupt TTrak module header.");
    bad = file;
    bad[0x82 + 3] = 1;  // the first sample does not start at 0
    CHECK(error(bad) == "Corrupt TTrak sample table.");
    bad = file;
    bad[0x82 + 4 * 32 + 3] = 0xFF;  // the block of samples longer than the file
    CHECK(error(bad) == "The TTrak module is cut.");
}

TEST_CASE("TTrak instruments and samples: .TPI written and read back, 4-bit volumes with their end byte",
          "[ttrak]") {
    ymio::TtrakInstrumentFile f;
    f.instrument = TtrakInstrument::blank(1);
    f.instrument.at(I::kTimerType) = 7;
    f.instrument.setName("Kick");
    f.sample = ymio::ttrakSampleBytes(std::vector<uint8_t>{15, 12, 8, 4, 0});
    const std::vector<uint8_t> file = ymio::writeTtrakInstrument(f);
    CHECK(ymio::isTtrakInstrument(bytes(file)));
    CHECK_FALSE(ymio::isTtrakModule(bytes(file)));
    const ymio::TtrakResult<ymio::TtrakInstrumentFile> back = ymio::parseTtrakInstrument(bytes(file));
    REQUIRE(back.value);
    CHECK(back.value->instrument == f.instrument);
    CHECK(back.value->instrument.name() == "Kick");
    CHECK(back.value->sample == f.sample);
    // Without a sample: the single byte $80.
    f.sample.clear();
    const std::vector<uint8_t> none = ymio::writeTtrakInstrument(f);
    CHECK(none.back() == 0x80);
    CHECK(ymio::parseTtrakInstrument(bytes(none)).value->sample.empty());
    const std::vector<uint8_t> cut(file.begin(), file.begin() + 100);
    CHECK(ymio::parseTtrakInstrument(bytes(cut)).error == "The TTrak instrument is cut.");
    CHECK(ymio::parseTtrakInstrument(bytes(std::vector<uint8_t>{1, 2, 3})).error == "Not a TTrak instrument (.TPI).");

    // Samples: volumes 0-15, bit 7 on the last byte; the routine stops at the first byte with bit 7.
    CHECK(ymio::ttrakSampleBytes(std::vector<uint8_t>{1, 18, 15}) == std::vector<uint8_t>{1, 2, 0x8F});
    CHECK(ymio::ttrakSampleBytes(std::vector<uint8_t>{}).empty());
    CHECK(ymio::ttrakSampleLevels(std::vector<uint8_t>{0x31, 2, 0x8F, 5}) == std::vector<uint8_t>{1, 2, 15});
    CHECK(ymio::ttrakSampleLevels(std::vector<uint8_t>{0x80}).empty());
    CHECK(ymio::parseTtrak4bi(std::vector<uint8_t>{3, 4, 5}) == std::vector<uint8_t>{3, 4, 0x85});
    CHECK(ymio::parseTtrak4bi(std::vector<uint8_t>{}).empty());
}

TEST_CASE("TTrak routine: a note plays the square of its pitch at the instrument's volume, rows of speed + 1 ticks",
          "[ttrak]") {
    TtrakModule m = TtrakModule::blank();
    instrument(m, 1, {0x0F, 0x0C, 0x0A}, 1);  // 15, 12, 10, held: two ticks each
    m.patterns[0].rows[0][0] = {46, 1, 0x1C, 2};
    m.patterns[0].rows[1][0] = {0, 0, 0x1F, 3};  // the effect alone attenuates the playing note
    m.patterns[0].rows[2][1] = {58, 1, 0x1F, 5};
    ymio::TtrakPlayer p(m);
    const auto& period = ymio::TtrakPlayer::defaultTables().period;
    CHECK(period[0] == 4060);
    CHECK(period[736] == int(std::lround(ymio::kTtrakPeriod0 * std::pow(2.0, -736.0 / 192))));

    std::vector<ymio::TtrakTick> ticks;
    for (int i = 0; i < 12; ++i) ticks.push_back(p.tick());
    // Rows start every 3 ticks ($1C 2).
    for (int i = 0; i < 12; ++i) CHECK(ticks[size_t(i)].rowStart == (i % 3 == 0));
    CHECK(ticks[3].row == 1);
    CHECK(ticks[6].row == 2);
    // Channel A: note 46 (A4, pitch 736), square on, noise off, the envelope two ticks per step, 3 less from row 1.
    CHECK(tone(ticks[0], 0) == period[736]);
    CHECK((ticks[0].reg[7] & 0x09) == 0x08);
    const int volumes[] = {15, 15, 12, 9, 7, 7, 7};
    for (int i = 0; i < 7; ++i) CHECK(ticks[size_t(i)].reg[8] == volumes[i]);
    CHECK(ticks[0].ch[0].attack);
    CHECK(ticks[0].ch[0].noteRow);
    // Channel B: note 58 an octave higher, the attenuation $1F 5 taken off.
    CHECK(tone(ticks[6], 1) == period[58 * 16]);
    CHECK(ticks[6].reg[9] == 10);
    CHECK(ticks[5].reg[9] == 0);  // silent before its note
}

TEST_CASE("TTrak routine: pattern break, song length and restart position, groove", "[ttrak]") {
    TtrakModule m = TtrakModule::blank();
    instrument(m, 1, {0x0F});
    m.length = 2;
    m.restart = 1;
    m.order[0] = 0;
    m.order[1] = 1;
    m.patterns.resize(2);
    m.patterns[0].rows[0][0] = {37, 1, 0x1C, 0};  // one tick a row
    m.patterns[0].rows[3][2] = {0, 0, 0x0B, 0};   // break after row 3
    m.patterns[1].rows[0][0] = {49, 1, 0, 0};
    ymio::TtrakPlayer p(m);
    std::vector<ymio::TtrakTick> t;
    for (int i = 0; i < 4 + 64 + 3; ++i) t.push_back(p.tick());
    CHECK(t[3].position == 0);
    CHECK(t[3].row == 3);
    CHECK(t[4].position == 1);  // the break
    CHECK(t[4].row == 0);
    CHECK(p.loops() == 1);  // past the song's length: back to position 1
    CHECK(t[4 + 64].position == 1);
    CHECK(t[4 + 64].row == 0);

    // Groove: row r lasts speed + groove[r & 15] + 1 ticks (rows 0 and 16 four ticks, the others two).
    TtrakModule g = TtrakModule::blank();
    g.groove[0] = 2;
    g.patterns[0].rows[0][0] = {0, 0, 0x1C, 1};
    ymio::TtrakPlayer q(g);
    std::vector<int> starts;
    for (int i = 0; i < 40; ++i)
        if (q.tick().rowStart) starts.push_back(i);
    std::vector<int> expect{0};
    for (int r = 1; r <= 17; ++r) expect.push_back(expect.back() + (r == 1 || r == 17 ? 4 : 2));
    CHECK(starts == expect);
}

TEST_CASE("TTrak routine: noise table, buzzer and its shape, arpeggio, a linked instrument", "[ttrak]") {
    TtrakModule m = TtrakModule::blank();
    // 1: noise only, periods 5 then 10 held ($F1: back to entry 1).
    TtrakInstrument& drum = instrument(m, 1, {0x6F});
    const uint8_t noise[] = {0, 5, 10, 0xF1};
    for (int i = 0; i < 4; ++i) drum.at(I::kNoiseTable + i) = noise[i];
    // 2: buzzer (envelope 8), square off; the buzzer's period the square's / 16.
    TtrakInstrument& bass = instrument(m, 2, {0x30});
    bass.at(I::kBuzzerShape) = 8;
    // 3: square with an arpeggio 0, 12 held on the square, linked to 1 when its envelope reaches its last step.
    TtrakInstrument& lead = instrument(m, 3, {0x0F, 0x0F, 0x0F, 0x0F}, 0, 0);
    const uint8_t arp[] = {0, 0, 12, 0x81};
    for (int i = 0; i < 4; ++i) lead.at(I::kArpTable + i) = arp[i];
    lead.at(I::kMatrixArp) = 1;
    lead.at(I::kLink) = 1;
    m.patterns[0].rows[0][0] = {40, 1, 0x1C, 7};
    m.patterns[0].rows[0][1] = {34, 2, 0, 0};
    m.patterns[0].rows[0][2] = {46, 3, 0, 0};
    ymio::TtrakPlayer p(m);
    const auto& period = ymio::TtrakPlayer::defaultTables().period;
    const ymio::TtrakTick t0 = p.tick(), t1 = p.tick(), t2 = p.tick(), t3 = p.tick();
    // A: noise 5 then 10, square off.
    CHECK((t0.reg[7] & 0x09) == 0x01);
    CHECK(t0.reg[6] == 5);
    CHECK(t1.reg[6] == 10);
    CHECK(t2.reg[6] == 10);
    // B: the buzzer, its shape written at the note.
    CHECK(t0.reg[9] == 0x10);
    CHECK(t0.shapes == 1);
    CHECK(t0.reg[13] == 8);
    CHECK((t0.reg[11] | t0.reg[12] << 8) == ((period[34 * 16] >> 3) + 1) >> 1);
    // C: arpeggio on the square, then instrument 1 takes over at the last step of the envelope, its noise from 5.
    CHECK(tone(t0, 2) == period[46 * 16]);
    CHECK(tone(t1, 2) == period[58 * 16]);
    CHECK(tone(t2, 2) == period[58 * 16]);
    CHECK(t2.ch[2].instrument == 3);
    CHECK(t3.ch[2].instrument == 1);
    CHECK(t3.ch[2].attack);
    CHECK((t3.reg[7] & 0x24) == 0x04);
    CHECK(t3.reg[6] == 5);
}
