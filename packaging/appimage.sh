#!/usr/bin/env bash
# Build dist/Bloodborne-bbport-x86_64.AppImage (run build.sh first).
# Vulkan loader and GPU drivers are supplied by the host, as in the GoW3 Linux package.
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
[[ -f out/bb-probe && -f out/gpu/libbbgpu.so ]] || { echo 'Build first: bash build.sh' >&2; exit 1; }
root=$PWD
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# RUNPATH entries are search directories, not dependencies. Include only Nix-store
# libraries actually resolved by the ELF loader, otherwise build-time packages leak in.
{
    for elf in out/bb-probe out/gpu/libbbgpu.so; do
        ldd "$elf"
    done
} | awk '
    $2 == "=>" && $3 ~ /^\// { print $3 }
    $1 ~ /^\// { print $1 }
' | grep '^/nix/store/' | sed -E 's#^(/nix/store/[^/]+).*#\1#' | sort -u > "$work/runtime-paths"

{
    echo '['
    sed 's#^#  "#; s#$#"#' "$work/runtime-paths"
    echo ']'
} > packaging/runtime-paths.nix

echo "===== Nix runtime inputs =====" >&2
cat packaging/runtime-paths.nix >&2
mapfile -t runtime_paths < "$work/runtime-paths"
if (("${#runtime_paths[@]}" > 0)); then
    echo "===== Runtime dependency closure sizes =====" >&2
    nix path-info --closure-size "${runtime_paths[@]}" >&2
fi

(cd "$work" && nix bundle --impure --bundler github:ralismark/nix-appimage \
    --expr "import $root/packaging {}")
mkdir -p dist
install -m755 "$work/bbport.AppImage" dist/Bloodborne-bbport-x86_64.AppImage
ls -lh dist/Bloodborne-bbport-x86_64.AppImage
