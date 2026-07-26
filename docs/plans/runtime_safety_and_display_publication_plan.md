# Runtime Safety and Display Publication Plan

## Status

All three phases are implemented. Loader and collector state remains alive across bounded shutdown, Win32 resources
use move-only ownership wrappers, and paint consumes a reference-counted immutable display frame published atomically
with the ready state. Fit calculations are shared across JPEG, HEIF, and GDI-backed loading, and rendering preserves
the caller's DC state. Process-unique browser and load-context generations allow the UI to reject late notifications,
including after a context reset or window-handle reuse.

Remaining work is the broader error-result and static-analysis pass, release validation, and the separate modularity
plan.

This plan supersedes the remaining implementation items in `imgvw_stability_refactor_plan.md` and
`windowing_display_improvement_plan.md`. Keep those documents as design history and acceptance-detail references.

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
- Add renderer tests for failed DC/bitmap selection where a seam is practical.
- Manually close, resize, replace, and move the viewer between monitors while large files are loading.
- [x] Run x86/x64 MSYS tests and release builds. Loader timeout tests retain reachable worker state, and paint reads only
  immutable completed display data.
