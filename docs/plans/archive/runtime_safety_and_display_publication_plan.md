# Runtime Safety and Display Publication Plan

## Status

**Completed on 2026-07-26.**

All three phases are implemented. Loader and collector state remains alive across bounded shutdown, Win32 resources
use move-only ownership wrappers, and paint consumes a reference-counted immutable display frame published atomically
with the ready state. Fit calculations are shared across JPEG, HEIF, and GDI-backed loading, and rendering preserves
the caller's DC state. Process-unique browser and load-context generations allow the UI to reject late notifications,
including after a context reset or window-handle reuse.

Broader error-result, static-analysis, and modularity work is completed and recorded in
`imgvw_modularity_and_testability_refactor_plan.md`.

This plan supersedes the remaining implementation items in `imgvw_stability_refactor_plan.md` and
`windowing_display_improvement_plan.md`. Those documents remain design-history and acceptance-detail
references.

## Phase 1: Prove and Stabilize Loader Lifetime

1. [x] Introduce move-only wrappers for closeable handles, find handles, selected GDI objects, and critical-section locks
   under `src/platform/win32/`.
2. [x] Replace loader worker arguments with a reference-counted worker state that outlives every started thread.
3. [x] Make cancellation event-driven or otherwise synchronized; do not retain unsynchronized shared cancellation flags.
4. [x] Change loader stop/start outcomes to an explicit result containing completed, timed-out, and Win32-failure states.
5. [x] On timeout, keep reachable state alive and prevent re-use until workers have exited; never clear worker input that
   a live thread can reach.

## Phase 2: Align Browser Collection and Session Replacement

1. [x] Apply the same bounded cancellation and result model to folder collection.
2. [x] Protect collector-thread input and `FindFirstFile` ownership with the new wrappers.
3. [x] Use a monotonically increasing browse-session generation on collection and load notifications.
4. [x] Drop late notifications before they reach UI state, including during window destruction and path replacement.
5. [x] Preserve warm-cache behaviour only when its generation and target-size key are still valid.

## Phase 3: Publish Immutable Display State

1. [x] Extract a fit-and-center helper with tests, then use it from JPEG, HEIF, and GDI-backed items.
2. [x] Build a complete display frame off the UI thread and publish it with one synchronized ready/error transition.
3. [x] Make paint consume only the published snapshot; it must never read a buffer, stride, offsets, or bitmap information
   still being written by a worker.
4. [x] Preserve the caller paint DC state with scoped GDI selection and DC-state restoration; use client coordinates for
   fallback painting.

## Tests and Exit Criteria

- [x] Add deterministic seams for idle double-stop, queued cancellation, active-worker cancellation, collection stop,
  timeout handling, and stale-notification suppression.
- [x] Decide whether a practical seam can cover failed renderer DC/bitmap selection; add the tests or record why the
  seam would add more complexity than value.
- [x] Manually close, resize, replace, and move the viewer between monitors while large files are loading, then record
  the result.
- [x] Run x86/x64 MSYS tests and release builds. Loader timeout tests retain reachable worker state, and paint reads only
  immutable completed display data.

## Closeout Evidence

The renderer keeps real-GDI tests for invalid input, successful drawing, caller-clip preservation, bitmap-selection
failure, and compatible-DC creation failure. A complete injected GDI call table was rejected: it would add eight
replaceable Win32 operations to production-facing test structure and would primarily test mock sequencing. An attempted
invalid-brush `FillRect` case also proved non-deterministic because GDI accepted the pseudo-handle on the tested system.
The remaining `SaveDC`, clip, restore, fill, and blit failures therefore retain defensive result paths without a broad
injection seam.

The loading smoke test used the x86 release build and the 345,978,692-byte panorama JPEG plus the 130,640,186-byte HEIC
fixture. On a three-monitor desktop, the window was resized, given a replacement path through `WM_DROPFILES`, moved
from the 1920x1200 primary display to 1920x1200 and 1440x900 secondary displays, captured directly in its stable loading
state, and closed while decode work was active. Every window operation succeeded; the process exited normally with
code 0 within the bounded wait, with no forced termination, partial frame, stale image, or crash.
