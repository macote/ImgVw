# JPEG, PNG, and HEIF Color Management Plan

## Summary

Add one explicit color-management policy for every image decoder:

> Convert decoded source pixels to 8-bit sRGB BGR before they enter `ImgBuffer` and the existing GDI rendering path.

This preserves the current 24-bit `CreateDIBSection`/`BitBlt` renderer and Windows XP compatibility while producing
predictable SDR output for RGB, grayscale, CMYK, wide-gamut, and future HEIF inputs.

Initial work covers source color management only. Monitor-profile conversion, Windows 11 Advanced Color, native HDR
output, and a higher-precision display pipeline are separate future features.

## Current State

- JPEG uses libjpeg-turbo and extracts APP2 ICC data.
- Embedded ICC profiles are opened only for CMYK/YCCK JPEGs.
- `ImgItem::OpenICCProfile()` rejects non-CMYK profiles.
- CMYK/YCCK pixels are transformed through Little CMS to 8-bit sRGB BGR.
- Untagged CMYK/YCCK requires the user-selected default CMYK profile.
- RGB/YCbCr JPEGs are decoded directly to BGR; embedded RGB profiles are ignored.
- PNG and other GDI+ formats use `Gdiplus::Bitmap(filepath, FALSE)`, so embedded color management is disabled.
- The display path stores untagged 24-bit BGR and renders it with GDI.
- Basic HEIF support is already implemented with libheif, RGBA decode, alpha-aware downscaling, embedded RGB ICC
  handling, and BGR output.
- Current HEIF embedded ICC conversion happens after resizing. This is acceptable as a known interim behavior, but the
  target policy remains color conversion before resize where practical.
- `artifacts/libheif_support_plan.md` remains useful as historical context for dependency integration, decode flow,
  transforms, alpha, and build wiring. This plan supplies the shared color policy used by follow-up HEIF color work.

## Product Decisions

### Internal display color space

- Treat every `ImgBuffer` handed to the renderer as 8-bit sRGB BGR.
- Perform color conversion before resize and rotation where practical.
- Keep alpha handling separate from color transforms; alpha is not color-managed.
- Treat untagged ordinary RGB and grayscale images as sRGB.
- Do not attach profiles to GDI bitmaps or depend on implicit GDI/GDI+ color management.

### Rendering scope

The first implementation intentionally converts to nominal sRGB, not to the current monitor profile. This is a stable
baseline for the existing renderer and avoids:

- invalidating image-cache entries when a window moves between monitors;
- GDI ICM and compatible-DC double-conversion hazards;
- dependence on Windows-version-specific color behavior;
- changing the Windows XP compatibility target.

A later monitor-aware renderer can transform the normalized sRGB buffer to the active monitor profile or replace the
GDI output path.

### Failure policy

- Malformed or incompatible embedded color metadata must not crash the loader.
- Every valid embedded ICC profile compatible with the decoded pixel color model must take precedence over
  application defaults, assumed color spaces, and format-level fallback metadata.
- For RGB/grayscale images, ignore invalid metadata and fall back to sRGB.
- For CMYK/YCCK JPEGs, use a valid embedded CMYK profile, then the configured default CMYK profile.
- If no usable CMYK profile exists, retain the current explicit load error rather than silently inventing a CMYK
  interpretation.
- Record enough error detail to distinguish invalid profile, unsupported profile color space, transform creation
  failure, and allocation failure.

## Shared Color-Management Architecture

Move profile parsing and transforms out of the CMYK-specific `ImgItem` methods into a small image-layer component.
Suggested files:

- `src/image/ColorProfile.h`
- `src/image/ColorProfile.cpp`
- `src/image/ColorTransform.h`
- `src/image/ColorTransform.cpp`

The exact split may be reduced to one pair of files if the implementation remains small.

Responsibilities:

- own `cmsHPROFILE` and `cmsHTRANSFORM` through RAII;
- validate profile size before passing untrusted data to Little CMS;
- inspect profile color space and device class;
- create an sRGB destination profile;
- transform RGB, BGR, grayscale, or reversed CMYK input to BGR;
- validate stride, dimensions, and allocation sizes before transforming;
- expose explicit status/result values instead of mutating `iccprofileloadfailed_` as the only failure signal.

