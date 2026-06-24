# HEIC/HEIF Support Through libheif

Date: 2026-06-19

## Goal

Add read-only HEIC/HEIF still-image support using libheif, with behavior comparable to the dedicated JPEG path:

- Recognize `.heic`, `.heif`, and `.hif` files during folder enumeration and direct file opening.
- Decode the primary image through libheif with the libde265 HEVC decoder built in.
- Apply HEIF crop, mirror, and rotation properties exactly once.
- Respect embedded ICC profiles and libheif NCLX color information.
- Produce the existing bottom-up BGR24 `ImgBuffer` representation.
- Resize to the viewer target while preserving aspect ratio.
- Preserve the asynchronous loader and cache behavior.
- Supply reproducible x86/x64 Visual Studio and MSYS static dependency builds.
- Preserve the documented Windows XP target unless compatibility testing proves that the selected dependency stack
  cannot meet it.

The initial scope is the primary still image only. It does not include encoding, image sequences, animation, video,
multiple top-level image selection, depth maps, gain maps, auxiliary thumbnails as user-visible images, or AVIF.

## Current ImgVw Integration Points

The current image pipeline is organized around `ImgItem` subclasses:

- `ImgItemHelper::GetImgFormatFromExtension()` maps filename extensions to `ImgItem::Format`.
- `ImgItemFactory::Create()` chooses `ImgJPEGItem` for JPEG and `ImgGDIItem` for other GDI+ formats.
- `ImgLoader` loads cache-owned `ImgItem` instances on worker threads.
- Each decoder produces a bottom-up, padded BGR24 `ImgBuffer`.
- `ImgItem::SetupDisplayParameters()` creates the `BITMAPINFO` used by the Win32 rendering path.

JPEG has a dedicated path because it needs decoder-specific scaling, EXIF orientation, CMYK handling, and ICC
processing. HEIF should follow that model with a new `ImgHEIFItem`; it should not be routed through GDI+.

Relevant current limitations:

- `ImgItem::OpenICCProfile()` accepts only CMYK profiles, which is appropriate for the JPEG CMYK path but not for
  normal RGB HEIF ICC profiles.
- The display pipeline is 8-bit BGR without alpha or HDR output.
- `ImgBuffer::WriteData()` expects a complete contiguous buffer and stores its byte count in a `DWORD`.
- The loader may decode multiple images concurrently, so libheif/libde265 thread settings must avoid excessive nested
  parallelism.

## Recommended Dependency Versions

Start with:

- libheif `1.23.0`.
- libde265 `1.1.1`.

Reasons:

- As of June 19, 2026, these are the latest upstream stable releases.
- libheif `1.23.0` includes a 2026 security fix and current color-decoding controls.
- libde265 `1.1.1` includes the new security-limit work, additional 2026 security fixes, and HEIC decode performance
  improvements.
- libheif uses libde265 as its default HEVC decoder.

Pin exact release archives and SHA-256 hashes in the build script. Do not track an unpinned branch or rely on whatever
version is installed in MSYS2 or vcpkg.

Before implementation begins, recheck both projects for newer security releases. If a newer patch release exists,
update the pins and repeat the compatibility gate below.

## Dependency Configuration

Build a minimal decode-only stack. Do not use libheif's dynamic codec plugin mechanism.

Recommended libheif configuration:

```text
BUILD_SHARED_LIBS=OFF
BUILD_TESTING=OFF
BUILD_DOCUMENTATION=OFF
WITH_EXAMPLES=OFF
WITH_GDK_PIXBUF=OFF
BUILD_DEVELOPMENT_TOOLS=OFF
ENABLE_PLUGIN_LOADING=OFF
ENABLE_EXPERIMENTAL_FEATURES=OFF
WITH_LIBDE265=ON
WITH_LIBDE265_PLUGIN=OFF
WITH_X265=OFF
WITH_KVAZAAR=OFF
WITH_AOM_DECODER=OFF
WITH_AOM_ENCODER=OFF
WITH_DAV1D=OFF
WITH_SvtEnc=OFF
WITH_RAV1E=OFF
WITH_JPEG_DECODER=OFF
WITH_JPEG_ENCODER=OFF
WITH_OpenJPEG_DECODER=OFF
WITH_OpenJPEG_ENCODER=OFF
WITH_OPENJPH_ENCODER=OFF
WITH_FFMPEG_DECODER=OFF
WITH_OpenH264_DECODER=OFF
WITH_X264=OFF
WITH_VVDEC=OFF
WITH_VVENC=OFF
WITH_UVG266=OFF
WITH_UNCOMPRESSED_CODEC=OFF
WITH_HEADER_COMPRESSION=OFF
WITH_LIBSHARPYUV=OFF
ENABLE_PARALLEL_TILE_DECODING=OFF
```

