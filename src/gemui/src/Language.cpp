// STYM2149 — gemui: interface language
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "gemui/Language.h"

#include <unordered_map>
#include <utility>

namespace stym::gemui {

namespace {

struct Hash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
};

// Texts translated so far, by English text; nodes do not move, so the views handed out stay valid.
std::unordered_map<std::string, std::string, Hash, std::equal_to<>> gCache;
constexpr size_t kCacheMax = 50000;  // values shown one after the other (a tempo being dragged) are cached too
UiLine gLine;
uint64_t gGeneration = 0;
std::function<void(std::string_view)> gSink;
int gRaw = 0;

bool hasLetter(std::string_view s) noexcept {
    for (const char c : s)
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return true;
    return false;
}

// One line: the spaces around it stay as they are (a label padded or followed by a space), the text between them is
// looked up.
void translateLine(std::string_view one, std::string& out) {
    const size_t a = one.find_first_not_of(' '), b = one.find_last_not_of(' ');
    const std::optional<std::string> t =
        a != std::string_view::npos && hasLetter(one) ? gLine(one.substr(a, b + 1 - a)) : std::nullopt;
    if (!t) {
        out += one;
        return;
    }
    out += one.substr(0, a);
    out += *t;
    out += one.substr(b + 1);
}

std::string translate(std::string_view english) {
    std::string out;
    out.reserve(english.size() + english.size() / 4);
    for (size_t from = 0;;) {
        const size_t nl = english.find('\n', from);
        translateLine(english.substr(from, nl == std::string_view::npos ? nl : nl - from), out);
        if (nl == std::string_view::npos) break;
        out += '\n';
        from = nl + 1;
    }
    return out;
}

}  // namespace

void setUiLanguage(UiLine line) {
    gLine = std::move(line);
    gCache.clear();
    ++gGeneration;
}

bool uiTranslated() noexcept { return static_cast<bool>(gLine); }
uint64_t uiLanguageGeneration() noexcept { return gGeneration; }

std::string_view uiText(std::string_view english) {
    if (gRaw > 0 || !hasLetter(english)) return english;
    if (gSink) gSink(english);
    if (!gLine) return english;
    if (const auto it = gCache.find(english); it != gCache.end()) return it->second;
    if (gCache.size() >= kCacheMax) gCache.clear();
    return gCache.emplace(std::string(english), translate(english)).first->second;
}

RawText::RawText(bool on) noexcept : on_(on) { gRaw += on ? 1 : 0; }
RawText::~RawText() { gRaw -= on_ ? 1 : 0; }
bool uiRaw() noexcept { return gRaw > 0; }

std::function<void(std::string_view)> recordUiTexts(std::function<void(std::string_view)> sink) {
    std::swap(gSink, sink);
    return sink;
}

}  // namespace stym::gemui
