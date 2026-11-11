{ lib
, stdenv
, fetchFromGitHub
, cmake
, ninja
, pkg-config
, alsaLib
, dbus
, libGL
, libdecor
, libdrm
, libpulseaudio
, libusb1
, libxkbcommon
, mesa
, pipewire
, systemdLibs
, wayland
, wayland-scanner
, xorg
}:

let
  # SDL loads several of these backends with dlopen(), so keep them in its
  # runtime search path while building. The AppImage packager leaves the
  # desktop/audio/driver stack to the host.
  runtimeInputs = [
    alsaLib
    dbus
    libGL
    libdecor
    libdrm
    libpulseaudio
    libusb1
    libxkbcommon
    mesa
    pipewire
    systemdLibs
    wayland
    xorg.libX11
    xorg.libXScrnSaver
    xorg.libXcursor
    xorg.libXext
    xorg.libXfixes
    xorg.libXi
    xorg.libXrandr
  ];
in
stdenv.mkDerivation {
  pname = "sdl3";
  version = "3.1.8";

  src = fetchFromGitHub {
    owner = "libsdl-org";
    repo = "SDL";
    tag = "preview-3.1.8";
    hash = "sha256-yfnW5y99EegifRavvZWmXPH2NFPKWoe90RmGDGk6PI4=";
  };

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    wayland-scanner
  ];

  buildInputs = runtimeInputs;

  cmakeFlags = [
    "-DSDL_SHARED=ON"
    "-DSDL_STATIC=OFF"
    "-DSDL_TESTS=OFF"
    "-DSDL_TEST_LIBRARY=OFF"
    "-DSDL_ALSA=ON"
    "-DSDL_DBUS=ON"
    "-DSDL_JACK=OFF"
    "-DSDL_KMSDRM=ON"
    "-DSDL_LIBUDEV=ON"
    "-DSDL_OPENGL=ON"
    "-DSDL_PIPEWIRE=ON"
    "-DSDL_PULSEAUDIO=ON"
    "-DSDL_SNDIO=OFF"
    "-DSDL_WAYLAND=ON"
    "-DSDL_WAYLAND_LIBDECOR=ON"
    "-DSDL_X11=ON"
  ];

  # SDL discovers some of its display and audio backends at runtime.
  dontPatchELF = true;
  NIX_LDFLAGS = "-rpath ${lib.makeLibraryPath runtimeInputs}";

  meta = {
    description = "Simple DirectMedia Layer 3";
    homepage = "https://libsdl.org";
    license = lib.licenses.zlib;
    platforms = lib.platforms.unix;
  };
}
