# Native PNG Support Through libpng

Date: 2026-06-20

## Goal

Replace PNG decoding through `ImgGDIItem`/GDI+ with a dedicated, read-only libpng decoder while preserving the
existing viewer architecture and Windows XP compatibility target.

The implementation should:

- Recognize PNG files through the existing `.png` extension mapping and the planned content-based dispatcher.
- Decode PNG data with statically linked libpng and zlib, without using GDI or GDI+ for decoding, pixel conversion,
  alpha handling, color management, or resizing.
- Support the common PNG color types, bit depths, palettes, transparency, and Adam7 interlacing.
- Respect embedded color metadata with a deterministic conversion to the viewer's sRGB-like BGR24 display format.
- Composite alpha deterministically because the current `ImgBuffer` and renderer do not preserve alpha.
- Reuse `ImgResampler` for high-quality downscaling.
- Produce the existing bottom-up, DWORD-aligned BGR24 `ImgBuffer`.
- Preserve asynchronous loading, caching, error signaling, and folder navigation behavior.
- Supply reproducible x86/x64 Visual Studio and MSYS static dependency builds.

The initial scope is one static image. It does not include PNG encoding, APNG playback, frame selection, metadata
editing, HDR output, or changing the renderer to an alpha-capable surface.

## Why libpng

Use libpng rather than Windows Imaging Component, GDI+, or a small single-header decoder.

- libpng is the official PNG reference library and covers palette, grayscale, 16-bit, transparency, interlacing,
  ancillary chunks, CRC handling, and malformed-input validation.
- Its C API is stable, portable, and suitable for static builds with both MSVC and MinGW.
- It provides explicit resource limits and callback-based I/O, which are important for decoding untrusted image files.
- Its permissive license is compatible with the repository's BSD license and static distribution model.
- It works with Little CMS for the color-management behavior already present in ImgVw.

Do not route native PNG pixels through a temporary GDI+ bitmap. The final Win32 renderer may continue displaying the
completed BGR24 DIB; the restriction is on decoding and image processing.

## Recommended Dependency Versions

Start with:

- libpng `1.6.58`.
- zlib `1.3.2`.

As of June 20, 2026, these are the current public releases. libpng `1.6.58` contains the fixes from the 2025 and 2026
security releases and fixes a palette regression introduced in `1.6.56`. zlib `1.3.2` includes its 2026 audit and
portability fixes.

Pin exact official release archives and SHA-256 hashes in the dependency build script. Recheck both projects for newer
security releases immediately before implementation or release.

Do not depend on the libpng or zlib packages installed in MSYS2, vcpkg, or the developer's machine.

## Current Integration Points

PNG already has a distinct `ImgItem::Format::PNG`, but it currently routes to `ImgGDIItem`:

- `ImgItemHelper::GetImgFormatFromExtension()` maps `.png` to `PNG`.
- `ImgItemFactory::Create()` constructs `ImgGDIItem` for `PNG`.
- `ImgGDIItem` uses GDI+ for decode, resize, conversion, and buffer extraction.
- `ImgLoader` invokes `ImgItem::Load()` on worker threads.
- `ImgResampler` can downscale RGBA8 pixels independently of GDI+.
- `ImgHEIFItem` already contains useful patterns for mapped input, RGBA8 downscaling, embedded RGB ICC conversion,
  alpha composition, checked BGR24 conversion, RAII completion signaling, and error reporting.
- `ImgBuffer` stores a complete bottom-up BGR24 image and limits its byte count to `DWORD`.

Add a dedicated `ImgPNGItem`; do not extend `ImgGDIItem` with libpng-specific branches.

## Dependency Build and Vendored Layout

Add `scripts/build-libpng.ps1`, following the architecture/toolchain handling in the existing dependency scripts.
Every documented PowerShell invocation must include `-NoProfile`.

Suggested parameters:

```powershell
param(
    [string]$LibpngVersion = "1.6.58",
    [string]$ZlibVersion = "1.3.2",
    [ValidateSet("all", "vs", "msys", "source")]
    [string]$Mode = "all",
    [ValidateSet("all", "x86", "x64")]
    [string]$Arch = "all",
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$WorkRoot = (Join-Path $env:TEMP "imgvw-libpng"),
    [string]$MsysRoot = "",
    [switch]$Clean
)
```

Use out-of-tree CMake builds. Build zlib first into a private toolchain/architecture-specific staging prefix, then
configure libpng to use only that staged zlib.

Recommended minimal configuration:

