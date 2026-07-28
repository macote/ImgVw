# ImgVw Plan Index

This index records which plans are active, pending, or historical. A plan moves to `archive/` when its implementation is
complete or a smaller follow-up plan has replaced its remaining work.

## Active

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

## Pending

### Native PNG through libpng

`libpng_support_plan.md`

Vendor/build libpng, implement `ImgPNGItem`, integrate format dispatch and projects, add security limits and fixtures,
then connect PNG colour metadata to the shared colour-management layer.

### Portable core and native frontends

`portable_core_native_frontends_plan.md`

Not started. Define portable data/results/services, move decoding and application policy behind those boundaries, then
add Linux adapters and GTK4. The macOS portion remains a future phase.

## Recommended Order

1. Implement cloud file download consent and prevent unapproved content probes/preloads.
2. Complete shared JPEG/HEIF SDR colour transforms.
3. Implement native PNG and integrate its colour metadata.
4. Perform Store release preflight when a release candidate and external publishing inputs are available.
5. Begin the portable-core migration only after the Win32 and image-pipeline boundaries are stable.

Store publication preparation can proceed in parallel with internal refactors, but final packaging must use a frozen,
fully validated release candidate.
