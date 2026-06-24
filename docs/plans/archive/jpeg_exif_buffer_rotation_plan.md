# JPEG EXIF Buffer Rotation Plan

## Goal

Replace the GDI+ rotation and flip step in the JPEG loading path with direct manipulation of the decoded BGR pixel
buffer. Since `ImgBuffer` persists the final display pixels to a temporary file, transformed scanlines can be written
directly to that file without allocating another image-sized output buffer.

This change should:

- Preserve Windows XP compatibility.
- Support all EXIF orientation values 1 through 8.
- Keep the existing TurboJPEG decode and ICC conversion behavior.
- Remove GDI+ from JPEG resizing and EXIF orientation.
- Limit additional memory use to one padded destination scanline.

## Current Flow

The current JPEG path in `ImgJPEGItem::Load()` is:

1. Read the JPEG header and saved APP markers.
2. Parse EXIF orientation.
3. Decode through TurboJPEG into a bottom-up buffer.
4. Convert CMYK/YCCK pixels to BGR when required.
5. Wrap the decoded buffer in a GDI+ bitmap.
6. Rotate or flip it with `Bitmap::RotateFlip()`.
7. Lock the resulting bitmap and copy the entire output into another memory buffer.
8. Write that copied buffer to the `ImgBuffer` temporary file.

The source decode buffer, GDI+ bitmap storage, and full output copy can coexist during this operation.

## Proposed Flow

The first option is to replace only the non-resized orientation path:

1. Decode through TurboJPEG into the existing bottom-up BGR buffer.
2. Parse EXIF orientation into an ImgVw-owned transform value.
3. Generate one transformed destination scanline at a time.
4. Write each scanline directly to the `ImgBuffer` temporary file.

The resulting flow is:

`TurboJPEG decode buffer -> scanline transform -> ImgBuffer temporary file`

EXIF orientation 1 can retain the existing bulk `ImgBuffer::WriteData()` path because no transform is required.

The preferred complete option is to combine resize and orientation in one destination pass:

1. Use libjpeg-turbo's existing DCT scaling to decode to the smallest practical intermediate dimensions that are not
   smaller than the required pre-orientation display dimensions.
2. Calculate the exact aspect-ratio-preserving display dimensions.
3. Generate each final, oriented destination scanline directly from the decoded BGR source.
4. Perform resampling and inverse orientation mapping while generating that scanline.
5. Write the scanline directly to the `ImgBuffer` temporary file.

The resulting flow is:

`libjpeg scaled decode buffer -> combined resize/orientation scanline -> ImgBuffer temporary file`

This is one pass over the final destination pixels. It is not a single streaming pass directly from libjpeg scanlines,
because 90-degree rotations require column-wise and reverse-order access to the decoded source.

## Transform Representation

Introduce an image-layer enum that does not depend on GDI+:

```cpp
enum class ImageTransform
{
    None,
    FlipHorizontal,
    Rotate180,
    FlipVertical,
    Transpose,
    Rotate90,
    Transverse,
    Rotate270
};
```

Map EXIF values directly:

| EXIF | Transform |
| ---: | --- |
| 1 | `None` |
| 2 | `FlipHorizontal` |
| 3 | `Rotate180` |
| 4 | `FlipVertical` |
| 5 | `Transpose` |
| 6 | `Rotate90` |
| 7 | `Transverse` |
| 8 | `Rotate270` |

This enum should be owned by the image subsystem rather than exposing `Gdiplus::RotateFlipType` to JPEG decoding
logic.

## `ImgBuffer` Streaming Write API

Add a row-oriented writing interface to `ImgBuffer`. One possible shape is:

```cpp
void BeginWrite(INT width, INT height, INT stride);
void WriteRow(const BYTE* row, INT bytecount);
void EndWrite();
```

Requirements:

- `BeginWrite()` creates or truncates the temporary file and records the output metadata.
- `WriteRow()` validates the expected row size and reports partial or failed writes.
- `EndWrite()` verifies that exactly `height` rows were written and closes the file.
- Errors must leave the object in a valid, destructible state.
- Existing `WriteData()` should either remain as the bulk path or be implemented using the same internal write
  machinery.

An RAII write session would provide stronger failure handling, but it can be deferred if it would expand the initial
change unnecessarily.

## BGR24 Transform and Resize Writer

Add a pure image-layer helper, separate from `ImgItemHelper`'s GDI+ operations:

```cpp
ImgBuffer WriteTransformedBgr24(
    INT source_width,
    INT source_height,
    INT source_stride,
    const BYTE* source,
    ImageTransform transform);
```

