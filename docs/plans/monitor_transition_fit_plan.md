# Monitor Transition Image Fit Plan

## Summary

ImgVw can be moved between displays, but the displayed image may keep sizing and centering decisions that were computed
for the original viewport. The current display path captures the client size when browsing starts, passes that size into
`ImgBrowser`, `ImgCache`, and each `ImgItem`, and then stores viewport-derived display buffers and offsets on the item.
`WM_SIZE` is currently ignored in `ImgVwWindow::HandleMessage()`, so a later client-size or monitor/work-area change does
not refresh the current presentation state.

The near-term fix should make monitor and viewport changes explicit without changing the app's fullscreen-first workflow
or Windows XP compatibility target.

## Goals

- Refit the current image when the window moves to a monitor with a different usable size.
- Refit the current image when the client area changes for any reason.
- Keep newly collected or loaded items using the latest viewport target.
- Cache viewport-specific display items without mutating an item that was loaded for a different target size.
- Preserve current navigation, slideshow, cursor, and borderless popup behavior.
- Avoid Win32 APIs newer than the XP target unless guarded with compatible fallbacks.

## Non-Goals

- Do not introduce per-monitor DPI behavior as part of the first fix. Windows XP has system DPI, but not per-monitor DPI.
- Do not replace GDI rendering, add Direct2D, or change decoder selection.
- Do not redesign zoom/pan behavior.
- Do not broadly refactor loader/thread ownership unless needed to make viewport updates correct.
- Do not edit generated build output under `bin/` or `obj/`.

## Current Behavior

- `ImgVwWindow::Create()` creates a `WS_POPUP` window sized from `SM_CXSCREEN` and `SM_CYSCREEN`.
- `ImgVwWindow::InitializeBrowser()` calls `GetClientRect()` once and passes that width and height to
  `ImgBrowser::BrowseAsync()`.
- `ImgBrowser` stores `targetwidth_` and `targetheight_`, and `CollectFile()` uses those values when adding items to
  `ImgCache`.
- `ImgItem` stores `targetwidth_` and `targetheight_`; `SetupDisplayParameters()` stores `offsetx_` and `offsety_` from
  those target dimensions.
- `ImgVwWindow::DisplayImage()` uses the current client rectangle for background fill, but it still draws the item using
  the item's stored `displaywidth()`, `displayheight()`, `offsetx()`, and `offsety()`.
- `WM_SIZE` and `WM_SETFOCUS` return without updating display state.
- There is no current monitor tracking via `MonitorFromWindow()` / `GetMonitorInfo()`.

## Root Cause

The viewport is treated as an image-load parameter instead of current presentation state. Moving the window to another
monitor can change the effective client size, work area, or scaling environment, but the ready image bitmap and offsets
remain tied to the earlier target size.

## Recommended PR Sequence

### PR 1: Add Viewport-Sized Cache Keys And Refitting

Add a small viewport path in `ImgVwWindow`, `ImgBrowser`, and `ImgCache`:

1. Add a cache key that includes the file path and the current client-area pixel target:
   - `std::wstring filepath`;
   - `INT targetwidth`;
   - `INT targetheight`.
2. Change `ImgCache` from a filepath-only map to a key-based map, so one source file can have separate `ImgItem`
   instances for different viewport sizes.
3. Add an `ImgBrowser::UpdateTargetSize(INT targetwidth, INT targetheight)` API.
4. Store the latest valid target size in `ImgBrowser`.
5. When the target size changes, ensure future `CollectFile()` calls use the new size.
6. Make `ImgBrowser::GetCurrentItem()` resolve the current filepath with the latest target size:
   - if an item for that key exists, return it;
   - if the filepath exists but that viewport-specific item is missing, create it and queue it at high priority;
   - leave older viewport-specific entries available for reuse if the window moves back to a previous size.
7. Handle `WM_SIZE` in `ImgVwWindow`:
   - ignore minimized or zero-sized client areas;
   - compare against the last tracked client size;
   - call the browser target-size update only when dimensions actually change;
   - invalidate the window after the update.

The key should not include monitor identity in the first implementation. The current loaders produce display buffers from
pixel dimensions, so two monitors with the same client width and height can share the same cached display item. The key
should also not include DPI unless a later display policy actually changes the produced bitmap based on DPI.

This fixes the real symptom without needing an immediate rewrite of decoded-image versus display-image ownership. It also
avoids mutating an `ImgItem` that may already be loading or ready for another viewport.

### PR 2: Detect Monitor Changes Explicitly

Track monitor identity in `ImgVwWindow`:

1. Add a `HMONITOR current_monitor_` member.
2. Initialize it after window creation or during `OnCreate()` with:
   - `MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST)`.
3. Handle `WM_WINDOWPOSCHANGED` or `WM_MOVE`:
   - call `MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST)`;
   - compare with `current_monitor_`;
   - if it changed, update `current_monitor_`, refresh viewport state, and invalidate.
4. Use `GetMonitorInfo()` only for optional clamping or diagnostics. The core refit should be based on the actual client
   rectangle, because that is what `ImgRenderer` paints into.

This preserves XP compatibility: `MonitorFromWindow()` and `GetMonitorInfo()` are available for the current target.