Suggested internal types:

```cpp
enum class SourceColorModel
{
    Gray8,
    Rgb8,
    Bgr8,
    Cmyk8Reversed
};

enum class ColorProfileSource
{
    None,
    EmbeddedIcc,
    EmbeddedNclx,
    PngSrgb,
    PngGammaChromaticity,
    DefaultCmyk,
    AssumedSrgb
};

struct ColorTransformResult
{
    bool succeeded;
    ColorTransformStatus status;
    ImgBuffer buffer;
};
```

Do not expose Little CMS handles outside the image subsystem.

## JPEG Support

### Required behavior

1. Continue saving and assembling JPEG APP2 ICC markers.
2. Determine the decoder source model from libjpeg:
   - `JCS_GRAYSCALE`;
   - RGB/YCbCr decoded to BGR;
   - CMYK/YCCK decoded to CMYK.
3. Validate an embedded ICC profile against the decoded source model:
   - RGB profile for RGB/YCbCr;
   - gray profile for grayscale;
   - CMYK profile for CMYK/YCCK.
4. Apply these rules:
   - valid embedded RGB ICC: BGR through embedded profile to sRGB BGR;
   - valid embedded gray ICC: gray through embedded profile to sRGB BGR;
   - valid embedded CMYK ICC: reversed CMYK through embedded profile to sRGB BGR;
   - untagged RGB/YCbCr or grayscale: assume sRGB;
   - untagged CMYK/YCCK: use configured default CMYK profile.
5. Treat a mismatched or malformed RGB/gray profile as invalid metadata and use the sRGB fallback.
6. Preserve EXIF orientation behavior.

### CMYK inversion verification

Do not expand CMYK support until the existing `TYPE_CMYK_8_REV` assumption is tested against:

- CMYK JPEG with Adobe APP14;
- YCCK JPEG with Adobe APP14;
- CMYK JPEG without APP14;
- embedded and default-profile cases.

If libjpeg output inversion differs by source type or marker state, make the selected Little CMS input format explicit
instead of applying `TYPE_CMYK_8_REV` universally.

### JPEG implementation notes

- Refactor `ImgItem::OpenICCProfile()` so profile validation is requested by expected source color space instead of
  hard-coded to CMYK.
- Replace `TranformCMYK8ColorsToBGR8()` with a correctly spelled, general transform entry point.
- Preserve the current decode scaling optimization.
- Prefer conversion before GDI+ resize so resizing operates on normalized sRGB pixels.

## PNG Support

PNG pixel data can be grayscale, indexed, RGB, or include alpha; it is not CMYK. Color metadata still determines what
the RGB values mean.

### Metadata precedence

Use the highest-precedence supported valid color description:

1. `iCCP` embedded ICC profile;
2. `cICP`, when implemented;
3. `sRGB`;
4. `gAMA` plus `cHRM`;
5. no usable metadata: assume sRGB.

Follow the PNG specification's conflict and precedence rules rather than combining mutually exclusive descriptions.

### Initial PNG milestone

Replace the implicit GDI+ PNG decode path with a decoder path that exposes pixel and metadata decisions. The first
milestone must support:

- grayscale, grayscale+alpha, indexed, RGB, and RGBA PNG;
- 8-bit output;
- transparency composited using the viewer's defined background policy;
- `iCCP` RGB and grayscale profiles;
- `sRGB`;
- untagged-image sRGB fallback.

Add `gAMA` + `cHRM` conversion next. Supporting these chunks requires constructing a source RGB profile with the
specified primaries, white point, and transfer curve, then converting to sRGB.

Add PNG `cICP`, PQ/HLG, and 16-bit-to-high-precision processing only with the HDR milestone. Until then:

- decode ordinary 16-bit SDR PNG with deliberate down-conversion;
- report unsupported HDR transfer metadata clearly or use a documented SDR tone-map implementation;
- do not interpret PQ/HLG values as ordinary sRGB.

### PNG decoder selection

Evaluate libpng or WIC against these requirements. Preserve XP compatibility:

