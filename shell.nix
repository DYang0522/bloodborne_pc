# Build environment: `nix-shell native_probe/shell.nix --run 'bash native_probe/run.sh'`
{ pkgs ? import <nixpkgs> {} }:
let
  # The Linux AVPlayer accepts H.264, HEVC and AAC only. Keep FFmpeg's core
  # demux/decode/resample/scale libraries and their built-in decoders; omit
  # unrelated codecs, encoders, filters, hardware backends and network I/O.
  ffmpegRuntime = pkgs.ffmpeg-headless.override {
    withHeadlessDeps = false;
    withSmallDeps = false;
    withFullDeps = false;
    # FFmpeg's own pixelutils test target expects this internal libavutil component.
    withPixelutils = true;
    withNetwork = false;
    withAlsa = false;
    withAmf = false;
    withAom = false;
    withAss = false;
    withBluray = false;
    withDav1d = false;
    withDrm = false;
    withGnutls = false;
    withNvcodec = false;
    withNvdec = false;
    withNvenc = false;
    withOpenapv = false;
    withOpencl = false;
    withOpenjpeg = false;
    withOpenmpt = false;
    withOpus = false;
    withPulse = false;
    withRist = false;
    withSoxr = false;
    withSpeex = false;
    withSrt = false;
    withSsh = false;
    withSvtav1 = false;
    withTheora = false;
    withVaapi = false;
    withVdpau = false;
    withVmaf = false;
    withVpx = false;
    withVulkan = false;
    withWebp = false;
    withX264 = false;
    withX265 = false;
    withXml2 = false;
    withXvid = false;
    withZvbi = false;
    # Keep common compressed-container support and safe decoding.
    withBzlib = true;
    withHardcodedTables = true;
    withLzma = true;
    withSafeBitstreamReader = true;
    withZlib = true;
    # No CLI utilities, capture devices, filters or post-processing library.
    buildFfmpeg = false;
    buildFfplay = false;
    buildFfprobe = false;
    # FFmpeg 9's default build also links tools/uncoded_frame against both libraries.
    # Keep the internal libs, while all optional external backends remain disabled above.
    buildAvdevice = true;
    buildAvfilter = true;
    buildAvformat = true;
    buildAvcodec = true;
    buildAvutil = true;
    buildPostproc = false;
    buildSwresample = true;
    buildSwscale = true;
    withDocumentation = false;
    withHtmlDoc = false;
    withManPages = false;
    withPodDoc = false;
    withTxtDoc = false;
    withStripping = true;
  };
in
pkgs.mkShell {
  packages = with pkgs; [
    gcc gnumake cmake ninja pkg-config binutils
    (python3.withPackages (ps: [ ps.pillow ])) # Pillow: the trophy pop-up's pictures (run.py)
    vulkan-headers vulkan-loader sdl3
    # GPU library (gpu/): shadPS4 video core dependencies
    ffmpegRuntime boost fmt magic-enum robin-map xxhash vulkan-memory-allocator glslang spirv-cross xbyak zydis spirv-headers miniz libx11 libxcb xorgproto wayland
    # Online (gpu/bbnet): shadNet's protobuf, HTTPS and gzip for cpp-httplib, JSON, UPnP
    protobuf openssl zlib nlohmann_json miniupnpc
  ];
}