Keep `ENABLE_MULTITHREADING_SUPPORT=ON` initially because libde265 benefits from it, but cap decoder threads per image
if the public API and selected version permit it. Benchmark against the existing parallel `ImgLoader`; if nested
parallelism causes CPU oversubscription, prefer one libde265 worker per image and retain parallelism at the ImgVw item
level.

Do not add x265. ImgVw only decodes, and x265 would add an unnecessary encoder plus GPL licensing concerns.

## Windows XP Compatibility Gate

libheif `1.23.0` requires C++20 and does not document Windows XP as a supported runtime. Therefore, XP compatibility
must be treated as a release gate rather than assumed from successful compilation.

Perform this spike before integrating decoder code:

1. Build the exact static libde265/libheif configuration for MSVC x86/x64 and MSYS x86/x64.
2. Link a minimal console probe that:
   - initializes libheif;
   - reads a memory-backed HEIC;
   - gets the primary image;
   - decodes it to interleaved RGB;
   - releases all objects and shuts down cleanly.
3. Inspect the final probe executable, not only the static archives:
   - MSVC: `dumpbin /imports`.
   - MSYS: `objdump -p`.
4. Reject unguarded imports newer than Windows XP. Pay particular attention to synchronization, thread, condition
   variable, file, and processor-information APIs introduced after XP.
5. Run the x86 release probe on a clean Windows XP SP3 VM.
6. Run malformed/truncated input and repeated decode/shutdown tests on the VM.

If the current releases fail this gate:

- First try disabling libheif parallel tile decoding and reducing libde265 threading.
- If failure is isolated and upstream code has a small, maintainable XP-compatible fallback, document and vendor a
  minimal patch.
- Otherwise stop and report that HEIF support requires changing the compatibility target. Do not silently weaken the
  XP promise or pin an old codec version with known security defects.

The modern Visual Studio runtime/toolset is a separate compatibility risk. The probe must use the same compiler,
runtime linkage, and linker settings as the application build being evaluated.

## Licensing Gate

libheif and libde265 are LGPL-3.0-or-later libraries. Static linking has distribution obligations beyond copying the
license text, including allowing recipients to relink the application with a modified library.

Before distributing a statically linked ImgVw build:

- Confirm the project's intended LGPL compliance approach.
- Include the required notices and license texts.
- Make the exact corresponding library source and build scripts available.
- Provide suitable application object files or another compliant relinking mechanism if static linking is retained.
- Do not enable GPL codec backends.

If supplying relinkable objects is undesirable, reconsider DLL deployment. Dynamic linking simplifies the LGPL
relinking requirement but adds DLL/plugin deployment and XP import-surface concerns. This is a legal/product decision;
the implementation should not choose silently.

## Proposed Vendored Layout

Keep generated build trees outside the repository and copy only consumed headers, libraries, licenses, readmes, and
version markers:

```text
3rd-party/
  libde265/
    1.1.1
    COPYING
    README.md
    de265.h
    libde265.lib             # Visual Studio Win32
    libde265.a               # MSYS x86
    x64/
      libde265.lib           # Visual Studio x64
    ucrt64/
      libde265.a             # MSYS x64
  libheif/
    1.23.0
    COPYING
    README.md
    libheif/
      heif.h
      heif_*.h               # complete installed public C API header set
    heif.lib                 # Visual Studio Win32
    libheif.a                # MSYS x86
    x64/
      heif.lib               # Visual Studio x64
    ucrt64/
      libheif.a              # MSYS x64
```

Use the actual installed target filenames if upstream emits different names, then make the project paths explicit.
Do not rename archives until their machine type and symbols have been verified.