- libpng gives explicit access to all relevant chunks and predictable behavior;
- WIC is available from Windows XP SP2 but must be verified for required metadata, profile, and deployment behavior;
- GDI+ with `useEmbeddedColorManagement=TRUE` is insufficient as the shared architecture because its exact profile,
  metadata-precedence, and output-space behavior is not explicit enough for consistent JPEG/PNG/HEIF handling.

Record the decoder choice in this artifact before implementation and update the Visual Studio project, filters,
Makefile, and dependency scripts together.

## HEIC/HEIF Support

Basic HEIF decode support is already implemented. Follow-up color-management work should refactor the existing local
embedded ICC conversion into the shared color-management helpers, then add explicit NCLX and HDR policy.

Known issue:

- Current HEIF ICC conversion happens after RGBA downscaling. This may resize in the source profile rather than in the
  normalized sRGB space. Keep it documented until shared HEIF color conversion can be moved before resize or proven
  equivalent for the supported SDR path.

### Color metadata behavior

1. Query raw ICC and NCLX independently because a HEIF image may contain both.
2. Prefer a valid embedded RGB ICC profile for source-to-sRGB color conversion.
3. If no usable ICC profile exists, use NCLX:
   - color primaries;
   - transfer characteristics;
   - matrix coefficients;
   - full/limited range.
4. If neither exists and the image is ordinary SDR, use a documented BT.709/sRGB fallback.
5. Verify whether the selected libheif decode API has already applied matrix/range/NCLX conversion. Do not apply the
   same conversion twice.
6. Treat HEIF as RGB/YCbCr-oriented; do not add a default CMYK-profile concept.

### SDR HEIF

- Decode at the highest practical precision exposed by the selected library path.
- Convert YCbCr and range correctly.
- Convert Display P3, BT.2020, or other source primaries to sRGB.
- Apply the source transfer function correctly before gamut conversion.
- Quantize to 8-bit BGR only at the end of color conversion.

### HDR HEIF

PQ and HLG require tone mapping for the current SDR renderer. Do not treat 10-bit decode followed by bit truncation as
HDR support.

The initial HEIF release must choose and document one of these policies:

- include a tested HDR-to-SDR tone-map and gamut-map path; or
- reject PQ/HLG images with a clear unsupported-color-transfer error while still supporting 10-bit SDR HEIF.

The preferred implementation is a bounded, deterministic tone mapper producing sRGB SDR. Native HDR display is out of
scope for the GDI renderer.

## Processing Order

Use this order unless a decoder requires an equivalent fused operation:

1. Parse and validate dimensions and metadata.
2. Decode source samples without discarding useful precision.
3. Apply codec/container range and matrix conversion.
4. Resolve the source color description.
5. Convert or tone-map to sRGB.
6. Composite alpha in linear light when partial alpha is present.
7. Quantize to 8-bit BGR.
8. Resize and rotate.
9. Store in `ImgBuffer`.
10. Render through the existing GDI path.

For performance, operations may be fused, but tests must demonstrate equivalent output.

## Implementation Phases

### Phase 1: Shared ICC foundation

- Add RAII wrappers for Little CMS profiles and transforms.
- Add checked buffer-size and stride calculations.
- Migrate only the existing CMYK/YCCK JPEG transform first, preserving current behavior and output.
- Keep `TYPE_CMYK_8_REV` behavior unchanged until dedicated inversion fixtures are available.
- Keep the current default CMYK profile UI and storage behavior.
- Replace low-level Little CMS handle ownership in `ImgItem` with shared helper ownership.
- Add RGB and grayscale transforms only after the CMYK path is behavior-preserving and tested.

### Phase 2: JPEG RGB and grayscale profiles

- Accept and validate embedded RGB and grayscale ICC profiles.
- Apply RGB/gray-to-sRGB conversion.
- Preserve the untagged RGB fast path.
- Verify CMYK/YCCK inversion and fallback behavior.
- Add fixtures and automated transform tests.

### Phase 3: Explicit PNG decoder

- Select and integrate the PNG decoder.
- Decode all baseline PNG color types and alpha.
- Implement `iCCP`, `sRGB`, and untagged fallback.
- Add `gAMA` + `cHRM`.
- Add 16-bit SDR down-conversion tests.

