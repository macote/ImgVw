# ImgVw Stability and Ownership Refactor Implementation Plan

## Status

**Completed and archived on 2026-07-26.** The loader/browser lifetime, cancellation, RAII, immutable display
publication, buffer validation, explicit failure reporting, focused tests, and x86/x64 validation work was completed on
`codex/stability-ownership-refactor`. The active closeout record is
`runtime_safety_and_display_publication_plan.md`.

## Purpose

This document turns `imgvw_stability_refactor_plan.md` into an implementation sequence grounded in the
current source tree. The work is intentionally incremental: each phase should compile and test independently, avoid
unrelated formatting churn, and preserve the Windows XP target (`WINVER` and `_WIN32_WINNT` set to `0x0501`).

The implementation branch was `codex/stability-ownership-refactor`. Production modularity and test division are tracked
separately by `../imgvw_modularity_and_testability_refactor_plan.md`.

## Baseline

- Baseline commit: `b42dd4f` (`Archive completed plans`).
- Baseline x86 MSYS tests passed on 2026-07-21 with:
  `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test-msys.ps1 -Arch x86`.
- The test target is a single console executable built by `tests/Makefile`.
- The application build lists source objects explicitly in `Makefile`; Visual Studio lists headers and sources explicitly
  in `ImgVw.vcxproj` and `ImgVw.vcxproj.filters`.

## Implementation Progress

- Phase 1 is complete on `codex/stability-ownership-refactor`: foundational Win32 handle/find/critical-section wrappers,
  focused ownership tests, and initial `CountingSemaphore`/`FileMapView` conversions.
- Phase 2 is complete in `1252e1f` (`Harden image loader shutdown`): loader loop and worker threads retain shared
  runtime contexts instead of facade or list-element pointers; cancellation-aware semaphore waits and explicit
  start/stop results are present; deterministic blocking-worker tests cover timeout, retry, queued discard, and
  repeated stop behavior.
- Phase 3 is complete: `ImgBrowser` is a facade over a shared core retained by worker requests; collection and target
  queueing use independent event-backed cancellation generations; bounded stop results retain timeout/failure detail;
  find/thread/event handles use the Phase 1 owners; browse-path classification is isolated and covered.
- Phases 4 and 5 are complete: prioritized handles, locks, DCs, selected GDI objects, and renderer state use scoped
  ownership, and paint consumes immutable completed display frames.
- Phase 6 is complete for the planned boundaries: loader, browser, queueing, notification, mapping, ICC, and completion
  failures are observable through narrow status/result paths.
- Phase 7 and final validation are complete for the refactor scope: image boundaries were hardened, focused tests were
  split by subsystem, and x86/x64 MSYS and Visual Studio validation passed.

## Current Risk Inventory

### Loader lifetime and synchronization

`ImgLoader` currently owns its queue, events, loop-thread handle, worker records, semaphore, notification list, and
critical section directly.

- `cancellationflag_` is a plain `BOOL` read and written by several threads without synchronization.
- The loop entry point receives a raw `ImgLoader*`, so a loop-thread timeout followed by destruction leaves the worker
  able to access a destroyed object.
- Each load worker receives `LoaderItem*`, where the `LoaderItem` is owned by `loaderitems_` in the loop object.
- `StopLoading()` clears `loaderitems_` even after a worker wait times out or fails. This can destroy the exact object
  still used by `StaticThreadLoad()`.
- Worker completion callbacks capture `this`, so merely extending `LoaderItem` lifetime would not remove the use-after-
  free risk.
- `StopLoading()` resets `cancellationflag_` even when a thread may still be running.
- `LoadAsync()` and worker creation do not report `CreateThread()` failure. A failed worker creation can also consume a
  semaphore slot without a matching release.
- event creation, event signaling/reset, semaphore setup/wait/release, and wait failures are mostly silent.
- `loaderitems_` is mutated by the loop thread but read by `GetStats()` and `StopLoading()` without one consistently
  documented ownership or locking rule.