```text
BUILD_SHARED_LIBS=OFF
PNG_SHARED=OFF
PNG_STATIC=ON
PNG_TESTS=OFF
PNG_TOOLS=OFF
PNG_FRAMEWORK=OFF
ZLIB_ROOT=<private staged zlib>
```

Disable architecture-specific assembly/SIMD initially if it complicates the Win32 build or XP compatibility gate.
Enable an optimization only after all four library variants and malformed-input tests pass. In particular, do not
allow CMake to silently select a system zlib or emit a DLL dependency.

Proposed layout:

```text
3rd-party/
  libpng/
    1.6.58
    LICENSE
    README
    png.h
    pngconf.h
    pnglibconf.h
    libpng16_static.lib       # Visual Studio Win32
    libpng16.a                # MSYS x86
    x64/
      libpng16_static.lib     # Visual Studio x64
    ucrt64/
      libpng16.a              # MSYS x64
  zlib/
    1.3.2
    LICENSE
    README
    zconf.h
    zlib.h
    zlibstatic.lib            # Visual Studio Win32
    libz.a                    # MSYS x86
    x64/
      zlibstatic.lib          # Visual Studio x64
    ucrt64/
      libz.a                  # MSYS x64
```

Use the actual installed archive names if upstream emits different names and reference them explicitly in the project.
Do not reuse MSVC archives for MinGW or x86 archives for x64.

The script should:

1. Download official libpng and zlib source archives.
2. Verify pinned SHA-256 hashes before extraction.
3. Locate Visual Studio through `vswhere` and MSYS2 through the same rules as the existing scripts.
4. Build static zlib and libpng for each requested toolchain and architecture.
5. Verify libpng was configured against the staged zlib.
6. Run upstream zlib and libpng tests for each build when the toolchain supports executing the target binaries.
7. Copy only consumed headers, archives, licenses, readmes, and empty version marker files.
8. Verify archive machine types and expected public symbols.
9. Fail if shared libraries, executables, or unexpected runtime DLL dependencies are selected for packaging.

For static links, keep dependency order explicit: libpng before zlib.

## Windows XP Compatibility Gate

libpng and zlib are portable C libraries, but the current compiler, C runtime, CMake configuration, and optional
optimizations can still introduce post-XP imports.

Before application integration:

1. Build a small memory-backed libpng decode probe with the exact release libraries for MSVC x86/x64 and MSYS
   x86/x64.
2. Decode palette, interlaced, 16-bit, and alpha PNGs and exercise malformed/truncated input.
3. Inspect the final probe executable:
   - MSVC: `dumpbin /imports`.
   - MSYS: `objdump -p`.
4. Reject unguarded imports newer than Windows XP.
5. Run the x86 release probe on a clean Windows XP SP3 VM.
6. Repeat the test with several concurrent decoder instances because normal ImgVw loading is parallel.

Pay special attention to zlib `1.3.2`'s one-time initialization implementation and any compiler-generated synchronization
or CPU-feature probing. If a current dependency cannot pass, first disable optional assembly or runtime CPU dispatch.
Do not silently pin an older release with known security defects.

## Decoder Class

Add:

- `src/image/ImgPNGItem.h`
- `src/image/ImgPNGItem.cpp`

`ImgPNGItem` should own the complete PNG operation. Do not expose `png_structp`, `png_infop`, zlib types, or libpng
callbacks outside the image layer.

Use `FileMapView` and a bounded in-memory read callback rather than libpng's filename/stdio API. The read state should
contain:

- start pointer;
- total mapped byte count;
- current offset.

The callback must reject reads that overflow `size_t` or extend beyond the mapping and call `png_error()` with a stable
message.

Use libpng's classic read API rather than `png_image_*`. The classic API is required to inspect color-profile chunks,
set limits, control transforms, process interlacing deliberately, and report detailed failures.

## libpng Error Handling and C++ Safety

libpng uses `setjmp`/`longjmp` for fatal decode errors. A `longjmp` does not run C++ destructors created after
`setjmp`, so the integration must isolate that boundary.

Recommended structure:

- A small RAII owner outside the jump region destroys `png_struct` and both info structs.
- A plain decode context owns pointers to buffers and fixed error storage.
- The function containing `setjmp(png_jmpbuf(...))` must not create non-trivial automatic C++ objects whose lifetime
  crosses a libpng call that can jump.
- Allocate vectors, profiles, and transforms either before entering the jump region, behind explicitly cleaned raw
  handles, or in a second C++ processing phase after all libpng calls have completed.