Vendor the complete installed libheif public C header directory. In recent libheif releases, `heif.h` includes
topic-specific public headers; copying only one header is insufficient.

## Dependency Build Script

Add `scripts/build-libheif.ps1`, modeled after `scripts/build-libjpeg-turbo.ps1` and
`scripts/build-little-cms.ps1`.

Suggested parameters:

```powershell
param(
    [string]$LibheifVersion = "1.23.0",
    [string]$Libde265Version = "1.1.1",
    [ValidateSet("all", "vs", "msys", "source")]
    [string]$Mode = "all",
    [ValidateSet("all", "x86", "x64")]
    [string]$Arch = "all",
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$WorkRoot = (Join-Path $env:TEMP "imgvw-libheif"),
    [string]$MsysRoot = "",
    [switch]$Clean
)
```

Script responsibilities:

1. Locate Visual Studio with `vswhere` and enter the matching x86/x64 developer environment.
2. Locate MSYS2 and reinvoke itself in `MINGW32` or `UCRT64`, matching the established dependency scripts.
3. Download official libde265 and libheif release archives.
4. Validate pinned SHA-256 hashes before extraction.
5. Build and install libde265 to a private, architecture/toolchain-specific staging prefix.
6. Configure libheif with `CMAKE_PREFIX_PATH` pointing only at that staged libde265 installation.
7. Verify CMake's summary reports:
   - HEIC decoding: `YES`;
   - libde265: built in;
   - all unrequested codecs: disabled;
   - plugin loading: disabled.
8. Build the static libheif target.
9. Copy the installed public headers, static archives, license/readme files, and empty version marker files to the
   proposed vendor layout.
10. Verify artifact machine types and expected public symbols.
11. Fail if shared libraries, codec plugin DLLs, x265, AV1 codecs, examples, or command-line tools were produced for
    packaging.

Use out-of-tree CMake builds. The script should not require vcpkg, package-manager state, or manually installed codec
libraries.

Potential Windows static-link issue: libheif's CMake discovery can find libde265 but still omit its transitive symbols
from a final consumer link. Verify the final ImgVw link explicitly includes both libraries. For GNU-style static
archives, link order should normally be `-lheif -lde265`.