### Browser lifetime and synchronization

`ImgBrowser` collection and queue workers all retain a raw `ImgBrowser*`.

- `cancellationflag_` is a plain `BOOL` shared by collector and target-queue workers.
- Collection cancellation and target-queue cancellation use the same flag but are stopped separately. A successful stop
  of one group can reset cancellation while the other group is still running.
- `StopBrowsing()` ignores both stop outcomes. The destructor then deletes the critical section and closes state even if
  a thread timed out and still uses the browser.
- `BrowseAsync()` returns success even when collector `CreateThread()` fails.
- `TargetSizeQueueRequest` and `PathQueueRequest` are heap-owned correctly on normal paths, but still carry raw browser
  pointers and therefore do not protect browser lifetime.
- `CollectFolder()` manually manages its `FindFirstFile()` handle and does not preserve `FindNextFile()` failure details.
- `readyevent_`, `collectorthread_`, and target-queue handles use mixed `NULL`/`INVALID_HANDLE_VALUE` conventions.
- browser fields such as `recursive_`, `folders_`, target sizes, notifications, and thread vectors do not have a single
  clear synchronization contract.

### Other ownership hot spots

- `ImgItem` manually owns `loadedevent_` and assumes creation succeeded.
- `CountingSemaphore` duplicates generic handle ownership and exposes no failure result.
- `FileMapView` duplicates two generic handle owners and ignores `GetFileSizeEx()` failure.
- `ImgRenderer` manually restores the selected bitmap and deletes its compatible DC on every exit path.
- renderer tests manually restore selected objects and delete DC/bitmap/brush objects.
- `ImgCache` and the default ICC profile use raw `CRITICAL_SECTION` objects with manual initialization and destruction.

## Design Decisions

### 1. Use XP-compatible Win32 primitives

Cancellation will use manual-reset events rather than `std::atomic` as the primary cross-thread signal. Event state is
available on Windows XP, integrates directly with existing waits, and avoids uncertainty about older runtime support.
Cancellation checks must use `WaitForSingleObject(cancel_event, 0) == WAIT_OBJECT_0`; no unsynchronized mirror Boolean
will be retained.

### 2. Separate object facade lifetime from worker runtime lifetime

Both `ImgLoader` and `ImgBrowser` should become small facades over heap-owned runtime state. Thread entry points receive a
`std::shared_ptr`-owned request/runtime object, never `this` and never the address of a container element.

This is necessary because image decode and filesystem calls are not guaranteed to finish within the shutdown timeout.
Waiting forever in a destructor would avoid use-after-free but would violate bounded shutdown. With shared runtime
state, a timed-out facade can report failure and release its thread handle while the thread safely retains only the
state it needs. The remaining thread must not call back into the destroyed facade or touch window-owned memory.

### 3. Give worker groups independent cancellation generations

Loader-loop cancellation, browser collection cancellation, and browser target-queue cancellation need distinct
manual-reset events. A new operation gets a fresh operation state/event after the previous operation has definitely
stopped. A timed-out operation remains cancelled and cannot have its event reset underneath it.

### 4. Keep result types narrow

Do not introduce a project-wide error framework. Add subsystem-specific status enums carrying a `DWORD win32_error`
where a Win32 call failed. Expected lifecycle states such as already stopped and timed out are distinct from API
failures.

Proposed shape:

```cpp
enum class AsyncStopStatus
{
    AlreadyStopped,
    Stopped,
    TimedOut,
    WaitFailed,
    SignalFailed
};

struct AsyncStopResult
{
    AsyncStopStatus status{AsyncStopStatus::AlreadyStopped};
    DWORD win32_error{ERROR_SUCCESS};
    bool Stopped() const;
};
```

The exact loader/browser names may differ, but callers must be able to distinguish success, timeout, and native API
failure without parsing debug strings.

### 5. Make thread-count limits explicit

