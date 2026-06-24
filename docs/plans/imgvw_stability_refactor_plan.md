# ImgVw Stability and Ownership Refactor Plan

## Summary

This plan supersedes the forward-looking parts of `artifacts/imgvw_architecture_refactor_plan.md`. The broad folder
reorganization, formatting setup, first test target, renderer result object, file operation result object, and initial
async image loading fixes are already in place. The remaining work should focus on correctness, lifetime safety, and
test coverage rather than another large architecture reshuffle.

The highest-priority risks are thread lifetime, cancellation state, raw Win32 handle ownership, and shutdown behavior in
the browser and loader subsystems. Preserve the Windows XP compatibility target throughout.

## Goals

- Make loader and browser shutdown deterministic, bounded, and free of use-after-free risks.
- Replace high-risk raw Win32 ownership with small XP-compatible RAII wrappers.
- Keep image decoding, browsing, and rendering boundaries simple and testable.
- Expand tests around cancellation, navigation, path handling, rendering, and image decode behavior.
- Continue improving explicit result/status reporting at subsystem boundaries.

## Non-Goals

- Do not perform another broad folder reorganization.
- Do not reformat unrelated files.
- Do not touch vendored code under `3rd-party/`.
- Do not replace the Win32 UI framework.
- Do not introduce Windows APIs newer than the current XP target unless they are guarded with compatible fallbacks.

## Current State

- First-party code is already organized under `src/app`, `src/ui/win32`, `src/browse`, `src/image`, and
  `src/platform/win32`.
- Formatting and lint configuration already exist.
- A first-party console test target exists under `tests/`.
- `ImgBrowser`, `ImgLoader`, `ImgRenderer`, and `FileOperations` already provide useful boundaries.
- `ImgLoader` queues cache-owned `std::shared_ptr<ImgItem>` instances instead of raw `ImgItem*`.
- Paint and common UI paths no longer wait indefinitely for image loading.

## Priority 1: Loader and Browser Shutdown Safety

### Problems To Address

- `ImgLoader` and `ImgBrowser` use shared cancellation flags across threads without synchronization.
- `ImgLoader::StopLoading()` can time out and then clear loader item storage while worker threads may still be running.
- Worker threads currently receive pointers to `LoaderItem` objects owned by a list that can be cleared during shutdown.
- `CreateThread`, `SetEvent`, `ResetEvent`, and wait failures are not consistently surfaced as result states.
- Browser collection and loader cancellation are separate flows, which makes app shutdown behavior harder to reason about.

### Plan

1. Introduce a small cancellation state that is safe to read from worker and controller threads.
   - Prefer Win32 event state for cancellation where practical.
   - If using C++ atomics, verify the project toolchain and XP target support the chosen usage.
2. Decouple worker thread input from `LoaderItem` list lifetime.
   - Pass worker threads a heap-owned or shared state object whose lifetime cannot end before the worker exits.
   - Do not let a timed-out shutdown destroy state still reachable by a running worker thread.
3. Make `StopLoading()` idempotent and explicit about outcomes.
   - Return or record whether shutdown completed, timed out, or failed.
   - Leave objects in a conservative state after timeout.
4. Make browser collection shutdown follow the same pattern.
   - Signal cancellation.
   - Wait with a bounded timeout.
   - Capture wait failure details.
   - Avoid resetting cancellation before a still-running thread has observed it.
5. Add focused tests with fake or controllable load items.
   - Idle loader double-stop.
   - Queued load cancellation.
   - Active worker cancellation.
   - Browser stop while collection is active.
   - Destructor after timeout, if a deterministic test seam can be introduced.

## Priority 2: Win32 RAII Ownership

### Initial Wrappers

Add minimal wrappers under `src/platform/win32/`:

- `Win32Handle` for `HANDLE` values closed with `CloseHandle`.
- `FindHandle` for `FindFirstFile` handles closed with `FindClose`.
- `GdiObject` or typed wrappers for objects closed with `DeleteObject`.
- `SelectedGdiObject` guard for restoring `SelectObject`.
- `CriticalSection` and `CriticalSectionLock` wrappers.

### Guidelines

- Keep wrappers tiny, move-only, and header-only unless implementation complexity grows.
- Preserve the invalid-handle distinction between `NULL` and `INVALID_HANDLE_VALUE`.
- Capture `GetLastError()` before wrapper cleanup can disturb the failing call context.
- Convert call sites incrementally, starting with thread/event/find handles and GDI selection restoration.

### First Conversion Targets

1. `ImgLoader` events and thread handles.
2. `ImgBrowser` collector thread and `FindFirstFile` handles.
3. `ImgItem` loaded event.
4. `ImgRenderer` memory DC and selected bitmap restoration.
5. Renderer tests that currently manually restore and delete GDI objects.

## Priority 3: Result and Error Boundaries

### Existing Good Patterns

- `FileOperationResult` reports shell operation status and shell error codes.
- `ImgRenderResult` reports render status and Win32 error codes.
- `ImgResampler::Result` keeps resize output explicit.

### Next Boundaries

- Loader start/stop result.
- Browser start/stop and collection result.
- File enumeration errors.
- ICC profile load/reset errors.
- Temporary file and mapping errors currently represented as strings or exceptions.

### Guidance

- Do not force one project-wide result type immediately.
- Add narrow result types where a subsystem boundary already exists.
- Prefer explicit status enums plus native error code fields over string-only errors.
- Keep user-facing error messages separate from internal diagnostic detail.

## Priority 4: Tests and Build Checks

### Test Expansion

Add or improve tests for:

- Loader shutdown with queued and active work.
- Browser collection cancellation.
- File enumeration path normalization.
- Empty, single-item, and duplicate-heavy navigation.
- Delete path double-null termination and long path handling.
- Renderer error paths for failed `CreateCompatibleDC`, `SelectObject`, and `BitBlt` where injectable seams are practical.
- ICC profile selection and fallback behavior.

### Build Verification

For changes in this plan, prefer at least one local build per PR:

- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x86 -Clean`
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x64 -Clean`
- Visual Studio build through `ImgVw.slnx` where available.
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\format.ps1 -Check`
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\tidy.ps1` when the compile database/toolchain is available.

## Priority 5: Static Analysis Cleanup

Handle clang-tidy and analyzer findings in small groups:

- Real narrowing conversions that can affect image dimensions, strides, or buffer sizes.
- Pointer casts crossing Win32 or C library boundaries.
- Unchecked Win32 return values.
- Manual resource management that should move to wrappers.
- Readability findings only when they reduce risk or clarify ownership.

Avoid large mechanical churn until shutdown and ownership risks are under control.

## Suggested PR Sequence

1. Add `Win32Handle`, `FindHandle`, and basic lock guards with tests or small compile coverage.
2. Harden `ImgLoader` worker state lifetime and `StopLoading()` behavior.
3. Harden `ImgBrowser` collection cancellation and find-handle ownership.
4. Convert selected GDI ownership in `ImgRenderer` and renderer tests.
5. Add loader/browser cancellation tests.
6. Add explicit result objects for loader/browser lifecycle outcomes.
7. Address high-confidence clang-tidy findings.

## Acceptance Criteria

- Shutdown does not destroy memory that a live worker thread can still access.
- Cancellation state is synchronized or event-driven, not an unsynchronized shared flag.
- Bounded waits report timeout/failure clearly and leave objects in a defensible state.
- Raw handles are reduced in the highest-risk paths without broad formatting churn.
- Existing Visual Studio and MSYS build entrypoints remain in sync.
- Tests cover the behavior that motivated each safety change.

