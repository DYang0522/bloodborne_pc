#!/usr/bin/env bash
# AppDir + recursive ldd + appimagetool, following the working GoW3 package.
# GTK/Python are bundled. glibc, Vulkan, GPU drivers and display libraries stay host-provided.
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
[[ -f out/bb-probe && -f out/gpu/libbbgpu.so ]] || { echo 'Build first: bash build.sh' >&2; exit 1; }

appdir="$PWD/out/AppDir"
share="$appdir/usr/share/bbport"
libdir="$appdir/usr/lib"
pyver=3.12
py=/usr/bin/python$pyver
[[ -x $py ]] || { echo "Need $py (Ubuntu 24.04 Python)." >&2; exit 1; }
for path in /usr/lib/python$pyver /usr/lib/python3/dist-packages/gi /usr/lib/python3/dist-packages/PIL /usr/lib/x86_64-linux-gnu/girepository-1.0; do
    [[ -e $path ]] || { echo "Missing GTK/Python runtime input: $path" >&2; exit 1; }
done

rm -rf "$appdir"
mkdir -p "$share/bin/gpu" "$share/launcher" "$share/scripts" "$libdir" "$appdir/usr/bin" "$appdir/usr/lib/python$pyver" "$appdir/usr/lib/python3/dist-packages"
install -m755 out/bb-probe out/bb-gpu-capabilities "$share/bin/"
install -m755 out/gpu/libbbgpu.so "$share/bin/gpu/"
cp run.sh run.py "$share/"
cp -a scripts patches launcher "$share/"
mkdir -p "$share/tools"
install -m644 tools/legacy-cpu-plan.json "$share/tools/legacy-cpu-plan.json"
find "$share" -type d -name __pycache__ -prune -exec rm -rf {} +
find "$share" -type f -name '*.pyc' -delete

# CPython + the only third-party Python packages used at runtime.
install -m755 "$(readlink -f "$py")" "$appdir/usr/bin/python3"
cp -a "/usr/lib/python$pyver/." "$appdir/usr/lib/python$pyver/"
cp -a /usr/lib/python3/dist-packages/gi "$appdir/usr/lib/python3/dist-packages/"
cp -a /usr/lib/python3/dist-packages/PIL "$appdir/usr/lib/python3/dist-packages/"
mkdir -p "$libdir/girepository-1.0"
cp -a /usr/lib/x86_64-linux-gnu/girepository-1.0/. "$libdir/girepository-1.0/"

# GTK resources, icon themes and GSettings schemas.
for item in gtk-4.0 glib-2.0/icons glib-2.0/schemas icons/Adwaita icons/hicolor themes/Adwaita; do
    if [[ -e "/usr/share/$item" ]]; then
        mkdir -p "$appdir/usr/share/$(dirname "$item")"
        cp -a "/usr/share/$item" "$appdir/usr/share/$item"
    fi
done
if [[ -d /usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0 ]]; then
    mkdir -p "$libdir/gdk-pixbuf-2.0"
    cp -a /usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/. "$libdir/gdk-pixbuf-2.0/"
fi
install -m644 launcher/bloodborne.png "$appdir/bbport.png"
install -m644 packaging/linux/bbport.desktop "$appdir/bbport.desktop"

# GTK libraries are loaded by GI typelibs, not necessarily by DT_NEEDED.
for pattern in 'libgtk-4.so*' 'libadwaita-1.so*' 'libgdk-4.so*' 'libgsk-4.so*'; do
    while IFS= read -r -d '' lib; do
        name=$(basename "$lib")
        [[ -e "$libdir/$name" ]] || cp -L "$lib" "$libdir/$name"
    done < <(find /usr/lib/x86_64-linux-gnu -maxdepth 1 -name "$pattern" -print0)
done

