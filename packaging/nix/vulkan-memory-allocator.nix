{ stdenvNoCC, fetchFromGitHub }:

stdenvNoCC.mkDerivation {
  pname = "vulkan-memory-allocator";
  version = "3.5.0";

  src = fetchFromGitHub {
    owner = "GPUOpen-LibrariesAndSDKs";
    repo = "VulkanMemoryAllocator";
    tag = "v3.5.0";
    hash = "sha256-MQg4Bzqj6lLz6W7sRmjC3gxtddWlwEPsED+2Dwk8KII=";
  };

  dontConfigure = true;
  dontBuild = true;

  installPhase = ''
    mkdir -p "$out/include"
    install -m644 include/vk_mem_alloc.h "$out/include/vk_mem_alloc.h"
  '';
}