All documented invocations must include `powershell -NoProfile`, for example:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-libheif.ps1 -Mode msys -Arch x86
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-libheif.ps1 -Mode msys -Arch x64
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-libheif.ps1 -Mode vs -Arch all
```

## Decoder Class

Add:

- `src/image/ImgHEIFItem.h`
- `src/image/ImgHEIFItem.cpp`

Use libheif's C API directly. Keep all libheif types and cleanup inside this class or a small image-layer RAII wrapper;
do not expose them to the browser, loader, or Win32 UI.

Add local RAII deleters for:

- `heif_context`;
- `heif_image_handle`;
- `heif_image`;
- `heif_decoding_options`;
- allocated NCLX profiles when queried.

Use `heif_context_read_from_memory_without_copy()` with the existing `FileMapView`. The mapping must remain alive until
the context, image handle, and decoded image no longer need the source.

Call `heif_init()` and `heif_deinit()` through a process-wide, thread-safe lifetime guard. Do not initialize and
deinitialize the library around every image while other loader threads may still be decoding.

## Decode Flow

Recommended `ImgHEIFItem::Load()` flow:

1. Set status to `Loading`.
2. Map the file with `FileMapView`.
3. Validate that the file size can be represented by libheif's memory-input API and the application's size types.
4. Allocate a `heif_context`.
5. Keep libheif's default security limits enabled. Apply stricter application limits if the selected API supports
   setting maximum image dimensions and total decode memory.
6. Read the mapped file without copying it.
7. Get the primary image handle.
8. Read transformed display dimensions and reject zero, negative, overflowing, or unreasonable sizes.
9. Extract color-profile metadata before decode.
10. Allocate decoding options:
    - leave `ignore_transformations` false;
    - request 8-bit SDR output suitable for the current display pipeline;
    - do not enable experimental behavior;
    - configure alpha composition only if using a libheif option instead of an ImgVw row conversion.
11. Decode the primary image to 8-bit interleaved RGB or RGBA.
12. If resizing is needed, calculate aspect-ratio-preserving target dimensions and use ImgVw's first-party separable
    area resampler. Do not use `heif_image_scale_image()` because libheif 1.23.0 implements it with nearest-neighbor
    sampling.
13. Obtain the interleaved plane and stride.
14. Convert top-down RGB/RGBA rows to bottom-up padded BGR24 rows:
    - swap red and blue;
    - composite alpha using the chosen viewer background policy;
    - perform ICC conversion when required;
    - zero DIB padding bytes.
15. Write the final pixels to `ImgBuffer`.
16. Call `SetupDisplayParameters()` with a positive-height bottom-up DIB.
17. Set status to `Ready`.
18. On every error, store a useful message containing the libheif error code, subcode, and message, then set `Error`.
19. Release libheif and ICC resources before signaling `loadedevent_`.

Do not use `goto` across non-trivial C++ object lifetimes. The new decoder should use RAII plus one final status/event
guard even if the existing JPEG implementation is not yet structured that way.

## Geometry

HEIF stores display orientation and mirroring as item properties. With default decoding options, libheif applies
geometric transformations such as crop, rotation, and mirroring.

Rules:

- Keep `ignore_transformations` false.
- Treat the libheif-decoded dimensions and pixels as display-oriented.
- Do not parse or apply EXIF orientation. Applying it after libheif's container transforms risks double rotation and
  provides no current viewer feature.
- Revisit EXIF orientation only if a reproducible real-world file displays incorrectly without it.

## ICC and NCLX Color Handling

HEIF color metadata differs from the current JPEG CMYK case:

- libheif can expose a raw embedded ICC profile (`rICC` or `prof`).
- libheif can expose NCLX primaries, transfer characteristics, matrix coefficients, and range.
- A file may contain both ICC and NCLX metadata.
- HEIF images are normally RGB/YCbCr, not CMYK.

Recommended policy:

1. Query raw ICC and NCLX independently; do not rely only on the single profile-type convenience function.
2. If a valid embedded RGB ICC profile exists:
   - prefer it over NCLX for color-management policy;
   - use Little CMS to transform decoded RGB pixels to sRGB/BGR8;
   - reject obviously excessive profile sizes before allocation;
   - treat an invalid profile as a recoverable metadata failure and continue with libheif's normal RGB conversion,
     while recording diagnostics.
3. If no ICC profile exists:
   - let libheif convert NCLX-described YCbCr/RGB to 8-bit sRGB output;
   - do not apply a second Little CMS transform.
4. For HDR PQ/HLG or wide-gamut NCLX input, accept libheif's SDR 8-bit conversion for the initial implementation.
   Native HDR display requires a separate rendering architecture change.

Before coding the ICC transform, verify with upstream test files whether libheif's interleaved RGB output has already
been transformed for raw ICC profiles. The expected design is that libheif performs codec/NCLX conversion while the
caller uses the raw ICC profile for ICC color management. A known wide-gamut ICC sample must confirm that applying
Little CMS improves correctness rather than double-transforming.

Refactor color management carefully:

- Preserve the current JPEG CMYK behavior.
- Do not change `ImgItem::OpenICCProfile()` to accept any profile and accidentally feed an RGB profile into
  `TranformCMYK8ColorsToBGR8()`.
- Prefer explicit helpers such as `TransformCmyk8ToBgr8()` and `TransformRgb8ToBgr8()` with profile color-space
  validation.
- Add RAII for `cmsHPROFILE` and `cmsHTRANSFORM` in new code.

## Alpha Handling

The current display buffer has no alpha channel. For HEIF images with an auxiliary alpha channel:

- Decode RGBA.
- Composite to BGR24 using a documented solid background.
- Match the existing GDI+ path's effective background if it can be established; otherwise use black for the first
  implementation because zero-initialized transparent pixels already resolve to black in many current paths.
- Add a transparent test image with opaque, partially transparent, and fully transparent pixels.

Do not simply discard alpha, because hidden RGB values under transparent pixels can produce incorrect output.

## Resizing and Memory

The simple implementation decodes the primary image, optionally asks libheif to scale it, and then writes BGR24. This
may temporarily retain both full-size and scaled libheif images.

Initial policy:

- Resize only when the transformed image is larger than the target; do not upscale.
- Preserve aspect ratio using checked integer or double calculations.
- Use the reusable first-party separable area resampler for high-quality, anti-aliased downscaling.
- Keep the resampler independent from libheif and GDI+ so a future native PNG decoder can use the same RGBA8 path.
- Filter premultiplied color and alpha to prevent fringes around transparent pixels.
- Convert directly from the final libheif plane into one bottom-up BGR destination buffer or streamed `ImgBuffer`
  rows.

Preferred implementation if the planned `ImgBuffer` streaming API lands first:

- Add `BeginWrite()`, `WriteRow()`, and `EndWrite()` or an RAII write session.
- Convert and write one destination BGR row at a time.
- Avoid a second full image-sized BGR allocation.

If streaming is not available, allocate one checked BGR24 output buffer, call `WriteData()`, and free it immediately.
In either case, validate:

- width, height, source stride, destination stride, and row offsets;
- multiplication against `size_t`, `DWORD`, and `INT` limits;
- output size against application memory/file limits;
- all allocations and partial writes.

Very large tiled HEIF support is out of initial scope. Keep libheif's security limits enabled and return a clear error
instead of disabling limits.

## Format Detection and Factory Integration

Add `HEIF` to `ImgItem::Format`.

Map these extensions case-insensitively:

- `.heic`
- `.heif`
- `.hif`

Do not add `.avif` in this change because the dependency build intentionally has no AV1 decoder.

Update `ImgItemFactory::Create()` to instantiate `ImgHEIFItem`.

Content-based detection and decoder rerouting are owned by
`artifacts/content_based_image_dispatch_plan.md`. HEIF integration must consume that dispatcher rather than adding a
HEIF-specific header exception. `ImgHEIFItem` remains responsible for full validation after a file is routed to it.

## Project and Build Integration

Update together:

- `ImgVw.vcxproj`
- `ImgVw.vcxproj.filters`
- `Makefile`
- `scripts/tidy.ps1`

Visual Studio:

- Add `3rd-party\libheif` and `3rd-party\libde265` include directories.
- Add the new first-party source/header files.
- Link the x86 or x64 static libheif and libde265 libraries explicitly.
- Add any required Windows system libraries discovered by the minimal probe, but do not add newer APIs that violate
  the XP gate.

Makefile:

- Add `ImgHEIFItem.o`.
- Add both dependency include paths.
- Add x86/x64 library search paths.
- Link `-lheif -lde265` after ImgVw objects and before any dependent system libraries.
- Keep the final application link through `g++`, because libheif and libde265 contain C++ code.

Static archives from MSVC and MinGW are not interchangeable. Continue producing separate artifacts for each
toolchain and architecture.

## Error Reporting

Add a small formatter that converts `heif_error` into a stable `std::wstring` or intermediate UTF-8 message containing:

- high-level error code;
- suberror code;
- upstream message when non-null;
- failing operation context, such as reading the container, getting the primary image, decoding, or scaling.

Do not retain pointers to libheif-owned error strings after the relevant call/lifetime without copying them.

Distinguish at least:

- invalid or unsupported HEIF container;
- no primary image;
- unsupported compression/decoder unavailable;
- security limit exceeded;
- decode failure;
- invalid dimensions/overflow;
- ICC metadata invalid;
- allocation or output-write failure.

## Tests and Fixtures

Add redistributable fixtures under `artifacts/sample/heif/` with source and license information in the sample README.
The corpus should cover:

- standard iPhone HEIC with no rotation;
- portrait HEIC using container rotation;
- mirror plus rotation;
- crop property;
- embedded RGB ICC profile;
- NCLX-only sRGB/BT.709;
- wide-gamut Display P3 or equivalent;
- 10-bit HEIC converted to the 8-bit display pipeline;
- HEIC with alpha;
- embedded thumbnail plus full-resolution primary image;
- grid/tiled HEIC;
- multiple top-level images with a defined primary image;
- malformed/truncated container;
- valid HEIF container using an intentionally unsupported codec;
- oversized dimensions/security-limit case.

Where licensing permits, include compact fixtures directly. Otherwise document download URLs, hashes, licenses, and a
fixture preparation script.

Add focused tests when the repository gains its native test target:

- extension fallback mapping;
- content-based routing to HEIF;
- factory selection;
- aspect-ratio target calculation;
- RGB/RGBA top-down to bottom-up BGR conversion;
- row padding;
- alpha composition;
- ICC profile size and color-space validation;
- arithmetic overflow rejection;
- libheif error formatting.

## Manual Verification

For every fixture:

- Open directly from the command line.
- Discover it during folder browsing.
- Navigate forward/backward and reload it.
- Verify no double rotation or mirroring.
- Verify target fitting, centering, and no upscaling.
- Compare ICC/NCLX color against a color-managed reference viewer.
- Verify alpha composition.
- Confirm malformed input reports an error and the loader continues.
- Repeatedly navigate between JPEG, PNG, and HEIF files to exercise concurrent cache/load/unload behavior.

Monitor:

- process memory before, during, and after large decodes;
- thread count and CPU oversubscription;
- file-handle and temporary-file leaks;
- shutdown while HEIF work is queued or active.

## Build Verification

Dependency builds:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-libheif.ps1 -Mode vs -Arch all -Clean
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-libheif.ps1 -Mode msys -Arch x86 -Clean
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-libheif.ps1 -Mode msys -Arch x64 -Clean
```