### XP DPI Compatibility

Windows XP supports a system-wide DPI setting that code can read with the long-standing GDI `GetDeviceCaps()` values such
as `LOGPIXELSX` and `LOGPIXELSY`. Windows XP does not support per-monitor DPI, `WM_DPICHANGED`, `GetDpiForWindow()`,
`GetDpiForMonitor()`, or per-monitor DPI awareness modes.

For this feature, the important input is the actual client area in pixels. That value is available through
`GetClientRect()` and remains XP-compatible. If a later feature needs DPI-aware text overlays or physical-size zoom
policy, add a separate compatibility layer that uses system DPI on XP and newer DPI APIs only behind runtime/compile-time
guards.

### PR 3: Clamp Borderless Window To The Target Monitor When Appropriate

The app creates a borderless popup using primary-screen metrics. When moved to a smaller monitor, the window itself can be
larger than the target monitor. Add a narrow policy decision:

1. If ImgVw is intended to remain fullscreen-like, resize/reposition the popup to the target monitor bounds or work area
   after a monitor change.
2. Use `MONITORINFO.rcMonitor` for true fullscreen behavior, or `MONITORINFO.rcWork` if the app should respect the
   taskbar/work area.
3. Avoid fighting user-driven window placement if a future windowed mode is added. Keep this policy isolated in a helper
   such as `ApplyMonitorBoundsIfFullscreen()`.

Open question for implementation: choose `rcMonitor` or `rcWork`. The existing `WS_POPUP` fullscreen-like startup points
to `rcMonitor`, but the user-facing "fit target monitor" complaint may be better served by `rcWork` if the taskbar should
remain visible.

### PR 4: Centralize Presentation Fit Calculation

After the conservative reload/refit fix is in place, reduce recurrence risk:

1. Add a fit helper, for example `DisplayFit CalculateDisplayFit(source_width, source_height, target_width,
   target_height)`.
2. Use the helper from JPEG, HEIF, and GDI-backed item paths.
3. Keep the no-upscale rule unless the existing format-specific path already does otherwise.
4. Add unit tests for:
   - source smaller than viewport;
   - source wider than viewport;
   - source taller than viewport;
   - exact fit;
   - rotated dimensions.

### PR 5: Split Decode Data From Viewport-Specific Display Data

The longer-term design should stop requiring file reloads for simple viewport changes:

1. Store intrinsic decoded image data separately from display buffers and offsets.
2. Regenerate only the display buffer when the viewport changes.
3. Publish completed display state immutably to the UI thread.
4. Keep memory limits explicit so large images do not unexpectedly remain fully decoded in memory.

This overlaps with `docs/plans/windowing_display_improvement_plan.md`; that plan should remain the broader display
architecture reference.

## Implementation Notes

- Keep monitor-change handling in `src/ui/win32/ImgVwWindow.*`.
- Keep cache and loading target-size behavior in `src/browse/ImgBrowser.*` and `src/image/ImgCache.*`.
- Prefer `filepath + targetwidth + targetheight` as the Phase 1 cache key.
- Avoid broad changes to `ImgItem` during the first PR; separate item instances per target size should reduce the need
  for item mutation.
- Capture `GetLastError()` close to failing Win32 calls if new error handling is added.
- Use `-NoProfile` for PowerShell verification commands.
- Do not add unguarded DPI APIs such as `WM_DPICHANGED`, `GetDpiForWindow()`, or `GetDpiForMonitor()`; they are not
  available on Windows XP.

## Acceptance Criteria

- Moving ImgVw from a larger monitor to a smaller monitor cannot leave the current image scaled or centered against the
  old viewport.
- Moving ImgVw from a smaller monitor to a larger monitor refreshes the current image presentation.
- Resizing or otherwise changing the client area refreshes the current image presentation.
- Newly discovered files use the latest target dimensions.
- Returning to a previously used client size can reuse the matching viewport-specific cached item.
- Small images still do not upscale if that is the active policy for their loader path.
- Slideshow continues to advance after a viewport or monitor change.
- The app still builds for the documented XP-compatible configuration.

## Verification

Build at least one local path:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x86 -Clean
```

Prefer both architectures when the toolchain is available:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x64 -Clean
```

Manual checks:

- Launch on the primary monitor with a large landscape JPEG.
- Move the window to a smaller secondary monitor and verify the image refits.
- Move the window back to the larger monitor and verify the image refits again.
- Repeat with a portrait image, a small image, and a rotated EXIF JPEG.
- Start slideshow, move the window to another monitor, and verify it keeps advancing.
- Navigate while an image is loading and verify no stale dimensions appear after load completion.

## Risks

- Creating a new cache entry for every live-resize size can increase memory use. Mitigate by only reacting to real
  dimension changes, considering resize-end/debounce behavior if needed, and adding cache pruning if repeated sizes become
  a problem.
- Multiple viewport-specific items for the same filepath can duplicate decode work. This is acceptable for the first
  correctness fix, but PR 5 should replace it with regenerated presentation data where practical.
- Monitor clamping can surprise users if they intentionally moved only part of the popup onto another monitor. Keep that
  behavior isolated and easy to adjust.
