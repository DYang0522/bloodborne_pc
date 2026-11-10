#!/usr/bin/env bash
# Builds dist/Bloodborne-bbport-x86_64.AppImage: the port's current build (run build.sh first),
# its scripts and the launcher, bundled with their Nix closure (nix-appimage: the AppImage mounts
# its /nix/store with user namespaces, available on SteamOS and most desktops).
# Running it opens the launcher; `--play` starts the game with the launcher's saved settings.
# Data (generated files, saves, bbport.ini): ~/.local/share/bbport (BB_DATA_DIR).
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
[[ -f out/bb-probe && -f out/gpu/libbbgpu.so ]] || { echo 'Build first: bash build.sh' >&2; exit 1; }
root=$PWD
# Include only Nix store packages that actually satisfy ELF DT_NEEDED dependencies.
# RUNPATH contains search directories, not a list of required libraries: treating every entry as
# a dependency pulls build-only packages into the AppImage (and can inflate it by hundreds of MB).
{
    echo '['
    for elf in out/bb-probe out/gpu/libbbgpu.so; do
        ldd "$elf"
    done | awk '
        $2 == "=>" && $3 ~ /^\// { print $3 }
        $1 ~ /^\// { print $1 }
    ' | grep '^/nix/store/' | sed -E 's#^(/nix/store/[^/]+).*#\1#' | sort -u | sed 's/.*/  "&"/'
    echo ']'
} > packaging/runtime-paths.nix
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
(cd "$work" && nix bundle --impure --bundler github:ralismark/nix-appimage \
    --expr "import $root/packaging {}")
mkdir -p dist
install -m755 "$work/bbport.AppImage" dist/Bloodborne-bbport-x86_64.AppImage
ls -lh dist/Bloodborne-bbport-x86_64.AppImage
 | sed 's/.*/  "&"/'
    echo ']'
} > packaging/runtime-paths.nix
echo "===== Generated runtime-paths.nix =====" >&2
cat packaging/runtime-paths.nix >&2
mapfile -t runtime_paths < <(grep -o '/nix/store/[^"]*' packaging/runtime-paths.nix)
if (("${#runtime_paths[@]}" > 0)); then
    echo "===== Runtime dependency closure sizes =====" >&2
    nix path-info --closure-size "${runtime_paths[@]}" >&2
fi
echo "===== Input ELF sizes =====" >&2
ls -lh out/bb-probe out/gpu/libbbgpu.so >&2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
(cd "$work" && nix bundle --impure --bundler github:ralismark/nix-appimage \
    --expr "import $root/packaging {}")
mkdir -p dist
install -m755 "$work/bbport.AppImage" dist/Bloodborne-bbport-x86_64.AppImage
ls -lh dist/Bloodborne-bbport-x86_64.AppImage
