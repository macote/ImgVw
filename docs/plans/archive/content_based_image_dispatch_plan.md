# Content-Based Image Detection and Decoder Dispatch Plan

## Goal

Replace extension-only decoder selection with one bounded header probe applied only to files whose extension maps to a
currently supported image format. Route recognized image content to the correct existing decoder when the filename
extension is wrong or belongs to another supported format.

This plan owns general format detection and rerouting. Format-specific plans, including
`artifacts/libheif_support_plan.md`, should register their decoder with this dispatcher instead of adding local
extension exceptions.

## Confirmed Cases

The following real files demonstrate the problem:

- `IMG_7569.heic` begins with the exact PNG signature `89 50 4E 47 0D 0A 1A 0A`; GDI+ identifies it as a
  4284-by-5712 PNG.
- `IMG_20250911_174811.heic` begins with `FF D8 FF E0` and a JFIF segment; GDI+ identifies it as a
  4032-by-3024 JPEG.

The current `ImgItemFactory` selects `ImgHEIFItem` solely from `.heic`, so both files fail before reaching their actual
decoders.

## Required Behavior

For every regular file with a currently supported image extension during folder collection, and every explicitly opened
file with a currently supported image extension:

1. Read a small bounded prefix once.
2. Classify recognized image content from signatures and minimally parsed container headers.
3. Route to the decoder registered for the detected content.
4. Use the filename extension as a fallback when content is unknown or the prefix could not be read.
5. Let the selected decoder perform complete format validation.

Content detection takes precedence over the extension. The dispatcher does not rename files or alter their metadata.
Files whose extensions are not in the supported-image set are not opened for header probing during folder enumeration.

## Detection Scope

Support all formats currently handled by ImgVw:

| Detected content | Minimum identification | Route |
| --- | --- | --- |
| JPEG | `FF D8 FF` | `ImgJPEGItem` |
| PNG | Exact 8-byte PNG signature | `ImgGDIItem` |
| GIF | `GIF87a` or `GIF89a` | `ImgGDIItem` |
| BMP | `BM`, with basic bounded header sanity checks | `ImgGDIItem` |
| TIFF | `II 2A 00`, `MM 00 2A`, and supported BigTIFF signatures if GDI+ accepts them | `ImgGDIItem` |
| ICO | Reserved field 0, type 1, nonzero image count | `ImgGDIItem` |
| HEIF/HEIC | ISO BMFF `ftyp` with a libheif-supported HEIF brand | `ImgHEIFItem` |

Do not classify generic ISO BMFF data as HEIF from `ftyp` alone. Use `heif_check_filetype()` on the bounded prefix, or
equivalent compatible-brand validation, so MP4/MOV files are not added as images.

Keep the content-format enum distinct from decoder type. PNG, GIF, BMP, TIFF, and ICO are different detected formats
but currently share the GDI+ decoder. This prevents the detector from being coupled permanently to today’s decoder
choices.

## Architecture

Add three explicit layers:

1. `ImageFormatDetector`
   - Pure classification of a `(bytes, byte_count)` prefix.
   - No filesystem access and no decoder construction.
   - Returns `DetectedImageFormat` plus a confidence/result state such as `Recognized`, `Unknown`, or `NeedMoreData`.
2. `ImageHeaderProbe`
   - Win32 file wrapper that opens a path and reads at most the configured prefix size.
   - Returns bytes and a status without throwing through folder collection.
3. `ImgItemDispatcher`
   - Resolves detected content first, extension fallback second.
   - Constructs `ImgJPEGItem`, `ImgHEIFItem`, or `ImgGDIItem`.

`ImgItemFactory` may be renamed to `ImgItemDispatcher` or reduced to a thin compatibility wrapper. Do not put Win32
file I/O or signature parsing in the factory header.

The resolved format should be stored with the cache entry or passed into item construction so the file is not probed
again during loading.

## Folder Enumeration

Apply the header probe only to regular files whose extensions are already supported by ImgVw.

- A recognized image is included when its extension is supported but misleading.
- An unrecognized file with a currently supported image extension remains included through extension fallback. Its
  decoder will report the normal item error if the file is malformed.
- A file with an unsupported or missing extension is skipped during folder enumeration without header probing.
- Probe failures must not abort folder enumeration.

This changes `ImgBrowser::IsFileFormatSupported()` from an extension-only predicate to a path-based resolution step.
Avoid probing once for filtering and again for cache insertion: return or retain the resolved dispatch result from the
first probe.

Because every supported-extension candidate is opened, keep the operation bounded and cancellable between files. Measure
enumeration on large local, removable, and network folders before merging.

## Header Read Policy

- Start with a 4 KiB prefix limit. This is enough for fixed signatures and typical ISO BMFF `ftyp` boxes.
- Use one `CreateFile` and one `ReadFile` per candidate where practical.
- Open with `GENERIC_READ` and sharing compatible with files being viewed or written:
  `FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE`.