- Supply custom error and warning callbacks. Copy the fatal message into caller-owned fixed storage before calling
  `png_longjmp()`. Warnings may go to debug diagnostics without failing otherwise valid images.

Do not throw C++ exceptions through libpng C callbacks.

Always destroy libpng state with `png_destroy_read_struct()` on success and failure. Use the same `LoadCompletion`
pattern as `ImgHEIFItem` so `loadedevent_` is signaled on every exit.

## Security and Resource Limits

Treat PNG files as untrusted input.

Before reading image data:

- Validate the exact eight-byte PNG signature with `png_sig_cmp()`.
- Set conservative width and height limits with `png_set_user_limits()`.
- Set a bounded ancillary-chunk allocation limit with `png_set_chunk_malloc_max()`.
- Set a bounded ancillary-chunk cache count with `png_set_chunk_cache_max()`.
- Reject zero dimensions and dimensions that cannot fit the application's `INT`, `size_t`, `DWORD`, stride, or output
  buffer limits.
- Reject decoded images whose RGBA working set or final BGR24 buffer exceeds a documented application limit.
- Ignore unknown ancillary chunks unless a specific feature requires them.
- Keep critical-chunk CRC errors fatal. Record ancillary-chunk warnings and continue only when libpng's documented
  behavior leaves the core image valid.

Do not allocate based only on IHDR values before checked row-byte and total-size calculations.

The exact dimension, chunk, and memory limits should be named constants shared by validation and tests. Choose limits
that remain practical on 32-bit Windows XP; for example, constrain a single decoded working set well below the 2 GiB
process address-space ceiling.

## Decode and Normalize Flow

Recommended `ImgPNGItem::Load()` flow:

1. Set status to `Loading` and install completion signaling.
2. Map the file read-only.
3. Reject files shorter than the PNG signature and files too large for the callback's size representation.
4. Create libpng read and info structures with custom error/warning callbacks.
5. Install the memory read callback and resource limits.
6. Read IHDR and validate width, height, color type, bit depth, and interlace method.
7. Extract color metadata needed by the later color-management phase.
8. Configure transforms to normalize input:
   - palette to RGB;
   - grayscale below 8 bits to 8 bits;
   - `tRNS` to alpha;
   - grayscale/grayscale-alpha to RGB/RGBA;
   - 16-bit samples to 8-bit using `png_set_scale_16()` when available, with a documented fallback to
     `png_set_strip_16()`;
   - add opaque alpha when no alpha is present, producing RGBA8 for every image.
9. Call `png_read_update_info()` and verify the resulting format is RGBA8 and row bytes are at least `width * 4`.
10. Allocate one checked top-down RGBA working buffer and row-pointer array.
11. Read all rows with `png_read_image()`; let libpng handle Adam7 passes.
12. Call `png_read_end()` so trailing image data and relevant metadata are validated.
13. Leave the libpng jump region and destroy the libpng objects.
14. Apply color management according to the policy below.
15. Calculate aspect-ratio-preserving display dimensions without upscaling.
16. Downscale with `ImgResampler::DownscaleRgba8()` when needed.
17. Composite alpha and convert top-down RGBA to bottom-up padded BGR24.
18. Write the complete checked output to `ImgBuffer`.
19. Call `SetupDisplayParameters()` for the bottom-up DIB.
20. Set status to `Ready`.

On failure, retain a useful PNG-specific message and set `Status::Error`. Catch allocation and unexpected C++
exceptions outside the libpng callback boundary.

## Color Management

The viewer output is 8-bit BGR24 and is effectively treated as sRGB. Use PNG color metadata in this precedence:

1. Valid `iCCP` RGB profile.
2. Valid `sRGB` chunk.
3. Valid `cHRM` plus `gAMA`.
4. Valid `gAMA` only.
5. Untagged input, assumed sRGB.

For `iCCP`:

- Bound the decompressed profile size before copying it.
- Open it with Little CMS and require an RGB input color space.
- Transform RGBA color channels to sRGB while preserving alpha separately.
- Treat a malformed or non-RGB profile as recoverable metadata failure; continue with the next applicable PNG color
  metadata and record diagnostics.

For `sRGB`, no color transform is needed after normalization because the display buffer is sRGB-targeted.

For `cHRM` plus `gAMA`, construct a temporary Little CMS RGB profile from the PNG white point, primaries, and transfer
curve, then transform to sRGB. Validate all values before profile construction. For `gAMA` alone, use a documented
gamma-based conversion to sRGB or libpng's read transform, covered by reference-image tests.

Do not apply both libpng gamma correction and a Little CMS transform to the same pixels. Metadata extraction should
produce one explicit color-conversion decision.

