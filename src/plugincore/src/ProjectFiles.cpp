// STYM2149 — plugincore: binary files of the File menu (preset, pattern, Song, kit) and the naming rules of the
// Patches library
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/ProjectFiles.h"

#include <cctype>
#include <vector>

#include "plugincore/Json.h"

namespace stym::plugin {

using json::Value;

std::string ensureExtension(std::string_view name, std::initializer_list<std::string_view> extensions) {
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.remove_prefix(1);
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.remove_suffix(1);
    if (name.empty()) return {};
    std::string n(name);
    for (std::string_view e : extensions) {
        if (n.size() < e.size()) continue;
        std::string tail = n.substr(n.size() - e.size());
        for (char& c : tail) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (tail == e) return n;
    }
    return extensions.size() ? n + std::string(*extensions.begin()) : n;
}

std::string ensureJson(std::string_view name) { return ensureExtension(name, {".json"}); }

void markStymFile(Value& doc) {
    Value stym = Value::object();
    stym.set("v", Value(kStymFileVersion));
    doc.set("stym", std::move(stym));
}

bool newerStymFile(const Value& doc) noexcept {
    const Value* stym = doc.isObject() ? doc.find("stym") : nullptr;
    if (!stym || !stym->isObject()) return false;
    const Value* v = stym->find("v");
    return v && !v->isNull() && v->number(0) > kStymFileVersion;
}

void applySongFile(const SongFile& f, ymseq::Project& p, bool& songOn) {
    p.song = f.song;
    songOn = f.songOn;
    if (!f.hasPatterns) return;
    const int steps = p.steps();
    for (int i = 0; i < ymseq::kPatterns; ++i) {
        const std::optional<ymseq::Pattern>& pat = f.patterns[size_t(i)];
        p.patterns[size_t(i)] = pat ? *pat : ymseq::Pattern{};
        p.used.set(size_t(i), pat.has_value());
    }
    // loadPattern(PATS[curPat] || emptyPattern()): a missing pattern becomes empty, of the current length.
    if (!f.patterns[size_t(p.current)]) p.cur().steps = steps;
    p.used.set(size_t(p.current));
    // A pending switch to a pattern that no longer exists is dropped.
    if (p.pending >= 0 && !p.used[size_t(p.pending)]) p.pending = -1;
}

namespace {

StateKind stateKind(FileKind k) noexcept {
    switch (k) {
    case FileKind::Pattern: return StateKind::Pattern;
    case FileKind::Song: return StateKind::Song;
    case FileKind::Kit: return StateKind::Kit;
    case FileKind::Preset:
    case FileKind::Patches: break;
    }
    return StateKind::State;
}

}  // namespace

std::string_view nativeExtension(FileKind k) noexcept {
    switch (k) {
    case FileKind::Pattern: return ".stympat";
    case FileKind::Song: return ".stymsng";
    case FileKind::Kit: return ".stymkit";
    case FileKind::Preset:
    case FileKind::Patches: break;
    }
    return ".stym";
}

std::string nativeFileName(FileKind k) { return "stym2149" + std::string(nativeExtension(k)); }

std::vector<uint8_t> encodeNativeFile(FileKind k, const ParamValues& values, const NonParamState& extras) {
    PluginState st;
    switch (k) {
    case FileKind::Preset:
    case FileKind::Patches:
        st.values = values;
        st.extras = extras;
        break;
    case FileKind::Pattern:  // the current pattern alone, as pattern 0
        st.extras.seq.patterns[0] = extras.seq.cur();
        st.extras.seq.used.reset();
        st.extras.seq.used.set(0);
        break;
    case FileKind::Song:
        st.extras.global = extras.global;
        st.extras.seq = extras.seq;
        break;
    case FileKind::Kit: st.extras.kit = extras.kit; break;
    }
    return encodeState(st, stateKind(k), false);
}

bool isNativeFile(std::string_view bytes) noexcept { return bytes.substr(0, 4) == "STYM"; }

NativeRead readNativeFile(std::string_view bytes) {
    NativeRead r;
    DecodeResult d = decodeState({reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()});
    if (!d.ok) {
        r.newer = d.version > kStateVersion;
        return r;
    }
    r.ok = true;
    NonParamState& x = d.state.extras;
    switch (d.kind) {
    case StateKind::State:
        r.kind = FileKind::Preset;
        r.values = d.state.values;
        r.extras = std::move(x);
        break;
    case StateKind::Pattern:
        r.kind = FileKind::Pattern;
        r.pattern = x.seq.patterns[0];
        break;
    case StateKind::Song:
        r.kind = FileKind::Song;
        r.song.song = x.seq.song;
        r.song.songOn = x.global.songMode;
        r.song.hasPatterns = true;
        for (int i = 0; i < ymseq::kPatterns; ++i)
            if (x.seq.used[size_t(i)]) r.song.patterns[size_t(i)] = x.seq.patterns[size_t(i)];
        break;
    case StateKind::Kit:
        r.kind = FileKind::Kit;
        r.kit = x.kit;
        break;
    }
    return r;
}

}  // namespace stym::plugin
