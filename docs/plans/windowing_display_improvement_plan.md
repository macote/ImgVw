# ImgVw Windowing and Display Improvement Plan

## Summary

ImgVw's current Win32 display path is intentionally small: `ImgVwWindow` owns window messages, browser commands,
slideshow state, and paint dispatch, while `ImgRenderer` fills the background and blits the ready display bitmap. This
works for the current fullscreen-first model, but several responsibilities are still coupled in ways that make resize,
shutdown, async loading, and future display features harder to reason about.

The near-term goal is not to replace the Win32 frontend or introduce a new rendering stack. The goal is to make the
existing GDI-based window/display path more correct, more explicit, and easier to extend while preserving the Windows XP
compatibility target.

## Goals

- Handle viewport changes explicitly and correctly.
- Separate decoded image data from viewport-specific presentation state.
- Make paint behavior deterministic for ready, loading, and error states.
- Keep worker-thread notifications safe during shutdown.
- Reduce manual GDI/DC state management in the renderer.
- Preserve the current fullscreen keyboard/mouse workflow unless a later feature changes it deliberately.

## Non-Goals

- Do not replace the Win32 UI layer.
- Do not introduce Direct2D, WIC-only rendering, or APIs newer than the XP target.
- Do not perform broad formatting-only changes.
- Do not move vendored dependency code.
- Do not redesign navigation, slideshow policy, or image decoding except where display correctness requires a boundary
  change.

## Current State

- `ImgVwWindow::Create()` creates a borderless popup window sized from `SM_CXSCREEN` and `SM_CYSCREEN`.
- `ImgVwWindow` handles commands, mouse/cursor behavior, slideshow timers, browser notifications, and painting.
- `ImgBrowser::BrowseAsync()` receives the initial client size and stores it as the target size for cached items.
- Each `ImgItem` computes display dimensions and offsets during load using its cached target size.
- `ImgRenderer::Render()` creates a compatible memory DC, selects the item bitmap, excludes the image rectangle from the
  target DC clip, fills the background, resets clipping, and `BitBlt`s the image.
- Loading and enumeration complete through posted window messages.

## Main Problems

### Viewport Size Is Captured Too Early

`WM_SIZE` currently does not update browser, cache, item, or presentation state. Since item scaling and offsets are
computed from the startup client size, a later client-size change can leave the image incorrectly scaled or centered.
This matters for display-mode changes, taskbar/work-area changes, remote-desktop changes, future windowed mode, and any
future zoom/pan work.

### Decode Output and Presentation State Are Coupled

Loaders decide the final display buffer size using the target viewport. That is useful for memory and initial speed, but
it means a viewport change is treated like a decode/cache problem instead of a presentation problem. It also spreads
layout concerns into JPEG, HEIF, GDI, and base item code.

### Paint Reads Cross-Thread State Informally

The UI reads item status, bitmap info, offsets, and display buffers that worker threads write during load. The current
message flow usually avoids visible races by painting only after `Ready`, but the state publication contract is not
explicit. Future changes can easily break this assumption.

### Shutdown and Notification Lifetime Are Still Risky

Worker and collector threads post messages back to the window. Shutdown performs bounded waits, but a timeout can leave
threads running after window teardown. Display code should not rely on a valid `HWND` or loader object after destruction
has begun.

### Renderer Mutates Caller DC State

`ImgRenderer::Render()` changes the target DC clip region and resets it with `SelectClipRgn(..., nullptr)`. This can
discard the paint clip region and makes rendering dependent on careful cleanup paths.

### Fallback Painting Uses Mixed Coordinate Spaces

`DisplayFileInformation()` fills using `GetWindowRect()` against a client paint DC. It should use client coordinates.

## Priority 1: Correct Viewport Handling

### Plan

1. Add a small viewport type, for example `DisplayViewport { int width; int height; RECT client_rect; }`.
2. Track the current viewport in `ImgVwWindow`.
3. Handle `WM_SIZE` by updating the viewport and invalidating the window.
4. Add a browser/cache API for viewport changes.
   - First conservative version: reload the current item and newly queued items when the target size materially changes.
   - Better follow-up: keep decoded image state separate and regenerate only presentation state.
5. Recompute image offsets from the current viewport at paint or presentation-update time, not only during decode.
6. Add tests or a manual verification checklist for:
   - initial fullscreen display;
   - resize to smaller and larger client areas;
   - rotated JPEGs;
   - small images that should not upscale;
   - slideshow running during a size change.

### Acceptance Criteria

- A client-size change cannot leave the current image centered using stale dimensions.
- Newly collected items use the latest viewport target.
- Existing fullscreen startup behavior remains unchanged.

## Priority 2: Separate Image Data From Presentation State

### Recommended Shape

