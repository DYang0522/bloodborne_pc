{ lib
, stdenv
, fetchFromGitHub
, cmake
, ninja
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "vulkan-headers";
  version = "1.4.363.0";

  # Header-only package: use current Khronos declarations while keeping every
  # compiled runtime dependency on the pinned Ubuntu 24.04 / glibc 2.39 toolchain.
  nativeBuildInputs = [
    cmake
    ninja
  ];

  cmakeFlags = lib.optionals stdenv.hostPlatform.isDarwin [
    "-DVULKAN_HEADERS_ENABLE_MODULE=OFF"
  ];

  src = fetchFromGitHub {
    owner = "KhronosGroup";
    repo = "Vulkan-Headers";
    rev = "vulkan-sdk-${finalAttrs.version}";
    hash = "sha256-E/SBlMW1oUeUT0MtWzQp0k+OSZLc9ho7g+1oTd7z7+Y=";
  };

  meta = {
    description = "Vulkan Header files and API registry";
    homepage = "https://www.lunarg.com";
    platforms = lib.platforms.unix ++ lib.platforms.windows;
    license = lib.licenses.asl20;
  };
})