Refactor reusable RGB-profile transformation and RGBA-to-BGR conversion out of `ImgHEIFItem.cpp` only if this can be
done without changing HEIF behavior. A small image-layer helper shared by HEIF and PNG is preferable to copying the
Little CMS and alpha code, but the PNG change should include regression tests for HEIF before moving that code.

## Alpha Policy

The renderer does not preserve alpha. Decode every PNG to RGBA8, retain alpha through color conversion and resizing,
and composite it onto the same solid background used by `ImgHEIFItem`.

The current HEIF path effectively uses black. Use black initially for consistency unless a separate product decision
changes both decoders together.

Requirements:

- Do not simply discard alpha.
- Respect palette `tRNS`, grayscale/RGB `tRNS`, grayscale-alpha, and RGBA images.
- Use premultiplied-alpha filtering during downscaling to avoid color fringes.
- Avoid transforming premultiplied RGB values as if they were straight color values.
- Fully transparent pixels must produce deterministic black output regardless of hidden RGB values.

Consider extracting a shared `RgbaDisplayConverter` that accepts top-down RGBA8, source alpha mode, optional RGB
profile information, target size, and background color, then emits bottom-up BGR24. Keep this independent of Win32 and
GDI+.

## 16-Bit PNG

The current display path is 8-bit, so native 16-bit display is out of scope.

- Accept valid 16-bit grayscale, grayscale-alpha, RGB, and RGBA input.
- Prefer `png_set_scale_16()` for rounded 16-to-8 conversion.
- Apply color metadata consistently with 8-bit input.
- Document that precision is reduced to the viewer's 8-bit display buffer.
- Test low values and gradients to catch byte-order and truncation errors.

Do not allocate a second full 16-bit image unless color-management correctness requires it. If tests show that
converting to 8-bit before Little CMS causes unacceptable results, revise the working format to RGBA16 and add a
specific memory-impact review.

## Interlacing and APNG

Support standard Adam7 interlacing through libpng.

APNG playback is out of scope. Standard libpng `1.6.x` does not provide full APNG frame decoding. For an APNG file,
display the normal PNG default image produced by the standard PNG data stream. Ignore animation chunks as ancillary
unknown chunks, and add a fixture proving the result is deterministic.

Do not vendor an APNG-patched libpng or add a second animation decoder in this change.

## Format Detection and Factory Integration

Keep `ImgItem::Format::PNG`.

Update `ImgItemFactory::Create()` so `PNG` constructs `ImgPNGItem` rather than `ImgGDIItem`.

The planned content dispatcher in `artifacts/content_based_image_dispatch_plan.md` currently routes PNG content to
`ImgGDIItem`; update that plan and implementation to route recognized PNG signatures to `ImgPNGItem`.

Content detection must continue to override extensions:

- PNG bytes named `.heic` route to `ImgPNGItem`.
- JPEG bytes named `.png` route to `ImgJPEGItem`.
- Unknown bytes named `.png` route to `ImgPNGItem` through extension fallback and fail with a PNG-specific error.

Other formats may continue using `ImgGDIItem`.

## Project and Build Integration

Update together:

- `ImgVw.vcxproj`
- `ImgVw.vcxproj.filters`
- `Makefile`
- `scripts/tidy.ps1`
- `README.md`

Visual Studio changes:

- Add `3rd-party\libpng` and `3rd-party\zlib` include directories.
- Add the new PNG source/header files.
- Link architecture-matched static libpng and zlib archives.
- Keep library order explicit where relevant.

Makefile changes:

- Add `ImgPNGItem.o`.
- Add libpng and zlib include and architecture-specific library paths.
- Link `-lpng16 -lz` after ImgVw objects.

Static archives from MSVC and MinGW are not interchangeable. Verify that the final executable does not depend on
`libpng*.dll` or `zlib*.dll`.

Update `README.md` to list libpng and zlib, document their rebuild command, and state that PNG decoding is native.

## Tests and Fixtures

Add redistributable samples under `artifacts/sample/png/` with source and license information. Prefer compact PngSuite
fixtures where licensing permits, supplemented by purpose-built images with known pixel values.

Cover:

