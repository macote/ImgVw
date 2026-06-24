# Bundled CMYK Fallback Profile Plan

## Decision

Bundle `CGATS21_CRPC5.icc` unchanged in the ImgVw executable and use it only as the final source-profile fallback for
CMYK/YCCK JPEG files that do not contain a valid embedded CMYK ICC profile.

This is acceptable for ImgVw as a general-purpose Windows image viewer because an untagged CMYK image cannot be
interpreted accurately without assuming a printing condition. A standardized, documented approximation is more useful
than failing the image or interrupting the user with a mandatory profile picker.

The built-in profile is not a universal or authoritative interpretation of every CMYK JPEG. The UI and documentation
must call it a generic fallback, and the existing user-selected profile must continue to override it.

## Validation Result

### Technical compatibility

The official profile downloaded from the ICC Profile Registry was inspected on June 20, 2026:

- file name: `CGATS21_CRPC5.icc`;
- profile version: ICC v4.2;
- device class: output/printing profile (`prtr`);
- input color space: CMYK;
- profile connection space: CIELAB;
- ICC signature: `acsp`;
- profile header rendering intent: perceptual;
- available transforms reported by the registry: `AToB0`, `AToB1`, `AToB2`, `BToA0`, `BToA1`, and `BToA2`;
- profile ID: `b0eb43159bf229c6eedfaaaaa54ff497`;
- exact file size: `3,339,888` bytes;
- SHA-256: `6c201673f030227dc4cd15209ab381c4427ce2b6de26dd2cade38dabc0d31e8c`.

The profile is suitable for the existing Little CMS transform direction:

```text
CMYK source samples -> profile connection space -> sRGB destination -> 8-bit BGR display buffer
```

An output-class CMYK profile is valid as a source profile for display conversion because its `AToB` tables describe
CMYK-to-PCS conversion. The profile supplies all three standard rendering intents needed by the application.

Little CMS must still open and validate the embedded bytes at runtime. Build-time header validation is not a substitute
for normal runtime error handling.

### Intended printing condition

The ICC Registry describes CGATS21 CRPC5 as:

- CGATS.21-2 / ISO 15339-oriented;
- publication-coated;
- usable as a reference for multiple CMYK processes, including offset, screen, inkjet, flexo, toner, gravure, and
  dye-sublimation;
- 300% total area coverage with medium-plus GCR.

This makes CRPC5 a defensible general fallback for an image viewer. It is broader than a press-specific profile such as
legacy SWOP, but it remains an assumption. Untagged files intended for uncoated paper, newsprint, unusual inks, or a
specific press condition can display differently from the creator's intent.

### Licensing

The registry states that the profile:

- may be used, embedded, exchanged, and shared without restriction;
- may not be altered;
- may not be sold independently without written permission from IDEAlliance.

Bundling the unchanged profile as an application resource is explicitly permitted. ImgVw must:

- preserve the profile bytes exactly;
- not remove its optional tags or metadata to reduce size;
- retain attribution and the license statement in the repository and distribution notices;
- distribute it only as part of ImgVw, not as a separately sold profile asset.

This is a project-level licensing assessment, not legal advice.

### Application impact

Embedding the profile as ordinary `RCDATA` increases each executable by approximately 3.19 MiB before PE alignment.
Windows resources are not automatically compressed.

This is a meaningful increase for a small viewer, but it is acceptable because:

- no external runtime file or installer action is required;
- the fallback works for portable/single-executable deployment;
- the resource is memory-mapped with the executable and only needs to be opened by Little CMS when fallback is needed;
- CMYK conversion is already an exceptional path rather than part of ordinary RGB image loading;
- the feature eliminates a blocking first-use prompt for untagged CMYK images.

Do not add a compression library solely to reduce this one resource. Re-evaluate that decision only if executable size
is a formal product constraint.

## Profile Selection Order

Use this precedence for a CMYK/YCCK JPEG:

1. Valid embedded CMYK ICC profile from the JPEG.
2. Valid user-selected CMYK ICC profile stored at `%APPDATA%\ImgVw\default.icc`.
3. Valid bundled `CGATS21_CRPC5.icc` resource.
4. Image load error if no profile can be opened or no transform can be created.

The fallback must not affect RGB, grayscale, PNG, or HEIF color handling.

If the stored user profile exists but is invalid, record or expose that condition and continue to the bundled fallback.
Do not make a corrupt override prevent image display.

## User Experience

### Automatic behavior

