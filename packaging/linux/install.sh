#!/bin/sh
# STYM2149 — per-user install on Linux: VST3, LV2, CLAP, the standalone application and the tooltip
# languages.
# Programming: Pascal "Indus" Nowak (with Claude's help).
# Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#
# Usage: ./install.sh              copy the plugins to ~/.vst3, ~/.lv2 and ~/.clap, the application to ~/.local/bin,
#                                  with its menu entry and its icon in ~/.local/share, the tooltip languages
#                                  to ~/.local/share/STYM2149/Languages, where the plugins read them
#        ./install.sh --uninstall  remove what install.sh copied (the settings file of the user stays)
# No root access is needed and nothing outside the home folder is touched.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
vst3="$HOME/.vst3"
lv2="$HOME/.lv2"
clap="$HOME/.clap"
bin="$HOME/.local/bin"
share="${XDG_DATA_HOME:-$HOME/.local/share}"

case "${1:-}" in
--uninstall)
    rm -rf "$vst3/STYM2149.vst3" "$lv2/STYM2149.lv2"
    rm -f "$clap/STYM2149.clap" "$bin/STYM2149" "$share/applications/stym2149.desktop"
    rm -rf "$share/STYM2149/Languages"
    rmdir "$share/STYM2149" 2>/dev/null || true
    for size in 16 32 48 256; do rm -f "$share/icons/hicolor/${size}x$size/apps/stym2149.png"; done
    echo "STYM2149 removed."
    exit 0
    ;;
"") ;;
*)
    echo "usage: $0 [--uninstall]" >&2
    exit 2
    ;;
esac

for f in STYM2149.vst3 STYM2149.lv2 STYM2149.clap STYM2149 Languages; do
    if [ ! -e "$here/$f" ]; then
        echo "$f is missing next to install.sh: extract the whole archive first." >&2
        exit 1
    fi
done

mkdir -p "$vst3" "$lv2" "$clap" "$bin"
# A previous version is replaced as a whole, so that no file of it stays in the bundles.
rm -rf "$vst3/STYM2149.vst3" "$lv2/STYM2149.lv2"
cp -R "$here/STYM2149.vst3" "$vst3/"
cp -R "$here/STYM2149.lv2" "$lv2/"
cp "$here/STYM2149.clap" "$clap/"
cp "$here/STYM2149" "$bin/"
chmod 755 "$bin/STYM2149"
# Tooltip languages, replaced as a whole like the bundles; the user's own files stay in ~/.config/STYM2149.
rm -rf "$share/STYM2149/Languages"
mkdir -p "$share/STYM2149"
cp -R "$here/Languages" "$share/STYM2149/"
# Menu entry of the application, with its icon at the sizes drawn.
for size in 16 32 48 256; do
    if [ -e "$here/icons/STYM2149-$size.png" ]; then
        mkdir -p "$share/icons/hicolor/${size}x$size/apps"
        cp "$here/icons/STYM2149-$size.png" "$share/icons/hicolor/${size}x$size/apps/stym2149.png"
    fi
done
mkdir -p "$share/applications"
cat >"$share/applications/stym2149.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=STYM2149
Comment=YM2149F synthesizer
Exec="$bin/STYM2149"
Icon=stym2149
Terminal=false
Categories=AudioVideo;Audio;Music;
EOF

echo "STYM2149 installed:"
echo "  VST3        $vst3/STYM2149.vst3"
echo "  LV2         $lv2/STYM2149.lv2"
echo "  CLAP        $clap/STYM2149.clap"
echo "  Standalone  $bin/STYM2149"
echo "  Menu entry  $share/applications/stym2149.desktop"
echo "  Languages   $share/STYM2149/Languages"
case ":$PATH:" in
*":$bin:"*) ;;
*) echo "Note: $bin is not in PATH; start the application with its full path." ;;
esac
echo "Let the host rescan its plugins to find STYM2149."
