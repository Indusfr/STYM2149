// STYM2149 — record of the fidelity measurements (M1 report)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#pragma once
#include <string>

namespace stym::test {

// Prints a measurement "[METRIC] threshold | subject | value | limit | OK/FAIL" and appends it to the file named by
// the STYM_METRICS environment variable if it exists.
void metric(const std::string& id, const std::string& what, const std::string& value, const std::string& limit, bool ok);
// Diagnostic measurement, without a threshold.
void note(const std::string& id, const std::string& what, const std::string& value);
std::string fmt(double v, int precision = 4);

// Environment variable, or empty string.
std::string env(const char* name);

}  // namespace stym::test