# Copy actual ELF dependencies recursively, while leaving host graphics/display
# libraries untouched so RADV and the host Vulkan loader remain a matched pair.
excluded='^(ld-linux.*|libc|libm|libdl|librt|libpthread|libresolv|libutil|libnsl|libanl|libvulkan|libGL|libGLX|libGLdispatch|libEGL|libOpenGL|libgbm|libdrm.*|libX11|libX11-xcb|libxcb.*|libXau|libXdmcp|libXext|libXfixes|libXrender|libXi|libXcursor|libXrandr|libwayland-.*|libasound|libudev|libpulse.*|libfontconfig|libfreetype|libOpenCL|libva.*|libvdpau|libvpl|libnvidia-.*|libcuda|libz)\.so'
declare -A seen=()
bundle() {
    local elf=$1 line lib name resolved existing
    [[ -f $elf ]] || return 0
    resolved=$(readlink -f "$elf")
    [[ -n ${seen[$resolved]:-} ]] && return 0
    seen[$resolved]=1
    while IFS= read -r line; do
        if [[ $line == *"not found"* ]]; then
            echo "Unresolved dependency of $elf: $line" >&2
            return 1
        fi
        lib=$(awk '/=> \// {print $3} /^\// {print $1}' <<<"$line")
        [[ -n $lib && -f $lib ]] || continue
        name=$(basename "$lib")
        [[ $name =~ $excluded ]] && continue
        if [[ -e "$libdir/$name" ]]; then
            existing=$(readlink -f "$libdir/$name")
            if [[ $existing != "$(readlink -f "$lib")" ]] && ! cmp -s "$libdir/$name" "$lib"; then
                echo "Conflicting runtime library name: $name ($existing vs $lib)" >&2
                return 1
            fi
        else
            cp -L "$lib" "$libdir/$name"
        fi
        bundle "$lib"
    done < <(ldd "$elf")
}

while IFS= read -r -d '' elf; do
    if file -b "$elf" | grep -q '^ELF'; then bundle "$elf"; fi
done < <(find "$appdir/usr/bin" "$appdir/usr/lib/python$pyver/lib-dynload" "$appdir/usr/lib/python3/dist-packages/gi" "$appdir/usr/lib/python3/dist-packages/PIL" "$libdir" "$share/bin" -type f -print0)

find "$libdir" -type f -name '*.so*' -exec strip --strip-unneeded {} + 2>/dev/null || true
find "$appdir/usr/lib/python$pyver" "$appdir/usr/lib/python3/dist-packages" -type f -name '*.so' -exec strip --strip-unneeded {} + 2>/dev/null || true

# Fail the package step if PyGObject cannot load both GTK4 and libadwaita.
export LD_LIBRARY_PATH="$libdir"
export PYTHONHOME="$appdir/usr"
export PYTHONPATH="$appdir/usr/lib/python3/dist-packages"
export GI_TYPELIB_PATH="$libdir/girepository-1.0"
export GSETTINGS_SCHEMA_DIR="$appdir/usr/share/glib-2.0/schemas"
"$appdir/usr/bin/python3" - <<'PY'
import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, Gtk
from PIL import Image
print("GTK", Gtk.get_major_version(), "libadwaita", Adw.get_major_version(), "Pillow", Image.__version__)
PY

need=$(find "$appdir" -type f \( -name '*.so*' -o -perm -u+x \) -exec objdump -T {} + 2>/dev/null | grep -o 'GLIBC_2\.[0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -1 || true)
max_glibc=2.39
if [[ -n $need && $(printf '%s\n' "$need" "$max_glibc" | sort -V | tail -1) != "$max_glibc" ]]; then
    echo "Bundle needs glibc $need, newer than $max_glibc; build on Ubuntu 24.04." >&2
    exit 1
fi
echo "Minimum glibc: ${need:-unknown}"
du -sh "$appdir"

# Keep Steam's injected overlay out of the bundled GTK/Python process.
cc -O2 -Wall -Werror -static packaging/bbport-entry.c -o "$appdir/AppRun"
chmod 755 "$appdir/AppRun"
install -m755 packaging/linux/AppRun.sh "$appdir/AppRun.sh"

tool=${APPIMAGETOOL:-$(command -v appimagetool || true)}
if [[ -z $tool ]]; then
    tool=out/appimagetool-x86_64.AppImage
    [[ -x $tool ]] || {
        curl -fL -o "$tool" https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
        chmod +x "$tool"
    }
fi
mkdir -p dist
ARCH=x86_64 "$tool" --appimage-extract-and-run "$appdir" dist/Bloodborne-bbport-x86_64.AppImage.new
mv -f dist/Bloodborne-bbport-x86_64.AppImage.new dist/Bloodborne-bbport-x86_64.AppImage
ls -lh dist/Bloodborne-bbport-x86_64.AppImage