- Do not prompt when the bundled fallback successfully renders the image.
- Do not write the bundled profile into `%APPDATA%`.
- Keep the user override distinct from the built-in resource.
- Continue using an embedded JPEG profile without prompting or consulting defaults.

### Existing Ctrl+I behavior

Keep Ctrl+I as the explicit command to select a preferred default CMYK profile. Update the menu and dialog wording from
ambiguous “ICC profile” text to:

- menu: `Select default CMYK profile...`;
- dialog title: `Select default CMYK ICC profile...`.

After successful selection:

- copy the selected profile to the existing application-data location;
- unload the cached user/default profile state;
- reload the current image;
- prefer the selected profile over the bundled fallback.

### Reset behavior

Add an explicit way to remove the stored override and return to the built-in fallback. Suitable options are:

- a separate `Use built-in CMYK profile` menu command; or
- a small profile-selection dialog with “Select...” and “Use built-in” actions.

Prefer the separate command for the current minimal Win32 UI. It should delete only the known
`%APPDATA%\ImgVw\default.icc` file after validating the resolved path.

No prompt is required on first use. A non-blocking indication of which source profile was used can be added later to
file information or diagnostics.

## Resource Integration

### Vendored asset

Add the unchanged official file at a first-party resource location such as:

```text
resources/color/CGATS21_CRPC5.icc
```

Add an adjacent notice file:

```text
resources/color/CGATS21_CRPC5_LICENSE.txt
```

The notice must contain:

- profile name;
- copyright holder: X-Rite, Inc.;
- provider: IDEAlliance;
- exact registry license statement;
- source URL;
- profile ID;
- file size;
- SHA-256;
- retrieval date.

Do not place the profile under `3rd-party/` if the build expects it as a Windows resource; `resources/color/` makes the
runtime role explicit.

### Windows resources

Add a resource identifier to `resources/resource.h`, for example:

```cpp
#define IDR_DEFAULT_CMYK_ICC 125
```

Embed the file in `resources/ImgVw.rc`:

```rc
IDR_DEFAULT_CMYK_ICC RCDATA "color\\CGATS21_CRPC5.icc"
```

Load it using XP-compatible Win32 resource APIs:

1. `FindResource(instance, MAKEINTRESOURCE(IDR_DEFAULT_CMYK_ICC), RT_RCDATA)`;
2. `SizeofResource`;
3. verify the resource size equals the expected profile size;
4. `LoadResource`;
5. `LockResource`;
6. pass the stable bytes and size to Little CMS.

The resource memory remains valid for the module lifetime. Do not copy the full profile unless Little CMS ownership
requirements or later tests demonstrate that a copy is necessary.

The MSYS windres and Visual Studio resource builds must both include the binary resource. Verify path quoting and
dependency tracking in both build systems.

## Code Architecture

Implement this with the shared color-management refactor described in
`artifacts/color_management_support_plan.md`.

Preferred ownership:

- a profile store owns the cached user profile and bundled fallback profile;
- image items request a CMYK source profile by precedence;
- returned profile ownership remains valid for the duration of transform creation/use;
- synchronization protects cache initialization and replacement;
- UI code selects or resets the override but does not manipulate Little CMS handles.

Suggested result metadata:

```cpp
enum class CmykProfileSource
{
    Embedded,
    UserDefault,
    BundledFallback
};
```

Retain this source in diagnostics or image-load results so behavior can be tested and explained.

Do not preserve the current meaning of `iccprofileloadfailed_` unchanged. With a bundled fallback, “user profile is
missing” is not a load failure. Distinguish:

- override unavailable or invalid;
- bundled resource unavailable or invalid;
- transform creation failure;
- complete absence of a usable CMYK profile.

## Rendering Intent

Keep `INTENT_PERCEPTUAL` for the initial bundled fallback. It is consistent with the current code and appropriate for
general image viewing where out-of-gamut appearance should be compressed rather than clipped.

Add a test proving that Little CMS can create:

```cpp
cmsCreateTransform(cmyk_profile, TYPE_CMYK_8_REV, srgb_profile, TYPE_BGR_8, INTENT_PERCEPTUAL, 0)
```

Do not expose rendering-intent selection in the UI for this feature.

The separate CMYK inversion verification in `artifacts/color_management_support_plan.md` remains mandatory. A correct
profile cannot compensate for incorrectly inverted CMYK samples.

## Implementation Steps

