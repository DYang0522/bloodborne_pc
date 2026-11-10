#!/usr/bin/env bash
set -euo pipefail
here=${APPDIR:-$(dirname -- "$(readlink -f -- "$0")")}
share="$here/usr/share/bbport"
export LD_LIBRARY_PATH="$here/usr/lib:$here/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PYTHONHOME="$here/usr"
export PYTHONPATH="$here/usr/lib/python3/dist-packages:$share/launcher:$share/scripts"
export PYTHON="$here/usr/bin/python3"
export GI_TYPELIB_PATH="$here/usr/lib/girepository-1.0"
export GSETTINGS_SCHEMA_DIR="$here/usr/share/glib-2.0/schemas"
export XDG_DATA_DIRS="$here/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export BB_PREBUILT=1
export BB_DATA_DIR=${BB_DATA_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/bbport}
mkdir -p "$BB_DATA_DIR"
cd -- "$share"
exec "$PYTHON" "$share/launcher/bbport_vulkan.py" "$PYTHON" "$share/launcher/bbport_launcher.py" "$@"