`WaitForMultipleObjects()` has a handle-count limit. The loader currently uses at most two workers, but browser queue
threads can grow without a comparable bound. Stop logic should either wait handles individually against one overall
deadline or process bounded batches while preserving a single total timeout. It must not spend three seconds per
thread.

### 6. Test deterministic seams, not timing luck

Tests should coordinate through events. A controllable fake `ImgItem` can signal that `Load()` started and wait on a
test-owned release event. Browser enumeration tests should use a small injectable enumeration/worker seam or an
operation object that can be exercised directly; tests should not depend on a large folder being slow enough to cancel.

## Phase 1: Foundational Win32 RAII

### Files

- Add `src/platform/win32/Win32Handle.h`.
- Add `src/platform/win32/FindHandle.h`.
- Add `src/platform/win32/CriticalSection.h`.
- Add `src/platform/win32/GdiObject.h` and `src/platform/win32/SelectedGdiObject.h` only when needed by Phase 5.
- Add a small `CompatibleDc`/`DeviceContext` owner if the renderer conversion shows that putting `DeleteDC` into a GDI
  object wrapper would blur incompatible cleanup rules.
- Update `ImgVw.vcxproj` and `ImgVw.vcxproj.filters` for every new header. Header-only wrappers require no `Makefile`
  object entry.

### Wrapper contracts

- Move-only; moved-from instances are empty.
- Default construction is empty and destruction is harmless.
- `Win32Handle` treats `NULL` as empty and calls `CloseHandle()`.
- `FindHandle` treats `INVALID_HANDLE_VALUE` as empty and calls `FindClose()`.
- Provide `get()`, `valid()`, `reset()`, and `release()` only; avoid implicit conversion to reduce accidental ownership
  transfer.
- A reset operation closes the previous valid value before storing the replacement.
- `CriticalSection` owns initialization/destruction and exposes no raw copy/move operation.
- `CriticalSectionLock` is a scoped non-copyable lock and supports the existing recursive semantics of Win32 critical
  sections.
- Do not attempt to report cleanup failure from destructors. Capture errors at the failing API call before cleanup.

### Initial conversions

- Convert `CountingSemaphore` to `Win32Handle` without changing its public behavior yet.
- Convert `FileMapView::file_` and `mapfile_` to `Win32Handle` while preserving the unmap-before-handle-close order.
- Add focused compile/runtime tests for default, reset, release, and move behavior using harmless event/find handles.

### Exit criteria

- No double close across move/reset paths.
- `NULL` and `INVALID_HANDLE_VALUE` conventions remain distinct.
- x86 and x64 tests compile with the C++17 MinGW toolchains.

## Phase 2: Loader Runtime and Deterministic Shutdown

### Runtime ownership model

Introduce an internal `ImgLoaderState` held by `std::shared_ptr` with:

- queue, pending-item set, notification list, preferred target size, and their critical section;
- work and cancellation events;
- semaphore state;
- worker registry and a dedicated lock/ownership rule;
- lifecycle status needed to prevent queueing or restarting after shutdown begins.

Introduce an internal `ImgLoadWorkerState` held independently by each worker with:

- `std::shared_ptr<ImgItem>`;
- shared loader runtime needed for completion bookkeeping;
- its own `Win32Handle` thread owner stored by the controller, not accessed through a list-element pointer;
- no callback capturing `ImgLoader*`.

The loop thread receives a heap bootstrap containing `std::shared_ptr<ImgLoaderState>`. The bootstrap is deleted or moved
into a local shared pointer at thread entry. Each load thread follows the same pattern with `ImgLoadWorkerState`.

### Lifecycle behavior

- Construction creates all required primitives before starting the loop. If setup fails, retain an explicit start
  result/status and leave the object safely non-running.
- `QueueItem()` rejects new work after stop begins. Decide whether to return `bool` or a narrow queue result; update only
  callers that need the outcome.
