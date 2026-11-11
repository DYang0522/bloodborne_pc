# Linux build environment. The Nix package set is pinned in the AppImage workflow
# to the Ubuntu 24.04-compatible glibc 2.39 baseline.
{ pkgs ? import <nixpkgs> {} }:
let
  sdl3 = pkgs.callPackage ./packaging/nix/sdl3.nix {};
  vma = pkgs.callPackage ./packaging/nix/vulkan-memory-allocator.nix {};
in
pkgs.mkShell {
  packages = with pkgs; [
    gcc14 gnumake cmake ninja pkg-config binutils patchelf
    (python3.withPackages (ps: [ ps.pillow ])) # Pillow: the trophy pop-up's pictures (run.py)
    vulkan-headers vulkan-loader sdl3
    # GPU library (gpu/): shadPS4 video core dependencies
    ffmpeg-headless boost fmt magic-enum robin-map xxHash vma glslang spirv-cross zydis zydis.zycore spirv-headers miniz xorg.libX11 xorg.libxcb xorg.libXau xorg.libXdmcp xorg.xorgproto wayland
    # Online (gpu/bbnet): shadNet's protobuf, HTTPS and gzip for cpp-httplib, JSON, UPnP
    protobuf openssl zlib nlohmann_json miniupnpc
  ];

  # Keep C and C++ on the GCC 14 toolchain supplied by this pinned package set.
  shellHook = ''
    export CC=${pkgs.gcc14}/bin/gcc
    export CXX=${pkgs.gcc14}/bin/g++
  '';
}
