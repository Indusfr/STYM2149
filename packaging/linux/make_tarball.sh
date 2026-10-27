#!/bin/sh
# STYM2149 — Linux archive: the plugins, the standalone application, the tooltip languages,
# install.sh and the licences.
# Programming: Pascal "Indus" Nowak (with Claude's help).
# Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#
# Usage: make_tarball.sh <artefacts folder> <version> <output folder>
# The artefacts folder is the one JUCE builds (build-plugin/src/plugin/STYM2149_artefacts/Release). Prints the path of
# the archive, STYM2149-<version>-linux-<machine>.tar.gz.
set -eu

if [ $# -ne 3 ]; then
    echo "usage: $0 <artefacts folder> <version> <output folder>" >&2
    exit 2
fi
art=$1
version=$2
out=$3
root=$(cd "$(dirname "$0")/../.." && pwd)
name="STYM2149-$version-linux-$(uname -m)"

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
dir="$stage/$name"
mkdir -p "$dir/licenses" "$dir/docs" "$dir/icons" "$dir/Languages"
cp -R "$art/VST3/STYM2149.vst3" "$art/LV2/STYM2149.lv2" "$dir/"
cp "$art/CLAP/STYM2149.clap" "$art/Standalone/STYM2149" "$dir/"
cp "$root/packaging/linux/install.sh" "$dir/"
cp "$root"/packaging/icon/STYM2149-*.png "$dir/icons/"
cp "$root"/languages/*.po "$dir/Languages/"
cp "$root/README.md" "$root/README.fr.md" "$root/LICENSE" "$root/THIRD_PARTY.md" "$dir/"
cp "$root/docs/MANUAL.md" "$root/docs/MANUAL.fr.md" "$dir/docs/"
cp "$root"/third_party/licenses/* "$dir/licenses/"
chmod 755 "$dir/install.sh" "$dir/STYM2149"
# Symbol tables are not needed to run: about a fifth of each binary.
if command -v strip >/dev/null; then
    strip --strip-unneeded "$dir/STYM2149" "$dir/STYM2149.clap" "$dir/STYM2149.lv2/libSTYM2149.so" \
        "$dir/STYM2149.vst3/Contents/$(uname -m)-linux/STYM2149.so"
fi

mkdir -p "$out"
out=$(cd "$out" && pwd)
tar -C "$stage" -czf "$out/$name.tar.gz" --owner=0 --group=0 "$name"
echo "$out/$name.tar.gz"
