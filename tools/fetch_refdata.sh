#!/usr/bin/env bash
# STYM2149 — fetches the reference tunes of the tests.
# Programming: Pascal "Indus" Nowak (with Claude's help).
# Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#
# Source: public collections of slippyex's ym2149-rs repository (MIT licence), pinned commit.
# The files are never versioned in STYM2149.
#
# usage: tools/fetch_refdata.sh [destination] [--full]
#   destination: output folder (default: refdata/ at the root of the repository)
#   --full     : adds the whole website/player/ym corpus (1,942 files, ~12 MB) in destination/corpus
set -euo pipefail

repo_url="https://github.com/slippyex/ym2149-rs.git"
commit="5e42228c0b00f76d40fd748e712b913cca8647f8"
root="$(cd "$(dirname "$0")/.." && pwd)"
dest="$root/refdata"
full=0
for arg in "$@"; do
  case "$arg" in
    --full) full=1 ;;
    *) dest="$arg" ;;
  esac
done

music="crates/bevy_ym2149_examples/assets/music"
files=(
  "$music/Decade Where's my willy.ym"
  "$music/DHS Intro.ym"
  "$music/Digizak.ym"
  "$music/Gritty.ym"
  "$music/Scout.ym"
  "$music/ST News 61.ym"
  "$music/Mon Tune 8.ym"
  "$music/Ashtray.ym"
  "$music/Credits.ym"
  "$music/Iceage (digi).ym"
  "$music/Prelude.ym"
  "$music/Steps.ym"
  "$music/ND-Toxygene.ym"
  "$music/LethalXcess1.ym"
  "$music/Synth Dream 5.ym"
  "$music/Wings2.ym"
  "$music/WingsLeveltune3.ym"
  "$music/WingsLeveltune5.ym"
  "$music/Amazine 2.ym"
  "website/player/ym/Hippel.Jochen (Mad Max)/Prehistoric Tale/Prehistoric Tale (title).ym"
  "website/player/ym/Misc.Games/Simpsons.ym"
  "website/player/ym/Misc.Composers/Huelsbeck.Chris/Jim Power/jimpowr5.ym"
)

# SNDH: played by AtariAudio's machine, compared with its SndhRenderer.
sndh_files=(
  "examples/sndh/Mad_Max/Bomber_(ST).sndh"
  "examples/sndh/Mad_Max/Bomber_(STe).sndh"
  "examples/sndh/Mad_Max/Car_Race.sndh"
  "examples/sndh/Mad_Max/Demos/Best_In_Galaxy-SIDVoice/Master_of_Magic.sndh"
  "examples/sndh/Mad_Max/Demos/Best_In_Galaxy-Digi/Sanxion.sndh"
  "examples/sndh/Jess/Buzzmosphere.sndh"
  "examples/sndh/Cube/Outpost.sndh"
  "crates/bevy_ym2149_examples/assets/sndh/Lethal_Xcess_(STe).sndh"
  "crates/bevy_ym2149_examples/assets/music/Wings_Of_Death.sndh"
  "crates/ym2149-wasm/examples/Intensity_200hz.sndh"
  "docs/505/Schwerlasttransport.sndh"
  "docs/tao/mOdulator.sndh"
)

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
git clone --quiet --filter=blob:none --no-checkout "$repo_url" "$work/ym2149-rs"
cd "$work/ym2149-rs"
if [ "$full" = 1 ]; then
  patterns=("/$music/" "/website/player/ym/")
else
  patterns=()
  for f in "${files[@]}"; do patterns+=("/$f"); done
fi
for f in "${sndh_files[@]}"; do patterns+=("/$f"); done
# Git Bash (Windows) would convert these patterns, which start with "/", into Windows paths. The conversion stays
# active elsewhere: git.exe needs Windows paths for the temporary folder.
MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' git sparse-checkout set --no-cone "${patterns[@]}"
git checkout --quiet "$commit"

mkdir -p "$dest/ym" "$dest/sndh"
for f in "${files[@]}"; do
  cp "$f" "$dest/ym/"
done
for f in "${sndh_files[@]}"; do
  cp "$f" "$dest/sndh/"
done
if [ "$full" = 1 ]; then
  rm -rf "$dest/corpus"
  cp -R website/player/ym "$dest/corpus"
  find "$dest/corpus" -type f ! -iname '*.ym' -delete
fi
printf 'ym2149-rs %s\n' "$commit" > "$dest/SOURCE.txt"
echo "Reference tunes in $dest ($(ls "$dest/ym" | wc -l | tr -d ' ') YM, $(ls "$dest/sndh" | wc -l | tr -d ' ') SNDH)"
