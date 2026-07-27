# ImgVw Modularity and Testability Refactor Plan

## Status

**Active.** The ownership, immutable-display, notification-generation, buffer-boundary, and focused-test prerequisites
are complete. Track 1 (`ImgVwWindow` modularity) and Track 3 (test division) are complete. The remaining work is Track
2 production hardening: finish buffer/decode arithmetic, extend the remaining explicit-error boundaries, and close the
layering and static-analysis gaps.

## Progress Checklist

This checklist is the concise source of truth for plan progress. A checked parent item is complete; an unchecked parent
with checked children is in progress.

### Track 1: `ImgVwWindow` Modularity

- [x] Phase 1: characterize behavior and split command, timer, and mouse dispatch.
- [x] Phase 2: extract and test `WindowGeometry`, `EmptyStateLayout`, and `OverlayText`.
- [x] Phase 3: extract `EmptyStateView`, including controls, resources, layout, painting, and focus behavior.
- [x] Phase 4: extract display and overlay presentation.
  - [x] Introduce immutable `DisplaySnapshot` input.
  - [x] Extract ready/loading/error decisions and renderer invocation into `DisplayPresenter`.
  - [x] Introduce `InfoOverlay` and move item-text and loading-progress debounce policy into it.
  - [x] Move cached overlay text and rectangles into `InfoOverlay`.
  - [x] Move overlay font lifetime, layout, and composited drawing into `InfoOverlay`.
  - [x] Return timer and invalidation actions to `ImgVwWindow` instead of performing those side effects internally.
  - [x] Add focused loading, error, first-paint, partial-paint, and overlay tests.
- [x] Phase 5: extract and test `CursorController`, `WindowDragController`, and `MonitorPlacement`.
- [x] Phase 6: extract slideshow policy.
  - [x] Extract and test `SlideShowStateMachine`.
  - [x] Extract multi-monitor coordination with safe registration and teardown.
- [x] Phase 7: reduce `ImgVwWindow` to a composition root and Win32 adapter.
  - [x] Extract loader-stat snapshot aggregation from the window.
  - [x] Extract display-selection and first-paint session state.
  - [x] Consolidate browse/view-session state and remove dead window fields.
  - [x] Extract filename-versus-stats overlay visibility policy.
  - [x] Audit the remaining methods as Win32 adaptation or explicit subsystem orchestration.

### Track 2: Remaining Production Hardening

- [x] Preserve completed loader/browser cancellation, worker-lifetime, generation, and notification-target contracts.
- [x] Finish lower-risk Win32 RAII conversions beyond the completed high-risk prerequisites.
  - [x] Convert remaining registry-key and COM interface/allocation ownership.
  - [x] Convert remaining lower-risk UI GDI and acquired-DC ownership.
- [x] Split `ImgBrowserCore`.
  - [x] Extract `FolderScanner`.
  - [x] Extract `BrowseSession`.
  - [x] Extract `PreloadScheduler`.
- [x] Finish buffer and decode-boundary hardening beyond the completed foundational checks.
  - [x] Validate JPEG output strides, pointer arithmetic, and libjpeg input-size conversion.
  - [x] Bound RGBA resampling dimensions, intermediate storage, and allocation failures.
  - [x] Validate GDI+ bitmap dimensions, locked-buffer layouts, and unlock ownership.
  - [x] Limit HEIF decode dimensions and validate RGBA-to-BGR conversion storage.
  - [x] Validate CMYK transform layouts and transformed display-buffer size.
- [ ] Extend explicit result/error boundaries for enumeration, ICC handling, mappings, settings, and temporary paths.
- [ ] Run focused layering and static-analysis cleanup.

### Track 3: Tests and Final Validation

- [x] Split shared test support and subsystem test sources.
- [x] Add independently runnable core, platform, image, concurrency, and UI shards to MSYS and Visual Studio builds.
- [x] Add tests alongside each remaining production component extraction.
- [ ] Complete final x86/x64 MSYS and Visual Studio builds, formatting/static analysis, and manual regression checks.