- The loop waits on work and cancellation events and gives cancellation priority before dispatching more work.
- Acquire a semaphore slot in a cancel-aware wait (`semaphore` plus cancellation event), rather than an unconditional
  infinite wait that can delay shutdown.
- If worker `CreateThread()` fails, remove pending bookkeeping, release the semaphore slot, set/report the failure, and
  continue or stop according to the selected lifecycle policy.
- Worker completion updates shared runtime, releases its slot exactly once, and posts notifications from a snapshot taken
  under lock. Do not hold the queue lock while calling `PostMessage()`.
- `StopLoading(timeout)` is idempotent. It signals cancellation once, waits against one overall deadline, and reports an
  `AlreadyStopped`, `Stopped`, `TimedOut`, or failure result.
- On timeout/failure, do not clear live worker state, reset cancellation, or permit a restart. The shared runtime remains
  valid until the last worker exits.
- Queue/pending cleanup is performed under lock and only destroys items not retained by an active worker.
- The destructor calls the bounded stop path. A timed-out thread may outlive the facade but owns no facade pointer.

### Test additions

- idle loader stop, repeated stop, and destruction;
- queued work discarded during stop;
- fake item blocked before `Load()` completion, proving active worker cancellation/timeout does not free worker state;
- release of a blocked fake after a timeout, proving cleanup completes without calling a destroyed facade;
- worker thread creation failure through an injectable thread factory seam;
- event signal and wait failure result mapping where practical;
- semaphore slot recovery after worker start failure;
- notification removal concurrent with completion, verified without posting while holding a stale facade pointer.

### Exit criteria

- No thread entry point receives `ImgLoader*` or `LoaderItem*`.
- No plain shared cancellation Boolean remains.
- Timeout paths retain all state reachable by live threads.
- `StopLoading()` outcome is observable by tests and callers.

## Phase 3: Browser Operation State and Shutdown

### Split operation state

Use separate shared operation objects rather than one browser-wide cancellation flag:

- `CollectionOperation`: cancellation event, normalized root/folder inputs, recursive mode, ready event reference, and the
  shared browser data needed to publish discovered files/folders.
- `TargetQueueOperation`: cancellation event, captured paths/target sizes/load-next flag, cache/loader context, and the
  minimum shared data required to check active target sizes and queue items.

If sharing the mutable browser file list directly makes the operation object too coupled, introduce a shared
`ImgBrowserState` containing the critical section, file list, folder list, target-size state, notification target, and
load context. The facade owns this state and workers retain it. Do not solve timeout safety by retaining `ImgBrowser`
itself in a self-cycle.

### Collection changes

- Convert all find handles to `FindHandle`.
- Normalize startup path parsing into a small synchronous helper that returns file/folder classification plus an error
  code. This creates a direct test seam for forced folders, relative file names, trailing separators, missing paths,
  and `FindFirstFile()` failure.
- `BrowseAsync()` must return/report collector thread creation failure and leave state consistent.
- The collector checks its cancellation event between entries and before recursion.
- Preserve the error from a failed `FindNextFile()` when it is not `ERROR_NO_MORE_FILES`.
- Publishing a file should take the browser-state lock only for state mutation. Loader queueing and notification posting
  should occur outside the lock wherever possible to reduce re-entrant/blocking risk.

### Target queue changes

- Replace heap requests containing `ImgBrowser*` with shared operation state.
- Cancellation of target queueing must not cancel collection and vice versa.
- Bound the number of queue threads or consolidate requests into one queue worker. Prefer one worker with a request queue
  if it can be introduced without changing preload ordering.
- Cleanup closes completed thread handles through RAII; timeout cleanup drops only the controller handle, not the state
  retained by a live thread.

### Lifecycle/result changes

- Replace internal `BOOL` stop outcomes with explicit collection and queue stop results carrying `DWORD` errors.
- `StopBrowsing()` combines the two outcomes without discarding either. Define precedence: wait/signal failure outranks
  timeout, which outranks successful/already-stopped results.