### Phase 4: HEIF SDR color

- Refactor the existing HEIF embedded ICC conversion to use the shared color helpers.
- Add independent ICC and NCLX queries.
- Implement ICC and NCLX source-to-sRGB conversion.
- Move HEIF color conversion before resize where practical, or document and test any equivalent fused order.
- Verify P3 and BT.2020 SDR samples against a reference viewer.

### Phase 5: HDR-to-SDR policy

- Detect PQ and HLG explicitly.
- Implement and test tone mapping or return a clear unsupported status.
- Keep native HDR output and Advanced Color integration out of this phase.

### Phase 6: Optional monitor-aware output

- Discover the profile for the monitor displaying the window.
- Define cache invalidation when monitor/profile changes.
- Transform normalized sRGB to monitor RGB or replace GDI with a color-managed renderer.
- Test multi-monitor movement and profile changes.
- Retain an XP-compatible fallback.

## Files Expected to Change

- `src/image/ImgItem.h`
- `src/image/ImgItem.cpp`
- `src/image/ImgJpegDecoder.h`
- `src/image/ImgJpegDecoder.cpp`
- `src/image/ImgJPEGItem.cpp`
- `src/image/ImgGDIItem.cpp` or its PNG replacement
- `src/image/ImgItemFactory.h`
- new shared color-management files under `src/image/`
- new PNG decoder/item files under `src/image/`
- HEIF files listed in `artifacts/libheif_support_plan.md`
- `ImgVw.vcxproj`
- `ImgVw.vcxproj.filters`
- `Makefile`
- dependency build scripts if libpng or additional color support is added
- test target and fixture documentation

Do not edit vendored sources unless a separately documented dependency patch is required.

## Test Assets

Extend `artifacts/sample/` or the future test-fixture directory with licensed, provenance-documented files:

### JPEG

- untagged sRGB JPEG;
- JPEG with embedded sRGB profile;
- Adobe RGB JPEG;
- Display P3 or ProPhoto RGB JPEG;
- grayscale JPEG with and without a gray ICC profile;
- current CMYK fixtures;
- YCCK and CMYK inversion variants;
- malformed and color-space-mismatched profiles.

### PNG

- each PNG color type;
- alpha at 0%, 50%, and 100%;
- untagged, `sRGB`, `iCCP`, and `gAMA` + `cHRM`;
- wide-gamut ICC;
- conflicting metadata for precedence tests;
- 16-bit SDR;
- malformed compressed ICC data;
- PQ/HLG `cICP` samples when HDR work begins.

### HEIF

- untagged SDR;
- sRGB ICC;
- Display P3 ICC;
- NCLX-only BT.709;
- NCLX-only Display P3-compatible primaries;
- BT.2020 SDR;
- files containing both ICC and NCLX;
- full- and limited-range YCbCr;
- 8-, 10-, and 12-bit inputs where supported;
- PQ and HLG HDR;
- malformed or oversized profile metadata.

## Automated Verification

- Unit-test profile validation by profile color space.
- Unit-test checked stride and allocation calculations.
- Unit-test known pixel transforms with tolerances:
  - RGB ICC to sRGB;
  - gray ICC to sRGB;
  - CMYK ICC to sRGB;
  - NCLX matrix/range conversion;
  - PNG gamma/chromaticity conversion.
- Test malformed profile data and transform-creation failure.
- Test metadata precedence independently from pixel decoding.
- Add regression tests proving that compatible embedded RGB, grayscale, and CMYK ICC profiles are never bypassed in
  favor of assumed sRGB, NCLX, PNG fallback chunks, the configured CMYK profile, or the bundled CMYK fallback.
- Compare fixture output against precomputed reference pixels or hashes generated by a documented color-managed tool.
- Allow small per-channel tolerances where CMM implementations or rounding differ.
- Run x86 and x64 release builds through `scripts/build-msys.ps1`.
- Run the Visual Studio build when the toolchain is available.
- Run formatting checks for all changed first-party C++ files.

## Manual Verification

