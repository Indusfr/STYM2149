// STYM2149 — record of the fidelity measurements
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "Metrics.h"

#include <cstdio>
#include <cstdlib>

namespace stym::test {

std::string env(const char* name) {
#if defined(_MSC_VER)
    char* buf = nullptr;
    size_t len = 0;
    std::string v;
    if (_dupenv_s(&buf, &len, name) == 0 && buf) {
        v = buf;
        std::free(buf);
    }
    return v;
#else
    const char* v = std::getenv(name);
    return v ? v : "";
#endif
}

std::string fmt(double v, int precision) {
    char b[64];
    std::snprintf(b, sizeof b, "%.*g", precision, v);
    return b;
}

static void emit(const std::string& line) {
    std::printf("[METRIC] %s\n", line.c_str());
    const std::string path = env("STYM_METRICS");
    if (path.empty()) return;
    if (std::FILE* f = std::fopen(path.c_str(), "ab")) {
        std::fprintf(f, "%s\n", line.c_str());
        std::fclose(f);
    }
}

void note(const std::string& id, const std::string& what, const std::string& value) {
    emit(id + " | " + what + " | " + value + " | diagnostic | —");
}

void metric(const std::string& id, const std::string& what, const std::string& value, const std::string& limit, bool ok) {
    emit(id + " | " + what + " | " + value + " | " + limit + " | " + (ok ? "OK" : "FAIL"));
}

}  // namespace stym::test
