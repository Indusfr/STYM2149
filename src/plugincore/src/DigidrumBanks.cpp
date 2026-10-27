// STYM2149 — plugincore: named Digidrum banks
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/DigidrumBanks.h"

#include <algorithm>
#include <cmath>

#include "plugincore/Json.h"
#include "ymengine/Engine.h"

namespace stym::plugin {

namespace {

constexpr std::string_view kKind = "digidrumlib";
constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr char kHex[] = "0123456789ABCDEF";

// One slot as a JSON object; the 8-bit source is written as unsigned bytes in base64.
json::Value slotJson(const DigidrumSlotState& s, const std::vector<uint8_t>& nibbles) {
    json::Value o = json::Value::object();
    o.set("name", s.name);
    o.set("rate", s.play.rateHz);
    o.set("root", int(s.play.root));
    o.set("cents", s.play.cents);
    o.set("loop", s.play.loop ? 1 : 0);
    o.set("loopStart", double(s.play.start));
    o.set("loopEnd", double(s.play.end));
    o.set("convert", s.convert == DigiConvert::YmLevels ? "ym" : "linear");
    std::string hex(nibbles.size(), '0');
    for (size_t i = 0; i < nibbles.size(); ++i) hex[i] = kHex[nibbles[i] & 15];
    o.set("data", std::move(hex));
    std::vector<uint8_t> raw(s.source.size());
    for (size_t i = 0; i < raw.size(); ++i) raw[i] = uint8_t(s.source[i]);
    o.set("source", base64Encode(raw));
    return o;
}

// Reads one slot over its default state: each value is brought back into its range, and the loop start never passes
// the loop end.
void readSlot(const json::Value& o, DigidrumSlotState& s, std::vector<uint8_t>& nibbles) {
    using namespace ymengine;
    if (const json::Value* v = o.find("name"); v && v->isString()) s.name = cleanDigidrumName(v->string());
    auto num = [&](std::string_view k, double def) {
        const json::Value* v = o.find(k);
        const double x = v ? v->number(def) : def;
        return std::isfinite(x) ? x : def;
    };
    s.play.rateHz = std::clamp(num("rate", kDigidrumRate), kDigidrumRateMin, kDigidrumRateMax);
    s.play.root = uint8_t(std::clamp(std::lround(num("root", 60)), 0L, 127L));
    s.play.cents = std::clamp(num("cents", 0), -kDigidrumCentsMax, kDigidrumCentsMax);
    s.play.loop = num("loop", 0) != 0;
    const double maxLen = double(Engine::kMaxUserSample);
    s.play.end = uint32_t(std::clamp(std::floor(num("loopEnd", 0)), 0.0, maxLen));
    s.play.start = uint32_t(std::clamp(std::floor(num("loopStart", 0)), 0.0, double(s.play.end)));
    if (const json::Value* v = o.find("convert"); v && v->isString())
        s.convert = v->string() == "ym" ? DigiConvert::YmLevels : DigiConvert::Linear;
    if (const json::Value* v = o.find("data"); v && v->isString()) {
        // One value per hexadecimal digit; any other character is skipped.
        for (char c : v->string()) {
            if (nibbles.size() >= Engine::kMaxUserSample) break;
            if (c >= '0' && c <= '9') nibbles.push_back(uint8_t(c - '0'));
            else if (c >= 'A' && c <= 'F') nibbles.push_back(uint8_t(c - 'A' + 10));
            else if (c >= 'a' && c <= 'f') nibbles.push_back(uint8_t(c - 'a' + 10));
        }
    }
    if (const json::Value* v = o.find("source"); v && v->isString()) {
        std::vector<uint8_t> raw = base64Decode(v->string());
        if (raw.size() > Engine::kMaxUserSample) raw.resize(Engine::kMaxUserSample);
        s.source.assign(raw.begin(), raw.end());
    }
}

}  // namespace

DigidrumBank captureBank(const std::string& name, const NonParamState& state) {
    DigidrumBank b;
    b.name = name;
    b.slots = state.digiSlots;
    b.nibbles = state.digidrums;
    return b;
}

void applyBank(const DigidrumBank& bank, NonParamState& state) {
    state.digiSlots = bank.slots;
    state.digidrums = bank.nibbles;
}

std::optional<std::vector<DigidrumBank>> parseDigidrumBanks(std::string_view text, std::string* error) {
    const std::optional<json::Value> doc = json::parse(text, error);
    if (!doc) return std::nullopt;
    const json::Value* kind = doc->isObject() ? doc->find("kind") : nullptr;
    const json::Value* banks = doc->isObject() ? doc->find("banks") : nullptr;
    if (!kind || !kind->isString() || kind->string() != kKind || !banks || !banks->isArray()) {
        if (error) *error = "not a Digidrum bank library";
        return std::nullopt;
    }
    std::vector<DigidrumBank> out;
    for (const json::Value& b : banks->items()) {
        if (!b.isObject()) continue;
        const json::Value* name = b.find("name");
        if (!name || !name->isString() || cleanDigidrumName(name->string()).empty()) continue;
        DigidrumBank bank;
        bank.name = cleanDigidrumName(name->string());
        // Slots missing from the file stay empty; slots beyond the eighth are ignored.
        if (const json::Value* slots = b.find("slots"); slots && slots->isArray())
            for (size_t k = 0; k < slots->items().size() && k < bank.slots.size(); ++k)
                if (slots->items()[k].isObject()) readSlot(slots->items()[k], bank.slots[k], bank.nibbles[k]);
        out.push_back(std::move(bank));
    }
    return out;
}

std::string dumpDigidrumBanks(const std::vector<DigidrumBank>& banks) {
    json::Value doc = json::Value::object();
    doc.set("kind", std::string(kKind));
    doc.set("version", 1);
    json::Value list = json::Value::array();
    for (const DigidrumBank& b : banks) {
        json::Value o = json::Value::object();
        o.set("name", b.name);
        json::Value slots = json::Value::array();
        for (size_t k = 0; k < b.slots.size(); ++k) slots.push(slotJson(b.slots[k], b.nibbles[k]));
        o.set("slots", std::move(slots));
        list.push(std::move(o));
    }
    doc.set("banks", std::move(list));
    return json::dump(doc, 2);
}

std::string base64Encode(std::span<const uint8_t> bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    // Three bytes give four characters; a short last group is padded with '='.
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const uint32_t n = uint32_t(bytes[i]) << 16 | (i + 1 < bytes.size() ? uint32_t(bytes[i + 1]) << 8 : 0) |
                           (i + 2 < bytes.size() ? uint32_t(bytes[i + 2]) : 0);
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += i + 1 < bytes.size() ? kAlphabet[(n >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? kAlphabet[n & 63] : '=';
    }
    return out;
}

std::vector<uint8_t> base64Decode(std::string_view text) {
    std::vector<uint8_t> out;
    uint32_t acc = 0;
    int bits = 0;
    for (char c : text) {
        const char* p = std::find(kAlphabet, kAlphabet + 64, c);
        if (p == kAlphabet + 64) continue;  // padding, whitespace, unknown characters
        // Only the low bits of the accumulator matter: bits already read are shifted out of the 32-bit word.
        acc = acc << 6 | uint32_t(p - kAlphabet);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(uint8_t((acc >> bits) & 0xFF));
        }
    }
    return out;
}

}  // namespace stym::plugin