Application builds:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x86 -Clean
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x64 -Clean
```

Also build Visual Studio Release Win32 and x64. Run the format check and clang-tidy after adding the new first-party
files.

The release verification matrix must include:

- current Windows x86 and x64;
- Windows XP SP3 x86 if the compatibility gate passes;
- clean machines without vcpkg/MSYS dependency DLLs on `PATH`;
- import inspection of the final executable;
- confirmation that no codec plugin or unexpected runtime DLL is required.

## Implementation Order

1. Resolve the LGPL static-link distribution approach.
2. Implement the dependency build script and minimal decode probe.
3. Complete the Windows XP import/runtime compatibility gate.
4. Vendor only verified headers, archives, notices, and version markers.
5. Add HEIF extension fallback mapping and `ImgHEIFItem` project wiring; integrate with the general content-based
   dispatcher defined in `artifacts/content_based_image_dispatch_plan.md`.
6. Implement process-wide libheif initialization and RAII wrappers.
7. Implement primary-image decode with container transformations and security limits.
8. Implement checked RGB/RGBA-to-bottom-up-BGR24 conversion and resizing.
9. Add ICC/NCLX handling and verify it with known color-managed samples.
10. Add fixtures and focused tests.
11. Run the full x86/x64 build and manual verification matrix.
12. Update user-facing documentation and third-party notices.

## Acceptance Criteria

HEIF support is complete when:

- `.heic`, `.heif`, and `.hif` files browse and open through a dedicated decoder.
- Files identified as HEIF by the general content dispatcher route to `ImgHEIFItem` regardless of filename extension.
- The primary still image is displayed with correct crop, orientation, mirroring, aspect ratio, and BGR channels.
- Embedded RGB ICC and NCLX-only files have a documented, tested color-management path.
- 8/10-bit input converts safely to the existing 8-bit display pipeline.
- Alpha input composites deterministically.
- Malformed, unsupported, and oversized files fail without crashing or wedging loader threads.
- Visual Studio and MSYS release builds succeed for x86 and x64.
- The final executable has no accidental dynamic codec/plugin dependency.
- Windows XP compatibility is either demonstrated by the defined gate or explicitly escalated before merging.
- Third-party licensing and redistribution requirements are satisfied.

## Upstream References

- libheif repository and build options:
  <https://github.com/strukturag/libheif>
- libheif `1.23.0` release:
  <https://github.com/strukturag/libheif/releases/tag/v1.23.0>
- libheif public C API headers:
  <https://github.com/strukturag/libheif/tree/v1.23.0/libheif/api/libheif>
- libheif license:
  <https://github.com/strukturag/libheif/blob/v1.23.0/COPYING>
- libde265 `1.1.1` release:
  <https://github.com/strukturag/libde265/releases/tag/v1.1.1>
- libde265 license:
  <https://github.com/strukturag/libde265/blob/v1.1.1/COPYING>