Current next item: extend explicit result/error boundaries for enumeration, ICC handling, mappings, settings, and
temporary paths.

## Review Update (2026-07-26)

The plan was reconciled with the current source tree and history after the completed browser decomposition and
lower-risk RAII work. The remaining work is production hardening, not further responsibility extraction.

- `ImgVwWindow.cpp` remains approximately 2,200 lines, but the remaining methods are Win32 adaptation and explicit
  subsystem orchestration after the completed composition audit.
- `ImgBrowser` is a lifetime-safe facade over a core that now delegates folder enumeration to `FolderScanner`,
  collection/generation state to `BrowseSession`, and target/path worker-queue ownership to `PreloadScheduler`.
  Navigation, cache/load-context coordination, and notification adaptation remain intentionally visible in the core.
- `WindowGeometry`, `EmptyStateLayout`, and `OverlayText` now provide the first pure helper boundaries and core tests.
  `EmptyStateView` owns empty/searching state, its message, logo image and backing stream, caption and text fonts,
  control windows, layout, painting, and focus/dialog behavior. `DisplayPresenter` consumes an immutable display
  snapshot and owns ready/loading/error paint decisions plus renderer invocation. `InfoOverlay` owns item and statistics
  presentation, cached layout/content, font lifetime, composited drawing, and loading-progress policy. It returns timer
  and invalidation requests for `ImgVwWindow` to execute. `CursorController`, `WindowDragController`, and
  `MonitorPlacement` own interaction state and calculations while the window retains Win32 side effects.
  `SlideShowStateMachine` owns single-window timing/navigation state, and `MultiMonitorSlideShowCoordinator` owns
  stable secondary-window registrations, deterministic target rotation, shared sequential cursor/preload state, and
  target-size load-context reuse.
- Move-only wrappers now own the remaining registry, COM interface/task-memory, global-memory, acquired-DC, and UI GDI
  resources. Platform ownership tests cover move and teardown behavior, and both MSYS architectures build successfully.

The next implementation slice is buffer and decode-boundary hardening. Keep arithmetic/validation changes separate
from the subsequent explicit-error-boundary work where practical.

## Summary

`ImgVwWindow.cpp` has grown into the main concentration of application behavior. It currently combines Win32 message
dispatch, window creation, browser coordination, empty-state controls, painting, overlays, cursor and drag behavior,
single-monitor slideshow policy, multi-monitor slideshow coordination, ICC commands, and file operations. The class is
therefore difficult to change safely even though several lower-level subsystems already have useful boundaries.

This plan incrementally reduces `ImgVwWindow` to a Win32 adapter and composition root, improves the quality and
robustness of the remaining first-party code, and divides the current monolithic test target into focused, independently
runnable units. Refactoring should preserve current behavior and the Windows XP compatibility target.

The completed safety prerequisites are recorded in
`archive/runtime_safety_and_display_publication_plan.md`; the superseded detail plans remain under `archive/`. Their
ownership and display-publication rules remain authoritative while the coupled slideshow and presentation code is
extracted.

## Test Refactor Status

The test-refactor portion of this plan is complete:

- shared harness, temporary-file support, and JPEG fixtures live under `tests/support/`;
- tests are split into `core`, `platform`, `image`, `concurrency`, and `ui` suites;
- both MSYS and Visual Studio can build and run one suite without compiling the other test translation units;
- the complete suite remains the default for release validation.