- RGB and RGBA at 8 and 16 bits;
- grayscale and grayscale-alpha at 1, 2, 4, 8, and 16 bits where valid;
- indexed color at 1, 2, 4, and 8 bits;
- palette `tRNS`;
- grayscale and RGB color-key `tRNS`;
- opaque, partially transparent, and fully transparent pixels;
- each standard PNG filter type;
- non-interlaced and Adam7-interlaced images;
- odd widths that exercise BGR24 row padding;
- `iCCP`, `sRGB`, `cHRM` plus `gAMA`, `gAMA` only, and untagged files;
- malformed, oversized, and decompression-bomb-style inputs;
- bad critical CRC and bad ancillary CRC;
- truncated signature, IHDR, IDAT, and IEND;
- unknown ancillary chunks;
- APNG with a defined default image;
- a valid PNG named `.heic`;
- a JPEG named `.png`.

Add focused tests for pure helpers:

- display-size calculation;
- checked RGBA and BGR stride/size calculations;
- memory read callback bounds;
- every normalization color type;
- top-down RGBA to bottom-up BGR conversion;
- alpha composition and premultiplied resampling;
- ICC/gamma metadata precedence;
- color-profile size and color-space validation;
- libpng error-message capture;
- resource-limit rejection.

Where pixel correctness matters, compare exact output bytes or use a small tolerance for color-managed results. Do not
rely only on screenshots.

## Manual Verification

For representative fixtures:

- Open directly and discover through folder browsing.
- Navigate repeatedly between JPEG, PNG, HEIF, and remaining GDI+ formats.
- Verify channel order, row orientation, row padding, aspect ratio, centering, and no upscaling.
- Compare palette, grayscale, 16-bit, interlaced, and transparent output with a trusted color-managed viewer.
- Verify color-managed samples against known reference values.
- Confirm malformed files report errors without crashing or wedging loader threads.
- Test shutdown while several PNG loads are queued or active.
- Monitor memory and handle counts during repeated large-image loads.

## Build Verification

Dependency builds:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-libpng.ps1 -Mode vs -Arch all -Clean
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-libpng.ps1 -Mode msys -Arch x86 -Clean
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-libpng.ps1 -Mode msys -Arch x64 -Clean
```

Application builds:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x86 -Clean
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x64 -Clean
```

Also build Visual Studio Release Win32 and x64, run the native tests, run the formatting check and clang-tidy, inspect
final imports, and execute the Windows XP SP3 x86 compatibility test.

## Implementation Order

1. Implement `build-libpng.ps1`, produce all static dependency variants, and run the XP compatibility probe.
2. Add PNG fixtures and low-level tests before replacing the current decoder.
3. Add `ImgPNGItem` with mapped input, signature validation, callbacks, limits, and safe `setjmp` isolation.
4. Normalize all supported PNG types to RGBA8 and validate interlaced and 16-bit inputs.
5. Add checked alpha composition and BGR24 output without GDI/GDI+.
6. Reuse `ImgResampler` for downscaling and verify transparent-edge behavior.
7. Add color metadata extraction and Little CMS conversion.
8. Route `ImgItem::Format::PNG` and detected PNG content to `ImgPNGItem`.
9. Update all build files, static-analysis configuration, documentation, and third-party notices.
10. Run the full fixture, build, import, concurrency, memory, and XP verification matrix.

## Acceptance Criteria

PNG support is complete when:

- PNG decoding, normalization, alpha handling, color conversion, and resizing use libpng/first-party code, not GDI or
  GDI+.
- `.png` files and PNG-signature files route to `ImgPNGItem`.
- Palette, grayscale, RGB, alpha, `tRNS`, 8/16-bit, and Adam7 files display correctly.
- Output has correct BGR channels, bottom-up row order, DWORD padding, aspect ratio, and centering.
- Embedded color metadata follows the documented precedence and produces tested sRGB-targeted output.
- Transparent images composite deterministically without resize fringes.
- Malformed, truncated, oversized, and hostile files fail without memory corruption, excessive allocation, crashes, or
  stuck loader events.
- APNG files display only their default static image and do not imply animation support.
- Visual Studio and MSYS release builds pass for x86 and x64.
- The final executable has no accidental libpng/zlib DLL dependency.
- Windows XP compatibility is demonstrated by the defined gate or explicitly escalated before merging.
- libpng and zlib license, notice, and redistribution requirements are documented and satisfied.

## Upstream References

- libpng home page, release information, checksums, and security notices:
  <https://www.libpng.org/pub/png/libpng.html>
- libpng source repository:
  <https://github.com/pnggroup/libpng>
- libpng manual:
  <https://www.libpng.org/pub/png/libpng-manual.txt>
- zlib home page and release information:
  <https://zlib.net/>
- zlib change log:
  <https://zlib.net/ChangeLog.txt>
- PNG specification:
  <https://www.w3.org/TR/png-3/>