For the combined option, generalize it to:

```cpp
ImgBuffer WriteResizedAndTransformedBgr24(
    INT source_width,
    INT source_height,
    INT source_stride,
    const BYTE* source,
    INT resized_width,
    INT resized_height,
    ImageTransform transform,
    ResamplingMode resampling_mode);
```

`resized_width` and `resized_height` describe the image before orientation. Orientations 5 through 8 swap those
dimensions in the persisted output.

Potential filenames:

- `src/image/BgrTransform.h`
- `src/image/BgrTransform.cpp`

If new files are added, update `ImgVw.vcxproj`, its filters file if present, and the Makefile together.

### Output dimensions

- Orientations 1 through 4 retain `source_width` by `source_height`.
- Orientations 5 through 8 produce `source_height` by `source_width`.

Calculate the destination stride as:

```cpp
const INT destination_stride = TJPAD(destination_width * 3);
```

Do not assume the source stride is equal to `source_width * 3`; account for DIB padding.

### Scratch memory

Allocate one destination row:

```cpp
std::vector<BYTE> row(static_cast<std::size_t>(destination_stride));
```

For each output row:

1. Map each oriented destination coordinate back into the pre-orientation resized coordinate space.
2. Map that resized coordinate into the decoded source coordinate space.
3. Sample the source pixel using the selected resampling method.
4. Initialize padding bytes to zero.
5. Write the completed row through the streaming `ImgBuffer` API.

Peak additional pixel memory is then approximately one destination stride.

### Resampling

Start with bilinear resampling to provide predictable quality for arbitrary target dimensions. Keep nearest-neighbor as
an optional implementation or test mode, but do not use it as the default viewer behavior.

Use pixel-center mapping rather than edge-based integer division:

```cpp
source_x = ((destination_x + 0.5) * source_width / resized_width) - 0.5;
source_y = ((destination_y + 0.5) * source_height / resized_height) - 0.5;
```

Clamp the sample coordinates at the image edges. Perform interpolation with fixed-point arithmetic if profiling shows
that floating-point calculation is significant on the Windows XP-era target hardware.

The exact output may differ slightly from the current GDI+ `Graphics::DrawImage()` result. Verification should compare
display quality and geometry rather than requiring byte-identical pixels.

## Coordinate Handling

The TurboJPEG decode uses `TJFLAG_BOTTOMUP`, and the display buffer is stored as a positive-height bottom-up DIB.
Transform formulas should operate in logical top-left coordinates so they remain understandable and testable.

For a logical source coordinate `(source_x, source_y)`, locate its memory row using:

```cpp
const INT source_memory_y = source_height - 1 - source_y;
const BYTE* source_pixel =
    source + source_memory_y * source_stride + source_x * 3;
```

When generating bottom-up destination rows, convert the destination memory row to a logical coordinate:

```cpp
const INT destination_y = destination_height - 1 - destination_memory_y;
```

Then map `(destination_x, destination_y)` back to the corresponding logical source coordinate for the selected
transform. For the combined path, this yields a coordinate in the pre-orientation resized image. Apply the inverse
resize mapping next to locate the source sample.

Centralize these mappings in one small function instead of distributing orientation-specific indexing across the
write loop.

## What libjpeg-turbo Can Do

The repository vendors libjpeg-turbo 3.1.4.1, while `turbojpeg_ImgVw.cpp` contains a project-specific decompression
wrapper originally based on the TurboJPEG 1.5.2 implementation.

libjpeg-turbo can:

- Decode directly at its supported DCT scaling factors.
- Produce the current packed BGR or CMYK output.
- Perform lossless JPEG-domain rotations and flips through the TurboJPEG transform API.

libjpeg-turbo cannot perform an arbitrary final-size resize and EXIF rotation/flip together while decompressing to the
packed BGR display buffer.

The lossless transform API is not a good fit for this display path:

- It produces another compressed JPEG rather than final BGR display pixels.
- Arbitrary resizing still requires a later decode and resampling step.
- Some lossless rotations are constrained by JPEG MCU boundaries unless trimming or imperfect transforms are accepted.
- It introduces another compressed buffer and complicates EXIF orientation-marker handling.

Therefore, retain libjpeg-turbo for efficient scaled decode and implement the exact resize plus orientation in ImgVw's
image-buffer layer.

### Compatibility with the current code

The combined writer is compatible with the current code structure:

- `ImgJPEGItem::Load()` already selects a libjpeg scaling factor.
- The complete decoded source remains available in memory.
- CMYK/YCCK conversion already produces BGR before resize or orientation.
- The existing target-dimension calculation already accounts for transforms that swap width and height.
- `ImgBuffer` already persists final bottom-up BGR24 pixels.

