// STYM2149 — plugincore: named DMA banks
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/DmaBanks.h"

#include <algorithm>
#include <cmath>

#include "plugincore/Digidrum.h"
#include "plugincore/DigidrumBanks.h"
#include "plugincore/Json.h"

namespace stym::plugin {

namespace {

using ymengine::DmaSlot;

constexpr std::string_view kKind = "dmalib";

// One slot as a JSON object; the signed bytes are written as unsigned bytes in base64.
json::Value slotJson(const DmaSlot& s, std::span<const int8_t> bytes, const std::string& name) {
    json::Value o = json::Value::object();
    o.set("name", name);
    o.set("rate", ymengine::kDmaRateHz[std::min<size_t>(s.rate, ymengine::kDmaRates - 1)]);
    o.set("stereo", s.stereo ? 1 : 0);
    o.set("root", int(s.root));
    o.set("cents", s.cents);
    o.set("loop", s.loop ? 1 : 0);
    o.set("start", double(s.start));
    o.set("end", double(s.end));
    std::vector<uint8_t> raw(bytes.size());
    for (size_t i = 0; i < raw.size(); ++i) raw[i] = uint8_t(bytes[i]);
    o.set("data", base64Encode(raw));
    return o;
}

// Reads one slot over its default state: the rate is the nearest of the four, each value is brought back into its
// range, and Start never passes End.
void readSlot(const json::Value& o, DmaSlot& s, SharedSamples<int8_t>& bytes, std::string& name) {
    if (const json::Value* v = o.find("name"); v && v->isString()) name = cleanDigidrumName(v->string());
    auto num = [&](std::string_view k, double def) {
        const json::Value* v = o.find(k);
        const double x = v ? v->number(def) : def;
        return std::isfinite(x) ? x : def;
    };
    const double hz = num("rate", ymengine::kDmaRateHz[s.rate]);
    for (int k = 0; k < ymengine::kDmaRates; ++k)
        if (std::abs(ymengine::kDmaRateHz[size_t(k)] - hz) < std::abs(ymengine::kDmaRateHz[s.rate] - hz))
            s.rate = uint8_t(k);
    s.stereo = num("stereo", 0) != 0;
    s.root = uint8_t(std::clamp(std::lround(num("root", 60)), 0L, 127L));
    s.cents = std::clamp(num("cents", 0), -ymengine::kDmaCentsMax, ymengine::kDmaCentsMax);
    s.loop = num("loop", 0) != 0;
    s.end = uint32_t(std::clamp(std::floor(num("end", 0)), 0.0, double(UINT32_MAX)));
    s.start = uint32_t(std::clamp(std::floor(num("start", 0)), 0.0, double(s.end)));
    if (const json::Value* v = o.find("data"); v && v->isString()) {
        const std::vector<uint8_t> raw = base64Decode(v->string());
        bytes = std::vector<int8_t>(raw.begin(), raw.end());
    }
}

}  // namespace

DmaBank captureDmaBank(const std::string& name, const NonParamState& state) {
    DmaBank b;
    b.name = name;
    b.dma = state.dma;
    size_t n = 8;
    for (size_t k = 0; k < state.dmaSamples.size(); ++k)
        if (!state.dmaSamples[k].empty() || !state.dmaNames[k].empty() || !(state.dma.slots[k] == DmaSlot{}))
            n = std::max(n, k + 1);
    for (size_t k = n; k < b.dma.slots.size(); ++k) b.dma.slots[k] = DmaSlot{};
    b.samples.assign(state.dmaSamples.begin(), state.dmaSamples.begin() + std::ptrdiff_t(n));
    b.names.assign(state.dmaNames.begin(), state.dmaNames.begin() + std::ptrdiff_t(n));
    return b;
}

void applyDmaBank(const DmaBank& bank, NonParamState& state) {
    state.dma = bank.dma;
    for (size_t k = 0; k < state.dmaSamples.size(); ++k) {
        state.dmaSamples[k] = k < bank.samples.size() ? bank.samples[k] : SharedSamples<int8_t>{};
        state.dmaNames[k] = k < bank.names.size() ? bank.names[k] : std::string{};
    }
}

std::optional<std::vector<DmaBank>> parseDmaBanks(std::string_view text, std::string* error) {
    const std::optional<json::Value> doc = json::parse(text, error);
    if (!doc) return std::nullopt;
    const json::Value* kind = doc->isObject() ? doc->find("kind") : nullptr;
    const json::Value* banks = doc->isObject() ? doc->find("banks") : nullptr;
    if (!kind || !kind->isString() || kind->string() != kKind || !banks || !banks->isArray()) {
        if (error) *error = "not a DMA bank library";
        return std::nullopt;
    }
    std::vector<DmaBank> out;
    for (const json::Value& b : banks->items()) {
        if (!b.isObject()) continue;
        const json::Value* name = b.find("name");
        if (!name || !name->isString() || cleanDigidrumName(name->string()).empty()) continue;
        DmaBank bank;
        bank.name = cleanDigidrumName(name->string());
        // Eight slots at least; those past ymengine::kDmaSlotsMax are ignored.
        const json::Value* slots = b.find("slots");
        const size_t n =
            slots && slots->isArray() ? std::min<size_t>(slots->items().size(), ymengine::kDmaSlotsMax) : 0;
        bank.samples.resize(std::max<size_t>(n, 8));
        bank.names.resize(bank.samples.size());
        for (size_t k = 0; k < n; ++k)
            if (slots->items()[k].isObject())
                readSlot(slots->items()[k], bank.dma.slots[k], bank.samples[k], bank.names[k]);
        out.push_back(std::move(bank));
    }
    return out;
}

std::string dumpDmaBanks(const std::vector<DmaBank>& banks) {
    json::Value doc = json::Value::object();
    doc.set("kind", std::string(kKind));
    doc.set("version", 1);
    json::Value list = json::Value::array();
    for (const DmaBank& b : banks) {
        json::Value o = json::Value::object();
        o.set("name", b.name);
        json::Value slots = json::Value::array();
        for (size_t k = 0; k < b.samples.size(); ++k) slots.push(slotJson(b.dma.slots[k], b.samples[k], b.names[k]));
        o.set("slots", std::move(slots));
        list.push(std::move(o));
    }
    doc.set("banks", std::move(list));
    return json::dump(doc, 2);
}

}  // namespace stym::plugin
