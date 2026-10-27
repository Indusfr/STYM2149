// STYM2149 — ym2wav: renders a YM file to WAV with ymcore, timer effects included.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ReadFile.h"
#include "ymcore/ChipRenderer.h"
#include "ymio/YmFile.h"
#include "ymio/YmPlayer.h"

using namespace stym;

namespace {
void usage() {
    std::fprintf(stderr,
                 "usage: ym2wav input.ym output.wav [options]\n"
                 "  --rate N            output rate (48000)\n"
                 "  --mode M            raw | rawbox | retro (raw)\n"
                 "  --noise N           maximal | atariaudio (maximal)\n"
                 "  --timing T          exact | atariaudio (exact)\n"
                 "  --fx F              timer effects: exact | atariaudio (exact)\n"
                 "  --jitter C          interrupt jitter in 68000 cycles, exact effects (0)\n"
                 "  --seed S            jitter seed (1)\n"
                 "  --dc D              average | none (average, raw chain only)\n"
                 "  --chain C           board filters: raw | st | ste (raw)\n"
                 "  --hpf F             coupling high-pass of the st and ste chains, Hz (13.7; 0 = none)\n"
                 "  --bass G            LMC1992 bass (ste), -12 to 12 dB in steps of 2 (0)\n"
                 "  --treble G          LMC1992 treble (ste), -12 to 12 dB in steps of 2 (0)\n"
                 "  --seconds S         duration (one pass of the tune by default)\n"
                 "  --float             32-bit float WAV (16-bit integer by default)\n");
}

void le16(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}
void le32(std::vector<uint8_t>& b, uint32_t v) {
    le16(b, v & 0xffff);
    le16(b, v >> 16);
}
void tag(std::vector<uint8_t>& b, const char* t) { b.insert(b.end(), t, t + 4); }

// Mono RIFF/WAVE header. Format 1 (16-bit PCM) or 3 (32-bit float, with a "fact" chunk).
std::vector<uint8_t> wavHeader(uint32_t rate, uint32_t frames, bool isFloat) {
    const uint32_t bytesPerSample = isFloat ? 4 : 2;
    const uint32_t dataBytes = frames * bytesPerSample;
    const uint32_t fmtBytes = isFloat ? 18 : 16;
    const uint32_t factBytes = isFloat ? 12 : 0;
    std::vector<uint8_t> h;
    tag(h, "RIFF");
    le32(h, 4 + (8 + fmtBytes) + factBytes + (8 + dataBytes));
    tag(h, "WAVE");
    tag(h, "fmt ");
    le32(h, fmtBytes);
    le16(h, isFloat ? 3 : 1);
    le16(h, 1);
    le32(h, rate);
    le32(h, rate * bytesPerSample);
    le16(h, uint16_t(bytesPerSample));
    le16(h, uint16_t(bytesPerSample * 8));
    if (isFloat) {
        le16(h, 0);
        tag(h, "fact");
        le32(h, 4);
        le32(h, frames);
    }
    tag(h, "data");
    le32(h, dataBytes);
    return h;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        usage();
        return 2;
    }
    ymcore::RenderConfig rc;
    ymio::YmPlayerOptions options;
    double seconds = 0;
    bool isFloat = false;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        const char* v = i + 1 < argc ? argv[i + 1] : "";
        if (a == "--rate") rc.hostRate = uint32_t(std::strtoul(v, nullptr, 10)), ++i;
        else if (a == "--mode") {
            const std::string m = v;
            rc.mode = m == "rawbox" ? ymcore::OutputMode::RawBox : m == "retro" ? ymcore::OutputMode::Retro : ymcore::OutputMode::Raw;
            ++i;
        } else if (a == "--noise") rc.chip.noise = std::string(v) == "atariaudio" ? ymcore::NoiseLfsr::AtariAudio : ymcore::NoiseLfsr::Maximal, ++i;
        else if (a == "--timing") options.timing = std::string(v) == "atariaudio" ? ymio::YmTiming::AtariAudio : ymio::YmTiming::Exact, ++i;
        else if (a == "--fx") options.fx = std::string(v) == "atariaudio" ? ymio::YmFxModel::AtariAudio : ymio::YmFxModel::Exact, ++i;
        else if (a == "--jitter") options.irqJitterCpuCycles = uint32_t(std::strtoul(v, nullptr, 10)), ++i;
        else if (a == "--seed") options.jitterSeed = std::strtoull(v, nullptr, 10), ++i;
        else if (a == "--chain") {
            const std::string c = v;
            rc.board.chain = c == "st" ? ymcore::BoardChain::ST : c == "ste" ? ymcore::BoardChain::STE : ymcore::BoardChain::Raw;
            ++i;
        } else if (a == "--hpf") rc.board.highPassHz = std::strtod(v, nullptr), ++i;
        else if (a == "--bass") rc.board.bassDb = int(std::strtol(v, nullptr, 10)), ++i;
        else if (a == "--treble") rc.board.trebleDb = int(std::strtol(v, nullptr, 10)), ++i;
        else if (a == "--dc") rc.dc = std::string(v) == "none" ? ymcore::DcFilter::None : ymcore::DcFilter::MovingAverage, ++i;
        else if (a == "--seconds") seconds = std::strtod(v, nullptr), ++i;
        else if (a == "--float") isFloat = true;
        else {
            usage();
            return 2;
        }
    }
    if (rc.hostRate < 8000 || rc.hostRate > 384000) {
        std::fprintf(stderr, "rate out of range (8000-384000)\n");
        return 2;
    }

    const std::vector<uint8_t> raw = tools::readFile(argv[1]);
    if (raw.empty()) {
        std::fprintf(stderr, "cannot read: %s\n", argv[1]);
        return 1;
    }
    const ymio::YmParseResult parsed = ymio::parseYm(raw);
    if (!parsed.file) {
        std::fprintf(stderr, "%s: %s\n", argv[1], parsed.error.c_str());
        return 1;
    }
    const ymio::YmFile& ym = *parsed.file;
    rc.chip.clock = ym.clock;

    ymcore::ChipRenderer renderer;
    renderer.prepare(rc);
    ymio::YmPlayer player(ym, options, rc.hostRate);
    const uint64_t total = seconds > 0 ? uint64_t(seconds * rc.hostRate) : player.songLengthSamples();

    std::FILE* out = std::fopen(argv[2], "wb");
    if (!out) {
        std::fprintf(stderr, "cannot write: %s\n", argv[2]);
        return 1;
    }
    const std::vector<uint8_t> header = wavHeader(rc.hostRate, uint32_t(total), isFloat);
    std::fwrite(header.data(), 1, header.size(), out);
    std::vector<float> buf(4096);
    std::vector<int16_t> pcm(4096);
    uint64_t clipped = 0;
    for (uint64_t done = 0; done < total;) {
        const uint32_t n = uint32_t(std::min<uint64_t>(buf.size(), total - done));
        player.render(renderer, buf.data(), n);
        if (isFloat) {
            std::fwrite(buf.data(), sizeof(float), n, out);
        } else {
            for (uint32_t i = 0; i < n; ++i) {
                const double s = std::floor(double(buf[i]) * 32768.0 + 0.5);
                if (s > 32767 || s < -32768) ++clipped;
                pcm[i] = int16_t(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
            }
            for (uint32_t i = 0; i < n; ++i) {  // little-endian whatever the host
                const uint8_t b[2] = {uint8_t(pcm[i]), uint8_t(uint16_t(pcm[i]) >> 8)};
                std::fwrite(b, 1, 2, out);
            }
        }
        done += n;
    }
    std::fclose(out);
    std::printf("%s: %s, %u frames at %u Hz, clock %.2f Hz, loop %u, \"%s\" by \"%s\"\n", argv[1],
                ymio::formatName(ym.format), ym.frameCount, ym.playerRate, ym.clock.hz(), ym.loopFrame, ym.title.c_str(),
                ym.author.c_str());
    std::printf("%llu samples at %u Hz, latency %u, clipped: %llu\n", (unsigned long long)total, rc.hostRate,
                renderer.latencySamples(), (unsigned long long)clipped);
    std::printf("timer effects: %llu frames, %llu interrupts, %llu Sinus-SID frames not played\n",
                (unsigned long long)player.effectFrames(), (unsigned long long)player.timerInterrupts(),
                (unsigned long long)player.ignoredEffectFrames());
    return 0;
}