- Compare wide-gamut JPEG, PNG, and HEIF files side by side with a known color-managed reference viewer.
- Confirm ordinary untagged images remain visually unchanged.
- Confirm CMYK JPEG behavior and default-profile selection remain intact.
- Check gradients for banding after 16-bit or 10-bit conversion.
- Check dark and bright HDR regions after tone mapping.
- Navigate repeatedly among mixed JPEG, PNG, and HEIF files to exercise cache and worker-thread ownership.
- Test on an sRGB display and, when available, a wide-gamut display.

## Performance Constraints

- Preserve libjpeg scaled decode.
- Avoid creating the same destination sRGB profile repeatedly where safe ownership allows reuse.
- Cache Little CMS transforms only when the profile identity, source pixel format, intent, and flags all match.
- Place strict bounds on embedded profile size and decoded image allocation.
- Never block paint or UI message handling on color transformation.
- Measure conversion cost before adding global transform caches; correctness and ownership safety take priority.

## Security and Robustness

- Treat ICC, PNG chunks, NCLX metadata, and image dimensions as untrusted.
- Reject arithmetic overflow before allocation.
- Bound ICC profile sizes before copying or opening them.
- Check every Little CMS allocation and transform result.
- Avoid process-global mutable error handlers unless access and lifetime are controlled.
- Keep decoder failures local to the image item and signal completion normally.
- Add fuzzing targets for metadata parsing when a test/fuzz harness exists.

## Acceptance Criteria By Milestone

### Milestone 1: JPEG CMYK Refactor

- existing CMYK/YCCK JPEG behavior is preserved;
- embedded CMYK ICC, configured default CMYK profile, and bundled fallback precedence remains unchanged;
- Little CMS profile and transform handles are owned by shared RAII helpers;
- checked stride/allocation behavior remains in place for CMYK-to-BGR conversion;
- x86 and x64 builds remain functional.

### Milestone 2: JPEG RGB And Grayscale ICC

- regression tests cover the previously ignored embedded RGB/YCbCr JPEG ICC case;
- embedded RGB and grayscale JPEG ICC profiles are honored;
- malformed or mismatched RGB/gray profiles fall back to assumed sRGB without crashing;
- the untagged RGB fast path remains visually unchanged.

### Milestone 3: Explicit PNG Decoder

- PNG `iCCP`, `sRGB`, `gAMA` + `cHRM`, and untagged fallback are explicit and tested;
- all baseline PNG color types and alpha cases in the PNG plan decode to the documented display format;
- malformed PNG color metadata cannot crash the application or cause unchecked allocation.

### Milestone 4: HEIF NCLX And SDR ICC

- existing HEIF embedded ICC conversion uses the shared color helpers;
- HEIF ICC and NCLX SDR paths are explicit and tested;
- current post-resize ICC conversion is either moved before resize or justified with equivalence tests;
- Display P3, BT.2020 SDR, full-range, and limited-range samples are verified against references.

### Milestone 5: HDR Policy

- PQ/HLG images are either tone-mapped correctly or rejected with a specific error;
- HDR handling does not silently truncate PQ/HLG content as ordinary SDR;
- native HDR output remains out of scope for the GDI renderer.

Color-management support is complete for the initial SDR scope when all applicable milestones satisfy these global
criteria:

- every rendered `ImgBuffer` has a documented 8-bit sRGB BGR contract;
- every valid embedded ICC profile compatible with the decoded pixel color model takes precedence over assumed,
  configured, bundled, and format-level fallback color descriptions;
- malformed metadata cannot crash the application or cause unchecked allocation;
- x86 and x64 builds remain functional;
- Windows XP compatibility is preserved unless a separate decision explicitly changes the target.

## References

- PNG Specification, Third Edition: <https://www.w3.org/TR/png-3/>
- ICC profile embedding guidance: <https://www.color.org/technotes/ICC-Technote-ProfileEmbedding.pdf>
- libheif color-profile API:
  <https://github.com/strukturag/libheif/blob/master/libheif/api/libheif/heif_color.h>
- Windows `IWICColorTransform`:
  <https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nn-wincodec-iwiccolortransform>
- Windows `SetICMMode`:
  <https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-seticmmode>
- Existing HEIF plan: `artifacts/libheif_support_plan.md`
