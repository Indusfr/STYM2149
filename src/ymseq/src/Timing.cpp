// STYM2149 — ymseq: Ticks timing of the sequencer (speed, groove, equivalent tempo)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymseq/Timing.h"

#include <algorithm>
#include <charconv>

#include "ymseq/Tables.h"

namespace stym::ymseq {

namespace {
// Separators between the values of a groove, also trimmed around a typed speed: space, tab, comma, semicolon.
bool space(char c) noexcept { return c == ' ' || c == '\t' || c == ',' || c == ';'; }

// Whole text as a decimal integer (a leading minus allowed, at most 6 characters once trimmed); nothing otherwise.
std::optional<int> parseInt(std::string_view t) {
    while (!t.empty() && space(t.front())) t.remove_prefix(1);
    while (!t.empty() && space(t.back())) t.remove_suffix(1);
    if (t.empty() || t.size() > 6) return std::nullopt;
    int v = 0;
    const auto [end, ec] = std::from_chars(t.data(), t.data() + t.size(), v);
    if (ec != std::errc{} || end != t.data() + t.size()) return std::nullopt;
    return v;
}
}  // namespace

std::string grooveText(const GrooveValues& groove, int length) {
    std::string out;
    const int n = std::clamp(length, 1, ymengine::kGrooveSteps);
    for (int i = 0; i < n; ++i) {
        if (i) out += ' ';
        out += std::to_string(groove[size_t(i)]);
    }
    return out;
}

std::optional<int> parseSpeed(std::string_view text) {
    const auto v = parseInt(text);
    if (!v) return std::nullopt;
    return std::clamp(*v, 1, ymengine::kSpeedMax);
}

std::optional<Groove> parseGroove(std::string_view text) {
    Groove g;
    int n = 0;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && space(text[i])) ++i;
        if (i >= text.size()) break;
        size_t j = i;
        while (j < text.size() && !space(text[j])) ++j;
        const auto v = parseInt(text.substr(i, j - i));
        if (!v || n >= ymengine::kGrooveSteps) return std::nullopt;
        g.values[size_t(n++)] = uint8_t(std::clamp(*v, 0, ymengine::kGrooveMax));
        i = j;
    }
    g.length = std::max(n, 1);
    return g;
}

double ticksBpm(double tickHz, int speed, int resolution) noexcept {
    const double spb = kResolutions[size_t(std::clamp(resolution, 0, kResolutionCount - 1))].stepsPerBeat;
    return 60 * tickHz / (std::clamp(speed, 1, ymengine::kSpeedMax) * spb);
}

}  // namespace stym::ymseq