Move toward separate concepts:

- `ImageFrame`: decoded or normalized pixel data plus intrinsic dimensions and orientation already applied.
- `DisplayFrame`: viewport-specific display buffer, display dimensions, offsets, and bitmap info.
- `DisplayState`: current status, filepath, optional error text, and optional ready `DisplayFrame`.

### Incremental Plan

1. Add a small presentation calculation helper that computes fit size and offsets from source dimensions and viewport.
2. Use that helper from all loaders so sizing policy is centralized before moving ownership around.
3. Stop storing viewport-derived offsets as long-lived item state where practical.
4. Introduce immutable completed display data swapped into the item when loading finishes.
5. Later, allow `DisplayFrame` regeneration without re-reading the source file when the retained decoded data is
   available and memory limits allow it.

### Acceptance Criteria

- Fit/center rules live in one place.
- JPEG, HEIF, and GDI-backed images use the same presentation policy.
- Paint consumes a stable ready display object instead of partially built item fields.

## Priority 3: Harden Paint and Render Behavior

### Plan

1. Fix fallback painting to use `GetClientRect()`.
2. Make paint flow explicit:
   - fill background;
   - draw ready image if available;
   - otherwise draw a minimal loading/error/file path state.
3. Preserve the original paint DC state in `ImgRenderer`.
   - Use `SaveDC` and `RestoreDC`, or a scoped clip/selection guard.
   - Avoid resetting all clipping on the caller DC.
4. Add RAII guards for:
   - compatible memory DC;
   - selected bitmap;
   - saved DC state.
5. Consider a client-sized back buffer only if flicker or partial-paint artifacts remain after DC-state cleanup.

### Acceptance Criteria

- Renderer cleanup is correct on every failure path.
- Rendering respects the paint clip.
- Fallback/error display fills the correct client area.

## Priority 4: Make Async Display Publication Explicit

### Plan

1. Define how worker threads publish ready display data to the UI thread.
   - Prefer immutable completed state plus a single status transition.
   - Use synchronization that is supported by the project toolchain and XP target.
2. Avoid UI reads of mutable fields while a worker may still be writing them.
3. Carry enough detail in browser/loader notifications for the UI to ignore stale completions if the current item has
   changed.
4. Clear or disable notification targets before window destruction.
5. Coordinate this work with `artifacts/imgvw_stability_refactor_plan.md`, especially loader shutdown ownership.

### Acceptance Criteria

- The UI never paints from partially initialized bitmap or layout data.
- Late load notifications after shutdown or navigation are harmless.
- Thread lifetime and display state lifetime are documented in code comments or boundary types.

## Priority 5: Simplify Window Responsibilities

### Plan

1. Keep `ImgVwWindow` as the Win32 message adapter, but extract focused helpers:
   - `DisplayController` for viewport, current display state, invalidation, and paint decisions.
   - `CursorController` for capture, show/hide timer, and cursor balance.
   - `SlideshowController` for timer policy and waiting-for-image behavior.
2. Do this only after the lower-risk correctness fixes above, so extraction follows stable behavior.
3. Keep helpers free of broad Win32 dependencies unless they are explicitly UI helpers.

### Acceptance Criteria

- `ImgVwWindow` remains readable as command/message dispatch.
- Paint behavior can be tested or reasoned about without reading command handling and cursor code.
- No user-visible behavior changes are introduced by extraction alone.

## Suggested PR Sequence

1. Fix `DisplayFileInformation()` client coordinates and add renderer DC-state guards.
2. Add viewport tracking and `WM_SIZE` invalidation/reload behavior.
3. Centralize fit/center calculation and apply it across image formats.
4. Introduce immutable ready display data for item publication.
5. Clear notification targets and coordinate loader/browser shutdown behavior.
6. Extract display/cursor/slideshow helpers if the window file remains too broad.
7. Add focused tests or manual verification artifacts for resize, rotation, loading/error paint, and shutdown.

## Verification

Prefer at least one local build for each implementation PR:

- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x86 -Clean`
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x64 -Clean`

Manual display checks should include:

- launch with a large JPEG, a small image, a rotated EXIF JPEG, a HEIF image, and a PNG/GDI-backed image;
- resize or otherwise force a client-size change;
- navigate while images are still loading;
- start and stop slideshow while loading;
- close the window while a large image is loading;
- verify no stale background, mis-centering, or late-notification crash occurs.

## Relationship To Other Plans

- This plan complements `artifacts/imgvw_stability_refactor_plan.md`.
- The stability plan should own thread lifetime and general Win32 RAII work.
- This plan should own viewport, paint, renderer, and display-state behavior.
- If the two plans conflict, prefer the stability plan for shutdown ownership and this plan for presentation semantics.