- `BrowseAsync()` starts new work only after prior collection and queue operations are confirmed stopped. A timed-out old
  generation remains isolated and cannot publish into a new generation; use a generation ID or operation identity check
  during publication.
- Destructor performs bounded cancellation and may release the facade safely because workers own shared operation/state.
- Notification removal must be synchronized and old generations must not post to a detached window.

### Test/build integration

The current test target does not compile `ImgBrowser.cpp` and avoids the full HEIF dispatcher chain. Prefer testing the
new path-classification and collection-operation helpers separately with narrow dependencies. If direct browser tests
are added, update `tests/Makefile` deliberately and link only the extra first-party/dependency objects required.

Add tests for:

- file path, folder path, forced-folder trailing separator, missing path, and relative path normalization;
- collection cancellation using a controllable enumerator seam;
- stop during active collection and repeated stop;
- collection thread creation failure;
- independent collection and target-queue cancellation;
- old operation generation unable to publish after a replacement browse;
- destructor after deterministic timeout with later worker release;
- empty, single-item, and duplicate-heavy navigation regression coverage.

### Exit criteria

- No browser worker request or entry point contains `ImgBrowser*`.
- Collection and target queueing have independent cancellation state.
- Old/timed-out operations cannot mutate a new browse generation.
- Browser destruction cannot invalidate state reachable by a live worker.

## Phase 4: Remaining High-Risk Handle and Lock Conversion

Convert in small reviewable groups:

1. `ImgLoader` events, loop thread, workers, and semaphore.
2. `ImgBrowser` ready event, collector/queue threads, and enumeration handles.
3. `ImgItem::loadedevent_`.
4. `ImgCache` and the loader/browser critical sections.
5. Default ICC profile critical-section ownership, taking care with static initialization/destruction order.

For each group:

- capture `GetLastError()` immediately after failure;
- remove redundant manual close/delete code only after the wrapper owns the resource;
- verify member destruction order matches dependency order (threads/operations before events and locks);
- avoid converting unrelated `ImgVwWindow` GDI resources in the same change.

## Phase 5: Renderer GDI Ownership

### Implementation

- Add a compatible-DC owner that calls `DeleteDC()`.
- Add typed or templated GDI object owners only for objects destroyed by `DeleteObject()`.
- Add `SelectedGdiObject` to restore the previous object unless explicitly released.
- Convert `ImgRenderer::Render()` first. Ensure `GetLastError()` is captured before guard destructors run.
- Preserve the existing clip-region cleanup behavior and result precedence.
- Convert renderer test setup/cleanup next so failure assertions cannot leak objects or leave an object selected into a
  deleted DC.
- Defer the many `ImgVwWindow.cpp` GDI sites to follow-up commits grouped by rendering function.

### Tests

- existing success and invalid-input cases;
- failed compatible-DC creation, select, clip, fill, reset, and copy via a narrow injectable Win32-call table where
  practical;
- guard move/release behavior and restoration on early return;
- verify the native error is the one from the failed API, not from cleanup.

## Phase 6: Result and Error Boundaries

After lifecycle safety is stable, expose narrow results at the remaining boundaries:

- loader construction/start, queue dispatch, and stop;
- browser path classification/start, collection completion, and stop;
- file enumeration completion/error;
- `ImgItem` loaded-event creation;
- ICC profile open/reset, separating profile status from user-facing messages;
- `FileMapView` open, size, mapping, and view errors.

Compatibility guidance:

- Introduce result-returning overloads or internal methods before changing UI call sites broadly.
- Preserve existing user-visible behavior unless the new diagnostic enables a clearly actionable message.
- Avoid string-only error state; retain the native code and generate text at the UI/log boundary.
- Fix `FileMapView::GetFileSize()` to check `GetFileSizeEx()` and capture its error before cleanup.

## Phase 7: Static Analysis and Follow-Up Cleanup

