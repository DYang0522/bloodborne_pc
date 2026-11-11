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
# The full RUNPATH contains build-shell search directories too. Bundle only the
# Nix store roots of libraries that ldd resolves for the installed ELF files;
# their Nix closures bring along the transitive runtime dependencies.
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
: > "$work/runtime-roots.txt"
for elf in out/bb-probe out/gpu/libbbgpu.so; do
    if ! ldd "$elf" > "$work/ldd.txt"; then
        echo "Could not resolve runtime dependencies for $elf" >&2
        exit 1
    fi
    if grep -q 'not found' "$work/ldd.txt"; then
        echo "Unresolved runtime dependency in $elf:" >&2
        cat "$work/ldd.txt" >&2
        exit 1
    fi
    grep -oE '/nix/store/[^ /]+' "$work/ldd.txt" >> "$work/runtime-roots.txt" || true
done
sort -u "$work/runtime-roots.txt" > "$work/runtime-roots.sorted"
{
    echo '['
    while IFS= read -r path; do
        [[ -n "$path" ]] && printf '  "%s"\n' "$path"
    done < "$work/runtime-roots.sorted"
    echo ']'
} > packaging/runtime-paths.nix
printf 'Bundling %s Nix runtime roots (resolved by ldd):\n' "$(wc -l < "$work/runtime-roots.sorted")"
cat "$work/runtime-roots.sorted"
(cd "$work" && nix bundle --impure --bundler github:ralismark/nix-appimage \
    --expr "import $root/packaging {}")
mkdir -p dist
install -m755 "$work/bbport.AppImage" dist/Bloodborne-bbport-x86_64.AppImage
ls -lh dist/Bloodborne-bbport-x86_64.AppImage