For a quick MSYS iteration, select a suite through the repository script:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test-msys.ps1 -Arch x86 -Suite ui
```

The corresponding direct Make targets are `test-core`, `test-platform`, `test-image`, `test-concurrency`, and `test-ui`.
Visual Studio builds select the same suites with `/p:TestSuite=Core`, `Platform`, `Image`, `Concurrency`, or `Ui`.
Omitting the selector runs all tests.

The remaining phases in this document concern production modularity and UI ownership; they are separate from the
completed test split.

## Goals

- Keep `ImgVwWindow` focused on Win32 message handling, command dispatch, and component composition.
- Give empty-state UI, presentation, overlays, window interaction, and slideshow policy explicit ownership boundaries.
- Extract policy and calculations from Win32 calls so they can be tested without creating real windows.
- Harden thread lifetime, resource ownership, buffer validation, and error reporting throughout the application.
- Split tests by subsystem and execution characteristics so focused tests are quick to build, run, and diagnose.
- Keep Visual Studio and MSYS/Makefile project definitions synchronized throughout the work.

## Non-Goals

- Do not replace the Win32 frontend or GDI rendering stack.
- Do not redesign the user-visible keyboard, mouse, slideshow, or multi-monitor workflow.
- Do not introduce a general application framework, dependency-injection framework, or third-party test framework.
- Do not create interfaces for every class merely to reduce direct dependencies.
- Do not move method definitions into additional files without also improving state ownership and cohesion.
- Do not perform broad formatting-only changes or modify vendored code under `3rd-party/`.
- Do not introduce post-XP APIs without compile-time guards and compatible fallbacks.

## Current State

- `ImgVwWindow.cpp` is substantially larger than any other production translation unit.
- `ImgVwWindow` owns multiple independent state groups: empty state, viewport and monitor state, cursor and dragging,
  overlays, slideshow timing, multi-monitor windows, and browser/display paths.
- Its `HandleMessage()` function performs both message adaptation and application command policy.
- `ImgBrowser`, `ImgLoader`, `ImgRenderer`, `PathPicker`, and `FileOperations` already provide useful subsystem seams.
- `ImgBrowser` is now a lifetime-safe facade over `ImgBrowserCore`, but the core remains a large concentration of
  enumeration, cancellation, navigation, preloading, cache coordination, and notifications.
- `tests/ImgVwTests.cpp` is now a small all-suite aggregator. Shared support is under `tests/support/`, and focused test
  translation units are grouped by subsystem under `tests/unit/`, `tests/platform/`, `tests/image/`,
  `tests/concurrency/`, and `tests/ui/`.
- The completed safety plans define cancellation, thread-input lifetime, notification-generation, RAII, and immutable
  display-publication contracts that must be retained while components are moved.

## Design Principles

1. Extract behavior before abstraction. Start with cohesive value calculations and state machines, then introduce the
   minimum boundary needed by their callers.
2. Keep Win32 calls at the edge. Policy components may use simple Win32 value types such as `RECT`, `SIZE`, and `POINT`,
   but should not require an `HWND` when a returned action or value is sufficient.
3. Make ownership visible. Every handle, secondary window registration, timer, callback target, and worker input must
   have a documented owner and teardown order.
4. Separate snapshots from live subsystems. Paint and overlay code should consume immutable item/browser snapshots
   rather than repeatedly reading mutable cross-thread state.
5. Keep extraction PRs behavior-preserving. Correctness changes should be isolated and tested separately from moves.
6. Prefer narrow result types with native error codes at subsystem boundaries.

## Track 1: Refactor `ImgVwWindow`

### Phase 1: Characterize Dispatch and Behavior

1. Group the existing private fields by responsibility and document their owner and lifetime.
2. Split `HandleMessage()` into private dispatch helpers such as:
   - `HandleCommand(UINT command, UINT notification)`;
   - `HandleTimer(UINT_PTR timer)`;
   - `HandleMouseMessage(UINT message, WPARAM wparam, LPARAM lparam)`.
3. Preserve the top-level message switch as the only `Window::HandleMessage()` adapter.
4. Add characterization coverage for pure calculations and a repeatable manual smoke checklist for behavior that still
   requires a real window.

Acceptance criteria:

- Message and command routing is easier to scan without changing behavior.
- Every existing command and timer has one clearly identified handler.
- Baseline x86 and x64 test results and at least one application build are recorded.

### Phase 2: Extract Pure Presentation Calculations

Add small helpers under `src/ui/win32/`:

- `WindowGeometry` for welcome bounds, rectangle containment, monitor/client calculations, and DPI scaling.
- `EmptyStateLayout` for panel, logo, and button geometry.
- `OverlayText` for byte sizes, percentages, filenames, progress, and loader-stat text.

These helpers should have no live window, browser, renderer, or global state dependencies. Prefer functions operating on
values over classes when no state is required.

Acceptance criteria:

- Geometry and text rules have focused unit tests.
- `ImgVwWindow.cpp` no longer contains formatting or layout algorithms that can be expressed as pure functions.
- Integer arithmetic validates invalid dimensions and avoids overflow where sizes are multiplied.

### Phase 3: Extract `EmptyStateView`

Create an `EmptyStateView` that owns:

- the logo image and backing stream;
- the caption font;
- the open-image, open-folder, search-subfolders, and exit button windows;
- empty-state visibility, message, layout, painting, focus restoration, and owner-draw behavior.

The view should expose intent through command IDs or a narrow callback. It must not open a path, start a browse, stop a
slideshow, or own application navigation policy. `ImgVwWindow` remains responsible for translating user intent into
application actions.

Acceptance criteria:

- All owned controls and GDI/COM resources have deterministic teardown.
- Welcome, error, no-images, and searching states retain their current appearance and keyboard behavior.
- Empty-state layout tests do not require a real top-level window.

### Phase 4: Extract Display and Overlay Presentation

Introduce two focused components:

1. `DisplayPresenter`
   - consumes a display snapshot containing path, item status, and ready render data;
   - decides ready, loading, and error presentation;
   - tracks first paint, requested display path, and last successfully painted path;
   - calls `ImgRenderer` and returns an explicit paint outcome.
2. `InfoOverlay`
   - owns filename, loader-stat, and loading-progress visibility state;
   - owns debounce/timer state, cached text, rectangles, and overlay font;
   - consumes immutable browser/item statistics;
   - lays out and draws overlay content.

Keep timer creation and invalidation calls in the window initially. Components should return actions such as
`ArmTimer`, `CancelTimer`, or `InvalidateRect` rather than silently manipulating the top-level window.

Acceptance criteria:

- Paint decisions can be tested for loading, ready, error, first-paint, and partial-invalid-region cases.
- Overlay text/layout tests do not need active loader threads.
- Painting does not block on worker completion or read an object whose publication state is not stable.
- Renderer and overlay code restore any DC state they modify.

### Phase 5: Extract Window Interaction Helpers

Add focused state holders:

- `CursorController` for capture state, activity tracking, idle timeout, and desired visibility.
- `WindowDragController` for drag start, movement, cancellation, and final placement calculations.
- `MonitorPlacement` for current-monitor detection, monitor bounds, welcome placement, and DPI-change suggestions.

The helpers should return requested operations. `ImgVwWindow` should continue to invoke `SetCapture`, `ReleaseCapture`,
`ShowCursor`, `SetTimer`, `KillTimer`, and `SetWindowPos`, keeping Win32 side effects centralized.

Acceptance criteria:

- Cursor state cannot become unbalanced after activation, slideshow changes, or shutdown.
- Drag calculations are unit tested across negative monitor coordinates and monitor boundaries.
- DPI and monitor transitions preserve the current viewport/update behavior.

### Phase 6: Extract Slideshow Policy

The completed loader/browser shutdown and late-notification contracts permit this phase, but every extraction must
preserve those contracts.

First introduce a Win32-independent `SlideShowStateMachine` responsible for:

- stopped, running, and waiting-for-image states;
- sequential and random modes;
- interval limits and speed changes;
- initial-advance and timer-arm decisions;
- the next navigation action requested by a timer or user input.

Then introduce a `MultiMonitorSlideShowCoordinator` responsible for:

- monitor enumeration input and secondary-window registration;
- shared sequential/random cursor policy;
- choosing the next monitor to advance;
- target-size load-context reuse;
- removing destroyed windows and shutting down all owned registrations.

Secondary `Window` objects may continue to follow the current self-destruction convention, but the coordinator must not
retain an unverified raw pointer. Use explicit registration/unregistration and a stable identifier or validated window
reference. Document the teardown order between owner, secondary windows, browsers, loaders, and notification targets.

Implemented teardown order: the owner first releases all stable secondary-window registrations, then synchronously
destroys each registered `HWND`. Each secondary handles `WM_NCDESTROY` by stopping its browser/load notification target
before self-destruction and attempts an idempotent unregister. Only after every owned secondary has completed teardown
does the owner stop its own browser/load notification target. No coordinator entry retains a raw `ImgVwWindow*`;
registered identifiers are validated and resolved through the live Win32 window immediately before use.

Acceptance criteria:

- Slideshow timer and interval policy is fully unit tested without a window.
- Sequential and random multi-monitor assignment is deterministic under a supplied navigation sequence.
- Closing a secondary window, stopping a slideshow, or closing the owner cannot leave a callable stale registration.
- Existing single- and multi-monitor workflows remain unchanged in manual verification.

### Phase 7: Reduce `ImgVwWindow` to Composition

After the extractions, `ImgVwWindow` should primarily contain:

- top-level and secondary-window creation/destruction;
- Win32 message adaptation and command dispatch;
- composition of views/controllers and platform services;
- browser-notification handling;
- explicit calls crossing UI, browse, image, and platform boundaries.

Line count is not the primary criterion, but a cohesive implementation in the approximate range of 400 to 700 lines is
a reasonable outcome. Avoid replacing the current large class with a single equally broad `Controller` class.

Completion audit: `ImgVwWindow` now composes dedicated browser-window, display-session, empty-state, display,
information-overlay, overlay-visibility, cursor, drag, monitor-placement, single-slideshow, and multi-monitor
slideshow components. Loader-stat snapshot aggregation is also outside the window. Remaining window fields are Win32
runtime state or composed subsystem objects; remaining methods adapt messages, execute Win32 side effects, or make
explicit calls across UI, browse, image, and platform boundaries. The implementation remains longer than the
non-binding line-count guide because those explicit XP-compatible adapter paths were kept visible instead of being
hidden in a broad replacement controller.

## Track 2: Improve the Remaining Code

### Priority 1: Loader and Browser Lifetime Safety

Follow the completed prerequisites in `archive/runtime_safety_and_display_publication_plan.md` and the detailed
history in `archive/imgvw_stability_refactor_plan.md`. These requirements are complete for the current loader and
browser implementation. Preserve them during further work:

- cancellation remains synchronized and independent from state reset/storage destruction;
- worker input outlives workers, including after bounded waits;
- stop outcomes remain explicit and idempotent;
- notification targets are removed before window destruction; and
- browse/load generations keep late completion from updating a newer session.

This priority blocks slideshow ownership extraction and any change that moves browser or loader ownership.

### Priority 2: Win32 RAII

Add small, move-only, XP-compatible wrappers for:

- null-invalid and `INVALID_HANDLE_VALUE` handles;
- find handles;
- GDI objects and selected-object restoration;
- critical sections and scoped locks;
- registry keys and COM resources where ownership is currently manual.

Convert loader and browser thread/event/find handles first, followed by renderer resources, image buffers, and window UI
resources. Capture `GetLastError()` before cleanup can alter it.

Completion status: loader/browser handles and synchronization primitives use the established wrappers. Registry keys,
COM interfaces and task memory, global memory, acquired DCs, selected GDI objects, and remaining UI GDI resources now
have deterministic move-only ownership.

### Priority 3: Split `ImgBrowser` by Responsibility

Extract cohesive collaborators without changing the public browsing workflow:

- `FolderScanner` for directory enumeration, recursion, and format resolution input;
- `BrowseSession` for collection generation, cancellation, readiness, and notification;
- the existing `ImgFileList` for sequential/random navigation;
- `PreloadScheduler` for target-size and explicit-path queueing.

Keep cache and loader sharing explicit in `ImgBrowserLoadContext`. Enumeration threads should not directly own
navigation policy or top-level UI behavior.

Completion status: `FolderScanner`, `BrowseSession`, and `PreloadScheduler` own the responsibilities listed above.
`PreloadScheduler` owns its worker handles and shared cancellation lifetime while browser callbacks retain cache,
loader, generation, and notification behavior.

### Priority 4: Harden Buffers and Decode Boundaries

- Validate positive dimensions, row strides, target dimensions, and supported pixel layouts.
- Use checked multiplication before computing buffer sizes, file sizes, and allocation sizes.
- Reject values that cannot be represented by the Win32 or decoder APIs being called.
- Verify complete reads/writes, not only successful API return values.
- Make temporary-file creation, mapping, and deletion outcomes explicit.
- Keep format detection, decoding, orientation, color transformation, resampling, and presentation conversion as
  separate stages with clear result types.
- Add reasonable decode limits and tests for malformed, truncated, and extreme-dimension input.

### Priority 5: Make Error Boundaries Explicit

Extend the existing result/status approach to:

- loader and browser start/stop;
- directory enumeration;
- ICC profile selection and fallback;
- image buffer and mapping operations;
- settings and temporary-directory initialization.

Use narrow subsystem-specific result types. Preserve native error codes for diagnostics and generate user-facing text at
the UI boundary.

### Priority 6: Enforce Layering and Analysis

- Keep `browse/` and platform-independent image policy free of UI header dependencies.
- Keep window, message, and presentation APIs under `ui/win32/`.
- Keep raw platform calls behind `platform/win32/` where they are not intrinsic to the UI or renderer.
- Address high-confidence lifetime, overflow, narrowing, and unchecked-return findings before readability-only findings.
- Avoid broad cleanup diffs while ownership-sensitive work is in progress.

## Track 3: Divide Tests into Small Units

### Test Support Layout

Replace the single-file harness with reusable first-party support:

```text
tests/
  support/
    TestHarness.h
    TestMain.cpp
    TempFile.h
    TempFile.cpp
    JpegFixture.h
    JpegFixture.cpp
    Win32Fakes.h
    Win32Fakes.cpp
