#!/bin/sh
# Run a prepared CRStudio install under Wine in its own prefix.
#   ./run.sh /path/to/CRStudio
# Prefix: $WINEPREFIX or ~/.local/share/crscan-lizard-wine/prefix.
# Logs: crstudio.log and ctrl_bridge.log in the app dir.
# Also runs tools/lizard_ctrl_bridge.py, which applies CRStudio's camera
# settings (Brightness/Sensitivity) to V4L2 — Wine's DirectShow capture ignores them.
REPO="$(cd "$(dirname "$0")" && pwd)"
APP="$(cd "${1:?usage: run.sh /path/to/CRStudio}" && pwd)"
export WINEPREFIX="${WINEPREFIX:-$HOME/.local/share/crscan-lizard-wine/prefix}"
export WINEDEBUG="${WINEDEBUG:-err+all,fixme-all}"
# Use Microsoft's OpenMP runtime shipped with CRStudio instead of Wine's builtin
# vcomp140: with the builtin one the Poisson mesher (meshrecon.dll) loses most
# of the surface ("idx=-1, the index is out of boundary") and meshes come out empty.
export WINEDLLOVERRIDES="vcomp140=n,b${WINEDLLOVERRIDES:+;$WINEDLLOVERRIDES}"
mkdir -p "$WINEPREFIX"
# keep the previous session's log for diagnosing
[ -f "$APP/crstudio.log" ] && mv -f "$APP/crstudio.log" "$APP/crstudio.log.1"
pkill -f "tools/lizard_ctrl_bridge.py" 2>/dev/null
python3 -I "$REPO/tools/lizard_ctrl_bridge.py" > "$APP/ctrl_bridge.log" 2>&1 &
BRIDGE=$!
trap 'kill $BRIDGE 2>/dev/null' EXIT
cd "$APP"
wine CRStudio.exe > "$APP/crstudio.log" 2>&1
