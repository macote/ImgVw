# Color Management Follow-Up Plan

## Status

**Active.**

The CMYK JPEG foundation is complete: the application extracts JPEG ICC data, uses the bundled CMYK fallback when
needed, owns profiles through `ColorProfile`, performs the reversed-CMYK-to-BGR transform through `ColorTransform`,
and covers the fallback profile and transform in native tests. RGB and grayscale JPEG ICC transforms, native PNG
decoding, and consistent HEIF color handling are not implemented.

This plan supersedes the remaining implementation work in `archive/color_management_support_plan.md`; that document
remains the detailed product and fixture reference.

## Scope

Deliver colour-correct SDR output targeting sRGB for JPEG and HEIF first. Native PNG support is deliberately a
separate dependency project under `libpng_support_plan.md`; it joins this plan only after its decoder is available.
Do not add monitor-profile output or HDR rendering in this phase.

## Phase 1: Generalize Shared Transforms

1. Extend `ColorProfile` with checked colour-space inspection for RGB and grayscale profiles.
2. Add explicit RGB8/RGBA8 and Gray8-to-BGR8 transform entry points to `ColorTransform`.
3. Retain the existing reversed-CMYK transform unchanged and share only RAII, allocation, stride validation, and
   result reporting.
4. Bound embedded-profile sizes before copying or passing data to Little CMS.
5. Add unit tests for profile rejection, transform creation failure, row stride validation, and known-pixel output.

## Phase 2: JPEG RGB and Grayscale ICC

1. Keep untagged RGB JPEG on its current fast path.
2. For tagged RGB and grayscale JPEGs, validate the embedded profile's colour space and convert to sRGB/BGR before
   resizing.
3. Preserve the current CMYK/YCCK profile precedence: embedded profile, user CMYK default, then bundled fallback.
4. Treat malformed, mismatched, or oversized profiles as a local decode failure with a diagnostic reason; never use
   a CMYK fallback for RGB or grayscale content.
5. Add licensed fixtures for sRGB, Adobe RGB or Display P3, grayscale, malformed profiles, CMYK, and YCCK.

## Phase 3: HEIF SDR Consolidation

1. Move HEIF ICC handling onto `ColorProfile` and `ColorTransform`; remove duplicated direct Little CMS ownership.
2. Query embedded ICC and NCLX metadata independently, with documented precedence of valid ICC over valid NCLX.
3. Convert SDR RGB content to sRGB before resizing where practical, and test alpha/stride behaviour.
4. Detect PQ and HLG explicitly and return a clear unsupported-image result until a separately approved tone-mapping
   design is implemented.

## Phase 4: Integrate Native PNG Colour Work

After `libpng_support_plan.md` has delivered `ImgPNGItem`, add `iCCP`, `sRGB`, and `gAMA` plus `cHRM` precedence
handling. The PNG implementation must use the shared helpers from Phases 1-3 rather than introducing another Little
CMS wrapper.

## Verification and Exit Criteria

- Add exact or tolerance-based reference-pixel tests for every supported transform.
- Verify untagged JPEG output remains unchanged.
- Compare tagged JPEG and HEIF SDR fixtures with a known colour-managed viewer.
- Run `scripts/test-msys.ps1` and x86/x64 release builds for each implementation phase.
- Do not mark this plan complete until JPEG RGB/grayscale and HEIF SDR share the helper boundary. PNG completion is
  tracked by its own plan.
