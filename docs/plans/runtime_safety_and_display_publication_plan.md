# Runtime Safety and Display Publication Plan

## Status

Viewport-aware fitting, monitor transitions, multi-monitor cache capacity, and empty-session replacement are in place.
The high-risk remaining work is unchanged from the stability and windowing plans: loader shutdown can time out while
workers still reference loader-owned state, and paint can still observe mutable image/display fields. The repository
does not yet contain the proposed general Win32 ownership wrappers.

This plan supersedes the remaining implementation items in `imgvw_stability_refactor_plan.md` and
`windowing_display_improvement_plan.md`. Keep those documents as design history and acceptance-detail references.

## Phase 1: Prove and Stabilize Loader Lifetime

1. Introduce move-only wrappers for closeable handles, find handles, selected GDI objects, and critical-section locks
   under `src/platform/win32/`.
2. Replace loader worker arguments with a reference-counted worker state that outlives every started thread.
3. Make cancellation event-driven or otherwise synchronized; do not retain unsynchronized shared cancellation flags.
4. Change loader stop/start outcomes to an explicit result containing completed, timed-out, and Win32-failure states.
5. On timeout, keep reachable state alive and prevent re-use until workers have exited; never clear worker input that
   a live thread can reach.

## Phase 2: Align Browser Collection and Session Replacement

1. Apply the same bounded cancellation and result model to folder collection.
2. Protect collector-thread input and `FindFirstFile` ownership with the new wrappers.
3. Use a monotonically increasing browse-session generation on collection and load notifications.
4. Drop late notifications before they reach UI state, including during window destruction and path replacement.
5. Preserve warm-cache behaviour only when its generation and target-size key are still valid.

## Phase 3: Publish Immutable Display State

1. Extract a fit-and-center helper with tests, then use it from JPEG, HEIF, and GDI-backed items.
2. Build a complete display frame off the UI thread and publish it with one synchronized ready/error transition.
3. Make paint consume only the published snapshot; it must never read a buffer, stride, offsets, or bitmap information
   still being written by a worker.
4. Preserve the caller paint DC state with scoped GDI selection and DC-state restoration; use client coordinates for
   fallback painting.

## Tests and Exit Criteria

- Add deterministic seams for idle double-stop, queued cancellation, active-worker cancellation, collection stop,
  timeout handling, and stale-notification suppression.
- Add renderer tests for failed DC/bitmap selection where a seam is practical.
- Manually close, resize, replace, and move the viewer between monitors while large files are loading.
- Run native tests plus x86/x64 MSYS release builds. Do not claim completion until a timeout cannot leave a worker with
  access to destructible state and paint reads only immutable completed display data.
