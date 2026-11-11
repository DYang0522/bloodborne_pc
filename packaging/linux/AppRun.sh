#!/usr/bin/env bash
set -euo pipefail
here=${APPDIR:-$(dirname -- "$(readlink -f -- "$0")")}
share="$here/usr/share/bbport"
export LD_LIBRARY_PATH="$here/usr/lib:$here/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export ALSA_PLUGIN_DIR="$share/lib/alsa-lib${ALSA_PLUGIN_DIR:+:$ALSA_PLUGIN_DIR}"
if [[ ${EUID:-0} -eq 0 ]]; then
    printf '%s\n' 'bbport: warning: running as root cannot use the logged-in desktop audio session; launch without sudo.' >&2
fi
if [[ -z ${XDG_RUNTIME_DIR:-} || ! -d "$XDG_RUNTIME_DIR" ]]; then
    printf '%s\n' 'bbport: warning: XDG_RUNTIME_DIR is missing; desktop audio services may be unavailable.' >&2
elif [[ -S "$XDG_RUNTIME_DIR/pipewire-0" ]]; then
    export BB_ALSA_CONFIG_DIR="$share/etc/alsa/conf.d"
    export ALSA_CONFIG_PATH="$share/alsa/alsa.conf"
fi
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