The scaling-factor selection should remain unchanged initially. When an exact resize is needed, decode using the
current next-larger libjpeg scaling factor and let the combined writer downsample to the exact display size. This
preserves the current intent of avoiding upscaling from an undersized libjpeg result.

The custom wrapper does not need to migrate to the TurboJPEG 3 API for this change.

## `ImgJPEGItem` Integration

Replace the current non-resize rotation branch:

```cpp
else if (rotateflip != Gdiplus::RotateNoneFlipNone)
{
    displaybuffer_ =
        ImgItemHelper::Rotate24bppRGBImage(decompresswidth, decompressheight, buffer, rotateflip);
}
```

with the BGR transform writer:

```cpp
else if (transform != ImageTransform::None)
{
    displaybuffer_ = WriteTransformedBgr24(
        decompresswidth, decompressheight, stride, buffer, transform);
}
```

The no-transform branch remains:

```cpp
displaybuffer_.WriteData(decompresswidth, decompressheight, stride, buffer);
```

The target-width and target-height calculation must use whether the transform swaps dimensions rather than comparing
against GDI+ constants.

## Combined Resize and Orientation Option

There are two viable implementation scopes:

### Option A: staged replacement

- No exact resize required: use the GDI-free transform writer.
- Exact resize required: temporarily retain the existing GDI+ resize-and-rotate implementation.

This is lower risk but leaves the highest-cost path dependent on GDI+.

### Option B: combined final-pixel writer

Implement resize and orientation together while generating final output rows. This avoids:

- A GDI+ bitmap.
- A full resized intermediate buffer.
- An intermediate temporary file.
- A separate rotation pass.

Option B is the recommended end state. It can be implemented directly if the change includes focused pixel-mapping
tests for all orientations and resize cases. Option A remains a useful incremental fallback if verification exposes
quality or compatibility differences in the custom resampler.

## Error and Overflow Handling

Validate before allocation or writing:

- Width, height, and stride are positive.
- Source stride is at least `source_width * 3`.
- Multiplication for row offsets and file sizes cannot overflow the involved integer types.
- Destination stride and total output size fit the existing `ImgBuffer` metadata types.
- `HeapAlloc`, vector allocation, file creation, seeks, and writes are checked.
- Partial `WriteFile()` results are treated as failures.

Use `std::size_t` for memory offsets and retain explicit checks before narrowing to Win32 integer types.

## Verification

Use `artifacts/sample/exif/Landscape_1.jpg` through `Landscape_8.jpg`.

Verify:

- All eight samples display with the same visual orientation.
- Orientations 5 through 8 swap output dimensions.
- BGR channels are not exchanged.
- Destination padding bytes are initialized.
- Images with widths that require one, two, or three bytes of row padding work correctly.
- CMYK/YCCK images still transform after conversion to BGR.
- Orientation 1 follows the unchanged bulk-write path.
- Downscaled output retains the correct aspect ratio.
- Combined resize and orientation produce the same dimensions and placement as the current GDI+ path.
- Very small targets and one-pixel source or destination dimensions do not divide by zero or sample out of bounds.
- Multiple loader threads no longer serialize on the GDI+ semaphore for non-resized JPEG orientation.
- With the complete option, JPEG resizing no longer serializes on the GDI+ semaphore either.

Add focused tests around a small artificial BGR grid if a test target is introduced. Each pixel should have a unique
color so that every orientation mapping, especially EXIF 5 and 7, can be checked exactly.

Build at least:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x86 -Clean
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x64 -Clean
```

## Implementation Order

1. Add the image-owned transform enum and EXIF mapping.
2. Add validated streaming writes to `ImgBuffer`.
3. Implement and test bottom-up BGR24 coordinate mapping for all orientations.
4. Add bilinear source sampling for exact target dimensions.
5. Combine inverse orientation and inverse resize mapping in the final-row writer.
6. Integrate the writer into both resized and non-resized JPEG paths.
7. Retain the old GDI+ path temporarily only if needed for side-by-side verification.
8. Test all eight EXIF orientations, multiple resize ratios, and padded row widths.
9. Build x86 and x64 release configurations.

## Expected Result

For an oriented and/or resized JPEG, the application will retain only:

- The existing decoded source image.
- One resized and transformed destination scanline.
- The persisted display buffer in the temporary file.

It will no longer require a GDI+ resize/rotation bitmap or a second full image-sized memory copy for JPEG display
processing.
