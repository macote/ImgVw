# Little CMS Upgrade Plan

Date: 2026-06-17

## Goal

Upgrade ImgVw's vendored `3rd-party/Little-CMS` package from the current Little CMS 2.9-era drop to a current supported
Little CMS release, while preserving the existing static-link layout for both build paths:

- Visual Studio: Win32 and x64, linked as `lcms2_static.lib`.
- MSYS/Makefile: x86 and x64, linked as `liblcms2.a`.

Keep the change conservative: do not refactor image-loading code unless the newer headers or libraries require it, and
do not change the documented Windows XP compatibility target.

## Current State

ImgVw vendors a minimal Little CMS package:

- Header at `3rd-party/Little-CMS/lcms2.h`.
- Visual Studio Win32 library at `3rd-party/Little-CMS/lcms2_static.lib`.
- Visual Studio x64 library expected at `3rd-party/Little-CMS/x64/lcms2_static.lib`.
- MSYS x86 library at `3rd-party/Little-CMS/liblcms2.a`.
- MSYS x64 library expected by the Makefile at `3rd-party/Little-CMS/ucrt64/liblcms2.a`.
- A zero-byte provenance marker named `3rd-party/Little-CMS/7e302af92e5602e8d99b05b10f9a6fe80c8ea688`.

The vendored `lcms2.h` defines `LCMS_VERSION 2090`, so the current header is Little CMS 2.9-era. ImgVw uses Little CMS
only in `src/image/ImgItem.*` for ICC profile handling and CMYK-to-BGR conversion:

- `cmsOpenProfileFromMem`
- `cmsCloseProfile`
- `cmsCreate_sRGBProfile`
- `cmsCreateTransform`
- `cmsDoTransformLineStride`
- `cmsDeleteTransform`
- Format and intent constants such as `TYPE_CMYK_8_REV`, `TYPE_BGR_8`, and `INTENT_PERCEPTUAL`

This narrow API usage should make the source-level upgrade low risk. The higher-risk part is producing static libraries
that match ImgVw's supported Windows toolchains and architectures.

## Recommended Target Version

Use Little CMS `2.19.1` as the initial upgrade target.

Rationale:

- GitHub marks `2.19.1` as the latest release tag: `lcms2.19.1`.
- The `2.19.1` tag resolves to commit `21c582a594fe5279f90c0b93437c398f93bf62b0`.
- `2.19.1` is a hot-fix release after the feature release `2.19`, so it is the conservative target over `2.19`.
- `2.19` added the upstream CMake build system and expanded CI coverage for Cygwin and MSYS, which should make ImgVw's
  two Windows build paths easier to reproduce.
- Recent Little CMS releases include hardening fixes for malformed profiles and `.cube` input handling. Even though ImgVw
  does not use all of those code paths directly, image metadata and embedded ICC profiles are untrusted input.

## Proposed Build Output Layout

Preserve current include and link paths so the first upgrade does not require project file churn:

```text
3rd-party/Little-CMS/
  2.19.1
  LICENSE
  README.md
  lcms2.h
  lcms2_static.lib       # VS Win32
  liblcms2.a             # MSYS x86
  x64/
    lcms2_static.lib     # VS x64
  ucrt64/
    liblcms2.a           # MSYS x64
```

Remove the old zero-byte commit marker after the replacement is verified, and replace it with a zero-byte `2.19.1`
marker or a small `VERSION.txt`. Prefer the zero-byte marker if matching the existing dependency style is the priority;
prefer `VERSION.txt` if provenance clarity is more important.

Do not vendor Little CMS build trees, object files, generated test output, or command-line utilities.

## Proposed Script

Add `scripts/build-little-cms.ps1` so the binary drop is reproducible. The script should download the official GitHub
release archive or source tarball, build out-of-tree, and copy only the files ImgVw consumes into `3rd-party/Little-CMS`.

Recommended script behavior:

1. Parameters:
   - `Version = "2.19.1"`
   - `Tag = "lcms2.19.1"`
   - `RepoRoot = ..`
   - `WorkRoot = $env:TEMP\imgvw-little-cms`
2. Download source from `https://github.com/mm2/Little-CMS`.
3. Verify the tag/commit if `git` is available:
   - `lcms2.19.1`
   - `21c582a594fe5279f90c0b93437c398f93bf62b0`
4. Build Visual Studio Win32 and x64 static libraries.
5. Build MSYS x86 and x64 static libraries.
6. Copy:
   - `include/lcms2.h` or the release root's `include/lcms2.h`
   - license/readme files useful for attribution
   - `lcms2_static.lib`
   - `liblcms2.a`
7. Write the provenance marker only after all expected outputs exist.

