# ImgVw Plan Index

This index lists current plans. A plan moves to `archive/` when its implementation is complete or a smaller follow-up
plan has replaced its remaining work.

## Plans

### Cloud file download consent

`cloud_file_download_consent_plan.md`

High priority. Detect online-only images without reading their content, gate header probes and every preload/load path,
and show an ImgVw-native consent surface before retrieval. Modern Cloud Files APIs must be dynamically discovered so
the portable executable remains compatible with Windows XP.

### Colour-management follow-up

`color_management_follow_up_plan.md`

The CMYK foundation is complete. Remaining work:

1. Generalize shared RGB/grayscale transform helpers and validation.
2. Apply embedded ICC transforms to RGB and grayscale JPEG.
3. Consolidate HEIF SDR ICC/NCLX handling and reject unsupported PQ/HLG explicitly.
4. Integrate native PNG colour metadata after the PNG decoder exists.

### Microsoft Store preflight

`store_publication_preflight_plan.md`

Packaging inputs exist. Remaining work is release-specific and partly external: final listing/screenshots, public
support and privacy URLs, LGPL source/relink materials, signed-package compatibility tests, Partner Center submission,
certification, certified acquisition, and publication verification.

### Native PNG through libpng

`libpng_support_plan.md`

Vendor/build libpng, implement `ImgPNGItem`, integrate format dispatch and projects, add security limits and fixtures,
then connect PNG colour metadata to the shared colour-management layer.

### Portable core foundation

`portable_core_foundation_plan.md`

Establish a small CMake core boundary with portable command, error, frame, and geometry types, native-header checks,
and focused tests. Then port paths, pixel storage, decoders, services, browser/loader/cache behavior, and application
control behind contracts shared by every native implementation.

The native implementation plans are children of the portable core foundation:

#### Native Linux GTK4 frontend

`linux_native_frontend_plan.md`

After the shared core contracts are usable, add Linux filesystem/XDG/settings/Trash/dispatch adapters and a native
GTK4 frontend with Wayland/X11 verification and installed-package resource handling.

#### Native macOS AppKit frontend

`macos_native_frontend_plan.md`

The AppKit application bundle, Foundation directories adapter, placeholder window, Finder open-file delivery, and
macOS target skeleton exist. Image loading is not connected. The plan tracks everything required for a fully working
arm64 macOS viewer, including portable decode, controller integration, Core Graphics presentation, native workflows,
color, Trash, accessibility, personal-tap preview distribution, a signed public Homebrew cask, notarization, and
licensing.

#### Native Windows frontend migration

`windows_native_frontend_migration_plan.md`

Adapt the shipping Win32 application to the shared core without rewriting it or dropping Windows XP compatibility.
The plan owns Win32 services, immutable-frame-to-DIB presentation, staged `AppController` cutover, Visual Studio/MSYS
integration, format fallback continuity, cloud-file consent, and the complete Windows regression/release matrix.

## Recommended Order

1. Implement cloud file download consent and prevent unapproved content probes/preloads.
2. Continue the small portable-core boundary without changing shipping Windows behavior.
3. Complete shared JPEG/HEIF SDR colour transforms.
4. Implement native PNG and integrate its colour metadata.
5. Port decoding, paths, services, browser/loader/cache, and application control one boundary at a time, adopting each
   boundary through the Win32 adapters.
6. Complete the Win32 controller/frame cutover and Windows regression/XP gates.
7. Connect and complete the native macOS application.
8. Perform Store release preflight when a release candidate and external publishing inputs are available.
9. Begin the Linux adapters and GTK4 frontend after the shared contracts are proven by Windows and macOS.

Store publication preparation can proceed in parallel with internal refactors, but final packaging must use a frozen,
fully validated release candidate.