Run static analysis only after the ownership changes settle, then address findings in risk-oriented groups:

- image dimension/stride/buffer-size narrowing;
- thread parameter and Win32/C library pointer casts;
- unchecked Win32 calls;
- remaining manual ownership in touched paths;
- readability findings only where they clarify ownership or synchronization.

Do not mix broad naming, formatting, or vendored-code changes into these commits.

## Proposed Commit Sequence

1. `Add Win32 ownership wrappers`
2. `Cover Win32 wrapper ownership behavior`
3. `Separate loader runtime from facade lifetime`
4. `Report loader lifecycle failures`
5. `Cover loader cancellation and timeout behavior`
6. `Separate browser worker operation state`
7. `Harden browser collection shutdown`
8. `Cover browser cancellation and path handling`
9. `Restore renderer GDI objects with guards`
10. `Report remaining mapping and profile failures`
11. `Fix high-confidence ownership analyzer findings`

Commits may be combined when a test cannot compile meaningfully without its implementation, but loader and browser
runtime changes should remain separate.

## Verification Matrix

Run incrementally after each phase; clean builds are reserved for project/build-system changes and final validation.

| Change type | Required verification |
| --- | --- |
| Header-only RAII wrappers | x86 and x64 MSYS tests; format check |
| Loader changes | x86 and x64 MSYS tests; incremental x86 application build |
| Browser changes | x86 and x64 MSYS tests; incremental x86 and x64 application builds |
| Project file changes | Visual Studio build where available; one clean MSYS build per architecture before merge |
| Resource changes | Not expected; if needed, preserve ANSI/CRLF and run a `windres` or Visual Studio build |
| Renderer/GDI changes | x86 and x64 tests plus application smoke test |
| Final branch validation | release x86/x64 application builds, x86/x64 tests, format check, tidy when available |

Commands:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test-msys.ps1 -Arch x86
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test-msys.ps1 -Arch x64
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x86
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x64
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\format.ps1 -Check
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\tidy.ps1
```

Final clean validation may add `-Clean` to the test/build commands.

## Review Checklist for Every Async Change

- Does any thread entry point receive `this` or a container-element address?
- Can a timeout destroy state the thread can still reach?
- Is cancellation event-driven and generation-specific?
- Can cancellation be reset while an older worker is alive?
- Is there one overall timeout rather than one timeout per thread?
- Are thread creation, event operations, semaphore operations, and waits checked?
- Is `GetLastError()` captured before cleanup or another Win32 call?
- Are callbacks/window notifications detached before UI destruction?
- Are shared collections consistently protected, and are external calls made outside locks where possible?
- Are thread handles and runtime lifetime treated as separate concerns?
- Does a failed start leave queue/semaphore/pending-item bookkeeping balanced?
- Is the failure observable in a result and covered by a deterministic test?

## Completion Criteria

The refactor is complete when:

- loader and browser workers retain only shared operation/runtime state, never facade pointers;
- destructor and explicit stop paths use bounded waits without risking use-after-free;
- cancellation events replace all unsynchronized shared cancellation flags;
- timeouts and Win32 failures are explicit, stable result states;
- thread/event/find/GDI resources in the prioritized paths use the correct RAII owner;
- old browser generations cannot publish after a new browse begins;
- deterministic tests cover idle, active, cancelled, failed-start, timeout, and repeated-stop behavior;
- application/test project definitions remain synchronized;
- x86/x64 MSYS tests and release builds pass, with Visual Studio verification where available;
- Windows XP compatibility definitions and APIs are preserved.

## Recommended First Implementation Slice

Begin with Phase 1 only: add `Win32Handle`, `FindHandle`, and critical-section guards; add ownership tests; convert
`CountingSemaphore` and the two `FileMapView` handles; update Visual Studio project/filter files; run x86/x64 tests and
an incremental x86 application build. This establishes the ownership vocabulary used by later async work without
mixing it with thread-lifetime behavior.