Prefer upstream CMake for new automation because `2.19` added a CMake build system. If the CMake static-library target
name differs by generator, keep the copy step target-aware rather than renaming arbitrary library files blindly.

## Integration Steps

1. Add the build script under `scripts/`.
2. Build Little CMS static libraries for:
   - Visual Studio Win32
   - Visual Studio x64
   - MSYS x86
   - MSYS x64
3. Replace only the vendored Little CMS header, license/readme files, static libraries, and provenance marker.
4. Confirm `ImgVw.vcxproj`, `ImgVw.vcxproj.filters`, and `Makefile` still point at valid files.
5. Build ImgVw:
   - `make`
   - `make arch=x64`
   - Visual Studio Win32 Debug or Release
   - Visual Studio x64 Debug or Release
6. Fix compile/link errors with minimal source changes, expected only around headers or static runtime settings.
7. Smoke test color-management behavior:
   - JPEG without embedded ICC profile.
   - RGB JPEG with embedded sRGB ICC profile.
   - CMYK or YCCK JPEG with embedded ICC profile.
   - CMYK JPEG that falls back to the default ICC profile selected by `Ctrl + I`.
   - Missing or invalid default ICC profile.
   - Large JPEG with embedded ICC profile.

## Expected Code Risks

- `cmsDoTransformLineStride` exists in the current header and should still be available, but verify the signature against
  `lcms2.h` before assuming binary compatibility.
- `cmsCreateTransform` can return `nullptr` for malformed or unsupported profiles. Existing failure handling should be
  preserved and smoke-tested with invalid profiles.
- `ImgItem::TranformCMYK8ColorsToBGR8()` currently allocates `newstride * height` without checking for overflow or
  allocation failure before calling `cmsDoTransformLineStride`. That is pre-existing risk, not required for the dependency
  upgrade, but it is worth fixing in a small follow-up because ICC/profile handling processes untrusted image metadata.
- `LoadDefaultICCProfile()` uses `FileMapView::filesize().LowPart`, so default profiles larger than 4 GiB would be
  truncated even though newer Little CMS releases support larger profiles. Keep this unchanged for the first upgrade
  because normal ICC profiles are tiny; document it if large-profile support becomes a product goal.
- Static libraries must be built with a compatible runtime/toolset for ImgVw's Visual Studio configurations. Do not mix
  libraries built with incompatible MSVC runtime assumptions.
- Preserve Windows XP compatibility expectations. If upstream CMake or Visual Studio defaults introduce newer Windows API
  dependencies, adjust build flags or use a compatible fallback rather than raising ImgVw's target.

## Verification Checklist

- `3rd-party/Little-CMS/lcms2.h` reports `LCMS_VERSION 2190`, which is the value shipped by upstream tag `lcms2.19.1`.
- `dumpbin /headers 3rd-party/Little-CMS/lcms2_static.lib` and the x64 library show the expected machine types.
- `file 3rd-party/Little-CMS/liblcms2.a` and `file 3rd-party/Little-CMS/ucrt64/liblcms2.a` show the expected MinGW/MSYS
  architectures.
- `make` succeeds.
- `make arch=x64` succeeds.
- Visual Studio Win32 build succeeds.
- Visual Studio x64 build succeeds.
- Manual smoke tests confirm CMYK images still render through Little CMS and ordinary RGB images still render unchanged.

## Optional Follow-Ups

After the dependency upgrade lands cleanly:

1. Add defensive checks in `ImgItem::TranformCMYK8ColorsToBGR8()` for multiplication overflow and `HeapAlloc` failure.
2. Consider wrapping `cmsHPROFILE` and `cmsHTRANSFORM` in small RAII helpers local to `src/image/` to make error paths
   harder to leak.
3. Add a tiny image/color-management smoke-test corpus under `artifacts/` if no automated image test harness exists.
4. Document the Little CMS rebuild process in `README.md` or keep it encoded in `scripts/build-little-cms.ps1`.

## References Checked

- ImgVw vendored header: `3rd-party/Little-CMS/lcms2.h`, `LCMS_VERSION 2090`.
- ImgVw usage: `src/image/ImgItem.cpp` and `src/image/ImgItem.h`.
- ImgVw build wiring: `ImgVw.vcxproj`, `ImgVw.vcxproj.filters`, and `Makefile`.
- Little CMS GitHub tags: `lcms2.19.1` resolves to `21c582a594fe5279f90c0b93437c398f93bf62b0`.
- Little CMS GitHub releases: `2.19.1` is the latest release, published after `2.19`.
- Little CMS 2.19 release notes: CMake build system, large profile support, MSYS/Cygwin CI coverage, and additional
  hardening checks.