1. Download the official profile from the ICC Registry.
2. Verify exact size, ICC header fields, profile ID, and SHA-256 against this plan.
3. Add the unchanged profile and attribution/license notice under `resources/color/`.
4. Add the `RCDATA` resource ID and resource-script entry.
5. Introduce or extend the profile-store abstraction.
6. Implement embedded -> user override -> bundled fallback precedence.
7. Treat an invalid stored override as a recoverable fallback condition.
8. Stop automatic profile-picker invocation when the bundled profile succeeds.
9. Preserve Ctrl+I selection and current-image reload behavior.
10. Add a command to remove the stored override and return to the built-in profile.
11. Add diagnostics identifying the profile source used.
12. Update project files and both build paths.
13. Add automated and manual tests.
14. Document the generic-fallback limitation in the README or user-facing help.

## Tests

### Resource and integrity tests

- Resource exists in x86 and x64 builds.
- Resource size is `3,339,888` bytes.
- SHA-256 of the source asset is
  `6c201673f030227dc4cd15209ab381c4427ce2b6de26dd2cade38dabc0d31e8c`.
- ICC profile ID is `b0eb43159bf229c6eedfaaaaa54ff497`.
- Little CMS opens it and reports CMYK input color space.
- Perceptual CMYK-to-sRGB transform creation succeeds.

Avoid hashing the mapped resource on every application run. Integrity verification belongs in tests/build validation;
runtime validation should use size, ICC parsing, and color-space checks.

### Precedence tests

- Valid embedded profile wins over user and bundled profiles.
- User-selected profile wins when the JPEG has no valid embedded profile.
- Bundled profile is used when no override exists.
- Bundled profile is used when the override is corrupt or is not CMYK.
- Complete profile failure produces a specific image error.
- RGB JPEGs never consult the bundled CMYK profile.

### UI tests

- First untagged CMYK image displays without opening a file dialog.
- Ctrl+I installs a valid CMYK override and reloads the current image.
- Invalid profile selection is rejected without replacing the existing override.
- Reset command removes the override and reloads using the built-in profile.
- Cancelling profile selection leaves the current configuration unchanged.

### Image tests

- Existing untagged CMYK fixture renders with the bundled fallback.
- Existing embedded-profile CMYK fixture remains unchanged.
- CMYK and YCCK inversion variants render correctly.
- A file known to target another printing condition demonstrates and documents that the fallback is approximate.
- Repeated mixed RGB/CMYK navigation does not leak profiles or transforms.

### Build tests

- MSYS release x86 and x64 builds.
- Visual Studio release Win32 and x64 builds.
- Verify executable-size increase is approximately the resource size plus PE alignment.
- Verify Windows XP startup and resource loading when an XP test environment is available.

## Error Handling

- Failure to load the user override is recoverable if the bundled profile works.
- Failure to find, size, load, lock, validate, or transform with the bundled resource is an application/package error.
- Capture `GetLastError()` immediately for failing Win32 resource calls where meaningful.
- Do not launch the picker automatically for corrupt packaged resources.
- Surface a concise image error and retain detailed diagnostics.

## Security

- Treat the user-selected profile as untrusted and retain size limits and Little CMS validation.
- Treat the packaged profile as trusted by provenance but still validate it before use.
- Check all size conversions before passing resource lengths to APIs with narrower parameters.
- Do not permit the reset command to delete arbitrary paths.
- Keep profile loading off paint and window-message paths.

## Acceptance Criteria

The feature is complete when:

- an untagged CMYK/YCCK JPEG displays on a clean ImgVw installation without prompting;
- valid embedded CMYK profiles remain highest priority;
- Ctrl+I still installs a persistent user override;
- users can explicitly return to the built-in profile;
- the bundled bytes and license notice match the validated official profile;
- invalid user overrides fall back safely;
- the selected profile source is testable;
- x86 and x64 builds include and load the resource;
- CMYK inversion behavior is verified;
- documentation states that the built-in profile is an approximate generic fallback, not exact proofing.

## Sources

- ICC Registry entry:
  <https://registry.color.org/profile-registry/CGATS21_CRPC5>
- Official profile:
  <https://registry.color.org/profile-registry/profiles/CGATS21_CRPC5.icc>
- ICC Profile Registry:
  <https://registry.color.org/profile-registry/>
- ICC CMYK characterization registry:
  <https://registry.color.org/cmyk-registry/>
- Shared color-management plan: `artifacts/color_management_support_plan.md`
