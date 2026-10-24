#!/usr/bin/env bash
# Rebuilds and runs the M0 trial §3.0. Prerequisites: git, cargo, g++ (C++20), python3 + numpy + scipy.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
ext="$here/ext"; mkdir -p "$ext"
[ -d "$ext/ym2149-rs" ] || git clone https://github.com/slippyex/ym2149-rs.git "$ext/ym2149-rs"
git -C "$ext/ym2149-rs" checkout -q 5e42228c0b00f76d40fd748e712b913cca8647f8
[ -d "$ext/AtariAudio" ] || git clone https://github.com/arnaud-carre/AtariAudio.git "$ext/AtariAudio"
git -C "$ext/AtariAudio" checkout -q 8471e8492a4cfe44c874352fcce97b9a8dfdeef8
sed -i.bak "s#path = \"[^\"]*ym2149-core\"#path = \"$ext/ym2149-rs/crates/ym2149-core\"#" "$here/ymffi/Cargo.toml"
(cd "$here/ymffi" && cargo build --release -q)
aa="$ext/AtariAudio/src"
python3 - "$aa/ym2149_tables.h" "$here/cpp/env_data.inc" <<'PY'
import re, sys
src = open(sys.argv[1]).read()
m = re.search(r's_envData\[10 \* 32 \* 4\] =\s*\{(.*?)\};', src, re.S)
v = [int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]+', m.group(1))]
open(sys.argv[2], 'w').write('static constexpr uint8_t ENV_DATA[1280] = {' + ','.join(map(str, v)) + '};\n')
PY
g++ -std=c++20 -O2 -I"$aa" -o "$here/trial" "$here/cpp/main.cpp" "$aa/ym2149c.cpp" "$here/ymffi/target/release/libymffi.a" -lpthread -ldl -lm
mkdir -p "$here/out"
"$here/trial" "$here/out" | tee "$here/out/report.txt"
python3 -I "$here/py/analyse.py" "$here/out" "$aa/ym2149_tables.h" | tee "$here/out/analyse.txt"
