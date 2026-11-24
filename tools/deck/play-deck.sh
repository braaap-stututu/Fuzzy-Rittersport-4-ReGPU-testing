#!/bin/sh
# Starts the Deck build (deck-build/fm4.exe: the low-end work + PGO + the Steam Deck profile, DECK-RESULTS.md) through
# Proton, with the switches the launcher sets. Same saves, settings and caches as play.sh.
#
#   ./play.sh                                  game files in ./game
#   FM4_GAME_DIR=/path/to/disc ./play.sh       game files elsewhere
#   PROTON="/path/to/proton" ./play.sh         a specific Proton
#   REGPU_PERF=1 ./play.sh                     any REGPU_* / FM4_* / FM2_* variable passes through
#
# The log goes to ./out/play-fm4.log (Proton's lines) and ./fm4.log (the game's).
# Saves and settings: the Proton prefix's Documents/fm4 (~/.fm4-linux/compatdata/pfx/drive_c/users/steamuser/Documents/fm4),
# the same place the launcher (./launcher/FM4Launcher) uses.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
HOME_DIR=${FM4_LINUX_HOME:-$HOME/.fm4-linux}
COMPAT=${FM4_COMPATDATA:-$HOME_DIR/compatdata}
GAME=${FM4_GAME_DIR:-$HERE/game}

if [ -z "$PROTON" ]; then
    for dir in "$HOME/.local/share/Steam/steamapps/common" "$HOME/.steam/steam/steamapps/common" \
               "$HOME/.var/app/com.valvesoftware.Steam/data/Steam/steamapps/common"; do
        for name in "Proton - Experimental" "Proton 11.0" "Proton 10.0"; do
            [ -x "$dir/$name/proton" ] && { PROTON="$dir/$name/proton"; break 2; }
        done
    done
fi
[ -n "$PROTON" ] && [ -x "$PROTON" ] || { echo "play.sh: no Proton found; install Proton Experimental in Steam or set PROTON=/path/to/proton" >&2; exit 1; }
[ -f "$GAME/default.xex" ] || { echo "play.sh: no game files in $GAME: copy the extracted disc there (default.xex, Media, ...) or set FM4_GAME_DIR" >&2; exit 1; }

mkdir -p "$COMPAT" "$HERE/out"
cd "$HERE"
# As the launcher (launcher/FM4Launcher/Install.cs, Launch): settings from settings.ini, fences written when recorded,
# resolves up to 1024 pixels written back to memory (the road surface reads them), the live debug server on 127.0.0.1.
export FM2_NO_LAUNCHER=1
export REGPU_EARLY_FENCES=${REGPU_EARLY_FENCES:-1}
export REGPU_RESOLVE_WRITEBACK_MAX=${REGPU_RESOLVE_WRITEBACK_MAX:-1024}
export REGPU_DEBUG=${REGPU_DEBUG:-1}
# Proton's xalia (a UI helper that watches windows for controller navigation) isn't needed by the game, which reads
# pads itself: 1.7% of the CPU samples of a Steam Deck race. PROTON_USE_XALIA=1 brings it back.
export PROTON_USE_XALIA=${PROTON_USE_XALIA:-0}
# Wine's own d3dcompiler_47 can't compile the renderer's shaders: the Microsoft DLL beside fm4.exe instead.
STEAM_COMPAT_CLIENT_INSTALL_PATH=${STEAM_COMPAT_CLIENT_INSTALL_PATH:-$HOME/.steam/steam} \
STEAM_COMPAT_DATA_PATH=$COMPAT \
WINEDLLOVERRIDES="d3dcompiler_47=n${WINEDLLOVERRIDES:+;$WINEDLLOVERRIDES}" \
    "$PROTON" run "$HERE/deck-build/fm4.exe" "$GAME" "$@" > "$HERE/out/play-fm4.log" 2>&1