```

Keep the harness lightweight. Prefer explicit suite registration or `RunXxxTests()` functions over complex static
registration. Shared fixtures must use RAII and clean up successfully even after an assertion fails.

### Test Source Layout

```text
tests/
  unit/
    ImgFileListTests.cpp
    ImgCacheTests.cpp
    ImageFormatTests.cpp
    ImgResamplerTests.cpp
    OverlayTextTests.cpp
    WindowGeometryTests.cpp
    SlideShowStateMachineTests.cpp
  platform/
    FileOperationsTests.cpp
    FileMapViewTests.cpp
    ImgBufferTests.cpp
  image/
    ImgJPEGDecoderTests.cpp
    ImgGDIItemTests.cpp
    ColorTransformTests.cpp
  concurrency/
    ImgLoaderTests.cpp
    ImgBrowserTests.cpp
  ui/
    ImgRendererTests.cpp
    DisplayPresenterTests.cpp
```

Adjust exact files as components are introduced; do not create empty placeholder suites.

### Independently Runnable Test Shards

Build focused executables:

- `ImgVwCoreTests` for pure navigation, format, cache, resampling, geometry, text, and state-machine tests;
- `ImgVwPlatformTests` for filesystem, shell, mapping, buffer, and other Win32 platform behavior;
- `ImgVwImageTests` for decoders, orientation, color, and image-item behavior;
- `ImgVwConcurrencyTests` for loader/browser lifecycle and notification behavior;
- `ImgVwUiTests` for renderer and presentation behavior that requires GDI or UI-adjacent fixtures.

`make test` and `scripts/test-msys.ps1` should continue to run every shard. Add focused Makefile targets so developers
can run one shard during iteration. Keep x86 and x64 support.

### Test Case Structure

- Keep each test focused on one observable behavior using arrange, act, and assert sections.
- Move JPEG creation, temporary paths, shell mocks, and GDI setup into reusable fixtures.
- Avoid mutable process-wide fake state where an explicit fixture object or scoped override is practical.
- Include the failing suite and test name in output.
- Allow a single test or suite filter if it can be added without making the harness complex.
- Keep large sample-file and conformance tests separate from fast unit tests when their runtime becomes significant.

### Deterministic Concurrency Seams

Do not base concurrency tests on arbitrary sleeps. Introduce controllable seams such as:

- a load operation blocked and released by events;
- worker-started and cancellation-observed signals;
- an injected notification sink or narrow notification adapter;
- an injected enumeration source for browser collection;
- a configurable bounded-wait adapter or timeout value.

Required scenarios include:

- idle and repeated stop;
- queued work cancellation;
- active worker cancellation;
- browser cancellation during enumeration;
- shutdown timeout without worker-state destruction;
- late notification after generation change or target removal;
- empty, single-item, duplicate-heavy, and removal-during-random-cycle navigation.

## Suggested PR Sequence

1. Split `ImgVwTests.cpp` into support files and subsystem translation units without changing the single test
   executable.
2. Divide the test build into independently runnable shards and update both build systems.
3. Add pure `WindowGeometry`, `EmptyStateLayout`, and `OverlayText` helpers with tests.
4. Split message, command, timer, and mouse dispatch inside `ImgVwWindow`.
5. Complete loader/browser cancellation, lifetime, notification-generation, and RAII prerequisites.
6. Extract `EmptyStateView`.
7. Introduce stable display snapshots, then extract `DisplayPresenter` and `InfoOverlay`.
8. Extract cursor, drag, and monitor-placement helpers.
9. Add and test `SlideShowStateMachine`.
10. Extract multi-monitor slideshow coordination with explicit registration and teardown.
11. Split `ImgBrowser` enumeration/session/preload responsibilities.
12. Harden buffer/decode arithmetic and remaining result boundaries.
13. Perform a final dependency, static-analysis, build, and manual-regression pass.

Keep each extraction and correctness fix in a separate reviewable PR when practical. A PR should not combine ownership
changes with broad renaming or formatting.

## Verification

For routine changes, prefer incremental tests and builds. Use clean builds after project, Makefile, configuration,
toolchain, or dependency changes and for final validation.

Automated verification should include:

- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test-msys.ps1 -Arch x86`
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test-msys.ps1 -Arch x64`
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x86`
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-msys.ps1 -Config release -Arch x64`
- Visual Studio builds through `ImgVw.slnx` where available;
- `scripts/format.ps1 -Check` and focused static analysis where the required toolchain is available.

Manual verification for UI-affecting PRs should cover:

- empty launch, open image, open folder, invalid path, and recursive search;
- keyboard, context-menu, mouse-wheel, drag-and-drop, and deletion commands;
- small, large, rotated, CMYK, HEIF, PNG/GDI-backed, malformed, and slow-loading images;
- resize, minimize/restore, DPI change, negative-coordinate monitors, and theme change;
- sequential/random slideshow, interval changes, loading waits, and stop/restart;
- multi-monitor start, secondary-window destruction, monitor transitions, and owner shutdown;
- navigation and shutdown while collection or decoding is active.

## Completion Criteria

- `ImgVwWindow` is a readable Win32 adapter and composition root rather than the owner of unrelated policy.
- Extracted components each have a single documented responsibility and deterministic resource teardown.
- Pure layout, formatting, interaction, paint-decision, and slideshow policy are covered by focused tests.
- Loader and browser shutdown cannot destroy state reachable by a live worker.
- Late notifications and stale browsing generations are harmless.
- Dimension and buffer arithmetic is validated before allocation, mapping, decoding, or rendering.
- Tests are organized by subsystem and available as independently runnable shards.
- `make test` and repository test scripts run all shards on x86 and x64.
- Visual Studio and Makefile source lists remain synchronized.
- Existing user-visible behavior and Windows XP compatibility are preserved.

## Relationship to Existing Plans

- `archive/runtime_safety_and_display_publication_plan.md` records completed loader/browser shutdown safety,
  cancellation, viewport publication, and high-risk Win32 RAII work.
- `archive/windowing_display_improvement_plan.md` retains the detailed viewport, renderer, and display-state rationale.
- This plan owns the modular decomposition of `ImgVwWindow`, the broader responsibility split, and the test-suite
  organization.
- If plans conflict, preserve the completed lifetime, shutdown, and immutable-publication contracts.