- Use a stack or fixed-size owned buffer; never map or read the whole file for detection.
- Capture `GetLastError()` immediately after a failed Win32 call.
- Empty and short files return `Unknown`; all signature checks must be length guarded.
- If HEIF detection returns `NeedMoreData`, optionally perform one second bounded read up to a strict maximum such as
  64 KiB. Do not follow arbitrary box sizes or allocate based solely on untrusted header values.
- Check the browser cancellation flag between probes. Do not add synchronous header I/O to paint or window-message
  paths.

## Dispatch Rules

Use this precedence:

1. Recognized content format.
2. Existing case-insensitive extension mapping.
3. Unsupported.

Examples:

- PNG bytes named `.heic` route to `ImgGDIItem`.
- JPEG bytes named `.png` route to `ImgJPEGItem`, preserving JPEG EXIF, CMYK, and ICC handling.
- HEIF bytes named `.jpg` route to `ImgHEIFItem`.
- Valid GIF bytes named `.bin` are skipped during folder enumeration because `.bin` is not a supported image extension.
- Unknown bytes named `.jpg` route to `ImgJPEGItem` and fail there with its normal error.
- Unknown bytes named `.bin` are skipped during folder enumeration without header probing.

Do not fall through to a second decoder after a recognized decoder reports a decode error. Signature recognition
chooses the decoder; decode failure indicates malformed or unsupported content, not a reason to try unrelated codecs.

## Error and Diagnostics Policy

Detection failures are non-fatal to browsing:

- unreadable prefix: record debug diagnostics and use extension fallback;
- unknown signature: use extension fallback;
- malformed recognized signature: route by the recognized format and let that decoder report the detailed error;
- unsupported HEIF codec/brand: route to `ImgHEIFItem` only when libheif identifies the container as HEIF, then report
  the decoder limitation normally.

Add debug-only diagnostics containing the path, detected content, extension fallback, and final decoder selection.
Avoid user-facing warnings merely because content and extension differ; successfully displaying the image is the
desired behavior.

## Tests

Add pure detector tests for:

- every supported signature and minimum valid prefix length;
- every signature truncated at each boundary;
- near-miss signatures;
- little- and big-endian TIFF;
- valid and invalid ICO headers;
- HEIF supported, HEIF unsupported, generic MP4/MOV, malformed `ftyp`, and `NeedMoreData`;
- random data and empty input.

Add dispatcher tests for the supported-extension content/extension matrix:

- matching content and extension;
- each supported content type with a wrong supported extension;
- unknown content with a supported extension;
- supported content with no or unknown extension is skipped during folder enumeration;
- unknown content with an unsupported extension is skipped without probing;
- unreadable file with supported and unsupported extensions.

Add integration regressions using:

- `IMG_7569.heic` -> detected PNG -> `ImgGDIItem`;
- `IMG_20250911_174811.heic` -> detected JPEG -> `ImgJPEGItem`;
- normal HEIC, JPEG, PNG, GIF, BMP, TIFF, and ICO files;
- a HEIF file renamed `.jpg`;
- a non-image ISO BMFF file to prove it is not classified as HEIF.

## Project Integration

When adding source files, update together:

- `ImgVw.vcxproj`;
- `ImgVw.vcxproj.filters`;
- `Makefile`;
- the native test targets.

Keep Win32 file access under `src/platform/win32/` where practical and pure detection/dispatch policy under
`src/image/`.

## Implementation Order

1. Define `DetectedImageFormat` and pure prefix-classification tests.
2. Implement fixed-signature detection for JPEG, PNG, GIF, BMP, TIFF, and ICO.
3. Add bounded HEIF/ISO BMFF detection without accepting generic video containers.
4. Implement the Win32 bounded-prefix reader.
5. Implement content-first, extension-fallback dispatch.
6. Change folder collection to probe only supported-extension regular files and retain the resolved result for cache
   insertion.
7. Change direct-file opening to use the same dispatcher.
8. Add the confirmed mislabeled-file regressions and cross-extension matrix.
9. Measure folder enumeration performance and cancellation behavior.
10. Run x86/x64 MSYS and Visual Studio builds plus the Windows XP compatibility gate.

## Acceptance Criteria

- Every supported-extension regular file considered by folder browsing receives one bounded content probe.
- Directly opened files use the same detection and dispatch policy.
- Recognized content overrides the filename extension.
- All currently supported formats route to their correct existing decoder.
- Recognized images with unsupported or missing extensions are not added by folder browsing.
- Unknown content retains extension fallback behavior.
- `IMG_7569.heic` displays through the PNG/GDI+ path.
- `IMG_20250911_174811.heic` displays through the JPEG path.
- Generic MP4/MOV files are not misclassified as HEIF.
- Detection never reads an entire file and remains bounded on malformed input.
- Folder enumeration remains cancellable and has acceptable measured performance.
- Existing JPEG color management, JPEG EXIF orientation, HEIF color management, and GDI+ format behavior do not
  regress.
- x86/x64 Visual Studio and MSYS builds and tests pass.
- No post-Windows-XP API is introduced without an existing compatible fallback.
